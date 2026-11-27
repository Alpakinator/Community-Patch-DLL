"""Issue 3007: offline accuracy of the server's hex grid against the EXE's own hex outlines (ground truth of issue 3004).

For each ground-truth game (D:/c5-64bit-work/i2-ground-truth/{huge,tiny}, or C5_GROUND_TRUTH) the test writes the
dumped camera (camera_validate.json) and grid constants into a camera block of a fresh section, exactly as
shell_composite.dll would publish them, and runs shell_server.exe --scene grid --dump on a hidden desktop: the server
renders its grid with its own math (vertex generation on the CPU, view_i * proj in the vertex shader, D3D11
rasterisation) at the EXE's back-buffer size and writes the frame as a BMP. Measured against the frame:

- **error vs the EXE** (the number the issue asks for): every yellow grid pixel that belongs to one of the plots the
  capture outlined in magenta (Events.SerialEventHexHighlight; validate_highlight.png minus validate_nohighlight.png),
  distance to the nearest magenta pixel. The EXE draws that outline at about 0.9 of the hex corner radius
  (gt_validate.py fit), so the grid is rendered at --grid-scale 0.9 for this number; scale 1.0 (the true hex edges) is
  reported too. Pixels whose magenta is hidden by models or UI give large distances: median, p75 and the shares within
  1 and 3 px are reported, as gt_validate.py does.
- **error vs the reference projection**: every grid pixel in the box of the outlined hexes against gt_reader.project of
  the same outlines (Python, double precision) of the outlined hexes and two rings of neighbours: the server's math
  and rasterisation against the formula, independent of the screenshot.

Variants: with the dumped heightmap (z of every vertex from the terrain) and with flat z 12. Results go to
<tmp>/offline_<game>_<variant>.json and are printed.
"""
import json
import os
import sys
from pathlib import Path

import numpy as np
import pytest

import camblock
import shellrun

sys.path.insert(0, str(shellrun.SHELL / "tools"))
import gt_reader as gt  # noqa: E402

GT = Path(os.environ.get("C5_GROUND_TRUTH", "D:/c5-64bit-work/i2-ground-truth"))
AXIAL = [(1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1), (1, -1)]
VARIANTS = {"hm_s090": (0.9, True), "flat_s090": (0.9, False), "hm_s100": (1.0, True)}
FLAT_Z = 12.0   # the server's default --flat-z
WATER_Z = 10.0  # the server's default --water-z (issue 3012; 12 until 2026-11-13)


def load_gt(game):
    d = GT / game
    if not (d / "camera_validate.json").exists():
        pytest.skip("ground truth %s not found" % d)
    cam = json.loads((d / "camera_validate.json").read_text())
    grid = json.loads((d / "grid_constants.json").read_text())
    info = json.loads((d / "heightmap_info.json").read_text())
    raw = d / ("heightmap_u16le_%dx%d.raw" % (info["samples_x"], info["samples_y"]))
    hm = np.memmap(str(raw), dtype="<u2", mode="r", shape=(info["samples_y"], info["samples_x"]))
    cap = json.loads((d / "capture.json").read_text())
    hexes = []
    for r in cap["hex_to_world_lua"].split(";"):
        f = r.split(",")
        hexes.append((int(f[2]), int(f[3])))
    return d, cam, grid, hm, raw, sorted(set(hexes))


def magenta(d):
    from PIL import Image
    a = np.asarray(Image.open(d / "validate_highlight.png").convert("RGB")).astype(int)
    b = np.asarray(Image.open(d / "validate_nohighlight.png").convert("RGB")).astype(int)
    diff = np.abs(a - b).sum(axis=2)
    m = (diff > 60) & (a[..., 0] > 150) & (a[..., 2] > 150) & (a[..., 1] < 120)
    ys, xs = np.nonzero(m)
    return np.stack([xs, ys], axis=1).astype(float)


def render(tmp, desktop, built, cam, grid, raw, scale, use_hm, tag):
    W, H = cam["viewport"]["Width"], cam["viewport"]["Height"]
    name = "Local\\VPShellOffline_%d_%s" % (os.getpid(), tag)
    sec = camblock.Section(name)
    try:
        sec.write_camera(camblock.block_from_ground_truth(cam, grid))
        bmp, log = tmp / ("grid_%s.bmp" % tag), tmp / ("server_%s.log" % tag)
        args = [str(built / "shell_server.exe"), "--section", name, "--name", "Local\\VPShellOfflineTex_%d_%s" % (os.getpid(), tag),
                "--scene", "grid", "--rate", "0", "--size", "%dx%d" % (W, H), "--grid-scale", str(scale),
                "--flat-z", str(FLAT_Z), "--dump", str(bmp), "--dump-after", "2", "--seconds", "90", "--log", str(log)]
        if use_hm:
            args += ["--heightmap", str(raw)]
        p = shellrun.launch(args, desktop, console=True)
        code = p.wait(120)
        p.close()
        assert code == 0 and bmp.exists(), shellrun.tail(log, 10)
        text = log.read_text(encoding="utf-8", errors="replace")
        assert ("z from the heightmap" in text) == use_hm, text[-800:]
    finally:
        sec.close()
    img = shellrun.Image(bmp)
    a = np.frombuffer(img.px, dtype=np.uint8).reshape(img.h, img.w, 4)
    return a if img.top_down else a[::-1]


def outline_points(cam, grid, hm, hx, hy, scale, flat_z=None, n=600):
    """gt_validate.py's outline of hex (hx, hy) at `scale` of the corner radius, projected with gt_reader.project:
    z from the heightmap along the edge, or flat_z."""
    ctr = np.array(gt.hex_to_world(grid, hm, hx, hy, 0.0)[:2])
    nb = [np.array(gt.hex_to_world(grid, hm, hx + dx, hy + dy, 0.0)[:2]) for dx, dy in AXIAL]
    corners = [ctr + scale * ((ctr + nb[i] + nb[(i + 1) % 6]) / 3.0 - ctr) for i in range(6)]
    pts = []
    for i in range(6):
        p0, p1 = corners[i], corners[(i + 1) % 6]
        for t in np.linspace(0, 1, n, endpoint=False):
            p = p0 + (p1 - p0) * t
            z = flat_z if flat_z is not None else gt.grid_height(
                hm, grid, (p[0] + grid["grid_ox"]) / grid["grid_sx"], (p[1] + grid["grid_oy"]) / grid["grid_sy"])
            if flat_z is None:
                z = max(z, WATER_Z)   # issue 3012: the server raises water (z 0) to the EXE's outline height
            q = gt.project(cam, [p[0], p[1], z])
            if q:
                pts.append(q[:2])
    return np.array(pts).reshape(-1, 2)


def measure(a, cam, grid, hm, hexes, mag, scale, flat_z=None):
    """Grid pixels near the outlined hexes, each assigned to the nearest point of the reference outlines of the
    outlined hexes and two rings of neighbours (a screen-space nearest-centre assignment fails under the strong
    perspective at the bottom of the screen). ref = distance to that nearest reference point (every grid pixel in the
    box); exe = for the pixels assigned to an outlined hex, distance to the nearest magenta pixel."""
    from scipy.spatial import cKDTree
    b, g, r = a[..., 0].astype(int), a[..., 1].astype(int), a[..., 2].astype(int)
    ys, xs = np.nonzero((r > 200) & (g > 200) & (b < 80))
    # pixel (i, j) covers [i, i+1) x [j, j+1) in D3DXVec3Project coordinates: compare its centre
    pix = np.stack([xs, ys], axis=1).astype(float) + 0.5
    mag = mag + 0.5
    ring = set(hexes)
    for _ in range(2):
        ring |= {(hx + dx, hy + dy) for hx, hy in ring for dx, dy in AXIAL}
    pts, lab = [], []
    for h in sorted(ring):
        o = outline_points(cam, grid, hm, h[0], h[1], scale, flat_z)
        pts.append(o)
        lab += [h in hexes] * len(o)
    pts, lab = np.vstack(pts), np.array(lab)
    own_pts = pts[lab]
    H, W = a.shape[:2]
    x0, y0 = max(0, own_pts[:, 0].min() - 5), max(0, own_pts[:, 1].min() - 5)
    x1, y1 = min(W, own_pts[:, 0].max() + 5), min(H, own_pts[:, 1].max() + 5)
    box = (pix[:, 0] >= x0) & (pix[:, 0] < x1) & (pix[:, 1] >= y0) & (pix[:, 1] < y1)
    pix = pix[box]
    d_ref, idx = cKDTree(pts).query(pix)
    sel = pix[lab[idx]]
    assert len(sel) > 200, len(sel)
    d_exe = cKDTree(mag).query(sel)[0]
    d_own = d_ref[lab[idx]]
    return {"grid_pixels_box": int(len(pix)), "grid_pixels_outlined": int(len(sel)), "magenta_pixels": int(len(mag)),
            "exe_median_px": float(np.median(d_exe)), "exe_p75_px": float(np.percentile(d_exe, 75)),
            "exe_within_1px": float((d_exe <= 1).mean()), "exe_within_3px": float((d_exe <= 3).mean()),
            "ref_median_px": float(np.median(d_ref)), "ref_p95_px": float(np.percentile(d_ref, 95)),
            "ref_p99_px": float(np.percentile(d_ref, 99)), "ref_max_px": float(d_ref.max()),
            "ref_outlined_median_px": float(np.median(d_own)), "ref_outlined_max_px": float(d_own.max()),
            "box": [float(x0), float(y0), float(x1), float(y1)]}


@pytest.mark.parametrize("variant", list(VARIANTS))
@pytest.mark.parametrize("game", ["huge", "tiny"])
def test_grid_matches_exe_hexes(tmp_path, hidden_desktop, built, game, variant):
    scale, use_hm = VARIANTS[variant]
    d, cam, grid, hm, raw, hexes = load_gt(game)
    mag = magenta(d)
    a = render(tmp_path, hidden_desktop, built, cam, grid, raw, scale, use_hm, "%s_%s" % (game, variant))
    res = measure(a, cam, grid, hm, hexes, mag, scale, None if use_hm else FLAT_Z)
    res.update(game=game, variant=variant, scale=scale, heightmap=use_hm, hexes=hexes)
    out = tmp_path / ("offline_%s_%s.json" % (game, variant))
    out.write_text(json.dumps(res, indent=1))
    print("OFFLINE %s" % json.dumps(res))
    # the server renders the same outlines as the Python reference (its math and rasterisation)
    # (with the heightmap the server follows the terrain with 4 straight segments per edge, the reference with 600 points:
    # that, not the projection, is the tail above 1 px)
    assert res["ref_median_px"] <= 0.75 and res["ref_p95_px"] <= 2.0, res
    if variant == "hm_s090":
        # the number the issue asks for: grid lines on the EXE's own hex outline
        assert res["exe_median_px"] <= 1.5 and res["exe_within_3px"] >= 0.5, res
