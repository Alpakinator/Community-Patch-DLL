"""Issue 4106: product tests of bakecompare's same-session options (--decals, --mods, --t8-rule decals, --tie-order,
--tile-flags, --drop-decals). Each case runs the built tool and asserts its exit code and the stderr text; a missing
binary or a missing input fails (skipped counts as failed, COMMON 9).

    python -I render/tools/bakecompare/test_bakecompare_4106.py BAKECOMPARE_EXE [--dtap FILE.dtap]

--dtap (default: the Tiny recording F:/c5-64bit-work/0166/4106/ref/C5H-0166-TINY_0008/armS/decals/load0003.dtap, game
data, never committed) is used by the positive case: the server's reader accepts it, and the run then stops at the
missing reference (exit 3), which shows the decal checks come first and pass. Exit 0 all passed, 1 a case failed, 2 usage.
"""
import json
import os
import subprocess
import sys
import tempfile

DTAP = "F:/c5-64bit-work/0166/4106/ref/C5H-0166-TINY_0008/armS/decals/load0003.dtap"
DTAP_AREA = "F:/c5-64bit-work/0166/4111/ref/C5H-0166-4111-TINY_0008/armS/decals/load*.dtap"   # 4111
DTAP_WALK = "F:/c5-64bit-work/0366/ref/C5H-0366-TINY_0008/armS/decals/load*.dtap"   # 0366: with the walk block


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    exe = argv[1]
    dtap = argv[argv.index("--dtap") + 1] if "--dtap" in argv else DTAP
    if not os.path.isfile(exe):
        print("FAIL: bakecompare binary missing: %s" % exe)
        return 1
    tmp = tempfile.mkdtemp(prefix="bc4106_", dir="C:/c5-64bit-work/0166/4106" if os.path.isdir("C:/c5-64bit-work/0166/4106") else None)
    ref = os.path.join(tmp, "ref")          # an empty folder: the reference checks come after the 4106 inputs
    os.makedirs(ref)
    junk = os.path.join(tmp, "junk.dtap")
    open(junk, "wb").write(b"C5DT" + b"\x01\x00\x00\x00" + os.urandom(200))
    short = os.path.join(tmp, "short.dtap")
    open(short, "wb").write(b"C5DT")
    flags_bad = os.path.join(tmp, "flags_bad.json")
    open(flags_bad, "w").write("{not json")
    flags_empty = os.path.join(tmp, "flags_empty.json")
    open(flags_empty, "w").write(json.dumps({"tiles": []}))
    flags_field = os.path.join(tmp, "flags_field.json")
    open(flags_field, "w").write(json.dumps({"tiles": [{"tx": 0, "ty": 0, "f2fc": 1}]}))
    base = ["--ref", ref, "--game-dir", tmp, "--exe", exe, "--out", os.path.join(tmp, "out")]
    cases = [
        # name, extra args, expected exit, expected stderr text
        ("t8 rule decals without --decals", ["--t8-rule", "decals"], 2, "--t8-rule decals needs --decals"),
        ("t8 rule tap without --decals (4111)", ["--t8-rule", "tap"], 2, "--t8-rule tap needs --decals"),
        ("tie order without --decals", ["--tie-order", "reverse"], 2, "--tie-order needs --decals"),
        ("drop without --decals", ["--drop-decals", "5"], 2, "--drop-decals needs --decals"),
        ("unknown tie order", ["--decals", junk, "--tie-order", "random"], 2, "--tie-order takes list, reverse, small-rev, large-first, listorder or rule4111"),
        ("bad drop list", ["--decals", junk, "--drop-decals", "5,x"], 2, "--drop-decals: not a list order: x"),
        ("t8 rule unknown", ["--t8-rule", "maybe"], 2, "--t8-rule takes exe, list, decals or tap"),
        ("decal file missing", ["--decals", os.path.join(tmp, "nope.dtap")], 3, "decal file"),
        ("decal file random bytes", ["--decals", junk], 4, "refused by the server's reader"),
        ("decal file truncated", ["--decals", short], 4, "refused by the server's reader"),
        ("mod folder missing", ["--mods", os.path.join(tmp, "nomods")], 3, "mod folder"),
        ("tile flags missing", ["--tile-flags", os.path.join(tmp, "nope.json")], 3, "tile flags"),
        ("tile flags not JSON", ["--tile-flags", flags_bad], 4, flags_bad),
        ("tile flags without tiles", ["--tile-flags", flags_empty], 4, "has no \"tiles\" array"),
        ("tile flags without f300", ["--tile-flags", flags_field], 4, "a tile without tx, ty, f2fc, f300"),
    ]
    if os.path.isfile(dtap):
        cases.append(("recorded decal file accepted, then the empty reference", ["--decals", dtap, "--tie-order", "small-rev"], 3,
                      "manifest.json missing"))
        # 4111: 4106's recording has no area block: the tap rule refuses it
        cases.append(("t8 rule tap on a .dtap without the area block (4111)", ["--decals", dtap, "--t8-rule", "tap"], 5,
                      "carries no area block"))
    else:
        print("FAIL: the positive case's .dtap is missing: %s" % dtap)
        return 1
    # 4111: a recording with the area block (4111's Tiny), accepted with the tap rule, then the empty reference
    import glob
    area = sorted(glob.glob(DTAP_AREA))
    if not area:
        print("FAIL: 4111's .dtap with the area block is missing: %s" % DTAP_AREA)
        return 1
    cases.append(("recorded decal file with the area block accepted, tap rule (4111)", ["--decals", area[-1], "--t8-rule", "tap",
                  "--tie-order", "listorder"], 3, "manifest.json missing"))
    # 0366: a recording with the walk block, accepted in the EXE's order and with --tie-order rule4111
    walk = sorted(glob.glob(DTAP_WALK))
    if not walk:
        print("FAIL: 0366's .dtap with the walk block is missing: %s" % DTAP_WALK)
        return 1
    cases.append(("recorded decal file with the walk block accepted (0366)", ["--decals", walk[-1], "--t8-rule", "tap"], 3,
                  "manifest.json missing"))
    cases.append(("recorded decal file with the walk block accepted, rule4111 (0366)", ["--decals", walk[-1], "--tie-order",
                  "rule4111"], 3, "manifest.json missing"))
    failed = 0
    for name, extra, want, text in cases:
        p = subprocess.run([exe] + base + extra, capture_output=True, text=True)
        ok = p.returncode == want and text in p.stderr and "refused" not in p.stderr if want == 3 and "accepted" in name else \
            p.returncode == want and text in p.stderr
        print("%s: %s (exit %d, want %d)%s" % ("ok" if ok else "FAIL", name, p.returncode, want,
                                              "" if ok else "\n    stderr: " + p.stderr.strip()[:300]))
        failed += not ok
    print("%d cases, %d failed" % (len(cases), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
