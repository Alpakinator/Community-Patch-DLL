"""Ticket 0420: the forest tap of shell_composite.dll (composite/forest_tap.cpp), on its test entry forest_tap_host.exe
(shell/tests/forest_tap_host.cpp: the tap's own OnPresent loop without D3D; a console program started without a window).

forest_test=<mode> feeds a synthetic Forest object (forest_tap.cpp BuildTestWorld) through the real poll, checks and file
writer. Asserted: every byte of the file against the synthetic object recomputed here (layouts, records in list order,
header, FNV-1a 64), one file per set (a second poll of the same set writes nothing), a new generation for a changed set,
a new load, the min-write spacing and the file limit. Negatives (COMMON 9; each checks the files and the log, and that the
host still exits 0): not ready (no file), a layout count past 64, a record of kind 9, a layout index past the count, a
loop in the list, a list shorter than its length field (each LOUD, no file), an unmapped record array (SEH: LOUD, the tap
off for the process), the real path on an EXE of no known build and on an EXE the composite did not validate
(refused for good), a broken pattern in an image that holds all the others, the switch off, no map loaded; ticket 4965:
fake images of both known builds, a header of no build, a symbol not carried.

Needs C5_SHELL_BUILD (shell/build.cmd, which builds forest_tap_host.exe). A missing binary fails the test (no skip).
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

MAGIC = 0x54465056
HDR = 80
LIST = [3, 0, 7, 12, 5, 9, 1, 14, 2, 11]
NLAY = 20


def host_exe() -> Path:
    p = shellrun.bin_dir() / "forest_tap_host.exe"
    assert p.exists(), "forest_tap_host.exe not built in %s: run `cmd /c shell\\build.cmd`" % p.parent
    return p


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def parse_file(b):
    """A forest tap file (forest_tap.cpp, "The file") as a dict: header fields, layouts (lists of 7-float tuples),
    records (dicts). Raises AssertionError on a malformed file."""
    assert len(b) >= HDR
    keys = ["magic", "version", "headerBytes", "pid", "load", "generation", "layoutCount", "recordCount", "randomTrees",
            "griddedTrees", "builtCount", "treeBytes", "recordBytes", "layoutsOffset", "recordsOffset", "fileBytes"]
    h = dict(zip(keys, struct.unpack_from("<16I", b, 0)))
    h["fnv"], h["unixMs"] = struct.unpack_from("<QQ", b, 64)
    assert h["magic"] == MAGIC and h["version"] == 1 and h["headerBytes"] == HDR and h["fileBytes"] == len(b), h
    assert h["treeBytes"] == 28 and h["recordBytes"] == 32
    assert fnv1a64(b[HDR:]) == h["fnv"]
    lays, at = [], h["layoutsOffset"]
    for _ in range(h["layoutCount"]):
        n = struct.unpack_from("<I", b, at)[0]
        at += 4
        lays.append([struct.unpack_from("<7f", b, at + 28 * i) for i in range(n)])
        at += 28 * n
    assert at == h["recordsOffset"]
    recs = []
    for i in range(h["recordCount"]):
        f = struct.unpack_from("<2I3i3I", b, at + 32 * i)
        recs.append(dict(index=f[0], layout=f[1], cube=f[2:5], kind=f[5], built=f[6], decals=f[7]))
    assert at + 32 * h["recordCount"] == len(b)
    assert sum(r["built"] for r in recs) == h["builtCount"]
    return h, lays, recs


def expected(flipped=False):
    lays = []
    for k in range(NLAY):
        n = 180 - (k % 3)
        lays.append([tuple(float(k * 1000 + i) + 0.125 * j for j in range(7)) for i in range(n)])
    recs = []
    for p, idx in enumerate(LIST):
        built = p % 2
        if flipped and p == 1:
            built ^= 1
        recs.append(dict(index=idx, layout=(p * 7) % NLAY, cube=(p, -2 * p, p), kind=1 + p % 4, built=built, decals=p % 3))
    return lays, recs


class Host:
    def __init__(self, tmp: Path, env: dict, args=(), seconds=3.0):
        self.dir = tmp
        self.out = tmp / "out"
        self.out.mkdir()
        self.exe = tmp / "forest_tap_host.exe"
        shutil.copy(host_exe(), self.exe)
        asan = shellrun.bin_dir() / "clang_rt.asan_dynamic-i386.dll"
        if asan.exists():
            shutil.copy(asan, tmp / asan.name)
        self.log = tmp / "host.log"
        e = {k: v for k, v in os.environ.items() if not k.startswith("VPSHELL_FOREST")}
        e.update({"VPSHELL_FOREST_POLL_MS": "20", "VPSHELL_FOREST_MIN_WRITE_MS": "0"})
        e.update({k: str(v) for k, v in env.items()})
        self.p = subprocess.Popen([str(self.exe), "--seconds", str(seconds), "--log", str(self.log), "--out",
                                   str(self.out), *args], env=e, creationflags=0x08000000)

    def files(self):
        d = self.out / "forest_tap"
        return sorted(d.glob("forest_*.bin"), key=lambda f: int(f.stem.split("_")[3])) if d.exists() else []

    def lines(self):
        return self.log.read_text(errors="replace").splitlines() if self.log.exists() else []

    def wait(self, pred, timeout_s=8.0, what=""):
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            if pred():
                return
            time.sleep(0.03)
        raise TimeoutError("%s not reached; files %s; log %s" % (what, self.files(), self.lines()[-8:]))

    def wait_log(self, pattern, timeout_s=8.0):
        self.wait(lambda: any(re.search(pattern, ln) for ln in self.lines()), timeout_s, "log line %r" % pattern)

    def trigger(self, name, text=""):
        (self.dir / name).write_text(text)

    def finish(self, timeout_s=30):
        rc = self.p.wait(timeout_s)
        assert rc == 0, (rc, self.lines()[-10:])
        assert any("forest_tap_host:" in ln and "Presents" in ln for ln in self.lines())
        assert not list(self.out.glob("forest_tap/*.tmp"))
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


def check_file(f, flipped=False, load=1, generation=1, pid=None):
    h, lays, recs = parse_file(f.read_bytes())
    want_lays, want_recs = expected(flipped)
    assert (h["load"], h["generation"], h["layoutCount"], h["recordCount"]) == (load, generation, NLAY, len(LIST)), h
    assert (h["randomTrees"], h["griddedTrees"]) == (180, 0)
    if pid is not None:
        assert h["pid"] == pid
    assert f.name == "forest_%d_%d_%d.bin" % (h["pid"], load, generation)
    assert lays == want_lays
    assert recs == want_recs
    return h


def test_ok_one_file_per_set(host):
    h = host({"VPSHELL_FOREST_TEST": "ok"}, seconds=3.0)
    h.wait(lambda: h.files(), what="the first file")
    h.finish()
    fs = h.files()
    assert len(fs) == 1, fs          # the same set at every later poll: no second file
    check_file(fs[0], pid=h.p.pid)
    assert any("forest_tap: 12 byte patterns match (test world)" in ln for ln in h.lines())
    assert any(re.search(r"load 1 generation 1: 20 layouts \(3581 trees; random 180, gridded 0\), 10 records "
                         r"\(built 5, decal handles 9; kinds 1/2/3/4 3/3/2/2\)", ln) for ln in h.lines()), h.lines()[-5:]
    assert not any("LOUD" in ln for ln in h.lines())


def test_change_new_generation_and_new_load(host):
    # the host lives long enough for three waits under load (a 4 s host failed once while the machine built)
    h = host({"VPSHELL_FOREST_TEST": "ok"}, seconds=9.0)
    h.wait(lambda: len(h.files()) == 1, what="the first file")
    h.trigger("shell_composite.forestchange")
    h.wait(lambda: len(h.files()) == 2, what="the second generation")
    h.trigger("forest_tap.grid", "7")
    h.wait(lambda: len(h.files()) == 3, what="the file of the new load")
    h.finish()
    fs = h.files()
    assert len(fs) == 3, fs
    check_file(fs[0], generation=1)
    check_file(fs[1], flipped=True, generation=2)
    check_file(fs[2], flipped=True, load=7, generation=3)   # the same set, but a new load is written again
    assert any("forest_tap: load 1 -> 7" in ln for ln in h.lines())


def test_min_write_spacing_and_file_limit(host):
    h = host({"VPSHELL_FOREST_TEST": "ok", "VPSHELL_FOREST_MIN_WRITE_MS": "60000", "VPSHELL_FOREST_MAX_FILES": "1"},
             seconds=3.0)
    h.wait(lambda: len(h.files()) == 1, what="the first file")
    h.trigger("shell_composite.forestchange")
    time.sleep(1.0)
    assert len(h.files()) == 1        # the spacing holds the second generation back
    h.finish()
    assert len(h.files()) == 1
    h2 = host({"VPSHELL_FOREST_TEST": "ok", "VPSHELL_FOREST_MAX_FILES": "1"}, seconds=6.0)
    h2.wait(lambda: len(h2.files()) == 1, what="the first file")
    h2.trigger("shell_composite.forestchange")
    h2.wait_log(r"LOUD: forest_max_files \(1\) reached")
    h2.finish()
    assert len(h2.files()) == 1


def test_shutdown_rebuilds_without_a_second_file(host):
    h = host({"VPSHELL_FOREST_TEST": "ok"}, seconds=6.0)
    h.wait(lambda: len(h.files()) == 1, what="the first file")
    h.trigger("forest_tap.shutdown")
    h.wait_log(r"forest_tap: shutdown \(host: forest_tap.shutdown\)")
    h.wait(lambda: sum("byte patterns match" in ln for ln in h.lines()) == 2, what="the second set-up")
    h.finish()
    assert len(h.files()) == 1        # the same set after the rebuild: nothing new


@pytest.mark.parametrize("mode,loud", [
    ("badcount", r"LOUD: load 1: a check failed .*layout count 1000 \(1\.\.64 expected\)"),
    ("badkind", r"LOUD: load 1: a check failed .*record 5: kind 9 \(1\.\.4 expected\)"),
    ("badindex", r"LOUD: load 1: a check failed .*record 7: layout index 20 of 20"),
    ("cycle", r"LOUD: load 1: a check failed .*longer than its length 10"),
    ("short", r"LOUD: load 1: a check failed .*the list holds 10 records, its length says 11"),
])
def test_bad_layouts_write_nothing(host, mode, loud):
    h = host({"VPSHELL_FOREST_TEST": mode}, seconds=3.0)
    h.wait_log(loud)
    h.finish()
    assert h.files() == []
    louds = [ln for ln in h.lines() if "LOUD" in ln]
    assert len(louds) == 1, louds      # once per reason and load, not once per poll


def test_not_ready_writes_nothing(host):
    h = host({"VPSHELL_FOREST_TEST": "notready"}, seconds=3.0)
    h.wait_log(r"load 1: the Forest system is not ready")
    h.finish()
    assert h.files() == []
    assert not any("LOUD" in ln for ln in h.lines())


def test_fault_turns_the_tap_off(host):
    h = host({"VPSHELL_FOREST_TEST": "fault"}, seconds=3.0)
    h.wait_log(r"LOUD: a read of EXE memory faulted \(record 3 at 0x[0-9A-F]+ is not readable\): the tap is off")
    h.finish()
    assert h.files() == []
    assert sum("faulted" in ln for ln in h.lines()) == 1


def test_real_path_of_no_known_build_is_refused(host):
    """The host's own image as the EXE: its header names no build the patch knows (ticket 4965, epic 0590)."""
    h = host({}, args=("--exe", "self"), seconds=3.0)
    h.wait_log(r"LOUD: refused for good: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage "
               r"0x[0-9A-F]{8}\)")
    h.finish()
    assert h.files() == []


def test_unvalidated_exe_is_refused(host):
    h = host({}, args=("--exe", "self", "--unvalidated"), seconds=3.0)
    h.wait_log(r"LOUD: refused for good: the composite did not validate the EXE")
    h.finish()
    assert h.files() == []


BUILDS = ["4390913", "26577625"]


def pattern_symbols():
    """The symbol of every row of forest_tap.cpp's pattern table, in order."""
    src = (Path(__file__).resolve().parents[1] / "composite" / "forest_tap.cpp").read_text(encoding="utf-8")
    body = src[src.index("kFtSigs[] = {"):src.index("};", src.index("kFtSigs[] = {"))]
    return re.findall(r'X\(VPX_(\w+),', body)


@pytest.mark.parametrize("build", BUILDS)
def test_patterns_all_match_in_an_image_that_holds_them(host, build):
    """A fake image of each known build: its header and every pattern at that build's addresses (the build table)."""
    h = host({"VPSHELL_FOREST_TEST": "patterns", "VPSHELL_FOREST_TEST_PATTERN": "-1", "VPSHELL_FOREST_TEST_BUILD": build}, seconds=3.0)
    h.wait_log(r"forest_tap: test image of Steam build %s " % build)
    h.wait_log(r"forest_tap: 12 byte patterns match \(test world\)")
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())
    assert len(pattern_symbols()) == 12


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("k", [0, 7, 11])
def test_a_broken_pattern_is_refused(host, build, k):
    h = host({"VPSHELL_FOREST_TEST": "patterns", "VPSHELL_FOREST_TEST_PATTERN": str(k), "VPSHELL_FOREST_TEST_BUILD": build}, seconds=3.0)
    h.wait_log(r"LOUD: refused for good: pattern '.+?' at %s does not match on Steam build %s " % (pattern_symbols()[k], build))
    h.finish()
    assert h.files() == []


def test_an_image_of_no_known_build_is_refused(host):
    h = host({"VPSHELL_FOREST_TEST": "patterns", "VPSHELL_FOREST_TEST_BUILD": "0"}, seconds=3.0)
    h.wait_log(r"LOUD: refused for good: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage 0x06519000\)")
    h.finish()
    assert h.files() == []


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("sym,text", [
    ("forest_system", r"symbol forest_system is not carried to Steam build %s "),
    ("forest_tree", r"pattern '.+?': symbol forest_tree is not carried to Steam build %s "),
])
def test_a_symbol_not_carried_is_refused(host, build, sym, text):
    h = host({"VPSHELL_FOREST_TEST": "patterns", "VPSHELL_FOREST_TEST_BUILD": build, "VPSHELL_FOREST_TEST_DROP": sym}, seconds=3.0)
    h.wait_log(r"LOUD: refused for good: " + text % build)
    h.finish()
    assert h.files() == []


def test_switch_off_and_no_map(host, tmp_path):
    h = host({"VPSHELL_FOREST_TEST": "ok"}, seconds=3.0)
    (h.dir / "forest_tap.off").write_text("")   # the host reads it at every call; written before the first poll is due
    h.finish()
    # the file may have been written by a first call before the switch file existed: the switch then stops the rest
    assert len(h.files()) <= 1
    h2 = host({"VPSHELL_FOREST_TEST": "ok"}, args=("--grid", "0"), seconds=3.0)
    h2.finish()
    assert h2.files() == []
    assert not any("forest_tap: load" in ln for ln in h2.lines())


# The patterns against the EXE of each known build: test_exe_tables_shell_b.py, from the local per-build tables that
# tools/exebuild/gen_table.py checked against the builds' golden EXEs (ticket 4965, epic 0590; the Steam install, now
# another build, is not read any more). 0420's lesson stays covered: gen_table checks each row at its symbol's address in
# the real EXE of each build (a pattern one byte off its instruction is BAD there).


def test_switch_off_from_the_start(tmp_path):
    d = tmp_path / "off"
    d.mkdir()
    (d / "forest_tap.off").write_text("")
    h = Host(d, {"VPSHELL_FOREST_TEST": "ok"}, seconds=1.0)
    try:
        h.finish()
        assert h.files() == []
        assert not any("forest_tap:" in ln for ln in h.lines())
    finally:
        h.kill()
