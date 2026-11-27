"""Ticket 0586: the server's ice and coast-wave decisions on recorded inputs (offline, no game).

Reads the plot tables of a scene bundle's feed.bin (offline-scene-harness.md 2.5; reader: render/evidence/0431/4550/
c5data.py), keeps the first table and every later one that differs from the one before in terrain, plot type, feature or
flags, writes them as test_icewaves.exe's --replay file and runs it: per table, whether the server would ask the EXE for
Ice_Reload (and which plots' bits it flips) or Waves_Reload, and what the gate holds.

    python -I server/tests/render/liveterrain/icewaves/icewaves_replay.py --exe <lt-tests>/bin/test_icewaves.exe \
        --bundle <bundle folder> [--bundle ...] --work <scratch dir> [--out result.json]

Exit 0 when every bundle ran; the decisions are printed and written to --out.
"""
import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]
KEYS = ("terrain", "plotType", "feature", "flags")


def load_c5data():
    spec = importlib.util.spec_from_file_location("c5data", REPO / "render" / "evidence" / "0431" / "4550" / "c5data.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--bundle", required=True, type=Path, action="append")
    ap.add_argument("--work", required=True, type=Path)
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    c5 = load_c5data()
    a.work.mkdir(parents=True, exist_ok=True)
    results = []
    rc_all = 0
    for b in a.bundle:
        tabs = c5.feed_tables(str(b / "feed.bin"))
        keep = []
        for _caller, _gen, w, h, t in tabs:
            row = [tuple(p[k] for k in KEYS) for p in t]
            if not keep or keep[-1][2] != row:
                keep.append((w, h, row))
        w, h = keep[0][0], keep[0][1]
        keep = [k for k in keep if (k[0], k[1]) == (w, h)]
        txt = a.work / (b.name + ".tables.txt")
        with open(txt, "w", encoding="ascii") as f:
            f.write("%d %d %d\n" % (w, h, len(keep)))
            for _w, _h, row in keep:
                f.write(" ".join("%d %d %d %d" % r for r in row) + "\n")
        cp = subprocess.run([str(a.exe), "--replay", str(txt)], capture_output=True, text=True, timeout=120)
        lines = [json.loads(ln) for ln in cp.stdout.splitlines() if ln.startswith("{")]
        print("%s: %d feed tables, %d distinct, map %d x %d, exit %d" % (b.name, len(tabs), len(keep), w, h, cp.returncode))
        for ln in lines:
            print("  " + json.dumps(ln))
        if cp.returncode != 0:
            rc_all = 1
            print(cp.stdout + cp.stderr)
        results.append({"bundle": b.name, "feed_tables": len(tabs), "distinct": len(keep), "map": [w, h], "decisions": lines})
    if a.out:
        a.out.write_text(json.dumps(results, indent=1), encoding="utf-8")
    return rc_all


if __name__ == "__main__":
    sys.exit(main())
