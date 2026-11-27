"""cityscene.py - the offline harness of the BuildingShader draw layer (ticket 4873, epic 0561; research/05-graphics-
composition/landmark-renderer.md 7). Own code, 2026-11-18.

    python -I render/tools/cityscene/cityscene.py frame EXPORT OUT --tool CITYSCENE_EXE --replay TERRAINREPLAY_EXE
        --game INSTALL --db DB --exe EXE [--objects OBJECTS.csv --export-name NAME] [--keep]

One capture export (3907 / 3910, rdc-export format): every colour-pass draw of the four BuildingShader pairs into a
target with a depth view (the draws the compositor drops and taps, shell_model_scene.h) is listed in OUT/frame.tsv;
`cityscene tap` makes the tap frame OUT/frame.mtap from the capture's own buffers; `cityscene run` resolves it against
the library (OUT/scene.c5models) and draws it with the EXE's programs (OUT/ours_<t>.rgba, _id.u32); the reference is
the captured draws themselves replayed by terrainreplay (models/tools/unitreplay/unit_manifest.py: the capture's
buffers, textures, programs and states into a cleared target, OUT/ref_<t>/captured.rgba, _id.u32), because the exports
hold no contents of the scene target. Compared on the object pixels (either side's id buffer set): per frame and per
object (3910 / 3907 catalogue objects through 4870's placement table), median, p95, max of the per-pixel largest RGB
difference and the share within 16 (COMMON 25); OUT/result.json, OUT/diff_<t>.png (x8, DIAGNOSTIC), OUT/states.json
(the capture's states against the draw layer's table). Without --keep the large raw files are deleted at the end.

Exit codes: 0 compared; 1 a step failed; 2 usage; 3 the export has no family colour draw.
Nothing of the game is committed (D83): every output is under the work folders.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import struct
import subprocess
import sys
import zlib
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
UNIT_MANIFEST = REPO / "models" / "tools" / "unitreplay" / "unit_manifest.py"
WORK_ROOTS = ("c:/c5-64bit-work/", "d:/c5-64bit-work/", "f:/c5-64bit-work/")
NONE = 0xFFFFFFFF

# The four DROP pairs (shell_model_scene.h kVPModelDropPairs; digest prefixes as the exports name them) -> material.
PAIRS = {("7ec4e9a5", "c704c80b"): 5, ("c4bb25da", "f0e1f0a6"): 3, ("7ec4e9a5", "f0e1f0a6"): 4,
         ("7a9e17f6", "f0e1f0a6"): 6}
FMT_INDEX = {57: 2, 42: 4}   # DXGI R16_UINT, R32_UINT

# The draw layer's state table (render/src/landmarks/draw.cpp captureStates), checked against every listed draw.
def expected_states(mat, reflection):
    blend = mat != 5
    return {"blend_enable": blend, "src": 5 if blend else 2, "dst": 6 if blend else 1, "src_a": 2, "dst_a": 2 if blend else 1,
            "cull": 2 if reflection else 3, "front_ccw": True, "depth_enable": True, "depth_write": 1, "depth_func": 4,
            "stencil": False}


def ev(x):
    return x["v"] if isinstance(x, dict) else x


def work_path(p):
    return any(str(Path(p).resolve()).replace("\\", "/").lower().startswith(r) for r in WORK_ROOTS)


def png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def tex_entry(root, data, vd):
    d = data.get("desc") or {}
    dim = 3 if data.get("kind") == "texture3d" else (4 if ev(vd.get("ViewDimension")) in (9, 10) else 2)
    sizes = data.get("subresource_bytes") or []
    return {"id": str(data["res"]), "format": ev(d.get("Format")), "view": ev(vd.get("Format")) or ev(d.get("Format")),
            "dim": dim, "w": d.get("Width", 1), "h": d.get("Height", 1), "d": d.get("Depth") or 1,
            "mips": d.get("MipLevels") or 1, "array": d.get("ArraySize") or 1, "path": (root / data["file"]).as_posix(),
            "sizes": sizes, "sha": data.get("sha256", "")}


def list_frame(root: Path):
    """The family colour draws (pair known, a depth view bound), their targets and textures, and the state checks."""
    idx = json.loads((root / "draws.json").read_text())
    picked = [d for d in idx["draws"] if (d.get("vs"), d.get("ps")) in PAIRS and d.get("dsv_res") is not None
              and d.get("call") == "DrawIndexedInstanced"]
    targets, tex, draws, states = [], {}, [], Counter()
    tindex = {}
    env_sets = defaultdict(Counter)
    view_fmt = Counter()
    for d in picked:
        st = json.loads((root / d["dir"] / "state.json").read_text())
        mat = PAIRS[(d["vs"], d["ps"])]
        om = st["om"]
        rt = om["rtv"][0]
        v0 = st["rs"]["viewports"][0]
        key = (rt["res"], om["dsv"]["res"])
        if key not in tindex:
            dd = om["dsv"].get("res_desc") or {}
            rd = rt.get("res_desc") or {}
            tindex[key] = len(targets)
            targets.append({"res": rt["res"], "dsv": om["dsv"]["res"], "w": int(v0["Width"]), "h": int(v0["Height"]),
                            "samples": (rd.get("SampleDesc") or {}).get("Count", 1) if rd else 0,
                            "format": ev(rt["view_desc"]["Format"]), "dw": dd.get("Width", 0), "dh": dd.get("Height", 0)})
        ti = tindex[key]
        reflection = targets[ti]["w"] == targets[ti]["h"] == 1024
        a = st["args"]
        ia = st["ia"]
        vb = ia["vb"]
        vs, ps = st["VS"], st["PS"]

        def buf(b):
            return (root / b["data"]["file"]).as_posix() if b and (b.get("data") or {}).get("file") else "-"
        rec = {"n": d["n"], "mat": mat, "target": ti, "ic": a["IndexCountPerInstance"], "si": a["StartIndexLocation"],
               "bv": a["BaseVertexLocation"], "inst": a["InstanceCount"], "sinst": a["StartInstanceLocation"],
               "ibb": FMT_INDEX.get(ev(ia["ib"]["format"]), 0), "ib": buf(ia["ib"]), "iboff": ia["ib"]["offset"] or 0,
               "vb": buf(vb.get("0")), "vbstride": vb["0"]["stride"], "vboff": vb["0"]["offset"] or 0,
               "s1": buf(vb.get("1")), "s1stride": vb["1"]["stride"] if "1" in vb else "-",
               "s1off": vb["1"]["offset"] if "1" in vb else "-",
               "cb0": buf(vs["cb"].get("0")), "vscb1": buf(vs["cb"].get("1")), "pscb1": buf(ps["cb"].get("1")),
               "t27": buf(vs["srv"].get("27")), "tex": []}
        for k in range(10):
            s = ps["srv"].get(str(k))
            if s and (s.get("data") or {}).get("file"):
                e = tex_entry(root, s["data"], s.get("view_desc") or {})
                tex[e["id"]] = e
                rec["tex"].append(e["id"])
                if k < 2:
                    view_fmt[(k, e["view"])] += 1
            else:
                rec["tex"].append("-")
        env_sets[(mat, ti)][tuple(tex[i]["sha"] if i in tex else "-" for i in rec["tex"][2:5])] += 1
        # states against the table
        exp = expected_states(mat, reflection)
        bl = ((om.get("blend") or {}).get("desc") or {}).get("RenderTarget", [{}])[0]
        ds = (om.get("depth") or {}).get("desc") or {}
        rs = (st["rs"].get("state") or {}).get("desc") or {}
        got = {"blend_enable": bool(bl.get("BlendEnable")), "src": ev(bl.get("SrcBlend")), "dst": ev(bl.get("DestBlend")),
               "src_a": ev(bl.get("SrcBlendAlpha")), "dst_a": ev(bl.get("DestBlendAlpha")), "cull": ev(rs.get("CullMode")),
               "front_ccw": bool(rs.get("FrontCounterClockwise")), "depth_enable": bool(ds.get("DepthEnable")),
               "depth_write": ev(ds.get("DepthWriteMask")), "depth_func": ev(ds.get("DepthFunc")),
               "stencil": bool(ds.get("StencilEnable"))}
        if not exp["blend_enable"]:   # with blending off the factors are not read
            got["src"], got["dst"], got["src_a"], got["dst_a"] = exp["src"], exp["dst"], exp["src_a"], exp["dst_a"]
        diff = sorted(k for k in exp if exp[k] != got[k])
        states[(mat, "reflection" if reflection else "scene", ",".join(diff) or "equal")] += 1
        rec["state_diff"] = diff
        draws.append(rec)
    return targets, tex, draws, states, env_sets, view_fmt


def write_frame(path: Path, targets, tex, draws):
    L = ["# cityscene frame list (ticket 4873); game data paths, never committed"]
    for i, t in enumerate(targets):
        L.append("\t".join(map(str, ["target", i, t["w"], t["h"], t["samples"], t["format"], t["dw"], t["dh"]])))
    for e in tex.values():
        L.append("\t".join(map(str, ["tex", e["id"], e["format"], e["view"], e["dim"], e["w"], e["h"], e["d"], e["mips"],
                                     e["array"], e["path"], ",".join(map(str, e["sizes"])) or "0"])))
    for i, r in enumerate(draws):
        L.append("\t".join(map(str, ["draw", i, r["n"], r["mat"], r["target"], r["ic"], r["si"], r["bv"], r["inst"], r["sinst"],
                                     r["ibb"], r["ib"], r["iboff"], r["vb"], r["vbstride"], r["vboff"], r["s1"], r["s1stride"],
                                     r["s1off"], r["cb0"], r["vscb1"], r["pscb1"], r["t27"]] + r["tex"])))
    path.write_text("\n".join(L) + "\n")


def objects_of(objects_csv, export_name):
    """export draw n -> catalogued object id (4870's placement: the first instance placed on an object), '' = none."""
    m = {}
    if not objects_csv:
        return m
    with open(objects_csv, newline="") as f:
        for r in csv.DictReader(f):
            if r["export"] != export_name:
                continue
            n = int(r["draw"])
            if r.get("object") and n not in m:
                m[n] = r["object"]
            m.setdefault(n, "")
    return m


def stats(d):
    if d.size == 0:
        return {"pixels": 0}
    return {"pixels": int(d.size), "median": float(np.median(d)), "p95": float(np.percentile(d, 95)), "max": int(d.max()),
            "within16": float((d <= 16).mean())}


def compare(out: Path, ti, W, H, ref_dir: Path, list_ti, items, obj):
    cap = np.fromfile(ref_dir / "captured.rgba", np.uint8).reshape(H, W, 4)
    cid = np.fromfile(ref_dir / "captured_id.u32", np.uint32).reshape(H, W)
    ours = np.fromfile(out / ("ours_%d.rgba" % ti), np.uint8).reshape(H, W, 4)
    oid = np.fromfile(out / ("ours_%d_id.u32" % ti), np.uint32).reshape(H, W)
    # draw numbers per pixel on both sides
    cap_n = np.full(cid.shape, -1, np.int64)
    ok = cid != NONE
    lut = np.array([r["n"] for r in list_ti] + [-1], np.int64)
    cap_n[ok] = lut[np.minimum(cid[ok], len(list_ti))]
    our_n = np.full(oid.shape, -1, np.int64)
    item_n = np.array([-1] + [it["n"] for it in items], np.int64)
    oo = oid != 0
    our_n[oo] = item_n[np.minimum(oid[oo], len(items))]
    mask = (cap_n >= 0) | (our_n >= 0)
    diff = np.abs(cap[..., :3].astype(np.int16) - ours[..., :3].astype(np.int16)).max(axis=2)
    res = {"target": ti, "size": [W, H], "frame": stats(diff[mask]),
           "captured_only": int(((cap_n >= 0) & (our_n < 0)).sum()), "ours_only": int(((our_n >= 0) & (cap_n < 0)).sum())}
    per_obj = defaultdict(list)
    per_draw = {}
    ns = sorted(set(cap_n[cap_n >= 0].tolist()) | set(our_n[our_n >= 0].tolist()))
    for n in ns:
        m = (cap_n == n) | (our_n == n)
        per_draw[n] = m
        o = obj.get(n, "")
        per_obj[o if o else "save"].append(n)
    objs = {}
    for o, lst in per_obj.items():
        m = np.zeros(mask.shape, bool)
        for n in lst:
            m |= per_draw[n]
        s = stats(diff[m])
        s["draws"] = lst
        objs[o] = s
    res["objects"] = objs
    cn, cc = np.unique(cap_n[cap_n >= 0], return_counts=True)
    res["captured_pixels_per_draw"] = {int(n): int(c) for n, c in zip(cn, cc)}   # for R1 (landmark-renderer.md 7.6)
    worst =sorted(((stats(diff[per_draw[n]])["within16"], n) for n in ns))[:10]
    res["worst_draws"] = [{"n": n, "within16": w} for w, n in worst]
    vis = np.zeros((H, W, 3), np.uint8)
    vis[mask] = np.clip(diff[mask] * 8, 0, 255).astype(np.uint8)[:, None]
    png(out / ("diff_%d.png" % ti), vis)
    png(out / ("ours_%d.png" % ti), ours[..., :3].copy())
    png(out / ("captured_%d.png" % ti), cap[..., :3].copy())
    return res


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["frame"])
    ap.add_argument("export")
    ap.add_argument("out")
    ap.add_argument("--tool", required=True)
    ap.add_argument("--replay", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--db", required=True)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--objects")
    ap.add_argument("--export-name")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--list-only", action="store_true", help="write frame.tsv and states.json only (R1's frames)")
    ap.add_argument("--inputs", choices=["capture", "install", "none"], default="capture",
                    help="PS t2-t6 / t9: the capture's (default), the server's install sources, or unbound (ticket 4882)")
    a = ap.parse_args(argv)
    root, out = Path(a.export), Path(a.out)
    if not work_path(out):
        print("cityscene.py: OUT %s is not under the work folders (game data, D83)" % out, file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    targets, tex, draws, states, env_sets, view_fmt = list_frame(root)
    if not draws:
        print("cityscene.py: no family colour draw in %s" % root, file=sys.stderr)
        return 3
    write_frame(out / "frame.tsv", targets, tex, draws)
    (out / "states.json").write_text(json.dumps({
        "states": [{"material": k[0], "pass": k[1], "differs": k[2], "draws": v} for k, v in sorted(states.items())],
        "differing_draws": [{"n": d["n"], "material": d["mat"], "differs": d["state_diff"]} for d in draws if d["state_diff"]],
        "env_sets": [{"material": k[0], "target": k[1], "distinct_t2_t4": len(v), "draws": sum(v.values())}
                     for k, v in sorted(env_sets.items())],
        "t0_t1_view_formats": [{"slot": k[0], "format": k[1], "draws": v} for k, v in sorted(view_fmt.items())]}, indent=1))
    if a.list_only:
        return 0
    r = subprocess.run([a.tool, "tap", str(out / "frame.tsv"), str(out / "frame.mtap")], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode != 0:
        return 1
    r = subprocess.run([a.tool, "run", "--game", a.game, "--db", a.db, "--exe", a.exe, "--frame", str(out / "frame.tsv"),
                        "--tap", str(out / "frame.mtap"), "--out", str(out), "--inputs", a.inputs], capture_output=True,
                       text=True)
    (out / "run.log").write_text(r.stdout + r.stderr)
    print(r.stdout.strip(), r.stderr.strip()[-2000:])
    if r.returncode != 0:
        return 1
    with open(out / "items.tsv", newline="") as f:
        items = list(csv.DictReader(f, delimiter="\t"))
    for it in items:
        it["n"] = int(it["n"])
    obj = objects_of(a.objects, a.export_name or root.name)
    with open(out / "unknown.tsv", newline="") as f:
        unknown = {int(u["n"]) for u in csv.DictReader(f, delimiter="\t")}
    result = {"export": str(root), "inputs": a.inputs, "draws": len(draws), "unknown_draws": sorted(unknown), "targets": [],
              "run": r.stdout.splitlines()}
    for ti, t in enumerate(targets):
        # The reference: the captured draws the compositor would drop, i.e. those the library holds (an unknown key
        # is outside the identity set and stays the EXE's, city-renderer.md 3.2): replayed in frame order.
        list_ti = [d for d in draws if d["target"] == ti and d["n"] not in unknown]
        if not list_ti:
            continue
        ref = out / ("ref_%d" % ti)
        rr = subprocess.run([sys.executable, "-I", str(UNIT_MANIFEST), str(root), str(ref), "--draws",
                             ",".join(str(d["n"]) for d in list_ti), "--exe", a.replay], capture_output=True, text=True)
        if rr.returncode != 0:
            print("reference replay of target %d failed: %s %s" % (ti, rr.stdout[-1000:], rr.stderr[-1000:]), file=sys.stderr)
            return 1
        items_ti = [it for it in items]   # ours' id buffer holds 1 + the scene's item index over all targets
        res = compare(out, ti, t["w"], t["h"], ref, list_ti, items_ti, obj)
        res["res"] = t["res"]
        result["targets"].append(res)
        if not a.keep:
            for p in [out / ("ours_%d.rgba" % ti), out / ("ours_%d_id.u32" % ti), ref / "captured.rgba",
                      ref / "captured_id.u32", ref / "captured_depth.f32"]:
                if p.exists():
                    p.unlink()
    (out / "result.json").write_text(json.dumps(result, indent=1))
    for t in result["targets"]:
        f = t["frame"]
        print("target %d (res %s) %dx%d: pixels %d median %.1f p95 %.1f max %d within16 %.4f captured-only %d ours-only %d"
              % (t["target"], t["res"], t["size"][0], t["size"][1], f.get("pixels", 0), f.get("median", 0), f.get("p95", 0),
                 f.get("max", 0), f.get("within16", 0), t["captured_only"], t["ours_only"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
