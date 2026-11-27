"""Ticket 0573 (D193, MC #6794): tools/x64-dll/build.py gives the compile VP64_HIDDEN_EMBARK only when the DLL source
carries the feature (CvGameCoreDLL_Expansion2/VP64HiddenEmbark.h); every other source's compile line stays exactly
what it was, so the pins and their locked hashes do not move.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest tools/x64-dll/tests/test_hidden_embark_define.py -q
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


b = _load("x64build_0573", X64 / "build.py")
DEFINE = "/DVP64_HIDDEN_EMBARK"


@pytest.mark.parametrize("config", ["release", "debug", "fast"])
@pytest.mark.parametrize("major", [None, "/DMOD_GLOBAL_MAX_MAJOR_CIVS=43"])
def test_the_define_only_with_the_feature_header(tmp_path, config, major):
    src = tmp_path / "src"
    (src / "CvGameCoreDLL_Expansion2").mkdir(parents=True)
    old = b.cl_args(src, config, True, None, major)
    assert DEFINE not in old                                  # no header: no define
    (src / "CvGameCoreDLL_Expansion2" / "VP64HiddenEmbark.h").write_text("#pragma once\n", encoding="ascii")
    new = b.cl_args(src, config, True, None, major)
    assert new.count(DEFINE) == 1
    i = new.index(DEFINE)
    assert new[:i] + new[i + 1:] == old                       # the one define is all that changes
    before = major if major else "/D" + b.PREDEFS[config][-1]
    assert new[i - 1] == before and new[i + 1].startswith("/I")


def test_a_header_elsewhere_does_not_count(tmp_path):
    src = tmp_path / "src"
    (src / "CvGameCoreDLL_Expansion2").mkdir(parents=True)
    (src / "VP64HiddenEmbark.h").write_text("#pragma once\n", encoding="ascii")   # not in the DLL's folder
    (src / "CvGameCoreDLL_Expansion2" / "VP64HiddenEmbark.h.txt").write_text("x", encoding="ascii")
    assert DEFINE not in b.cl_args(src, "release", True)


def test_the_define_is_never_in_predefs():
    for cfg, defs in b.PREDEFS.items():
        assert not any("VP64_HIDDEN_EMBARK" in d for d in defs), cfg
