"""Issue 3402: turn bake_inputs.py's tables.json and make_ref.py's draw folders into plain text the C++ test reads.

    python render/tests/bakeblend/tools/ref_txt.py <ref dir>

Writes <ref dir>/blendref.txt (map tables, kernel, the captured draws with their tile and texture slots). Game data;
the folder stays on D (D83).
"""
import glob
import json
import os
import struct
import sys

FMT = {"DXGI_FORMAT_BC3_UNORM": 77, "DXGI_FORMAT_BC3_TYPELESS": 77, "DXGI_FORMAT_BC1_UNORM": 71,
       "DXGI_FORMAT_BC1_TYPELESS": 71, "DXGI_FORMAT_R8G8B8A8_UNORM": 28, "DXGI_FORMAT_R8G8B8A8_TYPELESS": 28,
       "DXGI_FORMAT_BC2_UNORM": 74, "DXGI_FORMAT_BC2_TYPELESS": 74, "DXGI_FORMAT_B8G8R8A8_UNORM": 87,
       "DXGI_FORMAT_B8G8R8A8_TYPELESS": 87}


def main():
    ref = sys.argv[1]
    T = json.load(open(os.path.join(ref, "tables.json")))
    t = T["tr8"]
    L = []
    L.append("samples %d %d" % (t["samples_x_1aa8"], t["samples_y_1aac"]))
    L.append("steps " + " ".join(t["step_hex"]))
    L.append("layer_count %d" % t["layer_count"])
    L.append("scale_of_set " + " ".join(t["scale_of_set_hex"]))
    for k in ("first_layer_of_material", "slope_projected", "set_of_layer", "projection_of_layer"):
        L.append(k + " " + " ".join(str(v) for v in t[k]))
    L.append("kernel %d %s" % (t["kernel_count"], " ".join("%d %d %s" % (a, b, c) for a, b, c in
                                                           zip(t["kernel_dx"], t["kernel_dy"], t["kernel_w_hex"]))))
    L.append("largest_cache %d" % T["settings"]["0xD90F2C"])
    L.append("map_plots_x %d" % (t["samples_x_1aa8"] // 64))
    tiles = {(e["ox"], e["oy"]): e for e in T["tiles"]}
    for d in sorted(glob.glob(os.path.join(ref, "frame*_c*"))):
        m = json.load(open(os.path.join(d, "meta.json")))
        vb = open(os.path.join(d, "vb.bin"), "rb").read()
        sx, sy = struct.unpack_from("<2f", vb, 12)
        e = tiles[(int(sx) + 1, int(sy) + 1)]
        L.append("draw %s %d %d %d %d %d %d %d %s" % (os.path.basename(d), e["ox"], e["oy"], e["w"], e["h"], e["e4"],
                                                      e["e8"], e["n"], " ".join(str(v) for v in e["ids"])))
        for slot in range(24):
            x = m["textures"].get(str(slot))
            if not x or not x["desc"]:
                continue
            ds = x["desc"]
            fmt = FMT.get(x["view_desc"]["Format"]) or FMT.get(ds["Format"]) or 0
            L.append("tex %d %d %d %d %d %d" % (slot, x["resource"], ds["Width"], ds["Height"], ds["MipLevels"], fmt))
        L.append("shaders %s %s" % (m["vs"]["sha256"], m["ps"]["sha256"]))
    with open(os.path.join(ref, "blendref.txt"), "w") as f:
        f.write("\n".join(L) + "\n")
    print("wrote", os.path.join(ref, "blendref.txt"), len(L), "lines")


if __name__ == "__main__":
    main()
