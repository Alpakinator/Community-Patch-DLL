// render/tests/unitanim/test_combat_controller.cpp - issue 4775: the controller's combat entry (controller.h
// combatFigures / combatClips / combat; combat.h) and the BasicBridge's run / combat_ready / idle. Positive: a held
// unit's figures and members, the clip timings from the member's bindings and trigger file (Action, else Transfer), a
// whole combat on two held units replayed through the controller. Negative: a unit not held or failed, a clip missing in
// the install (4770's flag), an ec not bound, a clip without a duration, the combat tables from a source that lacks a
// table.
#include <map>
#include <string>

#include "anim_testkit.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"

using namespace anim_test;

namespace {

const double kH = 1.0 / 30.0;

c5m::FtsTrigger trig(c5m::TriggerType type, int32_t ec, float time, int32_t id) {
    c5m::FtsTrigger t;
    t.type = type;
    t.type_name = type == c5m::TriggerType::Action ? "Action" : "Transfer";
    t.ec = ec;
    t.time = time;
    t.id = id;
    return t;
}

// The Rig of anim_testkit.h with a trigger file, an attack-city clip, and a charge-attack clip missing in the install.
struct CombatRig {
    Rig r;
    CombatArtTables tables;
    void open(int figures = 4, int16_t ddamage = 0) {   // 4744: the defender created with ddamage of 100
        r.open(figures);
        c5r::units::MemberArt& m = r.art.rows["W"].members[0];
        c5m::ResolvedAnimation city;
        city.file.name = "w_attackcity.gr2";
        city.file.found = true;
        city.ec = {1160};
        m.files.animations.push_back(city);
        c5m::ResolvedAnimation charge;
        charge.file.name = "w_chargeattack.gr2";
        charge.file.found = false;   // named by the .fxsxml, not in the install
        charge.ec = {1140};
        m.files.animations.push_back(charge);
        c5m::ResolvedTrigger rt;
        rt.file.name = "w.ftsxml";
        rt.file.found = true;
        m.files.timed_triggers.push_back(rt);
        m.domain = "Land";
        c5m::TriggerSystem ts;
        ts.type = "FGrannyTimedTriggerSystem";
        ts.triggers = {trig(c5m::TriggerType::Action, 1100, 0.4889f, 1), trig(c5m::TriggerType::Transfer, 1160, 0.5759f, 2),
                       trig(c5m::TriggerType::Action, 2100, 0.4741f, 3)};
        r.c.logic()->putTriggers("w.ftsxml", ts);
        CombatArtRow row;
        for (const char* w : {"Idle", "Attack", "RunCharge", "AttackCity", "Bombard", "Death", "BombardDefend", "Run",
                              "Fortify", "CombatReady", "Walk", "AttackCharge"})
            row.enable.insert(w);
        CombatWeapon blade, torch;
        torch.projectile = torch.vs_city = true;
        row.weapons = {blade, torch};
        tables["M"] = row;
        c5r::units::UnitScene sc;
        for (int32_t pl : {0, 63}) {
            c5r::units::PlayerInput p;
            p.player = pl;
            sc.players.push_back(p);
        }
        c5r::units::UnitInput a = Rig::unit(0, 1), d = Rig::unit(63, 2);
        d.damage = ddamage;
        sc.units = {a, d};
        r.c.start(sc);
        CHECK_ANIM(r.c.apply(Rig::frame(1, kH)), AnimCode::Ok);
    }
};

std::map<std::string, float> durations() {
    return {{"w_attacka.gr2", 1.0f}, {"w_attackcity.gr2", 1.2f}, {"w_idlea.gr2", 6.0f}, {"w_fidgeta.gr2", 5.0f},
            {"w_fortify_idle.gr2", 4.0f}};
}

CombatInput record(int32_t afinal, int32_t dfinal) {
    CombatInput in;
    in.seq = 9;
    in.flags = kCombatVisualize | kCombatDefenderRetaliates;
    in.attacker.owner = 0;
    in.attacker.unit = 1;
    in.attacker.final_damage = afinal;
    in.defender.owner = 63;
    in.defender.unit = 2;
    in.defender.final_damage = dfinal;
    return in;
}

class NoTables : public c5m::TableSource {
public:
    std::string missing;
    bool noVisKill = false;   // 4783: a database without the VisKillStrength columns (the tests' hand-made tables)
    bool rows(const std::string& table, const std::vector<std::string>& cols, std::vector<c5m::DbRow>& out,
              c5m::Error* err) override {
        out.clear();
        if (table == missing) {
            if (err) err->what = "no such table";
            return false;
        }
        for (const std::string& c : cols)
            if (noVisKill && c.compare(0, 15, "VisKillStrength") == 0) {
                if (err) err->what = "no such column: " + c;
                return false;
            }
        if (table == "ArtDefine_UnitMemberCombats") {
            c5m::DbRow r(6);
            r[0].null = false;
            r[0].text = "M";
            r[1].null = false;
            r[1].text = "Idle Attack  Death";
            r[2].null = false;
            r[2].text = "DefaultCavalry";
            r[4].null = false;
            r[4].text = "1";
            out.push_back(r);
        } else {
            for (int i = 0; i < 2; ++i) {
                c5m::DbRow r(3);
                r[0].null = r[1].null = r[2].null = false;
                r[0].text = "M";
                r[1].text = std::to_string(i);
                r[2].text = i ? "FLAMING_ARROW" : "ARROW";
                out.push_back(r);
            }
        }
        return true;
    }
};

}  // namespace

C5R_TEST(figures_of_a_held_unit) {
    CombatRig cr;
    cr.open(4);
    CombatUnitFigures f;
    CHECK_ANIM(cr.r.c.combatFigures(0, 1, cr.tables, f), AnimCode::Ok);
    CHECK_EQ(f.player, 0);
    CHECK_EQ(f.id, 1);
    CHECK_EQ(f.members.size(), 1u);
    CHECK_EQ(f.members[0].count, 4);
    CHECK(f.members[0].enable.count("Attack"));
    CHECK(!f.members[0].victory);   // the idle machine reads no 1800
    CHECK_EQ(f.members[0].anti_air, 0);   // 4726: ... and no 1130 (as the submarines' 1_omni machine)
    CHECK_EQ(f.standing.size(), 4u);
    // a kill takes figure 3 out of the standing ones
    TimelineFrame fr = Rig::frame(2, kH);
    TimelineEvent k;
    k.kind = EventKind::Kill;
    k.player = 0;
    k.id = 1;
    k.figures.all = false;
    k.figures.figures = {3};
    fr.events.push_back(k);
    CHECK_ANIM(cr.r.c.apply(fr), AnimCode::Ok);
    CHECK_ANIM(cr.r.c.combatFigures(0, 1, cr.tables, f), AnimCode::Ok);
    CHECK(f.standing == std::vector<uint16_t>({0, 1, 2}));
    // negative: not held
    CHECK_ANIM(cr.r.c.combatFigures(0, 99, cr.tables, f), AnimCode::BadEvent);
    CHECK(f.members.empty());
}

// 4744 (unit-combat-server.md 18.16): a unit created damaged is held with every figure while its placement draws only
// 0 .. living-1; combatFigures names the drawn count and the fight (CombatParams::fight_drawn_figures, on by default) takes
// only those, so a kill plans its deaths over the figures the player sees, as the EXE's model does (0321 13.4). Off: the
// undrawn figures stand and die too (the rule before 4744).
C5R_TEST(a_unit_created_damaged_fights_with_the_figures_it_shows) {
    CombatRig cr;
    cr.open(4, 50);   // the defender at 50 of 100: 4 held, 4 - round(2) = 2 drawn
    CombatUnitFigures d;
    CHECK_ANIM(cr.r.c.combatFigures(63, 2, cr.tables, d), AnimCode::Ok);
    CHECK_EQ(d.standing.size(), 4u);
    CHECK_EQ(d.drawn, 2u);
    CombatUnitFigures a;
    CHECK_ANIM(cr.r.c.combatFigures(0, 1, cr.tables, a), AnimCode::Ok);
    CHECK_EQ(a.drawn, 4u);   // undamaged: every figure drawn
    std::map<std::string, float> dur = durations();
    dur["w_idleb.gr2"] = 6.0f;
    auto clips = cr.r.c.combatClips([&](const std::string& c) { return dur.count(c) ? dur[c] : 0.f; });
    CombatResult on, off;
    CombatParams p;
    p.seed = 5;
    CHECK_ANIM(cr.r.c.combat(record(0, 100), cr.tables, *clips, p, on), AnimCode::Ok);
    CHECK(on.status.ok());
    CHECK_EQ(on.defender.standing, 2u);
    CHECK_EQ(on.defender.planned, 2u);
    CHECK_EQ(on.defender.died, 2u);
    CHECK(on.defender.killed);
    CHECK(!on.at_limit);
    for (const CombatHit& h : on.hits)   // no hit on an undrawn figure
        CHECK(!(h.target_player == 63 && h.target_figure >= 2));
    CombatParams old = p;
    old.fight_drawn_figures = false;
    CHECK_ANIM(cr.r.c.combat(record(0, 100), cr.tables, *clips, old, off), AnimCode::Ok);
    CHECK_EQ(off.defender.standing, 4u);
    CHECK_EQ(off.defender.planned, 4u);
    CHECK_EQ(off.defender.died, 4u);
    int undrawn = 0;   // the old rule kills the figures the placement does not draw too
    for (const CombatHit& h : off.hits)
        if (h.target_player == 63 && h.target_figure >= 2 && h.result == HitResult::Death) ++undrawn;
    CHECK_EQ(undrawn, 2);
}

// 4726: anti_air from the member's machine: a machine that reads 1130 on path 4 (a ship's) gives 1
C5R_TEST(anti_air_from_the_members_machine) {
    Rig r;
    r.open(2);
    c5r::units::UnitArt s;
    s.art_row = "ART_S";
    s.members.push_back(memberArt("SHIP", 1, "ship.fsmxml", warriorClips()));
    r.art.rows["S"] = s;
    c5m::StateMachine m = idleMachine();
    m.sockets.push_back(socket(1, 9, {checkPath(4, 1130), setPath(4, 0)}));
    CHECK_ANIM(r.c.logic()->putMachine("ship.fsmxml", m), AnimCode::Ok);
    c5r::units::UnitScene sc;
    c5r::units::PlayerInput p;
    p.player = 0;
    sc.players.push_back(p);
    sc.units = {Rig::unit(0, 1), Rig::unit(0, 2, "S")};
    r.c.start(sc);
    CHECK_ANIM(r.c.apply(Rig::frame(1, kH)), AnimCode::Ok);
    CombatArtTables t;
    CombatUnitFigures w, sh;
    CHECK_ANIM(r.c.combatFigures(0, 1, t, w), AnimCode::Ok);
    CHECK_ANIM(r.c.combatFigures(0, 2, t, sh), AnimCode::Ok);
    CHECK_EQ(w.members.size(), 1u);
    CHECK_EQ(sh.members.size(), 1u);
    CHECK_EQ(w.members[0].anti_air, 0);
    CHECK_EQ(sh.members[0].anti_air, 1);
}

C5R_TEST(a_failed_unit_has_no_combat_figures) {
    CombatRig cr;
    cr.open(3);
    TimelineFrame fr = Rig::frame(2, kH);
    TimelineEvent bad;
    bad.kind = EventKind::Action;
    bad.player = 63;
    bad.id = 2;
    bad.action = ActionKind::Paradrop;   // BasicBridge: BadEvent, the unit fails
    fr.events.push_back(bad);
    CHECK_ANIM(cr.r.c.apply(fr), AnimCode::Ok);
    CombatUnitFigures f;
    CHECK_ANIM(cr.r.c.combatFigures(63, 2, cr.tables, f), AnimCode::BadEvent);
    CombatResult res;
    auto clips = cr.r.c.combatClips([](const std::string&) { return 1.0f; });
    CHECK_ANIM(cr.r.c.combat(record(0, 40), cr.tables, *clips, CombatParams(), res), AnimCode::BadEvent);
}

C5R_TEST(clip_timings_from_the_member) {
    CombatRig cr;
    cr.open(3);
    std::map<std::string, float> d = durations();
    auto clips = cr.r.c.combatClips([&](const std::string& c) { return d.count(c) ? d[c] : 0.f; });
    CombatUnitFigures f;
    CHECK_ANIM(cr.r.c.combatFigures(0, 1, cr.tables, f), AnimCode::Ok);
    const CombatMemberArt& m = f.members[0];
    ActionTiming a = clips->timing(0, 1, m, 1100);
    CHECK(a.found);
    CHECK_NEAR(a.duration, 1.0, 1e-6);
    CHECK(a.hits.size() == 1 && std::fabs(a.hits[0] - 0.4889f) < 1e-6);
    CHECK(!a.transfer);
    CHECK(a.hit_ids == std::vector<int32_t>({1}));   // the trigger's id (the clip-instance identity, 4795)
    ActionTiming c = clips->timing(0, 1, m, 1160);
    CHECK(c.found && c.transfer && c.hits.size() == 1);
    // negative: missing in the install (4770's flag), not bound, no duration, unit not held, member unknown
    ActionTiming x = clips->timing(0, 1, m, 1140);
    CHECK(!x.found);
    CHECK(x.why.find("missing in install: ") == 0);
    ActionTiming y = clips->timing(0, 1, m, 1180);
    CHECK(!y.found && y.why.find("binds no clip") != std::string::npos);
    ActionTiming z = clips->timing(0, 1, m, 2000);   // idle B: bound, no duration given
    CHECK(!z.found && z.why.find("no duration") != std::string::npos);
    CHECK(!clips->timing(0, 99, m, 1100).found);
    CombatMemberArt other = m;
    other.member = "X";
    CHECK(!clips->timing(0, 1, other, 1100).found);
}

C5R_TEST(a_whole_combat_on_held_units_replays_through_the_controller) {
    CombatRig cr;
    cr.open(4);
    std::map<std::string, float> d = durations();
    d["w_idleb.gr2"] = 6.0f;
    auto clips = cr.r.c.combatClips([&](const std::string& c) { return d.count(c) ? d[c] : 0.f; });
    CombatResult res;
    CombatParams p;
    p.seed = 5;
    CHECK_ANIM(cr.r.c.combat(record(25, 50), cr.tables, *clips, p, res), AnimCode::Ok);
    CHECK(res.status.ok());
    CHECK(!res.at_limit);
    CHECK_EQ(res.attacker.planned, 1u);   // 4 x 25 % = 1
    CHECK_EQ(res.defender.planned, 2u);   // 4 x 50 % = 2
    CHECK_EQ(res.attacker.died, 1u);
    CHECK_EQ(res.defender.died, 2u);
    for (auto& h : res.hits) CHECK_EQ(h.trigger, h.code == 1100 ? 1 : (h.code == 1160 ? 2 : -1));
    // the death clip (1200) is not bound in this member: removal at the 3 s cap
    for (auto& s : res.steps)
        if (s.state == FigState::Dying)
            for (auto& x : res.steps)
                if (x.player == s.player && x.figure == s.figure && x.state == FigState::Removed)
                    CHECK_NEAR(x.time - s.time, 3.0, kH + 1e-6);
    // the events through the controller (BasicBridge maps run / combat_ready / idle now): every unit stays Ok
    uint64_t seq = 10;
    for (auto& fr : combatFrames(res, kH, seq)) {
        CHECK_ANIM(cr.r.c.apply(fr), AnimCode::Ok);
        for (auto& s : cr.r.c.statuses()) CHECK_ANIM(s.status, AnimCode::Ok);
    }
    CombatUnitFigures f;
    CHECK_ANIM(cr.r.c.combatFigures(63, 2, cr.tables, f), AnimCode::Ok);
    CHECK(f.standing == std::vector<uint16_t>({0, 1}));   // LastFirst: figures 2 and 3 died
    // a city defender takes no unit
    CombatInput city = record(0, 30);
    city.kind = kCombatKindCity;
    city.defender.unit = -1;
    city.defender.city = 4;
    city.defender.max_hp = 200;
    CHECK_ANIM(cr.r.c.combat(city, cr.tables, *clips, p, res), AnimCode::Ok);
    CHECK(res.status.ok());
    CHECK_EQ(res.defender.paid, 1u);
    // a refusal of the choreography is its own status, not the controller's
    CombatInput q = record(0, 30);
    q.flags = 0;
    CHECK_ANIM(cr.r.c.combat(q, cr.tables, *clips, p, res), AnimCode::Ok);
    CHECK(res.status.code == CombatCode::NotVisualised);
}

C5R_TEST(basic_bridge_maps_the_new_kinds) {
    BasicBridge b;
    c5r::units::UnitInput u = Rig::unit(0, 1);
    const ActionKind kinds[] = {ActionKind::Run, ActionKind::CombatReady, ActionKind::Idle};
    const int32_t codes[] = {1400, 1600, 1000};
    for (int i = 0; i < 3; ++i) {
        TimelineEvent e;
        e.kind = EventKind::Action;
        e.action = kinds[i];
        e.figures.all = false;
        e.figures.figures = {1};
        std::vector<Request> out;
        CHECK_ANIM(b.requests(e, u, out), AnimCode::Ok);
        CHECK(out.size() == 1 && out[0].value == codes[i] && !out[0].figures.all);
    }
}

C5R_TEST(combat_tables_from_the_database) {
    NoTables db;
    CombatArtTables t;
    std::string err;
    CHECK(loadCombatTables(db, t, err));
    CHECK(err.empty());
    CHECK_EQ(t.size(), 1u);
    const CombatArtRow& m = t["M"];
    CHECK(m.enable == std::set<std::string>({"Idle", "Attack", "Death"}));
    CHECK(m.rush && !m.last_to_die && m.ranged);
    CHECK_EQ(m.weapons.size(), 2u);
    CHECK(m.weapons[0].projectile && !m.weapons[0].vs_city);
    CHECK(m.weapons[1].projectile && m.weapons[1].vs_city);
    // 4783 (W33, C1; MC #8906): the strike rule the load reports, with and without the VisKillStrength columns; without
    // them the load still succeeds and every landing strikes one figure
    {
        CombatTablesInfo info;
        CHECK(loadCombatTables(db, t, err, &info));
        CHECK(info.vis_kill_columns);
        CHECK_EQ(combatStrikeRule(info), std::string("VisKillStrength per landing"));
        NoTables old;
        old.noVisKill = true;
        CHECK(loadCombatTables(old, t, err, &info));
        CHECK(err.empty());
        CHECK(!info.vis_kill_columns);
        CHECK_EQ(combatStrikeRule(info), std::string("one figure per landing (database has no VisKillStrength columns)"));
        CHECK_EQ(t["M"].weapons.size(), 2u);
        CHECK(t["M"].weapons[0].vis_kill_max == 0.0f);
    }
    // negative: each table missing
    db.missing = "ArtDefine_UnitMemberCombats";
    CHECK(!loadCombatTables(db, t, err));
    CHECK(err.find("ArtDefine_UnitMemberCombats") == 0);
    db.missing = "ArtDefine_UnitMemberCombatWeapons";
    CHECK(!loadCombatTables(db, t, err));
    CHECK(err.find("ArtDefine_UnitMemberCombatWeapons") == 0);
}

C5R_TEST_MAIN
