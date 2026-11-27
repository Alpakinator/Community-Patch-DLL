"""Offline geometry of c5r::terrain against the EXE's stock shots (epic 3200, issue 3202).

For each ground-truth view: terrainshot draws the ground-truth height field at the shot's camera (normal shading,
world position per pixel); this script measures how far the drawn relief sits from the relief in the stock shot.

Method ("shift_match per block"): the method of shell/tools/shift_match.py (FFT cross-correlation of two binary-free
images and the shift of the correlation peak), applied per block of 48 x 48 px on the comparison mask, to band-passed
luminance (Gaussian sigma 1.5 minus sigma 6, both images normalised per block). Each block whose correlation peak is
clear (normalised peak >= --min-peak and above the second best outside a 2-px radius by --min-margin) gives one shift
(dx, dy) with a sub-pixel parabola fit; the result is the distribution of |shift| over those blocks, weighted by
their mask pixels: median, 95th percentile, share within 1 and 3 px. A self-check shifts the drawn image by a known
(3, -2) (so d = (-3, 2) in its convention) and must recover it (|error| <= 0.5 px median), so the method is shown to see a 3-px error.

Comparison mask: terrain pixels of the drawn image (w = 1) above the water height + 0.3 (no water, no coast slope
under water), stock pixel not dark (luminance > 12: unrevealed is black), minus the plots of capture.json's plot list
that carry a route, an improvement, a resource or a city, dilated by 16 px (their models), and minus a 16-px frame.
Plots outside capture.json's list are not classified offline (forests and units there stay in the mask; the block
statistics are robust to them, and the live comparison of issue 3206 uses the D33 passthrough shot instead).

    python render/tools/terrainshot/compare_shots.py --shot S.png --render R.png --world R.f32 --gt-dir DIR
        --camera CAM.json [--out res.json] [--evidence-dir DIR] [--name NAME]

Colour statistics (mean absolute difference per channel on the mask, share within 16, a 16-bin histogram of the
difference) are reported, not judged: offline has no colour caches.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

BLOCK = 48
MAXS = 12


def load_world(path):
    meta = json.loads(Path(str(path) + ".json").read_text())
    a = np.fromfile(str(path), dtype="<f4")
    return a.reshape(meta["height"], meta["width"], 4)


def lum(rgb):
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def bandpass(x):
    return ndimage.gaussian_filter(x, 1.5) - ndimage.gaussian_filter(x, 6.0)


def plot_of(x, y, gc):
    """Nearest plot centre (grid x, grid y) of world (x, y): hex rows 64 * 0.866 apart, odd rows shifted +32."""
    ox, oy, sy = gc["grid_ox"], gc["grid_oy"], gc["grid_sy"]
    wy = (y + oy) / sy
    gy0 = np.rint(wy / 64.0 - 2.0).astype(int)
    best = None
    for dgy in (-1, 0, 1):
        gy = gy0 + dgy
        odd = gy & 1
        gx = np.rint((x + ox - 32.0 - 32.0 * odd) / 64.0).astype(int)
        for dgx in (-1, 0, 1):
            cx = 64.0 * (gx + dgx) + 32.0 * odd + 32.0 - ox
            cy = (64.0 * gy + 128.0) * sy - oy
            d = (x - cx) ** 2 + (y - cy) ** 2
            if best is None:
                best = [d, gx + dgx, gy]
            else:
                m = d < best[0]
                best[0] = np.where(m, d, best[0])
                best[1] = np.where(m, gx + dgx, best[1])
                best[2] = np.where(m, gy, best[2])
    return best[1], best[2]


# The EXE's HUD at 2554 x 1411 (stock UI with EUI, read from shot_v1 of the huge map, 2026-11-11): rectangles x0, y0,
# x1, y1 in back-buffer pixels, scaled for other sizes. Top bar; advisor buttons; left city column; right leader column;
# unit panel; minimap and production buttons.
HUD = [(0, 0, 2554, 40), (2140, 0, 2554, 85), (0, 0, 160, 1411), (2420, 0, 2554, 1411), (0, 800, 680, 1411),
       (2240, 1110, 2554, 1411)]


def hud_mask(h, w):
    m = np.zeros((h, w), bool)
    sx, sy = w / 2554.0, h / 1411.0
    for x0, y0, x1, y1 in HUD:
        m[int(y0 * sy):int(np.ceil(y1 * sy)), int(x0 * sx):int(np.ceil(x1 * sx))] = True
    return m


def model_plots(capture):
    out = set()
    for p in capture.get("plots", "").split(";"):
        f = p.split(",")
        if len(f) < 7:
            continue
        x, y, route, imp, res = int(f[0]), int(f[1]), f[2], f[3], f[4]
        city = f[6]
        if route != "-" or imp != "-" or res != "-" or city:
            out.add((x, y))
    return out


def block_shift(t, m, window, min_peak, min_margin):
    """Normalised cross-correlation of the masked template t (weights m) over window (template + MAXS on each side),
    computed with FFTs (scipy.signal.correlate, valid mode) as in shift_match.py, but normalised over the overlap so that
    the estimate is not pulled toward zero. Returns (dx, dy, peak) with t(x) ~ window(x + MAXS + d), or None if the peak
    is unclear."""
    from scipy.signal import correlate
    sw = m.sum()
    tz = (t - (t * m).sum() / sw) * m
    tn = np.sqrt((tz * tz).sum())
    if tn < 1e-6:
        return None
    num = correlate(window, tz, mode="valid", method="fft")
    s1 = correlate(window, m, mode="valid", method="fft")
    s2 = correlate(window * window, m, mode="valid", method="fft")
    var = np.maximum(s2 - s1 * s1 / sw, 1e-9)
    win = num / (tn * np.sqrt(var))
    iy, ix = np.unravel_index(np.argmax(win), win.shape)
    peak = win[iy, ix]
    yy, xx = np.mgrid[0:win.shape[0], 0:win.shape[1]]
    far = (yy - iy) ** 2 + (xx - ix) ** 2 > 4
    second = win[far].max()
    if peak < min_peak or peak - second < min_margin:
        return None
    if ix in (0, win.shape[1] - 1) or iy in (0, win.shape[0] - 1):
        return None

    def sub(m1, c0, p1):
        d = m1 - 2 * c0 + p1
        return 0.0 if d == 0 else 0.5 * (m1 - p1) / d

    fx = sub(win[iy, ix - 1], peak, win[iy, ix + 1])
    fy = sub(win[iy - 1, ix], peak, win[iy + 1, ix])
    return ix - MAXS + fx, iy - MAXS + fy, float(peak)


def measure(A, B, mask, min_peak, min_margin):
    h, w = mask.shape
    rows = []
    for y0 in range(MAXS, h - BLOCK - MAXS + 1, BLOCK):
        for x0 in range(MAXS, w - BLOCK - MAXS + 1, BLOCK):
            m = mask[y0:y0 + BLOCK, x0:x0 + BLOCK]
            if m.mean() < 0.6:
                continue
            r = block_shift(A[y0:y0 + BLOCK, x0:x0 + BLOCK], m, B[y0 - MAXS:y0 + BLOCK + MAXS, x0 - MAXS:x0 + BLOCK + MAXS],
                            min_peak, min_margin)
            if r is None:
                rows.append({"x": x0, "y": y0, "used": False, "mask_px": int(m.sum())})
                continue
            rows.append({"x": x0, "y": y0, "used": True, "dx": r[0], "dy": r[1], "peak": r[2], "mask_px": int(m.sum())})
    used = [r for r in rows if r["used"]]
    if not used:
        return {"blocks": len(rows), "blocks_used": 0}, rows
    mag = np.array([np.hypot(r["dx"], r["dy"]) for r in used])
    wts = np.array([r["mask_px"] for r in used], dtype=float)
    order = np.argsort(mag)
    cw = np.cumsum(wts[order]) / wts.sum()

    def wq(q):
        return float(mag[order][np.searchsorted(cw, q)])

    return {
        "blocks": len(rows), "blocks_used": len(used),
        "median_px": wq(0.5), "p95_px": wq(0.95),
        "within_1px": float(wts[mag <= 1].sum() / wts.sum()), "within_3px": float(wts[mag <= 3].sum() / wts.sum()),
        "mean_dx": float(np.average([r["dx"] for r in used], weights=wts)),
        "mean_dy": float(np.average([r["dy"] for r in used], weights=wts)),
        "mask_px_used": int(wts.sum()),
    }, rows


def down(img, maxw=512):
    im = Image.fromarray(img)
    if im.width > maxw:
        im = im.resize((maxw, round(im.height * maxw / im.width)), Image.LANCZOS)
    return im


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", required=True)
    ap.add_argument("--render", required=True)
    ap.add_argument("--world", required=True)
    ap.add_argument("--gt-dir", required=True)
    ap.add_argument("--camera", required=True)
    ap.add_argument("--out")
    ap.add_argument("--evidence-dir")
    ap.add_argument("--name", default="shot")
    ap.add_argument("--min-peak", type=float, default=0.25)
    ap.add_argument("--min-margin", type=float, default=0.05)
    a = ap.parse_args(argv)
    S = np.asarray(Image.open(a.shot).convert("RGB")).astype(float)
    R = np.asarray(Image.open(a.render).convert("RGB")).astype(float)
    W = load_world(a.world)
    if S.shape != R.shape or W.shape[:2] != S.shape[:2]:
        sys.exit(f"compare_shots: sizes differ: shot {S.shape}, render {R.shape}, world {W.shape}")
    gd = Path(a.gt_dir)
    gc = json.loads((gd / "grid_constants.json").read_text())
    capture = json.loads((gd / "capture.json").read_text())
    import struct
    water = struct.unpack("<f", bytes.fromhex(gc["heightmap_header_hex"][8:16]))[0]
    terrain = (W[..., 3] == 1.0) & (W[..., 2] > water + 0.3)
    dark = lum(S) <= 12
    gx, gy = plot_of(W[..., 0].astype(float), W[..., 1].astype(float), gc)
    mp = model_plots(capture)
    models = np.zeros(terrain.shape, bool)
    for (px, py) in mp:
        models |= (gx == px) & (gy == py) & (W[..., 3] > 0)
    models = ndimage.binary_dilation(models, iterations=16)
    hud = hud_mask(*terrain.shape)
    mask = terrain & ~dark & ~models & ~hud
    mask[:16, :] = mask[-16:, :] = False
    mask[:, :16] = mask[:, -16:] = False
    A = bandpass(lum(S))
    B = bandpass(lum(R))
    stats, rows = measure(A, B, mask.astype(float), a.min_peak, a.min_margin)
    # coastline: the drawn water (w = 2) against the stock shot's blue pixels, one global shift by the FFT
    # cross-correlation of shift_match.py on the two binary masks outside the HUD (rivers are blue in the shot but not
    # water in the drawing: they lower the peak, not move it)
    blue = (S[..., 2] > S[..., 0] + 15) & (S[..., 2] > S[..., 1] - 5) & ~hud
    wat = (W[..., 3] == 2.0) & ~hud
    coast = {"stock_blue_px": int(blue.sum()), "drawn_water_px": int(wat.sum())}
    if blue.sum() > 2000 and wat.sum() > 2000:
        h, w = blue.shape
        mx = 40
        s = (h + mx, w + mx)
        corr = np.fft.irfft2(np.fft.rfft2(blue.astype(float), s=s) * np.conj(np.fft.rfft2(wat.astype(float), s=s)), s=s)
        best = max(((corr[dy % s[0], dx % s[1]], dx, dy) for dy in range(-mx, mx + 1) for dx in range(-mx, mx + 1)))
        inter = (blue & wat).sum()
        coast.update(shift=[int(best[1]), int(best[2])], iou_at_zero=float(inter / max(1, (blue | wat).sum())))
    # self-check: the drawn image shifted by a known (3, -2) against itself
    Bs = np.roll(np.roll(B, -2, axis=0), 3, axis=1)
    ms = np.roll(np.roll(mask, -2, axis=0), 3, axis=1) & mask
    chk, chk_rows = measure(Bs, B, ms.astype(float), a.min_peak, a.min_margin)
    errs = [np.hypot(r["dx"] + 3, r["dy"] - 2) for r in chk_rows if r["used"]]  # Bs(x) = B(x + (-3, 2))
    selfcheck = {"want": [-3, 2], "blocks_used": len(errs), "median_error_px": float(np.median(errs)) if errs else None,
                 "mean_dx": chk.get("mean_dx"), "mean_dy": chk.get("mean_dy")}
    # colour statistics on the mask (reported, not judged)
    d = np.abs(S - R)[mask]
    colour = {"mean_abs": [float(v) for v in d.mean(axis=0)] if d.size else None,
              "within_16": float((d.max(axis=1) <= 16).mean()) if d.size else None,
              "hist16": np.histogram(d.max(axis=1), bins=16, range=(0, 256))[0].tolist() if d.size else None}
    res = {"name": a.name, "shot": a.shot, "render": a.render, "camera": a.camera,
           "size": [int(S.shape[1]), int(S.shape[0])], "water_z": water,
           "mask_px": int(mask.sum()), "terrain_px": int(terrain.sum()), "model_plots": sorted(mp),
           "method": "shift_match.py FFT cross-correlation per 48-px block on band-passed luminance (sigma 1.5 - 6)",
           "geometry": stats, "coast": coast, "selfcheck": selfcheck, "colour": colour,
           "pass": bool(stats.get("blocks_used") and stats["median_px"] <= 1.0 and stats["p95_px"] <= 3.0)}
    if a.evidence_dir:
        ev = Path(a.evidence_dir)
        ev.mkdir(parents=True, exist_ok=True)
        mimg = (np.stack([mask, terrain & ~mask, models], axis=2) * 255).astype(np.uint8)
        down(mimg).save(ev / f"{a.name}_mask_512.png")
        side = np.concatenate([S.astype(np.uint8), R.astype(np.uint8)], axis=1)
        down(side, 512).save(ev / f"{a.name}_stock_vs_drawn_512.png")
        res["evidence"] = [str(ev / f"{a.name}_mask_512.png"), str(ev / f"{a.name}_stock_vs_drawn_512.png")]
    txt = json.dumps(res, indent=1)
    if a.out:
        Path(a.out).write_text(txt)
        Path(str(a.out) + ".blocks.json").write_text(json.dumps(rows))
    print(json.dumps({k: res[k] for k in ("name", "mask_px", "geometry", "selfcheck", "pass")}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
