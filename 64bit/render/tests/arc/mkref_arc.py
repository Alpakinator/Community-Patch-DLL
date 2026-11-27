"""Turn 0179's recorded arc into the plain files that c5r_arc_arc and c5r_arc_picture read (ticket 0199;
research/05-graphics-composition/arrows-and-path-line.md 12):
  - 3801's stock capture frame2846, draws 475-476 (the arc's two draws), exported with
    `cli.py rdc-export CAPTURE OUT --xml-dir X --select draws:475-476`;
  - with --stock3804, task 3804's Tiny stock arm (D:/c5-64bit-work/0179/3804/captures/tiny/stock): the map's grid and
    height field (the same save copy as the capture), and the arc cameras a1 and a2 with their stock pictures (on twice,
    off with the camera put back).

    python -I render/tests/arc/mkref_arc.py <rdc-export dir> <out dir> [--stock3804 DIR]

Output (game data, never committed, D83; keep it on F:/c5-64bit-work/0199/ref):
  arc.txt      records, one per line:
                 start X Y Z            the capture's start point: the centre of ring 0 ((v0 + v4) / 2)
                 end X Y Z              the end point: x, y by least squares over the 24 other ring centres through the
                                        curve's weights (z fitted too and reported as zfit; 20 = target_height)
                 zfit Z | fitrms R      the fit's own z and the rms of the ring centres against the fitted curve
                 plots X0 Y0 X1 Y1      the capture's attacker and target plots (3801: archer (50, 14), target (49, 16))
                 counts V I             the vertices the indices use and the index count of the draws
                 viewport W H
                 pass N <19 numbers>    blendEnable src dst op srcA dstA opA mask depthEnable depthWrite depthFunc
                                        stencil fill cull frontCCW depthClip scissor multisample topology (D3D11 values)
                 vs SHA256 | ps SHA256  the captured ArrowShader blobs
               with --stock3804:
                 grid SX SY MAPW MAPH WRAP sx sy sz ox oy   the height field's layout (heights.u16: SX x SY u16 rows)
                 h2w <16 floats>        M_h2w, row-major
                 pics W H               the size of every picture
                 cam NAME X0 Y0 X1 Y1 OFFDIFF <16 floats>   a camera: the pair's plots, max |viewProj(on) - viewProj(off)|,
                                        and viewProj = views[0] x proj of the on shot (row-major, row vectors)
  vb.bin       V x 40 bytes; ib.bin I x 4 bytes; cb.bin 96 bytes (the draws' constant buffer)
  heights.u16  (--stock3804) the height field
  NAME_on.rgba, NAME_on2.rgba, NAME_off.rgba  (--stock3804) the stock pictures as raw RGBA8 rows
  manifest.txt SHA-256 of every file
Exit 1 when a check fails (the two draws differ in buffers or constants, no arc-sized draw, a picture missing or of
another size, a camera or grid file missing).
"""
import argparse
import hashlib
import json
import math
import shutil
import struct
import sys
from pathlib import Path

CAPTURE_PLOTS = (50, 14, 49, 16)   # 3801's archer and barbarian (arrows-and-path-line.md 2)
CAMS = ("a1", "a2")


def die(msg):
    sys.exit("mkref_arc: " + msg)


def ev(x):
    return x["v"] if isinstance(x, dict) else int(x)


def weights(s):
    u = 2.0 * s
    f = u - math.floor(u)
    if u < 1:
        return ((1 - f) ** 2, 2 * f - 1.5 * f * f, 0.5 * f * f, 0.0)
    return (0.0, 0.5 * (1 - f) ** 2, 0.5 + f - 1.5 * f * f, f * f)


def fit_end(cen, arc_height=75.0):
    """P3 by least squares: centre(s) = a P0 + b P3 + (0, 0, arc (w1 + w2)), a = w0 + .75 w1 + .25 w2, b = .25 w1 + .75 w2
    + w3 (the control points of arrows-and-movement-path.md 1.6)."""
    p0 = cen[0]
    num, den = [0.0, 0.0, 0.0], 0.0
    rows = []
    for r in range(1, len(cen)):
        w = weights(0.04 * r)
        a = w[0] + 0.75 * w[1] + 0.25 * w[2]
        b = 0.25 * w[1] + 0.75 * w[2] + w[3]
        rhs = [cen[r][k] - a * p0[k] - (arc_height * (w[1] + w[2]) if k == 2 else 0.0) for k in range(3)]
        for k in range(3):
            num[k] += b * rhs[k]
        den += b * b
        rows.append((a, b, w))
    p3 = [num[k] / den for k in range(3)]
    err = 0.0
    for r, (a, b, w) in enumerate(rows, 1):
        for k in range(3):
            e = a * p0[k] + b * p3[k] + (arc_height * (w[1] + w[2]) if k == 2 else 0.0) - cen[r][k]
            err += e * e
    return p3, math.sqrt(err / (3 * len(rows)))


def pass_numbers(st):
    rt = st["om"]["blend"]["desc"]["RenderTarget"][0]
    d = st["om"]["depth"]["desc"]
    r = st["rs"]["state"]["desc"]
    return [int(rt["BlendEnable"]), ev(rt["SrcBlend"]), ev(rt["DestBlend"]), ev(rt["BlendOp"]), ev(rt["SrcBlendAlpha"]),
            ev(rt["DestBlendAlpha"]), ev(rt["BlendOpAlpha"]), ev(rt["RenderTargetWriteMask"]), int(d["DepthEnable"]),
            ev(d["DepthWriteMask"]), ev(d["DepthFunc"]), int(d["StencilEnable"]), ev(r["FillMode"]), ev(r["CullMode"]),
            int(r["FrontCounterClockwise"]), int(r["DepthClipEnable"]), int(r["ScissorEnable"]),
            int(r["MultisampleEnable"]), ev(st["ia"]["topology"])]


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def view_proj(cam_json):
    j = json.loads(Path(cam_json).read_text())
    return matmul(j["views"][0], j["proj"])


def add_3804(src, out, lines, files):
    from PIL import Image
    g = json.loads((src / "grid.json").read_text())
    hdr = g["heightmap_header"]
    sx_, sy_ = hdr["samples_x_0C"], hdr["samples_y_10"]
    raw = src / "heights" / ("heightmap_u16le_%dx%d.raw" % (sx_, sy_))
    if not raw.exists() or raw.stat().st_size != sx_ * sy_ * 2:
        die("height field missing or of another size: %s" % raw)
    plots = json.loads((src / "plots.json").read_text())
    shutil.copyfile(raw, out / "heights.u16")
    files.append("heights.u16")
    lines.append("grid %d %d %d %d %d %.9g %.9g %.9g %.9g %.9g" % (sx_, sy_, plots["w"], sy_ // 64, int(g["wrap_x"]), g["grid_sx"],
                                                                g["grid_sy"], g["grid_height_scale"], g["grid_ox"], g["grid_oy"]))
    lines.append("h2w " + " ".join("%.9g" % v for row in g["M_h2w"] for v in row))
    cams = {c["cam"]: c for c in json.loads((src / "cameras.json").read_text())["cams"]}
    size = None
    for name in CAMS:
        c = cams.get(name)
        if not c:
            die("camera %s not in %s/cameras.json" % (name, src))
        vp_on = view_proj(src / ("%s_camera_on.json" % name))
        vp_off = view_proj(src / ("%s_camera_offback.json" % name))
        diff = max(abs(vp_on[i][j] - vp_off[i][j]) for i in range(4) for j in range(4))
        for pic, dst in (("%s__on_stock_1.png", "%s_on.rgba"), ("%s__on_stock_2.png", "%s_on2.rgba"), ("%s__off_stock_1.png", "%s_off.rgba")):
            p = src / (pic % name)
            if not p.exists():
                die("stock picture missing: %s" % p)
            im = Image.open(p).convert("RGBA")
            if size is None:
                size = im.size
            elif im.size != size:
                die("%s is %s, not %s" % (p, im.size, size))
            (out / (dst % name)).write_bytes(im.tobytes())
            files.append(dst % name)
        x0, y0 = c["unit"][1], c["unit"][2]
        x1, y1 = c["target"]
        lines.append("cam %s %d %d %d %d %.6g %s" % (name, x0, y0, x1, y1, diff, " ".join("%.9g" % v for row in vp_on for v in row)))
    lines.append("pics %d %d" % size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("export")
    ap.add_argument("out")
    ap.add_argument("--stock3804", help="3804's Tiny stock folder (D:/c5-64bit-work/0179/3804/captures/tiny/stock)")
    a = ap.parse_args()
    exp, out = Path(a.export), Path(a.out)
    if not out.resolve().as_posix().lower().startswith(("c:/c5-64bit-work/", "f:/c5-64bit-work/")):
        die("out must be under C:/ or F:/c5-64bit-work/ (D83, D133)")
    draws = sorted(p for p in (exp / "draws").iterdir() if (p / "state.json").exists())
    if len(draws) != 2:
        die("want the two arc draws in %s, found %d" % (exp, len(draws)))
    st = [json.loads((d / "state.json").read_text()) for d in draws]
    f0 = [st[0]["ia"]["vb"]["0"]["data"]["file"], st[0]["ia"]["ib"]["data"]["file"], st[0]["VS"]["cb"]["1"]["data"]["file"]]
    f1 = [st[1]["ia"]["vb"]["0"]["data"]["file"], st[1]["ia"]["ib"]["data"]["file"], st[1]["VS"]["cb"]["1"]["data"]["file"]]
    if f0 != f1:
        die("the two draws bind different buffers: %s / %s" % (f0, f1))
    if st[0]["PS"]["cb"]["1"]["data"]["file"] != f0[2]:
        die("VS and PS constant buffers differ")
    nidx = st[0]["args"]["IndexCountPerInstance"]
    if nidx != st[1]["args"]["IndexCountPerInstance"] or nidx < 108 or nidx % 3:
        die("not the arc's draws (index counts %d, %d)" % (nidx, st[1]["args"]["IndexCountPerInstance"]))
    ib = (exp / f0[1]).read_bytes()[:nidx * 4]
    idx = struct.unpack("<%dI" % nidx, ib)
    nv = max(idx) + 1
    vb = (exp / f0[0]).read_bytes()[:nv * 40]
    cb = (exp / f0[2]).read_bytes()
    if len(cb) != 96:
        die("constant buffer of %d bytes, not 96" % len(cb))
    rings = 0
    while rings * 16 + 68 < nv:
        rings += 1
    if rings * 16 + 68 != nv:
        die("%d vertices is not 16 x rings + 68" % nv)
    pos = [struct.unpack_from("<3f", vb, i * 40) for i in range(nv)]
    cen = [tuple((pos[16 * r][k] + pos[16 * r + 4][k]) / 2 for k in range(3)) for r in range(rings)]
    p3, rms = fit_end(cen)
    out.mkdir(parents=True, exist_ok=True)
    lines = ["start %.9g %.9g %.9g" % cen[0], "end %.9g %.9g 20" % (p3[0], p3[1]), "zfit %.9g" % p3[2],
             "fitrms %.9g" % rms, "plots %d %d %d %d" % CAPTURE_PLOTS, "counts %d %d" % (nv, nidx)]
    vp = st[0]["rs"]["viewports"][0]
    lines.append("viewport %d %d" % (int(vp["Width"]), int(vp["Height"])))
    for n, s in enumerate(st):
        lines.append("pass %d %s" % (n, " ".join(str(x) for x in pass_numbers(s))))
    lines.append("vs %s" % st[0]["VS"]["shader"]["sha256"])
    lines.append("ps %s" % st[0]["PS"]["shader"]["sha256"])
    (out / "vb.bin").write_bytes(vb)
    (out / "ib.bin").write_bytes(ib)
    (out / "cb.bin").write_bytes(cb)
    files = ["vb.bin", "ib.bin", "cb.bin"]
    if a.stock3804:
        add_3804(Path(a.stock3804), out, lines, files)
    (out / "arc.txt").write_text("\n".join(lines) + "\n")
    files.append("arc.txt")
    man = ["%s %s" % (hashlib.sha256((out / f).read_bytes()).hexdigest(), f) for f in files]
    (out / "manifest.txt").write_text("\n".join(man) + "\n")
    print("\n".join(ln if len(ln) < 160 else ln[:160] + " ..." for ln in lines))
    print("wrote %d files into %s" % (len(files) + 1, out))


if __name__ == "__main__":
    main()
