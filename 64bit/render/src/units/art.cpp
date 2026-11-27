// art.cpp - L0 of the unit module: art rows, member infos and the files they name (epic 0041, issue 4352;
// research/05-graphics-composition/unit-renderer.md 6.1 and 10.1). Own code.
#include "c5r/units/art.h"

#include <algorithm>
#include <cstdlib>
#include <set>

#include "c5r/gamefs/gamefiles.h"

namespace c5r { namespace units {

// ---------------------------------------------------------------------------------------------- files
bool GameFilesProvider::read(const std::string& basename_lower, c5m::Bytes& out, std::string* origin) {
    std::vector<uint8_t> b;
    std::string o;
    Status s = gf_.read(basename_lower, b, &o);
    if (!s.ok()) return false;
    out.assign(b.begin(), b.end());
    if (origin) *origin = o;
    return true;
}

bool GameFilesProvider::exists(const std::string& basename_lower, std::string* origin) {
    if (!gf_.exists(basename_lower)) return false;
    if (origin) *origin = gf_.origin(basename_lower);
    return true;
}

uint32_t UnitArt::figures() const {
    uint32_t n = 0;
    for (auto& m : members) n += static_cast<uint32_t>(m.count > 0 ? m.count : 0);
    return n;
}

// ---------------------------------------------------------------------------------------------- tables
namespace {

const std::string& txt(const c5m::DbRow& r, size_t i) {
    static const std::string empty;
    return (i < r.size() && !r[i].null) ? r[i].text : empty;
}
bool truthy(const c5m::DbRow& r, size_t i) {
    const std::string& t = txt(r, i);
    return !t.empty() && t != "0" && t != "false" && t != "FALSE";
}
float flt(const c5m::DbRow& r, size_t i) {
    const std::string& t = txt(r, i);
    return t.empty() ? 0.0f : std::strtof(t.c_str(), nullptr);
}

UnitStatus tableFail(const std::string& table, const c5m::Error& e) {
    return UnitStatus::fail(UnitCode::BadInput, "database table " + table + ": " + e.what);
}

}  // namespace

UnitStatus ArtResolver::open(c5m::TableSource& db, c5m::FileProvider& files) {
    *this = ArtResolver();
    files_ = &files;
    c5m::Error e;
    std::vector<c5m::DbRow> rows;

    if (!db.rows("Units", {"Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"}, rows,
                 &e))
        return tableFail("Units", e);
    for (auto& r : rows) {
        UnitRow u;
        u.art = txt(r, 1);
        u.cultural = truthy(r, 2);
        u.era = truthy(r, 3);
        if (units_.emplace(txt(r, 0), u).second) unit_types_.push_back(txt(r, 0));
    }
    rows.clear();
    if (!db.rows("ArtDefine_UnitInfos", {"Type", "Formation", "DamageStates"}, rows, &e))
        return tableFail("ArtDefine_UnitInfos", e);
    for (auto& r : rows) {
        InfoRow i;
        i.formation = txt(r, 1);
        i.damage_states = std::atoi(txt(r, 2).c_str());
        infos_[txt(r, 0)] = i;
    }
    rows.clear();
    if (!db.rows("ArtDefine_UnitInfoMemberInfos", {"UnitInfoType", "UnitMemberInfoType", "NumMembers"}, rows, &e))
        return tableFail("ArtDefine_UnitInfoMemberInfos", e);
    for (auto& r : rows) info_members_[txt(r, 0)].emplace_back(txt(r, 1), std::atoi(txt(r, 2).c_str()));
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberInfos", {"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"}, rows,
                 &e))
        return tableFail("ArtDefine_UnitMemberInfos", e);
    for (auto& r : rows) {
        MemberRow m;
        m.scale = flt(r, 1);
        m.zoffset = flt(r, 2);
        m.domain = txt(r, 3);
        m.model = txt(r, 4);
        m.model_null = r.size() < 5 || r[4].null;
        m.material = txt(r, 5);
        members_[txt(r, 0)] = m;
    }
    rows.clear();
    if (!db.rows("Eras", {"Type", "ArtPrefix"}, rows, &e)) return tableFail("Eras", e);
    for (auto& r : rows) {
        era_prefix_.emplace_back(txt(r, 0), txt(r, 1));
        eras_.push_back(txt(r, 0));
    }
    std::set<std::string> suffixes;
    suffixes.insert("");
    for (const char* table : {"Civilizations", "MinorCivilizations"}) {
        rows.clear();
        if (!db.rows(table, {"Type", "ArtStyleType", "ArtStyleSuffix"}, rows, &e)) return tableFail(table, e);
        for (auto& r : rows) {
            const std::string& suf = txt(r, 2);
            civ_suffix_[txt(r, 0)] = suf;
            if (!txt(r, 1).empty()) {
                auto& v = style_suffixes_[txt(r, 1)];
                if (std::find(v.begin(), v.end(), suf) == v.end()) v.push_back(suf);
            }
            suffixes.insert(suf);
        }
    }
    suffixes_.assign(suffixes.begin(), suffixes.end());
    rows.clear();
    if (!db.rows("PlayerColors", {"Type", "SecondaryColor"}, rows, &e)) return tableFail("PlayerColors", e);
    for (auto& r : rows) player_colour_[txt(r, 0)] = txt(r, 1);
    rows.clear();
    if (!db.rows("Colors", {"Type", "Red", "Green", "Blue", "Alpha"}, rows, &e)) return tableFail("Colors", e);
    for (auto& r : rows) colours_[txt(r, 0)] = {flt(r, 1), flt(r, 2), flt(r, 3), flt(r, 4)};
    return UnitStatus::Ok();
}

bool ArtResolver::has_row(const std::string& art_row) const { return infos_.count(art_row) != 0; }

std::string ArtResolver::suffix_of(const std::string& art_style, bool* ok) const {
    *ok = true;
    if (art_style.empty()) return std::string();
    if (art_style[0] == '_') return art_style;                     // a suffix as such
    auto c = civ_suffix_.find(art_style);                          // a civilization or minor civilization Type
    if (c != civ_suffix_.end()) return c->second;
    auto s = style_suffixes_.find(art_style);                      // an ArtStyleType with one suffix
    if (s != style_suffixes_.end() && s->second.size() == 1) return s->second[0];
    *ok = false;
    return std::string();
}

UnitStatus ArtResolver::art_row(const std::string& unit_type, const std::string& art_style, const std::string& era,
                                std::string& row) const {
    row.clear();
    auto u = units_.find(unit_type);
    if (u == units_.end()) return UnitStatus::fail(UnitCode::NoArtRow, "no Units row " + unit_type);
    if (u->second.art.empty()) return UnitStatus::fail(UnitCode::NoArtRow, unit_type + " has no UnitArtInfo");
    const std::string& base = u->second.art;
    bool ok = true;
    std::string suffix = suffix_of(art_style, &ok);
    if (!ok)
        return UnitStatus::fail(UnitCode::BadInput, "art style " + art_style +
                                " is no ArtStyleSuffix, civilization Type or single-suffix ArtStyleType");
    int eraIndex = -1;
    if (!era.empty()) {
        for (size_t i = 0; i < era_prefix_.size(); ++i)
            if (era_prefix_[i].first == era) eraIndex = static_cast<int>(i);
        if (eraIndex < 0) return UnitStatus::fail(UnitCode::BadInput, "era " + era + " is not in Eras");
    }
    if (u->second.cultural && !suffix.empty() && has_row(base + suffix)) {
        row = base + suffix;
        return UnitStatus::Ok();
    }
    if (u->second.era && eraIndex >= 0) {
        for (int i = eraIndex; i >= 0; --i) {
            std::string cand = base + "_" + era_prefix_[static_cast<size_t>(i)].second;
            if (has_row(cand)) {
                row = cand;
                return UnitStatus::Ok();
            }
        }
        int industrial = -1;
        for (size_t i = 0; i < era_prefix_.size(); ++i)
            if (era_prefix_[i].first == "ERA_INDUSTRIAL") industrial = static_cast<int>(i);
        if (industrial >= 0 && eraIndex >= industrial && has_row(base + "_LATE")) {
            row = base + "_LATE";
            return UnitStatus::Ok();
        }
    }
    if (!has_row(base)) return UnitStatus::fail(UnitCode::NoArtRow, "no ArtDefine_UnitInfos row " + base +
                                                " (" + unit_type + ")");
    row = base;
    return UnitStatus::Ok();
}

// issue 4464: the cache per art row (art.h). The uncached rule fills a fresh UnitArt (unit_type ""), so the cached art
// is exactly what resolve_uncached gives on a default UnitArt; resolve() overwrites the whole of `out` with it (before
// 4464 a failed row left the fields it did not reach, unit_type among them, as the caller had them; no caller read them).
UnitStatus ArtResolver::resolve_shared(const std::string& art_row, std::shared_ptr<const UnitArt>& out) {
    auto it = cache_.find(art_row);
    if (it == cache_.end()) {
        std::shared_ptr<UnitArt> a = std::make_shared<UnitArt>();
        Cached c;
        c.status = resolve_uncached(art_row, *a);
        c.art = std::move(a);
        it = cache_.emplace(art_row, std::move(c)).first;
    }
    out = it->second.art;
    return it->second.status;
}

UnitStatus ArtResolver::resolve(const std::string& art_row, UnitArt& out) {
    std::shared_ptr<const UnitArt> a;
    UnitStatus s = resolve_shared(art_row, a);
    out = *a;
    return s;
}

UnitStatus ArtResolver::resolve_uncached(const std::string& art_row, UnitArt& out) {
    out.art_row = art_row;
    out.members.clear();
    auto info = infos_.find(art_row);
    if (info == infos_.end()) return UnitStatus::fail(UnitCode::NoArtRow, "no ArtDefine_UnitInfos row " + art_row);
    out.formation = info->second.formation;
    out.damage_states = info->second.damage_states;
    auto mi = info_members_.find(art_row);
    if (mi == info_members_.end() || mi->second.empty())
        return UnitStatus::fail(UnitCode::NoMembers, "no ArtDefine_UnitInfoMemberInfos rows for " + art_row);
    if (!files_) return UnitStatus::fail(UnitCode::BadInput, "art resolver not opened");
    for (auto& p : mi->second) {
        auto m = members_.find(p.first);
        if (m == members_.end())
            return UnitStatus::fail(UnitCode::NoMembers, art_row + ": no ArtDefine_UnitMemberInfos row " + p.first);
        if (p.second <= 0)
            return UnitStatus::fail(UnitCode::NoMembers, art_row + ": member " + p.first + " NumMembers " +
                                    std::to_string(p.second));
        MemberArt a;
        a.member = p.first;
        a.count = p.second;
        a.scale = m->second.scale;
        a.zoffset = m->second.zoffset;
        a.domain = m->second.domain;
        a.material_tag = m->second.material;
        a.model = m->second.model;
        if (m->second.model_null || a.model.empty())
            return UnitStatus::fail(UnitCode::ModelNotFound, art_row + ": member " + p.first + " has no Model");
        c5m::Error e;
        ++fxsxml_parses_;
        if (!c5m::resolve_fxsxml(*files_, a.model, a.files, &e)) {
            if (e.code == c5m::Err::Xml) return UnitStatus::fail(UnitCode::BadModel, art_row + ": " + e.what);
            return UnitStatus::fail(UnitCode::ModelNotFound, art_row + ": member " + p.first + ": " + e.what);
        }
        if (!a.files.mesh.found)
            return UnitStatus::fail(UnitCode::ModelNotFound, art_row + ": member " + p.first + ": mesh " +
                                    a.files.mesh.name + " not found (" + c5m::basename_lower(a.model) + ")");
        out.members.push_back(std::move(a));
    }
    return UnitStatus::Ok();
}

UnitStatus ArtResolver::team_colour(const std::string& player_colour, float rgba[4]) const {
    auto p = player_colour_.find(player_colour);
    if (p == player_colour_.end())
        return UnitStatus::fail(UnitCode::BadInput, "no PlayerColors row " + player_colour);
    auto c = colours_.find(p->second);
    if (c == colours_.end() || c->second.size() < 3)
        return UnitStatus::fail(UnitCode::BadInput, "no Colors row " + p->second + " (" + player_colour + ")");
    // trunc(float32(v) x 255) x float32(1/255): a float32 PRODUCT with the reciprocal, not a division. Measured
    // bit-equal on 228 captured instances of 6 owners (issue 4453, 2026-11-16, research unit-draws.md 5.1); the
    // division / 255.0f misses by one ulp for many bytes (57, 118, 229, 217, 212, 55, ...).
    const float kInv255 = 1.0f / 255.0f;
    for (int i = 0; i < 3; ++i) {
        float v = c->second[static_cast<size_t>(i)] * 255.0f;
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        rgba[i] = static_cast<float>(static_cast<uint8_t>(v)) * kInv255;
    }
    rgba[3] = 1.0f;
    return UnitStatus::Ok();
}

}}  // namespace c5r::units
