"""Drive server.exe's renderer with the fake composite while a game runs (issue 3301, product-level test).

    python server/tests/render/drive_fake_composite.py --section NAME --bin DIR --seconds S --out DIR [--wait S]

Waits (up to --wait seconds) until a server's renderer has initialised the version-3 section NAME (server.cfg
render_section=NAME, render=1), then for --seconds plays the composite's producer half (a synthetic C5HF tap and camera
blocks at 30 Hz, renderfx.py) and runs fake_consumer.exe (DIR/fake_consumer.exe) on a hidden desktop to take frames.
Writes OUT/consumer.jsonl and OUT/drive.json (frames taken, camera stamps, the server block of NAME.Frame). Exit 0 when
the consumer took frames drawn with the cameras written (terrain drawn, depth stamps among them), 1 otherwise, 2 when
the section never appeared.
Started next to proxy/tools/run_proxy_game.py with --server-cfg render=1 render_section=NAME: the game's stats.db must
stay identical to a render=0 run (the renderer never touches the DLL).
"""
import argparse
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

import camblock  # noqa: E402
import shellrun  # noqa: E402
import desktops  # noqa: E402
import renderfx as fx  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--section", required=True)
    ap.add_argument("--bin", type=Path, required=True)
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--wait", type=float, default=120)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--tap-file", type=Path, help="a C5HF file as the tap (e.g. the huge ground truth's) instead of the "
                                                   "synthetic 6 x 6 field")
    ap.add_argument("--gt-dir", type=Path, help="ground-truth folder of epic 3000 (camera_*.json, grid_constants.json): "
                                                "its camera instead of the synthetic one")
    ap.add_argument("--gt-camera", default="v1_city_default_after")
    ap.add_argument("--request", help="WxH: the consumer asks for this back-buffer size (the composite's resize request)")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    sec = camblock.Section(a.section)
    frame = fx.FrameSection(a.section + ".Frame")
    t_end = time.time() + a.wait
    while time.time() < t_end:
        if struct.unpack("<I", sec.read(0, 4))[0] == 0x4C485356:   # VPSHELL_MAGIC: the v3 sink is open
            break
        time.sleep(0.1)
    else:
        print("drive: the section %s was never initialised" % a.section)
        return 2
    srv0 = frame.server()
    print("drive: section ready, server pid %d state %s" % (srv0["serverPid"], fx.STATES.get(srv0["state"])), flush=True)
    payload = a.tap_file.read_bytes() if a.tap_file else fx.c5hf(fx.synth_field())
    map_w, map_h = struct.unpack("<II", payload[8:16])
    name = "Local\\VPRenderDriveTap_%d" % os.getpid()
    tap = fx.TapSection(name, payload)
    if a.gt_dir:
        gcam = json.loads((a.gt_dir / ("camera_%s.json" % a.gt_camera)).read_text())
        grid = json.loads((a.gt_dir / "grid_constants.json").read_text())
        base = camblock.block_from_ground_truth(gcam, grid)
    else:
        base = None

    def camera(frame):
        if base is None:
            return fx.synth_camera(frame=frame, pan=float(frame % 9))
        b = dict(base)
        b["frameStamp"] = b["presentCount"] = frame
        return b
    sec.write_camera(camera(1))
    fx.write_tap(sec, load_seq=1, name=name, payload=payload, map_w=map_w, map_h=map_h)
    print("drive: tap of %d bytes (%d x %d plots) published" % (len(payload), map_w, map_h), flush=True)
    d = desktops.HiddenDesktop(prefix="C5RDRIVE")
    out = a.out / "consumer.jsonl"
    nframes = max(1, int(a.seconds * 10))
    cargs = [str(a.bin / "fake_consumer.exe"), "--section", a.section, "--frames", str(nframes), "--timeout",
             str(a.seconds + 30), "--out", str(out)]
    if a.request:
        cargs += ["--request", a.request]
    c = shellrun.launch(cargs, d.name, console=True)
    f = 1
    t1 = time.time() + a.seconds
    while time.time() < t1:
        f += 1
        sec.write_camera(camera(f))
        time.sleep(1 / 30.0)
    code = c.wait(30)
    c.close()
    d.close()
    rows = fx.frames(out) if out.exists() else []
    srv = frame.server()
    ack = fx.tap_ack(sec)
    res = {"frames_taken": len(rows), "consumer_exit": code, "cameras_written": f, "tap_ack": list(ack),
           "stamps": [r["camFrameStamp"] for r in rows][:400], "drawn_min": min((r["drawn"] for r in rows), default=0),
           "server": srv}
    ok = (code == 0 and len(rows) >= nframes and ack[:2] == (1, 0) and res["drawn_min"] > 0.3 and
          all(1 <= r["camFrameStamp"] <= f for r in rows) and all(r["frameCounter"] == r["depthFrameCounter"] for r in rows))
    res["ok"] = ok
    (a.out / "drive.json").write_text(json.dumps(res, indent=1))
    print("drive: %s: %d frames taken (consumer exit %s), %d cameras written, tap ack %s, drawn min %.3f, server state %s draws %d"
          % ("OK" if ok else "FAILED", len(rows), code, f, ack[:2], res["drawn_min"], fx.STATES.get(srv["state"]), srv["drawsTotal"]))
    tap.close()
    frame.close()
    sec.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
