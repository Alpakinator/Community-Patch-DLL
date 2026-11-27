// c5r/effects/effect_routes.h - the trigger intake of a fight's effects (epic 0042, ticket 4793, route S stage A; D210,
// D211). Own code, GPL-3.0-or-later. Written from the knowledge base only (research/05-graphics-composition/
// effects-formats.md 1, 6, 8.1, 14 and its section "E8 as built (4793)"): no decompiler output, nothing of the game's
// files (D83). Effect definitions and trigger files are read from the player's install at run time.
//
// Stage A (the Architect's route S note 2.1, 2.3): every effect start that belongs to a fight leaves the server as an
// EffectStart / EffectMove / EffectStop record (shell/common/vp_trigger.h) and the EXE's Particle system plays it. The
// records are written by 4722's emitter (server/src/effects/srv_trigger_emitter.h); this header is the part that needs
// no server: which effect a start names once its variables are resolved (EffectNames), what a combat hit starts
// (planHitEffect: a one-shot at the landing, a city volley flown by 4792's ProjectileSet, or nothing because the
// projectile's own flight starts it), and which Effect rows of a trigger file are a projectile held in the hand until
// its Transfer (HeldEffects). The server's side is server/src/effects/srv_effect_routes.h.
//
// Nothing throws. Not thread-safe (one per render thread).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "c5models/effect_xml.h"
#include "c5models/logic.h"
#include "c5r/effects/projectile.h"
#include "c5r/unitanim/combat.h"

namespace c5r { namespace effects {

// ---------------------------------------------------------------------------------------------- names
// The values a name's variables take for one start (the figure's plot and weapon). A value not given (has_* false) is
// "not known": $(TERRAIN) / $(BASETERRAIN) then take the fallback surface (counted), $(WEAPON) the empty value when that
// variant is defined (counted).
struct NameValues {
    bool has_terrain = false, has_base_terrain = false, has_weapon = false;
    std::string terrain, base_terrain, weapon;
    NameValues& set_terrain(const std::string& v) { terrain = v; has_terrain = true; return *this; }
    NameValues& set_base_terrain(const std::string& v) { base_terrain = v; has_base_terrain = true; return *this; }
    NameValues& set_weapon(const std::string& v) { weapon = v; has_weapon = true; return *this; }
};

enum class NameResult : uint8_t {
    Exact = 0,      // the name (after substitution with the given values) is defined
    Fallback,       // the surface's variant is not defined (or no surface was given): the fallback surface's is
    WeaponEmpty,    // no weapon value was given: the empty $(WEAPON) variant is defined and taken
    NotDefined,     // nothing defined: a dangling name (effects-formats.md 6: 2 trigger names) or no variant at all
    BadName,        // an unknown variable or an unterminated "$(" (fx_substitute's refusal)
};
const char* nameResultName(NameResult r);

struct NameStats {
    size_t exact = 0, fallback = 0, weapon_empty = 0, not_defined = 0, bad_name = 0;
    std::map<std::string, size_t> refused;    // name as written -> refusals (NotDefined, BadName)
    std::map<std::string, size_t> fell_back;  // resolved name -> starts that took it by the fallback
};

// The resolver over 4786's library (effects-formats.md 14). The rule for a $(TERRAIN) / $(BASETERRAIN) variant that is
// not defined for the surface (14.3: four impact families exist only for CITY and DIRT; the EXE's own choice is not
// read): the fallback surface (DIRT) [I], counted per name; a name still not defined is refused by name, never sent.
class EffectNames {
public:
    explicit EffectNames(const c5m::EffectLibrary& lib, const std::string& fallback_surface = "DIRT")
        : lib_(lib), fallback_(fallback_surface) {}
    // `out` = the VisEffect name in force (no variable left) for Exact, Fallback and WeaponEmpty; `why` names a refusal.
    NameResult resolve(const std::string& name, const NameValues& v, std::string& out, std::string* why = nullptr);
    // The ProjectileEnd children of a defined VisEffect, each resolved by the same rule with the same values; refused
    // children are left out (counted). Returns how many were refused.
    size_t children(const std::string& resolved, const NameValues& v, std::vector<std::string>& out);
    bool defined(const std::string& resolved) const { return lib_.vis_effect(resolved) != nullptr; }
    const c5m::VisEffectDef* def(const std::string& resolved) const { return lib_.vis_effect(resolved); }
    // fBaseDuration x fBaseDurationScale (seconds; the model-animation rule needs the clip, not read here); -1 unknown.
    float baseDuration(const std::string& resolved) const;
    // fMovementSpeed (plots per second; 0 = none written).
    float movementSpeed(const std::string& resolved) const;
    const NameStats& stats() const { return st_; }
    void resetStats() { st_ = NameStats(); }
    const std::string& fallbackSurface() const { return fallback_; }

private:
    bool tryName(const std::string& name, const c5m::FxSubstitutions& s, std::string& out, c5m::Error& e) const;
    const c5m::EffectLibrary& lib_;
    std::string fallback_;
    NameStats st_;
};

// ---------------------------------------------------------------------------------------------- held projectiles
// An Effect row that a Transfer row of the same trigger file names by `refid` starts a projectile in the hand (the arrow,
// the torch, the rock) that the Transfer then hands to a flight (effects-formats.md 6 and 7, effects-server.md 1.4:
// the EXE's own hand-over record 0x2B). 579 of the install's 603 Transfer rows name an Effect row.
class HeldEffects {
public:
    // Adds one parsed trigger file under its name (lower-case basename, as TriggerEvent / TriggerIn carry it).
    // Returns the Effect rows found held in it.
    size_t add(const std::string& file, const c5m::TriggerSystem& ts);
    // The Effect row (file, its id) is a Transfer's refid; `transfer` (may be null) gets that Transfer row's id.
    bool held(const std::string& file, int32_t trigger, int32_t* transfer = nullptr) const;
    // The Effect row a Transfer row of the file hands over (its refid); -1 = none or not an Effect row.
    int32_t refidOf(const std::string& file, int32_t transfer) const;
    size_t files() const { return files_.size(); }
    size_t heldRows() const;

private:
    struct File {
        std::map<int32_t, int32_t> held;       // Effect id -> the Transfer id that names it
        std::map<int32_t, int32_t> transfers;  // Transfer id -> its Effect refid (-1 when it names no Effect row)
    };
    std::map<std::string, File> files_;
};

// ---------------------------------------------------------------------------------------------- combat hits
// What a combat hit of the core (c5r/unitanim/combat.h CombatHit, 4777's fields) starts on the trigger stream.
enum class HitRoute : uint8_t {
    None = 0,      // no effect name, an area share (the blast's effect rides on the first hit), or no position
    Flight,        // a unit's projectile: its flight (4792's ProjectileSet) starts the HitEffect where it lands
    OneShot,       // a weapon without a flight (machine guns, anti-air): the HitEffect at the landing, at hit.time
    CityVolley,    // a city's strike: the era's CITY_BOMBARD_* VisEffect flown from the city's centre (a projectile
                   // effect: arrow model + trail + ProjectileEnd child), its children where it lands
    CityDefence,   // a city's answer in a melee (projectile false): no effect [I] (named, section E8)
    Refused,       // a name that resolves to nothing (counted by EffectNames, named in why)
};
const char* hitRouteName(HitRoute r);

struct HitEffectPlan {
    HitRoute route = HitRoute::None;
    std::string effect;            // resolved VisEffect (OneShot, CityVolley)
    double time = 0;               // OneShot: when it starts (hit.time); CityVolley: the launch (hit.launch)
    float pos[3] = {0, 0, 0};      // OneShot: where (hit.pos)
    float scale = 0;               // OneShot: HitEffectScale; 0 = unset: the striking member's scale (the caller's)
    ProjectileSpec flight;         // CityVolley: the flight to add to the ProjectileSet (path, t0, children)
    std::string why;               // None / Refused / CityDefence: why nothing is started
};

struct HitPlanParams {
    // The city volley's launch direction: up by this angle from the horizontal towards the target [I: the EXE's city
    // strike was not read; 45 degrees gives the arrow's arc of 4792's launches a similar rise].
    float city_launch_deg = 45.0f;
    // The city volley's launch height above the city's centre (world units) [I].
    float city_launch_height = 10.0f;
    // A volley with no flight time in the hit (time <= launch): this flight [I, the core's city_flight].
    float city_flight = 0.6f;
    // 0742: the city volley's projectile scale and its ProjectileEnd children's (a city has no member). 1.0 is the
    // rule before 0742 (the archer's arrow model about seven times stock's: unit-combat-server.md 18.43); the server
    // sets them from RouteParams when render_units_city_volley is on.
    float city_scale = 1.0f;
    float city_child_scale = 1.0f;
};

// `target` = the values at the landing's plot ($(TERRAIN): the target plot's surface, CITY for a city target).
// Never throws; false only for a non-finite input (out.why names it).
bool planHitEffect(const unitanim::CombatHit& hit, EffectNames& names, const NameValues& target, const HitPlanParams& p,
                   HitEffectPlan& out);

// ---------------------------------------------------------------------------------------------- projectile speed
// effects-server.md 1.1 item 3 [C]: a projectile flies at its VisEffect's fMovementSpeed when that is not 0, else at
// the weapon's ProjectileSpeed. loadCombatTables (c5r/unitanim) reads ProjectileSpeed only, so the core's flight time
// of a catapult (NULL speed, effect 3.3) was 2.0's (effects-server.md 2.5 item 2, W15's open item). For each member
// given with its trigger file: the Effect row that a Transfer row hands over names the projectile effect; when its
// fMovementSpeed is above 0 it becomes `speed` of the member's projectile weapons that are not dropped. A member whose
// Transfers name effects of different speeds takes the first Transfer's (file order) and is noted. Returns the weapons
// changed. The caller is whoever builds the CombatArtTables with the members' art (4774/4726's adapter in the server,
// the scene tools offline).
size_t applyEffectSpeeds(unitanim::CombatArtTables& tables,
                         const std::map<std::string, const c5m::TriggerSystem*>& member_triggers, EffectNames& names,
                         std::vector<std::string>* notes = nullptr);

}}  // namespace c5r::effects
