"""Issue 3402: read-only dump, by PID, of everything the bake's blend-pass vertex fill reads (for the offline test).

    python render/tests/bakeblend/tools/bake_inputs.py --pid PID --out DIR

ReadProcessMemory only (shell/tools/gt_reader.Reader; TimeDateStamp checked, addresses rebased). Layouts are described
in research/05-graphics-composition/terrain-bake-blend.md and terrain-bake-records.md. Writes into DIR (which must be
under D:/c5-64bit-work, game data never goes into the repository):
  tap.c5hf      the height field (u16 heights + material byte layer) in the C5HF tap format (heightfield.h)
  tables.json   TR8 tables (layer tables, kernel taps, plot steps, sample counts), bake settings, the tiles' layer
                lists / origins / extents, and the span globals 0xD90FCC / 0xD90FD8
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "shell" / "tools"))
import gt_reader as gt  # noqa: E402

VA_TS, TS_TR = 0xD165E0, 0x75FA4
VA_GRID = 0xD91270
TILE_SIZE = 0x330


def u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def i32(b, o=0):
    return struct.unpack_from("<i", b, o)[0]


def f32(b, o=0):
    return struct.unpack_from("<f", b, o)[0]


def layer_dense(rd, off, elem):
    """Dense [Y][X] copy of one FDataField layer of 0xD91270 (heights at +0x0C, bytes at +0x4C)."""
    import numpy as np
    hdr = rd.rd(rd.va(VA_GRID) + off, 0x40)
    X, Y, tpr = struct.unpack_from("<3i", hdr, 0)
    d_ptr, p_ptr = u32(hdr, 0x10), u32(hdr, 0x1C)
    trows = (Y + 15) // 16
    n_ent = trows * tpr
    raw = rd.rd(d_ptr, n_ent * 8)
    flag = np.frombuffer(raw, dtype="<u2").reshape(n_ent, 4)[:, 0]
    payload = np.frombuffer(raw, dtype="<u4").reshape(n_ent, 2)[:, 1]
    var = flag == 0
    n_tiles = int(payload[var].max()) + 1 if var.any() else 0
    tb = 256 * elem
    pool = b"".join(rd.rd(p_ptr + a, min(1 << 20, n_tiles * tb - a)) for a in range(0, n_tiles * tb, 1 << 20))
    dt = "<u2" if elem == 2 else "u1"
    pa = np.frombuffer(pool, dtype=dt).reshape(n_tiles, 16, 16) if n_tiles else None
    out = np.zeros((trows * 16, tpr * 16), dtype=np.uint16 if elem == 2 else np.uint8)
    for e in range(n_ent):
        ty, tx = divmod(e, tpr)
        if flag[e] != 0:
            out[ty * 16:ty * 16 + 16, tx * 16:tx * 16 + 16] = (payload[e] & (0xFFFF if elem == 2 else 0xFF))
        else:
            out[ty * 16:ty * 16 + 16, tx * 16:tx * 16 + 16] = pa[payload[e]]
    return out[:Y, :X].copy()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    out = Path(a.out)
    if not str(out.resolve()).replace("\\", "/").lower().startswith("d:/c5-64bit-work/"):
        print("refused: --out must be under D:/c5-64bit-work (game data)", file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    rd = gt.Reader(a.pid)
    res = {"time": time.strftime("%Y-%m-%d %H:%M:%S"), "pid": a.pid}
    st = rd.rva(0xD90F14, 0xD90FE8 - 0xD90F14)
    res["settings"] = {"0x%X" % (0xD90F14 + o): u32(st, o) for o in range(0, len(st), 4)}
    res["span_d90fcc"] = f32(st, 0xD90FCC - 0xD90F14)
    res["span_d90fd8"] = f32(st, 0xD90FD8 - 0xD90F14)
    res["wrap_d90fbc"] = u32(st, 0xD90FBC - 0xD90F14)
    tr = rd.u32(VA_TS + TS_TR)
    trb = rd.rd(tr, 0x2400)
    nx, ny, n = u32(trb, 0x2310), u32(trb, 0x2314), u32(trb, 0x2318)
    arr = u32(trb, 0x1ADC)
    ptrs = rd.rd(arr, 4 * n)
    tiles = []
    tr8 = None
    for i in range(n):
        b = rd.rd(u32(ptrs, 4 * i), TILE_SIZE)
        tr8 = tr8 or u32(b, 0x13C)
        lc = u32(b, 0x1F8)
        tiles.append({"i": i, "tx": i // ny, "ty": i % ny, "ox": i32(b, 0xDC), "oy": i32(b, 0xE0),
                      "w": i32(b, 0xD4), "h": i32(b, 0xD8), "e4": i32(b, 0xE4), "e8": i32(b, 0xE8),
                      "n": lc, "ids": [u32(b, 0x1FC + 4 * k) for k in range(min(lc, 8))],
                      "f2fc": u32(b, 0x2FC), "size17c": u32(b, 0x17C), "s310": u32(b, 0x310), "cls": u32(b, 0x328),
                      "cached": u32(b, 0x198) != 0})
    t8 = rd.rd(tr8, 0x1AD0)
    nk = u32(t8, 0x3CC)
    res["tr8"] = {
        "layer_count": u32(t8, 0x1110),
        "first_layer_of_material": [u32(t8, 0xC10 + 4 * k) for k in range(64)],
        "slope_projected": [u32(t8, 0xB10 + 4 * k) for k in range(64)],
        "set_of_layer": [u32(t8, 0xF10 + 4 * k) for k in range(64)],
        "projection_of_layer": [u32(t8, 0x1010 + 4 * k) for k in range(64)],
        "scale_of_set": [f32(t8, 0xE10 + 4 * k) for k in range(64)],
        "scale_of_set_hex": ["%08x" % u32(t8, 0xE10 + 4 * k) for k in range(64)],
        "spec_handle_of_set": [u32(t8, 0x808 + 4 * k) & 0xFFFF for k in range(64)],
        "kernel_count": nk,
        "kernel_dx": [i32(t8, 0x000 + 4 * k) for k in range(nk)],
        "kernel_dy": [i32(t8, 0x144 + 4 * k) for k in range(nk)],
        "kernel_w_hex": ["%08x" % u32(t8, 0x288 + 4 * k) for k in range(nk)],
        "samples_x_1aa8": u32(t8, 0x1AA8), "samples_y_1aac": u32(t8, 0x1AAC),
        "step_1ac4": f32(t8, 0x1AC4), "step_1ac8": f32(t8, 0x1AC8), "step_1acc": f32(t8, 0x1ACC),
        "step_hex": ["%08x" % u32(t8, o) for o in (0x1AC4, 0x1AC8, 0x1ACC)],
    }
    res["tiles_x"], res["tiles_y"] = nx, ny
    res["tiles"] = tiles
    g = rd.grid()
    hdr = g["heightmap_header"]
    h16 = layer_dense(rd, 0x0C, 2)
    mat = layer_dense(rd, 0x4C, 1)
    Y, X = h16.shape
    water = struct.unpack("<f", rd.rva(VA_GRID + 4, 4))[0]
    head = bytearray(128)
    flags = (1 if res["wrap_d90fbc"] == 1 else 0) | 2
    struct.pack_into("<4sIIIIII", head, 0, b"C5HF", 1, X // 64, Y // 64, X, Y, flags)
    struct.pack_into("<6f", head, 28, hdr["sx_A8"], hdr["sy_AC"], hdr["hscale_B0"], hdr["ox_B4"], hdr["oy_B8"], water)
    struct.pack_into("<I", head, 52, 0)
    struct.pack_into("<16f", head, 56, *[v for row in g["M_h2w"] for v in row] if isinstance(g["M_h2w"][0], list)
                     else g["M_h2w"])
    with open(out / "tap.c5hf", "wb") as f:
        f.write(head)
        f.write(h16.astype("<u2").tobytes())
        f.write(mat.astype("u1").tobytes())
    res["tap"] = {"samples_x": X, "samples_y": Y, "wrap": flags & 1}
    (out / "tables.json").write_text(json.dumps(res, indent=1))
    rd.close()
    print("wrote", out, "tiles", n, "kernel", nk, "layers", res["tr8"]["layer_count"], "samples", X, Y)
    return 0


if __name__ == "__main__":
    sys.exit(main())
