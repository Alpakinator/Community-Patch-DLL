// draw.cpp - the draw layer of the BuildingShader models (ticket 4873, epic 0561): the EXE's pass-0 programs by digest,
// the input layouts, the captures' states, the library's buffers and textures on the device, the draws of a scene.
// Own code from the knowledge base's description (D83): city-renderer.md 3.1 and 5.4-5.6, landmark-renderer.md 2 and 7.
#include "c5r/landmarks/draw.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "c5models/art.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/units/draw.h"   // units::sha256Hex
#include "shell_model_scene.h"
#include "shell_model_tap.h"

namespace c5r { namespace landmarks {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::string hex(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s.push_back(d[b[i] >> 4]);
        s.push_back(d[b[i] & 15]);
    }
    return s;
}

const VPModelPair* pairOf(Material m) {
    const uint8_t cls = classOf(m);
    for (const VPModelPair& p : kVPModelDropPairs)
        if (p.cls == cls) return &p;
    return nullptr;
}

int stateKey(Material m, DrawPass p) { return static_cast<int>(m) * 2 + static_cast<int>(p); }

// ticket 4884: the stage timers of DrawReport (QueryPerformanceCounter)
int64_t qpcNow() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}
double qpcMs(int64_t d) {
    static const double f = [] {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        return static_cast<double>(q.QuadPart);
    }();
    return 1000.0 * static_cast<double>(d) / f;
}

DXGI_FORMAT srgbOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default: return f;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------- programs
Status BuildingPrograms::openFromBlobs(const std::vector<std::vector<uint8_t>>& blobs) {
    release();
    bytes_.clear();
    std::map<std::string, const std::vector<uint8_t>*> byHash;
    for (const auto& b : blobs) byHash[units::sha256Hex(b.data(), b.size())] = &b;
    std::string missing;
    for (int i = 0; i < VPMODEL_DROP_PROGRAMS; ++i) {
        const std::string want = hex(kVPModelPrograms[i].sha256, 32);
        auto it = byHash.find(want);
        if (it == byHash.end()) {
            char va[16];
            std::snprintf(va, sizeof va, "0x%X", kVPModelPrograms[i].exeVa);
            missing += std::string(missing.empty() ? "" : "; ") + want + " (" +
                       (kVPModelPrograms[i].stage == VPMODEL_STAGE_VS ? "VS" : "PS") + ", build 4390913 VA " + va + ")";
            continue;
        }
        bytes_[i] = *it->second;
    }
    if (!missing.empty()) {
        bytes_.clear();
        return Status::fail(Err::NotFound, "BuildingShader programs not in the EXE's .shader section: " + missing);
    }
    return Status::Ok();
}

Status BuildingPrograms::open(const std::wstring& exePath) {
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(sec.blobs().size());
    for (const auto& b : sec.blobs()) blobs.emplace_back(b.bytes, b.bytes + b.size);
    return openFromBlobs(blobs);
}

const std::vector<uint8_t>* BuildingPrograms::vsBytes(Material m) const {
    const VPModelPair* p = pairOf(m);
    if (!p) return nullptr;
    auto it = bytes_.find(p->vs);
    return it == bytes_.end() ? nullptr : &it->second;
}

const std::vector<uint8_t>* BuildingPrograms::psBytes(Material m) const {
    const VPModelPair* p = pairOf(m);
    if (!p) return nullptr;
    auto it = bytes_.find(p->ps);
    return it == bytes_.end() ? nullptr : &it->second;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements(Material m) {
    const D3D11_INPUT_CLASSIFICATION V = D3D11_INPUT_PER_VERTEX_DATA, I = D3D11_INPUT_PER_INSTANCE_DATA;
    std::vector<D3D11_INPUT_ELEMENT_DESC> e;
    switch (m) {
    case Material::Blocks:
        e = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, V, 0},
             {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, V, 0},
             {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, V, 0}};
        break;
    case Material::Single:
    case Material::Instanced:
        e = {{"POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, V, 0},
             {"NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 8, V, 0},
             {"TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 16, V, 0}};
        break;
    case Material::Skinned:
        e = {{"POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, V, 0},
             {"BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, V, 0},
             {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 12, V, 0},
             {"NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 16, V, 0},
             {"TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 24, V, 0},
             {"BLENDINDICES", 1, DXGI_FORMAT_R32_UINT, 1, 0, I, 1}};
        break;
    }
    if (m == Material::Blocks || m == Material::Instanced) {
        e.push_back({"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, I, 1});
        e.push_back({"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, I, 1});
        e.push_back({"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, I, 1});
        e.push_back({"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, I, 1});
    }
    return e;
}

Status BuildingPrograms::create(ID3D11Device* dev) {
    if (!dev) return Status::fail(Err::Device, "no device");
    if (!opened()) return Status::fail(Err::NotFound, "programs not opened");
    release();
    for (auto& kv : bytes_) {
        const VPModelProgram& p = kVPModelPrograms[kv.first];
        HRESULT hr;
        if (p.stage == VPMODEL_STAGE_VS) {
            ID3D11VertexShader* vs = nullptr;
            hr = dev->CreateVertexShader(kv.second.data(), kv.second.size(), nullptr, &vs);
            objects_[kv.first] = vs;
        } else {
            ID3D11PixelShader* ps = nullptr;
            hr = dev->CreatePixelShader(kv.second.data(), kv.second.size(), nullptr, &ps);
            objects_[kv.first] = ps;
        }
        if (FAILED(hr)) return Status::fail(Err::Device, "creating program " + std::to_string(kv.first) + ": " + std::to_string(hr));
    }
    for (Material m : {Material::Single, Material::Instanced, Material::Blocks, Material::Skinned}) {
        const std::vector<uint8_t>* vsb = vsBytes(m);
        const std::vector<D3D11_INPUT_ELEMENT_DESC> e = inputElements(m);
        ID3D11InputLayout* il = nullptr;
        const HRESULT hr = dev->CreateInputLayout(e.data(), static_cast<UINT>(e.size()), vsb->data(), vsb->size(), &il);
        if (FAILED(hr))
            return Status::fail(Err::Device, std::string("CreateInputLayout for ") + materialName(m) + ": " + std::to_string(hr));
        layouts_[static_cast<int>(m)] = il;
    }
    return Status::Ok();
}

void BuildingPrograms::release() {
    for (auto& kv : objects_)
        if (kv.second) kv.second->Release();
    for (auto& kv : layouts_)
        if (kv.second) kv.second->Release();
    objects_.clear();
    layouts_.clear();
}

ID3D11VertexShader* BuildingPrograms::vs(Material m) const {
    const VPModelPair* p = pairOf(m);
    auto it = p ? objects_.find(p->vs) : objects_.end();
    return it == objects_.end() ? nullptr : static_cast<ID3D11VertexShader*>(it->second);
}
ID3D11PixelShader* BuildingPrograms::ps(Material m) const {
    const VPModelPair* p = pairOf(m);
    auto it = p ? objects_.find(p->ps) : objects_.end();
    return it == objects_.end() ? nullptr : static_cast<ID3D11PixelShader*>(it->second);
}
ID3D11InputLayout* BuildingPrograms::layout(Material m) const {
    auto it = layouts_.find(static_cast<int>(m));
    return it == layouts_.end() ? nullptr : it->second;
}

// ---------------------------------------------------------------------------------------------- states
DrawStates captureStates(Material m, DrawPass p) {
    DrawStates s;
    D3D11_RENDER_TARGET_BLEND_DESC& rt = s.blend.RenderTarget[0];
    // Blend: material 5 opaque; 3, 4 and 6 straight alpha, alpha summed (the captures' states 885 / 893 [C]).
    rt.BlendEnable = m == Material::Blocks ? FALSE : TRUE;
    rt.SrcBlend = m == Material::Blocks ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = m == Material::Blocks ? D3D11_BLEND_ZERO : D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = m == Material::Blocks ? D3D11_BLEND_ZERO : D3D11_BLEND_ONE;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    // Depth: less-equal, written, no stencil (state 898 [C]).
    s.depth.DepthEnable = TRUE;
    s.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    s.depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    s.depth.StencilEnable = FALSE;
    s.depth.FrontFace = s.depth.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                            D3D11_COMPARISON_ALWAYS};
    // Raster: solid, front faces counter-clockwise; the scene culls back faces, the mirrored reflection front faces
    // (states 901 / 894 [C]).
    s.raster.FillMode = D3D11_FILL_SOLID;
    s.raster.CullMode = p == DrawPass::Scene ? D3D11_CULL_BACK : D3D11_CULL_FRONT;
    s.raster.FrontCounterClockwise = TRUE;
    s.raster.DepthClipEnable = TRUE;
    return s;
}

D3D11_SAMPLER_DESC captureSampler(uint32_t slot) {
    D3D11_SAMPLER_DESC d = {};
    d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    const D3D11_TEXTURE_ADDRESS_MODE a =
        (slot == 6 || slot == 9) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
    d.AddressU = d.AddressV = d.AddressW = a;
    d.MipLODBias = 0;
    d.MaxAnisotropy = 1;   // the captures say 0; the filter is not anisotropic, so the value is not read
    d.ComparisonFunc = D3D11_COMPARISON_NEVER;
    d.MinLOD = 0;
    d.MaxLOD = D3D11_FLOAT32_MAX;
    return d;
}

// ---------------------------------------------------------------------------------------------- the renderer
namespace {

// The id program: its input mirrors the VS's output signature (as terrainreplay's id_ps, render/tools/terrainreplay),
// its output the item index from b13.
Status idProgram(ID3D11Device* dev, const std::vector<uint8_t>& vs, ID3D11PixelShader** out) {
    ID3D11ShaderReflection* r = nullptr;
    if (FAILED(D3DReflect(vs.data(), vs.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&r))))
        return Status::fail(Err::Device, "D3DReflect of a BuildingShader VS");
    D3D11_SHADER_DESC sd = {};
    r->GetDesc(&sd);
    std::string src = "cbuffer IdCb : register(b13) { uint g_id; };\nstruct In {\n";
    for (UINT i = 0; i < sd.OutputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC p = {};
        r->GetOutputParameterDesc(i, &p);
        const unsigned mask = p.Mask;
        unsigned lo = 0, hi = 0;
        for (unsigned b = 0; b < 4; ++b)
            if (mask & (1u << b)) {
                if (!hi) lo = b;
                hi = b + 1;
            }
        const unsigned width = hi > lo ? hi - lo : 1;
        const char* typ = p.ComponentType == D3D_REGISTER_COMPONENT_UINT32   ? "uint"
                          : p.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? "int"
                                                                             : "float";
        std::string sem = p.SemanticName;
        std::string up = sem;
        for (char& ch : up) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        if (up.compare(0, 3, "SV_") != 0) sem += std::to_string(p.SemanticIndex);
        src += std::string("  ") + (std::strcmp(typ, "float") ? "nointerpolation " : "") + typ +
               (width > 1 ? std::to_string(width) : std::string()) + " v" + std::to_string(i) + " : " + sem + ";\n";
    }
    r->Release();
    src += "};\nuint main(In i) : SV_Target { return g_id; }\n";
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    const HRESULT hr = D3DCompile(src.data(), src.size(), "id_ps", nullptr, nullptr, "main", "ps_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        std::string why = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "";
        if (err) err->Release();
        return Status::fail(Err::Device, "compiling the id program: " + why);
    }
    if (err) err->Release();
    const HRESULT h2 = dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, out);
    code->Release();
    if (FAILED(h2)) return Status::fail(Err::Device, "creating the id program");
    return Status::Ok();
}

Status makeBuffer(ID3D11Device* dev, const void* data, size_t bytes, UINT bind, D3D11_USAGE usage, ID3D11Buffer** out,
                  UINT misc = 0) {
    *out = nullptr;
    if (bytes == 0 || bytes > 0x7FFFFFFFu) return Status::fail(Err::BadLayout, "buffer size 0 or above 2 GB");
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth = static_cast<UINT>(bytes);
    d.Usage = usage;
    d.BindFlags = bind;
    d.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
    d.MiscFlags = misc;
    D3D11_SUBRESOURCE_DATA s = {data, 0, 0};
    const HRESULT hr = dev->CreateBuffer(&d, data ? &s : nullptr, out);
    if (hr == E_OUTOFMEMORY) return Status::fail(Err::OutOfMemory, "CreateBuffer: out of memory");
    if (FAILED(hr)) return Status::fail(Err::Device, "CreateBuffer: " + std::to_string(hr));
    return Status::Ok();
}

}  // namespace

Status ModelRenderer::init(ID3D11Device* dev, BuildingPrograms& programs, ModelLibrary& lib,
                           const gamefs::GameFiles& files) {
    release();
    if (!dev) return Status::fail(Err::Device, "no device");
    dev_ = dev;
    dev_->AddRef();
    programs_ = &programs;
    lib_ = &lib;
    files_ = &files;
    C5R_TRY(programs.create(dev));
    for (Material m : {Material::Single, Material::Instanced, Material::Blocks, Material::Skinned})
        for (DrawPass p : {DrawPass::Scene, DrawPass::Reflection}) {
            const DrawStates s = captureStates(m, p);
            const int k = stateKey(m, p);
            if (FAILED(dev->CreateBlendState(&s.blend, &blend_[k])) ||
                FAILED(dev->CreateDepthStencilState(&s.depth, &depth_[k])) ||
                FAILED(dev->CreateRasterizerState(&s.raster, &raster_[k])))
                return Status::fail(Err::Device, "creating the pipeline states");
        }
    for (uint32_t i = 0; i < kPsSamplers; ++i) {
        const D3D11_SAMPLER_DESC d = captureSampler(i);
        if (FAILED(dev->CreateSamplerState(&d, &samplers_[i]))) return Status::fail(Err::Device, "creating a sampler");
    }
    D3D11_DEPTH_STENCIL_DESC idd = captureStates(Material::Instanced, DrawPass::Scene).depth;
    idd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    idd.DepthFunc = D3D11_COMPARISON_EQUAL;
    D3D11_BLEND_DESC idb = {};
    idb.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    idb.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    idb.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    idb.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    idb.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    idb.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    idb.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev->CreateDepthStencilState(&idd, &idDepth_)) || FAILED(dev->CreateBlendState(&idb, &idBlend_)))
        return Status::fail(Err::Device, "creating the id states");
    for (Material m : {Material::Single, Material::Instanced, Material::Blocks, Material::Skinned})
        C5R_TRY(idProgram(dev, *programs.vsBytes(m), &idPs_[static_cast<int>(m)]));
    C5R_TRY(makeBuffer(dev, nullptr, 256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, &cb0_));
    C5R_TRY(makeBuffer(dev, nullptr, 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, &idCb_));
    std::vector<uint32_t> seq(VPSHELL_MODELTAP_MAX_INSTANCES);
    for (uint32_t i = 0; i < seq.size(); ++i) seq[i] = i;
    C5R_TRY(makeBuffer(dev, seq.data(), seq.size() * 4, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, &ids_));
    return Status::Ok();
}

void ModelRenderer::release() {
    for (auto& kv : blend_) rel(kv.second);
    for (auto& kv : depth_) rel(kv.second);
    for (auto& kv : raster_) rel(kv.second);
    for (auto& kv : idPs_) rel(kv.second);
    blend_.clear();
    depth_.clear();
    raster_.clear();
    idPs_.clear();
    rel(idDepth_);
    rel(idBlend_);
    for (auto& s : samplers_) rel(s);
    rel(cb0_);
    rel(idCb_);
    rel(ids_);
    for (auto& kv : gpu_) {
        rel(kv.second.vb);
        rel(kv.second.skinVb);
        rel(kv.second.ib);
    }
    gpu_.clear();
    prepared_.clear();
    for (auto& kv : cpuGpu_) rel(kv.second);
    cpuGpu_.clear();
    cpuGen_ = 0;
    for (auto& kv : textures_) {
        rel(kv.second.srv);
        rel(kv.second.res);
    }
    textures_.clear();
    partSrv_.clear();   // ticket 4884
    fileGpu_.clear();
    rel(rowsBuf_);
    rowsCap_ = 0;
    for (auto& c : constBufs_) rel(c);
    constBufs_.clear();
    constBytes_.clear();
    gpuBytes_ = 0;
    // The programs are the caller's (their own destructor releases their shaders): the renderer only borrows them.
    // (ASan, 2026-11-18: releasing them here touched a BuildingPrograms that had already gone out of scope.)
    programs_ = nullptr;
    rel(dev_);
}

Status ModelRenderer::fileBuffers(uint32_t file, FileGpu** out) {
    *out = nullptr;
    FileGpu& g = gpu_[file];
    if (g.failed) return Status::fail(Err::NotFound, "file buffers failed before");
    if (!g.ib) {
        const ModelLibrary::Buffers* b = nullptr;
        Status s = lib_->resident(file, &b);
        if (!s.ok()) {
            g.failed = true;
            return s;
        }
        s = makeBuffer(dev_, b->indices.data(), b->indices.size(), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, &g.ib);
        if (s.ok() && !b->vertices.empty())
            s = makeBuffer(dev_, b->vertices.data(), b->vertices.size(), D3D11_BIND_VERTEX_BUFFER,
                           D3D11_USAGE_IMMUTABLE, &g.vb);
        if (s.ok() && !b->skinnedVertices.empty())
            s = makeBuffer(dev_, b->skinnedVertices.data(), b->skinnedVertices.size(), D3D11_BIND_VERTEX_BUFFER,
                           D3D11_USAGE_IMMUTABLE, &g.skinVb);
        if (!s.ok()) {
            g.failed = true;
            rel(g.ib);
            rel(g.vb);
            rel(g.skinVb);
            return s;
        }
        gpuBytes_ += b->indices.size() + b->vertices.size() + b->skinnedVertices.size();
        // ticket 0581: nothing reads the CPU copy after the upload (city-renderer.md 12.10); a device made again re-reads it
        lib_->release(file);
    }
    *out = &g;
    return Status::Ok();
}

Status ModelRenderer::prepare(uint32_t file) {
    if (!dev_ || !lib_) return Status::fail(Err::Device, "renderer not initialised");
    if (file >= lib_->files().size()) return Status::fail(Err::BadLayout, "file index out of range");
    if (prepared(file)) return Status::Ok();
    FileGpu* g = nullptr;
    C5R_TRY(fileBuffers(file, &g));
    for (const Part& p : lib_->files()[file].layout.parts)
        for (const std::string* t : {&p.t0, &p.t1})
            if (!t->empty() && !texture(*t))
                return Status::fail(Err::NotFound, lib_->files()[file].gr2 + ": texture " + *t + " not drawable");
    prepared_[file] = true;
    return Status::Ok();
}

ID3D11ShaderResourceView* ModelRenderer::texture(const std::string& name) {
    if (name.empty()) return nullptr;
    const std::string key = c5m::basename_lower(name);
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second.srv;
    Tex& t = textures_[key];
    std::string n = key;
    if (!files_->exists(n)) {
        const size_t dot = key.rfind('.');
        n = (dot == std::string::npos ? key : key.substr(0, dot)) + ".dds";
    }
    std::vector<uint8_t> b;
    gamefs::DdsImage img;
    if (!files_->exists(n) || !files_->read(n, b).ok() || !gamefs::parseDds(b.data(), b.size(), img).ok()) return nullptr;
    if (img.format == DXGI_FORMAT_B8G8R8A8_UNORM && !img.converted) {
        // the EXE uploads A8R8G8B8 files as R8G8B8A8 (unit-renderer.md 10.3)
        img.bytes.assign(img.src, img.src + (img.subresources.empty() ? 0 : img.subresources.back().offset +
                                                                                 img.subresources.back().size));
        img.converted = true;
        for (size_t i = 0; i + 3 < img.bytes.size(); i += 4) std::swap(img.bytes[i], img.bytes[i + 2]);
        img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    img.format = srgbOf(img.format);   // t0 / t1 are sampled through sRGB views (the captures' views [C])
    if (!gamefs::createTexture(dev_, img, &t.res, &t.srv).ok()) {
        rel(t.res);
        rel(t.srv);
        return nullptr;
    }
    for (const auto& sr : img.subresources) gpuBytes_ += sr.size;
    return t.srv;
}

// ---- ticket 4884: what the pass has bound; each setter only when its value differs from the bound one ----
struct ModelRenderer::Bound {
    bool fresh = true;     // the first item of a pass sets everything (what the context held before is not known)
    bool vbKnown[2] = {false, false};
    bool idBound = false;
    ID3D11InputLayout* layout = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11ShaderResourceView* t01[2] = {};
    ID3D11ShaderResourceView* t24[3] = {};
    ID3D11Buffer* vcb1 = nullptr;
    ID3D11Buffer* pcb1 = nullptr;
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11Buffer* vb[2] = {};
    UINT stride[2] = {}, off[2] = {};
    uint32_t calls = 0;

    // The item's pipeline: input layout, programs, the three states, PS t0-t4, VS / PS cb0 (always cb0) and cb1, the
    // index buffer: the same calls in the same order as one draw's full set, those whose value is already bound left out.
    void bind(ID3D11DeviceContext* ctx, ID3D11InputLayout* l, ID3D11VertexShader* v, ID3D11PixelShader* p,
              ID3D11BlendState* bs, ID3D11DepthStencilState* ds, ID3D11RasterizerState* rs,
              ID3D11ShaderResourceView* const* t01n, ID3D11ShaderResourceView* const* t24n, ID3D11Buffer* cb0,
              ID3D11Buffer* vc, ID3D11Buffer* pc, ID3D11Buffer* ibn, DXGI_FORMAT ibf) {
        static const float zero[4] = {0, 0, 0, 0};
        if (fresh || l != layout) {
            ctx->IASetInputLayout(l);
            layout = l;
            ++calls;
        }
        if (fresh || v != vs) {
            ctx->VSSetShader(v, nullptr, 0);
            vs = v;
            ++calls;
        }
        if (fresh || p != ps) {
            ctx->PSSetShader(p, nullptr, 0);
            ps = p;
            ++calls;
        }
        if (fresh || bs != blend) {
            ctx->OMSetBlendState(bs, zero, 0xFFFFFFFFu);
            blend = bs;
            ++calls;
        }
        if (fresh || ds != depth) {
            ctx->OMSetDepthStencilState(ds, 0);
            depth = ds;
            ++calls;
        }
        if (fresh || rs != raster) {
            ctx->RSSetState(rs);
            raster = rs;
            ++calls;
        }
        if (fresh || t01n[0] != t01[0] || t01n[1] != t01[1]) {
            ctx->PSSetShaderResources(0, 2, t01n);
            t01[0] = t01n[0];
            t01[1] = t01n[1];
            ++calls;
        }
        if (fresh || t24n[0] != t24[0] || t24n[1] != t24[1] || t24n[2] != t24[2]) {
            ctx->PSSetShaderResources(2, 3, t24n);
            for (int k = 0; k < 3; ++k) t24[k] = t24n[k];
            ++calls;
        }
        if (fresh || vc != vcb1) {
            ID3D11Buffer* b[2] = {cb0, vc};
            ctx->VSSetConstantBuffers(0, 2, b);
            vcb1 = vc;
            ++calls;
        }
        if (fresh || pc != pcb1) {
            ID3D11Buffer* b[2] = {cb0, pc};
            ctx->PSSetConstantBuffers(0, 2, b);
            pcb1 = pc;
            ++calls;
        }
        if (fresh || ibn != ib || ibf != ibFormat) {
            ctx->IASetIndexBuffer(ibn, ibf, 0);
            ib = ibn;
            ibFormat = ibf;
            ++calls;
        }
        fresh = false;
    }
    // Vertex slots 0 .. n-1 (n = 1: material 3, whose layout reads slot 0 only).
    void vertexBuffers(ID3D11DeviceContext* ctx, UINT n, ID3D11Buffer* const* b, const UINT* s, const UINT* o) {
        bool same = true;
        for (UINT i = 0; same && i < n; ++i) same = vbKnown[i] && vb[i] == b[i] && stride[i] == s[i] && off[i] == o[i];
        if (same) return;
        ctx->IASetVertexBuffers(0, n, b, s, o);
        for (UINT i = 0; i < n; ++i) {
            vb[i] = b[i];
            stride[i] = s[i];
            off[i] = o[i];
            vbKnown[i] = true;
        }
        ++calls;
    }
};

Status ModelRenderer::uploadFrame(ID3D11DeviceContext* ctx, const ModelScene& scene) {
    curRows_ = nullptr;
    auto write = [&](ID3D11Buffer* b, const void* p, size_t n) -> Status {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return Status::fail(Err::Device, "Map of the frame's rows");
        std::memcpy(ms.pData, p, n);
        ctx->Unmap(b, 0);
        return Status::Ok();
    };
    if (!scene.rows.empty()) {
        const size_t bytes = scene.rows.size() * sizeof(Float4);
        if (bytes > rowsCap_) {
            rel(rowsBuf_);
            rowsCap_ = 0;
            size_t cap = (bytes + bytes / 2 + 0xFFFF) & ~static_cast<size_t>(0xFFFF);   // room to grow, 64 KB steps
            if (cap > 0x7FFFFFFFu) cap = bytes;
            C5R_TRY(makeBuffer(dev_, nullptr, cap, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE,
                               D3D11_USAGE_DYNAMIC, &rowsBuf_));
            rowsCap_ = cap;
        }
        C5R_TRY(write(rowsBuf_, scene.rows.data(), bytes));
        curRows_ = rowsBuf_;
    }
    if (constBufs_.size() < scene.consts.size()) {
        constBufs_.resize(scene.consts.size(), nullptr);
        constBytes_.resize(scene.consts.size(), 0);
    }
    for (size_t i = 0; i < scene.consts.size(); ++i) {
        const uint32_t bytes = scene.consts[i].rows * 16u;
        if (!constBufs_[i] || constBytes_[i] != bytes) {
            rel(constBufs_[i]);
            constBytes_[i] = 0;
            C5R_TRY(makeBuffer(dev_, nullptr, bytes, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, &constBufs_[i]));
            constBytes_[i] = bytes;
        }
        C5R_TRY(write(constBufs_[i], scene.rows.data() + scene.consts[i].firstRow, bytes));
    }
    return Status::Ok();
}

const std::array<ID3D11ShaderResourceView*, 2>& ModelRenderer::partTextures(uint32_t file, const ModelFile& mf,
                                                                            uint32_t part) {
    if (partSrv_.size() <= file) partSrv_.resize(lib_->files().size() > file ? lib_->files().size() : file + 1);
    std::vector<std::array<ID3D11ShaderResourceView*, 2>>& v = partSrv_[file];
    if (v.empty()) {
        v.resize(mf.layout.parts.size());
        for (size_t p = 0; p < mf.layout.parts.size(); ++p)
            v[p] = {texture(mf.layout.parts[p].t0), texture(mf.layout.parts[p].t1)};
    }
    return v[part];
}

Status ModelRenderer::drawItems(ID3D11DeviceContext* ctx, const ModelScene& scene, const ViewInput& view,
                                const EnvInput& env, bool ids, DrawReport& rep) {
    // The frame's row block on the device: slot 1 of materials 4 / 5 (stride 64 from instRow) and t27 of material 6.
    const int64_t q0 = qpcNow();
    C5R_TRY(uploadFrame(ctx, scene));
    ID3D11Buffer* const rows = curRows_;
    const std::vector<ID3D11Buffer*>& consts = constBufs_;
    const int64_t q1 = qpcNow();
    Bound b;
    uint32_t& calls = b.calls;
    ctx->OMSetRenderTargets(1, ids ? &view.idRtv : &view.rtv, view.dsv);
    ctx->RSSetViewports(1, &view.viewport);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetSamplers(0, kPsSamplers, samplers_);
    ctx->PSSetShaderResources(5, 5, env.frame);
    if (fileGpu_.size() < lib_->files().size()) fileGpu_.resize(lib_->files().size(), nullptr);
    for (uint32_t ii = 0; ii < scene.items.size(); ++ii) {
        const DrawItem& it = scene.items[ii];
        if (it.target != view.target) continue;
        if (!ids) ++rep.items;
        auto skip = [&](const std::string& why) {
            if (!ids) {
                ++rep.skipped;
                if (rep.problems.size() < 32) rep.problems.push_back("item " + std::to_string(ii) + ": " + why);
            }
        };
        const Material m = it.material;
        ID3D11VertexShader* vs = programs_->vs(m);
        ID3D11PixelShader* ps = ids ? idPs_[static_cast<int>(m)] : programs_->ps(m);
        if (!vs || !ps) {
            skip("no program");
            continue;
        }
        if (it.file >= lib_->files().size() || it.part >= lib_->files()[it.file].layout.parts.size()) {
            skip("file or part out of range");
            continue;
        }
        const ModelFile& mf = lib_->files()[it.file];
        const Part& part = mf.layout.parts[it.part];
        FileGpu* g = fileGpu_[it.file];   // ticket 4884: the file's buffers found once (std::map nodes do not move)
        if (!g) {
            if (!fileBuffers(it.file, &g).ok()) g = nullptr;
            fileGpu_[it.file] = g;
        }
        if (!g || !(part.skinned ? g->skinVb : g->vb)) {
            if (!ids) ++rep.bufferFailed;
            skip("buffers of " + mf.gr2);
            continue;
        }
        const std::array<ID3D11ShaderResourceView*, 2>& t01 = partTextures(it.file, mf, it.part);
        if ((!part.t0.empty() && !t01[0]) || (!part.t1.empty() && !t01[1]))
            if (!ids) ++rep.textureMissing;
        ID3D11ShaderResourceView* t24[3] = {};
        for (int k = 0; k < 3; ++k) {
            if (!it.env[k]) continue;
            auto e = env.env.find(it.env[k]);
            if (e == env.env.end()) {
                if (!ids && k == 0) ++rep.envMissing;
            } else {
                t24[k] = e->second;
            }
        }
        const int sk = stateKey(m, view.pass);
        b.bind(ctx, programs_->layout(m), vs, ps, ids ? idBlend_ : blend_[sk], ids ? idDepth_ : depth_[sk], raster_[sk],
               t01.data(), t24, cb0_, it.vsCb1 != kNoConst ? consts[it.vsCb1] : nullptr,
               it.psCb1 != kNoConst ? consts[it.psCb1] : nullptr, g->ib,
               mf.layout.indexBytes == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT);
        if (ids) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (FAILED(ctx->Map(idCb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
                return Status::fail(Err::Device, "Map of the id constants");
            const uint32_t v[4] = {ii + 1, 0, 0, 0};
            std::memcpy(ms.pData, v, 16);
            ctx->Unmap(idCb_, 0);
            calls += 2;
            if (!b.idBound) {
                ctx->PSSetConstantBuffers(13, 1, &idCb_);
                b.idBound = true;
                ++calls;
            }
        }
        ID3D11Buffer* vb0 = part.skinned ? g->skinVb : g->vb;
        const UINT stride0 = vertexStride(part.skinned ? VertexForm::Skinned28 : mf.layout.form);
        const UINT ic = part.key.indexCount, si = part.key.firstIndex;
        const INT bv = part.key.baseVertex;
        if (m == Material::Single) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (FAILED(ctx->Map(cb0_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
                return Status::fail(Err::Device, "Map of cb0");
            std::memset(ms.pData, 0, 256);
            std::memcpy(ms.pData, scene.rows.data() + it.instRow, 64);
            ctx->Unmap(cb0_, 0);
            const UINT off = 0;
            b.vertexBuffers(ctx, 1, &vb0, &stride0, &off);
            ctx->DrawIndexedInstanced(ic, 1, si, bv, 0);
            calls += 3;
        } else if (m == Material::Skinned) {
            // t27: the instances' palettes (96 rows each, ids 0 .. n-1); split when they exceed the EXE's 16,384 rows
            const uint32_t perCall = 16384u / VPSHELL_MODELTAP_PALETTE_ROWS;
            ID3D11Buffer* vbs[2] = {vb0, ids_};
            const UINT strides[2] = {stride0, 4};
            const UINT offs[2] = {0, 0};
            b.vertexBuffers(ctx, 2, vbs, strides, offs);
            if (it.instanceCount > perCall && !ids) ++rep.paletteSplits;
            for (uint32_t first = 0; first < it.instanceCount; first += perCall) {
                const uint32_t n = std::min(perCall, it.instanceCount - first);
                D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
                vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                vd.Buffer.FirstElement = it.instRow + first * VPSHELL_MODELTAP_PALETTE_ROWS;
                vd.Buffer.NumElements = n * VPSHELL_MODELTAP_PALETTE_ROWS;
                ID3D11ShaderResourceView* pal = nullptr;
                if (FAILED(dev_->CreateShaderResourceView(rows, &vd, &pal)))
                    return Status::fail(Err::Device, "the palette view");
                ctx->VSSetShaderResources(27, 1, &pal);
                ctx->PSSetShaderResources(27, 1, &pal);
                ctx->DrawIndexedInstanced(ic, n, si, bv, 0);
                ID3D11ShaderResourceView* none = nullptr;
                ctx->VSSetShaderResources(27, 1, &none);
                ctx->PSSetShaderResources(27, 1, &none);
                pal->Release();
                calls += 6;
            }
        } else {
            ID3D11Buffer* vbs[2] = {vb0, rows};
            const UINT strides[2] = {stride0, 64};
            const UINT offs[2] = {0, it.instRow * 16u};
            b.vertexBuffers(ctx, 2, vbs, strides, offs);
            ctx->DrawIndexedInstanced(ic, it.instanceCount, si, bv, 0);
            ++calls;
        }
        if (!ids) ++rep.drawn;
    }
    if (!scene.cpuItems.empty())   // ticket 4879: the CPU-built meshes, after the files' items
        C5R_TRY(drawCpuItems(ctx, scene, view, env, ids, rep, b));
    ID3D11ShaderResourceView* none[10] = {};
    ctx->PSSetShaderResources(0, 10, none);
    ID3D11Buffer* noBuf[2] = {nullptr, nullptr};
    UINT z[2] = {0, 0};
    ctx->IASetVertexBuffers(0, 2, noBuf, z, z);
    if (!ids) {   // ticket 4884: the colour pass's CPU stages
        const int64_t q2 = qpcNow();
        rep.uploadMs += qpcMs(q1 - q0);
        rep.itemsMs += qpcMs(q2 - q1);
        rep.contextCalls += calls + 5 + 2;   // the pass's set-up and the unbinds at its end
    }
    return Status::Ok();
}

// ---- ticket 4879: the CPU-built meshes (cpumesh.h) ----
void ModelRenderer::pruneCpu() {
    if (!cpuStore_ || cpuStore_->generation() == cpuGen_) return;
    for (auto it = cpuGpu_.begin(); it != cpuGpu_.end();) {
        if (!cpuStore_->has(static_cast<GeomKind>(it->first.first), it->first.second)) {
            rel(it->second);
            it = cpuGpu_.erase(it);
        } else {
            ++it;
        }
    }
    cpuGen_ = cpuStore_->generation();
}

ID3D11Buffer* ModelRenderer::cpuBuffer(GeomKind kind, uint64_t hash) {
    const std::pair<uint8_t, uint64_t> k{static_cast<uint8_t>(kind), hash};
    auto it = cpuGpu_.find(k);
    if (it != cpuGpu_.end()) return it->second;
    const CpuBuffer* b = cpuStore_ ? cpuStore_->get(kind, hash) : nullptr;
    if (!b || b->bytes.empty()) return nullptr;
    ID3D11Buffer* g = nullptr;
    if (!makeBuffer(dev_, b->bytes.data(), b->bytes.size(),
                    kind == GeomKind::Index32 ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, &g)
             .ok())
        return nullptr;
    gpuBytes_ += b->bytes.size();
    cpuGpu_[k] = g;
    return g;
}

bool ModelRenderer::prepareTextures(const std::string& t0, const std::string& t1) {
    if (!dev_) return false;
    return (t0.empty() || texture(t0)) && (t1.empty() || texture(t1));
}

Status ModelRenderer::drawCpuItems(ID3D11DeviceContext* ctx, const ModelScene& scene, const ViewInput& view,
                                   const EnvInput& env, bool ids, DrawReport& rep, Bound& b) {
    pruneCpu();
    ID3D11Buffer* const rows = curRows_;
    const std::vector<ID3D11Buffer*>& consts = constBufs_;
    uint32_t& calls = b.calls;
    for (uint32_t ci = 0; ci < scene.cpuItems.size(); ++ci) {
        const CpuDrawItem& it = scene.cpuItems[ci];
        const uint32_t ii = static_cast<uint32_t>(scene.items.size()) + ci;   // the id pass: after the files' items
        if (it.target != view.target) continue;
        if (!ids) ++rep.items;
        auto skip = [&](const std::string& why) {
            if (!ids) {
                ++rep.skipped;
                if (rep.problems.size() < 32) rep.problems.push_back("cpu item " + std::to_string(ci) + ": " + why);
            }
        };
        const Material m = it.material;
        if (!cpuMaterialFits(m, it.vertexStride)) {
            skip("material and vertex stride do not fit");
            continue;
        }
        ID3D11VertexShader* vs = programs_->vs(m);
        ID3D11PixelShader* ps = ids ? idPs_[static_cast<int>(m)] : programs_->ps(m);
        if (!vs || !ps) {
            skip("no program");
            continue;
        }
        ID3D11Buffer* ib = cpuBuffer(GeomKind::Index32, it.key.ib);
        ID3D11Buffer* vb0 = cpuBuffer(GeomKind::Vertex, it.key.vb);
        if (!ib || !vb0) {
            if (!ids) ++rep.bufferFailed;
            skip("CPU-built buffers not held");
            continue;
        }
        ID3D11ShaderResourceView* t01[2] = {texture(it.t0), texture(it.t1)};
        if ((!it.t0.empty() && !t01[0]) || (!it.t1.empty() && !t01[1]))
            if (!ids) ++rep.textureMissing;
        ID3D11ShaderResourceView* t24[3] = {};
        for (int k = 0; k < 3; ++k) {
            if (!it.env[k]) continue;
            auto e = env.env.find(it.env[k]);
            if (e == env.env.end()) {
                if (!ids && k == 0) ++rep.envMissing;
            } else {
                t24[k] = e->second;
            }
        }
        const int sk = stateKey(m, view.pass);
        b.bind(ctx, programs_->layout(m), vs, ps, ids ? idBlend_ : blend_[sk], ids ? idDepth_ : depth_[sk], raster_[sk], t01,
               t24, cb0_, it.vsCb1 != kNoConst ? consts[it.vsCb1] : nullptr,
               it.psCb1 != kNoConst ? consts[it.psCb1] : nullptr, ib, DXGI_FORMAT_R32_UINT);
        if (ids) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (FAILED(ctx->Map(idCb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
                return Status::fail(Err::Device, "Map of the id constants");
            const uint32_t v[4] = {ii + 1, 0, 0, 0};
            std::memcpy(ms.pData, v, 16);
            ctx->Unmap(idCb_, 0);
            calls += 2;
            if (!b.idBound) {
                ctx->PSSetConstantBuffers(13, 1, &idCb_);
                b.idBound = true;
                ++calls;
            }
        }
        const UINT stride0 = it.vertexStride;
        if (m == Material::Single) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (FAILED(ctx->Map(cb0_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return Status::fail(Err::Device, "Map of cb0");
            std::memset(ms.pData, 0, 256);
            std::memcpy(ms.pData, scene.rows.data() + it.instRow, 64);
            ctx->Unmap(cb0_, 0);
            const UINT off = 0;
            b.vertexBuffers(ctx, 1, &vb0, &stride0, &off);
            ctx->DrawIndexedInstanced(it.key.indexCount, 1, it.key.startIndex, it.key.baseVertex, 0);
            calls += 3;
        } else {   // Blocks: slot 1 the tapped instance rows (walls: one identity row)
            ID3D11Buffer* vbs[2] = {vb0, rows};
            const UINT strides[2] = {stride0, 64};
            const UINT offs[2] = {0, it.instRow * 16u};
            b.vertexBuffers(ctx, 2, vbs, strides, offs);
            ctx->DrawIndexedInstanced(it.key.indexCount, it.instanceCount, it.key.startIndex, it.key.baseVertex, 0);
            ++calls;
        }
        if (!ids) ++rep.drawn;
    }
    return Status::Ok();
}

Status ModelRenderer::draw(ID3D11DeviceContext* ctx, const ModelScene& scene, const ViewInput& view,
                           const EnvInput& env, DrawReport* report) {
    if (!dev_ || !programs_ || !ctx) return Status::fail(Err::Device, "renderer not initialised");
    if (!view.rtv) return Status::fail(Err::BadLayout, "no render target view");
    DrawReport rep;
    C5R_TRY(drawItems(ctx, scene, view, env, false, rep));
    if (view.idRtv) C5R_TRY(drawItems(ctx, scene, view, env, true, rep));
    if (report) *report = rep;
    return Status::Ok();
}

}}  // namespace c5r::landmarks
