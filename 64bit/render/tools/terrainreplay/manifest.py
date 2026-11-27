"""Issue 0182 (from 3019): write a terrainreplay.exe manifest for the terrain draws of an export.py export, optionally
with the server's pixel-shader inputs swapped in, run the replay, and save the outputs.

    python -I render/tools/terrainreplay/manifest.py EXPORT_DIR OUT_DIR --exe BUILD/tools/terrainreplay/terrainreplay.exe
           --variant NAME [--variant NAME ...] [--viewproj CAM.json] [--target-res 2380]

Origin: this repository, render/evidence/3019/replay_manifest.py at commit 1234c563; 0182 changed the replayer's name,
made --exe required, refuses an OUT_DIR outside the work folders (D83) and returns 3 for a missing export.
`--no-run` (issue 0302, used by `tools/harness/cli.py rdc-export --manifest`): write the manifests only; no --exe needed.
Buffer SRVs (issue 0347; a `--select all` export holds them, e.g. the bone palette t27): a BUFFER view of a format the
replayer's `buf` directive takes becomes a `buf` line; any other (structured, raw, BUFFEREX, no view, past the file's
end) is skipped with a warning on stderr naming draw, stage and slot, once per SRV, and listed in replay.json's
`skipped_srvs` (written only when one is skipped). Exports without buffer SRVs give byte-equal outputs as before.
Exit codes: 0 every variant replayed (or, with --no-run, every manifest written); 1 a replay failed (its stderr is in replay.json); 2 usage; 3 export missing or
unreadable; 4 OUT_DIR not under C:/c5-64bit-work/, D:/c5-64bit-work/ or F:/c5-64bit-work/.
The overlay: the `exe` variant binds the export's t1, i.e. the target's contents at the captured frame's start (the
previous frame's finished overlay, `research/05-graphics-composition/overlay-system.md` 3); `nooverlay` binds a
transparent t1. Pixels where the two differ are the overlay's pixels (compare.py).

Variants (each a separate replay of the same draws, the main scene's terrain draws in frame order):
  exe        the draws as captured (the EXE's terrain pass: caches, overlay, shadow, fog of war, distance fog, spec)
  noshadow   TerrainFrame.Arg6 = 0 (the server's value: no shadow)
  nooverlay  t1 = (0,0,0,0) (the server's: no overlay layer: borders, selection, grid)
  nofow      t4 = (1,1,1,1) and t5 black (the server's: every plot visible, no TransFOW LUT)
  nofog      DistanceFog.FogParams = 0 (the server's: no distance fog)
  pass1      every draw with the pass-1 pixel shader (the server's default `render_pass` -1 -> 1), which reads no t8/t9
  server     all of the above, and t2/t3/t8/t9 white, t6/t7 identity LUTs, as `render/src/terrain/terrain.cpp` binds
  server_p0  as server but keeping the EXE's pass choice and its t8/t9 (what the server would draw with pass 0)
  lod0       TerrainTile.LodMip = 0 (the server's: the height page's mip 0; the EXE picks it per frame)
  server_lod0  server and lod0: every input the server sets, with the EXE's caches
Outputs per variant: <variant>.rgba (raw RGBA8), <variant>.png, and for `exe` also depth.f32 and id.u32 (index into
draws.txt). Game data: OUT_DIR must be under C:/c5-64bit-work/ or D:/c5-64bit-work/ (D83).
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent

# TerrainFrame (cb1) and DistanceFog (cb2) members, render/src/terrainsets/sets.cpp
ARG6, FLAGC, FOGPARAMS = 0x190, 0x1C0, 0x10
LODMIP = 0x68  # TerrainTile (cb0)

BC = {70: 8, 71: 8, 72: 8, 79: 16, 80: 16, 76: 16, 77: 16, 78: 16, 73: 16, 74: 16, 75: 16, 82: 8, 83: 16}
BPP = {2: 16, 10: 8, 11: 8, 13: 8, 27: 4, 28: 4, 29: 4, 87: 4, 88: 4, 91: 4, 41: 4, 56: 2, 54: 2, 65: 1, 61: 1, 49: 2,
       35: 4, 24: 4, 85: 2, 86: 2}


WORK_ROOTS = ("C:/c5-64bit-work/", "D:/c5-64bit-work/", "F:/c5-64bit-work/")


def work_path(p: Path) -> bool:
    """True when p lies under one of the work folders (game data never goes anywhere else, D83)."""
    s = p.resolve().as_posix().lower() + "/"
    return any(s.startswith(r.lower()) for r in WORK_ROOTS)


def ev(x):
    return x["v"] if isinstance(x, dict) and "v" in x else x


# Issue 0347: buffer SRVs (a `--select all` export holds them, e.g. the unit draws' bone palette t27). The formats the
# replayer's `buf` directive takes (terrainreplay.cpp elemBytes(), issue 4152): DXGI format -> bytes per element.
BUF_FMT = {2: 16, 3: 16, 4: 16, 16: 8, 17: 8, 10: 8, 41: 4, 42: 4, 43: 4, 28: 4}
SRV_DIM_BUFFER, SRV_DIM_BUFFEREX = 1, 11  # D3D11_SRV_DIMENSION
SRV_DIM_TEXTURECUBE, SRV_DIM_TEXTURECUBEARRAY = 9, 10  # issue 4355: written as the replayer's DIM 4


def is_buffer_srv(data, view_desc) -> bool:
    """True when the SRV views a buffer: its view is BUFFER / BUFFEREX, or its resource has a ByteWidth (a buffer's
    desc) and no Width (a texture's)."""
    if ev((view_desc or {}).get("ViewDimension")) in (SRV_DIM_BUFFER, SRV_DIM_BUFFEREX):
        return True
    d = data.get("desc") or {}
    return data.get("kind") == "buffer" or ("ByteWidth" in d and "Width" not in d)


def buf_line(root: Path, stage, slot, data, view_desc):
    """(line, None) with the `buf` directive of a buffer SRV, or (None, reason) when the replayer cannot bind it."""
    vd = view_desc or {}
    dim, fmt = ev(vd.get("ViewDimension")), ev(vd.get("Format"))
    if dim != SRV_DIM_BUFFER:
        return None, "view dimension %s is not BUFFER (1)" % dim
    if fmt not in BUF_FMT:
        return None, "format %s not handled by the buf directive (4, 8 or 16 bytes per element)" % fmt
    b = vd.get("Buffer") or {}
    first = b.get("FirstElement", b.get("ElementOffset"))
    num = b.get("NumElements", b.get("ElementWidth"))
    if not isinstance(first, int) or not isinstance(num, int) or num <= 0:
        return None, "no element range in the view (Buffer %s)" % b
    size = data.get("size")
    if isinstance(size, int) and (first + num) * BUF_FMT[fmt] > size:
        return None, "the view's elements %d..%d pass the file's %d bytes" % (first, first + num - 1, size)
    return "buf %s %d %s %d %d %d" % (stage, slot, (root / data["file"]).as_posix(), fmt, first, num), None


def tex_line(root: Path, stage, slot, data, view_desc, files_dir: Path):
    d = data.get("desc") or {}
    w, h = d.get("Width", 1), d.get("Height", 1)
    dep = d.get("Depth") or 1
    mips = d.get("MipLevels") or 1
    arr = d.get("ArraySize") or 1
    fmt = ev(d.get("Format"))
    vfmt = ev((view_desc or {}).get("Format")) or fmt
    dim = 3 if data.get("kind") == "texture3d" else 2
    # Issue 4355 (0347's open point, 0346): a cube view (TEXTURECUBE / TEXTURECUBEARRAY) is the replayer's DIM 4, as
    # 4152's unit_manifest.py did by hand; it was DIM 2 ARR 6 (a 2D array), so cube lookups read the wrong texels.
    if ev((view_desc or {}).get("ViewDimension")) in (SRV_DIM_TEXTURECUBE, SRV_DIM_TEXTURECUBEARRAY) and dim == 2             and arr % 6 == 0 and w == h:
        dim = 4
    sb = data.get("subresource_bytes") or []
    rp = data.get("row_pitch")
    subs, off, k = [], 0, 0
    for a in range(arr):
        for m in range(mips):
            mw, mh, md = max(1, w >> m), max(1, h >> m), max(1, dep >> m)
            if fmt in BC:
                rows, tight = (mh + 3) // 4, ((mw + 3) // 4) * BC[fmt]
            else:
                if fmt not in BPP:
                    raise SystemExit("texture format %s not handled (%s)" % (fmt, data.get("file")))
                rows, tight = mh, mw * BPP[fmt]
            pitch = rp[k] if isinstance(rp, list) and k < len(rp) and rp[k] else tight
            slice_ = pitch * rows
            subs.append((off, pitch, slice_))
            off += sb[k] if k < len(sb) else slice_ * md
            k += 1
    path = (root / data["file"]).as_posix()
    head = "tex %s %d %s %d %d %d %d %d %d %d %d %d" % (stage, slot, path, fmt, vfmt, dim, w, h, dep, mips, arr, len(subs))
    return [head] + ["sub %d %d %d" % s for s in subs]


def const_tex(files_dir: Path, name, rgba, size=1, dim=2):
    """A constant RGBA8 texture file (size^2, or size^3 for an identity LUT when rgba == 'lut')."""
    p = files_dir / ("%s.bin" % name)
    if dim == 3:
        n = size
        g = np.stack(np.meshgrid(np.arange(n), np.arange(n), np.arange(n), indexing="ij"), -1)  # z, y, x
        lut = np.zeros((n, n, n, 4), np.uint8)
        lut[..., 0] = np.round(g[..., 2] * 255.0 / (n - 1))
        lut[..., 1] = np.round(g[..., 1] * 255.0 / (n - 1))
        lut[..., 2] = np.round(g[..., 0] * 255.0 / (n - 1))
        lut[..., 3] = 255
        p.write_bytes(lut.tobytes())
        return ["tex %%s %%d %s 28 28 3 %d %d %d 1 1 1" % (p.as_posix(), n, n, n), "sub 0 %d %d" % (n * 4, n * n * 4)]
    px = np.tile(np.array(rgba, np.uint8), size * size)
    p.write_bytes(px.tobytes())
    return ["tex %%s %%d %s 28 28 2 %d %d 1 1 1 1" % (p.as_posix(), size, size), "sub 0 %d %d" % (size * 4, size * size * 4)]


def osgn(dxbc: bytes):
    """Output signature of a DXBC blob: [(name, index, sysvalue, comptype, register, mask)]."""
    n = struct.unpack_from("<I", dxbc, 28)[0]
    for i in range(n):
        off = struct.unpack_from("<I", dxbc, 32 + 4 * i)[0]
        tag = dxbc[off:off + 4]
        if tag in (b"OSGN", b"OSG5", b"OSG1"):
            base = off + 8
            cnt = struct.unpack_from("<I", dxbc, base)[0]
            esz = 24 if tag == b"OSGN" else 28
            first = 8 if tag == b"OSGN" else 8
            out = []
            for e in range(cnt):
                eo = base + first + e * esz + (4 if tag != b"OSGN" else 0)
                no, si, sv, ct, reg = struct.unpack_from("<5I", dxbc, eo)
                mask = dxbc[eo + 20]
                name = dxbc[base + no:dxbc.index(b"\0", base + no)].decode()
                out.append((name, si, sv, ct, reg, mask))
            return out
    raise SystemExit("no output signature")


def id_ps(sig):
    """HLSL of a PS whose input mirrors `sig` and that returns the draw index."""
    lines = ["cbuffer IdCb : register(b13) { uint g_id; };", "struct In {"]
    for i, (name, si, sv, ct, reg, mask) in enumerate(sig):
        lo = (mask & -mask).bit_length() - 1
        hi = mask.bit_length()
        width = hi - lo
        typ = {1: "uint", 2: "int", 3: "float"}.get(ct, "float")
        t = typ + (str(width) if width > 1 else "")
        sem = name if name.upper().startswith("SV_") else "%s%d" % (name, si)
        interp = "nointerpolation " if typ != "float" else ""
        lines.append("  %s%s v%d : %s;" % (interp, t, i, sem))
    lines += ["};", "uint main(In i) : SV_Target { return g_id; }"]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("export")
    ap.add_argument("out")
    ap.add_argument("--variant", action="append", default=[])
    ap.add_argument("--exe", help="terrainreplay.exe of a render build (render/build.py --target terrainreplay)")
    ap.add_argument("--no-run", action="store_true", help="issue 0302 (cli.py rdc-export): write the manifests only, "
                    "run no replay (then --exe is not needed)")
    ap.add_argument("--target-res", type=int, default=None, help="the scene target's resource id (default: the target of "
                    "the most terrain draws)")
    ap.add_argument("--viewproj", help="camera JSON (views[0], proj) whose View x Proj replaces TerrainFrame.ViewProj")
    a = ap.parse_args()
    root, out = Path(a.export), Path(a.out)
    if not work_path(out):
        print("manifest.py: OUT_DIR %s is not under %s (the replay is game data, D83)" % (out, " or ".join(WORK_ROOTS)),
              file=sys.stderr)
        return 4
    try:
        idx = json.loads((root / "draws.json").read_text())
        states = []
        for d in idx["draws"]:
            st = json.loads((root / d["dir"] / "state.json").read_text())
            rts = [r.get("res") for r in st["om"]["rtv"]]
            states.append((d, st, rts[0] if rts else None))
    except (OSError, ValueError, KeyError) as e:
        print("manifest.py: export %s missing or unreadable: %s" % (root, e), file=sys.stderr)
        return 3
    if not states:
        print("manifest.py: export %s has no draws" % root, file=sys.stderr)
        return 3
    out.mkdir(parents=True, exist_ok=True)
    files = out / "files"
    files.mkdir(exist_ok=True)
    if a.target_res is None:
        from collections import Counter
        a.target_res = Counter(r for _, _, r in states).most_common(1)[0][0]
    scene = [(d, st) for d, st, r in states if r == a.target_res]
    if not scene:
        print("manifest.py: no draw of the export renders into resource %s" % a.target_res, file=sys.stderr)
        return 3
    if not a.no_run and not (a.exe and Path(a.exe).is_file()):
        print("manifest.py: --exe %s not found (or pass --no-run)" % a.exe, file=sys.stderr)
        return 2
    dsvd = (scene[0][1]["om"]["dsv"] or {}).get("res_desc") or {}
    W, H = int(scene[0][1]["rs"]["viewports"][0]["Width"]), int(scene[0][1]["rs"]["viewports"][0]["Height"])
    # pass-1 PS of the capture (any m73p1 draw), for the pass1 / server variants
    p1 = [st for d, st in scene if d["system"] == "m73p1"]
    p1_ps = (root / p1[0]["PS"]["shader"]["file"]).as_posix() if p1 else None
    (out / "draws.txt").write_text("\n".join("%d %d %s" % (i, d["n"], d["system"]) for i, (d, _) in enumerate(scene)) + "\n")
    # the id PS mirrors the DS (or VS) output
    last = scene[0][1]
    src_stage = "DS" if last["DS"]["shader"] else "VS"
    sig = osgn((root / last[src_stage]["shader"]["file"]).read_bytes())
    (out / "id_ps.hlsl").write_text(id_ps(sig))
    newvp = None
    if a.viewproj:
        cam = json.loads(Path(a.viewproj).read_text())
        newvp = (np.array(cam["views"][0], np.float64).reshape(4, 4) @ np.array(cam["proj"], np.float64).reshape(4, 4)).astype(np.float32)
    white = const_tex(files, "white", (255, 255, 255, 255))
    clear = const_tex(files, "clear", (0, 0, 0, 0))
    black = const_tex(files, "black", (0, 0, 0, 255))
    lut = const_tex(files, "lut", None, 32, 3)
    summary = {"export": str(root), "target_res": a.target_res, "draws": len(scene), "size": [W, H],
               "dsv": dsvd.get("Width"), "pass1_ps": p1_ps, "variants": {},
               "viewproj": newvp.astype(float).tolist() if newvp is not None else None, "viewproj_from": a.viewproj}
    skipped = {}  # (draw, stage, slot) -> why: buffer SRVs the replayer cannot bind (issue 0347), warned once
    for var in a.variant or ["exe"]:
        lines = ["target %d %d 1 0 0 0 0" % (W, H), "out_color %s" % (out / ("%s.rgba" % var)).as_posix()]
        if var == "exe":
            lines += ["out_depth %s" % (out / "depth.f32").as_posix(), "out_id %s" % (out / "id.u32").as_posix(),
                      "id_ps %s" % (out / "id_ps.hlsl").as_posix()]
        for i, (d, st) in enumerate(scene):
            lines.append("draw %d" % d["n"])
            ia = st["ia"]
            lines.append("topo %d" % ev(ia["topology"]))
            for e in ia.get("layout") or []:
                lines.append("elem %s %d %d %d %d %d %d" % (e["SemanticName"], e["SemanticIndex"], ev(e["Format"]),
                                                            e["InputSlot"], e["AlignedByteOffset"], ev(e["InputSlotClass"]),
                                                            e["InstanceDataStepRate"]))
            for slot, v in (ia.get("vb") or {}).items():
                if (v.get("data") or {}).get("file"):
                    lines.append("vb %s %d %d %s" % (slot, v["stride"], v["offset"], (root / v["data"]["file"]).as_posix()))
            if ia.get("ib") and (ia["ib"].get("data") or {}).get("file"):
                lines.append("ib %d %d %s" % (ev(ia["ib"]["format"]), ia["ib"]["offset"] or 0, (root / ia["ib"]["data"]["file"]).as_posix()))
            ar = st["args"]
            if "IndexCountPerInstance" in ar or "IndexCount" in ar:
                lines.append("call 1 %d %d %d" % (ar.get("IndexCountPerInstance", ar.get("IndexCount")),
                                                  ar.get("StartIndexLocation", 0), ar.get("BaseVertexLocation", 0)))
            else:
                lines.append("call 0 %d %d 0" % (ar.get("VertexCountPerInstance", ar.get("VertexCount")), ar.get("StartVertexLocation", 0)))
            server = var in ("server", "server_p0", "server_lod0")
            for stage in ("VS", "HS", "DS", "GS", "PS"):
                s = st[stage]
                if not s["shader"]:
                    continue
                path = (root / s["shader"]["file"]).as_posix()
                if stage == "PS" and var in ("pass1", "server", "server_lod0") and p1_ps:
                    path = p1_ps
                lines.append("shader %s %s" % (stage, path))
                for slot, c in s["cb"].items():
                    f = (c.get("data") or {}).get("file")
                    if not f:
                        continue
                    b = bytearray((root / f).read_bytes())
                    changed = False
                    if int(slot) == 0 and len(b) >= LODMIP + 4 and var in ("lod0", "server_lod0"):
                        b[LODMIP:LODMIP + 4] = struct.pack("<f", 0.0)
                        changed = True
                    if int(slot) == 1 and len(b) >= 0x200:
                        if newvp is not None:
                            b[0:64] = newvp.tobytes()
                            changed = True
                        if var in ("noshadow", "server", "server_p0", "server_lod0"):
                            b[ARG6:ARG6 + 4] = struct.pack("<f", 0.0)
                            changed = True
                    if int(slot) == 2 and len(b) >= 32 and var in ("nofog", "server", "server_p0", "server_lod0"):
                        b[FOGPARAMS:FOGPARAMS + 16] = bytes(16)
                        changed = True
                    if changed:
                        p = files / ("%s_%s_%d_%s_cb%s.bin" % (var, stage, d["n"], Path(f).stem, slot))
                        p.write_bytes(bytes(b))
                        lines.append("cb %s %s %s" % (stage, slot, p.as_posix()))
                    else:
                        lines.append("cb %s %s %s" % (stage, slot, (root / f).as_posix()))
                for slot, v in s["srv"].items():
                    k = int(slot)
                    rep = None
                    if stage == "PS":
                        if var == "nooverlay" and k == 1:
                            rep = clear
                        if var == "nofow" and k == 4:
                            rep = white
                        if var == "nofow" and k == 5:
                            rep = black
                        if server:
                            rep = {1: clear, 2: white, 3: white, 4: white, 5: black, 6: lut, 7: lut}.get(k, rep)
                            if var in ("server", "server_lod0") and k in (8, 9):
                                rep = white
                    if rep is not None:
                        lines.append(rep[0] % (stage, k))
                        lines += rep[1:]
                        continue
                    data = v.get("data") or {}
                    if not data.get("file"):
                        continue
                    if is_buffer_srv(data, v.get("view_desc")):
                        bl_, why = buf_line(root, stage, k, data, v.get("view_desc"))
                        if bl_:
                            lines.append(bl_)
                        elif (d["n"], stage, k) not in skipped:
                            skipped[(d["n"], stage, k)] = why
                            print("manifest.py: warning: draw %d %s t%d: buffer SRV %s skipped: %s" % (
                                d["n"], stage, k, data["file"], why), file=sys.stderr)
                        continue
                    lines += tex_line(root, stage, k, data, v.get("view_desc"), files)
                for slot, sp in s["samp"].items():
                    sd = sp.get("desc")
                    if not sd:
                        continue
                    bc = sd.get("BorderColor") or [0, 0, 0, 0]
                    if isinstance(bc, dict):
                        bc = list(bc.values())
                    lines.append("samp %s %s %d %d %d %d %r %d %d %r %r %r %r %r %r" % (
                        stage, slot, ev(sd["Filter"]), ev(sd["AddressU"]), ev(sd["AddressV"]), ev(sd["AddressW"]),
                        float(sd.get("MipLODBias", 0)), sd.get("MaxAnisotropy", 1), ev(sd.get("ComparisonFunc", 1)),
                        float(bc[0]), float(bc[1]), float(bc[2]), float(bc[3]), float(sd.get("MinLOD", 0)),
                        float(sd.get("MaxLOD", 3.4e38))))
            om = st["om"]
            bl = ((om.get("blend") or {}).get("desc") or {})
            rt0 = (bl.get("RenderTarget") or [{}])[0] if bl else {}
            fac = (om.get("blend") or {}).get("factor") or [0, 0, 0, 0]
            sm = (om.get("blend") or {}).get("sample_mask")
            if bl:
                lines.append("blend %d %d %d %d %d %d %d %d %d %r %r %r %r %d" % (
                    1 if rt0.get("BlendEnable") else 0, ev(rt0["SrcBlend"]), ev(rt0["DestBlend"]), ev(rt0["BlendOp"]),
                    ev(rt0["SrcBlendAlpha"]), ev(rt0["DestBlendAlpha"]), ev(rt0["BlendOpAlpha"]), ev(rt0["RenderTargetWriteMask"]),
                    1 if bl.get("AlphaToCoverageEnable") else 0, *[float(x) for x in fac], 0xFFFFFFFF if sm is None else sm))
            ds = ((om.get("depth") or {}).get("desc") or {})
            if ds:
                ff, bf = ds.get("FrontFace") or {}, ds.get("BackFace") or {}

                def so(f):
                    return (ev(f.get("StencilFailOp", 1)), ev(f.get("StencilDepthFailOp", 1)), ev(f.get("StencilPassOp", 1)),
                            ev(f.get("StencilFunc", 8)))
                lines.append("depth %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d" % (
                    1 if ds.get("DepthEnable") else 0, ev(ds.get("DepthWriteMask", 1)), ev(ds.get("DepthFunc", 2)),
                    1 if ds.get("StencilEnable") else 0, ds.get("StencilReadMask", 255), ds.get("StencilWriteMask", 255),
                    *so(ff), *so(bf), (om.get("depth") or {}).get("stencil_ref") or 0))
            rs = ((st["rs"].get("state") or {}).get("desc") or {})
            if rs:
                lines.append("raster %d %d %d %d %r %r %d %d" % (
                    ev(rs["FillMode"]), ev(rs["CullMode"]), 1 if rs.get("FrontCounterClockwise") else 0, rs.get("DepthBias", 0),
                    float(rs.get("DepthBiasClamp", 0)), float(rs.get("SlopeScaledDepthBias", 0)),
                    1 if rs.get("DepthClipEnable", True) else 0, 1 if rs.get("MultisampleEnable") else 0))
            v0 = st["rs"]["viewports"][0]
            lines.append("viewport %r %r %r %r %r %r" % (v0["TopLeftX"], v0["TopLeftY"], v0["Width"], v0["Height"],
                                                       v0["MinDepth"], v0["MaxDepth"]))
            lines.append("end")
        mf = out / ("%s.manifest" % var)
        mf.write_text("\n".join(lines) + "\n")
        if a.no_run:
            summary["variants"][var] = {"manifest": str(mf), "run": False}
            continue
        r = subprocess.run([a.exe, str(mf)], capture_output=True, text=True)
        summary["variants"][var] = {"exit": r.returncode, "stdout": r.stdout.strip(), "stderr": r.stderr.strip()[-2000:]}
        print(var, r.returncode, r.stdout.strip(), r.stderr.strip()[-500:])
        if r.returncode == 0:
            from PIL import Image
            px = np.fromfile(out / ("%s.rgba" % var), np.uint8).reshape(H, W, 4)
            Image.fromarray(px[..., :3]).save(out / ("%s.png" % var))
    if skipped:  # only then, so that a manifest without buffer SRVs keeps its replay.json byte for byte
        summary["skipped_srvs"] = [{"draw": n, "stage": st_, "slot": k, "why": w} for (n, st_, k), w in skipped.items()]
    (out / "replay.json").write_text(json.dumps(summary, indent=1))
    return 0 if all(v.get("exit", 0) == 0 for v in summary["variants"].values()) else 1


if __name__ == "__main__":
    sys.exit(main())
