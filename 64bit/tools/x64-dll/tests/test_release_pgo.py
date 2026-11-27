"""Ticket 0513 (D184): the fifth configuration `release-pgo`, the locked profile of deps/dll.lock, the retrain rule, and
build_all.py's and server/build_server.py's handling of it.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest tools/x64-dll/tests/test_release_pgo.py -q -p no:cacheprovider

Offline: no DLL build. The CLI cases run build.py and build_all.py and check that a wrong use is refused (exit code,
message) before anything is written; the rest call the functions with crafted locks and profiles.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent
REPO = X64.parents[1]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


b = _load("x64build_0513", X64 / "build.py")
bs = _load("build_server_0513", REPO / "server" / "build_server.py")

SHA = "ab" * 32
PIN = "1b87dd1857e2b4e4ba87c76c7b481161b33e1368"


def lock_with(sha=SHA, file=None, pin=PIN, first=None):
    d = {"commit": PIN, "profile": first or f"sha256 {sha} bytes 123 pin {pin} name vp64-1b87dd185.profdata\n"
                                             "training: M 150->160, S 0->50, H 300->305"}
    if file is not None:
        d["profile-file"] = str(file)
    return d


def indexed_profile(path: Path, body: bytes = b"x") -> tuple:
    """A file with an indexed profile's magic (build.py reads only the magic and the SHA-256). Returns (path, sha)."""
    data = b.PROFDATA_MAGIC + body
    path.write_bytes(data)
    return path, hashlib.sha256(data).hexdigest()


def args(**kw):
    d = dict(config="release-pgo", profile=None, profile_unlocked=False, compile_only=False, link_only=False)
    d.update(kw)
    return SimpleNamespace(**d)


# ---- the lock line ----

def test_lock_profile_reads_the_structured_first_line_and_the_file():
    lp = b.lock_profile(lock_with(file="C:/keep/p.profdata"))
    assert (lp["sha256"], lp["bytes"], lp["pin"], lp["name"], lp["file"]) == \
        (SHA, 123, PIN, "vp64-1b87dd185.profdata", "C:/keep/p.profdata")
    assert "training" in lp["text"]


def test_lock_without_a_profile_line_has_none_and_a_garbled_line_is_an_error():
    assert b.lock_profile({"commit": PIN}) is None
    with pytest.raises(ValueError, match="not \"sha256"):
        b.lock_profile(lock_with(first="e06e4628 13502368 B"))


def test_the_repository_lock_parses():
    # deps/dll.lock either has no profile yet or a well-formed one (a hand edit must not break every release-pgo build)
    lp = b.lock_profile()
    if lp is not None:
        assert len(lp["sha256"]) == 64 and lp["bytes"] > 0 and lp["file"]


def test_train_writes_lock_lines_that_build_reads(tmp_path):
    t = _load("train_0513", X64 / "pgo" / "train.py")
    rec = {"pin": PIN, "profile_name": "vp64-1b87dd185.profdata", "profile_sha256": SHA, "profile_bytes": 13502368,
           "trained": "2026-11-24 16:20 -0500", "instrumented_dll": "sha256 " + "cd" * 32,
           "training": [{"set": "M", "input": "F:/x/TS0438-M-T150.Civ5Save", "from": 150, "turns": 160,
                         "raw_bytes": 1, "raw_sha256": "ef" * 32}],
           "tools": {"llvm-profdata": "LLVM version 19.1.7"}, "summary": "Total functions: 49286"}
    lock = tmp_path / "dll.lock"
    lock.write_text(f"commit = {PIN}\n" + t.dll_lock_lines(rec, "C:/c5-64bit-work/keep/pgo/vp64-1b87dd185.profdata"),
                    encoding="utf-8")
    lp = b.lock_profile(b.read_dll_lock(lock))
    assert (lp["sha256"], lp["bytes"], lp["pin"], lp["file"]) == \
        (SHA, 13502368, PIN, "C:/c5-64bit-work/keep/pgo/vp64-1b87dd185.profdata")
    assert "TS0438-M-T150.Civ5Save turns 150->160" in lp["text"] and "F:/x" not in lp["text"]


def test_profile_file_through_the_pgo_dir_setting(tmp_path, monkeypatch):
    # ticket 0774: the exported lock names no machine folder: <pgo_dir>/<name>, resolved through the setting
    monkeypatch.setenv("C5_PGO_DIR", str(tmp_path / "keep"))
    lp = b.lock_profile(lock_with(file="<pgo_dir>/vp64-x.profdata"))
    assert Path(lp["file"]) == tmp_path / "keep" / "vp64-x.profdata"
    assert lp["file_line"] == "<pgo_dir>/vp64-x.profdata" and lp["file_problem"] is None
    monkeypatch.setenv("C5_PGO_KEEP_0774", str(tmp_path / "k2"))
    lp = b.lock_profile(lock_with(file="%C5_PGO_KEEP_0774%/y.profdata"))
    assert Path(lp["file"]) == tmp_path / "k2" / "y.profdata"


def test_an_unset_pgo_dir_is_named_and_refused(tmp_path, monkeypatch):
    monkeypatch.delenv("C5_PGO_DIR", raising=False)
    monkeypatch.setenv("C5_SETTINGS_FILE", str(tmp_path / "none.ini"))
    lp = b.lock_profile(lock_with(file="<pgo_dir>/vp64-x.profdata"))
    assert lp["file"] is None and "pgo_dir" in lp["file_problem"] and "C5_PGO_DIR" in lp["file_problem"]
    with pytest.raises(SystemExit, match="C5_PGO_DIR"):
        b.check_pgo_args(args(), lock_with(file="<pgo_dir>/vp64-x.profdata"))
    lp = b.lock_profile(lock_with(file="<nosuch>/x"))
    assert lp["file"] is None and "not a path setting" in lp["file_problem"]


def test_the_repository_locks_carry_no_machine_path():
    # ticket 0774 (MC #15669): deps/dll.lock is exported; its path lines use placeholders or settings
    lock = (REPO / "deps" / "dll.lock").read_text(encoding="utf-8")
    m = re.search(r"^profile-file\s*=\s*(\S+)\s*$", lock, re.M)
    if m:
        assert m.group(1).startswith(("<", "%")), m.group(0)
    for name in ("dll.lock", "i0.lock"):
        text = (REPO / "deps" / name).read_text(encoding="utf-8")
        assert not re.search(r"[A-Za-z]:[/\\]Users[/\\]", text, re.I), name


def test_same_commit():
    assert b.same_commit(PIN, PIN[:9]) and b.same_commit(PIN[:7], PIN)
    assert not b.same_commit(PIN, "04b1211b8") and not b.same_commit("1b87dd", PIN)  # fewer than 7 digits never match


# ---- the retrain rule ----

def test_retrain_rule_under_the_threshold_is_current():
    v = b.retrain_verdict({"hash_mismatch": 8, "missing_function": 8, "malformed": 0}, 49286, PIN, PIN)
    assert v["needed"] is False and v["reasons"] == [] and v["unmatched"] == 16
    assert v["unmatched_share"] == round(16 / 49286, 6) and v["threshold"] == 0.01


def test_retrain_rule_over_one_percent_asks_for_a_retrain():
    v = b.retrain_verdict({"hash_mismatch": 400, "missing_function": 100}, 49286, PIN, PIN)
    assert v["needed"] is True and "500 of 49286 functions unmatched (1.01 %)" in v["reasons"][0]
    assert b.retrain_verdict({"hash_mismatch": 492, "missing_function": 0}, 49286)["needed"] is False  # 0.998 %


def test_retrain_rule_other_pin_malformed_or_unknown_count():
    assert "retrain before each release" in b.retrain_verdict({}, 100, "04b1211b8", PIN)["reasons"][0]
    assert "malformed" in b.retrain_verdict({"malformed": 2}, 100, PIN, PIN)["reasons"][0]
    v = b.retrain_verdict({}, None, PIN, PIN)
    assert v["needed"] is True and "unknown" in v["reasons"][0] and v["unmatched_share"] is None


# ---- check_pgo_args: the locked profile ----

def test_release_pgo_takes_the_locked_profile_from_the_lock_file_line(tmp_path):
    p, sha = indexed_profile(tmp_path / "p.profdata")
    r = b.check_pgo_args(args(), lock_with(sha=sha, file=p))
    assert (r["path"], r["sha256"], r["locked"]) == (p, sha, True)


def test_release_pgo_refuses_a_profile_whose_sha256_is_not_the_locks(tmp_path):
    p, sha = indexed_profile(tmp_path / "p.profdata")
    with pytest.raises(SystemExit) as e:
        b.check_pgo_args(args(profile=p), lock_with(file=p))
    assert f"has SHA-256 {sha}, not deps/dll.lock's profile {SHA}" in str(e.value)
    assert "--profile-unlocked" in str(e.value)
    r = b.check_pgo_args(args(profile=p, profile_unlocked=True), lock_with(file=p))   # on purpose: recorded
    assert r["locked"] is False and r["sha256"] == sha


def test_release_pgo_without_any_profile_or_with_a_missing_file_is_refused(tmp_path):
    with pytest.raises(SystemExit, match="needs a profile"):
        b.check_pgo_args(args(), {"commit": PIN})
    with pytest.raises(SystemExit, match=r"profile-file\): no such file"):
        b.check_pgo_args(args(), lock_with(file=tmp_path / "gone.profdata"))
    with pytest.raises(SystemExit, match="which has no profile line"):
        p, _ = indexed_profile(tmp_path / "p.profdata")
        b.check_pgo_args(args(profile=p), {"commit": PIN})


def test_other_configurations_take_no_profile(tmp_path):
    p, _ = indexed_profile(tmp_path / "p.profdata")
    assert b.check_pgo_args(args(config="release"), lock_with(file=p)) is None   # release ignores the lock's profile
    with pytest.raises(SystemExit, match="consumes a profile is release-pgo"):
        b.check_pgo_args(args(config="release", profile=p), lock_with(file=p))
    with pytest.raises(SystemExit, match="--profile-unlocked is for --config release-pgo only"):
        b.check_pgo_args(args(config="fast", profile_unlocked=True), {})


def _run(script, *a):
    return subprocess.run([sys.executable, "-I", str(X64 / script)] + [str(x) for x in a], capture_output=True,
                          text=True)


def test_cli_refuses_an_unlocked_profile_before_building(tmp_path):
    p, sha = indexed_profile(tmp_path / "other.profdata", b"another profile")
    out = tmp_path / "o"
    cp = _run("build.py", "--config", "release-pgo", "--profile", p, "--out", out)
    assert cp.returncode == 1 and f"has SHA-256 {sha}, not deps/dll.lock" in cp.stderr, cp.stderr
    assert not out.exists()


def test_build_all_refuses_wrong_profile_uses_before_any_step(tmp_path):
    p, _ = indexed_profile(tmp_path / "other.profdata", b"another profile")
    out = tmp_path / "o"
    cp = _run("build_all.py", "--config", "release", "--profile", p, "--out", out)
    assert cp.returncode == 1 and "--profile and --profile-unlocked are for --config release-pgo only" in cp.stdout
    cp = _run("build_all.py", "--config", "release-pgo", "--profile", p, "--out", out)
    assert cp.returncode == 1 and "not deps/dll.lock" in cp.stderr, (cp.stdout, cp.stderr)
    assert not out.exists()


def test_build_all_builds_the_libraries_as_release_and_passes_the_profile():
    text = (X64 / "build_all.py").read_text(encoding="utf-8")
    assert "lib_config = 'release' if a.config == x64build.RELEASE_PGO else a.config" in text
    for step in ("closedlib-stubs", "BUILD_DB", "BUILD_LOC", "build_host.py"):
        line = next(ln for ln in text.splitlines() if step in ln and "py +" in ln)
        assert "a.config" not in line, line
    assert "dll_cmd += ['--profile', str(pgo['path'])]" in text


# ---- server/build_server.py --deps ----

def test_build_server_lays_out_a_release_pgo_dll_under_release_only():
    assert bs.deps_config_problem("release-pgo", "release") is None
    assert bs.deps_config_problem("release", "release") is None and bs.deps_config_problem(None, "release") is None
    assert "laid out by --config release only" in bs.deps_config_problem("release-pgo", "debug")
    assert bs.deps_config_problem("debug", "release") == "config debug differs from --config release"
    assert bs.deps_config_problem("fast", "release") == "config fast differs from --config release"
    assert "'deps_config': deps_config" in (REPO / "server" / "build_server.py").read_text(encoding="utf-8")


def test_a_release_pgo_dll_is_not_refused_for_timing(tmp_path):
    lay = tmp_path / "x64-1b87dd18-p1234567-pgo" / "bin"
    lay.mkdir(parents=True)
    (lay / "CvGameCore_Expansion2.dll").write_bytes(b"MZ")
    (lay / "build.json").write_text(json.dumps({"config": "release", "deps_config": "release-pgo"}))
    assert b.timing_refusal(lay) is None
