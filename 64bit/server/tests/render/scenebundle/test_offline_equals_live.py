"""Task 3905 (epic 0301): the standing check "offline equals live" (P1) on the scene library's recorded bundles.

For every bundle in the library (C5_SCENE_LIBRARY, default F:/c5-64bit-work/scene-library/bundles; one folder per bundle,
read in place, nothing written there), runs the player

    sceneshot.exe --bundle B --out TMP --dumped --compare --no-pictures [--adapter bundle]

and asserts exit 0, the last line "sceneshot: OK ...", one COMPARE line per dumped frame with depth EQUAL, and every shot
in sceneshot.json compared and within the tolerance. The tolerance is the MC's accepted standing tolerance 1:1 (MC #1403,
2026-11-15: at most 1 colour pixel per frame, at most 1 in any channel; depth always byte-equal), because the water drawn
over the overlay is not bit-stable run to run; it goes back to 0:0 (equal bytes) when follow-up 0336 lands.
C5_P1_TOLERANCE (PIXELS:DELTA) overrides it. The tolerance in force is printed (run pytest with -s to see it) and named in
every assertion message (contract section 16). It runs again whenever a bundle is
recorded again (3904's recorder, 3906's runs). A missing player, a missing library or a library without a bundle FAILS:
a skip counts as failed (COMMON 9). Runs on the machine of the bundle's adapter (the player picks it by the manifest;
another adapter is not P1 and the player exits 7 NoDevice).

    set C5_SCENESHOT_EXE=C:\\c5-64bit-work\\0301\\3905\\ss\\bin\\sceneshot.exe
    python -I -m pytest server/tests/render/scenebundle/test_offline_equals_live.py -q -s -p no:cacheprovider
"""
import json
import os
import subprocess
from pathlib import Path

import pytest

LIBRARY = Path(os.environ.get("C5_SCENE_LIBRARY", "F:/c5-64bit-work/scene-library/bundles"))
ACCEPTED = "1:1"   # MC #1403 (2026-11-15); back to "0:0" when 0336 (water over overlay not bit-stable) lands
TOLERANCE = os.environ.get("C5_P1_TOLERANCE", ACCEPTED)
MODEL_GEOM_GAP = "gap: models: CPU-built meshes"   # ticket 4883: sb::kModelGeomNote (srv_scene_bundle.h), its exact prefix
TOLERANCE_NOTE = "tolerance %s (%s; the MC's standing tolerance is %s, MC #1403, back to 0:0 when 0336 lands)" % (
    TOLERANCE, "C5_P1_TOLERANCE" if "C5_P1_TOLERANCE" in os.environ else "default", ACCEPTED)


def bundles():
    if not LIBRARY.is_dir():
        return ["<no library at %s>" % LIBRARY]
    found = sorted(p.name for p in LIBRARY.iterdir() if (p / "scene.json").is_file())
    return found or ["<no bundle in %s>" % LIBRARY]


def player():
    exe = os.environ.get("C5_SCENESHOT_EXE", "")
    assert exe and Path(exe).is_file(), "C5_SCENESHOT_EXE must name the built sceneshot.exe (server/tools/sceneshot/build_sceneshot.py)"
    return exe


@pytest.mark.parametrize("bundle", bundles())
def test_offline_equals_live(bundle, tmp_path):
    assert not bundle.startswith("<"), bundle   # no library or no bundle: FAIL, never skip
    b = LIBRARY / bundle
    scene = json.loads((b / "scene.json").read_text(encoding="utf-8"))
    dumped = scene["frames"]["dumped"]
    assert len(dumped) >= 2, "a bundle needs at least two dumped frames (done item 1): %s" % dumped
    # ticket 4883: a bundle whose recorder noted the CPU-built meshes it could not record (ticket 4879's geometry section,
    # no bundle member yet) cannot replay equal to live; keyed on that exact note, never on the bundle's name. Reported as
    # xfailed with the note (not a skip: run_all counts a skip as failed, COMMON 9; never a pass).
    gaps = [n for n in scene.get("version_notes", []) if n.startswith(MODEL_GEOM_GAP)]
    if gaps:
        pytest.xfail("%s: not P1 (offline cannot equal live): %s" % (bundle, gaps[0]))
    tol = TOLERANCE
    print("test_offline_equals_live: %s: %s" % (bundle, TOLERANCE_NOTE))
    cp = subprocess.run([player(), "--bundle", str(b), "--out", str(tmp_path), "--dumped", "--compare", "--no-pictures", "--tolerance", tol],
                        capture_output=True, text=True, errors="replace", timeout=3600, creationflags=0x08000000)
    out = TOLERANCE_NOTE + "\n" + cp.stdout + cp.stderr
    lines = [ln for ln in (cp.stdout + cp.stderr).splitlines() if ln.strip()]
    assert cp.returncode == 0, out
    assert lines and lines[-1].startswith("sceneshot: OK %d shot(s), each " % len(dumped)), out
    compares = [ln for ln in lines if ln.startswith("sceneshot: COMPARE frame ")]
    assert len(compares) == len(dumped), out
    assert all("depth EQUAL" in ln for ln in compares), out
    if tol == "0:0":
        assert all("colour EQUAL" in ln for ln in compares), out
    res = json.loads((tmp_path / "sceneshot.json").read_text(encoding="utf-8"))
    assert res["same_adapter"] is True, res
    assert sorted(s["frame_id"] for s in res["shots"]) == sorted(dumped), res
    assert all(s["compared"] and s["within_tolerance"] and s["depth_equal"] for s in res["shots"]), res


def test_missing_bundle_fails(tmp_path):
    """A bundle that is not there fails with the checker's NoManifest (21), never a skip, and writes nothing."""
    out = tmp_path / "out"
    cp = subprocess.run([player(), "--bundle", str(tmp_path / "no_such_bundle"), "--out", str(out), "--dumped", "--compare",
                         "--tolerance", TOLERANCE], capture_output=True, text=True, errors="replace", timeout=600, creationflags=0x08000000)
    lines = [ln for ln in (cp.stdout + cp.stderr).splitlines() if ln.strip()]
    assert cp.returncode == 21, lines
    assert any(ln.startswith("scenebundle: REFUSED NoManifest") for ln in lines), lines
    assert lines[-1].startswith("sceneshot: FAILED 21 NoManifest: no bundle at"), lines
    assert not out.exists(), "a refused bundle created --out"
