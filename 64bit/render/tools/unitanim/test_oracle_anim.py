"""Tests of render/tools/unitanim/oracle_anim.py (issue 4406): the guards (exit codes 2, 3, 4) without any input,
and one positive run of the transitions step on the player's files (UNITANIM = the built unitanim.exe; the positive
case skips with a message without it or without the install copy).

    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 && set UNITANIM=<render build>/tools/unitanim/unitanim.exe
    python -I -m pytest render/tools/unitanim/test_oracle_anim.py -q -rs
"""
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ORACLE = HERE / "oracle_anim.py"
WORK = Path("C:/c5-64bit-work/0374/4406/pytest")


def run(args):
    return subprocess.run([sys.executable, "-I", "-X", "utf8", str(ORACLE)] + [str(a) for a in args],
                          capture_output=True, text=True)


def test_out_outside_work_folders_is_4(tmp_path):
    r = run(["--exe", "x.exe", "--out", tmp_path / "o"])
    assert r.returncode == 4, r.stderr
    assert "not under" in r.stderr


def test_unknown_step_or_clock_is_2():
    r = run(["--exe", "x.exe", "--out", WORK / "u", "--steps", "nonsense"])
    assert r.returncode == 2 and "unknown step" in r.stderr
    r = run(["--exe", "x.exe", "--out", WORK / "u", "--clocks", "steps,wobbly"])
    assert r.returncode == 2 and "wobbly" in r.stderr
    r = run(["--out", WORK / "u"])            # --exe missing: argparse
    assert r.returncode == 2


def test_missing_input_is_3():
    r = run(["--exe", WORK / "no.exe", "--out", WORK / "m", "--steps", "transitions"])
    assert r.returncode == 3 and "input missing" in r.stderr
    exe = os.environ.get("UNITANIM", "")
    if exe:
        r = run(["--exe", exe, "--out", WORK / "m", "--steps", "sequences", "--q1", WORK / "absent.csv"])
        assert r.returncode == 3 and "absent.csv" in r.stderr


def test_transitions_positive():
    exe = os.environ.get("UNITANIM", "")
    if not exe or not os.path.isfile(exe):
        pytest.skip("UNITANIM not set to the built unitanim.exe: the positive run did not run")
    if not os.path.isdir(os.path.expanduser("~/c5-instances/c5h-c/install")):
        pytest.skip("c5h-c's install copy is not on this machine")
    out = WORK / "pos"
    r = run(["--exe", exe, "--out", out, "--steps", "transitions", "--clocks", "steps", "--quick"])
    assert r.returncode == 0, r.stderr
    s = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    tr = s["transitions"]["steps"]
    # the move request on frame 3 starts the run on that frame; the attack request enters combat ready first (state 8)
    assert tr["idle_to_run"]["first_state_change_frame"] == 3
    assert tr["idle_to_attack"]["states"][1] == 8
    assert tr["idle_to_fidget"]["status"][0] == "Ok"
    assert (out / "steps" / "transitions.csv").is_file()


def test_palettes_missing_input_is_3():
    # 4406 r2: the palettes step needs the extracted unit models
    r = run(["--exe", WORK / "no.exe", "--out", WORK / "m", "--steps", "palettes", "--units_dir", WORK / "absent_um"])
    assert r.returncode == 3 and "absent_um" in r.stderr


def test_program_text_form():
    # 4406 r2: programs.csv / masks.csv rows -> unitscene program's text form (oracle_palettes.program_text)
    pytest.importorskip("numpy")
    sys.path.insert(0, str(HERE))
    import oracle_palettes as op
    nodes = [{"kind": "sample", "clip": "A.gr2", "t": "1.5", "a": "-", "b": "-", "w": "-", "mask": "-"},
             {"kind": "sample", "clip": "Aim.gr2", "t": "0.02", "a": "-", "b": "-", "w": "-", "mask": "-"},
             {"kind": "blend", "clip": "-", "t": "-", "a": "0", "b": "1", "w": "1", "mask": "0"}]
    masks = [{"mask": "0", "binding": "TrackMaskAIM_LAYER", "default_weight": "1", "target_weight": "0"}]
    assert op.program_text(nodes, masks) == "S|A.gr2|1.5;S|Aim.gr2|0.02;B|0|1|1|0;M|TrackMaskAIM_LAYER|1|0"
    assert op.program_text(nodes[:1], []) == "S|A.gr2|1.5"


def test_live0395_missing_input_is_3():
    # ticket 0395: the live0395 step needs its table, fits and bones
    r = run(["--exe", WORK / "no.exe", "--out", WORK / "m", "--steps", "live0395", "--table0395", WORK / "absent95.csv"])
    assert r.returncode == 3 and "absent95.csv" in r.stderr


def _oracle_module():
    sys.path.insert(0, str(HERE))
    import importlib
    return importlib.import_module("oracle_anim")


def _fort_table(w1_783_t, horse=(3.192, 0.5977, 0.5084), w1_951=(("1520", 0.5772),)):
    """A synthetic FORT table as l95_table() returns it: W1 at (42,12), W2 at (43,12), the horseman at (44,12)."""
    import collections
    tab = collections.defaultdict(lambda: collections.defaultdict(list))

    def put(fr, plot, ec, t, mesh="Warrior", i=0):
        tab[("FORT", fr)][plot].append({"ec": ec, "t": "%.5f" % t, "mesh": mesh, "id": str(i)})
    for fr, t in zip((690, 783, 951), horse):
        put(fr, "44,12", "1000", t, "Horseman")
    put(690, "42,12", "1500", 0.2668)
    put(783, "42,12", "1520", w1_783_t)
    put(783, "43,12", "1500", 0.3999)
    put(951, "43,12", "1520", 1.3053)
    for k, (ec, t) in enumerate(w1_951):
        put(951, "42,12", ec, t, i=k)
    return tab


def test_live0395_clock_rows_pass_and_fail():
    # the clip-end overflow (8.10) must lie in [0, one 1/30 s step]; a figure off the unit's carried clock is a skip
    oa = _oracle_module()
    rows = oa.l95_clock_rows(_fort_table(2.6665))
    ends = [r for r in rows if r["part"] == "k6_clip_end"]
    assert [r["result"] for r in ends] == ["pass", "pass"]
    after = [r for r in rows if r["part"] == "k6_after_order"]
    assert after and after[0]["result"] == "pass"
    # W1's 1520 0.1 s later than the bystander's clock allows: the overflow is negative, a failure
    rows = oa.l95_clock_rows(_fort_table(2.7665))
    assert [r["result"] for r in rows if r["part"] == "k6_clip_end"][0] == "fail"
    # a figure 0.2 s behind the carried clock after the order is reported as a skip, not a pass
    rows = oa.l95_clock_rows(_fort_table(2.6665, w1_951=(("1520", 0.3772),)))
    assert [r["result"] for r in rows if r["part"] == "k6_after_order"] == ["skip_0.200"]


def test_live0395_positive():
    # the step on 0395's own table and fits (F:), the player's files and the built driver and unitscene
    exe = os.environ.get("UNITANIM", "")
    if not exe or not os.path.isfile(exe):
        pytest.skip("UNITANIM not set to the built unitanim.exe: the positive run did not run")
    if not os.path.isfile("F:/c5-64bit-work/0395/tables/table_0395.csv"):
        pytest.skip("0395's table is not on this machine")
    out = Path("C:/c5-64bit-work/0395/pytest/pos95")
    r = run(["--exe", exe, "--out", out, "--steps", "live0395", "--clocks", "continuous"])
    assert r.returncode == 0, r.stderr
    s = json.loads((out / "summary.json").read_text(encoding="utf-8"))["live0395"]["continuous"]
    assert s["fortify_on"] == {"pass": 2}
    assert s["k6_clip_end"] == {"pass": 2}
    assert s["aim"].get("pass") == 2      # steer selector 0 at w 1 under the mask, and the single-clip reference
    assert s["embark"].get("pass") == 4   # the canoe seen in stop run, at all four frames
