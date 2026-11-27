// placement_check.cpp - the placement oracle check (epic 0041, U3, issue 4354, DoD D). Own code.
// See c5r/units/placement_check.h; method and results in research/05-graphics-composition/unit-renderer.md 12.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement_check.h"

namespace c5r { namespace units {

namespace {

std::vector<std::string> splitTabs(const std::string& s)
{
    std::vector<std::string> f;
    size_t p = 0;
    for (;;) {
        size_t t = s.find('\t', p);
        f.push_back(s.substr(p, t == std::string::npos ? std::string::npos : t - p));
        if (t == std::string::npos) break;
        p = t + 1;
    }
    return f;
}

bool toLong(const std::string& s, long& v)
{
    if (s.empty()) return false;
    errno = 0;
    char* e = nullptr;
    v = std::strtol(s.c_str(), &e, 10);
    return e && *e == '\0' && errno == 0;
}

bool toFloat(const std::string& s, float& v)
{
    if (s.empty()) return false;
    errno = 0;
    char* e = nullptr;
    double d = std::strtod(s.c_str(), &e);
    v = static_cast<float>(d);
    return e && *e == '\0' && errno == 0 && std::isfinite(d);
}

Status lineErr(int line, const std::string& what)
{
    return Status::fail(Err::BadLayout, "catalogue line " + std::to_string(line) + ": " + what);
}

float yawDiffDeg(float a, float b)
{
    float d = std::fmod(std::fabs(a - b), 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

bool crossFade(const FigureCheck& f) { return f.rotMaxDeg >= 0.05f || f.partSpread > 0.002f; }

std::string heightClassOf(const FigureCheck& f, const UnitCheck& u, float tol)
{
    const float dz = f.figure >= 0 ? f.dz : f.dzAtMeasured;
    if (std::fabs(dz) <= tol) return "";
    if (f.domain == "Air" || f.domain == "Hover") return "air";
    if (f.domain == "Sea") return "sea";
    if (crossFade(f)) return "fit";
    if (u.kind == "figures!=members") return "grouping";
    return "other";
}

}  // namespace

Status parseCatalogue(const std::string& text, std::vector<CatalogueUnit>& out)
{
    out.clear();
    std::vector<CatalogueUnit> units;
    bool header = false;
    int line = 0;
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        p = e + 1;
        ++line;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] == '#') {
            if (e == text.size()) break;
            continue;
        }
        const std::vector<std::string> f = splitTabs(l);
        if (!header) {
            if (f.size() != 2 || f[0] != "c5placement-catalogue" || f[1] != "1")
                return Status::fail(Err::BadVersion, "catalogue line " + std::to_string(line) +
                                                         ": expected the header 'c5placement-catalogue\\t1'");
            header = true;
            continue;
        }
        long a = 0, b = 0, c = 0, d = 0, s = 0;
        if (f[0] == "unit") {
            if (f.size() != 8) return lineErr(line, "unit needs 8 fields, has " + std::to_string(f.size()));
            if (!toLong(f[2], a) || !toLong(f[4], b) || !toLong(f[5], c) || !toLong(f[6], d) || !toLong(f[7], s) ||
                s < 0 || s > 0x7FFFFFFF)
                return lineErr(line, "unit: a number does not parse");
            CatalogueUnit u;
            u.frame = f[1];
            u.object = static_cast<int>(a);
            u.artRow = f[3];
            u.plotX = static_cast<int32_t>(b);
            u.plotY = static_cast<int32_t>(c);
            u.facing = static_cast<int>(d);
            u.seed = static_cast<uint32_t>(s);
            u.line = line;
            units.push_back(u);
        } else if (f[0] == "member") {
            if (units.empty()) return lineErr(line, "member before any unit");
            if (f.size() != 4) return lineErr(line, "member needs 4 fields, has " + std::to_string(f.size()));
            if (!toLong(f[2], a) || a < 1 || a > static_cast<long>(kMaxFigures))
                return lineErr(line, "member: count does not parse or is out of range");
            CatalogueMember m;
            m.type = f[1];
            m.count = static_cast<int>(a);
            m.domain = f[3] == "-" ? std::string() : f[3];
            units.back().members.push_back(m);
        } else if (f[0] == "fig") {
            if (units.empty()) return lineErr(line, "fig before any unit");
            if (f.size() != 8) return lineErr(line, "fig needs 8 fields, has " + std::to_string(f.size()));
            CatalogueFigure g;
            if (!toFloat(f[2], g.x) || !toFloat(f[3], g.y) || !toFloat(f[4], g.z) || !toFloat(f[5], g.yawDeg) ||
                !toFloat(f[6], g.rotMaxDeg) || !toFloat(f[7], g.partSpread))
                return lineErr(line, "fig: a number does not parse");
            size_t q = 0;
            for (;;) {
                size_t bar = f[1].find('|', q);
                g.members.push_back(f[1].substr(q, bar == std::string::npos ? std::string::npos : bar - q));
                if (bar == std::string::npos) break;
                q = bar + 1;
            }
            units.back().figures.push_back(g);
        } else {
            return lineErr(line, "unknown record '" + f[0] + "'");
        }
        if (e == text.size()) break;
    }
    if (!header) return Status::fail(Err::BadVersion, "catalogue: no header 'c5placement-catalogue\\t1'");
    out = std::move(units);
    return Status::Ok();
}

void minCostAssignment(const std::vector<double>& cost, size_t n, std::vector<size_t>& col)
{
    // Hungarian method with potentials (O(n^3)); rows and columns 1-based inside, 0 a sentinel.
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0), minv(n + 1);
    std::vector<size_t> p(n + 1, 0), way(n + 1, 0);
    std::vector<char> used(n + 1);
    for (size_t i = 1; i <= n; ++i) {
        p[0] = i;
        size_t j0 = 0;
        std::fill(minv.begin(), minv.end(), inf);
        std::fill(used.begin(), used.end(), static_cast<char>(0));
        do {
            used[j0] = 1;
            const size_t i0 = p[j0];
            double delta = inf;
            size_t j1 = 0;
            for (size_t j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const double cur = cost[(i0 - 1) * n + (j - 1)] - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (size_t j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const size_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    col.assign(n, 0);
    for (size_t j = 1; j <= n; ++j)
        if (p[j]) col[p[j] - 1] = j - 1;
}

Status checkCatalogue(const FormationTable& table, const heightfield::HeightGrid& g, const PlacementColumns& cols,
                      const std::vector<CatalogueUnit>& units, float waterLevel, const CheckTolerances& tol,
                      CatalogueReport& out)
{
    out = CatalogueReport();
    for (const CatalogueUnit& cu : units) {
        UnitCheck uc;
        uc.unit = &cu;
        // figure list in member order
        std::vector<const CatalogueMember*> figMember;
        for (const CatalogueMember& m : cu.members)
            for (int k = 0; k < m.count; ++k) figMember.push_back(&m);
        const size_t n = figMember.size();
        auto heightsOnly = [&](const std::string& kind) {
            uc.kind = kind;
            for (size_t j = 0; j < cu.figures.size(); ++j) {
                const CatalogueFigure& mf = cu.figures[j];
                FigureCheck fc;
                fc.measured = static_cast<int>(j);
                fc.member = mf.members.empty() ? std::string() : mf.members[0];
                for (const CatalogueMember& m : cu.members)
                    if (m.type == fc.member) fc.domain = m.domain;
                float zo = 0;
                auto it = cols.zOffset.find(fc.member);
                if (it != cols.zOffset.end()) zo = it->second;
                fc.dzAtMeasured = std::max(heightAtWorld(g, mf.x, mf.y), waterLevel) + zo - mf.z;
                fc.rotMaxDeg = mf.rotMaxDeg;
                fc.partSpread = mf.partSpread;
                uc.figures.push_back(fc);
            }
        };
        if (cu.figures.empty()) {
            uc.kind = "no-figures";
        } else if (cu.figures.size() != n) {
            heightsOnly("figures!=members");
            uc.detail = std::to_string(cu.figures.size()) + " measured figures, " + std::to_string(n) + " members";
        } else {
            PlacementInput in;
            auto formIt = cols.formation.find(cu.artRow);
            Status st;
            if (formIt == cols.formation.end()) {
                st = Status::fail(Err::NotFound, "art row " + cu.artRow + " not in ArtDefine_UnitInfos");
            } else {
                in.formation = formIt->second;
                for (const CatalogueMember* m : figMember) {
                    auto zit = cols.zOffset.find(m->type);
                    if (zit == cols.zOffset.end()) {
                        st = Status::fail(Err::NotFound, "member " + m->type + " not in ArtDefine_UnitMemberInfos");
                        break;
                    }
                    in.zOffset.push_back(zit->second);
                }
            }
            UnitPlacement up;
            if (st.ok()) {
                in.plotX = cu.plotX;
                in.plotY = cu.plotY;
                in.facing = cu.facing;
                in.seed = cu.seed;
                in.waterLevel = waterLevel;
                st = placeUnit(table, g, in, up);
            }
            if (!st.ok()) {
                heightsOnly("failed");
                uc.detail = st.str();
                ++out.failed;
            } else {
                uc.kind = n == 1 ? "single" : "multi";
                uc.detail = in.formation.empty() ? "Default" : in.formation;
                // pair placed (rows) and measured (columns) figures of the same member type by least total distance
                std::vector<double> cost(n * n);
                for (size_t i = 0; i < n; ++i)
                    for (size_t j = 0; j < n; ++j) {
                        const CatalogueFigure& mf = cu.figures[j];
                        const bool same = std::find(mf.members.begin(), mf.members.end(), figMember[i]->type) !=
                                          mf.members.end();
                        const double dx = static_cast<double>(up.placed[i].x) - mf.x;
                        const double dy = static_cast<double>(up.placed[i].y) - mf.y;
                        cost[i * n + j] = dx * dx + dy * dy + (same ? 0.0 : 1e12);
                    }
                std::vector<size_t> col;
                minCostAssignment(cost, n, col);
                const float yawDeg = up.yaw * 57.29577951308232f;
                for (size_t i = 0; i < n; ++i) {
                    const FigurePlacement& pf = up.placed[i];
                    const CatalogueFigure& mf = cu.figures[col[i]];
                    FigureCheck fc;
                    fc.figure = static_cast<int>(i);
                    fc.measured = static_cast<int>(col[i]);
                    fc.member = figMember[i]->type;
                    fc.domain = figMember[i]->domain;
                    fc.dxy = std::hypot(pf.x - mf.x, pf.y - mf.y);
                    fc.dyawDeg = yawDiffDeg(yawDeg, mf.yawDeg);
                    fc.dz = pf.z - mf.z;
                    fc.dzAtMeasured = std::max(heightAtWorld(g, mf.x, mf.y), waterLevel) + in.zOffset[i] - mf.z;
                    fc.rotMaxDeg = mf.rotMaxDeg;
                    fc.partSpread = mf.partSpread;
                    uc.maxDxy = std::max(uc.maxDxy, fc.dxy);
                    uc.maxDyawDeg = std::max(uc.maxDyawDeg, fc.dyawDeg);
                    uc.maxDz = std::max(uc.maxDz, std::fabs(fc.dz));
                    uc.figures.push_back(fc);
                }
                uc.within = uc.maxDxy <= tol.xy && uc.maxDyawDeg <= tol.yawDeg;
                if (!uc.within) {
                    bool air = false, fit = false, sea = false;
                    for (const FigureCheck& fc : uc.figures) {
                        if (fc.dxy <= tol.xy && fc.dyawDeg <= tol.yawDeg) continue;
                        air = air || fc.domain == "Air" || fc.domain == "Hover";
                        sea = sea || fc.domain == "Sea";
                        fit = fit || crossFade(fc);
                    }
                    uc.outsideClass = air ? "air" : fit ? "fit" : sea ? "sea" : "other";
                    if (n > 1) {
                        if (air) ++out.multiOutsideAir;
                        else if (fit) ++out.multiOutsideFit;
                        else ++out.multiOutsideOther;
                    }
                }
                if (n == 1) {
                    ++out.single;
                    if (uc.within) ++out.singleWithin;
                } else {
                    ++out.multi;
                    if (uc.within) ++out.multiWithin;
                }
            }
        }
        if (uc.kind == "no-figures" || uc.kind == "figures!=members") ++out.notCompared;
        for (FigureCheck& fc : uc.figures) {
            fc.heightClass = heightClassOf(fc, uc, tol.z);
            ++out.heightFigures;
            if (fc.heightClass.empty()) ++out.heightWithin;
        }
        out.units.push_back(std::move(uc));
    }
    return Status::Ok();
}

std::string catalogueReportText(const CatalogueReport& r, const CheckTolerances& tol)
{
    std::string s = "frame\tobject\tart_row\tkind\tdetail\tfigure\tmeasured\tmember\tdomain\tdxy\tdyaw_deg\tdz\t"
                    "dz_at_measured\trot_max_deg\tpart_spread\theight_class\tunit_within\toutside_class\n";
    char buf[512];
    for (const UnitCheck& u : r.units) {
        if (u.figures.empty()) {
            std::snprintf(buf, sizeof buf, "%s\t%d\t%s\t%s\t%s\t\t\t\t\t\t\t\t\t\t\t\t\t\n", u.unit->frame.c_str(),
                          u.unit->object, u.unit->artRow.c_str(), u.kind.c_str(), u.detail.c_str());
            s += buf;
            continue;
        }
        for (const FigureCheck& f : u.figures) {
            std::snprintf(buf, sizeof buf,
                          "%s\t%d\t%s\t%s\t%s\t%d\t%d\t%s\t%s\t%.4f\t%.4f\t%.4f\t%.4f\t%.5f\t%.4f\t%s\t%d\t%s\n",
                          u.unit->frame.c_str(), u.unit->object, u.unit->artRow.c_str(), u.kind.c_str(),
                          u.detail.c_str(), f.figure, f.measured, f.member.c_str(), f.domain.c_str(), f.dxy,
                          f.dyawDeg, f.dz, f.dzAtMeasured, f.rotMaxDeg, f.partSpread, f.heightClass.c_str(),
                          u.within ? 1 : 0, u.outsideClass.c_str());
            s += buf;
        }
    }
    int cls[5] = {0, 0, 0, 0, 0};
    const char* names[5] = {"air", "sea", "fit", "grouping", "other"};
    for (const UnitCheck& u : r.units)
        for (const FigureCheck& f : u.figures)
            for (int k = 0; k < 5; ++k)
                if (f.heightClass == names[k]) ++cls[k];
    std::snprintf(buf, sizeof buf,
                  "# summary: tolerances xy %.4f, yaw %.4f deg, z %.4f\n"
                  "# multi-figure units within: %d of %d (outside: air %d, fit %d, other %d); single figures within: "
                  "%d of %d; not compared %d; failed %d\n"
                  "# heights within: %d of %d figures; outside by class: air %d, sea %d, fit %d, grouping %d, other %d\n",
                  tol.xy, tol.yawDeg, tol.z, r.multiWithin, r.multi, r.multiOutsideAir, r.multiOutsideFit,
                  r.multiOutsideOther, r.singleWithin, r.single, r.notCompared, r.failed,
                  r.heightWithin, r.heightFigures, cls[0], cls[1], cls[2], cls[3], cls[4]);
    s += buf;
    for (const UnitCheck& u : r.units) {
        if ((u.kind == "multi" || u.kind == "single") && !u.within) {
            std::snprintf(buf, sizeof buf, "# outside: %s %d %s (%s, %s, class %s): max dxy %.4f, max dyaw %.4f deg\n",
                          u.unit->frame.c_str(), u.unit->object, u.unit->artRow.c_str(), u.kind.c_str(),
                          u.detail.c_str(), u.outsideClass.c_str(), u.maxDxy, u.maxDyawDeg);
            s += buf;
        }
    }
    return s;
}

}}  // namespace c5r::units
