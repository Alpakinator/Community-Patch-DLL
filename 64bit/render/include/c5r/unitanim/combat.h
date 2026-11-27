// c5r/unitanim/combat.h - the combat choreography core (epic 0042, issue 4775; research/05-graphics-composition/
// combat-choreography.md (0321) sections 2-7, written from that text only (D83: no decompiler output, no EXE bytes);
// as built: unit-combat-server.md "Choreography as built (4775)").
//
// One visualised combat in, a schedule out. The input is the server's combat record (SrvFeed::CombatRecord's fields,
// unit-server-scene.md 2.3 and 11) plus, for each side that is a unit, the unit's figures as the controller holds them
// (members, the figures standing, each member's EnableActions words and weapons). The output is, per figure, the
// sequence of choreography states (0321 5.2) with the action code each requests, the target figure and the approach
// intent; the controller events those requests make (action attack / charge / attack_charge / attack_city / bombard /
// bombard_defend / run / combat_ready / idle / victory, kill), each at its time in seconds from the combat's start;
// the hit moments (the Action trigger, or the Transfer trigger plus the flight for a projectile); and the end time:
// the choreography's own end, or the limit (12 s; 20 s with an air figure, 0321 4.2 and 13.2).
//
// Route independent: the core runs the whole combat at once from the clip timings a CombatClips gives (the member's
// clip durations and trigger times), with its own seeded random source, so a given (input, seed) always gives the same
// schedule (0321 P7: the EXE's own random choices are not reproduced). Where the figures stand and move is 4776's: the
// core names the target figure and whether the figure approaches (run or run charge); it moves nothing. The aircraft,
// city-attacker, nuclear, area and interception cases are 4777's (unit-combat-server.md "Special cases (4777)"): played
// since 4777, no longer refused (their CombatCode values stay for the callers' texts). Nothing throws.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/unitanim/timeline.h"

namespace c5r { namespace unitanim {

// ---------------------------------------------------------------------------------------------- the input
// SrvFeed::CombatRecord::flags (unit-server-scene.md 11), the same bits.
enum CombatFlagBits : uint16_t {
    kCombatAttackerAdvances = 1u << 0,
    kCombatDefenderRetaliates = 1u << 1,
    kCombatRanged = 1u << 2,
    kCombatBombing = 1u << 3,
    kCombatAirSweep = 1u << 4,
    kCombatDefenderCaptured = 1u << 5,
    kCombatVisualize = 1u << 6,
    kCombatReturnedKnown = 1u << 7,
    kCombatQuickCombat = 1u << 8,
    kCombatTurnActive = 1u << 9,
    kCombatGameFlagsRead = 1u << 10,
};
constexpr uint16_t kCombatKindUnit = 25;   // kUnitCombat (GameplayUnitCombat)
constexpr uint16_t kCombatKindCity = 28;   // kCityCombat (GameplayCityCombat)

struct CombatSide {
    int32_t owner = -1, unit = -1;   // the unit (unit = -1 for a city side)
    int32_t city = -1;               // >= 0: the side is a city (a participant without figures, 0321 5.5)
    int32_t final_damage = 0;        // GetFinalDamage: the unit's damage after the combat
    int32_t max_hp = 100;            // the unit's (or city's) maximum hit points
    // 4777 (additive): the damage this side takes in this combat, as the RunCombatSim event's damage field carries it (the
    // EXE's participant record, 0321 13.4: the air strike's opening phase reads the attacker's); -1 = not known (the
    // server's SrvFeed::CombatRecord does not carry it today: the retaliate flag decides then).
    int32_t damage = -1;
};

// One damage member (SrvFeed::CombatMember). 4777: each is a participant (3 onward, 0321 4.1): a unit member whose figures
// the caller gives (CombatRun::start's `others`) loses its planned deaths at the area hit; a city member, or a unit
// member without figures, takes part without figures (its damage shows on the unit model after the fight).
struct CombatMemberIn {
    int32_t player = -1, id = -1;
    uint16_t flags = 0;              // bit 0: a city (INFO_FLAG_IS_CITY)
    int16_t damage = 0, final_damage = 0, max_hp = 0;
};

struct CombatInput {
    uint32_t seq = 0;                // the combat event's seq
    uint16_t kind = kCombatKindUnit;
    uint16_t flags = kCombatVisualize;
    int32_t plot = -1;
    CombatSide attacker, defender;   // 4777: with nuclear_level > 0 the defender may name neither unit nor city (the
                                     // plot target, 0321 4.1; the DLL's nuclear record has no defender unless a city
                                     // intercepts); its centre is then CombatUnitFigures::centre of the defender's figures
    int32_t nuclear_level = 0;
    std::vector<CombatMemberIn> members;
    // 4777 (additive): the interceptor, participant 2 (0321 5.1, 13.4; the DLL's BATTLE_UNIT_INTERCEPTOR of an air strike):
    // unit = -1 when none. Its `damage` is its record's damage field. Not in SrvFeed::CombatRecord today (a named gap).
    CombatSide interceptor;
};

// A weapon of a member (ArtDefine_UnitMemberCombatWeapons, 0321 6).
struct CombatWeapon {
    bool projectile = false;         // a projectile weapon (kind 1): its hit is the Transfer trigger plus the flight
    bool one_shot = false;           // the weapon record's one-shot byte (a missile, a suicide attacker; 0321 5.3)
    bool vs_city = false;            // Usage Vs_City
    float flight = 0;                // seconds from the Transfer trigger to the arrival (the caller's: ProjectileSpeed
                                     // and the distance; 0 = arrives at the trigger) [I: 0321 5.5]
    // 4777 (additive): the weapon row's columns the special cases read (ArtDefine_UnitMemberCombatWeapons, 0321 6, 13.5;
    // names as the table writes them, read from the player's database at run time, D83).
    bool dropped = false;            // IsDropped: a bomb, it falls from the striker's position (0321 13.5)
    bool target_ground = false;      // TargetGround
    float speed = 0;                 // ProjectileSpeed (0 = none); read as plots per second, x 64 world units [I]
    float area_delay = 0;            // AreaDamageDelay: seconds from the hit to the area's damage
    bool wait_effect = false;        // WaitForEffectCompletion: the fight waits for the hit's effect to end
    std::string effect;              // HitEffect as written ($(TERRAIN) left for the trigger stream; "" = none)
    float effect_scale = 0;          // HitEffectScale; 0 when NULL, 0 or below: the striking member's own scale applies
                                     // (effects-server.md 1.1 item 1; never 1.0: effects-formats.md 15.7.8)
    std::string sound;               // WeaponTypeSoundOverrideTag, else WeaponTypeTag ("" = none)
    // 4783 (W33, additive): VisKillStrengthMin / Max, percent. A landing strikes max(1, ceil(U(min, max) / 100 x the
    // target's figures)) figures; one figure when max is 0 (NULL). combat-choreography.md 5.4 [C].
    float vis_kill_min = 0;
    float vis_kill_max = 0;
};

// 4776 (additive): how a member's figures move in a fight, from ArtDefine_UnitMemberCombats with the EXE's defaults for
// a NULL or non-positive value (unit-combat-server.md "Figure motion (4776)" M2, [C] the DB reader 0x4B8BF0). A move
// covers MoveRate x 64 world units per second (64 = the plot spacing, [C] 0x61D7D0 with the float at 0x9E0AA8) and ends
// within MoveRadius of its destination; a move whose destination lies within ShortMoveRadius is a shuffle (1450) at
// ShortMoveRate x 64 ([C] 0x6902C0).
struct CombatMotion {
    float move_radius = 3.5f;        // MoveRadius (default 3.5)
    float short_move_radius = 0;     // ShortMoveRadius (default: MoveRadius)
    float move_rate = 0.78125f;      // MoveRate, plots per second (default 0.78125: 50 world units per second)
    float short_move_rate = 0;       // ShortMoveRate (default: MoveRate)
    float turn_rate_min = 0.5f, turn_rate_max = 1.0f;   // TurnRateMin / Max, turns per second (max >= min)
    // 0685 (additive): TurnFacingRateMin / Max (default 15 / 20, max >= min; unit-combat-server.md M1 [C]); the server's
    // walking formations turn at the mid as radians per second (unit-server-scene.md 19.9 [I]); no fight code reads them
    float turn_facing_rate_min = 15.0f, turn_facing_rate_max = 20.0f;
    int reform = 0;                  // ReformBeforeCombat (0, 1, 2)
    float altitude = 90.0f;          // 4777 (additive): AttackAltitude (default 90, M1), world units: an aircraft's flight
    float attack_radius = 8.0f;      // 0743 (additive): AttackRadius (default 8.0, M1): half of stock's melee reach
    float shortRadius() const { return short_move_radius > 0 ? short_move_radius : move_radius; }
    float shortRate() const { return short_move_rate > 0 ? short_move_rate : move_rate; }
};
const float kMoveRateUnits = 64.0f;   // world units per plot of MoveRate ([C] 0x61D7D0)

// One member of a unit, as the choreography needs it.
struct CombatMemberArt {
    std::string member;              // ArtDefine_UnitMemberInfos.Type
    int count = 0;                   // figures of this member
    std::string domain;              // "Land", "Sea", "Air" ("" = Land)
    std::set<std::string> enable;    // ArtDefine_UnitMemberCombats.EnableActions words (empty = no Combats row:
                                     // nothing can be checked, the member cannot attack)
    bool victory = false;            // the member's machine takes 1800 on path 4 (0321 5.3: "it has 1800 enabled") [I]
    bool rush = false;               // RushAttackFormation set (DefaultCavalry, ChariotElephant): approaches by run
                                     // charge (1120) and strikes with charge attack (1140) [I: 0321 5.5]
    bool last_to_die = false;        // LastToDie: its figure is the protected survivor first (0321 13.4)
    std::vector<CombatWeapon> weapons;   // Index order; the first is the one used (Vs_City for a city target)
    CombatMotion motion;             // 4776 (additive)
    // 4777 (additive): whether the member's machine takes the anti-air request 1130 (0321 13.5): 1 yes, 0 no (the
    // submarines' 1_OMNI machine: the answer never plays, the fight holds to its limit as on stock), -1 not known (a
    // member that binds no 1130 clip then answers with its attack clip, as the ships' machines do).
    int anti_air = -1;
};

// A unit side's figures: members in member order (figure index = position in member order, 0041 P8) and the figures
// standing at the combat's start (sorted, distinct).
struct CombatUnitFigures {
    int32_t player = -1, id = -1;
    std::vector<CombatMemberArt> members;
    std::vector<uint16_t> standing;
    uint32_t figures() const;        // sum of the members' counts
    // 4776 (additive): where the standing figures stand at the start, world units (x[i], y[i] for standing[i]; empty =
    // not known: the approach then lasts CombatParams::approach). For a city side only the centre is read.
    std::vector<float> x, y;
    bool has_centre = false;         // the unit's root (or the city's plot centre)
    float centre_x = 0, centre_y = 0;
    // 4735 (additive): the heading the unit's formation stands in at the start (placement.h's yaw convention: radians
    // about +z, the figure facing its local -y; combatPositions sets it from the unit's facing). Without it no figure of
    // the unit turns before its fight (CombatParams::turn_first).
    bool has_facing = false;
    float facing_yaw = 0;
    // 4744 (additive): the figures the unit's placement draws, 0 .. drawn-1 (units::livingFigures of its damage before
    // the fight; Controller::combatFigures sets it). A unit created damaged is held with every figure (4736) while only
    // these are drawn. UINT32_MAX: not known (every standing figure is drawn). CombatParams::fight_drawn_figures.
    uint32_t drawn = 0xFFFFFFFFu;
};

// 4776 (additive): one straight move of a figure in a fight (the approach run, a run charge or a shuffle).
struct FigureMove {
    int32_t player = -1, id = -1;
    uint16_t figure = 0;
    int32_t code = 1400;             // 1400 run, 1120 run charge, 1450 shuffle
    double t0 = 0, t1 = 0;           // seconds from the combat's start: leaves, arrives
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // world units
    // 4777 (additive): an aircraft's move (out to its target, back to its base) at its AttackAltitude: z0 / z1 the height
    // above its start at leaving / arriving (0 for a figure on the ground); `back` the flight home.
    float z0 = 0, z1 = 0;
    bool air = false, back = false;
    // 4735 (additive): a turn in place before the fight (CombatParams::turn_first): the figure stays where it stands
    // (x1, y1 = x0, y0) and its heading goes from yaw0 to yaw1 (the shorter way) over t0..t1, playing `code` (1400).
    bool turn = false;
    float yaw0 = 0, yaw1 = 0;
};

// The clip timings of a member's action code: the clip's duration and the hit moments, seconds from the action's
// start (the Action triggers of the code's .ftsxml track; for a projectile or a city attack, the Transfer triggers).
struct ActionTiming {
    bool found = false;              // false: the member binds no playable clip for the code (4770's missing clip)
    float duration = 0;
    std::vector<float> hits;         // sorted; the Action triggers of the code's clip, or, when it has none, its
                                     // Transfer triggers (a projectile or a thrown torch: 6.4 of unit-controller.md)
    std::vector<int32_t> hit_ids;    // the trigger id of each hit (same order; empty = not known): the clip-instance
                                     // identity the trigger stream keeps (lead-0042, 4795)
    bool transfer = false;           // the hits are Transfer triggers: each lands after the weapon's flight
    std::string why;                 // when !found: the member, code and file
};
class CombatClips {
public:
    virtual ~CombatClips() = default;
    // The timing of `code` on member `member` of unit (player, id). Called once per member and code.
    virtual ActionTiming timing(int32_t player, int32_t id, const CombatMemberArt& member, int32_t code) = 0;
    // 4777 (additive): seconds the named hit effect plays (WaitForEffectCompletion holds the fight that long after the
    // hit); < 0 = not known (CombatParams::effect_wait is used). Stage A's effect player can answer it.
    virtual double effectDuration(const std::string& effect) {
        (void)effect;
        return -1;
    }
};

// Which figures die when a side has deaths planned (lead-0042 #7555).
enum class DeathPick : uint8_t {
    LastFirst,   // the last standing figures by index (the 0x4B4B80 order, unit-formations.md 7): the placement draws
                 // figures 0..m-1 and the damage rule removes from the last; the hit figure is chosen among these
    Targeted,    // any standing figure but the protected one may be the one hit (0321 13.4 as the EXE has it); for 4776
};
// Who plays victory at the end (0321 5.3: "its side's count at +0x34 is not above +0x2B84", read [I]).
enum class VictoryRule : uint8_t {
    OpponentKilled,   // only figures whose opponent unit is killed (the reading of the count test as "its side won")
    Always,           // every eligible survivor (the other reading; for comparison)
};

struct CombatParams {
    uint64_t seed = 0;               // the scene seed; the combat's draws are seeded with it, the seq and both units
    double step = 1.0 / 30.0;        // the choreography's clock step (the controller's h, unit-controller.md 13.1)
    double limit = 12.0;             // 0321 4.2 [C]
    double limit_air = 20.0;         // with an air figure of any participant (0321 4.2, 13.2 [L]; 4777)
    double death_cap = 3.0;          // a dying figure is removed after its 1200 clip or this many seconds (0321 5.3 [C])
    double approach = 1.0;           // seconds of run before a melee strike when the figures' positions are not given
                                     // (4776: with positions the run lasts its distance at the member's rate)
    float city_reach = 32.0f;        // 4776: a figure attacking a city stops this far from the city's centre [I]
    int victory_threshold = 25;      // victory when random(100) > this (0321 5.3 [C]: 74 of 100 values)
    int max_in_flight = 0;           // strikes under way against one participant at once (0 = as many as it needs);
                                     // the wave counts of 0321 5.1 are not read: a comparison knob [I]
    DeathPick deaths = DeathPick::LastFirst;
    VictoryRule victory = VictoryRule::OpponentKilled;
    // 4777 (additive): the special cases (unit-combat-server.md "Special cases (4777)"). Marks as there.
    double air_flight = 1.0;         // an aircraft's flight out (and the same back) when positions are not given [I]
    double city_strike_at = 0.5;     // a city's ranged strike: its volley leaves this long after the start [I]
    double city_flight = 0.6;        // ... and each shot lands this long after it leaves [I]
    double city_stagger = 0.1;       // ... the volley's shots this far apart [I]
    std::string city_effect, city_sound;   // the city strike's hit effect and sound names (the caller's, "" = none)
    double effect_wait = 10.3;       // a WaitForEffectCompletion hit holds the fight this long when CombatClips names
                                     // no duration ([L 0553]: the Nuclear Missile's end 10.1-10.4 s after its flash)
    bool unnamed_effect_holds = true;   // a WaitForEffectCompletion weapon without a HitEffect (the Atomic Bomb's dropped
                                        // bomb) holds the fight to its limit: stock's 20 s ([L 0553] 6 of 6; ticket 0560)
    bool ships_approach = true;      // a sea figure's melee attack moves to its target (4776's land rule); off: ships
                                     // strike from where they stand. On: 0557 n1-n3 within +0.82 / +0.73 / -0.07 s of
                                     // stock, off: -0.94 / -1.04 / -2.07 s (unit-combat-server.md 8.4) [I]
    // 4783 (W33, C2; combat-choreography.md 13.4 [C], 2026-11-20): every figure a landing strikes pays one owed hit of its
    // side, a death too, as the EXE's hit does; so a killed side's owed hit is paid by its first death. Off: the 4775
    // rule (an owed hit paid only by a landing with no planned death left), under which a lone striker's kill held to
    // the limit (unit-combat-server.md 18.2: the battleship's infantry kill, 12.00 s). A comparison knob.
    bool owed_paid_by_any_hit = true;
    // 4783 (W33, C1; combat-choreography.md 5.4 [C], 2026-11-20): a landing strikes as many figures as the EXE's hit does
    // (the weapon's VisKillStrength range on the target's figures; CombatWeapon::vis_kill_min / max), the aimed one and
    // the nearest others, as many as planned deaths are left. Off: one figure per landing (the 4775 rule), under which few
    // strikers killing a 14-figure unit held to the limit (unit-combat-server.md 18.2). A comparison knob.
    bool vis_kill = true;
    // 4733 (W36; combat-choreography.md 13.4 [C]): the last standing figure of the attacker or the defender is spared (only
    // hit, not killed) while the other side still has planned deaths unplayed or an owed hit open, as the EXE's hit spares
    // it; so a killed side keeps one figure to play the other side's deaths. Off: the 4775 rule, under which a side whose
    // figures all fell before any of its strikes landed left the other side's deaths unplayed and held the fight to the
    // limit (live W0-b, 4733: a 14-figure attacker owed 4-6 deaths, 0 played). A comparison knob.
    bool spare_last_figure = true;
    // 4737 (W39; research/02-exe-internals/exe-audio.md 13.3 [C], lead-0042 #9324): an attacking figure that approaches
    // run-charges (1120) whenever its member enables RunCharge, as the EXE's target step puts every attacking figure
    // farther than its MoveRadius from its target into the charge state 0xC (0x5C7100) with no rush test; with
    // AttackCharge it then strikes with 1140 and skips the combat-ready entry (1600). Off: the 4775 rule, run charge only
    // for a RushAttackFormation member (the swordsman then ran, entered 1600 and sent its FOOT_ALWAYS / FORTIFY_VOX per
    // figure; stock charged, W37's Tiny pair). A comparison knob.
    // 4743 (W46, MC #9883): OFF by default. Before charge_wait (below), with it on a charge's 1140 strike could be
    // requested of a figure whose machine is not in its run loop (the only state that takes 1140); the strike then never
    // played and the fight held to the 12 s limit (S15 on 26577625, Pathfinder on a barbarian Warrior; offline with one
    // long frame, unit-combat-server.md 18.13). The fix exists (charge_wait, W46/W48).
    // 4743 (W81, MC #11142 after W79's live sessions S26-S30, unit-combat-server.md 18.28, 18.28.1): ON by default again.
    // With charge_wait, S15's fight ends in 5.15 s (0 at the limit, every hold ended by OwnerEnd), and only charge_any
    // gives stock's run-charge vox in infantry melee; same-dice length cost melee -0.17 s, kill +0.26 s. Off is the 4775
    // rush-only rule (the default from 4743 W46 to W81).
    bool charge_any = true;
    // 4743 (W46, unit-combat-server.md 18.13): live (with the controller's feedback), a charging figure with AttackCharge
    // strikes with 1140 only once its machine takes 1140 (its run loop); it waits for that at most this long past its
    // charge's end, then stops in combat ready and strikes 1100. Negative: the 4737 rule (1140 at the charge's end
    // whatever the machine's state; a 1140 asked of a figure in combat ready never plays and the fight holds to the
    // limit). Self-timed runs (no feedback) are not affected.
    double charge_wait = 0.5;
    // 4733 (W36, form C of unit-combat-server.md 18.9): without positions, a melee attack's figures approach whatever
    // their weapon (the EXE's fight phase moves every attacking figure of a melee attack, 4776 M3 [C]), so they are
    // engaged and a melee defender can strike at them. Off: the 4775 rule (melee weapons only), under which an
    // attacker with a projectile weapon (HasShortRangedAttack: Great War Infantry, Marines, Paratroopers) stood still and
    // a melee-only defender (a Mamluk, a Knight) never found a target: the attacker's deaths held the fight to the limit
    // (live fe44, event id 120850). A comparison knob.
    bool unplaced_attacker_approaches = true;
    // 4735 (W45; unit-combat-server.md 10.11): before a fight whose figures stand (a ranged attack: both sides; a city's
    // volley: its target; a ship that strikes where it stands), a placed figure whose unit's heading (CombatUnitFigures::
    // facing_yaw) is more than turn_threshold off the direction to the opponent's centre first turns in place, playing
    // the run (1400), then goes on as before (combat ready, bombard defend). The EXE's phase 2 turns both sides (5.1 M3,
    // state 0xF [C]) at TurnRateMin + (TurnRateMax - TurnRateMin) x a fraction turns per second (M5 [C]); stock's figures
    // play their run and its loops about 0.1 s into every such fight (W37's Miles logs [L]). Read [I]: the fraction 0.5;
    // 30 degrees, half a facing step (a unit facing its target's hex direction needs no turn); the turn lasts
    // turn_min..turn_max seconds (a run shorter than turn_min crosses none of its sound triggers). The strike waits for
    // the turn, so these fights are longer by it, as stock's. Off: no turn (the rule before 4735). A comparison knob.
    bool turn_first = true;
    float turn_threshold = 0.5235988f;   // radians (30 degrees)
    double turn_min = 0.3, turn_max = 1.2;   // seconds
    // 4744 (W49; unit-combat-server.md 18.16): only the standing figures the unit's placement draws take part (index below
    // CombatUnitFigures::drawn). The EXE plans a fight's deaths over the figures its model shows: figures x damage % /
    // 100 minus the figures already out (combat-choreography.md 13.4, 5.4 [C 0321]); a unit the controller created
    // damaged holds every figure, so without this its undrawn figures stood, took planned deaths and were the riders'
    // first targets (W46's horseman kill of a Warrior at 50 damage: 10 standing for 5 drawn, 12 s offline; S15's
    // Warrior at 75: 10 standing for 2 drawn, live). Off: every held standing figure takes part (the rule before 4744).
    // A comparison knob.
    bool fight_drawn_figures = true;
    // 4745 + 4747 (W53; unit-combat-server.md 18.15, 18.15.1, 18.17): stock's phase 1, the reform, for every fight kind.
    // A participant (the attacker or the defender) whose members' ReformBeforeCombat (CombatMotion::reform) has its side's
    // bit (1 the attacker, 2 the defender) lays its formation out again around its own centre (CombatUnitFigures::centre,
    // else its figures' centroid), turned from its heading (facing_yaw) to face the opponent's centre; each standing
    // figure takes the nearest new slot (greedy, nearest pair first) and runs there at the 4776 motion (1400 run, or the
    // 1450 shuffle within ShortMoveRadius, x 64 world units per second; FigState::Reform), then plays its run stop (1440)
    // and stands (1000). No figure of either side looks, approaches or strikes (a city's volley waits too) until every
    // reforming figure has arrived and stopped: stock's phase ends when every living figure of both sides stands [C 0321
    // read, 18.15]; stock re-lays in place, not towards the opponent [L 18.15.1]. A reformed unit's figures face the
    // opponent and take no turn_first turn. Needs positions and the unit's heading (combatPositions); without them no
    // reform. [I]: the new layout is the figures' own start slots turned about the centre (the formation for the living
    // count is not re-derived); a figure nearer its slot than reform_min_move stands. Off: no reform (the rule before
    // 4745). A comparison knob.
    bool reform_first = true;
    float reform_min_move = 1.0f;    // world units
    // 4742 (W54; unit-combat-server.md 18.18): stock's walk home inside the fight. A surviving figure that finishes
    // without victory, of a placed participant (not an aircraft: 4777's flight home), more than walk_home_min_move from
    // its home runs there inside the fight: Done (0xB) with 1400 (the 1450 shuffle inside ShortMoveRadius, at the 4776
    // motion), then Reface (7) with 1000 at its arrival; the fight ends at the last arrival, so the run's loops are sent
    // before the fight's OwnerEnd and stop at it (D210). Stock walks each figure to its own home slot and refaces there,
    // the simulation ends just after the last arrival [C 0xB's end handler, L 18.15.1]; the victory branch does not walk
    // [C]. [I]: the home is the start slot of the survivor's rank (rank i of the participant's survivors in figure order
    // -> the start position of figure i: the slot the post-fight placement draws it in, as FightMotion's walk after the
    // end ranks them), not the figure's own slot (stock closes that gap outside the fight); the limit cuts a walk (the
    // after-end walk then finishes it). Off: Done with 1000 at once, the walk after the fight's end (the rule before
    // 4742). A comparison knob.
    bool walk_home = true;
    float walk_home_min_move = 1.0f;   // world units
    // 4748 (W59; unit-combat-server.md 18.21, MC #10313): a melee attack on a city strikes from where the attacking
    // figures stand. Stock's fight on a city (type 0: phases 1, 2, 4) moves no attacking figure: every rider of W52's five
    // recorded Lancer attacks on a city struck where it stood, first strike 0.03-0.83 s, whole 2.57-3.95 s [L 18.21]. So
    // an attacking figure (not an aircraft) whose target part is a city takes no approach: no run, no run charge, no move;
    // it is engaged and enters Ready (its 1600 entry, then the strike). Before 4748 it ran (or, a rush member, charged) to
    // within city_reach of the city's centre, played its run stop (1440) and its 1600 entry, and only then struck: the
    // Lancer fights ended 5.93-6.70 s (1.65x stock). Off: that approach (the rule before 4748). A comparison knob.
    bool city_in_place = true;
    // 4746 (W57; unit-combat-server.md 10.12; research/02-exe-internals/exe-audio.md 13.3 [C], the state setter): stock
    // requests combat ready (1600) for a waiting figure (state 2) only when its current request, or with none its last
    // action, is a movement (1400 run, 1450 shuffle, 1120 run charge), and then 1600 when its member enables Walk (1450),
    // else 1000; a figure that was not moving gets no new request (an idle defender waiting for its turn stays in its
    // idle clip). State 5 (ready to strike) always requests 1600, unchanged here. Before 4746 every figure put into Wait
    // requested 1600 (ready()), so route S's waiting defenders and attackers sent the 1600 clip's tier -1 FOOT_ALWAYS and
    // FORTIFY_VOX (W44's Tiny pair: about 10 fortify vox and 10 foot dirt per land fight, none on stock [L]). Applies to a
    // figure's first combat-ready entry only (a figure already combat ready after a strike keeps 1600 as its state's
    // request without a new one, as before). A waiting figure that then gets a target enters Ready with 1600 and strikes
    // when that entry is done (0321 5.3). Off: every waiting figure requests 1600 (the rule before 4746). A comparison knob.
    bool ready_after_move = true;
    // 4750 (W61; unit-combat-server.md 10.14): stock's phase-2 stance of a non-answering defender. Stock puts every figure
    // of a defender that does not answer (and every figure that cannot attack) into 0xE with 1280 at phase 5's start, as
    // route S does [C 18.14 / 18.15; L W52's 401 figures]; but its figures meet that request after phase 2's 0xF, which
    // requests 1450 (shuffle) for about 0.47 s [L 18.15.1]: the machine is then in its run loop (or combat ready), and
    // the 1280 request takes the machine's path-6 = 1 branch, the 1285 bombard-defend loop, which has no Sound trigger;
    // from idle (path 6 = 0) it plays the 1280 clip and its tier -1 FOOT_ALWAYS x2 and FORTIFY_VOX per figure (~32
    // one-shots per ranged fight offline, 10.12; none on stock's Tiny warrior [L 15.12]). So a defender figure that does
    // not answer or cannot attack, of a land member that enables Walk and BombardDefend and has a 1450 clip, whose last
    // request is not a movement already (a turn_first turn leaves the machine running), in a fight whose attacker is not
    // an aircraft (type 5's phase 2 not read), first requests 1450 (ActionKind::Walk) and defend_stance_s later 1280
    // (at least 0.15 s: the machine's start-run time). Ships (no Walk; their machines take no 1450 from idle), members
    // without Walk and air fights keep 1280 from idle, as stock's do as far as read. Off: 1280 at once (the rule before
    // 4750). A comparison knob.
    bool defend_from_stance = true;
    double defend_stance_s = 0.45;   // seconds (W52's ranged type-1 phase 2: 0.468 / 0.477 s [L 18.15.1])
    // 5050 (W65; unit-combat-server.md 18.22, 18.23) (a): stock's phase 1 ends at the LAST ARRIVAL of the reforming
    // figures (each goes 0xD -> 3 on arrival and the phase map moves on when every living figure stands in 3; the run stop
    // 1440 is the machine's own and is not waited for [C rule, L W52: phase 2 starts in the pass of the last 0xD exit]).
    // On: reform_first's phase 1 ends at the last arrival (the figures stand and request 1000 there, as before). Off:
    // it ends at the last arrival plus that figure's 1440 clip (the rule of 4745, before 5050). No effect without a
    // reform. A comparison knob; ON by default since ticket 5050 W74 (2026-11-22, MC #10667 / #10726, after W70's live
    // session, unit-combat-server.md 18.23.1, 18.23.2); off is the rule before 5050.
    bool reform_ends_at_arrival = true;
    // 5050 (W65; unit-combat-server.md 10.14 section 4, 18.23) (b): stock's phase 2 for the attacker. Stock's attacking
    // figures stand in 0xF, which requests 1450 (shuffle), for about 0.45 s before phase 5 (0.468 / 0.477 s ranged,
    // 0.406 s melee type 1 [L 18.15.1]); state 3 then requests 1600 (with Walk), which meets the machine in its run loop
    // with the shuffle selected: the stop-shuffle state (26, about 0.15 s) takes it to the combat-ready loop with path 6
    // = 1, without the enter-combat-ready clip (1600 with FOOT_ALWAYS / FORTIFY_VOX) [C census, 10.14]. On: an attacking
    // figure's first entry into Ready (5), when it has not moved (its last request is not 1400, 1450 or 1120), of a land
    // member that enables Walk and CombatReady and has a 1450 clip, in a fight whose attacker is not an aircraft, first
    // requests 1450 (FigState::Turn, ActionKind::Walk) for attack_stance_s, then 1600 and strikes attack_ready_s after it
    // (self-timed; live: when the controller says the machine takes the strike). Skips as 4750's: a figure already moving
    // (an approach, a charge, a turn_first turn), air, ships. A land member WITHOUT Walk or a 1450 clip (the Gatling gun,
    // cannon, artillery, machine gun crews: no Walk in VP's art [D census]) takes stock's phase 2 as a hold instead when
    // attack_hold_without_walk: FigState::Turn with no request for attack_stance_s, then 1600 from idle with its whole
    // entry clip before the strike (no live shortcut), as stock's crew plays the entry and then fires (W64's live S20:
    // stock's Gatling fires at 2.68 s after the combat-ready entry's sounds, route S at 1.47 s) [I for the hold: stock's
    // 0xF requests 1450, which such a member has no clip for]. Off: Ready requests 1600 at once and may strike when the
    // machine takes it (the rule before 5050). A comparison knob; ON by default since ticket 5050 W74 (2026-11-22, MC
    // #10667 / #10726, unit-combat-server.md 18.23.2).
    bool attack_from_stance = true;
    double attack_stance_s = 0.45;   // seconds (stock's phase 2, as defend_stance_s; at least 0.15: the start-run time)
    double attack_ready_s = 0.15;    // seconds: the stop-shuffle state's time to combat ready (10.14 [C census])
    bool attack_hold_without_walk = true;   // with attack_from_stance: the hold and the full entry for a member without Walk
    // 5074 (W162; unit-combat-server.md 18.26.4): stock's crew entry after a turn. Stock's attacking figures play phase 2
    // as 0xF (the run clip's sounds for a Gatling crew, 0.59-0.83 s [L W52's sampler]); a member without Walk then takes
    // state 3's 1000 (4746's rule), so its machine plays the run stop (1440) and goes idle, and the strike (10, 1180) from
    // idle enters combat ready through the whole 1600 entry clip (the Gatling's riflecock and fortify vox) before the
    // attack [L W158's stock sounds, 18.26.2]. Route S's turn_first turn is that 0xF; after it, ready() sent 1600 straight
    // to the run loop (5050 (b) skips a figure that moved), so the machine went to combat ready without the entry and the
    // crew struck 1.0 s after the turn, about 1.2 s before stock. On: at the end of a turn_first turn, an attacking
    // figure of a land member without Walk or a 1450 clip (attack_hold_without_walk's class), with CombatReady and a 1440
    // clip, of a fight whose attacker is not an aircraft, keeps its run in place until attack_turn_stance_s after the
    // turn's start when the turn was shorter (stock's 0xF), then requests 1000 and stands until attack_turn_stop_s or
    // its 1440 clip, whichever is longer; then it looks again and requests 1600 from idle, striking only after the whole
    // entry clip (no live shortcut). Off: 1600 at the turn's end (the rule before 5074), byte for byte. A parity fix, on
    // by default (D255: no player key).
    bool attack_entry_after_turn = true;
    // seconds from the turn's start to the stop: stock's crew runs 0.454-0.489 s in 0xF before its 1.0 s run stop
    // (W158's four stock Gatling fights: the run clip starts 0.096 s after Run, the entry clip at 1.550-1.585 s
    // [L 18.26.4]) less the 0.064 s by which route S's crew starts its run later after Run (its run triggers at 0.241 s
    // against stock's 0.177)
    double attack_turn_stance_s = 0.40;
    double attack_turn_stop_s = 0.0;   // seconds: the stand after the turn at least this long (0: the 1440 clip alone)
    // 5057 (W85, design W80 #11160, MC #11162; unit-combat-server.md 18.29, 18.29.1) (A): stock's riders strike as they
    // ride through. Stock's knights kill each Spearman figure as a rider reaches it in the run charge, ride on in the
    // 1120 loop and never play the run stop (1440) or the combat-ready entry (1600) before their hits [L 18.29]. On: a
    // rush member (RushAttackFormation) without AttackCharge whose charge (1120) ends at a target figure lands its hit at
    // the arrival (a landing with no attack clip, the 1120 left playing; the hit's code is 1120); it then takes its next
    // target: one ahead of it within its ShortMoveRadius of its line [I] it rides on to in the same 1120 (a move with no
    // new request) and lands on arrival again; else it stops (1000 from the run: the machine's run stop 1440, then idle)
    // and stands without a 1600 request, looking again after the 1440 clip. A re-charge lands on its arrival too. A
    // standing rider in reach of its target strikes 1100 at once only when the controller says its machine takes it
    // (live), else it enters combat ready (1600) first as before. [I, open until the live pair]: whether stock's hit is a
    // landing at arrival or a 1140 played from the run (the sounds fit the first, 18.29); the 1140 form would change only
    // Sim::rideLand (18.29.1). Off: today's route S (every rider stops, enters 1600, strikes 1100), byte for byte.
    bool rush_ride_through = true;
    // 5057 (B) (18.15 phase 3 [C], 18.29): in stock's fight type 2 (an attacker with no ranged member against a defender
    // with none: no Part::can_answer side, no air, a melee attack on a unit) the defender's figures do not strike before
    // the attacker's first landing (phase 3 ends there); they wait (no request) and look from that landing on. The wait
    // also ends when no attacking figure can still land (none approaching, charging, ready or striking). Off: the defender
    // strikes as soon as an attacker is engaged (today's route S), byte for byte.
    bool defender_waits_first_landing = true;
    // 5053 (W112, design W72; unit-combat-server.md 18.26 (d)) R2: a dying figure lasts the death clip its machine
    // plays. Stock's machines draw death A (1200) or B (2200) 50/50 (unit-controller.md 6.5) and a dying figure lasts
    // the clip drawn, up to death_cap [L 18.26 (b) R2]. On, with the controller's feedback (live, trigger_scene): the
    // first step the figure's machine plays ec 2200 (CombatFeedback::playingVariant) sets its removal to the dying start
    // plus min(death_cap, the member's 2200 clip); on 1200 it stays at A's length. Self-timed (no feedback) the core
    // draws A or B 50/50 at die() from a generator of its own (the fight's other draws are unchanged). Off: every
    // dying figure is removed after its member's 1200 clip (the rule before 5053), byte for byte. A comparison knob; on
    // by default (the MC's word, 2026-11-25: a parity fix back to stock's fight-end timing).
    bool death_by_variant = true;
    // 5053 R1 + R3: stock holds a fight's standing figures until every dying figure is removed (state 0x10), then
    // ends them; a non-answering land target leaves its bombard-defend stance with one shuffle [L 18.26 (b)]. On: the
    // end test also holds while any figure of the fight is Dying (the attacker's figures are not held: R4), and a
    // finished defender figure in Defend (1280) of a land member with Walk, BombardDefend and a 1450 clip (4750's test)
    // requests 1450 (FigState::Done, ActionKind::Walk) for end_stance_s, then refaces (Reface, 1000) before it counts
    // as ended; without Walk it ends at once. Off: the standing figures end in the step of the last landing (the rule
    // before 5053), byte for byte. A comparison knob; on by default (the MC's word, 2026-11-25, as death_by_variant).
    bool end_after_dying = true;
    double end_stance_s = 0.45;      // seconds: R3's 0x10 -> 0xB -> 7 (0.485 [0.070] s, n 26 [L 18.26 (b)])
    // 5059 (W100; design W96, unit-combat-server.md 18.36; lead-0042 GO #11696 / #11703): stock takes victory (0x12, 1800)
    // only for a member whose EnableActions carry the word Victory (the EXE's enable mask has it as bit 13; 0 of VP's 364
    // ArtDefine_UnitMemberCombats rows carry it), so stock plays no victory clip and no victory vox in VP [L 18.36: no entry
    // into 0x12 in two live samplers, no victory vox in any stored stock Miles arm; I: that 5.3's "1800 enabled" test reads
    // that bit]. On: victoryEligible also needs the member's enable word "Victory" (CombatMemberArt::enable); a survivor
    // without it ends Done with no victory draw. Off: eligibility by the machine's 1800 alone (CombatMemberArt::victory),
    // the rule before 5059, byte for byte.
    bool victory_needs_word = true;
    // 5067 S1 (W122, lead-0042 #12649 / #12652; unit-combat-server.md 18.29.3): stock's phase 2 of a type 2 fight (the
    // fights 5057 (B) calls type 2). Stock starts the attacker's charge (phase 3) only after phase 2: in W91's 24 sampled
    // type 2 fights phase 2 lasts 0.665 s [0.648-0.667] whenever phase 1 ended at once (22 of 24), so the charge starts
    // 0.699 s after the simulation in every Knight fight [L, 18.33's sampler re-read in 18.29.3]; the defender's figures
    // stand in 0xF meanwhile (W52's one type 2 sample [L]). On: in type 2, phase 2 starts when phase 1 ends (the reform's
    // end, or the first step) and lasts type2_phase2_s; no figure looks before its end; each defender figure of a land
    // member with Walk and a 1450 clip requests 1450 (stock's 0xF) at its start, the attacker's figures stand with no
    // request ("waits in 3"). [I]: whether stock's phase 2 is a fixed time or its slowest figure's turn (after
    // sp_warrior's 0.73 s reform it lasted 0.066 s; this rule gives 0.665 s there). Off: no phase 2 in a charge fight
    // (route S before 5067), byte for byte. Off by default until the MC's word.
    bool type2_phase2 = false;
    double type2_phase2_s = 0.665;   // seconds [L W91 n 22: 0.648-0.667]
    // 5071 (unit-combat-server.md 18.46): with rush_ride_through, stock's riders strike again from their run. Stock's ride
    // pays only part of the defender's planned deaths (4 of 5, 6 of 7 [L W91's sampler]); every knight attack vox comes
    // after the ride with a sword swing and no run stop (1440) or combat-ready entry (1600) sound [L W101 S34, W79 S28].
    // Stock's strike state asks 1140 when the last action was 1120 and the member enables RunCharge (0321 5.2 state 10
    // [C]); the mounted machine takes 1140 straight from its run loop (17 -> 14, no stop) while 1100 from the run goes
    // through the whole 1440 stop and the 1600 entry [C: the machine as read, unit-controller.md 6.2]; the knight's 1140
    // clip carries the attack vox and the swing [C: its trigger file]. Under 5057 alone the ride landings paid every
    // planned death, the riders found no open need and stood: 0 rider attack vox (W147, 18.36.4). On: (1) a ride landing
    // never pays the defender's last planned death (nor do its extra struck figures): the rider whose arrival would pay
    // it strikes its reserved target with 1140 from its run instead; (2) a rider standing after its ride while the
    // defender still has a planned death or an owed hit open strikes the nearest standing defender figure with no
    // reservation (1140 from its run, riding on in its 1120 when out of reach; 1100 from combat ready after a strike);
    // a landing kills only a planned death (0321 13.4), so the deaths are those of 5057. Live, a run strike is asked
    // only when the controller's machine takes it (charge_wait at most at an arrival, then 5057's landing). Only rush
    // riders after a ride-through with a 1140 clip: every other fight is byte for byte the same. Off: 5057's rule.
    bool ride_strikes_while_held = true;
    // 5072 (W165; 18.29.3's T1, unit-combat-server.md 18.48): stock ends no rider of a type 2 fight while a figure of
    // the fight is dying. In W91's 24 type 2 fights the attacker's first release (0xB or 7) comes 0.020 s [0.000-0.034]
    // after the last dying figure's death is done [L 18.29.3 (1)], so the riders walk home after it, the fight ends at
    // their arrival and their run stop (1440, HORSE_FOOT) plays after End [L W151's stock arm: End + 0.15 s]. 5053's R1
    // hold (end_after_dying) leaves the attacker out (R4, read from the Gatling's crew), so route S's riders walked home
    // while the last figure fell and played that run stop at home 0.7-1.6 s before End [L W151's 5071 arms, 18.46]. On:
    // in a type 2 fight (the fights 5057 (B) calls type 2) the attacker's standing figures of a rush member are held
    // too while any figure of the fight is Dying (they keep their state and request, stock's 0x10) and are finished
    // (victory draw, walk home) in the step the last dying figure is removed; a rider still charging then rides on to
    // its arrival. While held, a rider stands: it neither looks nor strikes (stock's riders stand in 0x10 [L W91's
    // sampler, 18.29.3 (1)]). A rider home from its walk before the fight's end waits there
    // without a request and refaces (1000) in the fight's last step, so no run stop at home comes before the end (stock
    // [L]: W151's stock arms hear none before End although W91's sampler has riders reach 7 0.1-0.7 s before the
    // removal; stock's reason is open, 18.48 [I]). Foot attackers keep R4 (stock ends the simulation before their walk home ends, 18.29.3 T1 [I]). Off:
    // the rule before 5072 (riders finished in the step of the last landing), byte for byte. A parity fix; on by default.
    bool type2_end_after_dying = true;
    // 0743 (W137; unit-combat-server.md 18.44): stock's melee reach. Stock's target step (old build [C]) sends a melee
    // figure (no HasShortRangedAttack) whose target is a figure to strike (state 10) once its distance to that figure is
    // within the striker's AttackRadius plus the target's AttackRadius (8 + 8 = 16 world units by default), and keeps it
    // charging (0xC, 1120) only while it is farther; W91's live stock type 2 fights end their foot charges in a strike
    // 12-16 units from the nearest defender [L 18.44]. On: an attacking land figure of a member without
    // RushAttackFormation that approaches a target figure stops at that reach instead of MoveRadius (3.5), and takes as
    // its target the nearest of its candidates (stock's melee chooser 0x6F8DC0 [C]: the nearest opponent figure not yet
    // claimed, then the claimed ones; here among the figures the death rule allows, least struck first) instead of a
    // random one, so it strikes from where stock's does and no longer runs into the defender's formation (the owner's beta 4 report: melee
    // attackers move much further forward than in stock). Rush members (riders: stock sends them to the target's slots
    // and they ride through, 5057 (A)), aircraft, ships and city targets keep the rule before 0743. Off: byte for byte
    // the rule before 0743 (every approach ends MoveRadius short of its target figure). A comparison knob; on by default.
    bool melee_reach = true;
    // 5061 (W139, lead-0042 #13806; unit-combat-server.md 18.45): stock's phase 4 of a type 2 fight for the defender. From
    // the attacker's first landing on, stock's target step treats the defender's figures as the attacker's: W91's live
    // foot fights [L, re-read per figure in 18.45] send 8-9 of 12 defender figures into the run charge (0xC) 0.067 s after
    // the first landing, 109 charge spans in 12 fights, each ending 16.8 units (median) from the nearest attacker figure,
    // the two AttackRadius values of 0743's reach; figures already within it strike where they stand; a rush defender
    // does not step. On: in a type 2 fight (the fights 5057 (B) calls type 2), from the attacker's first landing, a
    // defender figure that answers, of a land member without RushAttackFormation (not a ship, not a flyer), with both
    // sides placed, (a) takes the nearest of its candidates as its target and runs to it as an attacker does under
    // melee_reach (stopping within the two AttackRadius values of the first standing attacker figure it meets, the
    // attacker's clip rule), then strikes; (b) without a strike to hold, it steps to that reach of the nearest standing
    // attacker figure that has made its approach and is not walking home, and waits there (combat ready), again whenever
    // that figure is out of reach by more than defender_step_slack, while a strike is still due. Off: the defender
    // strikes from where it stands, byte for byte the rule before 5061.
    bool defender_step = true;
    float defender_step_slack = 1.0f;   // world units [I: keeps a stepped figure from shuffling at its reach's edge]
};

// ---------------------------------------------------------------------------------------------- the output
enum class CombatCode : uint8_t {
    Ok = 0,
    QuickCombat,      // the record says Quick Combat: no choreography; the damage and deaths show at once (0321 7)
    NotVisualised,    // the visualise flag is clear: the EXE starts no simulation (0321 4.1, 7)
    BadInput,         // a field out of range or the figures do not match the record (text names it)
    NoFigures,        // a unit side has no figure standing at the start
    // 4777: the next four are no longer returned (the cases are played); kept so that callers' names and texts stay valid
    CityAttacker,     // a city attacks (InitCityRangeStrike)
    AirCombat,        // an air figure, a bombing run or an air sweep
    Nuclear,          // nuclear_level > 0
    AreaMembers,      // damage members (an area attack, a garrison bystander)
    UnknownKind,      // a record kind other than 25 / 28
};
const char* combatCodeName(CombatCode c);

struct CombatStatus {
    CombatCode code = CombatCode::Ok;
    std::string text;
    bool ok() const { return code == CombatCode::Ok; }
};

// The choreography's figure states (0321 5.2; the numbers are the EXE's state numbers).
enum class FigState : uint8_t {
    Look = 1,          // 0/1: idle, looking for a target
    Wait = 2,          // waiting (1000 idle or 1600 combat ready)
    Approach = 3,      // approaching (1400 run)
    Ready = 5,         // ready to strike (1600)
    Reface = 7,        // after combat (1000 idle); 4742: a figure home from its walk (CombatParams::walk_home)
    Dying = 8,         // 1200
    Removed = 9,       // the dying figure is gone (reported dead)
    Strike = 10,       // the attack action
    Done = 11,         // 0xB: finished, refaced; 4742: with 1400, walking home inside the fight (walk_home)
    Charge = 12,       // 0xC: run charge (1120)
    Defend = 14,       // 0xE: bombard defend (1280): cannot attack, or does not answer
    Reform = 13,       // 0xD: running to its re-laid slot before the fight (4745: 1400; CombatParams::reform_first)
    Turn = 15,         // 0xF: turning to face the opponent before the fight (4735: 1400 while it turns)
    Victory = 18,      // 0x12: 1800
};
const char* figStateName(FigState s);

struct FigureStep {
    double time = 0;                 // seconds from the combat's start
    int32_t player = -1, id = -1;
    uint16_t figure = 0;
    FigState state = FigState::Look;
    int32_t code = 0;                // the action requested on entering the state (0 = keeps the current one)
    int32_t target_player = -1, target_id = -1, target_figure = -1;   // -1: none; a city target: figure -1
    uint8_t approach = 0;            // 1 run, 2 run charge (the motion is 4776's)
};

enum class HitResult : uint8_t { Death, OwedHit, Spent };
struct CombatHit {
    double time = 0;                 // the hit lands (the trigger, or the trigger plus the flight)
    double launch = 0;               // the trigger's time
    int32_t player = -1, id = -1;    // the striker
    uint16_t figure = 0;
    int32_t code = 0;                // the striker's action code (its clip's ec); 0 for a city's (no clip)
    int32_t trigger = -1;            // the trigger's id in the member's trigger file (-1: not known)
    int32_t target_player = -1, target_id = -1, target_figure = -1;
    bool projectile = false;
    HitResult result = HitResult::Spent;
    // 4777 (additive): what the hit shows and sounds, for the trigger stream (4795; names as vp_trigger.h's records take
    // them: an effect and a sound script by name, a position, a scale). A figure's own clip triggers stay the
    // controller's; these name what no clip carries: the weapon's HitEffect at the landing, a city's strike from the
    // city's position, a nuke's blast, an area member's share. Names are the database's, read at run time ("" = none).
    std::string effect;              // the striker's weapon's HitEffect ($(TERRAIN) unresolved), or the city's
    std::string sound;               // the weapon's sound tag (WeaponTypeSoundOverrideTag, else WeaponTypeTag)
    float scale = 0;                 // HitEffectScale; 0 = unset: the striker's member scale (the router fills it)
    bool area = false;               // an area hit: a nuke's blast on its members, a garrison's share of a city's hit
    bool city = false;               // the striker is a city (no figure: `figure` 0, `code` 0)
    bool has_pos = false;            // pos: where it lands (world units, z up); false when no positions were given
    float pos[3] = {0, 0, 0};
    bool has_origin = false;         // origin: where it leaves (the striking figure, or the city's centre)
    float origin[3] = {0, 0, 0};
    int32_t shot = -1;               // 0742 (additive): a city volley's shot, its index in CombatResult::launches; -1 else
};

// A controller event at its time (Action with its figure set, Kill).
struct TimedEvent {
    double time = 0;
    TimelineEvent ev;
};

struct CombatNote {
    double time = 0;
    std::string text;                // "MissingClip ...", "NoHitTrigger ...", "Naval ..."
};

struct SideOutcome {
    int32_t player = -1, id = -1;    // the unit, or (owner, -1) for a city
    bool city = false, killed = false;
    uint32_t standing = 0;           // figures standing at the start
    uint32_t planned = 0, died = 0;  // deaths the damage calls for, deaths played
    uint32_t owed = 0, paid = 0;     // owed hits (0321 13.4), paid
    int32_t protected_figure = -1;   // the survivor no chooser takes while another figure stands
    bool figures = true;             // 4777 (additive): false when the participant took part without figures (a city,
                                     // the nuke's plot, a damage member whose figures were not given)
};

struct CombatResult {
    CombatStatus status;
    double end = 0;                  // seconds: every figure finished, or the limit
    double limit = 0;
    bool at_limit = false;
    SideOutcome attacker, defender;
    std::vector<FigureStep> steps;   // in time order, then participant, then figure
    std::vector<CombatHit> hits;     // in time order
    // 0742 (additive): a city volley's shots at their launch (one per shot, in launch order; `time` = its planned
    // landing, `pos` = the aimed figure at the launch, `shot` = the index here); each landing's own CombatHit follows
    // in `hits` with the same `shot`. The trigger stream flies the projectile from these (unit-combat-server.md 18.43)
    std::vector<CombatHit> launches;
    std::vector<TimedEvent> events;  // in time order; same time: in the order made
    std::vector<CombatNote> notes;
    std::vector<FigureMove> moves;   // 4776: every figure's moves in time order (only with positions given; 4777: an
                                     // aircraft's flights out and back always, timed by CombatParams::air_flight
                                     // without positions)
    std::vector<SideOutcome> others; // 4777: the interceptor (when named), then the damage members in input order
};

// Runs one combat. `attacker` / `defender` are the units' figures (ignored for a city side; pass an empty one; 4776: a
// city or plot side's centre is read). The result's status names a refusal; on a refusal nothing else is filled. (= a
// CombatRun stepped to its end, self-timed)
CombatResult choreograph(const CombatInput& in, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                         CombatClips& clips, const CombatParams& params);
// 4777 (additive): the same with the figures of the other participants (the interceptor, the damage members that are
// units), found by (player, id); a participant whose figures are not in `others` takes part without figures.
CombatResult choreograph(const CombatInput& in, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                         const std::vector<CombatUnitFigures>& others, CombatClips& clips, const CombatParams& params);

// 4782 (additive): the live feedback a CombatRun asks for (Controller's ControllerFeedback, controller.h).
class CombatFeedback {
public:
    virtual ~CombatFeedback() = default;
    // True when the figure's machine would take a path-4 request of `code` now (its current state reads it).
    virtual bool accepts(int32_t player, int32_t id, uint16_t figure, int32_t code) = 0;
    // True when the figure plays the clip of `code` (or its B variant, code + 1000) now.
    virtual bool playing(int32_t player, int32_t id, uint16_t figure, int32_t code) = 0;
    // 5053 (additive): which variant of `code` the figure plays now: `code` (A), `code + 1000` (B) or 0 (neither, or not
    // known). Unlike playing(), a dead figure (it plays its death) is answered. Default 0: a feedback without it
    // leaves CombatParams::death_by_variant at A's length.
    virtual int32_t playingVariant(int32_t player, int32_t id, uint16_t figure, int32_t code) {
        (void)player;
        (void)id;
        (void)figure;
        (void)code;
        return 0;
    }
};

// 4782 (additive): one combat stepped with a caller's clock (the server's fight clock, srv_units_fights.h). With a
// feedback, a figure in Ready sends its attack request when its machine takes it now or its combat-ready entry is over
// (whichever first), and its strike's clock (the hit moments, the strike's end) starts only when the controller's figure
// plays the attack clip: the EXE's simulation waits for the model's animation the same way (0321 4.2 slots 3-4, 5.3); a
// clip that never plays holds the fight to its limit. Everything else is the self-timed rule of choreograph(). Each step appends the controller events it made (time = the step's time); the
// result grows as it runs and is final when step() / advance() returns true.
class CombatRun {
public:
    CombatRun();
    ~CombatRun();
    CombatRun(const CombatRun&) = delete;
    CombatRun& operator=(const CombatRun&) = delete;
    // The checks and refusals of choreograph(). `clips` and `fb` (may be null) must outlive the run.
    CombatStatus start(const CombatInput& in, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                       CombatClips& clips, const CombatParams& params, CombatFeedback* fb);
    // 4777 (additive): with the other participants' figures (as choreograph()'s overload).
    CombatStatus start(const CombatInput& in, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                       const std::vector<CombatUnitFigures>& others, CombatClips& clips, const CombatParams& params,
                       CombatFeedback* fb);
    bool step(std::vector<TimedEvent>& out);                // one step of params.step; true when ended
    bool advance(double dt, std::vector<TimedEvent>& out);  // every step the caller's clock (sum of dt) has reached
    bool done() const;                                      // ended, or never started (a refusal)
    const CombatResult& result() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

// ---------------------------------------------------------------------------------------------- the art tables
// What the database gives a member (ArtDefine_UnitMemberCombats, ArtDefine_UnitMemberCombatWeapons; 0321 6).
struct CombatArtRow {
    std::set<std::string> enable;    // EnableActions, split at spaces
    bool rush = false;               // RushAttackFormation not empty
    bool last_to_die = false;        // LastToDie = 1
    bool ranged = false;             // HasShortRangedAttack or HasLongRangedAttack = 1 (the weapon of Index 0 is a
                                     // projectile weapon) [I: the weapon record's kind 1 is not a column]
    CombatMotion motion;             // 4776: MoveRadius, ShortMoveRadius, MoveRate, ShortMoveRate, TurnRateMin / Max,
                                     // ReformBeforeCombat with the EXE's defaults (CombatMotion); 4777: AttackAltitude
    bool collision = false;          // 4777: HasCollisionAttack (0321 5.3, M6): every weapon of the member is one-shot
    std::vector<CombatWeapon> weapons;   // by Index; Index >= 1 with WeaponTypeTag FLAMING_ARROW is the Vs_City one
                                         // [I: Usage is in the templates, not the table]; flight 0 (the caller's)
};
using CombatArtTables = std::map<std::string, CombatArtRow>;   // by UnitMemberType
// Reads both tables. False with `err` naming the table or column when one is missing; rows of a member without a
// Combats row are kept with the weapons only.
bool loadCombatTables(c5m::TableSource& db, CombatArtTables& out, std::string& err);
// 4783 (W33, C1; MC #8906): what the load found about the figures a landing strikes. vis_kill_columns: the database
// has ArtDefine_UnitMemberCombatWeapons.VisKillStrengthMin / Max (C1's rule); else every landing strikes one figure.
struct CombatTablesInfo {
    bool vis_kill_columns = false;
};
bool loadCombatTables(c5m::TableSource& db, CombatArtTables& out, std::string& err, CombatTablesInfo* info);
// The rule in use, as the server's log names it once per load: "VisKillStrength per landing" or "one figure per landing
// (database has no VisKillStrength columns)".
std::string combatStrikeRule(const CombatTablesInfo& info);

// The events of `r` as timeline frames of length `dt` (the first frame at t = 0 holds the events at time < dt, ...),
// numbered from `first_seq`, through max(r.end, last event) plus one frame. Empty for a refused result.
std::vector<TimelineFrame> combatFrames(const CombatResult& r, double dt, uint64_t first_seq);

}}  // namespace c5r::unitanim
