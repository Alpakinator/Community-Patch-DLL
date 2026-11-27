// projscene.cpp - ticket 4792: the projectiles of one recorded combat, offline (D137). Reads a scene folder that
// c5r_unitanim_combat_scene --positions wrote (start.c5units, hits.csv, motion.csv, summary.txt; 0557's recorded fights
// through render/evidence/0042/4776/combat_scenes.py), finds for every projectile hit its Transfer trigger and the
// Effect trigger its refid names in the striker's member trigger files, picks the weapon row, plans the flight
// (c5r/effects/projectile.h) and runs the flights through a ProjectileSet frame by frame (1/30 s) to the combat's end.
//
//   c5r_effects_projscene --game INSTALL --db DB --scene DIR --attacker-plot X:Y --defender-plot X:Y
//       [--city] [--seed N] [--launch-height H] [--model-mode effect|server]
//
// Writes DIR/projectiles.csv (one row per flight) and DIR/projectile_records.txt (one line per record) and prints
// `projscene Ok flights N landed L removed R late K core_end E last_land M ...`. Stand-ins, named [I]: the launch point
// is the striker figure's position at the Effect trigger's time at --launch-height (default 10) above the flat field
// (the bone is not posed offline); the target figure's position at the launch from motion.csv; a city target is the
// city plot's centre. Exit codes: 0 Ok; 2 usage; 4 inputs (install, database, tables, scene files); 5 an output could not
// be written; 6 a projectile hit whose trigger rows or weapon were not found (each named). Reads only; writes only into
// DIR; nothing from the game's files is written but names and numbers.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5models/effect_xml.h"
#include "c5models/logic.h"
#include "c5r/effects/projectile.h"
#include "c5r/effects/projectile_model.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/art.h"
#include "c5r/units/placement.h"
#include "c5r/units/scene_file.h"

using namespace c5r;
using namespace c5r::effects;

namespace {

int fail(int code, const std::string& why) {
    std::fprintf(stderr, "projscene: %s\n", why.c_str());
    std::printf("projscene FAIL %d %s\n", code, why.c_str());
    return code;
}
std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == c) {
            out.push_back(cur);
            cur.clear();
        } else if (ch != '\r') {
            cur += ch;
        }
    }
    out.push_back(cur);
    return out;
}
bool readText(const std::string& path, std::string& out) {
    std::ifstream in(gamefs::fromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
bool writeText(const std::string& path, const std::string& text) {
    FILE* f = _wfopen(gamefs::fromUtf8(path).c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}
bool plotArg(const std::string& v, int& x, int& y) {
    auto p = split(v, ':');
    if (p.size() != 2) return false;
    x = std::atoi(p[0].c_str());
    y = std::atoi(p[1].c_str());
    return true;
}
std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", v);
    return b;
}
std::string attr(const c5m::FtsTrigger& t, const char* k) {
    auto it = t.attrs.find(k);
    return it == t.attrs.end() ? std::string() : it->second;
}

struct Pose {
    double t;
    float x, y, yaw;
};
struct MemberRow {
    float target_radius = kDefaultTargetRadius, target_height = 0, move_rate = 0.78125f;
};

}  // namespace

int main(int argc, char** argv) {
    std::string game, db, scene, mode = "effect";
    int ax = -1, ay = -1, dx = -1, dy = -1;
    bool city = false;
    uint64_t seed = 1;
    float launchHeight = 10.0f;
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
        if (a == "--scene" && next(scene)) continue;
        if (a == "--attacker-plot" && next(v) && plotArg(v, ax, ay)) continue;
        if (a == "--defender-plot" && next(v) && plotArg(v, dx, dy)) continue;
        if (a == "--city") { city = true; continue; }
        if (a == "--seed" && next(v)) { seed = std::strtoull(v.c_str(), nullptr, 10); continue; }
        if (a == "--launch-height" && next(v)) { launchHeight = static_cast<float>(std::atof(v.c_str())); continue; }
        if (a == "--model-mode" && next(mode) && (mode == "effect" || mode == "server")) continue;
        return fail(2, "unknown or incomplete argument " + a);
    }
    if (game.empty() || db.empty() || scene.empty() || ax < 0 || dx < 0 || !std::isfinite(launchHeight))
        return fail(2, "needs --game, --db, --scene, --attacker-plot and --defender-plot");

    // ---- inputs
    gamefs::GameFiles gf;
    Status s = gf.open(gamefs::fromUtf8(game));
    if (!s.ok()) return fail(4, "game files: " + s.str());
    c5m::SqliteTables tables;
    c5m::Error e;
    if (!c5m::SqliteTables::available() || !tables.open(db, &e)) return fail(4, "database: " + e.what);
    units::GameFilesProvider files(gf);
    units::ArtResolver resolver;
    units::UnitStatus us = resolver.open(tables, files);
    if (!us.ok()) return fail(4, "art tables: " + us.str());
    c5m::EffectLibrary lib;
    std::string err;
    if (!loadInstallEffects(gf, files, lib, err)) return fail(4, err);
    ProjectileWeaponTable weapons;
    if (!loadProjectileWeapons(tables, weapons, err)) return fail(4, err);
    std::map<std::string, MemberRow> memberRows;
    {
        std::vector<c5m::DbRow> rows;
        if (!tables.rows("ArtDefine_UnitMemberCombats", {"UnitMemberType", "TargetRadius", "TargetHeight", "MoveRate"},
                         rows, &e))
            return fail(4, "ArtDefine_UnitMemberCombats: " + e.what);
        for (auto& r : rows) {
            if (r.size() != 4 || r[0].null) continue;
            MemberRow m;
            auto f = [](const c5m::DbValue& v, float def) {
                const double d = v.null || v.text.empty() ? 0.0 : std::atof(v.text.c_str());
                return d > 0 && d < 1e6 ? static_cast<float>(d) : def;
            };
            m.target_radius = f(r[1], kDefaultTargetRadius);
            m.target_height = r[2].null ? 0.0f : static_cast<float>(std::atof(r[2].text.c_str()));
            m.move_rate = f(r[3], 0.78125f);
            memberRows[r[0].text] = m;
        }
    }
    units::UnitScene sc;
    units::SceneFileStatus ss = units::readSceneFile(gamefs::fromUtf8(scene + "/start.c5units"), sc);
    if (!ss.ok()) return fail(4, "start.c5units: " + ss.str());
    std::string hitsText, motionText, summaryText;
    if (!readText(scene + "/hits.csv", hitsText) || !readText(scene + "/motion.csv", motionText) ||
        !readText(scene + "/summary.txt", summaryText))
        return fail(4, "the scene folder needs hits.csv, motion.csv (combat_scene --positions) and summary.txt");
    double coreEnd = 0;
    for (auto& line : split(summaryText, '\n'))
        if (line.compare(0, 4, "end ") == 0) coreEnd = std::atof(line.c_str() + 4);

    // the units' members and their trigger files
    struct UnitArtT {
        units::UnitArt art;
        std::vector<std::vector<c5m::TriggerSystem>> triggers;   // per member
    };
    std::map<std::pair<int32_t, int32_t>, UnitArtT> arts;
    for (auto& u : sc.units) {
        UnitArtT ua;
        std::string row = u.art_row;
        if (row.empty()) {
            us = resolver.art_row(u.unit_type, "", "", row);
            if (!us.ok()) return fail(4, u.unit_type + ": " + us.str());
        }
        us = resolver.resolve(row, ua.art);
        if (!us.ok()) return fail(4, row + ": " + us.str());
        for (auto& m : ua.art.members) {
            std::vector<c5m::TriggerSystem> tsv;
            for (auto& rt : m.files.timed_triggers) {
                c5m::Bytes b;
                if (!files.read(c5m::basename_lower(rt.file.name), b, nullptr)) continue;
                c5m::TriggerSystem ts;
                if (c5m::parse_ftsxml(reinterpret_cast<const char*>(b.data()), b.size(), ts, &e)) tsv.push_back(ts);
            }
            ua.triggers.push_back(tsv);
        }
        arts[std::make_pair(u.player, u.id)] = ua;
    }
    // poses
    std::map<std::string, std::vector<Pose>> poses;   // "player:id:figure"
    {
        auto lines = split(motionText, '\n');
        for (size_t i = 1; i < lines.size(); ++i) {
            auto c = split(lines[i], ',');
            if (c.size() < 7) continue;
            poses[c[1] + ":" + c[2] + ":" + c[3]].push_back(
                Pose{std::atof(c[0].c_str()), static_cast<float>(std::atof(c[4].c_str())),
                     static_cast<float>(std::atof(c[5].c_str())), static_cast<float>(std::atof(c[6].c_str()))});
        }
    }
    auto poseAt = [&](int32_t p, int32_t id, int fig, double t, Pose& out) {
        auto it = poses.find(std::to_string(p) + ":" + std::to_string(id) + ":" + std::to_string(fig));
        if (it == poses.end() || it->second.empty()) return false;
        out = it->second.front();
        for (auto& q : it->second)
            if (std::fabs(q.t - t) < std::fabs(out.t - t)) out = q;
        return true;
    };
    // the stand-in field of combat_scene --positions (its plot centres)
    heightfield::HeightGrid grid;
    grid.mapW = static_cast<uint32_t>(std::max(ax, dx) + 4);
    grid.mapH = static_cast<uint32_t>(std::max(ay, dy) + 8);
    grid.samplesX = grid.mapW * 64;
    grid.samplesY = grid.mapH * 64;
    grid.ox = static_cast<float>(grid.mapW * 32);
    grid.oy = static_cast<float>(grid.mapH * 32) * grid.sy;
    const float h2w[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 1, 0, 32, 128, 0, 1};
    for (int k = 0; k < 16; ++k) grid.hexToWorld[k] = h2w[k];
    grid.h.assign(static_cast<size_t>(grid.samplesX) * grid.samplesY, 0);
    float cityX = 0, cityY = 0;
    if (!units::plotCentre(grid, dx, dy, cityX, cityY).ok()) return fail(2, "--defender-plot outside the field");

    // ---- the flights
    ProjectileSet set;
    set.setModelMode(mode == "server" ? ModelMode::ServerModel : ModelMode::EffectOnly);
    FlightRandom rng;
    rng.state = seed;
    std::string csv = "launch_s,effect_start_s,player,id,figure,member,code,trigger,refid,effect,weapon_tag,weapon_id,"
                      "speed,distance,flight_s,land_s,result,target,kind,radius,children,hit_effect\n";
    std::vector<std::string> missing;
    double lastLand = 0;
    size_t flights = 0;
    std::map<std::string, std::pair<size_t, double>> perClass;   // weapon tag -> (count, flight seconds summed)
    auto lines = split(hitsText, '\n');
    for (size_t i = 1; i < lines.size(); ++i) {
        auto c = split(lines[i], ',');
        if (c.size() < 12 || c[10] != "1") continue;   // projectile hits only
        const double launch = std::atof(c[1].c_str());
        const int32_t player = std::atoi(c[2].c_str()), id = std::atoi(c[3].c_str());
        const int fig = std::atoi(c[4].c_str()), code = std::atoi(c[5].c_str()), trig = std::atoi(c[6].c_str());
        const int32_t tp = std::atoi(c[7].c_str()), tid = std::atoi(c[8].c_str()), tfig = std::atoi(c[9].c_str());
        const std::string result = c[11];
        auto ua = arts.find(std::make_pair(player, id));
        if (ua == arts.end()) {
            missing.push_back("striker " + c[2] + ":" + c[3] + " not in the scene");
            continue;
        }
        // the striker's member by figure index (member order)
        int mi = -1, base = 0;
        for (size_t m = 0; m < ua->second.art.members.size(); ++m) {
            if (fig < base + ua->second.art.members[m].count) {
                mi = static_cast<int>(m);
                break;
            }
            base += ua->second.art.members[m].count;
        }
        if (mi < 0) {
            missing.push_back("figure " + c[4] + " past the members of " + c[2] + ":" + c[3]);
            continue;
        }
        const units::MemberArt& member = ua->second.art.members[static_cast<size_t>(mi)];
        const c5m::FtsTrigger* tr = nullptr;
        const c5m::FtsTrigger* ef = nullptr;
        for (auto& ts : ua->second.triggers[static_cast<size_t>(mi)]) {
            for (auto& t : ts.triggers)
                if (t.type == c5m::TriggerType::Transfer && t.id == trig && (t.ec == code || t.ec == code + 1000)) tr = &t;
            if (!tr) continue;
            const int refid = std::atoi(attr(*tr, "refid").c_str());
            for (auto& t : ts.triggers)
                if (t.id == refid && t.type == c5m::TriggerType::Effect) ef = &t;
            break;
        }
        if (!tr || !ef) {
            missing.push_back(member.member + " ec " + c[5] + " trigger " + c[6] + (tr ? ": refid Effect row" : ": Transfer row") +
                              " not found");
            continue;
        }
        const std::string effect = attr(*ef, "event");
        auto wr = weapons.find(member.member);
        const ProjectileWeapon* w = wr == weapons.end() ? nullptr : pickWeapon(wr->second, effect, 0);
        if (!w) {
            missing.push_back(member.member + ": no weapon row");
            continue;
        }
        const c5m::VisEffectDef* vd = lib.vis_effect(effect);
        const double effectStart = launch - (tr->time - ef->time);
        Pose sp;
        if (!poseAt(player, id, fig, effectStart, sp)) {
            missing.push_back("no pose of " + c[2] + ":" + c[3] + ":" + c[4]);
            continue;
        }
        LaunchRequest rq;
        rq.weapon = w;
        rq.launch = V3{sp.x, sp.y, launchHeight};
        rq.tangent_local = V3{static_cast<float>(std::atof(attr(*tr, "tangent.x").c_str())),
                              static_cast<float>(std::atof(attr(*tr, "tangent.y").c_str())),
                              static_cast<float>(std::atof(attr(*tr, "tangent.z").c_str()))};
        const float cy = std::cos(sp.yaw), sy = std::sin(sp.yaw);
        rq.figure_world.m[0] = cy;
        rq.figure_world.m[1] = sy;
        rq.figure_world.m[4] = -sy;
        rq.figure_world.m[5] = cy;
        rq.figure_world.m[12] = sp.x;
        rq.figure_world.m[13] = sp.y;
        rq.attacker_pos = rq.launch;
        rq.attacker_heading = sp.yaw;
        rq.effect_movement_speed = vd && vd->has_movement_speed ? vd->movement_speed : 0;
        rq.miss = result == "spent";
        std::string targetText;
        if (tfig < 0 || tid < 0) {
            rq.target = TargetKind::Participant;
            rq.target_pos = V3{cityX, cityY, 0};
            targetText = "city";
        } else {
            Pose tpz;
            if (!poseAt(tp, tid, tfig, launch, tpz)) {
                missing.push_back("no pose of the target " + c[7] + ":" + c[8] + ":" + c[9]);
                continue;
            }
            rq.target_pos = V3{tpz.x, tpz.y, 0};
            auto ta = arts.find(std::make_pair(tp, tid));
            if (ta != arts.end()) {
                int b2 = 0;
                for (auto& m : ta->second.art.members) {
                    if (tfig < b2 + m.count) {
                        auto mr = memberRows.find(m.member);
                        if (mr != memberRows.end()) {
                            rq.target_radius = mr->second.target_radius;
                            rq.target_height = mr->second.target_height;
                            rq.target_move_rate = mr->second.move_rate;
                        }
                        if (m.domain == "Air") rq.target = TargetKind::FlyingFigure;
                        break;
                    }
                    b2 += m.count;
                }
            }
            targetText = c[7] + ":" + c[8] + ":" + c[9];
        }
        LaunchPlan lp;
        std::string why;
        if (!planLaunch(rq, rng, HeightFn(), lp, &why)) {
            missing.push_back("launch refused: " + why);
            continue;
        }
        // the children and the hit effect with the surface of the target ($(TERRAIN): CITY or DIRT on the flat field)
        c5m::FxSubstitutions subs;
        const std::string surface = city || rq.target == TargetKind::Participant ? "CITY" : "DIRT";
        subs.set_terrain(surface).set_base_terrain(surface).set_weapon("");
        ProjectileSpec spec;
        spec.owner = 1;
        spec.player = player;
        spec.unit = id;
        spec.figure = static_cast<uint16_t>(fig);
        spec.effect = effect;
        spec.scale = member.scale > 0 ? member.scale : 1.0f;   // the firing member's Scale (effects-server.md 2.4 [L])
        spec.path = lp.path;
        spec.t0 = launch;
        spec.hit = !rq.miss;
        spec.child_scale = w->child_scale;
        std::string childText;
        if (vd)
            for (auto& ch : vd->children) {
                std::string n;
                if (c5m::fx_substitute(ch.name, subs, n, &e) && lib.vis_effect(n)) {
                    spec.children.push_back(n);
                } else {
                    // no variant for this surface (effects-formats.md 14.3): DIRT [I]
                    c5m::FxSubstitutions dirt;
                    dirt.set_terrain("DIRT").set_base_terrain("DIRT").set_weapon("");
                    if (c5m::fx_substitute(ch.name, dirt, n, &e) && lib.vis_effect(n)) spec.children.push_back(n);
                }
                childText += (childText.empty() ? "" : " ") + (spec.children.empty() ? ch.name : spec.children.back());
            }
        if (!w->hit_effect.empty()) {
            std::string n;
            if (c5m::fx_substitute(w->hit_effect, subs, n, &e) && lib.vis_effect(n)) spec.hit_effect = n;
            spec.hit_scale = hitScaleOf(*w, spec.scale);   // NULL: the firing member's (effects-formats.md 15.7.8)
        }
        uint32_t h = 0;
        if (!set.add(spec, h, &why)) {
            missing.push_back("flight refused: " + why);
            continue;
        }
        ++flights;
        const double land = launch + lp.path.duration;
        lastLand = std::max(lastLand, land);
        auto& pc = perClass[w->weapon_tag.empty() ? std::string("(none)") : w->weapon_tag];
        pc.first++;
        pc.second += lp.path.duration;
        V3 d{lp.target.x - rq.launch.x, lp.target.y - rq.launch.y, lp.target.z - rq.launch.z};
        csv += num(launch) + "," + num(effectStart) + "," + c[2] + "," + c[3] + "," + c[4] + "," + member.member + "," +
               c[5] + "," + c[6] + "," + attr(*tr, "refid") + "," + effect + "," + w->weapon_tag + "," + w->id + "," +
               num(lp.speed) + "," + num(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z)) + "," + num(lp.path.duration) +
               "," + num(land) + "," + result + "," + targetText + "," + pathKindName(lp.path.kind) + "," +
               num(lp.radius) + "," + childText + "," + spec.hit_effect + "\n";
    }
    // run the flights frame by frame to the combat's end, then a forced end of what is still in the air (12 s limit
    // case: none expected here)
    std::string log;
    std::vector<ProjectileRecord> recs;
    std::vector<ModelInstance> models;
    size_t modelFrames = 0;
    const double dt = 1.0 / 30.0;
    const double stop = std::max(coreEnd, lastLand) + dt;
    for (double t = 0; t <= stop + 1e-9; t += dt) {
        recs.clear();
        models.clear();
        set.frame(t, recs, &models);
        modelFrames += models.empty() ? 0 : 1;
        for (auto& r : recs) {
            const V3 p = r.world.translation();
            log += num(r.time) + " " + recordKindName(r.kind) + " h" + std::to_string(r.handle) + " " + r.effect + " " +
                   num(p.x) + " " + num(p.y) + " " + num(p.z) + " s" + num(r.scale) + "\n";
        }
    }
    recs.clear();
    const size_t forced = set.forceEnd(stop, UINT64_MAX, recs);
    for (auto& r : recs) log += num(r.time) + " " + recordKindName(r.kind) + " h" + std::to_string(r.handle) + "\n";
    if (!writeText(scene + "/projectiles.csv", csv) || !writeText(scene + "/projectile_records.txt", log))
        return fail(5, "cannot write projectiles.csv / projectile_records.txt into " + scene);
    std::string classes;
    for (auto& kv : perClass)
        classes += " " + kv.first + "=" + std::to_string(kv.second.first) + "/" + num(kv.second.second / kv.second.first);
    const ProjectileCounts& pc = set.counts();
    std::printf("projscene %s flights %zu landed %zu removed %zu forced %zu records %zu children %zu hits %zu "
                "core_end %.3f last_land %.3f late %.3f model_frames %zu classes%s\n",
                missing.empty() ? "Ok" : "MISSING", flights, pc.landed, pc.removed, forced, pc.records, pc.children,
                pc.hits, coreEnd, lastLand, std::max(0.0, lastLand - coreEnd), modelFrames, classes.c_str());
    for (auto& m : missing) std::printf("missing: %s\n", m.c_str());
    return missing.empty() ? 0 : 6;
}
