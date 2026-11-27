"""Issue 0182: a replay of a capture's terrain draws (manifest.py, variants exe, nooverlay, nofow) against the frame of
the same launch and camera (the stock picture), on the overlay's pixels, per plot.

    python -I render/tools/terrainreplay/compare.py REPLAY_DIR EXPORT_DIR REFERENCE.png OUT_DIR --map 56x40
           [--water-z 10.1167] [--mask-plot X,Y ...] [--min-plot-px 300]

Masks (all from the replay and the export, none drawn by hand):
  drawn     the replay drew the pixel (id.u32 != none)
  overlay   `exe` (t1 as captured) and `nooverlay` (t1 transparent) differ: the overlay changes the pixel
  land      world z (from the replay's depth and the draws' ViewProj) above --water-z + 0.5: the terrain under the sea
            and lakes is covered by the water pass later in the frame
  fow       `exe` and `nofow` differ: the fog of war changes the pixel
  plot      the plot under the pixel (nearest hex centre, `camera-and-picking.md` 5); a plot is SEEN when under half of
            its drawn pixels are fow pixels, and EDGE when one of its six neighbours is not seen (the fog-of-war clouds
            of an unseen plot reach over its seen neighbours)
  --mask-plot  plots left out by hand (the selected unit's plot: its ring is animated, `overlay-system.md` 5)
Sets reported (each: pixels, median / p95 / max of the largest channel difference, MAD and signed mean per channel,
share equal, within 4, within 16; COMMON 25):
  overlay_land      overlay & land & drawn, every plot
  overlay_clean     overlay_land on seen plots that are not edge plots and not masked
  nonoverlay_clean  the same plots' land pixels outside the overlay (the control: what the replay does without t1)
  per_plot          overlay_land per plot with its class (seen, edge, unseen, masked), and as its "control" the plot's
                    land pixels outside the overlay: where the two distributions agree, t1 adds no difference of its own
Outputs: OUT_DIR/compare.json, diff_overlay_land.png and diff_overlay_clean.png (red = difference x 4 inside the
set, the reference dimmed outside), side_by_side.png. Pictures are game data: OUT_DIR must be under a work folder (D83).
Exit codes: 0 written; 2 usage; 3 an input missing or unreadable; 4 sizes disagree; 5 OUT_DIR not under a work folder.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

NONE = 0xFFFFFFFF
SY = 0.866
WORK_ROOTS = ("c:/c5-64bit-work/", "d:/c5-64bit-work/", "f:/c5-64bit-work/")


def stats(a: np.ndarray, b: np.ndarray, m: np.ndarray) -> dict:
    """a, b: H x W x 3 uint8; m: H x W bool. Per pixel the largest channel difference."""
    n = int(m.sum())
    if n == 0:
        return {"pixels": 0}
    d = a[..., :3].astype(np.int16)[m] - b[..., :3].astype(np.int16)[m]
    ad = np.abs(d)
    v = ad.max(axis=1)
    return {"pixels": n, "median": float(np.median(v)), "p95": float(np.percentile(v, 95)), "max": int(v.max()),
            "mad_rgb": [round(float(ad[:, c].mean()), 2) for c in range(3)],
            "signed_rgb": [round(float(d[:, c].mean()), 2) for c in range(3)],
            "equal": round(float((v == 0).mean()), 4), "within_4": round(float((v <= 4).mean()), 4),
            "within_16": round(float((v <= 16).mean()), 4)}


def fmt(s: dict) -> str:
    if not s.get("pixels"):
        return "no pixels"
    return "%s px; median %g / p95 %g / max %d; equal %.1f %%, within 4 %.1f %%, within 16 %.1f %%; MAD %s; signed %s" % (
        "{:,}".format(s["pixels"]), s["median"], s["p95"], s["max"], 100 * s["equal"], 100 * s["within_4"],
        100 * s["within_16"], "/".join("%.1f" % x for x in s["mad_rgb"]), "/".join("%+.1f" % x for x in s["signed_rgb"]))


def diff_image(a: np.ndarray, b: np.ndarray, m: np.ndarray, scale: int = 4) -> np.ndarray:
    """Red = largest channel difference x scale inside m; outside m the reference dimmed to a quarter, grey."""
    mx = np.abs(a[..., :3].astype(np.int16) - b[..., :3].astype(np.int16)).max(axis=2)
    out = np.zeros(a.shape[:2] + (3,), np.uint8)
    g = (b[..., :3].astype(np.uint16).mean(axis=2) / 4).astype(np.uint8)
    out[..., 0] = np.where(m, np.clip(mx * scale, 0, 255), g)
    out[..., 1] = np.where(m, 0, g)
    out[..., 2] = np.where(m, 0, g)
    return out


def viewproj(export: Path) -> np.ndarray:
    """TerrainFrame.ViewProj (DS cb1, first 64 bytes) of the first draw that has it."""
    idx = json.loads((export / "draws.json").read_text())
    for d in idx["draws"]:
        st = json.loads((export / d["dir"] / "state.json").read_text())
        cb = st["DS"]["cb"].get("1") if st.get("DS") and st["DS"]["shader"] else None
        if cb and (cb.get("data") or {}).get("file"):
            b = (export / cb["data"]["file"]).read_bytes()
            return np.frombuffer(b[:64], np.float32).reshape(4, 4).astype(np.float64)
    raise ValueError("no TerrainFrame (DS cb1) in the export")


def world_from_depth(depth: np.ndarray, vp: np.ndarray) -> np.ndarray:
    H, W = depth.shape
    xs = (np.arange(W) + 0.5) / W * 2 - 1
    ys = 1 - (np.arange(H) + 0.5) / H * 2
    X, Y = np.meshgrid(xs, ys)
    p = np.stack([X, Y, depth.astype(np.float64), np.ones_like(X)], -1) @ np.linalg.inv(vp)
    w = p[..., :3] / p[..., 3:4]
    w[depth >= 1.0] = np.nan
    return w


def plots_from_world(world: np.ndarray, mw: int, mh: int) -> np.ndarray:
    """Plot index (y * mw + x) per pixel, -1 where nothing is drawn or off the map. HexToWorld of
    `camera-and-picking.md` 5 and 9 with the map centred on the world origin (render/evidence/3019/clean_mask.py)."""
    ox, oy = 64.0 * mw / 2.0, SY * 64.0 * mh / 2.0
    x, y = world[..., 0], world[..., 1]
    ok = np.isfinite(x) & np.isfinite(y)
    x, y = np.where(ok, x, 0), np.where(ok, y, 0)
    gyf = (y + oy) / SY / 64.0 - 2.0
    best_d = np.full(x.shape, np.inf)
    best = np.full(x.shape, -1, np.int64)
    for dgy in (-1, 0, 1):
        gy = np.round(gyf).astype(np.int64) + dgy
        cy = SY * (64.0 * gy + 128.0) - oy
        gxf = (x + ox - 32.0 - 32.0 * (gy & 1)) / 64.0
        for dgx in (0, 1):
            gx = np.floor(gxf).astype(np.int64) + dgx
            cx = 64.0 * gx + 32.0 * (gy & 1) + 32.0 - ox
            d = (x - cx) ** 2 + (y - cy) ** 2
            take = (gy >= 0) & (gy < mh) & (d < best_d)
            best_d = np.where(take, d, best_d)
            best = np.where(take, gy * mw + (gx % mw), best)
    return np.where(ok, best, -1)


def neighbours(p: int, mw: int, mh: int):
    x, y = p % mw, p // mw
    odd = y & 1
    for dxe, dxo, dy in ((-1, -1, 0), (1, 1, 0), (-1, 0, 1), (0, 1, 1), (-1, 0, -1), (0, 1, -1)):
        nx, ny = (x + (dxo if odd else dxe)) % mw, y + dy
        if 0 <= ny < mh:
            yield ny * mw + nx


def load_rgba(path: Path, H: int, W: int) -> np.ndarray:
    b = np.fromfile(path, np.uint8)
    if b.size != H * W * 4:
        raise ValueError("%s has %d bytes, expected %d (%d x %d RGBA8)" % (path, b.size, H * W * 4, W, H))
    return b.reshape(H, W, 4)[..., :3]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("replay")
    ap.add_argument("export")
    ap.add_argument("reference")
    ap.add_argument("out")
    ap.add_argument("--map", required=True, help="plots WxH, e.g. 56x40 (Tiny)")
    ap.add_argument("--water-z", type=float, default=10.1167)
    ap.add_argument("--mask-plot", action="append", default=[], help="X,Y of a plot to leave out (repeatable)")
    ap.add_argument("--min-plot-px", type=int, default=300)
    a = ap.parse_args(argv)
    from PIL import Image
    try:
        mw, mh = (int(v) for v in a.map.lower().split("x"))
        masked = {int(s.split(",")[1]) * mw + int(s.split(",")[0]) for s in a.mask_plot}
    except ValueError:
        print("compare.py: --map WxH and --mask-plot X,Y take integers", file=sys.stderr)
        return 2
    out = Path(a.out)
    if not any((out.resolve().as_posix().lower() + "/").startswith(r) for r in WORK_ROOTS):
        print("compare.py: OUT_DIR %s is not under a work folder (game data, D83)" % out, file=sys.stderr)
        return 5
    rdir, export = Path(a.replay), Path(a.export)
    try:
        ref = np.asarray(Image.open(a.reference).convert("RGB"))
        H, W = ref.shape[:2]
        info = json.loads((rdir / "replay.json").read_text())
        if list(info["size"]) != [W, H]:
            print("compare.py: replay is %s, reference %d x %d" % (info["size"], W, H), file=sys.stderr)
            return 4
        exe = load_rgba(rdir / "exe.rgba", H, W)
        nov = load_rgba(rdir / "nooverlay.rgba", H, W)
        nofow = load_rgba(rdir / "nofow.rgba", H, W)
        ids = np.fromfile(rdir / "id.u32", np.uint32)
        depth = np.fromfile(rdir / "depth.f32", np.float32)
        if ids.size != H * W or depth.size != H * W:
            print("compare.py: id.u32 / depth.f32 do not hold %d x %d values" % (W, H), file=sys.stderr)
            return 4
        vp = np.array(info["viewproj"], np.float64) if info.get("viewproj") else viewproj(export)
    except (OSError, ValueError, KeyError) as e:
        print("compare.py: input missing or unreadable: %s" % e, file=sys.stderr)
        return 3
    ids, depth = ids.reshape(H, W), depth.reshape(H, W)
    drawn = ids != NONE
    world = world_from_depth(depth, vp)
    land = drawn & np.isfinite(world[..., 2]) & (world[..., 2] > a.water_z + 0.5)
    overlay = np.abs(exe.astype(np.int16) - nov.astype(np.int16)).max(2) > 0
    fow = np.abs(exe.astype(np.int16) - nofow.astype(np.int16)).max(2) > 0
    plot = plots_from_world(world, mw, mh)
    plot[~drawn] = -1
    seen = {}
    for p in np.unique(plot[plot >= 0]):
        pm = plot == p
        seen[int(p)] = float(fow[pm].mean()) < 0.5
    cls = {}
    for p, s in seen.items():
        if p in masked:
            cls[p] = "masked"
        elif not s:
            cls[p] = "unseen"
        elif any(not seen.get(n, False) for n in neighbours(p, mw, mh)):
            cls[p] = "edge"
        else:
            cls[p] = "seen"
    clean_plots = np.array([p for p, c in cls.items() if c == "seen"], np.int64)
    on_clean = np.isin(plot, clean_plots)
    sets = {"overlay_land": overlay & land, "overlay_clean": overlay & land & on_clean,
            "nonoverlay_clean": ~overlay & land & on_clean}
    res = {"replay": str(rdir), "export": str(export), "reference": str(a.reference), "size": [W, H], "map": [mw, mh],
           "water_z": a.water_z, "masked_plots": sorted("%d,%d" % (p % mw, p // mw) for p in masked),
           "pixels": {"drawn": int(drawn.sum()), "land": int(land.sum()), "overlay": int((overlay & drawn).sum()),
                      "fow": int((fow & drawn).sum())},
           "sets": {}, "per_plot": []}
    for k, m in sets.items():
        res["sets"][k] = stats(exe, ref, m)
        print("%-17s %s" % (k, fmt(res["sets"][k])))
    m = sets["overlay_land"]
    for p in sorted(cls):
        pm = m & (plot == p)
        if pm.sum() < a.min_plot_px:
            continue
        s = stats(exe, ref, pm)
        s.update({"plot": [p % mw, p // mw], "class": cls[p], "control": stats(exe, ref, ~overlay & land & (plot == p))})
        res["per_plot"].append(s)
    res["plots_by_class"] = {c: sorted("%d,%d" % (p % mw, p // mw) for p, v in cls.items() if v == c)
                             for c in ("seen", "edge", "unseen", "masked")}
    out.mkdir(parents=True, exist_ok=True)
    (out / "compare.json").write_text(json.dumps(res, indent=1))
    for k in ("overlay_land", "overlay_clean"):
        Image.fromarray(diff_image(exe, ref, sets[k])).save(out / ("diff_%s.png" % k))
    side = np.concatenate([ref, exe], 0)
    Image.fromarray(side).resize((W // 2, H)).save(out / "side_by_side.png")
    for r in res["per_plot"]:
        print("plot (%2d,%2d) %-6s overlay %s\n                    control %s" % (r["plot"][0], r["plot"][1], r["class"], fmt(r),
                                                                         fmt(r["control"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
