// terrainreplay (issue 0182): replay a list of captured D3D11 draws (VS/HS/DS/GS/PS, tessellated or not) into one
// target with one shared depth buffer, from a plain-text manifest written by manifest.py. Outputs the target (RGBA8,
// raw), the depth (float32, raw) and, with an id pixel shader, the index of the draw that owns each pixel (uint32, raw;
// 0xFFFFFFFF = none). A measurement tool: it holds no renderer code and no game data (the manifest names the capture's
// files, which stay under C:/ or D:/c5-64bit-work/, D83).
//
// Origin: this repository, render/evidence/3019/terrain_replay.cpp at commit 1234c563 (issue 3019), extended by 0182
// with the `device` and `shader_hlsl` directives, a size check of every texture subresource and exit codes. Issue 4152
// (epic 0327, unit draws; granted by the MC, queue 1446 and 1463) added, without changing anything existing: the
// optional INSTANCES [START_INSTANCE] of `call` (DrawIndexedInstanced / DrawInstanced), the `buf` directive (a buffer
// SRV, e.g. the unit draws' bone palette t27), `tex` DIM 4 (a cube texture; DIM 2 with ARR 6 stays a 2D array) and
// the `background` directive (the colour target's starting contents).
//
//   terrainreplay MANIFEST
//
// Exit codes: 0 done; 1 bad manifest or input file (message names it; no output file is written); 2 usage;
// 3 a D3D11 call failed.
//
// Manifest lines (whitespace separated; one directive per line; '#' starts a comment line):
//   device hardware|warp                default hardware (the tests use warp)
//   target W H SAMPLES R G B A          clear colour 0..1
//   background PATH                     (4152) raw RGBA8, W x H x 4 bytes, the target's contents before the first
//                                       draw (SAMPLES 1 only)
//   out_color PATH | out_depth PATH | out_id PATH | id_ps PATH (HLSL text of the id pixel shader, entry "main",
//                                       input signature = the DS / VS output; cbuffer b13 { uint id; })
//   draw N  ...  end                    one captured draw; inside:
//     topo T                            D3D11_PRIMITIVE_TOPOLOGY (33..64 = 1..32 control point patch lists; the terrain: 48)
//     elem SEM IDX FMT SLOT OFF CLASS STEP
//     vb SLOT STRIDE OFFSET PATH
//     ib FMT OFFSET PATH
//     call INDEXED COUNT START BASE [INSTANCES [START_INSTANCE]]
//                                       (4152) INSTANCES given (1..65536): an instanced draw
//     shader VS|HS|DS|GS|PS PATH        DXBC bytecode
//     shader_hlsl VS|HS|DS|GS|PS PATH ENTRY PROFILE   HLSL text compiled at load (tests, hand-made draws)
//     cb STAGE SLOT PATH
//     tex STAGE SLOT PATH RESFMT VIEWFMT DIM W H D MIPS ARR NSUB, then NSUB lines "sub OFFSET ROWPITCH SLICEPITCH"
//                                       DIM 2 = 2D, 3 = 3D, (4152) 4 = cube (square, ARR a multiple of 6);
//                                       subresources in D3D11 order (array slice, then mip)
//     buf STAGE SLOT PATH FMT FIRST NUM (4152) a buffer SRV of format FMT (4, 8 or 16 bytes per element, elemBytes())
//                                       over elements FIRST .. FIRST + NUM - 1 of the file's bytes
//     samp STAGE SLOT FILTER AU AV AW BIAS MAXANISO CMP B0 B1 B2 B3 MINLOD MAXLOD
//     blend EN SRC DST OP SRCA DSTA OPA MASK A2C F0 F1 F2 F3 SAMPLEMASK
//     depth EN WRITE FUNC SEN SREAD SWRITE FFAIL FDFAIL FPASS FFUNC BFAIL BDFAIL BPASS BFUNC REF
//     raster FILL CULL FCCW DBIAS DBIASCLAMP SLOPE DCLIP MSAA
//     viewport X Y W H MIN MAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Sub { size_t off = 0; UINT pitch = 0, slice = 0; };
struct Tex {
    std::string stage;
    UINT slot = 0;
    std::string path;
    DXGI_FORMAT res = DXGI_FORMAT_UNKNOWN, view = DXGI_FORMAT_UNKNOWN;
    int dim = 2;
    UINT w = 1, h = 1, d = 1, mips = 1, arr = 1;
    std::vector<Sub> subs;
};
struct Cb { std::string stage; UINT slot = 0; std::string path; };
struct BufSrv { std::string stage; UINT slot = 0; std::string path; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; UINT first = 0, num = 0; };
struct Samp { std::string stage; UINT slot = 0; D3D11_SAMPLER_DESC d = {}; };
struct Vb { UINT slot = 0, stride = 0, offset = 0; std::string path; };
struct Shader { std::string path, entry, profile; bool hlsl = false; };
struct Draw {
    int n = 0;
    UINT topo = 0;
    std::vector<std::string> semNames;
    std::vector<D3D11_INPUT_ELEMENT_DESC> elems;
    std::vector<Vb> vbs;
    bool haveIb = false;
    DXGI_FORMAT ibFmt = DXGI_FORMAT_R16_UINT;
    UINT ibOff = 0;
    std::string ibPath;
    bool indexed = true;
    UINT count = 0, start = 0;
    INT base = 0;
    UINT instances = 0, startInstance = 0;  // instances 0: not an instanced draw
    std::vector<BufSrv> bufsrvs;
    std::map<std::string, Shader> shaders;
    std::vector<Cb> cbs;
    std::vector<Tex> texs;
    std::vector<Samp> samps;
    D3D11_BLEND_DESC blend = {};
    float blendFactor[4] = {0, 0, 0, 0};
    UINT sampleMask = 0xFFFFFFFFu;
    D3D11_DEPTH_STENCIL_DESC depth = {};
    UINT stencilRef = 0;
    D3D11_RASTERIZER_DESC raster = {};
    D3D11_VIEWPORT vp = {};
};

[[noreturn]] void die(const std::string& m, int code = 1) {
    std::fprintf(stderr, "terrainreplay: %s\n", m.c_str());
    std::exit(code);
}

void ok(HRESULT h, const std::string& what) {
    if (FAILED(h)) {
        char b[64];
        std::snprintf(b, sizeof b, " failed 0x%08lx", static_cast<unsigned long>(h));
        die(what + b, 3);
    }
}

const std::vector<uint8_t>& file(const std::string& p) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(p);
    if (it != cache.end()) return it->second;
    std::ifstream f(p, std::ios::binary);
    if (!f) die("cannot read " + p);
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return cache.emplace(p, std::move(v)).first->second;
}

void writeFile(const std::string& p, const void* d, size_t n) {
    std::ofstream f(p, std::ios::binary);
    if (!f) die("cannot write " + p);
    f.write(static_cast<const char*>(d), static_cast<std::streamsize>(n));
}

// Bytes of one row (one block row for BC formats) of a mip w texels wide; 0 when the format is not in the table.
size_t rowBytes(DXGI_FORMAT f, UINT w) {
    const size_t blocks = (static_cast<size_t>(w) + 3) / 4;
    switch (f) {
        case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
            return blocks * 8;
        case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
            return blocks * 16;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return static_cast<size_t>(w) * 16;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R32G32_FLOAT:
            return static_cast<size_t>(w) * 8;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R10G10B10A2_UNORM:
            return static_cast<size_t>(w) * 4;
        case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
            return static_cast<size_t>(w) * 2;
        case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM:
            return w;
        default:
            return 0;
    }
}

bool isStage(const std::string& s) { return s == "VS" || s == "HS" || s == "DS" || s == "GS" || s == "PS"; }

// Bytes of one element of a buffer SRV format; 0 when the format is not handled.
UINT elemBytes(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return 16;
        case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
        case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
        default: return 0;
    }
}

// Bytecode of a shader: the DXBC file, or the HLSL file compiled now.
std::vector<uint8_t> bytecode(const Shader& s) {
    if (!s.hlsl) return file(s.path);
    const auto& src = file(s.path);
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    const HRESULT h = D3DCompile(src.data(), src.size(), s.path.c_str(), nullptr, nullptr, s.entry.c_str(), s.profile.c_str(),
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(h)) {
        std::string m = "HLSL " + s.path + " (" + s.entry + ", " + s.profile + "): ";
        m += err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "compile failed";
        die(m);
    }
    const auto* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
    std::vector<uint8_t> v(p, p + blob->GetBufferSize());
    blob->Release();
    if (err) err->Release();
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: terrainreplay MANIFEST\n");
        return 2;
    }
    std::ifstream mf(argv[1]);
    if (!mf) die(std::string("cannot open ") + argv[1]);
    UINT W = 0, H = 0, samples = 1;
    float clear[4] = {0, 0, 0, 0};
    bool warp = false;
    std::string outColor, outDepth, outId, idPs, background;
    std::vector<Draw> draws;
    std::string line;
    bool inDraw = false;
    while (std::getline(mf, line)) {
        std::istringstream s(line);
        std::string k;
        if (!(s >> k) || k[0] == '#') continue;
        if (k == "device") {
            std::string v;
            s >> v;
            if (v != "hardware" && v != "warp") die("device must be hardware or warp: " + line);
            warp = v == "warp";
        } else if (k == "target") {
            s >> W >> H >> samples >> clear[0] >> clear[1] >> clear[2] >> clear[3];
            if (!s.fail() && (W == 0 || H == 0 || W > 16384 || H > 16384 || samples == 0)) die("bad target size: " + line);
        } else if (k == "out_color") s >> outColor;
        else if (k == "out_depth") s >> outDepth;
        else if (k == "background") s >> background;
        else if (k == "out_id") s >> outId;
        else if (k == "id_ps") s >> idPs;
        else if (k == "draw") {
            if (inDraw) die("draw inside a draw (missing end): " + line);
            if (!W || !H) die("draw before target: " + line);
            draws.emplace_back();
            inDraw = true;
            Draw& d = draws.back();
            s >> d.n;
            d.blend.RenderTarget[0].SrcBlend = d.blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            d.blend.RenderTarget[0].DestBlend = d.blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            d.blend.RenderTarget[0].BlendOp = d.blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            d.blend.RenderTarget[0].RenderTargetWriteMask = 15;
            d.depth.DepthEnable = TRUE;
            d.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            d.depth.DepthFunc = D3D11_COMPARISON_LESS;
            d.raster.FillMode = D3D11_FILL_SOLID;
            d.raster.CullMode = D3D11_CULL_BACK;
            d.raster.DepthClipEnable = TRUE;
            d.vp = D3D11_VIEWPORT{0, 0, static_cast<float>(W), static_cast<float>(H), 0, 1};
        } else if (k == "end") {
            if (!inDraw) die("end without draw: " + line);
            inDraw = false;
        } else {
            if (!inDraw) die("directive outside a draw: " + line);
            Draw& d = draws.back();
            if (k == "topo") s >> d.topo;
            else if (k == "elem") {
                std::string sem;
                UINT idx = 0, fmt = 0, slot = 0, off = 0, cls = 0, step = 0;
                s >> sem >> idx >> fmt >> slot >> off >> cls >> step;
                d.semNames.push_back(sem);
                d.elems.push_back(D3D11_INPUT_ELEMENT_DESC{nullptr, idx, static_cast<DXGI_FORMAT>(fmt), slot, off,
                                                           static_cast<D3D11_INPUT_CLASSIFICATION>(cls), step});
            } else if (k == "vb") {
                Vb v;
                s >> v.slot >> v.stride >> v.offset >> v.path;
                d.vbs.push_back(v);
            } else if (k == "ib") {
                UINT f = 0;
                s >> f >> d.ibOff >> d.ibPath;
                d.ibFmt = static_cast<DXGI_FORMAT>(f);
                d.haveIb = true;
            } else if (k == "call") {
                int ix = 0;
                s >> ix >> d.count >> d.start >> d.base;
                d.indexed = ix != 0;
                if (s.fail()) die("bad line: " + line);
                std::string rest[3];
                s >> rest[0] >> rest[1] >> rest[2];
                if (!rest[2].empty()) die("trailing text: " + line);
                if (!rest[0].empty()) {  // INSTANCES [START_INSTANCE]
                    char* e = nullptr;
                    const long long inst = std::strtoll(rest[0].c_str(), &e, 10);
                    if (*e || inst < 1 || inst > 65536) die("instances must be 1..65536: " + line);
                    d.instances = static_cast<UINT>(inst);
                    if (!rest[1].empty()) {
                        const long long si = std::strtoll(rest[1].c_str(), &e, 10);
                        if (*e || si < 0 || si > 0x7FFFFFFF) die("bad start instance: " + line);
                        d.startInstance = static_cast<UINT>(si);
                    }
                }
                s.clear();
            } else if (k == "shader" || k == "shader_hlsl") {
                std::string st;
                Shader sh;
                s >> st >> sh.path;
                if (k == "shader_hlsl") {
                    s >> sh.entry >> sh.profile;
                    sh.hlsl = true;
                }
                if (!s.fail() && !isStage(st)) die("unknown stage: " + line);
                d.shaders[st] = sh;
            } else if (k == "cb") {
                Cb c;
                s >> c.stage >> c.slot >> c.path;
                if (!s.fail() && !isStage(c.stage)) die("unknown stage: " + line);
                d.cbs.push_back(c);
            } else if (k == "tex") {
                Tex t;
                UINT rf = 0, vf = 0, ns = 0;
                s >> t.stage >> t.slot >> t.path >> rf >> vf >> t.dim >> t.w >> t.h >> t.d >> t.mips >> t.arr >> ns;
                if (s.fail()) die("bad line: " + line);
                if (!isStage(t.stage)) die("unknown stage: " + line);
                if ((t.dim != 2 && t.dim != 3 && t.dim != 4) || !t.w || !t.h || !t.d || !t.mips || !t.arr) die("bad texture shape: " + line);
                if (t.dim == 4 && (t.arr % 6 || t.w != t.h)) die("a cube needs square faces and ARR a multiple of 6: " + line);
                if (ns != t.mips * t.arr) die("texture needs MIPS x ARR sub lines: " + line);
                t.res = static_cast<DXGI_FORMAT>(rf);
                t.view = static_cast<DXGI_FORMAT>(vf);
                for (UINT i = 0; i < ns; ++i) {
                    std::string l2, kk;
                    if (!std::getline(mf, l2)) die("short sub list: " + line);
                    std::istringstream s2(l2);
                    Sub x;
                    s2 >> kk >> x.off >> x.pitch >> x.slice;
                    if (kk != "sub" || s2.fail()) die("expected sub: " + l2);
                    t.subs.push_back(x);
                }
                d.texs.push_back(t);
            } else if (k == "buf") {
                BufSrv b;
                UINT f = 0;
                s >> b.stage >> b.slot >> b.path >> f >> b.first >> b.num;
                if (s.fail()) die("bad line: " + line);
                if (!isStage(b.stage)) die("unknown stage: " + line);
                b.fmt = static_cast<DXGI_FORMAT>(f);
                if (elemBytes(b.fmt) == 0) die("buf format not handled: " + line);
                if (b.num == 0) die("buf needs at least one element: " + line);
                d.bufsrvs.push_back(b);
            } else if (k == "samp") {
                Samp p;
                UINT f = 0, au = 0, av = 0, aw = 0, ma = 0, cmp = 0;
                s >> p.stage >> p.slot >> f >> au >> av >> aw >> p.d.MipLODBias >> ma >> cmp >> p.d.BorderColor[0] >>
                    p.d.BorderColor[1] >> p.d.BorderColor[2] >> p.d.BorderColor[3] >> p.d.MinLOD >> p.d.MaxLOD;
                p.d.Filter = static_cast<D3D11_FILTER>(f);
                p.d.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(au);
                p.d.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(av);
                p.d.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(aw);
                p.d.MaxAnisotropy = ma;
                p.d.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(cmp);
                d.samps.push_back(p);
            } else if (k == "blend") {
                UINT en = 0, a = 0, b = 0, c = 0, e = 0, f = 0, g = 0, m = 0, a2c = 0;
                s >> en >> a >> b >> c >> e >> f >> g >> m >> a2c >> d.blendFactor[0] >> d.blendFactor[1] >> d.blendFactor[2] >>
                    d.blendFactor[3] >> d.sampleMask;
                auto& r = d.blend.RenderTarget[0];
                r.BlendEnable = en;
                r.SrcBlend = static_cast<D3D11_BLEND>(a);
                r.DestBlend = static_cast<D3D11_BLEND>(b);
                r.BlendOp = static_cast<D3D11_BLEND_OP>(c);
                r.SrcBlendAlpha = static_cast<D3D11_BLEND>(e);
                r.DestBlendAlpha = static_cast<D3D11_BLEND>(f);
                r.BlendOpAlpha = static_cast<D3D11_BLEND_OP>(g);
                r.RenderTargetWriteMask = static_cast<UINT8>(m);
                d.blend.AlphaToCoverageEnable = a2c;
            } else if (k == "depth") {
                UINT en = 0, wr = 0, fn = 0, sen = 0, sr = 0, sw = 0, a = 0, b = 0, c = 0, e = 0, f = 0, g = 0, h = 0, i = 0;
                s >> en >> wr >> fn >> sen >> sr >> sw >> a >> b >> c >> e >> f >> g >> h >> i >> d.stencilRef;
                d.depth.DepthEnable = en;
                d.depth.DepthWriteMask = static_cast<D3D11_DEPTH_WRITE_MASK>(wr);
                d.depth.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(fn);
                d.depth.StencilEnable = sen;
                d.depth.StencilReadMask = static_cast<UINT8>(sr);
                d.depth.StencilWriteMask = static_cast<UINT8>(sw);
                d.depth.FrontFace = {static_cast<D3D11_STENCIL_OP>(a), static_cast<D3D11_STENCIL_OP>(b), static_cast<D3D11_STENCIL_OP>(c),
                                     static_cast<D3D11_COMPARISON_FUNC>(e)};
                d.depth.BackFace = {static_cast<D3D11_STENCIL_OP>(f), static_cast<D3D11_STENCIL_OP>(g), static_cast<D3D11_STENCIL_OP>(h),
                                    static_cast<D3D11_COMPARISON_FUNC>(i)};
            } else if (k == "raster") {
                UINT fill = 0, cull = 0, fccw = 0, dclip = 0, msaa = 0;
                INT db = 0;
                s >> fill >> cull >> fccw >> db >> d.raster.DepthBiasClamp >> d.raster.SlopeScaledDepthBias >> dclip >> msaa;
                d.raster.FillMode = static_cast<D3D11_FILL_MODE>(fill);
                d.raster.CullMode = static_cast<D3D11_CULL_MODE>(cull);
                d.raster.FrontCounterClockwise = fccw;
                d.raster.DepthBias = db;
                d.raster.DepthClipEnable = dclip;
                d.raster.MultisampleEnable = msaa;
            } else if (k == "viewport") {
                s >> d.vp.TopLeftX >> d.vp.TopLeftY >> d.vp.Width >> d.vp.Height >> d.vp.MinDepth >> d.vp.MaxDepth;
            } else die("unknown directive: " + line);
        }
        if (s.fail()) die("bad line: " + line);
    }
    if (inDraw) die("last draw has no end");
    if (!W || !H || draws.empty()) die("no target or no draws");
    for (const Draw& d : draws) {
        if (d.shaders.find("VS") == d.shaders.end()) die("draw " + std::to_string(d.n) + " has no VS");
        if (!d.count) die("draw " + std::to_string(d.n) + " has no call");
        if (d.indexed && !d.haveIb) die("draw " + std::to_string(d.n) + " is indexed but has no ib");
    }
    if (!outId.empty() && idPs.empty()) die("out_id needs id_ps");
    for (const Draw& d : draws)
        for (const BufSrv& b : d.bufsrvs) {
            const size_t need = (static_cast<size_t>(b.first) + b.num) * elemBytes(b.fmt);
            if (file(b.path).size() < need) die("buffer file shorter than its SRV: " + b.path);
        }
    if (!background.empty()) {
        if (samples != 1) die("background needs SAMPLES 1");
        if (file(background).size() != static_cast<size_t>(W) * H * 4) die("background is not W x H x 4 bytes: " + background);
    }

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got = D3D_FEATURE_LEVEL_11_0;
    ok(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION,
                         &dev, &got, &ctx),
       "D3D11CreateDevice");

    // targets: colour (RGBA8, optionally multisampled + resolve), depth D32_S8 (typeless for the read-back), id R32_UINT
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W;
    td.Height = H;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* rt = nullptr;
    ok(dev->CreateTexture2D(&td, nullptr, &rt), "CreateTexture2D colour");
    D3D11_RENDER_TARGET_VIEW_DESC rvd = {};
    rvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rvd.ViewDimension = samples > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv = nullptr;
    ok(dev->CreateRenderTargetView(rt, &rvd, &rtv), "CreateRenderTargetView colour");
    D3D11_TEXTURE2D_DESC dd = td;
    dd.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* dt = nullptr;
    ok(dev->CreateTexture2D(&dd, nullptr, &dt), "CreateTexture2D depth");
    D3D11_DEPTH_STENCIL_VIEW_DESC dvd = {};
    dvd.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dvd.ViewDimension = samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    ID3D11DepthStencilView* dsv = nullptr;
    ok(dev->CreateDepthStencilView(dt, &dvd, &dsv), "CreateDepthStencilView");
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    if (!background.empty()) ctx->UpdateSubresource(rt, 0, nullptr, file(background).data(), W * 4, W * H * 4);

    std::map<std::string, std::vector<uint8_t>> codeCache;
    auto code = [&](const Shader& s) -> const std::vector<uint8_t>& {
        const std::string key = s.path + "|" + s.entry + "|" + s.profile;
        auto it = codeCache.find(key);
        if (it == codeCache.end()) it = codeCache.emplace(key, bytecode(s)).first;
        return it->second;
    };
    std::map<std::string, ID3D11DeviceChild*> shaderCache;
    auto getShader = [&](const std::string& st, const Shader& sh) -> ID3D11DeviceChild* {
        const std::string key = st + "|" + sh.path + "|" + sh.entry;
        auto it = shaderCache.find(key);
        if (it != shaderCache.end()) return it->second;
        const auto& b = code(sh);
        ID3D11DeviceChild* o = nullptr;
        const std::string what = "Create" + st + " " + sh.path;
        if (st == "VS") ok(dev->CreateVertexShader(b.data(), b.size(), nullptr, reinterpret_cast<ID3D11VertexShader**>(&o)), what);
        else if (st == "HS") ok(dev->CreateHullShader(b.data(), b.size(), nullptr, reinterpret_cast<ID3D11HullShader**>(&o)), what);
        else if (st == "DS") ok(dev->CreateDomainShader(b.data(), b.size(), nullptr, reinterpret_cast<ID3D11DomainShader**>(&o)), what);
        else if (st == "GS") ok(dev->CreateGeometryShader(b.data(), b.size(), nullptr, reinterpret_cast<ID3D11GeometryShader**>(&o)), what);
        else ok(dev->CreatePixelShader(b.data(), b.size(), nullptr, reinterpret_cast<ID3D11PixelShader**>(&o)), what);
        shaderCache[key] = o;
        return o;
    };
    std::map<std::string, ID3D11Buffer*> bufCache;
    auto getBuffer = [&](const std::string& p, UINT bind) -> ID3D11Buffer* {
        const std::string key = std::to_string(bind) + "|" + p;
        auto it = bufCache.find(key);
        if (it != bufCache.end()) return it->second;
        const auto& b = file(p);
        if (b.empty()) die("empty buffer file " + p);
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = static_cast<UINT>(bind == D3D11_BIND_CONSTANT_BUFFER ? (b.size() + 15) / 16 * 16 : (b.size() < 4 ? 4 : b.size()));
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind;
        std::vector<uint8_t> pad(d.ByteWidth, 0);
        std::memcpy(pad.data(), b.data(), b.size());
        D3D11_SUBRESOURCE_DATA sd = {pad.data(), 0, 0};
        ID3D11Buffer* o = nullptr;
        ok(dev->CreateBuffer(&d, &sd, &o), "CreateBuffer " + p);
        bufCache[key] = o;
        return o;
    };
    std::map<std::string, ID3D11ShaderResourceView*> texCache;
    auto getTex = [&](const Tex& t) -> ID3D11ShaderResourceView* {
        const std::string key = t.path + "|" + std::to_string(t.view);
        auto it = texCache.find(key);
        if (it != texCache.end()) return it->second;
        const auto& b = file(t.path);
        std::vector<D3D11_SUBRESOURCE_DATA> sd;
        for (size_t i = 0; i < t.subs.size(); ++i) {
            const Sub& x = t.subs[i];
            const UINT mip = static_cast<UINT>(i % t.mips);
            const size_t slices = t.dim == 3 ? ((t.d >> mip) ? (t.d >> mip) : 1u) : 1u;
            // D3D11 reads (slices-1) slice pitches + (rows-1) row pitches + one tight row; RenderDoc stores the last row
            // of a small mip tight (e.g. a 32 x 32 BC1 mip: 7 rows of pitch 128 and one of 64 bytes)
            const UINT mw = (t.w >> mip) ? (t.w >> mip) : 1u;
            const size_t tight = rowBytes(t.res, mw);
            if (!x.pitch || !x.slice || x.slice % x.pitch) die("bad pitches for " + t.path);
            const size_t rows = x.slice / x.pitch;
            const size_t last = tight ? tight : 1;
            const size_t need = x.off + (slices - 1) * x.slice + (rows - 1) * x.pitch + last;
            if (need > b.size()) die("texture shorter than its subresources: " + t.path);
            sd.push_back(D3D11_SUBRESOURCE_DATA{b.data() + x.off, x.pitch, x.slice});
        }
        ID3D11Resource* r = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
        vd.Format = t.view;
        if (t.dim == 3) {
            D3D11_TEXTURE3D_DESC d = {t.w, t.h, t.d, t.mips, t.res, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            ok(dev->CreateTexture3D(&d, sd.data(), reinterpret_cast<ID3D11Texture3D**>(&r)), "CreateTexture3D " + t.path);
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            vd.Texture3D.MipLevels = t.mips;
        } else {
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = t.w;
            d.Height = t.h;
            d.MipLevels = t.mips;
            d.ArraySize = t.arr;
            d.Format = t.res;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_IMMUTABLE;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (t.dim == 4) d.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            ok(dev->CreateTexture2D(&d, sd.data(), reinterpret_cast<ID3D11Texture2D**>(&r)), "CreateTexture2D " + t.path);
            if (t.dim == 4 && t.arr == 6) {
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                vd.TextureCube.MipLevels = t.mips;
            } else if (t.dim == 4) {
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                vd.TextureCubeArray.MipLevels = t.mips;
                vd.TextureCubeArray.NumCubes = t.arr / 6;
            } else if (t.arr > 1) {
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                vd.Texture2DArray.MipLevels = t.mips;
                vd.Texture2DArray.ArraySize = t.arr;
            } else {
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                vd.Texture2D.MipLevels = t.mips;
            }
        }
        ID3D11ShaderResourceView* v = nullptr;
        ok(dev->CreateShaderResourceView(r, &vd, &v), "CreateShaderResourceView " + t.path);
        r->Release();
        texCache[key] = v;
        return v;
    };

    std::map<std::string, ID3D11ShaderResourceView*> bufSrvCache;
    auto getBufSrv = [&](const BufSrv& b) -> ID3D11ShaderResourceView* {
        const std::string key = b.path + "|" + std::to_string(b.fmt) + "|" + std::to_string(b.first) + "|" + std::to_string(b.num);
        auto it = bufSrvCache.find(key);
        if (it != bufSrvCache.end()) return it->second;
        ID3D11Buffer* buf = getBuffer(b.path, D3D11_BIND_SHADER_RESOURCE);
        D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
        vd.Format = b.fmt;
        vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        vd.Buffer.FirstElement = b.first;
        vd.Buffer.NumElements = b.num;
        ID3D11ShaderResourceView* v = nullptr;
        ok(dev->CreateShaderResourceView(buf, &vd, &v), "CreateShaderResourceView buffer " + b.path);
        bufSrvCache[key] = v;
        return v;
    };

    auto setState = [&](const Draw& d, bool idPass) {
        ctx->ClearState();
        for (const Vb& v : d.vbs) {
            ID3D11Buffer* b = getBuffer(v.path, D3D11_BIND_VERTEX_BUFFER);
            ctx->IASetVertexBuffers(v.slot, 1, &b, &v.stride, &v.offset);
        }
        if (d.haveIb) ctx->IASetIndexBuffer(getBuffer(d.ibPath, D3D11_BIND_INDEX_BUFFER), d.ibFmt, d.ibOff);
        ctx->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(d.topo));
        if (!d.elems.empty()) {
            std::vector<D3D11_INPUT_ELEMENT_DESC> el = d.elems;
            for (size_t i = 0; i < el.size(); ++i) el[i].SemanticName = d.semNames[i].c_str();
            const auto& vsb = code(d.shaders.at("VS"));
            ID3D11InputLayout* il = nullptr;
            ok(dev->CreateInputLayout(el.data(), static_cast<UINT>(el.size()), vsb.data(), vsb.size(), &il), "CreateInputLayout");
            ctx->IASetInputLayout(il);
            il->Release();
        }
        for (const auto& kv : d.shaders) {
            if (idPass && kv.first == "PS") continue;
            ID3D11DeviceChild* o = getShader(kv.first, kv.second);
            if (kv.first == "VS") ctx->VSSetShader(static_cast<ID3D11VertexShader*>(o), nullptr, 0);
            if (kv.first == "HS") ctx->HSSetShader(static_cast<ID3D11HullShader*>(o), nullptr, 0);
            if (kv.first == "DS") ctx->DSSetShader(static_cast<ID3D11DomainShader*>(o), nullptr, 0);
            if (kv.first == "GS") ctx->GSSetShader(static_cast<ID3D11GeometryShader*>(o), nullptr, 0);
            if (kv.first == "PS") ctx->PSSetShader(static_cast<ID3D11PixelShader*>(o), nullptr, 0);
        }
        for (const Cb& c : d.cbs) {
            ID3D11Buffer* b = getBuffer(c.path, D3D11_BIND_CONSTANT_BUFFER);
            if (c.stage == "VS") ctx->VSSetConstantBuffers(c.slot, 1, &b);
            if (c.stage == "HS") ctx->HSSetConstantBuffers(c.slot, 1, &b);
            if (c.stage == "DS") ctx->DSSetConstantBuffers(c.slot, 1, &b);
            if (c.stage == "GS") ctx->GSSetConstantBuffers(c.slot, 1, &b);
            if (c.stage == "PS" && !idPass) ctx->PSSetConstantBuffers(c.slot, 1, &b);
        }
        for (const Tex& t : d.texs) {
            if (idPass && t.stage == "PS") continue;
            ID3D11ShaderResourceView* v = getTex(t);
            if (t.stage == "VS") ctx->VSSetShaderResources(t.slot, 1, &v);
            if (t.stage == "HS") ctx->HSSetShaderResources(t.slot, 1, &v);
            if (t.stage == "DS") ctx->DSSetShaderResources(t.slot, 1, &v);
            if (t.stage == "GS") ctx->GSSetShaderResources(t.slot, 1, &v);
            if (t.stage == "PS") ctx->PSSetShaderResources(t.slot, 1, &v);
        }
        for (const BufSrv& b : d.bufsrvs) {
            if (idPass && b.stage == "PS") continue;
            ID3D11ShaderResourceView* v = getBufSrv(b);
            if (b.stage == "VS") ctx->VSSetShaderResources(b.slot, 1, &v);
            if (b.stage == "HS") ctx->HSSetShaderResources(b.slot, 1, &v);
            if (b.stage == "DS") ctx->DSSetShaderResources(b.slot, 1, &v);
            if (b.stage == "GS") ctx->GSSetShaderResources(b.slot, 1, &v);
            if (b.stage == "PS") ctx->PSSetShaderResources(b.slot, 1, &v);
        }
        for (const Samp& p : d.samps) {
            if (idPass && p.stage == "PS") continue;
            ID3D11SamplerState* ss = nullptr;
            ok(dev->CreateSamplerState(&p.d, &ss), "CreateSamplerState");
            if (p.stage == "VS") ctx->VSSetSamplers(p.slot, 1, &ss);
            if (p.stage == "HS") ctx->HSSetSamplers(p.slot, 1, &ss);
            if (p.stage == "DS") ctx->DSSetSamplers(p.slot, 1, &ss);
            if (p.stage == "GS") ctx->GSSetSamplers(p.slot, 1, &ss);
            if (p.stage == "PS") ctx->PSSetSamplers(p.slot, 1, &ss);
            ss->Release();
        }
        D3D11_RASTERIZER_DESC rd = d.raster;
        rd.ScissorEnable = FALSE;
        ID3D11RasterizerState* rs = nullptr;
        ok(dev->CreateRasterizerState(&rd, &rs), "CreateRasterizerState");
        ctx->RSSetState(rs);
        rs->Release();
        ctx->RSSetViewports(1, &d.vp);
    };

    auto issue = [&](const Draw& d) {
        if (d.instances) {
            if (d.indexed) ctx->DrawIndexedInstanced(d.count, d.instances, d.start, d.base, d.startInstance);
            else ctx->DrawInstanced(d.count, d.instances, d.start, d.startInstance);
        } else if (d.indexed) {
            ctx->DrawIndexed(d.count, d.start, d.base);
        } else {
            ctx->Draw(d.count, d.start);
        }
    };

    for (const Draw& d : draws) {
        setState(d, false);
        ID3D11BlendState* bs = nullptr;
        ok(dev->CreateBlendState(&d.blend, &bs), "CreateBlendState");
        ctx->OMSetBlendState(bs, d.blendFactor, d.sampleMask);
        bs->Release();
        ID3D11DepthStencilState* ds = nullptr;
        ok(dev->CreateDepthStencilState(&d.depth, &ds), "CreateDepthStencilState");
        ctx->OMSetDepthStencilState(ds, d.stencilRef);
        ds->Release();
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        issue(d);
    }
    ctx->ClearState();

    // Read-backs are collected first and written at the end, so that a failure leaves no partial output.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> outputs;
    auto readBack = [&](ID3D11Texture2D* src, DXGI_FORMAT resolveFmt, UINT bpp) -> std::vector<uint8_t> {
        D3D11_TEXTURE2D_DESC sd;
        src->GetDesc(&sd);
        ID3D11Texture2D* single = src;
        if (sd.SampleDesc.Count > 1) {
            D3D11_TEXTURE2D_DESC r = sd;
            r.SampleDesc.Count = 1;
            ok(dev->CreateTexture2D(&r, nullptr, &single), "CreateTexture2D resolve");
            ctx->ResolveSubresource(single, 0, src, 0, resolveFmt);
        }
        D3D11_TEXTURE2D_DESC st = sd;
        st.SampleDesc.Count = 1;
        st.Usage = D3D11_USAGE_STAGING;
        st.BindFlags = 0;
        st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* stg = nullptr;
        ok(dev->CreateTexture2D(&st, nullptr, &stg), "CreateTexture2D staging");
        ctx->CopyResource(stg, single);
        D3D11_MAPPED_SUBRESOURCE m;
        ok(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m), "Map");
        std::vector<uint8_t> px(static_cast<size_t>(W) * H * bpp);
        for (UINT y = 0; y < H; ++y)
            std::memcpy(px.data() + static_cast<size_t>(y) * W * bpp, static_cast<uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                        static_cast<size_t>(W) * bpp);
        ctx->Unmap(stg, 0);
        stg->Release();
        if (single != src) single->Release();
        return px;
    };
    if (!outColor.empty()) outputs.emplace_back(outColor, readBack(rt, DXGI_FORMAT_R8G8B8A8_UNORM, 4));
    if (!outDepth.empty()) {
        if (samples > 1) std::fprintf(stderr, "terrainreplay: depth read-back of a multisampled target not done\n");
        else {
            const std::vector<uint8_t> raw = readBack(dt, DXGI_FORMAT_UNKNOWN, 8);
            std::vector<uint8_t> z(static_cast<size_t>(W) * H * 4);
            for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) std::memcpy(z.data() + i * 4, raw.data() + i * 8, 4);
            outputs.emplace_back(outDepth, std::move(z));
        }
    }
    if (!outId.empty() && samples == 1) {
        std::ifstream hf(idPs);
        if (!hf) die("cannot read " + idPs);
        std::string src((std::istreambuf_iterator<char>(hf)), std::istreambuf_iterator<char>());
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(src.data(), src.size(), "idps", nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, &err)))
            die(std::string("id PS: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : "compile failed"));
        ID3D11PixelShader* ips = nullptr;
        ok(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ips), "CreatePixelShader id");
        D3D11_TEXTURE2D_DESC it = td;
        it.Format = DXGI_FORMAT_R32_UINT;
        ID3D11Texture2D* idt = nullptr;
        ok(dev->CreateTexture2D(&it, nullptr, &idt), "CreateTexture2D id");
        ID3D11RenderTargetView* idv = nullptr;
        ok(dev->CreateRenderTargetView(idt, nullptr, &idv), "CreateRenderTargetView id");
        // a UINT target's ClearRenderTargetView takes floats: fill it with "no draw" by a full-screen triangle
        {
            static const char kFill[] =
                "float4 vs(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
                "uint ps(float4 p : SV_Position) : SV_Target { return 0xFFFFFFFF; }\n";
            ID3DBlob* vb = nullptr;
            ID3DBlob* pb = nullptr;
            ok(D3DCompile(kFill, sizeof kFill - 1, "fill", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vb, nullptr), "fill vs");
            ok(D3DCompile(kFill, sizeof kFill - 1, "fill", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &pb, nullptr), "fill ps");
            ID3D11VertexShader* fv = nullptr;
            ID3D11PixelShader* fp = nullptr;
            ok(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &fv), "fill vs create");
            ok(dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &fp), "fill ps create");
            ctx->ClearState();
            const D3D11_VIEWPORT full = {0, 0, static_cast<float>(W), static_cast<float>(H), 0, 1};
            ctx->RSSetViewports(1, &full);
            ctx->OMSetRenderTargets(1, &idv, nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(fv, nullptr, 0);
            ctx->PSSetShader(fp, nullptr, 0);
            ctx->Draw(3, 0);
        }
        D3D11_BUFFER_DESC cbd = {16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        ID3D11Buffer* idcb = nullptr;
        ok(dev->CreateBuffer(&cbd, nullptr, &idcb), "CreateBuffer id");
        for (size_t i = 0; i < draws.size(); ++i) {
            const Draw& d = draws[i];
            setState(d, true);
            const UINT v[4] = {static_cast<UINT>(i), 0, 0, 0};
            ctx->UpdateSubresource(idcb, 0, nullptr, v, 0, 0);
            ctx->PSSetShader(ips, nullptr, 0);
            ctx->PSSetConstantBuffers(13, 1, &idcb);
            D3D11_DEPTH_STENCIL_DESC eq = d.depth;
            eq.DepthEnable = TRUE;
            eq.DepthFunc = D3D11_COMPARISON_EQUAL;
            eq.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            eq.StencilEnable = FALSE;
            ID3D11DepthStencilState* ds = nullptr;
            ok(dev->CreateDepthStencilState(&eq, &ds), "CreateDepthStencilState id");
            ctx->OMSetDepthStencilState(ds, 0);
            ds->Release();
            ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
            ctx->OMSetRenderTargets(1, &idv, dsv);
            issue(d);
        }
        ctx->ClearState();
        outputs.emplace_back(outId, readBack(idt, DXGI_FORMAT_UNKNOWN, 4));
    }
    for (const auto& o : outputs) writeFile(o.first, o.second.data(), o.second.size());
    std::printf("terrainreplay: %zu draws, %u x %u, samples %u, %s\n", draws.size(), W, H, samples, warp ? "warp" : "hardware");
    return 0;
}
