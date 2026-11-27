"""Build (and optionally test) the models/ library and tools (epic 3100) in one command.

Usage:  python models/build.py [--config Release|Debug] [--asan] [--test [--allow-skip]] [--clean] [--out DIR] [--target T]

Toolchain as the server side (decision D15, same as ipc/build.py x64): clang-cl -m64 + lld-link + Ninja against
the VS 2022 vcvarsall x64 environment (MSVC 14.4x STL, Windows 10 SDK), C++14, /MD. LLVM from
C:/Program Files/LLVM (or VP_LLVM_BIN).

Build tree (issue 9025, bug 0093 V7: it used to default to epic 3100's folder D:/c5-64bit-work/3100/build, so every
later caller wrote into that epic's scratch): --out DIR, else $C5M_BUILD_DIR/<config>, else build/models/<config>
inside this checkout (git-ignored; about 8 MB). It never defaults to another team's folder.

Tests that need game files read them from the environment variable C5M_SAMPLES (see models/README.md). When it is
not set, the first existing extraction of the convention is used (read only): F:/c5-64bit-work/3100/extract (where
models/tools/pyref/extract.py writes since ticket 0181, D133), C:/c5-64bit-work/3100/extract, then the 2026-11-10
extraction kept on D: as storage, D:/c5-64bit-work/3100/extract. A test that had to skip a part exits 77 and ctest lists it as skipped; --test then fails (exit 2), because a
run with skipped parts is not a full run (COMMON rule 9), unless --allow-skip is given.
"""
import argparse
import os
import re
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
# vcvarsall starts a background PowerShell for its telemetry unless this is set; PowerShell can hang at start on the
# workstation (bug 0108; 4000 group C, MC 2026-11-13)
os.environ.setdefault("VSCMD_SKIP_SENDTELEMETRY", "1")
LLVM = pathlib.Path(os.environ.get("VP_LLVM_BIN", r"C:/Program Files/LLVM/bin"))
VS2022 = pathlib.Path(r"C:/Program Files/Microsoft Visual Studio/2022/Community")
CMAKE_DIRS = [pathlib.Path(r"C:/Program Files/CMake/bin"),
              VS2022 / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"]
NINJA = VS2022 / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
VCVARSALL = VS2022 / "VC/Auxiliary/Build/vcvarsall.bat"
# Read only; models/README.md. Ticket 0181 (D133: D: is storage only): new extractions go to F: (extract.py's
# default); the one on D: is still read, as storage, when no work copy exists.
SAMPLES_CANDIDATES = [pathlib.Path("F:/c5-64bit-work/3100/extract"), pathlib.Path("C:/c5-64bit-work/3100/extract"),
                      pathlib.Path("D:/c5-64bit-work/3100/extract")]


def samples_convention(cands=None):
    """The first existing extraction of the convention, or None."""
    for c in (SAMPLES_CANDIDATES if cands is None else cands):
        if pathlib.Path(c).is_dir():
            return pathlib.Path(c)
    return None


def capture_env():
    out = subprocess.run('call "%s" x64 >nul && set' % str(VCVARSALL).replace("/", "\\"), shell=True,
                         capture_output=True, text=True, env=os.environ.copy())
    if out.returncode:
        sys.exit("vcvarsall x64 failed:\n%s\n%s" % (out.stdout, out.stderr))
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
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd, cwd=ROOT, env=env)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="Release", choices=["Release", "Debug", "RelWithDebInfo"])
    ap.add_argument("--test", action="store_true", help="run ctest after building")
    ap.add_argument("--clean", action="store_true", help="delete the build tree first")
    ap.add_argument("--out", default=None,
                    help="build tree (default: $C5M_BUILD_DIR/<config>, else build/models/<config> in this checkout)")
    ap.add_argument("--asan", action="store_true",
                    help="MSVC AddressSanitizer build with cl.exe (tree <default>-asan); clang-cl's ASan runtime does not "
                         "start on this Windows build")
    ap.add_argument("--allow-skip", action="store_true",
                    help="with --test: pass although tests skipped parts (no game files on this machine)")
    ap.add_argument("--target", default=None)
    ap.add_argument("--quietlock-override", metavar='"WHO: WHY"',
                    help="start although another team holds the quiet-machine lock (emergencies only, logged)")
    a = ap.parse_args()
    # COMMON rule 16 launch gate: refuse (exit 3) while another team holds the quiet-machine lock.
    sys.path.insert(0, str(ROOT / "perf" / "quietlock"))
    try:
        import gate as _launch_gate
    except ImportError:
        _launch_gate = None
        print("launch gate: warning: perf/quietlock/gate.py not found; launch goes on", file=sys.stderr)
    if _launch_gate:
        _launch_gate.require("a models/ build (models/build.py)", a.quietlock_override)
    if a.out:
        bdir = pathlib.Path(a.out)
    elif os.environ.get("C5M_BUILD_DIR"):
        bdir = pathlib.Path(os.environ["C5M_BUILD_DIR"]) / a.config
    else:
        bdir = ROOT / "build" / "models" / a.config
    if a.asan and not a.out:
        bdir = bdir.with_name(bdir.name + "-asan")
    print("build tree:", bdir, flush=True)
    conv = None if os.environ.get("C5M_SAMPLES") else samples_convention()
    if conv is not None:
        os.environ["C5M_SAMPLES"] = str(conv)
        print("C5M_SAMPLES not set; using the extraction of the convention, read only:", conv, flush=True)
    env = capture_env()
    if a.clean and bdir.exists():
        shutil.rmtree(bdir)
    cfg = ["cmake", "-G", "Ninja", "-S", str(HERE), "-B", str(bdir), "-DCMAKE_BUILD_TYPE=" + a.config,
           "-DCMAKE_MAKE_PROGRAM=" + NINJA.as_posix()]
    if a.asan:
        cfg += ["-DCMAKE_CXX_COMPILER=cl", "-DC5M_ASAN=ON"]
    else:
        cfg += ["-DCMAKE_TOOLCHAIN_FILE=" + (HERE / "cmake" / "clang-cl-x64.cmake").as_posix()]
    if run(cfg, env):
        return 1
    cmd = ["cmake", "--build", str(bdir)] + (["--target", a.target] if a.target else [])
    if run(cmd, env):
        return 1
    if a.test:
        cmd = ["ctest", "--test-dir", str(bdir), "--output-on-failure"]
        print("+", " ".join(cmd), flush=True)
        r = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, errors="replace")
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        if r.returncode:
            return r.returncode
        skipped = re.findall(r"Test +#\d+: (\S+) \.*\s*\*+Skipped", r.stdout)
        if skipped:
            print("tests that skipped parts: %s (game files missing: set C5M_SAMPLES, models/README.md)%s"
                  % (", ".join(skipped), "; allowed by --allow-skip" if a.allow_skip else "; failing the run"),
                  flush=True)
            if not a.allow_skip:
                return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
