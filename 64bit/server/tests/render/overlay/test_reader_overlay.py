"""Issue 3703 (epic 0167): server.exe's reader of the Terrain Overlay target tap (server/src/render/srv_overlay_tap.cpp),
run as test_reader_tool.exe (test_reader_build.py; C5_OVERLAY_READER names it) on sections crafted here: a main section
with the overlay block at 3904 and a camera presentCount, the overlay section Local\\VPShellOverlay.<pid> with three
slots, and (through the tool's --craft) the NT-shared texture of the slot. No DLL: the producer side is
shell/tests/test_overlay_tap.py. One broken piece per test; the exit code, the reader's state, its counters, the text
it prints and what it writes back into the block (inUseSlot, ackGeneration) are asserted. A missing binary fails (no
skip; COMMON 9).
"""
import itertools
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "shell" / "tests"))
import tapcheck  # noqa: E402  (shell/tests/tapcheck.py: named sections through ctypes)

OVL_OFFSET = 3904
SLOT_BYTES, SLOTS, SEC_HDR = 336, 3, 64
MAGIC_O, VERSION_O = 0x594C564F, 1
NONE, ACTIVE, FAILED, OFF = 0, 1, 2, 3
FMT_TYPELESS, FMT_UNORM, FMT_BGRA = 27, 28, 87
CAM_PRESENT = tapcheck.CAMERA_OFFSET + 8
_pids = itertools.count(3_900_000 + (os.getpid() % 1000) * 100)


def tool():
    p = os.environ.get("C5_OVERLAY_READER", "")
    assert p and Path(p).exists(), ("C5_OVERLAY_READER must name test_reader_tool.exe "
                                    "(python -I server/tests/render/overlay/test_reader_build.py --out DIR)")
    return p


def run_tool(section, *args, timeout=60):
    p = subprocess.run([tool(), "--section", section, *args], capture_output=True, text=True, timeout=timeout,
                       creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def proj(k=7.0, v=0):
    m = [0.0] * 16
    m[0], m[5], m[10], m[15] = 2 / 832, 2 / 640, -0.5, 1.0
    m[12], m[13], m[14] = k, float(v), 0.5
    return m


class Crafted:
    """The main section (camera presentCount, overlay block) and the overlay section of a fake EXE pid."""

    def __init__(self, tag, *, state=ACTIVE, fail=0, newest=1, generation=5, slot=None, section=True, magic=MAGIC_O,
                 version=VERSION_O, slot_count=SLOTS, slot_bytes=SLOT_BYTES, sec_pid=None, block_slots=SLOTS,
                 present=100, sec_size=None):
        self.pid = next(_pids)
        self.name = "Local\\VPShellOTest_%d_%s" % (os.getpid(), tag)
        self.main = tapcheck.Mapping(self.name, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        self.main.write(CAM_PRESENT, struct.pack("<q", present))
        self.sec = None
        if section:
            self.sec = tapcheck.Mapping("Local\\VPShellOverlay.%d" % self.pid, create=sec_size or SEC_HDR + SLOTS * SLOT_BYTES)
            self.sec.write(0, struct.pack("<5I", magic, version, slot_count, slot_bytes, self.pid if sec_pid is None else sec_pid))
            s = dict(valid=1, generation=generation, views=2, present=present - 1, w=2048, h=2048, mips=12, fmt=FMT_TYPELESS,
                     proj=[proj(7, 0), proj(7, 1)], seq=2, epoch=1)
            s.update(slot or {})
            self.write_slot(newest if newest < SLOTS else 0, **s)
        self.write_block(state=state, fail=fail, newest=newest, generation=generation, slots=block_slots)

    def write_slot(self, i, *, valid, generation, views, present, w, h, mips, fmt, proj, seq, epoch):
        b = bytearray(SLOT_BYTES)
        struct.pack_into("<4IQq4I", b, 0, seq, valid, generation, views, present, 1, w, h, mips, fmt)
        for v, m in enumerate(proj[:4]):
            struct.pack_into("<16f", b, 48 + 64 * v, *m)
        struct.pack_into("<I", b, 304, epoch)
        self.sec.write(SEC_HDR + SLOT_BYTES * i, bytes(b))

    def write_block(self, *, state, fail, newest, generation, slots=SLOTS):
        b = bytearray(112)
        struct.pack_into("<8IQff3I", b, 0, 2, state, fail, self.pid, slots, newest, generation, 1, 10, 0.1, 0.05,
                         0xFFFFFFFF, 0, 0)
        self.main.write(OVL_OFFSET, bytes(b))

    def consumer(self):
        return struct.unpack("<3I", self.main.read(OVL_OFFSET + 48, 12))   # inUseSlot, ackGeneration, ackServerPid

    def close(self):
        if self.sec:
            self.sec.close()
        self.main.close()


@pytest.fixture
def crafted(request):
    made = []

    def make(**kw):
        c = Crafted("%d_%d" % (abs(hash(request.node.name)) % 10**8, len(made)), **kw)
        made.append(c)
        return c
    yield make
    for c in made:
        c.close()


def craft_args(c, slot=1, w=2048, h=2048, mips=12, fmt=FMT_TYPELESS):
    return ["--craft", "%d,%d,%d,%d" % (w, h, mips, fmt), "--craft-pid", str(c.pid), "--craft-slot", str(slot)]


def test_reader_binds_a_good_slot_and_acks(crafted, tmp_path):
    c = crafted()
    js = tmp_path / "r.json"
    rc, txt = run_tool(c.name, "--seconds", "5", "--json", str(js), *craft_args(c))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["state"] == "Active" and r["accepted"] == 1 and r["refused"] == 0 and r["stale"] == 0, r
    p = r["pairs"][0]
    assert (p["generation"], p["slot"], p["numViews"], p["presentCount"]) == (5, 1, 2, 99)
    assert p["proj0"][:1] == [pytest.approx(2 / 832)] and p["proj0"][12] == 7.0
    assert (r["inUseSlot"], r["ackGeneration"]) == (1, 5)        # named while bound, acked
    assert "first pair bound: slot 1 generation 5" in txt and "section 'Local\\VPShellOverlay.%d' open" % c.pid in txt
    assert c.consumer()[0] == 0xFFFFFFFF                           # the reader's destructor names no slot
    assert c.consumer()[1] == 5 and c.consumer()[2] != 0


def test_reader_unorm_texture_is_accepted(crafted):
    c = crafted(slot=dict(fmt=FMT_UNORM))
    rc, txt = run_tool(c.name, "--seconds", "5", *craft_args(c, fmt=FMT_UNORM))
    assert rc == 0, txt


@pytest.mark.parametrize("case,kw,craft,text", [
    ("views0", dict(slot=dict(views=0)), {}, "numViews 0 (1..4)"),
    ("views5", dict(slot=dict(views=5)), {}, "numViews 5 (1..4)"),
    ("nanproj", dict(slot=dict(proj=[proj(7, 0)[:5] + [float("nan")] + proj(7, 0)[6:], proj(7, 1)])), {},
     "OverlayProj view 0 element 5 is not finite"),
    ("infproj", dict(slot=dict(views=2, proj=[proj(7, 0), proj(float("inf"), 1)])), {}, "OverlayProj view 1 element 12 is not finite"),
    ("slotsize", dict(slot=dict(w=1024)), {}, "slot describes 1024 x 2048, 12 mips, format 27"),
    ("slotmips", dict(slot=dict(mips=1)), {}, "slot describes 2048 x 2048, 1 mips"),
    ("slotformat", dict(slot=dict(fmt=FMT_BGRA)), {}, "format 87 (expected"),
    ("present0", dict(slot=dict(present=0)), {}, "presentCount 0"),
    ("newest", dict(newest=7), {}, "newest slot 7 of 3"),
    ("notexture", dict(), None, "OpenSharedResourceByName('Local\\VPShellOverlayTex."),
    ("texsize", dict(), dict(w=1024, h=1024, mips=11), "is 1024 x 1024, 11 mips"),
    ("texformat", dict(), dict(fmt=FMT_BGRA), "format 87 (expected 2048 x 2048, 12 mips, R8G8B8A8)"),
    ("texmips", dict(), dict(mips=1), "2048 x 2048, 1 mips"),
])
def test_reader_refuses_a_bad_slot(crafted, case, kw, craft, text):
    """A slot that fails a check is refused (counted, LOUD), nothing is bound, inUseSlot goes back to none; the reader
    stays Active (a fault in play, D132), so the next good copy would bind."""
    c = crafted(**kw)
    args = [] if craft is None else craft_args(c, **craft)
    rc, txt = run_tool(c.name, "--seconds", "1.5", *args)
    assert rc == 4, txt
    assert "overlay: LOUD: slot" in txt and text in txt, txt
    res = txt.split("result:")[1]
    assert "state Active" in res and "accepted 0" in res and "refused 0" not in res, res
    assert c.consumer()[0] == 0xFFFFFFFF and c.consumer()[1] == 0


@pytest.mark.parametrize("case,kw,text", [
    ("magic", dict(magic=0x1234), "overlay section magic 0x00001234 version 1"),
    ("version", dict(version=2), "version 2, expected 0x594C564F version 1"),
    ("slotbytes", dict(slot_bytes=320), "slots 3 x 320 bytes"),
    ("slotcount", dict(slot_count=2), "slots 2 x 336 bytes"),
    ("blockslots", dict(block_slots=4), "(block 4 slots)"),
    ("pid", dict(sec_pid=42), "overlay section pid 42"),
])
def test_reader_refuses_a_section_layout_for_good(crafted, case, kw, text):
    """A section whose layout this reader does not know: Failed with VPOVLF_VERSION (8), final (MC #701: the server
    fails its render start with this text)."""
    c = crafted(**kw)
    rc, txt = run_tool(c.name, "--seconds", "1.5", *craft_args(c))
    assert rc == 1, txt
    assert "state Failed failReason 8" in txt and "the overlay tap's section is refused by the server" in txt and text in txt, txt
    assert "(VPOVLF_VERSION)" in txt
    assert c.consumer()[0] == 0xFFFFFFFF


@pytest.mark.parametrize("fail,name", [(1, "PATTERN"), (2, "HANDLE"), (3, "DESC"), (4, "SHARED"), (5, "EXCEPTION"), (6, "VIEWS")])
def test_reader_reports_the_producers_install_failure(crafted, fail, name):
    c = crafted(state=FAILED, fail=fail, section=False)
    rc, txt = run_tool(c.name, "--seconds", "1")
    assert rc == 1, txt
    assert "state Failed failReason %d" % fail in txt
    assert "the overlay tap could not install: VPOVL_FAILED %s (reason %d" % (name, fail) in txt, txt
    assert txt.count("overlay: LOUD:") == 1, txt     # once, not per call


def test_reader_off_and_none_bind_nothing(crafted):
    c = crafted(state=OFF, section=False)
    rc, txt = run_tool(c.name, "--seconds", "0.5")
    assert rc == 4 and "state Off" in txt, txt
    c2 = crafted(state=NONE, section=False)
    rc, txt = run_tool(c2.name, "--seconds", "0.5")
    assert rc == 4 and "state Waiting" in txt, txt


def test_reader_absent_block(crafted):
    c = crafted(section=False)
    c.main.write(OVL_OFFSET, bytes(112))          # never written: seq 0 (an older DLL)
    rc, txt = run_tool(c.name, "--seconds", "0.5")
    assert rc == 4 and "state Absent" in txt, txt


def test_reader_missing_section_is_not_final(crafted):
    c = crafted(section=False)
    rc, txt = run_tool(c.name, "--seconds", "0.5")
    assert rc == 4 and "cannot open the overlay section 'Local\\VPShellOverlay.%d'" % c.pid in txt and "state Waiting" in txt, txt
    assert txt.count("LOUD: cannot open the overlay section") == 1, txt   # LOUD once per distinct text


@pytest.mark.parametrize("slot", [dict(valid=0), dict(seq=3), dict(generation=4)])
def test_reader_skips_a_slot_in_flight_without_counting(crafted, slot):
    """valid 0, an odd seq (the producer writing) or a generation that is not the block's: the hand-over is retried, not
    refused; inUseSlot goes back to none."""
    c = crafted(slot=slot)
    rc, txt = run_tool(c.name, "--seconds", "1", *craft_args(c))
    assert rc == 4, txt
    res = txt.split("result:")[1]
    assert "accepted 0 refused 0" in res and "LOUD" not in txt, txt
    assert c.consumer()[0] == 0xFFFFFFFF


MISSED_O = OVL_OFFSET + 60   # VPShellOverlayTap.missedTotal (outside the seqlock)


def _presents_run(c, tmp_path, map_frames):
    """Bind the crafted pair, then let the target's Presents go on (camera presentCount) for 2.5 s without a new
    generation; with map_frames, each Present is also a map frame the DLL counted as missed (missedTotal), as when the
    copy's query is late; without, the Presents have no scene-clear list (an AI leader screen: no copy, no miss)."""
    import threading
    import time
    stop = threading.Event()

    def presents():
        n, m = 100, 0
        time.sleep(0.6)
        while not stop.is_set():
            n += 1
            c.main.write(CAM_PRESENT, struct.pack("<q", n))
            if map_frames:
                m += 1
                c.main.write(MISSED_O, struct.pack("<I", m))
            time.sleep(0.005)
    t = threading.Thread(target=presents)
    t.start()
    try:
        js = tmp_path / "r.json"
        rc, txt = run_tool(c.name, "--seconds", "2.5", "--pairs", "0", "--json", str(js), *craft_args(c))
    finally:
        stop.set()
        t.join()
    return rc, txt, json.loads(js.read_text()) if js.exists() else {}


def test_reader_stale_episode_counted_once(crafted, tmp_path):
    """A pair is bound, then map frames go on (missedTotal: the copy's query is late) without a new generation: one
    stale count and one log line, not LOUD (MC #891); the old pair stays bound (acquire still returns it)."""
    c = crafted()
    rc, txt, r = _presents_run(c, tmp_path, map_frames=True)
    assert rc == 0, txt
    assert r["stale"] == 1 and r["accepted"] == 1 and r["state"] == "Active", r
    assert txt.count("stale (lag, counted, D132): no new copy for") == 1, txt   # MC #891: a lag, not LOUD
    assert re.search(r"no new copy for \d+ map frames \(\d+ Presents of the target\), \d+ ms", txt), txt
    assert "LOUD: stale" not in txt, txt


def test_reader_presents_without_map_frames_are_not_stale(crafted, tmp_path):
    """Issue 0194: Presents without map frames (no copy, no missed copy: an AI leader screen, frames without the
    scene-clear list) are not a lag: stale stays 0 and the gap is noted once, not LOUD. The rule before 0194 counted
    one stale episode here (Presents of the target only)."""
    c = crafted()
    rc, txt, r = _presents_run(c, tmp_path, map_frames=False)
    assert rc == 0, txt
    assert r["stale"] == 0 and r["accepted"] == 1 and r["state"] == "Active", r
    assert txt.count("map frames since the last copy (frames without the scene-clear list") == 1, txt
    assert "with 0 map frames since the last copy" in txt, txt
    assert "stale (lag" not in txt and "LOUD" not in txt, txt


def test_reader_usage_and_missing_main_section():
    p = subprocess.run([tool()], capture_output=True, text=True, creationflags=0x08000000)
    assert p.returncode == 2 and "--section required" in p.stdout
    p = subprocess.run([tool(), "--bogus"], capture_output=True, text=True, creationflags=0x08000000)
    assert p.returncode == 2 and "unknown argument" in p.stdout
    rc, txt = run_tool("Local\\VPShellOTestNoSuch_%d" % os.getpid(), "--seconds", "0.5")
    assert rc == 3 and "does not open" in txt, txt
