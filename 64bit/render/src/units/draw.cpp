// draw.cpp - the unit module's frame: prepare (L0-L3 on the CPU into a FramePlan) and draw (palette blocks, id
// stream, cb1 per pass, material 87's cb0, the three passes with the states of unit-draws.md 1) on the module's own
// device (epic 0041, issue 4352; research/05-graphics-composition/unit-renderer.md 5, 10). Own code.
#include "c5r/units/units.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "c5r/gamefs/gamefiles.h"

namespace c5r { namespace units {

const char* const kEnvironmentTextures[4] = {"unit_irradiance.dds", "unit_environment_dull.dds",
                                             "unit_environment_sharp.dds", "fogramp.dds"};

// ---------------------------------------------------------------------------------------------- small pieces
void paletteHeader(const float colour[4], float fade_from, float fade_to, float fade, Float4 rows[2]) {
    rows[0] = {colour[0], colour[1], colour[2], 1.0f};
    rows[1] = {fade_from, fade_to, fade, 0.0f};
}

void yawRows(float yaw, const float T[3], Float4 out[3]) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    out[0] = {c, -s, 0, T[0]};
    out[1] = {s, c, 0, T[1]};
    out[2] = {0, 0, 1, T[2]};
}

void staticCb0(const float colour[4], float fade_from, float fade_to, float fade, const Float4 world[3],
               Float4 out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = Float4();
    out[0] = world[0];
    out[1] = world[1];
    out[2] = world[2];
    out[3] = {colour[0], colour[1], colour[2], 1.0f};
    out[4] = {fade_from, fade_to, fade, 0.0f};
}

uint32_t MeshDraw::instances() const {
    return material == Material::Static ? static_cast<uint32_t>(cb0.size() / 16) : static_cast<uint32_t>(blocks.size());
}

void FramePlan::clear() {
    units.clear();
    palette.clear();
    blocks = 0;
    water_level = 0;
}

size_t FramePlan::drawn() const {
    size_t n = 0;
    for (auto& u : units) n += u.status.ok() ? 1 : 0;
    return n;
}

void buildIdStream(const FramePlan& plan, size_t passes, IdStream& out) {
    out.ids.clear();
    out.offsets.assign(passes, std::vector<uint32_t>());
    for (size_t p = 0; p < passes; ++p)
        for (auto& u : plan.units) {
            if (!u.status.ok()) continue;
            for (auto& d : u.draws) {
                if (d.material == Material::Static) continue;
                out.offsets[p].push_back(static_cast<uint32_t>(out.ids.size() * 4));
                for (uint32_t b : d.blocks) out.ids.push_back(b % kBlocksPerBuffer);
            }
        }
}

// ---------------------------------------------------------------------------------------------- renderer
struct UnitRenderer::Gpu {
    std::vector<ID3D11Buffer*> palette;
    std::vector<ID3D11ShaderResourceView*> paletteSrv;
    ID3D11Buffer* ids = nullptr;
    size_t idBytes = 0;
    ID3D11Buffer* cb1 = nullptr;
    ID3D11Buffer* cb0 = nullptr;
    ID3D11RasterizerState* cullBack = nullptr;
    ID3D11RasterizerState* cullFront = nullptr;
    ID3D11DepthStencilState* depthOn = nullptr;
    ID3D11DepthStencilState* depthOff = nullptr;
    ID3D11BlendState* alpha = nullptr;
    ID3D11BlendState* opaque = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    template <class T> static void rel(T*& p) {
        if (p) p->Release();
        p = nullptr;
    }
    ~Gpu() {
        for (auto*& b : palette) rel(b);
        for (auto*& v : paletteSrv) rel(v);
        rel(ids);
        rel(cb1);
        rel(cb0);
        rel(cullBack);
        rel(cullFront);
        rel(depthOn);
        rel(depthOff);
        rel(alpha);
        rel(opaque);
        rel(sampler);
    }
};

UnitRenderer::UnitRenderer() = default;
UnitRenderer::~UnitRenderer() = default;

namespace {
Status hrFail(const char* what, HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", static_cast<unsigned>(hr));
    return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + " " + b);
}
}  // namespace

Status UnitRenderer::open(ID3D11Device* dev, const gamefs::GameFiles& files, c5m::TableSource& db,
                          const std::wstring& exe, const std::wstring& installRoot) {
    open_ = false;
    dev_ = dev;
    rows_.clear();    // issue 4464: the row cache names models of the cache below and rows of the tables
    rowOf_.clear();
    provider_.reset(new GameFilesProvider(files));
    UnitStatus us = art_.open(db, *provider_);
    if (!us.ok()) return Status::fail(Err::BadLayout, us.str());
    strings_.reset(new c5m::StringDatabase());
    us = loadStringDatabases(files.names(), *provider_, *strings_);
    if (!us.ok()) return Status::fail(Err::BadLayout, us.str());
    models_.reset(new ModelCache(*provider_));
    models_->setStrings(strings_.get());
    for (int i = 0; i < 4; ++i) {
        us = models_->texture(kEnvironmentTextures[i], env_[i]);
        if (!us.ok()) return Status::fail(Err::NotFound, "environment texture t" + std::to_string(2 + i) + ": " + us.str());
    }
    // A missing program (or EXE) does not stop open(): the units of that material get ProgramNotFound.
    programStatus_ = programs_.open(exe);
    ownPose_ = makeModulePose(*provider_, strings_.get());
    Status ps;
    std::wstring root = installRoot;
    if (root.empty()) {
        const size_t slash = exe.find_last_of(L"/\\");
        root = slash == std::wstring::npos ? std::wstring(L".") : exe.substr(0, slash);
    }
    ownPlace_ = makeModulePlacement(files, root, &ps);
    C5R_TRY(ps);
    gpu_.reset(new Gpu());
    if (dev) {
        C5R_TRY(programs_.create(dev));
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.FrontCounterClockwise = TRUE;
        r.DepthClipEnable = TRUE;
        r.CullMode = D3D11_CULL_BACK;
        HRESULT hr = dev->CreateRasterizerState(&r, &gpu_->cullBack);
        if (FAILED(hr)) return hrFail("CreateRasterizerState", hr);
        r.CullMode = D3D11_CULL_FRONT;
        hr = dev->CreateRasterizerState(&r, &gpu_->cullFront);
        if (FAILED(hr)) return hrFail("CreateRasterizerState", hr);
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = TRUE;
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        hr = dev->CreateDepthStencilState(&d, &gpu_->depthOn);
        if (FAILED(hr)) return hrFail("CreateDepthStencilState", hr);
        d.DepthEnable = FALSE;
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        hr = dev->CreateDepthStencilState(&d, &gpu_->depthOff);
        if (FAILED(hr)) return hrFail("CreateDepthStencilState", hr);
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = dev->CreateBlendState(&b, &gpu_->alpha);
        if (FAILED(hr)) return hrFail("CreateBlendState", hr);
        b.RenderTarget[0].BlendEnable = FALSE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        hr = dev->CreateBlendState(&b, &gpu_->opaque);
        if (FAILED(hr)) return hrFail("CreateBlendState", hr);
        D3D11_SAMPLER_DESC s = {};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        s.MaxAnisotropy = 1;
        s.ComparisonFunc = D3D11_COMPARISON_NEVER;
        s.MinLOD = 0;
        s.MaxLOD = FLT_MAX;
        hr = dev->CreateSamplerState(&s, &gpu_->sampler);
        if (FAILED(hr)) return hrFail("CreateSamplerState", hr);
        D3D11_BUFFER_DESC cb = {};
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cb.ByteWidth = kCb1Bytes;
        hr = dev->CreateBuffer(&cb, nullptr, &gpu_->cb1);
        if (FAILED(hr)) return hrFail("CreateBuffer cb1", hr);
        cb.ByteWidth = kStaticCb0Bytes;
        hr = dev->CreateBuffer(&cb, nullptr, &gpu_->cb0);
        if (FAILED(hr)) return hrFail("CreateBuffer cb0", hr);
    }
    open_ = true;
    return Status::Ok();
}

// ---------------------------------------------------------------------------------------------- L0 and L1 per art row
// Issue 4464 (units.h): the rule of a row is buildRow; loadRow keeps its result per row while the cache is on.
UnitStatus UnitRenderer::rowOf(const UnitInput& u, const PlayerInput* owner, std::string& row) {
    if (!u.art_row.empty()) {
        row = u.art_row;
        return UnitStatus::Ok();
    }
    static const std::string kNone;
    const std::string& style = owner ? owner->art_style : kNone;
    const std::string& era = owner ? owner->era : kNone;
    if (!artCache_) return art_.art_row(u.unit_type, style, era, row);
    std::string key;
    key.reserve(u.unit_type.size() + style.size() + era.size() + 2);
    key += u.unit_type;
    key += '\n';
    key += style;
    key += '\n';
    key += era;
    auto it = rowOf_.find(key);
    if (it == rowOf_.end()) {
        std::pair<UnitStatus, std::string> v;
        v.first = art_.art_row(u.unit_type, style, era, v.second);
        it = rowOf_.emplace(std::move(key), std::move(v)).first;
    }
    row = it->second.second;
    return it->second.first;
}

void UnitRenderer::buildRow(const std::string& row, bool cachedArt, LoadedRow& out) {
    out = LoadedRow();
    if (cachedArt) {
        out.status = art_.resolve_shared(row, out.art);
    } else {
        std::shared_ptr<UnitArt> a = std::make_shared<UnitArt>();
        out.status = art_.resolve_uncached(row, *a);
        out.art = std::move(a);
    }
    if (!out.status.ok()) return;
    const UnitArt& art = *out.art;
    for (auto& m : art.members) {
        const UnitModel* model = nullptr;
        UnitStatus st = models_->get(m, model);
        if (!st.ok()) {
            out.status = UnitStatus::fail(st.code, art.art_row + ": member " + m.member + ": " + st.text);
            return;
        }
        out.models.push_back(model);
    }
    for (size_t i = 0; i < out.models.size(); ++i)
        for (auto& mesh : out.models[i]->meshes)
            for (PassKind p : {PassKind::Scene, PassKind::ShadowMask})
                for (bool vs : {true, false})
                    if (!programs_.bytes(mesh.material, p, vs)) {
                        out.status = UnitStatus::fail(UnitCode::ProgramNotFound,
                                                      art.art_row + ": " + out.models[i]->gr2 + " mesh " + mesh.name +
                                                      " (material " + std::to_string(static_cast<int>(mesh.material)) +
                                                      "): " + programStatus_.str());
                        return;
                    }
    UnitStatus ok;
    for (auto* model : out.models)
        ok.warnings.insert(ok.warnings.end(), model->fallbacks.begin(), model->fallbacks.end());
    out.status = ok;
}

const UnitRenderer::LoadedRow& UnitRenderer::loadRow(const std::string& row, LoadedRow& scratch) {
    if (!artCache_) {
        buildRow(row, false, scratch);
        return scratch;
    }
    auto it = rows_.find(row);
    if (it == rows_.end()) {
        it = rows_.emplace(row, LoadedRow()).first;
        buildRow(row, true, it->second);
    }
    return it->second;
}

UnitStatus UnitRenderer::loadUnit(const UnitInput& u, const PlayerInput* owner, UnitArt& art,
                                  std::vector<const UnitModel*>& models) {
    models.clear();
    std::string row;
    UnitStatus st = rowOf(u, owner, row);
    if (!st.ok()) return st;
    LoadedRow scratch;
    const LoadedRow& lr = loadRow(row, scratch);
    art = *lr.art;
    art.unit_type = u.unit_type;
    models = lr.models;
    return lr.status;
}

Status UnitRenderer::prepare(const UnitScene& scene, const heightfield::HeightGrid* hf, FramePlan& out) {
    out.clear();
    if (!open_) return Status::fail(Err::NotFound, "unit renderer not opened");
    out.water_level = scene.water_level;
    PoseSource* pose = pose_ ? pose_ : ownPose_.get();
    PlacementSource* place = place_ ? place_ : ownPlace_.get();
    std::map<int32_t, const PlayerInput*> players;
    for (auto& p : scene.players) players[p.player] = &p;
    Float4 rows[kPaletteBones * 3];
    for (auto& u : scene.units) {
        out.units.emplace_back();
        UnitPlan& up = out.units.back();
        up.player = u.player;
        up.id = u.id;
        auto pi = players.find(u.player);
        if (pi == players.end()) {
            up.status = UnitStatus::fail(UnitCode::BadInput, "player " + std::to_string(u.player) + " not in the scene");
            continue;
        }
        float colour[4];
        up.status = art_.team_colour(pi->second->colour, colour);
        if (!up.status.ok()) continue;
        // issue 4464: L0 and L1 from the row cache (one load per art row, no .fxsxml read per frame; units.h)
        std::string row;
        up.status = rowOf(u, pi->second, row);
        if (!up.status.ok()) continue;
        LoadedRow scratch;
        const LoadedRow& lr = loadRow(row, scratch);
        up.art = lr.art;
        up.status = lr.status;
        const std::vector<const UnitModel*>& models = lr.models;
        const UnitArt& art = *up.art;
        const std::vector<std::string> warnings = up.status.warnings;   // kept when the unit ends Ok
        if (!up.status.ok()) continue;
        // figures in member order
        std::vector<uint16_t> memberOf;
        for (size_t m = 0; m < art.members.size(); ++m)
            for (int k = 0; k < art.members[m].count; ++k) memberOf.push_back(static_cast<uint16_t>(m));
        std::map<uint32_t, const FigureInput*> figIn;
        for (auto& f : u.figures) {
            if (f.figure >= memberOf.size()) {
                up.status = UnitStatus::fail(UnitCode::BadInput, "figure " + std::to_string(f.figure) + " of " +
                                             std::to_string(memberOf.size()) + " (" + art.art_row + ")");
                break;
            }
            figIn[f.figure] = &f;
        }
        if (!up.status.ok()) continue;
        PlacementRequest pr;
        pr.unit = &u;
        pr.art = &art;
        pr.grid = hf;
        pr.water_level = scene.water_level;
        up.status = place->place(pr, up.placement);
        if (!up.status.ok()) {
            if (up.status.code != UnitCode::PlacementFailed)
                up.status = UnitStatus::fail(UnitCode::PlacementFailed, up.status.str());
            continue;
        }
        // draws: per member, per mesh; blocks figure-major (a figure's skinned meshes consecutive)
        std::vector<Float4> palette;
        std::vector<MeshDraw> draws;
        std::map<std::pair<uint16_t, uint32_t>, size_t> drawOf;   // (member, mesh) -> draws index
        for (size_t m = 0; m < art.members.size(); ++m)
            for (uint32_t j = 0; j < models[m]->meshes.size(); ++j) {
                MeshDraw d;
                d.model = models[m];
                d.mesh = &models[m]->meshes[j];
                d.material = d.mesh->material;
                d.member = static_cast<uint16_t>(m);
                drawOf[std::make_pair(static_cast<uint16_t>(m), j)] = draws.size();
                draws.push_back(d);
            }
        uint32_t block = out.blocks;
        for (auto& fp : up.placement.placed) {
            if (fp.figure >= memberOf.size()) {
                up.status = UnitStatus::fail(UnitCode::PlacementFailed, "placement gave figure " +
                                             std::to_string(fp.figure) + " of " + std::to_string(memberOf.size()));
                break;
            }
            const uint16_t m = memberOf[fp.figure];
            const UnitModel* model = models[m];
            auto fi = figIn.find(fp.figure);
            for (uint32_t j = 0; j < model->meshes.size() && up.status.ok(); ++j) {
                const ModelMesh& mesh = model->meshes[j];
                PoseRequest rq;
                rq.unit = &u;
                rq.member = &art.members[m];
                rq.model = model;
                rq.mesh = &mesh;
                rq.figure = fp.figure;
                rq.clip = fi != figIn.end() ? fi->second->clip : std::string();
                rq.t = fi != figIn.end() ? fi->second->t : 0.0f;
                rq.scale = art.members[m].scale;
                rq.yaw = fp.yaw;
                rq.T[0] = fp.x;
                rq.T[1] = fp.y;
                rq.T[2] = fp.z;
                MeshDraw& d = draws[drawOf[std::make_pair(m, j)]];
                const bool needPose = mesh.material != Material::Static || mesh.bones > 0;
                if (needPose) {
                    up.status = pose->rows(rq, rows);
                    if (!up.status.ok()) break;
                }
                if (mesh.material == Material::Static) {
                    Float4 world[3];
                    if (mesh.bones > 0) {
                        world[0] = rows[0];
                        world[1] = rows[1];
                        world[2] = rows[2];
                    } else {
                        yawRows(fp.yaw, rq.T, world);
                    }
                    Float4 cb[16];
                    staticCb0(colour, u.fade_from, u.fade_to, u.fade, world, cb);
                    d.cb0.insert(d.cb0.end(), cb, cb + 16);
                    continue;
                }
                Float4 head[2];
                paletteHeader(colour, u.fade_from, u.fade_to, u.fade, head);
                palette.push_back(head[0]);
                palette.push_back(head[1]);
                palette.insert(palette.end(), rows, rows + kPaletteBones * 3);
                d.blocks.push_back(block++);
            }
            if (!up.status.ok()) break;
        }
        if (!up.status.ok()) continue;   // nothing of the unit is kept
        up.status.warnings = warnings;
        up.first_block = out.blocks;
        up.blocks = block - out.blocks;
        out.blocks = block;
        out.palette.insert(out.palette.end(), palette.begin(), palette.end());
        for (auto& d : draws)
            if (d.instances() > 0) up.draws.push_back(std::move(d));
    }
    return Status::Ok();
}

Status UnitRenderer::ensureBuffers(uint32_t paletteBuffers, size_t idBytes) {
    D3D11_BUFFER_DESC d = {};
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    while (gpu_->palette.size() < paletteBuffers) {
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.ByteWidth = kPaletteBufferBytes;
        ID3D11Buffer* b = nullptr;
        HRESULT hr = dev_->CreateBuffer(&d, nullptr, &b);
        if (FAILED(hr)) return hrFail("CreateBuffer palette", hr);
        gpu_->palette.push_back(b);
        D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
        v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        v.Buffer.FirstElement = 0;
        v.Buffer.NumElements = kPaletteBufferBytes / 16;
        ID3D11ShaderResourceView* srv = nullptr;
        hr = dev_->CreateShaderResourceView(b, &v, &srv);
        if (FAILED(hr)) return hrFail("CreateShaderResourceView palette", hr);
        gpu_->paletteSrv.push_back(srv);
    }
    const size_t want = idBytes > kIdBufferBytes ? idBytes : kIdBufferBytes;
    if (!gpu_->ids || gpu_->idBytes < want) {
        Gpu::rel(gpu_->ids);
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.ByteWidth = static_cast<UINT>(want);
        HRESULT hr = dev_->CreateBuffer(&d, nullptr, &gpu_->ids);
        if (FAILED(hr)) return hrFail("CreateBuffer ids", hr);
        gpu_->idBytes = want;
    }
    return Status::Ok();
}

namespace {
Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const void* data, size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE m;
    HRESULT hr = ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    if (FAILED(hr)) return hrFail("Map", hr);
    if (bytes) std::memcpy(m.pData, data, bytes);
    ctx->Unmap(b, 0);
    return Status::Ok();
}
}  // namespace

Status UnitRenderer::draw(ID3D11DeviceContext* ctx, const FramePlan& plan, const PassSet& passes) {
    if (!open_ || !dev_ || !ctx) return Status::fail(Err::Device, "unit renderer has no device");
    const uint32_t nbuf = plan.blocks == 0 ? 1 : (plan.blocks + kBlocksPerBuffer - 1) / kBlocksPerBuffer;
    IdStream ids;
    buildIdStream(plan, passes.passes.size(), ids);
    C5R_TRY(ensureBuffers(nbuf, ids.ids.size() * 4));
    C5R_TRY(models_->upload(dev_));
    for (uint32_t b = 0; b < nbuf; ++b) {
        const size_t first = static_cast<size_t>(b) * kBlocksPerBuffer * kPaletteRows;
        const size_t rowsHere = plan.palette.size() > first
                                    ? std::min(plan.palette.size() - first, static_cast<size_t>(kBlocksPerBuffer) * kPaletteRows)
                                    : 0;
        C5R_TRY(upload(ctx, gpu_->palette[b], rowsHere ? &plan.palette[first] : nullptr, rowsHere * sizeof(Float4)));
    }
    C5R_TRY(upload(ctx, gpu_->ids, ids.ids.data(), ids.ids.size() * 4));
    ID3D11SamplerState* samp[6] = {gpu_->sampler, gpu_->sampler, gpu_->sampler, gpu_->sampler, gpu_->sampler, gpu_->sampler};
    for (size_t p = 0; p < passes.passes.size(); ++p) {
        const PassDesc& pd = passes.passes[p];
        const bool mask = pd.kind == PassKind::ShadowMask;
        ctx->OMSetRenderTargets(1, &pd.rtv, mask ? nullptr : pd.dsv);
        ctx->RSSetViewports(1, &pd.viewport);
        ctx->RSSetState(pd.kind == PassKind::Scene ? gpu_->cullBack : gpu_->cullFront);
        ctx->OMSetDepthStencilState(mask ? gpu_->depthOff : gpu_->depthOn, 0);
        const float bf[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(mask ? gpu_->opaque : gpu_->alpha, bf, 0xFFFFFFFFu);
        C5R_TRY(upload(ctx, gpu_->cb1, &pd.cb1, kCb1Bytes));
        ctx->VSSetConstantBuffers(1, 1, &gpu_->cb1);
        ctx->PSSetConstantBuffers(1, 1, &gpu_->cb1);
        ctx->PSSetSamplers(0, 6, samp);
        ID3D11ShaderResourceView* env[4];
        for (int i = 0; i < 4; ++i) env[i] = passes.env[i] ? passes.env[i] : (env_[i] ? env_[i]->srv : nullptr);
        ctx->PSSetShaderResources(2, 4, env);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        size_t k = 0;   // instanced draw index within the pass
        for (auto& u : plan.units) {
            if (!u.status.ok()) continue;
            for (auto& d : u.draws) {
                const ModelMesh& mesh = *d.mesh;
                ID3D11ShaderResourceView* tex[2] = {mesh.t0 ? mesh.t0->srv : nullptr, mesh.t1 ? mesh.t1->srv : nullptr};
                ctx->PSSetShaderResources(0, 2, tex);
                ctx->VSSetShader(programs_.vs(d.material, pd.kind), nullptr, 0);
                ctx->PSSetShader(programs_.ps(d.material, pd.kind), nullptr, 0);
                ctx->IASetInputLayout(programs_.layout(d.material, pd.kind));
                ctx->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R16_UINT, 0);
                const UINT idx = static_cast<UINT>(mesh.indices.size());
                if (d.material == Material::Static) {
                    UINT stride = kStaticStride, off = 0;
                    ctx->IASetVertexBuffers(0, 1, &mesh.vb, &stride, &off);
                    for (uint32_t i = 0; i < d.instances(); ++i) {
                        C5R_TRY(upload(ctx, gpu_->cb0, &d.cb0[static_cast<size_t>(i) * 16], kStaticCb0Bytes));
                        ctx->VSSetConstantBuffers(0, 1, &gpu_->cb0);
                        ctx->PSSetConstantBuffers(0, 1, &gpu_->cb0);
                        ctx->DrawIndexed(idx, 0, 0);
                    }
                    continue;
                }
                ID3D11Buffer* vbs[2] = {mesh.vb, gpu_->ids};
                UINT strides[2] = {kSkinnedStride, 4};
                const uint32_t base = ids.offsets[p][k++];
                // instances grouped by palette buffer (a draw never reads two buffers)
                size_t i = 0;
                while (i < d.blocks.size()) {
                    const uint32_t buf = d.blocks[i] / kBlocksPerBuffer;
                    size_t j = i;
                    while (j < d.blocks.size() && d.blocks[j] / kBlocksPerBuffer == buf) ++j;
                    UINT offs[2] = {0, static_cast<UINT>(base + 4 * i)};
                    ctx->IASetVertexBuffers(0, 2, vbs, strides, offs);
                    ctx->VSSetShaderResources(27, 1, &gpu_->paletteSrv[buf]);
                    ctx->PSSetShaderResources(27, 1, &gpu_->paletteSrv[buf]);
                    ctx->DrawIndexedInstanced(idx, static_cast<UINT>(j - i), 0, 0, 0);
                    i = j;
                }
            }
        }
    }
    return Status::Ok();
}

}}  // namespace c5r::units
