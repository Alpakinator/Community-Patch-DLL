"""Issue 4308 (epic 0169): the polar ice in server.exe's terrain scene (server/src/render/srv_render_scene.cpp) on the
stand-in: the ice cases of server/tests/render/shadow/test_scene_shadow.exe (built by build_scene_shadow.py).

A fake ice source hands the scene a synthetic c5r::ice::Mesh (one quad; the tap's reader is task 4307's); the scene's
c5r::ice::Renderer is started with the EXE's IceShader programs (C5R_EXE) and the install's ice textures (C5R_GAME_DIR,
read-only). Cases: the ice drawn (render_ice=0 and "on without a mesh" bit-equal to each other, the quad covering pixels,
the next frame equal), a mesh the module refuses in play (LOUD, counted, the last good mesh drawn, never fatal), install
failures fatal once (the source's installFailure(); the module that cannot start), the Absent bound
(render_test_ice_wait_ms). A missing binary, C5R_EXE or C5R_GAME_DIR fails (no skip).

    set C5_SHADOW_SCENE_BUILD=C:\\c5-64bit-work\\0169-4308\\shscene
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    set C5R_GAME_DIR=%USERPROFILE%\\Desktop\\pure_vp\\Sid Meier's Civilization V
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/render/ice/scene_ice_test.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["ice_drawn", "ice_refused_keeps_last_good", "ice_install_fatal_once", "ice_absent_bound", "bundle_round_trip"]


def exe():
    d = os.environ.get("C5_SHADOW_SCENE_BUILD", "")
    p = Path(d) / "bin" / "test_scene_shadow.exe"
    assert d and p.exists(), "C5_SHADOW_SCENE_BUILD must name a build_scene_shadow.py --out folder with bin/test_scene_shadow.exe"
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    assert os.environ.get("C5R_GAME_DIR"), "C5R_GAME_DIR must name the game install (read-only): the ice's textures"
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_scene_ice_cases():
    rc, out = run("ice")
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert rc == 0, out


def test_scene_ice_every_case_is_listed_here():
    rc, out = run("ice")
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out


def test_scene_ice_without_install_is_refused_not_skipped():
    env = dict(os.environ)
    env.pop("C5R_GAME_DIR", None)
    p = subprocess.run([exe(), "ice"], capture_output=True, text=True, timeout=120, creationflags=0x08000000, env=env)
    assert p.returncode == 99 and "C5R_GAME_DIR not set" in p.stdout, p.stdout
