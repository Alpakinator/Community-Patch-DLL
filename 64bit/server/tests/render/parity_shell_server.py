"""Parity of the renderer inside server.exe with shell_server.exe --scene terrain (issue 3301).

    python server/tests/render/parity_shell_server.py --shell-server PATH/shell_server.exe --host-build DIR/bin
        --exe CivilizationV_DX11.exe --out DIR [--gt-dir D:/c5-64bit-work/i2-ground-truth/tiny --tap-file F.c5hf]

The same inputs (the synthetic camera and C5HF of renderfx.py, or a ground-truth camera with a C5HF file) go to
shell_server.exe --scene terrain --depth 1 --dump (the program 3301 ported, unchanged) and to render_host_test.exe with
render_test_dump (the render objects of server.exe), each on its own section on a hidden desktop; the two first frames'
colour (BMP pixels) and depth (.depth.f32) are compared byte for byte. Prints one line per picture and exits 0 when both
are identical, 1 when they differ, 2 when a program failed.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "shell" / "tests"))

import numpy as np  # noqa: E402
import camblock  # noqa: E402
import shellrun  # noqa: E402
import desktops  # noqa: E402
import renderfx as fx  # noqa: E402


def inputs(sec, tag, a):
    payload = a.tap_file.read_bytes() if a.tap_file else fx.c5hf(fx.synth_field())
    mw, mh = struct.unpack("<II", payload[8:16])
    if a.gt_dir:
        gcam = json.loads((a.gt_dir / ("camera_%s.json" % a.gt_camera)).read_text())
        grid = json.loads((a.gt_dir / "grid_constants.json").read_text())
        cam = camblock.block_from_ground_truth(gcam, grid)
        size = tuple(gcam["backbuffer"])
    else:
        cam = fx.synth_camera(frame=1)
        size = (fx.W, fx.H)
    name = "Local\\VPParityTap_%d_%s" % (os.getpid(), tag)
    tap = fx.TapSection(name, payload)
    sec.write_camera(cam)
    fx.write_tap(sec, load_seq=1, name=name, payload=payload, map_w=mw, map_h=mh)
    return tap, size


def pixels(bmp):
    img = shellrun.Image(bmp)
    a = np.frombuffer(img.px, dtype=np.uint8).reshape(img.h, img.w, 4)
    return a if img.top_down else a[::-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell-server", type=Path, required=True)
    ap.add_argument("--host-build", type=Path, required=True)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--gt-dir", type=Path)
    ap.add_argument("--gt-camera", default="v1_city_default_after")
    ap.add_argument("--tap-file", type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    d = desktops.HiddenDesktop(prefix="C5RPARITY")
    res = {}
    try:
        # shell_server.exe --scene terrain (its own section)
        s1 = "Local\\VPParityA_%d" % os.getpid()
        sec1 = camblock.Section(s1)
        tap1, size = inputs(sec1, "A", a)
        dump_a = a.out / "shell_server.bmp"
        p = shellrun.launch([str(a.shell_server), "--section", s1, "--name", "Local\\VPParityTexA_%d" % os.getpid(), "--scene",
                             "terrain", "--size", "%dx%d" % size, "--seconds", "30", "--log", str(a.out / "shell_server.log"),
                             "--exe", str(a.exe), "--depth", "1", "--dump", str(dump_a), "--dump-after", "1"], d.name, console=True,
                            out_path=a.out / "shell_server.out")
        rc_a = p.wait(60)
        p.close()
        # render_host_test.exe (the render objects of server.exe)
        s2 = "Local\\VPParityB_%d" % os.getpid()
        sec2 = camblock.Section(s2)
        tap2, _ = inputs(sec2, "B", a)
        dump_b = a.out / "render_host.bmp"
        cfg = a.out / "server.cfg"
        cfg.write_text("render_section=%s\nrender_texture=Local\\VPParityTexB_%d\nrender_exe=%s\nrender_log=%s\nrender_size=%dx%d\n"
                       "render_test_dump=%s\nrender_test_dump_after=1\n" % (s2, os.getpid(), a.exe, a.out / "render.log", size[0],
                                                                            size[1], dump_b))
        p = shellrun.launch([str(a.host_build / "render_host_test.exe"), "--config", str(cfg), "--seconds", "12", "--run-dir",
                             str(a.out), "--game-dir", str(a.out)], d.name, console=True, out_path=a.out / "render_host.out")
        rc_b = p.wait(60)
        p.close()
        res["exit"] = [rc_a, rc_b]
        if rc_a != 0 or rc_b != 0 or not dump_a.exists() or not dump_b.exists():
            print("parity: a program failed: shell_server exit %s, render_host_test exit %s" % (rc_a, rc_b))
            return 2
        pa, pb = pixels(dump_a), pixels(dump_b)
        same_c = pa.shape == pb.shape and bool(np.array_equal(pa, pb))
        diff_c = int((pa != pb).any(axis=2).sum()) if pa.shape == pb.shape else -1
        da = Path(str(dump_a) + ".depth.f32").read_bytes()
        db = Path(str(dump_b) + ".depth.f32").read_bytes()
        same_d = da == db
        fa, fb = np.frombuffer(da, "<f4"), np.frombuffer(db, "<f4")
        diff_d = int((fa != fb).sum()) if fa.shape == fb.shape else -1
        drawn = float((pa[..., :3].max(axis=2) > 0).mean())
        res.update({"size": list(size), "colour_identical": same_c, "colour_pixels_differing": diff_c,
                    "colour_sha256": [hashlib.sha256(pa.tobytes()).hexdigest()[:16], hashlib.sha256(pb.tobytes()).hexdigest()[:16]],
                    "depth_identical": same_d, "depth_texels_differing": diff_d, "drawn_share": drawn})
        print("parity: colour %s (%d of %d pixels differ), depth %s (%d texels differ), %dx%d, terrain over %.1f %% of the view"
              % ("IDENTICAL" if same_c else "DIFFERENT", diff_c, pa.shape[0] * pa.shape[1], "IDENTICAL" if same_d else "DIFFERENT",
                 diff_d, size[0], size[1], 100 * drawn))
        (a.out / "parity.json").write_text(json.dumps(res, indent=1))
        tap1.close()
        tap2.close()
        sec1.close()
        sec2.close()
        return 0 if same_c and same_d else 1
    finally:
        d.close()


if __name__ == "__main__":
    sys.exit(main())
