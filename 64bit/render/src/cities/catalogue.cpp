// catalogue.cpp - c5r::cities' art catalogue (ticket 4872, epic 0561). Own code from the knowledge base's description
// (D83): research/05-graphics-composition/cities-and-landmarks.md 1.2, 1.6, 1.7, 1.10.
#include "c5r/cities/catalogue.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/art.h"   // units::GameFilesProvider

namespace c5r { namespace cities {

namespace {

const char* const kCultureNames[kCultures] = {"America", "Asia", "Africa", "Europe", "Mediterranean", "Polynesian",
                                              "Ruins"};
const char* const kCultureAdjectives[kCultures] = {"AMERICAN", "ASIAN", "AFRICAN", "EUROPEAN", "MEDITERRANEAN",
                                                   "POLYNESIAN", ""};
const char* const kWallFileNames[kWallFiles] = {"amer_walls.gr2", "asia_walls.gr2", "afri_walls.gr2", "euro_walls.gr2",
                                                "medit_walls.gr2", "poly_walls.gr2", "wall_of_babylon.gr2"};
const char* const kGreatWallFileNames[kGreatWallFiles] = {"hb_great_wall.gr2", "great_wall.gr2"};
const char* const kSlotWords[kEraSlots] = {"ANCIENT", "RENAISSANCE", "INDUSTRIAL", "MODERN"};
const char* const kEraWords[] = {"ANCIENT", "CLASSICAL", "MEDIEVAL", "RENAISSANCE", "INDUSTRIAL", "MODERN",
                                 "POSTMODERN", "FUTURE"};
const char* const kRandomWords[] = {"ONE", "TWO", "THREE", "FOUR"};

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

const std::string& txt(const c5m::DbRow& r, size_t i) {
    static const std::string empty;
    return (i < r.size() && !r[i].null) ? r[i].text : empty;
}
bool truthy(const c5m::DbRow& r, size_t i) {
    const std::string& t = txt(r, i);
    return !t.empty() && t != "0" && t != "false" && t != "FALSE";
}

Status dbRows(c5m::TableSource& db, const std::string& table, const std::vector<std::string>& cols,
              std::vector<c5m::DbRow>& out) {
    out.clear();
    c5m::Error e;
    if (!db.rows(table, cols, out, &e)) return Status::fail(Err::NotFound, "database table " + table + ": " + e.what);
    return Status::Ok();
}

}  // namespace

const char* const kWallPieces[3] = {"WALL", "GATE", "TOWER"};
const char* const kGreatWallPieces[4] = {"WALL1", "WALL2", "TOWER1", "TOWER2"};

const char* cultureName(uint32_t c) { return c < kCultures ? kCultureNames[c] : "?"; }
const char* cultureAdjective(uint32_t c) { return c < kCultures ? kCultureAdjectives[c] : ""; }
const char* wallFile(uint32_t i) { return i < kWallFiles ? kWallFileNames[i] : ""; }
const char* greatWallFile(uint32_t i) { return i < kGreatWallFiles ? kGreatWallFileNames[i] : ""; }

int cultureIndex(const std::string& name) {
    for (uint32_t c = 0; c < kCultures; ++c)
        if (ieq(name, kCultureNames[c])) return static_cast<int>(c);
    return -1;
}

uint32_t cultureFromArtStyle(int id) {
    switch (id) {
    case 0: case 1: case 2: case 4: case 5: case 6: return static_cast<uint32_t>(id);
    default: return kEurope;
    }
}

Status eraSlots(const std::vector<EraRow>& eras, std::vector<int>& slots) {
    slots.clear();
    if (eras.empty()) return Status::fail(Err::BadLayout, "no eras");
    std::vector<int> own(eras.size(), -1);
    for (size_t i = 0; i < eras.size(); ++i) {
        const std::string& s = !eras[i].cityArtType.empty() ? eras[i].cityArtType : eras[i].artPrefix;
        for (int k = 0; k < static_cast<int>(kEraSlots); ++k)
            if (s == kSlotWords[k]) own[i] = k;
    }
    slots.assign(eras.size(), -1);
    for (size_t i = 0; i < eras.size(); ++i)
        for (size_t j = i + 1; j-- > 0;)
            if (own[j] >= 0) {
                slots[i] = own[j];
                break;
            }
    return Status::Ok();
}

Status parseCitySetFile(const char* data, size_t size, const std::string& source, std::vector<CitySet>& out) {
    c5m::XmlElement root;
    c5m::Error e;
    if (!c5m::parse_xml(data, size, root, &e)) return Status::fail(Err::BadLayout, source + ": " + e.what);
    if (root.name != "BuildingArtInfos")
        return Status::fail(Err::BadLayout, source + ": root <" + root.name + ">, not <BuildingArtInfos>");
    std::vector<CitySet> v;
    for (const c5m::XmlElement& c : root.children) {
        if (c.name != "Culture") continue;
        const std::string* nm = c.attr("name");
        if (!nm) return Status::fail(Err::BadLayout, source + ": a <Culture> without a name (line " +
                                                         std::to_string(c.line) + ")");
        const int ci = cultureIndex(*nm);
        if (ci < 0) return Status::fail(Err::BadLayout, source + ": culture '" + *nm + "' is not one of the seven");
        uint32_t lighting = static_cast<uint32_t>(ci);
        if (const std::string* l = c.attr("lighting")) {
            const int li = cultureIndex(*l);
            if (li < 0) return Status::fail(Err::BadLayout, source + ": lighting '" + *l + "' names no culture");
            lighting = static_cast<uint32_t>(li);
        } else if (ci == 4 || ci == static_cast<int>(kRuins)) {
            lighting = kEurope;   // Mediterranean and Ruins are lit as Europe when the attribute is absent (1.2)
        }
        uint32_t slot = 0;
        for (const c5m::XmlElement& era : c.children) {
            if (era.name != "Era") continue;
            if (slot >= kEraSlots)
                return Status::fail(Err::BadLayout, source + ": culture " + *nm + " has more than 4 <Era>");
            const c5m::XmlElement* pb = era.child("PopulationBuildings");
            const c5m::XmlElement* g = pb ? pb->child("Granny") : nullptr;
            if (!g || g->text.empty())
                return Status::fail(Err::BadLayout, source + ": culture " + *nm + " era " + std::to_string(slot) +
                                                        " has no <PopulationBuildings><Granny>");
            CitySet s;
            s.culture = static_cast<uint32_t>(ci);
            s.eraSlot = slot++;
            s.lighting = lighting;
            const std::string* en = era.attr("name");
            s.eraName = en ? *en : std::string();
            s.granny = c5m::basename_lower(g->text);
            s.source = source;
            v.push_back(s);
        }
    }
    out.insert(out.end(), v.begin(), v.end());
    return Status::Ok();
}

std::vector<WonderForm> wonderForms(const std::string& tag, const std::vector<std::string>& artTypes) {
    std::vector<WonderForm> out;
    if (tag.empty()) return out;
    for (const std::string& t : artTypes) {
        WonderForm f;
        f.type = t;
        if (t == tag) {
            out.push_back(f);
            continue;
        }
        if (t.size() > tag.size() + 1 && t.compare(t.size() - tag.size(), tag.size(), tag) == 0 &&
            t[t.size() - tag.size() - 1] == ' ') {
            const std::string q = t.substr(0, t.size() - tag.size() - 1);
            for (uint32_t c = 0; c < kCultures; ++c)
                if (*kCultureAdjectives[c] && q == kCultureAdjectives[c]) f.kind = FormKind::Cultural;
            for (const char* w : kEraWords)
                if (q == w) f.kind = FormKind::Era;
            if (f.kind != FormKind::Plain) {
                f.qualifier = q;
                out.push_back(f);
            }
            continue;
        }
        if (t.size() > tag.size() + 1 && t.compare(0, tag.size(), tag) == 0 && t[tag.size()] == ' ') {
            const std::string q = t.substr(tag.size() + 1);
            for (const char* w : kRandomWords)
                if (q == w) {
                    f.kind = FormKind::Random;
                    f.qualifier = q;
                    out.push_back(f);
                }
        }
    }
    std::sort(out.begin(), out.end(), [](const WonderForm& a, const WonderForm& b) { return a.type < b.type; });
    return out;
}

// ---------------------------------------------------------------------------------------------- the catalogue
Status ArtCatalogue::load(const gamefs::GameFiles& files, c5m::TableSource& db) {
    *this = ArtCatalogue();
    ArtCatalogue c;
    // City sets: the base file first, then every other Civ5ArtDefines_CityBuildings*.xml by name.
    const std::string base = "civ5artdefines_citybuildings.xml";
    const std::string prefix = "civ5artdefines_citybuildings";
    std::vector<std::string> names;
    for (const std::string& n : files.names())
        if (n.size() > prefix.size() + 4 && n.compare(0, prefix.size(), prefix) == 0 &&
            n.compare(n.size() - 4, 4, ".xml") == 0 && n != base)
            names.push_back(n);
    std::sort(names.begin(), names.end());
    if (files.exists(base)) names.insert(names.begin(), base);
    if (names.empty()) return Status::fail(Err::NotFound, "no Civ5ArtDefines_CityBuildings*.xml in the game's files");
    std::vector<CitySet> all;
    for (const std::string& n : names) {
        std::vector<uint8_t> b;
        Status s = files.read(n, b);
        if (!s.ok()) return Status::fail(s.code, n + ": " + s.msg);
        C5R_TRY(parseCitySetFile(reinterpret_cast<const char*>(b.data()), b.size(), n, all));
        c.setFiles_.push_back(n);
    }
    // A later entry for the same culture and slot replaces an earlier one [I].
    std::map<std::pair<uint32_t, uint32_t>, CitySet> bySlot;
    for (const CitySet& s : all) bySlot[std::make_pair(s.culture, s.eraSlot)] = s;
    std::set<std::string> grannies;
    for (auto& kv : bySlot) {
        c.sets_.push_back(kv.second);
        grannies.insert(kv.second.granny);
    }
    c.counts_.citySetFiles = static_cast<uint32_t>(c.setFiles_.size());
    c.counts_.citySets = static_cast<uint32_t>(c.sets_.size());
    c.counts_.citySetModels = static_cast<uint32_t>(grannies.size());

    // Wonder art: the model reader's file list and parser.
    units::GameFilesProvider prov(files);
    c5m::ArtTables at;
    c5m::Error e;
    if (!at.load_wonders(prov, &e)) return Status::fail(Err::BadLayout, "wonder art: " + e.what);
    c.wonderMissing_ = at.wonder_files_missing;
    c.counts_.wonderArtEntries = static_cast<uint32_t>(at.wonders.size());
    std::vector<std::string> types;
    for (const c5m::WonderArt& w : at.wonders) {
        auto it = c.wonderIndex_.find(w.type);
        if (it == c.wonderIndex_.end()) {
            c.wonderIndex_[w.type] = c.wonders_.size();
            c.wonders_.push_back(w);
            types.push_back(w.type);
        } else {
            c.wonders_[it->second] = w;   // the last one loaded is in force (c5m::ArtTables::wonder)
        }
    }
    c.counts_.wonderArtTypes = static_cast<uint32_t>(c.wonders_.size());

    // Eras and their slots.
    std::vector<c5m::DbRow> rows;
    C5R_TRY(dbRows(db, "Eras", {"Type", "CityArtType", "ArtPrefix"}, rows));
    for (auto& r : rows) {
        EraRow er;
        er.type = txt(r, 0);
        er.cityArtType = txt(r, 1);
        er.artPrefix = txt(r, 2);
        c.eras_.push_back(er);
    }
    C5R_TRY(eraSlots(c.eras_, c.eraSlot_));

    // Buildings shown in the world.
    C5R_TRY(dbRows(db, "BuildingClasses", {"Type", "MaxGlobalInstances"}, rows));
    std::map<std::string, bool> worldClass;
    for (auto& r : rows) worldClass[txt(r, 0)] = std::atoi(txt(r, 1).c_str()) == 1;
    C5R_TRY(dbRows(db, "Buildings",
                   {"Type", "BuildingClass", "ArtDefineTag", "DisplayPosition", "ArtInfoCulturalVariation",
                    "ArtInfoEraVariation", "ArtInfoRandomVariation"},
                   rows));
    for (auto& r : rows) {
        const bool world = worldClass.count(txt(r, 1)) && worldClass[txt(r, 1)];
        if (world) ++c.counts_.worldWonders;
        const int dp = std::atoi(txt(r, 3).c_str());
        if (dp <= 0) continue;
        DisplayBuilding d;
        d.building = txt(r, 0);
        d.tag = txt(r, 2);
        d.displayPosition = dp;
        d.cultural = truthy(r, 4);
        d.era = truthy(r, 5);
        d.random = truthy(r, 6);
        d.worldWonder = world;
        d.forms = wonderForms(d.tag, types);
        if (d.forms.empty()) ++c.counts_.displayWithoutArt;
        if (world) ++c.counts_.displayWorldWonders;
        c.display_.push_back(d);
    }
    c.counts_.displayBuildings = static_cast<uint32_t>(c.display_.size());
    c.loaded_ = true;
    *this = c;
    return Status::Ok();
}

const CitySet* ArtCatalogue::citySet(uint32_t culture, uint32_t slot) const {
    for (const CitySet& s : sets_)
        if (s.culture == culture && s.eraSlot == slot) return &s;
    return nullptr;
}

const c5m::WonderArt* ArtCatalogue::wonder(const std::string& type) const {
    auto it = wonderIndex_.find(type);
    return it == wonderIndex_.end() ? nullptr : &wonders_[it->second];
}

int ArtCatalogue::eraSlot(const std::string& eraType) const {
    for (size_t i = 0; i < eras_.size() && i < eraSlot_.size(); ++i)
        if (eras_[i].type == eraType) return eraSlot_[i];
    return -1;
}

}}  // namespace c5r::cities
