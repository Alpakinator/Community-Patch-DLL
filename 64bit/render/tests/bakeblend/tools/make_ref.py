"""Issue 3402: extract every blend-pass draw of the terrain bake from a RenderDoc XML+ZIP export (renderdoccmd convert
-c zip.xml, tools/harness/rdc.convert) into a reference folder for the offline test of render/src/bakeblend.

    python render/tests/bakeblend/tools/make_ref.py <capture>.xml <out dir under D:/c5-64bit-work> [--tag NAME]

The export's buffers are in the zip beside the XML (same name, no extension). A blend draw is a DrawIndexedInstanced
with three render targets bound and a 48-byte vertex stride. For each one, <out>/<tag>_c<chunkIndex>/ receives:
  vb.bin  the (w+2)(h+2) 48-byte vertices the draw reads      ib.bin  its 16-bit indices (as stored, relative)
  cb_vs<k>.bin  the VS constant buffers' contents at the draw  meta.json  states, views, textures, shader hashes
and <out>/textures/<rid>_<sub>.bin the subresource contents of every texture the draw reads (for the replay).
Everything written is game data and stays on D (D83). Nothing here is EXE code.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


def kids(el):
    return list(el)


def named(el, name):
    for c in el:
        if c.get("name") == name:
            return c
    return None


def allnamed(el, name):
    return [c for c in el if c.get("name") == name]


def val(c):
    if c is None:
        return None
    t = c.tag
    if t in ("uint", "int", "enum", "ResourceId", "buffer"):
        return int(c.text)
    if t == "float":
        return float(c.text)
    if t == "bool":
        return c.text == "true"
    if t == "string":
        return c.text
    return c.text


def struct_dict(el):
    d = {}
    for c in el:
        n = c.get("name")
        if c.tag in ("struct",):
            d[n] = struct_dict(c)
        elif c.tag == "array":
            d[n] = [struct_dict(x) if x.tag == "struct" else val(x) for x in c]
        else:
            d[n] = val(c) if c.tag != "enum" else (c.get("string") or val(c))
    return d


def ids_of(arr):
    return [] if arr is None else [val(x) for x in arr]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("xml")
    ap.add_argument("out")
    ap.add_argument("--tag", default=None)
    a = ap.parse_args(argv)
    out = Path(a.out)
    if not str(out.resolve()).replace("\\", "/").lower().startswith("d:/c5-64bit-work/"):
        print("refused: out must be under D:/c5-64bit-work (game data)", file=sys.stderr)
        return 2
    xmlp = Path(a.xml)
    zf = zipfile.ZipFile(str(xmlp.with_suffix("")))
    tag = a.tag or xmlp.stem.split("_")[-1]
    out.mkdir(parents=True, exist_ok=True)
    (out / "textures").mkdir(exist_ok=True)

    def blob(i):
        return zf.read("%06d" % i)

    res = {}        # rid -> dict(kind, desc, subs=[buffer idx], data idx)
    views = {}      # view rid -> (resource rid, desc)
    shaders = {}    # rid -> bytecode buffer idx
    states = {}     # rid -> desc
    layouts = {}
    content = {}    # buffer rid -> bytearray
    ctxs = {}
    found = 0

    def st(ctx):
        return ctxs.setdefault(ctx, {"rtv": [], "srv": {}, "smp": {}, "vcb": {}, "pcb": {}, "vs": 0, "ps": 0, "vb": None,
                                     "ib": None, "il": 0, "blend": 0, "ds": 0, "rs": 0, "vp": None})

    def buf_bytes(rid):
        if rid in content:
            return bytes(content[rid])
        r = res.get(rid)
        if r and r.get("data") is not None:
            content[rid] = bytearray(blob(r["data"]))
            return bytes(content[rid])
        return None

    for ev, ch in ET.iterparse(str(xmlp), events=("end",)):
        if ch.tag != "chunk":
            continue
        name = ch.get("name")
        ci = int(ch.get("chunkIndex"))
        try:
            if name == "ID3D11Device::CreateBuffer":
                d = struct_dict(named(ch, "pDesc"))
                rid = val(named(ch, "pBuffer"))
                res[rid] = {"kind": "buffer", "desc": d, "data": val(named(ch, "InitialData"))}
                content.pop(rid, None)
            elif name == "ID3D11Device::CreateTexture2D":
                d = struct_dict(named(ch, "Descriptor"))
                rid = val(named(ch, "pTexture"))
                res[rid] = {"kind": "tex2d", "desc": d, "subs": [val(x) for x in allnamed(ch, "SubresourceContents")]}
            elif name == "Internal::Initial Contents":
                rid = val(named(ch, "id"))
                subs = [val(x) for x in allnamed(ch, "SubresourceContents")]
                if rid in res:
                    if res[rid]["kind"] == "buffer":
                        if subs:
                            content[rid] = bytearray(blob(subs[0]))
                    else:
                        res[rid]["subs"] = subs
                        res[rid]["pitches"] = [val(x) for x in allnamed(ch, "RowPitch")]
            elif name in ("ID3D11Device::CreateShaderResourceView", "ID3D11Device::CreateRenderTargetView"):
                views[val(named(ch, "pView"))] = (val(named(ch, "pResource")), struct_dict(named(ch, "pDesc")))
            elif name in ("ID3D11Device::CreateVertexShader", "ID3D11Device::CreatePixelShader"):
                shaders[val(named(ch, "pShader"))] = val(named(ch, "pShaderBytecode"))
            elif name in ("ID3D11Device::CreateSamplerState", "ID3D11Device::CreateBlendState",
                          "ID3D11Device::CreateDepthStencilState", "ID3D11Device::CreateRasterizerState"):
                states[val(named(ch, "pState"))] = struct_dict(named(ch, "Descriptor"))
            elif name == "ID3D11Device::CreateInputLayout":
                layouts[val(named(ch, "pInputLayout"))] = [struct_dict(x) for x in named(ch, "pInputElementDescs")]
            elif name.startswith("ID3D11DeviceContext::"):
                ctx = val(named(ch, "Context"))
                s = st(ctx)
                op = name.split("::")[1]
                if op == "OMSetRenderTargets":
                    s["rtv"] = [v for v in ids_of(named(ch, "ppRenderTargetViews")) if v]
                elif op == "PSSetShaderResources":
                    b = val(named(ch, "StartSlot"))
                    for k, v in enumerate(ids_of(named(ch, "ppShaderResourceViews"))):
                        s["srv"][b + k] = v
                elif op == "PSSetSamplers":
                    b = val(named(ch, "StartSlot"))
                    for k, v in enumerate(ids_of(named(ch, "ppSamplers"))):
                        s["smp"][b + k] = v
                elif op in ("VSSetConstantBuffers", "PSSetConstantBuffers"):
                    key = "vcb" if op[0] == "V" else "pcb"
                    b = val(named(ch, "StartSlot"))
                    for k, v in enumerate(ids_of(named(ch, "ppConstantBuffers"))):
                        s[key][b + k] = v
                elif op == "VSSetShader":
                    s["vs"] = val(named(ch, "pShader"))
                elif op == "PSSetShader":
                    s["ps"] = val(named(ch, "pShader"))
                elif op == "IASetVertexBuffers":
                    if val(named(ch, "StartSlot")) == 0:
                        s["vb"] = (ids_of(named(ch, "ppVertexBuffers"))[0], ids_of(named(ch, "pStrides"))[0],
                                   ids_of(named(ch, "pOffsets"))[0])
                elif op == "IASetIndexBuffer":
                    s["ib"] = (val(named(ch, "pIndexBuffer")), named(ch, "Format").get("string"),
                               val(named(ch, "Offset")))
                elif op == "IASetInputLayout":
                    s["il"] = val(named(ch, "pInputLayout"))
                elif op == "OMSetBlendState":
                    s["blend"] = val(named(ch, "pBlendState"))
                elif op == "OMSetDepthStencilState":
                    s["ds"] = val(named(ch, "pDepthStencilState"))
                elif op == "RSSetState":
                    s["rs"] = val(named(ch, "pRasterizerState"))
                elif op == "RSSetViewports":
                    vps = named(ch, "pViewports")
                    s["vp"] = struct_dict(vps[0]) if vps is not None and len(vps) else None
                elif op == "Unmap":
                    rid = val(named(ch, "pResource"))
                    data = named(ch, "MapWrittenData")
                    if data is not None and rid in res and res[rid]["kind"] == "buffer":
                        cur = buf_bytes(rid)
                        cur = bytearray(cur if cur is not None else bytes(res[rid]["desc"]["ByteWidth"]))
                        o = val(named(ch, "Byte offset to start of written data")) or 0
                        w = blob(val(data))
                        cur[o:o + len(w)] = w
                        content[rid] = cur
                elif op == "UpdateSubresource":
                    rid = val(named(ch, "pDstResource"))
                    if rid in res and res[rid]["kind"] == "buffer":
                        cur = bytearray(buf_bytes(rid) or bytes(res[rid]["desc"]["ByteWidth"]))
                        box = named(ch, "pDstBox")
                        o = int(struct_dict(box)["left"]) if box is not None and box.tag == "struct" else 0
                        w = blob(val(named(ch, "pSrcData")))
                        cur[o:o + len(w)] = w
                        content[rid] = cur
                elif op == "DrawIndexedInstanced" and len(s["rtv"]) == 3 and s["vb"] and s["vb"][1] == 48:
                    found += 1
                    n_idx = val(named(ch, "IndexCountPerInstance"))
                    q = int(round(math.sqrt(n_idx / 6)))
                    nv = (q + 1) * (q + 1)
                    base = val(named(ch, "BaseVertexLocation"))
                    sidx = val(named(ch, "StartIndexLocation"))
                    d = out / ("%s_c%d" % (tag, ci))
                    d.mkdir(exist_ok=True)
                    vbb = buf_bytes(s["vb"][0])
                    vo = s["vb"][2] + base * 48
                    (d / "vb.bin").write_bytes(vbb[vo:vo + nv * 48])
                    ibb = buf_bytes(s["ib"][0])
                    io = s["ib"][2] + sidx * 2
                    (d / "ib.bin").write_bytes(ibb[io:io + n_idx * 2])
                    cbs = {}
                    for k, rid in sorted(s["vcb"].items()):
                        if rid:
                            bb = buf_bytes(rid)
                            if bb is not None:
                                (d / ("cb_vs%d.bin" % k)).write_bytes(bb)
                            cbs["vs%d" % k] = {"rid": rid, "bytes": len(bb) if bb else None}
                    for k, rid in sorted(s["pcb"].items()):
                        cbs["ps%d" % k] = {"rid": rid}
                    tex = {}
                    for slot, vrid in sorted(s["srv"].items()):
                        if not vrid:
                            continue
                        rrid, vdesc = views.get(vrid, (None, None))
                        r = res.get(rrid, {})
                        hashes = []
                        for si, bi in enumerate(r.get("subs", [])):
                            b = blob(bi)
                            hashes.append(hashlib.sha256(b).hexdigest())
                            p = out / "textures" / ("%d_%d.bin" % (rrid, si))
                            if not p.exists():
                                p.write_bytes(b)
                        tex[slot] = {"view": vrid, "resource": rrid, "view_desc": vdesc, "desc": r.get("desc"),
                                     "sub_sha256": hashes}
                    rts = []
                    for v in s["rtv"]:
                        rrid, vdesc = views.get(v, (None, None))
                        rts.append({"view": v, "resource": rrid, "desc": res.get(rrid, {}).get("desc")})
                    def shash(rid):
                        bi = shaders.get(rid)
                        if bi is None:
                            return None
                        b = blob(bi)
                        return {"rid": rid, "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest()}
                    meta = {"chunk": ci, "context": ctx, "index_count": n_idx, "grid": q + 1,
                            "base_vertex": base, "start_index": sidx, "vb": s["vb"], "ib": s["ib"],
                            "vs": shash(s["vs"]), "ps": shash(s["ps"]), "input_layout": layouts.get(s["il"]),
                            "blend": states.get(s["blend"]), "depth": states.get(s["ds"]),
                            "raster": states.get(s["rs"]), "viewport": s["vp"],
                            "samplers": {k: {"rid": v, "desc": states.get(v)} for k, v in s["smp"].items()},
                            "cbuffers": cbs, "textures": tex, "render_targets": rts}
                    (d / "meta.json").write_text(json.dumps(meta, indent=1))
                    print("blend draw", d.name, "grid", q + 1, "textures", len(tex))
        finally:
            ch.clear()
    print("found", found)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
