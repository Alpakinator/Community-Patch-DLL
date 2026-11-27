"""regioncmp.py: the regional compare of two height fields around changed plots (ticket 4906, epic 0465 T4).

Given two fields of one map and the changed plots, counts the samples whose u16 heights differ by more than --tol
(the test of `hfbake compare --tol N`: |a - b| > N) and the material bytes that differ, split by region as
live-terrain.md 2.3 counted them: every sample belongs to the plot whose centre is nearest in world space (rows are
0.866 world units apart, columns 1.0; x wraps on a wrapping map); a sample farther than 0.75 plot from every centre
(the 4 margin rows) belongs to no plot. Regions: **inside** (the changed plots), **ring 1 .. ring N** (plots at hex
distance k from the nearest changed plot, each ring apart, N = --rings, default 4) and **outside** (farther plots and
the samples of no plot). Outside differences are listed per plot with their hex distance (--list, default 40).

    python -I render/tests/liveterrain/regioncmp.py A B --centres CENTRES.txt --plots X,Y [X,Y ...]
           [--tol 1] [--rings 4] [--heights-only] [--list 40] [--json OUT]

A and B: a field dump of the EXE (`render/evidence/0465/4901/drive.py field`: .npz with h16, m8 and info) or a C5HF
height-field tap (`render/include/c5r/heightfield/heightfield.h`: the 128-byte header, u16 heights, the byte layer
when flag bit 1 is set; the bundle's `heightfield/load<NNNN>.c5hf`). CENTRES: `lt.py centres --raw` (or 4901's
`centres.lua`): "W H wrap", then "x y wx wy" per plot (Lua HexToWorld).

Input checks, each refused with exit 2 and a line starting "regioncmp: REFUSED": a file missing or of an unknown
form; the two fields of different sample counts or grid geometry (sx, sy, ox, oy); one field with a material layer and
the other without (unless --heights-only); a centres file that is malformed, misses a plot or repeats one, or does not
fit the field (W x 64 samples across, (H + 4) x 64 down, every centre inside the field); no changed plot, a changed
plot outside the map or named twice; --tol below 0; --rings below 1. Exit 0 otherwise (the numbers are the result:
this tool judges nothing).
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

C5HF_HEADER = 128
SAMPLES_PER_PLOT = 64
MARGIN_ROWS = 4
ROW_STEP = 0.866          # world units between sample rows per column unit (sy / sx on every map seen)


class Refused(Exception):
    pass


# ---------------------------------------------------------------------------------------------- fields
def load_field(path: str) -> dict:
    """{h: u16 [Y, X], m: u8 [Y, X] or None, sx, sy, ox, oy, wrap, form}."""
    p = Path(path)
    if not p.is_file():
        raise Refused("field not found: %s" % path)
    head = p.read_bytes()[:4]
    if head == b"C5HF":
        return _load_c5hf(p)
    if head[:2] == b"PK":
        return _load_npz(p)
    raise Refused("unknown field form (neither C5HF nor a drive.py .npz): %s" % path)


def _load_c5hf(p: Path) -> dict:
    raw = p.read_bytes()
    if len(raw) < C5HF_HEADER:
        raise Refused("C5HF truncated (%d bytes): %s" % (len(raw), p))
    ver, mw, mh, sxn, syn, flags = struct.unpack_from("<6I", raw, 4)
    sx, sy, sz, ox, oy = struct.unpack_from("<5f", raw, 28)
    if ver != 1 or sxn != mw * SAMPLES_PER_PLOT or syn != mh * SAMPLES_PER_PLOT:
        raise Refused("unexpected C5HF header (version %d, %d x %d plots, %d x %d samples): %s"
                      % (ver, mw, mh, sxn, syn, p))
    if flags & ~3:
        raise Refused("C5HF unknown flags 0x%X: %s" % (flags, p))
    n = sxn * syn
    has_mat = bool(flags & 2)
    need = C5HF_HEADER + 2 * n + (n if has_mat else 0)
    if len(raw) < need:
        raise Refused("C5HF truncated: %d bytes, %d needed: %s" % (len(raw), need, p))
    h = np.frombuffer(raw, dtype="<u2", count=n, offset=C5HF_HEADER).reshape(syn, sxn)
    m = np.frombuffer(raw, dtype="u1", count=n, offset=C5HF_HEADER + 2 * n).reshape(syn, sxn) if has_mat else None
    return {"h": h, "m": m, "sx": sx, "sy": sy, "ox": ox, "oy": oy, "wrap": bool(flags & 1), "form": "c5hf"}


def _load_npz(p: Path) -> dict:
    try:
        with np.load(str(p)) as z:
            h = z["h16"]
            m = z["m8"] if "m8" in z.files else None
            info = json.loads(str(z["info"]))
    except Exception as e:  # noqa: BLE001 - any unreadable npz is a refusal
        raise Refused("not a drive.py field dump (%s): %s" % (e, p))
    if h.ndim != 2 or h.dtype != np.uint16:
        raise Refused("h16 is not a 2-D u16 array: %s" % p)
    if m is not None and m.shape != h.shape:
        raise Refused("m8 shape %s differs from h16 %s: %s" % (m.shape, h.shape, p))
    try:
        g = {k: float(info["grid_" + k]) for k in ("sx", "sy", "ox", "oy")}
    except (KeyError, TypeError, ValueError):
        raise Refused("info lacks grid_sx/sy/ox/oy: %s" % p)
    return {"h": h, "m": m, "wrap": bool(info.get("wrap_x", True)), "form": "npz", **g}


# ---------------------------------------------------------------------------------------------- centres and plots
def load_centres(path: str) -> tuple[int, int, bool, dict]:
    p = Path(path)
    if not p.is_file():
        raise Refused("centres not found: %s" % path)
    lines = [ln for ln in p.read_text(encoding="utf-8").splitlines() if ln.strip()]
    try:
        W, H, wrap = (int(v) for v in lines[0].split())
    except (IndexError, ValueError):
        raise Refused("centres: the first line is not 'W H wrap': %s" % path)
    if W <= 0 or H <= 0 or wrap not in (0, 1):
        raise Refused("centres: bad header %d %d %d" % (W, H, wrap))
    cen = {}
    for i, ln in enumerate(lines[1:], 2):
        parts = ln.split()
        try:
            x, y, wx, wy = int(parts[0]), int(parts[1]), float(parts[2]), float(parts[3])
            if len(parts) != 4:
                raise ValueError
        except (IndexError, ValueError):
            raise Refused("centres line %d is not 'x y wx wy': %r" % (i, ln))
        if not (0 <= x < W and 0 <= y < H):
            raise Refused("centres line %d: plot (%d,%d) outside %d x %d" % (i, x, y, W, H))
        if (x, y) in cen:
            raise Refused("centres: plot (%d,%d) twice" % (x, y))
        cen[(x, y)] = (wx, wy)
    if len(cen) != W * H:
        raise Refused("centres: %d plots, %d expected" % (len(cen), W * H))
    return W, H, bool(wrap), cen


def parse_plots(items, W, H) -> list:
    out = []
    for s in items or []:
        try:
            x, y = (int(v) for v in s.split(","))
        except ValueError:
            raise Refused("a changed plot is X,Y, not %r" % (s,))
        if not (0 <= x < W and 0 <= y < H):
            raise Refused("changed plot (%d,%d) outside the map %d x %d" % (x, y, W, H))
        if (x, y) in out:
            raise Refused("changed plot (%d,%d) named twice" % (x, y))
        out.append((x, y))
    if not out:
        raise Refused("no changed plot")
    return out


def hexdist(x0, y0, x1, y1, W, wrap) -> int:
    """Hex distance on Civ 5's offset grid (odd rows shifted right: hex x = x - (y >> 1), CvGameCoreUtils)."""
    def cube(x, y):
        q = x - (y - (y & 1)) // 2
        return q, y, -q - y
    best = 1 << 30
    b = cube(x1, y1)
    for dx in ((-W, 0, W) if wrap else (0,)):
        a = cube(x0 + dx, y0)
        best = min(best, max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])))
    return best


def label_samples(shape, cen_samples: dict, W: int, wrap: bool) -> np.ndarray:
    """Plot index (y * W + x) of the nearest centre per sample, or -1 beyond 0.75 plot (48 samples)."""
    Y, X = shape
    lab = np.full(shape, -1, np.int32)
    best = np.full(shape, np.inf, np.float32)
    R = 48
    for (x, y), (c, r) in cen_samples.items():
        rows = np.arange(int(round(r)) - R, int(round(r)) + R)
        rows = rows[(rows >= 0) & (rows < Y)]
        cols = np.arange(int(round(c)) - R, int(round(c)) + R)
        if wrap:
            colsw = cols % X
        else:
            cols = cols[(cols >= 0) & (cols < X)]
            colsw = cols
        if not len(rows) or not len(cols):
            continue
        dc = (cols - c)[None, :]
        dr = ((rows - r) * ROW_STEP)[:, None]
        d = np.sqrt(dc * dc + dr * dr).astype(np.float32)
        ix = np.ix_(rows, colsw)
        sub_best, sub_lab = best[ix], lab[ix]
        upd = d < sub_best
        sub_lab[upd] = y * W + x
        sub_best[upd] = d[upd]
        best[ix], lab[ix] = sub_best, sub_lab
    lab[best > 0.75 * SAMPLES_PER_PLOT] = -1
    return lab


# ---------------------------------------------------------------------------------------------- compare
def compare(a: dict, b: dict, W: int, H: int, wrap: bool, cen: dict, plots: list, tol: int = 1, rings: int = 4,
            heights_only: bool = False, list_n: int = 40) -> dict:
    if tol < 0:
        raise Refused("--tol below 0")
    if rings < 1:
        raise Refused("--rings below 1")
    if a["h"].shape != b["h"].shape:
        raise Refused("sample counts differ: %s and %s" % (a["h"].shape, b["h"].shape))
    for k in ("sx", "sy", "ox", "oy"):
        if abs(a[k] - b[k]) > 1e-4:
            raise Refused("grid %s differs: %r and %r" % (k, a[k], b[k]))
    for f in (a, b):
        if bool(f["wrap"]) != bool(wrap):
            raise Refused("the field's wrap flag (%s) differs from the centres' (%s)" % (bool(f["wrap"]), bool(wrap)))
    if not heights_only and ((a["m"] is None) != (b["m"] is None)):
        raise Refused("one field has a material layer and the other not (--heights-only compares heights alone)")
    Y, X = a["h"].shape
    if X != W * SAMPLES_PER_PLOT or Y != (H + MARGIN_ROWS) * SAMPLES_PER_PLOT:
        raise Refused("the field (%d x %d samples) does not fit the map %d x %d (%d x %d expected)"
                      % (X, Y, W, H, W * SAMPLES_PER_PLOT, (H + MARGIN_ROWS) * SAMPLES_PER_PLOT))
    cen_s = {}
    for xy, (wx, wy) in cen.items():
        c, r = (wx + a["ox"]) / a["sx"], (wy + a["oy"]) / a["sy"]
        if wrap:
            c = c % X        # an odd row's last centre may sit on the seam
        if not (0 <= c < X and 0 <= r < Y):
            raise Refused("the centre of plot (%d,%d) lies outside the field (sample %.1f, %.1f)" % (xy + (c, r)))
        cen_s[xy] = (c, r)

    lab = label_samples(a["h"].shape, cen_s, W, wrap)
    dist = np.full(W * H, 1 << 30, np.int32)
    for y in range(H):
        for x in range(W):
            dist[y * W + x] = min(hexdist(cx, cy, x, y, W, wrap) for cx, cy in plots)
    # region per sample: 0 inside, 1..rings ring k, rings + 1 outside (farther plots and the samples of no plot)
    out_code = rings + 1
    plot_region = np.minimum(dist, out_code)
    region = np.where(lab >= 0, plot_region[np.clip(lab, 0, None)], out_code)

    dh = np.abs(a["h"].astype(np.int32) - b["h"].astype(np.int32))
    bad = dh > tol
    dm = (a["m"] != b["m"]) if (a["m"] is not None and b["m"] is not None) else None
    res = {"tol": tol, "rings": rings, "plots": [list(p) for p in plots], "map": [W, H], "wrap": wrap,
           "samples": int(dh.size), "diff_total": int(bad.sum()), "max_abs": int(dh.max()),
           "material_diff_total": None if dm is None else int(dm.sum()), "regions": {}}
    names = ["inside"] + ["ring%d" % k for k in range(1, rings + 1)] + ["outside"]
    for k, name in enumerate(names):
        sel = region == k
        res["regions"][name] = {
            "plots": int((plot_region == k).sum()),
            "samples": int(sel.sum()), "diff": int((bad & sel).sum()),
            "max_abs": int(dh[sel].max()) if sel.any() else 0,
            "material_diff": None if dm is None else int((dm & sel).sum())}
    o = bad & (region == out_code)
    if o.any() and list_n > 0:
        lo = lab[o]
        uniq, cnt = np.unique(lo, return_counts=True)
        order = np.argsort(-cnt, kind="stable")
        res["outside_plots"] = [
            {"x": int(u % W), "y": int(u // W), "diff": int(c), "hexdist": int(dist[u])} if u >= 0
            else {"x": -1, "y": -1, "diff": int(c), "hexdist": -1}
            for u, c in zip(uniq[order][:list_n], cnt[order][:list_n])]
    return res


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--centres", required=True)
    p.add_argument("--plots", nargs="+", required=True)
    p.add_argument("--tol", type=int, default=1)
    p.add_argument("--rings", type=int, default=4)
    p.add_argument("--heights-only", action="store_true")
    p.add_argument("--list", type=int, default=40)
    p.add_argument("--json")
    a = p.parse_args(argv)
    try:
        fa, fb = load_field(a.a), load_field(a.b)
        W, H, wrap, cen = load_centres(a.centres)
        plots = parse_plots(a.plots, W, H)
        res = compare(fa, fb, W, H, wrap, cen, plots, a.tol, a.rings, a.heights_only, a.list)
    except Refused as e:
        print("regioncmp: REFUSED %s" % e)
        return 2
    res["a"], res["b"], res["centres"] = a.a, a.b, a.centres
    txt = json.dumps(res, indent=1)
    if a.json:
        Path(a.json).parent.mkdir(parents=True, exist_ok=True)
        Path(a.json).write_text(txt, encoding="utf-8")
    print(txt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
