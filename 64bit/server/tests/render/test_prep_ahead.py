"""Ticket 0712: prep ahead (ResidencySourceConfig::prepAhead, C5_RENDER_PREP_AHEAD; research/05-graphics-composition/
terrain-bake-residency.md 9). The pipeline's worker makes the next bakes' CPU products (layer list, slope classes, blend
vertices and indices, grid normals) and a bake takes them or computes inline; the bytes are the same. Off by default.

The rig of test_residency_source.py (render_host_test.exe = the render objects server.exe links; fake composite on a
hidden desktop). The pipeline cases need the player's install (C5R_GAME_DIR, read-only) and 3401's Tiny tap with the
material layer (C5R_TINY_TAP); a missing input fails the test (COMMON 9). Every negative case checks the outputs and the
exit code.
"""
import os
import re

import numpy as np
import pytest

import renderfx as fx
from test_residency_source import GAME_DIR, TINY_TAP, res_lines, res_totals


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


def steps_lines(text):
    """The 'pipeline steps 1s:' lines: (kinds {name: {field: value}}, prep totals {field: value} or {})."""
    out = []
    for ln in text.splitlines():
        if "pipeline steps 1s:" not in ln:
            continue
        parts = ln.split("pipeline steps 1s:", 1)[1].split("|")
        kinds, tot = {}, {}
        for p in parts[1:]:
            p = p.strip()
            if p.startswith("prep tot "):
                tot = {k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", p)}
            else:
                name = p.split(" ", 1)[0]
                kinds[name] = {k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", p)}
        out.append((kinds, tot))
    return out


def run_tiny(rig, monkeypatch, mode, verify="0"):
    if mode is None:
        monkeypatch.delenv("C5_RENDER_PREP_AHEAD", raising=False)
    else:
        monkeypatch.setenv("C5_RENDER_PREP_AHEAD", mode)
    monkeypatch.setenv("C5_RENDER_PREP_VERIFY", verify)
    if not GAME_DIR.joinpath("Assets").exists():
        pytest.fail("C5R_GAME_DIR %s has no Assets/ (the install, read-only)" % GAME_DIR)
    if not TINY_TAP.exists():
        pytest.fail("C5R_TINY_TAP %s missing (3401's Tiny tap with the material layer)" % TINY_TAP)
    payload = TINY_TAP.read_bytes()
    w, h = np.frombuffer(payload[8:16], dtype="<u4")
    rig.tap(load_seq=1, payload=payload, map_w=int(w), map_h=int(h))
    secs = 120 if os.environ.get("C5_ASAN_LOG_DIR") else 45
    p = rig.start_until(bound=secs, game_dir=GAME_DIR, render_tiles="bake")
    m = rig.wait_log(r"residency: bake-ahead complete: (\d+) tiles resident", timeout=secs - 1, p=p)
    rig.wait_next_residency(p, pred=fx.ahead_done)
    rig.wait_log(r"pipeline steps 1s:", timeout=10, p=p)
    assert rig.stop(p, timeout=45) == 0, rig.stdout()[-2000:]
    t = rig.text()
    last = res_lines(t)[-1]
    assert int(m.group(1)) == last["land"] > 1000 and last["failed"] == 0 and last["pending"] == 0, last
    assert res_totals(t)["source failures"] == 0
    return t


def test_prep_ahead_on_takes_the_workers_products(rig, monkeypatch):
    """Mode 1 with the TEST ONLY check: the bake-ahead's idle bakes take products the worker made from the residency's
    hints (hits), every hit equals the inline computation (verified, no mismatch), no job fails, the steps line names the
    idle kind with its hits; the cache stays within its bound."""
    t = run_tiny(rig, monkeypatch, "1", verify="1")
    assert "residency: prep ahead: mode 1 (a worker makes the next bakes'" in t, t[:4000]
    assert "C5_RENDER_PREP_VERIFY=1: every hit checked against the inline computation (TEST ONLY)" in t, t[:4000]
    sl = steps_lines(t)
    assert sl, t[-3000:]
    tot = sl[-1][1]
    assert tot, sl[-1]
    print("prep tot:", tot)
    assert tot["made"] >= 10 and tot["hits"] >= 10, tot
    assert tot["failed"] == 0 and tot["mismatches"] == 0 and tot["verified"] == tot["hits"] + tot["waits"], tot
    assert tot["cache"] <= 32 and tot["cache_kb_max"] < 32 * 400, tot
    idle_hits = sum(k.get("idle", {}).get("hit", 0) for k, _ in sl)
    assert idle_hits >= 10, sl[:3]
    assert "LOUD: pipeline: prep ahead" not in t, t[-3000:]


def test_prep_ahead_off_by_default(rig, monkeypatch):
    """The default (no variable): mode 0; every bake computes inline (no hit, no miss counted), the steps line has no
    prep totals."""
    t = run_tiny(rig, monkeypatch, None)
    assert "residency: prep ahead: mode 0 (off: every bake computes its CPU products inline" in t, t[:4000]
    sl = steps_lines(t)
    assert sl, t[-3000:]
    for kinds, tot in sl:
        assert tot == {}, tot
        for k in kinds.values():
            assert k["hit"] == 0 and k["waited"] == 0 and k["miss"] == 0, kinds


def test_prep_ahead_bad_value_is_named_and_off(rig, monkeypatch):
    """Negative: C5_RENDER_PREP_AHEAD=7: one LOUD line, mode 0."""
    t = run_tiny(rig, monkeypatch, "7")
    assert len(re.findall(r"LOUD: residency: C5_RENDER_PREP_AHEAD is not 0 or 1: the default \(0\) is used", t)) == 1, t[:4000]
    assert "residency: prep ahead: mode 0 (off" in t, t[:4000]


def test_prep_ahead_needs_the_pipeline(rig, monkeypatch):
    """Negative: the stand-in baker has no pipeline: prep ahead is off whatever the switch says (the line names why) and
    no steps line is written."""
    monkeypatch.setenv("C5_RENDER_PREP_AHEAD", "1")
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until(render_tiles="bake", render_baker="standin")
    rig.wait_log(r"residency: prep ahead: off \(the stand-in baker has no pipeline; mode 1 asked", p=p)
    rig.wait_residency(fx.ahead_done, p=p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    assert "pipeline steps 1s:" not in rig.text()
