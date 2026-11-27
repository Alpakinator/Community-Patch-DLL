// c5r/effects/projectile.h - projectiles timed and moved by the server (epic 0042, ticket 4792; route S, D210).
// Own code, GPL-3.0-or-later. Written from the knowledge base only (research/05-graphics-composition/effects-server.md
// section 1, the EXE's flight described in words; effects-formats.md 6-8.1): no decompiler output, nothing of the game's
// files (D83). The database's weapon rows and the effect definitions are read from the player's install at run time.
//
// What a projectile is (effects-server.md 1): when a firing figure's Transfer trigger fires, the effect that the trigger's
// `refid` Effect row started on a bone (an arrow, a shell, a rock, a torch) is handed to a flight: a target point chosen
// by the weapon row (hit or miss, HitRadius / MissTargetSlopRadius / TargetRadius, TargetHeight, TargetGround, a dropped
// bomb's lead), a speed (the effect's fMovementSpeed, else ProjectileSpeed, plots per second), a flight time (distance /
// 64 / speed, 0.001-5 s), and a path of four control points (an arc, a quadratic B-spline, or straight at a flying
// target). Each frame the effect is moved to the path's point, facing along the path; at the end its ProjectileEnd
// children and the weapon's HitEffect start at the end point.
//
// This header gives: the weapon rows (loadProjectileWeapons, pickWeapon), the flight's mathematics (arcPoints, evalPath,
// pathFrame, flightTime), the launch (planLaunch) and a set of flights that produces, frame by frame, the effect records
// (start, move with a velocity, stop, child and hit starts) and, behind a flag, the model instances the server would draw
// itself (lead-0042 #7986: product default B = the server starts and moves the VisEffect and draws no model; A = the
// server also draws the rigid model, off by default). The records are this header's own small form until 4722's
// emitter (W16) takes them; there is no second wire form here. Nothing throws.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace c5m { class TableSource; }

namespace c5r { namespace effects {

// ---------------------------------------------------------------------------------------------- vectors
struct V3 {
    float x = 0, y = 0, z = 0;
};
// A 4x4 matrix in the EXE's (D3DX) convention: row vectors, rows 0-2 the axes, row 3 the translation (m[12..14]).
struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    V3 translation() const { return V3{m[12], m[13], m[14]}; }
};
M4 mul(const M4& a, const M4& b);               // a x b (row vectors: a first, then b)
V3 transformNormal(const V3& v, const M4& m);   // v x the upper 3x3
V3 transformPoint(const V3& v, const M4& m);    // v x m with w = 1
// The rotation of m's upper 3x3 (orthonormal rows) as a unit quaternion (x, y, z, w), D3DXQuaternionRotationMatrix's
// convention: the form 4722's emitter takes (StartSpec::orient, W16 #8010).
void rotationQuaternion(const M4& m, float q[4]);

// ---------------------------------------------------------------------------------------------- constants
// effects-server.md 1.1 / 1.2 [C unless marked]
constexpr float kPlotUnits = 64.0f;            // world units per plot (speeds are plots per second)
constexpr float kMinSpeed = 0.001f;            // speed floor
constexpr float kMinFlight = 0.001f;           // flight time clamp
constexpr float kMaxFlight = 5.0f;
constexpr float kDefaultTargetRadius = 3.0f;   // the member combat record's TargetRadius default
constexpr float kNoFigureRadius = 5.0f;        // the radius added to MissTargetSlopRadius without a target figure
constexpr float kDropLeadMax = 16.0f;          // a dropped bomb's lead, world units, at most
constexpr float kAirSpeedFactor = 1.1f;        // against a flying target the speed is at least its MoveRate x this
constexpr float kNullSpeed = 2.0f;             // ProjectileSpeed NULL: 2.0 plots per second [L, effects-server.md 2.4:
                                               // 12 archer arrows and 11 thrown torches of stock, 2026-11-20]
constexpr float kChildScaleFloor = 0.14f;      // ProjectileChildEffectScale 0 or below
constexpr uint32_t kProjectileMarkerHash = 0xC89BA0ADu;   // the CRC of "PROJECTILE" (the marker ID; effects-server 1.1)

// ---------------------------------------------------------------------------------------------- weapon rows
// One ArtDefine_UnitMemberCombatWeapons row with the EXE reader's rules (effects-server.md 1.1 item 1). `has_*` false =
// the column was NULL (the record's constructor value applies; not known, see kNullSpeed).
struct ProjectileWeapon {
    std::string member;          // UnitMemberType
    int index = 0, sub_index = 0;
    std::string id;              // ID: a VisEffect name, the marker "PROJECTILE", or ""
    float speed = kNullSpeed;    // ProjectileSpeed (below 0: 2.0)
    bool has_speed = false;
    float turn_min = 3.0f, turn_max = 5.0f;   // ProjectileTurnRateMin / Max (below 0: 3.0 / 5.0; not used by the path)
    std::string hit_effect;      // HitEffect (may carry $(TERRAIN))
    float hit_effect_scale = 1.0f;   // HitEffectScale; meaningful only when has_hit_effect_scale (else: hitScaleOf)
    bool has_hit_effect_scale = false;   // false: NULL, 0 or below -> the firing member's own scale (hitScaleOf)
    float hit_radius = 0;        // HitRadius (below 0: 0)
    float miss_slop = 0;         // MissTargetSlopRadius (below 0: 0)
    float child_scale = 1.0f;    // ProjectileChildEffectScale (0 or below: 0.14; NULL: 1.0 [I])
    bool has_child_scale = false;
    float area_damage_delay = -1.0f;   // AreaDamageDelay (below 0: -1)
    bool continuous_fire = false, wait_for_effect = false, target_ground = false, dropped = false;
    std::string weapon_tag;      // WeaponTypeTag (fills $(WEAPON) for sounds and deaths, not used here)
};
using ProjectileWeaponTable = std::map<std::string, std::vector<ProjectileWeapon>>;   // by member, (Index, SubIndex)
// Reads the table. False with `err` naming the table or column when one is missing; a row without a member or an
// Index is skipped and counted in `skipped` when given.
bool loadProjectileWeapons(c5m::TableSource& db, ProjectileWeaponTable& out, std::string& err, size_t* skipped = nullptr);
// The weapon a Transfer of `effect` fires (effects-server.md 1.1 item 1): the row whose ID is the effect's name; else the
// row whose ID is "PROJECTILE"; else the first row of Index `index`; else null.
const ProjectileWeapon* pickWeapon(const std::vector<ProjectileWeapon>& rows, const std::string& effect, int index);
// The scale of a weapon's HitEffect: its HitEffectScale when written and above 0, else the firing member's own scale
// (`member_scale`, ArtDefine_UnitMemberInfos Scale), as the EXE's reader takes it (effects-server.md 1.1 item 1: the
// member's value at + 0x140). Never 1.0 for "unset": the EXE draws a hit at scale 1.0 about ten times stock's size
// (effects-formats.md 15.7.8, live r5/r6, ticket 4727). A member scale that is 0, negative or not finite gives 1.0.
inline float hitScaleOf(const ProjectileWeapon& w, float member_scale) {
    if (w.has_hit_effect_scale && w.hit_effect_scale > 0 && w.hit_effect_scale < 1e6f) return w.hit_effect_scale;
    return member_scale > 0 && member_scale < 1e6f ? member_scale : 1.0f;
}

// ---------------------------------------------------------------------------------------------- the path
enum class PathKind : uint8_t {
    Clamped = 0,    // clamped quadratic B-spline over P0..P3 (the EXE has it; the launcher does not use it)
    Uniform = 1,    // uniform quadratic B-spline: starts at (P0+P1)/2, ends at (P2+P3)/2 (the launcher's arc)
    Straight = 2,   // P0 to P3 (against a flying target)
};
const char* pathKindName(PathKind k);
struct FlightPath {
    PathKind kind = PathKind::Uniform;
    V3 p[4];
    float duration = 0;   // seconds, > 0
};
// The arc's control points (effects-server.md 1.2): launch L, target T, launch direction D (unit length).
void arcPoints(const V3& L, const V3& T, const V3& D, V3 out[4]);
// Position, unit tangent and velocity (world units per second) at t in [0, 1] (clamped). False for a path whose
// duration is not positive or finite, or whose points are not finite.
bool evalPath(const FlightPath& path, float t, V3& pos, V3& tangent, V3* velocity = nullptr);
// The frame of the moving effect: row 1 = the tangent (the model's +y faces the flight), row 0 = tangent x up
// normalised (or +x when the tangent is vertical), row 2 = row 0 x row 1, row 3 = the position [C for row 1; I for the
// others, effects-server.md 1.4].
M4 pathFrame(const V3& pos, const V3& tangent);
// distance / 64 / speed, clamped to [kMinFlight, kMaxFlight]; speed floored at kMinSpeed.
float flightTime(float distance, float speed);

// ---------------------------------------------------------------------------------------------- the launch
enum class TargetKind : uint8_t {
    Figure,        // a target figure standing on the ground or water
    FlyingFigure,  // a target figure in the air: straight path, radius 0, speed at least its MoveRate x 1.1
    Participant,   // no target figure (a city): its centre with HitRadius or MissTargetSlopRadius + 5
};
struct LaunchRequest {
    V3 launch;                    // the bone's world position when the refid Effect trigger fired
    V3 tangent_local{0, -1, 0};   // the Transfer's tangent, in the figure's frame
    M4 figure_world;              // the firing figure's world matrix (only its rotation turns the tangent)
    float effect_movement_speed = 0;   // the VisEffect's fMovementSpeed (0 = none)
    const ProjectileWeapon* weapon = nullptr;   // required
    TargetKind target = TargetKind::Figure;
    V3 target_pos;                // the target figure's (or participant's) world position
    float target_radius = kDefaultTargetRadius;   // the target member's TargetRadius
    float target_height = 0;      // the target member's TargetHeight
    float target_move_rate = 0.78125f;   // the target member's MoveRate (a flying target)
    bool miss = false;            // the choreography's Spent hit: the shot is not needed (effects-server 1.1 item 2)
    // a dropped weapon (IsDropped): where the dropping figure is and goes
    V3 attacker_pos;              // the firing figure's world position
    float attacker_heading = 0;   // radians; the lead runs along (sin h, -cos h)
    float attacker_move_rate = 0.78125f;
    float water_level = 0;        // the altitude is the figure's z above it
};
struct LaunchPlan {
    FlightPath path;
    V3 target;                    // the point the flight ends at
    float speed = 0;              // plots per second, after the floors
    float radius = 0;             // the random offset's radius used
    bool dropped = false;
};
// The ground height at (x, y); for the target point and a bomb's impact. Null: z = 0.
using HeightFn = std::function<float(float x, float y)>;
// A small deterministic generator for the random offset (the EXE's own draws are not reproduced, P3 of 0320).
struct FlightRandom {
    uint64_t state = 0x4792u;
    float unit();                 // [0, 1)
};
// effects-server.md 1.1 items 3-6. False with `why` for no weapon or a non-finite input; never throws.
bool planLaunch(const LaunchRequest& r, FlightRandom& rng, const HeightFn& height, LaunchPlan& out, std::string* why);

// ---------------------------------------------------------------------------------------------- the flights
// The records a flight sends. Kinds map onto the Architect's route S note 1.3 (EffectStart / EffectMove / EffectStop);
// ChildStart and HitStart are EffectStarts of one-shot effects at the end point. 4722's emitter takes them (W16).
enum class RecordKind : uint8_t { EffectStart, EffectMove, EffectStop, ChildStart, HitStart };
const char* recordKindName(RecordKind k);
struct ProjectileRecord {
    RecordKind kind = RecordKind::EffectMove;
    double time = 0;              // seconds on the flights' clock
    uint32_t handle = 0;          // the flight's handle (children and hit effects: the flight's handle too)
    uint64_t owner = 0;           // the fight (CombatRecord::returned; 0 = unknown)
    std::string effect;           // the VisEffect name, substitutions already applied
    M4 world;                     // the effect's world matrix (the EXE applies the definition's transform itself)
    float scale = 1;
    V3 velocity;                  // world units per second (EffectMove)
};
// A model the server would draw (option A, off by default): the flight's VisEffect at this world matrix.
struct ModelInstance {
    uint32_t handle = 0;
    std::string effect;
    M4 world;
    float scale = 1;
};
struct ProjectileSpec {
    uint64_t owner = 0;           // the fight's id
    int32_t player = -1, unit = -1;
    uint16_t figure = 0;
    std::string effect;           // the projectile's VisEffect (the Transfer's refid Effect event)
    float scale = 1;              // the firing member's Scale (ArtDefine_UnitMemberInfos; 0.14 archer, 0.12 catapult in
                                  // the EXE's flights, effects-server.md 2.4 [L])
    std::vector<std::string> children;   // the ProjectileEnd children, resolved ($(TERRAIN) applied)
    float child_scale = 1;
    std::string hit_effect;       // the weapon's HitEffect, resolved; "" = none
    float hit_scale = 1;
    bool hit = true;              // false: a miss (the hit effect still starts where it lands [I])
    FlightPath path;
    double t0 = 0;                // when the flight starts (the Transfer's moment), on the flights' clock
    bool start_effect = true;     // send an EffectStart at the path's start (false when the trigger route, 4793, has
                                  // started it on the bone and the flight takes over its handle)
    uint32_t handle = 0;          // 0 = the set gives one
};
struct ProjectileCounts {
    size_t launched = 0, landed = 0, removed = 0, records = 0, children = 0, hits = 0;
};
enum class ModelMode : uint8_t {
    EffectOnly = 0,   // B (lead-0042 #7986, the product default): the VisEffect draws model and trail (stage A)
    ServerModel = 1,  // A: the server draws the model as well (until a particle-only start exists, the EXE's copy of
                      // the model draws over it); for stage B and the offline harness
};
class ProjectileSet {
public:
    void setModelMode(ModelMode m) { mode_ = m; }
    ModelMode modelMode() const { return mode_; }
    // Adds a flight. False with `why` for a path that does not evaluate or an empty effect name; returns the handle.
    bool add(const ProjectileSpec& spec, uint32_t& handle, std::string* why);
    // Advances to time t: for each flight started by t (t >= t0): its EffectStart at the start (once), then an
    // EffectMove at its point at t; a flight whose time is over (t - t0 > duration) sends its end: the children
    // (ChildStart, scale child_scale) and the hit effect (HitStart) at the end point, no stop (the effect ends by its
    // own duration, effects-server.md 1.4), and leaves the set. `models` (may be null) gets the live flights' models
    // in ServerModel mode. Records come in handle order within a frame.
    void frame(double t, std::vector<ProjectileRecord>& out, std::vector<ModelInstance>* models);
    // A forced end (the fight's limit, a unit gone, a reset; the Architect's note 1.5 item 4): every flight of `owner`
    // still in the air (or not yet started) is removed with an EffectStop, and its impact is dropped. owner = UINT64_MAX:
    // every flight. Returns how many were removed.
    size_t forceEnd(double t, uint64_t owner, std::vector<ProjectileRecord>& out);
    size_t live() const { return flights_.size(); }
    const ProjectileCounts& counts() const { return counts_; }
    void clear();

private:
    struct Flight {
        ProjectileSpec spec;
        bool started = false;
    };
    std::map<uint32_t, Flight> flights_;
    uint32_t next_ = 1;
    ModelMode mode_ = ModelMode::EffectOnly;
    ProjectileCounts counts_;
};

}}  // namespace c5r::effects
