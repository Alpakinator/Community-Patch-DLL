"""Issue 3405: extract every terrain bake's two CPU-made grids from a RenderDoc XML+ZIP export (renderdoccmd convert
-c zip.xml, tools/harness/rdc.convert) for the offline test of render/src/bakegrid.

    python -I render/tests/bakegrid/tools/grid_ref.py <capture>.xml [<capture>.xml ...] --out <dir under D:/c5-64bit-work>
           [--tap <C5HF file of the same session>]

A bake's blend draw is a DrawIndexedInstanced with three render targets and a 48-byte vertex stride; its CacheBake draw
is the next DrawIndexedInstanced with a 24-byte stride and the same vertex count (any context) (terrain-bake-passes.md 2). For each pair, <out>/<capture
tag>_c<chunk of the blend draw>/ receives grid48.bin (the (w+2)(h+2) blend vertices) and grid24.bin (the CacheBake
vertices: POSITION f32x3, NORMAL f16x4, TEXCOORD0 f16x2), both as the draws read them. --tap copies the session's
height-field tap to <out>/tap.c5hf (the test computes the normals from it). Everything written is game data and stays
on D (D83); the tool refuses other output folders. Exit 0 with at least one pair, 1 none found, 2 usage.
"""
from __future__ import annotations

import argparse
import math
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


def named(el, name):
    for c in el:
        if c.get("name") == name:
            return c
    return None


def val(c):
    if c is None:
        return None
    if c.tag in ("uint", "int", "enum", "ResourceId", "buffer"):
        return int(c.text)
    return c.text


def ids_of(arr):
    return [int(x.text) for x in arr] if arr is not None else []


def struct_dict(el):
    return {c.get("name"): (struct_dict(c) if c.tag == "struct" else val(c)) for c in el}


def extract(xmlp: Path, out: Path) -> int:
    zf = zipfile.ZipFile(str(xmlp.with_suffix("")))
    blob = lambda i: zf.read("%06d" % i)
    tag = xmlp.stem.split("_")[-1]
    res, content, ctxs = {}, {}, {}
    pending = []  # (folder, grid vertex count) of blend draws waiting for their CacheBake draw, in chunk order (each pass
    #               is recorded on its own deferred context, so the pair is matched by order, not by context)
    pairs = 0

    def buf(rid):
        if rid in content:
            return content[rid]
        r = res.get(rid)
        if r is not None and r.get("data") is not None:
            content[rid] = bytearray(blob(r["data"]))
            return content[rid]
        return None

    for _, ch in ET.iterparse(str(xmlp), events=("end",)):
        if ch.tag != "chunk":
            continue
        name = ch.get("name")
        ci = int(ch.get("chunkIndex"))
        if name == "ID3D11Device::CreateBuffer":
            d = struct_dict(named(ch, "pDesc"))
            rid = val(named(ch, "pBuffer"))
            res[rid] = {"desc": d, "data": val(named(ch, "InitialData"))}
            content.pop(rid, None)
        elif name == "Internal::Initial Contents":
            rid = val(named(ch, "id"))
            subs = [val(x) for x in ch if x.get("name") == "SubresourceContents"]
            if rid in res and subs:
                content[rid] = bytearray(blob(subs[0]))
        elif name.startswith("ID3D11DeviceContext::"):
            ctx = val(named(ch, "Context"))
            s = ctxs.setdefault(ctx, {"rtv": [], "vb": None})
            op = name.split("::")[1]
            if op == "OMSetRenderTargets":
                s["rtv"] = [v for v in ids_of(named(ch, "ppRenderTargetViews")) if v]
            elif op == "IASetVertexBuffers" and val(named(ch, "StartSlot")) == 0:
                s["vb"] = (ids_of(named(ch, "ppVertexBuffers"))[0], ids_of(named(ch, "pStrides"))[0],
                           ids_of(named(ch, "pOffsets"))[0])
            elif op == "Unmap":
                rid = val(named(ch, "pResource"))
                data = named(ch, "MapWrittenData")
                if data is not None and rid in res:
                    cur = buf(rid)
                    cur = cur if cur is not None else bytearray(int(res[rid]["desc"]["ByteWidth"]))
                    o = val(named(ch, "Byte offset to start of written data")) or 0
                    w = blob(val(data))
                    cur[o:o + len(w)] = w
                    content[rid] = cur
            elif op == "DrawIndexedInstanced" and s["vb"] and s["vb"][1] in (48, 24):
                stride = s["vb"][1]
                n_idx = val(named(ch, "IndexCountPerInstance"))
                q = int(round(math.sqrt(n_idx / 6)))
                nv = (q + 1) * (q + 1)
                base = val(named(ch, "BaseVertexLocation"))
                data = buf(s["vb"][0])
                if data is None:
                    continue
                o = s["vb"][2] + base * stride
                if stride == 48 and len(s["rtv"]) == 3:
                    d = out / ("%s_c%d" % (tag, ci))
                    d.mkdir(parents=True, exist_ok=True)
                    (d / "grid48.bin").write_bytes(bytes(data[o:o + nv * 48]))
                    pending.append((d, nv))
                elif stride == 24 and pending and pending[0][1] == nv:
                    d, _ = pending.pop(0)
                    (d / "grid24.bin").write_bytes(bytes(data[o:o + nv * 24]))
                    pairs += 1
                    print("pair %s (%d vertices)" % (d.name, nv))
        ch.clear()
    for d, _ in pending:
        print("blend draw without a CacheBake draw: %s (removed)" % d.name)
        shutil.rmtree(d)
    return pairs


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("xml", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--tap", default=None)
    a = ap.parse_args(argv)
    out = Path(a.out)
    if not str(out.resolve()).replace("\\", "/").lower().startswith("d:/c5-64bit-work/"):
        print("refused: --out must be under D:/c5-64bit-work (game data)", file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    if a.tap:
        shutil.copyfile(a.tap, out / "tap.c5hf")
    total = 0
    for x in a.xml:
        total += extract(Path(x), out)
    print("%d bake(s) extracted to %s" % (total, out))
    return 0 if total else 1


if __name__ == "__main__":
    sys.exit(main())
