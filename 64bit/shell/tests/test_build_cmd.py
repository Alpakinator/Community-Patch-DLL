"""shell/build.cmd refuses to build without C5_SHELL_BUILD (MC order 2026-11-11 14:08, epic 3200).

A build without it once overwrote the shared folder D:/c5-64bit-work/3000/shell-build with another worker's unmerged
binaries. The refusal happens before anything runs (no compiler, no render/ build), so this test is hermetic and fast.
"""
import os
import subprocess
from pathlib import Path

SHELL = Path(__file__).resolve().parent.parent


def _run(env, *args):
    return subprocess.run(["cmd", "/c", str(SHELL / "build.cmd"), *args], env=env, capture_output=True, text=True,
                          timeout=60)


def test_build_refuses_without_c5_shell_build():
    env = {k: v for k, v in os.environ.items() if k.upper() != "C5_SHELL_BUILD"}
    for variant in ([], ["asan"]):
        r = _run(env, *variant)
        assert r.returncode == 2, (variant, r.returncode, r.stdout, r.stderr)
        assert "C5_SHELL_BUILD is not set" in r.stdout, r.stdout
        assert "refused, nothing built" in r.stdout
        assert "BUILD OK" not in r.stdout and "[0/5]" not in r.stdout


def test_build_refuses_empty_c5_shell_build():
    env = dict(os.environ)
    env["C5_SHELL_BUILD"] = ""
    r = _run(env)
    assert r.returncode == 2 and "C5_SHELL_BUILD is not set" in r.stdout, (r.returncode, r.stdout)


def test_unknown_variant_still_refused_first():
    env = dict(os.environ)
    env["C5_SHELL_BUILD"] = str(SHELL / "build-never-used")
    r = _run(env, "bogus")
    assert r.returncode == 2 and "unknown variant" in r.stdout, (r.returncode, r.stdout)
    assert not (SHELL / "build-never-used").exists()
