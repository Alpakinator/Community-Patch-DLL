// placement.cpp - L3 of the unit module (epic 0041, U3, issue 4354): formation slots, facing, deaths, plot centre,
// heights. The rule is research/05-graphics-composition/unit-formations.md sections 3, 7, 11.2, 12 (written in our own
// words from the EXE's behaviour, D83); the reference model is models/tools/formations/formations.py. Own code.
#include <algorithm>
#include <cmath>
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"

namespace c5r { namespace units {

namespace {

const float kRandomFactor = 0.4f;                 // amplitude = UnitRandomness x 0.4 x spacing (3.4)
const uint32_t kLcgMul = 214013u, kLcgAdd = 2531011u;
const uint32_t kLcgMod = 0xFFFFCFEFu;             // the state is reduced modulo this after every step (3.4)
const float kHalfPi = 1.5707950592041016f;        // the EXE's single-precision constant for pi / 2 (3.3)
const int32_t kUnbounded = 0x1FFFFFFF;            // a block size of 0 means one block (3.1)

bool finite(float v) { return std::isfinite(v); }

// issue 4982: the terrain surface between samples (bilinear on the same samples heightAtWorld reads)
float surfaceAt(const heightfield::HeightGrid& g, float x, float y)
{
    const float fc = (x + g.ox) / g.sx, fr = (y + g.oy) / g.sy;
    if (!(std::fabs(fc) < 1e9f) || !(std::fabs(fr) < 1e9f)) return 0.0f;
    const float c0f = std::floor(fc), r0f = std::floor(fr);
    const int c0 = static_cast<int>(c0f), r0 = static_cast<int>(r0f);
    const float tx = fc - c0f, ty = fr - r0f;
    const float h00 = heightfield::heightAtSample(g, c0, r0), h10 = heightfield::heightAtSample(g, c0 + 1, r0);
    const float h01 = heightfield::heightAtSample(g, c0, r0 + 1), h11 = heightfield::heightAtSample(g, c0 + 1, r0 + 1);
    return (h00 * (1 - tx) + h10 * tx) * (1 - ty) + (h01 * (1 - tx) + h11 * tx) * ty;
}

// issue 4982: the highest surface under a figure's feet: its origin, 12 points at r and 6 at r / 2 (half a step turned)
float footBase(const heightfield::HeightGrid& g, float x, float y, float r)
{
    float m = surfaceAt(g, x, y);
    for (int k = 0; k < 12; ++k) {
        const float a = 6.2831853f * static_cast<float>(k) / 12.0f;
        m = std::max(m, surfaceAt(g, x + r * std::cos(a), y + r * std::sin(a)));
    }
    for (int k = 0; k < 6; ++k) {
        const float a = 6.2831853f * (static_cast<float>(k) + 0.5f) / 6.0f;
        m = std::max(m, surfaceAt(g, x + 0.5f * r * std::cos(a), y + 0.5f * r * std::sin(a)));
    }
    return m;
}

}  // namespace

float FormationRng::next()
{
    state = state * kLcgMul + kLcgAdd;   // mod 2^32 by unsigned wrap
    state %= kLcgMod;
    return static_cast<float>(static_cast<double>(state) / static_cast<double>(kLcgMod));
}

Status formationSlots(const FormationRecord& rec, uint32_t n, uint32_t seed, bool withRandom, float unitScale,
                      std::vector<Slot>& out)
{
    out.clear();
    if (n == 0 || n > kMaxFigures)
        return Status::fail(Err::BadLayout, "formation " + rec.name + ": member count " + std::to_string(n) +
                                                " outside 1.." + std::to_string(kMaxFigures));
    if (!finite(unitScale) || !finite(rec.spacingMultiplier) || !finite(rec.unitRandomness) || !finite(rec.yOffsetInc))
        return Status::fail(Err::BadLayout, "formation " + rec.name + ": a parameter is not finite");
    int32_t width = rec.totalWidth;
    if (width < 1 || width > 11) width = static_cast<int32_t>(std::ceil(std::sqrt(static_cast<double>(n)))) + 1;
    const int32_t xblock = rec.xBlockSize ? rec.xBlockSize : kUnbounded;
    const int32_t yblock = rec.yBlockSize ? rec.yBlockSize : kUnbounded;
    const int32_t centreW = rec.centerWidth;
    const float sx = static_cast<float>(rec.spacingX) * unitScale * rec.spacingMultiplier;
    const float sy = static_cast<float>(rec.spacingY) * unitScale * rec.spacingMultiplier;
    const float xgap = static_cast<float>(rec.xBlockSpacingCount) * sx;
    const float ygap = static_cast<float>(rec.yBlockSpacingCount) * sy;
    const float halfGap = (centreW ? 0.0f : 1.0f) * xgap * 0.5f;
    const float amp = rec.unitRandomness * kRandomFactor;
    const bool curved = rec.yOffsetCurved != 0;
    FormationRng rng;
    rng.state = seed;
    out.reserve(n);
    int32_t k = 0, row = 0, extra = 0, stag = 0;
    float sign = 1.0f;   // alternates with every member over the whole unit (3.2)
    for (uint32_t m = 0; m < n; ++m) {
        const int32_t rowW = extra + stag + width;
        const int32_t rel = k - centreW;
        int32_t b = std::max(rel, 0) / (2 * xblock);   // C division, as the EXE (truncates toward zero)
        if (rel >= 0 && centreW) b += 1;
        const int32_t even = (rowW + 1) % 2;           // 1 when the row has an even number of slots
        const int32_t odd = rowW % 2;
        const float inner = static_cast<float>(even) * sx * 0.5f - halfGap - static_cast<float>(b) * xgap;
        float x = (static_cast<float>((k - odd + 2) / 2) * sx - inner) * sign;
        float yoff;
        if (!curved) {
            yoff = static_cast<float>((odd + k) / 2) * rec.yOffsetInc;
        } else {
            const float outer = static_cast<float>((rowW - odd + 1) / 2) * sx - inner;
            yoff = (1.0f - std::cos(x / outer * kHalfPi)) * static_cast<float>((odd + rowW) / 2) * rec.yOffsetInc;
            yoff = std::fabs(yoff);
        }
        float ry = 0.0f;
        if (withRandom) {
            const float u1 = rng.next();
            const float u2 = rng.next();
            x += (u1 - 0.5f) * 2.0f * amp * sx;
            ry = (u2 - 0.5f) * 2.0f * amp * sy;
        }
        const float y = static_cast<float>(row / yblock) * ygap + ry + static_cast<float>(row) * sy + yoff;
        if (!finite(x) || !finite(y)) {
            out.clear();
            return Status::fail(Err::BadLayout, "formation " + rec.name + ": slot " + std::to_string(m) +
                                                    " is not finite (a row of one slot with a curved bend)");
        }
        Slot s;
        s.x = x;
        s.y = y;
        out.push_back(s);
        sign = -sign;
        if (++k == rowW) {
            extra += rec.perRowInc * 2;
            k = 0;
            ++row;
            stag = (row % 2) * rec.stagger;
        }
    }
    // centring on the bounding box of the slots together with the origin (3.5)
    float minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const Slot& s : out) {
        minX = std::min(minX, s.x);
        maxX = std::max(maxX, s.x);
        minY = std::min(minY, s.y);
        maxY = std::max(maxY, s.y);
    }
    const float cx = (minX + maxX) * 0.5f, cy = (minY + maxY) * 0.5f;
    for (Slot& s : out) {
        s.x -= cx;
        s.y -= cy;
    }
    return Status::Ok();
}

float facingYaw(int facing)
{
    if (facing < 0 || facing > 5) return 1.0f;
    const int deg = ((150 - 60 * facing) % 360 + 360) % 360;
    return static_cast<float>(deg) * 0.017453292519943295f;
}

Status livingFigures(uint32_t n, int16_t damage, int16_t maxHp, int16_t alive, uint32_t& living)
{
    living = 0;
    if (n == 0 || n > kMaxFigures)
        return Status::fail(Err::BadLayout, "figure count " + std::to_string(n) + " outside 1.." +
                                                std::to_string(kMaxFigures));
    if (alive >= 0) {
        if (static_cast<uint32_t>(alive) > n)
            return Status::fail(Err::BadLayout, "alive " + std::to_string(alive) + " above the unit's " +
                                                    std::to_string(n) + " figures");
        living = static_cast<uint32_t>(alive);
        return Status::Ok();
    }
    if (alive != -1) return Status::fail(Err::BadLayout, "alive " + std::to_string(alive) + " (only -1 or 0..n)");
    if (maxHp <= 0) return Status::fail(Err::BadLayout, "max_hp " + std::to_string(maxHp) + " <= 0");
    if (damage < 0) return Status::fail(Err::BadLayout, "damage " + std::to_string(damage) + " < 0");
    const int32_t pct = std::min<int32_t>(static_cast<int32_t>(damage) * 100 / maxHp, 100);
    int32_t dead = static_cast<int32_t>(std::floor(static_cast<double>(n) * pct / 100.0 + 0.5));
    if (pct < 100) dead = std::min<int32_t>(dead, static_cast<int32_t>(n) - 1);
    else dead = static_cast<int32_t>(n);
    living = n - static_cast<uint32_t>(dead);
    return Status::Ok();
}

Status plotCentre(const heightfield::HeightGrid& g, int32_t plotX, int32_t plotY, float& x, float& y)
{
    x = y = 0;
    if (g.samplesX == 0 || g.samplesY == 0 || g.mapW == 0 || g.mapH <= 4 ||
        g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "height field empty or inconsistent");
    bool any = false;
    for (float v : g.hexToWorld) {
        if (!finite(v)) return Status::fail(Err::BadLayout, "height field's hexToWorld is not finite");
        any = any || v != 0.0f;
    }
    if (!any) return Status::fail(Err::BadLayout, "height field carries no hexToWorld matrix (all zero)");
    if (!finite(g.sx) || !finite(g.sy) || !finite(g.ox) || !finite(g.oy) || g.sx <= 0 || g.sy <= 0)
        return Status::fail(Err::BadLayout, "height field's grid scale or offset is not valid");
    if (plotX < 0 || plotY < 0 || plotX >= static_cast<int32_t>(g.mapW) || plotY >= static_cast<int32_t>(g.mapH - 4))
        return Status::fail(Err::BadLayout, "plot (" + std::to_string(plotX) + ", " + std::to_string(plotY) +
                                                ") outside the height field's map of " + std::to_string(g.mapW) + " x " +
                                                std::to_string(g.mapH - 4) + " plots");
    const float hx = static_cast<float>(plotX - (plotY >> 1)), hy = static_cast<float>(plotY);
    const float* m = g.hexToWorld;   // row-major D3DXMATRIX, row vector (hx, hy, 0, 1) times it
    const float wx = hx * m[0] + hy * m[4] + m[12];
    const float wy = hx * m[1] + hy * m[5] + m[13];
    x = wx * g.sx - g.ox;
    y = wy * g.sy - g.oy;
    return Status::Ok();
}

float heightAtWorld(const heightfield::HeightGrid& g, float x, float y)
{
    const float fc = (x + g.ox) / g.sx, fr = (y + g.oy) / g.sy;
    // truncation toward zero, as a C cast; guard the cast's range (far outside the grid gives 0 anyway)
    if (!(std::fabs(fc) < 1e9f) || !(std::fabs(fr) < 1e9f)) return 0.0f;
    return heightfield::heightAtSample(g, static_cast<int>(fc), static_cast<int>(fr));
}

Status placeUnit(const FormationTable& table, const heightfield::HeightGrid& g, const PlacementInput& in,
                 UnitPlacement& out)
{
    out = UnitPlacement();
    const FormationRecord* rec = table.find(in.formation);
    if (!rec) {
        if (table.records().empty()) return Status::fail(Err::NotFound, "formation table not loaded");
        return Status::fail(Err::NotFound, "formation '" + in.formation + "' is not in UnitFormations.xml");
    }
    const size_t n = in.zOffset.size();
    if (n == 0 || n > kMaxFigures)
        return Status::fail(Err::BadLayout, "figure count " + std::to_string(n) + " outside 1.." +
                                                std::to_string(kMaxFigures));
    for (float z : in.zOffset)
        if (!finite(z)) return Status::fail(Err::BadLayout, "a member's ZOffset is not finite");
    if (!finite(in.waterLevel)) return Status::fail(Err::BadLayout, "water level is not finite");
    if (!finite(in.footRadius) || in.footRadius < 0 || in.footRadius > 16)
        return Status::fail(Err::BadLayout, "footRadius outside 0..16");
    if (in.seed == 0xFFFFFFFFu)
        return Status::fail(Err::BadLayout, "seed -1 (the EXE's running generator) cannot be reproduced per unit");
    float px = 0, py = 0;
    C5R_TRY(plotCentre(g, in.plotX, in.plotY, px, py));
    if (in.hasRoot) {
        if (!finite(in.rootX) || !finite(in.rootY)) return Status::fail(Err::BadLayout, "root is not finite");
        px = in.rootX;
        py = in.rootY;
    }
    uint32_t living = 0;
    C5R_TRY(livingFigures(static_cast<uint32_t>(n), in.damage, in.maxHp, in.alive, living));
    std::vector<Slot> slots;
    C5R_TRY(formationSlots(*rec, static_cast<uint32_t>(n), in.seed, true, table.unitScale(), slots));
    const float yaw = facingYaw(in.facing);
    const float c = std::cos(yaw), s = std::sin(yaw);
    out.rootX = px;
    out.rootY = py;
    out.yaw = yaw;
    out.figures = static_cast<uint32_t>(n);
    out.living = living;
    // members die from the last backwards (unit-formations.md 7), so the living are figures 0 .. living-1 and each
    // takes the next slot of the n-member layout
    for (uint32_t i = 0; i < living; ++i) {
        FigurePlacement f;
        f.figure = static_cast<uint16_t>(i);
        f.slot = static_cast<uint16_t>(i);
        f.x = px + c * slots[i].x - s * slots[i].y;
        f.y = py + s * slots[i].x + c * slots[i].y;
        const float h0 = heightAtWorld(g, f.x, f.y);
        float base = std::max(h0, in.waterLevel);
        // issue 4982 (FixUnitPlacement): on land, the highest ground under the feet (fewer feet inside a slope)
        if (in.footRadius > 0 && h0 > in.waterLevel) base = std::max(footBase(g, f.x, f.y, in.footRadius), in.waterLevel);
        f.z = base + in.zOffset[i];
        f.yaw = yaw;
        out.placed.push_back(f);
    }
    return Status::Ok();
}

// ---- epic 0042, issue 4776: a fight's figure offsets, after placeUnit (agreed hunk, lead-0042 #7815) ----
Status applyFigureOffsets(const heightfield::HeightGrid& g, float waterLevel, const std::vector<FigureOffset>& offs,
                          UnitPlacement& p)
{
    if (offs.empty()) return Status::Ok();
    if (!finite(waterLevel) || !finite(p.yaw)) return Status::fail(Err::BadLayout, "figure offsets: water level or yaw");
    std::vector<FigurePlacement> moved = p.placed;
    const float c = std::cos(p.yaw), s = std::sin(p.yaw);
    for (const FigureOffset& o : offs) {
        if (!finite(o.dx) || !finite(o.dy) || (o.hasYaw && !finite(o.yaw)))
            return Status::fail(Err::BadLayout, "figure offset of figure " + std::to_string(o.figure) + " is not finite");
        FigurePlacement* f = nullptr;
        for (FigurePlacement& q : moved)
            if (q.figure == o.figure) f = &q;
        if (!f)
            return Status::fail(Err::BadLayout, "figure offset names figure " + std::to_string(o.figure) +
                                                    ", which is not placed (" + std::to_string(p.placed.size()) +
                                                    " living)");
        const float floorOld = std::max(heightAtWorld(g, f->x, f->y), waterLevel);
        f->x += c * o.dx - s * o.dy;
        f->y += s * o.dx + c * o.dy;
        f->z = f->z - floorOld + std::max(heightAtWorld(g, f->x, f->y), waterLevel);
        if (o.hasYaw) f->yaw = o.yaw;
    }
    p.placed.swap(moved);
    return Status::Ok();
}

}}  // namespace c5r::units
