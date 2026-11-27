"""Pixel alignment of the drawn terrain surface against the EXE's own hex highlights (epic 3200, issue 3202).

The method of shell/tools/grid_accuracy.py, turned around for a surface: epic 3000's ground truth holds, at one camera per
map (camera_validate.json = the last view's camera), a stock shot with five plots outlined in magenta by the EXE
(Events.SerialEventHexHighlight; validate_highlight.png) and the same shot without (validate_nohighlight.png). The EXE
draws that outline at about 0.925 of the hex corner radius on the terrain (validation.json: median 0.53 px against the
CPU heights). For every magenta pixel p that the drawing covers with terrain, terrainshot's world image gives W(p), the
point where the ray through p meets the DRAWN surface. If the drawn surface is the EXE's, W(p) lies on the outline:
the error of p is the screen distance between p and the projection of the outline point nearest to W(p) (at W(p)'s
height). Reported: median, 95th percentile, shares within 1 and 3 px, per plot and over all.

    python render/tools/terrainshot/highlight_accuracy.py --gt-dir DIR --world WORLD.f32 [--scale 0.925] [--out res.json]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

AXIAL = [(1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1), (1, -1)]


def vec4_mat(v, m):
    return np.array([sum(v[k] * m[k][j] for k in range(4)) for j in range(4)])


def hex_to_world_xy(gc, hx, hy):
    w = vec4_mat([hx, hy, 0.0, 1.0], gc["M_h2w"])
    return np.array([w[0] * gc["grid_sx"] - gc["grid_ox"], w[1] * gc["grid_sy"] - gc["grid_oy"]])


def project(cam, p):
    """gt_reader.project: D3DXVec3Project with an identity world; p is (N, 3)."""
    V, P = np.array(cam["views"][0]), np.array(cam["proj"])
    h = np.concatenate([p, np.ones((len(p), 1))], axis=1) @ V @ P
    vp = cam["viewport"]
    x = vp["X"] + (1 + h[:, 0] / h[:, 3]) * vp["Width"] / 2.0
    y = vp["Y"] + (1 - h[:, 1] / h[:, 3]) * vp["Height"] / 2.0
    return np.stack([x, y], axis=1)


def outline(gc, gx, gy, scale):
    hx, hy = gx - (gy >> 1), gy
    ctr = hex_to_world_xy(gc, hx, hy)
    nb = [hex_to_world_xy(gc, hx + dx, hy + dy) for dx, dy in AXIAL]
    return [ctr + scale * ((ctr + nb[i] + nb[(i + 1) % 6]) / 3.0 - ctr) for i in range(6)]


def nearest_on_outline(q, corners):
    """Nearest point of the closed polygon `corners` to each row of q (N, 2), its distance, and its position along the
    outline (edge index + t, 0..6)."""
    best_d, best_p, best_s = None, None, None
    for i in range(6):
        a, b = corners[i], corners[(i + 1) % 6]
        ab = b - a
        t = np.clip(((q - a) @ ab) / (ab @ ab), 0, 1)
        p = a + t[:, None] * ab
        d = np.hypot(*(q - p).T)
        if best_d is None:
            best_d, best_p, best_s = d, p, i + t
        else:
            m = d < best_d
            best_d = np.where(m, d, best_d)
            best_p = np.where(m[:, None], p, best_p)
            best_s = np.where(m, i + t, best_s)
    return best_d, best_p, best_s


def stats(e):
    return {"pixels": int(len(e)), "median_px": float(np.median(e)), "p95_px": float(np.percentile(e, 95)),
            "within_1px": float((e <= 1).mean()), "within_3px": float((e <= 3).mean())} if len(e) else {"pixels": 0}


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--gt-dir", required=True)
    ap.add_argument("--world", required=True, help="terrainshot --world output at camera_validate.json")
    ap.add_argument("--scale", type=float, default=0.925)
    ap.add_argument("--out")
    ap.add_argument("--debug-png", help="the highlight shot with the band cross-sections coloured by their error")
    ap.add_argument("--with-corners", dest="no_corners", action="store_false",
                    help="keep the cross-sections at the hex corners (by default dropped: the band is mitred there)")
    a = ap.parse_args(argv)
    gd = Path(a.gt_dir)
    gc = json.loads((gd / "grid_constants.json").read_text())
    cam = json.loads((gd / "camera_validate.json").read_text())
    plots = json.loads((gd / "capture.json").read_text())["validate"]["points"]
    plots = sorted({tuple(p) for p in plots})
    B = np.asarray(Image.open(gd / "validate_highlight.png").convert("RGB")).astype(int)
    C = np.asarray(Image.open(gd / "validate_nohighlight.png").convert("RGB")).astype(int)
    mag = (np.abs(B - C).sum(axis=2) > 60) & (B[..., 0] > 150) & (B[..., 2] > 150) & (B[..., 1] < 120)
    meta = json.loads(Path(a.world + ".json").read_text())
    W = np.fromfile(a.world, dtype="<f4").reshape(meta["height"], meta["width"], 4)
    if W.shape[:2] != mag.shape:
        sys.exit(f"highlight_accuracy: world {W.shape[:2]} and shots {mag.shape} differ")
    ys, xs = np.nonzero(mag & (W[..., 3] == 1.0))
    q = W[ys, xs, :3].astype(float)
    pix = np.stack([xs, ys], axis=1) + 0.5          # pixel centres in D3DXVec3Project coordinates

    def signed_errors(scale):
        best_d = best_p = best_k = best_s = best_in = None
        for k, (gx, gy) in enumerate(plots):
            corners = outline(gc, gx, gy, scale)
            d, p, s = nearest_on_outline(q[:, :2], corners)
            ctr = np.mean(corners, axis=0)
            inside = np.hypot(*(q[:, :2] - ctr).T) < np.hypot(*(p - ctr).T)
            if best_d is None:
                best_d, best_p, best_k, best_s, best_in = d, p, np.zeros(len(d), int), s, inside
            else:
                m = d < best_d
                best_d, best_p = np.where(m, d, best_d), np.where(m[:, None], p, best_p)
                best_k, best_s, best_in = np.where(m, k, best_k), np.where(m, s, best_s), np.where(m, inside, best_in)
        proj = project(cam, np.concatenate([best_p, q[:, 2:3]], axis=1))
        err = np.hypot(*(proj - pix).T) * np.where(best_in, -1.0, 1.0)  # px; negative = inside the outline
        return err, best_d, best_k, best_s

    # The highlight is a band about 10-15 px wide, so a pixel's distance to the outline is mostly the band's half-width.
    # The alignment error is where the band's CENTRE lies: per cross-section (plot, edge, 1/20 of an edge) the median
    # signed distance of its band pixels. The outline scale (the EXE draws it at about 0.9-0.925 of the corner radius)
    # is fitted: the scale whose cross-section medians have the smallest median absolute value.
    def sections(err, dist, k, s):
        keep = dist < 20  # world units: pixels far from every outline belong to no outline here
        key = k[keep] * 1000 + np.floor(s[keep] * 20).astype(int)
        e, z = err[keep], q[keep, 2]
        out = []
        for u in np.unique(key):
            v = e[key == u]
            if len(v) >= 8:
                out.append((int(u), float(np.median(v)), len(v), float(np.median(z[key == u]))))
        return out

    fits = []
    for scale in np.arange(0.85, 0.9501, 0.0025):
        err, dist, k, s = signed_errors(scale)
        sec = sections(err, dist, k, s)
        if sec:
            fits.append((float(np.median(np.abs([x[1] for x in sec]))), float(scale)))
    best_scale = min(fits)[1] if fits else a.scale
    err, dist, k, s = signed_errors(best_scale)
    sec = sections(err, dist, k, s)
    med = np.array([x[1] for x in sec])
    cnt = np.array([x[2] for x in sec])
    # a cross-section cut short by a model (fewer pixels than half the typical one) has a biased centre: dropped
    full = cnt >= 0.5 * np.median(cnt) if len(cnt) else cnt.astype(bool)
    # the band is mitred at the six corners: the first and last 1/20 of each edge have no centre line of their own
    corner = np.array([(x[0] % 1000) % 20 in (0, 19) for x in sec]) if sec else np.zeros(0, bool)
    with_corners = stats(np.abs(med[full]))
    full = full & ~corner if a.no_corners else full
    absm = np.abs(med[full])
    res = {"gt": str(gd), "world": a.world, "plots": plots, "magenta_px": int(mag.sum()),
           "magenta_on_drawn_terrain": int(len(err)), "scale_fit": fits, "scale": best_scale,
           "method": "grid_accuracy.py's outline distance, as the band centre: per cross-section (1/20 edge) the median "
                     "signed screen distance from the drawn surface point under each magenta pixel to the outline",
           "sections": int(full.sum()), "corners_dropped": bool(a.no_corners), "all_with_corners": with_corners,
           "all": stats(absm), "signed_mean_px": float(med[full].mean()) if full.any() else None,
           "per_plot": {f"{gx},{gy}": stats(np.abs(med[full & (np.array([x[0] for x in sec]) // 1000 == i)]))
                        for i, (gx, gy) in enumerate(plots)},
           "section_rows": [{"plot": plots[x[0] // 1000], "bin": x[0] % 1000, "median_px": x[1], "pixels": x[2],
                             "surface_z": x[3], "kept": bool(f)} for x, f in zip(sec, full)]}
    if a.debug_png:
        # the highlight shot with each kept cross-section's band pixels coloured by |centre error|: green < 1 px,
        # yellow < 3 px, red >= 3 px; cross-sections dropped as cut short in blue
        img = B.astype(np.uint8).copy()
        keep = dist < 20
        key = np.full(len(err), -1)
        key[keep] = k[keep] * 1000 + np.floor(s[keep] * 20).astype(int)
        for (u, m_, n_, z_), f in zip(sec, full):
            col = (0, 0, 255) if not f else (0, 200, 0) if abs(m_) < 1 else (255, 220, 0) if abs(m_) < 3 else (255, 0, 0)
            sel = key == u
            img[ys[sel], xs[sel]] = col
        Image.fromarray(img).save(a.debug_png)
        res["debug_png"] = a.debug_png
    txt = json.dumps(res, indent=1)
    if a.out:
        Path(a.out).write_text(txt)
    print(json.dumps({"gt": res["gt"], "all": res["all"], "magenta_on_drawn_terrain": res["magenta_on_drawn_terrain"]}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
