// render/tools/watershot/watershot_core.cpp - see watershot_core.h. Own code (team-3600 W4, issue 3604).
#include "watershot_core.h"

#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cfloat>
#include <initializer_list>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace c5r { namespace watershot {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const std::string& what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(Err::Device, what + b);
}

std::wstring widen(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], static_cast<int>(w.size()));
    w.resize(n > 0 ? static_cast<size_t>(n - 1) : 0);
    return w;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

Status readFile(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return Status::fail(Err::Io, "cannot open " + narrow(path));
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return Status::Ok();
}

// A relative path inside the export (no "..", no drive, no leading slash).
Status inside(const std::wstring& root, const std::string& rel, std::wstring& out) {
    if (rel.empty() || rel.find("..") != std::string::npos || rel.find(':') != std::string::npos || rel[0] == '/' ||
        rel[0] == '\\')
        return Status::fail(Err::BadLayout, "path outside the export: " + rel);
    out = root + L"/" + widen(rel);
    return Status::Ok();
}

bool num(const Json* j, const char* key, double& v) {
    const Json* x = j ? j->get(key) : nullptr;
    if (!x) return false;
    if (x->kind == Json::Number) {
        v = x->num;
        return true;
    }
    if (x->kind == Json::Bool) {
        v = x->b ? 1 : 0;
        return true;
    }
    if (x->kind == Json::Object) {  // the exporter's enum form {"v": number, "s": name}
        const Json* n = x->get("v");
        if (n && n->kind == Json::Number) {
            v = n->num;
            return true;
        }
    }
    return false;
}
// The file of an entry: entry.file or entry.data.file (3601's exporter nests the contents under "data").
const Json* fileOf(const Json* e) {
    if (!e) return nullptr;
    const Json* f = e->get("file");
    if (f && f->kind == Json::String) return f;
    const Json* d = e->get("data");
    f = d && d->kind == Json::Object ? d->get("file") : nullptr;
    return f && f->kind == Json::String ? f : nullptr;
}
// A state object's descriptor: entry.desc when present, else the entry itself (flat form).
const Json* descOf(const Json* e) {
    if (!e || e->kind != Json::Object) return nullptr;
    const Json* d = e->get("desc");
    if (d) return d->kind == Json::Object ? d : nullptr;
    return e;
}
uint32_t u32(const Json* j, const char* key, uint32_t dflt) {
    double v;
    return num(j, key, v) ? static_cast<uint32_t>(static_cast<int64_t>(v)) : dflt;
}
float f32(const Json* j, const char* key, float dflt) {
    double v;
    return num(j, key, v) ? static_cast<float>(v) : dflt;
}
// The first of several keys that is present.
const Json* any(const Json* j, std::initializer_list<const char*> keys) {
    if (!j) return nullptr;
    for (const char* k : keys)
        if (const Json* x = j->get(k)) return x;
    return nullptr;
}
// Slot-indexed entries: an object {"0": {...}} or an array [{...}, null, ...].
std::vector<std::pair<uint32_t, const Json*>> slots(const Json* j) {
    std::vector<std::pair<uint32_t, const Json*>> r;
    if (!j) return r;
    if (j->kind == Json::Array) {
        for (size_t i = 0; i < j->arr.size(); ++i)
            if (j->arr[i].kind == Json::Object) r.push_back({static_cast<uint32_t>(i), &j->arr[i]});
    } else if (j->kind == Json::Object) {
        for (const auto& kv : j->obj) {
            char* end = nullptr;
            const unsigned long s = std::strtoul(kv.first.c_str(), &end, 10);
            if (end && *end == 0 && kv.second.kind == Json::Object) r.push_back({static_cast<uint32_t>(s), &kv.second});
        }
    }
    return r;
}

DXGI_FORMAT typelessOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default: return f;
    }
}

bool rgba8(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM ||
           f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_TYPELESS || f == DXGI_FORMAT_B8G8R8A8_TYPELESS;
}

D3D11_SAMPLER_DESC samplerFrom(const Json* j) {
    D3D11_SAMPLER_DESC d = {};
    d.Filter = static_cast<D3D11_FILTER>(u32(j, "Filter", D3D11_FILTER_MIN_MAG_MIP_LINEAR));
    d.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(u32(j, "AddressU", D3D11_TEXTURE_ADDRESS_CLAMP));
    d.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(u32(j, "AddressV", D3D11_TEXTURE_ADDRESS_CLAMP));
    d.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(u32(j, "AddressW", D3D11_TEXTURE_ADDRESS_CLAMP));
    d.MipLODBias = f32(j, "MipLODBias", 0);
    d.MaxAnisotropy = std::max(1u, u32(j, "MaxAnisotropy", 1));
    d.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(u32(j, "ComparisonFunc", D3D11_COMPARISON_NEVER));
    const Json* bc = j ? j->get("BorderColor") : nullptr;
    for (int i = 0; i < 4; ++i)
        d.BorderColor[i] = bc && bc->kind == Json::Array && bc->arr.size() == 4 ? static_cast<float>(bc->arr[i].num) : 1.0f;
    d.MinLOD = f32(j, "MinLOD", -FLT_MAX);
    d.MaxLOD = f32(j, "MaxLOD", FLT_MAX);
    return d;
}

Status loadStage(const std::wstring& root, const Json* j, Stage& st, std::vector<std::string>& notes) {
    st = Stage();
    if (!j || j->kind != Json::Object) return Status::fail(Err::NotFound, "stage missing");
    const Json* file = any(j, {"file", "bytecode"});
    if (!file || file->kind != Json::String) file = fileOf(j->get("shader"));
    if (!file || file->kind != Json::String) return Status::fail(Err::NotFound, "stage without a bytecode file");
    std::wstring p;
    C5R_TRY(inside(root, file->str, p));
    C5R_TRY(readFile(p, st.code));
    for (const auto& s : slots(any(j, {"cb", "cbuffers"}))) {
        Stage::Cb cb;
        cb.slot = s.first;
        const Json* f = fileOf(s.second);
        if (!f) return Status::fail(Err::NotFound, "cb " + std::to_string(s.first) + " without a file");
        C5R_TRY(inside(root, f->str, p));
        C5R_TRY(readFile(p, cb.data));
        const Json* dd = s.second->get("data");
        const uint32_t size = u32(s.second, "size", u32(dd, "size", static_cast<uint32_t>(cb.data.size())));
        cb.data.resize(size, 0);
        st.cbs.push_back(std::move(cb));
    }
    for (const auto& s : slots(any(j, {"srv", "srvs"}))) {
        Stage::Srv v;
        v.slot = s.first;
        v.viewFormat = static_cast<DXGI_FORMAT>(u32(s.second, "format", u32(s.second->get("view_desc"), "Format", 0)));
        const Json* f = fileOf(s.second);
        if (!f) {
            notes.push_back("srv " + std::to_string(s.first) + " has no file: bound as null");
            continue;
        }
        C5R_TRY(inside(root, f->str, p));
        C5R_TRY(loadTexture(p, v.tex));
        if (v.tex.writtenInFrame) notes.push_back("t" + std::to_string(s.first) + " is rendered in the frame: its file holds the frame-start contents");
        st.srvs.push_back(std::move(v));
    }
    for (const auto& s : slots(any(j, {"samp", "samplers"}))) {
        const Json* d = descOf(s.second);
        if (!d && u32(s.second, "id", 0) != 0) notes.push_back("s" + std::to_string(s.first) + ": sampler without a descriptor in the export: D3D11 default used");
        st.samps.push_back(Stage::Samp{s.first, samplerFrom(d)});
    }
    return Status::Ok();
}

} // namespace

uint32_t formatBytes(DXGI_FORMAT f, bool* bc) {
    if (bc) *bc = false;
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT: return 8;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R32_TYPELESS:
        return 4;
    case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R8G8_UNORM: return 2;
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM: return 1;
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC4_UNORM:
        if (bc) *bc = true;
        return 8;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC5_UNORM:
        if (bc) *bc = true;
        return 16;
    default: return 0;
    }
}

Status loadTexture(const std::wstring& binPath, TexFile& out) {
    out = TexFile();
    // <name>.bin.json (3601's exporter) or <name>.json
    std::wstring jp = binPath + L".json";
    if (GetFileAttributesW(jp.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const size_t dot = binPath.rfind(L".bin");
        jp = (dot != std::wstring::npos && dot + 4 == binPath.size() ? binPath.substr(0, dot) : binPath) + L".json";
    }
    Json j;
    C5R_TRY(terrainshot::readJsonFile(jp, j));
    // flat keys, or the D3D11 description under "desc" (the exporter's form)
    const Json* dsc = j.get("desc");
    if (dsc && dsc->kind != Json::Object) dsc = nullptr;
    double f = 0, w = 0, h = 0;
    if (!(num(&j, "format", f) || num(dsc, "Format", f)) || !(num(&j, "width", w) || num(dsc, "Width", w)) ||
        !(num(&j, "height", h) || num(dsc, "Height", h)))
        return Status::fail(Err::NotFound, "texture json: format, width or height missing");
    out.format = static_cast<DXGI_FORMAT>(static_cast<int>(f));
    out.width = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    out.depth = std::max(1u, u32(&j, "depth", u32(dsc, "Depth", 1)));
    out.mips = std::max(1u, u32(&j, "mips", u32(dsc, "MipLevels", 1)));
    out.arraySize = std::max(1u, u32(&j, "array_size", u32(&j, "array", u32(dsc, "ArraySize", 1))));
    const Json* dim = j.get("dim");
    const Json* kind = j.get("kind");
    out.volume = out.depth > 1 || (dim && ((dim->kind == Json::String && (dim->str == "3d" || dim->str == "Texture3D")) ||
                                           (dim->kind == Json::Number && dim->num == 3))) ||
                 (kind && kind->kind == Json::String && kind->str == "texture3d");
    const Json* wif = j.get("written_in_frame");
    out.writtenInFrame = wif && wif->kind == Json::Bool && wif->b;
    if (out.width == 0 || out.height == 0 || out.width > 16384 || out.height > 16384 || out.depth > 2048 || out.mips > 15 ||
        out.arraySize > 2048)
        return Status::fail(Err::Unsupported, "texture size 0 or above the D3D11 limits");
    bool bc = false;
    const uint32_t bpp = formatBytes(out.format, &bc);
    if (!bpp) return Status::fail(Err::Unsupported, "texture format " + std::to_string(static_cast<int>(out.format)) + " not handled");
    C5R_TRY(readFile(binPath, out.bytes));
    // subresources: given, or packed tightly in D3D11 order
    const Json* subs = j.get("subresources");
    if (subs && subs->kind == Json::Array && !subs->arr.empty()) {
        for (const Json& s : subs->arr) {
            TexFile::Sub x;
            x.offset = static_cast<size_t>(u32(&s, "offset", 0));
            x.rowPitch = u32(&s, "row_pitch", 0);
            x.slicePitch = u32(&s, "slice_pitch", 0);
            out.subs.push_back(x);
        }
    }
    size_t off = 0;
    std::vector<TexFile::Sub> packed;
    for (uint32_t a = 0; a < out.arraySize; ++a)
        for (uint32_t m = 0; m < out.mips; ++m) {
            const uint32_t mw = std::max(1u, out.width >> m), mh = std::max(1u, out.height >> m), md = std::max(1u, out.depth >> m);
            const uint32_t rows = bc ? (mh + 3) / 4 : mh;
            const uint32_t pitch = bc ? ((mw + 3) / 4) * bpp : mw * bpp;
            TexFile::Sub x;
            x.offset = off;
            x.rowPitch = pitch;
            x.slicePitch = pitch * rows;
            packed.push_back(x);
            off += static_cast<size_t>(x.slicePitch) * md;
        }
    const Json* sb = j.get("subresource_bytes");
    if (out.subs.empty() && sb && sb->kind == Json::Array && sb->arr.size() == packed.size()) {
        size_t o = 0;
        for (size_t i = 0; i < packed.size(); ++i) {
            TexFile::Sub x = packed[i];
            x.offset = o;
            o += static_cast<size_t>(sb->arr[i].num);
            out.subs.push_back(x);
        }
    }
    if (out.subs.empty()) {
        const uint32_t rp = u32(&j, "row_pitch", 0);
        if (rp && out.mips == 1 && out.arraySize == 1) {
            packed[0].rowPitch = rp;
            packed[0].slicePitch = rp * (bc ? (out.height + 3) / 4 : out.height);
        }
        out.subs = packed;
    }
    if (out.subs.size() != packed.size())
        return Status::fail(Err::BadLayout, "texture json: " + std::to_string(out.subs.size()) + " subresources, the size says " +
                                                std::to_string(packed.size()));
    for (size_t i = 0; i < out.subs.size(); ++i) {
        TexFile::Sub& s = out.subs[i];
        if (!s.rowPitch) s.rowPitch = packed[i].rowPitch;
        if (!s.slicePitch) s.slicePitch = packed[i].slicePitch;
        if (s.rowPitch < packed[i].rowPitch) return Status::fail(Err::BadLayout, "texture json: row pitch below the width");
        const uint32_t m = static_cast<uint32_t>(i % out.mips);
        const uint32_t md = std::max(1u, out.depth >> m);
        if (s.offset + static_cast<size_t>(s.slicePitch) * md > out.bytes.size())
            return Status::fail(Err::Truncated, "texture .bin shorter than its subresources (" + std::to_string(out.bytes.size()) + " bytes)");
    }
    return Status::Ok();
}

Status loadIndex(const std::wstring& exportDir, ExportIndex& out) {
    out = ExportIndex();
    Json j;
    {
        // An export whose index is not written yet (the exporter writes draws.json last): the draws/ folders,
        // "<n>_<system>", in name order.
        const DWORD a = GetFileAttributesW((exportDir + L"/draws.json").c_str());
        const DWORD d = GetFileAttributesW((exportDir + L"/draws").c_str());
        if (a == INVALID_FILE_ATTRIBUTES && d != INVALID_FILE_ATTRIBUTES && (d & FILE_ATTRIBUTE_DIRECTORY)) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((exportDir + L"/draws/*").c_str(), &fd);
            std::vector<std::wstring> names;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') names.push_back(fd.cFileName);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            std::sort(names.begin(), names.end());
            for (const std::wstring& n : names) {
                const size_t us = n.find(L'_');
                if (us == std::wstring::npos) continue;
                DrawEntry e;
                e.n = static_cast<uint32_t>(_wtoi(n.substr(0, us).c_str()));
                e.system = narrow(n.substr(us + 1));
                e.dir = "draws/" + narrow(n);
                out.draws.push_back(e);
            }
            out.capture = "(no draws.json: index from the draws/ folders)";
            return Status::Ok();
        }
    }
    C5R_TRY(terrainshot::readJsonFile(exportDir + L"/draws.json", j));
    double v = 0;
    if (!num(&j, "version", v)) return Status::fail(Err::NotFound, "draws.json: no version");
    if (v != 1) return Status::fail(Err::BadVersion, "draws.json: version " + std::to_string(v) + ", this reader knows 1");
    if (const Json* c = j.get("capture"))
        if (c->kind == Json::String) out.capture = c->str;
    if (const Json* r = j.get("replay")) out.replay = r->kind == Json::Bool && r->b;
    const Json* draws = j.get("draws");
    if (!draws || draws->kind != Json::Array) return Status::fail(Err::NotFound, "draws.json: no draws");
    for (const Json& d : draws->arr) {
        DrawEntry e;
        double n = 0;
        const Json* sys = d.get("system");
        const Json* dir = d.get("dir");
        if (!num(&d, "n", n) || !sys || sys->kind != Json::String || !dir || dir->kind != Json::String)
            return Status::fail(Err::NotFound, "draws.json: an entry without n, system or dir");
        std::wstring tmp;
        C5R_TRY(inside(exportDir, dir->str, tmp));
        e.n = static_cast<uint32_t>(n);
        e.system = sys->str;
        e.dir = dir->str;
        out.draws.push_back(e);
    }
    return Status::Ok();
}

Status compareRgba8(const uint8_t* a, uint32_t pa, const uint8_t* b, uint32_t pb, uint32_t w, uint32_t h, Compare& out) {
    out = Compare();
    if (!a || !b || w == 0 || h == 0 || pa < w * 4 || pb < w * 4) return Status::fail(Err::BadLayout, "compare: size 0 or pitch below the width");
    std::vector<int> diffs;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = a + static_cast<size_t>(y) * pa + x * 4;
            const uint8_t* q = b + static_cast<size_t>(y) * pb + x * 4;
            int d = 0;
            for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(static_cast<int>(p[c]) - static_cast<int>(q[c])));
            ++out.total;
            if (!d) {
                ++out.equal;
                continue;
            }
            const int k = d <= 1 ? 0 : d <= 2 ? 1 : d <= 4 ? 2 : d <= 8 ? 3 : d <= 16 ? 4 : d <= 32 ? 5 : d <= 64 ? 6 : 7;
            ++out.hist[k];
            diffs.push_back(d);
        }
    if (!diffs.empty()) {
        std::sort(diffs.begin(), diffs.end());
        out.maxDiff = diffs.back();
        out.p50 = diffs[diffs.size() / 2];
        out.p95 = diffs[std::min(diffs.size() - 1, diffs.size() * 95 / 100)];
    }
    return Status::Ok();
}

std::string describe(const Compare& c) {
    char b[320];
    const double share = c.total ? 100.0 * static_cast<double>(c.equal) / static_cast<double>(c.total) : 0;
    std::snprintf(b, sizeof b,
                  "equal %llu / %llu (%.4f %%); differing by 1: %llu, 2: %llu, 3-4: %llu, 5-8: %llu, 9-16: %llu, 17-32: %llu, "
                  "33-64: %llu, 65+: %llu; median %d, p95 %d, max %d",
                  static_cast<unsigned long long>(c.equal), static_cast<unsigned long long>(c.total), share,
                  static_cast<unsigned long long>(c.hist[0]), static_cast<unsigned long long>(c.hist[1]),
                  static_cast<unsigned long long>(c.hist[2]), static_cast<unsigned long long>(c.hist[3]),
                  static_cast<unsigned long long>(c.hist[4]), static_cast<unsigned long long>(c.hist[5]),
                  static_cast<unsigned long long>(c.hist[6]), static_cast<unsigned long long>(c.hist[7]), c.p50, c.p95, c.maxDiff);
    return b;
}

Status loadDrawState(const std::wstring& exportDir, const DrawEntry& d, CapturedDraw& out) {
    out = CapturedDraw();
    std::wstring dir;
    C5R_TRY(inside(exportDir, d.dir, dir));
    Json j;
    C5R_TRY(terrainshot::readJsonFile(dir + L"/state.json", j));
    const Json* call = j.get("call");
    if (!call || call->kind != Json::String) return Status::fail(Err::NotFound, "state.json: no call");
    out.call = call->str;
    const Json* args = j.get("args");
    if (!args) return Status::fail(Err::NotFound, "state.json: no args");
    out.indexCount = u32(args, "IndexCountPerInstance", u32(args, "IndexCount", 0));
    out.startIndex = u32(args, "StartIndexLocation", 0);
    out.baseVertex = static_cast<int32_t>(u32(args, "BaseVertexLocation", 0));
    out.vertexCount = u32(args, "VertexCountPerInstance", u32(args, "VertexCount", 0));
    out.startVertex = u32(args, "StartVertexLocation", 0);
    out.instances = std::max(1u, u32(args, "InstanceCount", 1));
    const bool indexed = out.call.find("Indexed") != std::string::npos;
    if (out.instances != 1) return Status::fail(Err::Unsupported, "state.json: instanced draw");
    // input assembler
    const Json* ia = j.get("ia");
    if (!ia) return Status::fail(Err::NotFound, "state.json: no ia");
    const Json* topo = ia->get("topology");
    if (topo && topo->kind == Json::Number) out.topology = static_cast<uint32_t>(topo->num);
    else if (topo && topo->kind == Json::Object) out.topology = u32(topo, "number", u32(topo, "v", 0));
    if (!out.topology) return Status::fail(Err::NotFound, "state.json: no topology");
    const Json* els = any(ia, {"elements", "layout"});
    if (els && els->kind == Json::Object) els = any(els, {"elements", "desc"});
    if (els && els->kind == Json::Array) {
        out.semanticNames.reserve(els->arr.size());
        for (const Json& e : els->arr) {
            const Json* sn = e.get("SemanticName");
            out.semanticNames.push_back(sn && sn->kind == Json::String ? sn->str : "");
        }
        for (size_t i = 0; i < els->arr.size(); ++i) {
            const Json& e = els->arr[i];
            D3D11_INPUT_ELEMENT_DESC x = {};
            x.SemanticName = out.semanticNames[i].c_str();
            x.SemanticIndex = u32(&e, "SemanticIndex", 0);
            x.Format = static_cast<DXGI_FORMAT>(u32(&e, "Format", 0));
            x.InputSlot = u32(&e, "InputSlot", 0);
            x.AlignedByteOffset = u32(&e, "AlignedByteOffset", 0);
            x.InputSlotClass = static_cast<D3D11_INPUT_CLASSIFICATION>(u32(&e, "InputSlotClass", 0));
            x.InstanceDataStepRate = u32(&e, "InstanceDataStepRate", 0);
            out.elements.push_back(x);
        }
    }
    std::wstring p;
    for (const auto& s : slots(ia->get("vb"))) {
        CapturedDraw::Vb vb;
        vb.slot = s.first;
        vb.stride = u32(s.second, "stride", 0);
        vb.offset = u32(s.second, "offset", 0);
        const Json* f = fileOf(s.second);
        if (!f) return Status::fail(Err::NotFound, "vb " + std::to_string(s.first) + " without a file");
        C5R_TRY(inside(exportDir, f->str, p));
        C5R_TRY(readFile(p, vb.data));
        out.vbs.push_back(std::move(vb));
    }
    if (indexed) {
        const Json* ib = ia->get("ib");
        const Json* f = fileOf(ib);
        if (!f) return Status::fail(Err::NotFound, "indexed draw without an index-buffer file");
        out.ibFormat = static_cast<DXGI_FORMAT>(u32(ib, "format", DXGI_FORMAT_R16_UINT));
        out.ibOffset = u32(ib, "offset", 0);
        C5R_TRY(inside(exportDir, f->str, p));
        C5R_TRY(readFile(p, out.ib));
        const uint32_t isz = out.ibFormat == DXGI_FORMAT_R32_UINT ? 4 : 2;
        if (out.ibOffset + (static_cast<uint64_t>(out.startIndex) + out.indexCount) * isz > out.ib.size())
            return Status::fail(Err::Truncated, "index buffer shorter than the draw's indices");
    }
    // shaders
    for (const char* k : {"hs", "ds", "gs", "HS", "DS", "GS"}) {
        const Json* x = j.get(k);
        if (x && x->kind == Json::Object && (x->get("file") || x->get("bytecode") || fileOf(x->get("shader"))))
            return Status::fail(Err::Unsupported, std::string("state.json: a ") + k + " shader is bound");
    }
    C5R_TRY(loadStage(exportDir, any(&j, {"vs", "VS"}), out.vs, out.notes));
    C5R_TRY(loadStage(exportDir, any(&j, {"ps", "PS"}), out.ps, out.notes));
    // output merger
    const Json* om = j.get("om");
    out.blend = D3D11_BLEND_DESC{};
    out.blend.RenderTarget[0].SrcBlend = out.blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    out.blend.RenderTarget[0].DestBlend = out.blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    out.blend.RenderTarget[0].BlendOp = out.blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    out.blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    const Json* blEntry = any(om, {"blend", "blend_state"});
    const Json* bl = descOf(blEntry);
    if (blEntry && !bl && u32(blEntry, "id", 0) != 0) out.notes.push_back("blend state without a descriptor in the export: D3D11 default used");
    if (blEntry) {
        const Json* bf0 = any(blEntry, {"factor", "BlendFactor"});
        if (bf0 && bf0->kind == Json::Array && bf0->arr.size() == 4)
            for (int i = 0; i < 4; ++i) out.blendFactor[i] = static_cast<float>(bf0->arr[i].num);
        out.sampleMask = u32(blEntry, "sample_mask", out.sampleMask);
    }
    if (bl) {
        out.blend.AlphaToCoverageEnable = u32(bl, "AlphaToCoverageEnable", 0) != 0;
        out.blend.IndependentBlendEnable = u32(bl, "IndependentBlendEnable", 0) != 0;
        const Json* rts = bl->get("RenderTarget");
        const Json* rt0 = rts && rts->kind == Json::Array && !rts->arr.empty() ? &rts->arr[0] : bl;
        D3D11_RENDER_TARGET_BLEND_DESC& r = out.blend.RenderTarget[0];
        r.BlendEnable = u32(rt0, "BlendEnable", 0) != 0;
        r.SrcBlend = static_cast<D3D11_BLEND>(u32(rt0, "SrcBlend", r.SrcBlend));
        r.DestBlend = static_cast<D3D11_BLEND>(u32(rt0, "DestBlend", r.DestBlend));
        r.BlendOp = static_cast<D3D11_BLEND_OP>(u32(rt0, "BlendOp", r.BlendOp));
        r.SrcBlendAlpha = static_cast<D3D11_BLEND>(u32(rt0, "SrcBlendAlpha", r.SrcBlendAlpha));
        r.DestBlendAlpha = static_cast<D3D11_BLEND>(u32(rt0, "DestBlendAlpha", r.DestBlendAlpha));
        r.BlendOpAlpha = static_cast<D3D11_BLEND_OP>(u32(rt0, "BlendOpAlpha", r.BlendOpAlpha));
        r.RenderTargetWriteMask = static_cast<UINT8>(u32(rt0, "RenderTargetWriteMask", r.RenderTargetWriteMask));
        const Json* bf = any(bl, {"BlendFactor", "blend_factor"});
        if (!bf && om) bf = any(om, {"BlendFactor", "blend_factor"});
        if (bf && bf->kind == Json::Array && bf->arr.size() == 4)
            for (int i = 0; i < 4; ++i) out.blendFactor[i] = static_cast<float>(bf->arr[i].num);
        out.sampleMask = u32(bl, "SampleMask", u32(om, "SampleMask", out.sampleMask));
    }
    out.depth = D3D11_DEPTH_STENCIL_DESC{};
    out.depth.DepthEnable = TRUE;
    out.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    out.depth.DepthFunc = D3D11_COMPARISON_LESS;
    const Json* dsEntry = any(om, {"depth_stencil", "depth", "depth_stencil_state"});
    const Json* ds = descOf(dsEntry);
    if (dsEntry) out.stencilRef = u32(dsEntry, "stencil_ref", u32(dsEntry, "StencilRef", 0));
    if (dsEntry && !ds && u32(dsEntry, "id", 0) != 0) out.notes.push_back("depth state without a descriptor in the export: D3D11 default used");
    if (ds) {
        out.depth.DepthEnable = u32(ds, "DepthEnable", 1) != 0;
        out.depth.DepthWriteMask = static_cast<D3D11_DEPTH_WRITE_MASK>(u32(ds, "DepthWriteMask", 1));
        out.depth.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(u32(ds, "DepthFunc", D3D11_COMPARISON_LESS));
        out.depth.StencilEnable = u32(ds, "StencilEnable", 0) != 0;
        out.stencilRef = u32(ds, "StencilRef", out.stencilRef);
    }
    const Json* dsv = om ? any(om, {"dsv", "depth_target"}) : j.get("dsv");
    out.hasDsv = dsv && dsv->kind == Json::Object;
    const Json* rtl = om ? any(om, {"rt", "render_targets", "rtv"}) : nullptr;
    if (!rtl || rtl->kind != Json::Array || rtl->arr.empty() || rtl->arr[0].kind != Json::Object)
        return Status::fail(Err::NotFound, "state.json: no render target 0");
    const Json* rt0 = &rtl->arr[0];
    out.rtFormat = static_cast<DXGI_FORMAT>(u32(rt0, "format", u32(rt0, "fmt", u32(rt0->get("view_desc"), "Format", 0))));
    out.rtWidth = u32(rt0, "w", u32(rt0, "width", u32(rt0->get("res_desc"), "Width", 0)));
    out.rtHeight = u32(rt0, "h", u32(rt0, "height", u32(rt0->get("res_desc"), "Height", 0)));
    {
        const Json* rdesc = rt0->get("res_desc");
        const Json* sd = rdesc && rdesc->kind == Json::Object ? rdesc->get("SampleDesc") : nullptr;
        out.rtSamples = std::max(1u, u32(rt0, "samples", u32(sd, "Count", 1)));
        const Json* vd = rt0->get("view_desc");
        if (out.rtSamples == 1 && u32(vd, "ViewDimension", 0) == D3D11_RTV_DIMENSION_TEXTURE2DMS)
            out.notes.push_back("the target is multisampled but its sample count is not in the export: replayed single-sampled");
    }
    // rasterizer
    const Json* rs = j.get("rs");
    out.raster = D3D11_RASTERIZER_DESC{};
    out.raster.FillMode = D3D11_FILL_SOLID;
    out.raster.CullMode = D3D11_CULL_BACK;
    out.raster.DepthClipEnable = TRUE;
    const Json* rdEntry = any(rs, {"rasterizer", "state"});
    const Json* rd = descOf(rdEntry);
    if (rdEntry && !rd && u32(rdEntry, "id", 0) != 0) out.notes.push_back("rasterizer state without a descriptor in the export: D3D11 default used");
    if (rd) {
        out.raster.FillMode = static_cast<D3D11_FILL_MODE>(u32(rd, "FillMode", D3D11_FILL_SOLID));
        out.raster.CullMode = static_cast<D3D11_CULL_MODE>(u32(rd, "CullMode", D3D11_CULL_BACK));
        out.raster.FrontCounterClockwise = u32(rd, "FrontCounterClockwise", 0) != 0;
        out.raster.DepthBias = static_cast<INT>(u32(rd, "DepthBias", 0));
        out.raster.DepthBiasClamp = f32(rd, "DepthBiasClamp", 0);
        out.raster.SlopeScaledDepthBias = f32(rd, "SlopeScaledDepthBias", 0);
        out.raster.DepthClipEnable = u32(rd, "DepthClipEnable", 1) != 0;
        out.raster.ScissorEnable = u32(rd, "ScissorEnable", 0) != 0;
        out.raster.MultisampleEnable = u32(rd, "MultisampleEnable", 0) != 0;
        out.raster.AntialiasedLineEnable = u32(rd, "AntialiasedLineEnable", 0) != 0;
    }
    if (out.raster.ScissorEnable) out.notes.push_back("scissor enabled: not replayed (whole target)");
    out.viewport = D3D11_VIEWPORT{0, 0, static_cast<float>(out.rtWidth), static_cast<float>(out.rtHeight), 0, 1};
    const Json* vps = rs ? rs->get("viewports") : nullptr;
    if (vps && vps->kind == Json::Array && !vps->arr.empty()) {
        const Json& v = vps->arr[0];
        out.viewport = D3D11_VIEWPORT{f32(&v, "TopLeftX", 0), f32(&v, "TopLeftY", 0), f32(&v, "Width", out.viewport.Width),
                                      f32(&v, "Height", out.viewport.Height), f32(&v, "MinDepth", 0), f32(&v, "MaxDepth", 1)};
    }
    // targets before / after (only after the export's replay step)
    TexFile t;
    if (loadTexture(dir + L"/rt_before_0.bin", t).ok()) {
        out.before = std::move(t);
        out.haveBefore = true;
    }
    TexFile a;
    if (loadTexture(dir + L"/rt_after_0.bin", a).ok()) {
        out.after = std::move(a);
        out.haveAfter = true;
    }
    if ((!out.rtWidth || !out.rtHeight) && (out.haveBefore || out.haveAfter)) {
        const TexFile& tf = out.haveAfter ? out.after : out.before;
        out.rtWidth = tf.width;
        out.rtHeight = tf.height;
        if (out.rtFormat == DXGI_FORMAT_UNKNOWN) out.rtFormat = tf.format;
    }
    if ((!out.rtWidth || !out.rtHeight) && vps && vps->kind == Json::Array && !vps->arr.empty()) {
        out.rtWidth = static_cast<uint32_t>(out.viewport.TopLeftX + out.viewport.Width);
        out.rtHeight = static_cast<uint32_t>(out.viewport.TopLeftY + out.viewport.Height);
        out.notes.push_back("render target size taken from viewport 0 (the export has no resource description)");
    }
    if (!out.rtWidth || !out.rtHeight) return Status::fail(Err::NotFound, "state.json: render target 0 without a size (and no target files)");
    if (out.viewport.Width == 0 || out.viewport.Height == 0)
        out.viewport = D3D11_VIEWPORT{0, 0, static_cast<float>(out.rtWidth), static_cast<float>(out.rtHeight), 0, 1};
    if (out.hasDsv && out.depth.DepthEnable) out.notes.push_back("depth test on: the export has no depth contents, the replay clears depth to 1");
    return Status::Ok();
}

Status drawVertexShader(const std::wstring& exportDir, const DrawEntry& d, std::vector<uint8_t>& code) {
    code.clear();
    std::wstring dir;
    C5R_TRY(inside(exportDir, d.dir, dir));
    Json j;
    C5R_TRY(terrainshot::readJsonFile(dir + L"/state.json", j));
    const Json* vs = any(&j, {"vs", "VS"});
    if (!vs) return Status::fail(Err::NotFound, "state.json: no vertex shader");
    const Json* file = any(vs, {"file", "bytecode"});
    if (!file || file->kind != Json::String) file = fileOf(vs->get("shader"));
    if (!file) return Status::fail(Err::NotFound, "state.json: vertex shader without a file");
    std::wstring p;
    C5R_TRY(inside(exportDir, file->str, p));
    return readFile(p, code);
}

Status capturedWaterFrame(const CapturedDraw& s, std::vector<uint8_t>& out) {
    for (const Stage::Cb& c : s.vs.cbs)
        if (c.slot == 1 && c.data.size() >= water::kWaterFrameBytes) {
            out.assign(c.data.begin(), c.data.begin() + water::kWaterFrameBytes);
            return Status::Ok();
        }
    return Status::fail(Err::NotFound, "no 0x250-byte constant buffer at VS slot 1");
}

Status checkWaterFrame(const uint8_t cap[water::kWaterFrameBytes], const water::WaterFields& fields, const water::World* world,
                       std::vector<MemberCheck>& out, water::FrameInputs* used) {
    out.clear();
    if (!cap) return Status::fail(Err::BadLayout, "checkWaterFrame: no block");
    if (!fields.valid) return Status::fail(Err::NotFound, "checkWaterFrame: fields not valid");
    auto at = [&](uint32_t off) {
        float v;
        std::memcpy(&v, cap + off, 4);
        return v;
    };
    water::FrameInputs in;
    // camera: view = identity, proj = the captured ViewProj (View x Proj then reproduces it exactly)
    const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(in.cam.view, ident, sizeof ident);
    std::memcpy(in.cam.proj, cap, 64);
    in.cam.width = in.cam.height = 1;
    std::memcpy(in.overlayProj, cap + 0x40, 64);
    std::memcpy(in.shadowMatrix1, cap + 0x160, 64);
    std::memcpy(in.shadowMatrix0, cap + 0x1A0, 64);
    in.haveCamPos = true;
    std::memcpy(in.camPos, cap + 0x150, 12);
    in.shadowBlend = at(0x220);
    std::memcpy(in.fog.color, cap + 0x230, 16);
    in.fog.density = -at(0x140);
    in.fog.w = at(0x14C);
    if (in.fog.density != 0) {
        const double e = std::exp(-static_cast<double>(in.fog.density) * in.camPos[2]) * (-1.0 / in.fog.density);
        in.fog.scale = e != 0 ? static_cast<float>(at(0x144) / e) : 1.0f;
    }
    in.fields = fields;
    // world: given, or solved from HeightmapUV (1/width, -1/height, translation) and WavePhase [I]
    if (world && world->valid) {
        in.world = *world;
    } else {
        const float sxu = at(0x1E0), syu = at(0x1E0 + 20);
        if (sxu == 0 || syu == 0) return Status::fail(Err::BadLayout, "checkWaterFrame: HeightmapUV scale 0 and no world given");
        water::World w;
        w.width = 1.0f / sxu;
        w.height = -1.0f / syu;
        w.minX = -at(0x1E0 + 48) / sxu;
        w.minY = -at(0x1E0 + 52) / syu;
        w.maxX = -w.minX;
        w.maxY = -w.minY;
        w.samplesX = static_cast<uint32_t>(std::lround(w.width / w.sx)) + 1;
        w.samplesY = static_cast<uint32_t>(std::lround(w.height / w.sy)) + 1;
        w.valid = true;
        in.world = w;
    }
    // the clock-like fields from their members: the quotient, then the float within 8 ulp of it whose forward formula
    // gives the member's bits (a division does not always invert a product exactly)
    auto solve = [](float guess, float target, float (*fwd)(float, const void*), const void* ctxp) {
        float best = guess;
        float up = guess, dn = guess;
        for (int k = 0; k <= 8; ++k) {
            if (fwd(up, ctxp) == target) return up;
            if (fwd(dn, ctxp) == target) return dn;
            up = std::nextafter(up, FLT_MAX);
            dn = std::nextafter(dn, -FLT_MAX);
        }
        return best;
    };
    const float root2 = static_cast<float>(std::sqrt(2.0));
    in.fields.set(0xE4, solve(at(0x080) / (root2 * 0.5555555820465088f), at(0x080),
                              [](float e4, const void*) { return (static_cast<float>(std::sqrt(2.0)) * e4) * 0.5555555820465088f; }, nullptr));
    const float boxW = in.world.maxX - in.world.minX;
    const float n = static_cast<float>(static_cast<int>(in.fields.get(0xD4) * boxW));
    const float sn = (1.0f / boxW) * n;
    if (sn != 0)
        in.fields.set(0xDC, solve(at(0x0B0) / sn, at(0x0B0), [](float dc, const void* p) { return dc * *static_cast<const float*>(p); }, &sn));
    const float sz = in.world.sz;
    in.fields.set(0xC8, solve(at(0x0C0) / (sz * 255.0f), at(0x0C0), [](float c8, const void* p) { return (c8 * *static_cast<const float*>(p)) * 255.0f; }, &sz));
    if (in.fog.density != 0) {
        struct FogCtx { float d, camZ; } fc{in.fog.density, in.camPos[2]};
        in.fog.scale = solve(in.fog.scale, at(0x144),
                             [](float scale, const void* p) {
                                 const FogCtx& c = *static_cast<const FogCtx*>(p);
                                 const float x = -(c.d * c.camZ);
                                 const double e = std::exp2(static_cast<double>(x) * 1.4426950408889634);
                                 return static_cast<float>(e * (-1.0 / static_cast<double>(c.d)) * static_cast<double>(scale));
                             },
                             &fc);
    }
    uint8_t mine[water::kWaterFrameBytes];
    C5R_TRY(water::computeWaterFrame(in, mine));
    if (used) *used = in;
    struct M { const char* name; uint32_t off, bytes; bool input; };
    const M ms[] = {{"ViewProj", 0x000, 64, true},       {"OverlayProj", 0x040, 64, true}, {"WaveScale", 0x080, 16, false},
                    {"WavePhase", 0x090, 16, false},     {"WaveConst", 0x0A0, 16, false},  {"WaveR11", 0x0B0, 4, false},
                    {"WaveR12", 0x0C0, 4, false},        {"R13 (+0xD0)", 0x0D0, 4, false}, {"WaterColorA (+0x90)", 0x0E0, 12, false},
                    {"WaterColorB (+0x9C)", 0x0F0, 12, false}, {"R16 (+0x70)", 0x100, 16, false}, {"R17 (+0x80)", 0x110, 16, false},
                    {"R18 (+0xA8)", 0x120, 16, false},   {"R19 (+0xB8)", 0x130, 16, false}, {"FogParams", 0x140, 16, false},
                    {"CameraPos", 0x150, 12, true},      {"ShadowMatrix1", 0x160, 64, true}, {"ShadowMatrix0", 0x1A0, 64, true},
                    {"HeightmapUV", 0x1E0, 64, false},   {"R34 (0xD90FA8)", 0x220, 4, true}, {"FogColor", 0x230, 16, true},
                    {"R36 (+0x6C)", 0x240, 4, false}};
    for (const M& m : ms) {
        MemberCheck c;
        c.name = m.name;
        c.offset = m.off;
        c.bytes = m.bytes;
        c.input = m.input;
        c.equal = std::memcmp(mine + m.off, cap + m.off, m.bytes) == 0;
        for (uint32_t i = 0; i < m.bytes; i += 4) {
            float a, b;
            std::memcpy(&a, mine + m.off + i, 4);
            std::memcpy(&b, cap + m.off + i, 4);
            const double d = std::fabs(static_cast<double>(a) - b), s = std::max(std::fabs(static_cast<double>(b)), 1e-30);
            if (a != b) c.maxRelDiff = std::max(c.maxRelDiff, d / s);
        }
        out.push_back(c);
    }
    return Status::Ok();
}

Status replayDraw(ID3D11Device* dev, ID3D11DeviceContext* ctx, const CapturedDraw& s, const ModuleParts* mod, ReplayResult& out) {
    out.notes = s.notes;
    out.width = s.rtWidth;
    out.height = s.rtHeight;
    out.compared = false;
    if (!dev || !ctx) return Status::fail(Err::BadLayout, "replay: no device");
    if (!rgba8(s.rtFormat)) return Status::fail(Err::Unsupported, "replay: render target format " + std::to_string(static_cast<int>(s.rtFormat)) + " is not 4-byte RGBA");
    std::vector<IUnknown*> keep;  // released at the end
    auto hold = [&](IUnknown* p) {
        if (p) keep.push_back(p);
    };
    struct Guard {
        std::vector<IUnknown*>& k;
        ~Guard() {
            for (IUnknown* p : k) p->Release();
        }
    } guard{keep};
    // target with the "before" contents
    const DXGI_FORMAT rtTex = typelessOf(s.rtFormat);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = s.rtWidth;
    td.Height = s.rtHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = rtTex;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    std::vector<uint8_t> zeros;
    D3D11_SUBRESOURCE_DATA init = {};
    if (s.haveBefore && s.before.width == s.rtWidth && s.before.height == s.rtHeight && rgba8(s.before.format)) {
        init = {s.before.bytes.data() + s.before.subs[0].offset, s.before.subs[0].rowPitch, 0};
    } else {
        zeros.assign(static_cast<size_t>(s.rtWidth) * s.rtHeight * 4, 0);
        init = {zeros.data(), s.rtWidth * 4, 0};
        out.notes.push_back("no target 'before' contents: the replay starts from zeros");
    }
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* rt = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture2D(&td, &init, &rt), "CreateTexture2D (target)"));
    hold(rt);
    // A multisampled target (the stock frame's): every sample takes the "before" pixel through a full-screen copy, the
    // draw runs multisampled, and the result is resolved into `rt` for the comparison.
    const uint32_t samples = s.rtSamples;
    ID3D11Texture2D* rtMs = nullptr;
    D3D11_TEXTURE2D_DESC msd = td;
    if (samples > 1) {
        msd.SampleDesc.Count = samples;
        msd.BindFlags = D3D11_BIND_RENDER_TARGET;
        C5R_TRY(hrOk(dev->CreateTexture2D(&msd, nullptr, &rtMs), "CreateTexture2D (multisampled target)"));
        hold(rtMs);
    }
    D3D11_RENDER_TARGET_VIEW_DESC rvd = {};
    rvd.Format = s.rtFormat;
    rvd.ViewDimension = samples > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv = nullptr;
    C5R_TRY(hrOk(dev->CreateRenderTargetView(samples > 1 ? rtMs : rt, &rvd, &rtv), "CreateRenderTargetView"));
    hold(rtv);
    if (samples > 1) {
        static const char kCopy[] =
            "Texture2D<float4> src : register(t0);\n"
            "float4 vs(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
            "float4 ps(float4 p : SV_Position) : SV_Target { return src.Load(int3(p.xy, 0)); }\n";
        ID3DBlob* vb = nullptr;
        ID3DBlob* pb = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(kCopy, sizeof kCopy - 1, "copy", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vb, &err)) ||
            FAILED(D3DCompile(kCopy, sizeof kCopy - 1, "copy", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &pb, &err))) {
            if (err) err->Release();
            if (vb) vb->Release();
            return Status::fail(Err::Device, "D3DCompile (multisample fill)");
        }
        hold(vb);
        hold(pb);
        ID3D11VertexShader* cvs = nullptr;
        ID3D11PixelShader* cps = nullptr;
        C5R_TRY(hrOk(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &cvs), "CreateVertexShader (fill)"));
        hold(cvs);
        C5R_TRY(hrOk(dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &cps), "CreatePixelShader (fill)"));
        hold(cps);
        D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
        svd.Format = s.rtFormat;
        svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        svd.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* src = nullptr;
        C5R_TRY(hrOk(dev->CreateShaderResourceView(rt, &svd, &src), "CreateShaderResourceView (fill)"));
        hold(src);
        const D3D11_VIEWPORT full = {0, 0, static_cast<float>(s.rtWidth), static_cast<float>(s.rtHeight), 0, 1};
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->RSSetState(nullptr);
        ctx->RSSetViewports(1, &full);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(cvs, nullptr, 0);
        ctx->PSSetShader(cps, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &src);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* none1 = nullptr;
        ctx->PSSetShaderResources(0, 1, &none1);
        out.notes.push_back("replayed with " + std::to_string(samples) + " samples, every sample filled from the resolved 'before' target");
    }
    ID3D11DepthStencilView* dsv = nullptr;
    if (s.hasDsv) {
        D3D11_TEXTURE2D_DESC dd = samples > 1 ? msd : td;
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D* dt = nullptr;
        C5R_TRY(hrOk(dev->CreateTexture2D(&dd, nullptr, &dt), "CreateTexture2D (depth)"));
        hold(dt);
        C5R_TRY(hrOk(dev->CreateDepthStencilView(dt, nullptr, &dsv), "CreateDepthStencilView"));
        hold(dsv);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
    // shaders and layout
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    C5R_TRY(hrOk(dev->CreateVertexShader(s.vs.code.data(), s.vs.code.size(), nullptr, &vs), "CreateVertexShader (capture)"));
    hold(vs);
    C5R_TRY(hrOk(dev->CreatePixelShader(s.ps.code.data(), s.ps.code.size(), nullptr, &ps), "CreatePixelShader (capture)"));
    hold(ps);
    ID3D11InputLayout* il = nullptr;
    if (!s.elements.empty()) {
        C5R_TRY(hrOk(dev->CreateInputLayout(s.elements.data(), static_cast<UINT>(s.elements.size()), s.vs.code.data(), s.vs.code.size(), &il),
                     "CreateInputLayout (capture)"));
        hold(il);
    }
    auto makeBuffer = [&](const void* data, size_t n, UINT bind, ID3D11Buffer** b) -> Status {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = static_cast<UINT>(bind == D3D11_BIND_CONSTANT_BUFFER ? (n + 15) / 16 * 16 : std::max<size_t>(n, 4));
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind;
        std::vector<uint8_t> pad(d.ByteWidth, 0);
        if (n) std::memcpy(pad.data(), data, n);
        D3D11_SUBRESOURCE_DATA sd = {pad.data(), 0, 0};
        C5R_TRY(hrOk(dev->CreateBuffer(&d, &sd, b), "CreateBuffer (capture)"));
        hold(*b);
        return Status::Ok();
    };
    // vertex and index buffers (or the module's)
    for (const CapturedDraw::Vb& v : s.vbs) {
        ID3D11Buffer* b = nullptr;
        UINT stride = v.stride, off = v.offset;
        if (v.slot == 0 && mod && mod->vertices) {
            C5R_TRY(makeBuffer(mod->vertices->data(), mod->vertices->size() * 4, D3D11_BIND_VERTEX_BUFFER, &b));
            stride = 16;
            off = 0;
        } else {
            C5R_TRY(makeBuffer(v.data.data(), v.data.size(), D3D11_BIND_VERTEX_BUFFER, &b));
        }
        ctx->IASetVertexBuffers(v.slot, 1, &b, &stride, &off);
    }
    uint32_t startIndex = s.startIndex, indexCount = s.indexCount;
    if (!s.ib.empty() || (mod && mod->indices)) {
        ID3D11Buffer* b = nullptr;
        if (mod && mod->indices) {
            C5R_TRY(makeBuffer(mod->indices->data(), mod->indices->size() * 2, D3D11_BIND_INDEX_BUFFER, &b));
            ctx->IASetIndexBuffer(b, DXGI_FORMAT_R16_UINT, 0);
            startIndex = mod->startIndex;
            indexCount = mod->indexCount;
        } else {
            C5R_TRY(makeBuffer(s.ib.data(), s.ib.size(), D3D11_BIND_INDEX_BUFFER, &b));
            ctx->IASetIndexBuffer(b, s.ibFormat, s.ibOffset);
        }
    }
    ctx->IASetInputLayout(il);
    ctx->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(s.topology));
    // stage resources
    auto bindStage = [&](const Stage& st, bool pixel) -> Status {
        for (const Stage::Cb& c : st.cbs) {
            ID3D11Buffer* b = nullptr;
            if (c.slot == 1 && mod && mod->frame && c.data.size() >= water::kWaterFrameBytes) {
                std::vector<uint8_t> d = c.data;
                std::memcpy(d.data(), mod->frame, water::kWaterFrameBytes);
                C5R_TRY(makeBuffer(d.data(), d.size(), D3D11_BIND_CONSTANT_BUFFER, &b));
            } else {
                C5R_TRY(makeBuffer(c.data.data(), c.data.size(), D3D11_BIND_CONSTANT_BUFFER, &b));
            }
            if (pixel) ctx->PSSetConstantBuffers(c.slot, 1, &b);
            else ctx->VSSetConstantBuffers(c.slot, 1, &b);
        }
        for (const Stage::Srv& v : st.srvs) {
            const TexFile& t = v.tex;
            const DXGI_FORMAT viewF = v.viewFormat != DXGI_FORMAT_UNKNOWN ? v.viewFormat : t.format;
            const DXGI_FORMAT resF = viewF != t.format ? typelessOf(t.format) : t.format;
            std::vector<D3D11_SUBRESOURCE_DATA> sd;
            for (const TexFile::Sub& x : t.subs) sd.push_back(D3D11_SUBRESOURCE_DATA{t.bytes.data() + x.offset, x.rowPitch, x.slicePitch});
            ID3D11Resource* res = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
            vd.Format = viewF;
            if (t.volume) {
                D3D11_TEXTURE3D_DESC d = {};
                d.Width = t.width;
                d.Height = t.height;
                d.Depth = t.depth;
                d.MipLevels = t.mips;
                d.Format = resF;
                d.Usage = D3D11_USAGE_IMMUTABLE;
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                ID3D11Texture3D* x = nullptr;
                C5R_TRY(hrOk(dev->CreateTexture3D(&d, sd.data(), &x), "CreateTexture3D (capture t" + std::to_string(v.slot) + ")"));
                res = x;
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                vd.Texture3D.MipLevels = t.mips;
            } else {
                D3D11_TEXTURE2D_DESC d = {};
                d.Width = t.width;
                d.Height = t.height;
                d.MipLevels = t.mips;
                d.ArraySize = t.arraySize;
                d.Format = resF;
                d.SampleDesc.Count = 1;
                d.Usage = D3D11_USAGE_IMMUTABLE;
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                ID3D11Texture2D* x = nullptr;
                C5R_TRY(hrOk(dev->CreateTexture2D(&d, sd.data(), &x), "CreateTexture2D (capture t" + std::to_string(v.slot) + ")"));
                res = x;
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                vd.Texture2D.MipLevels = t.mips;
            }
            hold(res);
            ID3D11ShaderResourceView* srv = nullptr;
            C5R_TRY(hrOk(dev->CreateShaderResourceView(res, &vd, &srv), "CreateShaderResourceView (capture t" + std::to_string(v.slot) + ")"));
            hold(srv);
            if (pixel) ctx->PSSetShaderResources(v.slot, 1, &srv);
            else ctx->VSSetShaderResources(v.slot, 1, &srv);
        }
        for (const Stage::Samp& sp : st.samps) {
            ID3D11SamplerState* ss = nullptr;
            C5R_TRY(hrOk(dev->CreateSamplerState(&sp.desc, &ss), "CreateSamplerState (capture)"));
            hold(ss);
            if (pixel) ctx->PSSetSamplers(sp.slot, 1, &ss);
            else ctx->VSSetSamplers(sp.slot, 1, &ss);
        }
        return Status::Ok();
    };
    C5R_TRY(bindStage(s.vs, false));
    C5R_TRY(bindStage(s.ps, true));
    if (mod)
        for (UINT k = 0; k < 14; ++k)
            if (mod->psSrv[k]) ctx->PSSetShaderResources(k, 1, &mod->psSrv[k]);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ID3D11BlendState* bs = nullptr;
    C5R_TRY(hrOk(dev->CreateBlendState(&s.blend, &bs), "CreateBlendState (capture)"));
    hold(bs);
    ID3D11DepthStencilState* dss = nullptr;
    C5R_TRY(hrOk(dev->CreateDepthStencilState(&s.depth, &dss), "CreateDepthStencilState (capture)"));
    hold(dss);
    D3D11_RASTERIZER_DESC rd = s.raster;
    rd.ScissorEnable = FALSE;
    ID3D11RasterizerState* rs = nullptr;
    C5R_TRY(hrOk(dev->CreateRasterizerState(&rd, &rs), "CreateRasterizerState (capture)"));
    hold(rs);
    ctx->OMSetBlendState(bs, s.blendFactor, s.sampleMask);
    ctx->OMSetDepthStencilState(dss, s.stencilRef);
    ctx->RSSetState(rs);
    ctx->RSSetViewports(1, &s.viewport);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    if (s.call.find("Indexed") != std::string::npos) ctx->DrawIndexed(indexCount, startIndex, s.baseVertex);
    else ctx->Draw(s.vertexCount, s.startVertex);
    // unbind and read back
    ID3D11ShaderResourceView* none[16] = {};
    ctx->PSSetShaderResources(0, 16, none);
    ctx->VSSetShaderResources(0, 16, none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11_TEXTURE2D_DESC sdsc = td;
    sdsc.Usage = D3D11_USAGE_STAGING;
    sdsc.BindFlags = 0;
    sdsc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* stg = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture2D(&sdsc, nullptr, &stg), "CreateTexture2D (staging)"));
    hold(stg);
    if (samples > 1) ctx->ResolveSubresource(rt, 0, rtMs, 0, s.rtFormat);
    ctx->CopyResource(stg, rt);
    D3D11_MAPPED_SUBRESOURCE m;
    C5R_TRY(hrOk(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m), "Map (staging)"));
    out.rgba.resize(static_cast<size_t>(s.rtWidth) * s.rtHeight * 4);
    for (uint32_t y = 0; y < s.rtHeight; ++y)
        std::memcpy(out.rgba.data() + static_cast<size_t>(y) * s.rtWidth * 4, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                    s.rtWidth * 4);
    ctx->Unmap(stg, 0);
    if (s.haveAfter) {
        if (s.after.width != s.rtWidth || s.after.height != s.rtHeight || !rgba8(s.after.format))
            return Status::fail(Err::BadLayout, "rt_after_0 is not the target's size and format");
        C5R_TRY(compareRgba8(out.rgba.data(), s.rtWidth * 4, s.after.bytes.data() + s.after.subs[0].offset, s.after.subs[0].rowPitch,
                             s.rtWidth, s.rtHeight, out.cmp));
        out.compared = true;
    } else {
        out.notes.push_back("no target 'after' contents in the export (its replay step has not run): nothing to compare");
    }
    return Status::Ok();
}

}} // namespace c5r::watershot
