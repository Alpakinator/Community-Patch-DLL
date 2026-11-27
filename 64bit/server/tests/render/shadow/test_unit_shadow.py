"""Ticket 4785 (epic 0042): the server's unit shadow (server/src/render/srv_unit_shadow.cpp) on WARP, with the EXE's
ShadowShader resample programs read from C5R_EXE (read-only).

Runs test_unit_shadow.exe (built by build_unit_shadow.py into C5_UNIT_SHADOW_BUILD/bin, or bin-asan with
C5_UNIT_SHADOW_ASAN=1): every case must say PASS and the exit code be 0; an unknown case is refused; and the GPU resample
of four captured ShadowRaw / ShadowBlur pairs of the stock game (3907's exports: L01, S01, c_ancient, L10; the frame-start
contents of both, as the spike's resample_check.py read them, unit-combat-server.md 9.2 R1) is within 256 / 65535 of the
captured blur, 97 % of the shaded texels within 64. A missing binary, C5R_EXE or exports folder fails (no skip).

    set C5_UNIT_SHADOW_BUILD=C:\\c5-64bit-work\\0042\\w21\\b\\us
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    set C5_3907_EXPORTS=F:\\c5-64bit-work\\0301\\3907\\exports
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/render/shadow/test_unit_shadow.py -q -p no:cacheprovider
"""
import json
import os
import subprocess
from pathlib import Path

import pytest

CASES = ["wanted", "init_refused", "resample_cpu", "merge_adds", "merge_q1", "refusals", "targets_again"]
PAIRS = ["units_land/L01", "units_sea/S01", "cities/c_ancient", "units_land/L10"]


def exe():
    d = os.environ.get("C5_UNIT_SHADOW_BUILD", "")
    p = Path(d) / ("bin-asan" if os.environ.get("C5_UNIT_SHADOW_ASAN") == "1" else "bin") / "test_unit_shadow.exe"
    assert d and p.exists(), "C5_UNIT_SHADOW_BUILD must name a build_unit_shadow.py --out folder holding the test binary"
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_unit_shadow_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert rc == 0, out


def test_unit_shadow_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_unit_shadow_every_case_is_listed_here():
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out


def test_unit_shadow_pair_usage_is_refused():
    rc, out = run("pair", "x")
    assert rc == 2 and "usage" in out, out


def _pair_files(export):
    """The resample draw's source (ShadowRaw) and target (ShadowBlur) texture files of one export (as resample_check.py)."""
    d = json.load(open(export / "draws.json"))
    for x in d["draws"]:
        if (x.get("vs"), x.get("ps")) == ("c7615a1d", "be955cbb"):
            s = json.load(open(export / x["dir"] / "state.json"))
            src = s["PS"]["srv"]["0"]["data"]["res"]
            dst = s["om"]["rtv"][0]["res"]
            names = os.listdir(export / "textures")
            pick = lambda r: next(n for n in names if n.startswith("%d_" % r) and n.endswith(".bin"))
            return pick(src), pick(dst)
    raise AssertionError("no resample draw in %s" % export)


def _desc(export, name):
    meta = json.load(open(export / "textures" / (name + ".json")))
    d = meta["desc"]
    return d["Width"], d["Height"], meta["row_pitch"][0], d["Format"]["s"]


@pytest.mark.parametrize("pair", PAIRS)
def test_unit_shadow_gpu_resample_against_captured_pairs(pair):
    root = Path(os.environ.get("C5_3907_EXPORTS", "F:/c5-64bit-work/0301/3907/exports"))
    export = root / pair
    assert export.is_dir(), "the 3907 export %s is missing (C5_3907_EXPORTS)" % export
    m, b = _pair_files(export)
    mw, mh, mp, mf = _desc(export, m)
    bw, bh, bp, bf = _desc(export, b)
    assert mf == "DXGI_FORMAT_R16G16B16A16_FLOAT" and bf == "DXGI_FORMAT_R16_UNORM", (mf, bf)
    rc, out = run("pair", str(export / "textures" / m), str(mw), str(mh), str(mp), str(export / "textures" / b), str(bw), str(bh), str(bp))
    print(pair, out.strip())
    assert rc == 0 and "CASE pair PASS" in out, out
