"""Ticket 4905 (epic 0465): the EXE field write (server/src/render/srv_liveterrain_exefield.*) against a mock grid.

Runs test_exefield.exe (built by test_exefield_build.py into C5_EXEFIELD_BUILD/bin, or bin-asan with
C5_EXEFIELD_ASAN=1). CPU only: the mock GridHeightmap lives in the test program's own memory. Every case asserts its own
outputs inside the program; here each case's line must say PASS and the exit code 0. A missing binary fails (no skip).

    set C5_EXEFIELD_BUILD=C:\\c5-64bit-work\\0465\\4905\\ef-tests
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest server/tests/render/liveterrain/exe_field/test_exefield.py -q -p no:cacheprovider
"""
import os
import re
import subprocess
from pathlib import Path

CASES = ["write_readback", "wrap_split", "nothing_to_do", "refusals", "mapped_view", "image_page", "rollback",
         "open_target", "run_inside", "requery", "cost", "build_addresses"]
REPO = Path(__file__).resolve().parents[5]
WRITER = [REPO / "server" / "src" / "render" / "srv_liveterrain_exefield.cpp",
          REPO / "server" / "src" / "render" / "srv_liveterrain_exefield.h"]
FORBIDDEN = re.compile(r"\b(VirtualProtect|VirtualProtectEx|WriteProcessMemory|VirtualAllocEx|VirtualFreeEx)\b")


def code_only(text):
    """The source without // and /* */ comments (string literals kept: a call cannot hide in one)."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def test_exefield_writer_never_changes_protection_or_uses_writeprocessmemory():
    """MC #6744 (b): the writer calls no VirtualProtect(Ex) and no WriteProcessMemory (only NtWriteVirtualMemory, which
    never makes a page writable), and allocates or frees nothing in the EXE."""
    for p in WRITER:
        code = code_only(p.read_text(encoding="utf-8"))
        hits = sorted(set(FORBIDDEN.findall(code)))
        assert not hits, "%s calls %s outside comments" % (p.name, ", ".join(hits))
        assert p.suffix != ".cpp" or "NtWriteVirtualMemory" in code, "%s: the writes no longer go through NtWriteVirtualMemory" % p.name


def test_exefield_scan_sees_a_call():
    """The scan above is not blind: a call in code is found, one in a comment is not."""
    assert FORBIDDEN.findall(code_only("int a; WriteProcessMemory(h, p, b, n, 0);"))
    assert not FORBIDDEN.findall(code_only("// WriteProcessMemory(h)\n/* VirtualProtectEx */ int a;"))


def exe():
    d = os.environ.get("C5_EXEFIELD_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_EXEFIELD_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_exefield.exe"
    assert d and p.exists(), "C5_EXEFIELD_BUILD must name a test_exefield_build.py --out folder with %s/test_exefield.exe" % sub
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_exefield_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "ERROR: AddressSanitizer" not in out, out
    assert " 0 failed" in out and rc == 0, out


def test_exefield_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_exefield_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
