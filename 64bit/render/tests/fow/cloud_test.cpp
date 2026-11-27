// c5r::fow cloud pass, the parts that need neither the EXE nor the game (task 3955): the FOW CULL flags (a single
// unexplored plot, the wrap in x and y, the edge rows), HexToWorld and the cull quads' centres against 3951's captures, the
// planes' block ranges against the same captures, and CloudRenderer's refusals without the EXE.
#include <c5r/fow/fow.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "cloud_testutil.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

using namespace c5r;
using namespace c5r::fow;

namespace {

struct Map {
    uint32_t w, h;
    std::vector<PlotState> s;
    Map(uint32_t W, uint32_t H, PlotState fill) : w(W), h(H), s(static_cast<size_t>(W) * H, fill) {}
    void set(int x, int y, PlotState v) { s[static_cast<size_t>(y) * w + x] = v; }
    MapInput in() const
    {
        MapInput m;
        m.states = s.data();
        m.width = w;
        m.height = h;
        return m;
    }
};

uint8_t flag(const std::vector<uint8_t>& f, uint32_t w, int x, int y) { return f[static_cast<size_t>(y) * w + x]; }

bool inHood(int dx, int dy)   // the 21 plots
{
    return dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2 && !((dx == 2 || dx == -2) && (dy == 2 || dy == -2));
}

void testCullFlags()
{
    // all explored: 1 everywhere but rows 0, 1, h - 1
    {
        Map m(10, 10, PlotState::Visible);
        std::vector<uint8_t> f;
        CHECK(cullFlags(m.in(), f).ok());
        CHECK(f.size() == 100);
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x) CHECK(flag(f, 10, x, y) == ((y >= 2 && y <= 8) ? 1 : 0));
        Map s(10, 10, PlotState::Seen);   // seen counts as explored
        CHECK(cullFlags(s.in(), f).ok());
        CHECK(flag(f, 10, 5, 5) == 1);
    }
    // one unexplored plot: exactly the 21-plot neighbourhood loses its flag, the four corners keep it
    {
        Map m(12, 12, PlotState::Visible);
        m.set(6, 6, PlotState::Unexplored);
        std::vector<uint8_t> f;
        CHECK(cullFlags(m.in(), f).ok());
        for (int y = 2; y <= 10; ++y)
            for (int x = 0; x < 12; ++x) CHECK(flag(f, 12, x, y) == (inHood(x - 6, y - 6) ? 0 : 1));
        CHECK(flag(f, 12, 8, 8) == 1 && flag(f, 12, 4, 4) == 1 && flag(f, 12, 8, 4) == 1 && flag(f, 12, 4, 8) == 1);
        CHECK(flag(f, 12, 8, 7) == 0 && flag(f, 12, 7, 8) == 0 && flag(f, 12, 6, 8) == 0);
    }
    // the wrap in x: an unexplored plot in column 0 clears columns w-2, w-1, 0, 1, 2 of its rows
    {
        Map m(10, 12, PlotState::Visible);
        m.set(0, 6, PlotState::Unexplored);
        std::vector<uint8_t> f;
        CHECK(cullFlags(m.in(), f).ok());
        CHECK(flag(f, 10, 9, 6) == 0 && flag(f, 10, 8, 6) == 0 && flag(f, 10, 2, 6) == 0);
        CHECK(flag(f, 10, 7, 6) == 1 && flag(f, 10, 3, 6) == 1);
        CHECK(flag(f, 10, 8, 8) == 1 && flag(f, 10, 9, 8) == 0);   // (9, 8): dx = 1, dy = -2
        // wrapX false changes nothing: stock wraps both ways regardless
        MapInput in = m.in();
        in.wrapX = false;
        std::vector<uint8_t> g;
        CHECK(cullFlags(in, g).ok());
        CHECK(g == f);
    }
    // the wrap in y: an unexplored plot in row 0 clears rows h-2 (the last flagged row) as well
    {
        Map m(10, 10, PlotState::Visible);
        m.set(5, 0, PlotState::Unexplored);
        std::vector<uint8_t> f;
        CHECK(cullFlags(m.in(), f).ok());
        CHECK(flag(f, 10, 5, 8) == 0);   // dy = +2 wraps to row 0
        CHECK(flag(f, 10, 5, 2) == 0);
        CHECK(flag(f, 10, 5, 3) == 1);
        CHECK(flag(f, 10, 7, 8) == 1);   // (7, 8) -> (5, 0) is a corner (dx = -2, dy = +2)
    }
    // edge rows of a tall map; a 5-row map has only rows 2, 3 flaggable
    {
        Map m(6, 5, PlotState::Visible);
        std::vector<uint8_t> f;
        CHECK(cullFlags(m.in(), f).ok());
        for (int x = 0; x < 6; ++x) CHECK(flag(f, 6, x, 0) == 0 && flag(f, 6, x, 1) == 0 && flag(f, 6, x, 2) == 1 &&
                                         flag(f, 6, x, 3) == 1 && flag(f, 6, x, 4) == 0);
    }
    // negatives
    {
        std::vector<uint8_t> f(3, 1);
        MapInput none;
        CHECK(cullFlags(none, f).code == Err::BadLayout && f.empty());
        Map m(4, 4, PlotState::Visible);
        MapInput z = m.in();
        z.width = 0;
        CHECK(cullFlags(z, f).code == Err::BadLayout);
    }
}

// The t21 census of 3951's captures gives exactly the nine culled plots the captures' FOW CULL draw holds; their quad
// centres (HexToWorld + (32 sx, 32 sy - 128)) equal the captured ones [L, render/evidence/0168/3955/cloud_cb.py].
void testCullCentres()
{
    const CloudWorld w = cloudtest::tinyWorld();
    struct P { int x, y; float cx, cy; };
    const P cap[9] = {{8, 23, -1183.9947f, 176.8700f}, {8, 24, -1215.9932f, 232.2948f}, {9, 23, -1119.9962f, 176.8709f},
                      {9, 24, -1151.9948f, 232.2956f}, {10, 23, -1055.9977f, 176.8716f}, {10, 24, -1087.9963f, 232.2964f},
                      {11, 24, -1023.9978f, 232.2971f}, {11, 25, -991.9981f, 287.7228f}, {12, 25, -927.9997f, 287.7237f}};
    double worst = 0;
    for (const P& p : cap) {
        float hw[2];
        hexToWorldXY(w, p.x, p.y, hw);
        const float cx = w.sx * 32.0f + hw[0], cy = (w.sy * 32.0f + hw[1]) - 128.0f;
        worst = std::fmax(worst, std::fmax(std::fabs(cx - p.cx), std::fabs(cy - p.cy)));
    }
    std::printf("cull centres vs captured: worst %.4f world units\n", worst);
    CHECK(worst < 0.02);   // M_h2w is known to 4-5 digits only
    float hw[2];
    hexToWorldXY(w, 8, 23, hw);
    CHECK(std::fabs(hw[0] + 1215.995f) < 0.01f && std::fabs(hw[1] - 277.158f) < 0.01f);
}

void testPlaneBlocks()
{
    const CloudWorld w = cloudtest::tinyWorld();
    float vp[16];
    int32_t b[4];
    cloudtest::cam1VP(vp);
    CHECK(planeBlocks(vp, w, b).ok());
    std::printf("cam1 blocks [%d, %d) x [%d, %d) (captured: [4, 16) x [20, 32))\n", b[0], b[1], b[2], b[3]);
    CHECK(b[0] == 4 && b[1] == 16 && b[2] == 20 && b[3] == 32);
    cloudtest::cam2VP(vp);
    CHECK(planeBlocks(vp, w, b).ok());
    std::printf("cam2 blocks [%d, %d) x [%d, %d) (captured: [5, 13) x [23, 32))\n", b[0], b[1], b[2], b[3]);
    CHECK(b[0] == 5 && b[1] == 13 && b[2] == 23 && b[3] == 32);
    // the captured plane quads' centres: block (4, 20) -> (-1503.5, 28.145)
    CHECK(std::fabs((static_cast<float>(4 * 64) * w.sx + 32.0f * w.sx) + w.minX + 1503.5f) < 1e-3f);
    CHECK(std::fabs((static_cast<float>(20 * 64) * w.sy + 32.0f * w.sy) + w.minY - 28.145f) < 1e-2f);
    // 0572: at the south and north map edges the rows come from the UNCLAMPED footprint, without a clamp of the block
    // index (stock's plane draw [C]); x keeps the clamped footprint. cam1 moved by whole block rows: T(0, d) x VP shows
    // the world d units south (d > 0) or north (d < 0) of cam1's.
    {
        const float rows = 25.0f * 64.0f * w.sy;   // 25 block rows
        int32_t c1[4];
        cloudtest::cam1VP(vp);
        CHECK(planeBlocks(vp, w, c1).ok());
        auto moved = [&](float d, float* out) {
            float base[16];
            cloudtest::cam1VP(base);
            for (int c = 0; c < 4; ++c) out[12 + c] = base[12 + c] + d * base[4 + c];   // row 3 += d x row 1
            for (int i = 0; i < 12; ++i) out[i] = base[i];
        };
        float south[16], north[16];
        moved(rows, south);
        moved(-rows, north);
        int32_t s[4], n[4];
        CHECK(planeBlocks(south, w, s).ok());
        CHECK(planeBlocks(north, w, n).ok());
        // the old rule's values at these cameras: rows clamped to the world box (by0 0 in the south, by1 = the box's last
        // block row + 2 in the north)
        const int boxTop = static_cast<int>(std::floor((w.maxY - w.minY) / w.sy + 0.5)) / 64 + 2;
        std::printf("south edge blocks [%d, %d) x [%d, %d); north edge [%d, %d) x [%d, %d) (box top + 2 = %d)\n", s[0], s[1],
                    s[2], s[3], n[0], n[1], n[2], n[3], boxTop);
        CHECK(s[2] < 0);                                     // the planes go on south of the world box (was clamped to 0)
        CHECK(s[2] >= c1[2] - 25 && s[2] <= c1[2] - 25 + 1); // 25 rows south (toward zero: one more for a negative)
        CHECK(s[3] >= c1[3] - 25 && s[3] <= c1[3] - 25 + 1);
        CHECK(n[3] > boxTop);                                // and north of it (was the box's rows + 2)
        CHECK(n[3] == c1[3] + 25 && n[2] == c1[2] + 25);
        CHECK(s[0] == c1[0] && s[1] == c1[1] && n[0] == c1[0] && n[1] == c1[1]);   // x unchanged (clamped in x)
        // a view whose footprint leaves the world box in x keeps the clamp in x
        float east[16];
        for (int i = 0; i < 16; ++i) east[i] = vp[i];
        for (int c = 0; c < 4; ++c) east[12 + c] = vp[12 + c] - (w.maxX - w.minX) * 0.8f * vp[0 + c];
        int32_t e[4];
        CHECK(planeBlocks(east, w, e).ok());
        const int boxRight = static_cast<int>(std::floor((w.maxX - w.minX) / w.sx + 0.5)) / 64 + 1;
        std::printf("east edge blocks [%d, %d) (box right + 1 = %d)\n", e[0], e[1], boxRight);
        CHECK(e[1] == boxRight);
    }
    // negatives
    float bad[16];
    cloudtest::cam1VP(bad);
    bad[5] = std::nanf("");
    CHECK(planeBlocks(bad, w, b).code == Err::BadLayout);
    CloudWorld nw = w;
    nw.samplesX = 0;
    CHECK(planeBlocks(vp, nw, b).code == Err::BadLayout);
    nw = w;
    nw.maxX = nw.minX;
    CHECK(planeBlocks(vp, nw, b).code == Err::BadLayout);
    nw = w;
    nw.hexToWorld[0] = std::nanf("");
    CHECK(planeBlocks(vp, nw, b).code == Err::BadLayout);
}

void testRendererRefusals()
{
    CloudRenderer r;
    CHECK(r.cloudBase() == nullptr && r.planeTexture(0) == nullptr);
    CHECK(r.init(nullptr, L"x.exe", L"x").code == Err::BadLayout);
    CloudInput in;
    CHECK(r.draw(nullptr, in).code == Err::NotFound);   // not initialised comes first
    cloudtest::Device d;
    if (!d.create(true)) {
        std::printf("FAIL no D3D11 device (WARP)\n");
        ++g_fail;
        return;
    }
    CHECK(r.draw(d.ctx, in).code == Err::NotFound);
    Status s = r.init(d.dev, L"C:/c5-64bit-work/0168/3955/no-such-dir/CivilizationV_DX11.exe", L"C:/no-such-install");
    CHECK(s.code == Err::Io);
    CHECK(r.cloudBase() == nullptr);
}

} // namespace

int main()
{
    testCullFlags();
    testCullCentres();
    testPlaneBlocks();
    testRendererRefusals();
    std::printf("%s cloud (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
