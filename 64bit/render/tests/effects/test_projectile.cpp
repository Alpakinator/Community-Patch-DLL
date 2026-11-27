// render/tests/effects/test_projectile.cpp - ticket 4792: projectiles timed and moved by the server
// (c5r/effects/projectile.h, projectile_model.h; research/05-graphics-composition/effects-server.md).
// Synthetic: the arc's control points, the three path kinds and their coefficient rules, the flight time, the launch
// (hit, miss, HitRadius, a city, TargetHeight / TargetGround, a flying target, a dropped bomb, fMovementSpeed, the
// tangent turned by the figure), the flights' records (start, moves with a velocity, the end's children and hit effect
// with no stop, the forced end's stop with the impact dropped, owners, handles, the model mode), the weapon rows with
// the reader's rules and pickWeapon. Negative: every refusal with its text and the outputs after it.
// Install part (C5R_GAME_DIR): the install's effect definitions, the archer's arrow resolved to its rigid model and drawn
// through the unit renderer's static path (CPU plan only), and every VisEffect with a model resolved to its parts.
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/effects/projectile.h"
#include "c5r/effects/projectile_model.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/art.h"
#include "c5r/units/draw.h"
#include "testkit.h"

using namespace c5r::effects;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool approxV(const V3& a, const V3& b, float eps = 1e-4f) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}
V3 v(float x, float y, float z) { return V3{x, y, z}; }
V3 sub(const V3& a, const V3& b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
float len(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
V3 unit(const V3& a) {
    const float l = len(a);
    return V3{a.x / l, a.y / l, a.z / l};
}
float distXY(const V3& a, const V3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

ProjectileWeapon arrow() {
    ProjectileWeapon w;
    w.member = "ART_DEF_UNIT_MEMBER_TEST_ARCHER";
    w.miss_slop = 10;
    return w;   // speed NULL -> kNullSpeed
}

FlightPath arcPath(const V3& L, const V3& T, const V3& D, float dur) {
    FlightPath p;
    p.kind = PathKind::Uniform;
    p.duration = dur;
    arcPoints(L, T, D, p.p);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- the path
C5R_TEST(arc_starts_at_launch_ends_at_target_with_the_launch_direction) {
    const V3 L = v(0, 0, 10), T = v(128, 0, 0);
    const V3 D = unit(v(1, 0, 1));
    FlightPath p = arcPath(L, T, D, 1.0f);
    V3 pos, tan, vel;
    CHECK(evalPath(p, 0, pos, tan, &vel));
    CHECK(approxV(pos, L, 1e-3f));
    CHECK(approxV(tan, D, 1e-3f));                 // leaves along the launch direction
    CHECK(evalPath(p, 1, pos, tan, &vel));
    CHECK(approxV(pos, T, 1e-3f));
    // the arc's apex is above the chord at its middle (D points up)
    CHECK(evalPath(p, 0.5f, pos, tan, nullptr));
    CHECK(pos.z > 5.0f);
    // the control points' rule (effects-server.md 1.2): (P0 + P1) / 2 = L, (P2 + P3) / 2 = T, P1 - P0 along D
    const V3 mid01 = v((p.p[0].x + p.p[1].x) / 2, (p.p[0].y + p.p[1].y) / 2, (p.p[0].z + p.p[1].z) / 2);
    const V3 mid23 = v((p.p[2].x + p.p[3].x) / 2, (p.p[2].y + p.p[3].y) / 2, (p.p[2].z + p.p[3].z) / 2);
    CHECK(approxV(mid01, L, 1e-3f) && approxV(mid23, T, 1e-3f));
    CHECK(approxV(unit(sub(p.p[1], p.p[0])), D, 1e-3f));
    // the worked value: w = ((D.V)(D.Z) - (V.Z)(D.D)) / ((D.D) - (D.Z)^2); V = (64, 0, -5); D = (s, 0, s), s = 0.7071
    const float s = std::sqrt(0.5f);
    const float w = ((64 * s - 5 * s) * s - (-5) * 1) / (1 - 0.5f);
    const V3 Q = v(w * s, 0, 10 + w * s);
    CHECK(approxV(p.p[1], v(Q.x / 2, 0, 10 + (Q.z - 10) / 2), 1e-2f));
}

C5R_TEST(arc_with_a_vertical_launch_direction_uses_the_chord_length) {
    V3 out[4];
    arcPoints(v(0, 0, 0), v(64, 0, 0), v(0, 0, 1), out);   // D.Z = 1: denominator 0 -> |V| = 32
    // w = ((D.V)(1) - 0 * 1) / 32 = 0 -> Q = L: P0 = P1 = L
    CHECK(approxV(out[0], v(0, 0, 0)) && approxV(out[1], v(0, 0, 0)));
    FlightPath p;
    p.duration = 1;
    for (int i = 0; i < 4; ++i) p.p[i] = out[i];
    V3 pos, tan;
    CHECK(evalPath(p, 0, pos, tan, nullptr));   // degenerate start: the chord's direction
    CHECK(approxV(tan, v(1, 0, 0), 1e-3f) || len(tan) > 0.99f);
}

C5R_TEST(path_kinds_partition_of_unity_continuity_and_velocity) {
    FlightPath p;
    p.duration = 2.0f;
    p.p[0] = v(0, 0, 0);
    p.p[1] = v(10, 0, 20);
    p.p[2] = v(50, 5, 30);
    p.p[3] = v(80, 0, 0);
    for (PathKind k : {PathKind::Clamped, PathKind::Uniform, PathKind::Straight}) {
        p.kind = k;
        V3 a, ta, b, tb, va;
        // continuity across the joint (u = 1, t = 0.5) and a velocity equal to the finite difference
        CHECK(evalPath(p, 0.5f - 1e-4f, a, ta, nullptr));
        CHECK(evalPath(p, 0.5f + 1e-4f, b, tb, nullptr));
        CHECK(len(sub(a, b)) < 0.05f);
        for (float t : {0.1f, 0.3f, 0.7f, 0.9f}) {
            const float h = 1e-3f;
            CHECK(evalPath(p, t, a, ta, &va));
            CHECK(evalPath(p, t + h, b, tb, nullptr));
            const V3 fd = v((b.x - a.x) / (h * p.duration), (b.y - a.y) / (h * p.duration), (b.z - a.z) / (h * p.duration));
            CHECK(len(sub(fd, va)) < 0.05f * std::max(1.0f, len(va)));
            CHECK(approx(len(ta), 1.0f, 1e-4f));
        }
    }
    V3 pos, tan;
    p.kind = PathKind::Clamped;   // starts at P0, ends at P3
    CHECK(evalPath(p, 0, pos, tan, nullptr) && approxV(pos, p.p[0]));
    CHECK(evalPath(p, 1, pos, tan, nullptr) && approxV(pos, p.p[3]));
    p.kind = PathKind::Uniform;   // starts at (P0+P1)/2, ends at (P2+P3)/2
    CHECK(evalPath(p, 0, pos, tan, nullptr) && approxV(pos, v(5, 0, 10)));
    CHECK(evalPath(p, 1, pos, tan, nullptr) && approxV(pos, v(65, 2.5f, 15)));
    p.kind = PathKind::Straight;  // P0 to P3, linear
    CHECK(evalPath(p, 0.25f, pos, tan, nullptr) && approxV(pos, v(20, 0, 0)) && approxV(tan, v(1, 0, 0)));
    // t outside [0, 1] is clamped
    CHECK(evalPath(p, 2.0f, pos, tan, nullptr) && approxV(pos, p.p[3]));
}

C5R_TEST(path_refusals) {
    FlightPath p;
    p.p[3] = v(10, 0, 0);
    V3 pos{7, 7, 7}, tan;
    p.duration = 0;
    CHECK(!evalPath(p, 0.5f, pos, tan, nullptr));
    p.duration = -1;
    CHECK(!evalPath(p, 0.5f, pos, tan, nullptr));
    p.duration = std::nanf("");
    CHECK(!evalPath(p, 0.5f, pos, tan, nullptr));
    p.duration = 1;
    p.p[1].y = std::numeric_limits<float>::infinity();
    CHECK(!evalPath(p, 0.5f, pos, tan, nullptr));
    p.p[1].y = 0;
    CHECK(!evalPath(p, std::nanf(""), pos, tan, nullptr));
    CHECK(evalPath(p, 0.5f, pos, tan, nullptr));
}

C5R_TEST(frame_faces_the_flight) {
    const V3 t = unit(v(1, 1, 0.5f));
    M4 m = pathFrame(v(3, 4, 5), t);
    CHECK(approxV(v(m.m[4], m.m[5], m.m[6]), t));                 // row 1 = the tangent
    CHECK(approxV(m.translation(), v(3, 4, 5)));
    const V3 x = v(m.m[0], m.m[1], m.m[2]), z = v(m.m[8], m.m[9], m.m[10]);
    CHECK(approx(len(x), 1) && approx(len(z), 1));
    CHECK(approx(x.x * t.x + x.y * t.y + x.z * t.z, 0) && approx(z.x * t.x + z.y * t.y + z.z * t.z, 0));
    CHECK(approx(x.z, 0));                                          // the side axis stays horizontal
    CHECK(z.z > 0);                                               // the third axis points up
    M4 up = pathFrame(v(0, 0, 0), v(0, 0, 1));                    // vertical: +x as the side
    CHECK(approxV(v(up.m[0], up.m[1], up.m[2]), v(1, 0, 0)));
}

C5R_TEST(frame_rotation_as_quaternion) {
    M4 rz;   // +90 degrees about z in the row-vector convention
    rz.m[0] = 0; rz.m[1] = 1; rz.m[4] = -1; rz.m[5] = 0;
    float q[4];
    rotationQuaternion(rz, q);
    const float h = std::sqrt(0.5f);
    CHECK(approx(q[0], 0) && approx(q[1], 0) && approx(q[2], h) && approx(q[3], h));
    rotationQuaternion(M4(), q);
    CHECK(approx(q[0], 0) && approx(q[1], 0) && approx(q[2], 0) && approx(q[3], 1));
    // round trip over flight frames in every octant: the quaternion's matrix (D3DX row-vector form) equals the frame
    for (int i = 0; i < 64; ++i) {
        const V3 t = unit(v(std::cos(i * 0.7f) * (i % 3 + 1), std::sin(i * 1.3f) - 0.2f, std::cos(i * 2.1f)));
        const M4 f = pathFrame(v(0, 0, 0), t);
        rotationQuaternion(f, q);
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
        bool same = true;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) same = same && approx(r[a * 3 + b], f.m[a * 4 + b], 1e-4f);
        CHECK(same);
    }
}

C5R_TEST(flight_time_rule) {
    CHECK(approx(flightTime(64, 1), 1.0f));
    CHECK(approx(flightTime(128, 2), 1.0f));
    CHECK(approx(flightTime(0, 1), kMinFlight));
    CHECK(approx(flightTime(64 * 100, 1), kMaxFlight));
    CHECK(approx(flightTime(64, 0), kMaxFlight));                   // speed floored at 0.001
    CHECK(approx(flightTime(64, -3), kMaxFlight));
    CHECK(approx(flightTime(std::nanf(""), 1), kMinFlight));
    CHECK(approx(flightTime(64, std::nanf("")), kMaxFlight));
}

// ---------------------------------------------------------------------------------------------- the launch
C5R_TEST(launch_hit_lands_within_target_radius_at_target_height) {
    ProjectileWeapon w = arrow();
    LaunchRequest r;
    r.weapon = &w;
    r.launch = v(0, 0, 10);
    r.tangent_local = v(0, -1, 0.3f);
    r.target_pos = v(0, -128, 0);
    r.target_radius = 3;
    r.target_height = 8;
    FlightRandom rng;
    auto ground = [](float x, float y) { return 0.01f * x + 0.02f * y + 1.0f; };
    for (int i = 0; i < 200; ++i) {
        LaunchPlan lp;
        std::string why;
        CHECK(planLaunch(r, rng, ground, lp, &why));
        CHECK(distXY(lp.target, r.target_pos) <= 3.0f + 1e-3f);
        CHECK(approx(lp.target.z, ground(lp.target.x, lp.target.y) + 8, 1e-3f));
        CHECK(approx(lp.speed, kNullSpeed) && approx(lp.radius, 3));
        CHECK(lp.path.kind == PathKind::Uniform);
        CHECK(approx(lp.path.duration, flightTime(len(sub(lp.target, r.launch)), kNullSpeed), 1e-5f));
        V3 pos, tan;
        CHECK(evalPath(lp.path, 1, pos, tan, nullptr) && approxV(pos, lp.target, 1e-2f));
    }
    // a miss lands within MissTargetSlopRadius + TargetRadius, and some land beyond TargetRadius
    r.miss = true;
    int beyond = 0;
    for (int i = 0; i < 200; ++i) {
        LaunchPlan lp;
        CHECK(planLaunch(r, rng, ground, lp, nullptr));
        const float d = distXY(lp.target, r.target_pos);
        CHECK(d <= 13.0f + 1e-3f);
        if (d > 3.0f) ++beyond;
        CHECK(approx(lp.radius, 13));
    }
    CHECK(beyond > 100);
    // HitRadius overrides both
    w.hit_radius = 1.5f;
    LaunchPlan lp;
    CHECK(planLaunch(r, rng, ground, lp, nullptr) && approx(lp.radius, 1.5f) && distXY(lp.target, r.target_pos) <= 1.5f + 1e-3f);
    // TargetGround: no TargetHeight
    w.target_ground = true;
    CHECK(planLaunch(r, rng, ground, lp, nullptr));
    CHECK(approx(lp.target.z, ground(lp.target.x, lp.target.y), 1e-3f));
    // no height function: z = 0 (+ TargetHeight)
    w.target_ground = false;
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr) && approx(lp.target.z, 8));
}

C5R_TEST(launch_speed_sources_and_the_tangent_turned_by_the_figure) {
    ProjectileWeapon w = arrow();
    w.has_speed = true;
    w.speed = 4;
    LaunchRequest r;
    r.weapon = &w;
    r.launch = v(0, 0, 0);
    r.tangent_local = v(0, -1, 0);
    r.target_pos = v(256, 0, 0);
    r.target_radius = 0;
    FlightRandom rng;
    LaunchPlan lp;
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr));
    CHECK(approx(lp.speed, 4) && approx(lp.path.duration, 1.0f));     // 256 / 64 / 4
    r.effect_movement_speed = 2;                                    // fMovementSpeed wins
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr) && approx(lp.speed, 2) && approx(lp.path.duration, 2.0f));
    r.effect_movement_speed = 0;
    w.speed = 0;                                                    // floored at 0.001: the 5 s clamp
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr) && approx(lp.speed, kMinSpeed) && approx(lp.path.duration, kMaxFlight));
    // the figure turned 90 degrees about z: local -y becomes world +x
    w.speed = 4;
    M4 rot;
    rot.m[0] = 0; rot.m[1] = 1; rot.m[4] = -1; rot.m[5] = 0;        // row-vector rotation by +90 degrees
    r.figure_world = rot;
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr));
    V3 pos, tan;
    CHECK(evalPath(lp.path, 0, pos, tan, nullptr) && approxV(tan, v(1, 0, 0), 1e-3f));
}

C5R_TEST(launch_flying_target_city_and_dropped_bomb) {
    ProjectileWeapon w = arrow();
    LaunchRequest r;
    r.weapon = &w;
    r.launch = v(0, 0, 0);
    r.target = TargetKind::FlyingFigure;
    r.target_pos = v(64, 0, 90);
    r.target_move_rate = 3.0f;
    r.target_height = 0;
    FlightRandom rng;
    LaunchPlan lp;
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr));
    CHECK(lp.path.kind == PathKind::Straight && approx(lp.radius, 0) && approxV(lp.target, r.target_pos));
    CHECK(approx(lp.speed, 3.3f, 1e-4f));                           // at least MoveRate x 1.1
    // a city (no target figure): MissTargetSlopRadius + 5, no TargetHeight
    r.target = TargetKind::Participant;
    r.target_pos = v(0, 128, 0);
    r.target_height = 30;
    CHECK(planLaunch(r, rng, HeightFn(), lp, nullptr));
    CHECK(approx(lp.radius, 15) && distXY(lp.target, r.target_pos) <= 15 + 1e-3f && approx(lp.target.z, 0));
    // a dropped bomb: the lead along the heading, at most 16
    w.dropped = true;
    w.target_ground = true;
    w.has_speed = true;
    w.speed = 0.9f;
    r.target = TargetKind::Figure;
    r.attacker_pos = v(100, 200, 40);
    r.water_level = 4;
    r.attacker_heading = 0;                                       // (sin 0, -cos 0) = (0, -1)
    r.attacker_move_rate = 0.2f;
    auto ground = [](float, float) { return 2.0f; };
    CHECK(planLaunch(r, rng, ground, lp, nullptr));
    CHECK(lp.dropped && approxV(lp.target, v(100, 200 - 36 * 0.2f / 0.9f, 2), 1e-3f));
    r.attacker_move_rate = 5;                                     // lead 200 -> 16
    CHECK(planLaunch(r, rng, ground, lp, nullptr) && approxV(lp.target, v(100, 184, 2), 1e-3f));
}

C5R_TEST(launch_refusals) {
    LaunchRequest r;
    FlightRandom rng;
    LaunchPlan lp;
    lp.speed = 99;
    std::string why;
    CHECK(!planLaunch(r, rng, HeightFn(), lp, &why) && why == "no weapon" && lp.speed == 0);
    ProjectileWeapon w = arrow();
    r.weapon = &w;
    r.launch.x = std::nanf("");
    CHECK(!planLaunch(r, rng, HeightFn(), lp, &why) && why.find("not finite") != std::string::npos);
    r.launch.x = 0;
    r.figure_world.m[5] = std::numeric_limits<float>::infinity();
    CHECK(!planLaunch(r, rng, HeightFn(), lp, &why) && why.find("matrix") != std::string::npos);
    r.figure_world = M4();
    r.target_radius = std::nanf("");
    CHECK(!planLaunch(r, rng, HeightFn(), lp, &why) && why.find("number") != std::string::npos);
    r.target_radius = 3;
    auto bad = [](float, float) { return std::nanf(""); };
    CHECK(!planLaunch(r, rng, bad, lp, &why) && why.find("target point") != std::string::npos);
    CHECK(planLaunch(r, rng, HeightFn(), lp, &why));
}

// ---------------------------------------------------------------------------------------------- the flights
namespace {
ProjectileSpec spec(uint64_t owner, double t0, float dur) {
    ProjectileSpec s;
    s.owner = owner;
    s.effect = "ART_DEF_VEFFECT_TEST_PROJ";
    s.children = {"ART_DEF_VEFFECT_TEST_IMPACT"};
    s.child_scale = 0.14f;
    s.hit_effect = "ART_DEF_VEFFECT_TEST_HIT_DIRT";
    s.hit_scale = 2;
    s.path = arcPath(v(0, 0, 10), v(128, 0, 0), unit(v(1, 0, 1)), dur);
    s.t0 = t0;
    return s;
}
size_t countKind(const std::vector<ProjectileRecord>& r, RecordKind k) {
    size_t n = 0;
    for (auto& x : r) n += x.kind == k;
    return n;
}
}  // namespace

C5R_TEST(flight_records_start_moves_and_end_without_a_stop) {
    ProjectileSet set;
    uint32_t h = 0;
    std::string why;
    CHECK(set.add(spec(77, 1.0, 0.5f), h, &why) && h == 1);
    std::vector<ProjectileRecord> out;
    std::vector<ModelInstance> models;
    set.frame(0.5, out, &models);                                  // before t0: nothing
    CHECK(out.empty() && set.live() == 1);
    set.frame(1.0, out, &models);                                  // the start and the first move
    CHECK(out.size() == 2 && out[0].kind == RecordKind::EffectStart && out[1].kind == RecordKind::EffectMove);
    CHECK(out[0].handle == 1 && out[0].owner == 77 && out[0].effect == "ART_DEF_VEFFECT_TEST_PROJ");
    CHECK(approxV(out[0].world.translation(), v(0, 0, 10), 1e-3f));
    CHECK(models.empty());                                         // EffectOnly: no server model (lead #7986, B)
    out.clear();
    V3 last = v(0, 0, 10);
    for (double t = 1.05; t <= 1.5; t += 0.05) {
        set.frame(t, out, &models);
        CHECK(!out.empty() && out.back().kind == RecordKind::EffectMove);
        const V3 p = out.back().world.translation();
        CHECK(p.x > last.x);                                       // moves forward
        CHECK(len(out.back().velocity) > 100.0f);                  // world units per second (about 128 / 0.5)
        last = p;
        out.clear();
    }
    set.frame(1.6, out, &models);                                  // over: the child and the hit effect, no stop
    CHECK(countKind(out, RecordKind::ChildStart) == 1 && countKind(out, RecordKind::HitStart) == 1);
    CHECK(countKind(out, RecordKind::EffectStop) == 0 && countKind(out, RecordKind::EffectMove) == 0);
    for (auto& r : out) {
        CHECK(approxV(r.world.translation(), v(128, 0, 0), 1e-2f));
        CHECK(approx(static_cast<float>(r.time), 1.5f, 1e-5f));
    }
    CHECK(out[0].effect == "ART_DEF_VEFFECT_TEST_IMPACT" && approx(out[0].scale, 0.14f));
    CHECK(out[1].effect == "ART_DEF_VEFFECT_TEST_HIT_DIRT" && approx(out[1].scale, 2));
    CHECK(set.live() == 0 && set.counts().landed == 1 && set.counts().children == 1 && set.counts().hits == 1);
    out.clear();
    set.frame(2.0, out, &models);
    CHECK(out.empty());
}

C5R_TEST(forced_end_stops_the_trail_and_drops_the_impact) {
    ProjectileSet set;
    uint32_t a = 0, b = 0, c = 0;
    CHECK(set.add(spec(1, 0.0, 2.0f), a, nullptr));
    CHECK(set.add(spec(2, 0.0, 2.0f), b, nullptr));
    CHECK(set.add(spec(1, 5.0, 1.0f), c, nullptr));                // not started at the forced end: nothing sent
    std::vector<ProjectileRecord> out;
    set.frame(1.0, out, nullptr);
    out.clear();
    CHECK(set.forceEnd(1.0, 1, out) == 2);
    CHECK(out.size() == 1 && out[0].kind == RecordKind::EffectStop && out[0].handle == a);
    CHECK(set.live() == 1);
    out.clear();
    set.frame(10.0, out, nullptr);                                 // fight 2's flight lands; fight 1's never does
    CHECK(countKind(out, RecordKind::HitStart) == 1 && out.back().owner == 2);
    out.clear();
    // owner UINT64_MAX: every flight; a flight on the trigger route's effect (start_effect false) gets a stop even
    // before its start, since that effect is already on the bone
    ProjectileSpec s = spec(3, 9.0, 1.0f);
    s.start_effect = false;
    uint32_t d = 0;
    CHECK(set.add(s, d, nullptr));
    CHECK(set.forceEnd(1.0, UINT64_MAX, out) == 1 && out.size() == 1 && out[0].kind == RecordKind::EffectStop);
    CHECK(set.counts().removed == 3);
}

C5R_TEST(trigger_route_flight_sends_no_start_and_model_mode) {
    ProjectileSet set;
    set.setModelMode(ModelMode::ServerModel);
    ProjectileSpec s = spec(5, 0.0, 1.0f);
    s.start_effect = false;
    s.handle = 4792;
    uint32_t h = 0;
    CHECK(set.add(s, h, nullptr) && h == 4792);
    std::vector<ProjectileRecord> out;
    std::vector<ModelInstance> models;
    set.frame(0.0, out, &models);
    CHECK(out.size() == 1 && out[0].kind == RecordKind::EffectMove);
    CHECK(models.size() == 1 && models[0].handle == 4792 && models[0].effect == s.effect);
    CHECK(approxV(models[0].world.translation(), out[0].world.translation()));
    // a handle in use is refused, the set unchanged
    std::string why;
    uint32_t h2 = 99;
    CHECK(!set.add(s, h2, &why) && h2 == 0 && why.find("in use") != std::string::npos && set.live() == 1);
    // the automatic handles skip one in use
    ProjectileSpec t = spec(5, 0.0, 1.0f);
    uint32_t h3 = 0;
    CHECK(set.add(t, h3, nullptr) && h3 == 1);
    set.clear();
    CHECK(set.live() == 0 && set.counts().launched == 0);
}

C5R_TEST(flight_refusals) {
    ProjectileSet set;
    uint32_t h = 7;
    std::string why;
    ProjectileSpec s = spec(1, 0.0, 1.0f);
    s.effect.clear();
    CHECK(!set.add(s, h, &why) && h == 0 && why == "no effect name");
    s = spec(1, 0.0, 0.0f);
    CHECK(!set.add(s, h, &why) && why.find("path") != std::string::npos);
    s = spec(1, std::nan(""), 1.0f);
    CHECK(!set.add(s, h, &why) && why.find("start time") != std::string::npos);
    s = spec(1, 0.0, 1.0f);
    s.scale = std::nanf("");
    CHECK(!set.add(s, h, &why) && why.find("scale") != std::string::npos);
    CHECK(set.live() == 0 && set.counts().launched == 0);
    std::vector<ProjectileRecord> out;
    set.frame(std::nan(""), out, nullptr);
    CHECK(out.empty());
}

// ---------------------------------------------------------------------------------------------- weapon rows
C5R_TEST(weapon_rows_with_the_reader_rules_and_pick) {
    c5m::TsvTables db;
    c5m::Error e;
    const std::string tsv =
        "UnitMemberType\tIndex\tSubIndex\tID\tProjectileSpeed\tProjectileTurnRateMin\tProjectileTurnRateMax\tHitEffect\t"
        "HitEffectScale\tHitRadius\tProjectileChildEffectScale\tAreaDamageDelay\tContinuousFire\tWaitForEffectCompletion\t"
        "TargetGround\tIsDropped\tWeaponTypeTag\tMissTargetSlopRadius\n"
        "M_ARCHER\t0\t0\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\tARROW\t10\n"
        "M_ARCHER\t1\t0\t\\N\t-1\t-1\t-1\t\\N\t0\t-2\t0\t-5\t0\t0\t0\t0\tFLAMING_ARROW\t-3\n"
        "M_MECH\t0\t1\tART_DEF_VEFFECT_MECH_ROCKET_PROJECTILE\t5.3\t1\t2\tART_DEF_VEFFECT_GUIDED_MISSILE_IMPACT_$(TERRAIN)\t"
        "1.5\t4\t0.5\t0.5\t1\t1\t0\t0\tEXPLOSIVE\t\\N\n"
        "M_MECH\t0\t0\tPROJECTILE\t3\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t1\t1\tEXPLOSIVE\t\\N\n"
        "\\N\t0\t0\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\t\\N\n";
    CHECK(db.add("ArtDefine_UnitMemberCombatWeapons", tsv, &e));
    ProjectileWeaponTable t;
    std::string err;
    size_t skipped = 0;
    CHECK(loadProjectileWeapons(db, t, err, &skipped) && err.empty());
    CHECK(skipped == 1 && t.size() == 2);
    const auto& a = t["M_ARCHER"];
    CHECK(a.size() == 2);
    CHECK(!a[0].has_speed && approx(a[0].speed, kNullSpeed) && approx(a[0].miss_slop, 10) && a[0].weapon_tag == "ARROW");
    CHECK(!a[0].has_child_scale && approx(a[0].child_scale, 1.0f));
    CHECK(a[1].has_speed && approx(a[1].speed, 2.0f));               // below 0 -> 2.0
    CHECK(approx(a[1].turn_min, 3) && approx(a[1].turn_max, 5));       // below 0 -> 3 / 5
    CHECK(approx(a[1].hit_radius, 0) && approx(a[1].miss_slop, 0));    // below 0 -> 0
    CHECK(approx(a[1].child_scale, kChildScaleFloor));               // 0 -> 0.14
    CHECK(approx(a[1].hit_effect_scale, 1.0f) && !a[1].has_hit_effect_scale);
    // the hit's scale in force (effects-formats.md 15.7.8): unset (0 here, NULL in a[0]) -> the firing member's scale,
    // never 1.0; a written value above 0 -> that value; no member scale either -> 1.0
    CHECK(approx(hitScaleOf(a[1], 0.12f), 0.12f) && approx(hitScaleOf(a[0], 0.14f), 0.14f));
    CHECK(approx(hitScaleOf(a[1], 0.0f), 1.0f) && approx(hitScaleOf(a[1], NAN), 1.0f));
    {
        ProjectileWeapon set = a[1];
        set.hit_effect_scale = 0.4f;
        set.has_hit_effect_scale = true;
        CHECK(approx(hitScaleOf(set, 0.12f), 0.4f));
    }
    CHECK(approx(a[1].area_damage_delay, -1.0f));
    const auto& m = t["M_MECH"];
    CHECK(m.size() == 2 && m[0].sub_index == 0 && m[1].sub_index == 1);   // sorted by (Index, SubIndex)
    CHECK(m[1].continuous_fire && m[1].wait_for_effect && !m[1].dropped && approx(m[1].hit_radius, 4));
    CHECK(m[0].dropped && m[0].target_ground);
    // pick: the ID equal to the effect, else "PROJECTILE", else the first of the index
    CHECK(pickWeapon(m, "ART_DEF_VEFFECT_MECH_ROCKET_PROJECTILE", 0) == &m[1]);
    CHECK(pickWeapon(m, "ART_DEF_VEFFECT_OTHER", 0) == &m[0]);
    CHECK(pickWeapon(a, "ART_DEF_VEFFECT_PROJ_ARROW_ARCHER", 1) == &a[1]);
    CHECK(pickWeapon(a, "", 0) == &a[0]);
    CHECK(pickWeapon(a, "x", 5) == nullptr);
    CHECK(pickWeapon(std::vector<ProjectileWeapon>(), "x", 0) == nullptr);
}

C5R_TEST(weapon_rows_refusals) {
    c5m::TsvTables db;
    ProjectileWeaponTable t;
    t["stale"].push_back(ProjectileWeapon());
    std::string err;
    CHECK(!loadProjectileWeapons(db, t, err) && t.empty() && err.find("ArtDefine_UnitMemberCombatWeapons") == 0);
    c5m::Error e;
    CHECK(db.add("ArtDefine_UnitMemberCombatWeapons", "UnitMemberType\tIndex\nM\t0\n", &e));   // columns missing
    CHECK(!loadProjectileWeapons(db, t, err) && t.empty() && !err.empty());
}

C5R_TEST(effect_local_transform) {
    c5m::VisEffectDef d;
    M4 m = effectLocal(d);
    CHECK(approx(m.m[0], 1) && approx(m.m[5], 1) && approx(m.m[10], 1) && approx(m.m[12], 0));
    d.has_transform = true;
    d.scale = 2;
    d.translation = c5m::FxVec3{1, 2, 3};
    d.rotation_deg = c5m::FxVec3{0, 0, 90};
    m = effectLocal(d);
    const V3 p = transformPoint(v(1, 0, 0), m);                    // scaled by 2, turned to +y, moved
    CHECK(approxV(p, v(1, 4, 3), 1e-4f));
}

C5R_TEST(model_draws_skip_unknown_effects) {
    // no art for the effect: skipped and counted, the plan unchanged (the cache is not asked)
    class NoFiles : public c5m::FileProvider {
    public:
        bool read(const std::string&, c5m::Bytes&, std::string*) override { return false; }
    } nofiles;
    c5r::units::ModelCache cache(nofiles);
    std::map<std::string, ProjectileModelArt> arts;
    std::vector<ModelInstance> inst(2);
    inst[0].effect = "A";
    inst[1].effect = "B";
    c5r::units::FramePlan plan;
    size_t meshes = 0, skipped = 0;
    CHECK(appendProjectileDraws(cache, arts, inst, plan, &meshes, &skipped).ok());
    CHECK(skipped == 2 && meshes == 0 && plan.units.empty());
    // an art whose model file is missing: the cache's failure comes back
    ProjectileModelArt pa;
    pa.effect = "A";
    c5r::units::MemberArt ma;
    ma.member = "M";
    ma.scale = 1;
    ma.files.mesh.name = "missing.gr2";
    pa.parts.push_back(ma);
    arts["A"] = pa;
    c5r::units::UnitStatus st = appendProjectileDraws(cache, arts, inst, plan, &meshes, &skipped);
    CHECK(!st.ok() && st.code == c5r::units::UnitCode::ModelNotFound);
}

// ---------------------------------------------------------------------------------------------- install
C5R_TEST(install_arrow_model_and_every_weapon_effect) {
    const std::string dir = c5r::test::gameDir();
    c5r::gamefs::GameFiles gf;
    CHECK_OK(gf.open(c5r::gamefs::fromUtf8(dir)));
    c5r::units::GameFilesProvider files(gf);
    c5m::EffectLibrary lib;
    std::string err;
    CHECK(loadInstallEffects(gf, files, lib, err));
    if (!err.empty()) std::printf("effects: %s\n", err.c_str());
    std::printf("effects: %zu VisEffects, %zu files loaded, %zu missing\n", lib.distinct(c5m::FxKind::VisEffect),
                lib.loaded_files().size(), lib.missing_files().size());
    CHECK(lib.distinct(c5m::FxKind::VisEffect) == 538);           // effects-formats.md 14.1
    // the archer's arrow: one rigid model, drawn through the static path
    ProjectileModelArt art;
    std::string why;
    CHECK(resolveProjectileModel(lib, files, "ART_DEF_VEFFECT_PROJ_ARROW_ARCHER", art, &why));
    if (!why.empty()) std::printf("arrow: %s\n", why.c_str());
    CHECK(art.parts.size() == 1);
    c5r::units::ModelCache cache(files);
    c5m::StringDatabase strings;   // unit .gr2 files keep their names in the install's string databases
    CHECK(c5r::units::loadStringDatabases(gf.names(), files, strings).ok());
    cache.setStrings(&strings);
    std::map<std::string, ProjectileModelArt> arts;
    arts[art.effect] = art;
    std::vector<ModelInstance> inst(1);
    inst[0].effect = art.effect;
    inst[0].world = pathFrame(v(10, 20, 30), v(0, 1, 0));
    c5r::units::FramePlan plan;
    size_t meshes = 0, skipped = 0;
    c5r::units::UnitStatus st = appendProjectileDraws(cache, arts, inst, plan, &meshes, &skipped);
    CHECK(st.ok());
    if (!st.ok()) std::printf("arrow draw: %s\n", st.str().c_str());
    CHECK(plan.units.size() == 1 && skipped == 0 && meshes == 0);
    size_t verts = 0;
    if (!plan.units.empty())
        for (auto& d : plan.units[0].draws) {
            CHECK(d.material == c5r::units::Material::Static && d.cb0.size() == 16);
            verts += d.mesh->vertex_count;
            // the translation column: the effect's own Transform turned by the flight's frame, then the frame's point
            const V3 o = transformPoint(v(0, 0, 0), mul(art.local, inst[0].world));
            CHECK(approx(d.cb0[0].w, o.x, 1e-3f) && approx(d.cb0[1].w, o.y, 1e-3f) && approx(d.cb0[2].w, o.z, 1e-3f));
            CHECK(len(sub(o, v(10, 20, 30))) < 64.0f);
        }
    std::printf("arrow: %zu draws, %zu vertices\n", plan.units.empty() ? size_t(0) : plan.units[0].draws.size(), verts);
    CHECK(verts == 48);                                            // effects-formats.md 1: 48 vertices
    // a negative against the install: an unknown effect and an effect without a model
    CHECK(!resolveProjectileModel(lib, files, "ART_DEF_VEFFECT_NO_SUCH", art, &why) && why.find("NO_SUCH") != std::string::npos);
    std::string particlesOnly;
    for (const std::string& n : lib.names(c5m::FxKind::VisEffect)) {
        const c5m::VisEffectDef* d = lib.vis_effect(n);
        if (d && d->models.empty() && n.find("$(") == std::string::npos) {
            particlesOnly = n;
            break;
        }
    }
    CHECK(!particlesOnly.empty());
    CHECK(!resolveProjectileModel(lib, files, particlesOnly, art, &why) && why.find("no model") != std::string::npos);
    // every VisEffect with a model that a projectile could name resolves to its parts (rigid or not)
    size_t withModel = 0, resolved = 0;
    for (const std::string& n : lib.names(c5m::FxKind::VisEffect)) {
        const c5m::VisEffectDef* d = lib.vis_effect(n);
        if (!d || d->models.empty()) continue;
        ++withModel;
        ProjectileModelArt a;
        if (resolveProjectileModel(lib, files, n, a, &why)) ++resolved;
        else std::printf("not resolved: %s\n", why.c_str());
    }
    std::printf("VisEffects with a model: %zu, resolved %zu\n", withModel, resolved);
    CHECK(withModel == 101 && resolved == withModel);              // effects-formats.md 0 item 1
}

C5R_TEST_MAIN
