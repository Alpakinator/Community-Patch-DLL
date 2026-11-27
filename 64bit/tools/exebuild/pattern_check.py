"""pattern_check.py - check a product byte-pattern table against another EXE build through addr_match (ticket 0582).

Origin: a copy of research/02-exe-internals/evidence/0582/pattern_check.py at commit 94a825474 (ticket 0582), taken
into tools/exebuild/ on 2026-11-19 by ticket 4960 (epic 0590); the evidence copy stays as it was. gen_table.py does the
same check through the build table (symbol names, `??`, E8/E9 rel32 free); this script is kept for comparison.

    python -I pattern_check.py OLD.exe NEW.exe [header]   (default: shell/common/exe_camera.h)

Reads the X(0xVA, "name", "bytes") pattern table of the header (the exe_camera.h syntax: hex bytes, R(XXXXXX) = a
relocated dword holding VA XXXXXX), carries every VA (the pattern's own address and every R(...) operand) to the new
build with addr_match, rebuilds the expected bytes with the new operands, treats the rel32 of each E8 call as free
(callees moved), and compares with the new EXE. Prints OK/BAD per pattern and the carried addresses; prints no EXE
bytes (D83). 2026-11-19, build 4390913 -> 26577625: exe_camera.h 11 of 11 OK.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from addr_match import Img, match_code, match_data  # noqa: E402


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    hdr = argv[3] if len(argv) > 3 else os.path.join(os.path.dirname(__file__), "..", "..", "shell",
                                                       "common", "exe_camera.h")
    old, new = Img(argv[1]), Img(argv[2])
    sigs = re.findall(r'X\((0x[0-9A-Fa-f]+), "([^"]+)", "([^"]+)"\)', open(hdr, encoding="utf-8").read())
    cache = {}

    def mp(va):
        if va not in cache:
            cache[va] = (match_code if old.sec(va) == ".text" else match_data)(old, new, va)[0]
        return cache[va]

    ok = 0
    for va, name, pat in sigs:
        va = int(va, 16)
        nva = mp(va)
        if not nva:
            print("UNMAPPED 0x%X %s" % (va, name))
            continue
        exp = []
        for r, h in re.findall(r"R\(([0-9A-Fa-f]+)\)|([0-9A-Fa-f]{2})", pat):
            if r:
                t = mp(int(r, 16))
                exp += list(struct.pack("<I", t)) if t else [None] * 4
            else:
                exp.append(int(h, 16))
        i = 0
        while i < len(exp):
            if exp[i] == 0xE8 and i + 4 < len(exp):
                exp[i + 1:i + 5] = [None] * 4
                i += 5
            else:
                i += 1
        got = new.pe.read_rva(nva - new.base, len(exp))
        good = all(e is None or e == x for e, x in zip(exp, got))
        ok += good
        print("%s 0x%X -> 0x%X %s" % ("OK " if good else "BAD", va, nva, name))
    print("%d of %d patterns hold on the new build" % (ok, len(sigs)))
    return 0 if ok == len(sigs) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
