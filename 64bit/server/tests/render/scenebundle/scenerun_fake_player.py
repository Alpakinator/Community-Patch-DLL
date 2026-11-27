"""A stand-in for sceneshot.exe in scenerun's tests (task 3906): the same command line and the same sceneshot.json, with
pictures made from numbers (nothing from the game). Environment knobs:
    FAKE_EXIT=N        exit N after writing (10 = a P1 difference, 8 = a replay failure: no sceneshot.json)
    FAKE_SHIFT=N       add N to the red channel of every pixel (a regression)
    FAKE_PIXEL=N       change one pixel's red channel by N (a regression within or beyond a tolerance)
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bundle", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dumped", action="store_true")
    ap.add_argument("--camera")
    ap.add_argument("--compare", action="store_true")
    ap.add_argument("--diff", action="store_true")
    ap.add_argument("--tolerance", default="0:0")
    ap.add_argument("--no-pictures", action="store_true")
    ap.add_argument("--adapter", default="bundle")
    ap.add_argument("--install")
    ap.add_argument("--settle-ms")
    a = ap.parse_args()
    b = Path(a.bundle)
    if not (b / "scene.json").is_file():
        print("sceneshot: FAILED 21 NoManifest: no bundle")
        return 21
    scene = json.loads((b / "scene.json").read_text(encoding="utf-8"))
    code = int(os.environ.get("FAKE_EXIT", "0"))
    if code == 8:
        print("sceneshot: FAILED 8 ReplayFailed: stand-in")
        return 8
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    w, h = 64, 32
    names = [str(f) for f in scene["frames"]["dumped"]] if a.dumped else [a.camera]
    shots = []
    for i, name in enumerate(names):
        img = np.zeros((h, w, 4), dtype=np.uint8)
        img[..., 0] = (np.arange(w)[None, :] * 3) % 256
        img[..., 1] = (np.arange(h)[:, None] * 5) % 256
        img[..., 2] = 40
        img[..., 3] = 255
        img[: h // 4, :, :3] = 0                 # a dark band (the "unrevealed" region)
        shift = int(os.environ.get("FAKE_SHIFT", "0"))
        if shift:
            img[..., 0] = np.clip(img[..., 0].astype(int) + shift, 0, 255).astype(np.uint8)
        px = int(os.environ.get("FAKE_PIXEL", "0"))
        if px:
            img[h // 2, w // 2, 0] = np.uint8((int(img[h // 2, w // 2, 0]) + px) % 256)
        depth = np.full((h, w), 0.95, dtype="<f4")
        depth[: h // 8, :] = 1.0
        s = {"name": name, "frame_id": int(name) if a.dumped else None, "width": w, "height": h,
             "colour_sha256": hashlib.sha256(img.tobytes()).hexdigest(),
             "depth_sha256": hashlib.sha256(depth.tobytes()).hexdigest(), "compared": bool(a.compare),
             "within_tolerance": code != 10, "colour_equal": code != 10, "colour_differing": 0 if code != 10 else 5,
             "colour_max_delta": 0 if code != 10 else 9, "depth_equal": True, "settle_ms": 0.1,
             "shot_ms": 10.0 + i, "png": "", "depth": "", "diff": ""}
        if not a.no_pictures:
            Image.fromarray(img, "RGBA").save(str(out / (name + ".png")))
            depth.tofile(str(out / (name + ".depth.f32")))
            s["png"] = (out / (name + ".png")).as_posix()
            s["depth"] = (out / (name + ".depth.f32")).as_posix()
        shots.append(s)
        print("sceneshot: SHOT %s" % name)
    adapter = scene.get("adapter", {}).get("description", "Stand-in adapter")
    res = {"tool": "sceneshot", "bundle_id": scene["bundle_id"], "bundle": b.as_posix(),
           "adapter": adapter if a.adapter == "bundle" else "Other adapter", "adapter_luid": "0",
           "same_adapter": a.adapter == "bundle", "install": a.install or "C:/no/such/install", "shots": shots}
    (out / "sceneshot.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
    if code == 10:
        print("sceneshot: FAILED 10 Differs: stand-in")
        return 10
    print("sceneshot: OK %d shot(s)" % len(shots))
    return 0


if __name__ == "__main__":
    sys.exit(main())
