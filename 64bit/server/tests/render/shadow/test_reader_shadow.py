"""Issue 4304 (epic 0169, ticket 0312): server.exe's reader of the shadow tap (server/src/render/srv_shadow_tap.cpp), run as
shadow_reader_tool.exe (build_shadow_tests.py; C5_SHADOW_READER names it) on sections crafted here: the shadow section
Local\\VPShellShadowTap.<pid> with its control block and three slots, an optional main section (presentCount), and
(through the tool's --craft) the NT-shared texture of a slot. No DLL: the producer side is shell/tests/test_shadow_tap.py.
One broken piece per test; the exit code, the reader's state, its counters, the text it prints and what it writes back
into the block (inUseSlot, ackGeneration) are asserted. Every state (Absent, Waiting, Off, Active, Failed), every refusal
(layout and version, pid, numViews, a matrix, the strength, the quality, each quality's description, presentCount, the
shared texture missing or of another description, the newest slot), the torn slot kept out (seq odd, valid 0, a
generation that is not the block's), the verify compare. A missing binary fails (no skip; COMMON 9).
"""
import itertools
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "shell" / "tests"))
import tapcheck  # noqa: E402  (shell/tests/tapcheck.py: named sections through ctypes)

SEC_BYTES, SLOT_OFF, SLOT_BYTES, SLOTS, HDR_BYTES, NO_SLOT = 4096, 512, 368, 3, 320, 0xFFFFFFFF
MAGIC, VERSION = 0x57444853, 1
NONE, ACTIVE, FAILED, OFF = 0, 1, 2, 3
R16, R16F, RGBA16F, RGBA8 = 56, 54, 10, 28
NO_TEXTURE, VERIFY = 1, 2
_pids = itertools.count(4_300_000 + (os.getpid() % 1000) * 100)


def tool():
    p = os.environ.get("C5_SHADOW_READER", "")
    assert p and Path(p).exists(), ("C5_SHADOW_READER must name shadow_reader_tool.exe "
                                    "(python -I server/tests/render/shadow/build_shadow_tests.py --out DIR)")
    return p


def matrix(k=7.0, v=0):
    m = [0.0] * 16
    m[0], m[5], m[10], m[15] = 0.0015625, 0.0015625, 0.0001, 1.0
    m[12], m[13], m[14] = k, float(v), 0.41
    return m


QUALITY = {3: dict(w=2048, h=2048, fmt=R16), 2: dict(w=1024, h=1024, fmt=R16), 1: dict(w=640, h=360, fmt=RGBA16F),
           0: dict(w=0, h=0, fmt=0)}


class Crafted:
    """The shadow section of a fake EXE pid (and its main section): one published slot `newest`."""

    def __init__(self, *, quality=3, state=ACTIVE, fail=0, fail_text=b"", newest=1, generation=5, slot=None, magic=MAGIC,
                 version=VERSION, header_bytes=HDR_BYTES, slot_off=SLOT_OFF, slot_bytes=SLOT_BYTES, slot_count=SLOTS,
                 sec_pid=None, seq=2, present=100, epoch=7):
        self.pid = next(_pids)
        self.main_name = "Local\\VPShellSTest_%d_%d" % (os.getpid(), self.pid)
        self.main = tapcheck.Mapping(self.main_name, create=4096)
        self.main.write(tapcheck.CAMERA_OFFSET + 8, struct.pack("<q", present))
        self.sec = tapcheck.Mapping("Local\\VPShellShadowTap.%d" % self.pid, create=SEC_BYTES)
        self.epoch = epoch
        q = QUALITY[quality]
        s = dict(valid=1, generation=generation, views=2, present=present - 1, w=q["w"], h=q["h"], mips=1 if quality else 0,
                 fmt=q["fmt"], quality=quality, strength=1.0 if quality else 0.0, epoch=epoch,
                 flags=0 if quality else NO_TEXTURE, mats=[matrix(7, 0), matrix(7, 1)], seq=2, vhash=0, vbytes=0)
        s.update(slot or {})
        if newest < SLOTS:
            self.write_slot(newest, **s)
        self.write_block(state=state, fail=fail, fail_text=fail_text, newest=newest, generation=generation, magic=magic,
                         version=version, header_bytes=header_bytes, slot_off=slot_off, slot_bytes=slot_bytes,
                         slot_count=slot_count, sec_pid=self.pid if sec_pid is None else sec_pid, seq=seq, quality=quality)

    def write_slot(self, i, *, valid, generation, views, present, w, h, mips, fmt, quality, strength, epoch, flags, mats, seq,
                   vhash, vbytes):
        b = bytearray(SLOT_BYTES)
        struct.pack_into("<4IQq", b, 0, seq, valid, generation, views, present, 1)
        struct.pack_into("<5IfII", b, 32, w, h, mips, fmt, quality, strength, epoch, flags)
        for v, m in enumerate(mats[:4]):
            struct.pack_into("<16f", b, 64 + 64 * v, *m)
        struct.pack_into("<QQ", b, 320, vhash, vbytes)
        self.sec.write(SLOT_OFF + SLOT_BYTES * i, bytes(b))

    def write_block(self, *, state, fail, fail_text, newest, generation, magic, version, header_bytes, slot_off, slot_bytes,
                    slot_count, sec_pid, seq, quality):
        b = bytearray(HDR_BYTES)
        struct.pack_into("<16I", b, 0, seq, magic, version, header_bytes, slot_off, slot_bytes, slot_count, sec_pid, state,
                         fail, newest, generation, 0, quality, 1, 0)
        struct.pack_into("<Qff", b, 64, 10, 0.1, 0.05)
        b[80:80 + len(fail_text)] = fail_text
        struct.pack_into("<3I", b, 240, NO_SLOT, 0, 0)
        self.sec.write(0, bytes(b))

    def block(self):
        raw = self.sec.read(0, HDR_BYTES)
        return dict(zip(("inUseSlot", "ackGeneration", "ackServerPid"), struct.unpack_from("<3I", raw, 240)))

    def run(self, *args, craft=None, slot=1, timeout=60, main=True):
        cmd = [tool(), "--pid", str(self.pid), *args]
        if main:
            cmd += ["--main", self.main_name]
        if craft:
            cmd += ["--craft", "%d,%d,%d" % craft, "--craft-slot", str(slot), "--craft-epoch", str(self.epoch)]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
        return p.returncode, p.stdout + p.stderr

    def close(self):
        self.sec.close()
        self.main.close()


@pytest.fixture
def craft():
    made = []

    def make(**kw):
        c = Crafted(**kw)
        made.append(c)
        return c
    yield make
    for c in made:
        c.close()


def jrun(c, tmp_path, *args, **kw):
    js = tmp_path / ("r%d.json" % c.pid)
    rc, txt = c.run("--json", str(js), *args, **kw)
    return rc, txt, (json.loads(js.read_text()) if js.exists() else {})


def tex(q):
    return QUALITY[q]["w"], QUALITY[q]["h"], QUALITY[q]["fmt"]


# ---- states ------------------------------------------------------------------------------------------------------------

def test_absent_without_a_section(tmp_path):
    pid = next(_pids)
    p = subprocess.run([tool(), "--pid", str(pid), "--seconds", "0.5"], capture_output=True, text=True, timeout=60,
                       creationflags=0x08000000)
    assert p.returncode == 4 and "result: state Absent" in p.stdout, p.stdout


def test_waiting_while_nothing_is_published(craft, tmp_path):
    c = craft(seq=0)                                  # the section made, the block never published
    rc, txt, r = jrun(c, tmp_path, "--seconds", "0.5")
    assert rc == 4 and r["state"] == "Waiting" and r["accepted"] == 0 and r["refused"] == 0, (r, txt)
    c2 = craft(state=NONE, generation=0, newest=NO_SLOT)
    rc, txt, r = jrun(c2, tmp_path, "--seconds", "0.5")
    assert rc == 4 and r["state"] == "Waiting", (r, txt)
    assert "tap block state NONE" in txt


def test_off(craft, tmp_path):
    c = craft(state=OFF, fail=7, generation=0, newest=NO_SLOT)
    rc, txt, r = jrun(c, tmp_path, "--seconds", "0.5")
    assert rc == 4 and r["state"] == "Off" and not r["installFailure"], (r, txt)
    assert "tap block state OFF (fail reason KILLED" in txt


def test_failed_names_the_producers_text(craft, tmp_path):
    c = craft(state=FAILED, fail=3, fail_text=b"DESC: quality 3: handle 0x6DB is 1024x1024 (for 30 map frames)",
              generation=0, newest=NO_SLOT)
    rc, txt, r = jrun(c, tmp_path, "--seconds", "0.5")
    assert rc == 1 and r["state"] == "Failed" and r["installFailure"] and r["failReason"] == 3, (r, txt)
    assert r["why"] == ("the shadow tap could not install: VPSHD_FAILED DESC (reason 3: DESC: quality 3: handle 0x6DB is "
                        "1024x1024 (for 30 map frames))"), r["why"]
    assert "shadow: LOUD: the shadow tap could not install" in txt


@pytest.mark.parametrize("q", [3, 2, 1])
def test_active_binds_each_quality(craft, tmp_path, q):
    c = craft(quality=q, generation=5)
    rc, txt, r = jrun(c, tmp_path, "--seconds", "5", craft=tex(q))
    assert rc == 0 and r["state"] == "Active" and r["accepted"] == 1 and r["refused"] == 0 and r["reopened"] == 1, (r, txt)
    p = r["pairs"][0]
    w, h, fmt = tex(q)
    assert (p["quality"], p["width"], p["height"], p["format"], p["srv"]) == (q, w, h, fmt, True), p
    assert p["generation"] == 5 and p["slot"] == 1 and p["numViews"] == 2 and p["presentCount"] == 99 and p["strength"] == 1.0
    assert p["m0"][12] == 7.0 and p["m0"][0] == pytest.approx(0.0015625)
    assert r["inUseSlot"] == NO_SLOT and r["ackGeneration"] == 5   # bound, acked, let go at exit
    assert "shadow: first triple bound: slot 1 generation 5" in txt


def test_active_quality0_binds_without_a_texture(craft, tmp_path):
    c = craft(quality=0)
    rc, txt, r = jrun(c, tmp_path, "--seconds", "5")
    assert rc == 0 and r["accepted"] == 1 and r["reopened"] == 0, (r, txt)
    p = r["pairs"][0]
    assert p["quality"] == 0 and not p["srv"] and p["strength"] == 0 and p["format"] == 0 and p["numViews"] == 2, p


# ---- refusals -------------------------------------------------------------------------------------------------------------

@pytest.mark.parametrize("kw,text", [
    (dict(magic=0x12345678), "magic 0x12345678 version 1, expected 0x57444853 version 1"),
    (dict(version=2), "magic 0x57444853 version 2, expected 0x57444853 version 1"),
    (dict(header_bytes=256), "header 256 bytes"),
    (dict(slot_bytes=336), "slots 3 x 336 bytes at 512"),
    (dict(slot_count=4), "slots 4 x 368 bytes at 512"),
    (dict(slot_off=1024), "slots 3 x 368 bytes at 1024"),
    (dict(sec_pid=1), "section pid 1, expected the EXE's"),
])
def test_layout_refused_is_final(craft, tmp_path, kw, text):
    c = craft(**kw)
    rc, txt, r = jrun(c, tmp_path, "--seconds", "1", craft=tex(3))
    assert rc == 1 and r["state"] == "Failed" and r["failReason"] == 8 and r["installFailure"], (r, txt)
    assert r["why"].startswith("the shadow tap's section is refused by the server: ") and text in r["why"], r["why"]
    assert txt.count("(VPSHDF_VERSION)") == 1


def m_nan():
    m = matrix()
    m[9] = float("nan")
    return m


@pytest.mark.parametrize("q,slot,text", [
    (3, dict(views=0), "numViews 0 (1..4)"),
    (3, dict(views=5), "numViews 5 (1..4)"),
    (3, dict(mats=[matrix(), m_nan()]), "ShadowMatrix0 view 1 element 9 is not finite"),
    (3, dict(strength=-0.01), "strength -0.01 (finite, 0..1)"),
    (3, dict(strength=1.01), "strength 1.01 (finite, 0..1)"),
    (3, dict(strength=float("inf")), "strength inf (finite, 0..1)"),
    (3, dict(quality=4), "quality 4 (0..3)"),
    (3, dict(w=1024, h=1024), "quality 3 slot describes 1024 x 1024, 1 mips, format 56"),
    (3, dict(fmt=R16F), "quality 3 slot describes 2048 x 2048, 1 mips, format 54"),
    (3, dict(mips=2), "quality 3 slot describes 2048 x 2048, 2 mips"),
    (3, dict(flags=NO_TEXTURE), "quality 3 slot without a texture"),
    (2, dict(w=2048, h=2048), "quality 2 slot describes 2048 x 2048, 1 mips, format 56"),
    (1, dict(fmt=R16), "quality 1 slot describes 640 x 360, 1 mips, format 56"),
    (1, dict(w=0), "quality 1 slot describes 0 x 360"),
    (0, dict(strength=0.5), "quality 0 needs a slot without texture and strength 0 (flags 0x1, strength 0.5)"),
    (0, dict(flags=0), "quality 0 needs a slot without texture and strength 0 (flags 0x0, strength 0)"),
    (3, dict(present=0), "presentCount 0"),
])
def test_slot_refused_is_counted_and_loud_once(craft, tmp_path, q, slot, text):
    c = craft(quality=q, slot=slot)
    rc, txt, r = jrun(c, tmp_path, "--pairs", "0", "--seconds", "1", craft=tex(q) if q else None)
    assert rc == 4 and r["accepted"] == 0 and r["refused"] > 1 and r["state"] == "Waiting", (r, txt)
    assert text in r["why"] and txt.count("LOUD: slot 1 generation 5 refused: ") == 1, (r["why"], txt)
    assert r["inUseSlot"] == NO_SLOT and r["ackGeneration"] == 0, r


def test_newest_slot_out_of_range(craft, tmp_path):
    c = craft(newest=5)
    rc, txt, r = jrun(c, tmp_path, "--pairs", "0", "--seconds", "0.5")
    assert rc == 4 and r["refused"] > 0 and "newest slot 5 of 3" in r["why"], (r, txt)


def test_shared_texture_missing(craft, tmp_path):
    c = craft(quality=3)
    rc, txt, r = jrun(c, tmp_path, "--pairs", "0", "--seconds", "0.5")   # no --craft: nothing under the name
    assert rc == 4 and r["refused"] > 0, (r, txt)
    assert "OpenSharedResourceByName('Local\\VPShellShadowTex.%d.1.7') failed" % c.pid in r["why"], r["why"]


@pytest.mark.parametrize("made,text", [
    ((2048, 2048, R16F), "is 2048 x 2048, 1 mips, array 1, format 54; the slot describes 2048 x 2048, format 56"),
    ((1024, 1024, R16), "is 1024 x 1024, 1 mips, array 1, format 56; the slot describes 2048 x 2048, format 56"),
])
def test_shared_texture_of_another_description(craft, tmp_path, made, text):
    c = craft(quality=3)
    rc, txt, r = jrun(c, tmp_path, "--pairs", "0", "--seconds", "0.5", craft=made)
    assert rc == 4 and r["refused"] > 0 and r["reopened"] == 0 and text in r["why"], (r, txt)


# ---- torn copies are kept out ------------------------------------------------------------------------------------------

@pytest.mark.parametrize("slot", [dict(seq=3), dict(valid=0), dict(generation=4)])
def test_torn_slot_is_never_taken(craft, tmp_path, slot):
    """A slot being written (seq odd), not yet published (valid 0) or of another generation than the block names is a
    torn copy: never bound, never counted as refused (the producer moved on), inUseSlot given back."""
    c = craft(quality=3, slot=slot)
    rc, txt, r = jrun(c, tmp_path, "--pairs", "0", "--seconds", "0.5", craft=tex(3))
    assert rc == 4 and r["accepted"] == 0 and r["refused"] == 0, (r, txt)
    assert r["inUseSlot"] == NO_SLOT and r["ackGeneration"] == 0, r


# ---- verify ---------------------------------------------------------------------------------------------------------------

def test_verify_mismatch_is_counted(craft, tmp_path):
    """A slot carrying VPSHD_SLOT_VERIFY whose hash does not match the server's read: verifyBad, exit 7 (the equal case
    end to end: shell/tests/test_shadow_tap.py test_verify_mode_end_to_end)."""
    c = craft(quality=2, slot=dict(flags=VERIFY, vhash=1, vbytes=1024 * 1024 * 2))
    rc, txt, r = jrun(c, tmp_path, "--seconds", "3", craft=tex(2))
    assert rc == 7 and r["verifyBad"] == 1 and r["verifyOk"] == 0 and r["accepted"] == 1, (r, txt)
    assert "shadow: VERIFY generation 5 (slot 1, quality 2, Present 99): 2097152 bytes" in txt and ": DIFFERS" in txt, txt
