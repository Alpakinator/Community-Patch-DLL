"""One-off 9023 (auditor cycle 1, pattern P10): the build wrappers fail when a link did not succeed, never copy an
older artifact after a failed link, and print the SHA-256 and timestamp of what they copied. Fake link logs, fake
artifacts and fake sub-builds in temporary folders; no compiler, no linker, no product build.

    python -m pytest tools/x64-dll/tests/test_linkcheck.py -q
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import shutil
import stat
import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent
REPO = X64.parents[1]
sys.path.insert(0, str(X64))
import linkcheck  # noqa: E402


def _load(name, path, extra_paths=()):
    for p in extra_paths:
        if str(p) not in sys.path:
            sys.path.insert(0, str(p))
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.fixture(scope="module")
def build_all():
    return _load("build_all_9023", X64 / "build_all.py")


@pytest.fixture(scope="module")
def build_server():
    return _load("build_server_9023", REPO / "server" / "build_server.py",
                 [X64 / "smoke-host", X64, REPO / "proxy" / "gen"])


def _file(p: Path, data: bytes = b"MZ fake", age_s: float = 0.0) -> Path:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    if age_s:
        t = time.time() - age_s
        os.utime(p, (t, t))
    return p


# --- link_failure: the raw link -------------------------------------------------------------------------------------

def test_link_success(tmp_path):
    started = time.time()
    dll = _file(tmp_path / "x.dll")
    assert linkcheck.link_failure(0, "", dll, started) is None
    assert linkcheck.link_failure(0, "lld-link: warning: undefined symbol: foo\n", dll, started) is None


def test_link_nonzero_exit(tmp_path):
    started = time.time()
    dll = _file(tmp_path / "x.dll")
    assert linkcheck.link_failure(1, "", dll, started) == "linker exit code 1"


@pytest.mark.parametrize("log", [
    "lld-link: error: undefined symbol: CvGame::update\n",
    "LINK : fatal error LNK1102: out of memory\n",
    "x.obj : error LNK2019: unresolved external symbol\n",
    "LLVM ERROR: out of memory\nAllocation failed\n",
    "std::bad_alloc\n",
])
def test_link_log_with_an_error_line_fails_even_with_exit_0(tmp_path, log):
    started = time.time()
    dll = _file(tmp_path / "x.dll")
    why = linkcheck.link_failure(0, "first line\n" + log, dll, started)
    assert why and "error line" in why
    assert linkcheck.link_failure(0, log, dll, started, check_log=False) is None   # census links force errors


def test_link_without_artifact_fails(tmp_path):
    why = linkcheck.link_failure(0, "", tmp_path / "x.dll", time.time())
    assert why and "was not written" in why


def test_stale_artifact_after_exit_0_fails(tmp_path):
    """I-0379: a link that wrote nothing leaves the previous build's DLL in place; it must not count."""
    dll = _file(tmp_path / "x.dll", age_s=3600)
    why = linkcheck.link_failure(0, "", dll, time.time())
    assert why and "older than the link" in why and "stale" in why
    with pytest.raises(linkcheck.LinkCheckError):
        linkcheck.require_link(0, "", dll, time.time())


def test_artifact_line_has_hash_size_and_time(tmp_path):
    dll = _file(tmp_path / "x.dll", b"abc")
    line = linkcheck.artifact_line(dll, "dll")
    assert hashlib.sha256(b"abc").hexdigest() in line and "size 3" in line
    assert time.strftime("%Y-%m-%d", time.localtime(dll.stat().st_mtime)) in line


# --- copy_checked: never an older artifact, always the hash ---------------------------------------------------------

def test_copy_prints_hash_and_time(tmp_path):
    src = _file(tmp_path / "out" / "server.exe", b"new exe")
    said = []
    recs = linkcheck.copy_checked([(src, True)], tmp_path / "bin", say=said.append)
    assert (tmp_path / "bin" / "server.exe").read_bytes() == b"new exe"
    assert recs[0]["sha256"] == hashlib.sha256(b"new exe").hexdigest()
    assert said and recs[0]["sha256"] in said[0] and "mtime" in said[0]


def test_missing_required_source_copies_nothing(tmp_path):
    bin_ = tmp_path / "bin"
    _file(bin_ / "server.exe", b"old exe")
    new = _file(tmp_path / "out" / "server.exe", b"new exe")
    with pytest.raises(linkcheck.LinkCheckError) as e:
        linkcheck.copy_checked([(new, True), (tmp_path / "out" / "CvGameCore_Expansion2.dll", True)], bin_,
                               say=lambda *_: None)
    assert "missing" in str(e.value) and "nothing copied" in str(e.value)
    assert (bin_ / "server.exe").read_bytes() == b"old exe"            # nothing was copied


def test_old_source_is_refused(tmp_path):
    old = _file(tmp_path / "out" / "server.exe", age_s=3600)
    with pytest.raises(linkcheck.LinkCheckError) as e:
        linkcheck.copy_checked([(old, True)], tmp_path / "bin", since=time.time(), say=lambda *_: None)
    assert "older than this build" in str(e.value)
    assert not (tmp_path / "bin" / "server.exe").exists()


def test_missing_optional_removes_the_earlier_copy(tmp_path):
    bin_ = tmp_path / "bin"
    _file(bin_ / "server.pdb", b"old pdb")
    exe = _file(tmp_path / "out" / "server.exe")
    said = []
    linkcheck.copy_checked([(exe, True), (tmp_path / "out" / "server.pdb", False)], bin_, say=said.append)
    assert not (bin_ / "server.pdb").exists()
    assert any("removed" in s for s in said)


# --- ticket 0629: a read-only source (the shared pins folder) gives a writable copy -----------------------------------

def _ro(p):
    os.chmod(p, stat.S_IREAD)
    return p


def _is_ro(p):
    return not os.stat(p).st_mode & stat.S_IWRITE


@pytest.fixture
def ro_cleanup(tmp_path):
    yield
    for p in tmp_path.rglob("*"):
        if p.is_file():
            os.chmod(p, stat.S_IREAD | stat.S_IWRITE)


def test_read_only_source_gives_a_writable_copy(tmp_path, ro_cleanup):
    src = _ro(_file(tmp_path / "keep" / "CvGameCore_Expansion2.dll", b"dll"))
    bin_ = tmp_path / "bin"
    linkcheck.copy_checked([(src, True)], bin_, say=lambda *_: None)
    assert not _is_ro(bin_ / src.name)                  # the copy can be replaced and removed
    assert _is_ro(src)                                  # the source keeps its attribute
    shutil.rmtree(bin_)                                 # what test_proxy_rt.py's layout() does


def test_negative_copy2_alone_keeps_the_attribute(tmp_path, ro_cleanup):
    """The negative: without copy_checked's step a copy of a read-only source is read-only (what 0629 found)."""
    src = _ro(_file(tmp_path / "keep" / "lua51_x64.dll", b"lua"))
    (tmp_path / "bin").mkdir()
    shutil.copy2(str(src), str(tmp_path / "bin" / src.name))
    assert _is_ro(tmp_path / "bin" / src.name)


def test_read_only_earlier_copy_is_replaced_and_removed(tmp_path, ro_cleanup):
    """A bin left read-only by a build before 0629: a rebuild overwrites the exe and removes the stale pdb."""
    bin_ = tmp_path / "bin"
    _ro(_file(bin_ / "server.exe", b"old exe"))
    _ro(_file(bin_ / "server.pdb", b"old pdb"))
    exe = _ro(_file(tmp_path / "keep" / "server.exe", b"new exe"))
    linkcheck.copy_checked([(exe, True), (tmp_path / "keep" / "server.pdb", False)], bin_, say=lambda *_: None)
    assert (bin_ / "server.exe").read_bytes() == b"new exe" and not _is_ro(bin_ / "server.exe")
    assert not (bin_ / "server.pdb").exists()


# --- run_sub_build: a sub-build's exit 0 is not enough ---------------------------------------------------------------

def _fake_build(tmp_path, out_text, rc):
    script = tmp_path / "fake_build.py"
    script.write_text("import sys\nsys.stdout.write(%r)\nsys.exit(%d)\n" % (out_text, rc), encoding="utf-8")
    return [sys.executable, "-I", str(script)]


def test_sub_build_without_success_line_fails(tmp_path, capsys):
    rc, text, why = linkcheck.run_sub_build(_fake_build(tmp_path, "linking...\ncompleted\n", 0), r"^build_all: OK\b",
                                            "build_all.py")
    assert rc == 0 and why and "printed no success line" in why


def test_sub_build_with_success_line_passes(tmp_path, capsys):
    rc, text, why = linkcheck.run_sub_build(_fake_build(tmp_path, "build_all: OK in 3 s: dll 2 s\n", 0),
                                            r"^build_all: OK\b", "build_all.py")
    assert why is None and "build_all: OK" in capsys.readouterr().out      # the output is still streamed


def test_sub_build_nonzero_exit_fails(tmp_path, capsys):
    rc, text, why = linkcheck.run_sub_build(_fake_build(tmp_path, "build_all: OK\n", 3), r"^build_all: OK\b", "x")
    assert rc == 3 and why == "x exited 3"


# --- build_all.py: the dll step and the library steps -----------------------------------------------------------------

def _dll_step(tmp_path, log_text, summary_age=0.0, dll_age=0.0, summary_dll=True):
    dll_dir, logs = tmp_path / "dll", tmp_path / "logs"
    dll = _file(dll_dir / "CvGameCore_Expansion2.dll", age_s=dll_age)
    s = {"dll": str(dll) if summary_dll else None}
    _file(dll_dir / "build-summary.json", json.dumps(s).encode(), age_s=summary_age)
    _file(logs / "dll.log", log_text.encode())
    return dll_dir, logs / "dll.log"


def test_build_all_dll_step_without_success_line_fails(build_all, tmp_path):
    started = time.time() - 1
    d, log = _dll_step(tmp_path, "Linking (normal)...\nlld-link rc=0 in 1.0 s\n")
    why = build_all.dll_step_problem(d, log, started)
    assert why and "no success line" in why


def test_build_all_dll_step_with_an_earlier_summary_fails(build_all, tmp_path):
    """build.py dies before writing its summary on a compile failure; an earlier run's summary must not count."""
    started = time.time() - 1
    d, log = _dll_step(tmp_path, "OK: x\n", summary_age=3600, dll_age=3600)
    why = build_all.dll_step_problem(d, log, started)
    assert why and "older than the step" in why
    assert build_all.fresh_dll_summary(d, started) is None


def test_build_all_dll_step_with_a_stale_dll_fails(build_all, tmp_path):
    started = time.time() - 1
    d, log = _dll_step(tmp_path, "OK: x\n", dll_age=3600)
    why = build_all.dll_step_problem(d, log, started)
    assert why and "older than the link" in why


def test_build_all_dll_step_ok(build_all, tmp_path):
    started = time.time() - 1
    d, log = _dll_step(tmp_path, "Timings (s): link 1.0\nOK: dll sha256 x size 1 mtime y\n")
    assert build_all.dll_step_problem(d, log, started) is None
    assert build_all.fresh_dll_summary(d, started)["dll"].endswith("CvGameCore_Expansion2.dll")


def test_build_all_library_step_outputs(build_all, tmp_path):
    started = time.time() - 1
    stubs = tmp_path / "stubs"
    _file(stubs / "CvGameDatabase_x64.dll", age_s=3600)
    _file(stubs / "CvGameDatabase_x64.lib", age_s=3600)
    why = build_all.step_outputs_problem("cvgamedatabase", stubs, started)
    assert why and "CvGameDatabase_x64.dll" in why and "older than the step" in why
    _file(stubs / "CvGameDatabase_x64.dll")                     # fresh DLL, unchanged import library: fine
    assert build_all.step_outputs_problem("cvgamedatabase", stubs, started) is None
    (stubs / "CvGameDatabase_x64.lib").unlink()
    assert "missing" in build_all.step_outputs_problem("cvgamedatabase", stubs, started)


# --- server/build_server.py: the bin copy and the sub-build success lines ------------------------------------------------

def _server_tree(tmp_path):
    out = tmp_path / "out"
    exe = _file(out / "server" / "server.exe", b"exe")
    _file(out / "server" / "server.pdb", b"pdb")
    tool = _file(out / "server" / "civ5save" / "civ5save.exe", b"tool")
    dlldir, stubs = _file(tmp_path / "deps" / "dll" / "CvGameCore_Expansion2.dll", b"dll").parent, tmp_path / "deps" / "stubs"
    for n in ("lua51_x64", "CvGameDatabase_x64", "CvLocalization_x64"):
        _file(stubs / (n + ".dll"), n.encode())
    return exe, tool, dlldir, stubs


def test_build_server_copy_bin(build_server, tmp_path):
    exe, tool, dlldir, stubs = _server_tree(tmp_path)
    bin_ = tmp_path / "bin"
    _file(bin_ / "civ5save.pdb", b"earlier build's pdb")
    said = []
    recs = build_server.copy_bin(bin_, exe, tool, dlldir, stubs, say=said.append)
    assert {Path(r["path"]).name for r in recs} == {"server.exe", "server.pdb", "civ5save.exe",
                                                     "CvGameCore_Expansion2.dll", "lua51_x64.dll",
                                                     "CvGameDatabase_x64.dll", "CvLocalization_x64.dll"}
    assert not (bin_ / "civ5save.pdb").exists()                 # no pdb this build: the earlier one is gone
    assert sum("sha256" in s for s in said) == 7


def test_build_server_copy_bin_refuses_a_missing_dll(build_server, tmp_path, capsys):
    exe, tool, dlldir, stubs = _server_tree(tmp_path)
    (dlldir / "CvGameCore_Expansion2.dll").unlink()
    bin_ = tmp_path / "bin"
    _file(bin_ / "CvGameCore_Expansion2.dll", b"an earlier DLL")
    _file(bin_ / "server.exe", b"an earlier exe")
    with pytest.raises(SystemExit) as e:
        build_server.copy_bin(bin_, exe, tool, dlldir, stubs, say=lambda *_: None)
    assert e.value.code == 1
    assert "FAILED" in capsys.readouterr().out
    assert (bin_ / "server.exe").read_bytes() == b"an earlier exe"     # nothing copied over it


def test_success_line_patterns_match_the_sub_builds_own_lines(build_server):
    """The patterns build_server requires are the lines build_all.py and build_proxy.py really print."""
    import re
    assert re.search(build_server.BUILD_ALL_OK, "build_all: OK in 512 s: stubs 20 s, dll 300 s")
    assert re.search(build_server.BUILD_PROXY_OK, "build_proxy: OK in 25 s: C:/x/CvGameCore_Expansion2.dll sha256 ab")
    assert "build_all: OK in" in (X64 / "build_all.py").read_text(encoding="utf-8")
    assert "build_proxy: OK in" in (REPO / "proxy" / "build_proxy.py").read_text(encoding="utf-8")
    src = (REPO / "server" / "build_server.py").read_text(encoding="utf-8")
    assert "subprocess.run(cmd).returncode" not in src           # no sub-build judged by its exit code alone


def test_build_scripts_use_the_link_check():
    """Each wrapper that links calls the shared check (a source-level guard against a later edit dropping it)."""
    for path, needle in ((X64 / "build.py", "linkcheck.link_failure("),
                         (REPO / "server" / "build_server.py", "linkcheck.link_failure("),
                         (REPO / "proxy" / "build_proxy.py", "linkcheck.link_failure("),
                         (REPO / "proxy" / "build_proxy.py", "linkcheck.artifact_info("),
                         (X64 / "build_all.py", "dll_step_problem(")):
        assert needle in path.read_text(encoding="utf-8"), (path, needle)
