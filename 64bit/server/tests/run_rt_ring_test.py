"""Issue 0119 (2026-11-13): offline unit test of the server's RETURN payload ring, KeepReturn() of
server/src/remote/srv_rt.cpp. About 10 s; no game, no DLL, no fork source.

  python server/tests/run_rt_ring_test.py [--rt-src FILE | --rev REV] [--out DIR]

Copies the function `static void KeepReturn(vpipc::Reader& r)` out of srv_rt.cpp (from its line to the first line that is
a lone `}`) into <out>/keep_return.inc, compiles it with server/tests/test_rt_ring.cpp and ipc/src/marshal.cpp (the real
vpipc::Reader; clang-cl x64, the toolchain of tools/x64-dll/build.py) and runs it. --rt-src tests another file, --rev the
srv_rt.cpp of a git revision (`git show REV:server/src/remote/srv_rt.cpp`), e.g. the unfixed one: before issue 0119 the
ring keeps a 12.66 MB RETURN for the life of the thread and fails "held bytes fall back under 64 x 64 KB".
Exit 0 when every check passes, 1 when one fails, 2 when the function is not found or the build fails.
Start it through quietlock.py run (COMMON rule 16).
"""
import argparse
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
RT_REL = 'server/src/remote/srv_rt.cpp'
SIGNATURE = 'static void KeepReturn(vpipc::Reader& r)'


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def extract_keep_return(text):
    """The lines of KeepReturn's definition, or None. The function ends at the first line that is a lone '}'."""
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == SIGNATURE:
            for j in range(i + 1, len(lines)):
                if lines[j].rstrip() == '}':
                    return '\n'.join(lines[i:j + 1]) + '\n'
            return None
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--rt-src', type=Path, help=f'the srv_rt.cpp to test (default: {RT_REL} of this tree)')
    g.add_argument('--rev', help='test the srv_rt.cpp of this git revision')
    ap.add_argument('--out', type=Path, help='build folder (default: a new temporary folder)')
    a = ap.parse_args()

    out = (a.out or Path(tempfile.mkdtemp(prefix='rt-ring-test-'))).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if a.rev:
        cp = subprocess.run(['git', '-C', str(REPO), 'show', f'{a.rev}:{RT_REL}'], capture_output=True)
        if cp.returncode != 0:
            print(f'run_rt_ring_test: git show {a.rev}:{RT_REL} failed: {cp.stderr.decode(errors="replace").strip()}')
            return 2
        text = cp.stdout.decode('utf-8', errors='replace')
        origin = f'{a.rev}:{RT_REL}'
    else:
        src = a.rt_src or (REPO / RT_REL)
        text = src.read_text(encoding='utf-8', errors='replace')
        origin = str(src)
    body = extract_keep_return(text)
    if body is None:
        print(f'run_rt_ring_test: "{SIGNATURE}" not found in {origin}')
        return 2
    (out / 'keep_return.inc').write_text(f'// extracted from {origin} by server/tests/run_rt_ring_test.py\n' + body,
                                         encoding='utf-8')
    print(f'KeepReturn from {origin} ({len(body.splitlines())} lines) -> {out / "keep_return.inc"}')

    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m, flush=True), out / 'vcvars-env.json')
    exe = out / 'test_rt_ring.exe'
    if exe.exists():
        exe.unlink()
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
           f'/I{out}', f'/I{REPO / "ipc" / "include"}', f'/I{REPO / "ipc" / "src"}', f'/Fe{exe}', f'/Fo{out}\\',
           str(HERE / 'test_rt_ring.cpp'), str(REPO / 'ipc' / 'src' / 'marshal.cpp'), '/link', 'kernel32.lib']
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(out))
    print(f'compile+link: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0 or not exe.exists():
        print('run_rt_ring_test: BUILD FAILED')
        return 2
    rp = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace', creationflags=0x08000000)
    print(rp.stdout.rstrip())
    if rp.stderr.strip():
        print(rp.stderr.rstrip())
    print(f'run_rt_ring_test: {"OK" if rp.returncode == 0 else "FAILED"} (exit {rp.returncode}; {origin}); folder {out}')
    return 0 if rp.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
