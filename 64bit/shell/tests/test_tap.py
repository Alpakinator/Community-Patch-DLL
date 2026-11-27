"""Issue 3203 (epic 3200): the height-field tap of shell_composite.dll and its consumer checks, on the stand-in.

Two kinds of tests:
- The consumer (tests/tapcheck.py, the server's stand-in until issue 3205): sections crafted here in Python, one
  broken piece each, run through the command line; the exit code, the printed reason and the ack written back are
  asserted (wrong section version, wrong C5HF magic / version / layout, truncated section, truncated payload, stale
  loadSeq, wrong map size, hash mismatch, a section that does not open, FAILED and WRITING blocks).
- The DLL (test_host --fake-exe on a hidden desktop, rule 14; the main section is created here, no server): tap_test=
  feeds a synthetic field built in the DLL's own memory (test_host has no field) through the whole copy, or a hostile
  object (bad sizes, bad states and indexes, null / wrapping / unmapped pointers, too big, a change during the copy, a
  section that cannot be created, ready byte 0). Checked: the copy equals the synthetic field bit for bit with the
  header the EXE path writes; RELEASED after the ack and the section gone; no ack -> FAILED ACK_TIMEOUT and the section
  gone; every hostile object -> FAILED with its reason, the host alive and the composite not disabled; a new load
  supersedes an unacknowledged copy; requestSeq gets a new copy; tap=0 publishes OFF; the EXE path on the fake image
  (which has a grid object without layers) -> GRID_INVALID.
"""
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import pytest

import shellrun
import tapcheck

HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")
TAPCHECK = str(Path(tapcheck.__file__).resolve())
TINY_SX, TINY_SY = 3584, 2560          # the fake EXE's grid (the Tiny ground-truth map, 56 x 36 + 4 plots)


def cli(*args, timeout=120):
    p = subprocess.run([sys.executable, TAPCHECK, *args], capture_output=True, text=True, timeout=timeout)
    return p.returncode, (p.stdout + p.stderr).strip()


# ======================================================================================================================
# the consumer's checks on crafted sections

def c5hf(map_w, map_h, h=None, b=None, wrap=True, version=1, magic=b"C5HF", samples_x=None):
    n = map_w * 64 * map_h * 64
    h = np.arange(n, dtype=np.uint32).astype(np.uint16) if h is None else h
    b = (np.arange(n, dtype=np.uint32) * 7).astype(np.uint8) if b is None else b
    head = bytearray(128)
    head[0:4] = magic
    struct.pack_into("<6I", head, 4, version, map_w, map_h, samples_x or map_w * 64, map_h * 64, (1 if wrap else 0) | 2)
    struct.pack_into("<6f", head, 28, 1.0, 0.866, 0.2, map_w * 32.0, map_h * 32 * 0.866, 12.0)
    return bytes(head) + h.tobytes() + b.tobytes()


class Crafted:
    """A main section (magic, version, camera grid constants, tap block) and a tap section, made here."""

    def __init__(self, tag, *, version=3, grid_seq=1, cam_samples=(128, 128), image=None, state=2, load=1, copy=1,
                 payload=None, fnv=None, block_samples=None, section_bytes=None, section_name=None, fail=0):
        self.name = "Local\\VPShellTapTest_%d_%s" % (os.getpid(), tag)
        self.main = tapcheck.Mapping(self.name, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, version))
        cam = tapcheck.CAMERA_OFFSET
        self.main.write(cam + tapcheck.CAM_GRIDSEQ, struct.pack("<II", grid_seq, 1))
        self.main.write(cam + tapcheck.CAM_SAMPLESX, struct.pack("<II", *cam_samples))
        self.main.write(cam, struct.pack("<I", 2))                                    # camera seqlock: stable
        self.image = c5hf(2, 2) if image is None else image
        self.tapname = section_name or "Local\\VPShellTapTestData_%d_%s" % (os.getpid(), tag)
        size = section_bytes or len(self.image)
        self.sec = tapcheck.Mapping(self.tapname, create=size) if section_name is None else None
        if self.sec:
            self.sec.write(0, self.image[:size])
        mw, mh = struct.unpack_from("<II", self.image, 8)
        sx, sy = block_samples or struct.unpack_from("<II", self.image, 16)
        t = bytearray(tapcheck.TAP_SIZE)
        struct.pack_into("<9I", t, 4, state, load, fail, mw, mh, sx, sy, 3, os.getpid())
        struct.pack_into("<QQQ", t, 40, payload if payload is not None else len(self.image), size,
                         fnv if fnv is not None else tapcheck.fnv1a64(self.image))
        struct.pack_into("<I", t, 76, copy)
        nm = self.tapname.encode("utf-16-le")
        t[80:80 + len(nm)] = nm
        self.main.write(tapcheck.TAP_OFFSET + 4, bytes(t[4:208]))
        self.main.write(tapcheck.TAP_OFFSET, struct.pack("<I", 2))                     # tap seqlock: stable

    def tap(self):
        return tapcheck.parse_tap(self.main.read(tapcheck.TAP_OFFSET, tapcheck.TAP_SIZE))

    def close(self):
        for m in (self.sec, self.main):
            if m:
                m.close()


@pytest.fixture
def crafted(request):
    made = []

    def make(**kw):
        c = Crafted(request.node.name[-20:].replace("[", "_").replace("]", "") + str(len(made)), **kw)
        made.append(c)
        return c
    yield make
    for c in made:
        c.close()


def test_consumer_accepts_good_tap(crafted, tmp_path):
    c = crafted()
    out = tmp_path / "t.c5hf"
    code, text = cli("consume", "--section", c.name, "--wait", "2", "--out", str(out))
    assert code == 0, text
    assert "exit 0: ok" in text
    assert out.read_bytes() == c.image
    t = c.tap()
    assert (t["ackLoadSeq"], t["ackCopySeq"], t["ackResult"]) == (1, 1, 0), t
    assert t["ackServerPid"] != 0


def _broken_c5hf(kind):
    img = bytearray(c5hf(2, 2))
    if kind == "magic":
        img[0:4] = b"C5HX"
    elif kind == "version":
        struct.pack_into("<I", img, 4, 2)
    elif kind == "layout":
        struct.pack_into("<I", img, 16, 129)          # samplesX != 64 * mapW
    elif kind == "flags":
        struct.pack_into("<I", img, 24, 7)
    elif kind == "reserved":
        struct.pack_into("<I", img, 120, 1)
    return bytes(img)


@pytest.mark.parametrize("kind,exit_code,ack,text", [
    ("magic", 15, 3, "magic is not C5HF"),
    ("version", 15, 4, "tap: version 2"),
    ("layout", 15, 5, "samples 129x128 != 64 x map 2x2"),
    ("flags", 15, 5, "unknown flags 7"),
    ("reserved", 15, 5, "reserved words not 0"),
])
def test_consumer_refuses_bad_c5hf(crafted, kind, exit_code, ack, text):
    c = crafted(image=_broken_c5hf(kind))
    code, out = cli("consume", "--section", c.name, "--wait", "2")
    assert code == exit_code, out
    assert text in out, out
    t = c.tap()
    assert (t["ackLoadSeq"], t["ackResult"]) == (1, ack), t       # a refused copy is acked too: the DLL closes it


@pytest.mark.parametrize("case", ["main-missing", "main-version", "section-truncated", "payload-short", "payload-long",
                                  "stale", "map-size-camera", "map-size-block", "hash", "open", "failed", "off",
                                  "writing"])
def test_consumer_negative(crafted, case):
    """One broken piece each; exit code, reason and the ack result after the failure."""
    img = c5hf(2, 2)
    want_ack = None
    if case == "main-missing":
        code, out = cli("consume", "--section", "Local\\VPShellTapTest_%d_nosuch" % os.getpid(), "--wait", "1")
        assert code == 10 and "does not open" in out, out
        return
    if case == "main-version":
        c = crafted(version=2)
        code, out = cli("consume", "--section", c.name, "--wait", "1")
        assert code == 10 and "version 2 (want" in out, out
        assert c.tap()["ackLoadSeq"] == 0
        return
    kw = {}
    if case == "section-truncated":
        kw = dict(section_bytes=8192)                              # the section holds 8 KiB of a 48 KiB image
        want = (14, "truncated tap: section holds 8192 bytes, payload %d" % len(img)); want_ack = 2
    elif case == "payload-short":
        kw = dict(payload=len(img) - 1)
        want = (14, "truncated tap: tap: need %d bytes, have %d" % (len(img), len(img) - 1)); want_ack = 2
    elif case == "payload-long":
        kw = dict(payload=len(img) + 100, section_bytes=len(img) + 100)
        want = (14, "tap block payload %d != C5HF size %d" % (len(img) + 100, len(img))); want_ack = 2
    elif case == "stale":
        kw = dict(grid_seq=5, load=4)
        want = (12, "stale tap: loadSeq 4, camera gridSeq 5"); want_ack = -3
    elif case == "map-size-camera":
        kw = dict(cam_samples=(TINY_SX, TINY_SY))
        want = (16, "wrong map size: C5HF 128x128 samples (map 2x2, flags 3), camera 3584x2560"); want_ack = -4
    elif case == "map-size-block":
        kw = dict(block_samples=(192, 128))
        want = (16, "tap block 192x128"); want_ack = -4
    elif case == "hash":
        kw = dict(fnv=0x1234)
        want = (17, "FNV-1a 64 mismatch"); want_ack = -1
    elif case == "open":
        kw = dict(section_name="Local\\VPShellTapTest_%d_gone" % os.getpid())
        want = (13, "does not open"); want_ack = -2
    elif case == "failed":
        kw = dict(state=4, fail=3)
        want = (11, "tap state FAILED (fail EXCEPTION)")
    elif case == "off":
        kw = dict(state=5)
        want = (11, "tap state OFF")
    elif case == "writing":
        kw = dict(state=1)
        want = (11, "no READY tap within 1.0 s")
    c = crafted(**kw)
    code, out = cli("consume", "--section", c.name, "--wait", "1")
    assert code == want[0], out
    assert want[1] in out, out
    t = c.tap()
    if want_ack is None:
        assert t["ackLoadSeq"] == 0, t                             # nothing to ack: no section was offered
    else:
        assert (t["ackLoadSeq"], t["ackCopySeq"], t["ackResult"]) == (t["loadSeq"], 1, want_ack), t


def test_consumer_no_ack_flag(crafted):
    c = crafted()
    code, out = cli("consume", "--section", c.name, "--wait", "1", "--no-ack")
    assert code == 0, out
    assert c.tap()["ackLoadSeq"] == 0


def test_consumer_usage_and_r16(tmp_path):
    code, out = cli("consume")
    assert code == 2, out
    code, out = cli("bogus")
    assert code == 2, out
    raw = tmp_path / "h.r16"
    h = (np.arange(128 * 128, dtype=np.uint32) * 3).astype(np.uint16)
    raw.write_bytes(h.tobytes())
    code, out = cli("r16-to-c5hf", str(raw), "2", "2", str(tmp_path / "h.c5hf"), "--wrap")
    assert code == 0, out
    err, msg, f = tapcheck.check_c5hf((tmp_path / "h.c5hf").read_bytes()[:128], (tmp_path / "h.c5hf").stat().st_size)
    assert err == 0 and f["flags"] == 1, msg
    code, out = cli("r16-to-c5hf", str(raw), "3", "2", str(tmp_path / "x.c5hf"))
    assert code == 14 and "needs" in out, out


def test_fnv_reference_values():
    """FNV-1a 64 reference vectors (the DLL's TapFnv uses the same offset basis and prime)."""
    assert tapcheck.fnv1a64(b"") == 0xCBF29CE484222325
    assert tapcheck.fnv1a64(b"a") == 0xAF63DC4C8601EC8C
    assert tapcheck.fnv1a64(b"foobar") == 0x85944171F73967E8


# ======================================================================================================================
# the DLL on the stand-in

class TapRun:
    def __init__(self, run, ini, seconds=14, host=None):
        self.run = run
        self.main = tapcheck.Mapping(run.section, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", tap="1", tap_delay_frames="5",
                   tap_ack_timeout_ms="20000", tap_scene_frames="0")
        cfg.update(ini)
        run.set_ini(**cfg)
        run.start_host(*(host or HOST), seconds=seconds)

    def tap(self):
        raw = tapcheck.read_block(self.main, tapcheck.TAP_OFFSET, tapcheck.TAP_SIZE)
        return tapcheck.parse_tap(raw) if raw else {}

    def wait_state(self, state, timeout_s=15.0, load=None, copy=None):
        t_end = time.time() + timeout_s
        t = {}
        while time.time() < t_end:
            t = self.tap()
            if t and t["stateName"] == state and (load is None or t["loadSeq"] == load) and (copy is None or t["copySeq"] == copy):
                return t
            time.sleep(0.02)
        raise TimeoutError("tap state %s not reached: %s; log tail %s" % (state, t, shellrun.tail(self.run.composite_log, 8)))

    def close(self):
        self.main.close()


@pytest.fixture
def taprun(run):
    made = []

    def make(ini, seconds=14, host=None):
        t = TapRun(run, ini, seconds, host)
        made.append(t)
        return t
    yield make
    for t in made:
        t.close()


def finish(run, seconds=40, composite=True):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert rows and not any(r["disabled"] for r in rows)
    assert sum(r["frames"] for r in rows) > 0
    if composite:
        assert sum(r["comp_n"] for r in rows) > 0  # the composite kept running through the tap


def test_tap_synth_copied_exactly_and_released(run, taprun, tmp_path):
    t = taprun(dict(tap_test="synth"))
    ready = t.wait_state("READY")
    assert tapcheck.exists(ready["sectionName"])
    assert re.fullmatch(r"Local\\VPShellTap\.%d\.1\.1" % run.host.pid, ready["sectionName"]), ready
    out = tmp_path / "synth.c5hf"
    res = tapcheck.consume(run.section, wait_s=5, out=out)
    assert res.code == 0, res.line()
    rel = t.wait_state("RELEASED")
    line = run.wait_log(r"tap: RELEASED load 1 copy 1: ack from pid %d, result 0," % os.getpid(), timeout_s=5)
    assert "section closed" in line
    assert not tapcheck.exists(ready["sectionName"])         # the DLL's handle was the last one: the section is gone
    data = out.read_bytes()
    err, msg, f = tapcheck.check_c5hf(data[:128], len(data))
    assert err == 0, msg
    assert (f["mapW"], f["mapH"], f["samplesX"], f["samplesY"], f["flags"]) == (56, 40, TINY_SX, TINY_SY, 3)
    assert (f["sx"], f["sz"], f["ox"], f["waterZ"]) == (1.0, pytest.approx(0.2), TINY_SX / 2, 12.0)
    h2w = struct.unpack_from("<16f", data, 56)
    assert h2w[0] == pytest.approx(63.9986) and h2w[13] == pytest.approx(128.0033)   # the fake EXE's M_h2w
    n = TINY_SX * TINY_SY
    h = np.frombuffer(data, dtype=np.uint16, count=n, offset=128).reshape(TINY_SY, TINY_SX)
    b = np.frombuffer(data, dtype=np.uint8, count=n, offset=128 + 2 * n).reshape(TINY_SY, TINY_SX)
    eh, eb = tapcheck.synth_expected(TINY_SX, TINY_SY)
    assert np.array_equal(h, eh) and np.array_equal(b, eb)
    assert rel["fnv1a64"] == res.c5hf["fnv"] and rel["payloadBytes"] == 128 + 3 * n and rel["copyMs"] > 0
    rdy = run.wait_log(r"tap: READY load 1 copy 1: map 56x40 plots", timeout_s=1)
    assert "views of at most 4096 KiB" in rdy
    finish(run)
    rows = shellrun.stats(run.log_lines())
    assert rows[-1].get("tap_copies") == 1 and "tap=RELEASED" in [ln for ln in run.log_lines() if " stats " in ln][-1]


def test_tap_without_byte_layer_and_small_chunks(run, taprun, tmp_path):
    t = taprun(dict(tap_test="synth", tap_byte_layer="0", tap_chunk_kb="1024"))
    t.wait_state("READY")
    out = tmp_path / "h.c5hf"
    res = tapcheck.consume(run.section, wait_s=5, out=out)
    assert res.code == 0, res.line()
    t.wait_state("RELEASED")
    data = out.read_bytes()
    n = TINY_SX * TINY_SY
    assert len(data) == 128 + 2 * n and struct.unpack_from("<I", data, 24)[0] == 1
    eh, _ = tapcheck.synth_expected(TINY_SX, TINY_SY)
    assert np.array_equal(np.frombuffer(data, dtype=np.uint16, count=n, offset=128).reshape(TINY_SY, TINY_SX), eh)
    assert "views of at most 1024 KiB" in run.wait_log(r"tap: READY load 1", timeout_s=1)
    finish(run)


def test_tap_no_ack_times_out_and_closes(run, taprun):
    t = taprun(dict(tap_test="synth", tap_ack_timeout_ms="1500"))
    ready = t.wait_state("READY")
    assert tapcheck.exists(ready["sectionName"])
    failed = t.wait_state("FAILED", timeout_s=10)
    assert failed["failName"] == "ACK_TIMEOUT" and failed["loadSeq"] == 1
    run.wait_log(r"tap: FAILED ACK_TIMEOUT \(load 1 copy 1\): no ack within 1500 ms; section closed", timeout_s=2)
    assert not tapcheck.exists(ready["sectionName"])
    res = tapcheck.consume(run.section, wait_s=1)
    assert res.code == 11 and "FAILED (fail ACK_TIMEOUT)" in res.msg
    time.sleep(1.0)
    assert "tap: copy 2" not in "\n".join(run.log_lines())    # one copy per load: no retry after a failure
    finish(run)


HOSTILE = [
    ("bad-size", "GRID_INVALID", r"samples 3600x2560 are not a map of 64-sample plots", False),
    ("bad-tiles", "GRID_INVALID", r"ushort layer \+0x0C: tiles 223x160 do not cover 3584x2560", False),
    ("bad-state", "GRID_INVALID", r"ushort layer: tile 7,0 directory state 5", False),
    ("bad-index", "GRID_INVALID", r"ushort layer: tile 9,0 pool index \d+ past the pool of \d+ tiles", False),
    ("null-dir", "GRID_INVALID", r"ushort layer \+0x0C: directory 0x00000000 or pool 0x[0-9A-F]+ is null", False),
    ("wrap-ptr", "GRID_INVALID", r"byte layer \+0x4C: directory .* or pool 0xFFFFF000\.\.0x[0-9A-F]+ \(\d+ tiles\) outside the address space", False),
    ("bad-dir", "EXCEPTION", r"fault reading a tile directory", False),
    ("bad-pool", "EXCEPTION", r"fault reading the byte pool \(0x[0-9A-F]+, \d+ tiles\) in tile row 0", True),
    ("too-big", "TOO_BIG", r"payload 805306496 bytes above the limit 268435456", False),
    ("change", "CHANGED", r"the GridHeightmap object changed during the copy \(first differing byte \+0xB[4-7]", True),   # ox (+0xB4) + 1.0
    ("bad-section", "SECTION", r"CreateFileMapping\('Local\\VPShellTap\\no\\such\\1', \d+ bytes\) failed, error \d+", False),
    ("not-ready", "GRID_INVALID", r"ready byte \+0x09 is 0", False),
]


@pytest.mark.parametrize("mode,fail,text,had_section", HOSTILE, ids=[h[0] for h in HOSTILE])
def test_tap_hostile_grid(run, taprun, mode, fail, text, had_section):
    """A hostile grid object: FAILED with the right reason, nothing left open, the host and the composite run on."""
    t = taprun(dict(tap_test=mode), seconds=8)
    failed = t.wait_state("FAILED")
    assert failed["failName"] == fail, failed
    line = run.wait_log(r"tap: FAILED %s \(load 1 copy 1\): " % fail, timeout_s=2)
    assert re.search(text, line), line
    if had_section:
        assert failed["sectionName"].startswith("Local\\VPShellTap.")
        assert not tapcheck.exists(failed["sectionName"])
    res = tapcheck.consume(run.section, wait_s=1)
    assert res.code == 11 and ("fail %s" % fail) in res.msg, res.line()
    assert not any("EXCEPTION 0x" in ln for ln in run.log_lines())     # every fault was caught inside the tap
    finish(run)


def test_tap_exe_path_on_fake_image(run, taprun):
    """No tap_test: the real path (rebased 0xD90FC4 / 0xD91270 reads) on test_host's fake image, whose GridHeightmap has
    the ready byte, the layer size at +0x0C/+0x10 (what the camera block reads) and the scales, but not the object's own
    samplesX/Y at +0xA0/+0xA4 nor the directories and pools: GRID_INVALID, nothing else happens."""
    t = taprun({}, seconds=8)
    failed = t.wait_state("FAILED")
    assert failed["failName"] == "GRID_INVALID"
    line = run.wait_log(r"tap: FAILED GRID_INVALID \(load 1 copy 1\): ", timeout_s=2)
    assert "samples 0x0 are not a map of 64-sample plots" in line, line
    assert "the EXE's field" in run.wait_log(r"tap: copy 1 of load 1 started", timeout_s=1)
    finish(run)


def test_tap_waits_for_scene_clear_frames(run, taprun):
    """tap_scene_frames=30: the copy waits for 30 frames with the scene-clear list (clear_bb + clear_dsv in one deferred
    list); test_host --game has it (its clear list), so the copy runs and says so."""
    t = taprun(dict(tap_test="synth", tap_scene_frames="30"), seconds=10,
               host=("--game", "--vary", "1", "--mt", "1") + HOST)
    t.wait_state("READY")
    line = run.wait_log(r"tap: copy 1 of load 1 started", timeout_s=2)
    assert int(re.search(r"(\d+) scene-clear frames", line).group(1)) >= 30, line
    assert tapcheck.consume(run.section, wait_s=5).code == 0
    t.wait_state("RELEASED")
    finish(run, composite=False)        # insert=clear never fires in --game (its clears are deferred)


def test_tap_no_copy_without_scene_clear(run, taprun):
    """Negative: the same with test_host's plain path (clears on the immediate context, no scene-clear list, like the
    EXE's load screen): no copy at all, the block stays NONE while the camera is OK and the grid valid."""
    t = taprun(dict(tap_test="synth", tap_scene_frames="30"), seconds=6)
    run.wait_log(r"camera: grid constants read \(grid seq 1\): valid 1", timeout_s=10)
    time.sleep(3.0)
    assert struct.unpack("<II", t.main.read(tapcheck.TAP_OFFSET, 8)) == (0, 0)   # never written: seq 0, state NONE
    finish(run)
    assert not any("tap: copy" in ln for ln in run.log_lines())
    assert any("cam=OK" in ln for ln in run.log_lines() if " stats " in ln)


def test_tap_off_publishes_off(run, taprun):
    t = taprun(dict(tap="0", tap_test="synth"), seconds=6)
    off = t.wait_state("OFF")
    assert off["loadSeq"] == 0
    run.wait_log(r"tap: OFF \(tap=0\)", timeout_s=2)
    res = tapcheck.consume(run.section, wait_s=1)
    assert res.code == 11 and "tap state OFF" in res.msg
    time.sleep(1.0)
    assert not any("tap: copy" in ln for ln in run.log_lines())
    finish(run)


def test_tap_new_load_supersedes_and_request_recopies(run, taprun, tmp_path):
    """Load 1 READY but not acked; a new load (tap_test_load) closes it (FAILED CHANGED, section gone) and copy 2 of
    load 2 follows; the consumer takes it; then requestSeq asks for copy 3 of the same load (a restarted server)."""
    t = taprun(dict(tap_test="synth"), seconds=20)
    r1 = t.wait_state("READY", load=1)
    run.set_ini(tap_test_load="1")
    f1 = t.wait_state("FAILED", load=1)
    assert f1["failName"] == "CHANGED"
    run.wait_log(r"tap: FAILED CHANGED \(load 1 copy 1\): a new load \(grid seq 2\) superseded the unacknowledged copy", timeout_s=3)
    assert not tapcheck.exists(r1["sectionName"])
    r2 = t.wait_state("READY", load=2, copy=2)
    assert r2["sectionName"].endswith(".2.2")
    res = tapcheck.consume(run.section, wait_s=5)
    assert res.code == 0, res.line()
    t.wait_state("RELEASED", load=2, copy=2)
    # a consumer that lost the field asks again
    t.main.write(tapcheck.TAP_OFFSET + 220, struct.pack("<I", 1))
    r3 = t.wait_state("READY", load=2, copy=3)
    res3 = tapcheck.consume(run.section, wait_s=5, out=tmp_path / "c3.c5hf")
    assert res3.code == 0, res3.line()
    t.wait_state("RELEASED", load=2, copy=3)
    assert r3["fnv1a64"] == r2["fnv1a64"]                              # the same field, copied again
    assert not tapcheck.exists(r3["sectionName"]) and not tapcheck.exists(r2["sectionName"])
    finish(run)


# ======================================================================================================================
# issue 3208: the depth probe (depth_probe=1) on the game-like stand-in, whose depth use is known from its source:
# the clear list clears a D24S8 (45) back-buffer-size view with DEPTH|STENCIL to 1.0 / 0, the terrain list a D32 (40)
# 512x512 view with DEPTH to 1.0 and draws with depth LESS / write ALL, the units list draws into the back buffer with
# the D24S8 view and the same state.

GAME = ("--game", "--vary", "1", "--mt", "1", "--vsync", "0", "--occluded-sleep", "0", "--load", "after")


@pytest.mark.parametrize("probe", [1, 0])
def test_depth_probe_on_standin(run, probe):
    main = tapcheck.Mapping(run.section, create=4096)
    try:
        run.set_ini(mode="passthrough", insert="tagged", tag_rule="after", tag_match="clear_bb", trace_frames="0",
                    stats_ms="500", depth_probe=str(probe))
        run.start_host(*GAME, seconds=9)
        if probe:
            run.wait_log(r"depth: table", timeout_s=15)
            time.sleep(5.5)                                   # the second table, with every kind counted
        finish(run, composite=False)
    finally:
        main.close()
    lines = [ln for ln in run.log_lines() if " depth: " in ln or " depth:   " in ln]
    if not probe:
        assert lines == []
        return
    text = "\n".join(lines)
    assert re.search(r"clear #\d+: fmt 45 1280x720 flags 3 DEPTH STENCIL depth 1 stencil 0 - \d+ times", text), text
    assert re.search(r"clear #\d+: fmt 40 512x512 flags 1 DEPTH depth 1 stencil 0 - \d+ times", text), text
    assert re.search(r"\(list bb\+dsv, last draw of the list\): dsv fmt 45 1280x720, dss set: depth on func LESS write all, "
                     r"stencil off .*viewport depth 0\.000\.\.1\.000", text), text
    assert re.search(r"\(list rt\+dsv, last draw of the list\): dsv fmt 40 512x512, dss set: depth on func LESS write all", text), text
    assert re.search(r"\(immediate at the insertion, last draw of the list\): dsv fmt -1", text), text
    assert re.search(r"FinishCommandList restore 0/1: \d+/0; ExecuteCommandList restore 0/1: \d+/0", text), text
    assert "overflow 0" in text


# ======================================================================================================================
# issue 3208: the conversion of the server's depth to the EXE's, offline, at the six ground-truth cameras of epic 3000
# (D:/c5-64bit-work/i2-ground-truth; a missing ground truth skips, and a skip fails the run: conftest.py)

import tap_depth  # noqa: E402

CAMS = [(g, v) for g in ("huge", "tiny") for v in ("v1_city_default", "v2_city_zoomin", "v3_second_zoomin")]
needs_gt = pytest.mark.skipif(not tap_depth.GT.is_dir(), reason="ground truth of epic 3000 missing")


@needs_gt
@pytest.mark.parametrize("game,view", CAMS, ids=["%s-%s" % c for c in CAMS])
def test_depth_view_dist_converts_to_exe_d24(game, view):
    """VIEW_DIST (z_eye in world units) -> d = -P22 + P32 / z_eye -> D24 equals the EXE's own z/w of the same terrain
    point (float32, clip = p * (V * P)) within 1 step of 2^-24, at every visible heightmap sample (every 4th in x and y)."""
    cam = tap_depth.camera(tap_depth.GT / game / ("camera_%s_before.json" % view))
    pts = tap_depth.terrain_points(game, cam)
    assert len(pts) > 2000, len(pts)
    d_exe, w, _, _ = tap_depth.exe_depth(pts, cam["view"], cam["proj"])
    z_eye = tap_depth.eye_depth(pts, cam["view"])
    assert np.allclose(z_eye, w, rtol=1e-5)                          # w_clip is z_eye: the RH projection's -z_view
    d_conv = tap_depth.d_from_eye(z_eye, cam["proj"])
    # the exact value of the same matrices in float64: the formula must hit it within one D24 step
    h = np.concatenate([pts.astype(np.float64), np.ones((len(pts), 1))], axis=1)
    c = h @ (cam["view"].astype(np.float64) @ cam["proj"].astype(np.float64))
    d64 = c[:, 2] / c[:, 3]
    conv_err = np.abs(tap_depth.d24(d_conv) - tap_depth.d24(d64))
    exe_noise = np.abs(tap_depth.d24(d_exe) - tap_depth.d24(d64))          # the EXE's own float32 rounding
    diff = np.abs(tap_depth.d24(d_exe) - tap_depth.d24(d_conv))
    # NDC_Z: the server's own z/w in an R32_FLOAT texel is the EXE's value; the R32 transport is exact
    assert np.array_equal(tap_depth.d24(np.float32(d_exe)), tap_depth.d24(d_exe))
    zmed = float(np.median(z_eye))
    step = tap_depth.world_per_d24(zmed, cam["near"], cam["far"])
    print("%s %s: %d points, z_eye %.0f..%.0f (median %.0f), d %.6f..%.6f; D24 steps: formula vs float64 max %d, "
          "EXE float32 vs float64 max %d (mean %.2f), formula vs EXE float32 max %d (exact %.3f); one D24 step = %.5f "
          "world units at the median" % (game, view, len(pts), z_eye.min(), z_eye.max(), zmed, d_exe.min(), d_exe.max(),
                                         conv_err.max(), exe_noise.max(), exe_noise.mean(), diff.max(),
                                         (diff == 0).mean(), step))
    assert conv_err.max() <= 1, conv_err.max()
    assert diff.max() <= exe_noise.max() + 1, (diff.max(), exe_noise.max())
    assert diff.max() * tap_depth.world_per_d24(float(z_eye.max()), cam["near"], cam["far"]) < 0.01   # world units


@needs_gt
def test_depth_projection_is_standard_rh_0_to_1():
    """g_Proj is PerspectiveFovRH(fov, aspect, 50, 4000) with depth 0 at the near plane and 1 at the far plane (not
    reversed, not -1..1): the EXE's depth clear of 1.0 is the far plane."""
    for game, view in CAMS:
        cam = tap_depth.camera(tap_depth.GT / game / ("camera_%s_before.json" % view))
        n, f, P = cam["near"], cam["far"], cam["proj"].astype(np.float64)
        ys = 1.0 / np.tan(np.radians(cam["fov"]) / 2)
        want = np.array([[ys / cam["aspect"], 0, 0, 0], [0, ys, 0, 0], [0, 0, f / (n - f), -1], [0, 0, n * f / (n - f), 0]])
        assert np.allclose(P, want, rtol=2e-6, atol=1e-6), (game, view, P)
        d = tap_depth.d_from_eye(np.array([n, f]), P)
        assert abs(d[0]) < 1e-6 and abs(d[1] - 1.0) < 1e-6, d


@needs_gt
@pytest.mark.parametrize("wrong", ["z_view_sign", "reversed_z", "gl_minus1_1", "swapped_near_far"])
def test_depth_conversion_check_catches_wrong_conventions(wrong):
    """Negative tests of the check itself: each plausible wrong convention is off by far more than 1 D24 step."""
    cam = tap_depth.camera(tap_depth.GT / "huge" / "camera_v1_city_default_before.json")
    pts = tap_depth.terrain_points("huge", cam)
    d_exe = tap_depth.exe_depth(pts, cam["view"], cam["proj"])[0]
    z = tap_depth.eye_depth(pts, cam["view"]).astype(np.float64)
    n, f = cam["near"], cam["far"]
    if wrong == "z_view_sign":
        d = tap_depth.d_from_eye(-z, cam["proj"])
    elif wrong == "reversed_z":
        d = n / (f - n) * (f / z - 1.0)                 # reversed-Z: 1 at near, 0 at far
    elif wrong == "gl_minus1_1":
        d = (f + n) / (f - n) - 2 * f * n / ((f - n) * z)
    else:
        d = n / (n - f) - n * f / ((n - f) * z)
    diff = np.abs(tap_depth.d24(d_exe) - tap_depth.d24(d))
    assert diff.max() > 1000, (wrong, diff.max())
