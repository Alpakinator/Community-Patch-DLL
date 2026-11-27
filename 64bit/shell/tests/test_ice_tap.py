"""Issue 4307 (epic 0169): the ice tap of shell_composite.dll (composite/ice_tap.cpp) with its reader in server.exe
(server/src/render/srv_ice_tap.cpp, run as ice_tap_test.exe `read`, C5_ICE_READER), on the stand-in (hidden desktop,
rule 14). Model: test_water_tap.py.

test_host --fake-exe (the main section is created here) with ice_test=<mode>: a synthetic EXE in the DLL's memory (the
Ice object with its fields, a chunk array of 2 chunks, 2 draw records of material 0x3B, the renderer's buffer tables and
IMMUTABLE D3D11 buffers of known content; ice_tap.cpp BuildTestWorld) runs through the real watch, gather, staging copy,
map, image, section and publish code; the reader takes the image. Checked: the image byte for byte (header, the fields
with their floats and head, both chunks, VB, IB) and the reader's mesh and file form; the ack; an Ice_Reload (the twin
handles) published as generation 2 with the same bytes; an object that changes during the copy (CHANGED, then copied
again); a load without ice (NO_MESH); every hostile object (a null VB handle, a wrong vertex format, sizes that disagree,
an R32 index buffer, a chunk past the IB, a base vertex past the VB, a record of another material, a chunk count out of
range, an index past the VB, a section that cannot be made) -> FAILED with its reason and text, no retry, the reader's
install failure naming the text; a fault on an unmapped read (EXCEPTION, disabled for the process); every one of the 21
byte patterns broken in turn (PATTERN, its name in the text) and all of them matching; the image limit (TOO_BIG); the
kill switch (OFF; and while READY: the section closed); the real path refusing the fake EXE and an unvalidated one; an
unknown test mode named; the shipped ini key ice_tap=1; a static check of the tap's byte patterns against the EXE on disk.

Needs C5_SHELL_BUILD (shell/build.cmd) and C5_ICE_READER = the path of ice_tap_test.exe
(server/tests/render/ice/build_ice_tests.py). A missing binary fails the test (no skip).
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
CTL_FMT = "Local\\VPShellIceTap.%d"
STATES = ["NONE", "WRITING", "READY", "FAILED", "OFF", "NO_MESH"]
FAILS = ["NONE", "PATTERN", "EXCEPTION", "LAYOUT", "DESC", "D3D", "SECTION", "TOO_BIG", "CHANGED", "KILLED"]
MAGIC = 0x43493543
FIELDS_BASE, FIELDS_BYTES = 0xFE80, 0x1A0
N_PATTERNS = 21

# ice_tap.cpp TestFloat: the floats the draw reads, as the synthetic object holds them
TEST_FLOATS = {0xFF0C: 0.18431, 0xFF14: 10.1167, 0xFF18: 25.0, 0xFF1C: -1793.0, 0xFF20: -1098.5, 0xFF24: 1823.9,
               0xFF28: 1043.1, 0xFF38: 0.433, 0xFF3C: -0.25, 0xFF40: 0.866, 0xFF44: 0.0, 0xFF48: -0.4226, 0xFF4C: 0.9063,
               0xFFC0: 25.0, 0xFFC4: 8.0, 0xFFD8: 0.005, 0xFFDC: 0.009, 0xFFE0: 150.0, 0xFFE4: 0.005, 0xFFE8: 0.005,
               0xFFEC: 0.05, 0xFFF0: 0.05, 0xFFF4: 0.0033, 0xFFF8: 0.2758, 0xFFFC: 0.6187, 0x10000: 0.0329, 0x1000C: 0.0329,
               0x10004: 0.3297, 0x10010: 0.3297, 0x10008: 0.145, 0x10014: 0.145, 0x10018: 0.1, 0x1001C: 0.5}


def reader_exe():
    p = os.environ.get("C5_ICE_READER", "")
    assert p and Path(p).exists(), ("C5_ICE_READER must name ice_tap_test.exe "
                                    "(python -I server/tests/render/ice/build_ice_tests.py --out DIR)")
    return p


def reader(run, *args, timeout=90):
    p = subprocess.run([reader_exe(), "read", "--pid", str(run.host.pid), *args], capture_output=True, text=True,
                       timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def parse_block(raw):
    f = struct.unpack_from("<10I", raw, 0)
    d = dict(zip(("seq", "magic", "version", "headerBytes", "state", "failReason", "exePid", "generation", "loadSeq",
                  "chunkCount"), f))
    d["imageBytes"], d["fnv1a64"], d["readyQpc"] = struct.unpack_from("<QQq", raw, 40)
    d["copyMs"], d["copyCpuMs"] = struct.unpack_from("<ff", raw, 64)
    d["copies"], d["vbHandle"], d["ibHandle"] = struct.unpack_from("<3I", raw, 72)
    d["imageName"] = raw[88:216].decode("utf-16-le").split("\0", 1)[0]
    d["failText"] = raw[216:376].split(b"\0", 1)[0].decode("ascii", "replace")
    d["ackGeneration"], d["ackResult"], d["ackServerPid"] = struct.unpack_from("<IiI", raw, 376)
    d["stateName"] = STATES[d["state"]] if d["state"] < len(STATES) else "?"
    d["failName"] = FAILS[d["failReason"]] if d["failReason"] < len(FAILS) else "?"
    return d


def expected_parts():
    vb = bytes((31 * j + 22) & 0xFF for j in range(1280))
    ib = b"".join(struct.pack("<H", (5 * i + 1) % 20 if i < 18 else (5 * i + 2) % 20) for i in range(30))
    chunks = [((-1793.0, 600.0, 1823.9, 1043.1), 6, 0, 0, 5), ((-1793.0, -1098.5, 1823.9, -700.0), 4, 18, 20, 6)]
    return vb, ib, chunks


def check_image(img, *, gen, load, pid, vb_handle=7, ib_handle=9):
    """The file form / section image of the synthetic world, byte for byte (the fields' chunk pointer excepted)."""
    h = struct.unpack_from("<18IQ2I", img, 0)
    (magic, version, hb, cb, g, ld, epid, nch, fb, fbytes, stride, ifmt, vbb, ibb, fo, co, vo, io, total, vh, ih) = h
    assert (magic, version, hb, cb, fb, fbytes, stride, ifmt) == (MAGIC, 1, 112, 32, FIELDS_BASE, FIELDS_BYTES, 8, 57)
    assert (g, ld, epid, nch, vbb, ibb, total, vh, ih) == (gen, load, pid, 2, 1280, 60, len(img), vb_handle, ib_handle)
    assert (fo, co, vo, io) == (112, 528, 592, 1872)
    vb, ib, chunks = expected_parts()
    assert img[vo:vo + vbb] == vb and img[io:io + ibb] == ib
    for k, (box, tris, start, base, rec) in enumerate(chunks):
        got = struct.unpack_from("<4fIIiI", img, co + 32 * k)
        assert got[:4] == pytest.approx(box) and got[4:] == (tris, start, base, rec), got
    f = img[fo:fo + fbytes]
    want = bytearray((7 * i + 3) & 0xFF for i in range(FIELDS_BYTES))
    for off, v in TEST_FLOATS.items():
        struct.pack_into("<f", want, off - FIELDS_BASE, v)
    want[0xFEA5 - FIELDS_BASE] = 1
    struct.pack_into("<II", want, 0xFEE4 - FIELDS_BASE, vb_handle, ib_handle)
    struct.pack_into("<I", want, 0xFF30 - FIELDS_BASE, 2)
    ptr = 0xFF2C - FIELDS_BASE
    assert struct.unpack_from("<I", f, ptr)[0] >= 0x10000          # the chunk array's address in the stand-in
    want[ptr:ptr + 4] = f[ptr:ptr + 4]
    assert f == bytes(want)


class IceRun:
    def __init__(self, run, ini, seconds=20, host=None, main=True, server_pid=0):
        """main=False: no main section (no server: the ice tap's reader gate defers its copy, ticket 0385; make_main()
        creates it later); server_pid: VPShellHeader.serverPid (offset 12; 0 = a section that names no server, as the
        ring hand-over leaves it)."""
        self.run = run
        self.main = None
        if main:
            self.make_main()
        if server_pid:
            self.set_server_pid(server_pid)
        # ice_unread_ms=0 (ticket 0385): these tests offer images nobody reads; the gate's unread rule has tests of its own
        cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", ice_tap="1", ice_settle_frames="5",
                   ice_unread_ms="0",
                   water_tap="0", overlay_tap="0", decal_tap="0", shadow_tap="0")
        cfg.update(ini)
        run.set_ini(**cfg)
        run.start_host(*(host or HOST), seconds=seconds)
        self.ctl = None

    def control(self, timeout_s=15.0):
        t_end = time.time() + timeout_s
        while self.ctl is None:
            try:
                self.ctl = tapcheck.Mapping(CTL_FMT % self.run.host.pid)
            except OSError:
                if time.time() > t_end:
                    raise TimeoutError("ice control section of pid %d not made; log tail %s" % (
                        self.run.host.pid, shellrun.tail(self.run.composite_log, 10)))
                time.sleep(0.05)
        return self.ctl

    def block(self):
        raw = tapcheck.read_block(self.control(), 0, 504)
        return parse_block(raw) if raw else {}

    def wait(self, pred, what, timeout_s=20.0):
        t_end = time.time() + timeout_s
        b = {}
        while time.time() < t_end:
            b = self.block()
            if b and pred(b):
                return b
            time.sleep(0.02)
        raise TimeoutError("ice tap: %s not reached: %s; log tail %s" % (what, b, shellrun.tail(self.run.composite_log, 10)))

    def wait_state(self, state, timeout_s=20.0, gen=None):
        return self.wait(lambda b: b["stateName"] == state and (gen is None or b["generation"] == gen), "state " + state, timeout_s)

    def set_server_pid(self, pid):
        self.main.write(12, struct.pack("<I", pid))

    def make_main(self):
        self.main = tapcheck.Mapping(self.run.section, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))

    def close(self):
        if self.ctl:
            self.ctl.close()
        if self.main:
            self.main.close()


@pytest.fixture
def irun(run):
    made = []

    def make(ini, seconds=20, host=None, **kw):
        o = IceRun(run, ini, seconds, host, **kw)
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


def ilines(run):
    return [ln for ln in run.log_lines() if "ice_tap:" in ln]


def test_tap_synthetic_copied_exactly(run, irun, tmp_path):
    o = irun(dict(ice_test="ok"))
    b = o.wait_state("READY")
    pid = run.host.pid
    assert b["magic"] == MAGIC and b["version"] == 1 and b["headerBytes"] == 504 and b["exePid"] == pid, b
    assert b["generation"] == 1 and b["chunkCount"] == 2 and b["imageBytes"] == 1932 and b["fnv1a64"] and b["copies"] == 1, b
    assert (b["vbHandle"], b["ibHandle"]) == (7, 9) and b["imageName"] == "Local\\VPShellIce.%d.1" % pid, b
    assert b["failName"] == "NONE" and b["failText"] == "" and b["copyMs"] > 0, b
    img = tmp_path / "ice.c5ice"
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--seconds", "10", "--json", str(js), "--save", str(img))
    assert rc == 0, txt
    data = img.read_bytes()
    assert tapcheck.fnv1a64(data) == b["fnv1a64"]                    # the file form = the section's image as is
    check_image(data, gen=1, load=b["loadSeq"], pid=pid)
    r = json.loads(js.read_text())
    vb, ib, _ = expected_parts()
    m = r["mesh"]
    assert r["state"] == "Active" and r["accepted"] == 1 and r["refused"] == 0 and not r["installFailure"], r
    assert m["vbBytes"] == 1280 and m["ibBytes"] == 60 and int(m["vbFnv"], 16) == tapcheck.fnv1a64(vb), m
    assert int(m["ibFnv"], 16) == tapcheck.fnv1a64(ib) and m["triangles"] == 10, m
    assert [(c["triangles"], c["startIndex"], c["baseVertex"]) for c in m["chunks"]] == [(6, 0, 0), (4, 18, 20)], m
    b2 = o.wait(lambda x: x["ackGeneration"] == 1, "ack")
    assert b2["ackResult"] == 0 and b2["ackServerPid"], b2
    run.wait_log(r"ice_tap: generation 1 acked by pid \d+, result 0$", timeout_s=5)
    line = run.wait_log(r"ice_tap: READY generation 1 \(load \d+, copy 1\): VB 1280 bytes, IB 60 bytes, 2 chunks, 10 triangles; "
                        r"image 1932 bytes in 'Local\\VPShellIce\.%d\.1' \(1 views\)" % pid, timeout_s=2)
    assert re.search(r"composite CPU [\d.]+ ms", line), line
    assert tapcheck.exists(b["imageName"])                           # kept after the ack (a later server still reads it)
    # the file form through the reader's own loadFile
    p = subprocess.run([reader_exe(), "file", str(img)], capture_output=True, text=True, creationflags=0x08000000)
    assert p.returncode == 0 and '"triangles": 10' in p.stdout, p.stdout
    finish(run)
    assert not any("FAILED" in ln or "DISABLED" in ln for ln in ilines(run)), ilines(run)


def test_ice_reload_publishes_a_new_generation(run, irun, tmp_path):
    """An Ice_Reload (the stand-in's trigger file): the object's handles change (7/9 -> 8/10), the tap copies again:
    generation 2 of the same load, the same bytes; the previous section is closed."""
    o = irun(dict(ice_test="ok"), seconds=30)
    b1 = o.wait_state("READY", gen=1)
    run.trigger("shell_composite.iceload")
    b2 = o.wait_state("READY", gen=2)
    assert b2["loadSeq"] == b1["loadSeq"] and (b2["vbHandle"], b2["ibHandle"]) == (8, 10) and b2["copies"] == 2, b2
    assert not tapcheck.exists(b1["imageName"]) and tapcheck.exists(b2["imageName"])
    img = tmp_path / "ice2.c5ice"
    rc, txt = reader(run, "--seconds", "10", "--want-gen", "2", "--save", str(img))
    assert rc == 0 and "generation 2" in txt, txt
    check_image(img.read_bytes(), gen=2, load=b1["loadSeq"], pid=run.host.pid, vb_handle=8, ib_handle=10)
    run.wait_log(r"ice_tap: test: simulated Ice_Reload \(1\): VB 0x8, IB 0xA", timeout_s=2)
    finish(run)


def test_object_changed_during_the_copy_is_copied_again(run, irun):
    o = irun(dict(ice_test="change"))
    b = o.wait_state("READY")
    assert b["generation"] == 1 and b["copies"] == 2 and (b["vbHandle"], b["ibHandle"]) == (8, 10), b
    run.wait_log(r"ice_tap: FAILED CHANGED \(load \d+, copy 1\): the Ice object changed during the copy", timeout_s=2)
    assert not any("DISABLED" in ln for ln in ilines(run))
    finish(run)


def test_load_without_ice_publishes_no_mesh(run, irun, tmp_path):
    o = irun(dict(ice_test="nobuilt"), seconds=12)
    b = o.wait_state("NO_MESH")
    assert b["generation"] == 0 and b["imageName"] == "" and b["copies"] == 0 and b["failName"] == "NONE", b
    run.wait_log(r"ice_tap: NO_MESH \(load \d+\): built flag 0, 2 chunks - this load has no ice mesh", timeout_s=2)
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--seconds", "1", "--json", str(js))
    r = json.loads(js.read_text())
    assert rc == 4 and r["noMesh"] and r["state"] == "Waiting" and not r["installFailure"] and r["mesh"] is None, txt
    finish(run)


@pytest.mark.parametrize("mode,fail,text", [
    ("nullvb", "LAYOUT", r"VB handle 0x0 out of range \(1\.\.0x17FF\)"),
    ("badvfmt", "DESC", r"VB handle 0x7: vertex format not 0x16 \(EXE size 1280, D3D size 1280, bind 0x1, usage 1, vertex format 23"),
    ("badsize", "DESC", r"VB handle 0x7: the EXE's size differs from the buffer's \(EXE size 1288, D3D size 1280"),
    ("ibformat", "DESC", r"IB handle 0x9: index format not R16 or size not whole indices \(EXE size 60, D3D size 60, bind 0x2, usage 1, format 42"),
    ("chunkpast", "LAYOUT", r"chunk 1: 4 triangles from index 22, the index buffer holds 30 indices"),
    ("basepast", "LAYOUT", r"chunk 1: base vertex 160, the vertex buffer holds 160 vertices"),
    ("badrecord", "LAYOUT", r"chunk 1: draw record 0x6: material 58 IB 0x9 VB 0x7, expected 59 / 0x9 / 0x7"),
    ("hugecount", "LAYOUT", r"chunk array out of range: 100000 chunks at 0x[0-9A-F]{8} \(at most 4096\)"),
    ("badindex", "LAYOUT", r"chunk 1: index 25 = 140 \+ base 20, the vertex buffer holds 160 vertices"),
    ("badsection", "SECTION", r"CreateFileMapping\('Local\\VPShellIce\\no\\such\\1', 65536 bytes\) failed"),
])
def test_hostile_objects_fail_with_their_reason(run, irun, mode, fail, text):
    """A failed check fails this object state only: FAILED with its reason and text (D125's input), no image, no retry
    while the object stays the same; the reader reports the install failure with the producer's text; the host runs on."""
    o = irun(dict(ice_test=mode))
    b = o.wait_state("FAILED")
    assert b["failName"] == fail and b["imageName"] == "" and b["generation"] == 0, b
    assert re.search(text, b["failText"]), b["failText"]
    run.wait_log(r"ice_tap: FAILED %s \(load \d+, copy 1\): .*%s" % (fail, text), timeout_s=5)
    rc, txt = reader(run, "--seconds", "3")
    assert rc == 1 and "state Failed" in txt and "the ice tap failed: " in txt, txt
    time.sleep(1.0)
    assert o.block()["copies"] == 1 and not any("DISABLED" in ln for ln in ilines(run))   # no retry, not disabled
    finish(run)


def test_fault_disables_for_good(run, irun):
    """The chunk array pointer leads into reserved, uncommitted memory: the SEH guard catches the fault, the tap is off
    for the life of the process (EXCEPTION) and an Ice_Reload does not start it again."""
    o = irun(dict(ice_test="garbage"))
    b = o.wait_state("FAILED")
    assert b["failName"] == "EXCEPTION" and "fault reading 40 chunk bytes at 0x" in b["failText"], b
    run.wait_log(r"ice_tap: FAILED EXCEPTION \(load \d+, copy 1\): fault reading 40 chunk bytes at 0x[0-9A-F]+ - DISABLED for the "
                 r"life of the process", timeout_s=5)
    run.trigger("shell_composite.iceload")
    time.sleep(1.5)
    assert o.block()["copies"] == 1 and not any("simulated Ice_Reload" in ln for ln in ilines(run))
    finish(run)


def fake_build(build):
    """test_host's options for a fake EXE of this build (ticket 4964)."""
    return ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe", "--fake-exe-build", build)


# Ticket 4964: every pattern on build 4390913's image; on 26577625's the first, one with an operand, the last (and the
# trailing-call rows, whose free rel32 the corruption skips: 0, 3, 8).
@pytest.mark.parametrize("k,build", [(k, "4390913") for k in range(N_PATTERNS)] +
                         [(k, "26577625") for k in (0, 1, 3, 8, N_PATTERNS - 1)])
def test_each_pattern_refuses_when_broken(run, irun, k, build):
    o = irun(dict(ice_test="patterns", ice_test_pattern=str(k)), seconds=10, host=fake_build(build))
    b = o.wait_state("FAILED")
    assert b["failName"] == "PATTERN" and b["copies"] == 0, b
    assert re.match(r"pattern %d at [a-z0-9_]+ \(VA 0x[0-9A-F]{8}\): mismatch at byte \d+$" % k, b["failText"]), b["failText"]
    run.wait_log(r"ice_tap: pattern %d: pattern '.+' at [a-z0-9_]+ \(VA" % k, timeout_s=3)
    run.wait_log(r"ice_tap: REFUSED: pattern %d at .* - the tap never reads this EXE" % k, timeout_s=3)
    run.wait_log(r"ice_tap: FAILED PATTERN .* - DISABLED for the life of the process", timeout_s=3)
    finish(run)


@pytest.mark.parametrize("build", ("4390913", "26577625"))
def test_all_patterns_matching_lets_the_tap_run(run, irun, build):
    o = irun(dict(ice_test="patterns", ice_test_pattern="-1"), host=fake_build(build))
    b = o.wait_state("READY")
    assert b["generation"] == 1 and b["failName"] == "NONE", b
    finish(run)


def test_image_limit(run, irun):
    o = irun(dict(ice_test="ok", ice_max_kb="1"))
    b = o.wait_state("FAILED")
    assert b["failName"] == "TOO_BIG" and b["failText"] == "image of 1932 bytes, the limit is 1024", b
    finish(run)


def test_kill_switch_publishes_off(run, irun):
    o = irun(dict(ice_tap="0", ice_test="ok"), seconds=8)
    b = o.wait_state("OFF")
    assert b["copies"] == 0 and b["generation"] == 0, b
    run.wait_log(r"ice_tap: OFF \(ice_tap=0\)", timeout_s=2)
    time.sleep(1.0)
    assert not any("copy" in ln for ln in ilines(run))
    finish(run)


def test_kill_switch_while_ready_closes_the_image(run, irun, tmp_path):
    o = irun(dict(ice_test="ok"), seconds=20)
    b = o.wait_state("READY")
    run.set_ini(ice_tap="0")
    o.wait_state("OFF", timeout_s=5)
    assert not tapcheck.exists(b["imageName"])
    js = tmp_path / "r.json"
    rc, txt = reader(run, "--seconds", "1", "--json", str(js))
    assert rc == 4 and json.loads(js.read_text())["state"] == "Off", txt
    run.set_ini(ice_tap="1")                                          # on again: copied again, a new generation
    b2 = o.wait_state("READY", timeout_s=8)
    assert b2["generation"] == 2, b2
    finish(run)


# ======================================================================================================================
# ticket 0385: the reader gate (no copy while nothing can read it) and the worker that builds the image off the render
# thread (0384)

@pytest.mark.parametrize("worker", ["1", "0"])
def test_ready_line_names_the_render_thread_share_and_the_builder(run, irun, tmp_path, worker):
    """0384: the image is built, hashed and written by a worker thread (ice_worker=1, the default) or, with ice_worker=0
    (the A/B's base arm: the cost before 0385), on the render thread in the Present whose maps succeed. The READY line
    names the render thread's total, its most expensive single Present, the build's time and where it ran; the published
    copyCpuMs is the render thread's total; the image is the same either way (the reader takes it)."""
    o = irun(dict(ice_test="ok", ice_worker=worker))
    b = o.wait_state("READY")
    line = run.wait_log(r"ice_tap: READY generation 1 ", timeout_s=2)
    m = re.search(r"wall ([\d.]+) ms over (\d+) Presents, composite CPU ([\d.]+) ms \(most in one Present ([\d.]+) ms\), "
                  r"image built in ([\d.]+) ms (by the worker|on the render thread)$", line)
    assert m, line
    total, most = float(m.group(3)), float(m.group(4))
    assert 0 < most <= total + 1e-3, line
    if worker == "1":
        assert int(m.group(2)) >= 3 and m.group(6) == "by the worker", line     # gather, map (worker started), collect
    else:
        assert int(m.group(2)) >= 2 and m.group(6) == "on the render thread", line
    assert b["copyCpuMs"] == pytest.approx(total, abs=2e-3), (b, line)
    img = tmp_path / "ice.c5ice"
    rc, txt = reader(run, "--seconds", "10", "--save", str(img))
    assert rc == 0, txt
    check_image(img.read_bytes(), gen=1, load=b["loadSeq"], pid=run.host.pid)
    finish(run)


def test_no_server_section_no_copy(run, irun, tmp_path):
    """No main section (no server has made one): the composite publishes no camera (it has nowhere to), so no frame is a
    map frame and the settled mesh is never asked for: no copy, state NONE, no gate line. Once a section exists (without
    magic, as the ring hand-over's: the composite opens it within a second) the camera runs and the copy follows; the
    reader takes it."""
    o = irun(dict(ice_test="ok"), seconds=30, main=False)
    o.control()
    time.sleep(4.0)
    b = o.block()
    assert b["stateName"] == "NONE" and b["copies"] == 0 and b["generation"] == 0, b
    assert not any("copy" in ln or "deferred" in ln for ln in ilines(run)), ilines(run)
    o.main = tapcheck.Mapping(run.section, create=4096)
    b = o.wait_state("READY", timeout_s=10)
    assert b["generation"] == 1 and b["copies"] == 1, b
    rc, txt = reader(run, "--seconds", "10", "--json", str(tmp_path / "r.json"))
    assert rc == 0, txt
    o.wait(lambda x: x["ackGeneration"] == 1 and x["ackResult"] == 0, "ack")
    finish(run)


@pytest.mark.parametrize("opens_by,pid", [("ack", 4242), ("new_server", 4242), ("ack", 0)])
def test_unread_image_defers_the_next_copy(run, irun, tmp_path, opens_by, pid):
    """A server (pid 4242 in the section, or 0: a section that names none, as the ring hand-over leaves it) is offered
    generation 1 and acks nothing for ice_unread_ms (it runs without the ice reader): the next object state (an
    Ice_Reload) is not copied; generation 1 stays published (its server may still ack it). The gate opens when that image
    is acked (the reader acks it), or for another server process (pid 5151 in the section): the deferred copy starts and
    publishes generation 2."""
    o = irun(dict(ice_test="ok", ice_unread_ms="1000"), seconds=40, server_pid=pid)
    b1 = o.wait_state("READY", gen=1)
    time.sleep(1.5)
    run.trigger("shell_composite.iceload")
    who = r"the server \(pid 4242\)" if pid else r"the server"
    run.wait_log(r"ice_tap: copy of load \d+ deferred \(the reader gate\): %s has not acked generation 1 in "
                 r"\d+ ms \(ack 0; it reads no ice, render_ice=0\?\); copied when it acks, or for another server process$" % who,
                 timeout_s=10)
    time.sleep(1.0)
    b = o.block()
    assert b["stateName"] == "READY" and b["generation"] == 1 and b["copies"] == 1, b
    assert tapcheck.exists(b1["imageName"])
    if opens_by == "ack":
        rc, txt = reader(run, "--seconds", "10", "--json", str(tmp_path / "r1.json"))
        assert rc == 0 and json.loads((tmp_path / "r1.json").read_text())["accepted"] == 1, txt
    else:
        o.set_server_pid(5151)
    b2 = o.wait_state("READY", gen=2, timeout_s=10)
    assert b2["copies"] == 2 and (b2["vbHandle"], b2["ibHandle"]) == (8, 10), b2
    run.wait_log(r"ice_tap: deferred copy of load \d+ starts: the reader gate is open$", timeout_s=2)
    rc, txt = reader(run, "--seconds", "10", "--want-gen", "2", "--json", str(tmp_path / "r2.json"))
    assert rc == 0 and "generation 2" in txt, txt
    finish(run, seconds=80)   # a 40 s host whose start took long on a loaded machine under ASan (2026-11-16 07:13)


def test_unread_ms_0_never_defers(run, irun):
    """ice_unread_ms=0: the gate's second part is off; an unread image does not hold back the next copy."""
    o = irun(dict(ice_test="ok", ice_unread_ms="0"), seconds=30, server_pid=4242)
    o.wait_state("READY", gen=1)
    time.sleep(1.5)
    run.trigger("shell_composite.iceload")
    b2 = o.wait_state("READY", gen=2, timeout_s=10)
    assert b2["copies"] == 2, b2
    assert not any("deferred" in ln for ln in ilines(run)), ilines(run)
    finish(run)


def test_unread_rule_waits_for_ice_unread_ms(run, irun):
    """Before ice_unread_ms has passed since the offer, an unacked image does not hold back the next copy (a server
    still loading may read it late)."""
    o = irun(dict(ice_test="ok", ice_unread_ms="60000"), seconds=30, server_pid=4242)
    o.wait_state("READY", gen=1)
    run.trigger("shell_composite.iceload")
    o.wait_state("READY", gen=2, timeout_s=10)
    assert not any("deferred" in ln for ln in ilines(run)), ilines(run)
    finish(run)


# Bug 0120: refusals come at the first Present after the hook commit, which can take seconds under load (test_bake_tap.py)
SHORT_HOST_S = 20
HOOK_COMMIT_WAIT_S = 18


@pytest.mark.parametrize("build", ("4390913", "26577625"))
def test_real_path_refuses_the_fake_exe(run, irun, build):
    """No ice_test: the real path. test_host --fake-exe (of either build, ticket 4964) passes the composite's validation
    but has none of the tap's patterns: the tap refuses before any read and publishes FAILED PATTERN with the first
    pattern's name."""
    o = irun(dict(ice_test=""), seconds=SHORT_HOST_S, host=fake_build(build))
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    b = o.wait_state("FAILED", timeout_s=10)
    assert b["failName"] == "PATTERN", b
    assert re.fullmatch(r"pattern 0 at ice_createterrain \(VA 0x[0-9A-F]{8}\): (mismatch at byte \d+|unreadable)( \(fault\))?",
                        b["failText"]), b["failText"]
    run.wait_log(r"ice_tap: pattern 0: pattern 'CreateTerrain: the Ice object TerrainSystem\+0x529BC passed to the Ice step' "
                 r"at ice_createterrain", timeout_s=3)
    finish(run)


def test_real_path_refuses_an_unvalidated_exe(run, irun):
    o = irun(dict(ice_test=""), seconds=SHORT_HOST_S, host=("--vsync", "0", "--occluded-sleep", "0", "--load", "after"))
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    b = o.wait_state("FAILED", timeout_s=10)
    assert b["failName"] == "PATTERN" and b["failText"] == "the composite did not validate the EXE image", b
    finish(run)


def test_unknown_test_mode_is_named(run, irun):
    irun(dict(ice_test="bogus"), seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"ice_tap: ice_test=bogus is not a known mode - ignored", timeout_s=10)
    finish(run)


# ======================================================================================================================
# static

def test_shipped_ini_and_default_hold_the_tap_on():
    text = (shellrun.SHELL / "composite" / "shell_composite.ini").read_text(encoding="utf-8")
    assert re.search(r"^ice_tap=1$", text, re.M), "the shipped ini must hold ice_tap=1 (4308 binds the ice; lead-0169)"
    src = (shellrun.SHELL / "composite" / "shell_composite.cpp").read_text(encoding="utf-8")
    assert re.search(r"int iceTap = 1;", src), "the built-in default equals the shipped ini since 0490"


def test_ice_signatures_match_exe_on_disk():
    """Ticket 4964: the tap's patterns against both builds' golden EXEs, with each build's table values (exe_golden; a
    missing golden copy or local table fails, never skips)."""
    assert len(exe_golden.rows("shell/composite/ice_tap.cpp")) == N_PATTERNS
    bad = exe_golden.check_patterns("shell/composite/ice_tap.cpp")
    assert bad == {b: [] for b in exe_golden.BUILDS}, bad
