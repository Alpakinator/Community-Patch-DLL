"""Issue 3408: offline tests of shell/tools/bake_live.py (no game, no server: the pure parts and the CLI's refusals)."""
from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOOL = HERE.parent / "tools" / "bake_live.py"
sys.path.insert(0, str(HERE.parent / "tools"))

import bake_live as bl  # noqa: E402

LAND = [(x, y) for x in range(0, 60, 3) for y in range(1, 40, 3)]


def run_cli(*args):
    p = subprocess.run([sys.executable, str(TOOL)] + [str(a) for a in args], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def test_steps_shape_and_revisits():
    st = bl.make_steps(60, 40, (10, 10), LAND, 3408, 24, 10)
    kinds = [s["kind"] for s in st]
    assert kinds[0] == "start" and kinds[1:5] == ["pan"] * 4
    jumps = [s for s in st if s["kind"] in ("jump", "revisit")]
    assert len(jumps) == 24
    assert sum(1 for s in jumps if s["kind"] == "revisit") == 8
    seen = []
    for s in jumps:
        if s["kind"] == "jump":
            seen.append(tuple(s["plot"]))
        else:
            assert tuple(s["plot"]) in seen[:-1]          # an earlier place, never the one just before
    assert all(0 <= x < 60 and 0 <= y < 40 for s in st[1:5] for x, y in s["path"])


def test_steps_deterministic_and_seed_dependent():
    a = bl.make_steps(60, 40, (10, 10), LAND, 3408, 24, 10)
    b = bl.make_steps(60, 40, (10, 10), LAND, 3408, 24, 10)
    c = bl.make_steps(60, 40, (10, 10), LAND, 1, 24, 10)
    assert a == b and a != c


def test_steps_refusals():
    with pytest.raises(ValueError):
        bl.make_steps(60, 40, (10, 10), LAND, 3408, 19, 10)
    with pytest.raises(ValueError):
        bl.make_steps(60, 40, (10, 10), LAND[:5], 3408, 24, 10)


def test_pan_wraps_x_and_clamps_y():
    st = bl.make_steps(20, 10, (18, 8), LAND[:30], 3408, 20, 5)
    east = st[1]["path"]
    assert east[-1][0] == (18 + 5) % 20
    north = st[2]["path"]
    assert max(y for _, y in north) == 9


def write_script(tmp, **over):
    s = {"version": 1, "map": [60, 40], "steps": bl.make_steps(60, 40, (10, 10), LAND, 3408, 24, 10)}
    s.update(over)
    p = tmp / "script.json"
    p.write_text(json.dumps(s))
    return p


def test_load_script_refusals(tmp_path):
    good = write_script(tmp_path)
    assert bl.load_script(good)["map"] == [60, 40]
    s = json.loads(good.read_text())
    bad = tmp_path / "bad.json"
    bad.write_text(json.dumps(dict(s, version=2)))
    with pytest.raises(ValueError):
        bl.load_script(bad)
    s["steps"][5]["plot"] = [99, 1]
    good.write_text(json.dumps(s))
    with pytest.raises(ValueError, match="outside"):
        bl.load_script(good)
    s["steps"][5]["plot"] = [1, 1]
    s["steps"] = s["steps"][:10]
    good.write_text(json.dumps(s))
    with pytest.raises(ValueError, match="fewer than 20"):
        bl.load_script(good)


def test_t_all_cached():
    t0 = 100.0
    mk = lambda pairs: [{"t": t0 + t, "nocache": n} for t, n in pairs]  # noqa: E731
    assert bl.t_all_cached(mk([(0.1, 5), (0.2, 3), (0.3, 0), (0.4, 0)]), t0) == 0.3
    assert bl.t_all_cached(mk([(0.1, 0), (0.2, 4), (0.3, 0)]), t0) == 0.3      # gray again: the later recovery counts
    assert bl.t_all_cached(mk([(0.1, 0), (0.2, 0)]), t0) == 0.0
    assert bl.t_all_cached(mk([(0.1, 2), (0.2, 1)]), t0) is None                 # never all cached
    assert bl.t_all_cached([], t0) is None


LOG = """08:00:00.000 shell_server start pid 1
08:00:01.000 residency 1s: land=100 resident=50 resident_mb=4.2 pending=50 visible=10 visible_land=8 bakes=50 frames=0 gray_frames=0 gray_after_ahead=0 evicted=0 radius=-1 ahead_ms=-1
08:00:02.000 residency: bake-ahead complete: 100 tiles resident (8.3 MB) 1500 ms after the field (radius -1, 100 bakes, 0 failed)
08:00:02.500 residency 1s: land=100 resident=100 resident_mb=8.3 pending=0 visible=10 visible_land=8 bakes=100 frames=60 gray_frames=2 gray_after_ahead=0 evicted=0 radius=-1 ahead_ms=1500
08:00:03.500 residency 1s: land=100 resident=100 resident_mb=8.3 pending=0 visible=12 visible_land=9 bakes=100 frames=120 gray_frames=2 gray_after_ahead=0 evicted=0 radius=-1 ahead_ms=1500
08:00:04.500 mem periodic: private=900.50 MB working=300.00 MB video_local=500.25 MB
08:00:04.600 residency 1s: land=100 resident=100 resident_mb=8.3 pending=0 visible=30 visible_land=20 bakes=103 frames=180 gray_frames=3 gray_after_ahead=1 evicted=0 radius=-1 ahead_ms=1500
"""


def test_server_summary():
    day0 = bl.day_start(time.time())
    run = {"t_start": day0 + 8 * 3600, "t_end": day0 + 8 * 3600 + 5,
           "steps": [{"kind": "start", "plot": [1, 1], "t_look": day0 + 8 * 3600 + 3.0},
                     {"kind": "jump", "plot": [5, 5], "t_look": day0 + 8 * 3600 + 4.0}]}
    s = bl.summarize_server(run, LOG)
    assert len(s["ahead"]) == 1 and "1500 ms" in s["ahead"][0]
    assert s["mem"][0]["video_local"] == 500.25 and s["mem"][0]["private"] == 900.5
    assert s["steps"][1]["gray_after_ahead"] == 1 and s["steps"][1]["bakes"] == 3
    assert s["script_totals"]["gray_after_ahead"] == 1 and s["script_totals"]["frames"] == 120


ROAD_LOG = """08:10:00.000 residency 1s: rebakes=0 raises=0 lat_n=0 lat_max_ms=0.0 lat_mean_ms=-1.0 tilegen_raised=0 frames=1 gray_frames=0 gray_after_ahead=0
08:10:00.700 residency: tile generation block seq 4: 16 tiles changed, re-bake requested
08:10:01.000 residency 1s: rebakes=16 raises=16 lat_n=16 lat_max_ms=420.5 lat_mean_ms=300.0 tilegen_raised=16 frames=2 gray_frames=0 gray_after_ahead=0
"""


def test_road_from_log():
    day0 = bl.day_start(time.time())
    t_road = day0 + 8 * 3600 + 10 * 60 + 0.2
    r = bl.road_from_log(ROAD_LOG, t_road, day0)
    assert r["block_lines"][0]["after_road_s"] == 0.5
    assert r["rebakes"]["rebakes"] == 16 and r["rebakes"]["lat_max_ms"] == 420.5
    assert r["rebakes"]["first_1s_line_after_road_s"] == 0.8
    assert bl.road_from_log("", t_road, day0) == {"block_lines": [], "rebakes": None}


def test_cli_refusals(tmp_path):
    rc, out = run_cli("script", "-i", "c5h-x", "--out", tmp_path / "s.json", "--jumps", "5")
    assert rc == 2 and "jumps" in out
    rc, out = run_cli("run", "-i", "c5h-x", "--script", tmp_path / "missing.json", "--out", tmp_path, "--arm", "stock",
                      "--dry-run")
    assert rc == 2 and "bake_live" in out
    p = write_script(tmp_path)
    rc, out = run_cli("run", "-i", "c5h-x", "--script", p, "--out", tmp_path, "--arm", "stock", "--hold", "0")
    assert rc == 2
    assert not (tmp_path / "run.json").exists()


def test_cli_server_arm_needs_its_inputs(tmp_path):
    p = write_script(tmp_path)
    from tools.harness.instance import Instance   # an instance that exists on this machine, read only
    try:
        Instance.load("c5h-b")
    except Exception:  # noqa: BLE001
        pytest.skip("no instance c5h-b on this machine")
    rc, out = run_cli("run", "-i", "c5h-b", "--script", p, "--out", tmp_path / "o", "--arm", "server", "--dry-run")
    assert rc == 2 and "--server-exe is required" in out and "--section is required" in out
    rc, out = run_cli("run", "-i", "c5h-b", "--script", p, "--out", tmp_path / "o", "--arm", "server", "--dry-run",
                      "--server-exe", tmp_path / "nope.exe", "--exe", tmp_path / "nope2.exe", "--section", "Local\\x",
                      "--bake", "tap")
    assert rc == 2 and "does not exist" in out and "--bake must be" in out
    assert not (tmp_path / "o").exists()
