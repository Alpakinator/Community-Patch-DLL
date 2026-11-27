// projectile.cpp - projectiles timed and moved by the server (epic 0042, ticket 4792; c5r/effects/projectile.h).
// Written from research/05-graphics-composition/effects-server.md section 1 only (D83: no decompiler output).
#include "c5r/effects/projectile.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "c5models/art.h"

namespace c5r { namespace effects {

namespace {

const float kPi = 3.14159265358979323846f;

V3 add(const V3& a, const V3& b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 sub(const V3& a, const V3& b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 scl(const V3& a, float s) { return V3{a.x * s, a.y * s, a.z * s}; }
float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(const V3& a) { return std::sqrt(dot(a, a)); }
bool finite(const V3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
bool normalise(V3& a) {
    const float l = len(a);
    if (!(l > 1e-12f) || !std::isfinite(l)) return false;
    a = scl(a, 1.0f / l);
    return true;
}
// w0 P_i + w1 P_i+1 + w2 P_i+2
V3 blend(const V3* p, float w0, float w1, float w2) {
    return V3{w0 * p[0].x + w1 * p[1].x + w2 * p[2].x, w0 * p[0].y + w1 * p[1].y + w2 * p[2].y,
              w0 * p[0].z + w1 * p[1].z + w2 * p[2].z};
}

float num(const c5m::DbValue& v, float def, bool* had) {
    if (had) *had = false;
    if (v.null || v.text.empty()) return def;
    char* end = nullptr;
    const double d = std::strtod(v.text.c_str(), &end);
    if (end == v.text.c_str() || !std::isfinite(d)) return def;
    if (had) *had = true;
    return static_cast<float>(d);
}
bool flag(const c5m::DbValue& v) { return !v.null && !v.text.empty() && std::atof(v.text.c_str()) != 0; }

}  // namespace

// ---------------------------------------------------------------------------------------------- vectors
M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}
V3 transformNormal(const V3& v, const M4& m) {
    return V3{v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8], v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9],
              v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10]};
}
V3 transformPoint(const V3& v, const M4& m) { return add(transformNormal(v, m), V3{m.m[12], m.m[13], m.m[14]}); }

void rotationQuaternion(const M4& M, float q[4]) {
    // R = M^T (the column form); Shepperd's method
    const float* m = M.m;
    const float tr = m[0] + m[5] + m[10];
    float x, y, z, w;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (m[6] - m[9]) / s;
        y = (m[8] - m[2]) / s;
        z = (m[1] - m[4]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        w = (m[6] - m[9]) / s;
        x = 0.25f * s;
        y = (m[4] + m[1]) / s;
        z = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        w = (m[8] - m[2]) / s;
        x = (m[4] + m[1]) / s;
        y = 0.25f * s;
        z = (m[9] + m[6]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        w = (m[1] - m[4]) / s;
        x = (m[8] + m[2]) / s;
        y = (m[9] + m[6]) / s;
        z = 0.25f * s;
    }
    const float l = std::sqrt(x * x + y * y + z * z + w * w);
    if (!(l > 0) || !std::isfinite(l)) {
        q[0] = q[1] = q[2] = 0;
        q[3] = 1;
        return;
    }
    q[0] = x / l;
    q[1] = y / l;
    q[2] = z / l;
    q[3] = w / l;
}

// ---------------------------------------------------------------------------------------------- weapon rows
bool loadProjectileWeapons(c5m::TableSource& db, ProjectileWeaponTable& out, std::string& err, size_t* skipped) {
    out.clear();
    err.clear();
    if (skipped) *skipped = 0;
    static const std::vector<std::string> cols = {
        "UnitMemberType", "Index", "SubIndex", "ID", "ProjectileSpeed", "ProjectileTurnRateMin",
        "ProjectileTurnRateMax", "HitEffect", "HitEffectScale", "HitRadius", "ProjectileChildEffectScale",
        "AreaDamageDelay", "ContinuousFire", "WaitForEffectCompletion", "TargetGround", "IsDropped", "WeaponTypeTag",
        "MissTargetSlopRadius"};
    std::vector<c5m::DbRow> rows;
    c5m::Error e;
    if (!db.rows("ArtDefine_UnitMemberCombatWeapons", cols, rows, &e)) {
        err = "ArtDefine_UnitMemberCombatWeapons: " + e.what;
        return false;
    }
    for (auto& r : rows) {
        if (r.size() != cols.size() || r[0].null || r[0].text.empty() || r[1].null) {
            if (skipped) ++*skipped;
            continue;
        }
        ProjectileWeapon w;
        w.member = r[0].text;
        w.index = std::atoi(r[1].text.c_str());
        w.sub_index = r[2].null ? 0 : std::atoi(r[2].text.c_str());
        w.id = r[3].null ? std::string() : r[3].text;
        // the EXE reader's rules (effects-server.md 1.1 item 1)
        w.speed = num(r[4], kNullSpeed, &w.has_speed);
        if (w.has_speed && w.speed < 0) w.speed = 2.0f;
        bool had = false;
        w.turn_min = num(r[5], 3.0f, &had);
        if (had && w.turn_min < 0) w.turn_min = 3.0f;
        w.turn_max = num(r[6], 5.0f, &had);
        if (had && w.turn_max < 0) w.turn_max = 5.0f;
        w.hit_effect = r[7].null ? std::string() : r[7].text;
        w.hit_effect_scale = num(r[8], 1.0f, &w.has_hit_effect_scale);
        if (w.has_hit_effect_scale && w.hit_effect_scale <= 0) {
            w.hit_effect_scale = 1.0f;   // unset: the member's own scale applies (hitScaleOf, effects-formats.md 15.7.8)
            w.has_hit_effect_scale = false;
        }
        w.hit_radius = std::max(0.0f, num(r[9], 0.0f, nullptr));
        w.child_scale = num(r[10], 1.0f, &w.has_child_scale);
        if (w.has_child_scale && w.child_scale <= 0) w.child_scale = kChildScaleFloor;
        w.area_damage_delay = num(r[11], -1.0f, &had);
        if (had && w.area_damage_delay < 0) w.area_damage_delay = -1.0f;
        w.continuous_fire = flag(r[12]);
        w.wait_for_effect = flag(r[13]);
        w.target_ground = flag(r[14]);
        w.dropped = flag(r[15]);
        w.weapon_tag = r[16].null ? std::string() : r[16].text;
        w.miss_slop = std::max(0.0f, num(r[17], 0.0f, nullptr));
        out[w.member].push_back(w);
    }
    for (auto& kv : out)
        std::stable_sort(kv.second.begin(), kv.second.end(), [](const ProjectileWeapon& a, const ProjectileWeapon& b) {
            return a.index != b.index ? a.index < b.index : a.sub_index < b.sub_index;
        });
    return true;
}

const ProjectileWeapon* pickWeapon(const std::vector<ProjectileWeapon>& rows, const std::string& effect, int index) {
    if (!effect.empty())
        for (auto& w : rows)
            if (w.id == effect) return &w;
    for (auto& w : rows)
        if (w.id == "PROJECTILE") return &w;
    for (auto& w : rows)
        if (w.index == index) return &w;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------- the path
const char* pathKindName(PathKind k) {
    switch (k) {
    case PathKind::Clamped: return "clamped";
    case PathKind::Uniform: return "uniform";
    case PathKind::Straight: return "straight";
    }
    return "?";
}

void arcPoints(const V3& L, const V3& T, const V3& D, V3 out[4]) {
    // effects-server.md 1.2: w on the vertical line through the chord's midpoint, used along D
    const V3 V = scl(sub(T, L), 0.5f);
    const float dd = dot(D, D), dz = D.z, vz = V.z, dv = dot(D, V);
    float den = dd - dz * dz;
    if (den < 1e-4f) den = len(V);
    const float w = den > 0 ? (dv * dz - vz * dd) / den : 0.0f;
    const V3 Q = add(L, scl(D, w));
    const V3 a = scl(sub(Q, L), 0.5f), b = scl(sub(Q, T), 0.5f);
    out[0] = sub(L, a);
    out[1] = add(L, a);
    out[2] = add(T, b);
    out[3] = sub(T, b);
}

bool evalPath(const FlightPath& path, float t, V3& pos, V3& tangent, V3* velocity) {
    if (!(path.duration > 0) || !std::isfinite(path.duration) || !std::isfinite(t)) return false;
    for (const V3& p : path.p)
        if (!finite(p)) return false;
    t = std::min(1.0f, std::max(0.0f, t));
    V3 deriv;   // per second
    if (path.kind == PathKind::Straight) {
        const V3 d = sub(path.p[3], path.p[0]);
        pos = add(path.p[0], scl(d, t));
        deriv = scl(d, 1.0f / path.duration);
        tangent = d;
    } else {
        const float u = 2.0f * t;
        int i = static_cast<int>(std::floor(u));
        float s = u - static_cast<float>(i);
        if (i >= 1 && u >= 2.0f) {   // the end: segment 1 at s = 1
            i = 1;
            s = 1.0f;
        }
        const V3* P = &path.p[i];
        V3 d;
        if (path.kind == PathKind::Uniform) {
            pos = blend(P, 0.5f * (1 - s) * (1 - s), 0.5f + s - s * s, 0.5f * s * s);
            d = blend(P, s - 1, 1 - 2 * s, s);
        } else if (i == 0) {   // Clamped, first segment
            pos = blend(P, (1 - s) * (1 - s), 2 * s - 1.5f * s * s, 0.5f * s * s);
            d = blend(P, 2 * s - 2, 2 - 3 * s, s);
        } else {               // Clamped, last segment (continuous at u = 2: P3; the EXE's exact u = 2 gives P2)
            pos = blend(P, 0.5f * (1 - s) * (1 - s), 0.5f + s - 1.5f * s * s, s * s);
            d = blend(P, s - 1, 1 - 3 * s, 2 * s);
        }
        deriv = scl(d, 2.0f / path.duration);
        tangent = d;
    }
    if (!normalise(tangent)) {
        // a degenerate point (no direction): the chord's direction, else +y
        tangent = sub(path.p[3], path.p[0]);
        if (!normalise(tangent)) tangent = V3{0, 1, 0};
    }
    if (velocity) *velocity = deriv;
    return finite(pos) && finite(tangent);
}

M4 pathFrame(const V3& pos, const V3& tangent) {
    V3 y = tangent;
    if (!normalise(y)) y = V3{0, 1, 0};
    V3 x = cross(y, V3{0, 0, 1});
    if (!normalise(x)) x = V3{1, 0, 0};
    V3 z = cross(x, y);
    normalise(z);
    M4 m;
    const V3 rows[4] = {x, y, z, pos};
    for (int r = 0; r < 4; ++r) {
        m.m[r * 4 + 0] = rows[r].x;
        m.m[r * 4 + 1] = rows[r].y;
        m.m[r * 4 + 2] = rows[r].z;
        m.m[r * 4 + 3] = r == 3 ? 1.0f : 0.0f;
    }
    return m;
}

float flightTime(float distance, float speed) {
    if (!std::isfinite(speed) || speed < kMinSpeed) speed = kMinSpeed;
    if (!std::isfinite(distance) || distance < 0) distance = 0;
    const float t = distance / kPlotUnits / speed;
    return std::min(kMaxFlight, std::max(kMinFlight, t));
}

// ---------------------------------------------------------------------------------------------- the launch
float FlightRandom::unit() {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) / 16777216.0f;
}

bool planLaunch(const LaunchRequest& r, FlightRandom& rng, const HeightFn& height, LaunchPlan& out, std::string* why) {
    out = LaunchPlan();
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    const ProjectileWeapon* w = r.weapon;
    if (!w) return fail("no weapon");
    if (!finite(r.launch) || !finite(r.tangent_local) || !finite(r.target_pos) || !finite(r.attacker_pos))
        return fail("a position or the tangent is not finite");
    for (float v : r.figure_world.m)
        if (!std::isfinite(v)) return fail("the figure's matrix is not finite");
    if (!std::isfinite(r.effect_movement_speed) || !std::isfinite(r.target_radius) ||
        !std::isfinite(r.target_height) || !std::isfinite(r.target_move_rate) || !std::isfinite(r.attacker_heading) ||
        !std::isfinite(r.attacker_move_rate) || !std::isfinite(r.water_level))
        return fail("a number is not finite");
    auto ground = [&](float x, float y) { return height ? height(x, y) : 0.0f; };
    // 3. speed
    float speed = r.effect_movement_speed != 0 ? r.effect_movement_speed : (w->has_speed ? w->speed : kNullSpeed);
    if (!(speed >= kMinSpeed)) speed = kMinSpeed;
    if (r.target == TargetKind::FlyingFigure && speed < r.target_move_rate * kAirSpeedFactor)
        speed = r.target_move_rate * kAirSpeedFactor;
    // 4. the target point
    V3 T = r.target_pos;
    float radius = 0;
    if (w->dropped) {
        const float alt = std::fabs(r.attacker_pos.z - r.water_level);
        const float lead = std::min(kDropLeadMax, alt * r.attacker_move_rate / speed);
        T.x = r.attacker_pos.x + std::sin(r.attacker_heading) * lead;
        T.y = r.attacker_pos.y - std::cos(r.attacker_heading) * lead;
        T.z = ground(T.x, T.y);
        out.dropped = true;
    } else if (r.target == TargetKind::FlyingFigure) {
        radius = 0;   // the figure's own point
    } else {
        if (w->hit_radius > 0)
            radius = w->hit_radius;
        else if (r.target == TargetKind::Participant)
            radius = w->miss_slop + kNoFigureRadius;
        else
            radius = r.miss ? w->miss_slop + r.target_radius : r.target_radius;
        if (radius != 0) {
            const float a = rng.unit() * 2.0f * kPi;
            const float d = rng.unit() * radius;
            T.x += std::cos(a) * d;
            T.y += -std::sin(a) * d;
        }
        T.z = ground(T.x, T.y);
    }
    if (!w->target_ground && r.target != TargetKind::Participant) T.z += r.target_height;
    if (!finite(T)) return fail("the target point is not finite");
    // 5. flight time
    const float dist = len(sub(T, r.launch));
    out.path.duration = flightTime(dist, speed);
    // 6. the path
    if (r.target == TargetKind::FlyingFigure) {
        out.path.kind = PathKind::Straight;
        out.path.p[0] = r.launch;
        out.path.p[1] = r.launch;
        out.path.p[2] = T;
        out.path.p[3] = T;
    } else {
        V3 D = transformNormal(r.tangent_local, r.figure_world);
        if (!normalise(D)) {
            D = sub(T, r.launch);
            if (!normalise(D)) D = V3{0, 1, 0};
        }
        out.path.kind = PathKind::Uniform;
        arcPoints(r.launch, T, D, out.path.p);
    }
    for (const V3& p : out.path.p)
        if (!finite(p)) return fail("the path is not finite");
    out.target = T;
    out.speed = speed;
    out.radius = radius;
    return true;
}

// ---------------------------------------------------------------------------------------------- the flights
const char* recordKindName(RecordKind k) {
    switch (k) {
    case RecordKind::EffectStart: return "EffectStart";
    case RecordKind::EffectMove: return "EffectMove";
    case RecordKind::EffectStop: return "EffectStop";
    case RecordKind::ChildStart: return "ChildStart";
    case RecordKind::HitStart: return "HitStart";
    }
    return "?";
}

bool ProjectileSet::add(const ProjectileSpec& spec, uint32_t& handle, std::string* why) {
    handle = 0;
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (spec.effect.empty()) return fail("no effect name");
    if (!std::isfinite(spec.t0)) return fail("start time not finite");
    if (!std::isfinite(spec.scale) || !std::isfinite(spec.child_scale) || !std::isfinite(spec.hit_scale))
        return fail("a scale is not finite");
    V3 p, t;
    if (!evalPath(spec.path, 0, p, t, nullptr)) return fail("the path does not evaluate");
    uint32_t h = spec.handle;
    if (h == 0) {
        while (flights_.count(next_) || next_ == 0) ++next_;
        h = next_++;
    } else if (flights_.count(h)) {
        return fail("handle " + std::to_string(h) + " is in use");
    }
    Flight f;
    f.spec = spec;
    f.spec.handle = h;
    flights_[h] = f;
    handle = h;
    ++counts_.launched;
    return true;
}

void ProjectileSet::frame(double t, std::vector<ProjectileRecord>& out, std::vector<ModelInstance>* models) {
    if (!std::isfinite(t)) return;
    for (auto it = flights_.begin(); it != flights_.end();) {
        Flight& f = it->second;
        const ProjectileSpec& s = f.spec;
        if (t < s.t0) {
            ++it;
            continue;
        }
        ProjectileRecord r;
        r.handle = s.handle;
        r.owner = s.owner;
        r.effect = s.effect;
        r.scale = s.scale;
        V3 pos, tan, vel;
        if (!f.started) {
            f.started = true;
            if (s.start_effect) {
                evalPath(s.path, 0, pos, tan, &vel);
                r.kind = RecordKind::EffectStart;
                r.time = s.t0;
                r.world = pathFrame(pos, tan);
                r.velocity = vel;
                out.push_back(r);
                ++counts_.records;
            }
        }
        const double el = t - s.t0;
        if (el > s.path.duration) {
            // the end (effects-server.md 1.4): children and the hit effect at the end point; no stop
            evalPath(s.path, 1, pos, tan, nullptr);
            ProjectileRecord e = r;
            e.time = s.t0 + s.path.duration;
            e.world = pathFrame(pos, tan);
            e.velocity = V3();
            for (const std::string& c : s.children) {
                e.kind = RecordKind::ChildStart;
                e.effect = c;
                e.scale = s.child_scale;
                out.push_back(e);
                ++counts_.records;
                ++counts_.children;
            }
            if (!s.hit_effect.empty()) {
                e.kind = RecordKind::HitStart;
                e.effect = s.hit_effect;
                e.scale = s.hit_scale;
                out.push_back(e);
                ++counts_.records;
                ++counts_.hits;
            }
            ++counts_.landed;
            it = flights_.erase(it);
            continue;
        }
        evalPath(s.path, static_cast<float>(el / s.path.duration), pos, tan, &vel);
        r.kind = RecordKind::EffectMove;
        r.time = t;
        r.world = pathFrame(pos, tan);
        r.velocity = vel;
        out.push_back(r);
        ++counts_.records;
        if (models && mode_ == ModelMode::ServerModel) {
            ModelInstance m;
            m.handle = s.handle;
            m.effect = s.effect;
            m.world = r.world;
            m.scale = s.scale;
            models->push_back(m);
        }
        ++it;
    }
}

size_t ProjectileSet::forceEnd(double t, uint64_t owner, std::vector<ProjectileRecord>& out) {
    size_t n = 0;
    for (auto it = flights_.begin(); it != flights_.end();) {
        const ProjectileSpec& s = it->second.spec;
        if (owner != UINT64_MAX && s.owner != owner) {
            ++it;
            continue;
        }
        // a stop when the effect exists: the flight started it, or the trigger route (4793) started it on the bone and
        // the flight holds its handle; a flight that would start its own effect and has not started sent nothing
        if (it->second.started || !s.start_effect) {
            ProjectileRecord r;
            r.kind = RecordKind::EffectStop;
            r.time = t;
            r.handle = s.handle;
            r.owner = s.owner;
            r.effect = s.effect;
            out.push_back(r);
            ++counts_.records;
        }
        ++n;
        ++counts_.removed;
        it = flights_.erase(it);
    }
    return n;
}

void ProjectileSet::clear() {
    flights_.clear();
    next_ = 1;
    counts_ = ProjectileCounts();
}

}}  // namespace c5r::effects
