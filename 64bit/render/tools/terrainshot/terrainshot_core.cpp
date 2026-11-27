// render/tools/terrainshot/terrainshot_core.cpp - see terrainshot_core.h (epic 3200, issue 3202). Own code.
#include "terrainshot_core.h"

#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace c5r { namespace terrainshot {

namespace {

std::string u8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

Status readAll(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Status::fail(Err::Io, "cannot open " + u8(path));
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart > (1LL << 31)) {
        CloseHandle(f);
        return Status::fail(Err::Io, "cannot size (or above 2 GiB) " + u8(path));
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 30));
        if (!ReadFile(f, out.data() + done, want, &got, nullptr) || got == 0) {
            CloseHandle(f);
            return Status::fail(Err::Io, "cannot read " + u8(path));
        }
        done += got;
    }
    CloseHandle(f);
    return Status::Ok();
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& t) : s(t) {}
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    Status value(Json& out, int depth) {
        if (depth > 64) return Status::fail(Err::BadLayout, "json: nesting deeper than 64");
        ws();
        if (i >= s.size()) return Status::fail(Err::Truncated, "json: value expected at end");
        const char c = s[i];
        if (c == '{') {
            out.kind = Json::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return Status::Ok(); }
            for (;;) {
                ws();
                Json key;
                if (i >= s.size()) return Status::fail(Err::Truncated, "json: object not closed");
                if (s[i] != '"') return Status::fail(Err::BadLayout, "json: key expected at " + std::to_string(i));
                C5R_TRY(readString(key.str));
                ws();
                if (i >= s.size()) return Status::fail(Err::Truncated, "json: ':' expected at end");
                if (s[i] != ':') return Status::fail(Err::BadLayout, "json: ':' expected at " + std::to_string(i));
                ++i;
                Json v;
                C5R_TRY(value(v, depth + 1));
                out.obj[key.str] = std::move(v);
                ws();
                if (i >= s.size()) return Status::fail(Err::Truncated, "json: object not closed");
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == '}') { ++i; return Status::Ok(); }
                return Status::fail(Err::BadLayout, "json: ',' or '}' expected at " + std::to_string(i));
            }
        }
        if (c == '[') {
            out.kind = Json::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return Status::Ok(); }
            for (;;) {
                Json v;
                C5R_TRY(value(v, depth + 1));
                out.arr.push_back(std::move(v));
                ws();
                if (i >= s.size()) return Status::fail(Err::Truncated, "json: array not closed");
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == ']') { ++i; return Status::Ok(); }
                return Status::fail(Err::BadLayout, "json: ',' or ']' expected at " + std::to_string(i));
            }
        }
        if (c == '"') {
            out.kind = Json::String;
            return readString(out.str);
        }
        auto word = [&](const char* w, Json::Kind k, bool b) -> Status {
            const size_t n = std::strlen(w);
            if (s.compare(i, n, w) != 0)
                return i + n > s.size() ? Status::fail(Err::Truncated, "json: literal cut off")
                                        : Status::fail(Err::BadLayout, "json: bad literal at " + std::to_string(i));
            i += n;
            out.kind = k;
            out.b = b;
            return Status::Ok();
        };
        if (c == 't') return word("true", Json::Bool, true);
        if (c == 'f') return word("false", Json::Bool, false);
        if (c == 'n') return word("null", Json::Null, false);
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* b = s.c_str() + i;
            char* e = nullptr;
            const double d = std::strtod(b, &e);
            if (e == b) return Status::fail(Err::BadLayout, "json: bad number at " + std::to_string(i));
            i += static_cast<size_t>(e - b);
            out.kind = Json::Number;
            out.num = d;
            return Status::Ok();
        }
        return Status::fail(Err::BadLayout, "json: unexpected character at " + std::to_string(i));
    }
    Status readString(std::string& out) {
        ++i;  // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\') {
                if (i + 1 >= s.size()) break;
                const char e = s[i + 1];
                if (e == 'u') {
                    if (i + 6 > s.size()) return Status::fail(Err::Truncated, "json: \\u cut off");
                    out += '?';  // not needed by these files
                    i += 6;
                    continue;
                }
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e;
                i += 2;
                continue;
            }
            out += s[i++];
        }
        if (i >= s.size()) return Status::fail(Err::Truncated, "json: string not closed");
        ++i;
        return Status::Ok();
    }
};

Status number(const Json& j, const char* key, double& out) {
    const Json* v = j.get(key);
    if (!v || v->kind != Json::Number) return Status::fail(Err::NotFound, std::string("json: no number '") + key + "'");
    out = v->num;
    return Status::Ok();
}

Status matrix(const Json* m, float out[16], const char* what) {
    if (!m || m->kind != Json::Array || m->arr.size() != 4)
        return Status::fail(Err::NotFound, std::string("camera: '") + what + "' is not a 4x4 array");
    for (int r = 0; r < 4; ++r) {
        const Json& row = m->arr[static_cast<size_t>(r)];
        if (row.kind != Json::Array || row.arr.size() != 4)
            return Status::fail(Err::NotFound, std::string("camera: '") + what + "' row is not 4 numbers");
        for (int c = 0; c < 4; ++c) {
            const Json& v = row.arr[static_cast<size_t>(c)];
            if (v.kind != Json::Number) return Status::fail(Err::NotFound, std::string("camera: '") + what + "' has a non-number");
            if (!std::isfinite(v.num)) return Status::fail(Err::BadLayout, std::string("camera: '") + what + "' not finite");
            out[r * 4 + c] = static_cast<float>(v.num);
        }
    }
    return Status::Ok();
}

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

} // namespace

const Json* Json::get(const std::string& key) const {
    if (kind != Object) return nullptr;
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

Status parseJson(const std::string& text, Json& out) {
    out = Json();
    Parser p(text);
    p.ws();
    if (p.i >= text.size()) return Status::fail(Err::Truncated, "json: empty");
    C5R_TRY(p.value(out, 0));
    p.ws();
    if (p.i != text.size()) return Status::fail(Err::BadLayout, "json: trailing text at " + std::to_string(p.i));
    return Status::Ok();
}

Status readJsonFile(const std::wstring& path, Json& out) {
    std::vector<uint8_t> b;
    C5R_TRY(readAll(path, b));
    Status st = parseJson(std::string(b.begin(), b.end()), out);
    if (!st.ok()) st.msg = u8(path) + ": " + st.msg;
    return st;
}

Status loadGroundTruth(const std::wstring& dir, heightfield::HeightGrid& g) {
    g = heightfield::HeightGrid();
    Json info, gc;
    C5R_TRY(readJsonFile(dir + L"/heightmap_info.json", info));
    C5R_TRY(readJsonFile(dir + L"/grid_constants.json", gc));
    double sxN = 0, syN = 0;
    C5R_TRY(number(info, "samples_x", sxN));
    C5R_TRY(number(info, "samples_y", syN));
    const uint32_t X = static_cast<uint32_t>(sxN), Y = static_cast<uint32_t>(syN);
    if (X == 0 || Y == 0 || X % 64 || Y % 64 || X > heightfield::kMaxPlots * 64 || Y > heightfield::kMaxPlots * 64 ||
        static_cast<double>(X) != sxN || static_cast<double>(Y) != syN)
        return Status::fail(Err::BadLayout, "ground truth: samples " + std::to_string(sxN) + " x " + std::to_string(syN) +
                                                " are not plots * 64");
    double wrap = 0, gsx = 0, gsy = 0, gsz = 0, gox = 0, goy = 0;
    C5R_TRY(number(gc, "wrap_x", wrap));
    C5R_TRY(number(gc, "grid_sx", gsx));
    C5R_TRY(number(gc, "grid_sy", gsy));
    C5R_TRY(number(gc, "grid_height_scale", gsz));
    C5R_TRY(number(gc, "grid_ox", gox));
    C5R_TRY(number(gc, "grid_oy", goy));
    const Json* hex = gc.get("heightmap_header_hex");
    if (!hex || hex->kind != Json::String) return Status::fail(Err::NotFound, "ground truth: no heightmap_header_hex");
    if (hex->str.size() < 16) return Status::fail(Err::BadLayout, "ground truth: heightmap_header_hex shorter than 8 bytes");
    uint8_t hb[8];
    for (int k = 0; k < 8; ++k) {
        const std::string byte = hex->str.substr(static_cast<size_t>(k) * 2, 2);
        char* e = nullptr;
        const long v = std::strtol(byte.c_str(), &e, 16);
        if (!e || *e) return Status::fail(Err::BadLayout, "ground truth: heightmap_header_hex not hex");
        hb[k] = static_cast<uint8_t>(v);
    }
    float water = 0;
    std::memcpy(&water, hb + 4, 4);  // GridHeightmap +0x04 = 0xD91274, the water height
    const Json* h2w = gc.get("M_h2w");
    float m[16] = {0};
    if (h2w) C5R_TRY(matrix(h2w, m, "M_h2w"));
    wchar_t name[64];
    std::swprintf(name, 64, L"/heightmap_u16le_%ux%u.raw", X, Y);
    std::vector<uint8_t> raw;
    C5R_TRY(readAll(dir + name, raw));
    if (raw.size() != static_cast<size_t>(X) * Y * 2)
        return Status::fail(Err::BadLayout, "ground truth: raw has " + std::to_string(raw.size()) + " bytes, want " +
                                                std::to_string(static_cast<size_t>(X) * Y * 2));
    g.mapW = X / 64;
    g.mapH = Y / 64;
    g.samplesX = X;
    g.samplesY = Y;
    g.wrapX = wrap == 1;
    g.sx = static_cast<float>(gsx);
    g.sy = static_cast<float>(gsy);
    g.sz = static_cast<float>(gsz);
    g.ox = static_cast<float>(gox);
    g.oy = static_cast<float>(goy);
    g.waterZ = water;
    std::memcpy(g.hexToWorld, m, sizeof m);
    g.h.resize(static_cast<size_t>(X) * Y);
    std::memcpy(g.h.data(), raw.data(), raw.size());  // little-endian u16, x86-64 native order
    return Status::Ok();
}

Status loadTapFile(const std::wstring& path, heightfield::HeightGrid& out) {
    std::vector<uint8_t> b;
    C5R_TRY(readAll(path, b));
    return heightfield::readTap(b.data(), b.size(), out);
}

Status loadCamera(const std::wstring& path, int view, terrain::Camera& cam) {
    cam = terrain::Camera();
    Json j;
    C5R_TRY(readJsonFile(path, j));
    double nv = 1;
    if (j.get("num_views")) C5R_TRY(number(j, "num_views", nv));
    if (view < 0 || view >= static_cast<int>(nv) || view > 3)
        return Status::fail(Err::BadLayout, "camera: view " + std::to_string(view) + " outside num_views " + std::to_string(nv));
    C5R_TRY(matrix(j.get("proj"), cam.proj, "proj"));
    const Json* views = j.get("views");
    if (!views || views->kind != Json::Array || views->arr.size() <= static_cast<size_t>(view))
        return Status::fail(Err::NotFound, "camera: no views[" + std::to_string(view) + "]");
    C5R_TRY(matrix(&views->arr[static_cast<size_t>(view)], cam.view, "views[i]"));
    const Json* inv = j.get("inv_views");
    if (inv && inv->kind == Json::Array && inv->arr.size() > static_cast<size_t>(view))
        C5R_TRY(matrix(&inv->arr[static_cast<size_t>(view)], cam.camWorld, "inv_views[i]"));
    const Json* bb = j.get("backbuffer");
    if (!bb || bb->kind != Json::Array || bb->arr.size() != 2 || bb->arr[0].kind != Json::Number ||
        bb->arr[1].kind != Json::Number)
        return Status::fail(Err::NotFound, "camera: no backbuffer [w, h]");
    const double w = bb->arr[0].num, h = bb->arr[1].num;
    if (!(w >= 1 && h >= 1 && w <= 16384 && h <= 16384))
        return Status::fail(Err::BadLayout, "camera: backbuffer size outside 1..16384");
    cam.width = static_cast<uint32_t>(w);
    cam.height = static_cast<uint32_t>(h);
    return Status::Ok();
}

Device::~Device() {
    rel(ctx);
    rel(dev);
}

Status Device::create(bool warp) {
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    HRESULT h = E_FAIL;
    if (!warp) {
        h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (SUCCEEDED(h)) adapter = "hardware";
    }
    if (FAILED(h)) {
        h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (SUCCEEDED(h)) adapter = "WARP";
    }
    if (FAILED(h)) return Status::fail(Err::Device, "D3D11CreateDevice (feature level 11_0) failed");
    IDXGIDevice* dx = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dx)))) {
        IDXGIAdapter* a = nullptr;
        if (SUCCEEDED(dx->GetAdapter(&a))) {
            DXGI_ADAPTER_DESC d;
            if (SUCCEEDED(a->GetDesc(&d))) adapter += ": " + u8(d.Description);
            a->Release();
        }
        dx->Release();
    }
    return Status::Ok();
}

namespace {
Status target(ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT f, ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv,
              ID3D11Texture2D** staging) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, tex))) return Status::fail(Err::Device, "CreateTexture2D (target)");
    if (FAILED(dev->CreateRenderTargetView(*tex, nullptr, rtv))) return Status::fail(Err::Device, "CreateRenderTargetView");
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, staging))) return Status::fail(Err::Device, "CreateTexture2D (staging)");
    return Status::Ok();
}
Status readBack(ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, ID3D11Texture2D* staging, uint32_t rowBytes, uint32_t h,
                uint8_t* out) {
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return Status::fail(Err::Device, "Map (staging)");
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(out + static_cast<size_t>(y) * rowBytes, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, rowBytes);
    ctx->Unmap(staging, 0);
    return Status::Ok();
}
} // namespace

Status renderShot(Device& d, terrain::Renderer& r, const terrain::Camera& cam, const terrain::Options& o, bool wantWorld,
                  ShotResult& out) {
    out = ShotResult();
    if (!d.dev || !d.ctx) return Status::fail(Err::BadLayout, "renderShot: no device");
    if (cam.width == 0 || cam.height == 0) return Status::fail(Err::BadLayout, "renderShot: camera size 0");
    out.width = cam.width;
    out.height = cam.height;
    Status st;
    {
        ID3D11Texture2D *tex = nullptr, *stg = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        st = target(d.dev, cam.width, cam.height, DXGI_FORMAT_R8G8B8A8_UNORM, &tex, &rtv, &stg);
        if (st.ok()) st = r.draw(d.ctx, rtv, nullptr, cam, o, &out.stats);
        if (st.ok()) {
            double ms = -1;
            if (r.gpuTime(d.ctx, true, &ms).ok()) out.gpuMs = ms;
            out.rgba.resize(static_cast<size_t>(cam.width) * cam.height * 4);
            st = readBack(d.ctx, tex, stg, cam.width * 4, cam.height, out.rgba.data());
        }
        rel(rtv);
        rel(tex);
        rel(stg);
    }
    if (st.ok() && wantWorld) {
        ID3D11Texture2D *tex = nullptr, *stg = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        st = target(d.dev, cam.width, cam.height, DXGI_FORMAT_R32G32B32A32_FLOAT, &tex, &rtv, &stg);
        terrain::Options w = o;
        w.color = terrain::ColorSource::WorldPos;
        w.clearRgba = 0;
        if (st.ok()) {
            const float zero[4] = {0, 0, 0, 0};
            d.ctx->ClearRenderTargetView(rtv, zero);
            terrain::FrameStats ws;
            st = r.draw(d.ctx, rtv, nullptr, cam, w, &ws);
        }
        if (st.ok()) {
            out.world.resize(static_cast<size_t>(cam.width) * cam.height * 4);
            st = readBack(d.ctx, tex, stg, cam.width * 16, cam.height, reinterpret_cast<uint8_t*>(out.world.data()));
        }
        rel(rtv);
        rel(tex);
        rel(stg);
    }
    return st;
}

Status writePngRgba(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
    if (w == 0 || h == 0 || rgba.size() != static_cast<size_t>(w) * h * 4)
        return Status::fail(Err::BadLayout, "png: size 0 or pixel count mismatch");
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(hrInit);
    Status st;
    IWICImagingFactory* fac = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    if (FAILED(hr)) st = Status::fail(Err::Device, "png: WIC factory");
    if (st.ok()) {
        hr = fac->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) st = Status::fail(Err::Io, "png: cannot create " + u8(path));
    }
    if (st.ok()) {
        hr = fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
        // the PNG encoder takes 32bppBGRA (it answers RGBA with BGRA): swap R and B
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat32bppBGRA) hr = E_FAIL;
        std::vector<uint8_t> bgra(rgba);
        for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
        if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), bgra.data());
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = enc->Commit();
        if (FAILED(hr)) st = Status::fail(Err::Device, "png: WIC encode");
    }
    rel(frame);
    rel(enc);
    rel(stream);
    rel(fac);
    if (uninit) CoUninitialize();
    return st;
}

Status writeWorld(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<float>& xyzw) {
    if (w == 0 || h == 0 || xyzw.size() != static_cast<size_t>(w) * h * 4)
        return Status::fail(Err::BadLayout, "world: size 0 or value count mismatch");
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return Status::fail(Err::Io, "cannot create " + u8(path));
    const size_t n = std::fwrite(xyzw.data(), 4, xyzw.size(), f);
    std::fclose(f);
    if (n != xyzw.size()) return Status::fail(Err::Io, "cannot write " + u8(path));
    FILE* j = _wfopen((path + L".json").c_str(), L"wb");
    if (!j) return Status::fail(Err::Io, "cannot create " + u8(path) + ".json");
    std::fprintf(j, "{\"width\": %u, \"height\": %u, \"channels\": 4, \"format\": \"f32le\"}\n", w, h);
    std::fclose(j);
    return Status::Ok();
}

}} // namespace c5r::terrainshot
