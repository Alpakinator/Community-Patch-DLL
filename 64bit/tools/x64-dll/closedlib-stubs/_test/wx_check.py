"""Check the warnings-as-errors policy of build_stubs.py (issue 1405).

    python wx_check.py --out DIR [--src FORK]

Compiles two probe files with build_stubs.cpp_args (the flags every stub and common/ source gets):
  1. a file with an unused variable (-Wunused-variable, part of /W3 = -Wall): must FAIL (/WX);
  2. a file that includes the fork's FFastVector.h and instantiates BaseVector, which warns -Wreorder-ctor inside the
     fork's header: must COMPILE (the fork's include dirs are /external:I with /external:W0).
Exit 0 when both behave as expected.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import build_stubs as bs  # noqa: E402

PROBE_OWN = 'int cv64_wx_probe() { int unused = 1; return 0; }\n'
PROBE_FORK = ('#include "fw_prelude.h"\n'
              'int cv64_wx_probe2() { FFastVector<unsigned long, true> v; return (int)v.size(); }\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--src', type=Path, default=None)
    a = ap.parse_args()
    src = bs.x64build.resolve_src(a.src)['src']
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = bs.vcvars_env()
    args = bs.cpp_args(src, 'debug', 'MD') + [f'/I{HERE.parent / "FireWorks"}']
    # Control: the same fork-header probe with the fork's dirs as ordinary /I must fail, else probe 2 proves nothing.
    plain = [x.replace('/external:I', '/I') for x in args if x != '/external:W0']
    ok = True
    for name, text, want_ok, flags in (('probe_own.cpp', PROBE_OWN, False, args),
                                       ('probe_fork_header.cpp', PROBE_FORK, True, args),
                                       ('probe_fork_header_control.cpp', PROBE_FORK, False, plain)):
        f = out / name
        f.write_text(text, encoding='utf-8')
        _, rc, msg = bs.compile_one(env, flags, f, out / (f.stem + '.obj'))
        good = (rc == 0) == want_ok
        ok &= good
        print(f'{name}: rc={rc} (expected {"success" if want_ok else "failure"}): {"PASS" if good else "FAIL"}')
        if msg:
            print('  ' + msg.replace('\n', '\n  '))
    print('wx_check: ' + ('PASS' if ok else 'FAILED'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
