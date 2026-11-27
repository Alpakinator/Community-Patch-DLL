"""Ticket 0574 (MC #6806/#6824/#6827, with lead-0504): tools/x64-dll/build.py's marker defines. A DLL source that carries
a product feature's marker header in CvGameCoreDLL_Expansion2 gets that feature's defines on the compile line, in
MARKER_DEFINES order (hidden embarks, then 0504's danger package); a source without markers keeps its compile line
byte for byte; VP64_MARKER_DEFINES=0 turns every marker define off; build-summary.json names them; the speed kit's
define-off and as-pin builds always build with the opt-out.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest tools/x64-dll/tests/test_marker_defines.py -q
"""
from __future__ import annotations

import ast
import importlib.util
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent
REPO = X64.parent.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


b = _load("x64build_0574", X64 / "build.py")
HE = "/DVP64_HIDDEN_EMBARK"
DANGER = ["/DVP64_SPEED_DANGER", "/DVP64_SPEED_DANGER_DG7"]


def source(tmp_path, *markers):
    src = tmp_path / "src"
    (src / "CvGameCoreDLL_Expansion2").mkdir(parents=True)
    for m in markers:
        (src / "CvGameCoreDLL_Expansion2" / m).write_text("#pragma once\n", encoding="ascii")
    return src


@pytest.fixture(autouse=True)
def no_opt_out(monkeypatch):
    monkeypatch.delenv(b.MARKER_OPT_OUT_ENV, raising=False)


@pytest.mark.parametrize("config", ["release", "debug", "fast"])
@pytest.mark.parametrize("major", [None, "/DMOD_GLOBAL_MAX_MAJOR_CIVS=43"])
def test_each_marker_both_and_none(tmp_path, config, major):
    cases = {("VP64HiddenEmbark.h",): [HE], ("VP64SpeedDanger.h",): DANGER,
             ("VP64SpeedDanger.h", "VP64HiddenEmbark.h"): [HE] + DANGER}
    for i, (markers, want) in enumerate(cases.items()):
        src = source(tmp_path / str(i))
        plain = b.cl_args(src, config, True, None, major)   # the same folder without markers (paths are in the line)
        assert not any(d in plain for d in [HE] + DANGER)
        for m in markers:
            (src / "CvGameCoreDLL_Expansion2" / m).write_text("#pragma once\n", encoding="ascii")
        got = b.cl_args(src, config, True, None, major)
        at = got.index(want[0])
        assert got[at:at + len(want)] == want                            # in that order, together
        assert got[:at] + got[at + len(want):] == plain                  # nothing else changes
        before = major if major else "/D" + b.PREDEFS[config][-1]
        assert got[at - 1] == before and got[at + len(want)].startswith("/I")


def test_no_marker_line_is_byte_for_byte_the_old_one(tmp_path):
    # the line of a pin source: as before 0573 (no marker define anywhere)
    src = source(tmp_path)
    got = b.cl_args(src, "release", True, None, None)
    assert [a for a in got if "VP64_" in a] == []
    assert b.marker_defines(src) == []


def test_the_opt_out(tmp_path, monkeypatch):
    src = source(tmp_path)
    plain = b.cl_args(src, "release", True)
    for m in ("VP64HiddenEmbark.h", "VP64SpeedDanger.h"):
        (src / "CvGameCoreDLL_Expansion2" / m).write_text("#pragma once\n", encoding="ascii")
    monkeypatch.setenv(b.MARKER_OPT_OUT_ENV, "0")
    assert b.cl_args(src, "release", True) == plain
    rec = b.marker_record(src)
    assert rec == {"marker_defines": [], "markers_found": ["VP64HiddenEmbark.h", "VP64SpeedDanger.h"],
                   "marker_defines_opt_out": True}
    for other in ("1", "", "no", "00"):                                  # only exactly 0 opts out
        monkeypatch.setenv(b.MARKER_OPT_OUT_ENV, other)
        assert b.marker_defines(src) == ["VP64_HIDDEN_EMBARK", "VP64_SPEED_DANGER", "VP64_SPEED_DANGER_DG7"]


def test_the_summary_record(tmp_path):
    assert b.marker_record(source(tmp_path / "a")) == {"marker_defines": [], "markers_found": [],
                                                       "marker_defines_opt_out": False}
    assert b.marker_record(source(tmp_path / "b", "VP64SpeedDanger.h")) == {
        "marker_defines": ["VP64_SPEED_DANGER", "VP64_SPEED_DANGER_DG7"], "markers_found": ["VP64SpeedDanger.h"],
        "marker_defines_opt_out": False}


def test_markers_elsewhere_do_not_count(tmp_path):
    src = source(tmp_path)
    (src / "VP64SpeedDanger.h").write_text("x", encoding="ascii")
    (src / "CvGameCoreDLL_Expansion2" / "VP64SpeedDanger.h.txt").write_text("x", encoding="ascii")
    assert b.marker_defines(src) == []


def test_never_in_predefs_and_never_the_verify_defines():
    for cfg, defs in b.PREDEFS.items():
        assert not any(d.startswith("VP64_") for d in defs), cfg
    given = [d for _, ds in b.MARKER_DEFINES for d in ds]
    assert not any(x in d for d in given for x in ("_VERIFY", "_STATS", "_SPOIL"))


def test_defineoff_builds_with_the_opt_out():
    tree = ast.parse((REPO / "perf" / "speedkit" / "defineoff.py").read_text(encoding="utf-8"))
    envs = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and getattr(n.func, "id", "") == "dict"
            and n.args and ast.unparse(n.args[0]) == "os.environ"]
    assert envs, "defineoff.py builds no env from os.environ"
    for n in envs:
        kw = {k.arg: ast.unparse(k.value) for k in n.keywords}
        assert kw.get("VP64_MARKER_DEFINES") == "'0'", ast.unparse(n)


def test_build_as_pin_runs_build_py_with_the_opt_out(monkeypatch, tmp_path):
    sys.path.insert(0, str(REPO / "perf" / "speedkit"))
    sys.path.insert(0, str(X64))
    bap = _load("build_as_pin_0574", REPO / "perf" / "speedkit" / "build_as_pin.py")
    seen = {}
    monkeypatch.setenv(bap.x64build.MARKER_OPT_OUT_ENV, "1")   # so that the variable is restored after the test
    monkeypatch.setattr(bap.x64build, "main", lambda: seen.setdefault("env", __import__("os").environ.get(
        bap.x64build.MARKER_OPT_OUT_ENV)))
    monkeypatch.setattr(bap.x64build, "read_dll_lock", lambda: {"describe": "Release-5.4.8-1-gabc"})
    monkeypatch.setattr(bap, "make_resolver", lambda c, d: (lambda *a, **k: None))
    assert bap.main(["--commit", "a" * 40, "--describe", "pin", "--", "--src", str(tmp_path)]) == 0
    assert seen["env"] == "0"
