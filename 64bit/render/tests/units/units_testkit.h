// units_testkit.h - helpers shared by the 4352 tests of the unit module (epic 0041): an in-memory FileProvider, a
// TSV table builder and the status assertion. No game data.
#pragma once
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/units/art.h"
#include "c5r/units/status.h"
#include "testkit.h"

#define CHECK_UNIT(st, want) do { ::c5r::units::UnitStatus u_ = (st); if (u_.code != (want)) \
    ::c5r::test::fail(__FILE__, __LINE__, std::string("CHECK_UNIT(" #st "): want ") + \
                      ::c5r::units::unitCodeName(want) + ", got " + u_.str()); \
    else if (u_.code != ::c5r::units::UnitCode::Ok && u_.text.empty()) \
    ::c5r::test::fail(__FILE__, __LINE__, "CHECK_UNIT(" #st "): status without a text"); } while (0)

namespace units_test {

class MemFiles : public c5m::FileProvider {
public:
    std::map<std::string, std::string> files;   // lower-case basename -> bytes
    bool read(const std::string& name, c5m::Bytes& out, std::string* origin) override {
        auto it = files.find(name);
        if (it == files.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        if (origin) *origin = "mem:" + name;
        return true;
    }
};

// One table as TSV text: header row then rows, fields joined by tabs.
inline std::string tsv(const std::vector<std::vector<std::string>>& rows) {
    std::string s;
    for (auto& r : rows) {
        for (size_t i = 0; i < r.size(); ++i) s += (i ? "\t" : "") + r[i];
        s += "\n";
    }
    return s;
}

// Issue 4464: every field of two resolved arts (the cache's result against the rule's); "" when equal, else the first
// difference named. Floats compared bit for bit.
inline bool sameRef(const c5m::FileRef& a, const c5m::FileRef& b) {
    return a.name == b.name && a.found == b.found && a.origin == b.origin && a.others == b.others;
}
inline bool sameRefs(const std::vector<c5m::FileRef>& a, const std::vector<c5m::FileRef>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!sameRef(a[i], b[i])) return false;
    return true;
}
inline bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
inline std::string resolvedDiff(const c5m::ResolvedArt& a, const c5m::ResolvedArt& b) {
    if (a.kind != b.kind || a.key != b.key || a.source_row != b.source_row || a.model_path != b.model_path) return "names";
    if (!sameBits(a.scale, b.scale)) return "scale";
    if (!sameRef(a.fxsxml, b.fxsxml)) return "fxsxml";
    if (!sameRef(a.mesh, b.mesh)) return "mesh";
    if (a.animations.size() != b.animations.size()) return "animation count";
    for (size_t i = 0; i < a.animations.size(); ++i) {
        const c5m::ResolvedAnimation &x = a.animations[i], &y = b.animations[i];
        if (!sameRef(x.file, y.file) || x.ec_text != y.ec_text || x.ec != y.ec || x.name != y.name)
            return "animation " + std::to_string(i);
    }
    if (!sameRefs(a.textures, b.textures)) return "textures";
    if (!sameRefs(a.decision_trees, b.decision_trees)) return "decision trees";
    if (a.timed_triggers.size() != b.timed_triggers.size()) return "trigger count";
    for (size_t i = 0; i < a.timed_triggers.size(); ++i)
        if (!sameRef(a.timed_triggers[i].file, b.timed_triggers[i].file) || a.timed_triggers[i].state != b.timed_triggers[i].state)
            return "trigger " + std::to_string(i);
    if (!sameRefs(a.state_machines, b.state_machines)) return "state machines";
    if (!sameRefs(a.anim_graphs, b.anim_graphs)) return "anim graphs";
    if (a.bones != b.bones) return "bones";
    if (a.unknown_elements != b.unknown_elements) return "unknown elements";
    if (a.problems != b.problems) return "problems";
    return std::string();
}
inline std::string artDiff(const c5r::units::UnitArt& a, const c5r::units::UnitArt& b) {
    if (a.unit_type != b.unit_type) return "unit_type";
    if (a.art_row != b.art_row) return "art_row";
    if (a.formation != b.formation) return "formation";
    if (a.damage_states != b.damage_states) return "damage_states";
    if (a.members.size() != b.members.size()) return "member count";
    for (size_t i = 0; i < a.members.size(); ++i) {
        const c5r::units::MemberArt &x = a.members[i], &y = b.members[i];
        const std::string m = "member " + std::to_string(i) + " (" + x.member + "): ";
        if (x.member != y.member || x.count != y.count || x.domain != y.domain || x.material_tag != y.material_tag ||
            x.model != y.model)
            return m + "row fields";
        if (!sameBits(x.scale, y.scale) || !sameBits(x.zoffset, y.zoffset)) return m + "scale or zoffset";
        const std::string r = resolvedDiff(x.files, y.files);
        if (!r.empty()) return m + r;
    }
    return std::string();
}

}  // namespace units_test
