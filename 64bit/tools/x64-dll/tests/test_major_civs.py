"""Issue 4252 (epic 0334; MC decisions G2, G3): the major-civilisation count of tools/x64-dll/build.py and build_all.py.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest tools/x64-dll/tests/test_major_civs.py -q -p no:cacheprovider

Offline, no compile: the value check, the header-line reader, the define on the compile line (and never in PREDEFS, which
the server and the proxy read), the version string, the smoke-log check, and build.py's refusals from the command line
(exit code and output) on a two-file git tree that passes resolve_src with --unlocked.
"""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


b = _load("x64build_4252", X64 / "build.py")
sys.modules.setdefault("build", b)   # build_all.py does `import build as x64build`
sys.path.insert(0, str(X64))
ba = _load("x64buildall_4252", X64 / "build_all.py")

HEADER = ("// CustomModsGlobals.h\n#pragma once\n// For the default value of 22, just comment the line out\n"
          "// #define MOD_GLOBAL_MAX_MAJOR_CIVS (34)\n// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)\n"
          "// #define MOD_GLOBAL_MAX_MAJOR_CIVS (62)\n")
SRC = Path("C:/src")


def tree(tmp_path: Path, header: str | None = HEADER) -> Path:
    src = tmp_path / "src"
    (src / "CvGameCoreDLLUtil" / "include").mkdir(parents=True)
    if header is not None:
        (src / b.MAJOR_CIVS_HEADER).write_text(header, encoding="utf-8")
    return src


# ---- the value -------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("n", [22, 43, "43"])
def test_validated_values_pass(n):
    assert b.check_major_civs(n) == int(n)


@pytest.mark.parametrize("n", [34, 62, 2])
def test_unvalidated_values_need_the_flag(n):
    with pytest.raises(b.MajorCivsError, match="not validated .*--unvalidated-major-civs"):
        b.check_major_civs(n)
    assert b.check_major_civs(n, unvalidated=True) == n


@pytest.mark.parametrize("n", [63, 64, 80, 1, 0, -22])
def test_out_of_range_refused_even_with_the_flag(n):
    with pytest.raises(b.MajorCivsError, match=r"outside 2\.\.62"):
        b.check_major_civs(n, unvalidated=True)


def test_not_a_number_refused():
    with pytest.raises(b.MajorCivsError, match="not a number"):
        b.check_major_civs("forty-three")


# ---- the header line -------------------------------------------------------------------------------------------------
def test_commented_header_has_no_value(tmp_path):
    assert b.header_major_civs(tree(tmp_path)) == (None, None)


def test_live_line_is_read_with_its_place(tmp_path):
    src = tree(tmp_path, HEADER.replace("// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)", "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)"))
    val, where = b.header_major_civs(src)
    assert val == 43
    assert where == "CvGameCoreDLLUtil/include/CustomModsGlobal.h:5: #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)"


def test_line_inside_a_block_comment_or_after_code_comment_is_not_live(tmp_path):
    src = tree(tmp_path, HEADER + "/*\n#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)\n*/\nint x; // #define MOD_GLOBAL_MAX_MAJOR_CIVS 62\n")
    assert b.header_major_civs(src) == (None, None)


def test_two_live_lines_refused(tmp_path):
    src = tree(tmp_path, HEADER + "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)\n#  define MOD_GLOBAL_MAX_MAJOR_CIVS 34\n")
    with pytest.raises(b.MajorCivsError, match="2 live MOD_GLOBAL_MAX_MAJOR_CIVS lines"):
        b.header_major_civs(src)


def test_value_that_is_not_a_number_refused(tmp_path):
    src = tree(tmp_path, HEADER + "#define MOD_GLOBAL_MAX_MAJOR_CIVS (MAX_X + 1)\n")
    with pytest.raises(b.MajorCivsError, match="not a plain number"):
        b.header_major_civs(src)


def test_missing_header_refused(tmp_path):
    with pytest.raises(b.MajorCivsError, match="cannot read the header"):
        b.header_major_civs(tree(tmp_path, header=None))


# ---- resolve ---------------------------------------------------------------------------------------------------------
def test_resolve_43_on_a_commented_header_gives_the_define(tmp_path):
    r = b.resolve_major_civs(tree(tmp_path), 43)
    assert (r["major_civs"], r["define"], r["tag"], r["header"]) == (43, "/DMOD_GLOBAL_MAX_MAJOR_CIVS=43", "m43", None)


def test_resolve_22_gives_no_define(tmp_path):
    r = b.resolve_major_civs(tree(tmp_path))
    assert (r["major_civs"], r["define"], r["tag"]) == (22, None, "")


def test_resolve_live_43_and_default_request_is_refused_never_a_silent_22(tmp_path):
    src = tree(tmp_path, HEADER.replace("// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)", "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)"))
    with pytest.raises(b.MajorCivsError, match="the source sets MOD_GLOBAL_MAX_MAJOR_CIVS to 43 .*--major-civs is 22"):
        b.resolve_major_civs(src, 22)


def test_resolve_live_43_and_request_43_adds_no_second_define(tmp_path):
    src = tree(tmp_path, HEADER.replace("// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)", "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)"))
    r = b.resolve_major_civs(src, 43)
    assert (r["define"], r["tag"]) == (None, "m43")
    assert r["header"].endswith("#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)")


# ---- the compile line, the version string ----------------------------------------------------------------------------
@pytest.mark.parametrize("config", ["release", "debug", "fast"])
def test_compile_line_22_is_unchanged_and_43_adds_one_define_after_the_predefs(config):
    old = b.cl_args(SRC, config, True)
    assert b.cl_args(SRC, config, True, None, None) == old
    new = b.cl_args(SRC, config, True, None, "/DMOD_GLOBAL_MAX_MAJOR_CIVS=43")
    i = new.index("/DMOD_GLOBAL_MAX_MAJOR_CIVS=43")
    assert new[:i] + new[i + 1:] == old
    assert new[i - 1] == f"/D{b.PREDEFS[config][-1]}" and new[i + 1].startswith("/I")
    assert not any("MAJOR" in a for a in old)


def test_the_define_is_never_in_predefs_which_the_server_and_proxy_read():
    for cfg, defs in b.PREDEFS.items():
        assert not any(b.MAJOR_CIVS_DEFINE in d for d in defs), cfg
    assert b.MAJOR_CIVS_DEFAULT == 22 and b.MAJOR_CIVS_VALIDATED == (22, 43)


def test_version_string_carries_the_count_only_when_not_22():
    info = {"describe": "Release-5.4.8-51-g04b1211b8", "mode": "copy", "dirty": False}
    assert b.version_string(info) == "Release-5.4.8-51-g04b1211b8 Copy"
    assert b.version_string(info, "") == "Release-5.4.8-51-g04b1211b8 Copy"
    assert b.version_string(info, "m43") == "Release-5.4.8-51-g04b1211b8+m43 Copy"
    assert b.version_string({**info, "mode": "git", "dirty": True}, "m43") == "Release-5.4.8-51-g04b1211b8+m43 Dirty"


def test_commit_overlay_writes_the_tagged_string(tmp_path):
    info = {"describe": "Release-5.4.8-51-g04b1211b8", "mode": "copy", "dirty": False, "src": tmp_path / "s"}
    b.write_commit_overlay(tmp_path / "o", info, "m43")
    text = (tmp_path / "o" / "gen" / "commit_id.inc").read_text(encoding="utf-8")
    assert '"Release-5.4.8-51-g04b1211b8+m43 Copy"' in text
    b.write_commit_overlay(tmp_path / "p", info)
    assert '"Release-5.4.8-51-g04b1211b8 Copy"' in (tmp_path / "p" / "gen" / "commit_id.inc").read_text(encoding="utf-8")


def test_layout_tag():
    assert b.major_civs_tag(22) == "" and b.major_civs_tag(43) == "m43"


# ---- build_all's smoke check -----------------------------------------------------------------------------------------
LOG43 = ("CvGameCoreDLL_Expansion2\\CvDllContext.cpp[248]: static CvDllGameContext::InitializeSingleton - Supporting 43 "
         "major civilizations\nCvGameCoreDLL_Expansion2\\CvDllContext.cpp[250]: static CvDllGameContext::"
         "InitializeSingleton - Gamecore was built from git version Release-5.4.8-51-g04b1211b8+m43 Copy\n")


def smoke(tmp_path, text):
    logs = tmp_path / "run" / "work" / "Logs"
    logs.mkdir(parents=True)
    if text is not None:
        (logs / "CustomMods.log").write_text(text, encoding="utf-8")
    return ba.smoke_major_civs(tmp_path / "run")


def test_smoke_43_passes(tmp_path):
    seen = smoke(tmp_path, LOG43)
    assert (seen["major_civs"], seen["version"]) == (43, "Release-5.4.8-51-g04b1211b8+m43 Copy")
    assert ba.smoke_major_problem(seen, 43) is None


def test_smoke_22_passes_on_the_pins_log(tmp_path):
    seen = smoke(tmp_path, LOG43.replace("Supporting 43", "Supporting 22").replace("+m43", ""))
    assert ba.smoke_major_problem(seen, 22) is None


def test_smoke_wrong_count_fails(tmp_path):
    seen = smoke(tmp_path, LOG43.replace("Supporting 43", "Supporting 22"))
    assert "says \"Supporting 22 major civilizations\", --major-civs is 43" in ba.smoke_major_problem(seen, 43)


def test_smoke_missing_line_or_log_fails(tmp_path):
    assert "no \"Supporting N major civilizations\" line" in ba.smoke_major_problem(smoke(tmp_path, None), 22)


def test_smoke_version_without_the_tag_fails(tmp_path):
    seen = smoke(tmp_path, LOG43.replace("+m43", ""))
    assert "does not carry \"+m43\"" in ba.smoke_major_problem(seen, 43)


def test_smoke_22_with_a_count_tag_fails(tmp_path):
    seen = smoke(tmp_path, LOG43.replace("Supporting 43", "Supporting 22"))
    assert "carries a major-count tag" in ba.smoke_major_problem(seen, 22)


# ---- build.py from the command line: refusals before any compile -----------------------------------------------------
def git_tree(tmp_path, header=HEADER):
    src = tree(tmp_path, header)
    (src / "build_vp_clang.py").write_text("CPP = [\n]\n", encoding="utf-8")
    (src / "CvGameCoreDLL_Expansion2").mkdir()
    for args in (["init", "-q"], ["config", "user.email", "t@example.invalid"], ["config", "user.name", "t"],
                 ["add", "-A"], ["commit", "-q", "-m", "t"]):
        subprocess.run(["git", "-C", str(src)] + args, check=True, capture_output=True)
    return src


def run_build(src, out, *extra):
    cp = subprocess.run([sys.executable, "-I", str(X64 / "build.py"), "--src", str(src), "--unlocked", "--out", str(out)]
                        + list(extra), capture_output=True, text=True)
    return cp.returncode, cp.stdout + cp.stderr


def test_cli_refuses_63(tmp_path):
    rc, text = run_build(git_tree(tmp_path), tmp_path / "out", "--major-civs", "63", "--unvalidated-major-civs")
    assert rc != 0
    assert "build.py: FAILED: --major-civs 63: outside 2..62" in text
    assert not (tmp_path / "out").exists()   # nothing was written


def test_cli_refuses_an_unvalidated_value(tmp_path):
    rc, text = run_build(git_tree(tmp_path), tmp_path / "out", "--major-civs", "34")
    assert rc != 0
    assert "build.py: FAILED: --major-civs 34: not validated" in text


def test_cli_refuses_a_live_line_of_another_count(tmp_path):
    src = git_tree(tmp_path, HEADER.replace("// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)",
                                            "#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)"))
    rc, text = run_build(src, tmp_path / "out")
    assert rc != 0
    assert "the source sets MOD_GLOBAL_MAX_MAJOR_CIVS to 43" in text and "--major-civs is 22" in text


def test_link_only_refuses_objects_of_another_count(tmp_path):
    out = tmp_path / "out"
    out.mkdir()
    (out / "build-summary.json").write_text(json.dumps({"config": "debug", "major_civs": 22}), encoding="utf-8")
    rc, text = run_build(git_tree(tmp_path), out, "--link-only", "--major-civs", "43")
    assert rc != 0 and "built with another --major-civs" in text
