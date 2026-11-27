"""Issue 3603 (epic 3600): the water mesh tap of shell_composite.dll (composite/water_tap.cpp) and its reader in
server.exe (server/src/render/srv_water_tap.cpp, run as server/tests/render/water/water_reader_test.exe), on the
stand-in (hidden desktop, rule 14).

Two kinds of tests, as test_tap.py:
- The reader on sections crafted here (no host): one broken piece each; the exit code, the printed reason and the ack
  written back into the block are asserted (stale, open, truncated, magic, version, record size, image/block mismatch,
  too many records, data outside the image, VB and IB sizes, record and image hashes, a block without a hash, draws
  whose buffers are wrong or whose indices leave the VB, map size, a WRITING block, the requestSeq step, usage).
- The DLL (test_host --fake-exe; the main section is created here): water_test=<mode> feeds a synthetic EXE (river,
  Water and Wave_System objects, the renderer's handle tables, IMMUTABLE D3D11 buffers of known content; water_tap.cpp
  BuildTestWorld) through the real gather, staging copy, map, image and ack code; the reader takes the copy. Hostile
  objects fail the load (LAYOUT) or the process (EXCEPTION) with their reason; the host stays alive and the composite
  is not disabled. Plus: the ack timeout, a new load superseding an unacknowledged copy (and the stale refusal of the
  next), requestSeq, the kill switch (before and during a copy), the image limit, a section that cannot be created, the
  real path refusing the fake EXE and an unvalidated one, and a static check of the tap's byte patterns against the EXE.

Needs C5_SHELL_BUILD (shell/build.cmd) and C5_WATER_READER = the path of water_reader_test.exe
(server/tests/render/water/build_water_tests.py). A missing binary fails the test (no skip).
"""
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
WT_OFFSET, WT_SIZE = 3584, 320
STATES = ["NONE", "WRITING", "READY", "RELEASED", "FAILED", "OFF"]
FAILS = ["NONE", "REFUSED", "EXCEPTION", "LAYOUT", "SECTION", "D3D", "TOO_BIG", "ACK_TIMEOUT", "CHANGED", "KILLED"]
MAGIC_W, VERSION_W, HDR_BYTES, REC_BYTES = 0x52544157, 1, 120, 128
ERR_TRUNC, ERR_LAYOUT = 2, 5
ACK_HASH, ACK_OPEN, ACK_STALE, ACK_VERSION = -1, -2, -3, -4


def reader_exe():
    p = os.environ.get("C5_WATER_READER", "")
    assert p and Path(p).exists(), ("C5_WATER_READER must name water_reader_test.exe "
                                    "(python -I server/tests/render/water/build_water_tests.py --out DIR)")
    return p


def reader(section, *args, timeout=60):
    p = subprocess.run([reader_exe(), "--section", section, *args], capture_output=True, text=True, timeout=timeout,
                       creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def parse_block(raw):
    f = struct.unpack_from("<8I", raw, 0)
    d = dict(seq=f[0], state=f[1], loadSeq=f[2], failReason=f[3], exePid=f[4], copySeq=f[5], recordCount=f[6], systems=f[7])
    d["imageBytes"], d["sectionBytes"], d["fnv1a64"], d["readyQpc"] = struct.unpack_from("<QQQq", raw, 32)
    d["copyMs"] = struct.unpack_from("<f", raw, 64)[0]
    d["sectionName"] = raw[72:200].decode("utf-16-le").split("\0", 1)[0]
    d["ackLoadSeq"], d["ackResult"], d["ackServerPid"], d["requestSeq"], d["ackCopySeq"] = struct.unpack_from("<IiIII", raw, 200)
    d["stateName"] = STATES[d["state"]] if d["state"] < len(STATES) else "?"
    d["failName"] = FAILS[d["failReason"]] if d["failReason"] < len(FAILS) else "?"
    return d


# ======================================================================================================================
# images built here

def record(system, role, index=0, flags=0, off=0, nbytes=0, fnv=0, stride=0, count=0, fmt=0, topology=0, vb=0xFFFFFFFF,
           ib=0xFFFFFFFF, vcount=0, icount=0, start=0, base=0, material=0, pas=0):
    r = bytearray(REC_BYTES)
    struct.pack_into("<4IQQQ6I", r, 0, system, role, index, flags, off, nbytes, fnv, stride, count, fmt, 0, 0, 0)
    struct.pack_into("<6IiIII", r, 64, topology, vb, ib, vcount, icount, start, base, 0, material, pas)
    return r


def build_image(*, load=1, pid=1234, map_wh=(2, 2), water_z=10.117, vb_stride=16, verts=4, idx=None, idx_fmt=42,
                obj=b"\x01" * 48, mutate=None):
    """header + 4 records (an OBJECT, a VB, an IB, a DRAW) + data; mutate(recs, data_blocks) may break something."""
    idx = idx if idx is not None else [0, 1, 2, 2, 1, 3]
    vbd = bytes((7 * j + 3) & 0xFF for j in range(vb_stride * verts))
    ibd = b"".join(struct.pack("<I" if idx_fmt == 42 else "<H", v) for v in idx)
    datas = [obj, vbd, ibd, b""]
    recs = [record(1, 5, count=0), record(1, 1, stride=vb_stride, count=verts), record(1, 2, fmt=idx_fmt, count=len(idx)),
            record(1, 6, topology=4, vb=1, ib=2, vcount=verts, icount=len(idx), material=91, pas=1)]
    if mutate:
        mutate(recs, datas)
    off = HDR_BYTES + REC_BYTES * len(recs)
    body = bytearray()
    for r, d in zip(recs, datas):
        if d:
            off = (off + 15) & ~15
            if struct.unpack_from("<Q", r, 16)[0] == 0:
                struct.pack_into("<Q", r, 16, off)
            if struct.unpack_from("<Q", r, 24)[0] == 0:
                struct.pack_into("<Q", r, 24, len(d))
            if struct.unpack_from("<Q", r, 32)[0] == 0:
                struct.pack_into("<Q", r, 32, tapcheck.fnv1a64(d))
            off += len(d)
    total = off
    img = bytearray(total)
    hdr = struct.pack("<9IfQ", MAGIC_W, VERSION_W, HDR_BYTES, REC_BYTES, len(recs), load, pid, map_wh[0], map_wh[1], water_z, total)
    img[0:len(hdr)] = hdr
    for i, r in enumerate(recs):
        img[HDR_BYTES + REC_BYTES * i: HDR_BYTES + REC_BYTES * (i + 1)] = r
    for r, d in zip(recs, datas):
        if d:
            o = struct.unpack_from("<Q", r, 16)[0]
            if o + len(d) <= total:
                img[o:o + len(d)] = d
    return bytes(img)


class Crafted:
    """A main section (camera block with grid constants, a water tap block) and a water image section, made here."""

    def __init__(self, tag, image, *, grid_seq=1, cam_samples=(128, 128), state=2, load=1, copy=1, pid=1234,
                 fnv=None, section_bytes=None, block_bytes=None, block_records=None, no_section=False):
        self.name = "Local\\VPShellWTest_%d_%s" % (os.getpid(), tag)
        self.main = tapcheck.Mapping(self.name, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        cam = tapcheck.CAMERA_OFFSET
        self.main.write(cam + tapcheck.CAM_GRIDSEQ, struct.pack("<II", grid_seq, 1))
        self.main.write(cam + tapcheck.CAM_SAMPLESX, struct.pack("<II", *cam_samples))
        self.main.write(cam, struct.pack("<I", 2))
        self.secname = "Local\\VPShellWTestData_%d_%s" % (os.getpid(), tag)
        self.sec = None
        if not no_section:
            size = section_bytes or len(image)
            self.sec = tapcheck.Mapping(self.secname, create=size)
            self.sec.write(0, image[:size])
        nrec = struct.unpack_from("<I", image, 16)[0] if len(image) >= 20 else 0
        b = bytearray(WT_SIZE)
        struct.pack_into("<8I", b, 0, 2, state, load, 0, pid, copy, block_records if block_records is not None else nrec, 2)
        struct.pack_into("<QQQq", b, 32, block_bytes if block_bytes is not None else len(image),
                         (len(image) + 0xFFFF) & ~0xFFFF, tapcheck.fnv1a64(image) if fnv is None else fnv, 1)
        nm = self.secname.encode("utf-16-le")
        b[72:72 + len(nm)] = nm
        self.main.write(WT_OFFSET, bytes(b))

    def block(self):
        return parse_block(self.main.read(WT_OFFSET, WT_SIZE))

    def close(self):
        if self.sec:
            self.sec.close()
        self.main.close()


@pytest.fixture
def crafted(request):
    made = []

    def make(image, **kw):
        c = Crafted(request.node.name[-20:].replace("[", "_").replace("]", ""), image, **kw)
        made.append(c)
        return c
    yield make
    for c in made:
        c.close()


def test_reader_accepts_a_good_image(crafted, tmp_path):
    c = crafted(build_image())
    js = tmp_path / "r.json"
    rc, txt = reader(c.name, "--json", str(js))
    assert rc == 0, txt
    import json
    r = json.loads(js.read_text())
    assert r["result"] == 0 and r["accepted"] == 1 and r["loadSeq"] == 1
    assert [x["role"] for x in r["records"]] == [5, 1, 2, 6]
    assert all(x["fnv1a64"] == x["dataFnvCheck"] for x in r["records"] if x["dataBytes"])
    assert r["image"]["waterZ"] == pytest.approx(10.117) and r["image"]["mapW"] == 2
    b = c.block()
    assert (b["ackLoadSeq"], b["ackCopySeq"], b["ackResult"], b["ackServerPid"]) == (1, 1, 0, b["ackServerPid"]) and b["ackServerPid"]
    assert "hashes ok" in txt


def _set(rec_i, off, fmt, val):
    def m(recs, datas):
        struct.pack_into(fmt, recs[rec_i], off, val)
    return m


@pytest.mark.parametrize("case,kw,code,text", [
    ("stale", dict(grid_seq=2), ACK_STALE, "stale: the camera block's grid seq is 2, the tap's load seq 1"),
    ("open", dict(no_section=True), ACK_OPEN, "cannot open section"),
    ("truncated", dict(block_bytes=10000), ERR_TRUNC, "image 10000 bytes, section view 4096 bytes"),
    ("magic", dict(image="magic"), ACK_VERSION, "magic 0x00000000"),
    ("version", dict(image="version"), ACK_VERSION, "version 2"),
    ("recordsize", dict(image="recordsize"), ACK_VERSION, "record 96 bytes"),
    ("blockcount", dict(block_records=3), ERR_LAYOUT, "4 records, load 1, pid 1234; the block"),
    ("toomany", dict(block_records=5000), ERR_LAYOUT, "announces 5000 records"),
    ("outside", dict(image=dict(mutate=_set(1, 16, "<Q", 1 << 20))), ERR_TRUNC, "record 1: data 1048576"),
    ("vbsize", dict(image=dict(mutate=_set(1, 44, "<I", 5))), ERR_LAYOUT, "record 1: VB stride 16 x 5 vertices"),
    ("ibformat", dict(image=dict(mutate=_set(2, 48, "<I", 41))), ERR_LAYOUT, "record 2: IB format 41"),
    ("rechash", dict(image=dict(mutate=_set(1, 32, "<Q", 0x1234))), ACK_HASH, "record 1: FNV-1a 64"),
    ("imghash", dict(fnv=0x5555), ACK_HASH, "image FNV-1a 64"),
    ("nohash", dict(fnv=0), ACK_HASH, "no image hash"),
    ("drawrefs", dict(image=dict(mutate=_set(3, 68, "<I", 2))), ERR_LAYOUT, "draw record 3: VB record 2, IB record 2"),
    ("drawindex", dict(image=dict(idx=[0, 1, 2, 2, 1, 4])), ERR_LAYOUT, "index 5 = 4 (base 0) outside its 4 vertices"),
    ("drawcount", dict(image=dict(mutate=_set(3, 80, "<I", 9))), ERR_LAYOUT, "indices 0 + 9 of 6"),
    ("mapsize", dict(cam_samples=(192, 128)), ERR_LAYOUT, "map size: the image says 2x2 plots, the camera block's grid 3x2"),
])
def test_reader_refusals(crafted, case, kw, code, text):
    """Every check of the reader: the copy is refused with its code, the ack carries it (the DLL may close the section)."""
    kw = dict(kw)
    img = kw.pop("image", None)
    if img == "magic":
        image = bytearray(build_image()); struct.pack_into("<I", image, 0, 0); image = bytes(image)
    elif img == "version":
        image = bytearray(build_image()); struct.pack_into("<I", image, 4, 2); image = bytes(image)
    elif img == "recordsize":
        image = bytearray(build_image()); struct.pack_into("<I", image, 12, 96); image = bytes(image)
    elif isinstance(img, dict):
        image = build_image(**img)
    else:
        image = build_image()
    c = crafted(image, **kw)
    rc, txt = reader(c.name, "--seconds", "3")
    assert rc == 1, txt
    assert "last result %d" % code in txt and text in txt, txt
    b = c.block()
    assert (b["ackLoadSeq"], b["ackCopySeq"], b["ackResult"]) == (1, 1, code), b


def test_reader_ignores_a_block_that_is_not_ready(crafted):
    c = crafted(build_image(), state=1)
    rc, txt = reader(c.name, "--seconds", "1")
    assert rc == 4 and "accepted 0 refused 0" in txt, txt
    assert c.block()["ackLoadSeq"] == 0


def test_reader_requests_a_new_copy_of_a_released_load(crafted):
    """Step 4: RELEASED for the camera's load and this reader has none of it: requestSeq + 1, once per copy."""
    c = crafted(build_image(), state=3)
    rc, txt = reader(c.name, "--seconds", "1.5")
    assert rc == 4 and "requests 1" in txt and "new copy requested (requestSeq 1)" in txt, txt
    assert c.block()["requestSeq"] == 1


def test_reader_usage_and_missing_section():
    p = subprocess.run([reader_exe()], capture_output=True, text=True, creationflags=0x08000000)
    assert p.returncode == 2 and "--section required" in p.stdout
    p = subprocess.run([reader_exe(), "--bogus"], capture_output=True, text=True, creationflags=0x08000000)
    assert p.returncode == 2 and "unknown" in p.stdout
    rc, txt = reader("Local\\VPShellWTestNoSuch_%d" % os.getpid(), "--seconds", "0.5")
    assert rc == 3 and "does not open" in txt, txt


# ======================================================================================================================
# the DLL on the stand-in

def expected_records():
    """The synthetic EXE's records (water_tap.cpp BuildTestWorld) in the tap's order: (system, role, bytes, data or None)."""
    def vb(bid, n):
        return bytes((31 * j + 17 * bid + 5) & 0xFF for j in range(n))

    def ib(bid, count, verts, e):
        return b"".join(struct.pack("<I" if e == 4 else "<H", (5 * i + bid) % verts) for i in range(count))
    out = [(1, 5, 48, None), (1, 5, 128, None), (1, 1, 384, vb(1, 384)), (1, 2, 72, ib(2, 18, 12, 4)), (1, 6, 0, None),
           (2, 5, 0xEC, bytes(i ^ 0x5A for i in range(0xEC))), (3, 5, 0xB0, None), (3, 5, 72, None),
           (3, 5, 48, bytes((i * 3 + 1) & 0xFF for i in range(48)))]
    for k in range(2):
        out += [(3, 5, 128, None), (3, 1, 96, vb(3 + 2 * k, 96)), (3, 2, 24, ib(4 + 2 * k, 12, 8, 2)), (3, 6, 0, None)]
    return out


class WaterRun:
    def __init__(self, run, ini, seconds=20, host=None):
        self.run = run
        self.main = tapcheck.Mapping(run.section, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", water_tap="1",
                   water_delay_frames="5", water_scene_frames="0", water_ack_timeout_ms="20000")
        cfg.update(ini)
        run.set_ini(**cfg)
        run.start_host(*(host or HOST), seconds=seconds)

    def block(self):
        raw = tapcheck.read_block(self.main, WT_OFFSET, WT_SIZE)
        return parse_block(raw) if raw else {}

    def wait_state(self, state, timeout_s=20.0, load=None, copy=None):
        t_end = time.time() + timeout_s
        b = {}
        while time.time() < t_end:
            b = self.block()
            if b and b["stateName"] == state and (load is None or b["loadSeq"] == load) and (copy is None or b["copySeq"] == copy):
                return b
            time.sleep(0.02)
        raise TimeoutError("water tap state %s not reached: %s; log tail %s" % (state, b, shellrun.tail(self.run.composite_log, 8)))

    def close(self):
        self.main.close()


@pytest.fixture
def wrun(run):
    made = []

    def make(ini, seconds=20, host=None):
        w = WaterRun(run, ini, seconds, host)
        made.append(w)
        return w
    yield make
    for w in made:
        w.close()


def finish(run, seconds=40):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert rows and not any(r["disabled"] for r in rows)
    assert sum(r["frames"] for r in rows) > 0


def wlines(run):
    return [ln for ln in run.log_lines() if "water_tap:" in ln]


def test_tap_synthetic_copied_exactly_and_released(run, wrun, tmp_path):
    import json
    w = wrun(dict(water_test="ok"))
    ready = w.wait_state("READY")
    assert re.fullmatch(r"Local\\VPShellWater\.%d\.%d\.1" % (run.host.pid, ready["loadSeq"]), ready["sectionName"]), ready
    assert tapcheck.exists(ready["sectionName"])
    assert ready["recordCount"] == 17 and ready["systems"] == (1 << 1) | (1 << 3) and ready["fnv1a64"] and ready["copyMs"] > 0
    js = tmp_path / "r.json"
    rc, txt = reader(run.section, "--json", str(js), "--seconds", "10")
    assert rc == 0, txt
    rel = w.wait_state("RELEASED")
    assert rel["fnv1a64"] == ready["fnv1a64"]
    run.wait_log(r"water_tap: RELEASED load %d copy 1: ack from pid \d+, result 0, " % ready["loadSeq"], timeout_s=5)
    assert not tapcheck.exists(ready["sectionName"])        # the DLL's handle was the last one
    r = json.loads(js.read_text())
    got = [(x["system"], x["role"], x["dataBytes"]) for x in r["records"]]
    want = expected_records()
    assert got == [(s, ro, n) for s, ro, n, _ in want], got
    for x, (_, _, _, data) in zip(r["records"], want):
        if data is not None:
            assert int(x["fnv1a64"], 16) == tapcheck.fnv1a64(data), x
    river_vb, river_ib, river_draw = r["records"][2], r["records"][3], r["records"][4]
    assert (river_vb["stride"], river_vb["count"], river_vb["flags"] & 1) == (32, 12, 1)
    assert (river_ib["dxgiFormat"], river_ib["count"], river_ib["flags"] & 5) == (42, 18, 5)
    assert (river_draw["vbRecord"], river_draw["ibRecord"], river_draw["indexCount"], river_draw["material"], river_draw["pass"]) == (2, 3, 18, 91, 1)
    wave_draw = r["records"][12]
    assert (wave_draw["vbRecord"], wave_draw["ibRecord"], wave_draw["indexCount"], wave_draw["material"]) == (10, 11, 12, 92)
    assert r["records"][8]["flags"] == 2     # the wave instances: changed by the EXE every frame (copied once)
    assert r["image"]["waterZ"] == pytest.approx(10.117)
    line = run.wait_log(r"water_tap: READY load \d+ copy 1: 17 records \(3 VB, 3 IB, 3 draws", timeout_s=2)
    m = re.search(r"wall ([\d.]+) ms over (\d+) Presents, composite CPU ([\d.]+) ms", line)
    assert m and float(m.group(3)) > 0, line
    finish(run)
    assert not any("DISABLED" in ln or "FAILED" in ln for ln in wlines(run)), wlines(run)


def test_tap_without_rivers(run, wrun, tmp_path):
    w = wrun(dict(water_test="noriver"))
    ready = w.wait_state("READY")
    assert ready["recordCount"] == 13 and ready["systems"] == 1 << 3
    rc, txt = reader(run.section, "--seconds", "10")
    assert rc == 0 and "13 records (2 VB, 2 IB, 2 draws)" in txt, txt
    w.wait_state("RELEASED")
    finish(run)


@pytest.mark.parametrize("mode,fail,text", [
    ("badhandle", "LAYOUT", r"VB handle 0x1900 out of range"),
    ("badvtbl", "LAYOUT", r"VB handle 0x7: object 0x[0-9A-F]+ \(vtable 0x[0-9A-F]+\) is not a D3D11 buffer"),
    ("badsize", "LAYOUT", r"VB handle 0x7: the EXE's size differs from the buffer's \(EXE size 416, D3D size 384"),
    ("badindex", "LAYOUT", r"system 1 draw 0: index 7 = 15, the vertex buffer holds 12 vertices"),
    ("hugecount", "LAYOUT", r"wave chunks out of range: 100000 chunks"),
    ("badrecord", "LAYOUT", r"draw record 0x5: material 90 IB 0x9 VB 0x7, expected 91 / 0x9 / 0x7"),
    ("change", "CHANGED", r"the river or wave objects changed during the copy: a load started"),
    ("badsection", "SECTION", r"CreateFileMapping\('Local\\VPShellWater\\no\\such\\1'"),
])
def test_tap_hostile_objects_fail_the_load(run, wrun, mode, fail, text):
    """A failed check fails this load only: FAILED with its reason, no section left, the host and composite run on."""
    w = wrun(dict(water_test=mode))
    b = w.wait_state("FAILED")
    assert b["failName"] == fail and b["sectionName"] == "", b
    run.wait_log(r"water_tap: FAILED %s \(load \d+ copy 1\): .*%s" % (fail, text), timeout_s=5)
    assert not any("DISABLED" in ln for ln in wlines(run))
    time.sleep(1.0)
    assert w.block()["copySeq"] == 1      # one copy per load: no retry until a new load
    finish(run)


def test_tap_fault_disables_for_good(run, wrun):
    """The wave chunk pointer leads into reserved, uncommitted memory: the SEH guard catches the fault, the tap is off
    for the life of the process (EXCEPTION) and a new load does not start it again."""
    w = wrun(dict(water_test="garbage"))
    b = w.wait_state("FAILED")
    assert b["failName"] == "EXCEPTION"
    run.wait_log(r"water_tap: FAILED EXCEPTION \(load \d+ copy 1\): fault reading 72 bytes at 0x[0-9A-F]+ - DISABLED for the life of the process", timeout_s=5)
    run.trigger("shell_composite.waterload")
    time.sleep(1.5)
    assert w.block()["copySeq"] == 1 and not any("simulated new load" in ln for ln in wlines(run))
    finish(run)


def test_tap_image_limit(run, wrun):
    w = wrun(dict(water_test="ok", water_max_kb="1"))
    b = w.wait_state("FAILED")
    assert b["failName"] == "TOO_BIG"
    run.wait_log(r"water_tap: FAILED TOO_BIG \(load \d+ copy 1\): image of \d+ bytes, the limit is 1024", timeout_s=5)
    finish(run)


def test_tap_no_ack_times_out_and_closes(run, wrun):
    w = wrun(dict(water_test="ok", water_ack_timeout_ms="1500"))
    ready = w.wait_state("READY")
    b = w.wait_state("FAILED", timeout_s=6)
    assert b["failName"] == "ACK_TIMEOUT"
    run.wait_log(r"water_tap: FAILED ACK_TIMEOUT \(load \d+ copy 1\): no ack within 1500 ms; section closed", timeout_s=2)
    assert not tapcheck.exists(ready["sectionName"])
    finish(run)


def test_tap_new_load_supersedes_then_stale_then_request(run, wrun):
    """Load L READY, not acked; a new load (the test's trigger file) closes it (FAILED CHANGED, section gone) and copy 2
    of load L+1 follows. The camera block still says L, so the reader refuses copy 2 as stale (-3) and acks it; the
    DLL logs the refusal and closes. Then requestSeq asks for copy 3 of load L+1."""
    w = wrun(dict(water_test="ok"), seconds=30)
    r1 = w.wait_state("READY", copy=1)
    load = r1["loadSeq"]
    run.trigger("shell_composite.waterload")
    run.wait_log(r"water_tap: FAILED CHANGED \(load %d copy 1\): a new load \(seq %d\) superseded the unacknowledged copy" % (load, load + 1), timeout_s=3)
    assert not tapcheck.exists(r1["sectionName"])
    r2 = w.wait_state("READY", load=load + 1, copy=2)
    rc, txt = reader(run.section, "--seconds", "10")
    assert rc == 1 and "last result -3" in txt and "stale" in txt, txt
    w.wait_state("RELEASED", copy=2)
    run.wait_log(r"water_tap: RELEASED load %d copy 2: ack from pid \d+, result -3 \(the consumer refused the copy\)" % (load + 1), timeout_s=3)
    assert not tapcheck.exists(r2["sectionName"])
    w.main.write(WT_OFFSET + 212, struct.pack("<I", 1))      # requestSeq
    r3 = w.wait_state("READY", copy=3)
    assert r3["loadSeq"] == load + 1 and r3["fnv1a64"] == r2["fnv1a64"]   # the same load: the same image
    finish(run)


def test_tap_request_recopies_same_bytes(run, wrun, tmp_path):
    w = wrun(dict(water_test="ok"), seconds=30)
    w.wait_state("READY", copy=1)
    rc, txt = reader(run.section, "--seconds", "10")
    assert rc == 0, txt
    r1 = w.wait_state("RELEASED", copy=1)
    w.main.write(WT_OFFSET + 212, struct.pack("<I", 1))
    w.wait_state("READY", copy=2)
    rc, txt = reader(run.section, "--seconds", "10")
    assert rc == 0, txt
    r2 = w.wait_state("RELEASED", copy=2)
    assert r1["fnv1a64"] == r2["fnv1a64"] and r1["loadSeq"] == r2["loadSeq"]   # the same load, the same bytes
    finish(run)


def test_kill_switch_publishes_off(run, wrun):
    w = wrun(dict(water_tap="0", water_test="ok"), seconds=8)
    b = w.wait_state("OFF")
    assert b["copySeq"] == 0
    run.wait_log(r"water_tap: OFF \(water_tap=0\)", timeout_s=2)
    time.sleep(1.0)
    assert not any("copy" in ln for ln in wlines(run))
    finish(run)


def test_kill_switch_during_a_copy(run, wrun):
    w = wrun(dict(water_test="ok"), seconds=20)
    ready = w.wait_state("READY")
    run.set_ini(water_tap="0")
    w.wait_state("OFF", timeout_s=5)
    run.wait_log(r"water_tap: FAILED KILLED \(load \d+ copy 1\): water_tap=0", timeout_s=2)
    assert not tapcheck.exists(ready["sectionName"])
    finish(run)


# Bug 0120: refusals come at the first Present after the hook commit, which can take seconds under load (test_bake_tap.py)
SHORT_HOST_S = 20
HOOK_COMMIT_WAIT_S = 18


@pytest.mark.parametrize("build", exe_golden.BUILDS)
def test_real_path_refuses_the_fake_exe(run, build):
    """No water_test: the real path. test_host --fake-exe (of either build, ticket 4964) passes the composite's validation
    but has none of the tap's patterns: the tap refuses before any read and publishes FAILED REFUSED."""
    main = tapcheck.Mapping(run.section, create=4096)
    try:
        main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", water_tap="1", water_test="")
        run.start_host(*HOST, "--fake-exe-build", build, seconds=SHORT_HOST_S)
        run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
        run.wait_log(r"water_tap: REFUSED: pattern 'river build: RiverVisSystem .*' at water_river_build \(VA 0x[0-9A-F]{8}\): "
                     r"(mismatch at byte \d+|unreadable)", timeout_s=10)
        run.wait_log(r"water_tap: FAILED REFUSED .*EXE refused - DISABLED for the life of the process", timeout_s=5)
        b = parse_block(main.read(WT_OFFSET, WT_SIZE))
        assert b["stateName"] == "FAILED" and b["failName"] == "REFUSED"
        finish(run)
    finally:
        main.close()


def test_real_path_refuses_an_unvalidated_exe(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", water_tap="1", water_test="")
    run.start_host("--vsync", "0", "--occluded-sleep", "0", "--load", "after", seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"water_tap: REFUSED: the composite did not validate the EXE image", timeout_s=10)
    finish(run)


def test_unknown_test_mode_is_named(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", water_tap="1", water_test="bogus")
    run.start_host("--vsync", "0", "--occluded-sleep", "0", "--load", "after", seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"water_tap: water_test=bogus is not a known mode - ignored", timeout_s=10)
    finish(run)


# ======================================================================================================================
# static

def test_water_signatures_match_exe_on_disk():
    """Ticket 4964: the tap's 29 patterns against both builds' golden EXEs, with each build's table values (exe_golden;
    a missing golden copy or local table fails, never skips)."""
    assert len(exe_golden.rows("shell/composite/water_tap.cpp")) == 29
    bad = exe_golden.check_patterns("shell/composite/water_tap.cpp")
    assert bad == {b: [] for b in exe_golden.BUILDS}, bad
