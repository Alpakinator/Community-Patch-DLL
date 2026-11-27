"""Build the x64 smoke host (issue 1007): smoke_host.exe.

Same toolchain, /MD and defines as the DLL (tools/x64-dll/build.py, issue 1001), so the interface layouts and
the CRT heap are the DLL's. Steps: generate `host_ifaces.gen.h` from the fork's headers (gen_ifaces.py),
compile src/*.cpp with clang-cl -m64, link with lld-link (console EXE with an export table: the 22 host
exports the DLL's FireWorks stubs resolve with GetProcAddress(GetModuleHandle(NULL))). The host links Lua through
the import library `lua51_x64.lib` from the stubs directory (issue 1009) with /DELAYLOAD, and loads lua51_x64.dll
by full path at run time, so the host and the DLL share the one Lua copy.

The host's own sources compile with /WX (warnings are errors, issue 1405); the fork's headers are included as
external headers (/external:I, /external:W0), so their warnings do not count. `--src` is checked against
deps/dll.lock as in build.py.

Usage: python build_host.py --out DIR --stubs STUBDIR [--src FORK] [--unlocked] [--config debug|release]
Outputs: DIR/smoke_host.exe, .pdb, .map, DIR/build_host.log, DIR/gen/host_ifaces.gen.h
"""
import argparse
import importlib.util
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('x64build', HERE.parent / 'build.py')
x64build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x64build)
sys.path.insert(0, str(HERE))
import gen_ifaces  # noqa: E402

# The DLL's defines minus the ones that say "this is the DLL being built".
NOT_FOR_HOST = {'_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS'}
INIT_LOCKS_X64 = '??4_Init_locks@std@@QEAAAEAV01@AEBV01@@Z'


class Log:
    def __init__(self, path: Path):
        self.f = path.open('w', encoding='utf-8')

    def __call__(self, msg, echo=True):
        self.f.write(msg + '\n')
        self.f.flush()
        if echo:
            print(msg, flush=True)

    def close(self):
        self.f.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source (headers): git checkout at the deps/dll.lock commit or a plain copy of it')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit')
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--stubs', type=Path, required=True, help='build_stubs.py --out dir (holds lua51_x64.lib)')
    a = ap.parse_args()
    try:
        a.src = x64build.resolve_src(a.src, a.unlocked)['src']
    except x64build.SrcError as e:
        sys.exit(f'build_host: FAILED: {e}')
    lua_lib = (a.stubs / 'lua51_x64.lib').resolve()
    if not lua_lib.exists():
        sys.exit(f'build_host: FAILED: {lua_lib} not found: build the stubs with build_stubs.py --lua dll')
    # Issue 1404: the host opens the gameplay database through the real CvGameDatabase library (issue 1402).
    db_lib = (a.stubs / 'CvGameDatabase_x64.lib').resolve()
    if not db_lib.exists():
        sys.exit(f'build_host: FAILED: {db_lib} not found: build it with cvgamedatabase/build_db.py --out <stubs>')
    out = a.out.resolve()
    (out / 'obj').mkdir(parents=True, exist_ok=True)
    (out / 'gen').mkdir(exist_ok=True)
    log = Log(out / 'build_host.log')
    t0 = time.perf_counter()

    (out / 'gen' / 'host_ifaces.gen.h').write_text(gen_ifaces.generate(a.src), encoding='utf-8')
    log(f'generated {out / "gen" / "host_ifaces.gen.h"} from {a.src}')

    env = x64build.load_vcvars_env(log, out / 'vcvars-env.json')
    args = ['-m64', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/Zi', '/FS', '/std:c++14',
            '-fdiagnostics-absolute-paths', '/W3', '/WX']
    args += ['/O2'] if a.config == 'release' else ['/Od', '/Oy-']
    args += [f'/D{d}' for d in x64build.PREDEFS['debug'] if d not in NOT_FOR_HOST and d != 'VPDEBUG']
    args += ['/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING']
    args += ['/external:W0'] + [f'/external:I{a.src / d}' for d in x64build.INCLUDE_DIRS]
    args += [f'/I{HERE / "src"}', f'/I{out / "gen"}']
    args += [f'-Wno-{w}' for w in x64build.CL_SUPPRESS] + ['-Wno-deprecated-declarations', '-Wno-microsoft-include',
                                                           '-Wno-pragma-pack', '-Wno-unused-value',
                                                           '-Wno-overloaded-virtual', '-Wno-invalid-offsetof']
    objs = []
    ok = True
    for cpp in sorted((HERE / 'src').glob('*.cpp')):
        obj = out / 'obj' / (cpp.stem + '.obj')
        cmd = [str(x64build.CLANG_CL)] + args + [f'/Fo{obj}', f'/Fd{out / "obj" / "vc.pdb"}', str(cpp)]
        cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
        log(f'compile {cpp.name}: rc={cp.returncode}')
        if cp.stdout.strip() or cp.stderr.strip():
            log((cp.stdout + cp.stderr).rstrip(), echo=cp.returncode != 0)
        ok &= cp.returncode == 0
        objs.append(obj)
    if not ok:
        log('build_host: FAILED: compile errors (warnings are errors, /WX); see build_host.log')
        sys.exit(1)

    exe = out / 'smoke_host.exe'
    lines = [f'/OUT:"{exe}"', f'/PDB:"{exe.with_suffix(".pdb")}"', f'/MAP:"{exe.with_suffix(".map")}"',
             '/MACHINE:X64', '/SUBSYSTEM:CONSOLE', '/DEBUG', '/DYNAMICBASE', '/NXCOMPAT', '/ERRORLIMIT:0',
             f'"/EXPORT:{INIT_LOCKS_X64}=HostInitLocksAssign"', '/DELAYLOAD:lua51_x64.dll',
             '/DELAYLOAD:CvGameDatabase_x64.dll']
    lines += [f'"{o}"' for o in objs]
    lines += [f'"{lua_lib}"', f'"{db_lib}"', 'delayimp.lib', 'kernel32.lib', 'user32.lib', 'advapi32.lib', 'ole32.lib']
    rsp = out / 'link_host.rsp'
    rsp.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    cp = subprocess.run([str(x64build.LLD_LINK), f'@{rsp}'], capture_output=True, text=True, errors='replace', env=env,
                        cwd=str(out))
    log(f'link: rc={cp.returncode}')
    if cp.stdout.strip() or cp.stderr.strip():
        log((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0:
        log(f'build_host: FAILED: link rc={cp.returncode}; see build_host.log')
        sys.exit(1)
    log(f'built {exe} in {time.perf_counter() - t0:.1f} s')
    log.close()


if __name__ == '__main__':
    main()
