// inputs.cpp - the BuildingShader PS inputs read from the install (ticket 4882, epic 0561): c5r/landmarks/inputs.h.
#include "c5r/landmarks/inputs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "c5models/art.h"
#include "c5r/bake/post.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/tap.h"

namespace c5r { namespace landmarks {

namespace {

template <class T>
void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

DXGI_FORMAT srgbForm(DXGI_FORMAT f) {
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

// The pixels owned by img (a copy of the source when it was not converted), an A8R8G8B8 file's swapped to R8G8B8A8 as
// the EXE uploads it (unit-renderer.md 10.3; landmark-renderer.md 7.6's BGRA rule).
void ownAndSwap(gamefs::DdsImage& img) {
    if (!img.converted) {
        const size_t n = img.subresources.empty() ? 0 : img.subresources.back().offset + img.subresources.back().size;
        img.bytes.assign(img.src, img.src + n);
        img.converted = true;
    }
    if (img.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        for (size_t i = 0; i + 3 < img.bytes.size(); i += 4) std::swap(img.bytes[i], img.bytes[i + 2]);
        img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    img.src = nullptr;
}

std::vector<uint8_t> identityVolume() {
    std::vector<uint8_t> v(32u * 32u * 32u * 4u);
    size_t o = 0;
    for (uint32_t z = 0; z < 32; ++z)
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x) {
                v[o++] = static_cast<uint8_t>(x * 255 / 31);
                v[o++] = static_cast<uint8_t>(y * 255 / 31);
                v[o++] = static_cast<uint8_t>(z * 255 / 31);
                v[o++] = 255;
            }
    return v;
}

Status volumeOf(const gamefs::GameFiles& files, const std::string& name, std::vector<uint8_t>& out, bool& fromFile) {
    std::vector<uint8_t> tga;
    fromFile = false;
    if (!files.exists(name) || !files.read(name, tga).ok()) {
        out = identityVolume();   // ColorKeySystem keeps the identity volume when the file is missing
        return Status::Ok();
    }
    Status s = bake::buildColorKeyLut(tga.data(), tga.size(), out);
    if (!s.ok()) return Status::fail(s.code, name + ": " + s.msg);
    fromFile = true;
    return Status::Ok();
}

Status hrOk(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (0x%08lX)", static_cast<unsigned long>(hr));
    return Status::fail(Err::Device, std::string(what) + b);
}

Status makeVolume(ID3D11Device* dev, const std::vector<uint8_t>& v, ID3D11Resource** res, ID3D11ShaderResourceView** srv) {
    D3D11_TEXTURE3D_DESC d = {};
    d.Width = d.Height = d.Depth = 32;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {v.data(), 32 * 4, 32 * 32 * 4};
    ID3D11Texture3D* t = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture3D(&d, &s, &t), "CreateTexture3D (ColorKey volume)"));
    *res = t;
    return hrOk(dev->CreateShaderResourceView(t, nullptr, srv), "CreateShaderResourceView (ColorKey volume)");
}

Status makeSolid(ID3D11Device* dev, uint32_t rgba, ID3D11Resource** res, ID3D11ShaderResourceView** srv) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 1;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {&rgba, 4, 4};
    ID3D11Texture2D* t = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture2D(&d, &s, &t), "CreateTexture2D (1 x 1)"));
    *res = t;
    return hrOk(dev->CreateShaderResourceView(t, nullptr, srv), "CreateShaderResourceView (1 x 1)");
}

}  // namespace

bool envCubeCandidate(const std::string& n) {
    if (n.size() < 5 || n.compare(n.size() - 4, 4, ".dds") != 0) return false;
    for (const char* k : {"cube", "refl", "environment", "irradiance", "light"})
        if (n.find(k) != std::string::npos) return true;
    return false;
}

Status InstallInputs::open(const gamefs::GameFiles& files) {
    const auto t0 = std::chrono::steady_clock::now();
    opened_ = false;
    files_ = &files;
    counts_ = Counts();
    cubes_.clear();
    unknown_.clear();
    // t5: fogramp.dds as the EXE uploads it
    {
        std::vector<uint8_t> b;
        if (!files.exists("fogramp.dds") || !files.read("fogramp.dds", b).ok())
            return Status::fail(Err::NotFound, "fogramp.dds is not in the install (the BuildingShader PS's t5)");
        rampImg_ = gamefs::DdsImage();
        C5R_TRY(gamefs::parseDds(b.data(), b.size(), rampImg_));
        if (rampImg_.dim != gamefs::DdsDim::Tex2D || rampImg_.cube || rampImg_.arraySize != 1)
            return Status::fail(Err::Unsupported, "fogramp.dds: not a plain 2D texture");
        ownAndSwap(rampImg_);
        // the levels as uploaded, in order (the subresources' offsets are into the whole file, header included)
        rampBytes_.clear();
        for (const gamefs::DdsSubresource& sr : rampImg_.subresources)
            rampBytes_.insert(rampBytes_.end(), rampImg_.bytes.begin() + static_cast<std::ptrdiff_t>(sr.offset),
                              rampImg_.bytes.begin() + static_cast<std::ptrdiff_t>(sr.offset + sr.size));
    }
    // t6 and t9: the ColorKey volumes (style Generic)
    C5R_TRY(volumeOf(files, "Buildings_Generic_ColorKey.tga", lut6Bytes_, counts_.buildingsFromFile));
    C5R_TRY(volumeOf(files, "TerrainFOW_Generic_ColorKey.tga", lut9Bytes_, counts_.fowFromFile));
    // t2-t4: the install's cube maps by identity
    for (const std::string& name : files.names()) {
        const std::string n = c5m::basename_lower(name);
        if (!envCubeCandidate(n)) continue;
        ++counts_.candidates;
        std::vector<uint8_t> b;
        gamefs::DdsImage img;
        if (!files.read(name, b).ok() || !gamefs::parseDds(b.data(), b.size(), img).ok() || !img.cube) continue;
        ownAndSwap(img);
        std::vector<TextureLevel> mips;
        for (uint32_t m = 0; m < img.mipLevels && m < img.subresources.size(); ++m) {
            const gamefs::DdsSubresource& sr = img.subresources[m];
            mips.push_back(TextureLevel{img.bytes.data() + sr.offset, sr.size, sr.rowPitch});
        }
        const uint64_t id = textureIdentity(img.format, img.width, img.height, img.mipLevels, mips);
        if (!id) continue;
        ++counts_.cubes;
        if (cubes_.emplace(id, name).second) ++counts_.identities;
    }
    opened_ = true;
    counts_.openMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return Status::Ok();
}

Status InstallInputs::create(ID3D11Device* dev) {
    release();
    if (!opened_) return Status::fail(Err::NotFound, "the install inputs are not read (open)");
    if (!dev) return Status::fail(Err::Device, "no device");
    ID3D11Resource* r = nullptr;
    Status s = gamefs::createTexture(dev, rampImg_, &r, &ramp_);
    if (r) resources_.push_back(r);
    r = nullptr;
    if (s.ok()) s = makeVolume(dev, lut6Bytes_, &r, &lut6_);
    if (r) resources_.push_back(r);
    r = nullptr;
    if (s.ok()) s = makeVolume(dev, lut9Bytes_, &r, &lut9_);
    if (r) resources_.push_back(r);
    r = nullptr;
    if (s.ok()) s = makeSolid(dev, 0xFFFFFFFFu, &r, &white_);
    if (r) resources_.push_back(r);
    r = nullptr;
    if (s.ok()) s = makeSolid(dev, 0xFF000000u, &r, &black_);
    if (r) resources_.push_back(r);
    if (!s.ok()) {
        release();
        return s;
    }
    dev->AddRef();
    dev_ = dev;
    return Status::Ok();
}

void InstallInputs::release() {
    for (auto& kv : made_) rel(kv.second);
    made_.clear();
    counts_.made = 0;
    rel(ramp_);
    rel(lut6_);
    rel(lut9_);
    rel(white_);
    rel(black_);
    for (auto& r : resources_) rel(r);
    resources_.clear();
    rel(dev_);
}

ID3D11ShaderResourceView* InstallInputs::env(uint64_t id) {
    if (!id || !dev_) return nullptr;
    auto m = made_.find(id);
    if (m != made_.end()) return m->second;
    auto c = cubes_.find(id);
    if (c == cubes_.end()) {
        if (unknown_.emplace(id, true).second) ++counts_.unknown;
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    std::vector<uint8_t> b;
    gamefs::DdsImage img;
    if (files_ && files_->read(c->second, b).ok() && gamefs::parseDds(b.data(), b.size(), img).ok()) {
        ownAndSwap(img);
        img.format = srgbForm(img.format);   // t2-t4 are sampled through sRGB views (the captures' views [C])
        ID3D11Resource* res = nullptr;
        if (gamefs::createTexture(dev_, img, &res, &srv).ok() && res) {
            resources_.push_back(res);
        } else {
            rel(res);
            rel(srv);
        }
    }
    if (srv)
        ++counts_.made;
    else
        ++counts_.failed;
    made_[id] = srv;   // a failure is not retried
    return srv;
}

std::string InstallInputs::envName(uint64_t id) const {
    auto c = cubes_.find(id);
    return c == cubes_.end() ? std::string() : c->second;
}

void InstallInputs::fill(const ModelScene& s, uint32_t target, EnvInput& in) {
    auto add = [&](uint32_t t, const uint64_t* env) {
        if (target != ~0u && t != target) return;
        for (int k = 0; k < 3; ++k)
            if (env[k] && !in.env.count(env[k])) {
                ID3D11ShaderResourceView* v = this->env(env[k]);
                if (v) in.env[env[k]] = v;
            }
    };
    for (const DrawItem& it : s.items) add(it.target, it.env);
    for (const CpuDrawItem& it : s.cpuItems) add(it.target, it.env);   // ticket 4879's CPU-built meshes
    in.frame[0] = ramp_;
    in.frame[1] = lut6_;
    in.frame[4] = lut9_;
}

// ---------------------------------------------------------------------------------------------- the frame's camera
namespace {
void keepOne(const ModelScene& s, uint32_t vsCb1, TapCameraRows& out) {
    if (vsCb1 == kNoConst || vsCb1 >= s.consts.size() || out.count(vsCb1)) return;
    const TapConst& c = s.consts[vsCb1];
    if (c.rows < 4 || static_cast<size_t>(c.firstRow) + 4 > s.rows.size()) return;
    std::array<Float4, 5> r;
    for (int k = 0; k < 4; ++k) r[k] = s.rows[c.firstRow + k];
    r[4] = Float4{0, 0, 0, 0};   // ticket 0598: the tap's PS cb1 row 16 (the EXE's eye) when the constant holds it
    if (c.rows >= 17 && static_cast<size_t>(c.firstRow) + 17 <= s.rows.size()) r[4] = s.rows[c.firstRow + 16];
    out[vsCb1] = r;
}
Float4 rowOf(const float* m, int k) { return Float4{m[k * 4 + 0], m[k * 4 + 1], m[k * 4 + 2], m[k * 4 + 3]}; }
bool projectNdc(const Float4* m, const Float4& p, float& x, float& y) {
    const float cx = p.x * m[0].x + p.y * m[1].x + p.z * m[2].x + m[3].x;
    const float cy = p.x * m[0].y + p.y * m[1].y + p.z * m[2].y + m[3].y;
    const float cw = p.x * m[0].w + p.y * m[1].w + p.z * m[2].w + m[3].w;
    if (!(cw > 1e-6f)) return false;
    x = cx / cw;
    y = cy / cw;
    return true;
}
// Ticket 0652: a 4 x 4 row-major matrix moved by `shift` world units in x (P'(x) = P(x + shift): row 3 += shift row 0),
// the EXE's wrap view rule (trees.md 11.1; the server's MoveX in srv_render_scene.cpp).
void movedRows(const Float4* m, double shift, Float4* out) {
    for (int k = 0; k < 3; ++k) out[k] = m[k];
    out[3] = Float4{static_cast<float>(shift * m[0].x + m[3].x), static_cast<float>(shift * m[0].y + m[3].y),
                    static_cast<float>(shift * m[0].z + m[3].z), static_cast<float>(shift * m[0].w + m[3].w)};
}
bool usablePeriod(float w) { return std::isfinite(w) && w > 0.0f; }
// The frame's rows for the copy of a constant whose tapped rows are `tapRows` (null: copy 0); k out (kWrapUnfit: rows
// as now, the caller draws nothing). Ticket 0659: a constant that fits no copy within a quarter period (a far camera
// jump) takes the nearest copy, jump = true.
void copyRows(const Float4* now, const Float4* tapRows, float wrapPeriod, Float4* out, int& k, bool& jump) {
    jump = false;
    k = tapRows ? wrapCopyOf(tapRows, now, wrapPeriod) : 0;
    if (k == kWrapUnfit) {
        k = nearestWrapCopyOf(tapRows, now, wrapPeriod);
        jump = k != kWrapUnfit;
    }
    if (k != 0 && k != kWrapUnfit)
        movedRows(now, static_cast<double>(k) * wrapPeriod, out);
    else
        for (int j = 0; j < 4; ++j) out[j] = now[j];
}
bool insideNdc(const Float4* m, const Float4& p) {
    float x, y;
    return projectNdc(m, p, x, y) && std::fabs(x) <= 1.0f && std::fabs(y) <= 1.0f;
}
// One record (a file's item or a CPU-built one): its instances' shift, then its VS cb1 replaced once.
void cameraOne(ModelScene& s, Material material, uint32_t vsCb1, uint32_t instanceCount, uint32_t instRow,
               const FrameCamera& cam, const Float4* now0, const TapCameraRows& tap, std::vector<bool>& done,
               CameraShift* shift) {
    if (vsCb1 == kNoConst || vsCb1 >= s.consts.size()) return;
    const TapConst& c = s.consts[vsCb1];
    if (c.rows < 4 || static_cast<size_t>(c.firstRow) + c.rows > s.rows.size()) return;
    const auto tr = tap.find(vsCb1);
    // ticket 0652: the wrap copy of the frame's camera this constant was tapped through (city-renderer.md 17)
    Float4 now[4];
    int wk = 0;
    bool jump = false;
    copyRows(now0, tr != tap.end() ? tr->second.data() : nullptr, cam.wrapPeriod, now, wk, jump);
    if (wk == kWrapUnfit) {
        if (done[vsCb1]) return;
        done[vsCb1] = true;
        Float4* r = &s.rows[c.firstRow];
        for (int k = 0; k < 4; ++k) r[k] = Float4{0, 0, 0, 0};   // clip w = 0 at every vertex: nothing drawn this frame
        if (shift) ++shift->unfitConsts;
        return;
    }
    if (jump && shift && material != Material::Skinned) {
        // ticket 0659: how many of a far jump's records the frame shows at the copy taken, and at view 0 (the measure of
        // the choice against drawing nothing, city-renderer.md 17.9); not part of the shift (a jump is not a pan's error)
        const uint32_t n = material == Material::Single ? 1u : instanceCount;
        for (uint32_t i = 0; i < n; ++i) {
            const size_t r3 = static_cast<size_t>(instRow) + static_cast<size_t>(i) * 4 + 3;
            if (r3 >= s.rows.size()) break;
            ++shift->farInstances;
            if (insideNdc(now, s.rows[r3])) ++shift->farOnScreen;
            if (insideNdc(now0, s.rows[r3])) ++shift->farOnScreenView0;
        }
    }
    if (!jump && shift && tr != tap.end() && material != Material::Skinned) {
        // material 3: its cb0 rows (one instance); 4 and 5: four rows per instance; the origin is row 3
        const uint32_t n = material == Material::Single ? 1u : instanceCount;
        for (uint32_t i = 0; i < n; ++i) {
            const size_t r3 = static_cast<size_t>(instRow) + static_cast<size_t>(i) * 4 + 3;
            if (r3 >= s.rows.size()) break;
            float x0, y0, x1, y1;
            if (!projectNdc(tr->second.data(), s.rows[r3], x0, y0) || !projectNdc(now, s.rows[r3], x1, y1)) continue;
            const double dx = 0.5 * cam.width * static_cast<double>(x1 - x0);
            const double dy = 0.5 * cam.height * static_cast<double>(y1 - y0);
            const double d = std::sqrt(dx * dx + dy * dy);
            ++shift->n;
            shift->sumPx += d;
            shift->maxPx = std::max(shift->maxPx, d);
        }
    }
    if (done[vsCb1]) return;
    done[vsCb1] = true;
    Float4* r = &s.rows[c.firstRow];
    for (int k = 0; k < 4; ++k) r[k] = now[k];
    if (cam.overlayProj && c.rows >= 13) {
        // ticket 0652: the OverlayProj of the same wrap copy (the EXE's view of that copy when known, else view 0's moved)
        Float4 ov[4];
        for (int k = 0; k < 4; ++k) ov[k] = rowOf(cam.overlayProj, k);
        if (wk != 0) {
            const float* given = (wk == -1 || wk == 1) ? cam.overlayProjWrap[wk < 0 ? 0 : 1] : nullptr;
            if (given)
                for (int k = 0; k < 4; ++k) ov[k] = rowOf(given, k);
            else {
                const Float4 base[4] = {ov[0], ov[1], ov[2], ov[3]};
                movedRows(base, static_cast<double>(wk) * cam.wrapPeriod, ov);
            }
        }
        for (int k = 0; k < 4; ++k) r[8 + k] = ov[k];
        if (cam.fowAnim) r[12] = Float4{cam.fowAnim[0], cam.fowAnim[1], cam.fowAnim[2], cam.fowAnim[3]};
    }
    if (shift) {
        ++shift->consts;
        if (jump)
            ++shift->farConsts;
        else if (wk == -1 || wk == 1)
            ++shift->wrapConsts;
    }
}
// Ticket 0598: the tap's rows 0-3 are the frame's camera (to float rounding: every element within 1e-6 of the matrix's
// largest element; a pan step moves the translation row by about 1e-3 of it).
bool sameCamera(const std::array<Float4, 5>& tap, const Float4* now) {
    float big = 0.0f, diff = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const float a[4] = {tap[k].x, tap[k].y, tap[k].z, tap[k].w};
        const float b[4] = {now[k].x, now[k].y, now[k].z, now[k].w};
        for (int j = 0; j < 4; ++j) {
            big = std::max(big, std::fabs(a[j]));
            diff = std::max(diff, std::fabs(a[j] - b[j]));
        }
    }
    return big > 0.0f && diff <= 1e-6f * big;
}
// Ticket 0598: row 16 of a PS cb1 constant (once per constant): the tap's own row 16 (the EXE's exact eye) while the
// constant's tapped camera is the frame's, else the frame's eye (x, y, z, 1).
// Ticket 0652: through wrap copy k the eye is the frame's eye moved by -k W in x (the EXE's per-view camera position,
// trees.md 11.1 row 12 [C]; that the BuildingShader's row 16 follows it is [I]); a constant that fits no copy draws
// nothing this frame (cameraOne), so its row 16 is left as it is.
void eyeOne(ModelScene& s, uint32_t psCb1, const float* eye, const Float4* now0, float wrapPeriod,
            const TapCameraRows& tap, std::vector<bool>& done, CameraShift* shift) {
    if (psCb1 == kNoConst || psCb1 >= s.consts.size() || done[psCb1]) return;
    const TapConst& c = s.consts[psCb1];
    if (c.rows < 17 || static_cast<size_t>(c.firstRow) + 17 > s.rows.size()) return;
    done[psCb1] = true;
    const auto tr = tap.find(psCb1);
    Float4 now[4];
    int wk = 0;
    bool jump = false;
    copyRows(now0, tr != tap.end() ? tr->second.data() : nullptr, wrapPeriod, now, wk, jump);
    if (wk == kWrapUnfit) return;
    if (tr != tap.end() && tr->second[4].w != 0.0f && sameCamera(tr->second, now)) {
        s.rows[c.firstRow + 16] = tr->second[4];
        if (shift) ++shift->eyeKept;
        return;
    }
    // ticket 0659: a far jump's constant at its nearest copy k (up to +-2) takes the eye of that copy too
    const float ex = wk != 0 ? static_cast<float>(static_cast<double>(eye[0]) - wk * static_cast<double>(wrapPeriod)) : eye[0];
    s.rows[c.firstRow + 16] = Float4{ex, eye[1], eye[2], 1.0f};
    if (shift) ++shift->eyeConsts;
}
}  // namespace

void keepTapCamera(const ModelScene& s, TapCameraRows& out) {
    out.clear();
    for (const DrawItem& it : s.items) keepOne(s, it.vsCb1, out);
    for (const CpuDrawItem& it : s.cpuItems) keepOne(s, it.vsCb1, out);
}

void applyFrameCamera(ModelScene& s, uint32_t target, const FrameCamera& cam, const TapCameraRows& tap, CameraShift* shift) {
    if (!cam.viewProj) return;
    Float4 now[4];
    for (int k = 0; k < 4; ++k) now[k] = rowOf(cam.viewProj, k);
    std::vector<bool> done(s.consts.size(), false);
    for (const DrawItem& it : s.items)
        if (it.target == target) cameraOne(s, it.material, it.vsCb1, it.instanceCount, it.instRow, cam, now, tap, done, shift);
    for (const CpuDrawItem& it : s.cpuItems)
        if (it.target == target) cameraOne(s, it.material, it.vsCb1, it.instanceCount, it.instRow, cam, now, tap, done, shift);
    if (!cam.eye) return;
    std::vector<bool> eyeDone(s.consts.size(), false);
    for (const DrawItem& it : s.items)
        if (it.target == target) eyeOne(s, it.psCb1, cam.eye, now, cam.wrapPeriod, tap, eyeDone, shift);
    for (const CpuDrawItem& it : s.cpuItems)
        if (it.target == target) eyeOne(s, it.psCb1, cam.eye, now, cam.wrapPeriod, tap, eyeDone, shift);
}

namespace {
// The camera step d (world units) between the tap's rows and the frame's (city-renderer.md 17.2); false: no period or no
// usable row 0 (the caller takes copy 0).
bool tapStep(const Float4* tap, const Float4* now, float wrapPeriod, double& d) {
    if (!tap || !now || !usablePeriod(wrapPeriod)) return false;
    const float r0[4] = {now[0].x, now[0].y, now[0].z, now[0].w};
    const float t3[4] = {tap[3].x, tap[3].y, tap[3].z, tap[3].w};
    const float n3[4] = {now[3].x, now[3].y, now[3].z, now[3].w};
    int j = 0;
    for (int i = 1; i < 4; ++i)
        if (std::fabs(r0[i]) > std::fabs(r0[j])) j = i;
    if (!(std::fabs(r0[j]) > 0.0f) || !std::isfinite(r0[j]) || !std::isfinite(t3[j]) || !std::isfinite(n3[j])) return false;
    d = (static_cast<double>(t3[j]) - static_cast<double>(n3[j])) / static_cast<double>(r0[j]);
    return std::isfinite(d);
}
}  // namespace

int wrapCopyOf(const Float4* tap, const Float4* now, float wrapPeriod) {
    double d = 0.0;
    if (!tapStep(tap, now, wrapPeriod, d)) return 0;
    const double w = static_cast<double>(wrapPeriod);
    const double kk = std::floor(d / w + 0.5);
    if (kk < -1.0 || kk > 1.0 || std::fabs(d - kk * w) > 0.25 * w) return kWrapUnfit;
    return static_cast<int>(kk);
}

int nearestWrapCopyOf(const Float4* tap, const Float4* now, float wrapPeriod) {
    double d = 0.0;
    if (!tapStep(tap, now, wrapPeriod, d)) return 0;
    const double kk = std::floor(d / static_cast<double>(wrapPeriod) + 0.5);
    if (kk < -2.0 || kk > 2.0) return kWrapUnfit;
    return static_cast<int>(kk);
}

}}  // namespace c5r::landmarks
