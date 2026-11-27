"""Build (and optionally test) the c5-64bit-patch tree for both sides of the boundary.

Usage:  python ipc/build.py [--arch x86|x64|both] [--config Release|Debug] [--test] [--clean] [--target T]

Two build trees at the repository root (ignored by git), build/<arch>/<config>, both clang-cl + lld-link + Ninja,
/MD (dynamic CRT; CMake option C5_STATIC_CRT=ON gives /MT):
  build/x86  the proxy side: clang-cl -m32 against the Visual Studio 2008 (VC9) headers and libraries
             (vsvars32.bat), msvcr90 - the toolchain of the 32-bit game DLL (build_vp_clang.py), because the proxy
             lives in the EXE's process and shares its msvcr90 heap. Language level C++11: clang-cl's default with
             the VC9 headers (__cplusplus 201103, _MSC_VER 1500); the top-level CMAKE_CXX_STANDARD 14 does not
             raise it there, and /std:c++14 would not link against msvcr90 without /Zc:sizedDealloc-
             (ipc/PROTOCOL.md section 11, research/03-dll-engine-boundary/ipc-design-and-measurements.md 7.3).
  build/x64  the server side: clang-cl -m64 against the MSVC 14.4x STL and the Windows 10 SDK (vcvarsall x64),
             the toolchain of the x64 DLL and the server (decision D15). Language level C++14 (__cplusplus 201402).
Code in ipc/ is written to C++11 so that it builds in both trees.
Toolchain decision: epic 1200, 2026-11-10 (revised after D15). LLVM from C:\\Program Files\\LLVM (or VP_LLVM_BIN).
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# vcvarsall starts a background PowerShell for its telemetry unless this is set; PowerShell can hang at start on the
# workstation (bug 0108; 4000 group C, MC 2026-11-13)
os.environ.setdefault("VSCMD_SKIP_SENDTELEMETRY", "1")
LLVM = pathlib.Path(os.environ.get("VP_LLVM_BIN", r"C:\Program Files\LLVM\bin"))
VS2022 = pathlib.Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community")
VS90_TOOLS = pathlib.Path(os.environ.get("VS90COMNTOOLS", r"C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools"))
CMAKE_DIRS = [pathlib.Path(r"C:\Program Files\CMake\bin"),
              VS2022 / r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"]
NINJA = VS2022 / r"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

VSVARS32 = VS90_TOOLS / "vsvars32.bat"
VCVARSALL = VS2022 / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
ENV_SCRIPTS = {
    "x86": 'call "%s"' % VSVARS32,
    "x64": 'call "%s" x64' % VCVARSALL,
}


def capture_env(arch):
    """Run the toolchain's environment script in cmd and return the resulting environment."""
    out = subprocess.run(f'{ENV_SCRIPTS[arch]} >nul && set', shell=True, capture_output=True, text=True,
                         env=os.environ.copy())
    if out.returncode:
        sys.exit(f"environment script failed for {arch}:\n{out.stdout}\n{out.stderr}")
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
    ap.add_argument("--arch", default="both", choices=["x86", "x64", "both"])
    ap.add_argument("--config", default="Release", choices=["Release", "Debug", "RelWithDebInfo"])
    ap.add_argument("--test", action="store_true", help="run ctest after building")
    ap.add_argument("--clean", action="store_true", help="delete the build tree first")
    ap.add_argument("--target", default=None, help="build one target only")
    ap.add_argument('--quietlock-override', metavar='"WHO: WHY"',
                    help='start although another team holds the quiet-machine lock (emergencies only, '
                         'logged; COMMON rule 16)')
    a = ap.parse_args()
    # COMMON rule 16 launch gate (perf/quietlock/gate.py, tracker 7006): refuse while another team holds the
    # quiet-machine lock; the holder's own runs pass (C5_QUIETLOCK_WHO); a gate error never blocks.
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'perf' / 'quietlock'))
    try:
        import gate as _launch_gate
    except ImportError:
        _launch_gate = None
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    if _launch_gate:
        _launch_gate.require('an ipc build (ipc/build.py)', a.quietlock_override)

    arches = ["x86", "x64"] if a.arch == "both" else [a.arch]
    rc = 0
    for arch in arches:
        env = capture_env(arch)
        bdir = ROOT / "build" / arch / a.config
        if a.clean and bdir.exists():
            shutil.rmtree(bdir)
        # Configure every time (cheap with a cache): ipc/CMakeLists.txt picks up testapps/ and bench/ only at
        # configure time, so a tree configured before they existed would silently skip them.
        if True:
            cfg = ["cmake", "-G", "Ninja", "-S", str(ROOT), "-B", str(bdir),
                   f"-DCMAKE_BUILD_TYPE={a.config}",
                   f"-DCMAKE_MAKE_PROGRAM={NINJA.as_posix()}",
                   f"-DCMAKE_TOOLCHAIN_FILE={(ROOT / 'ipc' / 'cmake' / ('clang-cl-%s.cmake' % arch)).as_posix()}",
                   f"-DC5_TOOLCHAIN={'vc9' if arch == 'x86' else 'msvc14'}"]
            if run(cfg, env):
                return 1
        cmd = ["cmake", "--build", str(bdir)]
        if a.target:
            cmd += ["--target", a.target]
        if run(cmd, env):
            return 1
        if a.test:
            rc |= run(["ctest", "--test-dir", str(bdir), "--output-on-failure"], env)
    return rc


if __name__ == "__main__":
    sys.exit(main())
