// render/src/decal/decal_pass.cpp - c5r::decal::DecalPass (issue 4104, epic 0166): the terrain decals of a tile bake
// drawn with the EXE's own TerrainDecalShader pass 1. Behaviour as described in
// research/05-graphics-composition/terrain-decals.md sections 4 and 11 (D83: no decompiler output; the shaders are read
// from the player's EXE and the textures from the player's install at run time). Interface: c5r/decal/decal.h.
#include "c5r/decal/decal.h"

#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"

using Microsoft::WRL::ComPtr;

namespace c5r { namespace decal {

namespace {

constexpr uint32_t kTarget = 512;            // the blend and macro-shadow targets are always 512 x 512
constexpr uint32_t kIdentity = 0xFFFF;       // the EXE's identity index buffer: indices 0..0xFFFE
constexpr uint32_t kMaxRun = kIdentity;      // a decal's vertex run must fit the identity buffer

Status hrOk(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

std::string hexVa(uint32_t va) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", va);
    return b;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The blob checks of bakepost (stage, model 5, declared textures, constant buffers, VS inputs, PS targets).
Status checkBlob(const shaders::ShaderBlob& b, const char* name, shaders::Stage stage) {
    const std::string who = std::string(name) + " blob " + hexVa(b.va);
    if (b.stage != stage) return Status::fail(Err::BadLayout, who + " has another stage");
    shaders::ShaderReflection r;
    Status st = shaders::reflect(b.bytes, b.size, r);
    if (!st.ok()) return Status::fail(Err::BadLayout, who + ": reflection failed: " + st.str());
    if (r.major != 5) return Status::fail(Err::BadLayout, who + " is not shader model 5");
    for (const shaders::CbDecl& c : r.cbuffers)
        if (c.slot != 0 || c.vec4Count > kDecalCbFloats / 4)
            return Status::fail(Err::BadLayout, who + ": constant buffer slot " + std::to_string(c.slot) + " (" +
                                                    std::to_string(c.vec4Count) + " registers) is not provided");
    if (stage == shaders::Stage::Vertex) {
        if (!r.resources.empty()) return Status::fail(Err::BadLayout, who + ": declares textures, the pass binds none to the VS");
        for (const shaders::SigElement& s : r.input.elems)
            if (s.semantic != "POSITION" && s.semantic != "TEXCOORD")
                return Status::fail(Err::BadLayout, who + ": input " + s.semantic + " is not in the vertex layout");
        if (r.input.elems.empty()) return Status::fail(Err::BadLayout, who + ": no inputs");
    } else {
        std::vector<std::pair<uint32_t, uint32_t>> res, want = {{0, 3}, {1, 3}, {2, 3}};  // t0-t2, 2D
        for (const shaders::ResourceDecl& d : r.resources) res.push_back(std::make_pair(d.slot, d.dimension));
        if (res != want) return Status::fail(Err::BadLayout, who + ": declared textures differ from t0-t2 2D");
        uint32_t n = 0;
        for (const shaders::SigElement& s : r.output.elems)
            if (s.semantic == "SV_Target" || s.semantic == "SV_TARGET") ++n;
        if (n != 3) return Status::fail(Err::BadLayout, who + ": " + std::to_string(n) + " targets, the pass has 3");
    }
    return Status::Ok();
}

// The typeless family of a format, and its SRGB and linear view formats.
struct Views { DXGI_FORMAT res, srgb, lin; };
Views viewsOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
        return {DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC1_UNORM};
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
        return {DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM_SRGB, DXGI_FORMAT_BC2_UNORM};
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
        return {DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC3_UNORM};
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
        return {DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_BC7_UNORM};
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM};
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return {DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM};
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return {DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, DXGI_FORMAT_B8G8R8X8_UNORM};
    default:
        return {f, f, f};  // no SRGB form: both views in the format itself
    }
}

struct Tex {
    ComPtr<ID3D11Texture2D> res;
    ComPtr<ID3D11ShaderResourceView> srgb, lin;
};

Status makeViews(ID3D11Device* dev, ID3D11Texture2D* t, Tex& out) {
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    if (!(d.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return Status::fail(Err::BadLayout, "decal texture without SHADER_RESOURCE");
    const Views v = viewsOf(d.Format);
    const bool typeless = d.Format == v.res && v.srgb != v.res;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = d.MipLevels;
    sv.Format = typeless ? v.srgb : d.Format;
    C5R_TRY(hrOk(dev->CreateShaderResourceView(t, &sv, &out.srgb), "CreateShaderResourceView (decal albedo)"));
    sv.Format = typeless ? v.lin : d.Format;
    C5R_TRY(hrOk(dev->CreateShaderResourceView(t, &sv, &out.lin), "CreateShaderResourceView (decal)"));
    out.res = t;
    return Status::Ok();
}

// A DDS from the install as a typeless 2D texture (all mips of the file), as the EXE creates it.
Status createFromDds(ID3D11Device* dev, const gamefs::DdsImage& img, const std::string& name, Tex& out) {
    if (img.dim != gamefs::DdsDim::Tex2D || img.cube || img.arraySize != 1)
        return Status::fail(Err::Unsupported, "decal texture " + name + " is not a plain 2D texture");
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = img.width;
    d.Height = img.height;
    d.MipLevels = img.mipLevels;
    d.ArraySize = 1;
    // A legacy A8R8G8B8 file is created as gamefs gives it, B8G8R8A8 (memory order B, G, R, A), so the shader reads its
    // channels in their meaning, as the EXE samples them: the stone quarry's height map has its relief in red and 255 in
    // blue (terrain-decals.md 14.3, 17). 4104's earlier rule (R8G8B8A8 with the bytes unchanged, from the capture of the
    // grey tree_shadow_2.dds, 11.3) swapped red and blue; for grey files both give the same samples.
    d.Format = viewsOf(img.format).res;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const gamefs::DdsSubresource& s : img.subresources)
        init.push_back(D3D11_SUBRESOURCE_DATA{img.pixels() + s.offset, s.rowPitch, s.slicePitch});
    if (init.size() != img.mipLevels) return Status::fail(Err::BadLayout, "decal texture " + name + ": subresource count");
    ComPtr<ID3D11Texture2D> t;
    C5R_TRY(hrOk(dev->CreateTexture2D(&d, init.data(), &t), ("CreateTexture2D (decal texture " + name + ")").c_str()));
    return makeViews(dev, t.Get(), out);
}

Status checkTarget(ID3D11Texture2D* t, bool blend, int i, DXGI_FORMAT& viewFmt) {
    static const char* names[3] = {"albedo", "normal", "spec"};
    const std::string who = blend ? std::string("decal pass: blend target ") + names[i] : std::string("decal pass: macro-shadow target");
    if (!t) return Status::fail(Err::BadLayout, who + " missing");
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    if (blend && (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS))
        viewFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
    else if (!blend && (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS))
        viewFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    else
        return Status::fail(Err::BadLayout, who + ": format " + std::to_string(d.Format) +
                                                (blend ? ", want R16G16B16A16_FLOAT" : ", want R8G8B8A8_UNORM"));
    if (d.Width != kTarget || d.Height != kTarget || d.ArraySize != 1 || d.SampleDesc.Count != 1)
        return Status::fail(Err::BadLayout, who + ": " + std::to_string(d.Width) + " x " + std::to_string(d.Height) +
                                                ", want 512 x 512, one slice, no MSAA");
    if (!(d.BindFlags & D3D11_BIND_RENDER_TARGET)) return Status::fail(Err::BadLayout, who + ": not a render target");
    return Status::Ok();
}

}  // namespace

void decalViewProj(int originX, int originY, float out[16]) {
    // Fitted to the captured bits (section 11): the same in float and in double over every origin of a huge map.
    const float s = 1.0f / 33.0f;
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = s;
    out[5] = -s;
    out[10] = -0.5f;
    out[12] = static_cast<float>(-static_cast<double>(originX + 32) * static_cast<double>(s) - 1.0 / 512.0);
    out[13] = static_cast<float>(static_cast<double>(originY + 32) * static_cast<double>(s) + 1.0 / 512.0);
    out[14] = 0.5f;
    out[15] = 1.0f;
}

void decalConstants(int originX, int originY, const Decal& d, float out[kDecalCbFloats]) {
    std::memset(out, 0, kDecalCbFloats * sizeof(float));
    decalViewProj(originX, originY, out);
    out[16] = 1.0f;
    out[17] = 1.0f;
    for (int i = 0; i < 4; ++i) out[20 + i] = d.rect[i];
    out[24] = d.priority;
}

void defaultDecalTexels(uint8_t out[8 * 8 * 4]) {
    std::memset(out, 0, 8 * 8 * 4);
    out[0] = 255;  // texel (0, 0) = (255, 0, 0, 255): terrain-decals.md 4.3 "(none)"
    out[3] = 255;
}

struct DecalPass::Impl {
    ComPtr<ID3D11Device> dev;
    gamefs::GameFiles* files = nullptr;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11BlendState> blendAlpha, blendAdd;
    ComPtr<ID3D11DepthStencilState> depthOff;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> samp;
    ComPtr<ID3D11Buffer> ib, cb, vb;
    uint32_t vbVertices = 0;
    Tex dflt;
    std::map<std::string, Tex> cache;   // lower-case name -> texture

    // ---- ticket 0642: the loader thread. `known` and `failed` belong to the caller's thread; the rest is under `m`.
    struct Loaded {
        Tex tex;                        // made on the loader (a free-threaded device)
        std::vector<uint8_t> file;      // a SINGLETHREADED device: the file, parsed and created at adopt
        bool created = false, failed = false;
        Status why;
        TextureLoad load;
    };
    bool multiThreaded = true;          // the device was not created D3D11_CREATE_DEVICE_SINGLETHREADED
    std::thread worker;
    bool running = false;               // caller's view: a worker was started and not stopped
    mutable std::mutex m;
    std::condition_variable cv;
    bool stop = false;
    std::deque<std::pair<std::string, std::string>> queue;   // (key, bare file name)
    std::string inFlight;               // the key the loader works on
    std::map<std::string, Loaded> done; // finished, not adopted
    std::atomic<size_t> doneCount{0};
    std::set<std::string> known;        // requested: queued, in flight, finished or failed (caller's thread)
    std::set<std::string> failed;       // failed on the loader (caller's thread, after adopt)

    // The loader: one name at a time, read, parsed and created as DecalPass::draw does for a first use.
    static void run(Impl* p) {
        for (;;) {
            std::pair<std::string, std::string> job;
            {
                std::unique_lock<std::mutex> l(p->m);
                p->cv.wait(l, [p]() { return p->stop || !p->queue.empty(); });
                if (p->stop) return;
                job = p->queue.front();
                p->queue.pop_front();
                p->inFlight = job.first;
            }
            Loaded L;
            L.load.name = job.second;
            const std::chrono::steady_clock::time_point r0 = std::chrono::steady_clock::now();
            std::vector<uint8_t> bytes;
            Status st = p->files->exists(job.second)
                            ? p->files->read(job.second, bytes)
                            : Status::fail(Err::NotFound, "decal texture " + job.first + " not in the install");
            const std::chrono::steady_clock::time_point r1 = std::chrono::steady_clock::now();
            L.load.bytes = bytes.size();
            L.load.readMs = std::chrono::duration<double, std::milli>(r1 - r0).count();
            if (st.ok()) {
                gamefs::DdsImage img;
                st = gamefs::parseDds(bytes.data(), bytes.size(), img);
                if (st.ok() && p->multiThreaded) {
                    st = createFromDds(p->dev.Get(), img, job.first, L.tex);
                    L.created = st.ok();
                } else if (st.ok()) {
                    L.file.swap(bytes);
                }
            }
            L.load.createMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r1).count();
            L.failed = !st.ok();
            L.why = st;
            {
                std::lock_guard<std::mutex> l(p->m);
                p->inFlight.clear();
                p->done[job.first] = std::move(L);
                p->doneCount.store(p->done.size());
            }
            p->cv.notify_all();
        }
    }
};

DecalPass::DecalPass() {}
DecalPass::~DecalPass() {
    stopLoader();
    delete impl_;
}

Status DecalPass::startLoader() {
    if (!impl_) return Status::fail(Err::NotFound, "decal pass: startLoader before init");
    if (!impl_->files) return Status::fail(Err::NotFound, "decal pass: startLoader without game files");
    Impl& p = *impl_;
    if (p.running) return Status::Ok();
    p.multiThreaded = (p.dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) == 0;
    {
        std::lock_guard<std::mutex> l(p.m);
        p.stop = false;
    }
    p.worker = std::thread(&Impl::run, impl_);
    p.running = true;
    return Status::Ok();
}

void DecalPass::stopLoader() {
    if (!impl_ || !impl_->running) return;
    Impl& p = *impl_;
    {
        std::lock_guard<std::mutex> l(p.m);
        p.stop = true;
    }
    p.cv.notify_all();
    if (p.worker.joinable()) p.worker.join();
    std::lock_guard<std::mutex> l(p.m);
    for (const auto& q : p.queue) p.known.erase(q.first);   // never loaded: a draw reads it itself
    p.queue.clear();
    p.running = false;
}

bool DecalPass::loaderOn() const { return impl_ && impl_->running; }

size_t DecalPass::request(const std::vector<std::string>& names) {
    if (!impl_ || !impl_->running) return 0;
    Impl& p = *impl_;
    size_t n = 0;
    {
        std::lock_guard<std::mutex> l(p.m);
        for (const std::string& name : names) {
            const std::string key = lower(name);
            if (key.empty() || p.cache.count(key) || p.known.count(key)) continue;
            std::string bare = key;
            const size_t slash = bare.find_last_of("/\\");
            if (slash != std::string::npos) bare = bare.substr(slash + 1);
            p.queue.push_back(std::make_pair(key, bare));
            p.known.insert(key);
            ++n;
        }
    }
    if (n) p.cv.notify_all();
    loaderCounters_.requested += n;
    return n;
}

size_t DecalPass::adopt() {
    if (!impl_ || impl_->doneCount.load() == 0) return 0;
    Impl& p = *impl_;
    std::map<std::string, Impl::Loaded> got;
    {
        std::lock_guard<std::mutex> l(p.m);
        got.swap(p.done);
        p.doneCount.store(0);
    }
    size_t n = 0;
    for (auto& kv : got) {
        Impl::Loaded& L = kv.second;
        Status st = L.why;
        Tex t = L.tex;
        if (st.ok() && !L.created) {   // a SINGLETHREADED device: created here, on the caller's thread
            gamefs::DdsImage img;
            st = gamefs::parseDds(L.file.data(), L.file.size(), img);
            if (st.ok()) st = createFromDds(p.dev.Get(), img, kv.first, t);
            if (st.ok()) ++loaderCounters_.createdAtAdopt;
        }
        if (!st.ok()) {
            p.failed.insert(kv.first);
            ++loaderCounters_.failed;
            continue;
        }
        if (!p.cache.count(kv.first)) p.cache[kv.first] = t;   // a draw that read it meanwhile keeps its own
        ++loaderCounters_.adopted;
        loaderCounters_.bytes += L.load.bytes;
        loaderCounters_.readMs += L.load.readMs;
        loaderCounters_.readMsMax = std::max(loaderCounters_.readMsMax, L.load.readMs);
        loaderCounters_.createMs += L.load.createMs;
        loaderLoads_.push_back(L.load);
        ++n;
    }
    return n;
}

bool DecalPass::ready(const std::vector<std::string>& names) {
    if (!impl_) return names.empty();
    adopt();
    for (const std::string& name : names) {
        const std::string key = lower(name);
        if (impl_->cache.count(key) || impl_->failed.count(key)) continue;
        return false;
    }
    return true;
}

size_t DecalPass::loaderPending() const {
    if (!impl_) return 0;
    std::lock_guard<std::mutex> l(impl_->m);
    return impl_->queue.size() + (impl_->inFlight.empty() ? 0 : 1) + impl_->done.size();
}

size_t DecalPass::count(const TileDecalList& list, uint32_t pass) {
    if (!list.set) return 0;
    size_t n = 0;
    for (uint32_t i : list.indices)
        if (i < list.set->decals.size() && list.set->decals[i].pass == pass) ++n;
    return n;
}

Status DecalPass::init(ID3D11Device* device, const shaders::ShaderSection& sec, gamefs::GameFiles* files,
                       const DecalShaderVas& vas) {
    stopLoader();   // 0642: the loader reads through the old files and device
    delete impl_;
    impl_ = nullptr;
    if (!device) return Status::fail(Err::BadLayout, "decal pass init: no device");
    std::unique_ptr<Impl> p(new Impl());
    p->dev = device;
    p->files = files;
    const shaders::ShaderBlob* vsb = sec.findByVa(vas.vs);
    if (!vsb) return Status::fail(Err::NotFound, "TerrainDecalShader VS blob " + hexVa(vas.vs) + " not in the .shader section");
    const shaders::ShaderBlob* psb = sec.findByVa(vas.ps3);
    if (!psb) return Status::fail(Err::NotFound, "TerrainDecalShader PS blob " + hexVa(vas.ps3) + " not in the .shader section");
    C5R_TRY(checkBlob(*vsb, "TerrainDecalShader VS", shaders::Stage::Vertex));
    C5R_TRY(checkBlob(*psb, "TerrainDecalShader PS (three targets)", shaders::Stage::Pixel));
    C5R_TRY(hrOk(device->CreateVertexShader(vsb->bytes, vsb->size, nullptr, &p->vs), "CreateVertexShader (decal)"));
    C5R_TRY(hrOk(device->CreatePixelShader(psb->bytes, psb->size, nullptr, &p->ps), "CreatePixelShader (decal)"));
    const D3D11_INPUT_ELEMENT_DESC el[] = {
        {"POSITION", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    C5R_TRY(hrOk(device->CreateInputLayout(el, 2, vsb->bytes, vsb->size, &p->layout), "CreateInputLayout (decal)"));

    // States as captured (terrain-decals.md 4.1, 4.4).
    D3D11_BLEND_DESC bd = {};
    bd.IndependentBlendEnable = FALSE;
    D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ONE;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->blendAlpha), "CreateBlendState (decal)"));
    rt.SrcBlend = D3D11_BLEND_ONE;
    rt.DestBlend = D3D11_BLEND_ONE;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->blendAdd), "CreateBlendState (decal macro shadow)"));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dd.BackFace = dd.FrontFace;
    C5R_TRY(hrOk(device->CreateDepthStencilState(&dd, &p->depthOff), "CreateDepthStencilState (decal)"));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    C5R_TRY(hrOk(device->CreateRasterizerState(&rd, &p->raster), "CreateRasterizerState (decal)"));
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    C5R_TRY(hrOk(device->CreateSamplerState(&sd, &p->samp), "CreateSamplerState (decal)"));

    // The identity index buffer (index i = i, 0..0xFFFE), immutable, as the EXE's.
    std::vector<uint16_t> ident(kIdentity);
    for (uint32_t i = 0; i < kIdentity; ++i) ident[i] = static_cast<uint16_t>(i);
    D3D11_BUFFER_DESC b = {};
    b.ByteWidth = kIdentity * 2;
    b.Usage = D3D11_USAGE_IMMUTABLE;
    b.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA s = {ident.data(), 0, 0};
    C5R_TRY(hrOk(device->CreateBuffer(&b, &s, &p->ib), "CreateBuffer (decal identity indices)"));
    b.ByteWidth = kDecalCbFloats * 4;
    b.Usage = D3D11_USAGE_DYNAMIC;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    C5R_TRY(hrOk(device->CreateBuffer(&b, nullptr, &p->cb), "CreateBuffer (decal constants)"));

    // The EXE's default texture for handle 0.
    uint8_t texels[8 * 8 * 4];
    defaultDecalTexels(texels);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 8;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA ts = {texels, 8 * 4, 0};
    ComPtr<ID3D11Texture2D> t;
    C5R_TRY(hrOk(device->CreateTexture2D(&td, &ts, &t), "CreateTexture2D (decal default texture)"));
    C5R_TRY(makeViews(device, t.Get(), p->dflt));
    impl_ = p.release();
    return Status::Ok();
}

Status DecalPass::provideTexture(const std::string& name, ID3D11Texture2D* tex) {
    if (!impl_) return Status::fail(Err::NotFound, "decal pass: provideTexture before init");
    if (!tex) return Status::fail(Err::BadLayout, "decal pass: provideTexture without a texture");
    D3D11_RESOURCE_DIMENSION dim;
    tex->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return Status::fail(Err::BadLayout, "decal pass: " + name + " is not 2D");
    Tex t;
    C5R_TRY(makeViews(impl_->dev.Get(), tex, t));
    impl_->cache[lower(name)] = t;
    return Status::Ok();
}

Status DecalPass::drawBlend(ID3D11DeviceContext* ctx, int originX, int originY, const TileDecalList& list,
                            ID3D11Texture2D* const blend[3]) {
    return draw(ctx, originX, originY, list, 0, blend, 3);
}

Status DecalPass::drawMacroShadow(ID3D11DeviceContext* ctx, int originX, int originY, const TileDecalList& list,
                                  ID3D11Texture2D* macroShadow) {
    ID3D11Texture2D* t[1] = {macroShadow};
    return draw(ctx, originX, originY, list, 1, t, 1);
}

Status DecalPass::draw(ID3D11DeviceContext* ctx, int originX, int originY, const TileDecalList& list, uint32_t pass,
                       ID3D11Texture2D* const* targets, uint32_t targetCount) {
    if (!impl_) return Status::fail(Err::NotFound, "decal pass: draw before init");
    if (!ctx) return Status::fail(Err::BadLayout, "decal pass: no context");
    if (list.indices.empty()) return Status::Ok();
    if (!list.set) return Status::fail(Err::BadLayout, "decal pass: list without a decal set");
    Impl& p = *impl_;
    const DecalSet& set = *list.set;

    // 1. Check every decal of the pass; collect them in draw order.
    std::vector<const Decal*> run;
    uint64_t vertices = 0;
    for (uint32_t i : list.indices) {
        if (i >= set.decals.size()) {
            ++counters_.badDecals;
            return Status::fail(Err::BadLayout, "decal pass: list index " + std::to_string(i) + " outside the set's " +
                                                    std::to_string(set.decals.size()) + " decals");
        }
        const Decal& d = set.decals[i];
        if (d.pass != pass) continue;
        for (int k = 0; k < 3; ++k)
            if (d.texture[k] != kNoTexture && d.texture[k] >= set.textures.size()) {
                ++counters_.badDecals;
                return Status::fail(Err::BadLayout, "decal " + std::to_string(i) + ": texture index " +
                                                        std::to_string(d.texture[k]) + " outside the set's " +
                                                        std::to_string(set.textures.size()) + " names");
            }
        if (d.vertexCount == 0 || d.vertexCount % 3 || d.vertexCount > kMaxRun ||
            static_cast<uint64_t>(d.firstVertex) + d.vertexCount > set.vertices.size()) {
            ++counters_.badDecals;
            return Status::fail(Err::BadLayout, "decal " + std::to_string(i) + ": vertex run " + std::to_string(d.firstVertex) +
                                                    " + " + std::to_string(d.vertexCount) + " outside the set's " +
                                                    std::to_string(set.vertices.size()) +
                                                    " vertices, empty, not whole triangles or longer than 65,535");
        }
        run.push_back(&d);
        vertices += d.vertexCount;
    }
    if (run.empty()) return Status::Ok();
    DXGI_FORMAT viewFmt[3] = {};
    for (uint32_t k = 0; k < targetCount; ++k) C5R_TRY(checkTarget(targets[k], pass == 0, static_cast<int>(k), viewFmt[k]));

    // 2. Textures: every name the run uses, loaded once per pass object. 0642: with the loader, what it finished is
    // taken first, a name in flight is waited for, and only a name it never had (or failed) is read here.
    adopt();
    std::vector<const Tex*> bound(run.size() * 3, &p.dflt);
    for (size_t r = 0; r < run.size(); ++r)
        for (int k = 0; k < 3; ++k) {
            const uint32_t ti = run[r]->texture[k];
            if (ti == kNoTexture) continue;
            const std::string key = lower(set.textures[ti]);
            auto it = p.cache.find(key);
            if (it == p.cache.end() && p.running && p.known.count(key) && !p.failed.count(key)) {
                const std::chrono::steady_clock::time_point w0 = std::chrono::steady_clock::now();
                {
                    std::unique_lock<std::mutex> l(p.m);
                    p.cv.wait(l, [&p, &key]() { return p.stop || p.done.count(key) != 0; });
                }
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count();
                ++loaderCounters_.waits;
                loaderCounters_.waitMs += ms;
                loaderCounters_.waitMsMax = std::max(loaderCounters_.waitMsMax, ms);
                adopt();
                it = p.cache.find(key);
            }
            if (it == p.cache.end()) {
                if (p.running) ++loaderCounters_.syncLoads;
                if (!p.files) {
                    ++counters_.missingTextures;
                    return Status::fail(Err::NotFound, "decal texture " + set.textures[ti] + ": no game files and not provided");
                }
                // gamefs looks names up by bare file name.
                const std::chrono::steady_clock::time_point r0 = std::chrono::steady_clock::now();
                std::string bare = key;
                const size_t slash = bare.find_last_of("/\\");
                if (slash != std::string::npos) bare = bare.substr(slash + 1);
                if (!p.files->exists(bare)) {
                    ++counters_.missingTextures;
                    return Status::fail(Err::NotFound, "decal texture " + set.textures[ti] + " not in the install");
                }
                std::vector<uint8_t> bytes;
                C5R_TRY(p.files->read(bare, bytes));
                const std::chrono::steady_clock::time_point r1 = std::chrono::steady_clock::now();
                gamefs::DdsImage img;
                C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
                Tex t;
                C5R_TRY(createFromDds(p.dev.Get(), img, set.textures[ti], t));
                ++counters_.texturesLoaded;
                TextureLoad l;   // issue 0417
                l.name = bare;
                l.bytes = bytes.size();
                l.readMs = std::chrono::duration<double, std::milli>(r1 - r0).count();
                l.createMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r1).count();
                loads_.push_back(l);
                it = p.cache.insert(std::make_pair(key, t)).first;
            }
            bound[r * 3 + k] = &it->second;
        }

    // 3. The run's vertices, gathered in draw order into one dynamic buffer.
    if (vertices > p.vbVertices) {
        p.vb.Reset();
        D3D11_BUFFER_DESC b = {};
        const uint32_t cap = static_cast<uint32_t>(std::max<uint64_t>(vertices, 1u << 16));
        b.ByteWidth = cap * sizeof(DecalVertex);
        b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(p.dev->CreateBuffer(&b, nullptr, &p.vb), "CreateBuffer (decal vertices)"));
        p.vbVertices = cap;
    }
    {
        D3D11_MAPPED_SUBRESOURCE m;
        C5R_TRY(hrOk(ctx->Map(p.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (decal vertices)"));
        DecalVertex* out = static_cast<DecalVertex*>(m.pData);
        for (const Decal* d : run) {
            std::memcpy(out, set.vertices.data() + d->firstVertex, d->vertexCount * sizeof(DecalVertex));
            out += d->vertexCount;
        }
        ctx->Unmap(p.vb.Get(), 0);
    }

    // 4. The draws.
    std::vector<ComPtr<ID3D11RenderTargetView>> rtv(targetCount);
    for (uint32_t k = 0; k < targetCount; ++k) {
        D3D11_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = viewFmt[k];
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Texture2D.MipSlice = 0;
        C5R_TRY(hrOk(p.dev->CreateRenderTargetView(targets[k], &rv, &rtv[k]), "CreateRenderTargetView (decal target)"));
    }
    ID3D11RenderTargetView* rts[3] = {};
    for (uint32_t k = 0; k < targetCount; ++k) rts[k] = rtv[k].Get();
    ID3D11ShaderResourceView* const none[16] = {};
    ID3D11RenderTargetView* const noRt[8] = {};
    ctx->OMSetRenderTargets(8, noRt, nullptr);
    ctx->PSSetShaderResources(0, 16, none);
    ctx->OMSetRenderTargets(targetCount, rts, nullptr);
    const float zero4[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(pass == 0 ? p.blendAlpha.Get() : p.blendAdd.Get(), zero4, 0xffffffff);
    ctx->OMSetDepthStencilState(p.depthOff.Get(), 0);
    ctx->RSSetState(p.raster.Get());
    const D3D11_VIEWPORT vp = {0, 0, static_cast<float>(kTarget), static_cast<float>(kTarget), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(p.layout.Get());
    const UINT stride = sizeof(DecalVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, p.vb.GetAddressOf(), &stride, &off);
    ctx->IASetIndexBuffer(p.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->VSSetShader(p.vs.Get(), nullptr, 0);
    ctx->PSSetShader(p.ps.Get(), nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, p.cb.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, p.cb.GetAddressOf());
    ID3D11SamplerState* s3[3] = {p.samp.Get(), p.samp.Get(), p.samp.Get()};
    ctx->PSSetSamplers(0, 3, s3);
    uint32_t base = 0;
    for (size_t r = 0; r < run.size(); ++r) {
        const Decal& d = *run[r];
        float c[kDecalCbFloats];
        decalConstants(originX, originY, d, c);
        D3D11_MAPPED_SUBRESOURCE m;
        C5R_TRY(hrOk(ctx->Map(p.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (decal constants)"));
        std::memcpy(m.pData, c, sizeof c);
        ctx->Unmap(p.cb.Get(), 0);
        ID3D11ShaderResourceView* t[3] = {bound[r * 3 + 0]->srgb.Get(), bound[r * 3 + 1]->lin.Get(), bound[r * 3 + 2]->lin.Get()};
        ctx->PSSetShaderResources(0, 3, t);
        ctx->DrawIndexed(d.vertexCount, 0, static_cast<INT>(base));
        base += d.vertexCount;
        ++counters_.draws;
    }
    ctx->OMSetRenderTargets(8, noRt, nullptr);
    ctx->PSSetShaderResources(0, 16, none);
    if (pass == 0) ++counters_.tilesWithDecals;
    return Status::Ok();
}

}} // namespace c5r::decal
