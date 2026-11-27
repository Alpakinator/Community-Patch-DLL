// combat_scene.cpp - issue 4775: one recorded combat played by the choreography core on the install's own art, written
// as a .c5anim timeline that render/tools/unitanim replays (a tool, not a ctest: it needs the install and a database;
// unit-combat-server.md "Choreography as built (4775)"; the recorded cases are 0557's,
// tools/harness/evidence/0557/cases, driven by render/evidence/0042/4775/combat_scenes.py).
//
//   c5r_unitanim_combat_scene --game INSTALL --db DB --out DIR [--mods "A;B"]
//       --attacker PLAYER:ID:UNIT_TYPE:X:Y:DAMAGE_BEFORE:FINAL_DAMAGE:MAX_HP
//       --defender PLAYER:ID:UNIT_TYPE:X:Y:DAMAGE_BEFORE:FINAL_DAMAGE:MAX_HP   (or PLAYER:city:CITY_ID:X:Y:-:FINAL:MAX)
//       [--ranged] [--no-retaliate] [--seed N] [--approach SECONDS] [--max-in-flight N] [--tail FRAMES]
//       [--positions] (4776: the figures placed on a flat stand-in field; approaches timed by distance; writes
//       moves.csv and motion.csv too)
//       [--live]   (4782: the core stepped frame by frame with the controller's feedback, CombatRun)
//       [--attacker-alive N] [--defender-alive N] [--attacker-fortified] [--defender-fortified]   (4733: the figures
//       standing at the start, a unit fortified at the start) [--no-spare] (4733: the last-figure rule off)
//       [--all-held]   (4744: every held figure of a unit created damaged stands, not only the drawn ones)
//       [--no-reform-first]   (4745 + 4747: no unit reforms before its fight, CombatParams::reform_first off)
//       [--no-walk-home]   (4742: the survivors walk home after the fight's end, CombatParams::walk_home off)
//       [--no-rush-ride-through] [--no-defender-waits]   (5057: CombatParams::rush_ride_through /
//       defender_waits_first_landing off, the rules before 5057)
//       [--no-melee-reach]   (0743: CombatParams::melee_reach off, every approach ends MoveRadius short, the rule before)
//       [--type2-phase2]   (0743: CombatParams::type2_phase2 on, the server's default since 5067)
//       [--no-defender-step]   (5061: CombatParams::defender_step off, the type 2 defender strikes where it stands)
//       [--no-victory-word]   (5059: CombatParams::victory_needs_word off, the rule before 5059: victory by the
//       machine's 1800 alone)
//   4777 (the special fights, unit-combat-server.md "Special cases (4777)"):
//       --attacker PLAYER:city:CITY_ID:X:Y:-:0:MAX_HP     a city's ranged strike (with --ranged --no-retaliate)
//       --defender PLAYER:plot:0:X:Y:-:0:100             the nuke's plot target (with --nuclear N)
//       [--bombing] [--air-sweep] [--nuclear N] [--attacker-damage N] (the attacker's damage in this combat, 0321 13.4)
//       [--interceptor SIDE] [--interceptor-damage N] [--member SIDE]... (damage members: units with figures, or
//       PLAYER:city:ID:X:Y:-:FINAL:MAX) [--no-anti-air MEMBER]... (the member's machine takes no 1130: the submarines)
//       [--effect-wait S] [--no-unnamed-hold] [--ships-in-place] [--city-reach UNITS]
//       The special fights write hits.csv's 4777 columns and summary.txt's "other" lines; motion.csv (4726: every
//       participant with figures, and a column z, an aircraft's height) for every fight, a refused motion is exit 4.
//
// Writes DIR/start.c5units (the two units), DIR/combat.c5anim (a first frame, then the combat's events at their frames,
// then --tail frames, default 60), DIR/steps.csv, DIR/hits.csv, DIR/events.csv and DIR/summary.txt. Exit codes: 0 Ok;
// 2 usage; 4 inputs (install, database, tables, a unit the controller refuses); 6 the choreography refused (its status
// in summary.txt); 5 an output could not be written. Nothing from the game's files is written but names and numbers.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/unitanim/driver.h"
#include "c5r/unitanim/motion.h"
#include "c5r/unitanim/timeline.h"
#include "c5r/units/art.h"
#include "c5r/units/draw.h"
#include "c5r/units/placement.h"
#include "c5r/units/scene_file.h"

using namespace c5r;
using namespace c5r::unitanim;

namespace {

int fail(int code, const std::string& msg) {
    std::fprintf(stderr, "combat_scene: exit %d: %s\n", code, msg.c_str());
    return code;
}

std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t p = s.find(c, at);
        out.push_back(s.substr(at, p == std::string::npos ? std::string::npos : p - at));
        if (p == std::string::npos) break;
        at = p + 1;
    }
    return out;
}

struct SideArg {
    int32_t player = -1, id = -1, city = -1, x = 0, y = 0, before = 0, final_damage = 0, max_hp = 100;
    bool plot = false;   // 4777: the nuke's plot target
    int alive = -1;          // 4733: figures standing at the start (-1: all)
    bool fortified = false;  // 4733: the unit is fortified at the start
    std::string art;         // 4733: an art row in place of the unit type's (empty: the type's)
    std::string type;
};
bool parseSide(const std::string& s, SideArg& out) {
    std::vector<std::string> f = split(s, ':');
    if (f.size() != 8) return false;
    out.player = std::atoi(f[0].c_str());
    if (f[1] == "city") {
        out.city = std::atoi(f[2].c_str());
    } else if (f[1] == "plot") {
        out.plot = true;
    } else {
        out.id = std::atoi(f[1].c_str());
        out.type = f[2];
        out.before = std::atoi(f[5].c_str());
    }
    out.x = std::atoi(f[3].c_str());
    out.y = std::atoi(f[4].c_str());
    out.final_damage = std::atoi(f[6].c_str());
    out.max_hp = std::atoi(f[7].c_str());
    return !(out.city < 0 && !out.plot && out.type.empty());
}

bool writeText(const std::string& path, const std::string& text) {
    FILE* f = _wfopen(gamefs::fromUtf8(path).c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

std::string num(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6f", v);
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    std::string game, db, out, mods;
    SideArg A, D, I;
    std::vector<SideArg> members;
    std::set<std::string> noAntiAir;
    bool haveA = false, haveD = false, haveI = false, ranged = false, retaliate = true, live = false, positions = false;
    bool bombing = false, sweep = false, unnamedHold = true, shipsApproach = true, spare = true;
    bool allHeld = false;   // 4744: --all-held (CombatParams::fight_drawn_figures off, the rule before 4744)
    bool noReform = false;  // 4745: --no-reform-first (CombatParams::reform_first off, the rule before 4745)
    bool noWalk = false;    // 4742: --no-walk-home (CombatParams::walk_home off, the rule before 4742)
    bool noRide = false;    // 5057 (A): --no-rush-ride-through (CombatParams::rush_ride_through off, the rule before 5057)
    bool noDefWait = false; // 5057 (B): --no-defender-waits (CombatParams::defender_waits_first_landing off)
    bool noReach = false;   // 0743: --no-melee-reach (CombatParams::melee_reach off, the rule before 0743)
    bool phase2 = false;    // 0743: --type2-phase2 (CombatParams::type2_phase2 on, as the server runs since 5067)
    bool noStep = false;    // 5061: --no-defender-step (CombatParams::defender_step off, the rule before 5061)
    bool noVictoryWord = false;   // 5059: --no-victory-word (CombatParams::victory_needs_word off, the rule before 5059)
    float cityReach = CombatParams().city_reach;
    int nuclear = 0, attackerDamage = -1, interceptorDamage = -1;
    double effectWait = CombatParams().effect_wait;
    uint64_t seed = 0;
    double approach = CombatParams().approach;
    int tail = 60, inflight = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& v) {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--game" && next(game)) continue;
        if (a == "--db" && next(db)) continue;
        if (a == "--out" && next(out)) continue;
        if (a == "--mods" && next(mods)) continue;
        if (a == "--attacker" && next(v)) {
            haveA = parseSide(v, A);
            if (!haveA) return fail(2, "bad --attacker " + v);
            continue;
        }
        if (a == "--defender" && next(v)) {
            haveD = parseSide(v, D);
            if (!haveD) return fail(2, "bad --defender " + v);
            continue;
        }
        if (a == "--interceptor" && next(v)) {   // 4777
            haveI = parseSide(v, I) && I.city < 0 && !I.plot;
            if (!haveI) return fail(2, "bad --interceptor " + v);
            continue;
        }
        if (a == "--member" && next(v)) {   // 4777
            SideArg m;
            if (!parseSide(v, m) || m.plot) return fail(2, "bad --member " + v);
            members.push_back(m);
            continue;
        }
        if (a == "--ranged") { ranged = true; continue; }
        if (a == "--live") { live = true; continue; }   // 4782: CombatRun with the controller's feedback
        if (a == "--positions") { positions = true; continue; }   // 4776: the figures placed, approaches by distance
        if (a == "--no-retaliate") { retaliate = false; continue; }
        if (a == "--bombing") { bombing = true; continue; }   // 4777
        if (a == "--air-sweep") { sweep = true; continue; }
        if (a == "--no-unnamed-hold") { unnamedHold = false; continue; }
        if (a == "--ships-in-place") { shipsApproach = false; continue; }
        if (a == "--city-reach" && next(v)) { cityReach = static_cast<float>(std::atof(v.c_str())); continue; }
        if (a == "--nuclear" && next(v)) { nuclear = std::atoi(v.c_str()); continue; }
        if (a == "--attacker-damage" && next(v)) { attackerDamage = std::atoi(v.c_str()); continue; }
        if (a == "--interceptor-damage" && next(v)) { interceptorDamage = std::atoi(v.c_str()); continue; }
        if (a == "--no-anti-air" && next(v)) { noAntiAir.insert(v); continue; }
        if (a == "--effect-wait" && next(v)) { effectWait = std::atof(v.c_str()); continue; }
        if (a == "--seed" && next(v)) { seed = std::strtoull(v.c_str(), nullptr, 10); continue; }
        if (a == "--approach" && next(v)) { approach = std::atof(v.c_str()); continue; }
        if (a == "--tail" && next(v)) { tail = std::atoi(v.c_str()); continue; }
        // 4733: the figures a side has standing at the start (a unit that lost figures in earlier fights: the live
        // controller holds only those) and a side that is fortified (its machines start on path 4 = 1500)
        if (a == "--attacker-alive" && next(v)) { A.alive = std::atoi(v.c_str()); continue; }
        if (a == "--defender-alive" && next(v)) { D.alive = std::atoi(v.c_str()); continue; }
        if (a == "--attacker-fortified") { A.fortified = true; continue; }
        if (a == "--defender-fortified") { D.fortified = true; continue; }
        if (a == "--no-spare") { spare = false; continue; }   // 4733: CombatParams::spare_last_figure off (the 4775 rule)
        if (a == "--all-held") { allHeld = true; continue; }   // 4744: every held figure stands (the rule before 4744)
        if (a == "--no-reform-first") { noReform = true; continue; }   // 4745: no reform (the rule before 4745)
        if (a == "--no-walk-home") { noWalk = true; continue; }   // 4742: the walk after the end (the rule before 4742)
        if (a == "--no-rush-ride-through") { noRide = true; continue; }   // 5057 (A): every rider stops and enters 1600
        if (a == "--no-defender-waits") { noDefWait = true; continue; }   // 5057 (B): the type 2 defender strikes at once
        if (a == "--no-melee-reach") { noReach = true; continue; }   // 0743: approaches end MoveRadius short
        if (a == "--type2-phase2") { phase2 = true; continue; }   // 0743: the server's default since 5067
        if (a == "--no-defender-step") { noStep = true; continue; }   // 5061: the type 2 defender stands
        if (a == "--no-victory-word") { noVictoryWord = true; continue; }   // 5059: victory by the machine's 1800 alone
        // 4733: a side drawn with another art row (an embarked unit's ART_DEF_UNIT_TRANSPORT, as the server picks it)
        if (a == "--attacker-art" && next(v)) { A.art = v; continue; }
        if (a == "--defender-art" && next(v)) { D.art = v; continue; }
        if (a == "--max-in-flight" && next(v)) { inflight = std::atoi(v.c_str()); continue; }
        return fail(2, "unknown or incomplete argument " + a);
    }
    if (game.empty() || db.empty() || out.empty() || !haveA || !haveD || A.plot || tail < 0)
        return fail(2, "needs --game, --db, --out, --attacker (a unit or a city) and --defender");
    // every side, the unit sides among them (4777: the interceptor and the unit members too)
    std::vector<SideArg*> all = {&A, &D};
    if (haveI) all.push_back(&I);
    for (auto& m : members) all.push_back(&m);
    auto isUnit = [](const SideArg& s) { return s.city < 0 && !s.plot; };
    const bool special = A.city >= 0 || D.plot || haveI || !members.empty();

    gamefs::GameFiles gf;
    gamefs::GameFiles::Options gopt;
    for (auto& d : split(mods, ';'))
        if (!d.empty()) gopt.extraLooseRoots.push_back(gamefs::fromUtf8(d));
    Status s = gf.open(gamefs::fromUtf8(game), gopt);
    if (!s.ok()) return fail(4, "game files: " + s.str());
    c5m::SqliteTables tables;
    c5m::Error e;
    if (!c5m::SqliteTables::available() || !tables.open(db, &e)) return fail(4, "database: " + e.what);
    units::GameFilesProvider provider(gf);
    units::ArtResolver resolver;
    units::UnitStatus us = resolver.open(tables, provider);
    if (!us.ok()) return fail(4, "art tables: " + us.str());
    c5m::StringDatabase strings;
    us = units::loadStringDatabases(gf.names(), provider, strings);
    if (!us.ok()) return fail(4, "string databases: " + us.str());
    BridgeTables bt;
    AnimStatus as = loadBridgeTables(tables, bt);
    if (!as.ok()) return fail(4, as.text);
    CombatArtTables ct;
    std::string err;
    if (!loadCombatTables(tables, ct, err)) return fail(4, err);

    // the start scene: the unit sides at their plots, damage before the combat
    units::UnitScene scene;
    for (const SideArg* sa : all) {
        if (sa->player < 0) continue;
        bool have = false;
        for (auto& q : scene.players) have = have || q.player == sa->player;
        if (have) continue;
        units::PlayerInput pi;
        pi.player = sa->player;
        pi.colour = "PLAYERCOLOR_DEFAULT";
        pi.art_style = "ARTSTYLE_EUROPEAN";
        pi.era = "ERA_ANCIENT";
        scene.players.push_back(pi);
    }
    std::vector<std::string> clips;
    // 4776 (--positions): a flat stand-in height field with the EXE's plot spacing (64 units across, rows 55.4 apart,
    // unit-formations.md 3.7) over every plot; each unit faces the other side (the nearest of the six facings) [I]
    int maxX = 0, maxY = 0;
    for (const SideArg* sa : all) {
        maxX = std::max(maxX, sa->x);
        maxY = std::max(maxY, sa->y);
    }
    heightfield::HeightGrid grid;
    grid.mapW = static_cast<uint32_t>(maxX + 4);
    grid.mapH = static_cast<uint32_t>(maxY + 8);
    grid.samplesX = grid.mapW * 64;
    grid.samplesY = grid.mapH * 64;
    grid.ox = static_cast<float>(grid.mapW * 32);
    grid.oy = static_cast<float>(grid.mapH * 32) * grid.sy;
    const float h2w[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 1, 0, 32, 128, 0, 1};
    for (int k = 0; k < 16; ++k) grid.hexToWorld[k] = h2w[k];
    grid.h.assign(static_cast<size_t>(grid.samplesX) * grid.samplesY, 0);
    std::map<const SideArg*, std::pair<float, float>> centres;
    if (positions)
        for (const SideArg* sa : all) {
            float cx = 0, cy = 0;
            if (!units::plotCentre(grid, sa->x, sa->y, cx, cy).ok())
                return fail(2, "--positions: a plot outside the stand-in height field");
            centres[sa] = std::make_pair(cx, cy);
        }
    float ax = 0, ay = 0, dx = 0, dy = 0;
    if (positions) {
        ax = centres[&A].first;
        ay = centres[&A].second;
        dx = centres[&D].first;
        dy = centres[&D].second;
    }
    auto facingTo = [](float fx, float fy, float tx, float ty) {
        const float want = yawFacing(tx - fx, ty - fy);
        int best = 0;
        float bestDiff = 10;
        for (int d = 0; d < 6; ++d) {
            float diff = std::fabs(std::remainder(units::facingYaw(d) - want, 6.2831853f));
            if (diff < bestDiff) {
                bestDiff = diff;
                best = d;
            }
        }
        return best;
    };
    std::map<std::pair<int32_t, int32_t>, units::UnitArt> arts;   // by (player, id)
    for (const SideArg* sa : all) {
        if (!isUnit(*sa)) continue;
        units::UnitInput u;
        u.player = sa->player;
        u.id = sa->id;
        u.unit_type = sa->type;
        us = resolver.art_row(sa->type, "", "", u.art_row);
        if (us.ok() && !sa->art.empty()) u.art_row = sa->art;   // 4733 (--attacker-art / --defender-art)
        if (!us.ok()) return fail(4, sa->type + ": " + us.str());
        u.plot_x = sa->x;
        u.plot_y = sa->y;
        u.damage = static_cast<int16_t>(sa->before);
        u.max_hp = static_cast<int16_t>(sa->max_hp);
        u.alive = static_cast<int16_t>(sa->alive);                       // 4733
        if (sa->fortified) u.flags = static_cast<uint8_t>(u.flags | units::kFlagFortified);   // 4733
        if (positions) {
            const auto& c = centres[sa];
            u.facing = static_cast<int8_t>(sa == &A ? facingTo(c.first, c.second, dx, dy) : facingTo(c.first, c.second, ax, ay));
        }
        scene.units.push_back(u);
        units::UnitArt art;
        us = resolver.resolve(u.art_row, art);
        if (!us.ok()) return fail(4, u.art_row + ": " + us.str());
        for (auto& m : art.members)
            for (auto& an : m.files.animations) clips.push_back(an.file.name);
        arts[std::make_pair(sa->player, sa->id)] = art;
    }

    ResolverArtSource artSource(resolver);
    units::ClipFileLoader loader = units::clipLoaderFromProvider(provider, &strings);
    ClipGraphFactory graphs(loader);
    ClipGraphFactory::Durations durs;
    ClipGraphFactory::readDurations(loader, clips, durs);
    graphs.seed(durs);
    ActionBridge bridge(bt);
    ControllerOptions co;
    co.bridge = &bridge;
    co.strings = &strings;
    co.seed = seed;
    Controller c;
    c.open(provider, artSource, graphs, co);
    c.start(scene);
    TimelineFrame first;
    first.seq = 1;
    first.dt = 1.0 / 30.0;
    as = c.apply(first);
    if (!as.ok()) return fail(4, "first frame: " + as.text);
    for (auto& st : c.statuses())
        if (!st.status.ok())
            return fail(4, std::to_string(st.player) + "/" + std::to_string(st.id) + " " + name(st.status.code) + ": " +
                               st.status.text);

    CombatInput in;
    in.seq = 1;
    in.kind = (D.city >= 0 || A.city >= 0) ? kCombatKindCity : kCombatKindUnit;
    in.flags = static_cast<uint16_t>(kCombatVisualize | (retaliate ? kCombatDefenderRetaliates : 0) |
                                     (ranged ? kCombatRanged : 0) | (bombing ? kCombatBombing : 0) |
                                     (sweep ? kCombatAirSweep : 0));
    in.nuclear_level = nuclear;
    in.attacker.owner = A.player;
    in.attacker.unit = A.city >= 0 ? -1 : A.id;
    in.attacker.city = A.city;
    in.attacker.final_damage = A.final_damage;
    in.attacker.max_hp = A.max_hp;
    in.attacker.damage = attackerDamage;
    in.defender.owner = D.player;
    in.defender.unit = isUnit(D) ? D.id : -1;
    in.defender.city = D.city;
    in.defender.final_damage = D.final_damage;
    in.defender.max_hp = D.max_hp;
    if (haveI) {
        in.interceptor.owner = I.player;
        in.interceptor.unit = I.id;
        in.interceptor.final_damage = I.final_damage;
        in.interceptor.max_hp = I.max_hp;
        in.interceptor.damage = interceptorDamage;
    }
    for (auto& m : members) {
        CombatMemberIn mi;
        mi.player = m.player;
        mi.id = m.city >= 0 ? m.city : m.id;
        mi.flags = m.city >= 0 ? 1 : 0;
        mi.final_damage = static_cast<int16_t>(m.final_damage);
        mi.damage = static_cast<int16_t>(std::max(0, m.final_damage - m.before));
        mi.max_hp = static_cast<int16_t>(m.max_hp);
        in.members.push_back(mi);
    }
    auto clipTimes = c.combatClips([&](const std::string& clip) {
        auto it = durs.find(c5m::basename_lower(clip));
        return it == durs.end() ? 0.f : it->second.first;
    });
    CombatParams params;
    params.seed = seed;
    params.approach = approach;
    params.max_in_flight = inflight;
    params.effect_wait = effectWait;
    params.unnamed_effect_holds = unnamedHold;
    params.ships_approach = shipsApproach;
    params.city_reach = cityReach;
    params.spare_last_figure = spare;   // 4733
    params.fight_drawn_figures = !allHeld;   // 4744
    params.reform_first = !noReform;         // 4745
    params.walk_home = !noWalk;              // 4742
    params.rush_ride_through = !noRide;      // 5057 (A)
    params.defender_waits_first_landing = !noDefWait;   // 5057 (B)
    params.melee_reach = !noReach;                      // 0743
    params.type2_phase2 = phase2;                       // 0743: --type2-phase2
    params.defender_step = !noStep;                     // 5061: --no-defender-step
    params.victory_needs_word = !noVictoryWord;          // 5059
    CombatResult r;
    std::vector<TimelineFrame> liveFrames;
    // 4776 (--positions): the figures' start positions from the unit placement over the stand-in field; 4777: every unit
    // side's figures (the others for the interceptor and the unit members), the centre of a city or the plot
    CombatUnitFigures fa, fd;
    std::vector<CombatUnitFigures> others;
    std::map<const SideArg*, CombatUnitFigures*> figsOf;
    const bool needFigs = positions || live || special;
    if (needFigs) {
        others.reserve(all.size());
        for (SideArg* sa : all) {
            CombatUnitFigures* f = sa == &A ? &fa : (sa == &D ? &fd : nullptr);
            if (!f) {
                if (!isUnit(*sa)) continue;
                others.push_back(CombatUnitFigures());
                f = &others.back();
            }
            figsOf[sa] = f;
            if (!isUnit(*sa)) continue;
            as = c.combatFigures(sa->player, sa->id, ct, *f);
            if (!as.ok()) return fail(4, "combat figures: " + as.text);
            for (auto& m : f->members)
                if (noAntiAir.count(m.member)) m.anti_air = 0;
        }
    }
    if (positions) {
        units::FormationTable ft;
        s = ft.load(gf, gamefs::fromUtf8(game));
        if (!s.ok()) return fail(4, "UnitFormations.xml: " + s.str());
        for (auto& kv : figsOf) {
            const SideArg& sa = *kv.first;
            CombatUnitFigures* f = kv.second;
            if (!isUnit(sa)) {
                f->has_centre = true;
                f->centre_x = centres[&sa].first;
                f->centre_y = centres[&sa].second;
                continue;
            }
            const units::UnitArt& art = arts[std::make_pair(sa.player, sa.id)];
            units::PlacementInput pin;
            pin.formation = art.formation;
            for (auto& m : art.members)
                for (int k = 0; k < m.count; ++k) pin.zOffset.push_back(m.zoffset);
            pin.plotX = sa.x;
            pin.plotY = sa.y;
            for (auto& u : scene.units)
                if (u.player == sa.player && u.id == sa.id) pin.facing = u.facing;
            pin.seed = 0;
            pin.damage = static_cast<int16_t>(sa.before);
            pin.maxHp = static_cast<int16_t>(sa.max_hp);
            s = combatPositions(ft, grid, pin, *f);
            if (!s.ok()) return fail(4, "combat positions: " + s.str());
        }
    }
    if (!live && !positions && !special) {
        as = c.combat(in, ct, *clipTimes, params, r);
        if (!as.ok()) return fail(4, "combat entry: " + as.text);
    } else if (!live) {
        r = choreograph(in, fa, fd, others, *clipTimes, params);
    } else {
        // 4782: the core stepped frame by frame against this controller (each frame's events applied before the next
        // step asks the figures' machines), the frames kept for the timeline
        ControllerFeedback fb(c);
        CombatRun run;
        run.start(in, fa, fd, others, *clipTimes, params, &fb);
        uint64_t sq = 1;
        bool end = run.done();
        for (int k = 0; k < 30 * 30 && !end; ++k) {
            std::vector<TimedEvent> evs;
            end = run.advance(1.0 / 30.0, evs);
            TimelineFrame f;
            f.seq = ++sq;
            f.dt = 1.0 / 30.0;
            for (auto& te : evs) f.events.push_back(te.ev);
            as = c.apply(f);
            if (!as.ok()) return fail(4, "live frame: " + as.text);
            liveFrames.push_back(f);
        }
        r = run.result();
    }

    std::string sum = std::string("status ") + combatCodeName(r.status.code) + " " + r.status.text + "\n";
    if (r.status.ok()) {
        sum += "end " + num(r.end) + "\nat_limit " + (r.at_limit ? "1" : "0") + "\nlimit " + num(r.limit) + "\n";
        auto line = [](const char* role, const SideOutcome& o) {
            return std::string(role) + " " + std::to_string(o.player) + "/" + std::to_string(o.id) + " standing " +
                   std::to_string(o.standing) + " planned " + std::to_string(o.planned) + " died " +
                   std::to_string(o.died) + " owed " + std::to_string(o.owed) + " paid " + std::to_string(o.paid) +
                   " killed " + (o.killed ? "1" : "0") + " figures " + (o.figures ? "1" : "0") + "\n";
        };
        sum += line("attacker", r.attacker);
        sum += line("defender", r.defender);
        for (auto& o : r.others) sum += line("other", o);
        for (auto& n : r.notes) sum += "note " + num(n.time) + " " + n.text + "\n";
    }
    if (!writeText(out + "/summary.txt", sum)) return fail(5, "cannot write " + out + "/summary.txt");
    if (!r.status.ok()) return fail(6, std::string(combatCodeName(r.status.code)) + ": " + r.status.text);

    std::string steps = "time,player,id,figure,state,code,target_player,target_id,target_figure,approach\n";
    for (auto& x : r.steps)
        steps += num(x.time) + "," + std::to_string(x.player) + "," + std::to_string(x.id) + "," +
                 std::to_string(x.figure) + "," + figStateName(x.state) + "," + std::to_string(x.code) + "," +
                 std::to_string(x.target_player) + "," + std::to_string(x.target_id) + "," +
                 std::to_string(x.target_figure) + "," + std::to_string(x.approach) + "\n";
    std::string hits = "time,launch,player,id,figure,code,trigger,target_player,target_id,target_figure,projectile,"
                       "result,effect,sound,scale,area,city,has_pos,x,y,z,has_origin,ox,oy,oz\n";
    const char* res[] = {"death", "owed", "spent"};
    for (auto& h : r.hits)
        hits += num(h.time) + "," + num(h.launch) + "," + std::to_string(h.player) + "," + std::to_string(h.id) + "," +
                std::to_string(h.figure) + "," + std::to_string(h.code) + "," + std::to_string(h.trigger) + "," +
                std::to_string(h.target_player) + "," +
                std::to_string(h.target_id) + "," + std::to_string(h.target_figure) + "," + (h.projectile ? "1" : "0") +
                "," + res[static_cast<int>(h.result)] + "," + h.effect + "," + h.sound + "," + num(h.scale) + "," +
                (h.area ? "1" : "0") + "," + (h.city ? "1" : "0") + "," + (h.has_pos ? "1" : "0") + "," +
                num(h.pos[0]) + "," + num(h.pos[1]) + "," + num(h.pos[2]) + "," + (h.has_origin ? "1" : "0") + "," +
                num(h.origin[0]) + "," + num(h.origin[1]) + "," + num(h.origin[2]) + "\n";
    std::string evs = "time,player,id,kind,action,figures\n";
    for (auto& te : r.events) {
        std::string figs;
        for (auto f : te.ev.figures.figures) figs += (figs.empty() ? "" : " ") + std::to_string(f);
        evs += num(te.time) + "," + std::to_string(te.ev.player) + "," + std::to_string(te.ev.id) + "," +
               eventKindName(te.ev.kind) + "," + actionKindName(te.ev.action) + "," + figs + "\n";
    }
    if (!writeText(out + "/steps.csv", steps) || !writeText(out + "/hits.csv", hits) ||
        !writeText(out + "/events.csv", evs))
        return fail(5, "cannot write the CSV files into " + out);
    if (positions) {
        // 4776: the moves, and every figure's pose each 1/30 s through the fight (FightMotion); 4777: the air columns
        std::string mv = "player,id,figure,code,t0,t1,x0,y0,x1,y1,air,back,z0,z1\n";
        for (auto& m : r.moves)
            mv += std::to_string(m.player) + "," + std::to_string(m.id) + "," + std::to_string(m.figure) + "," +
                  std::to_string(m.code) + "," + num(m.t0) + "," + num(m.t1) + "," + num(m.x0) + "," + num(m.y0) +
                  "," + num(m.x1) + "," + num(m.y1) + "," + (m.air ? "1" : "0") + "," + (m.back ? "1" : "0") + "," +
                  num(m.z0) + "," + num(m.z1) + "\n";
        if (!writeText(out + "/moves.csv", mv)) return fail(5, "cannot write moves.csv into " + out);
        FightMotion fm;
        s = fm.build(r, fa, fd, others);   // 4726: the interceptor's and the members' figures too
        if (!s.ok()) {
            return fail(4, "fight motion: " + s.str());
        } else {
            // 4726: every participant with figures; z = an aircraft's height above its ground placement
            std::vector<const CombatUnitFigures*> parts = {&fa, &fd};
            for (const CombatUnitFigures& o : others) parts.push_back(&o);
            std::string po = "t,player,id,figure,x,y,yaw,moving,z\n";
            for (int k = 0; k * (1.0 / 30.0) <= r.end + 1e-9; ++k) {
                const double t = k * (1.0 / 30.0);
                for (const CombatUnitFigures* f : parts)
                    for (uint16_t fig : f->standing) {
                        FigurePose ps;
                        if (!fm.pose(t, f->player, f->id, fig, ps)) continue;
                        po += num(t) + "," + std::to_string(f->player) + "," + std::to_string(f->id) + "," +
                              std::to_string(fig) + "," + num(ps.x) + "," + num(ps.y) + "," + num(ps.yaw) + "," +
                              (ps.moving ? "1" : "0") + "," + num(ps.z) + "\n";
                    }
            }
            if (!writeText(out + "/motion.csv", po)) return fail(5, "cannot write motion.csv into " + out);
        }
    }

    // the timeline: the start scene, the first frame, the combat's frames, the tail
    units::SceneFileStatus ss = units::writeSceneFile(gamefs::fromUtf8(out + "/start.c5units"), scene);
    if (!ss.ok()) return fail(5, "start.c5units: " + ss.str());
    Timeline tl;
    tl.scene_path = "start.c5units";
    tl.seed = seed;
    tl.frames.push_back(first);
    std::vector<TimelineFrame> fr = live ? liveFrames : combatFrames(r, 1.0 / 30.0, 2);
    for (auto& f : fr) tl.frames.push_back(f);
    uint64_t seq = tl.frames.back().seq;
    for (int k = 0; k < tail; ++k) {
        TimelineFrame t;
        t.seq = ++seq;
        t.dt = 1.0 / 30.0;
        tl.frames.push_back(t);
    }
    TimelineStatus ts = writeTimelineFile(gamefs::fromUtf8(out + "/combat.c5anim"), tl, &scene);
    if (!ts.ok()) return fail(5, "combat.c5anim: " + ts.str());
    std::printf("combat_scene Ok end %.3f at_limit %d steps %zu hits %zu events %zu frames %zu\n", r.end,
                r.at_limit ? 1 : 0, r.steps.size(), r.hits.size(), r.events.size(), tl.frames.size());
    return 0;
}
