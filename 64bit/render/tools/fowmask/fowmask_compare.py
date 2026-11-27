"""Compare fowmask's mask (OUT.rgba) with stock's FOW Mask of a capture (task 3952, epic 0168).

    python -I render/tools/fowmask/fowmask_compare.py OURS.rgba EXPORT_DIR CENSUS.json [--json OUT.json] [--png DIFF.png]

Stock's mask: mip 0 of the terrain draw's t4 in fowmask_export.py's export (the frame-start contents = the previous
frame's finished mask; P8: equal to this frame's at rest, fog-of-war.md 2.6). Per channel: the share of texels equal,
the distribution of |difference| (0, 1, 2-4, 5-16, 17+), the largest; then the same per plot class of the texel's
nearest plot centre (visible, seen, unexplored, outside the map) and for texels where stock drew nothing (A = 0) or
something. --png: the difference picture (the largest channel difference x 8, red where stock's A and ours differ).
Tiny placement for the class labels (x = 64 X + 32 (Y odd) + 32 - 32 W, y = 55.4256 Y - 27.7128 H; labels only).
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np

S3 = 0.8660254


def stock_mask(export):
    exp = Path(export)
    for d in json.loads((exp / "draws.json").read_text())["draws"]:
        if d["system"].startswith("terrain"):
            s = json.loads((exp / d["dir"] / "state.json").read_text())
            t = s["PS"]["srv"]["4"]["data"]
            raw = (exp / t["file"]).read_bytes()
            n = t["desc"]["Width"]
            cb = s["VS"]["cb"].get("1") or s["PS"]["cb"].get("1")
            op = struct.unpack_from("<16f", (exp / cb["data"]["file"]).read_bytes(), 0x40)
            return np.frombuffer(raw[:n * n * 4], dtype=np.uint8).reshape(n, n, 4), op
    raise SystemExit("no terrain draw in " + export)


def labels(n, op, census):
    c = json.loads(Path(census).read_text())
    W, H = c["w"], c["h"]
    F = {k: i for i, k in enumerate(c["fields"])}
    cls = np.zeros((H, W), dtype=np.int8)
    for i, p in enumerate(c["plots"]):
        cls[i // W, i % W] = 2 if not p[F["revealed"]] else (0 if p[F["visible"]] else 1)
    u = (np.arange(n) + 0.5) / n
    ndx = 2 * u - 1
    ndy = 1 - 2 * u
    x = (ndx - op[12]) / op[0]
    y = (ndy - op[13]) / op[5]
    X, Y = np.meshgrid(x, y)
    Yf = (Y + H * 32 * S3) / (64 * S3)
    best = np.full(X.shape, 1e9)
    bx = np.zeros(X.shape, dtype=np.int64)
    by = np.zeros(X.shape, dtype=np.int64)
    for dr in (0, 1):
        r = np.floor(Yf).astype(np.int64) + dr
        Xf = (X - 32 - 32 * (r & 1) + 32 * W) / 64
        for dc in (0, 1):
            cx = np.floor(Xf).astype(np.int64) + dc
            px = 64 * cx + 32 * (r & 1) + 32 - 32 * W
            py = r * 64 * S3 - H * 32 * S3
            d = (X - px) ** 2 + (Y - py) ** 2
            m = d < best
            best[m] = d[m]
            bx[m] = cx[m]
            by[m] = r[m]
    lab = np.full(X.shape, 3, dtype=np.int8)   # 3 = outside the map
    inside = (by >= 0) & (by < H)
    lab[inside] = cls[by[inside], np.mod(bx[inside], W)]
    return lab


def dist(d):
    tot = d.size
    if tot == 0:
        return {"n": 0}
    return {"n": int(tot), "equal": round(float((d == 0).mean()), 6), "d1": int((d == 1).sum()),
            "d2_4": int(((d >= 2) & (d <= 4)).sum()), "d5_16": int(((d >= 5) & (d <= 16)).sum()),
            "d17plus": int((d >= 17).sum()), "max": int(d.max())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("export")
    ap.add_argument("census")
    ap.add_argument("--json")
    ap.add_argument("--png")
    a = ap.parse_args()
    ref, op = stock_mask(a.export)
    n = ref.shape[0]
    ours = np.fromfile(a.ours, dtype=np.uint8).reshape(n, n, 4)
    diff = np.abs(ours.astype(np.int16) - ref.astype(np.int16))
    lab = labels(n, op, a.census)
    names = ["visible", "seen", "unexplored", "outside"]
    out = {"size": n, "all_equal_texels": round(float((diff.max(axis=2) == 0).mean()), 6), "channels": {}, "classes": {},
           "stock_drawn": {}}
    for ch, nm in enumerate("RGBA"):
        out["channels"][nm] = dist(diff[..., ch])
    for k, nm in enumerate(names):
        m = lab == k
        out["classes"][nm] = {c: dist(diff[..., ch][m]) for ch, c in enumerate("RGBA")}
    for k, nm in ((0, "stock A = 0"), (1, "stock A > 0")):
        m = (ref[..., 3] > 0) == bool(k)
        out["stock_drawn"][nm] = {c: dist(diff[..., ch][m]) for ch, c in enumerate("RGBA")}
    print(json.dumps(out, indent=1))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1))
    if a.png:
        try:
            from PIL import Image
        except ImportError:
            print("no PIL: --png skipped")
            return
        mx = np.clip(diff[..., :3].max(axis=2).astype(np.int32) * 8, 0, 255).astype(np.uint8)
        pic = np.stack([mx, mx, mx], axis=2)
        am = (ours[..., 3] > 0) != (ref[..., 3] > 0)
        pic[am] = (255, 0, 0)
        Image.fromarray(pic).save(a.png)


if __name__ == "__main__":
    main()
