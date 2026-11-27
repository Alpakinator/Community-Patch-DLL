"""Ticket 4932 (epic 0454 X2): tools/x64-dll/build.py gives the DLL compile VP64_RENDER_EVENTS only when the source
carries the feature (CvGameCoreDLL_Expansion2/VP64RenderEvents.h) and --no-render-events is not given; every other
source's compile line stays exactly what it was, so the pins and their locked hashes do not move. Also the switch's
pass-through in build_all.py and server/build_server.py (read from their text: running them builds).

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest server/tests/feed/test_render_events_define.py -q
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
X64 = REPO / "tools" / "x64-dll"


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


b = _load("x64build_4932", X64 / "build.py")
DEFINE = "/DVP64_RENDER_EVENTS"


def _src(tmp_path, header=True, embark=False):
    src = tmp_path / "src"
    (src / "CvGameCoreDLL_Expansion2").mkdir(parents=True)
    if header:
        (src / "CvGameCoreDLL_Expansion2" / "VP64RenderEvents.h").write_text("#pragma once\n", encoding="ascii")
    if embark:
        (src / "CvGameCoreDLL_Expansion2" / "VP64HiddenEmbark.h").write_text("#pragma once\n", encoding="ascii")
    return src


@pytest.mark.parametrize("config", ["release", "debug", "fast"])
@pytest.mark.parametrize("major", [None, "/DMOD_GLOBAL_MAX_MAJOR_CIVS=43"])
def test_the_define_only_with_the_feature_header(tmp_path, config, major):
    src = _src(tmp_path, header=False)
    old = b.cl_args(src, config, True, None, major)
    assert DEFINE not in old                                   # no header: no define, the line as before
    assert b.render_events_state(src) == "absent"
    (src / "CvGameCoreDLL_Expansion2" / "VP64RenderEvents.h").write_text("#pragma once\n", encoding="ascii")
    new = b.cl_args(src, config, True, None, major)
    assert new.count(DEFINE) == 1 and b.render_events_state(src) == "on"
    i = new.index(DEFINE)
    assert new[:i] + new[i + 1:] == old                        # the one define is all that changes
    assert new[i + 1].startswith("/I")


@pytest.mark.parametrize("config", ["release", "fast"])
def test_no_render_events_leaves_it_out(tmp_path, config):
    src = _src(tmp_path)
    off = b.cl_args(src, config, True, None, None, render_events=False)
    on = b.cl_args(src, config, True, None, None)
    assert DEFINE not in off and b.render_events_state(src, False) == "off"
    assert [a for a in on if a != DEFINE] == off               # the define is the only difference


def test_with_hidden_embarks_both_defines_once(tmp_path):
    src = _src(tmp_path, embark=True)
    args = b.cl_args(src, "release", True)
    assert args.count(DEFINE) == 1 and args.count("/DVP64_HIDDEN_EMBARK") == 1
    assert args.index("/DVP64_HIDDEN_EMBARK") < args.index(DEFINE)


def test_a_header_elsewhere_does_not_count(tmp_path):
    src = _src(tmp_path, header=False)
    (src / "VP64RenderEvents.h").write_text("#pragma once\n", encoding="ascii")   # not in the DLL's folder
    (src / "CvGameCoreDLL_Expansion2" / "VP64RenderEvents.h.txt").write_text("x", encoding="ascii")
    assert DEFINE not in b.cl_args(src, "release", True)
    assert b.render_events_state(src) == "absent"


def test_the_define_is_never_in_predefs():
    for cfg, defs in b.PREDEFS.items():
        assert not any("VP64_RENDER_EVENTS" in d for d in defs), cfg


def test_the_switch_is_passed_through():
    ba = (X64 / "build_all.py").read_text(encoding="utf-8")
    bs = (REPO / "server" / "build_server.py").read_text(encoding="utf-8")
    assert "'--no-render-events'" in ba and "dll_cmd += ['--no-render-events'] if a.no_render_events else []" in ba
    assert "'render_events': x64build.render_events_state(src, not a.no_render_events)" in ba
    assert "'--no-render-events'" in bs and "cmd.append('--no-render-events')" in bs
    assert "render_events on" in bs   # a --deps of the other arm is refused
