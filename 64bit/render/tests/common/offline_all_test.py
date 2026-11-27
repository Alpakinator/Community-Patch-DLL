"""Auditor cycle 3 C4 (tracker 4000, 2026-11-13): render/tools/terrainshot/offline_all.py exits non-zero when a view
does not pass. Run by ctest as c5r_offline_all_test, or directly:

    python -I render/tests/common/offline_all_test.py

Offline: subprocess.run is replaced by a fake that plays terrainshot (through quietlock.py run) and compare_shots.py,
writing the compare JSON each view would get; nothing is started. Before the fix a run with a failed view exited 0
("exit 0 when every view ran (whatever the numbers)"), so a background notice read as a success; test_failed_view
fails on that version with "4 != 0".
"""
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.dont_write_bytecode = True  # no render/__pycache__ in the checkout

OFFLINE_ALL = pathlib.Path(__file__).resolve().parents[2] / "tools" / "terrainshot" / "offline_all.py"


def load():
    spec = importlib.util.spec_from_file_location("offline_all", str(OFFLINE_ALL))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Fake:
    """subprocess.run stand-in: terrainshot returns `shot_rc`; compare writes a JSON whose pass is `passes(name)`."""

    def __init__(self, shot_rc=0, passes=lambda name: True):
        self.shot_rc, self.passes, self.calls = shot_rc, passes, []

    def __call__(self, cmd, **kw):
        cmd = [str(c) for c in cmd]
        self.calls.append(cmd)
        if any(c.endswith("compare_shots.py") for c in cmd):
            out = pathlib.Path(cmd[cmd.index("--out") + 1])
            name = cmd[cmd.index("--name") + 1]
            out.write_text(json.dumps({"geometry": {"blocks": 10, "blocks_used": 10, "median_px": 0.5, "p95_px": 1.0},
                                       "selfcheck": {"median_error_px": 0.1}, "pass": self.passes(name),
                                       "colour": {}, "mask_px": 1}))
            return SimpleNamespace(returncode=0, stdout="", stderr="")
        return SimpleNamespace(returncode=self.shot_rc, stdout="terrainshot: ok\n", stderr="")


class OfflineAllExit(unittest.TestCase):
    def run_main(self, fake, extra=()):
        mod = load()
        mod.subprocess = SimpleNamespace(run=fake)
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            return mod.main(["--terrainshot", "ts.exe", "--gt", str(d / "gt"), "--work", str(d / "work"),
                             "--evidence", str(d / "ev"), "--exe", "civ.exe"] + list(extra))

    def test_all_pass(self):
        self.assertEqual(self.run_main(Fake()), 0)

    def test_failed_view(self):
        fake = Fake(passes=lambda name: name != "tiny_v3_second_zoomin")
        self.assertEqual(self.run_main(fake), 4)
        self.assertEqual(sum(1 for c in fake.calls if c[-1:] != [] and "compare_shots.py" in " ".join(c)), 6)

    def test_failed_view_allowed(self):
        self.assertEqual(self.run_main(Fake(passes=lambda name: False), ["--allow-fail"]), 0)

    def test_terrainshot_failure_and_lock(self):
        self.assertEqual(self.run_main(Fake(shot_rc=1)), 1)
        self.assertEqual(self.run_main(Fake(shot_rc=3)), 3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
