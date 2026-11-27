// Reflection tests (task 2902): shaders compiled in the test from HLSL written here, reflected by the token decoder and
// cross-checked against D3DDisassemble; hand-built token streams for every decoder check.
#include <algorithm>

#include "c5r/shaders/d3dcheck.h"
#include "c5r/shaders/reflect.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::shaders;
using namespace c5r::test;

namespace {

const char* kVs = R"(
cbuffer A : register(b0) { float4x4 m; float4 arr[8]; float4 tail; };
cbuffer B : register(b2) { float4 k; };
Texture2D t0 : register(t0); Texture3D t1 : register(t1); TextureCube t2 : register(t2);
Texture2DArray t3 : register(t3); TextureCubeArray t4 : register(t4); Buffer<float4> t5 : register(t5);
Texture2DMS<float4, 4> t6 : register(t6);
SamplerState s0 : register(s0); SamplerComparisonState s1 : register(s1);
struct VI { float3 pos : POSITION; float2 uv : TEXCOORD0; uint id : SV_VertexID; int idx : BLENDINDICES; };
struct VO { float4 pos : SV_Position; float4 c : COLOR0; float2 uv : TEXCOORD1; };
VO main(VI i) {
    VO o;
    o.pos = mul(float4(i.pos, 1), m);
    o.c = arr[i.idx] + k.xxyy + t0.SampleLevel(s0, i.uv, 0) + t1.SampleLevel(s0, i.pos, 0) + t2.SampleLevel(s0, i.pos, 0)
        + t3.SampleLevel(s0, float3(i.uv, 1), 0) + t4.SampleLevel(s0, float4(i.pos, 2), 0) + t5.Load(i.id)
        + t0.SampleCmpLevelZero(s1, i.uv, 0.5) + t6.Load(int2(i.uv), 1);
    o.uv = i.uv * tail.zw;
    return o;
}
)";

const char* kPs = R"(
static const float4 tbl[3] = { float4(1, 2, 3, 4), float4(5, 6, 7, 8), float4(9, 10, 11, 12) };
cbuffer P : register(b3) { float4 tint; float4 rows[4]; };
struct PI {
    float4 pos : SV_Position;
    nointerpolation uint f : FLAG;
    centroid float2 a : TEXCOORD0;
    noperspective float3 b : TEXCOORD1;
    sample float c : TEXCOORD2;
    bool ff : SV_IsFrontFace;
};
float4 main(PI i, out float d : SV_Depth) : SV_Target0 {
    float4 x[4];
    for (int j = 0; j < 4; ++j) x[j] = i.a.xyxy * j + rows[j];
    d = i.c;
    return tbl[i.f % 3] * tint.x + x[i.f & 3] + float4(i.b, i.ff ? 1 : 0);
}
)";

const char* kTess = R"(
struct CP { float3 p : POSITION; };
struct PC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };
cbuffer T : register(b1) { float4 f; };
PC pc(InputPatch<CP, 4> ip) {
    PC o;
    o.e[0] = f.x; o.e[1] = f.y; o.e[2] = f.z; o.e[3] = f.w; o.i[0] = f.x + ip[0].p.x; o.i[1] = f.y;
    return o;
}
[domain("quad")] [partitioning("integer")] [outputtopology("triangle_cw")] [outputcontrolpoints(4)]
[patchconstantfunc("pc")] [maxtessfactor(32.0)]
CP hs(InputPatch<CP, 4> ip, uint id : SV_OutputControlPointID) { CP o; o.p = ip[id].p; return o; }
[domain("quad")]
float4 ds(PC pc, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> p) : SV_Position {
    return float4(lerp(lerp(p[0].p, p[1].p, uv.x), lerp(p[2].p, p[3].p, uv.x), uv.y), 1) * pc.e[0];
}
)";

const char* kGs = R"(
struct GI { float4 p : SV_Position; };
[maxvertexcount(3)] [instance(2)]
void main(triangle GI v[3], inout TriangleStream<GI> s, uint iid : SV_GSInstanceID) {
    for (int k = 0; k < 3; ++k) { GI o = v[k]; o.p.x += iid; s.Append(o); }
}
)";

const char* kCs = R"(
StructuredBuffer<float4> sb : register(t0); ByteAddressBuffer rb : register(t1);
RWStructuredBuffer<float4> ub : register(u0); RWTexture2D<float4> ut : register(u1); RWByteAddressBuffer ur : register(u2);
groupshared float4 g[64];
[numthreads(8, 4, 2)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    g[gi] = sb[id.x] + asfloat(rb.Load(id.y * 4));
    GroupMemoryBarrierWithGroupSync();
    ub[id.x] = g[63 - gi];
    ut[id.xy] = g[gi];
    ur.Store(id.x * 4, asuint(g[gi].x));
}
)";

// Reflects, cross-checks against D3DDisassemble (0 differences) and returns the reflection; both the compiled
// container and the EXE-like stripped one.
ShaderReflection reflectChecked(const char* src, const char* entry, const char* target) {
    ShaderReflection r;
    const std::vector<uint8_t> full = compileHlsl(src, entry, target);
    CHECK(!full.empty());
    if (full.empty()) return r;
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<uint8_t> b = pass ? stripLikeExe(full) : full;
        CHECK(!b.empty());
        ShaderReflection x;
        CHECK_OK(reflect(b.data(), b.size(), x));
        std::string text;
        CHECK_OK(d3dDisassemble(b.data(), b.size(), text));
        std::vector<std::string> diffs;
        const size_t n = compareWithDisassembly(x, text, diffs);
        for (const std::string& d : diffs) std::fprintf(stderr, "  %s %s: %s\n", target, pass ? "stripped" : "full", d.c_str());
        CHECK_EQ(n, size_t(0));
        if (pass == 1) r = x;
    }
    return r;
}

const ResourceDecl* res(const ShaderReflection& r, uint32_t slot, bool uav = false) {
    for (const ResourceDecl& d : r.resources)
        if (d.slot == slot && d.uav == uav) return &d;
    return nullptr;
}
const CbRegUse* reg(const CbUse* u, uint32_t r) {
    if (!u) return nullptr;
    for (const CbRegUse& g : u->regs)
        if (g.reg == r) return &g;
    return nullptr;
}

Status reflectTokens(const std::vector<uint32_t>& tokens, std::vector<std::pair<uint32_t, std::vector<uint8_t>>> extra = {}) {
    extra.push_back({fourcc('S', 'H', 'E', 'X'), makeProgram(1, 5, 0, tokens)});
    const std::vector<uint8_t> c = makeContainer(extra);
    ShaderReflection r;
    return reflect(c.data(), c.size(), r);
}

std::vector<uint8_t> sigChunk(uint32_t count, uint32_t nameOff, const std::string& tail) {
    std::vector<uint8_t> s;
    put32(s, count);
    put32(s, 8);
    for (uint32_t i = 0; i < count && i < 1; ++i) {
        put32(s, nameOff); put32(s, 0); put32(s, 0); put32(s, 3); put32(s, 0); put32(s, 0x0F0F);
    }
    s.insert(s.end(), tail.begin(), tail.end());
    return s;
}

} // namespace

C5R_TEST(reflect_vertex_shader_declarations_and_cb_use) {
    const ShaderReflection r = reflectChecked(kVs, "main", "vs_5_0");
    CHECK(r.stage == Stage::Vertex);
    CHECK_EQ(r.chunks, std::string("ISGN+OSGN+SHEX"));
    // signatures
    CHECK_EQ(r.input.elems.size(), size_t(4));
    CHECK_EQ(r.input.elems[0].semantic, std::string("POSITION"));
    CHECK_EQ(int(r.input.elems[0].mask), 0x7);
    CHECK_EQ(r.input.elems[2].systemValue, 6u); // SV_VertexID
    CHECK_EQ(r.input.elems[3].componentType, 2u); // sint
    CHECK_EQ(r.output.elems.size(), size_t(3));
    CHECK_EQ(r.output.elems[0].systemValue, 1u); // SV_Position
    // constant buffers: A = 4 + 8 + 1 vec4, dynamically indexed; B = 1
    const CbDecl* a = r.findCb(0);
    CHECK(a && a->vec4Count == 13 && a->dynamicIndexed);
    const CbDecl* b = r.findCb(2);
    CHECK(b && b->vec4Count == 1 && !b->dynamicIndexed);
    // resources and samplers
    const uint32_t dims[] = {3, 5, 6, 8, 10, 1, 4};
    for (uint32_t s = 0; s < 7; ++s) {
        const ResourceDecl* d = res(r, s);
        CHECK(d != nullptr);
        if (d) CHECK_EQ(d->dimension, dims[s]);
    }
    CHECK_EQ(res(r, 6) ? res(r, 6)->sampleCount : 0u, 4u);
    CHECK_EQ(res(r, 0) ? int(res(r, 0)->returnType[0]) : 0, 5); // float
    CHECK_EQ(r.samplers.size(), size_t(2));
    CHECK_EQ(r.samplers[1].mode, 1u); // comparison
    // registers read: m = cb0[0..3], arr = cb0[r + 4], tail.zw = cb0[12]; k.xxyy = cb2[0].xy
    const CbUse* u0 = r.findCbUse(0);
    CHECK(u0 != nullptr);
    for (uint32_t i = 0; i < 4; ++i) CHECK(reg(u0, i) != nullptr);
    CHECK(reg(u0, 12) && reg(u0, 12)->usedMask == 0xC);
    CHECK(u0 && u0->dynamicBases == std::vector<uint32_t>{4});
    CHECK(reg(u0, 5) == nullptr);
    const CbUse* u2 = r.findCbUse(2);
    CHECK(reg(u2, 0) && reg(u2, 0)->usedMask == 0x3);
    CHECK(r.instructionCount > 5);
    CHECK(r.temps > 0);
}

C5R_TEST(reflect_pixel_shader_interpolation_icb_and_indexable_temp) {
    const ShaderReflection r = reflectChecked(kPs, "main", "ps_5_0");
    CHECK(r.stage == Stage::Pixel);
    std::vector<uint32_t> modes;
    for (const IoDecl& d : r.inputs)
        if (d.interpolation) modes.push_back(d.interpolation);
    auto has = [&modes](uint32_t m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    CHECK(has(1)); // constant (nointerpolation)
    CHECK(has(3)); // linear centroid
    CHECK(has(4)); // linear noperspective
    CHECK(has(6)); // linear sample
    bool depth = false;
    for (const IoDecl& d : r.outputs) depth = depth || d.operandType == 12;
    CHECK(depth);
    CHECK_EQ(r.immediateCbVec4, 3u);
    const CbDecl* p = r.findCb(3);
    CHECK(p && p->vec4Count == 5);
    // sm4.1 and sm4.0 variants of the same source cross-check too
    reflectChecked(kPs, "main", "ps_4_1");
    reflectChecked(kVs, "main", "vs_4_1");
}

C5R_TEST(reflect_hull_and_domain_tessellation_declarations) {
    const ShaderReflection h = reflectChecked(kTess, "hs", "hs_5_0");
    CHECK(h.stage == Stage::Hull);
    CHECK_EQ(h.tess.domain, 3u);
    CHECK_EQ(h.tess.partitioning, 1u);
    CHECK_EQ(h.tess.outputPrimitive, 3u);
    CHECK_EQ(h.tess.inputControlPoints, 4u);
    CHECK_EQ(h.tess.outputControlPoints, 4u);
    CHECK(h.tess.maxTessFactor == 32.0f);
    CHECK(h.tess.forkPhases + h.tess.joinPhases > 0);
    CHECK(h.patchConstant.elems.size() == 6);
    CHECK(h.findCbUse(1) != nullptr);
    const ShaderReflection d = reflectChecked(kTess, "ds", "ds_5_0");
    CHECK(d.stage == Stage::Domain);
    CHECK_EQ(d.tess.domain, 3u);
    CHECK_EQ(d.tess.inputControlPoints, 4u);
}

C5R_TEST(reflect_geometry_and_compute_declarations) {
    const ShaderReflection g = reflectChecked(kGs, "main", "gs_5_0");
    CHECK(g.stage == Stage::Geometry);
    CHECK_EQ(g.gs.inputPrimitive, 3u);
    CHECK_EQ(g.gs.outputTopology, 5u);
    CHECK_EQ(g.gs.maxOutputVertices, 3u);
    CHECK_EQ(g.gs.instanceCount, 2u);
    const ShaderReflection c = reflectChecked(kCs, "main", "cs_5_0");
    CHECK(c.stage == Stage::Compute);
    CHECK_EQ(c.threadGroup[0], 8u);
    CHECK_EQ(c.threadGroup[1], 4u);
    CHECK_EQ(c.threadGroup[2], 2u);
    CHECK(res(c, 0) && res(c, 0)->dimension == 12 && res(c, 0)->stride == 16);
    CHECK(res(c, 1) && res(c, 1)->dimension == 11);
    CHECK(res(c, 0, true) && res(c, 0, true)->dimension == 12);
    CHECK(res(c, 1, true) && res(c, 1, true)->dimension == 3);
    CHECK(res(c, 2, true) && res(c, 2, true)->dimension == 11);
}

// The cross-check notices a wrong decoder: a changed declaration, a changed component mask, a lost read.
C5R_TEST(reflect_cross_check_detects_differences) {
    const std::vector<uint8_t> b = stripLikeExe(compileHlsl(kVs, "main", "vs_5_0"));
    ShaderReflection r;
    CHECK_OK(reflect(b.data(), b.size(), r));
    std::string text;
    CHECK_OK(d3dDisassemble(b.data(), b.size(), text));
    std::vector<std::string> diffs;
    CHECK_EQ(compareWithDisassembly(r, text, diffs), size_t(0));
    ShaderReflection x = r;
    x.declLines[1] += "X";
    CHECK(compareWithDisassembly(x, text, diffs) > 0);
    x = r;
    x.cbUse[0].regs[0].swizzleMask ^= 0x8;
    CHECK(compareWithDisassembly(x, text, diffs) > 0);
    x = r;
    x.cbUse[0].regs.pop_back();
    CHECK(compareWithDisassembly(x, text, diffs) > 0);
    x = r;
    x.cbUse[0].dynamicBases.push_back(9);
    CHECK(compareWithDisassembly(x, text, diffs) > 0);
    x = r;
    ++x.instructionCount;
    CHECK(compareWithDisassembly(x, text, diffs) > 0);
}

// ---- negative tests: hand-built containers ----
C5R_TEST(reflect_without_program_is_not_found) {
    const std::vector<uint8_t> c = makeContainer({{fourcc('I', 'S', 'G', 'N'), sigChunk(0, 0, "")}});
    ShaderReflection r;
    CHECK_ERR(reflect(c.data(), c.size(), r), Err::NotFound);
}

C5R_TEST(reflect_corrupted_container_fails_checksum) {
    std::vector<uint8_t> c = compileHlsl(kVs, "main", "vs_5_0");
    c[c.size() / 2] ^= 0x10;
    ShaderReflection r;
    CHECK_ERR(reflect(c.data(), c.size(), r), Err::BadLayout);
}

C5R_TEST(reflect_signature_checks) {
    const std::vector<uint32_t> ret = {0x0100003E};
    CHECK_OK(reflectTokens(ret, {{fourcc('I', 'S', 'G', 'N'), sigChunk(1, 32, std::string("POS\0", 4))}}));
    CHECK_ERR(reflectTokens(ret, {{fourcc('I', 'S', 'G', 'N'), sigChunk(5, 32, "")}}), Err::Truncated);
    CHECK_ERR(reflectTokens(ret, {{fourcc('O', 'S', 'G', 'N'), sigChunk(1, 1000, "")}}), Err::BadLayout);
    CHECK_ERR(reflectTokens(ret, {{fourcc('I', 'S', 'G', 'N'), sigChunk(1, 32, "ABCD")}}), Err::BadLayout);
    std::vector<uint8_t> tiny(4, 0);
    CHECK_ERR(reflectTokens(ret, {{fourcc('P', 'C', 'S', 'G'), tiny}}), Err::Truncated);
}

C5R_TEST(reflect_token_stream_checks) {
    CHECK_OK(reflectTokens({0x0100003E}));                                     // ret
    CHECK_ERR(reflectTokens({0x00000036}), Err::BadLayout);                    // length 0
    CHECK_ERR(reflectTokens({0x05000036, 0}), Err::BadLayout);                 // runs past the program
    CHECK_ERR(reflectTokens({0x02000036, 0x00000003}), Err::Unsupported);      // N-component operand
    CHECK_ERR(reflectTokens({0x02000036, 0x0000000E}), Err::Unsupported);      // selection mode 3
    CHECK_ERR(reflectTokens({0x02000036, 0x001000F2}), Err::BadLayout);        // index past the instruction
    CHECK_ERR(reflectTokens({0x03000036, 0x001000F2 | (5u << 22), 0}), Err::Unsupported); // index representation 5
    CHECK_ERR(reflectTokens({0x02000036, 0x801000F2}), Err::BadLayout);        // extended operand token missing
    CHECK_ERR(reflectTokens({0x81000036}), Err::BadLayout);                    // extended opcode token missing
    CHECK_ERR(reflectTokens({0x00001835, 1}), Err::BadLayout);                 // customdata length < 2
    CHECK_ERR(reflectTokens({0x00001835}), Err::BadLayout);                    // customdata without its length
    CHECK_ERR(reflectTokens({0x01000068}), Err::BadLayout);                    // dcl_temps without its count
    CHECK_ERR(reflectTokens({0x01000059}), Err::BadLayout);                    // dcl_constantbuffer without its operand
    // relative index nested deeper than 3
    std::vector<uint32_t> deep = {0x0A000036};
    for (int i = 0; i < 5; ++i) deep.push_back(0x00100012u | (2u << 22)); // r[rel] with a relative index
    deep.resize(10, 0);
    CHECK_ERR(reflectTokens(deep), Err::BadLayout);
}

C5R_TEST(reflect_hand_built_cb_reads) {
    // dcl_constantbuffer cb4[10], dynamicIndexed; mov r0.xy, cb4[7].zwzz; mov r1.x, cb4[r0.x + 2].y; ret
    const uint32_t cbOp2d = 2u | (0xE4u << 4) | (1u << 2) | (8u << 12) | (2u << 20); // swizzle xyzw, 2-D, imm/imm
    const std::vector<uint32_t> t = {
        0x04000859, cbOp2d, 4, 10,                                    // dcl_constantbuffer, access bit 11 = dynamic
        0x06000036, 0x00100032, 0, (cbOp2d & ~(0xFFu << 4)) | (0xAEu << 4), 4, 7, // mov r0.xy, cb4[7].zwzz
        0x08000036, 0x00100012, 1,                                    // mov r1.x,
        (2u | (2u << 2) | (1u << 4) | (8u << 12) | (2u << 20) | (3u << 25)), 4, 2, 0x0010000A, 0, // cb4[r0.x + 2].y
        0x0100003E};
    const std::vector<uint8_t> c = makeContainer({{fourcc('S', 'H', 'E', 'X'), makeProgram(1, 5, 0, t)}});
    ShaderReflection r;
    CHECK_OK(reflect(c.data(), c.size(), r));
    CHECK_EQ(r.cbuffers.size(), size_t(1));
    CHECK(r.cbuffers[0].dynamicIndexed && r.cbuffers[0].vec4Count == 10 && r.cbuffers[0].slot == 4);
    const CbUse* u = r.findCbUse(4);
    CHECK(u && u->reads == 2);
    CHECK(reg(u, 7) && reg(u, 7)->swizzleMask == 0xC && reg(u, 7)->usedMask == 0xC);
    CHECK(u && u->dynamicBases == std::vector<uint32_t>{2} && u->dynamicSwizzleMask == 0x2);
    CHECK_EQ(r.instructionCount, 3u);
    CHECK_EQ(r.declLines.size(), size_t(1));
    CHECK_EQ(r.declLines[0], std::string("dcl_constantbuffer CB4[10], dynamicIndexed"));
}
