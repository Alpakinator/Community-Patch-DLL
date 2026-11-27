"""Issue 4105 (epic 0166): the decals in server.exe's bake. The scene's DecalTapReader (pid from the tap block), the
residency source's per-tile decals and re-bakes, the name resolution through farm_types.xml, render_decals, and the
failure route (decision 6, D118, D125): an install failure takes HostConfig::fatal once, an in-play fault never does.

The same rig as test_residency_source.py (render_host_test.exe = the render objects server.exe links, on a hidden
desktop; render_tiles=bake with the stand-in baker). This module is the fake decal tap: it creates the control block
`Local\\VPShellDecalTap.<this pid>` (the tap block of the rig names this process as the EXE) and one image section per
generation in the layout of shell/common/shell_decal_tap.h, as shell_composite.dll does. The pipeline baker with decals
(the pass itself, bit for bit without decals) is tested in render/tests/bake (c5r_bake_test) and render/tests/decal.

Every test sets C5_DOCUMENTS to a folder of its own, so the server's mod folders are the test's (never the user's).
Synthetic map: 6 x 6 plots = 384 x 384 height samples, all land; tile (x, y) = origin (64 x, 64 y).
"""
import ctypes
import os
import re
import struct
import time
from pathlib import Path

import pytest

import camblock
import renderfx as fx

MAGIC = 0x54443543           # 'C5DT'
READY, FAILED, OFF = 2, 3, 4
FAIL_PATTERN, FAIL_LAYOUT = 1, 3
OVERLAY_OFFSET = 3904       # VPSHELL_OVERLAYTAP_OFFSET (shell_shared.h): seq, state, failReason, exePid
WORLD = (0.0, 0.0, 384.0, 384.0)


def _align(n):
    return (n + 15) & ~15


def image(generation, decals, names, changed, load_seq=1, world=WORLD, uv=False):
    """A VPShellDecalImage: decals = [(rect, priority, pass, (t0, t1, t2), triangles)], vertices made per decal; uv:
    each vertex's texture coordinates equal its position (0642's frames), else 0."""
    hdr = 112
    nd, nn = len(decals), len(names)
    verts = b""
    recs = b""
    for i, (rect, prio, pas, tex, tris) in enumerate(decals):
        first = len(verts) // 8
        for k in range(3 * tris):
            px, py = 0x3C00 * (k & 1), 0x3C00 * ((k >> 1) & 1)
            verts += struct.pack("<HHHH", px, py, px if uv else 0, py if uv else 0)
        recs += struct.pack("<4ffI3IIII", *rect, prio, pas, *tex, first, tris, i)
    nvert = len(verts) // 8
    o_dec = _align(hdr)
    o_name = _align(o_dec + len(recs))
    namesb = b"".join(struct.pack("<II", 100 + i, 0) + n.encode("utf-8").ljust(192, b"\0") for i, n in enumerate(names))
    o_vert = _align(o_name + len(namesb))
    o_chg = _align(o_vert + len(verts))
    chg = b"".join(struct.pack("<4f", *r) for r in changed)
    total = _align(o_chg + len(chg))
    b = bytearray(total)
    struct.pack_into("<14I4fQII", b, 0, MAGIC, 1, hdr, 48, generation, load_seq, nd, nn, nvert, len(changed),
                     o_dec if nd else 0, o_name if nn else 0, o_vert if nvert else 0, o_chg if changed else 0, *world,
                     total, 0, 0)
    b[o_dec:o_dec + len(recs)] = recs
    b[o_name:o_name + len(namesb)] = namesb
    b[o_vert:o_vert + len(verts)] = verts
    b[o_chg:o_chg + len(chg)] = chg
    return bytes(b)


class FakeDecalTap:
    """The producer half of the decal tap (shell_composite.dll's side), for this process as the EXE."""

    def __init__(self):
        self.pid = os.getpid()
        self.ctl = fx.TapSection("Local\\VPShellDecalTap.%d" % self.pid, b"\0" * 4096, 4096)
        self.images = []
        self.seq = 0

    def block(self, state, generation=0, fail=0, img=None, fnv=None, load_seq=1):
        self.seq += 1
        ctypes.memmove(self.ctl.p, struct.pack("<I", self.seq | 1), 4)
        name = "Local\\VPShellDecal.%d.%d" % (self.pid, generation)
        nbytes = len(img) if img else 0
        ndec = struct.unpack_from("<I", img, 24)[0] if img else 0
        h = (fx.fnv1a64(img) if fnv is None else fnv) if img else 0
        body = struct.pack("<IIIIIIIQQqff", MAGIC, state, fail, self.pid, generation, load_seq, ndec, nbytes, h,
                           fx.qpc(), 1.0, 0.01)
        ctypes.memmove(self.ctl.p + 4, body, len(body))
        ctypes.memmove(self.ctl.p + 80, (name + "\0").encode("utf-16-le").ljust(128, b"\0"), 128)
        self.seq = (self.seq | 1) + 1
        ctypes.memmove(self.ctl.p, struct.pack("<I", self.seq), 4)

    def publish(self, generation, img, fnv=None):
        sec = fx.TapSection("Local\\VPShellDecal.%d.%d" % (self.pid, generation), img, (len(img) + 0xFFFF) & ~0xFFFF)
        self.images.append(sec)
        self.block(READY, generation, img=img, fnv=fnv)
        return fx.qpc()

    def ack(self):
        g, r, pid = struct.unpack("<IiI", ctypes.string_at(self.ctl.p + 208, 12))
        return g, r, pid

    def wait_ack(self, generation, timeout=10.0):
        t = time.time() + timeout
        while time.time() < t:
            g, r, _ = self.ack()
            if g == generation:
                return r
            time.sleep(0.02)
        raise TimeoutError("no ack of generation %d (ack %r)" % (generation, self.ack()))

    def close(self):
        for s in self.images:
            s.close()
        self.ctl.close()


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request, monkeypatch):
    docs = tmp_path / "docs"
    (docs / "My Games" / "Sid Meier's Civilization 5" / "MODS").mkdir(parents=True)
    monkeypatch.setenv("C5_DOCUMENTS", str(docs))
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    r.docs = docs
    yield r
    r.close()


@pytest.fixture
def tap():
    t = FakeDecalTap()
    yield t
    t.close()


class Cameras:
    """Camera blocks at 40 Hz from a thread (the bake-ahead and re-bakes run after each publish)."""

    def __init__(self, rig):
        import threading
        self.rig, self.stop, self.f = rig, False, 1
        self.t = threading.Thread(target=self.run, daemon=True)
        self.t.start()

    def run(self):
        while not self.stop:
            self.f += 1
            self.rig.sec.write_camera(fx.synth_camera(frame=self.f, pan=float(self.f % 7)))
            time.sleep(0.025)

    def close(self):
        self.stop = True
        self.t.join(2)


def start(rig, fatal=True, **keys):
    # render_fow=0, render_overlay=0: the fog and the overlay cannot install in this rig (no install, no overlay tap), and
    # with the fatal hook their routes would be taken too. Likewise render_ice=0 and render_shadow=0 (epic 0169, issue 4310):
    # the ice module cannot start without an install (an install failure, fatal), and the shadow tap's reader would be
    # Absent past its 20 s bound in this rig (no shadow section)
    cfg = dict(render_tiles="bake", render_baker="standin", render_decals=1, render_test_decal_trace=1000, render_fow=0,
               render_overlay=0, render_ice=0, render_shadow=0)
    cfg.update(keys)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    # ticket 0496: the run ends through --stop-event when the test has seen what it checks (rig.stop); it was a fixed
    # --seconds (4 to 13), which on a loaded machine or under ASan ended before 0365's 2 s decal hold and the bake-ahead
    return rig.start_until(extra=["--fatal-hook", "1"] if fatal else None, **cfg)


def fatal_or_set(rig, p, generation=1):
    """Ticket 0496: waits until the set of generation is either in use (render.log) or refused through the fatal hook
    (stdout), then for one more 'host 1s:' line (a second fatal call would be seen); was a fixed 1.5 s sleep."""
    rig._wait_text(lambda: rig.stdout() + rig.text(), "stdout or render.log",
                   r"result: fatal hook code|decals: set generation %d \(load 1\) in use" % generation, fx.WAIT_S, p)
    rig.wait_next_second(p)


def decal_totals(text):
    m = re.search(r"decal totals: (.*?)(;|$)", text, re.M)
    assert m, text[-2500:]
    return {k.strip(): int(v) for k, v in re.findall(r"([a-z -]+?) (-?\d+)", m.group(1))}


def res_totals(text):
    m = re.search(r"residency totals: (.*?)(;|$)", text, re.M)
    assert m, text[-2500:]
    return {k.strip(): int(v) for k, v in re.findall(r"([a-z ]+?) (-?\d+)", m.group(1))}


def decal_loud(text):
    """The LOUD lines of the decals (the rig's own, e.g. no install for the fog ramp, are not this module's)."""
    return [ln for ln in text.splitlines() if "LOUD: decal" in ln]


def traced_bakes(text):
    """(tile x, tile y, decals, set generation, qpc ms) of every traced bake with decals."""
    return [(int(a), int(b), int(n), int(g), float(q)) for a, b, n, g, q in
            re.findall(r"decals: bake \d+ tile \((\d+), (\d+)\) gen \d+ with (\d+) decals of set generation (\d+), areaChanged 1, "
                       r"t8 \d, at qpc ([\d.]+) ms", text)]


def fatal_calls(stdout):
    m = re.search(r"result: fatal hook calls (\d+)", stdout)
    assert m, stdout[-2000:]
    return int(m.group(1))


DEC_A = ((70.0, 70.0, 120.0, 110.0), 0.5, 0, (0, 1, 0xFFFFFFFF), 2)    # tile (1, 1) only (query square -2..66)
DEC_B = ((200.0, 200.0, 240.0, 240.0), 0.7, 0, (0, 1, 0xFFFFFFFF), 2)  # tile (3, 3) only
NAMES = ["RoadsandRails_new_D.dds", "RoadsandRails_new_H.dds"]


def test_decal_generations_rebake_exactly_their_tiles_after_the_set(rig, tap):
    """The field first, then generation 1 (decal A over tile (1, 1)) and generation 2 (decal B added over tile (3, 3),
    its rectangle in the changed block): each generation raises exactly the tiles of its rectangles, each re-bake carries
    that generation's set (never an older one after the raise: P13), every tile with decals is baked with areaChanged,
    and the residency re-bakes exactly 2 tiles. The stand-in ignores the decals; the trace shows what each bake carried."""
    p = start(rig)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete")
        q1 = tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
        assert tap.wait_ack(1) == 0
        rig.wait_log(r"decals: generation 1 \(after 0\): 1 changed rectangles raised 1 tiles")
        rig.wait_log(r"decals: bake \d+ tile \(1, 1\)")
        q2 = tap.publish(2, image(2, [DEC_A, DEC_B], NAMES, [DEC_B[0]]))
        assert tap.wait_ack(2) == 0
        rig.wait_log(r"decals: generation 2 \(after 1\): 1 changed rectangles raised 1 tiles")
        rig.wait_log(r"decals: bake \d+ tile \(3, 3\) gen \d+ with 1 decals of set generation 2")
        # one line per generation when its last raised land tile is re-baked (the D92 latency from the publication)
        m = rig.wait_log(r"decals: generation 2: its 1 land tiles re-baked, the last ([\d.]+) ms after the publication")
        assert 0 < float(m.group(1)) < 5000, m.group(0)
        rig.wait_log(r"decals: generation 1: its 1 land tiles re-baked")
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    bakes = traced_bakes(t)
    assert [(x, y, n, g) for x, y, n, g, _ in bakes] == [(1, 1, 1, 1), (3, 3, 1, 2)], bakes
    # each re-bake is later than the publication of its generation (QPC is machine-wide; the trace logs ms)
    ms = 1000.0 / fx.qpc_freq()
    assert bakes[0][4] > q1 * ms and bakes[1][4] > q2 * ms, (bakes, q1 * ms, q2 * ms)
    rt = res_totals(t)
    assert rt["bakes"] == 36 + rt["rebakes"] and rt["failed"] == 0, rt
    assert rt["rebakes"] == 2, rt   # exactly tile (1, 1) for generation 1 and tile (3, 3) for generation 2
    dt = decal_totals(t)
    assert dt["generation"] == 2 and dt["sets"] == 2 and dt["refused"] == 0 and dt["raises"] == 2 and dt["install failure"] == 0, dt
    assert decal_loud(t) == [], decal_loud(t)
    assert fatal_calls(rig.stdout()) == 0


def test_set_before_the_field_bakes_once_with_decals(rig, tap):
    """Generation 1 published before the field (the tap publishes during the load): the bake-ahead's first bake of
    tile (1, 1) already carries the decal and areaChanged; the change of generation 1 waits for the field and is then
    skipped (every bake of the new field drew it), so nothing is baked twice."""
    tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
    rig.sec.write_camera(fx.synth_camera(frame=1))
    # the overlay tap block names the EXE's pid before the field tap does (the composite writes it from its first Present)
    rig.sec.write(OVERLAY_OFFSET, struct.pack("<IIII", 2, 0, 0, os.getpid()))
    p = rig.start_until(extra=["--fatal-hook", "1"], render_tiles="bake", render_baker="standin", render_decals=1,
                        render_fow=0, render_overlay=0, render_ice=0, render_shadow=0,   # as start() (issue 4310)
                        render_test_decal_trace=1000)   # ticket 0496: was a fixed 5 s
    rig.wait_log(r"decals: set generation 1 \(load 1\) in use")   # applied; no field yet
    assert "terrain: field" not in rig.text()
    rig.tap(load_seq=1)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        time.sleep(1.0)
        rig.wait_next_residency(p)
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert [(x, y, g) for x, y, n, g, _ in traced_bakes(t)] == [(1, 1, 1)], traced_bakes(t)
    rt = res_totals(t)
    assert rt["bakes"] == 36 and rt["rebakes"] == 0, rt
    assert re.search(r"decal_changes_skipped=1", t), t[-2000:]
    assert "bake-ahead of load 1 waits" not in t, t[-2000:]   # 0365: a set of the load is in force: no wait
    assert fatal_calls(rig.stdout()) == 0


def _start_without_set(rig, camera=True):
    """The rig of test_set_before_the_field_bakes_once_with_decals without the set: the overlay tap block names this
    process as the EXE (the reader is made), one camera block (camera=False: none, so nothing is drawn and no visible tile
    is baked until the test writes one), no field yet."""
    if camera:
        rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.sec.write(OVERLAY_OFFSET, struct.pack("<IIII", 2, 0, 0, os.getpid()))
    return rig.start_until(extra=["--fatal-hook", "1"], render_tiles="bake", render_baker="standin", render_decals=1,
                           render_fow=0, render_overlay=0, render_ice=0, render_shadow=0,
                           render_test_decal_trace=1000)   # ticket 0496: was a fixed 8 s


def test_set_after_the_field_within_the_wait_bakes_once(rig, tap):
    """0365 (terrain-decals.md 19.3): the field first, no set yet: the bake-ahead waits for the load's first set; generation
    1, published 0.3 s after the field, ends the wait, and every tile is baked once, tile (1, 1) with the decal: its change
    is skipped (nothing was resident when it arrived), nothing is baked twice. No camera before the set: the visible tiles
    (which the wait does not hold, so a player never sees a gray frame for it) would be baked at once and, if the set
    names them, again."""
    p = _start_without_set(rig, camera=False)
    rig.wait_log(r"decals: control section .* opened")
    rig.tap(load_seq=1)
    rig.wait_log(r"residency: bake-ahead of load 1 waits up to 2000 ms for the load's first decal set \(no set yet\)")
    time.sleep(0.3)
    tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
    assert tap.wait_ack(1) == 0
    m = rig.wait_log(r"residency: bake-ahead of load 1 resumed after ([\d.]+) ms: decal set generation 1 \(load 1\) arrived")
    assert 200 < float(m.group(1)) < 2000, m.group(0)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        time.sleep(1.0)
        rig.wait_next_residency(p)
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert [(x, y, g) for x, y, n, g, _ in traced_bakes(t)] == [(1, 1, 1)], traced_bakes(t)
    rt = res_totals(t)
    assert rt["bakes"] == 36 and rt["rebakes"] == 0, rt
    assert re.search(r"decal_changes_skipped=1", t), t[-2000:]
    assert fatal_calls(rig.stdout()) == 0


def test_set_after_the_wait_bound_rebakes_its_tiles(rig, tap):
    """0365, the negative of the wait: no set within its 2,000 ms: the bake-ahead goes on at the bound (a set later than
    the bound costs what it cost before 0365); generation 1 published after the bake-ahead raises tile (1, 1), which had
    a bake, and re-bakes it once with the decal; the re-bake line names that one tile."""
    p = _start_without_set(rig)
    rig.wait_log(r"decals: control section .* opened")
    rig.tap(load_seq=1)
    m = rig.wait_log(r"residency: bake-ahead of load 1 resumed after ([\d.]+) ms: the bound passed without a set")
    assert 1990 < float(m.group(1)) < 4000, m.group(0)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete")
        tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
        assert tap.wait_ack(1) == 0
        rig.wait_log(r"decals: generation 1: its 1 re-baked land tiles \(a bake before the raise\) done, the last [\d.]+ ms after")
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert [(x, y, g) for x, y, n, g, _ in traced_bakes(t)] == [(1, 1, 1)], traced_bakes(t)
    rt = res_totals(t)
    assert rt["bakes"] == 37 and rt["rebakes"] == 1, rt
    assert fatal_calls(rig.stdout()) == 0


def test_refused_image_keeps_the_last_good_set_and_counts(rig, tap):
    """In play: generation 2 with a wrong hash, then generation 3 with a record past its vertices: each refused (acked
    with its code), LOUD, counted; the set of generation 1 stays in force and nothing is re-baked for them; generation 4
    (good) is applied. Never the fatal route."""
    p = start(rig)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete")
        tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
        assert tap.wait_ack(1) == 0
        rig.wait_log(r"decals: bake \d+ tile \(1, 1\)")
        tap.publish(2, image(2, [DEC_A, DEC_B], NAMES, [DEC_B[0]]), fnv=12345)
        assert tap.wait_ack(2) == -1                                    # hash
        bad = image(3, [DEC_A, (DEC_B[0], 0.7, 0, (0, 1, 0xFFFFFFFF), 2)], NAMES, [DEC_B[0]])
        bad = bytearray(bad)
        struct.pack_into("<I", bad, 112 + 48 + 36, 999)                  # decal 1's firstVertex past the block
        tap.publish(3, bytes(bad))
        assert tap.wait_ack(3) == 5                                      # BadLayout
        rig.wait_log(r"LOUD: decals: in play: an image was refused .* \(2 refused so far\)")
        tap.publish(4, image(4, [DEC_A, DEC_B], NAMES, [DEC_B[0]]))
        assert tap.wait_ack(4) == 0
        rig.wait_log(r"decals: bake \d+ tile \(3, 3\) gen \d+ with 1 decals of set generation 4")
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    dt = decal_totals(t)
    assert dt["refused"] == 2 and dt["in-play faults"] == 2 and dt["generation"] == 4 and dt["install failure"] == 0, dt
    # between generation 1 and 4 no bake carried another set
    assert all(g in (1, 4) for _, _, _, g, _ in traced_bakes(t)), traced_bakes(t)
    assert fatal_calls(rig.stdout()) == 0
    s, line = fx.summary(rig.stdout())
    assert s["state"] == "STOPPED", line


def test_tap_failed_layout_in_play_is_loud_not_fatal(rig, tap):
    """In play: the tap goes FAILED LAYOUT after generation 1 (it retries): LOUD once, counted, the set stays."""
    p = start(rig)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete")
        tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
        assert tap.wait_ack(1) == 0
        tap.block(FAILED, 1, fail=FAIL_LAYOUT)
        rig.wait_log(r"LOUD: decals: in play: the tap says FAILED LAYOUT")
    finally:
        cams.close()
    assert rig.stop(p) == 0
    dt = decal_totals(rig.text())
    assert dt["in-play faults"] == 1 and dt["generation"] == 1 and dt["install failure"] == 0, dt
    assert fatal_calls(rig.stdout()) == 0


def test_option_off_reads_nothing(rig, tap):
    """render_decals=0: no reader, no decal pass, no decal totals, nothing acked, no fatal route even without decals."""
    tap.publish(1, image(1, [DEC_A], NAMES, [DEC_A[0]]))
    p = start(rig, render_decals=0, render_test_decal_wait_ms=500)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        time.sleep(1.5)
        rig.wait_next_residency(p)
    finally:
        cams.close()
    assert rig.stop(p) == 0
    t = rig.text()
    assert "residency: render_decals=0: no decals" in t, t[:3000]
    assert "decals: reader" not in t and "decal totals" not in t and "decals: bake" not in t, t[-2000:]
    assert tap.ack()[0] == 0
    assert res_totals(t)["rebakes"] == 0
    assert fatal_calls(rig.stdout()) == 0


def test_option_refused(rig):
    """Negative: render_decals other than 0 / 1, a wait outside 100..600000: the config is refused (exit 2)."""
    for k, v in (("render_decals", "2"), ("render_test_decal_wait_ms", "50"), ("render_test_decal_trace", "-1")):
        p = start(rig, **{k: v})
        assert rig.finish(p) == 2, rig.stdout()
        assert "config refused" in rig.stdout() and k in rig.stdout(), rig.stdout()


@pytest.mark.parametrize("case", ["no_tap", "pattern", "seh"])
def test_install_failure_is_fatal_once(rig, tap, case):
    """Install: no decal set within the bound after the field and a valid camera grid (no control block at all: a
    composite without the tap), or the control block FAILED PATTERN / SEH: LOUD once, the fatal hook exactly once with
    the reason, the process goes on (the test hook returns)."""
    if case == "no_tap":
        tap.close()
        tap.ctl = fx.TapSection("Local\\VPRenderTestNoDecal_%d" % os.getpid(), b"\0" * 64, 64)
        tap.images = []
    elif case == "pattern":
        tap.block(FAILED, 0, fail=FAIL_PATTERN)
    else:
        tap.block(FAILED, 0, fail=2)
    p = start(rig, render_test_decal_wait_ms=1500)
    cams = Cameras(rig)
    try:
        rig.wait_stdout(r"result: fatal hook code", p=p)   # ticket 0496: was a 3.5 s sleep in a fixed 4 s run
        rig.wait_next_second(p)
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert fatal_calls(out) == 1, out[-3000:]
    m = re.search(r"result: fatal hook code (\d+): (.*)", out)
    assert m and int(m.group(1)) == 5, out[-2000:]
    want = {"no_tap": "no decal set within 1500 ms after the load", "pattern": "FAILED PATTERN", "seh": "FAILED SEH"}[case]
    assert want in m.group(2) and "D125" in m.group(2), m.group(2)
    t = rig.text()
    assert len(re.findall(r"LOUD: decals cannot install", t)) == 1, t[-3000:]
    assert decal_totals(t)["install failure"] == 1


def test_install_wait_counts_from_the_exes_first_map_scene(rig, tap):
    """Ticket 0491 (fog-of-war.md 7.6): with an overlay source (render_overlay=1) the decal wait counts from the EXE's first
    map scene of the load (the overlay tap's first copy after the field), since on a huge late load the EXE is still
    loading for 12-14 s after the field and its decal tap publishes the first set 22.7 s after it (9074's T1100, run d2).
    Here the tap block stays NONE (the EXE draws no map scene): past the 1,500-ms wait there is one note and no failure;
    the slow-load bound (6 x the wait, 9,000 ms from the field) ends it, fatal once, the text naming the missing scene."""
    tap.close()
    tap.ctl = fx.TapSection("Local\\VPRenderTestNoDecal_%d" % os.getpid(), b"\0" * 64, 64)
    tap.images = []
    rig.sec.write(OVERLAY_OFFSET, struct.pack("<IIII", 2, 0, 0, os.getpid()))   # the overlay tap block: NONE, no pair
    t0 = time.monotonic()
    p = start(rig, render_test_decal_wait_ms=1500, render_overlay=1)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"decals: no decal set \d+ ms after the field, and the EXE has drawn no map scene of the load yet \(its "
                     r"load goes on\): the 1500 ms count from that scene, at most 9000 ms from the field \(decalWaitMs x 6; "
                     r"ticket 0491\)")
        assert "decals cannot install" not in rig.text(), rig.text()[-2000:]
        m = rig.wait_log(r"LOUD: decals cannot install: no decal set within 1500 ms after the load .*?; (\d+) ms after the "
                         r"field, the EXE drew no map scene of the load; the slow-load bound 9000 ms, ticket 0491\)")
        assert 9000 <= int(m.group(1)) < 11000, m.group(0)
        assert time.monotonic() - t0 >= 9.0
    finally:
        cams.close()
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert fatal_calls(out) == 1, out[-3000:]
    t = rig.text()
    assert len(re.findall(r"LOUD: decals cannot install", t)) == 1, t[-3000:]
    assert len(re.findall(r"the EXE has drawn no map scene of the load yet", t)) == 1, t[-3000:]


def test_tap_off_is_loud_once_and_not_fatal(rig, tap):
    """The composite's decal_tap=0 (state OFF, a test switch): LOUD once, no decals, the bound does not apply."""
    tap.block(OFF, 0)
    p = start(rig, render_test_decal_wait_ms=1000)
    cams = Cameras(rig)
    try:
        # ticket 0496: was a 3 s sleep in a fixed 4 s run
        rig.wait_log(r"LOUD: decals: the tap is OFF", p=p)
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        time.sleep(1.5)
        rig.wait_next_second(p)
    finally:
        cams.close()
    assert rig.stop(p) == 0
    t = rig.text()
    assert len(re.findall(r"LOUD: decals: the tap is OFF", t)) == 1, t[-2000:]
    assert fatal_calls(rig.stdout()) == 0


def _install(tmp, files):
    inst = tmp / "install"
    (inst / "Resource").mkdir(parents=True, exist_ok=True)
    (inst / "Assets").mkdir(parents=True, exist_ok=True)
    for rel, data in files.items():
        f = inst / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(data)
    return inst


FARM_XML = b"""<?xml version="1.0" encoding="utf-8"?>
<!-- a comment -->
<FarmTypes>
  <Crops_Half_Built>
    <Crop_01 albedo="assets/Decals/Crops/Crops_Half_Built_01_D.dds" heightmap="assets/Decals/Crops/Crops_Generic_01_H.dds" priority="0.5"/>
  </Crops_Half_Built>
  <Crops_Africa>
    <Crop_01 albedo="assets/Decals/Crops/Crops_Africa_01_D.dds" priority="0.5"/>
    <Crop_02 albedo="crops_maize_d.dds" heightmap='assets/Decals/Crops/Crops_Generic_02_H.dds' priority="0.5"></Crop_02>
  </Crops_Africa>
</FarmTypes>
"""


def test_farm_references_resolve_through_the_mods_farm_types(rig, tap):
    """Names: '@farm_types.xml/Crops_Africa/1/albedo' resolves through the farm_types.xml of the mods' folder (above
    the install's own, which names another file) to crops_maize_d.dds, found in the mod folder; a plain name is found in
    the install's Assets. The set is accepted, the tile baked with it, no fatal."""
    inst = _install(rig.tmp, {"Assets/Decals/RoadsandRails_new_D.dds": b"x", "Assets/Decals/RoadsandRails_new_H.dds": b"x",
                              "Assets/Gameplay/farm_types.xml": FARM_XML.replace(b"crops_maize_d.dds", b"stock_only.dds")})
    mod = rig.docs / "My Games" / "Sid Meier's Civilization 5" / "MODS" / "(2) Test Mod"
    (mod / "Core").mkdir(parents=True)
    (mod / "Core" / "farm_types.xml").write_bytes(FARM_XML)
    (mod / "Art").mkdir()
    (mod / "Art" / "crops_maize_d.dds").write_bytes(b"x")
    (mod / "Art" / "Crops_Generic_02_H.dds").write_bytes(b"x")
    names = ["RoadsandRails_new_D.dds", "@farm_types.xml/Crops_Africa/1/albedo", "@farm_types.xml/crops_africa/1/HEIGHTMAP"]
    dec = (DEC_A[0], 0.5, 0, (1, 2, 0xFFFFFFFF), 2)
    p = start(rig, render_bake_install=str(inst))
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete")
        tap.publish(1, image(1, [dec], names, [dec[0]]))
        assert tap.wait_ack(1) == 0
        rig.wait_log(r"decals: set generation 1 \(load 1\) in use: 1 decals, 3 names \(2 farm references resolved\)")
        rig.wait_log(r"decals: bake \d+ tile \(1, 1\)")
    finally:
        cams.close()
    assert rig.stop(p) == 0
    t = rig.text()
    assert re.search(r"decals: farm_types.xml from .*\(2\) Test Mod.*: \d+ attributes", t), t[-3000:]
    assert fatal_calls(rig.stdout()) == 0 and decal_loud(t) == [], decal_loud(t)


@pytest.mark.parametrize("case", ["missing_file", "no_attribute", "no_farm_types"])
def test_unresolvable_name_is_an_install_failure(rig, tap, case):
    """A texture the install and the mods' folders lack, a reference to an attribute farm_types.xml does not have, or no
    farm_types.xml at all: the set is refused by the residency (the previous one stays), LOUD once, fatal once."""
    files = {"Assets/Decals/RoadsandRails_new_D.dds": b"x"}
    if case != "no_farm_types":
        files["Assets/Gameplay/farm_types.xml"] = FARM_XML
        files["Assets/Decals/Crops_Africa_01_D.dds"] = b"x"
    inst = _install(rig.tmp, files)
    names = {"missing_file": ["RoadsandRails_new_D.dds", "not_there_d.dds"],
             "no_attribute": ["RoadsandRails_new_D.dds", "@farm_types.xml/Crops_Africa/0/heightmap"],
             "no_farm_types": ["RoadsandRails_new_D.dds", "@farm_types.xml/Crops_Africa/0/albedo"]}[case]
    p = start(rig, render_bake_install=str(inst))
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        dec = (DEC_A[0], 0.5, 0, (0, 1, 0xFFFFFFFF), 2)
        tap.publish(1, image(1, [dec], names, [dec[0]]))
        assert tap.wait_ack(1) == 0                     # the reader accepts the image; the names fail in the residency
        fatal_or_set(rig, p)                            # ticket 0496: was a 1.5 s sleep in a fixed 4 s run
    finally:
        cams.close()
    assert rig.stop(p) == 0
    out, t = rig.stdout(), rig.text()
    assert fatal_calls(out) == 1, out[-2000:]
    why = re.search(r"result: fatal hook code 5: (.*)", out).group(1)
    want = {"missing_file": "texture 'not_there_d.dds' is not in the install or the mods' folders",
            "no_attribute": "names no attribute of the farm_types.xml",
            "no_farm_types": "farm_types.xml cannot be read"}[case]
    assert want in why, why
    assert traced_bakes(t) == [], traced_bakes(t)        # no bake ever carried the refused set
    assert decal_totals(t)["sets refused by names"] == 1


def test_decal_trace_and_loud_are_quiet_without_decals(rig, tap):
    """A set with no decal (an empty map): accepted, nothing re-baked, no bake carries decals, no LOUD line."""
    p = start(rig)
    cams = Cameras(rig)
    try:
        rig.wait_log(r"residency: bake-ahead complete", p=p)
        tap.publish(1, image(1, [], [], []))
        assert tap.wait_ack(1) == 0
        time.sleep(0.8)
        rig.wait_next_residency(p)   # ticket 0496: the run was a fixed 4 s
    finally:
        cams.close()
    assert rig.stop(p) == 0
    t = rig.text()
    assert traced_bakes(t) == [] and res_totals(t)["rebakes"] == 0, t[-2000:]
    assert decal_loud(t) == [], decal_loud(t)


# ---------------------------------------------------------------------------------------------------------------
# Ticket 0642: the decal textures read off the render thread (ResidencySourceConfig::decalLoad, C5_RENDER_DECAL_LOADER).
# The pipeline baker on the player's install (C5R_GAME_DIR, read-only) and 3401's Tiny tap (C5R_TINY_TAP, heights and
# material layer of a live load), one decal over the whole map with two of the install's road textures. Each mode's
# frame after the bake-ahead is dumped at the same camera: modes 0 (the bakes read), 1 (wait) and 2 (hold, the default)
# give the same bytes, and render_decals=0 another picture (the decal is in the frame).

def _release_setting(env, key):
    import importlib.util
    if env in os.environ:
        return Path(os.environ[env])
    spec = importlib.util.spec_from_file_location("c5_release_settings",
                                                  Path(__file__).resolve().parents[3] / "tools" / "release" / "settings.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    v = mod.get(key)
    return Path(v) if v is not None else Path("<%s not set: setting %s of tools/release/settings.py>" % (env, key))


GAME_DIR_0642 = _release_setting("C5R_GAME_DIR", "render_game_dir")
TINY_TAP_0642 = Path(os.environ.get("C5R_TINY_TAP", "D:/c5-64bit-work/3400/ref/C5H-3401-TINY_0008/armA/heightfield.c5hf"))


def _loader_run(tmp, hidden_desktop, built, exe, mode, decals=1):
    """One server run of the 0642 arm: returns (sha256 of the dumped frame, render.log text)."""
    import hashlib
    sub = tmp / ("m%s_d%d" % (mode, decals))
    sub.mkdir()
    docs = sub / "docs"
    (docs / "My Games" / "Sid Meier's Civilization 5" / "MODS").mkdir(parents=True)
    old = {k: os.environ.get(k) for k in ("C5_DOCUMENTS", "C5_RENDER_DECAL_LOADER")}
    os.environ["C5_DOCUMENTS"] = str(docs)
    os.environ["C5_RENDER_DECAL_LOADER"] = str(mode)
    r = fx.Rig(sub, hidden_desktop, built, exe, "dl0642_%s_%d" % (mode, decals))
    t = FakeDecalTap()
    try:
        import numpy as np
        payload = TINY_TAP_0642.read_bytes()
        w, h, X, Y = (int(x) for x in np.frombuffer(payload[8:24], dtype="<u4"))
        sx, sy, sz, ox, oy, water_z = struct.unpack_from("<6f", payload, 28)
        r.tap(load_seq=1, payload=payload, map_w=w, map_h=h)
        # a land spot near the map's centre whose 5 x 5 samples around are all land: the camera looks at it, one decal
        # of 4 x 4 tiles around it (the road textures stretched over it, uv 0..1)
        z = np.frombuffer(payload[128:128 + X * Y * 2], dtype="<u2").reshape(Y, X).astype(np.float64) * sz / 255.0
        land = z > water_z + 0.5
        ys, xs = np.nonzero(land[2:-2:16, 2:-2:16])
        ys, xs = ys * 16 + 2, xs * 16 + 2
        k = int(np.argmin((xs - X / 2) ** 2 + (ys - Y / 2) ** 2))
        cx, cy = int(xs[k]), int(ys[k])
        world = (0.0, 0.0, float(X), float(Y))
        rect = (float(max(cx - 128, 0)), float(max(cy - 128, 0)), float(min(cx + 128, X - 1)), float(min(cy + 128, Y - 1)))
        dec = (rect, 0.5, 0, (0, 1, 0xFFFFFFFF), 2)
        wx, wy, wz = cx * sx - ox, cy * sy - oy, float(z[cy, cx])
        view = fx.look_at_rh((wx, wy - 120, wz + 160), (wx, wy, wz), (0, 0, 1))

        def camera(f):
            c = fx.synth_camera(frame=f, samples=(X, Y))
            c.update({"view": list(view.ravel()) + [0.0] * 48, "camWorld": list(np.linalg.inv(view).ravel()), "ox": ox,
                      "oy": oy, "sx": sx, "sy": sy, "heightScale": sz})
            return c

        dump = sub / "frame.bin"
        secs = 240 if os.environ.get("C5_ASAN_LOG_DIR") else 120
        p = r.start_until(bound=secs, game_dir=GAME_DIR_0642, render_tiles="bake", render_decals=decals, render_fow=0,
                          render_overlay=0, render_ice=0, render_shadow=0, render_test_dump=str(dump),
                          render_test_dump_after=20)
        if decals:
            t.publish(1, image(1, [dec], NAMES, [rect], world=world, uv=True))
            assert t.wait_ack(1, timeout=60) == 0
            r.wait_log(r"decals: set generation 1 \(load 1\) in use", timeout=secs - 5, p=p)
        r.wait_log(r"residency: bake-ahead complete", timeout=secs - 5, p=p)
        r.wait_residency(fx.ahead_done, timeout=secs - 5, p=p)
        for f in range(1, 41):   # the same camera in every arm; the dump is the 20th published frame
            r.sec.write_camera(camera(f))
            time.sleep(0.025)
        r.wait_log(r"test: dump of frame \d+ to .* written", timeout=60, p=p)
        assert r.stop(p) == 0, r.stdout()[-2000:]
        return hashlib.sha256(dump.read_bytes()).hexdigest(), r.text()
    finally:
        t.close()
        r.close()
        for k, v in old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def test_decal_loader_modes_draw_the_same_frame(tmp_path, hidden_desktop, built, exe):
    if not GAME_DIR_0642.joinpath("Assets").exists():
        pytest.fail("C5R_GAME_DIR %s has no Assets/ (the install, read-only)" % GAME_DIR_0642)
    if not TINY_TAP_0642.exists():
        pytest.fail("C5R_TINY_TAP %s missing (3401's Tiny tap with the material layer)" % TINY_TAP_0642)
    runs = {m: _loader_run(tmp_path, hidden_desktop, built, exe, m) for m in (0, 1, 2)}
    plain, _ = _loader_run(tmp_path, hidden_desktop, built, exe, 2, decals=0)
    for m, (hsh, text) in runs.items():
        print("0642 mode %d: frame sha256 %s" % (m, hsh))
        assert decal_loud(text) == [], decal_loud(text)
        first = re.findall(r"pipeline: (?:first use|slow bake): .*?\((\d+) decal textures loaded\)", text)
        loaded = sum(int(x) for x in first)
        if m == 0:
            assert "read by the bakes on the render thread (C5_RENDER_DECAL_LOADER=0" in text, text[:3000]
            assert loaded >= 1, first   # the bakes read the road textures themselves
        else:
            assert re.search(r"read off the render thread by the decal pass's loader, mode %d" % m, text), text[:3000]
            assert loaded == 0, first   # no bake read a decal texture
    assert runs[0][0] == runs[1][0] == runs[2][0], {m: r[0] for m, r in runs.items()}
    assert plain != runs[2][0], "the decal is not in the dumped frame: the comparison would prove nothing"
