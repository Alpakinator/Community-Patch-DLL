"""Auditor cycle 3 C4 (tracker 4000, 2026-11-13): shell/tools/terrain_live.py's dry run imports every module its live
path uses, so a broken import stops the dry run (exit 2) instead of the first live hold (I-0620: a launch script whose
dry run never imported `shellrun` lost a hold's arm). Offline: nothing is launched.

On the code before C4 the dry run never imported them: test_dry_run_fails_on_a_broken_live_import fails there with
"assert 0 == 2".
"""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import terrain_live as tl  # noqa: E402


@pytest.fixture
def args(tmp_path):
    g = tmp_path / "gt"
    g.mkdir()
    (g / "plan.json").write_text(json.dumps({"views": [{"name": "v1_city_default", "plot": [8, 23], "ticks": 0}]}))
    (g / "camera_v1_city_default_before.json").write_text("{}")
    (g / "shot_v1_city_default.png").write_bytes(b"x")
    m = tmp_path / "markers"
    m.mkdir()
    (m / "RELEASED").write_text("")
    srv = tmp_path / "shell_server.exe"
    srv.write_bytes(b"")
    exe = tmp_path / "Civ.exe"
    exe.write_bytes(b"")
    return ["--gt-dir", str(g), "--out", str(tmp_path / "out"), "--server-exe", str(srv), "--exe", str(exe),
            "--marker-dir", str(m), "--dry-run"]


def test_dry_run_imports_the_live_modules(args, capsys):
    assert tl.main(args) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["live_modules_imported"] == sorted(tl.LIVE_MODULES)
    assert plan["ini_for_live"].startswith("MISSING")


@pytest.mark.parametrize("broken", ["gt_reader", "shellrun", "i2_capture"])
def test_dry_run_fails_on_a_broken_live_import(args, capsys, monkeypatch, broken):
    monkeypatch.setitem(sys.modules, broken, None)   # "import <broken>" now raises ImportError
    assert tl.main(args) == 2
    err = capsys.readouterr().err
    assert "module %s does not import" % broken in err
