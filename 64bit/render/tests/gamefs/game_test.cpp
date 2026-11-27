// render/tests/gamefs/game_test.cpp - game-file test (task 2901): opens every pack of the install named by
// C5R_GAME_DIR, rebuilds the lookup, and loads terrain-side textures through GameFiles + createTexture on a device
// with no window (hardware, WARP if there is none). SKIPs when C5R_GAME_DIR is not set. Reads only.
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/fpk.h"
#include "c5r/gamefs/gamefiles.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::gamefs;

namespace {
std::wstring wide(const std::string& s) { return fromUtf8(s); }

void walk(const std::wstring& dir, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(dir + L"\\" + n, out);
        else if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".fpk") == 0) out.push_back(dir + L"\\" + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

ID3D11Device* device(bool* warp) {
    ID3D11Device* dev = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    *warp = false;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, nullptr))) {
        *warp = true;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, nullptr);
    }
    return dev;
}

// Loads one name through GameFiles and creates it; returns the image description for further checks.
bool load(const GameFiles& gf, ID3D11Device* dev, const std::string& name, DdsImage& img) {
    std::vector<uint8_t> b;
    std::string origin;
    Status st = gf.read(name, b, &origin);
    if (!st.ok()) {
        std::printf("  %s: %s\n", name.c_str(), st.str().c_str());
        return false;
    }
    st = parseDds(b.data(), b.size(), img);
    if (st.ok()) {
        ID3D11Resource* t = nullptr;
        ID3D11ShaderResourceView* v = nullptr;
        st = createTexture(dev, img, &t, &v);
        if (v) v->Release();
        if (t) t->Release();
    }
    std::printf("  %-36s %-6s %-20s %ux%ux%u mips %u array %u  <- %s  %s\n", name.c_str(), img.sourceFormat.c_str(),
                dxgiFormatName(img.format).c_str(), img.width, img.height, img.depth, img.mipLevels, img.arraySize,
                origin.c_str(), st.ok() ? "ok" : st.str().c_str());
    img.src = nullptr;  // the buffer dies here
    return st.ok();
}
} // namespace

C5R_TEST(game_every_pack_opens) {
    std::wstring root = wide(c5r::test::gameDir());
    std::vector<std::wstring> packs;
    walk(root + L"\\Resource", packs);
    walk(root + L"\\Assets", packs);
    size_t entries = 0, opened = 0;
    for (const std::wstring& p : packs) {
        FpkArchive a;
        Status st = a.open(p);
        if (st.ok()) {
            ++opened;
            entries += a.entries().size();
        } else {
            std::printf("  %s\n", st.str().c_str());
        }
    }
    std::printf("  packs: %zu opened of %zu, %zu entries\n", opened, packs.size(), entries);
    CHECK(!packs.empty());
    CHECK_EQ(opened, packs.size());
}

C5R_TEST(game_lookup_and_terrain_textures) {
    std::wstring root = wide(c5r::test::gameDir());
    GameFiles gf;
    CHECK_OK(gf.open(root));
    std::printf("  GameFiles: %zu packages, %zu packs mounted, %zu names, %zu warnings\n", gf.packages().size(),
                gf.mountedPacks().size(), gf.fileCount(), gf.warnings().size());
    for (const std::string& w : gf.warnings()) std::printf("  warning: %s\n", w.c_str());
    CHECK(gf.fileCount() > 10000);
    bool warp = false;
    ID3D11Device* dev = device(&warp);
    CHECK(dev != nullptr);
    if (!dev) return;
    std::printf("  device: %s\n", warp ? "WARP" : "hardware");
    DdsImage img;
    // A terrain piece height map: 128x128 16-bit RGBA (GATE2 section 3).
    CHECK(load(gf, dev, "missing_hex_H.dds", img));
    CHECK(img.format == DXGI_FORMAT_R16G16B16A16_UNORM && img.width == 128 && img.height == 128);
    // BC3 terrain diffuse from TerrainTextures.fpk.
    CHECK(load(gf, dev, "forest_africa.dds", img));
    CHECK(img.format == DXGI_FORMAT_BC3_UNORM && img.mipLevels > 1);
    // A cube map on the terrain side.
    CHECK(load(gf, dev, "ice_environment_diffuse.dds", img));
    CHECK(img.cube && img.arraySize == 6);
    // Every DDS of TerrainTextures.fpk and DecalTextures.fpk, through the lookup.
    int total = 0, ok = 0;
    for (const std::string& n : gf.names()) {
        std::string o = gf.origin(n);
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".dds") != 0) continue;
        if (o.find("TerrainTextures.fpk:") == std::string::npos && o.find("DecalTextures.fpk:") == std::string::npos &&
            o.find("Assets/terrain/") != 0)
            continue;
        std::vector<uint8_t> b;
        DdsImage im;
        ++total;
        if (!gf.read(n, b).ok() || !parseDds(b.data(), b.size(), im).ok()) continue;
        ID3D11Resource* t = nullptr;
        if (createTexture(dev, im, &t, nullptr).ok()) ++ok;
        if (t) t->Release();
    }
    std::printf("  terrain packs + Assets/terrain resolved through the lookup: %d of %d created\n", ok, total);
    CHECK(total > 1000);
    CHECK_EQ(ok, total);
    dev->Release();
}

C5R_TEST_MAIN
