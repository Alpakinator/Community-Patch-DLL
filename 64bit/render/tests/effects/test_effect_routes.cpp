// test_effect_routes.cpp - tests of the trigger intake of a fight's effects (c5r/effects/effect_routes.h; epic 0042,
// ticket 4793). Own code, GPL-3.0-or-later. Synthetic definitions in memory; the install part (C5R_GAME_DIR) checks the
// resolver against the install's 538 VisEffects (effects-formats.md 14) and the held rows against the trigger files.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5models/effect_xml.h"
#include "c5models/logic.h"
#include "c5r/effects/effect_routes.h"
#include "c5r/effects/projectile_model.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/art.h"
#include "testkit.h"

using namespace c5r::effects;
using c5r::unitanim::CombatHit;
using c5r::unitanim::HitResult;

namespace {

class Mem : public c5m::FileProvider {
public:
    std::map<std::string, std::string> files;
    bool read(const std::string& n, c5m::Bytes& out, std::string* origin) override {
        auto it = files.find(n);
        if (it == files.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        if (origin) *origin = "mem:" + n;
        return true;
    }
};

std::string vis(const std::string& name, const std::string& body = "") {
    return "<VisEffectArtInfo><Name>" + name + "</Name><Transform><Translation x=\"0\" y=\"0\" z=\"0\"/>"
           "<Rotation x=\"0\" y=\"0\" z=\"0\"/><Scale>1.0</Scale></Transform><fBaseDuration>0.5</fBaseDuration>"
           "<fBaseDurationScale>2.0</fBaseDurationScale>" + body + "</VisEffectArtInfo>";
}
std::string child(const std::string& n) {
    return "<ChildEffects><ChildEffect trigger=\"ProjectileEnd\"><Name>" + n + "</Name></ChildEffect></ChildEffects>";
}

// A small library: impact families with and without surfaces, a projectile with a speed and a $(TERRAIN) child, the
// empty $(WEAPON) variant, a city volley effect.
bool loadLib(c5m::EffectLibrary& lib) {
    Mem fp;
    fp.files["civ5artdefines_viseffects.xml"] =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<VisEffectArtInfos>" +
        vis("ART_DEF_VEFFECT_HIT_DIRT") + vis("ART_DEF_VEFFECT_HIT_SNOW") +
        vis("ART_DEF_VEFFECT_FIRE_IMPACT_DIRT") + vis("ART_DEF_VEFFECT_FIRE_IMPACT_CITY") +
        vis("ART_DEF_VEFFECT_ROCK", "<fMovementSpeed>3.3</fMovementSpeed>" + child("ART_DEF_VEFFECT_FIRE_IMPACT_$(TERRAIN)")) +
        vis("ART_DEF_VEFFECT_ARROW", child("ART_DEF_VEFFECT_HIT_$(TERRAIN)")) +
        vis("ART_DEF_VEFFECT_DEATH_SM_") + vis("ART_DEF_VEFFECT_DEATH_SM_METAL") +
        vis("CITY_BOMBARD_EARLY", child("ART_DEF_VEFFECT_HIT_$(TERRAIN)")) +
        vis("ART_DEF_VEFFECT_MG_HIT_DIRT") + vis("ART_DEF_VEFFECT_MUZZLE") + "</VisEffectArtInfos>";
    c5m::Error e;
    return lib.load(fp, c5m::fx_load_list({}), &e);
}

CombatHit hitAt(float x, float y, double t) {
    CombatHit h;
    h.time = t;
    h.launch = t;
    h.player = 1;
    h.id = 7;
    h.figure = 2;
    h.has_pos = true;
    h.pos[0] = x;
    h.pos[1] = y;
    h.pos[2] = 0;
    h.result = HitResult::Death;
    return h;
}

}  // namespace

C5R_TEST(names_exact_fallback_weapon_and_refusals) {
    c5m::EffectLibrary lib;
    CHECK(loadLib(lib));
    EffectNames names(lib);
    std::string out, why;
    // exact, with and without variables
    CHECK(names.resolve("ART_DEF_VEFFECT_MUZZLE", NameValues(), out) == NameResult::Exact && out == "ART_DEF_VEFFECT_MUZZLE");
    CHECK(names.resolve("ART_DEF_VEFFECT_HIT_$(TERRAIN)", NameValues().set_terrain("SNOW"), out) == NameResult::Exact &&
          out == "ART_DEF_VEFFECT_HIT_SNOW");
    // the surface's variant missing: DIRT, counted per name
    CHECK(names.resolve("ART_DEF_VEFFECT_HIT_$(TERRAIN)", NameValues().set_terrain("SAND"), out) == NameResult::Fallback &&
          out == "ART_DEF_VEFFECT_HIT_DIRT");
    CHECK(names.resolve("ART_DEF_VEFFECT_FIRE_IMPACT_$(TERRAIN)", NameValues().set_terrain("SNOW"), out) ==
              NameResult::Fallback && out == "ART_DEF_VEFFECT_FIRE_IMPACT_DIRT");
    CHECK(names.resolve("ART_DEF_VEFFECT_FIRE_IMPACT_$(TERRAIN)", NameValues().set_terrain("CITY"), out) == NameResult::Exact);
    // no surface known: DIRT too
    CHECK(names.resolve("ART_DEF_VEFFECT_HIT_$(TERRAIN)", NameValues(), out) == NameResult::Fallback &&
          out == "ART_DEF_VEFFECT_HIT_DIRT");
    // $(BASETERRAIN) takes the terrain's value when only that is given
    CHECK(names.resolve("ART_DEF_VEFFECT_HIT_$(BASETERRAIN)", NameValues().set_terrain("SNOW"), out) == NameResult::Exact &&
          out == "ART_DEF_VEFFECT_HIT_SNOW");
    // $(WEAPON): a value, or the empty variant when none is known
    CHECK(names.resolve("ART_DEF_VEFFECT_DEATH_SM_$(WEAPON)", NameValues().set_weapon("METAL"), out) == NameResult::Exact &&
          out == "ART_DEF_VEFFECT_DEATH_SM_METAL");
    CHECK(names.resolve("ART_DEF_VEFFECT_DEATH_SM_$(WEAPON)", NameValues(), out) == NameResult::WeaponEmpty &&
          out == "ART_DEF_VEFFECT_DEATH_SM_");
    // a weapon value whose variant is not defined: refused (no fallback for weapons)
    CHECK(names.resolve("ART_DEF_VEFFECT_DEATH_SM_$(WEAPON)", NameValues().set_weapon("BLUNT"), out, &why) ==
              NameResult::NotDefined && why.find("ART_DEF_VEFFECT_DEATH_SM_BLUNT") != std::string::npos);
    // refusals by name: dangling, unknown variable, unterminated, no variant for any surface
    CHECK(names.resolve("ART_DEF_VEFFECT_SEAGULLS_01", NameValues(), out, &why) == NameResult::NotDefined &&
          why.find("SEAGULLS") != std::string::npos);
    CHECK(names.resolve("ART_DEF_VEFFECT_X_$(COLOR)", NameValues(), out, &why) == NameResult::BadName &&
          why.find("COLOR") != std::string::npos);
    CHECK(names.resolve("ART_DEF_VEFFECT_X_$(TERRAIN", NameValues().set_terrain("DIRT"), out, &why) == NameResult::BadName);
    CHECK(names.resolve("ART_DEF_VEFFECT_NONE_$(TERRAIN)", NameValues().set_terrain("SNOW"), out, &why) ==
              NameResult::NotDefined);
    const NameStats& s = names.stats();
    std::printf("names: exact %zu fallback %zu weapon_empty %zu not_defined %zu bad %zu\n", s.exact, s.fallback,
                s.weapon_empty, s.not_defined, s.bad_name);
    CHECK_EQ(s.exact, (size_t)5);
    CHECK_EQ(s.fallback, (size_t)3);
    CHECK_EQ(s.weapon_empty, (size_t)1);
    CHECK_EQ(s.not_defined, (size_t)3);
    CHECK_EQ(s.bad_name, (size_t)2);
    CHECK(s.fell_back.count("ART_DEF_VEFFECT_HIT_DIRT") && s.fell_back.at("ART_DEF_VEFFECT_HIT_DIRT") == 2);
    CHECK(s.refused.count("ART_DEF_VEFFECT_SEAGULLS_01"));
    // children with the target's surface and the fallback; durations and speeds
    std::vector<std::string> ch;
    CHECK(names.children("ART_DEF_VEFFECT_ROCK", NameValues().set_terrain("SNOW"), ch) == 0);
    CHECK(ch.size() == 1 && ch[0] == "ART_DEF_VEFFECT_FIRE_IMPACT_DIRT");
    CHECK(names.children("ART_DEF_VEFFECT_ARROW", NameValues().set_terrain("SNOW"), ch) == 0 && ch.size() == 1 &&
          ch[0] == "ART_DEF_VEFFECT_HIT_SNOW");
    CHECK(names.children("ART_DEF_VEFFECT_UNKNOWN", NameValues(), ch) == 0 && ch.empty());
    CHECK(std::fabs(names.baseDuration("ART_DEF_VEFFECT_MUZZLE") - 1.0f) < 1e-6f);
    CHECK(names.baseDuration("ART_DEF_VEFFECT_UNKNOWN") < 0);
    CHECK(std::fabs(names.movementSpeed("ART_DEF_VEFFECT_ROCK") - 3.3f) < 1e-6f);
    CHECK(names.movementSpeed("ART_DEF_VEFFECT_ARROW") == 0);
}

C5R_TEST(held_effects_from_transfer_rows) {
    const char* doc =
        "<trigger_system type=\"FGrannyTimedTriggerSystem\"><triggers>"
        "<trigger type=\"FTimedTriggerEffect\" id=\"1\" ec=\"1100\" time=\"0.3\" duration=\"0\" event=\"ART_DEF_VEFFECT_ARROW\" bone=\"Point_PROJECTILE\"/>"
        "<trigger type=\"FTimedTriggerTransfer\" id=\"2\" ec=\"1100\" time=\"0.5\" refid=\"1\" speed=\"17\"/>"
        "<trigger type=\"FTimedTriggerSound\" id=\"3\" ec=\"1100\" time=\"0.1\" event=\"AS3D_X\"/>"
        "<trigger type=\"FTimedTriggerTransfer\" id=\"4\" ec=\"1100\" time=\"0.6\" refid=\"3\"/>"
        "<trigger type=\"FTimedTriggerTransfer\" id=\"5\" ec=\"1100\" time=\"0.6\"/>"
        "<trigger type=\"FTimedTriggerEffect\" id=\"6\" ec=\"1100\" time=\"0.2\" duration=\"0\" event=\"ART_DEF_VEFFECT_MUZZLE\"/>"
        "</triggers></trigger_system>";
    c5m::TriggerSystem ts;
    c5m::Error e;
    if (!c5m::parse_ftsxml(doc, std::strlen(doc), ts, &e)) {
        std::printf("parse: %s\n", e.what.c_str());
        CHECK(false);
        return;
    }
    HeldEffects h;
    CHECK_EQ(h.add("fx_triggers_x.ftsxml", ts), (size_t)1);
    int32_t tr = -1;
    CHECK(h.held("fx_triggers_x.ftsxml", 1, &tr) && tr == 2);
    CHECK(!h.held("fx_triggers_x.ftsxml", 6));    // a muzzle flash: not handed over
    CHECK(!h.held("fx_triggers_x.ftsxml", 3));    // a Sound row handed over: not an effect
    CHECK(!h.held("other.ftsxml", 1));
    CHECK_EQ(h.refidOf("fx_triggers_x.ftsxml", 2), 1);
    CHECK_EQ(h.refidOf("fx_triggers_x.ftsxml", 4), -1);
    CHECK_EQ(h.refidOf("fx_triggers_x.ftsxml", 5), -1);
    CHECK_EQ(h.refidOf("fx_triggers_x.ftsxml", 99), -1);
    CHECK_EQ(h.files(), (size_t)1);
    CHECK_EQ(h.heldRows(), (size_t)1);
}

C5R_TEST(hit_routes) {
    c5m::EffectLibrary lib;
    CHECK(loadLib(lib));
    EffectNames names(lib);
    HitPlanParams p;
    HitEffectPlan plan;
    // a machine gun's hit (no flight): one-shot at the landing, its surface's variant or DIRT
    CombatHit h = hitAt(100, 50, 2.0);
    h.effect = "ART_DEF_VEFFECT_MG_HIT_$(TERRAIN)";
    h.scale = 0.5f;
    CHECK(planHitEffect(h, names, NameValues().set_terrain("SNOW"), p, plan));
    CHECK(plan.route == HitRoute::OneShot && plan.effect == "ART_DEF_VEFFECT_MG_HIT_DIRT" && plan.time == 2.0 &&
          plan.pos[0] == 100 && plan.pos[1] == 50 && plan.scale == 0.5f);
    // HitEffectScale 0 or unset -> 0: the striking member's scale, filled by the caller (never 1.0: a hit at 1.0 is about
    // ten times stock's size, effects-formats.md 15.7.8)
    h.scale = 0;
    CHECK(planHitEffect(h, names, NameValues().set_terrain("DIRT"), p, plan) && plan.scale == 0.0f);
    h.scale = -2;
    CHECK(planHitEffect(h, names, NameValues().set_terrain("DIRT"), p, plan) && plan.scale == 0.0f);
    // a projectile's hit: the flight's (4792)
    h.projectile = true;
    CHECK(planHitEffect(h, names, NameValues(), p, plan) && plan.route == HitRoute::Flight && plan.effect.empty());
    // an area share and a hit without an effect: nothing
    h.projectile = false;
    h.area = true;
    CHECK(planHitEffect(h, names, NameValues(), p, plan) && plan.route == HitRoute::None && !plan.why.empty());
    h.area = false;
    h.effect.clear();
    CHECK(planHitEffect(h, names, NameValues(), p, plan) && plan.route == HitRoute::None);
    // no position: nothing, named
    h.effect = "ART_DEF_VEFFECT_MUZZLE";
    h.has_pos = false;
    CHECK(planHitEffect(h, names, NameValues(), p, plan) && plan.route == HitRoute::None &&
          plan.why.find("position") != std::string::npos);
    // a dangling name: refused, named
    h.has_pos = true;
    h.effect = "ART_DEF_VEFFECT_SEAGULLS_01";
    CHECK(planHitEffect(h, names, NameValues(), p, plan) && plan.route == HitRoute::Refused &&
          plan.why.find("SEAGULLS") != std::string::npos);
    // a city's answer in a melee: no effect
    h.effect = "CITY_BOMBARD_EARLY";
    h.city = true;
    CHECK(planHitEffect(h, names, NameValues(), p, plan) && plan.route == HitRoute::CityDefence);
    // a city's volley: an arc from the city's centre, launched at hit.launch, landing at hit.time, the children with
    // the target's surface
    CombatHit v = hitAt(200, 0, 1.1);
    v.launch = 0.5;
    v.city = true;
    v.projectile = true;
    v.figure = 0;
    v.effect = "CITY_BOMBARD_EARLY";
    v.has_origin = true;
    v.origin[0] = 0;
    v.origin[1] = 0;
    v.origin[2] = 0;
    CHECK(planHitEffect(v, names, NameValues().set_terrain("SNOW"), p, plan));
    CHECK(plan.route == HitRoute::CityVolley && plan.effect == "CITY_BOMBARD_EARLY");
    CHECK(plan.flight.effect == "CITY_BOMBARD_EARLY" && std::fabs(plan.flight.t0 - 0.5) < 1e-9 &&
          std::fabs(plan.flight.path.duration - 0.6f) < 1e-6f && plan.flight.hit);
    CHECK(plan.flight.children.size() == 1 && plan.flight.children[0] == "ART_DEF_VEFFECT_HIT_SNOW");
    CHECK(plan.flight.player == 1 && plan.flight.unit == 7);
    V3 pos, tan;
    CHECK(evalPath(plan.flight.path, 0, pos, tan));
    CHECK(std::fabs(pos.x) < 1e-3f && std::fabs(pos.z - p.city_launch_height) < 1e-3f && tan.z > 0.5f && tan.x > 0.5f);
    CHECK(evalPath(plan.flight.path, 1, pos, tan));
    CHECK(std::fabs(pos.x - 200) < 1e-2f && std::fabs(pos.y) < 1e-2f && std::fabs(pos.z) < 1e-2f);
    // the flight goes into a ProjectileSet and lands with its child where the hit lands
    ProjectileSet set;
    uint32_t handle = 0;
    std::string why;
    CHECK(set.add(plan.flight, handle, &why));
    std::vector<ProjectileRecord> recs;
    for (int i = 0; i <= 40; ++i) set.frame(i / 30.0, recs, nullptr);
    size_t starts = 0, children = 0;
    for (const ProjectileRecord& r : recs) {
        if (r.kind == RecordKind::EffectStart) ++starts;
        if (r.kind == RecordKind::ChildStart) {
            ++children;
            CHECK(r.effect == "ART_DEF_VEFFECT_HIT_SNOW" && r.time >= 1.1 - 1e-6);
            CHECK(std::fabs(r.world.m[12] - 200) < 1e-2f);
        }
    }
    CHECK(starts == 1 && children == 1 && set.live() == 0);
    // a volley with no flight time in the hit: the default flight, ending at the hit
    v.launch = v.time;
    CHECK(planHitEffect(v, names, NameValues(), p, plan) && plan.route == HitRoute::CityVolley &&
          std::fabs(plan.flight.t0 - (1.1 - 0.6)) < 1e-6 && std::fabs(plan.flight.path.duration - 0.6f) < 1e-6f);
    // a volley without the city's position: nothing
    v.has_origin = false;
    CHECK(planHitEffect(v, names, NameValues(), p, plan) && plan.route == HitRoute::None);
    // non-finite input: refused
    v.has_origin = true;
    v.time = std::numeric_limits<double>::quiet_NaN();
    CHECK(!planHitEffect(v, names, NameValues(), p, plan) && !plan.why.empty());
}

C5R_TEST(effect_speeds_into_combat_tables) {
    c5m::EffectLibrary lib;
    CHECK(loadLib(lib));
    EffectNames names(lib);
    const char* doc =
        "<trigger_system type=\"FGrannyTimedTriggerSystem\"><triggers>"
        "<trigger type=\"FTimedTriggerEffect\" id=\"1\" ec=\"1100\" time=\"0.3\" duration=\"0\" event=\"ART_DEF_VEFFECT_ROCK\" bone=\"B\"/>"
        "<trigger type=\"FTimedTriggerTransfer\" id=\"2\" ec=\"1100\" time=\"0.5\" refid=\"1\"/>"
        "</triggers></trigger_system>";
    const char* docArrow =
        "<trigger_system type=\"FGrannyTimedTriggerSystem\"><triggers>"
        "<trigger type=\"FTimedTriggerEffect\" id=\"1\" ec=\"1100\" time=\"0.3\" duration=\"0\" event=\"ART_DEF_VEFFECT_ARROW\" bone=\"B\"/>"
        "<trigger type=\"FTimedTriggerTransfer\" id=\"2\" ec=\"1100\" time=\"0.5\" refid=\"1\"/>"
        "</triggers></trigger_system>";
    c5m::TriggerSystem rock, arrow;
    c5m::Error e;
    CHECK(c5m::parse_ftsxml(doc, std::strlen(doc), rock, &e));
    CHECK(c5m::parse_ftsxml(docArrow, std::strlen(docArrow), arrow, &e));
    c5r::unitanim::CombatArtTables t;
    c5r::unitanim::CombatWeapon w;
    w.projectile = true;
    t["CATAPULT"].weapons.push_back(w);          // NULL ProjectileSpeed (0)
    c5r::unitanim::CombatWeapon melee;
    t["CATAPULT"].weapons.push_back(melee);      // not a projectile weapon: unchanged
    c5r::unitanim::CombatWeapon bomb;
    bomb.projectile = true;
    bomb.dropped = true;
    t["CATAPULT"].weapons.push_back(bomb);       // dropped: unchanged
    w.speed = 5.0f;
    t["ARCHER"].weapons.push_back(w);            // the arrow has no fMovementSpeed: ProjectileSpeed stays
    std::map<std::string, const c5m::TriggerSystem*> m;
    m["CATAPULT"] = &rock;
    m["ARCHER"] = &arrow;
    m["NOT_IN_TABLES"] = &rock;
    CHECK_EQ(applyEffectSpeeds(t, m, names), (size_t)1);
    CHECK(std::fabs(t["CATAPULT"].weapons[0].speed - 3.3f) < 1e-6f);
    CHECK(t["CATAPULT"].weapons[1].speed == 0 && t["CATAPULT"].weapons[2].speed == 0);
    CHECK(t["ARCHER"].weapons[0].speed == 5.0f);
    CHECK_EQ(applyEffectSpeeds(t, m, names), (size_t)0);   // idempotent
}

// ---------------------------------------------------------------------------------------------- install
C5R_TEST(install_names_and_held_rows) {
    const std::string dir = c5r::test::gameDir();
    c5r::gamefs::GameFiles gf;
    CHECK_OK(gf.open(c5r::gamefs::fromUtf8(dir)));
    c5r::units::GameFilesProvider files(gf);
    c5m::EffectLibrary lib;
    std::string err;
    CHECK(loadInstallEffects(gf, files, lib, err));
    CHECK(lib.distinct(c5m::FxKind::VisEffect) == 538);
    EffectNames names(lib);
    // every VisEffect resolves exactly as itself
    size_t exact = 0;
    std::string out;
    for (const std::string& n : lib.names(c5m::FxKind::VisEffect))
        if (names.resolve(n, NameValues(), out) == NameResult::Exact && out == n) ++exact;
    CHECK_EQ(exact, (size_t)538);
    // effects-formats.md 14.3: the fire arrow's impact exists only for CITY and DIRT; snow falls back to DIRT
    CHECK(names.resolve("ART_DEF_VEFFECT_FIRE_ARROW_IMPACT_$(TERRAIN)", NameValues().set_terrain("SNOW"), out) ==
              NameResult::Fallback && out == "ART_DEF_VEFFECT_FIRE_ARROW_IMPACT_DIRT");
    CHECK(names.resolve("ART_DEF_VEFFECT_TANK_IMPACT_$(TERRAIN)", NameValues().set_terrain("SNOW"), out) ==
              NameResult::Exact && out == "ART_DEF_VEFFECT_TANK_IMPACT_SNOW");
    // the two dangling trigger names are refused by name
    CHECK(names.resolve("ART_DEF_VEFFECT_SEAGULLS_01", NameValues(), out) == NameResult::NotDefined);
    CHECK(names.resolve("ART_DEF_VEFFECT_WORKBOAT_INDUSTRIAL_HOIST_SPLASH_01", NameValues(), out) == NameResult::NotDefined);
    // the three city bombard tags are projectile effects with a ProjectileEnd child
    for (const char* tag : {"CITY_BOMBARD_EARLY", "CITY_BOMBARD_MID", "CITY_BOMBARD_LATE"}) {
        std::vector<std::string> ch;
        CHECK(names.defined(tag));
        CHECK(names.children(tag, NameValues().set_terrain("DIRT"), ch) == 0);
        std::printf("%s: %zu children%s%s, fBaseDuration %.3f\n", tag, ch.size(), ch.empty() ? "" : ", first ",
                    ch.empty() ? "" : ch[0].c_str(), names.baseDuration(tag));
    }
    // every weapon HitEffect family of the database (effects-formats.md 6) on every surface resolves (exact or DIRT)
    const char* hitFamilies[] = {"ART_DEF_VEFFECT_ANTIAIR_IMPACT_$(TERRAIN)", "ART_DEF_VEFFECT_ARTILLERY_IMPACT_$(TERRAIN)",
                                 "ART_DEF_VEFFECT_CANNON_IMPACT_$(TERRAIN)", "ART_DEF_VEFFECT_FIGHTER_MACHINE_GUN_HIT_$(TERRAIN)",
                                 "ART_DEF_VEFFECT_GUIDED_MISSILE_IMPACT_$(TERRAIN)", "ART_DEF_VEFFECT_GUNSHIP_MACHINE_GUN_HIT_$(TERRAIN)",
                                 "ART_DEF_VEFFECT_TANK_IMPACT_$(TERRAIN)", "ART_DEF_VEFFECT_NUCLEAR_BOMB_01"};
    size_t ok = 0, fb = 0;
    for (const char* f : hitFamilies)
        for (const char* s : {"DIRT", "SAND", "SNOW", "MUD", "LEAVES", "WATER", "CITY", "AIR"}) {
            const NameResult r = names.resolve(f, NameValues().set_terrain(s), out);
            if (r == NameResult::Exact || r == NameResult::Fallback) ++ok;
            if (r == NameResult::Fallback) ++fb;
        }
    std::printf("hit families: %zu of %zu resolve, %zu by the DIRT fallback\n", ok, sizeof(hitFamilies) / sizeof(*hitFamilies) * 8,
                fb);
    CHECK_EQ(ok, sizeof(hitFamilies) / sizeof(*hitFamilies) * 8);
}

C5R_TEST_MAIN
