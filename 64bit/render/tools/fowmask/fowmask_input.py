"""The input of fowmask (render/tools/fowmask/fowmask.cpp) from a stock capture (task 3952, epic 0168).

    python -I render/tools/fowmask/fowmask_input.py EXPORT_DIR CENSUS.json GRID_CONSTANTS.json OUT.txt
           [--drawn DRAWN.json | --derive]

EXPORT_DIR: render/evidence/0168/3952/fowmask_export.py's export (its terrain draw holds TerrainFrame: OverlayProj at
0x040). CENSUS.json: the Lua census of the capture's state (fields revealed, visible; plots row-major y * w + x).
GRID_CONSTANTS.json: the map's ground truth (M_h2w, grid_sx/sy, grid_ox/oy, wrap_x; D:/c5-64bit-work/i2-ground-truth/).
--drawn: fow_drawn.py's --json output: the view's range = the box of the plots stock drew; --derive: range left to the
module (X1 <= X0). One view (the captures hold single-view frames). Writes text only (no game bytes).
"""
import argparse
import json
import struct
from pathlib import Path


def terrain_frame(export):
    """(OverlayProj 0x040, ViewProj 0x000) of the export's terrain draw."""
    exp = Path(export)
    for d in json.loads((exp / "draws.json").read_text())["draws"]:
        if d["system"].startswith("terrain"):
            s = json.loads((exp / d["dir"] / "state.json").read_text())
            cb = s["VS"]["cb"].get("1") or s["PS"]["cb"].get("1")
            raw = (exp / cb["data"]["file"]).read_bytes()
            return list(struct.unpack_from("<16f", raw, 0x40)), list(struct.unpack_from("<16f", raw, 0))
    raise SystemExit("no terrain draw in " + export)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("export")
    ap.add_argument("census")
    ap.add_argument("grid")
    ap.add_argument("out")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--drawn")
    g.add_argument("--derive", action="store_true")
    a = ap.parse_args()
    c = json.loads(Path(a.census).read_text())
    gc = json.loads(Path(a.grid).read_text())
    F = {k: i for i, k in enumerate(c["fields"])}
    st = []
    for p in c["plots"]:
        st.append("2" if not p[F["revealed"]] else ("0" if p[F["visible"]] else "1"))
    h2w = [v for row in gc["M_h2w"] for v in row]
    op, vp = terrain_frame(a.export)
    if a.drawn:
        x0, x1, y0, y1 = json.loads(Path(a.drawn).read_text())["box"]
        rng = (x0, x1 + 1, y0, y1 + 1)
    else:
        rng = (0, 0, 0, 0)
    lines = ["c5r-fowmask-input 1",
             "# from %s, %s, %s" % (Path(a.export).name, Path(a.census).name, Path(a.grid).parent.name),
             "map %d %d %d" % (c["w"], c["h"], int(gc.get("wrap_x", 1))),
             "h2w " + " ".join(repr(float(v)) for v in h2w),
             "scale %r %r" % (float(gc["grid_sx"]), float(gc["grid_sy"])),
             "origin %r %r" % (float(gc["grid_ox"]), float(gc["grid_oy"])),
             "world %r %r" % (float(gc["wrap_period"]), float(gc["world_height"])),
             "states " + "".join(st),
             "views 1",
             "view " + " ".join(repr(float(v)) for v in op) + " %d %d %d %d" % rng,
             "camera " + " ".join(repr(float(v)) for v in vp)]
    Path(a.out).write_text("\n".join(lines) + "\n")
    print("wrote %s: map %dx%d, range %s" % (a.out, c["w"], c["h"], rng))


if __name__ == "__main__":
    main()
