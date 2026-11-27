"""Issue 9039: the lockstep probe of shell_composite.dll (ini lockstep_probe=1; research/05-graphics-composition/
lockstep-probe.md). The probe hooks D3DXMatrixInverse of the d3dx9 DLL the EXE's import slot points into and keeps only
UpdateViews' own call (pOut = &g_InvView[0], return address ls_ret), after checking the EXE's bytes at ls_loop and
ls_thunk and its import slot ls_iat_matinv (build-table symbols since ticket 4964; build 4390913: 0x51E1C8, 0x51E1BC,
0x8A86B6, 0x9D6B74). The positive path is test_lockstep.py (--fake-uv) and the live game; here: the static premise
against both builds' golden EXEs, and every refusal on the stand-in (hidden desktop, rule 14) with the composite
unaffected.
"""
import struct
from pathlib import Path

import pytest

import exe_golden
import shellrun



def _sections(d):
    e = struct.unpack_from("<I", d, 0x3C)[0]
    nsec, opt = struct.unpack_from("<H", d, e + 6)[0], struct.unpack_from("<H", d, e + 20)[0]
    secs = []
    for i in range(nsec):
        so = e + 24 + opt + 40 * i
        vs, va, rs, ro = struct.unpack_from("<IIII", d, so + 8)
        secs.append((va, max(vs, rs), ro))
    return e, secs


def _at(d, secs, rva, n):
    for sva, size, ro in secs:
        if sva <= rva < sva + size:
            return d[ro + rva - sva: ro + rva - sva + n]
    raise AssertionError("RVA 0x%X in no section" % rva)


def _cstr(d, secs, rva):
    b = _at(d, secs, rva, 256)
    return b[:b.index(b"\0")].decode("ascii")


@pytest.mark.parametrize("build,dll", [("4390913", "d3dx9_42.dll"), ("26577625", "d3dx9_43.dll")])
def test_probe_site_matches_exe_on_disk(build, dll):
    """Ticket 4964, both builds' golden EXEs with each build's table values: the bytes the DLL checks before hooking
    (the two patterns of shell_composite.cpp), the call from ls_loop going to ls_thunk and returning to ls_ret, and the
    import slot ls_iat_matinv = <dll>!D3DXMatrixInverse, read from the import table, not assumed (the 2026-11-19 update
    moved the import from d3dx9_42.dll to d3dx9_43.dll; the DLL hooks whatever module the slot points into)."""
    bad = exe_golden.check_patterns("shell/composite/shell_composite.cpp")
    assert bad[build] == [], bad[build]
    v = exe_golden.values(build)
    d = exe_golden.golden(build).read_bytes()
    e, secs = _sections(d)
    loop, ret, thunk = v["ls_loop"], v["ls_ret"], v["ls_thunk"]
    rel = struct.unpack("<i", _at(d, secs, loop + 8 - 0x400000, 4))[0]
    assert loop + 12 == ret and loop + 12 + rel == thunk, "the call at ls_loop does not reach ls_thunk / return to ls_ret"
    # the import directory: which DLL and name the slot belongs to
    opt = e + 24
    imp_rva = struct.unpack_from("<I", d, opt + 104)[0]
    found = None
    k = 0
    while True:
        desc = _at(d, secs, imp_rva + 20 * k, 20)
        oft, _, _, name_rva, ft = struct.unpack("<IIIII", desc)
        if not name_rva and not ft:
            break
        name = _cstr(d, secs, name_rva).lower()
        i = 0
        while True:
            th = struct.unpack("<I", _at(d, secs, (oft or ft) + 4 * i, 4))[0]
            if not th:
                break
            if ft + 4 * i == v["ls_iat_matinv"] - 0x400000:
                found = (name, _cstr(d, secs, th + 2) if not th & 0x80000000 else "#%d" % (th & 0xFFFF))
            i += 1
        k += 1
    assert found == (dll, "D3DXMatrixInverse"), found


def _start(run, probe, fake=True, build="4390913"):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", lockstep_probe=str(probe))
    run.start_server("--scene", "grid", "--rate", "60", seconds=25)
    host = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after") + (("--fake-exe", "--fake-exe-build", build) if fake else ())
    run.start_host(*host, seconds=5)


def _finish_unaffected(run):
    assert run.host.wait(30) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())[1:]
    assert rows and sum(r["comp_n"] for r in rows) > 0 and not any(r["disabled"] for r in rows), rows
    lines = run.log_lines()
    assert not any("lsprobe: hook ENABLED" in ln or "hook created" in ln for ln in lines)
    assert not (run.dir / "shell_composite.lsprobe.csv").exists()
    return lines


@pytest.mark.parametrize("fake,build", [(True, "4390913"), (True, "26577625"), (False, None)],
                         ids=["fake-exe-4390913", "fake-exe-26577625", "real-host"])
def test_probe_refused_outside_the_exe(run, fake, build):
    """With the fake EXE image of either build (camera patterns valid, no UpdateViews loop bytes) the probe refuses at
    the loop pattern; with test_host's own image the camera validation already failed. Either way: one REFUSED line
    naming why, no hook, no rows, the composite draws on."""
    _start(run, 1, fake, build)
    line = run.wait_log(r"lsprobe: REFUSED", timeout_s=10)
    want = "pattern 'UpdateViews' inverse loop" if fake else "the camera validation did not pass"
    assert want in line and (not fake or " at ls_loop (VA 0x" in line), line
    lines = _finish_unaffected(run)
    assert sum("lsprobe: REFUSED" in ln for ln in lines) == 1
    assert not any(ln for ln in lines if "lsprobe 1s:" in ln and " on=1 " in ln)


def test_probe_off_by_default(run):
    """lockstep_probe=0: nothing validated, nothing hooked, no lsprobe line at all."""
    _start(run, 0)
    run.wait_log(r" stats ", timeout_s=10)
    lines = _finish_unaffected(run)
    assert not any("lsprobe" in ln for ln in lines)
