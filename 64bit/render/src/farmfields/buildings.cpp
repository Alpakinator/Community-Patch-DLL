// buildings.cpp - the farm buildings as records (ticket 0653, D230). See c5r/farmfields/buildings.h. Own code from the
// knowledge base's description (D83): cities-and-landmarks.md 2.5, 2.6; farm-fields-layout.md 4.2, 7, 16.
// Written 2026-11-21 for this repository (GPL-3.0-or-later).
#include "c5r/farmfields/buildings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "c5r/farmfields/layout.h"
#include "c5r/heightfield/heightfield.h"

namespace c5r { namespace farmfields {

namespace {

const double kPiB = 3.14159265358979323846;

std::string lower(const std::string& s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// splitmix32-style finaliser: a second, independent stream from one seed
uint32_t mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

}  // namespace

uint8_t farmEraLevel(int32_t gameEra) {
    if (gameEra <= 2) return 1;   // < 0 (no owner, unknown) included
    if (gameEra == 3) return 2;
    if (gameEra == 4) return 3;
    return 4;
}

int32_t landmarkEraIndex(const std::string& era) {
    const std::string e = lower(era);
    if (e.empty() || e == "any") return -1;
    static const char* const names[] = {"ancient", "classical", "medieval", "renaissance", "industrial", "modern"};
    for (int32_t i = 0; i < 6; ++i)
        if (e == names[i]) return i;
    if (e == "postmodern" || e == "atomic") return 6;
    if (e == "future" || e == "information") return 7;
    return -2;
}

int32_t farmRowLevel(const std::string& era) {
    const int32_t i = landmarkEraIndex(era);
    if (i == -1) return 0;
    if (i < -1) return -1;
    return farmEraLevel(i);
}

bool FarmRowChoice::any() const {
    for (uint32_t l = 0; l < kFarmEraLevels; ++l)
        if (!level[l].empty()) return true;
    return false;
}

bool FarmRowChoice::operator==(const FarmRowChoice& o) const {
    for (uint32_t l = 0; l < kFarmEraLevels; ++l)
        if (level[l] != o.level[l]) return false;
    if (refused.size() != o.refused.size() || farmHandlerRows != o.farmHandlerRows) return false;
    for (size_t i = 0; i < refused.size(); ++i)
        if (refused[i].row != o.refused[i].row || refused[i].text != o.refused[i].text) return false;
    return true;
}

bool FarmBuilding::operator==(const FarmBuilding& o) const {
    // floats compared by their bits (a record is the same only when it is byte-equal in meaning)
    return plot == o.plot && x == o.x && y == o.y && owner == o.owner && era == o.era && eraLevel == o.eraLevel &&
           rowLevel == o.rowLevel && row == o.row && pick == o.pick && state == o.state && pillaged == o.pillaged &&
           field == o.field && std::memcmp(pos, o.pos, sizeof(pos)) == 0 &&
           std::memcmp(&distance, &o.distance, sizeof(float)) == 0 && std::memcmp(&z, &o.z, sizeof(float)) == 0 &&
           hasHeight == o.hasHeight && quarter == o.quarter && std::memcmp(&facing, &o.facing, sizeof(float)) == 0;
}

BuildingFacts::BuildingFacts() {
    for (int i = 0; i < 64; ++i) eraOfPlayer[i] = -1;
}

uint32_t buildingPick(uint32_t gameSeed, int32_t x, int32_t y) {
    return mix32(plotSeed(gameSeed ^ 0x46524D42u /* "FRMB" */, x, y));
}

uint8_t buildingQuarter(uint32_t gameSeed, int32_t x, int32_t y) {
    return static_cast<uint8_t>(mix32(plotSeed(gameSeed ^ 0x51545552u /* "QTUR" */, x, y) + 0x9E3779B9u) >> 30);
}

double stockFieldF(const FieldPolygon& f) {
    const std::vector<float>& c = f.corners;
    const size_t n = c.size() / 2;
    if (n < 2) return 0.0;
    double s = 0;
    for (size_t i = 0; i < n; ++i) {   // the previous corner of corner 0 is the last (the EXE's loop starts there)
        const size_t p = (i + n - 1) % n;
        const double xp = c[2 * p], yp = c[2 * p + 1] / kLayoutYScale, y = c[2 * i + 1] / kLayoutYScale;
        s += xp * (y - yp);
    }
    return 0.5 * s;
}

bool stockFieldCentroid(const FieldPolygon& f, double out[2]) {
    const std::vector<float>& c = f.corners;
    const size_t n = c.size() / 2;
    if (n < 3) return false;
    double cross = 0, sx = 0, sy = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t p = (i + n - 1) % n;
        const double xp = c[2 * p], yp = c[2 * p + 1] / kLayoutYScale;
        const double x = c[2 * i], y = c[2 * i + 1] / kLayoutYScale;
        const double k = y * xp - x * yp;
        cross += k;
        sx += (x + xp) * k;
        sy += (y + yp) * k;
    }
    if (cross == 0.0) return false;
    out[0] = sx / (3.0 * cross);
    out[1] = sy / (3.0 * cross);
    return true;
}

bool stockPlacement(const std::vector<FieldPolygon>& fields, const std::vector<uint32_t>& which, double px, double py,
                    float out[2], int32_t& field, double& dist) {
    double best = std::numeric_limits<double>::max();   // the EXE starts from FLT_MAX; strictly nearer replaces
    double bx = 0, by = 0;
    int32_t bf = -1;
    for (uint32_t fi : which) {
        if (fi >= fields.size()) continue;
        if (!(std::fabs(stockFieldF(fields[fi])) >= kStockFieldMinF)) continue;
        double c[2];
        if (!stockFieldCentroid(fields[fi], c)) continue;
        const double d = (c[0] - px) * (c[0] - px) + (c[1] - py) * (c[1] - py);
        if (d < best) {
            best = d;
            bx = c[0];
            by = c[1];
            bf = static_cast<int32_t>(fi);
        }
    }
    if (bf < 0) return false;
    out[0] = static_cast<float>(bx);
    out[1] = static_cast<float>(by * kLayoutYScale);
    field = bf;
    dist = std::sqrt(best);
    return true;
}

bool nearestOnOutlines(const std::vector<FieldPolygon>& fields, const std::vector<uint32_t>& which, double px, double py,
                       float out[2], int32_t& field, double& dist) {
    double best = std::numeric_limits<double>::infinity();
    double bx = 0, by = 0;
    int32_t bf = -1;
    for (uint32_t fi : which) {
        if (fi >= fields.size()) continue;
        const std::vector<float>& c = fields[fi].corners;
        const size_t n = c.size() / 2;
        if (n < 2) continue;
        for (size_t i = 0; i < n; ++i) {   // every edge of the closed outline, in L
            const size_t j = (i + 1) % n;
            const double ax = c[2 * i], ay = c[2 * i + 1] / kLayoutYScale;
            const double ex = c[2 * j], ey = c[2 * j + 1] / kLayoutYScale;
            const double dx = ex - ax, dy = ey - ay;
            const double len2 = dx * dx + dy * dy;
            double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            const double qx = ax + t * dx, qy = ay + t * dy;
            const double d = (qx - px) * (qx - px) + (qy - py) * (qy - py);
            if (d < best) {   // strictly smaller: the first field and edge win a tie
                best = d;
                bx = qx;
                by = qy;
                bf = static_cast<int32_t>(fi);
            }
        }
    }
    if (bf < 0) return false;
    out[0] = static_cast<float>(bx);
    out[1] = static_cast<float>(by * kLayoutYScale);
    field = bf;
    dist = std::sqrt(best);
    return true;
}

Status placeBuildings(const LayoutInput& in, const FarmLayout& layout, const BuildingFacts& facts, const FarmRowChoice& rows,
                      const heightfield::HeightGrid* height, std::vector<FarmBuilding>& out) {
    out.clear();
    C5R_TRY(checkInput(in));
    if (!facts.owner.empty() && facts.owner.size() != in.plots.size())
        return Status::fail(Err::BadLayout, "placeBuildings: " + std::to_string(facts.owner.size()) + " owners for " +
                                                std::to_string(in.plots.size()) + " farm plots");
    for (uint32_t l = 0; l < kFarmEraLevels; ++l)
        for (int32_t r : rows.level[l])
            if (r < 0) return Status::fail(Err::BadLayout, "placeBuildings: a FARM row id below 0 at level " + std::to_string(l));
    // the fields of each plot (the Crops_Damage copies share their field's outline: harmless)
    std::vector<std::vector<uint32_t>> byPlot(in.plots.size());
    for (uint32_t fi = 0; fi < layout.fields.size(); ++fi) {
        const uint32_t p = layout.fields[fi].plot;
        if (p >= in.plots.size())
            return Status::fail(Err::BadLayout, "placeBuildings: field " + std::to_string(fi) + " names plot " +
                                                    std::to_string(p) + " outside the input");
        byPlot[p].push_back(fi);
    }
    const FieldParams fp = fieldParamsFor(in.gameSeed);
    std::vector<FarmBuilding> res;
    res.reserve(in.plots.size());
    for (uint32_t i = 0; i < in.plots.size(); ++i) {
        const FarmPlot& p = in.plots[i];
        FarmBuilding b;
        b.plot = i;
        b.x = p.x;
        b.y = p.y;
        b.owner = facts.owner.empty() ? static_cast<int8_t>(-1) : facts.owner[i];
        b.era = (b.owner >= 0 && b.owner < 64) ? facts.eraOfPlayer[b.owner] : -1;
        b.eraLevel = farmEraLevel(b.era);
        b.pick = buildingPick(in.gameSeed, p.x, p.y);
        for (int32_t l = b.eraLevel; l >= 0; --l)   // the next lower level that has rows
            if (!rows.level[l].empty()) {
                b.rowLevel = static_cast<uint8_t>(l);
                b.row = rows.level[l][b.pick % rows.level[l].size()];
                break;
            }
        b.state = p.state;
        b.pillaged = p.state == FarmState::Pillaged;
        double c[2];
        plotCentreLayout(p.x, p.y, c);
        double dist = 0;
        // ticket 0657: the stock rule (the centroid of the nearest-centroid field with |F| >= 100), not the nearest
        // outline point of 0653 (cities-and-landmarks.md 2.6.2: 357 of 373 stock records within 1 unit of this rule)
        if (stockPlacement(layout.fields, byPlot[i], c[0], c[1], b.pos, b.field, dist)) {
            b.distance = static_cast<float>(dist);
        } else {
            b.field = -1;
            b.pos[0] = static_cast<float>(c[0]);
            b.pos[1] = static_cast<float>(c[1] * kLayoutYScale);
        }
        // the facing is read at the point clamped into the world rectangle; the position stays unclamped (the EXE clamps
        // only for its direction field read, 2.6.2 (1))
        const float fx = std::min(std::max(b.pos[0], in.worldRect[0]), in.worldRect[2]);
        const float fy = std::min(std::max(b.pos[1], in.worldRect[1]), in.worldRect[3]);
        b.quarter = buildingQuarter(in.gameSeed, p.x, p.y);
        double a = fieldAngle(fp, fx, fy / kLayoutYScale) + b.quarter * (kPiB / 2.0);
        a = std::fmod(a, 2.0 * kPiB);
        if (a < 0) a += 2.0 * kPiB;
        b.facing = static_cast<float>(a);
        if (b.facing >= static_cast<float>(2.0 * kPiB)) b.facing = 0.0f;
        if (height) {
            // at the clamped point: a centroid of an edge plot's field may lie just outside the samples, where
            // heightAtSample answers 0 (what the EXE's height read gives there was not read: [I], ticket 0657)
            b.z = heightfield::heightAtSample(*height, static_cast<int>(std::floor(fx)), static_cast<int>(std::floor(fy)));
            b.hasHeight = true;
        }
        res.push_back(b);
    }
    out.swap(res);
    return Status::Ok();
}

std::string buildingText(const FarmBuilding& b) {
    char z[32];
    if (b.hasHeight) std::snprintf(z, sizeof(z), "%.3f", b.z);
    else std::snprintf(z, sizeof(z), "-");
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%d,%d o=%d e=%d L%u/%d row=%d st=%u f=%d pos=%.3f,%.3f z=%s q=%u a=%.4f", b.x, b.y,
                  static_cast<int>(b.owner), b.era, static_cast<unsigned>(b.eraLevel),
                  b.rowLevel == 0xFF ? -1 : static_cast<int>(b.rowLevel), b.row, static_cast<unsigned>(b.state), b.field,
                  b.pos[0], b.pos[1], z, static_cast<unsigned>(b.quarter), b.facing);
    return buf;
}

}}  // namespace c5r::farmfields
