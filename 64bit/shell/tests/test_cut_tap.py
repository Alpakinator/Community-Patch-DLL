"""Ticket 4553: the cut-mask tap of shell_composite.dll (composite/cut_tap.cpp), on its test entry cut_tap_host.exe
(shell/tests/cut_tap_host.cpp: the tap's own OnPresent loop without D3D; a console program started without a window).

cut_test=<mode> feeds a synthetic GridHeightmap and plot mask grid (cut_tap.cpp BuildTestWorld) through the real worker:
the guarded reads, the checks, the comparison, the image section and the control block. The host reads its own control
block and writes each new generation's block and image to files, as the server's reader would see them. Asserted: every
byte of the image against the synthetic grid recomputed here (header, directory as the EXE holds it, the pool with its
free block, FNV-1a 64) and every field of the control block; one image while nothing changes; a new generation for a
changed cell, for a new load and after the grid was freed and made again. Negatives (COMMON 9; each checks the images,
the control block and the log, and that the host still exits 0): the grid not made, no GridHeightmap, a directory entry of
state 7, a pool index past the blocks, an in-use count that is not the pooled tiles, a width not a multiple of 16, sample
counts that give another width, an origin not finite (each: no image, LOUD, read again: refusedReads > 0), an unmapped
directory (SEH: FAILED EXCEPTION, the tap off for the process), the real path on an EXE of no known build and on an EXE
the composite did not validate (FAILED PATTERN for good), one broken pattern in an image that holds all the others, the
switch off from the start and during the run (OFF, nothing read, the images closed), no map loaded, an image above
cut_max_kb (FAILED TOO_BIG, no image). Ticket 4965: fake images of both known builds, a header of no build, a symbol not carried.
Ticket 0500: torn reads (the header changed during the copy; cut_test=torn) have their own run: 30 in a row (the old
LOUD streak was 4) are not LOUD and the next good read publishes; torn at every poll past cut_torn_loud_s is LOUD once.
Ticket 0690 (K28): the pool replaced and released during the copy (cut_test_torn_free=1|2) faults under a moved header: a
torn read, not FAILED EXCEPTION; released under an unchanged header (=3): FAILED EXCEPTION as before.

Needs C5_SHELL_BUILD (shell/build.cmd, which builds cut_tap_host.exe). A missing binary fails the test (no skip).
"""
import os
import re
import shutil
import struct
import subprocess
import time
from pathlib import Path

import pytest

import shellrun

MAGIC = 0x54433543
CTL = 504
IMG_HDR = 160
TX, TY, BLOCKS = 7, 3, 8
NAMES_STATE = {0: "NONE", 2: "READY", 3: "FAILED", 4: "OFF", 5: "NO_GRID"}


def host_exe() -> Path:
    p = shellrun.bin_dir() / "cut_tap_host.exe"
    assert p.exists(), "cut_tap_host.exe not built in %s: run `cmd /c shell\\build.cmd`" % p.parent
    return p


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def parse_control(b):
    assert len(b) == CTL, len(b)
    keys = ["seq", "magic", "version", "headerBytes", "state", "failReason", "exePid", "generation", "loadSeq", "tilesX",
            "tilesY", "poolBlocks"]
    c = dict(zip(keys, struct.unpack_from("<12I", b, 0)))
    c["imageBytes"], c["fnv"], c["readyQpc"] = struct.unpack_from("<QQq", b, 48)
    c["polls"], c["refusedReads"] = struct.unpack_from("<2I", b, 72)
    c["lastPollMs"], c["maxPollMs"] = struct.unpack_from("<2f", b, 80)
    c["imageName"] = b[88:216].decode("utf-16-le").split("\0", 1)[0]
    c["failText"] = b[216:376].split(b"\0", 1)[0].decode("latin-1")
    c["ackGeneration"], c["ackResult"], c["ackServerPid"] = struct.unpack_from("<IiI", b, 376)
    return c


def parse_image(b):
    keys = ["magic", "version", "headerBytes", "generation", "loadSeq", "exePid", "widthCells", "heightCells", "tilesX",
            "tilesY", "samplesX", "samplesY"]
    h = dict(zip(keys, struct.unpack_from("<12I", b, 0)))
    h["ox"], h["oy"] = struct.unpack_from("<2f", b, 48)
    k2 = ["poolBlocks", "pooledTiles", "dirOffset", "dirBytes", "poolOffset", "poolBytes"]
    h.update(zip(k2, struct.unpack_from("<6I", b, 56)))
    h["imageBytes"] = struct.unpack_from("<Q", b, 80)[0]
    h["treeCells"], h["setCells"] = struct.unpack_from("<2I", b, 88)
    h["gridHeader"] = b[96:144]
    return h


def expected_grid(flipped=False):
    """The synthetic grid of cut_tap.cpp BuildTestWorld: (directory bytes, pool bytes, tree cells, set cells)."""
    d = bytearray(b"\xCD" * (TX * TY * 8))
    for t in range(TX * TY):
        if t % 3 == 0:
            struct.pack_into("<H", d, t * 8, 0)
            struct.pack_into("<I", d, t * 8 + 4, 6 - t // 3)
        else:
            struct.pack_into("<H", d, t * 8, 1)
            d[t * 8 + 4] = (t * 37 + 5) & 0xFF
    pool = bytearray()
    for k in range(7):
        pool += bytes((k * 31 + i * 7) & 0xFF for i in range(256))
    fb = bytearray(b"\xEE" * 256)
    struct.pack_into("<I", fb, 0, 0x0FFFFFFF)
    struct.pack_into("<H", fb, 4, 1)
    pool += fb
    if flipped:
        pool[2 * 256 + 0x21] ^= 1
    tree = setc = 0
    for t in range(TX * TY):
        if t % 3:
            v = d[t * 8 + 4]
            tree += 256 if v & 7 else 0
            setc += 256 if v else 0
        else:
            k = 6 - t // 3
            for v in pool[k * 256:(k + 1) * 256]:
                tree += 1 if v & 7 else 0
                setc += 1 if v else 0
    return bytes(d), bytes(pool), tree, setc


def cells(dir_b, pool_b):
    """The grid as the EXE's lookup reads it: cell (col, row) -> byte."""
    w, h = TX * 16, TY * 16
    out = bytearray(w * h)
    for t in range(TX * TY):
        tx, ty = t % TX, t // TX
        st = struct.unpack_from("<H", dir_b, t * 8)[0]
        for r in range(16):
            for c in range(16):
                v = dir_b[t * 8 + 4] if st else pool_b[struct.unpack_from("<I", dir_b, t * 8 + 4)[0] * 256 + r * 16 + c]
                out[(ty * 16 + r) * w + tx * 16 + c] = v
    return bytes(out)


def check_image(img_path, ctl_path, generation, load=1, flipped=False, pid=None):
    b = img_path.read_bytes()
    c = parse_control(ctl_path.read_bytes())
    h = parse_image(b)
    d, pool, tree, setc = expected_grid(flipped)
    assert (h["magic"], h["version"], h["headerBytes"]) == (MAGIC, 1, IMG_HDR), h
    assert (h["generation"], h["loadSeq"]) == (generation, load), h
    assert (h["widthCells"], h["heightCells"], h["tilesX"], h["tilesY"]) == (112, 48, TX, TY), h
    assert (h["samplesX"], h["samplesY"], h["ox"], h["oy"]) == (200, 72, 1.5, -2.25), h
    assert (h["poolBlocks"], h["pooledTiles"]) == (BLOCKS, 7), h
    assert (h["dirOffset"], h["dirBytes"], h["poolOffset"], h["poolBytes"], h["imageBytes"]) == (160, 168, 336, 2048, 2384), h
    assert len(b) == h["imageBytes"]
    assert b[160:328] == d
    assert b[328:336] == b"\0" * 8
    assert b[336:] == pool
    assert (h["treeCells"], h["setCells"]) == (tree, setc)
    g = h["gridHeader"]
    assert g[0] == 1 and g[1:8] == b"\0" * 7
    assert struct.unpack_from("<4I", g, 8) == (112, 48, TX, TY)
    assert struct.unpack_from("<I", g, 0x1C)[0] == 7 and struct.unpack_from("<I", g, 0x20)[0] == 7
    assert struct.unpack_from("<2I", g, 0x28) == (BLOCKS, 12)
    assert fnv1a64(b) == c["fnv"]
    assert (c["magic"], c["version"], c["headerBytes"], c["state"], c["failReason"]) == (MAGIC, 1, CTL, 2, 0), c
    assert (c["generation"], c["loadSeq"], c["tilesX"], c["tilesY"], c["poolBlocks"], c["imageBytes"]) == \
        (generation, load, TX, TY, BLOCKS, 2384), c
    assert c["imageName"] == "Local\\VPShellCut.%d.%d" % (c["exePid"], generation)
    assert c["seq"] % 2 == 0 and c["failText"] == ""
    if pid is not None:
        assert c["exePid"] == pid and h["exePid"] == pid
    return h, c


class Host:
    def __init__(self, tmp: Path, env: dict, args=(), seconds=3.0):
        self.dir = tmp
        self.out = tmp / "out"
        self.out.mkdir()
        self.exe = tmp / "cut_tap_host.exe"
        shutil.copy(host_exe(), self.exe)
        asan = shellrun.bin_dir() / "clang_rt.asan_dynamic-i386.dll"
        if asan.exists():
            shutil.copy(asan, tmp / asan.name)
        self.log = tmp / "host.log"
        e = {k: v for k, v in os.environ.items() if not k.startswith("VPSHELL_CUT")}
        e.update({"VPSHELL_CUT_POLL_MS": "20"})
        e.update({k: str(v) for k, v in env.items()})
        self.p = subprocess.Popen([str(self.exe), "--seconds", str(seconds), "--log", str(self.log), "--out",
                                   str(self.out), *args], env=e, creationflags=0x08000000)

    def images(self):
        return sorted(self.out.glob("image_*.bin"), key=lambda f: int(f.stem.split("_")[1]))

    def control(self, gen):
        return self.out / ("control_%s.bin" % gen)

    def last(self):
        return parse_control((self.out / "control_last.bin").read_bytes())

    def lines(self):
        return self.log.read_text(errors="replace").splitlines() if self.log.exists() else []

    def wait(self, pred, timeout_s=10.0, what=""):
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            if pred():
                return
            time.sleep(0.03)
        raise TimeoutError("%s not reached; images %s; log %s" % (what, self.images(), self.lines()[-8:]))

    def wait_log(self, pattern, timeout_s=10.0):
        self.wait(lambda: any(re.search(pattern, ln) for ln in self.lines()), timeout_s, "log line %r" % pattern)

    def trigger(self, name, text=""):
        (self.dir / name).write_text(text)

    def finish(self, timeout_s=40):
        rc = self.p.wait(timeout_s)
        assert rc == 0, (rc, self.lines()[-10:])
        assert any("cut_tap_host:" in ln and "Presents" in ln for ln in self.lines())
        return rc

    def kill(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait(5)


@pytest.fixture
def host(tmp_path):
    made = []

    def make(env, args=(), seconds=3.0):
        d = tmp_path / ("h%d" % len(made))
        d.mkdir()
        h = Host(d, env, args, seconds)
        made.append(h)
        return h
    yield make
    for h in made:
        h.kill()


def loud(h):
    return [ln for ln in h.lines() if "LOUD" in ln]


def test_ok_one_image_while_nothing_changes(host):
    h = host({"VPSHELL_CUT_TEST": "ok"}, seconds=3.0)
    h.wait(lambda: h.images(), what="the first image")
    h.finish()
    imgs = h.images()
    assert len(imgs) == 1, imgs      # the same grid at every later poll: no second generation
    check_image(imgs[0], h.control(1), 1, pid=h.p.pid)
    c = h.last()
    assert (c["state"], c["generation"], c["refusedReads"]) == (2, 1, 0), c
    assert c["polls"] >= 20, c          # 3 s at 20 ms
    assert 0 <= c["lastPollMs"] <= c["maxPollMs"] < 1000
    assert any("cut_tap: 13 byte patterns match (test world)" in ln for ln in h.lines())
    assert any(re.search(r"cut_tap: READY generation 1 \(load 1\): 112 x 48 cells \(7 x 3 tiles\), samples 200 x 72, "
                         r"origin \(1\.5000, -2\.2500\); 7 pooled tiles of 8 blocks", ln) for ln in h.lines()), h.lines()[-5:]
    assert not loud(h)
    # the worker, not the render thread, does the work: the host's OnPresent calls stay cheap
    m = [ln for ln in h.lines() if "cut_tap_host:" in ln and "Presents" in ln][-1]
    assert float(re.search(r"wall max ([0-9.]+) ms", m).group(1)) < 50.0, m


def test_settle_polls_hold_a_changing_grid(host):
    # with 1000 settle polls the first image never comes within the run (the grid has to stay the same that long)
    h = host({"VPSHELL_CUT_TEST": "ok", "VPSHELL_CUT_SETTLE_POLLS": "1000"}, seconds=2.0)
    h.finish()
    assert h.images() == []
    assert h.last()["state"] == 0 and h.last()["generation"] == 0


def test_change_new_load_and_regrid(host):
    h = host({"VPSHELL_CUT_TEST": "ok"}, seconds=12.0)
    h.wait(lambda: len(h.images()) == 1, what="the first image")
    h.trigger("shell_composite.cutchange")
    h.wait(lambda: len(h.images()) == 2, what="the changed cell's generation")
    h.trigger("cut_tap.grid", "7")
    h.wait(lambda: len(h.images()) == 3, what="the new load's generation")
    h.trigger("shell_composite.cutclear")       # the grid freed (between loads)
    h.wait_log(r"cut_tap: state NO_GRID")
    h.trigger("shell_composite.cutclear")       # and made again
    h.wait(lambda: len(h.images()) == 4, what="the generation after the grid was made again")
    h.finish()
    imgs = h.images()
    assert len(imgs) == 4, imgs
    check_image(imgs[0], h.control(1), 1)
    check_image(imgs[1], h.control(2), 2, flipped=True)
    check_image(imgs[2], h.control(3), 3, load=7, flipped=True)   # the same bytes, but a new load is published again
    check_image(imgs[3], h.control(4), 4, load=7, flipped=True)
    # the changed cell: tile 12 is pooled (block 2), cell 0x21 = row 2, col 1 of tile (5, 1) -> cell (81, 18)
    a = parse_image(imgs[0].read_bytes())
    c0 = cells(imgs[0].read_bytes()[160:328], imgs[0].read_bytes()[336:])
    c1 = cells(imgs[1].read_bytes()[160:328], imgs[1].read_bytes()[336:])
    diff = [i for i in range(len(c0)) if c0[i] != c1[i]]
    assert diff == [18 * 112 + 81] and c0[diff[0]] ^ c1[diff[0]] == 1, diff
    assert a["generation"] == 1
    assert any("cut_tap: load 1 -> 7" in ln for ln in h.lines())
    assert not loud(h)


@pytest.mark.parametrize("mode,pattern", [
    ("badstate", r"tile 4: state 7 \(0 or 1 expected\)"),
    ("badindex", r"tile 9: pool index 8 of 8 blocks"),
    ("badused", r"7 pooled tiles, the grid says 8 blocks in use"),
    ("badsize", r"grid size 113 x 48 cells"),
    ("badsamples", r"GridHeightmap samples 240 x 72 do not give the grid's 112 x 48 cells"),
    ("badorigin", r"the origin \(-?nan[^,]*, -2\.25\) is not finite"),
])
def test_refused_reads_publish_nothing(host, mode, pattern):
    h = host({"VPSHELL_CUT_TEST": mode}, seconds=4.0)   # below-normal worker: starved for seconds under load (5.6 s once)
    h.wait_log(r"cut_tap: LOUD: load 1: a check failed 4 polls in a row \(read again at every poll, nothing published\): "
               + pattern)
    h.finish()
    assert h.images() == []
    c = h.last()
    assert c["state"] == 0 and c["generation"] == 0 and c["imageName"] == "", c
    assert c["refusedReads"] >= 5, c      # read again at every poll
    assert len(loud(h)) == 1, loud(h)     # LOUD once per reason and load
    first = [ln for ln in h.lines() if "a check failed (read again at the next poll)" in ln]
    assert len(first) == 1 and re.search(pattern, first[0]), first   # the first failure is a plain line


def test_a_failed_check_below_the_loud_streak_is_not_loud(host):
    # a torn read (the EXE stamping during the copy) fails once and passes at the next poll: never LOUD. Here the
    # streak needed for LOUD is longer than the run.
    h = host({"VPSHELL_CUT_TEST": "badstate", "VPSHELL_CUT_LOUD_POLLS": "1000"}, seconds=1.5)
    h.wait_log(r"cut_tap: load 1: a check failed \(read again at the next poll\): tile 4: state 7")
    h.finish()
    assert h.images() == [] and not loud(h)
    assert h.last()["refusedReads"] >= 5


TORN = "the grid's header changed during the copy (a torn read)"


def test_torn_reads_below_the_bound_are_not_loud_and_publish_after(host):
    # 0500: 0414's job 3bce arm 7-sleep: 4 torn polls in a row during a huge grid's growth after the load were LOUD under
    # the old rule (cut_loud_polls 4). Here 30 torn polls in a row (0.6 s at 20 ms) under the default cut_torn_loud_s.
    h = host({"VPSHELL_CUT_TEST": "torn", "VPSHELL_CUT_TEST_TORN": "30"}, seconds=4.0)
    h.wait(lambda: h.images(), what="the image after the torn reads")
    h.finish()
    assert not loud(h), loud(h)
    imgs = h.images()
    assert len(imgs) == 1, imgs
    check_image(imgs[0], h.control(1), 1)   # the header as before the tears (free head 7)
    c = h.last()
    assert (c["state"], c["generation"], c["refusedReads"]) == (2, 1, 30), c
    first = [ln for ln in h.lines() if "a check failed (read again at the next poll)" in ln]
    assert len(first) == 1 and TORN in first[0], first     # one plain line for the run
    assert any("alarm after 4 polls of one failed check or 120 s of torn reads, TEST torn" in ln for ln in h.lines())
    assert any(re.search(r"worker stopped: \d+ polls, 30 refused reads \(30 torn\), generation 1", ln)
               for ln in h.lines()), h.lines()[-3:]
    ready = [ln for ln in h.lines() if "cut_tap: READY generation 1" in ln]
    assert len(ready) == 1 and "30 refused reads" in ready[0], ready


def test_torn_at_every_poll_past_the_bound_is_loud_once(host):
    h = host({"VPSHELL_CUT_TEST": "torn", "VPSHELL_CUT_TEST_TORN": "-1", "VPSHELL_CUT_TORN_LOUD_S": "1"}, seconds=4.0)
    h.wait_log(r"cut_tap: LOUD: load 1: every poll for [0-9.]+ s \(\d+ in a row\) was a torn read: the grid's header "
               r"changed during the copy at each, the EXE does not settle \(cut_torn_loud_s 1; nothing published since\)")
    h.finish()
    assert h.images() == []
    ls = loud(h)
    assert len(ls) == 1, ls                # once per run
    m = re.search(r"every poll for ([0-9.]+) s \((\d+) in a row\)", ls[0])
    assert float(m.group(1)) >= 1.0 and int(m.group(2)) >= 4, ls
    c = h.last()
    assert c["state"] == 0 and c["generation"] == 0 and c["refusedReads"] >= int(m.group(2)), c
    assert not any("polls in a row (read again at every poll" in ln for ln in h.lines())   # not the layout rule


@pytest.mark.parametrize("free", [1, 2])
def test_a_pool_released_during_the_copy_is_a_torn_read(host, free):
    # 0690 (K28): during the growth after a huge load the EXE replaces the pool (the data pointer moves) and releases the
    # old block; a copy that started at the old pointer faults. Under a header that changed since the first read that is
    # a torn read (read again at the next poll), not FAILED EXCEPTION. free=1: the new pointer is stored before the copy;
    # free=2: the old block is released first and the pointer stored at the tap's first re-check after the fault.
    h = host({"VPSHELL_CUT_TEST": "torn", "VPSHELL_CUT_TEST_TORN": "30", "VPSHELL_CUT_TEST_TORN_FREE": str(free)},
             seconds=4.0)
    h.wait(lambda: h.images(), what="the image after the torn reads")
    h.finish()
    assert not loud(h), loud(h)
    imgs = h.images()
    assert len(imgs) == 1, imgs
    check_image(imgs[0], h.control(1), 1)   # the same bytes at the pool's new address
    c = h.last()
    assert (c["state"], c["failReason"], c["generation"], c["refusedReads"]) == (2, 0, 1, 30), c
    first = [ln for ln in h.lines() if "a check failed (read again at the next poll)" in ln]
    assert len(first) == 1, first     # one plain line for the run
    assert re.search(r"the grid's header changed during the copy \(a torn read; the pool at 0x[0-9A-F]{8} \(2048 bytes\) "
                     r"faulted\)", first[0]), first
    each = [ln for ln in h.lines() if re.search(r"cut_tap: load 1: torn read \d+ of this run after a fault", ln)]
    assert len(each) == 19, each      # torn reads 2..20 of the run (the first 20 faults, the first in the run's line)
    assert any(re.search(r"worker stopped: \d+ polls, 30 refused reads \(30 torn\), generation 1; 30 torn reads after a "
                         r"fault \(0690\)", ln) for ln in h.lines()), h.lines()[-3:]


def test_a_pool_released_under_an_unchanged_header_still_fails(host):
    # 0690's negative: the block released and the header still holding its pointer (a dangling pointer, not the growth):
    # FAILED EXCEPTION for the process, as before
    h = host({"VPSHELL_CUT_TEST": "torn", "VPSHELL_CUT_TEST_TORN_FREE": "3"}, seconds=1.5)
    h.wait_log(r"cut_tap: LOUD: FAILED \(2\): a read of EXE memory faulted \(the pool at 0x[0-9A-F]{8} \(2048 bytes\) is not "
               r"readable\): the tap is off for this process - DISABLED for the life of the process")
    h.finish()
    assert h.images() == []
    c = h.last()
    assert (c["state"], c["failReason"], c["polls"], c["refusedReads"]) == (3, 2, 1, 0), c
    assert len(loud(h)) == 1
    assert any("0 torn reads after a fault (0690)" in ln for ln in h.lines())


@pytest.mark.parametrize("mode", ["notmade", "nohm"])
def test_no_grid(host, mode):
    h = host({"VPSHELL_CUT_TEST": mode}, seconds=4.0)   # below-normal worker: starved for seconds under load (5.6 s once)
    h.wait_log(r"cut_tap: state NO_GRID")
    h.finish()
    assert h.images() == []
    c = h.last()
    assert c["state"] == 5 and c["generation"] == 0 and c["refusedReads"] == 0, c
    assert not loud(h)


def test_fault_turns_the_tap_off(host):
    h = host({"VPSHELL_CUT_TEST": "fault"}, seconds=1.5)
    h.wait_log(r"cut_tap: LOUD: FAILED \(2\): a read of EXE memory faulted \(the directory at 0x[0-9A-F]{8} is not readable\): "
               r"the tap is off for this process - DISABLED for the life of the process")
    h.finish()
    assert h.images() == []
    c = h.last()
    assert (c["state"], c["failReason"]) == (3, 2), c
    assert "faulted" in c["failText"]
    assert c["polls"] == 1, c             # nothing read after the fault
    assert len(loud(h)) == 1


def test_real_path_refuses_an_exe_of_no_known_build(host):
    """The host's own image as the EXE: its header names no build the patch knows (ticket 4965, epic 0590): refused before
    any pattern or address is read."""
    h = host({}, args=("--exe", "self"), seconds=1.5)
    h.wait_log(r"cut_tap: LOUD: FAILED \(1\): refused: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, "
               r"SizeOfImage 0x[0-9A-F]{8}\)")
    h.finish()
    assert h.images() == []
    c = h.last()
    assert (c["state"], c["failReason"], c["polls"]) == (3, 1, 0), c
    assert c["failText"].startswith("refused: not an EXE build the patch knows")


def test_unvalidated_exe_is_refused(host):
    h = host({"VPSHELL_CUT_TEST": ""}, args=("--exe", "self", "--unvalidated"), seconds=1.5)
    h.wait_log(r"cut_tap: LOUD: FAILED \(1\): refused: the composite did not validate the EXE")
    h.finish()
    assert h.images() == []
    assert (h.last()["state"], h.last()["failReason"]) == (3, 1)


def pattern_symbols():
    """The symbol of every row of cut_tap.cpp's pattern table, in order."""
    src = (Path(__file__).resolve().parents[1] / "composite" / "cut_tap.cpp").read_text(encoding="utf-8")
    body = src[src.index("kCtSigs[] = {"):src.index("};", src.index("kCtSigs[] = {"))]
    return re.findall(r'X\(VPX_(\w+),', body)


BUILDS = ["4390913", "26577625"]


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("k", [0, 2, 3, 6, 12])
def test_a_broken_pattern_is_refused(host, build, k):
    h = host({"VPSHELL_CUT_TEST": "patterns", "VPSHELL_CUT_TEST_PATTERN": str(k), "VPSHELL_CUT_TEST_BUILD": build}, seconds=1.0)
    sym = pattern_symbols()[k]
    h.wait_log(r"cut_tap: LOUD: FAILED \(1\): refused: pattern '.+?' at %s does not match on Steam build %s " % (sym, build))
    h.finish()
    assert (h.last()["state"], h.last()["failReason"]) == (3, 1)


@pytest.mark.parametrize("build", BUILDS)
def test_all_patterns_in_an_image_match(host, build):
    """A fake image of each known build (its header, every pattern at that build's addresses from the build table)."""
    h = host({"VPSHELL_CUT_TEST": "patterns", "VPSHELL_CUT_TEST_PATTERN": "-1", "VPSHELL_CUT_TEST_BUILD": build}, seconds=1.0)
    h.wait_log(r"cut_tap: test image of Steam build %s " % build)
    h.wait_log(r"cut_tap: 13 byte patterns match")
    h.finish()
    assert not loud(h)
    assert len(pattern_symbols()) == 13


def test_an_image_of_no_known_build_is_refused(host):
    h = host({"VPSHELL_CUT_TEST": "patterns", "VPSHELL_CUT_TEST_BUILD": "0"}, seconds=1.0)
    h.wait_log(r"cut_tap: LOUD: FAILED \(1\): refused: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, "
               r"SizeOfImage 0x06519000\)")
    h.finish()
    assert (h.last()["state"], h.last()["failReason"]) == (3, 1)


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("sym,text", [
    ("cut_grid", r"symbol cut_grid is not carried to Steam build %s "),                    # an address the tap reads
    ("VPX_cut_lookup", r"pattern '.+?': symbol cut_lookup is not carried to Steam build %s "),   # a pattern site
])
def test_a_symbol_not_carried_is_refused(host, build, sym, text):
    h = host({"VPSHELL_CUT_TEST": "patterns", "VPSHELL_CUT_TEST_BUILD": build, "VPSHELL_CUT_TEST_DROP": sym}, seconds=1.0)
    h.wait_log(r"cut_tap: LOUD: FAILED \(1\): refused: " + text % build)
    h.finish()
    assert (h.last()["state"], h.last()["failReason"]) == (3, 1)


def test_switch_off_from_the_start(host, tmp_path):
    d = tmp_path / "off"
    d.mkdir()
    (d / "cut_tap.off").write_text("")
    h = Host(d, {"VPSHELL_CUT_TEST": "ok"}, seconds=1.0)
    try:
        h.finish()
        assert h.images() == []
        c = h.last()
        assert c["state"] == 4 and c["generation"] == 0, c
        assert any("cut_tap: cut_tap=0: OFF published, nothing read" in ln for ln in h.lines())
        assert not any("worker on" in ln for ln in h.lines())    # no worker, no read
        assert not loud(h)
    finally:
        h.kill()


def test_switch_off_during_the_run(host):
    h = host({"VPSHELL_CUT_TEST": "ok"}, seconds=4.0)
    h.wait(lambda: len(h.images()) == 1, what="the first image")
    h.trigger("cut_tap.off")
    h.wait_log(r"cut_tap: state OFF")
    h.finish()
    c = h.last()
    assert c["state"] == 4 and c["generation"] == 0 and c["imageName"] == "", c   # the image is closed
    assert len(h.images()) == 1


def test_no_map_publishes_nothing(host):
    h = host({"VPSHELL_CUT_TEST": "ok"}, args=("--grid", "0"), seconds=1.5)
    h.finish()
    assert h.images() == []
    c = h.last()
    assert c["state"] == 5 and c["polls"] == 0, c
    assert not any("cut_tap: READY" in ln for ln in h.lines())


def test_image_above_the_limit_is_refused(host):
    h = host({"VPSHELL_CUT_TEST": "ok", "VPSHELL_CUT_MAX_KB": "1"}, seconds=1.5)
    h.wait_log(r"cut_tap: LOUD: FAILED \(7\): image of 2384 bytes, the limit is 1024 \(cut_max_kb\) \(this image; the last one stays\)")
    h.finish()
    assert h.images() == []
    c = h.last()
    assert (c["state"], c["failReason"], c["generation"]) == (3, 7, 0), c
    assert len([ln for ln in loud(h) if "FAILED (7)" in ln]) == 1    # not retried while the grid stays the same


# The patterns against the EXE of each known build: test_exe_tables_shell_b.py, from the local per-build tables that
# tools/exebuild/gen_table.py checked against the builds' golden EXEs (ticket 4965, epic 0590; the Steam install, now
# another build, is not read any more).
