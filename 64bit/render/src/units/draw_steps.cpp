// draw_steps.cpp - the module's own pose (L2) and placement (L3) sources (epic 0041, issue 4352).
//
// Pose: pose.h's ModulePose (4353), its bindAll as the pre-check's clip check.
// Placement: placement.h's placeUnit (4354) over UnitFormations.xml; the art row's Formation and the members' ZOffset
// come from the unit's resolved art (ArtResolver reads the same columns as placement.h's loadPlacementColumns).
#include "c5r/units/units.h"
#include "c5r/units/pose.h"

namespace c5r { namespace units {

namespace {

class CheckedPose : public ModulePose {
public:
    CheckedPose(c5m::FileProvider& files, const c5m::StringDatabase* strings) : ModulePose(files, strings) {}
    UnitStatus checkClips(const MemberArt& member, const UnitModel& model) override { return bindAll(member, model); }
};

class ModulePlacement : public PlacementSource {
public:
    FormationTable table;
    UnitStatus place(const PlacementRequest& r, UnitPlacement& out) override {
        out = UnitPlacement();
        if (!r.grid) return UnitStatus::fail(UnitCode::PlacementFailed, "no height field");
        PlacementInput in;
        in.formation = r.art->formation;
        for (auto& m : r.art->members)
            for (int k = 0; k < m.count; ++k) in.zOffset.push_back(m.zoffset);
        in.plotX = r.unit->plot_x;
        in.plotY = r.unit->plot_y;
        in.hasRoot = r.unit->has_root;
        in.rootX = r.unit->root_x;
        in.rootY = r.unit->root_y;
        in.facing = r.unit->facing;
        in.seed = r.unit->formation_seed;
        in.damage = r.unit->damage;
        in.maxHp = r.unit->max_hp;
        in.alive = r.unit->alive;
        in.waterLevel = r.water_level;
        in.footRadius = r.unit->foot_radius;   // issue 4982 (FixUnitPlacement); 0 = the EXE's rule
        Status s = placeUnit(table, *r.grid, in, out);
        if (!s.ok()) return UnitStatus::fail(UnitCode::PlacementFailed, r.art->art_row + ": " + s.str());
        return UnitStatus::Ok();
    }
};

}  // namespace

std::unique_ptr<PoseSource> makeModulePose(c5m::FileProvider& files, const c5m::StringDatabase* strings) {
    return std::unique_ptr<PoseSource>(new CheckedPose(files, strings));
}

std::unique_ptr<PlacementSource> makeModulePlacement(const gamefs::GameFiles& files, const std::wstring& installRoot,
                                                     Status* st) {
    std::unique_ptr<ModulePlacement> p(new ModulePlacement());
    Status s = p->table.load(files, installRoot);
    if (st) *st = s;
    return std::unique_ptr<PlacementSource>(p.release());
}

}}  // namespace c5r::units
