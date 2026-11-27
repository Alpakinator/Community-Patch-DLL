"""Ticket 0527 (D186, F1 + F2): the farm builder guard of shell/composite/farm_guard.cpp.

- farm_guard_test.exe (shell/tests/farm_guard_test.cpp, built by shell\\build.cmd): the node search and its negatives (a
  wrong channel, handler, count, index), the EXE's lock (never initialised, held, released after a fault), the swap and
  the pass-through of object and argument, a re-registration that the EXE's unregister cannot see (the stale node: the
  city-field flag, the builder's own node first, then retired), the kill switch, the free-list check with each of its
  rules broken (cycle, live entry, index past the array, a freed entry off the chain, live + freed != size, a count above
  4, an implausible header, a faulting read), the trip (LOUD, the record in the newest run folder of this process, no
  further builder call, farm_guard=0 ignored after it), the record without a run folder (beside the log, LOUD), the burst
  line of every load, and the 13 byte patterns on a fake image (each one broken alone is refused); ticket 4854: the graph
  line's counts on hand-built graphs (a sound grid, damage that leaves the lists sound, seven broken-list cases, an
  oversize grid with cell 65,535 sound, ringed and shared, cells above 65,535, ids past 65,535, an empty, implausible and
  faulting graph) and the line after every burst and in-game line with farm_builder 1 and 0; ticket 4855: rule 2's
  geometry on three hand-computed maps (9074's 180 x 112, a map with cell 65,535 beside the x seam, one with it above the
  last row), radius 2 and radius 4 (a coastline piece's box meeting the cell, also across the seam), every plot of each map
  through the hook (exactly 19, 61 or 0 calls skipped, hand-listed plots each way), grids at or below 65,535 cells never
  skipped, (a) with its LOUD line and record, farm_builder=0 untouched, no geometry (skip all, LOUD once), and the radius
  chosen again at the next registration.
- ticket 4965 (epic 0590): the patterns, refusals (unknown build, symbol not carried) and addresses on fake images of both
  known builds (farm_guard_test.exe section 1); the patterns against the builds' EXEs are checked from the local tables
  (test_exe_tables_shell_b.py; the Steam install is not read any more);
- the composite on the stand-in (test_host --fake-exe): the real path refuses the fake EXE (its build or a pattern), LOUD,
  and the host runs on; the composite reads and logs farm_guard.
Skipped counts as failed (conftest.py).
"""
import re
import subprocess

import pytest

import shellrun

# 334 before ticket 0535; its section 14 (the owner, the composite beside it, the stub's unload and reload) adds 28; ticket
# 4965: section 1 runs once per known build, with the refusals (more checks; this stays a lower bound)
MIN_UNIT_CHECKS = 555   # ticket 0655: the post (section 15) and the area probe (16) with their patterns (555 on 2026-11-21)


def test_farm_guard_unit(built, tmp_path):
    exe = built / "farm_guard_test.exe"
    if not exe.exists():
        pytest.fail("farm_guard_test.exe missing in %s: rebuild with shell\\build.cmd" % built)
    p = subprocess.run([str(exe), str(tmp_path / "scratch")], capture_output=True, text=True, timeout=60,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode == 0, p.stdout + p.stderr
    m = re.search(r"farm_guard_test: OK (\d+) checks", p.stdout)
    assert m and int(m.group(1)) >= MIN_UNIT_CHECKS, p.stdout   # 221 with 4854's graph line (section 12), 334 with 4855's rule 2 (13)


def test_farm_guard_unit_fails_on_a_missing_argument(built):
    """The negative of the runner itself: without its scratch folder it exits 2 and prints no OK line."""
    p = subprocess.run([str(built / "farm_guard_test.exe")], capture_output=True, text=True, timeout=30,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode == 2 and "OK" not in p.stdout, p.stdout


def test_farm_guard_unit_runs_both_builds(built, tmp_path):
    """Ticket 4965 (epic 0590): section 1 of the unit test lays out a fake image of every known build (the build table's
    addresses, shell_exe.h) and checks the patterns, the unknown-build and not-carried refusals and the addresses there.
    (The patterns against the builds' EXEs themselves: test_exe_tables_shell_b.py, from the local tables.)"""
    p = subprocess.run([str(built / "farm_guard_test.exe"), str(tmp_path / "scratch")], capture_output=True, text=True,
                       timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode == 0, p.stdout + p.stderr
    for sb in ("4390913", "26577625"):
        assert re.search(r"farm_guard_test: patterns, refusals and addresses on Steam build %s .*: done" % sb, p.stdout), p.stdout


def test_farm_end_post_unit(built, tmp_path):
    """Ticket 0655 (D230): the farm-end post on the skipped-server path: its five patterns (with their call targets) on fake
    images of both builds, one post per event that passes the builder's three tests, none when a test fails, none on the
    other skip paths or with farm_end_post=0, refused without the post's sites, the counters in the burst and in-game lines."""
    p = subprocess.run([str(built / "farm_guard_test.exe"), str(tmp_path / "scratch")], capture_output=True, text=True,
                       timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode == 0, p.stdout + p.stderr
    for sb in ("4390913", "26577625"):
        assert re.search(r"farm_guard_test: the farm-end post's patterns on Steam build %s .*: done" % sb, p.stdout), p.stdout
    assert "farm_guard_test: the farm-end post (ticket 0655): done" in p.stdout, p.stdout
    assert "farm_guard_test: the area probe (ticket 0655 P1): done" in p.stdout, p.stdout


def test_edge_placement_unit(built, tmp_path):
    """Ticket 0657: the edge placement: its 13 patterns (with their call targets) on fake images of both builds; on a fake
    farm manager with a placer by the rule of cities-and-landmarks.md 2.6.2 the building of a plot with a server point is
    placed at that point and nothing of the transient curve is left; the centre kind, the holds (not DONE, another load's
    points, no valid grid, a missing plot with its short limit, the deferral), a failed re-post, the shut-down view, no
    section, the era listener, a stale node retired at the next registration, a held lock, farm_edge=0."""
    p = subprocess.run([str(built / "farm_guard_test.exe"), str(tmp_path / "scratch")], capture_output=True, text=True,
                       timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode == 0, p.stdout + p.stderr
    for sb in ("4390913", "26577625"):
        assert re.search(r"farm_guard_test: the edge placement's patterns on Steam build %s .*: done" % sb, p.stdout), p.stdout
    assert "farm_guard_test: the edge placement (ticket 0657): done" in p.stdout, p.stdout


def test_proxy_reads_the_edge_switch():
    """Ticket 0657: the proxy reads farm_edge (environment first, then [composite]) and hands it to the guard; default on."""
    repo = shellrun.SHELL.parent
    pf = (repo / "proxy" / "src" / "proxy_farm_guard.cpp").read_text(encoding="utf-8")
    assert 'ReadKey(L"farm_edge", L"VPSHELL_FARM_EDGE", 1, &fe)' in pf
    assert "e.farmEdgeOff = g_farmEdge == 0 ? 1 : 0;" in pf


def test_proxy_reads_the_post_switch():
    """Ticket 0655: the proxy (the guard's owner) reads farm_end_post as it reads farm_builder (environment first, then
    [composite] of shell_composite.ini) and hands it to the guard; the default is on."""
    repo = shellrun.SHELL.parent
    pf = (repo / "proxy" / "src" / "proxy_farm_guard.cpp").read_text(encoding="utf-8")
    assert 'ReadKey(L"farm_end_post", L"VPSHELL_FARM_END_POST", 1, &fp)' in pf
    assert "e.farmEndPostOff = g_farmEndPost == 0 ? 1 : 0;" in pf


def test_composite_wires_the_guard():
    src = (shellrun.SHELL / "composite" / "shell_composite.cpp").read_text(encoding="utf-8")
    assert '#include "farm_guard.h"' in src
    assert "FarmGuard_OnPresent(fge);" in src
    assert 'get(L"farm_guard", v, MAX_PATH)' in src and 'add("farm_guard", "%d", c.farmGuard);' in src
    ini = (shellrun.SHELL / "composite" / "shell_composite.ini").read_text(encoding="utf-8")
    assert re.search(r"^farm_guard=1$", ini, re.M)
    build = (shellrun.SHELL / "build.cmd").read_text(encoding="utf-8")
    assert '"%ROOT%composite\\farm_guard.cpp"' in build and "farm_guard_test.exe" in build
    assert "/DFARM_GUARD_PEER_DLL" in build and "farm_guard_peer.dll" in build   # ticket 0535: the composite beside the owner


def _body(src, head):
    """The text of the top-level function that starts with `head` (a line of its own) up to its closing brace at column
    0. None when the function is not there."""
    i = src.find("\n" + head + "\n{\n")
    if i < 0:
        return None
    j = src.find("\n}\n", i)
    return src[i:j + 3] if j >= 0 else None


def _guard_install_problems(sh, rt):
    """Ticket 0658: where and in which order the proxy installs the farm builder guard, in the layout of ticket 0635 (the
    install in a function of its own, InstallFarmGuard, called from OnBoot and from OnBootOff). `sh` is the text of
    proxy/src/proxy_shell.cpp, `rt` of proxy/src/proxy_rt.cpp. Returns the list of rules broken (empty when all hold):
    - the guard's install call is made in InstallFarmGuard only;
    - patch on (OnBoot): InstallFarmGuard is called once, unconditionally (one tab deep), before every LoadLibraryW and
      before the test switch's Loader::LoadFromMarker, and setComposite(loaded) follows the loads;
    - patch off (OnBootOff): InstallFarmGuard is called once, no composite is loaded there, setComposite(false) follows;
    - proxy_rt.cpp calls OnBootOff only inside `if (GuardsWhenOffOption(&how))` of BootOff (which sets g_offMode first),
      and OnBoot only under `if (!g_offMode)`: one of the two per boot, never both (never twice)."""
    bad = []
    if sh.count("g_farmGuardHooks->install(") != 1:
        bad.append("the guard's install call is not made exactly once in proxy_shell.cpp")
    inst = _body(sh, "static void InstallFarmGuard()")
    if inst is None or "g_farmGuardHooks->install(out, ini)" not in inst:
        bad.append("InstallFarmGuard() does not hold the guard's install call")
    if sh.count("InstallFarmGuard();") != 2:
        bad.append("InstallFarmGuard() is not called exactly twice (OnBoot, OnBootOff)")
    boot = _body(sh, "void OnBoot()")
    if boot is None:
        bad.append("no OnBoot()")
    else:
        if boot.count("\n\tInstallFarmGuard();\n") != 1 or boot.count("InstallFarmGuard();") != 1:
            bad.append("OnBoot does not call InstallFarmGuard() exactly once at its top level")
        else:
            i_install = boot.index("InstallFarmGuard();")
            loads = [m.start() for m in re.finditer(r"LoadLibrary\w*\(|Loader::LoadFromMarker\(", boot)]
            if "LoadLibraryW(g_cfg.compositePath)" not in boot or not loads:
                bad.append("OnBoot loads no composite (LoadLibraryW(g_cfg.compositePath))")
            elif any(i < i_install for i in loads):
                bad.append("OnBoot loads a module before InstallFarmGuard()")
            i_set = boot.find("g_farmGuardHooks->setComposite(loaded)")
            if i_set < 0 or any(i > i_set for i in loads):
                bad.append("OnBoot's setComposite(loaded) does not follow the composite's load")
    off = _body(sh, "void OnBootOff()")
    if off is None:
        bad.append("no OnBootOff()")
    else:
        if off.count("InstallFarmGuard();") != 1:
            bad.append("OnBootOff does not call InstallFarmGuard() exactly once")
        if re.search(r"LoadLibrary\w*\(|LoadFromMarker\(", off):
            bad.append("OnBootOff loads a module")
        if "InstallFarmGuard();" in off and off.find("g_farmGuardHooks->setComposite(false)") < off.find("InstallFarmGuard();"):
            bad.append("OnBootOff's setComposite(false) does not follow the install")
    if rt.count("ProxyShell::OnBootOff();") != 1:
        bad.append("proxy_rt.cpp does not call ProxyShell::OnBootOff() exactly once")
    else:
        bo = _body(rt, 'ICvGameContext1* BootOff(const char* fallbackWhy = NULL, const char* notice = NULL)')
        k = bo.find("if (GuardsWhenOffOption(&how))\n") if bo else -1
        if bo is not None and k < 0:
            # ticket 0680 reads the option once into a local (it also gives the title prefix): accept
            # `const int <name> = GuardsWhenOffOption(&how, ...);` followed by `if (<name>)`
            m = re.search(r"const int (\w+) = GuardsWhenOffOption\(&how\b", bo)
            if m:
                k = bo.find("if (%s)\n" % m.group(1), m.end())
        if bo is None or bo.find("g_offMode = true;") < 0 or k < 0:
            bad.append("BootOff does not set g_offMode or does not ask GuardsWhenOffOption")
        else:
            block = bo[k:]
            block = block[:block.find("\n\t\t}\n") + 1]   # the if's braced block, two tabs deep
            if "ProxyShell::OnBootOff();" not in block or bo.find("g_offMode = true;") > k:
                bad.append("OnBootOff is not called under [Proxy] GuardsWhenOff in BootOff")
    # ticket 0680 made the call a braced block (`if (!g_offMode)\n{\n ProxyShell::OnBoot(); ...`): either form
    on_boot_under_guard = ("if (!g_offMode) ProxyShell::OnBoot();" in rt
                           or re.search(r"if \(!g_offMode\)\s*\{\s*ProxyShell::OnBoot\(\);", rt) is not None)
    if rt.count("ProxyShell::OnBoot();") != 1 or not on_boot_under_guard:
        bad.append("proxy_rt.cpp does not call ProxyShell::OnBoot() exactly once, under if (!g_offMode)")
    return bad


def _proxy_sources():
    src = shellrun.SHELL.parent / "proxy" / "src"
    return ((src / "proxy_shell.cpp").read_text(encoding="utf-8"), (src / "proxy_rt.cpp").read_text(encoding="utf-8"))


def test_proxy_owns_the_guard():
    """Ticket 0535: the proxy compiles the same farm_guard.cpp, installs it before the composite's LoadLibrary and detaches
    it at the unload; the composite's copy finds the owner's page first in FarmGuard_OnPresent. Ticket 0658: in 0635's
    layout (InstallFarmGuard, called from OnBoot and, under [Proxy] GuardsWhenOff, from OnBootOff): _guard_install_problems."""
    repo = shellrun.SHELL.parent
    bp = (repo / "proxy" / "build_proxy.py").read_text(encoding="utf-8")
    assert "FARM_GUARD_SRCS = [REPO / 'shell' / 'composite' / 'farm_guard.cpp']" in bp and "FARM_GUARD_SRCS" in bp.split("srcs = ")[1][:400]
    sh, rt = _proxy_sources()
    assert _guard_install_problems(sh, rt) == []
    assert "g_farmGuardHooks->unload()" in _body(sh, "void OnUnload()")
    pf = (repo / "proxy" / "src" / "proxy_farm_guard.cpp").read_text(encoding="utf-8")
    assert "ProxyShell::g_farmGuardHooks = &kHooks;" in pf and 'FarmGuard_OwnerAttach("proxy"' in pf
    fg = (shellrun.SHELL / "composite" / "farm_guard.cpp").read_text(encoding="utf-8")
    present = fg[fg.index("void FarmGuard_OnPresent(const FarmGuardEnv& env)"):]
    assert present.index("Follow(env)") < present.index("ReadyCheck(env)")


def _mutate(text, old, new):
    assert text.count(old) == 1, old   # the mutation's anchor is in today's source exactly once
    return text.replace(old, new)


def test_proxy_guard_order_check_fails_on_a_broken_order():
    """Ticket 0658, the negative of _guard_install_problems: each text mutation of today's sources breaks one rule and is
    reported with that rule's message (the install after the composite's load or after the marker's load at OnBoot's top
    level, inside a branch, twice, missing with the patch off, a load with the patch off, the install call outside
    InstallFarmGuard, OnBootOff outside GuardsWhenOff, OnBoot in off mode)."""
    sh, rt = _proxy_sources()
    assert _guard_install_problems(sh, rt) == []
    install = "\tInstallFarmGuard();\n\n\t// ---- the composite\n"
    no_install = "\t// ---- the composite\n"
    after_loads = "\tg_cfg.compositeLoaded = loaded;\n"
    off_install = "\tInstallFarmGuard();\n\tif (g_farmGuardHooks) g_farmGuardHooks->setComposite(false);"
    order = "OnBoot loads a module before InstallFarmGuard()"
    cases = {
        "install after the composite's load": (
            _mutate(_mutate(sh, install, no_install), after_loads, after_loads + "\tInstallFarmGuard();\n"), rt, order),
        "install after the marker's load": (
            _mutate(_mutate(sh, install, no_install), "\t\tloaded = Loader::LoadFromMarker();\n\t}\n",
                    "\t\tloaded = Loader::LoadFromMarker();\n\t}\n\tInstallFarmGuard();\n"), rt, order),
        "install inside a branch": (_mutate(sh, install, "\tif (g_cfg.option == 1) InstallFarmGuard();\n\n" + no_install), rt,
                                    "OnBoot does not call InstallFarmGuard() exactly once at its top level"),
        "installed twice on boot": (_mutate(sh, install, "\tInstallFarmGuard();\n" + install), rt,
                                    "OnBoot does not call InstallFarmGuard() exactly once at its top level"),
        "no install with the patch off": (_mutate(sh, off_install, off_install[len("\tInstallFarmGuard();\n"):]), rt,
                                          "OnBootOff does not call InstallFarmGuard() exactly once"),
        "a composite loaded with the patch off": (
            _mutate(sh, off_install, "\tLoadLibraryW(g_cfg.compositePath);\n" + off_install), rt, "OnBootOff loads a module"),
        "the install call outside InstallFarmGuard": (
            _mutate(sh, install, "\tg_farmGuardHooks->install(NULL, NULL);\n\tInstallFarmGuard();\n\n" + no_install), rt,
            "the guard's install call is not made exactly once in proxy_shell.cpp"),
        "OnBootOff outside GuardsWhenOff": (
            sh, _mutate(_mutate(rt, "\t\t\tProxyShell::OnBootOff();  // D225", "\t\t\t// D225"),
                        "\t\tif (guardsWhenOff)\n", "\t\tProxyShell::OnBootOff();\n\t\tif (guardsWhenOff)\n"),
            "OnBootOff is not called under [Proxy] GuardsWhenOff in BootOff"),
        "OnBoot in off mode too": (sh, _mutate(rt, "\t\tif (!g_offMode)\n\t\t{\n\t\t\tProxyShell::OnBoot();",
                                               "\t\t{\n\t\t\tProxyShell::OnBoot();"),
                                   "proxy_rt.cpp does not call ProxyShell::OnBoot() exactly once, under if (!g_offMode)"),
    }
    for name, (s, r, want) in cases.items():
        assert s != sh or r != rt, name   # the mutation changed something
        assert want in _guard_install_problems(s, r), (name, _guard_install_problems(s, r))


COMPOSITE_HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")
HOOK_COMMIT_WAIT_S = 18   # bug 0120: the first Present after the hook commit can take seconds under load


def test_composite_real_path_refuses_the_fake_exe(run):
    """The fake EXE passes the composite's validation but holds none of the guard's patterns: REFUSED before any read
    or write, LOUD, and the composite and the host run on."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", farm_guard="1")
    run.start_host(*COMPOSITE_HOST, seconds=12)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    # ticket 4965: the fake EXE's header names build 4390913 only with its SizeOfImage (test_host writes the stamp; with
    # 4964 it may write both): refused for its build, or by the first pattern
    run.wait_log(r"farm_guard: LOUD: REFUSED: (not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage "
                 r"0x[0-9A-F]{8}\)|pattern 'GameView Startup: .*' at fg_startup_group does not match on Steam build \d+ .*)"
                 r" - the farm builder is not guarded in this process", timeout_s=10)
    assert run.host.wait(40) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert rows and not any(r["disabled"] for r in rows)
    text = "\n".join(run.log_lines())
    assert len(re.findall(r"farm_guard: LOUD: REFUSED", text)) == 1, text[-2000:]
    assert re.search(r"\bfarm_guard=1\b", text)
