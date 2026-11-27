// programs.cpp - the EXE's unit programs, read from CivilizationV_DX11.exe in the player's install by digest, and the
// input layouts of the two vertex streams (epic 0041, issue 4352; unit-playback.md 6 K9, unit-draws.md 1-2).
// Only SHA-256 digests are in this file (D83): the bytes are read at run time.
#include "c5r/units/draw.h"

#include <windows.h>
#include <bcrypt.h>

#include "c5r/shaders/shadersection.h"

#pragma comment(lib, "bcrypt.lib")

namespace c5r { namespace units {

std::string sha256Hex(const uint8_t* data, size_t size) {
    uint8_t out[32] = {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return hex;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        size_t done = 0;
        while (done < size) {
            ULONG n = static_cast<ULONG>(size - done > 0x40000000u ? 0x40000000u : size - done);
            BCryptHashData(h, const_cast<PUCHAR>(data + done), n, 0);
            done += n;
        }
        if (BCryptFinishHash(h, out, 32, 0) == 0) {
            static const char* d = "0123456789abcdef";
            for (uint8_t b : out) {
                hex.push_back(d[b >> 4]);
                hex.push_back(d[b & 15]);
            }
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

const std::vector<ProgramRef>& unitProgramTable() {
    static const std::vector<ProgramRef> t = {
        {Material::Skinned, PassKind::Scene, true, "29a0b0611c827087296b1e8e048d9d056f89f6b821f03348110f78eb09132beb", 0x6803140},
        {Material::Skinned, PassKind::Scene, false, "60d270e1f4b737da53713db9c88b62f9f89d9e9c8a5027655e6e35b5efbcb45a", 0x6813630},
        {Material::Skinned, PassKind::ShadowMask, true, "8cb52b96d5e9d375bfdf95087be73f14157de2c520e9c80728dc173c37f2315b", 0x6804E48},
        {Material::Skinned, PassKind::ShadowMask, false, "72c27aeac4de2f8aa87f24a3e5935040d958c5ab1eb9c75b2d1e789baa9f7851", 0x6815328},
        {Material::SkinnedAlpha, PassKind::Scene, true, "29a0b0611c827087296b1e8e048d9d056f89f6b821f03348110f78eb09132beb", 0x6803140},
        {Material::SkinnedAlpha, PassKind::Scene, false, "5def4f99da5c78346fd8e48f1ce6334dd37a2a1afb1d960fa9121e3ab7c1b26a", 0x6811108},
        {Material::SkinnedAlpha, PassKind::ShadowMask, true, "8cb52b96d5e9d375bfdf95087be73f14157de2c520e9c80728dc173c37f2315b", 0x6804E48},
        {Material::SkinnedAlpha, PassKind::ShadowMask, false, "246527ed405ac6a853117af7612d170ea6e18834291742d3f179f6b593ab348f", 0x6812DA0},
        {Material::Static, PassKind::Scene, true, "82e59838888a178276508e98a22a46aab25783ce0fd9a8fc9caf0b72bbb97e58", 0x68005D0},
        {Material::Static, PassKind::Scene, false, "0e7a3566a56cb9fadc50dd247d9a672dedb0890c198e9c15ff0d0d2a8c2f20f4", 0x680CC38},
        {Material::Static, PassKind::ShadowMask, true, "59af33ffe6ff1a155b666c34dde3e7bf986166b7d43d29aee07df1979b1a8dd1", 0x6802938},
        {Material::Static, PassKind::ShadowMask, false, "9a5991d74d8be9470b2d6a4217f5c0adeedffb0250e45073ed0d132986440b1e", 0x680E7F0},
    };
    return t;
}

int UnitPrograms::key(Material m, PassKind p, bool vertex) {
    const int pass = p == PassKind::ShadowMask ? 1 : 0;   // reflection and scene share pass 0's programs
    return static_cast<int>(m) * 4 + pass * 2 + (vertex ? 1 : 0);
}

Status UnitPrograms::openFromBlobs(const std::vector<std::vector<uint8_t>>& blobs) {
    release();
    bytes_.clear();
    sha_.clear();
    std::map<std::string, const std::vector<uint8_t>*> byHash;
    for (auto& b : blobs) byHash[sha256Hex(b.data(), b.size())] = &b;
    std::string missing;
    for (auto& r : unitProgramTable()) {
        auto it = byHash.find(r.sha256);
        if (it == byHash.end()) {
            char va[16];
            std::snprintf(va, sizeof va, "0x%X", r.va);
            missing += std::string(missing.empty() ? "" : "; ") + r.sha256 + " (material " +
                       std::to_string(static_cast<int>(r.material)) + ", " + (r.vertex ? "VS" : "PS") +
                       ", build 4390913 VA " + va + ")";
            continue;
        }
        const int k = key(r.material, r.pass, r.vertex);
        bytes_[k] = *it->second;
        sha_[k] = r.sha256;
    }
    if (!missing.empty())
        return Status::fail(Err::NotFound, "unit programs not in the EXE's .shader section: " + missing);
    return Status::Ok();
}

Status UnitPrograms::open(const std::wstring& exePath) {
    release();
    bytes_.clear();
    sha_.clear();
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(sec.blobs().size());
    for (auto& b : sec.blobs()) blobs.emplace_back(b.bytes, b.bytes + b.size);
    return openFromBlobs(blobs);
}

const std::vector<uint8_t>* UnitPrograms::bytes(Material m, PassKind p, bool vertex) const {
    auto it = bytes_.find(key(m, p, vertex));
    return it == bytes_.end() ? nullptr : &it->second;
}

std::string UnitPrograms::sha256(Material m, PassKind p, bool vertex) const {
    auto it = sha_.find(key(m, p, vertex));
    return it == sha_.end() ? std::string() : it->second;
}

void UnitPrograms::release() {
    for (auto& kv : objects_)
        if (kv.second) kv.second->Release();
    for (auto& kv : layouts_)
        if (kv.second) kv.second->Release();
    objects_.clear();
    layouts_.clear();
}

Status UnitPrograms::create(ID3D11Device* dev) {
    if (!dev) return Status::fail(Err::Device, "no device");
    release();   // with no program read nothing is created (every unit is ProgramNotFound)
    static const D3D11_INPUT_ELEMENT_DESC skinned[] = {
        {"POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 1, DXGI_FORMAT_R32_UINT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    static const D3D11_INPUT_ELEMENT_DESC stat[] = {
        {"POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    for (Material m : {Material::Skinned, Material::SkinnedAlpha, Material::Static})
        for (PassKind p : {PassKind::Scene, PassKind::ShadowMask}) {
            const int kv = key(m, p, true), kp = key(m, p, false);
            if (!bytes_.count(kv) || !bytes_.count(kp)) continue;   // that material's units are ProgramNotFound
            const auto& vsb = bytes_[kv];
            const auto& psb = bytes_[kp];
            ID3D11VertexShader* vs = nullptr;
            ID3D11PixelShader* ps = nullptr;
            HRESULT hr = dev->CreateVertexShader(vsb.data(), vsb.size(), nullptr, &vs);
            if (FAILED(hr)) return Status::fail(Err::Device, "CreateVertexShader " + sha_[kv] + ": " + std::to_string(hr));
            objects_[kv] = vs;
            hr = dev->CreatePixelShader(psb.data(), psb.size(), nullptr, &ps);
            if (FAILED(hr)) return Status::fail(Err::Device, "CreatePixelShader " + sha_[kp] + ": " + std::to_string(hr));
            objects_[kp] = ps;
            ID3D11InputLayout* il = nullptr;
            if (m == Material::Static)
                hr = dev->CreateInputLayout(stat, 3, vsb.data(), vsb.size(), &il);
            else
                hr = dev->CreateInputLayout(skinned, 6, vsb.data(), vsb.size(), &il);
            if (FAILED(hr)) return Status::fail(Err::Device, "CreateInputLayout for " + sha_[kv] + ": " + std::to_string(hr));
            layouts_[kv] = il;
        }
    return Status::Ok();
}

ID3D11VertexShader* UnitPrograms::vs(Material m, PassKind p) const {
    auto it = objects_.find(key(m, p, true));
    return it == objects_.end() ? nullptr : static_cast<ID3D11VertexShader*>(it->second);
}
ID3D11PixelShader* UnitPrograms::ps(Material m, PassKind p) const {
    auto it = objects_.find(key(m, p, false));
    return it == objects_.end() ? nullptr : static_cast<ID3D11PixelShader*>(it->second);
}
ID3D11InputLayout* UnitPrograms::layout(Material m, PassKind p) const {
    auto it = layouts_.find(key(m, p, true));
    return it == layouts_.end() ? nullptr : it->second;
}

}}  // namespace c5r::units
