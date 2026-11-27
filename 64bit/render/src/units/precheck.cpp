// precheck.cpp - the unit module's pre-check: every unit type of the database, with every art style suffix and every
// era present, through L0 (art row, members, files), L1 (models, repacks, textures) and the clip check of the pose
// step, without drawing (epic 0041, issue 4352; unit-renderer.md 7; MC #1845 decision 7: every art row the game can
// reach). U6 calls it at game start.
// Issue 4460 (epic 0375; MC ruling 2026-11-16, file granted to 0375): an animation file that the member's .fxsxml names
// but that the install (and the mods) do not ship is not a failure: it is the named warning "ClipMissingInInstall: ..."
// on a row that stays Ok (the unit is drawn), and the clip binding checks the clips that exist. VP's database reaches one
// such row (ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER: U_Assyrian_Siege_Tower_Charge_Attack.gr2 is shipped by no package), and
// failing it ended every game with render_units=1. A missing model, .fxsxml or anything else stays a failure (D125).
#include "c5r/units/units.h"

#include <algorithm>
#include <set>

namespace c5r { namespace units {

Status UnitRenderer::precheck(std::vector<UnitTypeStatus>& out) {
    out.clear();
    if (!open_) return Status::fail(Err::NotFound, "unit renderer not opened");
    PoseSource* pose = pose_ ? pose_ : ownPose_.get();
    std::vector<std::string> eras = art_.eras();
    if (eras.empty()) eras.push_back(std::string());
    for (const std::string& type : art_.unit_types()) {
        std::set<std::string> seen;   // art rows, or failure texts, already recorded for this type
        for (const std::string& suffix : art_.style_suffixes())
            for (const std::string& era : eras) {
                UnitTypeStatus e;
                e.unit_type = type;
                e.art_style = suffix;
                e.era = era;
                e.status = art_.art_row(type, suffix, era, e.art_row);
                const std::string key = e.status.ok() ? e.art_row : "!" + e.status.str();
                if (!seen.insert(key).second) continue;
                if (e.status.ok()) {
                    UnitInput u;
                    u.unit_type = type;
                    u.art_row = e.art_row;
                    UnitArt art;
                    std::vector<const UnitModel*> models;
                    e.status = loadUnit(u, nullptr, art, models);
                    std::vector<std::string> missing;   // issue 4460: ClipMissingInInstall warnings of this row
                    for (size_t m = 0; e.status.ok() && m < art.members.size(); ++m) {
                        const MemberArt& ma = art.members[m];
                        MemberArt shipped = ma;   // the member with the clips the install ships
                        shipped.files.animations.clear();
                        for (auto& a : ma.files.animations) {
                            if (a.file.found)
                                shipped.files.animations.push_back(a);
                            else
                                missing.push_back("ClipMissingInInstall: " + e.art_row + ": member " + ma.member +
                                                  ": animation file " + a.file.name +
                                                  " is named by the art file but shipped by no package (the unit is drawn)");
                        }
                        UnitStatus c = pose->checkClips(shipped, *models[m]);
                        if (!c.ok()) e.status = UnitStatus::fail(c.code, e.art_row + ": member " + ma.member + ": " + c.text);
                    }
                    if (e.status.ok())
                        e.status.warnings.insert(e.status.warnings.end(), missing.begin(), missing.end());
                }
                out.push_back(std::move(e));
            }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const UnitTypeStatus& a, const UnitTypeStatus& b) { return a.unit_type < b.unit_type; });
    return Status::Ok();
}

}}  // namespace c5r::units
