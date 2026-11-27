"""Make the expected data of the bakepost comparison test from a RenderDoc capture of one tile bake (issue 3403).

    python -I render/tests/bakepost/mkref.py <export.xml> <export buffer archive> <out dir> [--ids IDS.json]

--ids (issue 0371): another capture's resource ids, a JSON {"textures": {name: [id, "init"|"create"]}, "buffers": {name:
archive entry}} that replaces the tables below (render/evidence/0371/ for the up-res capture of terrain-bake-passes.md
3.5.4, whose P_near0 / P_near1 are the near pair of the tile baked just before the frame).

Input: the `renderdoccmd convert -c zip.xml` export of the capture (tools/harness/rdc.py `convert()`): the .xml and the
buffer archive beside it (same name without extension). Output (game data, never committed; keep it on D and point
C5R_BAKE_REF at it): one `.c5rt` file per texture, one `.bin` per buffer, and `manifest.txt` with the SHA-256 of each.

The capture holds the frame's initial contents of every target the bake writes, i.e. the EXE's own results of the
bake that ran just before the frame (tile "P"): its three blend targets with their mips, the normal and macro-shadow
targets, both RGBA8 cache targets and both R32G32_UINT block targets. Those are the reference. P's CacheBake grid and
the constant buffers come from the initial contents of the dynamic buffers. Resource ids below are those of
`c5h-c_20261113-000224_frame380.rdc` (3023 run 3); research/05-graphics-composition/terrain-bake-passes.md section 7.

.c5rt layout (little endian): "C5RT", u32 version 1, u32 dxgi format, u32 width, height, depth, mips, array size,
u32 flags (1 = cube), u32 subresource count; per subresource: u32 row pitch, u32 depth pitch, u32 byte count, bytes.
"""
import hashlib
import os
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

# name -> (resource id, kind) ; kind "init" = Initial Contents chunk, "create" = data given at creation
TEXTURES = {
    "P_albedo": (43231, "init"), "P_normal": (43243, "init"), "P_spec": (43255, "init"),
    "P_nrm": (43280, "init"), "P_shadow": (43267, "init"),
    "P_cache0": (43461, "init"), "P_cache1": (43470, "init"),
    "P_blocks0": (43445, "init"), "P_blocks1": (43453, "init"),
    "height": (43203, "create"), "sun": (43164, "create"), "shadowlight": (43167, "create"),
    "lut_terrain": (2419, "create"),
}
# name -> buffer archive entry (the initial data of a buffer, or the bytes a Map wrote in the frame)
BUFFERS = {
    "P_grid24": 2762, "P_grid48": 2761, "P_ib": 2763, "P_frame": 6, "P_own": 130, "P_normcb": 17,
    "C_grid24": 3943, "C_grid48": 3944, "C_ib": 3949, "C_frame": 3939, "C_normcb": 3928,
    "C_shadow0": 3969, "C_shadow1": 3970, "C_shadow2": 3971,
}
DXGI = {"R16G16B16A16_TYPELESS": 9, "R16G16B16A16_FLOAT": 10, "R32G32_UINT": 17, "R8G8B8A8_TYPELESS": 27,
        "R8G8B8A8_UNORM": 28, "A8_UNORM": 65, "BC1_TYPELESS": 70, "BC1_UNORM": 71, "BC3_TYPELESS": 76}


def kids(el):
    return {c.get("name"): c for c in el}


def main():
    global TEXTURES, BUFFERS
    xml, bufzip, out = sys.argv[1:4]
    if len(sys.argv) > 5 and sys.argv[4] == "--ids":
        import json
        ids = json.load(open(sys.argv[5]))
        TEXTURES = {k: (int(v[0]), v[1]) for k, v in ids["textures"].items()}
        BUFFERS = {k: int(v) for k, v in ids["buffers"].items()}
    os.makedirs(out, exist_ok=True)
    z = zipfile.ZipFile(bufzip)
    want = {rid: name for name, (rid, _) in TEXTURES.items()}
    desc, subs = {}, {}
    for _, el in ET.iterparse(xml, events=("end",)):
        if el.tag != "chunk":
            continue
        nm = el.get("name") or ""
        if nm in ("ID3D11Device::CreateTexture2D", "ID3D11Device::CreateTexture3D"):
            k = kids(el)
            rid = int(k["pTexture"].text)
            if rid in want:
                d = kids(k["Descriptor"])
                fmt = d["Format"].get("string").replace("DXGI_FORMAT_", "")
                desc[rid] = (DXGI[fmt], int(d["Width"].text), int(d["Height"].text),
                             int(d["Depth"].text) if "Depth" in d else 1, int(d["MipLevels"].text),
                             int(d["ArraySize"].text) if "ArraySize" in d else 1,
                             1 if "TEXTURECUBE" in (d["MiscFlags"].get("string") or "") else 0)
                pitches = [(int(c.find("*[@name='SysMemPitch']").text), int(c.find("*[@name='SysMemSlicePitch']").text))
                           for c in k["pInitialData"]] if k.get("pInitialData") is not None else []
                data = [c.text for c in el if c.get("name") == "SubresourceContents"]
                if data and TEXTURES[want[rid]][1] == "create":
                    subs[rid] = [(p[0], p[1], z.read("%06d" % int(b))) for p, b in zip(pitches, data)]
        elif nm == "Internal::Initial Contents":
            k = kids(el)
            rid = int(k["id"].text) if "id" in k else -1
            if rid in want and TEXTURES[want[rid]][1] == "init":
                rows = [c.text for c in el if c.get("name") == "RowPitch"]
                data = [c.text for c in el if c.get("name") == "SubresourceContents"]
                subs[rid] = [(int(r), 0, z.read("%06d" % int(b))) for r, b in zip(rows, data)]
        el.clear()
    man = []
    for name, (rid, _) in sorted(TEXTURES.items()):
        if rid not in desc or rid not in subs:
            sys.exit("mkref: resource %d (%s) not found in %s" % (rid, name, xml))
        fmt, w, h, dpt, mips, arr, flags = desc[rid]
        blob = b"C5RT" + struct.pack("<9I", 1, fmt, w, h, dpt, mips, arr, flags, len(subs[rid]))
        for rp, dp, data in subs[rid]:
            blob += struct.pack("<3I", rp, dp, len(data)) + data
        path = os.path.join(out, name + ".c5rt")
        open(path, "wb").write(blob)
        man.append("%s  %s.c5rt  (id %d, %dx%dx%d, %d mips, %d subresources)" % (
            hashlib.sha256(blob).hexdigest(), name, rid, w, h, dpt, mips, len(subs[rid])))
    for name, bid in sorted(BUFFERS.items()):
        data = z.read("%06d" % bid)
        open(os.path.join(out, name + ".bin"), "wb").write(data)
        man.append("%s  %s.bin  (archive entry %d, %d B)" % (hashlib.sha256(data).hexdigest(), name, bid, len(data)))
    open(os.path.join(out, "manifest.txt"), "w").write("source: %s\n%s\n" % (os.path.abspath(xml), "\n".join(man)))
    print("\n".join(man))


if __name__ == "__main__":
    main()
