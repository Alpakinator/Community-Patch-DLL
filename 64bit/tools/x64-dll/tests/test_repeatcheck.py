"""Issue 0131: the repeatability check of the x64 DLL's link inputs (tools/x64-dll/repeatcheck.py) and the /Brepro
compile flag that keeps native objects free of time stamps. Offline: synthetic objects and archives, no compiler.

    python -m pytest tools/x64-dll/tests/test_repeatcheck.py -q -p no:cacheprovider
"""
from __future__ import annotations

import importlib.util
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
X64 = HERE.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


rc = _load("repeatcheck_0131", X64 / "repeatcheck.py")
x64build = _load("x64build_0131", X64 / "build.py")


def coff(ts: int, machine: int = 0x8664) -> bytes:
    return struct.pack('<HHIIIHH', machine, 1, ts, 0, 0, 0, 0) + b'\0' * 40


def bigobj(ts: int) -> bytes:
    return struct.pack('<HHHHI', 0, 0xFFFF, 2, 0x8664, ts) + b'\0' * 40


def short_import(ts: int) -> bytes:
    return struct.pack('<HHHHI', 0, 0xFFFF, 0, 0x8664, ts) + b'\0' * 8


def archive(members) -> bytes:
    """A COFF archive with a first linker member, a long-name table and the given (name, body) members."""
    longnames = b''.join(n.encode() + b'\0' for n, _ in members)
    parts = [b'!<arch>\n']

    def add(name: str, body: bytes):
        hdr = name.encode().ljust(16) + b'0'.ljust(12) + b''.ljust(6) + b''.ljust(6) + b'644'.ljust(8) + \
            str(len(body)).encode().ljust(10) + b'`\n'
        parts.append(hdr + body + (b'\n' if len(body) & 1 else b''))
    add('/', b'\0\0\0\0')
    add('//', longnames)
    off = 0
    for n, body in members:
        add('/%d' % off, body)
        off += len(n) + 1
    return b''.join(parts)


def test_member_kinds():
    assert rc.member_stamp(coff(5)) == ('coff', 5)
    assert rc.member_stamp(bigobj(7)) == ('bigobj', 7)
    assert rc.member_stamp(short_import(9)) == ('import', 9)
    assert rc.member_stamp(b'BC\xc0\xde' + b'\0' * 30)[0] == 'bitcode'
    assert rc.member_stamp(b'\xde\xc0\x17\x0b' + b'\0' * 30)[0] == 'bitcode'
    assert rc.member_stamp(b'xx') == (None, None)


def _build(tmp_path, stamp):
    out = tmp_path / 'out'
    (out / 'dll' / 'obj').mkdir(parents=True)
    (out / 'stubs').mkdir()
    (out / 'dll' / 'obj' / 'a.obj').write_bytes(b'BC\xc0\xde' + b'\0' * 30)          # LTO bitcode: never stamped
    (out / 'stubs' / 'FireWorks.lib').write_bytes(archive([('obj/FireWorks/FString.obj', coff(stamp)),
                                                           ('obj/_common/stublog.obj', bigobj(0))]))
    (out / 'stubs' / 'lua51_x64.lib').write_bytes(archive([('lua51_x64.dll', short_import(1794588320))]))
    rsp = out / 'dll' / 'link.rsp'
    rsp.write_text('/OUT:"CvGameCore_Expansion2.dll"\n/Brepro\n/PDBSOURCEPATH:C:\\vpbuild\n"obj\\a.obj"\n'
                   '"..\\stubs\\FireWorks.lib"\n"..\\stubs\\lua51_x64.lib"\nkernel32.lib\n', encoding='utf-8')
    return rsp


def test_clean_inputs_pass(tmp_path):
    r = rc.check(_build(tmp_path, 0))
    assert r['ok'] and r['inputs'] == 3 and r['members'] == 4 and r['stamped'] == 0, r
    assert rc.line(r, 'AB').startswith('repeatability check: OK: 3 link inputs') and r['examples'] == []


def test_stamped_stub_member_is_named(tmp_path):
    """The 9045 case: a stub object compiled without /Brepro carries its compile time."""
    r = rc.check(_build(tmp_path, 1794588319))
    assert not r['ok'] and r['stamped'] == 1, r
    assert r['examples'] == [('FireWorks.lib', 'obj/FireWorks/FString.obj', 'coff', 1794588319)]
    t = rc.line(r, 'AB')
    assert t.startswith('repeatability check: NOT REPEATABLE: 1 of 4') and 'FString.obj' in t and 'AB' in t


def test_no_inputs_is_not_ok(tmp_path):
    rsp = tmp_path / 'link.rsp'
    rsp.write_text('/DLL\nkernel32.lib\n"missing\\x.obj"\n', encoding='utf-8')
    r = rc.check(rsp)
    assert not r['ok'] and r['inputs'] == 0
    assert 'no link input' in rc.line(r)


def test_command_line_exit_codes(tmp_path):
    ok = subprocess.run([sys.executable, '-I', str(X64 / 'repeatcheck.py'), str(_build(tmp_path / 'a', 0))],
                        capture_output=True, text=True)
    assert ok.returncode == 0 and ok.stdout.startswith('repeatability check: OK'), ok.stdout + ok.stderr
    bad = subprocess.run([sys.executable, '-I', str(X64 / 'repeatcheck.py'), str(_build(tmp_path / 'b', 5))],
                         capture_output=True, text=True)
    assert bad.returncode == 1 and 'NOT REPEATABLE' in bad.stdout, bad.stdout + bad.stderr
    use = subprocess.run([sys.executable, '-I', str(X64 / 'repeatcheck.py')], capture_output=True, text=True)
    assert use.returncode != 0 and 'usage' in (use.stdout + use.stderr).lower()


def test_every_compile_gets_brepro():
    """repro_cl_args is the helper every clang-cl compile of the DLL, the stubs, the libraries, the server and the
    proxy takes (test_repro.py checks the callers); it carries /Brepro."""
    assert '/Brepro' in x64build.repro_cl_args((Path(r'D:\w\out'), 'VPOUT'))
    assert '/Brepro' in x64build.cl_args(Path(r'D:\some\src'), 'release', False, None)


def test_build_prints_the_check_line():
    t = (X64 / 'build.py').read_text(encoding='utf-8')
    assert "repeatcheck.check(out / 'link.rsp')" in t and 'repeatcheck.line(' in t
