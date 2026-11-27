"""Ticket 0159: the third build configuration (`fast`: release without whole-program optimisation) and the
whole-program link place of tools/x64-dll/build.py.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest tools/x64-dll/tests/test_config.py -q -p no:cacheprovider

Offline: no DLL build. One test compiles the marker object and links a resource-free test DLL from it with the real
clang-cl and lld-link (skipped when LLVM is not installed); the rest use crafted PE files and stand-ins.
"""
from __future__ import annotations

import importlib.util
import json
import shutil
import struct
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


b = _load("x64build_0159", X64 / "build.py")


def make_pe(path: Path, sections: dict) -> Path:
    """A minimal PE file: MZ header, PE signature, COFF header, no optional header, the given sections."""
    n = len(sections)
    table_off = 0x40 + 24
    data_off = table_off + 40 * n
    head = bytearray(b"MZ" + b"\0" * 0x3A + struct.pack("<I", 0x40))
    head += b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, n, 0, 0, 0, 0, 0x2022)
    table, blobs, off = bytearray(), bytearray(), data_off
    for name, data in sections.items():
        table += name.encode().ljust(8, b"\0") + struct.pack("<IIIIIIHHI", len(data), 0x1000, len(data), off, 0, 0,
                                                             0, 0, 0x40000040)
        blobs += data
        off += len(data)
    path.write_bytes(bytes(head + table + blobs))
    return path


SRC = Path("C:/src")


def test_fast_compiles_like_release_without_lto():
    rel, fast = b.cl_args(SRC, "release", False), b.cl_args(SRC, "fast", False)
    assert "-flto" in rel and "-flto" not in fast
    assert [x for x in rel if x != "-flto"] == fast  # same flags, same defines, same order
    for f in ("/Ox", "/Ob2", "/Zo", "/DNDEBUG", "/DSTRONG_ASSUMPTIONS", "/DVPRELEASE_ERRORMSG", "/DFINAL_RELEASE"):
        assert f in fast
    assert "/Od" not in fast and "/DVPDEBUG" not in fast
    assert b.PREDEFS["fast"] == b.PREDEFS["release"]


def test_release_and_debug_compile_flags_unchanged():
    """The release and debug command lines are what they were before ticket 0159 (their DLL bytes must not move)."""
    rel = b.cl_args(SRC, "release", False)
    assert rel[rel.index("/Ox"):rel.index("/Ox") + 4] == ["/Ox", "/Ob2", "/Zo", "-flto"]
    dbg = b.cl_args(SRC, "debug", False)
    assert "/Od" in dbg and "/Oy-" in dbg and "-flto" not in dbg


def _lines(config, marker=None, census=False):
    out = Path("D:/o")
    return b.link_lines(out, config, census, None, [out / "obj" / "a.obj"], True, out / "CvGameCore_Expansion2.dll",
                        out / "CvGameCoreDLL.def", marker)


def test_release_link_lines_unchanged():
    assert _lines("release") == [
        '/OUT:"CvGameCore_Expansion2.dll"', '/PDB:"CvGameCore_Expansion2.pdb"', '/MAP:"CvGameCore_Expansion2.map"',
        '/IMPLIB:"CvGameCore_Expansion2.lib"', '/MACHINE:X64', '/DLL', '/DEBUG', '/LTCG', '/DYNAMICBASE', '/NXCOMPAT',
        '/SUBSYSTEM:WINDOWS', '/MANIFEST:EMBED', '/ERRORLIMIT:0', '/DEF:"CvGameCoreDLL.def"'] + b.REPRO_LINK + [
        '/OPT:REF', '/OPT:ICF'] + b.LTO_DETERMINISM_LINK + ['"obj\\a.obj"'] + b.DEFAULT_LIBS  # issue 0373
    dbg = _lines("debug")
    assert "/LTCG" in dbg and "/OPT:REF" not in dbg


def test_fast_links_without_ltcg_with_the_marker():
    fast = _lines("fast", marker=Path("D:/o/gen/vpconfig.obj"))
    assert "/LTCG" not in fast
    assert "/OPT:REF" in fast and "/OPT:ICF" in fast
    assert f"/INCLUDE:{b.CONFIG_SYMBOL}" in fast and '"gen\\vpconfig.obj"' in fast
    assert [x for x in fast if x not in (f"/INCLUDE:{b.CONFIG_SYMBOL}", '"gen\\vpconfig.obj"')] == \
        [x for x in _lines("release") if x != "/LTCG" and x not in b.LTO_DETERMINISM_LINK]  # issue 0373: release only


def test_only_release_and_pgo_instr_are_whole_program_and_only_fast_and_pgo_instr_are_marked():
    # ticket 4801 added pgo-instr (whole-program, marked); ticket 0513 release-pgo (whole-program, not marked: it is
    # timed and shipped); release and debug stay as they were
    assert b.CONFIGS == ("debug", "release", "fast", "pgo-instr", "release-pgo")
    assert b.WHOLE_PROGRAM == ("release", "pgo-instr", "release-pgo")
    assert b.MARKED_CONFIGS == ("fast", "pgo-instr")
    assert "VP-BUILD-CONFIG=fast" in b.marker_source("fast")
    # fast's marker text is the one of ticket 0159 (its bytes do not move)
    assert 'const char vp_build_config[64] = "VP-BUILD-CONFIG=fast; not for timing (tools/x64-dll/build.py)";' in \
        b.marker_source("fast")
    assert "VP-BUILD-CONFIG=pgo-instr; instrumented: not for timing, pins or shipping" in b.marker_source("pgo-instr")


# ---- ticket 4801: PGO (pgo-instr configuration, release --profile) ----

def test_release_compile_line_has_no_pgo_flag_without_a_profile():
    rel = b.cl_args(SRC, "release", False)
    assert not any("profile" in x for x in rel)
    assert rel == b.cl_args(SRC, "release", False, None, None, profile_use=None)


def test_pgo_instr_compiles_like_release_plus_instrumentation():
    rel, ins = b.cl_args(SRC, "release", False), b.cl_args(SRC, "pgo-instr", False)
    assert ins == rel + ["/clang:-fprofile-generate"]  # no folder compiled in: LLVM_PROFILE_FILE names the output
    assert b.PREDEFS["pgo-instr"] == b.PREDEFS["release"]
    assert b.cl_args(SRC, "pgo-instr", True, None, "/DMOD_GLOBAL_MAX_MAJOR_CIVS=43")[-1] == "/clang:-fprofile-generate"


def test_release_pgo_compiles_like_release_plus_only_the_profile_flags():
    # ticket 0513: release-pgo is 4801's measured `release --profile` line under its own name
    p = Path("D:/o/gen/vp64.profdata")
    rel, use = b.cl_args(SRC, "release", False), b.cl_args(SRC, "release-pgo", False, None, None, profile_use=p)
    assert use == rel + [f"/clang:-fprofile-use={p}", "-mllvm", "-pgo-warn-missing-function"]
    assert b.PREDEFS["release-pgo"] == b.PREDEFS["release"]


def test_a_profile_is_refused_by_every_configuration_but_release_pgo_and_required_there():
    for cfg in ("debug", "release", "fast", "pgo-instr"):
        with pytest.raises(ValueError):
            b.cl_args(SRC, cfg, False, None, None, profile_use=Path("D:/p.profdata"))
    with pytest.raises(ValueError, match="needs a profile"):
        b.cl_args(SRC, "release-pgo", False)


def test_pgo_instr_link_lines_are_releases_plus_runtime_and_flush_object():
    flush = (Path("D:/o/gen/vppgo_flush.obj"), b.PGO_FLUSH_SYMBOL)
    marker = Path("D:/o/gen/vpconfig.obj")
    out = Path("D:/o")
    ins = b.link_lines(out, "pgo-instr", False, None, [out / "obj" / "a.obj"], True, out / "CvGameCore_Expansion2.dll",
                       out / "CvGameCoreDLL.def", marker, [flush])
    extra = [f'/LIBPATH:"{b.PGO_RT_DIR}"', f"/INCLUDE:{b.CONFIG_SYMBOL}", f"/INCLUDE:{b.PGO_FLUSH_SYMBOL}",
             '"gen\\vpconfig.obj"', '"gen\\vppgo_flush.obj"', b.PGO_RT_LIB]
    for x in extra:
        assert x in ins
    assert "/LTCG" in ins and all(x in ins for x in b.LTO_DETERMINISM_LINK)
    assert [x for x in ins if x not in extra] == _lines("release")


def test_release_pgo_link_lines_are_releases_plus_the_profile_marker():
    pm = (Path("D:/o/gen/vppgo.obj"), b.PROFILE_SYMBOL)
    out = Path("D:/o")
    use = b.link_lines(out, "release-pgo", False, None, [out / "obj" / "a.obj"], True, out / "CvGameCore_Expansion2.dll",
                       out / "CvGameCoreDLL.def", None, [pm])
    assert [x for x in use if x not in (f"/INCLUDE:{b.PROFILE_SYMBOL}", '"gen\\vppgo.obj"')] == _lines("release")
    assert b.PGO_RT_LIB not in use


def test_dll_profile_reads_the_profile_section(tmp_path):
    sha = "ab" * 32
    p = make_pe(tmp_path / "p.dll", {".text": b"\xcc", ".vppgo": b"VP-BUILD-PROFILE=" + sha.encode() + b" (x)\0"})
    assert b.dll_profile(p) == sha
    assert b.dll_config(p) is None and b.timing_refusal(p) is None  # the measured arm is not refused
    plain = make_pe(tmp_path / "n.dll", {".text": b"\xcc", ".rdata": b"VP-BUILD-PROFILE=" + sha.encode()})
    assert b.dll_profile(plain) is None
    assert b.dll_profile(tmp_path / "missing.dll") is None


def test_timing_refusal_of_pgo_instr(tmp_path):
    m = tmp_path / "m" / "bin"
    m.mkdir(parents=True)
    make_pe(m / "CvGameCore_Expansion2.dll", {".vpcfg": b"VP-BUILD-CONFIG=pgo-instr; instrumented\0"})
    assert '"pgo-instr"' in b.timing_refusal(m)
    rec = tmp_path / "rec"
    rec.mkdir()
    make_pe(rec / "CvGameCore_Expansion2.dll", {".text": b"\xcc"})
    (rec / "build-summary.json").write_text(json.dumps({"config": "pgo-instr"}))
    assert 'records config "pgo-instr"' in b.timing_refusal(rec)
    lay = tmp_path / "x64-04b1211b-pfdee656-pgo-instr" / "bin"
    lay.mkdir(parents=True)
    make_pe(lay / "CvGameCore_Expansion2.dll", {".text": b"\xcc"})
    assert '"-pgo-instr"' in b.timing_refusal(lay)


def test_pgo_diagnostics_counts():
    outs = {
        "a.cpp": "x.cpp:1:2: warning: x.cpp: function control flow change detected (hash mismatch) f Hash = 1\n"
                 "x.cpp:3:2: warning: No profile data available for function g\nother line\n",
        "b.cpp": "nothing\n",
        "c.cpp": "warning: function control flow change detected (hash mismatch) h Hash = 2\n",
    }
    d = b.pgo_diagnostics(outs)
    assert d["hash_mismatch"] == 2 and d["missing_function"] == 1 and d["tus_with_any"] == 2
    assert d["malformed"] == 0


def _run_build(*args):
    return subprocess.run([sys.executable, "-I", str(X64 / "build.py")] + list(args), capture_output=True, text=True)


def test_cli_refuses_wrong_pgo_uses_before_building(tmp_path):
    out = tmp_path / "o"
    raw = tmp_path / "x.profraw"
    raw.write_bytes(b"\x81rforpl\xff" + b"\0" * 64)  # a raw profile's magic, not an indexed one
    cases = [
        (["--config", "fast", "--profile", str(raw)], "--profile is for --config release-pgo only"),
        (["--config", "pgo-instr", "--profile", str(raw)], "--profile is for --config release-pgo only"),
        (["--config", "release", "--profile", str(raw)], "the configuration that consumes a profile is release-pgo"),
        (["--config", "release", "--profile-unlocked"], "--profile-unlocked is for --config release-pgo only"),
        (["--config", "release-pgo", "--profile", str(tmp_path / "none.profdata")], "no such file"),
        (["--config", "release-pgo", "--profile", str(raw)], "not an indexed profile"),
        (["--config", "release", "--compile-only", "--link-only"], "--compile-only and --link-only"),
    ]
    for args, msg in cases:
        cp = _run_build(*args, "--out", str(out))
        assert cp.returncode == 1, (args, cp.returncode, cp.stdout, cp.stderr)
        assert msg in cp.stderr, (args, cp.stderr)
    assert not out.exists()  # refused before anything was written


@pytest.mark.skipif(not b.CLANG_CL.exists() or not b.LLD_LINK.exists() or not (b.PGO_RT_DIR / b.PGO_RT_LIB).exists(),
                    reason="LLVM or its profile runtime not installed")
def test_flush_object_writes_the_profile_when_the_host_terminates_itself(tmp_path):
    """The instrumented DLL with compile_pgo_objects()'s flush object, loaded by a host that ends with TerminateProcess
    (as server.exe does), leaves a complete profile; without the flush object the file is empty."""
    (tmp_path / "lib.c").write_text("static __declspec(noinline) int h(int x) { return 3 * x + 1; }\n"
                                    "__declspec(dllexport) int work(int n) { int s = 0; for (int i = 0; i < n; ++i) "
                                    "s += (i & 1) ? h(i) : i; return s; }\n")
    (tmp_path / "host.c").write_text(
        "#include <windows.h>\ntypedef int (*W)(int);\nint main(void) { HMODULE m = LoadLibraryA(\"lib.dll\"); "
        "W w = m ? (W)(void *)GetProcAddress(m, \"work\") : 0; if (!w) return 9; w(1000); "
        "TerminateProcess(GetCurrentProcess(), 0); return 0; }\n")
    env = b.load_vcvars_env(_Log(), tmp_path / "vcvars-env.json")
    def cc(*a):
        cp = subprocess.run([str(b.CLANG_CL), "-m64", "/c", "/O2", "/MD"] + list(a), cwd=tmp_path, env=env,
                            capture_output=True, text=True)
        assert cp.returncode == 0, cp.stdout + cp.stderr
    def ln(*a):
        cp = subprocess.run([str(b.LLD_LINK)] + list(a), cwd=tmp_path, env=env, capture_output=True, text=True)
        assert cp.returncode == 0, cp.stdout + cp.stderr
    cc("host.c", "/Fohost.obj")
    ln("/OUT:host.exe", "host.obj")
    cc("/clang:-fprofile-generate", "lib.c", "/Folib.obj")
    objs = b.compile_pgo_objects(env, tmp_path, "pgo-instr")
    assert [s for _o, s in objs] == [b.PGO_FLUSH_SYMBOL]
    profdata = b.LLVM_BIN / "llvm-profdata.exe"
    results = {}
    for name, extra in (("with", [f"/INCLUDE:{b.PGO_FLUSH_SYMBOL}", str(objs[0][0])]), ("without", [])):
        ln("/DLL", "/OUT:lib.dll", "/OPT:REF", "lib.obj", f"/LIBPATH:{b.PGO_RT_DIR}", *extra)
        raw = tmp_path / f"{name}.profraw"
        cp = subprocess.run([str(tmp_path / "host.exe")], cwd=tmp_path, capture_output=True,
                            env=dict(env, LLVM_PROFILE_FILE=str(raw)))
        assert cp.returncode == 0
        results[name] = raw.stat().st_size if raw.exists() else -1
        if name == "with":
            show = subprocess.run([str(profdata), "show", "--all-functions", "--counts", str(raw)],
                                  capture_output=True, text=True)
            assert show.returncode == 0, show.stderr
            assert "lib.c;h" in show.stdout and "Block counts: [500]" in show.stdout, show.stdout
    assert results["with"] > 0 and results["without"] == 0, results


@pytest.mark.skipif(not b.CLANG_CL.exists() or not b.LLD_LINK.exists(), reason="LLVM not installed")
def test_profile_marker_survives_a_real_link(tmp_path):
    sha = "0123456789abcdef" * 4
    objs = b.compile_pgo_objects(None, tmp_path, "release", sha)
    assert [s for _o, s in objs] == [b.PROFILE_SYMBOL]
    assert b.compile_pgo_objects(None, tmp_path, "release", None) == []
    stub = tmp_path / "s.c"
    stub.write_text("int vp_test_fn(void) { return 1; }\n")
    subprocess.run([str(b.CLANG_CL), "-m64", "/c", "/Zl", "/GS-", str(stub), f"/Fo{tmp_path / 's.obj'}"], check=True,
                   capture_output=True)
    cp = subprocess.run([str(b.LLD_LINK), "/DLL", "/NOENTRY", "/NODEFAULTLIB", "/MACHINE:X64", "/OPT:REF", "/OPT:ICF",
                         "/EXPORT:vp_test_fn", f"/OUT:{tmp_path / 'p.dll'}", str(tmp_path / "s.obj"),
                         f"/INCLUDE:{b.PROFILE_SYMBOL}", str(objs[0][0])], capture_output=True, text=True)
    assert cp.returncode == 0, cp.stdout + cp.stderr
    assert b.dll_profile(tmp_path / "p.dll") == sha
    assert b.timing_refusal(tmp_path / "p.dll") is None


def test_dll_config_reads_the_marker_section(tmp_path):
    fast = make_pe(tmp_path / "f.dll", {".text": b"\xcc" * 16, ".vpcfg": b"VP-BUILD-CONFIG=fast; not for timing\0\0"})
    rel = make_pe(tmp_path / "r.dll", {".text": b"\xcc" * 16, ".rdata": b"VP-BUILD-CONFIG=fast"})  # not the section
    assert b.dll_config(fast) == "fast"
    assert b.dll_config(rel) is None
    (tmp_path / "junk.dll").write_bytes(b"not a PE file at all")
    assert b.dll_config(tmp_path / "junk.dll") is None
    assert b.dll_config(tmp_path / "missing.dll") is None


def test_timing_refusal_marker_record_and_layout_name(tmp_path):
    ok = tmp_path / "x64-04b1211b-pa803776" / "bin"
    ok.mkdir(parents=True)
    make_pe(ok / "CvGameCore_Expansion2.dll", {".text": b"\xcc"})
    (ok / "build.json").write_text(json.dumps({"config": "release"}))
    assert b.timing_refusal(ok) is None
    assert b.timing_refusal(ok / "CvGameCore_Expansion2.dll") is None

    marked = tmp_path / "renamed" / "bin"  # a copied DLL keeps its marker whatever the folder is called
    marked.mkdir(parents=True)
    make_pe(marked / "CvGameCore_Expansion2.dll", {".vpcfg": b"VP-BUILD-CONFIG=fast;"})
    assert "marker section" in b.timing_refusal(marked)

    rec = tmp_path / "rec"
    rec.mkdir()
    make_pe(rec / "CvGameCore_Expansion2.dll", {".text": b"\xcc"})
    (rec / "build-summary.json").write_text(json.dumps({"config": "fast"}))
    assert 'records config "fast"' in b.timing_refusal(rec)

    lay = tmp_path / "x64-04b1211b-pa803776-fast" / "bin"
    lay.mkdir(parents=True)
    make_pe(lay / "CvGameCore_Expansion2.dll", {".text": b"\xcc"})
    assert '"-fast"' in b.timing_refusal(lay)


class _StandIn:
    """perf/quietlock/linkplace.py stand-in: records calls; raises on demand."""
    class LinkPlaceTimeout(Exception):
        pass

    def __init__(self, timeout=False):
        self.calls, self.timeout = [], timeout

    def default_places(self):
        return 1

    def acquire(self, what, out=None, limit_min=None, say=None):
        self.calls.append((what, out, limit_min))
        if self.timeout:
            raise self.LinkPlaceTimeout("gave up after 60 min waiting for a whole-program link place: held by pid 7")
        return SimpleNamespace(taken=True, waited_s=0.0, memory_waited=False)


class _Log(list):
    def __call__(self, text="", echo=True):
        self.append(text)

    def close(self):
        pass


def test_link_place_taken_only_by_whole_program_links(tmp_path):
    lp = _StandIn()
    for cfg in ("debug", "fast"):
        assert b.take_link_place(cfg, tmp_path, _Log(), None, lp) is None
    assert lp.calls == []
    place = b.take_link_place("release", tmp_path, _Log(), 5, lp)
    assert place.taken and lp.calls == [("x64 DLL release link (tools/x64-dll/build.py)", tmp_path, 5)]
    place = b.take_link_place("pgo-instr", tmp_path, _Log(), 5, lp)  # ticket 4801: a whole-program link too
    assert place.taken and lp.calls[-1] == ("x64 DLL pgo-instr link (tools/x64-dll/build.py)", tmp_path, 5)


def test_link_place_timeout_exits_4_with_a_clear_message(tmp_path):
    log = _Log()
    with pytest.raises(SystemExit) as ei:
        b.take_link_place("release", tmp_path, log, None, _StandIn(timeout=True))
    assert ei.value.code == b.LINK_PLACE_EXIT == 4
    assert any("FAILED: gave up after 60 min" in x and "held by pid 7" in x and "objects kept" in x for x in log)


def test_the_real_linkplace_module_has_the_interface_build_uses():
    lp = _load("linkplace_0159", REPO / "perf" / "quietlock" / "linkplace.py")
    assert callable(lp.acquire) and callable(lp.default_places) and issubclass(lp.LinkPlaceTimeout, Exception)


@pytest.mark.skipif(not b.CLANG_CL.exists() or not b.LLD_LINK.exists(), reason="LLVM not installed")
def test_marker_object_survives_a_real_link(tmp_path):
    """The marker compiled by compile_marker, linked with /OPT:REF and /INCLUDE as build.py links it, is found by
    dll_config in the output DLL; the same link without the marker gives a DLL without it."""
    obj = b.compile_marker(None, tmp_path, "fast")
    assert obj.is_file() and b.compile_marker(None, tmp_path, "release") is None
    for name, extra in (("m.dll", [f"/INCLUDE:{b.CONFIG_SYMBOL}", str(obj)]), ("n.dll", [])):
        stub = tmp_path / ("stub_" + name + ".c")
        stub.write_text("int vp_test_fn(void) { return 1; }\n")
        so = tmp_path / ("stub_" + name + ".obj")
        subprocess.run([str(b.CLANG_CL), "-m64", "/c", "/Zl", "/GS-", str(stub), f"/Fo{so}"], check=True,
                       capture_output=True)
        cp = subprocess.run([str(b.LLD_LINK), "/DLL", "/NOENTRY", "/NODEFAULTLIB", "/MACHINE:X64", "/OPT:REF",
                             "/OPT:ICF", "/EXPORT:vp_test_fn", f"/OUT:{tmp_path / name}", str(so)] + extra,
                            capture_output=True, text=True)
        assert cp.returncode == 0, cp.stdout + cp.stderr
    assert b.dll_config(tmp_path / "m.dll") == "fast"
    assert b.dll_config(tmp_path / "n.dll") is None
    assert "marker section" in b.timing_refusal(tmp_path / "m.dll")
