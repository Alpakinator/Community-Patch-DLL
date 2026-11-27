"""Tests of render/tools/unitscene (issue 4355): the tool's commands and the oracle driver's guards, synthetic inputs
only (no game file). UNITSCENE = the built unitscene.exe (required: the tests skip with a message without it).

    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 && set UNITSCENE=<render build>/tools/unitscene/unitscene.exe
    python -I -m pytest render/tools/unitscene/test_unitscene.py -q -rs
Every negative case asserts the exit code and the message.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
WORK = Path("C:/c5-64bit-work/0041/4355/pytest")

GOOD = ("c5units\t1\nscene\town\t10.1167316\nplayer\t0\tPLAYERCOLOR_POLYNESIA\tCIVILIZATION_POLYNESIA\tERA_ANCIENT\n"
        "unit\t0\t1063\tUNIT_WARRIOR\tART_DEF_UNIT__WARRIOR\t38\t13\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n"
        "figure\t0\twarrior_idlea.gr2\t1.25\n")


def exe():
    p = os.environ.get("UNITSCENE", "")
    if not p or not os.path.isfile(p):
        pytest.skip("UNITSCENE not set to the built unitscene.exe: the tool's checks did not run")
    return p


@pytest.fixture
def work(tmp_path):
    return tmp_path


def run(args):
    return subprocess.run([exe()] + [str(a) for a in args], capture_output=True, text=True)


def test_usage_errors():
    for args in ([], ["bogus"], ["scene"], ["scene", "--write"], ["pose"], ["pose", "--list", "a"],
                 ["placement", "--game", "x"], ["scene", "--nope", "a.c5units"], ["prepare"],
                 ["prepare", "--game", "x", "--db", "y", "--exe", "z", "--heightfield", "h"], ["prepare", "--bogus"]):
        r = run(args)
        assert r.returncode == 2, args
        assert "usage" in r.stderr and r.stdout == ""


def test_scene_read_and_round_trip(work):
    f = work / "good.c5units"
    f.write_text(GOOD, encoding="utf-8")
    out = work / "rt"
    out.mkdir()
    r = run(["scene", f, "--write", out])
    assert r.returncode == 0, r.stdout + r.stderr
    assert "ok players 1 units 1 figures 1" in r.stdout and "round trip ok" in r.stdout
    assert (out / "good.c5units").read_text(encoding="utf-8") == GOOD


@pytest.mark.parametrize("text,code", [
    ("", "BadHeader"),
    ("c5units\t2\n", "BadHeader at line 1"),
    (GOOD.replace("unit\t0\t1063", "unit\t5\t1063"), "Orphan at line 4"),
    (GOOD + "figure\t0\tx\t0\n", "Duplicate at line 6"),
    (GOOD.replace("\t2\t20\t", "\t9\t20\t"), "BadValue at line 4"),
    (GOOD + "units\t1\n", "BadRecord at line 6"),
])
def test_scene_refused(work, text, code):
    f = work / "bad.c5units"
    f.write_text(text, encoding="utf-8")
    g = work / "good.c5units"
    g.write_text(GOOD, encoding="utf-8")
    r = run(["scene", f, g])   # every file is tried; one refusal makes the exit 1
    assert r.returncode == 1
    assert ("refused " + code) in r.stdout
    assert "good.c5units ok players 1" in r.stdout


def test_scene_missing_file(work):
    r = run(["scene", work / "none.c5units"])
    assert r.returncode == 1 and "refused Io: cannot open the file" in r.stdout


def test_pose_bad_inputs(work):
    lst = work / "list.tsv"
    rows = work / "rows.f32"
    rows.write_bytes(struct.pack("<12f", *([0.0] * 12)))
    r = run(["pose", "--list", work / "none.tsv", "--rows", rows, "--out", work / "o.tsv"])
    assert r.returncode == 1 and "cannot read" in r.stderr
    lst.write_text("header\nonly\tthree\tfields\n", encoding="utf-8")
    r = run(["pose", "--list", lst, "--rows", rows, "--out", work / "o.tsv"])
    assert r.returncode == 1 and "a line with 3 fields (14 or 15 wanted)" in r.stderr
    (work / "odd.f32").write_bytes(b"abc")
    r = run(["pose", "--list", lst, "--rows", work / "odd.f32", "--out", work / "o.tsv"])
    assert r.returncode == 1 and "cannot read" in r.stderr
    # a line whose model file is missing: processed, the row says BadModel, exit 0
    lst.write_text("h\nk1\t%s\t-\tMesh\t%s\t-\t0\t0.14\t30\t0\t0\t0\t0\t1\n" % (work / "m.gr2", work / "c.gr2"),
                   encoding="utf-8")
    r = run(["pose", "--list", lst, "--rows", rows, "--out", work / "o.tsv"])
    assert r.returncode == 0, r.stderr
    out = (work / "o.tsv").read_text(encoding="utf-8").splitlines()
    assert out[1].startswith("k1\tBadModel\t1") and "cannot read" in out[1]


def test_oracle_guards(work):
    sys.path.insert(0, str(HERE))
    import oracle
    assert oracle.main(["--out", str(work / "out"), "--exe", "x.exe"]) == 4   # tmp_path is not a work folder
    WORK.mkdir(parents=True, exist_ok=True)
    assert oracle.main(["--out", str(WORK / "o"), "--exe", "x.exe", "--index", str(work / "none.csv")]) == 3
    assert oracle.main(["--out", str(WORK / "o"), "--exe", str(work / "none.exe")]) == 3
    # a unitscene.exe whose build tree lacks the draw oracle and terrainreplay (B and C need them)
    fake = WORK / "fakebuild" / "tools" / "unitscene"
    fake.mkdir(parents=True, exist_ok=True)
    (fake / "unitscene.exe").write_bytes(b"")
    assert oracle.main(["--out", str(WORK / "o"), "--exe", str(fake / "unitscene.exe")]) == 3


def test_prepare_bad_scene(work):
    f = work / "bad.c5units"
    f.write_text("c5units\t2\n", encoding="utf-8")
    r = run(["prepare", "--game", work / "nogame", "--db", work / "none.db", "--exe", work / "none.exe",
             "--heightfield", work / "none.c5hf", f])
    assert r.returncode == 1 and "unitscene: prepare:" in r.stderr
