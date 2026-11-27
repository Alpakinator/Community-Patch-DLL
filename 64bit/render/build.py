"""Build (and optionally test) render/, the server's asset pipeline library (epic 2900), in one command.

    python render/build.py [--config Release|Debug] [--test] [--clean] [--target T] [--build-dir DIR]
                           [--game-dir DIR] [--exe PATH] [--work DIR] [-R REGEX] [--asan [--mem-cap-mb MB]]

clang-cl -m64 + lld-link + Ninja against the MSVC 14.4x STL and the Windows 10 SDK (vcvarsall x64): the server's
toolchain (D15). C++14, /MD. render/ is its own CMake project (render/CMakeLists.txt) and depends on nothing else in
the repository.

Build tree: --build-dir, else C:/c5-64bit-work/render-build/<key>/<config> when C:/c5-64bit-work exists (COMMON rule
15; on D:/c5-64bit-work until ticket 0181, D133: D: is storage only), else <repo>/build/render-x64/<config>. <key> is the first 10 hex digits of the SHA-1 of this checkout's resolved,
lower-cased path, so every checkout and worktree on the machine has its own tree (bug 0091 item 1: before 2026-11-11
every checkout shared D:/c5-64bit-work/2900/build/<config>, whose CMake cache stopped any other checkout at configure).

Launch gate (COMMON rule 16): the build and the tests each refuse with exit code 3 while another team holds the
quiet-machine lock (perf/quietlock/gate.py); exit 3 means "wait and try again".

--test runs ctest. Tests that read game files get C5R_GAME_DIR (an install root, read-only; default: the setting
render_game_dir, see below), C5R_EXE (CivilizationV_DX11.exe, read-only; default: the setting render_exe) and
C5R_WORK (scratch for large outputs; default <build tree>/test-out, so keyed by checkout too). Without them those
tests report SKIP, and **any skipped test fails the run** (exit 1, "FAILED: N tests skipped"): a run with skipped tests
counts as failed (COMMON rule 9; bug 0091 item 2). Nothing is ever written into the game folder.

--asan (issue 3209): the AddressSanitizer test variant. MSVC cl + link + Ninja (cmake/msvc-asan-x64.cmake; LLVM 19's
clang-cl ASan runtime crashes at start-up on this machine), /fsanitize=address /Zi on every target, own build tree
(<default>/<config>-asan, or --build-dir). With --test: ctest runs under a job-object memory cap (--mem-cap-mb, default
6144; shell/tools/memcap.py when present) with ASAN_OPTIONS log_path=<build tree>/asan-logs/asan (quoted: the drive's
colon would split the options) and log_exe_name=1, so each report lands in asan.<exe>.<pid>; any non-empty report file
fails the run ("FAILED: N ASan report(s)") and its SUMMARY line is printed. ctest's lines are streamed as progress.
A sanitizer run is heavy: start it through perf/quietlock/quietlock.py run (COMMON rule 16).

The EXE build table (ticket 4960, epic 0590, D202): before configuring, tools/exebuild/emit_header.py writes
vp_exe_tables.gen.h into <build tree>/gen-exebuild from the local per-build tables (setting exe_tables, environment
variable C5_EXE_TABLES); without them the build fails ("FAILED: the EXE address header"), naming the setting.
"""
import argparse
import hashlib
import importlib.util
import os
import re
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
LLVM = pathlib.Path(os.environ.get("VP_LLVM_BIN", r"C:\Program Files\LLVM\bin"))
VS2022 = pathlib.Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community")
VCVARSALL = VS2022 / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
CMAKE_DIRS = [pathlib.Path(r"C:\Program Files\CMake\bin"),
              VS2022 / r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"]
NINJA = VS2022 / r"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
WORK_ROOT = pathlib.Path("C:/c5-64bit-work")   # ticket 0181 (D133); was D:/c5-64bit-work
# Default test inputs, both copies read only (the brief: read game files from an install copy): a game folder (an
# install copy with the same Resource/ and Assets/ as the Steam install) and a copy of the EXE (SHA-256 b2ce96ae...,
# identical to the Steam EXE, build id 4390913). Ticket 4711: the settings render_game_dir and render_exe of
# tools/release/settings.py (environment variables C5_RENDER_GAME_DIR and C5_RENDER_EXE, else
# deps/local-settings.ini); without them the tests that need these inputs skip, as before where the default was absent.


def _setting(key):
    """A path setting of tools/release/settings.py (loaded by path: render/ has no package), or None."""
    spec = importlib.util.spec_from_file_location("c5_release_settings", REPO / "tools" / "release" / "settings.py")
    if spec is None or not pathlib.Path(spec.origin).is_file():
        return None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    v = mod.get(key)
    return pathlib.Path(v) if v is not None else None


def default_game():
    return _setting("render_game_dir")


def default_exe():
    return _setting("render_exe")


def checkout_key(repo):
    """Key of a checkout: first 10 hex digits of SHA-1 over its resolved, lower-cased path (Windows paths compare
    without case)."""
    return hashlib.sha1(str(pathlib.Path(repo).resolve()).lower().encode("utf-8")).hexdigest()[:10]


def default_build_dir(repo, config, work_root=WORK_ROOT):
    """The build tree used without --build-dir (bug 0091 item 1)."""
    if pathlib.Path(work_root).exists():
        return pathlib.Path(work_root) / "render-build" / checkout_key(repo) / config
    return pathlib.Path(repo) / "build" / "render-x64" / config


def default_work_dir(bdir):
    """Test scratch (C5R_WORK) without --work: inside the build tree, so keyed by checkout like it."""
    return pathlib.Path(bdir) / "test-out"


def count_skipped(ctest_output):
    """Number of tests ctest reports as skipped or not run in its output (0 when none)."""
    return len(re.findall(r"\*\*\*Skipped|\*\*\*Not Run", ctest_output))


ASAN_LOG_PREFIX = "asan"


def asan_build_dir(bdir):
    """The --asan build tree: next to the normal one, never the same (other compiler, other CMake cache)."""
    bdir = pathlib.Path(bdir)
    return bdir.with_name(bdir.name + "-asan")


def toolchain_file(asan):
    return HERE / "cmake" / ("msvc-asan-x64.cmake" if asan else "clang-cl-x64.cmake")


def asan_options(log_dir):
    """ASAN_OPTIONS for the test run: reports into files under log_dir, one per process, named after the executable.
    The path is quoted because ASan splits its options at ':' and a Windows path has one after the drive letter."""
    log = (pathlib.Path(log_dir) / ASAN_LOG_PREFIX).as_posix()
    return f'log_path="{log}":log_exe_name=1:halt_on_error=1:quarantine_size_mb=64:malloc_context_size=30'


def asan_reports(log_dir):
    """[(file name, summary)] for every non-empty ASan log file in log_dir, sorted by name. summary is the report's
    'SUMMARY: AddressSanitizer' line, else its first 'ERROR:' line, else its first line. Empty files are not reports."""
    found = []
    d = pathlib.Path(log_dir)
    if not d.is_dir():
        return found
    for f in sorted(d.iterdir()):
        if not f.is_file() or not f.name.startswith(ASAN_LOG_PREFIX + "."):
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        if not text.strip():
            continue
        lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
        summary = next((ln for ln in lines if ln.startswith("SUMMARY: AddressSanitizer")), None)             or next((ln for ln in lines if "ERROR:" in ln), None) or lines[0]
        found.append((f.name, summary))
    return found


def canary_verdict(reports):
    """None when the canary's logs hold its heap-buffer-overflow report, else the failure message."""
    if any("heap-buffer-overflow" in summary for _, summary in reports):
        return None
    return ("ASan canary produced no heap-buffer-overflow report (instrumentation, runtime or log_path broken): "
            + (repr(reports) if reports else "no report file"))


def asan_canary(bdir, env):
    """Run tests/common's c5r_asan_canary (one byte past a heap block) with the run's options into a separate log
    folder; None when ASan reported it, else the failure message."""
    exe = pathlib.Path(bdir) / "tests" / "common" / "c5r_asan_canary.exe"
    if not exe.exists():
        return f"ASan canary missing: {exe}"
    logs = pathlib.Path(bdir) / "asan-canary-logs"
    if logs.exists():
        shutil.rmtree(logs)
    logs.mkdir(parents=True)
    cenv = dict(env, ASAN_OPTIONS=asan_options(logs))
    rc = subprocess.call([str(exe), "16"], cwd=str(bdir), env=cenv, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    verdict = canary_verdict(asan_reports(logs))
    if verdict is None:
        print(f"render/build.py: ASan canary reported its overflow (exit {rc}, {logs})")
    return verdict


def capture_env():
    # Bug 0108: vcvarsall's telemetry step starts a background PowerShell, which hangs on this workstation.
    venv = dict(os.environ, VSCMD_SKIP_SENDTELEMETRY="1")
    out = subprocess.run(f'call "{VCVARSALL}" x64 >nul && set', shell=True, capture_output=True, text=True, env=venv)
    if out.returncode:
        sys.exit(f"render/build.py: vcvarsall x64 failed:\n{out.stdout}\n{out.stderr}")
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition("=")
        if sep and k:
            env[k] = v
    extra = [str(LLVM)] + [str(d) for d in CMAKE_DIRS if d.exists()] + [str(NINJA.parent)]
    env["PATH"] = os.pathsep.join(extra + [env.get("PATH", env.get("Path", ""))])
    env.pop("Path", None)
    return env


def run(cmd, env):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.call([str(c) for c in cmd], cwd=REPO, env=env)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="Release", choices=["Release", "Debug", "RelWithDebInfo"])
    ap.add_argument("--test", action="store_true")
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--target")
    ap.add_argument("--build-dir")
    ap.add_argument("--game-dir")
    ap.add_argument("--exe")
    ap.add_argument("--work")
    ap.add_argument("-R", dest="regex", help="ctest -R filter")
    ap.add_argument("--asan", action="store_true", help="MSVC /fsanitize=address test variant, own build tree")
    ap.add_argument("--mem-cap-mb", type=int, default=6144,
                    help="--asan --test: memory cap on ctest and every test process (job object), MB")
    ap.add_argument("--quietlock-override", metavar='"WHO: WHY"',
                    help="start although another team holds the quiet-machine lock (emergencies only, logged; "
                         "COMMON rule 16)")
    a = ap.parse_args()
    # COMMON rule 16 launch gate (perf/quietlock/gate.py): exit 3 while another team holds the quiet-machine lock.
    # Checked at the build start and again before the tests (the tests read 2.4 GB of packs and use the GPU). The gate
    # is optional so that render/ stays buildable on its own: without perf/ the build warns and goes on.
    sys.path.insert(0, str(REPO / "perf" / "quietlock"))
    try:
        import gate as launch_gate
    except ImportError:
        launch_gate = None
        print("launch gate: warning: perf/quietlock/gate.py not found; launch goes on", file=sys.stderr)
    if launch_gate:
        launch_gate.require("a render/ build (render/build.py)", a.quietlock_override)

    if a.build_dir:
        bdir = pathlib.Path(a.build_dir)
    elif a.asan:
        bdir = asan_build_dir(default_build_dir(REPO, a.config))
    else:
        bdir = default_build_dir(REPO, a.config)
    if a.clean and bdir.exists():
        shutil.rmtree(bdir)
    bdir.mkdir(parents=True, exist_ok=True)

    # Ticket 4960 (epic 0590, D202): the EXE address header from the local per-build tables (setting exe_tables), into
    # <build tree>/gen-exebuild before configuring; CMakeLists.txt puts it and exebuild/include on c5render's path.
    exegen = bdir / "gen-exebuild"
    cp = subprocess.run([sys.executable, "-I", str(REPO / "tools" / "exebuild" / "emit_header.py"), "--out", str(exegen)],
                        capture_output=True, text=True, errors="replace")
    print((cp.stdout + cp.stderr).rstrip(), flush=True)
    if cp.returncode != 0:
        print("render/build.py: FAILED: the EXE address header (tools/exebuild/emit_header.py)")
        return 1

    env = capture_env()
    cfg = ["cmake", "-G", "Ninja", "-S", HERE, "-B", bdir, f"-DCMAKE_BUILD_TYPE={a.config}",
           f"-DCMAKE_MAKE_PROGRAM={NINJA.as_posix()}",
           f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file(a.asan).as_posix()}",
           f"-DC5R_EXEBUILD_GEN={exegen.as_posix()}"]
    if run(cfg, env):
        print("render/build.py: FAILED: configure")
        return 1
    cmd = ["cmake", "--build", bdir] + (["--target", a.target] if a.target else [])
    if run(cmd, env):
        print("render/build.py: FAILED: build")
        return 1
    print(f"render/build.py: built in {bdir}")
    if a.test:
        if launch_gate:
            launch_gate.require("the render/ tests (render/build.py --test)", a.quietlock_override)
        dg = None if a.game_dir else default_game()
        game = pathlib.Path(a.game_dir) if a.game_dir else (dg if dg is not None and dg.exists() else None)
        if game:
            env["C5R_GAME_DIR"] = str(game)
        exe = pathlib.Path(a.exe) if a.exe else default_exe()
        if exe is not None and exe.exists():
            env["C5R_EXE"] = str(exe)
        env["C5R_WORK"] = a.work or str(default_work_dir(bdir))
        pathlib.Path(env["C5R_WORK"]).mkdir(parents=True, exist_ok=True)
        print(f"render/build.py: C5R_GAME_DIR={env.get('C5R_GAME_DIR', '(unset)')} "
              f"C5R_EXE={env.get('C5R_EXE', '(unset)')} C5R_WORK={env['C5R_WORK']}")
        ct = ["ctest", "--test-dir", bdir, "--output-on-failure"] + (["-R", a.regex] if a.regex else [])
        log_dir = bdir / "asan-logs"
        if a.asan:
            # a fresh log folder per run: an old report must not fail (or hide behind) this one
            if log_dir.exists():
                shutil.rmtree(log_dir)
            log_dir.mkdir(parents=True)
            env["ASAN_OPTIONS"] = asan_options(log_dir)
            print(f"render/build.py: ASAN_OPTIONS={env['ASAN_OPTIONS']}")
            memcap = REPO / "shell" / "tools" / "memcap.py"
            if memcap.exists():
                ct = [sys.executable, "-I", memcap, "--mb", str(a.mem_cap_mb), "--"] + ct
            else:
                print("render/build.py: warning: shell/tools/memcap.py not found; tests run without a memory cap",
                      file=sys.stderr)
            # the canary proves the variant can report at all (else "0 reports" could mean a dead log path)
            canary = asan_canary(bdir, env)
            if canary:
                print(f"render/build.py: FAILED: {canary}")
                return 1
        print("+", " ".join(str(c) for c in ct), flush=True)
        # streamed line by line: ctest's "n/N Test #k" lines are the progress of a long (sanitizer) run
        proc = subprocess.Popen([str(c) for c in ct], cwd=REPO, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        chunks = []
        for line in proc.stdout:
            chunks.append(line)
            sys.stdout.write(line)
            sys.stdout.flush()
        rc = proc.wait()
        output = "".join(chunks)
        skipped = count_skipped(output)
        reports = asan_reports(log_dir) if a.asan else []
        for name, summary in reports:
            print(f"render/build.py: ASan report {log_dir / name}: {summary}")
        if reports:
            print(f"render/build.py: FAILED: {len(reports)} ASan report(s) in {log_dir}")
            return 1
        if rc:
            print("render/build.py: FAILED: tests")
            return 1
        if skipped:
            print(f"render/build.py: FAILED: {skipped} test executable(s) skipped (a run with skipped tests counts "
                  f"as failed; set C5R_GAME_DIR / C5R_EXE or --game-dir / --exe)")
            return 1
        print("render/build.py: tests passed, 0 skipped" + (", 0 ASan reports" if a.asan else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
