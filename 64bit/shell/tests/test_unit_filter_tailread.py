"""Ticket 0758 (route A of research/05-graphics-composition/frame-cost-exe-side.md section 7): the composite's tail read at
Unmap (shell/composite/unit_filter.cpp OnUnmap, the pages of a cached map's [0, need) the EXE did not write, read from the
driver's pointer) is done with streaming loads (shell/composite/tail_read.h: AVX2 VMOVNTDQA, else SSE4.1 MOVNTDQA, else
memcpy; the level chosen once at the init from CPUID), on the same thread, in the same order, with the same bytes.

Two parts:
  1. Offline, the bench's cases (shell/evidence/0757/tailread/tailread.cpp): unit_filter_host.exe --tailread reads a 256 KB
     DYNAMIC vertex buffer mapped WRITE_DISCARD at every level this CPU has, over the bench's five tails and over edge cases
     (unaligned source, lengths around 16, 32, 64 bytes and a page, destinations at offsets 1 and 8). Every read must be
     byte-equal to memcpy's of the same map, write nothing outside its destination, and (when the host wrote the tail in
     the map) equal the host's bytes; the same on ordinary memory.
  2. In the composite, on the model stand-in (test_model_filter.py's host, the split map pattern, whose cached map leaves
     the second half of the instance buffer unwritten below the draws' mark: a tail every frame): the TEST ONLY key
     model_tail_stream forces each level (0, sse41, avx2; auto is the CPU's best) and verify reads every tail run again with
     memcpy (tail_diff must be 0). model_map_cache=verify compares every record read with the direct path's copy (rec_diff
     0), and the host checks the driver's buffers (--map-check) and the records against what it drew.

Run: `python -I shell/tests/test_unit_filter_tailread.py` (or through pytest with PYTEST_DISABLE_PLUGIN_AUTOLOAD=1) with
C5_SHELL_BUILD set to a build of shell/build.cmd (plain or asan). A missing binary fails (no skip).
"""
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import pytest  # noqa: E402

import shellrun  # noqa: E402
from test_unit_filter import host_exe  # noqa: E402
from test_model_filter import MAP_MODES, check_drops, map_lines, mf, mtable  # noqa: E402,F401 (mf, mtable: fixtures)

LEVELS = ["memcpy", "sse4.1", "avx2"]
BENCH_TAILS = [4096, 16384, 40960, 86016, 131072]
EDGE_CASES = 6 * 14 * 3          # source offsets x lengths x destination offsets (unit_filter_host.cpp TailReadBench)
KINDS = [("dyn", "half"), ("dyn", "full"), ("cached", "-")]


def tc_lines(lines):
    out = []
    for ln in lines:
        m = re.search(r"\] TC (.*)$", ln)
        if m:
            out.append(dict(re.findall(r"(\w+)=(\S+)", m.group(1))))
    return out


def test_tail_read_offline_bench_cases(tmp_path, hidden_desktop, built):
    """Every level this CPU has, over the bench's tails and the edge cases: byte-equal to memcpy, nothing written outside
    the destination, the host's bytes where it wrote them."""
    log = tmp_path / "tailread.log"
    p = shellrun.launch([str(host_exe()), "--tailread", "40", "--log", str(log)], hidden_desktop)
    code = p.wait(300)
    if code is None:
        p.kill()
    p.close()
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    assert code == 0, (code, lines[-12:])
    cpu = [re.search(r"\] TR cpu=(\S+) adapter=(\S*) vendor=(\S+)", ln) for ln in lines]
    cpu = [m for m in cpu if m]
    assert len(cpu) == 1, lines[:4]
    best = LEVELS.index(cpu[0].group(1))
    tc = tc_lines(lines)
    want_levels = LEVELS[:best + 1]
    for src, fill in KINDS:
        for lv in want_levels:
            mine = [x for x in tc if x["src"] == src and x["fill"] == fill and x["level"] == lv]
            assert len(mine) == 2 * len(BENCH_TAILS) + EDGE_CASES, (src, fill, lv, len(mine))
            bench = [x for x in mine if x["soff"] == "0" and int(x["rounds"]) == 40]
            assert sorted({int(x["n"]) for x in bench}) == BENCH_TAILS, bench
    assert {x["level"] for x in tc} == set(want_levels), {x["level"] for x in tc}
    bad = [x for x in tc if x["bad"] != "0" or x["guard"] != "0" or x["pattern"] != "0"]
    assert not bad, bad[:8]
    total = [re.search(r"\] TR cases=(\d+) bad=(\d+) guard=(\d+) pattern=(\d+)", ln) for ln in lines]
    total = [m for m in total if m]
    assert total and total[-1].groups() == (str(len(tc)), "0", "0", "0"), total[-1].groups() if total else lines[-4:]


def cpu_level(mf):
    m = [re.search(r"model_filter: tail read: (\S+) \(this CPU: (\S+)\)", ln) for ln in mf.lines()]
    m = [x for x in m if x]
    assert len(m) == 1, [ln for ln in mf.lines() if "tail read" in ln]
    return m[0].group(1), m[0].group(2)


@pytest.mark.parametrize("key,cache", [("", "1"), ("", "verify"), ("0", "verify"), ("sse41", "verify"), ("avx2", "verify"),
                                       ("verify", "verify"), ("verify", "1")])
def test_model_tail_stream_levels_same_bytes(mf, key, cache):
    """Each level in the composite: the drops and records of the direct path, the driver's buffers as written, every record
    read equal to the direct path's copy (cache=verify), and with key verify every tail run equal to memcpy's read."""
    kv = dict(model_map_cache=cache)
    if key:
        kv["model_tail_stream"] = key
    mf.run.set_ini(**kv)
    mf.go("--frames", "8", "--word", "0:6", "--map-check", *MAP_MODES["split"])
    check_drops(mf.mframes(), 6, 3)
    b = map_lines(mf)
    assert [x["f"] for x in b] == list(range(8)), b
    assert all(x["inst"] == "ok" and x["ids"] == "ok" and x["pal"] == "ok" for x in b), b
    t = mf.mtotals()
    level, cpu = cpu_level(mf)
    ci = LEVELS.index(cpu)
    want = {"": ci, "verify": ci, "0": 0, "sse41": min(1, ci), "avx2": min(2, ci)}[key]
    assert level == LEVELS[want] and t["tail_read"] == LEVELS[want], (level, cpu, t["tail_read"])
    assert t["map_cached"] > 0 and t["tail_kb"] > 0 and t["stage_fail"] == 0 and t["ww_fail"] == 0, t
    assert float(t["tail_ms"]) >= 0.0, t
    if cache == "verify":
        assert t["verify_bad"] == 0 and t["rec_diff"] == 0 and t["late_pages"] == 0, t
    if key == "verify":
        assert t["tail_diff"] == 0 and t["tail_vkb"] >= t["tail_kb"], t
        assert any("model_tail_stream=verify - TEST ONLY" in ln for ln in mf.lines())
    else:
        assert t["tail_diff"] == 0 and t["tail_vkb"] == 0, t
    if key in ("0", "sse41", "avx2"):
        assert any("model_tail_stream - TEST ONLY: the level forced" in ln for ln in mf.lines())
    if key in ("sse41", "avx2") and LEVELS.index(LEVELS[want]) < {"sse41": 1, "avx2": 2}[key]:
        assert any("model_tail_stream=%s: this CPU has" % key in ln for ln in mf.lines())


def test_model_tail_stream_bad_value(mf):
    """A value that is not a level: a CONFIG ERROR line, and the CPU's best."""
    mf.run.set_ini(model_tail_stream="fast")
    mf.go("--frames", "4", "--word", "0:6", *MAP_MODES["split"])
    level, cpu = cpu_level(mf)
    assert level == cpu, (level, cpu)
    assert any("CONFIG ERROR model_tail_stream='fast' (auto, 0, sse41, avx2 or verify): auto" in ln for ln in mf.lines())
    assert mf.mtotals()["tail_read"] == cpu


if __name__ == "__main__":
    os.environ.setdefault("PYTEST_DISABLE_PLUGIN_AUTOLOAD", "1")
    sys.exit(pytest.main([str(Path(__file__)), "-p", "no:cacheprovider", "-q"] + sys.argv[1:]))
