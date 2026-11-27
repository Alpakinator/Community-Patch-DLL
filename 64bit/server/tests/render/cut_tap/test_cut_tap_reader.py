"""Ticket 4553: the cut tap's reader (server/src/render/srv_cut_tap.cpp), on images and control blocks made by the test
program itself (cut_tap_test.exe, build_cut_tap_tests.py: a fake producer in its own process).

Every case asserts its own outputs inside the program: the grid applied cell for cell against the synthetic grid (the same
bytes as shell/composite/cut_tap.cpp's test world), the ack (generation, result, pid), the CutSource generation; each
refusal's reason, ackResult, counter, LOUD count and lastWhy, the last good grid kept and the refused generation not
retried (21 refusals over the block, the section, the image header, the hash, the sizes, the offsets and the directory);
a refused first image and a FAILED producer (pattern, too big) are install failures; OFF drops the grid (LOUD), NO_GRID
keeps it; a torn control block is counted and never acked; the file form (loadImage) and its refusals. Here: exit 0, a
PASS line for every case the program lists (read from its source, so a case cannot be forgotten), no FAIL line, "0
skipped", and no AddressSanitizer report. A missing binary fails (no skip).

    set C5_CUT_BUILD=C:\\c5-64bit-work\\4553\\reader
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/render/cut_tap/test_cut_tap_reader.py -q -p no:cacheprovider
(C5_CUT_ASAN=1 runs bin-asan.)
"""
import os
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent


def exe():
    d = os.environ.get("C5_CUT_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_CUT_ASAN") == "1" else "bin"
    p = Path(d) / sub / "cut_tap_test.exe"
    assert d and p.exists(), f"C5_CUT_BUILD must name a build_cut_tap_tests.py --out folder with {sub}/cut_tap_test.exe"
    return str(p)


def cases_in_source():
    src = (HERE / "cut_tap_test.cpp").read_text(encoding="utf-8")
    return re.findall(r'Run\("([a-z0-9_]+)"', src)


def test_every_case_passes():
    p = subprocess.run([exe()], capture_output=True, text=True, timeout=300, creationflags=0x08000000)
    out = p.stdout + p.stderr
    want = cases_in_source()
    assert len(want) == 31, want
    passed = re.findall(r"^PASS ([a-z0-9_]+)$", out, re.M)
    assert p.returncode == 0, out
    assert sorted(passed) == sorted(want), (set(want) - set(passed), out)
    assert "FAIL " not in out, out
    assert "cut_tap_test: %d passed, 0 failed, 0 skipped" % len(want) in out, out
    assert "AddressSanitizer" not in out, out
