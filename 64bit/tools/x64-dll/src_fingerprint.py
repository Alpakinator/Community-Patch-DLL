"""Source fingerprint of the DLL fork (issue 1508): the value of `fingerprint` in deps/dll.lock.

    python src_fingerprint.py --git <fork clone> [--commit REV]   from git (default REV: the commit of deps/dll.lock);
                                                                   prints the `fingerprint = ...` line for the lock
    python src_fingerprint.py --dir <source tree>                  from files on disk (what resolve_src checks for a
                                                                   plain copy without .git)

What is hashed (build.py FINGERPRINT_*): every *.h/.hpp/.inl/.c/.cpp/.def under the fork's build-input directories
(CvGameCoreDLL_Expansion2, CvGameCoreDLLUtil, CvGameDatabase, CvLocalization, CvWorldBuilderMap, FirePlace,
ThirdPartyLibs) plus build_vp_clang.py, CRLF folded to LF. Run with --git after re-pinning deps/dll.lock and paste the
printed line into the lock. Reads only; writes nothing.
"""
import argparse
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('x64build', HERE / 'build.py')
x64build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(x64build)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--git', type=Path, help='a clone of the DLL fork (any checkout state; only objects are read)')
    g.add_argument('--dir', type=Path, help='a source tree on disk')
    ap.add_argument('--commit', help='with --git: the revision (default: the commit of deps/dll.lock)')
    a = ap.parse_args()
    if a.git:
        rev = a.commit or x64build.read_dll_lock()['commit']
        digest, n = x64build.fingerprint_git(a.git, rev)
        print(f'fingerprint = {x64build.FINGERPRINT_VERSION} {digest} {n} files')
    else:
        digest, n = x64build.fingerprint_dir(a.dir)
        lock = x64build.lock_fingerprint(x64build.read_dll_lock())
        match = 'matches deps/dll.lock' if lock and lock[1] == digest else 'does NOT match deps/dll.lock'
        print(f'{x64build.FINGERPRINT_VERSION} {digest} {n} files ({match})')
        sys.exit(0 if lock and lock[1] == digest else 1)


if __name__ == '__main__':
    main()
