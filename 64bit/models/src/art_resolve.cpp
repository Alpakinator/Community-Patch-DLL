// art_resolve.cpp - art tables and the three resolution chains (landmark row, wonder, unit member) down to
// .fxsxml, .gr2 and .dds files. Epic 3100, issue 3104. Own code, GPL-3.0-or-later.
#include "c5models/art.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace c5m {

namespace {

void set_err(Error* err, Err code, const std::string& what) {
    if (err) *err = {code, what};
}

float to_float(const DbValue& v, float dflt) {
    if (v.null || v.text.empty()) return dflt;
    return std::strtof(v.text.c_str(), nullptr);
}

FileRef lookup(FileProvider& files, const std::string& name) {
    FileRef r;
    r.name = name;
    std::string key = basename_lower(name);
    r.found = !key.empty() && files.exists(key, &r.origin);
    if (r.found) {
        auto d = files.duplicates(key);
        for (auto& o : d)
            if (o != r.origin) r.others.push_back(o);
    }
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- tables
bool ArtTables::load_database(TableSource& db, Error* err) {
    std::vector<DbRow> rows;
    if (!db.rows("ArtDefine_Landmarks",
                 {"ImprovementType", "ResourceType", "Era", "State", "LayoutHandler", "Model", "Tech", "Scale",
                  "TerrainContour"},
                 rows, err))
        return false;
    landmarks.clear();
    for (auto& r : rows) {
        LandmarkRow l;
        l.improvement = r[0].text;
        l.resource = r[1].text;
        l.era = r[2].text;
        l.state = r[3].text;
        l.layout = r[4].text;
        l.model = r[5].text;
        l.model_null = r[5].null || r[5].text.empty();
        l.tech = r[6].text;
        l.scale = to_float(r[7], 1.0f);
        l.terrain_contour = r[8].text;
        landmarks.push_back(std::move(l));
    }
    if (!db.rows("Units", {"Type", "UnitArtInfo"}, rows, err)) return false;
    unit_art.clear();
    for (auto& r : rows)
        if (!r[1].null) unit_art[r[0].text] = r[1].text;
    if (!db.rows("ArtDefine_UnitInfoMemberInfos", {"UnitInfoType", "UnitMemberInfoType", "NumMembers"}, rows, err))
        return false;
    unit_info_members.clear();
    for (auto& r : rows) unit_info_members[r[0].text].push_back({r[1].text, std::atoi(r[2].text.c_str())});
    if (!db.rows("ArtDefine_UnitMemberInfos", {"Type", "Model", "Scale", "Domain", "MaterialTypeTag"}, rows, err))
        return false;
    unit_members.clear();
    unit_member_order.clear();
    for (auto& r : rows) {
        UnitMemberInfo m;
        m.type = r[0].text;
        m.model = r[1].text;
        m.model_null = r[1].null || r[1].text.empty();
        m.scale = to_float(r[2], 0);
        m.domain = r[3].text;
        m.material = r[4].text;
        if (!unit_members.count(m.type)) unit_member_order.push_back(m.type);
        unit_members[m.type] = m;
    }
    if (!db.rows("Buildings", {"Type", "ArtDefineTag"}, rows, err)) return false;
    building_art.clear();
    for (auto& r : rows)
        if (!r[1].null && !r[1].text.empty()) building_art[r[0].text] = r[1].text;
    return true;
}

bool ArtTables::load_wonders(FileProvider& files, Error* err) {
    wonders.clear();
    wonder_files_missing.clear();
    for (size_t i = 0; i < kWonderArtFileCount; ++i) {
        Bytes b;
        std::string origin;
        if (!files.read(kWonderArtFiles[i], b, &origin)) {
            wonder_files_missing.push_back(kWonderArtFiles[i]);
            continue;
        }
        if (!parse_wonder_art((const char*)b.data(), b.size(), origin, wonders, err)) return false;
    }
    return true;
}

const WonderArt* ArtTables::wonder(const std::string& type, int* entries) const {
    const WonderArt* r = nullptr;
    int n = 0;
    for (auto& w : wonders)
        if (w.type == type) {
            r = &w;
            ++n;
        }
    if (entries) *entries = n;
    return r;
}

// ---------------------------------------------------------------------------------------------- results
size_t ResolvedArt::missing_animations() const {
    size_t n = 0;
    for (auto& a : animations) n += !a.file.found;
    return n;
}

size_t ResolvedArt::missing_textures() const {
    size_t n = 0;
    for (auto& t : textures) n += !t.found;
    return n;
}

bool ResolvedArt::complete() const {
    return fxsxml.found && mesh.found && missing_animations() == 0 && missing_textures() == 0;
}

bool resolve_fxsxml(FileProvider& files, const std::string& model_path, ResolvedArt& out, Error* err) {
    out.model_path = model_path;
    out.fxsxml = FileRef();
    out.fxsxml.name = model_path;
    std::string key = basename_lower(model_path);
    Bytes b;
    std::string origin;
    if (key.empty() || !files.read(key, b, &origin)) {
        out.problems.push_back("missing .fxsxml " + (key.empty() ? std::string("(empty model)") : key));
        set_err(err, Err::NotFound, ".fxsxml not found: " + (key.empty() ? model_path : key));
        return false;
    }
    out.fxsxml = lookup(files, model_path);
    out.fxsxml.found = true;
    out.fxsxml.origin = origin;
    FxsAsset a;
    Error e;
    if (!parse_fxsxml((const char*)b.data(), b.size(), a, &e)) {
        out.problems.push_back("malformed .fxsxml " + key + ": " + e.what);
        set_err(err, Err::Xml, key + " (" + origin + "): " + e.what);
        return false;
    }
    out.mesh = lookup(files, a.mesh);
    if (!out.mesh.found) out.problems.push_back("missing mesh " + a.mesh);
    if (a.mesh_count > 1) out.problems.push_back(std::to_string(a.mesh_count) + " <Mesh> elements; the first is used");
    for (auto& an : a.animations) {
        ResolvedAnimation r;
        r.file = lookup(files, an.file);
        r.ec_text = an.ec_text;
        r.ec = an.ec;
        r.name = an.name;
        if (!r.file.found) out.problems.push_back("missing animation " + an.file + " (ec " + an.ec_text + ")");
        out.animations.push_back(std::move(r));
    }
    for (auto& t : a.textures) {
        out.textures.push_back(lookup(files, t));
        if (!out.textures.back().found) {
            // Some art names a source file (.psd, .tga, .png) where the archives hold a .dds of the same stem; say
            // so, without treating it as found (whether the engine substitutes is not known).
            std::string b = basename_lower(t);
            size_t dot = b.rfind('.');
            std::string dds = (dot == std::string::npos ? b : b.substr(0, dot)) + ".dds";
            std::string o;
            bool alt = dds != b && files.exists(dds, &o);
            out.problems.push_back("missing texture " + t + (alt ? " (a " + dds + " exists: " + o + ")" : ""));
        }
    }
    for (auto& d : a.decision_trees) {
        out.decision_trees.push_back(lookup(files, d));
        if (!out.decision_trees.back().found) out.problems.push_back("missing decision tree " + d);
    }
    for (auto& t : a.timed_triggers) {
        ResolvedTrigger r;
        r.file = lookup(files, t.file);
        r.state = t.state;
        if (!r.file.found) out.problems.push_back("missing timed trigger " + t.file);
        out.timed_triggers.push_back(std::move(r));
    }
    for (auto& s : a.state_machines) out.state_machines.push_back(lookup(files, s));
    for (auto& g : a.anim_graphs) out.anim_graphs.push_back(lookup(files, g));
    out.bones = a.bones;
    out.unknown_elements = a.unknown;
    return true;
}

// ---------------------------------------------------------------------------------------------- chain 1
bool resolve_landmark(const ArtTables& t, FileProvider& files, const std::string& improvement,
                      const std::string& resource, const std::string& era, const std::string& state,
                      std::vector<ResolvedArt>& out, Error* err) {
    size_t matched = 0;
    for (auto& l : t.landmarks) {
        if (l.improvement != improvement || l.resource != resource) continue;
        if (!era.empty() && l.era != era) continue;
        if (!state.empty() && l.state != state) continue;
        ++matched;
        if (l.model_null) continue;   // the state draws no model
        ResolvedArt r;
        r.kind = "landmark";
        r.key = l.improvement + "|" + l.resource + "|" + l.era + "|" + l.state;
        r.source_row = "ArtDefine_Landmarks LayoutHandler=" + l.layout + " Scale=" + std::to_string(l.scale) +
                       " TerrainContour=" + l.terrain_contour + (l.tech.empty() ? "" : " Tech=" + l.tech);
        r.scale = l.scale;
        resolve_fxsxml(files, l.model, r, nullptr);   // a missing or malformed .fxsxml is recorded in r.problems
        out.push_back(std::move(r));
    }
    if (!matched) {
        set_err(err, Err::NotFound, "no ArtDefine_Landmarks row for " + improvement + " / " + resource +
                                        (era.empty() ? "" : " / " + era) + (state.empty() ? "" : " / " + state));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------- chain 2
bool resolve_wonder_type(const ArtTables& t, FileProvider& files, const std::string& type, ResolvedArt& out,
                         Error* err) {
    int n = 0;
    const WonderArt* w = t.wonder(type, &n);
    out.kind = "wonder";
    if (out.key.empty()) out.key = type;
    if (!w) {
        set_err(err, Err::NotFound, "no WonderArtInfo with Type \"" + type + "\"");
        out.problems.push_back("no WonderArtInfo with Type \"" + type + "\"");
        return false;
    }
    out.source_row += (out.source_row.empty() ? "" : " -> ") + std::string("WonderArtInfo Type=\"") + type +
                      "\" state=" + w->state + " fScale=" + std::to_string(w->scale) + " in " + w->source;
    out.scale = w->scale;
    if (n > 1) {
        std::string srcs;
        for (auto& x : t.wonders)
            if (x.type == type) srcs += (srcs.empty() ? "" : ", ") + x.source + " (" + basename_lower(x.granny) + ")";
        out.problems.push_back(std::to_string(n) + " WonderArtInfo entries for \"" + type +
                               "\", the last loaded is used [I]: " + srcs);
    }
    return resolve_fxsxml(files, w->granny, out, err);
}

bool resolve_wonder(const ArtTables& t, FileProvider& files, const std::string& building, ResolvedArt& out,
                    Error* err) {
    out.kind = "wonder";
    auto it = t.building_art.find(building);
    if (it == t.building_art.end()) {
        set_err(err, Err::NotFound, "Buildings has no ArtDefineTag for " + building);
        return false;
    }
    out.key = building + "|" + it->second;
    out.source_row = "Buildings.ArtDefineTag=\"" + it->second + "\"";
    return resolve_wonder_type(t, files, it->second, out, err);
}

// ---------------------------------------------------------------------------------------------- chain 3
bool resolve_unit_member(const ArtTables& t, FileProvider& files, const std::string& member, ResolvedArt& out,
                         Error* err) {
    out.kind = "unit";
    if (out.key.empty()) out.key = member;
    auto it = t.unit_members.find(member);
    if (it == t.unit_members.end()) {
        set_err(err, Err::NotFound, "no ArtDefine_UnitMemberInfos row " + member);
        out.problems.push_back("no ArtDefine_UnitMemberInfos row " + member);
        return false;
    }
    const UnitMemberInfo& m = it->second;
    out.scale = m.scale;
    out.source_row += (out.source_row.empty() ? "" : " -> ") + std::string("ArtDefine_UnitMemberInfos ") + member +
                      " Scale=" + std::to_string(m.scale) + " Domain=" + m.domain + " MaterialTypeTag=" + m.material;
    if (m.model_null) {
        set_err(err, Err::NotFound, member + " has no Model");
        out.problems.push_back(member + " has no Model");
        return false;
    }
    return resolve_fxsxml(files, m.model, out, err);
}

bool resolve_unit(const ArtTables& t, FileProvider& files, const std::string& unit, std::vector<ResolvedArt>& out,
                  Error* err) {
    auto u = t.unit_art.find(unit);
    if (u == t.unit_art.end()) {
        set_err(err, Err::NotFound, "Units has no UnitArtInfo for " + unit);
        return false;
    }
    auto mi = t.unit_info_members.find(u->second);
    if (mi == t.unit_info_members.end()) {
        set_err(err, Err::NotFound, "no ArtDefine_UnitInfoMemberInfos rows for " + u->second + " (unit " + unit + ")");
        return false;
    }
    for (auto& m : mi->second) {
        ResolvedArt r;
        r.key = unit + "|" + u->second + "|" + m.member;
        r.source_row = "Units.UnitArtInfo=" + u->second + " -> ArtDefine_UnitInfoMemberInfos NumMembers=" +
                       std::to_string(m.count);
        resolve_unit_member(t, files, m.member, r, nullptr);   // failures recorded in r.problems
        out.push_back(std::move(r));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------- text
namespace {

void put_ref(std::ostringstream& o, const char* label, const FileRef& f, const std::string& extra = std::string()) {
    o << "  " << label << " " << f.name << extra << "  ";
    if (f.found) o << "<- " << f.origin;
    else o << "MISSING";
    if (!f.others.empty()) {
        o << "  (also:";
        for (auto& x : f.others) o << " " << x;
        o << ")";
    }
    o << "\n";
}

}  // namespace

std::string format_resolved(const ResolvedArt& r) {
    std::ostringstream o;
    o << r.kind << " " << r.key << (r.complete() ? "  [complete]" : "  [INCOMPLETE]") << "\n";
    if (!r.source_row.empty()) o << "  via " << r.source_row << "\n";
    char sc[32];
    std::snprintf(sc, sizeof sc, "%g", r.scale);
    o << "  model " << r.model_path << "  scale " << sc << "\n";
    put_ref(o, "fxsxml", r.fxsxml);
    if (r.fxsxml.found) {
        put_ref(o, "mesh", r.mesh);
        for (auto& a : r.animations) put_ref(o, "animation", a.file, " ec=" + a.ec_text + (a.name.empty() ? "" : " name=" + a.name));
        for (auto& t : r.textures) put_ref(o, "texture", t);
        for (auto& d : r.decision_trees) put_ref(o, "decisiontree", d);
        for (auto& t : r.timed_triggers) put_ref(o, "timedtrigger", t.file, t.state.empty() ? "" : " state=" + t.state);
        for (auto& s : r.state_machines) put_ref(o, "statemachine", s);
        for (auto& g : r.anim_graphs) put_ref(o, "animgraph", g);
        if (!r.bones.empty()) {
            o << "  bones";
            for (auto& b : r.bones) o << " [" << b << "]";
            o << "\n";
        }
        for (auto& u : r.unknown_elements) o << "  unknown element <" << u.first << "> x" << u.second << "\n";
    }
    for (auto& p : r.problems) o << "  problem: " << p << "\n";
    return o.str();
}

}  // namespace c5m
