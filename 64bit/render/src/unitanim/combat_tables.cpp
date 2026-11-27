// combat_tables.cpp - the combat choreography's art tables (epic 0042, issue 4775; combat.h): ArtDefine_UnitMemberCombats
// and ArtDefine_UnitMemberCombatWeapons (combat-choreography.md 6).
#include <algorithm>
#include <cstdlib>

#include "c5r/unitanim/combat.h"

namespace c5r { namespace unitanim {

namespace {
bool truthy(const c5m::DbValue& v) { return !v.null && !v.text.empty() && std::atof(v.text.c_str()) != 0; }
}  // namespace

bool loadCombatTables(c5m::TableSource& db, CombatArtTables& out, std::string& err) {
    return loadCombatTables(db, out, err, nullptr);
}

std::string combatStrikeRule(const CombatTablesInfo& info) {
    return info.vis_kill_columns ? "VisKillStrength per landing"
                                 : "one figure per landing (database has no VisKillStrength columns)";
}

bool loadCombatTables(c5m::TableSource& db, CombatArtTables& out, std::string& err, CombatTablesInfo* info) {
    out.clear();
    err.clear();
    if (info) *info = CombatTablesInfo();
    std::vector<c5m::DbRow> rows;
    c5m::Error e;
    if (!db.rows("ArtDefine_UnitMemberCombats",
                 {"UnitMemberType", "EnableActions", "RushAttackFormation", "LastToDie", "HasShortRangedAttack",
                  "HasLongRangedAttack"},
                 rows, &e)) {
        err = "ArtDefine_UnitMemberCombats: " + e.what;
        return false;
    }
    for (auto& r : rows) {
        if (r.size() != 6 || r[0].null) continue;
        CombatArtRow& row = out[r[0].text];
        if (!r[1].null) {
            const std::string& s = r[1].text;
            size_t at = 0;
            while (at < s.size()) {
                size_t sp = s.find(' ', at);
                std::string w = s.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
                if (!w.empty()) row.enable.insert(w);
                if (sp == std::string::npos) break;
                at = sp + 1;
            }
        }
        row.rush = !r[2].null && !r[2].text.empty();
        row.last_to_die = truthy(r[3]);
        row.ranged = truthy(r[4]) || truthy(r[5]);
    }
    // 4776: the motion columns, NULL or non-positive -> the EXE's defaults (CombatMotion; [C] the DB reader 0x4B8BF0)
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberCombats",
                 {"UnitMemberType", "MoveRadius", "ShortMoveRadius", "MoveRate", "ShortMoveRate", "TurnRateMin",
                  "TurnRateMax", "ReformBeforeCombat"},
                 rows, &e)) {
        err = "ArtDefine_UnitMemberCombats (motion columns): " + e.what;
        return false;
    }
    for (auto& r : rows) {
        if (r.size() != 8 || r[0].null) continue;
        CombatMotion& m = out[r[0].text].motion;
        auto num = [](const c5m::DbValue& v, float def) {
            const double d = v.null || v.text.empty() ? 0.0 : std::atof(v.text.c_str());
            return d > 0 && d < 1e6 ? static_cast<float>(d) : def;
        };
        m.move_radius = num(r[1], m.move_radius);
        m.short_move_radius = num(r[2], 0.0f);
        m.move_rate = num(r[3], m.move_rate);
        m.short_move_rate = num(r[4], 0.0f);
        m.turn_rate_min = num(r[5], m.turn_rate_min);
        m.turn_rate_max = std::max(num(r[6], m.turn_rate_max), m.turn_rate_min);
        m.reform = r[7].null ? 0 : std::atoi(r[7].text.c_str());
    }
    // 0743: AttackRadius (M1 + 0x0C, default 8.0), half of stock's melee reach (CombatParams::melee_reach). Its own query,
    // and a database without the column (the synthetic test databases) keeps the default
    rows.clear();
    if (db.rows("ArtDefine_UnitMemberCombats", {"UnitMemberType", "AttackRadius"}, rows, &e)) {
        for (auto& r : rows) {
            if (r.size() != 2 || r[0].null) continue;
            const double v = r[1].null || r[1].text.empty() ? 0.0 : std::atof(r[1].text.c_str());
            if (v > 0 && v < 1e6) out[r[0].text].motion.attack_radius = static_cast<float>(v);
        }
    }
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberCombatWeapons", {"UnitMemberType", "Index", "WeaponTypeTag"}, rows, &e)) {
        err = "ArtDefine_UnitMemberCombatWeapons: " + e.what;
        return false;
    }
    std::map<std::string, std::map<int, std::string>> weapons;
    for (auto& r : rows) {
        if (r.size() != 3 || r[0].null || r[1].null) continue;
        weapons[r[0].text][std::atoi(r[1].text.c_str())] = r[2].null ? std::string() : r[2].text;
    }
    // 4777: the member's collision byte and attack altitude (unit-combat-server.md "Figure motion (4776)" M1, M6)
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberCombats", {"UnitMemberType", "HasCollisionAttack", "AttackAltitude"}, rows, &e)) {
        err = "ArtDefine_UnitMemberCombats (4777 columns): " + e.what;
        return false;
    }
    for (auto& r : rows) {
        if (r.size() != 3 || r[0].null) continue;
        CombatArtRow& row = out[r[0].text];
        row.collision = truthy(r[1]);
        const double alt = r[2].null || r[2].text.empty() ? 0.0 : std::atof(r[2].text.c_str());
        if (alt > 0 && alt < 1e6) row.motion.altitude = static_cast<float>(alt);
    }
    // 0685 (additive): the facing turn rates, NULL or non-positive -> the EXE's defaults (CombatMotion), max raised to min.
    // A database without the columns (a scene bundle's or a test's TSV written before 0685) keeps the defaults: VP 5.4.8's
    // rows are all NULL or 15 / 20 (unit-server-scene.md 19.10), so the result is the same; nothing fails
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberCombats", {"UnitMemberType", "TurnFacingRateMin", "TurnFacingRateMax"}, rows, &e))
        rows.clear();
    for (auto& r : rows) {
        if (r.size() != 3 || r[0].null) continue;
        CombatMotion& m = out[r[0].text].motion;
        auto num = [](const c5m::DbValue& v, float def) {
            const double d = v.null || v.text.empty() ? 0.0 : std::atof(v.text.c_str());
            return d > 0 && d < 1e6 ? static_cast<float>(d) : def;
        };
        m.turn_facing_rate_min = num(r[1], m.turn_facing_rate_min);
        m.turn_facing_rate_max = std::max(num(r[2], m.turn_facing_rate_max), m.turn_facing_rate_min);
    }
    // 4777: the weapon columns the special cases read, by (member, Index)
    rows.clear();
    if (!db.rows("ArtDefine_UnitMemberCombatWeapons",
                 {"UnitMemberType", "Index", "IsDropped", "TargetGround", "ProjectileSpeed", "AreaDamageDelay",
                  "WaitForEffectCompletion", "HitEffect", "HitEffectScale", "WeaponTypeSoundOverrideTag"},
                 rows, &e)) {
        err = "ArtDefine_UnitMemberCombatWeapons (4777 columns): " + e.what;
        return false;
    }
    auto num = [](const c5m::DbValue& v) {
        const double d = v.null || v.text.empty() ? 0.0 : std::atof(v.text.c_str());
        return d > 0 && d < 1e6 ? static_cast<float>(d) : 0.0f;
    };
    std::map<std::pair<std::string, int>, CombatWeapon> extra;
    for (auto& r : rows) {
        if (r.size() != 10 || r[0].null || r[1].null) continue;
        CombatWeapon& w = extra[std::make_pair(r[0].text, std::atoi(r[1].text.c_str()))];
        w.dropped = truthy(r[2]);
        w.target_ground = truthy(r[3]);
        w.speed = num(r[4]);
        w.area_delay = num(r[5]);
        w.wait_effect = truthy(r[6]);
        w.effect = r[7].null ? std::string() : r[7].text;
        const float sc = num(r[8]);
        w.effect_scale = sc > 0 ? sc : 0.0f;   // NULL, 0 or below: the striker's member scale (effects-formats.md 15.7.8)
        w.sound = r[9].null ? std::string() : r[9].text;
    }
    // 4783 (W33, C1): VisKillStrengthMin / Max, the figures one landing strikes (combat-choreography.md 5.4 [C]); NULL or
    // below 0 is 0, as the EXE's reader clamps. A database without the two columns (the tests' hand-made tables) is not
    // an error: every weapon then strikes one figure per landing, the 4775 rule.
    rows.clear();
    if (db.rows("ArtDefine_UnitMemberCombatWeapons", {"UnitMemberType", "Index", "VisKillStrengthMin", "VisKillStrengthMax"},
                rows, &e)) {
        if (info) info->vis_kill_columns = true;
        for (auto& r : rows) {
            if (r.size() != 4 || r[0].null || r[1].null) continue;
            CombatWeapon& w = extra[std::make_pair(r[0].text, std::atoi(r[1].text.c_str()))];
            w.vis_kill_min = num(r[2]);
            w.vis_kill_max = num(r[3]);
        }
    }
    for (auto& kv : weapons) {
        CombatArtRow& row = out[kv.first];
        for (auto& w : kv.second) {
            CombatWeapon cw;
            cw.vs_city = w.first >= 1 && w.second == "FLAMING_ARROW";
            cw.projectile = cw.vs_city || (w.first == 0 && row.ranged);
            auto x = extra.find(std::make_pair(kv.first, w.first));
            if (x != extra.end()) {
                const CombatWeapon& e4 = x->second;
                cw.dropped = e4.dropped;
                cw.target_ground = e4.target_ground;
                cw.speed = e4.speed;
                cw.area_delay = e4.area_delay;
                cw.wait_effect = e4.wait_effect;
                cw.effect = e4.effect;
                cw.effect_scale = e4.effect_scale;
                cw.sound = e4.sound;
                cw.vis_kill_min = e4.vis_kill_min;
                cw.vis_kill_max = e4.vis_kill_max;
                if (cw.dropped) cw.projectile = true;   // a bomb is a projectile (0321 13.5)
            }
            if (cw.sound.empty()) cw.sound = w.second;   // WeaponTypeTag when no override
            cw.one_shot = row.collision;                 // M6: the member's HasCollisionAttack
            row.weapons.push_back(cw);
        }
    }
    return true;
}

}}  // namespace c5r::unitanim
