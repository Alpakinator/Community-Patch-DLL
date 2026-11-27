"""Turn 4109's reference set (one tile bake with decals, exported from a RenderDoc capture) into the plain files that
c5r_decal_test reads (issue 4104, epic 0166; research/05-graphics-composition/terrain-decals.md sections 4.5 and 11).

    python -I render/tests/decal/mkref_decal.py <4109 tile dir> <out dir> --install DIR [--mods DIR ...]

<4109 tile dir> is e.g. F:/c5-64bit-work/0166/4109/ref/A_roadfarm (inputs/ = the `cli.py rdc-export` of the bake's draws,
expected_*/ = the EXE's near-camera CacheBake pair). Output (game data, never committed, D83: keep it on F:):

  tile.txt        one record per line (below); every path relative to <out dir>
  *.c5rt          textures in render/tests/bakepost/mkref.py's .c5rt layout
  *.bin           buffers (blend grid, CacheBake grid, constant buffers, decal vertex chunks, the decals' constants)
  manifest.txt    SHA-256 of every file

tile.txt records:
  origin X Y | size S | second 0/1 | shadowdraws N
  blend <vb> <ib> <cb0> <cb2>                 the blend draw (TerrainShaderScreen pass 2): 48-byte grid, 16-bit indices
  blendtex SLOT <file.c5rt> VIEWFMT           the blend's textures t0..t23 (DXGI number of the view)
  height <file.c5rt> | own <bin> | frame <bin> | normcb <bin> | grid24 <bin>
  sun <c5rt> | shadowlight <c5rt> | lut <c5rt>
  decal I PASS FIRST COUNT <vbfile> <cbfile> T0 T1 T2   in draw order; T = a `tex` id, or "default" (the EXE's 8 x 8)
  tex RES <file.c5rt> NAME                     a decal texture as captured, and the install file with the same bytes
  expected <colour.c5rt> <spec.c5rt>           the EXE's output (one of the expected_* folders: byte-identical)

The decal texture names are found by comparing every captured subresource with the DDS payloads of the install's
packs (Resource/ and Assets/, *.fpk), its loose .dds files and the mod folders (--mods), smallest first. Exit 1 when a
decal texture has no match or a check fails (identity index buffer, a draw of another layout).
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
from pathlib import Path

PREFER = ["decaltextures.fpk", "upgrade1textures.fpk", "misctextures.fpk", "improvementtextures.fpk",
          "unitstextures.fpk"]
# TerrainDecalSystem's default spec texture (terrain-decals.md 1.1, TDS+0x4F0): byte-equal to invisible.dds and to
# Upgrade1Textures' trading_post_grnd_sref_early.dds (ImprovementTextures has another file of that name).
NAME_FIRST = ["blank_texture_spec_decal.dds"]


def die(msg):
    sys.exit("mkref_decal: " + msg)


def load_state(inp, d):
    return json.loads((inp / d["dir"] / "state.json").read_text())


def fmt_info(fmt, w, h):
    """(row pitch, rows) of a mip for the formats the bake uses."""
    bc8 = {70, 71, 72, 79, 80, 81}          # BC1, BC4
    bc16 = {73, 74, 75, 76, 77, 78, 82, 83, 84, 94, 95, 96, 97, 98, 99}  # BC2, BC3, BC5, BC6, BC7
    if fmt in bc8:
        return max(1, (w + 3) // 4) * 8, max(1, (h + 3) // 4)
    if fmt in bc16:
        return max(1, (w + 3) // 4) * 16, max(1, (h + 3) // 4)
    bpp = {65: 1, 61: 1, 9: 8, 10: 8, 17: 8, 2: 16, 27: 4, 28: 4, 29: 4, 87: 4, 88: 4, 90: 4, 91: 4}.get(fmt)
    if bpp is None:
        die("format %d not handled" % fmt)
    return w * bpp, h


def c5rt(inp, data):
    """.c5rt bytes from an export texture record (state.json 'data')."""
    desc = data["desc"]
    fmt = desc["Format"]["v"]
    w, h = desc["Width"], desc["Height"]
    depth = desc.get("Depth", 1)
    mips = desc["MipLevels"]
    arr = desc.get("ArraySize", 1)
    cube = 1 if "TEXTURECUBE" in (desc["MiscFlags"].get("s") or "") else 0
    blob = (inp / data["file"]).read_bytes()
    sizes = data["subresource_bytes"]
    if sum(sizes) != len(blob):
        die("texture %d: subresource sizes %d != file %d" % (data["res"], sum(sizes), len(blob)))
    out = b"C5RT" + struct.pack("<9I", 1, fmt, w, h, depth, mips, arr, cube, len(sizes))
    at = 0
    for i, n in enumerate(sizes):
        m = i % mips
        rp, rows = fmt_info(fmt, max(1, w >> m), max(1, h >> m))
        dp = rp * rows
        out += struct.pack("<3I", rp, dp, n) + blob[at:at + n]
        at += n
    return out, blob


class Install:
    """DDS payloads by size, from packs and loose files; read lazily."""

    def __init__(self, root, mods):
        self.cands = []  # (payload size, label, reader)
        packs = sorted(Path(root).rglob("*.fpk"), key=lambda p: (PREFER.index(p.name.lower()) if p.name.lower() in PREFER
                                                                 else len(PREFER), str(p).lower()))
        for p in packs:
            self._fpk(p)
        for base in [Path(root) / "Assets"] + [Path(m) for m in mods]:
            for f in sorted(base.rglob("*.dds")):
                n = f.stat().st_size
                for hdr in (128, 148):
                    self.cands.append((n - hdr, "loose:%s" % f, (lambda f=f, hdr=hdr: f.read_bytes()[hdr:])))

    def _fpk(self, p):
        size = p.stat().st_size
        with open(p, "rb") as fh:
            head = fh.read(min(size, 16 << 20))
            if head[4:8] != b"FPK_":
                return
            count, = struct.unpack_from("<I", head, 10)
            at = 14
            for _ in range(count):
                if at + 4 > len(head):
                    fh.seek(0)
                    head = fh.read()
                n, = struct.unpack_from("<I", head, at)
                at += 4
                name = head[at:at + n].decode("latin-1")
                at += n + ((4 - n % 4) % 4)
                _a, _b, esz, off = struct.unpack_from("<IIII", head, at)
                at += 16
                if not name.lower().endswith(".dds"):
                    continue
                for hdr in (128, 148):
                    self.cands.append((esz - hdr, "%s:%s" % (p.name, name),
                                       (lambda p=p, off=off, esz=esz, hdr=hdr: _read(p, off + hdr, esz - hdr))))

    def find(self, blob):
        """The matching file; among byte-equal files the EXE's own default names first (several packs hold the same
        4 x 4 spec texture under three names, and one of those names has other bytes in another pack)."""
        hits = [label for n, label, reader in self.cands if n == len(blob) and reader() == blob]
        for want in NAME_FIRST:
            for label in hits:
                if label.lower().endswith(want):
                    return label
        return hits[0] if hits else None


def _read(p, off, n):
    with open(p, "rb") as fh:
        fh.seek(off)
        return fh.read(n)


def default_texture(data, blob):
    d = data["desc"]
    if d["Width"] != 8 or d["Height"] != 8 or d["MipLevels"] != 1 or d["Format"]["v"] not in (27, 28, 29):
        return False
    want = bytearray(8 * 8 * 4)
    want[0], want[3] = 255, 255
    return bytes(want) == blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tile")
    ap.add_argument("out")
    ap.add_argument("--install", required=True)
    ap.add_argument("--mods", action="append", default=[])
    a = ap.parse_args()
    tile, out = Path(a.tile), Path(a.out)
    inp = tile / "inputs"
    out.mkdir(parents=True, exist_ok=True)
    draws = json.loads((inp / "draws.json").read_text())["draws"]
    lines, files = [], {}

    def put(name, data):
        (out / name).write_bytes(data)
        files[name] = hashlib.sha256(data).hexdigest()
        return name

    blend = [d for d in draws if d["system"] == "m76p1"]
    decals = [d for d in draws if d["system"] == "m78p1"]
    normal = [d for d in draws if d["system"] == "m74p0"]
    shadow = [d for d in draws if d["system"] == "m76p3"]
    cache = [d for d in draws if d["system"] == "m75p1"]
    if len(blend) != 1 or len(normal) != 1 or not shadow or len(cache) < 1 or not decals:
        die("%s: not one decal bake (blend %d, normal %d, shadow %d, cache %d, decals %d)" % (
            tile, len(blend), len(normal), len(shadow), len(cache), len(decals)))
    # the blend draw
    s = load_state(inp, blend[0])
    vb = (inp / s["ia"]["vb"]["0"]["data"]["file"]).read_bytes()
    ib = (inp / s["ia"]["ib"]["data"]["file"]).read_bytes()
    a_ = blend[0]["args"]
    if a_["BaseVertexLocation"] != 0 or s["ia"]["vb"]["0"]["offset"] != 0 or s["ia"]["vb"]["0"]["stride"] != 48:
        die("blend draw with a vertex offset")
    idx = ib[a_["StartIndexLocation"] * 2:(a_["StartIndexLocation"] + a_["IndexCountPerInstance"]) * 2]
    nv = max(struct.unpack("<%dH" % (len(idx) // 2), idx)) + 1
    ox, oy = struct.unpack_from("<2f", vb, 12)
    origin = (int(ox) + 1, int(oy) + 1)
    lines.append("origin %d %d" % origin)
    lines.append("blend %s %s %s %s" % (put("blend_vb.bin", vb[:nv * 48]), put("blend_ib.bin", idx),
                                        put("blend_cb0.bin", (inp / s["VS"]["cb"]["0"]["data"]["file"]).read_bytes()),
                                        put("blend_cb2.bin", (inp / s["VS"]["cb"]["2"]["data"]["file"]).read_bytes())))
    for k in sorted(s["PS"]["srv"], key=int):
        v = s["PS"]["srv"][k]
        b, _ = c5rt(inp, v["data"])
        lines.append("blendtex %s %s %d" % (k, put("blendtex_%d.c5rt" % v["data"]["res"], b), v["view_desc"]["Format"]["v"]))
    # the passes after the blend
    sh = load_state(inp, shadow[0])
    lines.append("height %s" % put("height.c5rt", c5rt(inp, sh["PS"]["srv"]["0"]["data"])[0]))
    lines.append("own %s" % put("own_shadow.bin", (inp / sh["VS"]["cb"]["0"]["data"]["file"]).read_bytes()))
    lines.append("shadowdraws %d" % len(shadow))
    nm = load_state(inp, normal[0])
    lines.append("normcb %s" % put("normcb.bin", (inp / nm["PS"]["cb"]["1"]["data"]["file"]).read_bytes()))
    cs = load_state(inp, cache[0])   # the first CacheBake draw: into the near-camera pair (the expected data)
    lines.append("frame %s" % put("frame.bin", (inp / cs["PS"]["cb"]["1"]["data"]["file"]).read_bytes()))
    g = (inp / cs["ia"]["vb"]["0"]["data"]["file"]).read_bytes()
    lines.append("grid24 %s" % put("grid24.bin", g[:nv * 24]))
    lines.append("size %d" % int(cs["rs"]["viewports"][0]["Width"]))
    lines.append("second %d" % len([r for r in cs["om"]["rtv"] if r]))
    lines.append("sun %s" % put("sun.c5rt", c5rt(inp, cs["PS"]["srv"]["4"]["data"])[0]))
    lines.append("shadowlight %s" % put("shadowlight.c5rt", c5rt(inp, cs["PS"]["srv"]["5"]["data"])[0]))
    lines.append("lut %s" % put("lut.c5rt", c5rt(inp, cs["PS"]["srv"]["8"]["data"])[0]))
    # the decal draws (TerrainDecalShader pass 1): blend targets (3 RTVs) or the macro shadow (1 RTV)
    install = Install(a.install, a.mods)
    texnames, chunks = {}, {}
    for i, d in enumerate(decals):
        st = load_state(inp, d)
        ag = d["args"]
        ibd = st["ia"]["ib"]["data"]
        ibb = (inp / ibd["file"]).read_bytes()
        n = len(ibb) // 2
        if ibd["desc"]["ByteWidth"] != 131070 or struct.unpack("<%dH" % n, ibb) != tuple(range(n)):
            die("decal draw %d: the index buffer is not the identity" % d["n"])
        if ag["BaseVertexLocation"] != 0 or st["ia"]["vb"]["0"]["stride"] != 8 or st["ia"]["vb"]["0"]["offset"] != 0:
            die("decal draw %d: vertex layout" % d["n"])
        vres = st["ia"]["vb"]["0"]["res"]
        if vres not in chunks:
            chunks[vres] = put("chunk_%d.bin" % vres, (inp / st["ia"]["vb"]["0"]["data"]["file"]).read_bytes())
        cb = (inp / st["VS"]["cb"]["0"]["data"]["file"]).read_bytes()
        rtv = [r for r in st["om"]["rtv"] if r]
        pas = 0 if len(rtv) == 3 else 1
        tk = []
        for k in ("0", "1", "2"):
            v = st["PS"]["srv"][k]
            data = v["data"]
            res = data["res"]
            if res not in texnames:
                b, blob = c5rt(inp, data)
                if default_texture(data, blob):
                    texnames[res] = "default"
                else:
                    label = install.find(blob)
                    if not label:
                        die("decal texture %d (%s) matches no file of the install" % (res, data["desc"]["Format"]["s"]))
                    name = label.split(":")[-1].replace("\\", "/").split("/")[-1].lower()
                    texnames[res] = name
                    lines.append("tex %d %s %s" % (res, put("tex_%d.c5rt" % res, b), name))
                    print("  texture %d -> %s (%s)" % (res, name, label))
            tk.append("default" if texnames[res] == "default" else str(res))
        lines.append("decal %d %d %d %d %s %s %s" % (i, pas, ag["StartIndexLocation"], ag["IndexCountPerInstance"],
                                                     chunks[vres], put("decal_cb_%02d.bin" % i, cb[:112]), " ".join(tk)))
    exp = sorted(tile.glob("expected_*"))[0]
    lines.append("expected %s %s" % (put("expected_colour.c5rt", (exp / "cache_colour.c5rt").read_bytes()),
                                     put("expected_spec.c5rt", (exp / "cache_spec.c5rt").read_bytes())))
    (out / "tile.txt").write_text("\n".join(lines) + "\n")
    man = ["source: %s (expected from %s)" % (tile, exp.name)] + ["%s  %s" % (h, f) for f, h in sorted(files.items())]
    (out / "manifest.txt").write_text("\n".join(man) + "\n")
    print("%s: origin %s, %d decal draws, %d textures, %d chunks -> %s" % (tile.name, origin, len(decals), len(texnames),
                                                                           len(chunks), out))


if __name__ == "__main__":
    main()
