// render/tests/farmfields/farmfields_test.cpp - c5r::farmfields (epic 0528, ticket 4851): the producer's frame.
//
// Unit tests on inputs the tests make (no game files, D83): the names, the seed and the centre, the input's checks, the
// placeholder layout through the whole chain, determinism (same input, same bytes), the vertex form (a fan stored as a
// list, binary16 positions in the rectangle), every invariant of checkLayout and checkDecals with a negative test that
// checks the status, the invariant named and the outputs, the records through decal::buildIndex / decalsForTile and the
// EXE's walk selection, the compose into a tap set (its farm records dropped, the walk's ranks closed up and extended,
// the area block), the change between two productions, and the compose's refusals.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "c5r/bake/grid.h"
#include "c5r/decal/decal.h"
#include "c5r/farmfields/farmfields.h"
#include "c5r/forest/crtrand.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::farmfields;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// farm_types.xml's shape: Half_Built 1 (no spec), Africa 3 (child 1 with spec), America 2, Asia 0, Europe 1, Wheat 2,
// Wheat_HB 1, Damage 1.
FarmTypes types()
{
    FarmTypes t;
    t.hasSpec[kHalfBuilt] = {0};
    t.hasSpec[kAfrica] = {0, 1, 0};
    t.hasSpec[kAmerica] = {0, 0};
    t.hasSpec[kEurope] = {1};
    t.hasSpec[kWheat] = {0, 0};
    t.hasSpec[kWheatHalfBuilt] = {0};
    t.hasSpec[kDamage] = {0};
    return t;
}

FarmPlot plot(int32_t x, int32_t y, FarmState s, uint8_t art)
{
    FarmPlot p;
    p.x = x;
    p.y = y;
    p.state = s;
    p.artType = art;
    return p;
}

// A 16 x 12 map (samples 1024 x 1024 with the 4 margin rows), seven farms of every state and art type.
LayoutInput input(uint32_t seed = 12345)
{
    LayoutInput in;
    in.gameSeed = seed;
    in.mapW = 16;
    in.mapH = 12;
    in.worldRect[0] = 0;
    in.worldRect[1] = 0;
    in.worldRect[2] = 1023;
    in.worldRect[3] = 1023;
    in.types = types();
    in.plots = {plot(1, 1, FarmState::Built, 3),    plot(2, 1, FarmState::Built, 1), plot(5, 2, FarmState::HalfBuilt, 0),
                plot(0, 3, FarmState::Pillaged, 4), plot(7, 3, FarmState::Built, 2), plot(3, 6, FarmState::Built, 4),
                plot(15, 11, FarmState::Built, 0)};
    return in;
}

// The frame's tests run on a fixed, simple layout (4851's placeholder, kept here as a fixture since 4852 replaced it
// with the real layout, tested in layout_test.cpp): every farm plot its own group; one square field of 40 x 40 samples
// on the plot's centre (clamped to the world), its category from the state (half built -> Crops_Half_Built, pillaged ->
// Crops_Damage, built -> the continent's crops by art type, Crops_Wheat for none or a category without children), its
// child and a quarter turn from the plot's stream; one track strip of 3 samples along the field's lower edge.
void squareLayout(const LayoutInput& in, FarmLayout& out)
{
    out = FarmLayout();
    const float half = 20.0f, trackW = 3.0f, uvScale = 1.0f / 32.0f;
    const float* w = in.worldRect;
    for (uint32_t i = 0; i < in.plots.size(); ++i) {
        const FarmPlot& p = in.plots[i];
        const uint32_t g = static_cast<uint32_t>(out.groups.size());
        FarmGroup grp;
        grp.plots.push_back(i);
        out.groups.push_back(grp);
        forest::CrtRand rng(plotSeed(in.gameSeed, p.x, p.y));
        uint32_t cat = kWheat;
        if (p.state == FarmState::HalfBuilt) cat = kHalfBuilt;
        else if (p.state == FarmState::Pillaged) cat = kDamage;
        else if (p.artType == 1) cat = kAmerica;
        else if (p.artType == 2) cat = kAsia;
        else if (p.artType == 3) cat = kAfrica;
        else if (p.artType == 4) cat = kEurope;
        if (!in.types.children(cat)) cat = kWheat;
        const uint32_t n = in.types.children(cat);
        if (!n) continue;
        const uint32_t child = static_cast<uint32_t>(rng.next()) % n;
        const int turn = rng.next() % 4;
        float c[2];
        plotCentre(p.x, p.y, c);
        const float x0 = (std::max)(w[0], c[0] - half), x1 = (std::min)(w[2], c[0] + half);
        const float y0 = (std::max)(w[1], c[1] - half), y1 = (std::min)(w[3], c[1] + half);
        if (!(x0 < x1 && y0 < y1)) continue;
        FieldPolygon f;
        f.plot = i;
        f.group = g;
        f.category = cat;
        f.child = child;
        f.corners = {x0, y0, x1, y0, x1, y1, x0, y1};
        for (size_t k = 0; k < 4; ++k) {
            const float u = (f.corners[2 * k] - c[0]) * uvScale, v = (f.corners[2 * k + 1] - c[1]) * uvScale;
            const float r[4][2] = {{u, v}, {-v, u}, {-u, -v}, {v, -u}};
            f.uv.push_back(r[turn][0]);
            f.uv.push_back(r[turn][1]);
        }
        out.fields.push_back(std::move(f));
        const float ty0 = (std::max)(w[1], y0 - trackW);
        if (ty0 < y0) {
            TrackStrip t;
            t.plot = i;
            t.group = g;
            const float u0 = 0.0f, u1 = (x1 - x0) * uvScale;
            t.xyuv = {x0, ty0, u0, 0.0f, x1, ty0, u1, 0.0f, x1, y0, u1, 1.0f, x0, ty0, u0, 0.0f, x1, y0, u1, 1.0f, x0, y0, u0, 1.0f};
            out.tracks.push_back(std::move(t));
        }
    }
}

struct Made {
    FarmLayout layout;
    FarmDecals decals;
};
// The fixture's layout through the frame's own steps (checkLayout, makeDecals, checkDecals).
Made made(const LayoutInput& in)
{
    Made m;
    squareLayout(in, m.layout);
    CHECK_OK(checkLayout(in, m.layout));
    CHECK_OK(makeDecals(in, m.layout, m.decals));
    CHECK_OK(checkDecals(in, m.decals));
    return m;
}

bool sameDecals(const FarmDecals& a, const FarmDecals& b)
{
    return a.textures == b.textures && a.owner == b.owner && a.track == b.track && a.decals.size() == b.decals.size() &&
           a.vertices.size() == b.vertices.size() &&
           (a.decals.empty() || std::memcmp(a.decals.data(), b.decals.data(), a.decals.size() * sizeof(decal::Decal)) == 0) &&
           (a.vertices.empty() ||
            std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(decal::DecalVertex)) == 0);
}

void addDecal(decal::DecalSet& s, float x0, float y0, float x1, float y1, float prio, uint32_t t0, uint32_t t1, uint32_t t2,
              uint32_t order)
{
    decal::Decal d;
    d.rect[0] = x0;
    d.rect[1] = y0;
    d.rect[2] = x1;
    d.rect[3] = y1;
    d.priority = prio;
    d.texture[0] = t0;
    d.texture[1] = t1;
    d.texture[2] = t2;
    d.firstVertex = static_cast<uint32_t>(s.vertices.size());
    d.vertexCount = 3;
    d.listOrder = order;
    d.hash = 1000 + order;
    const uint16_t h0 = bake::floatToHalf(0.0f), h1 = bake::floatToHalf(1.0f);
    s.vertices.push_back(decal::DecalVertex{h0, h0, h0, h0});
    s.vertices.push_back(decal::DecalVertex{h1, h0, h1, h0});
    s.vertices.push_back(decal::DecalVertex{h1, h1, h1, h1});
    s.decals.push_back(d);
}

// A tap set over the same world: 0 a ground (other), 1 an EXE farm field, 2 an EXE farm track, 3 a road (other), 4 a
// large ground (other). Names: 0 ground.dds, 1 a farm reference, 2 blank_texture.dds, 3 road.dds.
decal::DecalSet tapSet()
{
    decal::DecalSet s;
    s.generation = 9;
    s.loadSeq = 2;
    s.worldRect[2] = 1023;
    s.worldRect[3] = 1023;
    s.textures = {"ground.dds", "@farm_types.xml/Crops_Africa/0/albedo", "blank_texture.dds", "road.dds"};
    const uint32_t N = decal::kNoTexture;
    addDecal(s, 90, 90, 110, 110, 0.0f, 0, 0, 2, 0);
    addDecal(s, 95, 95, 105, 105, 0.5f, 1, 1, 2, 1);
    addDecal(s, 80, 96, 120, 99, 0.6f, N, N, 2, 2);
    addDecal(s, 290, 290, 310, 310, 0.7f, 3, 3, 2, 3);
    addDecal(s, 0, 0, 600, 40, 0.0f, 0, 0, 2, 4);
    decal::buildIndex(s);
    return s;
}

// The same with the EXE's walk (cells of 256 samples, 4 x 4) and an area block (16 x 16 tiles, the track's tile set).
decal::DecalSet tapSetWithWalkAndArea()
{
    decal::DecalSet s = tapSet();
    decal::DecalWalk& w = s.walk;
    w.origin[0] = w.origin[1] = 0.0f;
    w.inverse[0] = w.inverse[1] = 1.0f / 256.0f;
    w.cols = w.rows = 4;
    // cell 0: decal 1 newest (rank 0), decal 0 (rank 1), decal 2 (rank 2); cell 5: decal 3; large: decal 4
    w.cell = {0, 0, 0, 5, decal::kWalkLarge};
    w.rank = {1, 0, 2, 0, 0};
    for (const decal::Decal& d : s.decals) {
        w.point.push_back((d.rect[0] + d.rect[2]) * 0.5f);
        w.point.push_back((d.rect[1] + d.rect[3]) * 0.5f);
    }
    s.areaTilesX = s.areaTilesY = 16;
    s.areaChanged.assign(256, 0);
    s.areaChanged[1 * 16 + 1] = 1;
    decal::buildIndex(s);
    return s;
}

// The decals of a set whose rectangle overlaps tile (ox, oy)'s query square, by a scan.
std::vector<uint32_t> scan(const decal::DecalSet& s, int ox, int oy)
{
    std::vector<uint32_t> r;
    for (uint32_t i = 0; i < s.decals.size(); ++i) {
        const float* q = s.decals[i].rect;
        if (q[0] <= ox + decal::kTileQueryHi && q[2] >= ox + decal::kTileQueryLo && q[1] <= oy + decal::kTileQueryHi &&
            q[3] >= oy + decal::kTileQueryLo)
            r.push_back(i);
    }
    return r;
}

// Every negative of checkLayout: the status, the invariant, and makeDecals refusing the same layout with empty output.
void expectLayoutBroken(const LayoutInput& in, const FarmLayout& l, Invariant want, const char* part)
{
    Invariant which = Invariant::kNone;
    const Status s = checkLayout(in, l, &which);
    CHECK_ERR(s, Err::BadLayout);
    CHECK(which == want);
    CHECK(has(s.msg, invariantName(want)));
    CHECK(has(s.msg, part));
    FarmDecals d;
    d.textures.push_back("stale");
    CHECK_ERR(makeDecals(in, l, d), Err::BadLayout);
    CHECK(d.decals.empty() && d.vertices.empty() && d.textures.empty() && d.owner.empty());
}

void expectDecalsBroken(const LayoutInput& in, const FarmDecals& d, Invariant want, const char* part)
{
    Invariant which = Invariant::kNone;
    const Status s = checkDecals(in, d, &which);
    CHECK_ERR(s, Err::BadLayout);
    CHECK(which == want);
    CHECK(has(s.msg, invariantName(want)));
    CHECK(has(s.msg, part));
}

} // namespace

// ---- names, seed, centre ----

C5R_TEST(names_round_trip_and_refuse)
{
    CHECK_EQ(std::string(categoryName(kHalfBuilt)), std::string("Crops_Half_Built"));
    CHECK_EQ(std::string(categoryName(kDamage)), std::string("Crops_Damage"));
    CHECK(categoryName(kCategoryCount) == nullptr);
    uint32_t c = 99;
    CHECK(categoryOf("crops_wheat_hb", c) && c == kWheatHalfBuilt);
    CHECK(!categoryOf("Crops_Mars", c));
    const std::string r = farmTypesRef(kAfrica, 2, "albedo");
    CHECK_EQ(r, std::string("@farm_types.xml/Crops_Africa/2/albedo"));
    CHECK(farmTypesRef(kCategoryCount, 0, "albedo").empty());
    uint32_t cat = 0, child = 0;
    std::string attr;
    CHECK(parseFarmTypesRef(r, cat, child, attr) && cat == kAfrica && child == 2 && attr == "albedo");
    CHECK(parseFarmTypesRef("@FARM_TYPES.XML/crops_europe/0/HeightMap", cat, child, attr) && cat == kEurope && child == 0 &&
          attr == "heightmap");
    // refusals: another file, missing parts, an unknown category, a child that is not a number or too large, extra parts
    const char* badRefs[] = {"crops.dds", "@decal_filenames.xml/Crops_Africa/0/albedo", "@farm_types.xml/Crops_Africa/0",
                             "@farm_types.xml//0/albedo", "@farm_types.xml/Crops_Africa//albedo", "@farm_types.xml/Crops_Africa/0/",
                             "@farm_types.xml/Crops_Mars/0/albedo", "@farm_types.xml/Crops_Africa/x1/albedo",
                             "@farm_types.xml/Crops_Africa/2147483648/albedo", "@farm_types.xml/Crops_Africa/0/albedo/x",
                             "@farm_types.xml/"};
    for (const char* b : badRefs) {
        cat = 77;
        CHECK(!parseFarmTypesRef(b, cat, child, attr));
        CHECK_EQ(cat, 77u);   // outputs untouched on a refusal
    }
}

C5R_TEST(seed_is_a_pure_function_of_game_and_plot)
{
    CHECK_EQ(plotSeed(1, 2, 3), plotSeed(1, 2, 3));
    CHECK(plotSeed(1, 2, 3) != plotSeed(2, 2, 3));
    CHECK(plotSeed(1, 2, 3) != plotSeed(1, 3, 2));
    CHECK(plotSeed(1, 2, 3) != plotSeed(1, 2, 4));
    // spread: 64 x 64 plots of one game give distinct seeds
    std::vector<uint32_t> v;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) v.push_back(plotSeed(777, x, y));
    std::sort(v.begin(), v.end());
    CHECK(std::unique(v.begin(), v.end()) == v.end());
    float c[2];
    plotCentre(0, 0, c);
    CHECK_EQ(c[0], 32.0f);
    CHECK_EQ(c[1], 128.0f);
    plotCentre(3, 1, c);   // odd row: half a plot to the right
    CHECK_EQ(c[0], 64.0f * 3 + 64.0f);
    CHECK_EQ(c[1], 192.0f);
    plotCentre(3, 2, c);
    CHECK_EQ(c[0], 64.0f * 3 + 32.0f);
}

// ---- the input ----

C5R_TEST(input_refusals)
{
    CHECK_OK(checkInput(input()));
    struct Case { const char* part; LayoutInput in; };
    std::vector<Case> cases;
    LayoutInput a = input();
    a.mapW = 0;
    cases.push_back({"no plots", a});
    a = input();
    a.worldRect[2] = kNaN;
    cases.push_back({"world rectangle", a});
    a = input();
    a.worldRect[2] = a.worldRect[0];
    cases.push_back({"world rectangle", a});
    a = input();
    a.plots[2].x = 16;
    cases.push_back({"outside", a});
    a = input();
    a.plots[0].y = -1;
    cases.push_back({"outside", a});
    a = input();
    std::swap(a.plots[0], a.plots[1]);
    cases.push_back({"ascending", a});
    a = input();
    a.plots[1] = a.plots[0];
    cases.push_back({"ascending", a});
    a = input();
    a.plots[0].state = static_cast<FarmState>(0);
    cases.push_back({"state 0", a});
    a = input();
    a.plots[0].state = static_cast<FarmState>(4);
    cases.push_back({"state 4", a});
    a = input();
    a.plots[0].artType = 5;
    cases.push_back({"art type 5", a});
    for (const Case& k : cases) {
        const Status s = checkInput(k.in);
        CHECK_ERR(s, Err::BadLayout);
        CHECK(has(s.msg, k.part));
        // the whole chain refuses it, every output empty
        FarmLayout l;
        l.groups.resize(3);
        FarmDecals d;
        d.textures.push_back("stale");
        Invariant which = Invariant::kNone;
        CHECK_ERR(produce(k.in, l, d, &which), Err::BadLayout);
        CHECK(which == Invariant::kInput);
        CHECK(l.groups.empty() && l.fields.empty() && l.tracks.empty());
        CHECK(d.decals.empty() && d.textures.empty() && d.vertices.empty());
        CHECK_ERR(layoutFarms(k.in, l), Err::BadLayout);
        CHECK(l.groups.empty());
        Invariant w2 = Invariant::kNone;
        CHECK_ERR(checkDecals(k.in, FarmDecals(), &w2), Err::BadLayout);
        CHECK(w2 == Invariant::kInput);
    }
}

// ---- the placeholder layout through the chain ----

C5R_TEST(placeholder_layout_passes_every_check)
{
    const LayoutInput in = input();
    Made m = made(in);
    CHECK_EQ(m.layout.groups.size(), in.plots.size());          // placeholder: one group per plot
    CHECK_EQ(m.layout.fields.size(), in.plots.size());          // every category has a child (Asia -> Wheat)
    CHECK_EQ(m.layout.tracks.size(), in.plots.size());
    CHECK_EQ(m.decals.decals.size(), 2 * in.plots.size());
    Invariant which = Invariant::kHash;   // set back to kNone by a check that holds
    CHECK_OK(checkLayout(in, m.layout, &which));
    CHECK(which == Invariant::kNone);
    CHECK_OK(checkDecals(in, m.decals, &which));
    CHECK(which == Invariant::kNone);
    // categories by state and art type (the placeholder's rule)
    const uint32_t want[7] = {kAfrica, kAmerica, kHalfBuilt, kDamage, kWheat /* Asia has no child */, kEurope, kWheat};
    for (size_t i = 0; i < m.layout.fields.size(); ++i) {
        CHECK_EQ(m.layout.fields[i].plot, static_cast<uint32_t>(i));
        CHECK_EQ(m.layout.fields[i].category, want[i]);
    }
    // fields first, then tracks; owners; priorities; list order
    for (size_t i = 0; i < m.decals.decals.size(); ++i) {
        const decal::Decal& d = m.decals.decals[i];
        const bool track = i >= in.plots.size();
        CHECK_EQ(m.decals.track[i], static_cast<uint8_t>(track ? 1 : 0));
        CHECK_EQ(m.decals.owner[i], static_cast<uint32_t>(track ? i - in.plots.size() : i));
        CHECK_EQ(d.priority, track ? 0.6f : 0.5f);
        CHECK_EQ(d.pass, 0u);
        CHECK_EQ(d.listOrder, static_cast<uint32_t>(i));
        CHECK_EQ(d.hash, contentHash(d, m.decals.textures, m.decals.vertices));
    }
    // names: the Europe child has a spec, the others the default
    const decal::Decal& eu = m.decals.decals[5];
    CHECK_EQ(m.decals.textures[eu.texture[0]], std::string("@farm_types.xml/Crops_Europe/0/albedo"));
    CHECK_EQ(m.decals.textures[eu.texture[1]], std::string("@farm_types.xml/Crops_Europe/0/heightmap"));
    CHECK_EQ(m.decals.textures[eu.texture[2]], std::string("@farm_types.xml/Crops_Europe/0/spec"));
    const decal::Decal& hb = m.decals.decals[2];
    CHECK_EQ(m.decals.textures[hb.texture[2]], std::string(kDefaultSpec));
    const decal::Decal& tr = m.decals.decals[in.plots.size()];
    CHECK_EQ(tr.texture[0], decal::kNoTexture);
    CHECK_EQ(tr.texture[1], decal::kNoTexture);
    CHECK_EQ(m.decals.textures[tr.texture[2]], std::string(kDefaultSpec));
}

C5R_TEST(no_children_means_no_fields)
{
    LayoutInput in = input();
    in.types = FarmTypes();
    Made m = made(in);
    CHECK_EQ(m.layout.groups.size(), in.plots.size());
    CHECK(m.layout.fields.empty() && m.layout.tracks.empty());
    CHECK(m.decals.empty());
    // and no plots at all: an empty, valid layout
    in = input();
    in.plots.clear();
    Made e = made(in);
    CHECK(e.layout.groups.empty() && e.decals.empty());
}

C5R_TEST(determinism_same_input_same_bytes)
{
    const LayoutInput in = input(4242);
    Made a = made(in), b = made(in);
    CHECK(sameDecals(a.decals, b.decals));
    // a plot's records depend on the game's seed and that plot only: removing another plot leaves them byte-equal
    LayoutInput less = in;
    less.plots.erase(less.plots.begin() + 1);
    Made c = made(less);
    const decal::Decal& d0 = a.decals.decals[0];
    const decal::Decal& c0 = c.decals.decals[0];
    CHECK_EQ(d0.hash, c0.hash);
    CHECK(std::memcmp(&a.decals.vertices[d0.firstVertex], &c.decals.vertices[c0.firstVertex], d0.vertexCount * 8) == 0);
    // another game's seed changes some choice of some plot (child or turn)
    bool differs = false;
    for (uint32_t s = 1; s < 40 && !differs; ++s) {
        Made o = made(input(4242 + s));
        for (size_t i = 0; i < o.decals.decals.size(); ++i) differs = differs || o.decals.decals[i].hash != a.decals.decals[i].hash;
    }
    CHECK(differs);
}

C5R_TEST(vertex_form_fan_as_list_in_the_rectangle)
{
    const LayoutInput in = input();
    Made m = made(in);
    const FieldPolygon& f = m.layout.fields[0];
    const decal::Decal& d = m.decals.decals[0];
    const size_t nc = f.corners.size() / 2;
    CHECK_EQ(d.vertexCount, static_cast<uint32_t>(3 * (nc - 2)));
    // the rectangle is the corners' box
    float lo[2] = {f.corners[0], f.corners[1]}, hi[2] = {f.corners[0], f.corners[1]};
    for (size_t k = 1; k < nc; ++k)
        for (int a = 0; a < 2; ++a) {
            lo[a] = (std::min)(lo[a], f.corners[2 * k + a]);
            hi[a] = (std::max)(hi[a], f.corners[2 * k + a]);
        }
    CHECK_EQ(d.rect[0], lo[0]);
    CHECK_EQ(d.rect[1], lo[1]);
    CHECK_EQ(d.rect[2], hi[0]);
    CHECK_EQ(d.rect[3], hi[1]);
    // every triangle repeats the first corner; positions (p - min) x (1 / extent) as binary16; uv as binary16
    const float ix = 1.0f / (d.rect[2] - d.rect[0]), iy = 1.0f / (d.rect[3] - d.rect[1]);
    for (size_t t = 0; t < nc - 2; ++t) {
        const size_t idx[3] = {0, t + 1, t + 2};
        for (int k = 0; k < 3; ++k) {
            const decal::DecalVertex& v = m.decals.vertices[d.firstVertex + 3 * t + k];
            const size_t c = idx[k];
            CHECK_EQ(v.px, bake::floatToHalf((f.corners[2 * c] - d.rect[0]) * ix));
            CHECK_EQ(v.py, bake::floatToHalf((f.corners[2 * c + 1] - d.rect[1]) * iy));
            CHECK_EQ(v.u, bake::floatToHalf(f.uv[2 * c]));
            CHECK_EQ(v.v, bake::floatToHalf(f.uv[2 * c + 1]));
        }
    }
    // the box's corners are 0 and 1 exactly
    bool zero = false, one = false;
    for (uint32_t k = 0; k < d.vertexCount; ++k) {
        zero = zero || bake::halfToFloat(m.decals.vertices[d.firstVertex + k].px) == 0.0f;
        one = one || bake::halfToFloat(m.decals.vertices[d.firstVertex + k].px) == 1.0f;
    }
    CHECK(zero && one);
}

C5R_TEST(edge_plots_are_clamped_to_the_world)
{
    LayoutInput in = input();
    // plot (15, 11), an odd row's last plot: centre (1024, 832), its field 1004..1044 x 812..852
    in.worldRect[2] = 1010.0f;
    in.worldRect[3] = 840.0f;
    Made m = made(in);
    CHECK_EQ(m.layout.fields.size(), static_cast<size_t>(7));
    const decal::Decal& d = m.decals.decals[6];
    CHECK_EQ(d.rect[0], 1004.0f);
    CHECK_EQ(d.rect[2], 1010.0f);
    CHECK_EQ(d.rect[3], 840.0f);
    CHECK_OK(checkDecals(in, m.decals));
    // a plot whose field lies wholly outside the world gets none (fieldless), the rest are unchanged
    in.worldRect[2] = 1000.0f;
    Made n = made(in);
    CHECK_EQ(n.layout.fields.size(), static_cast<size_t>(6));
    CHECK_EQ(n.layout.groups.size(), static_cast<size_t>(7));
    CHECK_EQ(n.decals.decals[0].hash, m.decals.decals[0].hash);
}

// ---- checkLayout's invariants, one negative each ----

C5R_TEST(layout_invariants)
{
    const LayoutInput in = input();
    const Made good = made(in);
    FarmLayout l;
    {   // input
        LayoutInput bad = in;
        bad.plots[0].x = 99;
        Invariant which = Invariant::kNone;
        CHECK_ERR(checkLayout(bad, good.layout, &which), Err::BadLayout);
        CHECK(which == Invariant::kInput);
    }
    l = good.layout;
    l.groups[2].plots.clear();
    expectLayoutBroken(in, l, Invariant::kGroup, "is empty");
    l = good.layout;
    l.groups[2].plots[0] = 99;
    expectLayoutBroken(in, l, Invariant::kGroup, "outside");
    l = good.layout;
    l.groups[2].plots = {3, 2};
    expectLayoutBroken(in, l, Invariant::kGroup, "ascending");
    l = good.layout;
    l.groups[2].plots = {1};   // plot 1 is in group 1 too
    expectLayoutBroken(in, l, Invariant::kGroup, "groups 1 and 2");
    l = good.layout;
    l.fields[0].plot = 99;
    expectLayoutBroken(in, l, Invariant::kOwner, "outside the input");
    l = good.layout;
    l.tracks[1].group = 99;
    expectLayoutBroken(in, l, Invariant::kOwner, "outside the layout");
    l = good.layout;
    l.fields[0].group = 1;
    expectLayoutBroken(in, l, Invariant::kOwner, "not in its group");
    l = good.layout;
    l.fields[0].category = 8;
    expectLayoutBroken(in, l, Invariant::kCategory, "category 8");
    l = good.layout;
    l.fields[0].category = kAsia;   // Asia has no child
    l.fields[0].child = 0;
    expectLayoutBroken(in, l, Invariant::kCategory, "Crops_Asia");
    l = good.layout;
    l.fields[1].child = 2;          // America has 2
    expectLayoutBroken(in, l, Invariant::kCategory, "child 2");
    l = good.layout;
    l.fields[0].corners.resize(4);
    l.fields[0].uv.resize(4);
    expectLayoutBroken(in, l, Invariant::kShape, "field 0");
    l = good.layout;
    l.fields[0].uv.pop_back();
    expectLayoutBroken(in, l, Invariant::kShape, "uv floats");
    l = good.layout;
    l.fields[0].corners.push_back(1.0f);   // odd count
    expectLayoutBroken(in, l, Invariant::kShape, "corner floats");
    l = good.layout;
    l.tracks[0].xyuv.pop_back();
    expectLayoutBroken(in, l, Invariant::kShape, "track 0");
    l = good.layout;
    l.tracks[0].xyuv.clear();
    expectLayoutBroken(in, l, Invariant::kShape, "track 0");
    l = good.layout;
    l.tracks[0].xyuv.assign(12 * (kMaxDecalVertices / 3 + 1), 100.0f);
    expectLayoutBroken(in, l, Invariant::kShape, "track 0");
    l = good.layout;
    l.fields[0].corners[3] = kNaN;
    expectLayoutBroken(in, l, Invariant::kFinite, "field 0");
    l = good.layout;
    l.fields[0].uv[1] = std::numeric_limits<float>::infinity();
    expectLayoutBroken(in, l, Invariant::kFinite, "uv");
    l = good.layout;
    l.tracks[0].xyuv[2] = kNaN;            // a track's uv
    expectLayoutBroken(in, l, Invariant::kFinite, "track 0");
    l = good.layout;
    l.fields[3].corners[0] = -1.0f;
    expectLayoutBroken(in, l, Invariant::kInsideWorld, "field 3");
    l = good.layout;
    l.tracks[2].xyuv[1] = 1024.0f;
    expectLayoutBroken(in, l, Invariant::kInsideWorld, "track 2");
    l = good.layout;
    for (size_t k = 0; k < l.fields[0].corners.size(); k += 2) l.fields[0].corners[k] = 100.0f;   // all on one x
    expectLayoutBroken(in, l, Invariant::kEmptyRect, "field 0");
}

// ---- checkDecals' invariants, one negative each ----

C5R_TEST(decal_invariants)
{
    const LayoutInput in = input();
    const Made good = made(in);
    const size_t F = 0, T = in.plots.size();   // a field and a track
    FarmDecals d;
    auto rehash = [](FarmDecals& x, size_t i) { x.decals[i].hash = contentHash(x.decals[i], x.textures, x.vertices); };

    d = good.decals;
    d.owner.pop_back();
    expectDecalsBroken(in, d, Invariant::kTables, "owners");
    d = good.decals;
    d.track.push_back(0);
    expectDecalsBroken(in, d, Invariant::kTables, "track flags");
    d = good.decals;
    d.decals[F].vertexCount = 0;
    expectDecalsBroken(in, d, Invariant::kVertexRun, "decal 0");
    d = good.decals;
    d.decals[F].vertexCount = 4;
    expectDecalsBroken(in, d, Invariant::kVertexRun, "decal 0");
    d = good.decals;
    d.decals[T].firstVertex = static_cast<uint32_t>(d.vertices.size());
    expectDecalsBroken(in, d, Invariant::kVertexRun, "decal 7");
    d = good.decals;
    d.decals[F].vertexCount = kMaxDecalVertices + 3;
    expectDecalsBroken(in, d, Invariant::kVertexRun, "decal 0");
    d = good.decals;
    d.decals[F].rect[1] = kNaN;
    expectDecalsBroken(in, d, Invariant::kFinite, "rectangle is not finite");
    d = good.decals;
    d.decals[F].rect[2] = d.decals[F].rect[0];
    expectDecalsBroken(in, d, Invariant::kEmptyRect, "empty rectangle");
    d = good.decals;
    d.decals[F].rect[3] = 2000.0f;
    expectDecalsBroken(in, d, Invariant::kInsideWorld, "not inside the world");
    d = good.decals;
    d.decals[F].priority = 0.6f;
    expectDecalsBroken(in, d, Invariant::kPriority, "field");
    d = good.decals;
    d.decals[T].priority = 0.5f;
    expectDecalsBroken(in, d, Invariant::kPriority, "track");
    d = good.decals;
    d.decals[F].pass = 1;
    expectDecalsBroken(in, d, Invariant::kPriority, "pass 1");
    // texture names
    d = good.decals;
    d.decals[F].texture[0] = d.decals[F].texture[1];   // the heightmap reference as albedo
    expectDecalsBroken(in, d, Invariant::kTexture, "albedo");
    d = good.decals;
    d.textures[d.decals[F].texture[0]] = "Crops_Africa_01_D.dds";   // a plain name
    expectDecalsBroken(in, d, Invariant::kTexture, "albedo");
    d = good.decals;
    d.decals[F].texture[0] = decal::kNoTexture;
    expectDecalsBroken(in, d, Invariant::kTexture, "albedo");
    {   // a child farm_types.xml lacks
        LayoutInput fewer = in;
        fewer.types.hasSpec[kAfrica].resize(0);
        fewer.types.hasSpec[kAfrica].push_back(0);   // Africa with 1 child; the first field names Africa's child
        d = good.decals;
        uint32_t cat = 0, child = 0;
        std::string attr;
        CHECK(parseFarmTypesRef(d.textures[d.decals[F].texture[0]], cat, child, attr));
        if (child > 0) expectDecalsBroken(fewer, d, Invariant::kTexture, "lacks");
        fewer.types.hasSpec[kAfrica].clear();
        expectDecalsBroken(fewer, d, Invariant::kTexture, "lacks");
    }
    d = good.decals;
    d.decals[F].texture[1] = d.decals[5].texture[1];   // another child's heightmap
    expectDecalsBroken(in, d, Invariant::kTexture, "heightmap");
    d = good.decals;
    d.decals[5].texture[2] = d.decals[F].texture[2];   // Europe 0 has a spec: the default is wrong there
    expectDecalsBroken(in, d, Invariant::kTexture, "has one");
    d = good.decals;
    d.decals[2].texture[2] = d.decals[2].texture[0];   // half built: no spec attribute, the default needed
    expectDecalsBroken(in, d, Invariant::kTexture, "default");
    d = good.decals;
    d.decals[T].texture[0] = d.decals[F].texture[0];
    expectDecalsBroken(in, d, Invariant::kTexture, "kNoTexture");
    d = good.decals;
    d.decals[T].texture[2] = decal::kNoTexture;
    expectDecalsBroken(in, d, Invariant::kTexture, "spec must be");
    // a vertex outside its rectangle (1.5) and a NaN position
    d = good.decals;
    d.vertices[d.decals[F].firstVertex + 1].px = bake::floatToHalf(1.5f);
    expectDecalsBroken(in, d, Invariant::kVertexInRect, "vertex 1");
    d = good.decals;
    d.vertices[d.decals[T].firstVertex + 2].py = 0x7E00;   // binary16 NaN
    expectDecalsBroken(in, d, Invariant::kVertexInRect, "vertex 2");
    d = good.decals;
    d.vertices[d.decals[T].firstVertex].py = bake::floatToHalf(-0.25f);
    expectDecalsBroken(in, d, Invariant::kVertexInRect, "vertex 0");
    // the owner: a plot of the input
    d = good.decals;
    d.owner[3] = static_cast<uint32_t>(in.plots.size());
    expectDecalsBroken(in, d, Invariant::kOwner, "not a farm plot");
    // the hash, then the list order (a rehashed record so that only the order is wrong)
    d = good.decals;
    d.decals[4].hash ^= 1;
    expectDecalsBroken(in, d, Invariant::kHash, "decal 4");
    d = good.decals;
    d.vertices[d.decals[4].firstVertex].u ^= 1;   // a uv bit: content changed, hash stale
    expectDecalsBroken(in, d, Invariant::kHash, "decal 4");
    d = good.decals;
    d.decals[4].listOrder = 40;
    rehash(d, 4);
    expectDecalsBroken(in, d, Invariant::kListOrder, "list order 40");
    // and the good one still holds
    CHECK_OK(checkDecals(in, good.decals));
}

// ---- the records in the decal set: buildIndex, decalsForTile, the EXE's selection ----

C5R_TEST(records_through_build_index_and_decals_for_tile)
{
    const LayoutInput in = input();
    const Made m = made(in);
    decal::DecalSet empty;
    empty.generation = 1;
    std::memcpy(empty.worldRect, in.worldRect, sizeof empty.worldRect);
    decal::DecalSet s;
    ComposeStats st;
    CHECK_OK(composeFarmSet(empty, true, m.decals, nullptr, s, &st));
    CHECK_EQ(st.farmDecals, static_cast<uint32_t>(m.decals.decals.size()));
    CHECK(s.grid.cols > 0);   // buildIndex made a grid
    size_t seen = 0;
    for (int oy = 0; oy < 1024; oy += 64)
        for (int ox = 0; ox < 1024; ox += 64) {
            const decal::TileDecalList list = decal::decalsForTile(s, ox, oy);
            std::vector<uint32_t> got = list.indices, want = scan(s, ox, oy);
            seen += got.size();
            std::sort(got.begin(), got.end());
            CHECK(got == want);
            // draw order: ascending priority (every field before every track of the tile)
            for (size_t k = 1; k < list.indices.size(); ++k)
                CHECK(s.decals[list.indices[k - 1]].priority <= s.decals[list.indices[k]].priority);
        }
    CHECK(seen >= m.decals.decals.size());
    // each decal is in the list of the tile holding its rectangle's centre
    for (uint32_t i = 0; i < s.decals.size(); ++i) {
        const float cx = (s.decals[i].rect[0] + s.decals[i].rect[2]) * 0.5f, cy = (s.decals[i].rect[1] + s.decals[i].rect[3]) * 0.5f;
        const int ox = 64 * static_cast<int>(cx / 64), oy = 64 * static_cast<int>(cy / 64);
        const std::vector<uint32_t> l = decal::decalsForTile(s, ox, oy).indices;
        CHECK(std::find(l.begin(), l.end(), i) != l.end());
    }
}

// ---- the compose into the tap's set ----

C5R_TEST(compose_drops_the_tap_farm_and_appends)
{
    const LayoutInput in = input();
    const Made m = made(in);
    const decal::DecalSet tap = tapSet();
    const decal::DecalSet keep = tap;
    CHECK(tapClass(tap, 0) == TapClass::kOther);
    CHECK(tapClass(tap, 1) == TapClass::kField);
    CHECK(tapClass(tap, 2) == TapClass::kTrack);
    CHECK(tapClass(tap, 3) == TapClass::kOther);
    CHECK(tapClass(tap, 99) == TapClass::kOther);
    const TapFarmCount c = tapFarmCount(tap);
    CHECK_EQ(c.fields, static_cast<size_t>(1));
    CHECK_EQ(c.tracks, static_cast<size_t>(1));

    // asked not to drop them: refused, output empty
    decal::DecalSet out;
    out.decals.resize(2);
    ComposeStats st;
    st.farmDecals = 5;
    CHECK_ERR(composeFarmSet(tap, false, m.decals, nullptr, out, &st), Err::Unsupported);
    CHECK(out.decals.empty() && out.vertices.empty());
    CHECK_EQ(st.farmDecals, 0u);

    CHECK_OK(composeFarmSet(tap, true, m.decals, nullptr, out, &st));
    CHECK_EQ(tap.decals.size(), keep.decals.size());   // the tap's set is not modified
    CHECK(std::memcmp(tap.decals.data(), keep.decals.data(), tap.decals.size() * sizeof(decal::Decal)) == 0);
    CHECK_EQ(st.tapDecals, 5u);
    CHECK_EQ(st.tapRemoved, 2u);
    CHECK_EQ(st.farmDecals, static_cast<uint32_t>(m.decals.decals.size()));
    CHECK(!st.walk && !st.area);
    CHECK_EQ(out.generation, 9u);
    CHECK_EQ(out.loadSeq, 2u);
    CHECK_EQ(out.decals.size(), 3 + m.decals.decals.size());
    // kept tap decals 0, 3, 4 in order, unchanged
    const size_t keptIdx[3] = {0, 3, 4};
    for (size_t k = 0; k < 3; ++k) CHECK(std::memcmp(&out.decals[k], &tap.decals[keptIdx[k]], sizeof(decal::Decal)) == 0);
    // farm decals: names by text, vertices after the tap's, list order after the tap's largest (4), hash and content kept
    CHECK_EQ(out.textures[2], std::string("blank_texture.dds"));
    for (size_t k = 0; k < m.decals.decals.size(); ++k) {
        const decal::Decal& src = m.decals.decals[k];
        const decal::Decal& d = out.decals[3 + k];
        CHECK_EQ(d.listOrder, static_cast<uint32_t>(5 + k));
        CHECK_EQ(d.hash, src.hash);
        CHECK_EQ(d.firstVertex, static_cast<uint32_t>(tap.vertices.size()) + src.firstVertex);
        CHECK(std::memcmp(d.rect, src.rect, sizeof d.rect) == 0);
        for (int t = 0; t < 3; ++t) {
            if (src.texture[t] == decal::kNoTexture) {
                CHECK_EQ(d.texture[t], decal::kNoTexture);
            } else {
                CHECK(d.texture[t] < out.textures.size());
                CHECK_EQ(out.textures[d.texture[t]], m.decals.textures[src.texture[t]]);
            }
        }
        CHECK(std::memcmp(&out.vertices[d.firstVertex], &m.decals.vertices[src.firstVertex], src.vertexCount * 8) == 0);
        // the content hash in the composed set's own names is the same (names by text)
        CHECK_EQ(contentHash(d, out.textures, out.vertices), src.hash);
    }
    // blank_texture.dds was in the tap's table: reused, not added
    CHECK_EQ(st.namesAdded, static_cast<uint32_t>(m.decals.textures.size() - 1));
    CHECK_EQ(tapFarmCount(out).fields, m.layout.fields.size());   // the server's fields read as farm fields
    CHECK_EQ(tapFarmCount(out).tracks, m.layout.tracks.size());

    // no farm records in the tap and nothing produced: the tap's own decals (removeTapFarm either way)
    decal::DecalSet plain = tapSet();
    plain.decals.erase(plain.decals.begin() + 1, plain.decals.begin() + 3);
    decal::buildIndex(plain);
    CHECK_OK(composeFarmSet(plain, false, FarmDecals(), nullptr, out, &st));
    CHECK_EQ(out.decals.size(), static_cast<size_t>(3));
    CHECK_EQ(st.namesAdded, 0u);
}

C5R_TEST(compose_closes_and_extends_the_walk)
{
    LayoutInput in = input();
    in.plots = {plot(1, 1, FarmState::Built, 3), plot(2, 1, FarmState::Built, 1)};   // both in walk cell 0
    const Made m = made(in);
    const decal::DecalSet tap = tapSetWithWalkAndArea();
    CHECK(decal::hasWalk(tap));
    decal::DecalSet out;
    ComposeStats st;
    std::vector<decal::TileOrigin> marks = {decal::TileOrigin{10, 12}, decal::TileOrigin{99, 0}};
    CHECK_OK(composeFarmSet(tap, true, m.decals, &marks, out, &st));
    CHECK(st.walk && st.area);
    CHECK(decal::hasWalk(out));
    CHECK_EQ(out.decals.size(), static_cast<size_t>(3 + 4));
    // kept: tap 0 (cell 0, was rank 1 -> 0 after the drop, then behind the 4 farm decals of cell 0 -> 4), tap 3 (cell 5,
    // rank 0), tap 4 (large, rank 0)
    CHECK_EQ(out.walk.cell[0], 0u);
    CHECK_EQ(out.walk.cell[1], 5u);
    CHECK_EQ(out.walk.cell[2], decal::kWalkLarge);
    CHECK_EQ(out.walk.rank[1], 0u);
    CHECK_EQ(out.walk.rank[2], 0u);
    uint32_t farmInCell0 = 0;
    for (size_t k = 3; k < out.decals.size(); ++k) {
        const decal::Decal& d = out.decals[k];
        CHECK_EQ(out.walk.point[2 * k], (d.rect[0] + d.rect[2]) * 0.5f);
        CHECK_EQ(out.walk.point[2 * k + 1], (d.rect[1] + d.rect[3]) * 0.5f);
        farmInCell0 += out.walk.cell[k] == 0;
    }
    CHECK_EQ(farmInCell0, 4u);
    CHECK_EQ(out.walk.rank[0], 4u);
    // newest first: the last farm decal given has rank 0, the first rank 3
    CHECK_EQ(out.walk.rank[out.decals.size() - 1], 0u);
    CHECK_EQ(out.walk.rank[3], 3u);
    // the EXE's selection of tile (128, 128) walks cell 0 newest first, then the large list, then sorts by priority:
    // every farm decal of the tile is selected and drawn
    const std::vector<decal::ExeDrawEntry> sel = decal::exeSelection(out, 128, 128, 0);
    size_t farmSel = 0;
    for (const decal::ExeDrawEntry& e : sel) farmSel += e.decal >= 3;
    CHECK_EQ(farmSel, static_cast<size_t>(4));
    const decal::TileDecalList list = decal::decalsForTile(out, 128, 128);
    size_t farmDrawn = 0;
    for (uint32_t i : list.indices) farmDrawn += i >= 3;
    CHECK_EQ(farmDrawn, static_cast<size_t>(4));
    // the area block: the dropped track's bit kept, each farm decal's tiles, the mark inside the grid, the outside one ignored
    CHECK_EQ(out.areaChanged[1 * 16 + 1], static_cast<uint8_t>(1));
    for (size_t k = 3; k < out.decals.size(); ++k) {
        const decal::Decal& d = out.decals[k];
        CHECK_EQ(out.areaChanged[static_cast<int>(d.rect[1] / 64) * 16 + static_cast<int>(d.rect[0] / 64)], static_cast<uint8_t>(1));
    }
    CHECK_EQ(out.areaChanged[12 * 16 + 10], static_cast<uint8_t>(1));
    CHECK(st.areaBitsSet >= 2);
    // a large farm decal goes to the end of the large list
    FarmDecals big = m.decals;
    big.decals.resize(1);
    big.owner.resize(1);
    big.track.resize(1);
    big.decals[0].rect[0] = 10.0f;
    big.decals[0].rect[2] = 400.0f;
    CHECK_OK(composeFarmSet(tap, true, big, nullptr, out, &st));
    CHECK_EQ(st.walkLarge, 1u);
    CHECK_EQ(out.walk.cell[3], decal::kWalkLarge);
    CHECK_EQ(out.walk.rank[3], 1u);   // after the tap's large decal
    CHECK(decal::hasWalk(out));
}

C5R_TEST(compose_refusals)
{
    const LayoutInput in = input();
    const Made m = made(in);
    const decal::DecalSet tap = tapSet();
    struct Case { const char* part; decal::DecalSet t; FarmDecals f; };
    std::vector<Case> cases;
    decal::DecalSet badWorld = tap;
    badWorld.worldRect[3] = kNaN;
    cases.push_back({"world rectangle", badWorld, m.decals});
    FarmDecals f = m.decals;
    f.owner.pop_back();
    cases.push_back({"disagree", tap, f});
    f = m.decals;
    f.decals[2].firstVertex = static_cast<uint32_t>(f.vertices.size());
    cases.push_back({"vertex run", tap, f});
    f = m.decals;
    f.decals[2].vertexCount = 5;
    cases.push_back({"vertex run", tap, f});
    f = m.decals;
    f.decals[1].texture[0] = static_cast<uint32_t>(f.textures.size());
    cases.push_back({"names texture", tap, f});
    f = m.decals;
    f.decals[1].rect[2] = 5000.0f;
    cases.push_back({"rectangle", tap, f});
    f = m.decals;
    f.decals[1].rect[0] = kNaN;
    cases.push_back({"rectangle", tap, f});
    decal::DecalSet late = tap;
    late.decals[0].listOrder = 0xFFFFFFFEu;   // the next order is 2^32 - 1: no room for 14 farm decals
    cases.push_back({"list order", late, m.decals});
    decal::DecalSet broken = tapSetWithWalkAndArea();
    broken.walk.cell[3] = 99;   // a kept decal in a cell outside the 4 x 4 grid (the set's lists were built before)
    cases.push_back({"walk", broken, m.decals});
    for (const Case& k : cases) {
        decal::DecalSet out;
        out.decals.resize(1);
        ComposeStats st;
        st.farmDecals = 9;
        const Status s = composeFarmSet(k.t, true, k.f, nullptr, out, &st);
        CHECK_ERR(s, Err::BadLayout);
        CHECK(has(s.msg, k.part));
        CHECK(out.decals.empty() && out.vertices.empty() && out.textures.empty());
        CHECK_EQ(st.farmDecals, 0u);
    }
}

// ---- the change between two productions ----

C5R_TEST(change_names_only_what_moved)
{
    const LayoutInput in = input();
    const Made a = made(in);
    FarmChange ch;
    CHECK_OK(farmChange(a.decals, a.decals, in.worldRect, ch));
    CHECK(ch.rects.empty() && ch.bakeTiles.empty() && ch.areaTiles.empty());
    CHECK_EQ(ch.added + ch.removed, 0u);
    // plot 4 pillaged: its field changes category (Crops_Damage); its track may stay byte-equal
    LayoutInput p = in;
    p.plots[4].state = FarmState::Pillaged;
    const Made b = made(p);
    CHECK_OK(farmChange(a.decals, b.decals, in.worldRect, ch));
    CHECK(ch.added >= 1 && ch.removed >= 1);
    CHECK_EQ(ch.added, ch.removed);
    float c[2];
    plotCentre(in.plots[4].x, in.plots[4].y, c);
    for (const decal::DecalRect& r : ch.rects) CHECK(r.v[0] <= c[0] && r.v[2] >= c[0] && r.v[1] <= c[1] + 30 && r.v[3] >= c[1] - 30);
    CHECK(!ch.bakeTiles.empty());
    CHECK(!ch.areaTiles.empty());
    // a farm built: only added; then removed: only removed
    LayoutInput q = in;
    q.plots.push_back(plot(12, 11, FarmState::Built, 2));
    std::sort(q.plots.begin(), q.plots.end(), [](const FarmPlot& x, const FarmPlot& y) { return x.y != y.y ? x.y < y.y : x.x < y.x; });
    const Made qm = made(q);
    CHECK_OK(farmChange(a.decals, qm.decals, in.worldRect, ch));
    CHECK_EQ(ch.added, 2u);
    CHECK_EQ(ch.removed, 0u);
    CHECK_OK(farmChange(qm.decals, a.decals, in.worldRect, ch));
    CHECK_EQ(ch.added, 0u);
    CHECK_EQ(ch.removed, 2u);
    // the tiles are sorted and unique
    for (size_t k = 1; k < ch.areaTiles.size(); ++k) {
        const decal::TileOrigin& x = ch.areaTiles[k - 1];
        const decal::TileOrigin& y = ch.areaTiles[k];
        CHECK(x.y < y.y || (x.y == y.y && x.x < y.x));
    }
    // refusals
    float badWorld[4] = {0, 0, kNaN, 10};
    ch.added = 7;
    CHECK_ERR(farmChange(a.decals, b.decals, badWorld, ch), Err::BadLayout);
    CHECK(ch.rects.empty() && ch.added == 0);
    FarmDecals nan = b.decals;
    nan.decals[0].rect[0] = kNaN;
    ch.added = 7;
    CHECK_ERR(farmChange(a.decals, nan, in.worldRect, ch), Err::BadLayout);
    CHECK(ch.rects.empty() && ch.added == 0);
}

// ---- the inputs from the server's tables ----

C5R_TEST(farm_types_from_attributes)
{
    const std::vector<std::pair<std::string, std::string>> attrs = {
        {"crops_africa/0/albedo", "a0.dds"},  {"crops_africa/0/heightmap", "h0.dds"}, {"crops_africa/2/albedo", "a2.dds"},
        {"crops_africa/2/spec", "s2.dds"},    {"crops_europe/0/spec", ""},            {"crops_europe/0/albedo", "e.dds"},
        {"detail_road/0/priority", "0.6"},    {"crops_wheat/x/albedo", "bad"},        {"crops_wheat/99999/albedo", "bad"},
        {"crops_wheat", "bad"},               {"crops_damage/1/albedo", "d1.dds"}};
    const FarmTypes t = farmTypesFromAttributes(attrs);
    CHECK_EQ(t.children(kAfrica), 3u);   // children 0..2 (1 named by nothing: the largest index counts)
    CHECK_EQ(t.hasSpec[kAfrica][2], static_cast<uint8_t>(1));
    CHECK_EQ(t.hasSpec[kAfrica][0], static_cast<uint8_t>(0));
    CHECK_EQ(t.children(kEurope), 1u);
    CHECK_EQ(t.hasSpec[kEurope][0], static_cast<uint8_t>(0));   // an empty spec is none
    CHECK_EQ(t.children(kWheat), 0u);                            // the malformed keys are ignored
    CHECK_EQ(t.children(kDamage), 2u);
    CHECK_EQ(t.children(kCategoryCount), 0u);
}

C5R_TEST(farm_plot_of_a_record)
{
    FarmPlot p;
    p.x = -9;
    CHECK(!farmPlotOf(1, 2, -1, 3, 2, 0, 3, 1, false, p));   // id not known
    CHECK(!farmPlotOf(1, 2, 3, 4, 2, 0, 3, 1, false, p));    // another improvement
    CHECK(!farmPlotOf(1, 2, 3, 3, 2, 0, 2, 1, true, p));     // not revealed
    CHECK_EQ(p.x, -9);                                        // untouched
    CHECK(farmPlotOf(1, 2, 3, 3, 2, 0, 2, 1, false, p));
    CHECK(p.x == 1 && p.y == 2 && p.state == FarmState::Built && p.artType == 1);
    CHECK(farmPlotOf(1, 2, 3, 3, -1, 0, 1, 2, true, p) && p.state == FarmState::Built);   // no event seen yet
    CHECK(farmPlotOf(1, 2, 3, 3, 1, 0, 1, 2, true, p) && p.state == FarmState::HalfBuilt);
    CHECK(farmPlotOf(1, 2, 3, 3, 3, 0, 1, 2, true, p) && p.state == FarmState::Pillaged);
    CHECK(farmPlotOf(1, 2, 3, 3, 2, 2, 1, 2, true, p) && p.state == FarmState::Pillaged);   // flags bit 1
    CHECK(farmPlotOf(1, 2, 3, 3, 2, 0, 1, 9, true, p) && p.artType == 0);                  // art type outside 0..4
}

// ---- the producer's state across a load ----

C5R_TEST(producer_updates_changes_marks_and_compose_cache)
{
    Producer pr;
    LayoutInput in = input();
    bool changed = false;
    CHECK_OK(pr.update(in, changed));
    CHECK(changed);
    CHECK_EQ(pr.productions(), static_cast<uint64_t>(1));
    CHECK(!pr.decals().decals.empty());   // the real layout (layoutFarms)
    const uint64_t v1 = pr.version();
    FarmChange ch;
    CHECK(pr.takeChange(ch));
    CHECK_EQ(ch.rects.size(), pr.decals().decals.size());   // the first production: every decal added
    CHECK(!ch.bakeTiles.empty());
    CHECK(!pr.takeChange(ch));
    CHECK(ch.rects.empty());
    const size_t marks1 = pr.marks().size();
    CHECK(marks1 > 0);
    // the same input again: nothing
    CHECK_OK(pr.update(in, changed));
    CHECK(!changed);
    CHECK_EQ(pr.productions(), static_cast<uint64_t>(1));
    CHECK_EQ(pr.version(), v1);
    // compose: cached per tap set and version
    std::shared_ptr<const decal::DecalSet> tap = std::make_shared<decal::DecalSet>(tapSet());
    std::shared_ptr<const decal::DecalSet> a, b;
    CHECK_OK(pr.compose(tap, true, a));
    CHECK_OK(pr.compose(tap, true, b));
    CHECK(a == b && a != tap);
    // refused compose: the tap's set itself
    CHECK_ERR(pr.compose(tap, false, b), Err::Unsupported);
    CHECK(b == tap);
    CHECK_ERR(pr.compose(nullptr, true, b), Err::BadLayout);
    CHECK(!b);
    // a farm pillaged: changed, a change with that plot's decals, the marks keep the old tiles
    in.plots[0].state = FarmState::Pillaged;
    CHECK_OK(pr.update(in, changed));
    CHECK(changed);
    CHECK(pr.version() > v1);
    CHECK(pr.takeChange(ch));
    CHECK(!ch.rects.empty());
    {   // only the pillaged farm's own neighbourhood: every changed rectangle within a plot of its centre
        float pc[2];
        plotCentre(in.plots[0].x, in.plots[0].y, pc);
        for (const decal::DecalRect& r : ch.rects)
            CHECK(r.v[0] < pc[0] + 96 && r.v[2] > pc[0] - 96 && r.v[1] < pc[1] + 96 && r.v[3] > pc[1] - 96);
    }
    CHECK(pr.marks().size() >= marks1);
    CHECK_OK(pr.compose(tap, true, b));
    CHECK(b != a);   // a new version, a new set
    // a failing input: Status returned, counted, the decals in force kept
    LayoutInput bad = in;
    bad.plots[1].artType = 7;
    const uint64_t v2 = pr.version();
    const FarmDecals keep = pr.decals();
    CHECK_ERR(pr.update(bad, changed), Err::BadLayout);
    CHECK(!changed);
    CHECK_EQ(pr.failures(), static_cast<uint64_t>(1));
    CHECK_EQ(pr.version(), v2);
    CHECK(sameDecals(keep, pr.decals()));
    CHECK(!pr.takeChange(ch));
    // a new load: everything dropped, the next update produces again
    pr.newLoad();
    CHECK(pr.decals().empty() && pr.marks().empty());
    CHECK(!pr.takeChange(ch));
    CHECK_OK(pr.update(in, changed));
    CHECK(changed);
    CHECK(sameInput(in, in));
    LayoutInput other = in;
    other.types.hasSpec[kWheat].push_back(1);
    CHECK(!sameInput(in, other));
    other = in;
    other.worldRect[3] = 1000.0f;
    CHECK(!sameInput(in, other));
}

// ---- the span text (the scene bundle's render_farmfields_state, ticket 4852) ----

C5R_TEST(span_text_round_trip_and_refusals)
{
    LayoutInput in = input(77);
    in.plots[1].wheat = true;
    in.worldRect[2] = 1023.25f;   // a value that only an exact form keeps
    const std::string t = spanText(in, 3, 7);
    CHECK(t.compare(0, 4, "fv1 ") == 0);
    LayoutInput back;
    back.types = in.types;   // the text carries no types
    int f = 0, w = 0;
    CHECK_OK(parseSpanText(t, back, f, w));
    back.types = in.types;
    CHECK(sameInput(in, back));
    CHECK(f == 3 && w == 7);
    CHECK_EQ(spanText(back, 3, 7), t);
    // no farms
    LayoutInput none = in;
    none.plots.clear();
    CHECK_OK(parseSpanText(spanText(none, -1, -1), back, f, w));
    CHECK(back.plots.empty() && f == -1);
    // refusals leave the outputs as they were
    back = in;
    f = 42;
    const char* bad[] = {"", "nope", "fv2 seed=1", "fv1 seed=1",
                         "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.0.1.0",
                         "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.2.5.0",
                         "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.2.1.2",
                         "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.2.1.0,",
                         "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=2.1.2.1.0,1.1.2.1.0",
                         "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1"};
    for (const char* b : bad) {
        const Status s = parseSpanText(b, back, f, w);
        CHECK(!s.ok());
        CHECK(sameInput(back, in) && f == 42);
    }
    CHECK_ERR(parseSpanText("fv2 seed=1", back, f, w), Err::BadVersion);
    CHECK_ERR(parseSpanText("fv1 seed=1", back, f, w), Err::BadLayout);
}

C5R_TEST_MAIN
