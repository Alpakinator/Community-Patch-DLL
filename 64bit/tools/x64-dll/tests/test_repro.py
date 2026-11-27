"""Issue 0115: builds repeat. Offline checks of the inputs that made two builds of one source and tool commit differ:
the build time in the build-id header, the absolute paths the compiler writes, the link time and the PDB path.
No compiler, no linker. The real double builds are recorded in research/06-tooling-and-workflows/
implementation-effectiveness.md section 7.

    python -m pytest tools/x64-dll/tests/test_repro.py -q -p no:cacheprovider
"""
from __future__ import annotations

import importlib.util
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
X64 = HERE.parent
REPO = X64.parents[1]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


build_info = _load("build_info_0115", REPO / "proxy" / "gen" / "build_info.py")
x64build = _load("x64build_0115", X64 / "build.py")


def test_build_id_header_repeats(tmp_path, monkeypatch):
    """Two headers written more than a second apart are the same bytes (the old VP_BUILD_TIME was the wall clock:
    this test fails on it)."""
    monkeypatch.delenv("SOURCE_DATE_EPOCH", raising=False)
    p1, v1 = build_info.write_header(tmp_path / "a", None, "release")
    time.sleep(1.2)
    p2, v2 = build_info.write_header(tmp_path / "b", None, "release")
    assert p1.read_bytes() == p2.read_bytes(), (v1["VP_BUILD_TIME"], v2["VP_BUILD_TIME"])


def test_build_time_follows_source_date_epoch(monkeypatch):
    monkeypatch.setenv("SOURCE_DATE_EPOCH", "1762992000")
    assert build_info.build_time() == "2025-11-13T00:00:00+0000"
    monkeypatch.setenv("SOURCE_DATE_EPOCH", "")
    t = build_info.build_time()
    assert t == "unknown" or t[:2] == "20"     # the commit time of HEAD (ISO 8601), never the clock


def test_prefix_maps_longest_first_and_skip_none():
    a = x64build.repro_cl_args((Path(r"D:\w\out"), "VPOUT"), (None, "X"), (Path(r"D:\w\out\gen\deep"), "VPGEN"))
    assert a[:2] == [r"/clang:-ffile-prefix-map=D:\w\out\gen\deep=VPGEN", r"/clang:-ffile-prefix-map=D:\w\out=VPOUT"]
    assert "/clang:-gno-codeview-command-line" in a and "/clang:-fdebug-compilation-dir=VPBUILD" in a


def test_sources_by_relative_name_and_object_names():
    """clang names anonymous namespaces by the main file name as given; S_OBJNAME is the /Fo path unless set."""
    assert x64build.source_cwd(Path(r"D:\w\src\a\b.cpp"), None, Path(r"D:\w\src")) == (r"D:\w\src", r"a\b.cpp")
    assert x64build.source_cwd(Path(r"E:\x\c.cpp"), Path(r"D:\w")) == (None, r"E:\x\c.cpp")
    assert x64build.objname_args(Path(r"D:\w\out\obj\x.obj"), Path(r"D:\w\out")) == \
        ["-Xclang", r"-object-file-name=obj\x.obj"]
    assert "/PDBSOURCEPATH:" + x64build.PDB_SOURCE_PATH in x64build.REPRO_LINK


def test_source_date_epoch(monkeypatch):
    monkeypatch.setenv("SOURCE_DATE_EPOCH", "123")
    assert x64build.repro_env({"A": "1"}) == {"A": "1", "SOURCE_DATE_EPOCH": "123"}
    monkeypatch.delenv("SOURCE_DATE_EPOCH")
    assert x64build.source_date_epoch().isdigit()


def test_dll_compile_and_link_flags():
    args = x64build.cl_args(Path(r"D:\some\src"), "release", False, None)
    assert r"/clang:-ffile-prefix-map=D:\some\src=VPSRC" in args
    assert "/Brepro" in x64build.REPRO_LINK and "/PDBALTPATH:%_PDB%" in x64build.REPRO_LINK


def test_every_link_and_compile_of_the_wrappers_uses_them():
    """The server, civ5save, vpipc, zlib and proxy command lines carry the flags (source text: no build is run)."""
    srv = (REPO / "server" / "build_server.py").read_text(encoding="utf-8")
    assert srv.count("x64build.REPRO_LINK") == 2          # civ5save.exe and server.exe
    assert srv.count("x64build.repro_cl_args(") == 5      # zlib, civ5save, server, vpipc, c5render (0131: was 4)
    prx = (REPO / "proxy" / "build_proxy.py").read_text(encoding="utf-8")
    assert "x64build.REPRO_LINK" in prx and "x64build.repro_cl_args(" in prx
    assert prx.count("+ repro") == 3                      # vp64helper (3520), vpipc and the proxy sources (0360: was 2)
    dll = (X64 / "build.py").read_text(encoding="utf-8")
    assert "+ REPRO_LINK" in dll
    for sub in ("closedlib-stubs/build_stubs.py", "cvgamedatabase/build_db.py", "cvlocalization/build_loc.py"):
        t = (X64 / sub).read_text(encoding="utf-8")
        assert "x64build.REPRO_LINK" in t and "x64build.repro_env(" in t, sub
