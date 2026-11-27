// effect_routes.cpp - the trigger intake of a fight's effects (epic 0042, ticket 4793; c5r/effects/effect_routes.h).
// Own code, GPL-3.0-or-later. Written from the knowledge base only (D83: no decompiler output, no game file content).
#include "c5r/effects/effect_routes.h"

#include <cmath>
#include <cstdlib>

namespace c5r { namespace effects {

namespace {

bool finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

bool hasVar(const std::string& name, const char* var) { return name.find(std::string("$(") + var + ")") != std::string::npos; }

c5m::FxSubstitutions toSubs(const NameValues& v, const std::string* surface, bool emptyWeapon) {
    c5m::FxSubstitutions s;
    if (surface) {
        s.set_terrain(*surface).set_base_terrain(*surface);
    } else {
        if (v.has_terrain) s.set_terrain(v.terrain);
        if (v.has_base_terrain) s.set_base_terrain(v.base_terrain);
        else if (v.has_terrain) s.set_base_terrain(v.terrain);
    }
    if (v.has_weapon) s.set_weapon(v.weapon);
    else if (emptyWeapon) s.set_weapon("");
    return s;
}

}  // namespace

const char* nameResultName(NameResult r) {
    switch (r) {
        case NameResult::Exact: return "Exact";
        case NameResult::Fallback: return "Fallback";
        case NameResult::WeaponEmpty: return "WeaponEmpty";
        case NameResult::NotDefined: return "NotDefined";
        case NameResult::BadName: return "BadName";
    }
    return "?";
}

const char* hitRouteName(HitRoute r) {
    switch (r) {
        case HitRoute::None: return "None";
        case HitRoute::Flight: return "Flight";
        case HitRoute::OneShot: return "OneShot";
        case HitRoute::CityVolley: return "CityVolley";
        case HitRoute::CityDefence: return "CityDefence";
        case HitRoute::Refused: return "Refused";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------- names
bool EffectNames::tryName(const std::string& name, const c5m::FxSubstitutions& s, std::string& out, c5m::Error& e) const {
    std::string n;
    if (!c5m::fx_substitute(name, s, n, &e)) return false;
    if (!lib_.vis_effect(n)) {
        e.code = c5m::Err::NotFound;
        e.what = "unknown effect name: " + n;
        out = n;
        return false;
    }
    out = n;
    return true;
}

NameResult EffectNames::resolve(const std::string& name, const NameValues& v, std::string& out, std::string* why) {
    out.clear();
    if (why) why->clear();
    // a name with an unknown variable or an unterminated "$(" is refused before any value is tried
    for (const std::string& var : c5m::fx_variables(name))
        if (var != "TERRAIN" && var != "BASETERRAIN" && var != "WEAPON") {
            ++st_.bad_name;
            ++st_.refused[name];
            if (why) *why = "unknown variable $(" + var + ") in " + name;
            return NameResult::BadName;
        }
    const bool surfaceVar = hasVar(name, "TERRAIN") || hasVar(name, "BASETERRAIN");
    const bool weaponVar = hasVar(name, "WEAPON");
    const bool surfaceKnown = (!hasVar(name, "TERRAIN") || v.has_terrain) &&
                              (!hasVar(name, "BASETERRAIN") || v.has_base_terrain || v.has_terrain);
    const bool emptyWeapon = weaponVar && !v.has_weapon;
    c5m::Error e;
    std::string n;
    if (surfaceKnown) {
        if (tryName(name, toSubs(v, nullptr, emptyWeapon), n, e)) {
            out = n;
            if (emptyWeapon) {
                ++st_.weapon_empty;
                return NameResult::WeaponEmpty;
            }
            ++st_.exact;
            return NameResult::Exact;
        }
        if (e.code == c5m::Err::Xml) {
            ++st_.bad_name;
            ++st_.refused[name];
            if (why) *why = e.what;
            return NameResult::BadName;
        }
    }
    if (surfaceVar && !(surfaceKnown && v.terrain == fallback_ && v.base_terrain == fallback_)) {
        c5m::Error e2;
        if (tryName(name, toSubs(v, &fallback_, emptyWeapon), n, e2)) {
            out = n;
            ++st_.fallback;
            ++st_.fell_back[n];
            return NameResult::Fallback;
        }
        if (!surfaceKnown) e = e2;
    }
    ++st_.not_defined;
    ++st_.refused[name];
    if (why) *why = e.what.empty() ? "unknown effect name: " + name : e.what;
    return NameResult::NotDefined;
}

size_t EffectNames::children(const std::string& resolved, const NameValues& v, std::vector<std::string>& out) {
    out.clear();
    const c5m::VisEffectDef* d = lib_.vis_effect(resolved);
    if (!d) return 0;
    size_t refused = 0;
    for (const c5m::FxChildRef& c : d->children) {
        if (!c.trigger.empty() && c.trigger != "ProjectileEnd") continue;
        std::string n;
        const NameResult r = resolve(c.name, v, n);
        if (r == NameResult::NotDefined || r == NameResult::BadName)
            ++refused;
        else
            out.push_back(n);
    }
    return refused;
}

float EffectNames::baseDuration(const std::string& resolved) const {
    const c5m::VisEffectDef* d = lib_.vis_effect(resolved);
    if (!d) return -1;
    return c5m::EffectLibrary::duration(*d, -1.0f);
}

float EffectNames::movementSpeed(const std::string& resolved) const {
    const c5m::VisEffectDef* d = lib_.vis_effect(resolved);
    return d && d->has_movement_speed ? d->movement_speed : 0.0f;
}

// ---------------------------------------------------------------------------------------------- held projectiles
size_t HeldEffects::add(const std::string& file, const c5m::TriggerSystem& ts) {
    File f;
    std::set<int32_t> effects;
    for (const c5m::FtsTrigger& t : ts.triggers)
        if (t.type == c5m::TriggerType::Effect) effects.insert(t.id);
    for (const c5m::FtsTrigger& t : ts.triggers) {
        if (t.type != c5m::TriggerType::Transfer) continue;
        int32_t refid = -1;
        auto it = t.attrs.find("refid");
        if (it != t.attrs.end() && !it->second.empty()) {
            char* end = nullptr;
            const long v = std::strtol(it->second.c_str(), &end, 10);
            if (end && *end == 0) refid = static_cast<int32_t>(v);
        }
        if (refid >= 0 && !effects.count(refid)) refid = -1;   // a Sound row or no row: not an effect in the hand
        f.transfers[t.id] = refid;
        if (refid >= 0 && !f.held.count(refid)) f.held[refid] = t.id;
    }
    const size_t n = f.held.size();
    files_[file] = f;
    return n;
}

bool HeldEffects::held(const std::string& file, int32_t trigger, int32_t* transfer) const {
    auto f = files_.find(file);
    if (f == files_.end()) return false;
    auto it = f->second.held.find(trigger);
    if (it == f->second.held.end()) return false;
    if (transfer) *transfer = it->second;
    return true;
}

int32_t HeldEffects::refidOf(const std::string& file, int32_t transfer) const {
    auto f = files_.find(file);
    if (f == files_.end()) return -1;
    auto it = f->second.transfers.find(transfer);
    return it == f->second.transfers.end() ? -1 : it->second;
}

size_t HeldEffects::heldRows() const {
    size_t n = 0;
    for (const auto& f : files_) n += f.second.held.size();
    return n;
}

// ---------------------------------------------------------------------------------------------- combat hits
bool planHitEffect(const unitanim::CombatHit& h, EffectNames& names, const NameValues& target, const HitPlanParams& p,
                   HitEffectPlan& out) {
    out = HitEffectPlan();
    if (!std::isfinite(h.time) || !std::isfinite(h.launch) || !finite3(h.pos) || !finite3(h.origin) ||
        !std::isfinite(h.scale) || !std::isfinite(p.city_launch_deg) || !std::isfinite(p.city_launch_height) ||
        !std::isfinite(p.city_flight)) {
        out.why = "a value of the hit or the parameters is not finite";
        return false;
    }
    if (h.effect.empty()) {
        out.why = "the hit names no effect";
        return true;
    }
    if (h.area) {
        out.why = "an area share: the blast's effect rides on the first hit";
        return true;
    }
    if (h.city && !h.projectile) {
        out.route = HitRoute::CityDefence;
        out.why = "a city's answer in a melee: no effect [I]";
        return true;
    }
    if (h.projectile && !h.city) {
        out.route = HitRoute::Flight;
        out.why = "the projectile's flight starts its HitEffect where it lands (4792)";
        return true;
    }
    if (!h.has_pos) {
        out.why = "the hit has no position";
        return true;
    }
    std::string name, why;
    const NameResult r = names.resolve(h.effect, target, name, &why);
    if (r == NameResult::NotDefined || r == NameResult::BadName) {
        out.route = HitRoute::Refused;
        out.why = why;
        return true;
    }
    out.effect = name;
    if (!h.city) {
        out.route = HitRoute::OneShot;
        out.time = h.time;
        out.pos[0] = h.pos[0];
        out.pos[1] = h.pos[1];
        out.pos[2] = h.pos[2];
        // HitEffectScale when set; 0 = unset: the caller takes the striking member's own scale (never 1.0, which draws a
        // hit about ten times stock's size: effects-formats.md 15.7.8)
        out.scale = h.scale > 0 ? h.scale : 0.0f;
        return true;
    }
    // a city's volley: the era's projectile VisEffect from the city's centre to the landing
    if (!h.has_origin) {
        out.route = HitRoute::None;
        out.why = "a city volley without the city's position";
        return true;
    }
    const V3 L{h.origin[0], h.origin[1], h.origin[2] + p.city_launch_height};
    const V3 T{h.pos[0], h.pos[1], h.pos[2]};
    float dx = T.x - L.x, dy = T.y - L.y;
    const float hl = std::sqrt(dx * dx + dy * dy);
    if (hl > 1e-6f) {
        dx /= hl;
        dy /= hl;
    } else {
        dx = 0;
        dy = 0;
    }
    const float a = p.city_launch_deg * 3.14159265358979323846f / 180.0f;
    V3 D{dx * std::cos(a), dy * std::cos(a), std::sin(a)};
    if (hl <= 1e-6f) D = V3{0, 0, 1};
    ProjectileSpec& s = out.flight;
    s.player = h.player;
    s.unit = h.id;
    s.figure = 0;
    s.effect = name;
    // 0742: the caller's scales (1.0 by default, the rule before 0742); a value not above 0 or not finite: 1.0
    s.scale = std::isfinite(p.city_scale) && p.city_scale > 0 ? p.city_scale : 1.0f;
    names.children(name, target, s.children);
    s.child_scale = std::isfinite(p.city_child_scale) && p.city_child_scale > 0 ? p.city_child_scale : 1.0f;
    s.hit = h.result != unitanim::HitResult::Spent;
    arcPoints(L, T, D, s.path.p);
    s.path.kind = PathKind::Uniform;
    const double flight = h.time - h.launch;
    s.path.duration = flight > 1e-3 ? static_cast<float>(flight) : p.city_flight;
    s.t0 = flight > 1e-3 ? h.launch : h.time - p.city_flight;
    out.route = HitRoute::CityVolley;
    out.time = s.t0;
    return true;
}

// ---------------------------------------------------------------------------------------------- projectile speed
size_t applyEffectSpeeds(unitanim::CombatArtTables& tables,
                         const std::map<std::string, const c5m::TriggerSystem*>& member_triggers, EffectNames& names,
                         std::vector<std::string>* notes) {
    size_t changed = 0;
    for (const auto& mt : member_triggers) {
        auto row = tables.find(mt.first);
        if (row == tables.end() || !mt.second) continue;
        const c5m::TriggerSystem& ts = *mt.second;
        std::map<int32_t, const c5m::FtsTrigger*> effects;
        for (const c5m::FtsTrigger& t : ts.triggers)
            if (t.type == c5m::TriggerType::Effect) effects[t.id] = &t;
        float speed = 0;
        std::string first;
        for (const c5m::FtsTrigger& t : ts.triggers) {
            if (t.type != c5m::TriggerType::Transfer) continue;
            auto ra = t.attrs.find("refid");
            if (ra == t.attrs.end()) continue;
            auto ef = effects.find(std::atoi(ra->second.c_str()));
            if (ef == effects.end()) continue;
            auto ev = ef->second->attrs.find("event");
            if (ev == ef->second->attrs.end()) continue;
            std::string n;
            const NameResult r = names.resolve(ev->second, NameValues(), n);
            if (r == NameResult::NotDefined || r == NameResult::BadName) continue;
            const float s = names.movementSpeed(n);
            if (first.empty()) {
                first = n;
                speed = s;
            } else if (s != speed && notes) {
                notes->push_back(mt.first + ": Transfers name " + first + " and " + n + " of different speeds; the first is used");
            }
        }
        if (!(speed > 0)) continue;
        for (unitanim::CombatWeapon& w : row->second.weapons) {
            if (!w.projectile || w.dropped) continue;
            if (w.speed != speed) {
                w.speed = speed;
                ++changed;
            }
        }
    }
    return changed;
}

}}  // namespace c5r::effects
