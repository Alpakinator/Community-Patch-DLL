"""Issue 0182: the test of terrainreplay.exe and of the input checks of manifest.py, export.py and compare.py.

    python -I render/tools/terrainreplay/test_terrainreplay.py --exe BUILD/tools/terrainreplay/terrainreplay.exe

Hermetic: WARP device, hand-made HLSL (`shader_hlsl`), files in a temporary folder; no game data, no GPU, no window.
Positive cases: a 4-control-point patch tessellated (factor 4, seen in the domain locations of the provoking vertices) with an
overlay texture t1 composed as the TerrainShader does (`colour * (1 - t1.a) + t1.rgb`, premultiplied), per quadrant;
the same with t1 transparent; depth and id outputs. Negative cases: every input check, each with its exit code and no
output file written. Issue 4152 added (unit_draw_cases): a hand-made instanced skinned draw (`call`
INSTANCES, the `buf` directive in VS and PS t27, a start instance), a cube texture (`tex` DIM 4), a `background`, and
every new input check. Issue 0347 added (buffer_export_cases): manifest.py on a synthetic export with buffer SRVs
(buf lines, named warnings for the ones skipped, replay.json's skipped_srvs; manifest outputs under a temporary folder in
C:/c5-64bit-work/). Exit 0 when every case passes, 1 otherwise; 77 never (nothing is skipped).
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
W = H = 64

VS = "struct V { float2 p : POSITION; }; struct O { float2 p : CP; };\nO main(V v) { O o; o.p = v.p; return o; }\n"
HS = """struct O { float2 p : CP; };
struct PC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };
PC pc(InputPatch<O, 4> ip) { PC c; c.e[0] = c.e[1] = c.e[2] = c.e[3] = 4; c.i[0] = c.i[1] = 4; return c; }
[domain("quad")] [partitioning("integer")] [outputtopology("triangle_cw")] [outputcontrolpoints(4)]
[patchconstantfunc("pc")]
O main(InputPatch<O, 4> ip, uint i : SV_OutputControlPointID) { return ip[i]; }
"""
DS = """struct O { float2 p : CP; };
struct PC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };
struct D { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[domain("quad")]
D main(PC c, float2 t : SV_DomainLocation, const OutputPatch<O, 4> q) {
  float2 p = lerp(lerp(q[0].p, q[1].p, t.x), lerp(q[2].p, q[3].p, t.x), t.y);
  D d; d.pos = float4(p, 0.5, 1); d.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); return d;
}
"""
PS = """cbuffer Base : register(b0) { float4 g_base; };
Texture2D t1 : register(t1); SamplerState s0 : register(s0);
struct D { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(D d) : SV_Target { float4 o = t1.Sample(s0, d.uv); return float4(g_base.rgb * (1 - o.a) + o.rgb, 1); }
"""
# the domain location of each triangle's provoking vertex, not interpolated: tessellation factor 4 gives the
# quarter steps 0, 64, 128, 191, 255 (SV_PrimitiveID would not do: after tessellation it is the patch's id)
DS_DL = DS.replace("float2 uv : TEXCOORD0; };", "float2 uv : TEXCOORD0; nointerpolation float2 dl : TEXCOORD1; };").replace(
    "d.uv = float2(", "d.dl = t; d.uv = float2(")
PS_DL = """struct D { float4 pos : SV_Position; float2 uv : TEXCOORD0; nointerpolation float2 dl : TEXCOORD1; };
float4 main(D d) : SV_Target { return float4(d.dl, 0, 1); }
"""
ID_PS = """cbuffer IdCb : register(b13) { uint g_id; };
struct D { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
uint main(D d) : SV_Target { return g_id; }
"""
# t1, 2 x 2 premultiplied RGBA8: top left red at alpha 128, top right transparent, bottom left opaque black,
# bottom right green at alpha 64
T1 = bytes([128, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 255, 0, 64, 0, 64])
BASE = (0.0, 0.0, 1.0, 1.0)  # blue
# expected colour at each quadrant's centre: base * (1 - a) + rgb
EXPECT = {"tl": (128, 0, 127), "tr": (0, 0, 255), "bl": (0, 0, 0), "br": (0, 64, 191)}


class T:
    def __init__(self, exe: Path, tmp: Path):
        self.exe, self.tmp, self.fails, self.n = exe, tmp, [], 0

    def check(self, name, cond, detail=""):
        self.n += 1
        print("%s %s %s" % ("PASS" if cond else "FAIL", name, detail))
        if not cond:
            self.fails.append(name)

    def files(self):
        (self.tmp / "vs.hlsl").write_text(VS)
        (self.tmp / "hs.hlsl").write_text(HS)
        (self.tmp / "ds.hlsl").write_text(DS)
        (self.tmp / "ps.hlsl").write_text(PS)
        (self.tmp / "ds_dl.hlsl").write_text(DS_DL)
        (self.tmp / "ps_dl.hlsl").write_text(PS_DL)
        (self.tmp / "id_ps.hlsl").write_text(ID_PS)
        (self.tmp / "bad.hlsl").write_text("float4 main( : SV_Target { return 0; }\n")
        (self.tmp / "vb.bin").write_bytes(struct.pack("<8f", -1, 1, 1, 1, -1, -1, 1, -1))  # TL TR BL BR
        (self.tmp / "cb.bin").write_bytes(struct.pack("<4f", *BASE))
        (self.tmp / "t1.bin").write_bytes(T1)
        (self.tmp / "clear.bin").write_bytes(bytes(16))
        (self.tmp / "short.bin").write_bytes(T1[:7])

    def manifest(self, name, t1="t1.bin", ps="ps.hlsl", ds="ds.hlsl", outs=True, extra_draw=(), head=("device warp",), tail=("end",)):
        p = self.tmp
        lines = list(head) + ["target %d %d 1 0 0 0 0" % (W, H)]
        if outs:
            lines += ["out_color %s" % (p / (name + ".rgba")).as_posix(), "out_depth %s" % (p / (name + ".f32")).as_posix(),
                      "out_id %s" % (p / (name + ".u32")).as_posix(), "id_ps %s" % (p / "id_ps.hlsl").as_posix()]
        lines += ["draw 7", "topo 36", "elem POSITION 0 16 0 0 0 0", "vb 0 8 0 %s" % (p / "vb.bin").as_posix(),
                  "call 0 4 0 0",
                  "shader_hlsl VS %s main vs_5_0" % (p / "vs.hlsl").as_posix(),
                  "shader_hlsl HS %s main hs_5_0" % (p / "hs.hlsl").as_posix(),
                  "shader_hlsl DS %s main ds_5_0" % (p / ds).as_posix(),
                  "shader_hlsl PS %s main ps_5_0" % (p / ps).as_posix(),
                  "cb PS 0 %s" % (p / "cb.bin").as_posix(),
                  "tex PS 1 %s 28 28 2 2 2 1 1 1 1" % (p / t1).as_posix(), "sub 0 8 16",
                  "samp PS 0 0 3 3 3 0.0 1 1 0 0 0 0 0.0 3.4e38",
                  "raster 3 1 0 0 0.0 0.0 1 0"]
        lines += list(extra_draw) + list(tail)
        f = p / (name + ".manifest")
        f.write_text("\n".join(lines) + "\n")
        return f

    def run(self, *args):
        return subprocess.run([str(self.exe), *map(str, args)], capture_output=True, text=True, timeout=120)

    def outputs(self, name):
        return [self.tmp / (name + e) for e in (".rgba", ".f32", ".u32")]


def at(img, x, y):
    return tuple(int(v) for v in img[y, x, :3])


def close(a, b, tol=1):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


# ---------------------------------------------------------------- issue 4152: instanced draws, `buf`, cube, background
# A hand-made instanced skinned draw in the EXE's unit layout: a 28-byte vertex (half position, UNORM8 weights, UINT8
# bone indices, half normal, half uv), a per-instance R32_UINT id stream in slot 1 read from a byte offset, the bone
# palette as a buffer SRV in VS and PS t27 (block of instance i = rows 98 i ..; row 0 a colour the PS reads, bone k =
# rows 2 + 3k .. 4 + 3k, a 3 x 4 matrix with the translation in w; three weights blended).
U_VS = """Buffer<float4> pal : register(t27);
struct V { float4 p : POSITION; float4 w : BLENDWEIGHT; uint4 b : BLENDINDICES; float4 n : NORMAL; float2 uv : TEXCOORD;
           uint id : BLENDINDICES1; };
struct O { float4 pos : SV_Position; nointerpolation uint id : TEXCOORD0; float3 n : TEXCOORD1; };
float3 bone(uint id, uint k, float3 p) {
  uint r = id * 98 + 2 + 3 * k;
  return float3(dot(pal[r], float4(p, 1)), dot(pal[r + 1], float4(p, 1)), dot(pal[r + 2], float4(p, 1)));
}
O main(V v) {
  float3 p = v.w.x * bone(v.id, v.b.x, v.p.xyz) + v.w.y * bone(v.id, v.b.y, v.p.xyz) + v.w.z * bone(v.id, v.b.z, v.p.xyz);
  O o; o.pos = float4(p.xy, 0.5, 1); o.id = v.id; o.n = v.n.xyz; return o;
}
"""
U_PS = """Buffer<float4> pal : register(t27);
struct O { float4 pos : SV_Position; nointerpolation uint id : TEXCOORD0; float3 n : TEXCOORD1; };
float4 main(O o) : SV_Target { return float4(pal[o.id * 98].rgb, 1); }
"""
U_PS_CUBE = """TextureCube cube : register(t2); SamplerState s0 : register(s0);
struct O { float4 pos : SV_Position; nointerpolation uint id : TEXCOORD0; float3 n : TEXCOORD1; };
float4 main(O o) : SV_Target { return cube.Sample(s0, o.n); }
"""
U_ID_PS = """cbuffer IdCb : register(b13) { uint g_id; };
struct O { float4 pos : SV_Position; nointerpolation uint id : TEXCOORD0; float3 n : TEXCOORD1; };
uint main(O o) : SV_Target { return g_id; }
"""
U_BLOCKS = {3: ((255, 0, 0), -0.5, 0.5), 5: ((0, 255, 0), 0.5, 0.5), 9: ((0, 0, 255), 0.0, -0.5)}
U_CUBE_FACES = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]  # +X -X +Y -Y +Z -Z


def u_vertex(x, y, w, b, n):
    return (np.array([x, y, 0, 0], np.float16).tobytes() + bytes(w) + bytes(b) +
            np.array(list(n) + [0], np.float16).tobytes() + np.array([0, 0], np.float16).tobytes())


def u_palette(size=16384):
    pal = np.zeros((size, 4), np.float32)
    for blk, (col, dx, dy) in U_BLOCKS.items():
        r = blk * 98
        pal[r] = [c / 255.0 for c in col] + [1]
        pal[r + 1] = [1, 0, 0, 0]
        for k in range(32):
            base = r + 2 + 3 * k
            pal[base] = [1, 0, 0, dx]
            pal[base + 1] = [0, 1, 0, dy]
            pal[base + 2] = [0, 0, 1, 0]
    return pal


class UnitT:
    """files and manifests of the unit-draw cases (also used by models/tools/unitreplay/test_unitreplay.py)."""

    def __init__(self, exe: Path, tmp: Path):
        self.exe, self.tmp = exe, tmp

    def files(self):
        p = self.tmp
        (p / "u_vs.hlsl").write_text(U_VS)
        (p / "u_ps.hlsl").write_text(U_PS)
        (p / "u_ps_cube.hlsl").write_text(U_PS_CUBE)
        (p / "u_id_ps.hlsl").write_text(U_ID_PS)
        s = 0.2
        vb = (u_vertex(-s, s, (255, 0, 0, 0), (0, 1, 2, 3), (1, 0, 0)) + u_vertex(s, s, (85, 85, 85, 0), (0, 1, 2, 3), (1, 0, 0)) +
              u_vertex(-s, -s, (0, 0, 255, 0), (0, 1, 2, 3), (1, 0, 0)) + u_vertex(s, -s, (128, 127, 0, 0), (2, 1, 0, 3), (1, 0, 0)))
        (p / "u_vb.bin").write_bytes(vb)
        (p / "u_ib.bin").write_bytes(struct.pack("<6H", 0, 1, 2, 2, 1, 3))
        (p / "u_ids.bin").write_bytes(struct.pack("<5I", 77, 3, 5, 9, 1))   # read from byte offset 4: ids 3, 5, 9
        (p / "u_pal.bin").write_bytes(u_palette().tobytes())
        (p / "u_pal_short.bin").write_bytes(u_palette().tobytes()[:100 * 16])
        (p / "u_cube.bin").write_bytes(b"".join(bytes(list(c) + [255]) * 4 for c in U_CUBE_FACES))
        bg = np.zeros((H, W, 4), np.uint8)
        bg[...] = (10, 20, 30, 255)
        (p / "u_bg.rgba").write_bytes(bg.tobytes())
        (p / "u_bg_short.rgba").write_bytes(bg.tobytes()[:-4])

    def manifest(self, name, call="call 1 6 0 0 3", ps="u_ps.hlsl", bg=None, buf="buf VS 27 {pal} 2 0 16384", cube=False):
        p = self.tmp
        lines = ["device warp", "target %d %d 1 0 0 0 0" % (W, H)]
        if bg:
            lines.append("background %s" % (p / bg).as_posix())
        lines += ["out_color %s" % (p / (name + ".rgba")).as_posix(), "out_depth %s" % (p / (name + ".f32")).as_posix(),
                  "out_id %s" % (p / (name + ".u32")).as_posix(), "id_ps %s" % (p / "u_id_ps.hlsl").as_posix(),
                  "draw 1", "topo 4",
                  "elem POSITION 0 10 0 0 0 0", "elem BLENDWEIGHT 0 28 0 8 0 0", "elem BLENDINDICES 0 30 0 12 0 0",
                  "elem NORMAL 0 10 0 16 0 0", "elem TEXCOORD 0 34 0 24 0 0", "elem BLENDINDICES 1 42 1 0 1 1",
                  "vb 0 28 0 %s" % (p / "u_vb.bin").as_posix(), "vb 1 4 4 %s" % (p / "u_ids.bin").as_posix(),
                  "ib 57 0 %s" % (p / "u_ib.bin").as_posix(), call,
                  "shader_hlsl VS %s main vs_5_0" % (p / "u_vs.hlsl").as_posix(),
                  "shader_hlsl PS %s main ps_5_0" % (p / ps).as_posix(),
                  buf.format(pal=(p / "u_pal.bin").as_posix()),
                  "buf PS 27 %s 2 0 16384" % (p / "u_pal.bin").as_posix(),
                  "raster 3 1 0 0 0.0 0.0 1 0"]
        if cube:
            lines += ["tex PS 2 %s 28 28 4 2 2 1 1 6 6" % (p / "u_cube.bin").as_posix()] + ["sub %d 8 16" % (16 * i) for i in range(6)]
            lines += ["samp PS 0 0 3 3 3 0.0 1 1 0 0 0 0 0.0 3.4e38"]
        lines += ["end"]
        f = p / (name + ".manifest")
        f.write_text("\n".join(lines) + "\n")
        return f

    def outputs(self, name):
        return [self.tmp / (name + e) for e in (".rgba", ".f32", ".u32")]


def u_px(img, x, y):
    return tuple(int(v) for v in img[y, x, :3])


def u_at(ndc_x, ndc_y):
    return int((ndc_x + 1) * 0.5 * W), int((1 - ndc_y) * 0.5 * H)


def unit_draw_cases(t):
    """issue 4152's directives on WARP: positives and every new input check (exit code, no output written)."""
    u = UnitT(t.exe, t.tmp)
    u.files()

    def run(f):
        return subprocess.run([str(t.exe), str(f)], capture_output=True, text=True, timeout=120)

    def img(name):
        return np.fromfile(t.tmp / (name + ".rgba"), np.uint8).reshape(H, W, 4)
    r = run(u.manifest("u_inst"))
    t.check("unit_instanced_exit0", r.returncode == 0, r.stderr.strip())
    if r.returncode == 0:
        im = img("u_inst")
        for blk, (col, dx, dy) in U_BLOCKS.items():
            t.check("unit_instance_block_%d_at_offset" % blk, u_px(im, *u_at(dx, dy)) == col, str(u_px(im, *u_at(dx, dy))))
        t.check("unit_three_quads_only", 300 <= int(im[..., :3].any(axis=2).sum()) <= 675 and u_px(im, W // 2, H // 2) == (0, 0, 0))
    r = run(u.manifest("u_start", call="call 1 6 0 0 2 1"))
    got = {b: u_px(img("u_start"), *u_at(dx, dy)) for b, (c, dx, dy) in U_BLOCKS.items()} if r.returncode == 0 else {}
    t.check("unit_start_instance", r.returncode == 0 and got == {3: (0, 0, 0), 5: (0, 255, 0), 9: (0, 0, 255)}, str(got))
    r = run(u.manifest("u_single", call="call 1 6 0 0"))
    got = {b: u_px(img("u_single"), *u_at(dx, dy)) for b, (c, dx, dy) in U_BLOCKS.items()} if r.returncode == 0 else {}
    t.check("unit_not_instanced_is_one_quad", r.returncode == 0 and got == {3: (255, 0, 0), 5: (0, 0, 0), 9: (0, 0, 0)}, str(got))
    r = run(u.manifest("u_cube", ps="u_ps_cube.hlsl", cube=True))
    t.check("unit_cube_face_plus_x", r.returncode == 0 and u_px(img("u_cube"), *u_at(-0.5, 0.5)) == U_CUBE_FACES[0], r.stderr.strip())
    r = run(u.manifest("u_bg", bg="u_bg.rgba"))
    t.check("unit_background_kept", r.returncode == 0 and u_px(img("u_bg"), W // 2, H // 2) == (10, 20, 30) and
            u_px(img("u_bg"), *u_at(0.5, 0.5)) == (0, 255, 0), r.stderr.strip())

    def neg(name, f, needle):
        for o in u.outputs(name):
            if o.exists():
                o.unlink()
        r = run(f)
        written = [o.name for o in u.outputs(name) if o.exists()]
        t.check("neg_" + name, r.returncode == 1 and not written and needle in r.stderr,
                "exit %d, stderr %r, written %s" % (r.returncode, r.stderr.strip()[:160], written))
    neg("u_instances_zero", u.manifest("u_instances_zero", call="call 1 6 0 0 0"), "instances must be")
    neg("u_instances_text", u.manifest("u_instances_text", call="call 1 6 0 0 three"), "instances must be")
    neg("u_start_instance_bad", u.manifest("u_start_instance_bad", call="call 1 6 0 0 2 -1"), "bad start instance")
    neg("u_call_trailing", u.manifest("u_call_trailing", call="call 1 6 0 0 2 0 9"), "trailing text")
    neg("u_buf_format", u.manifest("u_buf_format", buf="buf VS 27 {pal} 71 0 16384"), "format not handled")
    neg("u_buf_short", u.manifest("u_buf_short", buf="buf VS 27 %s 2 0 16384" % (t.tmp / "u_pal_short.bin").as_posix()), "shorter than its SRV")
    neg("u_buf_zero", u.manifest("u_buf_zero", buf="buf VS 27 {pal} 2 0 0"), "at least one element")
    neg("u_buf_stage", u.manifest("u_buf_stage", buf="buf XS 27 {pal} 2 0 16384"), "unknown stage")
    neg("u_buf_missing", u.manifest("u_buf_missing", buf="buf VS 27 %s 2 0 16" % (t.tmp / "nope.bin").as_posix()), "cannot read")
    neg("u_buf_short_line", u.manifest("u_buf_short_line", buf="buf VS 27 {pal} 2 0"), "bad line")
    neg("u_background_size", u.manifest("u_background_size", bg="u_bg_short.rgba"), "W x H x 4")
    neg("u_background_missing", u.manifest("u_background_missing", bg="nope.rgba"), "cannot read")
    f = u.manifest("u_background_msaa", bg="u_bg.rgba")
    f.write_text(f.read_text().replace("target %d %d 1 " % (W, H), "target %d %d 4 " % (W, H)))
    neg("u_background_msaa", f, "SAMPLES 1")
    f = u.manifest("u_cube_arr", ps="u_ps_cube.hlsl", cube=True)
    f.write_text(f.read_text().replace(" 28 28 4 2 2 1 1 6 6", " 28 28 4 2 2 1 1 5 5").replace("sub 80 8 16\n", ""))
    neg("u_cube_arr", f, "a cube needs")
    f = u.manifest("u_cube_dim", ps="u_ps_cube.hlsl", cube=True)
    f.write_text(f.read_text().replace(" 28 28 4 2 2 1 1 6 6", " 28 28 5 2 2 1 1 6 6"))
    neg("u_cube_dim", f, "bad texture shape")


def fake_dxbc() -> bytes:
    """Issue 0347: a DXBC container holding only an output signature (SV_Position, float4, register 0): what manifest.py
    reads of a shader (osgn) with --no-run; nothing compiles or runs it."""
    name = b"SV_Position\0"
    body = struct.pack("<2I", 1, 8) + struct.pack("<5I4B", 8 + 24, 0, 1, 3, 0, 0xF, 0, 0, 0) + name
    chunk = b"OSGN" + struct.pack("<I", len(body)) + body
    head = b"DXBC" + bytes(16) + struct.pack("<3I", 1, 36 + len(chunk), 1) + struct.pack("<I", 36)
    return head + chunk


def buffer_export_cases(t):
    """Issue 0347: manifest.py on a synthetic `--select all`-style export with buffer SRVs. A BUFFER view of a format the
    `buf` directive takes becomes a buf line (VS and PS t27, as the bone palette); a structured buffer (format 0), a
    BUFFEREX view, a view past the file's end and a buffer without a view are skipped with a warning naming draw, stage
    and slot, listed once in replay.json's skipped_srvs; a texture SRV of the same draw still gives its tex line."""
    ex = t.tmp / "bufexport"
    for sub in ("draws/0003_draw", "shaders", "buffers", "textures"):
        (ex / sub).mkdir(parents=True, exist_ok=True)
    (ex / "shaders/s.bin").write_bytes(fake_dxbc())
    (ex / "buffers/pal.bin").write_bytes(struct.pack("<16f", *range(16)))   # 4 x R32G32B32A32
    (ex / "textures/t0.bin").write_bytes(bytes(4))

    def bsrv(fmt, dim, first, num, size=64, view=True):
        d = {"data": {"res": 9, "desc": {"ByteWidth": size, "StructureByteStride": 0}, "size": size, "file": "buffers/pal.bin"}}
        if view:
            d["view_desc"] = {"Format": {"v": fmt}, "ViewDimension": {"v": dim}, "Buffer": {"FirstElement": first, "NumElements": num}}
        return d
    tex = {"view_desc": {"Format": {"v": 28}, "ViewDimension": {"v": 4}},
           "data": {"kind": "texture2d", "file": "textures/t0.bin", "row_pitch": None,
                    "desc": {"Width": 1, "Height": 1, "MipLevels": 1, "ArraySize": 1, "Format": {"v": 28}}}}
    none = {"shader": None, "cb": {}, "srv": {}, "samp": {}}
    st = {"n": 3, "system": "draw", "args": {"VertexCountPerInstance": 3, "StartVertexLocation": 0},
          "ia": {"topology": {"v": 4}, "layout": [], "vb": {}, "ib": None},
          "VS": {"shader": {"file": "shaders/s.bin"}, "cb": {}, "srv": {"27": bsrv(2, 1, 0, 4)}, "samp": {}},
          "HS": none, "DS": none, "GS": none,
          "PS": {"shader": {"file": "shaders/s.bin"}, "cb": {}, "samp": {},
                 "srv": {"0": tex, "27": bsrv(2, 1, 1, 3), "12": bsrv(0, 1, 0, 4), "13": bsrv(41, 11, 0, 16),
                         "14": bsrv(2, 1, 2, 3), "15": bsrv(2, 1, 0, 4, view=False)}},
          "om": {"rtv": [{"res": 5}], "dsv": None},
          "rs": {"viewports": [{"TopLeftX": 0.0, "TopLeftY": 0.0, "Width": 64.0, "Height": 64.0, "MinDepth": 0.0, "MaxDepth": 1.0}]}}
    (ex / "draws/0003_draw/state.json").write_text(json.dumps(st))
    (ex / "draws.json").write_text(json.dumps({"draws": [{"n": 3, "system": "draw", "dir": "draws/0003_draw"}]}))
    Path("C:/c5-64bit-work").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="0347-test-", dir="C:/c5-64bit-work") as od:
        out = Path(od) / "m"
        r = subprocess.run([sys.executable, "-I", str(HERE / "manifest.py"), str(ex), str(out), "--no-run",
                            "--variant", "exe", "--variant", "nofog"], capture_output=True, text=True)
        t.check("buf_export_exit0", r.returncode == 0 and "Traceback" not in r.stderr, "exit %d %s" % (r.returncode, r.stderr.strip()[-200:]))
        if r.returncode != 0:
            return
        m = (out / "exe.manifest").read_text().splitlines()
        pal = (ex / "buffers/pal.bin").as_posix()
        t.check("buf_export_vs_t27", "buf VS 27 %s 2 0 4" % pal in m, [x for x in m if x.startswith("buf")])
        t.check("buf_export_ps_t27", "buf PS 27 %s 2 1 3" % pal in m)
        t.check("buf_export_texture_kept", any(x.startswith("tex PS 0 ") and x.endswith(" 28 28 2 1 1 1 1 1 1") for x in m))
        t.check("buf_export_skipped_no_line", not any(x.split()[:3] in (["buf", "PS", s], ["tex", "PS", s])
                                                      for x in m for s in ("12", "13", "14", "15")))
        t.check("buf_export_nofog_same_srvs", [x for x in (out / "nofog.manifest").read_text().splitlines() if x[:4] in ("buf ", "tex ")]
                == [x for x in m if x[:4] in ("buf ", "tex ")])
        warn = [x for x in r.stderr.splitlines() if "warning" in x]
        want = {"t12": "format 0 not handled", "t13": "view dimension 11", "t14": "pass the file's 64 bytes",
                "t15": "view dimension None"}
        for slot, why in want.items():
            t.check("buf_export_warn_%s" % slot, sum(("draw 3 PS %s:" % slot) in x and why in x for x in warn) == 1, warn)
        t.check("buf_export_warned_once", len(warn) == 4, "%d warnings for 2 variants" % len(warn))
        sk = json.loads((out / "replay.json").read_text()).get("skipped_srvs") or []
        t.check("buf_export_replay_json", sorted((s["draw"], s["stage"], s["slot"]) for s in sk) ==
                [(3, "PS", 12), (3, "PS", 13), (3, "PS", 14), (3, "PS", 15)], sk)


def cube_export_cases(t):
    """Issue 4355 (0347's open point): manifest.py writes a TEXTURECUBE / TEXTURECUBEARRAY view (9, 10) as the replayer's
    DIM 4; a 2D array of 6 slices (view 5) stays DIM 2, and so does a cube view of non-square faces (negative case)."""
    ex = t.tmp / "cubeexport"
    for sub in ("draws/0004_draw", "shaders", "textures"):
        (ex / sub).mkdir(parents=True, exist_ok=True)
    (ex / "shaders/s.bin").write_bytes(fake_dxbc())
    (ex / "textures/c.bin").write_bytes(bytes(4 * 6 * 2))

    def srv(view_dim, w=1, h=1):
        return {"view_desc": {"Format": {"v": 28}, "ViewDimension": {"v": view_dim}},
                "data": {"kind": "texture2d", "file": "textures/c.bin", "row_pitch": None,
                         "desc": {"Width": w, "Height": h, "MipLevels": 1, "ArraySize": 6, "Format": {"v": 28}}}}
    none = {"shader": None, "cb": {}, "srv": {}, "samp": {}}
    st = {"n": 4, "system": "draw", "args": {"VertexCountPerInstance": 3, "StartVertexLocation": 0},
          "ia": {"topology": {"v": 4}, "layout": [], "vb": {}, "ib": None},
          "VS": {"shader": {"file": "shaders/s.bin"}, "cb": {}, "srv": {}, "samp": {}},
          "HS": none, "DS": none, "GS": none,
          "PS": {"shader": {"file": "shaders/s.bin"}, "cb": {}, "samp": {},
                 "srv": {"2": srv(9), "3": srv(10), "4": srv(5), "5": srv(9, 2, 1)}},
          "om": {"rtv": [{"res": 5}], "dsv": None},
          "rs": {"viewports": [{"TopLeftX": 0.0, "TopLeftY": 0.0, "Width": 64.0, "Height": 64.0, "MinDepth": 0.0, "MaxDepth": 1.0}]}}
    (ex / "draws/0004_draw/state.json").write_text(json.dumps(st))
    (ex / "draws.json").write_text(json.dumps({"draws": [{"n": 4, "system": "draw", "dir": "draws/0004_draw"}]}))
    Path("C:/c5-64bit-work").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="4355-test-", dir="C:/c5-64bit-work") as od:
        out = Path(od) / "m"
        r = subprocess.run([sys.executable, "-I", str(HERE / "manifest.py"), str(ex), str(out), "--no-run",
                            "--variant", "exe"], capture_output=True, text=True)
        t.check("cube_export_exit0", r.returncode == 0 and "Traceback" not in r.stderr, "exit %d %s" % (r.returncode, r.stderr.strip()[-200:]))
        if r.returncode != 0:
            return
        heads = {x.split()[2]: x.split() for x in (out / "exe.manifest").read_text().splitlines() if x.startswith("tex PS ")}
        t.check("cube_view_dim4", heads.get("2", [""] * 7)[6] == "4", heads.get("2"))
        t.check("cube_array_view_dim4", heads.get("3", [""] * 7)[6] == "4", heads.get("3"))
        t.check("array_view_stays_dim2", heads.get("4", [""] * 7)[6] == "2", heads.get("4"))
        t.check("nonsquare_cube_stays_dim2", heads.get("5", [""] * 7)[6] == "2", heads.get("5"))
        t.check("cube_six_sub_lines", all(len(h) == 13 and h[-1] == "6" for h in heads.values()), list(heads.values()))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    a = ap.parse_args()
    exe = Path(a.exe)
    if not exe.is_file():
        print("FAIL terrainreplay.exe not found: %s" % exe)
        return 1
    with tempfile.TemporaryDirectory(prefix="c5r-terrainreplay-") as td:
        t = T(exe, Path(td))
        t.files()
        # positive: tessellated patch with t1
        r = t.run(t.manifest("with_t1"))
        t.check("replay_with_t1_exit0", r.returncode == 0, r.stderr.strip())
        if r.returncode == 0:
            img = np.fromfile(t.tmp / "with_t1.rgba", np.uint8).reshape(H, W, 4)
            for q, (x, y) in {"tl": (16, 16), "tr": (48, 16), "bl": (16, 48), "br": (48, 48)}.items():
                t.check("t1_composed_%s" % q, close(at(img, x, y), EXPECT[q]), "got %s want %s" % (at(img, x, y), EXPECT[q]))
            z = np.fromfile(t.tmp / "with_t1.f32", np.float32).reshape(H, W)
            t.check("depth_is_0.5_everywhere", bool(np.all(np.abs(z - 0.5) < 1e-6)), "min %g max %g" % (z.min(), z.max()))
            ids = np.fromfile(t.tmp / "with_t1.u32", np.uint32).reshape(H, W)
            t.check("id_is_draw_0_everywhere", bool(np.all(ids == 0)), "values %s" % np.unique(ids)[:4])
        # positive: t1 transparent gives the base colour everywhere (the `nooverlay` variant)
        r = t.run(t.manifest("no_t1", t1="clear.bin", outs=False, tail=("end", "out_color %s" % (t.tmp / "no_t1.rgba").as_posix())))
        t.check("replay_without_t1_exit0", r.returncode == 0, r.stderr.strip())
        if r.returncode == 0:
            img = np.fromfile(t.tmp / "no_t1.rgba", np.uint8).reshape(H, W, 4)
            t.check("transparent_t1_is_base", bool(np.all(img[..., :3] == (0, 0, 255))), "unique %s" % np.unique(img[..., :3].reshape(-1, 3), axis=0)[:3].tolist())
        # positive: the hull and domain shaders run with factor 4: the provoking vertices' domain locations are quarter
        # steps, at least four distinct ones per axis (an untessellated quad would have only 0 and 1)
        r = t.run(t.manifest("dl", ps="ps_dl.hlsl", ds="ds_dl.hlsl", outs=False,
                             tail=("end", "out_color %s" % (t.tmp / "dl.rgba").as_posix())))
        t.check("replay_domain_locations_exit0", r.returncode == 0, r.stderr.strip())
        if r.returncode == 0:
            img = np.fromfile(t.tmp / "dl.rgba", np.uint8).reshape(H, W, 4)
            quarters = {0, 64, 128, 191, 255}
            for c, axis in ((0, "u"), (1, "v")):
                vals = set(int(v) for v in np.unique(img[..., c]))
                t.check("tessellated_%s_quarter_steps" % axis, vals <= quarters and len(vals) >= 4, "values %s" % sorted(vals))

        # negatives: exit code and no output written
        def neg(name, manifest_lines_or_file, code=1, needle=""):
            if isinstance(manifest_lines_or_file, list):
                f = t.tmp / (name + ".manifest")
                f.write_text("\n".join(manifest_lines_or_file) + "\n")
            else:
                f = manifest_lines_or_file
            for o in t.outputs(name):
                if o.exists():
                    o.unlink()
            r = t.run(f)
            written = [o.name for o in t.outputs(name) if o.exists()]
            t.check("neg_" + name, r.returncode == code and not written and needle in r.stderr,
                    "exit %d, stderr %r, written %s" % (r.returncode, r.stderr.strip()[:160], written))

        base = t.manifest("n").read_text().splitlines()

        def variant(name, f):
            lines = [ln.replace("/n.", "/%s." % name) for ln in base]
            return f(lines)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        t.check("neg_no_argument", r.returncode == 2, "exit %d" % r.returncode)
        neg("missing_manifest", t.tmp / "does_not_exist.manifest", 1, "cannot open")
        neg("unknown_directive", variant("unknown_directive", lambda ls: ls[:-1] + ["frobnicate 1", "end"]), 1, "unknown directive")
        neg("outside_draw", variant("outside_draw", lambda ls: ls + ["topo 4"]), 1, "outside a draw")
        neg("no_draws", variant("no_draws", lambda ls: ls[:ls.index("draw 7")]), 1, "no target or no draws")
        neg("no_end", variant("no_end", lambda ls: ls[:-1]), 1, "no end")
        neg("bad_number", variant("bad_number", lambda ls: [x if not x.startswith("call") else "call 0 four 0 0" for x in ls]), 1, "bad line")
        neg("missing_shader", variant("missing_shader", lambda ls: [x.replace("ds.hlsl", "nope.hlsl") for x in ls]), 1, "cannot read")
        neg("hlsl_error", variant("hlsl_error", lambda ls: [x.replace("ps.hlsl main", "bad.hlsl main") for x in ls]), 1, "HLSL")
        neg("short_texture", variant("short_texture", lambda ls: [x.replace("t1.bin", "short.bin") for x in ls]), 1, "shorter than")
        neg("sub_count", variant("sub_count", lambda ls: [x.replace("2 2 1 1 1 1", "2 2 1 2 1 1") for x in ls]), 1, "MIPS x ARR")
        neg("bad_stage", variant("bad_stage", lambda ls: [x.replace("cb PS 0", "cb XS 0") for x in ls]), 1, "unknown stage")
        neg("indexed_without_ib", variant("indexed_without_ib", lambda ls: [x.replace("call 0 4 0 0", "call 1 4 0 0") for x in ls]), 1, "no ib")
        neg("bad_device", variant("bad_device", lambda ls: ["device quantum"] + ls[1:]), 1, "device must be")
        neg("bad_target", variant("bad_target", lambda ls: [x if not x.startswith("target") else "target 0 64 1 0 0 0 0" for x in ls]), 1, "bad target")
        neg("id_without_ps", variant("id_without_ps", lambda ls: [x for x in ls if not x.startswith("id_ps")]), 1, "out_id needs id_ps")

        # the Python front ends: input checks and their exit codes
        py = [sys.executable, "-I"]
        r = subprocess.run(py + [str(HERE / "manifest.py"), str(t.tmp / "noexport"), str(t.tmp / "out"), "--exe", str(exe),
                                 "--variant", "exe"], capture_output=True, text=True)
        t.check("neg_manifest_out_outside_work", r.returncode == 4 and not (t.tmp / "out").exists(), "exit %d" % r.returncode)
        r = subprocess.run(py + [str(HERE / "manifest.py"), str(t.tmp / "noexport"), "C:/c5-64bit-work/0182-test-never-created",
                                 "--exe", str(exe)], capture_output=True, text=True)
        t.check("neg_manifest_missing_export", r.returncode == 3 and not Path("C:/c5-64bit-work/0182-test-never-created").exists(),
                "exit %d" % r.returncode)
        r = subprocess.run(py + [str(HERE / "export.py"), str(t.tmp / "none.rdc"), str(t.tmp / "x"), "--xml-dir", str(t.tmp / "xml")],
                           capture_output=True, text=True)
        t.check("neg_export_outside_work", r.returncode == 4 and not (t.tmp / "x").exists(), "exit %d" % r.returncode)
        r = subprocess.run(py + [str(HERE / "export.py"), str(t.tmp / "none.rdc"), "C:/c5-64bit-work/0182-test-never-created",
                                 "--xml-dir", "C:/c5-64bit-work/0182-test-never-created/xml"], capture_output=True, text=True)
        t.check("neg_export_missing_capture", r.returncode == 3 and not Path("C:/c5-64bit-work/0182-test-never-created").exists(),
                "exit %d" % r.returncode)
        # compare.py on a fake replay folder: missing files, size mismatch, bad --map, OUT_DIR outside the work folders
        from PIL import Image
        rep = t.tmp / "rep"
        rep.mkdir()
        Image.fromarray(np.zeros((H, W, 3), np.uint8)).save(t.tmp / "ref.png")
        (rep / "replay.json").write_text(json.dumps({"size": [W, H], "viewproj": np.eye(4).tolist()}))
        never = "C:/c5-64bit-work/0182-test-never-created"
        cmp = py + [str(HERE / "compare.py")]
        r = subprocess.run(cmp + [str(rep), str(t.tmp), str(t.tmp / "ref.png"), str(t.tmp / "cmp"), "--map", "56x40"], capture_output=True, text=True)
        t.check("neg_compare_out_outside_work", r.returncode == 5 and not (t.tmp / "cmp").exists(), "exit %d" % r.returncode)
        r = subprocess.run(cmp + [str(rep), str(t.tmp), str(t.tmp / "ref.png"), never, "--map", "56x40"], capture_output=True, text=True)
        t.check("neg_compare_missing_replay", r.returncode == 3 and not Path(never).exists(), "exit %d %s" % (r.returncode, r.stderr.strip()[-120:]))
        (rep / "replay.json").write_text(json.dumps({"size": [W + 1, H], "viewproj": np.eye(4).tolist()}))
        r = subprocess.run(cmp + [str(rep), str(t.tmp), str(t.tmp / "ref.png"), never, "--map", "56x40"], capture_output=True, text=True)
        t.check("neg_compare_size_mismatch", r.returncode == 4 and not Path(never).exists(), "exit %d" % r.returncode)
        r = subprocess.run(cmp + [str(rep), str(t.tmp), str(t.tmp / "ref.png"), never, "--map", "big"], capture_output=True, text=True)
        t.check("neg_compare_bad_map", r.returncode == 2 and not Path(never).exists(), "exit %d" % r.returncode)
        # compare.py's statistics on known arrays
        sys.path.insert(0, str(HERE))
        import compare as C
        a = np.zeros((2, 2, 3), np.uint8)
        b = a.copy()
        b[0, 0] = (3, 0, 0)
        b[1, 1] = (0, 40, 10)
        s = C.stats(a, b, np.ones((2, 2), bool))
        t.check("stats_known_values", s["median"] == 1.5 and s["max"] == 40 and s["equal"] == 0.5 and s["within_4"] == 0.75,
                json.dumps(s))
        t.check("stats_empty_mask", C.stats(a, b, np.zeros((2, 2), bool)) == {"pixels": 0})
        unit_draw_cases(t)   # issue 4152
        buffer_export_cases(t)   # issue 0347
        cube_export_cases(t)     # issue 4355
        print("%d cases, %d failed" % (t.n, len(t.fails)))
        return 1 if t.fails else 0


if __name__ == "__main__":
    sys.exit(main())
