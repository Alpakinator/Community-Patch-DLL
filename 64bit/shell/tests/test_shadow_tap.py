"""Issue 4304 (epic 0169, ticket 0312): the model shadow map tap of shell_composite.dll (composite/shadow_tap.cpp) with its
reader in server.exe (server/src/render/srv_shadow_tap.cpp, run as shadow_reader_tool.exe, C5_SHADOW_READER), on the
stand-in (hidden desktop, rule 14). Model: test_overlay_tap.py.

test_host --fake-exe (the main section is created here) with shadow_test=<mode>: a synthetic shadow state in the DLL's
memory (handles ShadowRaw / ShadowBlur, view table, quality, factor, g_NumViews, ShadowMatrix0 and real textures of each
quality's description with known texels; shadow_tap.cpp BuildTestWorld) runs through the real resolution, description,
slot, copy and publish code. Every Present the test world writes frame k into texel (0,0) of the texture (every 16-bit
word), into ShadowMatrix0 view 0 element 12 and into the factor ((k & 1023) + 1) / 1025, so the reader checks that
texels, matrices and strength of a bound slot are one frame (0312's frame rule). Checked: the triples exact and
consecutive at q3, q2, q1 and q0; the verify mode end to end (the DLL's byte compare and the reader's hash of its own
read); every install failure (handle, view object, each description per quality, quality, views, fault, section) ->
FAILED with its reason and text, LOUD once, the reader's Failed with that text; a description change between frames ->
slots made again, counted, never a mixed triple; faults in play (a bad description, the texture gone, a matrix or a
strength the reader refuses) -> LOUD, counted, the last triple stays; a slot in use is never written; the kill switch
(the shipped default 0, and during copies); the map-frame gate; the real path refusing the fake EXE and an unvalidated
one; a static check of the tap's byte patterns against the EXE on disk.

Needs C5_SHELL_BUILD (shell/build.cmd) and C5_SHADOW_READER (server/tests/render/shadow/build_shadow_tests.py). A
missing binary fails the test (no skip).
"""
import json
import os
import re
import struct
import subprocess
import time
from pathlib import Path

import pytest

import exe_golden
import shellrun
import tapcheck

HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")
SEC_FMT = "Local\\VPShellShadowTap.%d"
SLOT_OFF, SLOT_BYTES, NO_SLOT = 512, 368, 0xFFFFFFFF
STATES = ["NONE", "ACTIVE", "FAILED", "OFF"]
FAILS = ["NONE", "PATTERN", "HANDLE", "DESC", "SHARED", "EXCEPTION", "VIEWS", "KILLED", "VERSION", "QUALITY"]
KNOWN_WORD = (100 * 7 + 37 * 13 + 0x1234) & 0xFFFF     # TestWord(100, 37, 0) of shadow_tap.cpp
R16, RGBA16F = 56, 10


def reader_exe():
    p = os.environ.get("C5_SHADOW_READER", "")
    assert p and Path(p).exists(), ("C5_SHADOW_READER must name shadow_reader_tool.exe "
                                    "(python -I server/tests/render/shadow/build_shadow_tests.py --out DIR)")
    return p


def reader(run, *args, timeout=90):
    p = subprocess.run([reader_exe(), "--pid", str(run.host.pid), "--main", run.section, *args], capture_output=True,
                       text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def parse_block(raw):
    f = struct.unpack_from("<16I", raw, 0)
    d = dict(seq=f[0], magic=f[1], version=f[2], headerBytes=f[3], slotOffset=f[4], slotBytes=f[5], slotCount=f[6],
             exePid=f[7], state=f[8], failReason=f[9], newestSlot=f[10], generation=f[11], loadSeq=f[12], quality=f[13],
             slotSet=f[14])
    d["copiesTotal"], d["copyMsTotal"], d["copyMsMax"] = struct.unpack_from("<Qff", raw, 64)
    d["failText"] = raw[80:240].split(b"\0", 1)[0].decode("ascii", "replace")
    (d["inUseSlot"], d["ackGeneration"], d["ackServerPid"], d["missedTotal"], d["faultsTotal"], d["descChanges"],
     d["verifyCount"], d["verifyDiffer"]) = struct.unpack_from("<8I", raw, 240)
    d["stateName"] = STATES[d["state"]] if d["state"] < len(STATES) else "?"
    d["failName"] = FAILS[d["failReason"]] if d["failReason"] < len(FAILS) else "?"
    return d


def parse_slot(raw):
    seq, valid, gen, nv, pc, _q = struct.unpack_from("<4IQq", raw, 0)
    w, h, mips, fmt, quality = struct.unpack_from("<5I", raw, 32)
    strength, = struct.unpack_from("<f", raw, 52)
    epoch, flags = struct.unpack_from("<2I", raw, 56)
    return dict(seq=seq, valid=valid, generation=gen, numViews=nv, presentCount=pc, width=w, height=h, mips=mips, format=fmt,
                quality=quality, strength=strength, texEpoch=epoch, flags=flags)


class ShadowRun:
    def __init__(self, run, ini, seconds=20, host=None):
        self.run = run
        self.main = tapcheck.Mapping(run.section, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        # shadow_idle_ms=0 (ticket 0385): these tests start their reader seconds after the copies began, and some count
        # copies with no reader at all; the reader gate (pause while nothing acks) has tests of its own below
        cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", shadow_tap="1",
                   shadow_need_scene="0", shadow_fail_frames="5", water_tap="0", overlay_tap="0", decal_tap="0",
                   shadow_idle_ms="0")
        cfg.update(ini)
        run.set_ini(**cfg)
        run.start_host(*(host or HOST), seconds=seconds)
        self.sec = None

    def section(self, timeout_s=15.0):
        t_end = time.time() + timeout_s
        while self.sec is None:
            try:
                self.sec = tapcheck.Mapping(SEC_FMT % self.run.host.pid)
            except OSError:
                if time.time() > t_end:
                    raise TimeoutError("shadow section of pid %d not made; log tail %s" % (
                        self.run.host.pid, shellrun.tail(self.run.composite_log, 10)))
                time.sleep(0.05)
        return self.sec

    def block(self):
        raw = tapcheck.read_block(self.section(), 0, 320)
        return parse_block(raw) if raw else {}

    def slot(self, i):
        return parse_slot(self.section().read(SLOT_OFF + SLOT_BYTES * i, SLOT_BYTES))

    def wait(self, pred, what, timeout_s=20.0):
        t_end = time.time() + timeout_s
        b = {}
        while time.time() < t_end:
            b = self.block()
            if b and pred(b):
                return b
            time.sleep(0.02)
        raise TimeoutError("shadow tap: %s not reached: %s; log tail %s" % (what, b, shellrun.tail(self.run.composite_log, 10)))

    def wait_state(self, state, timeout_s=20.0):
        return self.wait(lambda b: b["stateName"] == state, "state " + state, timeout_s)

    def close(self):
        if self.sec:
            self.sec.close()
        self.main.close()


@pytest.fixture
def srun(run):
    made = []

    def make(ini, seconds=20, host=None):
        o = ShadowRun(run, ini, seconds, host)
        made.append(o)
        return o
    yield make
    for o in made:
        o.close()


def finish(run, seconds=40):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert rows and not any(r["disabled"] for r in rows)
    assert sum(r["frames"] for r in rows) > 0


def slines(run):
    return [ln for ln in run.log_lines() if "shadow_tap:" in ln]


def no_loud(run):
    bad = [ln for ln in slines(run) if "LOUD" in ln or "FAILED" in ln]
    assert not bad, bad


@pytest.mark.parametrize("mode,quality,fmt", [("ok3", 3, R16), ("ok2", 2, R16), ("ok1", 1, RGBA16F)])
def test_triples_exact_and_consecutive(run, srun, tmp_path, mode, quality, fmt):
    """40 triples read while the producer copies at the stand-in's full rate: each triple's texels, ShadowMatrix0 and
    strength are of one frame; generations, frames and presentCounts only grow; the quality's description; the known
    texels survive the copy."""
    o = srun(dict(shadow_test=mode), seconds=30)
    a = o.wait_state("ACTIVE")
    assert a["exePid"] == run.host.pid and a["slotCount"] == 3 and a["failName"] == "NONE" and a["quality"] == quality, a
    assert a["magic"] == 0x57444853 and a["version"] == 1 and a["headerBytes"] == 320 and a["slotBytes"] == SLOT_BYTES
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "40", "--seconds", "20", "--readback", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["mismatches"] == 0 and r["state"] == "Active" and r["refused"] == 0 and not r["installFailure"], r
    ps = r["pairs"]
    assert len(ps) == 40
    size = {3: 2048, 2: 1024}.get(quality)
    for p in ps:
        assert p["quality"] == quality and p["format"] == fmt and p["numViews"] == 2 and p["srv"], p
        assert p["texelKnown"] == KNOWN_WORD and p["texel0"] == (p["k"] & 0xFFFF), p
        assert p["strength"] == pytest.approx(((p["k"] & 1023) + 1) / 1025, abs=1e-6), p
        assert p["m0"][0] == pytest.approx(0.0015625) and p["m0"][15] == 1.0 and p["m0"][14] == pytest.approx(0.41)
        if size:
            assert p["width"] == size and p["height"] == size, p
        else:
            assert p["width"] > 100 and p["height"] > 37, p   # ShadowRaw: the stand-in's back buffer
        # ticket 4785: ShadowRaw's size (the back buffer's) in every slot; at quality 1 the slot is ShadowRaw itself; the unit
        # mask flag is clear (no server's word asks the filter to drop the units' mask here)
        assert p["rawWidth"] > 100 and p["rawHeight"] > 37 and not p["unitMaskDropped"], p
        if not size:
            assert (p["rawWidth"], p["rawHeight"]) == (p["width"], p["height"]), p
    for x, y in zip(ps, ps[1:]):
        assert y["generation"] > x["generation"] and y["k"] > x["k"] and y["presentCount"] > x["presentCount"], (x, y)
    assert len({p["slot"] for p in ps}) >= 2
    assert r["inUseSlot"] == NO_SLOT and r["ackGeneration"] == ps[-1]["generation"]   # the reader let go at exit
    b = o.block()
    assert b["copiesTotal"] >= 40 and b["copyMsTotal"] > 0 and b["faultsTotal"] == 0 and b["descChanges"] == 0, b
    run.wait_log(r"shadow_tap: INSTALLED: first frame published \(slot \d, generation 1, Present \d+, quality %d, 2 views"
                 % quality, timeout_s=2)
    run.wait_log(r"shadow_tap: 3 slots of \d+x\d+, format %d created \(NT-shared, set 1\)" % fmt, timeout_s=2)
    finish(run)
    no_loud(run)
    inflight = [ln for ln in slines(run) if "missed copy (lag, counted, D132)" in ln]
    assert len(inflight) <= 3, inflight


def test_quality0_publishes_strength0_without_a_copy(run, srun, tmp_path):
    o = srun(dict(shadow_test="ok0"), seconds=20)
    a = o.wait_state("ACTIVE")
    assert a["quality"] == 0, a
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "20", "--seconds", "10", "--readback", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    for p in r["pairs"]:
        assert p["quality"] == 0 and not p["srv"] and p["strength"] == 0 and p["width"] == 0 and p["format"] == 0, p
        assert p["numViews"] == 2 and p["m0"][12] == p["k"]
    finish(run)
    no_loud(run)
    assert not any("slots of" in ln for ln in slines(run)), "no slot texture is made at quality 0"


@pytest.mark.parametrize("mode", ["ok3", "ok1"])
def test_verify_mode_end_to_end(run, srun, tmp_path, mode):
    """shadow_verify=1: every copy is read back in the same Present (the DLL's byte compare: 0 differ) and its FNV-1a 64
    carried in the slot; the reader hashes its own read of the shared texture: equal on every verified triple."""
    o = srun(dict(shadow_test=mode, shadow_verify="1"), seconds=25)
    o.wait_state("ACTIVE")
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "5", "--seconds", "20", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["verifyOk"] >= 5 and r["verifyBad"] == 0, r
    assert re.search(r"shadow: VERIFY generation \d+ .*: EQUAL", txt), txt
    b = o.block()
    assert b["verifyCount"] >= 5 and b["verifyDiffer"] == 0, b
    lines = [ln for ln in slines(run) if "VERIFY copy" in ln]
    assert lines and all(re.search(r": \d+ bytes, 0 differ, fnv [0-9A-F]{16}", ln) for ln in lines), lines[:3]
    if mode == "ok3":
        assert ": 8388608 bytes, 0 differ" in lines[0], lines[0]
    finish(run)


@pytest.mark.parametrize("mode,fail,text", [
    ("nullhandle", "HANDLE", r"handle \[shadow_blur\] = 0x0 \(no ShadowBlur\) \(for 5 map frames\)"),
    ("badvtbl", "HANDLE", r"handle 0x22: no D3D11 texture view in its table entry"),
    ("wrongsize3", "DESC", r"quality 3: handle 0x22 is 1024x1024, 1 mips, array 1, 1 samples, format 56 \(expected 2048 x 2048"),
    ("wrongformat3", "DESC", r"quality 3: handle 0x22 is 2048x2048, 1 mips, array 1, 1 samples, format 54"),
    ("wrongmips3", "DESC", r"quality 3: handle 0x22 is 2048x2048, 2 mips"),
    ("wrongsize2", "DESC", r"quality 2: handle 0x22 is 2048x2048, 1 mips, array 1, 1 samples, format 56 \(expected 1024 x 1024"),
    ("wrongformat2", "DESC", r"quality 2: handle 0x22 is 1024x1024, 1 mips, array 1, 1 samples, format 54"),
    ("q1notraw", "HANDLE", r"quality 1 but ShadowBlur 0x22 is not ShadowRaw's handle 0x21"),
    ("q1wrongformat", "DESC", r"quality 1: handle 0x21 is \d+x\d+, 1 mips, array 1, 1 samples, format 56 \(expected ShadowRaw"),
    ("q1wrongsize", "DESC", r"quality 1: handle 0x21 is \d+x\d+, 1 mips, array 1, 1 samples, format 10 \(expected ShadowRaw: "
                            r"the back buffer's size"),
    ("badquality", "QUALITY", r"the shadow quality \[shadow_quality\] = 7 \(0\.\.3\)"),
    ("views0", "VIEWS", r"g_NumViews = 0 \(1\.\.4\)"),
    ("views9", "VIEWS", r"g_NumViews = 9 \(1\.\.4\)"),
    ("fault", "EXCEPTION", r"fault reading the view object 0x[0-9A-F]+ of handle 0x22"),
])
def test_install_failure_is_loud_and_final(run, srun, tmp_path, mode, fail, text):
    """FAILED with its reason and text, one LOUD line, the reader Failed naming the producer's text (the reason for D125's
    route, 4308); the host and the composite run on; no slot texture is made."""
    o = srun(dict(shadow_test=mode))
    b = o.wait_state("FAILED")
    assert b["failName"] == fail and b["copiesTotal"] == 0, b
    assert re.search(text, b["failText"]) and b["failText"].startswith(fail + ": "), b["failText"]
    run.wait_log(r"shadow_tap: LOUD: FAILED %s at the install: .*%s" % (fail, text), timeout_s=5)
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--seconds", "2", "--json", str(js))
    assert rc == 1, txt
    r = json.loads(js.read_text())
    assert r["state"] == "Failed" and r["installFailure"] and r["failReason"] == FAILS.index(fail), r
    assert r["why"].startswith("the shadow tap could not install: VPSHD_FAILED %s (reason %d: %s: " % (
        fail, FAILS.index(fail), fail)), r["why"]
    time.sleep(0.5)
    assert sum("LOUD: FAILED" in ln for ln in slines(run)) == 1
    assert not any("slots of" in ln for ln in slines(run))
    finish(run)


def test_section_failure_is_named(run, srun):
    srun(dict(shadow_test="ok3", shadow_section="Local\\VPShellShadowTap\\no\\such"))
    run.wait_log(r"shadow_tap: CreateFileMapping\('Local\\VPShellShadowTap\\no\\such', 4096\) failed", timeout_s=15)
    run.wait_log(r"shadow_tap: LOUD: FAILED SHARED at the install: the shadow section could not be created", timeout_s=2)
    rc, txt = reader(run, "--seconds", "1")
    assert rc == 4 and "state Absent" in txt, txt   # nothing to read: the server's own rule decides (4308)
    finish(run)


def test_description_change_remakes_slots_never_mixed(run, srun, tmp_path):
    """shadow_test=descchange: 3 s after the first test frame the setup re-creates ShadowBlur under the same handle at quality 2
    (1024^2). The producer makes its slots again (counted once, new names); the reader re-opens and every triple carries
    one description: q3 triples 2048^2, q2 triples 1024^2, texels and strength of their own frame."""
    o = srun(dict(shadow_test="descchange", shadow_test_ms="3000"), seconds=30)
    o.wait_state("ACTIVE")
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "0", "--seconds", "7", "--readback", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    ps = r["pairs"]
    assert r["mismatches"] == 0 and r["refused"] == 0, r
    qs = [p["quality"] for p in ps]
    assert qs[0] == 3 and qs[-1] == 2 and qs == sorted(qs, reverse=True), qs   # one change, never back
    for p in ps:
        assert (p["quality"], p["width"], p["height"], p["format"]) in ((3, 2048, 2048, R16), (2, 1024, 1024, R16)), p
    assert r["reopened"] >= 2, r          # the first set and the new one
    assert "the bound description changed: quality 3 2048 x 2048 format 56 -> quality 2 1024 x 1024 format 56" in txt, txt
    b = o.block()
    assert b["descChanges"] == 1 and b["slotSet"] == 2 and b["faultsTotal"] == 0 and b["stateName"] == "ACTIVE", b
    run.wait_log(r"shadow_tap: description changed \(2048x2048 format 56 -> 1024x1024 format 56, quality 3 -> 2\): slots "
                 r"made again \(change 1\)", timeout_s=2)
    finish(run)
    no_loud(run)


@pytest.mark.parametrize("mode,text", [
    ("descbad", r"LOUD: missed copy: DESC quality 3: handle 0x22 is 2048x2048, 1 mips, array 1, 1 samples, format 28"),
    ("lost", r"LOUD: missed copy: HANDLE handle \[shadow_blur\] = 0x0 \(no ShadowBlur\)"),
])
def test_fault_in_play_is_counted_and_the_last_triple_stays(run, srun, tmp_path, mode, text):
    """After the install the texture gets a wrong description (descbad) or goes (lost): the block stays ACTIVE with the
    last copy, faults and missed count up, LOUD lines are rate-limited; the reader keeps its triple and counts one stale
    episode."""
    o = srun(dict(shadow_test=mode, shadow_test_frame="600"), seconds=20)
    o.wait_state("ACTIVE")
    b = o.wait(lambda b: b["faultsTotal"] > 50, "faults counted")
    assert b["stateName"] == "ACTIVE" and b["missedTotal"] >= b["faultsTotal"] - 1 and b["descChanges"] == 0, b
    gen = b["generation"]
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "0", "--seconds", "3", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["stale"] == 1 and r["accepted"] == 1 and r["pairs"][0]["generation"] == gen, r
    assert "stale (lag, counted, D132): no new copy for" in txt
    loud = [ln for ln in slines(run) if re.search(text, ln)]
    # the first in full, then only every 1000th repetition of the same text (D132's rate limit, as the overlay's)
    assert loud and "(repeated" not in loud[0] and all("(repeated 1000 times)" in ln for ln in loud[1:]), loud[:3]
    assert len(loud) <= 1 + o.block()["faultsTotal"] // 1000, (len(loud), o.block())
    assert o.block()["generation"] == gen
    finish(run)


@pytest.mark.parametrize("mode,text", [
    ("nanmatrix", "ShadowMatrix0 view 0 element 5 is not finite"),
    ("badstrength", "strength 1.5 (finite, 0..1)"),
])
def test_reader_refuses_a_bad_value_and_keeps_the_last_triple(run, srun, tmp_path, mode, text):
    """From test frame 600 a matrix holds a NaN, or the factor is 1.5. The DLL copies what the EXE has; the reader refuses
    those slots (counted, LOUD once per text) and keeps the last good triple."""
    o = srun(dict(shadow_test=mode, shadow_test_frame="600"), seconds=20)
    o.wait(lambda b: b["generation"] > 700, "700 copies")
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "0", "--seconds", "2", "--json", str(js))
    r = json.loads(js.read_text())
    assert r["refused"] > 0 and r["accepted"] == 0 and r["state"] in ("Waiting", "Active"), r
    assert rc == 4, txt
    assert text in txt and txt.count("LOUD: slot") == 1, txt
    finish(run)


def test_quality_change_to_0(run, srun, tmp_path):
    """shadow_test=qchange0: quality 3 -> 0 3 s after the first test frame: triples with a texture, then without one at
    strength 0."""
    o = srun(dict(shadow_test="qchange0", shadow_test_ms="3000"), seconds=25)
    o.wait_state("ACTIVE")
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "0", "--seconds", "7", "--readback", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    ps = r["pairs"]
    assert ps[0]["quality"] == 3 and ps[0]["srv"] and ps[-1]["quality"] == 0 and not ps[-1]["srv"], (ps[0], ps[-1])
    assert all(p["strength"] == 0 for p in ps if p["quality"] == 0) and r["mismatches"] == 0, r
    run.wait_log(r"shadow_tap: quality 3 -> 0: strength 0, no copies", timeout_s=2)
    finish(run)
    no_loud(run)


def test_slot_in_use_is_never_written(run, srun):
    """The server names slot s in inUseSlot: over 1.5 s of copies the producer never touches s (its seq, generation and
    valid stay), while the published generation moves on through the other two slots."""
    o = srun(dict(shadow_test="ok3"), seconds=20)
    b = o.wait(lambda b: b["generation"] > 20, "20 copies")
    sec = o.section()
    s = b["newestSlot"]
    sec.write(240, struct.pack("<I", s))            # inUseSlot = s (as the reader does before it binds s)
    before = o.slot(s)
    g0 = o.block()["generation"]
    time.sleep(1.5)
    after = o.slot(s)
    b2 = o.block()
    sec.write(240, struct.pack("<I", NO_SLOT))
    assert after == before and after["valid"] == 1, (before, after)
    assert b2["generation"] > g0 + 20 and b2["newestSlot"] != s, b2
    others = [o.slot(i)["generation"] for i in range(3) if i != s]
    assert max(others) > before["generation"], others
    finish(run)


def test_kill_switch_shipped_default_publishes_off(run, srun):
    """The ini as shipped has shadow_tap=1 since 4308 binds the tap in the scene; the kill switch shadow_tap=0: OFF, no copy, the
    reader's state Off."""
    shipped = (shellrun.SHELL / "composite" / "shell_composite.ini").read_text(encoding="utf-8")
    assert re.search(r"^shadow_tap=1$", shipped, re.M)
    o = srun(dict(shadow_tap="0", shadow_test="ok3"), seconds=8)
    b = o.wait_state("OFF")
    assert b["copiesTotal"] == 0 and b["failName"] == "NONE"
    run.wait_log(r"shadow_tap: OFF \(shadow_tap=0\)$", timeout_s=3)
    rc, txt = reader(run, "--seconds", "1")
    assert rc == 4 and "state Off" in txt, txt
    finish(run)


def test_kill_switch_during_copies(run, srun):
    o = srun(dict(shadow_test="ok3"), seconds=20)
    o.wait(lambda b: b["generation"] > 10, "10 copies")
    run.set_ini(shadow_tap="0")
    b = o.wait_state("OFF", timeout_s=5)
    assert b["failName"] == "KILLED"
    run.wait_log(r"shadow_tap: OFF \(shadow_tap=0\): KILLED after \d+ copies; slots released", timeout_s=2)
    rc, txt = reader(run, "--seconds", "1")
    assert rc == 4 and "state Off" in txt, txt
    run.set_ini(shadow_tap="1")
    o.wait(lambda b: b["stateName"] == "ACTIVE" and b["generation"] > 20, "ACTIVE again", timeout_s=8)
    run.wait_log(r"shadow_tap: on again \(shadow_tap=1\)", timeout_s=2)
    finish(run)


def test_map_frame_gate(run, srun):
    """shadow_need_scene=1 (the product's default): the stand-in executes no scene-clear list, so no frame is a map frame:
    no copy, no failure, the state stays NONE."""
    o = srun(dict(shadow_test="ok3", shadow_need_scene="1"), seconds=8)
    time.sleep(4)
    b = o.block()
    assert b["stateName"] == "NONE" and b["copiesTotal"] == 0 and b["failName"] == "NONE", b
    finish(run)


# ======================================================================================================================
# ticket 0385: the reader gate. The reader acks every generation it binds; while a published generation waits
# shadow_idle_ms for an ack, the copies pause to one probe copy per shadow_probe_ms, and the next ack resumes them.

NO_READER_RX = (r"shadow_tap: no reader: no copy acked for (\d+) ms \(generation \d+, ack (\d+)\): copies paused, one probe "
                r"copy every %d ms \(episode (\d+); skipped copies are not counted as missed\)")


def gen_rate(o, seconds):
    """Generations published per second over `seconds`, and the block before and after."""
    b0 = o.block()
    t0 = time.time()
    time.sleep(seconds)
    b1 = o.block()
    return (b1["generation"] - b0["generation"]) / (time.time() - t0), b0, b1


def test_unread_copies_pause_and_a_reader_resumes_them(run, srun, tmp_path):
    """No reader: after shadow_idle_ms (500) the copies pause to the probes (200 ms: about 5 a second, against hundreds at
    the stand-in's frame rate); missed and faults do not move. A reader that comes binds a probe frame, acks it, and the
    copies resume at once: its 30 triples are exact and consecutive; once it has gone the copies pause again."""
    o = srun(dict(shadow_test="ok3", shadow_idle_ms="500", shadow_probe_ms="200"), seconds=40)
    o.wait_state("ACTIVE")
    line = run.wait_log(NO_READER_RX % 200, timeout_s=10)
    m = re.search(NO_READER_RX % 200, line)
    assert int(m.group(1)) >= 500 and m.group(2) == "0" and m.group(3) == "1", line
    rate, b0, b1 = gen_rate(o, 2.0)
    assert 2 <= rate <= 8, (rate, b0, b1)                       # the probes only
    assert b1["missedTotal"] == b0["missedTotal"] and b1["faultsTotal"] == 0 and b1["stateName"] == "ACTIVE", (b0, b1)
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--pairs", "30", "--seconds", "20", "--readback", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["mismatches"] == 0 and r["refused"] == 0 and r["state"] == "Active" and len(r["pairs"]) == 30, r
    ps = r["pairs"]
    for x, y in zip(ps, ps[1:]):
        assert y["generation"] > x["generation"] and y["k"] > x["k"], (x, y)
    back = run.wait_log(r"shadow_tap: reader back: generation \d+ acked by pid \d+ after \d+ ms unread \((\d+) copies skipped, "
                        r"(\d+) probe copies\): a copy every map frame again$", timeout_s=2)
    assert int(re.search(r"\((\d+) copies skipped", back).group(1)) > 50, back
    run.wait_log(r"shadow_tap: no reader: .*\(episode 2;", timeout_s=10)   # the reader has gone: paused again
    finish(run)
    no_loud(run)


def test_unread_pause_uses_the_default_idle_time(run, srun):
    """Without the keys (the shipped behaviour): the pause comes after shadow_idle_ms = 2000, probes every 250 ms."""
    o = srun(dict(shadow_test="ok3", shadow_idle_ms="", shadow_probe_ms=""), seconds=20)
    o.wait_state("ACTIVE")
    line = run.wait_log(NO_READER_RX % 250, timeout_s=15)
    assert 2000 <= int(re.search(NO_READER_RX % 250, line).group(1)) < 4000, line
    rate, b0, b1 = gen_rate(o, 2.0)
    assert 2 <= rate <= 6, (rate, b0, b1)
    finish(run)


def test_idle_ms_0_never_pauses(run, srun):
    """shadow_idle_ms=0 (the gate off): copies every map frame with nobody reading, for longer than any idle time."""
    o = srun(dict(shadow_test="ok3", shadow_idle_ms="0"), seconds=12)
    o.wait_state("ACTIVE")
    time.sleep(3.0)
    rate, b0, b1 = gen_rate(o, 1.5)
    assert rate > 50, (rate, b0, b1)
    assert not any("no reader" in ln for ln in slines(run)), slines(run)
    finish(run)


SHORT_HOST_S = 20
HOOK_COMMIT_WAIT_S = 18


@pytest.mark.parametrize("build", exe_golden.BUILDS)
def test_real_path_refuses_the_fake_exe(run, build):
    """No shadow_test: the real path. test_host --fake-exe (of either build, ticket 4964) passes the composite's
    validation but has none of the tap's patterns: FAILED PATTERN before any read, the text naming the first pattern."""
    main = tapcheck.Mapping(run.section, create=4096)
    try:
        main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", shadow_tap="1", shadow_test="")
        run.start_host(*HOST, "--fake-exe-build", build, seconds=SHORT_HOST_S)
        run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
        run.wait_log(r"shadow_tap: REFUSED: pattern 'shadow pass: returns when ShadowRaw .*' at shadow_pass_raw "
                     r"\(VA 0x[0-9A-F]{8}\): (mismatch at byte \d+|unreadable)", timeout_s=10)
        run.wait_log(r"shadow_tap: LOUD: FAILED PATTERN at the install: pattern at shadow_pass_raw", timeout_s=5)
        sec = tapcheck.Mapping(SEC_FMT % run.host.pid)
        b = parse_block(tapcheck.read_block(sec, 0, 320))
        sec.close()
        assert b["stateName"] == "FAILED" and b["failName"] == "PATTERN", b
        assert b["failText"].startswith("PATTERN: pattern at shadow_pass_raw (shadow pass: returns when ShadowRaw"), b
        finish(run)
    finally:
        main.close()


def test_real_path_refuses_an_unvalidated_exe(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", shadow_tap="1", shadow_test="")
    run.start_host("--vsync", "0", "--occluded-sleep", "0", "--load", "after", seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"shadow_tap: REFUSED: the composite did not validate the EXE image", timeout_s=10)
    run.wait_log(r"shadow_tap: LOUD: FAILED PATTERN at the install: EXE not validated by the composite", timeout_s=5)
    finish(run)


def test_unknown_test_mode_is_named(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", shadow_tap="1", shadow_test="bogus")
    run.start_host("--vsync", "0", "--occluded-sleep", "0", "--load", "after", seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"shadow_tap: shadow_test=bogus is not a known mode - ignored", timeout_s=10)
    finish(run)


# ======================================================================================================================
# static

def test_shadow_signatures_match_exe_on_disk():
    """Ticket 4964: the tap's 20 patterns against both builds' golden EXEs, with each build's table values (exe_golden;
    a missing golden copy or local table fails, never skips), and their lengths as 4302 wrote them (the anchors are the
    catalogue's: exebuild/symbols/shell.tsv)."""
    rows = exe_golden.rows("shell/composite/shadow_tap.cpp")
    assert len(rows) == 20
    bad = exe_golden.check_patterns("shell/composite/shadow_tap.cpp")
    assert bad == {b: [] for b in exe_golden.BUILDS}, bad
    # the two lone call / jump rows: their targets, which the tap checks in code (the generator frees the rel32)
    for build in exe_golden.BUILDS:
        v = exe_golden.values(build)
        pe = exe_golden.pe32.PE(str(exe_golden.golden(build)))
        for site, target in (("shadow_pass_tail", "shadow_resample"), ("shadow_render_call", "shadow_pass")):
            rel = struct.unpack("<i", pe.read_rva(v[site] + 1 - pe.image_base, 4))[0]
            assert v[site] + 5 + rel == v[target], (build, site, target)
    lens = {}
    for _f, _name, at, tmpl in rows:
        lens[exe_golden.table.anchor(at)] = len(exe_golden.gen_table.parse_template(tmpl)) + 3 * tmpl.count("R(")
    # 4302's 19 patterns with their lengths (terrain-surface.md 11, S6) and the view-table write (build 4390913 anchors)
    assert lens == {0x523CA6: 7, 0x523CB4: 5, 0x523D97: 8, 0x523DD9: 5, 0x523E2D: 6, 0x523E51: 7, 0x523ED5: 5,
                    0x5580B0: 7, 0x5580F4: 5, 0x52FC4C: 20, 0x52FC98: 5, 0x615B86: 13, 0x615BE2: 6, 0x615BFA: 6,
                    0x4241E6: 13, 0x42456A: 6, 0x73AB06: 6, 0x73AB1B: 8, 0x589524: 5, 0x8BC0CA: 14}, lens
