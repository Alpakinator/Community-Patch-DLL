"""The server's depth against the EXE's depth (issue 3208, epic 3200; decision D58 point 2: published, not yet written).

The EXE projects with g_Proj = D3DXMatrixPerspectiveFovRH(fov, aspect, near 50, far 4000) [L, camera-and-picking.md
section 10; the camera block's `proj`], D3DX row-vector convention: clip = (x, y, z, 1) * View * Proj. Its depth buffer
is D24_UNORM_S8_UINT; the value it stores for a point is z_clip / w_clip in [0, 1] (viewport depth range 0..1),
quantised to 24 bits. With the row-vector RH projection, w_clip = -z_view = z_eye (the distance in front of the camera
along its view axis) and

    d = (z_view * P22 + P32) / (-z_view) = -P22 + P32 / z_eye          (P22 = f / (n - f), P32 = n f / (n - f))
      = f / (f - n) - n f / ((f - n) z_eye)

The two encodings of shell_shared.h VPShellDepth:
  VPDEPTH_NDC_Z (1)     R32_FLOAT texel = z_clip / w_clip of the EXE's projection (the server draws with the camera
                        block's proj): the EXE's value is round(clamp(d, 0, 1) * (2^24 - 1)).
  VPDEPTH_VIEW_DIST (2) R32_FLOAT texel = z_eye in world units (0 = nothing drawn): d = -P22 + P32 / z_eye, then the same
                        quantisation. Independent of the projection the server drew with.
This module holds the conversion (`d_from_eye`, `d24`) and an EXE-side reference (`exe_depth`: float32 math in the order
the EXE's shaders use it, clip = p * (V * P)) for the offline proof in test_tap.py.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np

D24_MAX = (1 << 24) - 1
GT = Path("D:/c5-64bit-work/i2-ground-truth")


def camera(path) -> dict:
    """A ground-truth camera JSON (gt_reader.py): view 0, proj, viewport as float32 arrays."""
    d = json.loads(Path(path).read_text())
    return {"view": np.array(d["views"][0], dtype=np.float32), "proj": np.array(d["proj"], dtype=np.float32),
            "vp": d["viewport"], "near": float(d["gvs_camera"]["near"]), "far": float(d["gvs_camera"]["far"]),
            "fov": float(d["gvs_camera"]["fov_y_deg"]), "aspect": float(d["gvs_camera"]["aspect"])}


def exe_depth(p: np.ndarray, view: np.ndarray, proj: np.ndarray):
    """(N, 3) world points -> (z/w, w, x_ndc, y_ndc), float32, clip = p * (view * proj) as the EXE's ViewProj."""
    vp = (view.astype(np.float32) @ proj.astype(np.float32)).astype(np.float32)
    h = np.concatenate([p.astype(np.float32), np.ones((len(p), 1), np.float32)], axis=1)
    c = (h @ vp).astype(np.float32)
    w = c[:, 3]
    return (c[:, 2] / w).astype(np.float32), w, c[:, 0] / w, c[:, 1] / w


def eye_depth(p: np.ndarray, view: np.ndarray) -> np.ndarray:
    """z_eye = -(p * view).z, float32 (what a VIEW_DIST server writes)."""
    h = np.concatenate([p.astype(np.float32), np.ones((len(p), 1), np.float32)], axis=1)
    return (-(h @ view.astype(np.float32))[:, 2]).astype(np.float32)


def d_from_eye(z_eye: np.ndarray, proj: np.ndarray) -> np.ndarray:
    """VIEW_DIST -> the EXE's z/w (float64 math, the conversion a writer would do in its shader)."""
    p22, p32 = float(proj[2][2]), float(proj[3][2])
    return -p22 + p32 / z_eye.astype(np.float64)


def d24(d: np.ndarray) -> np.ndarray:
    return np.rint(np.clip(d.astype(np.float64), 0.0, 1.0) * D24_MAX).astype(np.int64)


def world_per_d24(z_eye: float, near: float, far: float) -> float:
    """World units along the view axis covered by one D24 step at distance z_eye: dz = z^2 (f - n) / (n f) / (2^24 - 1)."""
    return z_eye * z_eye * (far - near) / (near * far) / D24_MAX


def terrain_points(game: str, cam: dict, step: int = 4, max_points: int = 400000):
    """World positions of heightmap samples (every `step`-th in x and y) that the camera sees, from the 3004 dump."""
    g = json.loads((GT / game / "grid_constants.json").read_text())
    info = json.loads((GT / game / "heightmap_info.json").read_text())
    sx_, sy_ = int(info.get("samples_x") or info.get("width")), int(info.get("samples_y") or info.get("height"))
    raw = np.fromfile(next((GT / game).glob("heightmap_u16le_*.raw")), dtype="<u2").reshape(sy_, sx_)
    r, c = np.mgrid[0:sy_:step, 0:sx_:step]
    h = raw[r, c].astype(np.float32)
    pts = np.stack([c.ravel().astype(np.float32) * np.float32(g["grid_sx"]) - np.float32(g["grid_ox"]),
                    r.ravel().astype(np.float32) * np.float32(g["grid_sy"]) - np.float32(g["grid_oy"]),
                    h.ravel() * np.float32(1.0 / 255.0) * np.float32(g["grid_height_scale"])], axis=1)
    z, w, x, y = exe_depth(pts, cam["view"], cam["proj"])
    keep = (w > 0) & (np.abs(x) <= 1) & (np.abs(y) <= 1) & (z >= 0) & (z <= 1)
    return pts[keep][:max_points]
