"""Ticket 0403 (MC #3092): the live FOWLevel-3 particles are byte-identical to main's after 0403's change to
server/src/render/srv_fow_scene.{h,cpp} (the spawn seed read once at init into m_partSeed instead of at each spawn;
particleState / setParticleState added).

The case `particles_sequence` of test_fow_scene.exe (server/tests/render/fow/test_fow_scene.cpp) draws 24 frames of the
fog scene with the real c5r::fow modules at FOWLevel 3, the particles on and C5_RENDER_FOW_PARTICLE_SEED=7 set through
the whole run, and prints per frame "SEQ <k> <SHA-256 of the frame's pixels> <SHA-256 of ParticleSim::saveState>". The
case uses only the API main had, so it is built twice with test_fow_build.py: A from a copy of this branch's tree whose
srv_fow_scene.{h,cpp}, srv_scene_bundle.{h,cpp} and srv_scene_recorder.{h,cpp} are main's (C5_FOW_SCENE_BUILD_MAIN), B
from this branch (C5_FOW_SCENE_BUILD). Each build runs twice (the sequence is deterministic, so the comparison means
something); A's 24 lines must equal B's, and the simulation's state must change in every frame (the particles step).

    set C5_FOW_SCENE_BUILD=<this branch's test_fow_build.py --out>
    set C5_FOW_SCENE_BUILD_MAIN=<the main-file copy's test_fow_build.py --out>
    set C5R_EXE=<CivilizationV_DX11.exe, read only>
    set C5R_GAME_DIR=<a game install, read only>   (the particle module's files; default the owner's pure_vp copy)
    python -m pytest -q -p no:cacheprovider server/tests/render/scenebundle/test_particles_unchanged.py

A missing build FAILS (never skips: COMMON 9). Own code, 2026-11-16 (GPL-3.0-or-later).
"""
import os
import subprocess
from pathlib import Path

import pytest


def exe(var):
    d = os.environ.get(var, "")
    p = Path(d) / "bin" / "test_fow_scene.exe"
    assert d and p.is_file(), "%s must name a test_fow_build.py --out folder with bin/test_fow_scene.exe" % var
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    return str(p)


def sequence(path):
    p = subprocess.run([path, "particles_sequence"], capture_output=True, text=True, timeout=900, creationflags=0x08000000)
    out = p.stdout + p.stderr
    assert p.returncode == 0 and "CASE particles_sequence PASS" in out, out[-3000:]
    lines = [ln for ln in out.splitlines() if ln.startswith("SEQ ")]
    assert len(lines) == 24 and "SEQEND" in out, out[-3000:]
    return lines


def test_live_particles_equal_main():
    main, branch = exe("C5_FOW_SCENE_BUILD_MAIN"), exe("C5_FOW_SCENE_BUILD")
    runs = {(n, k): sequence(b) for n, b in (("main", main), ("branch", branch)) for k in (1, 2)}
    for n in ("main", "branch"):
        assert runs[(n, 1)] == runs[(n, 2)], "%s: two runs differ: the sequence is not deterministic" % n
    pixels = [ln.split()[2] for ln in runs[("branch", 1)]]
    states = [ln.split()[3] for ln in runs[("branch", 1)]]
    # the simulation steps every frame (24 distinct states); the rig's camera takes 5 places (frame % 5) and the particles'
    # moves over 24 frames stay below a pixel of its small target, so the pictures repeat with the camera (measured
    # 2026-11-16: 5 distinct pictures); the particles' effect on the picture is test_fow_scene's `particles` case
    assert len(set(states)) == 24 and len(set(pixels)) >= 5, "the particles do not step (%d distinct frames, %d states)" % (
        len(set(pixels)), len(set(states)))
    diff = [(a, b) for a, b in zip(runs[("main", 1)], runs[("branch", 1)]) if a != b]
    print("test_particles_unchanged: 24 frames, main = branch: %s; first %s; last %s" % (not diff, runs[("branch", 1)][0],
                                                                                        runs[("branch", 1)][-1]))
    assert not diff, "the branch's particles differ from main's in %d of 24 frames, first: %s" % (len(diff), diff[0])


def test_missing_main_build_fails(monkeypatch):
    """The build variable unset or wrong: an assertion, never a skip."""
    monkeypatch.setenv("C5_FOW_SCENE_BUILD_MAIN", "C:/no/such/build_0403")
    with pytest.raises(AssertionError):
        exe("C5_FOW_SCENE_BUILD_MAIN")
