// c5r/unitanim/bridge.h - the event bridge: timeline events to path-4 requests per figure (epic 0374, issue 4404 part
// B; research/05-graphics-composition/unit-controller.md 1, 6.2, 6.5 and 14).
//
// Path 4 is a level (6.2): the bridge writes the request code and the machine clears it itself. Codes:
//   fortify 1 / 0 -> 1500 / 1000          move -> 1400 (the input has no shuffle field: 1450 is never requested)
//   arrive -> 1000                         destroyed -> 1200 (every living figure), kill -> 1200 (the set)
//   action attack 1100, attack_charge 1140, charge 1120, attack_city 1160, bombard 1180, anti_air 1130,
//          bombard_defend 1280, victory 1800, captured 1195, activate 1990,
//          run 1400, combat_ready 1600, idle 1000 (4775: the choreography's per-figure requests, checked against
//          EnableActions' Run, CombatReady, Idle),
//          work <build> -> the EXE's tool for the build (workTool, 0439; a caller's work table by
//          Builds.EntityEvent replaces it), work with no build (the stop) -> 1000,
//          paradrop -> BadEvent (no code in the files)
//   damage -> 1200 for the figures the 0x4B4B80 rule takes out (units::livingFigures on the previous and the new
//          damage; deaths from the last figure backwards), each on its own machine, marked dead (X4); removed
//          (remove_fallen); a fall in damage gives removed figures back (Controller, 0439)
//   turn, embark, garrison, visibility -> no request (the controller handles them; steer writes paths 7 and 8)
// choice, steer, reset, place, create, remove never reach the bridge (Controller::event handles them).
//
// EnableActions (ArtDefine_UnitMemberCombats, 6.5; 0439: not for fortify and work, not for a member without a row):
// a code with a word (Idle 1000, Attack 1100, RunCharge 1120,
// AttackSurfaceToAir 1130, AttackCharge 1140, AttackCity 1160, Bombard 1180, Death 1200, BombardDefend 1280, Run 1400,
// Walk 1450, Fortify 1500, CombatReady 1600) is BadEvent for a request that reaches a member whose words lack it (was:
// also a member with no Combats row; 0439 lifts that). Codes without a word (1800, 1195, 1990, work) meet only the
// controller's rule: a value the member's machine never reads is a no-op (0439; was BadEvent). Nothing throws.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/unitanim/controller.h"

namespace c5r { namespace unitanim {

// The database rows the bridge needs.
struct BridgeTables {
    std::map<std::string, std::set<std::string>> words;   // ArtDefine_UnitMemberCombats: UnitMemberType -> EnableActions
    std::map<std::string, std::string> build_event;       // Builds: Type -> EntityEvent ("" for NULL)
    std::map<std::string, int32_t> work_codes;            // EntityEvent -> path-4 code; empty = the EXE's rule (workTool)
};

// Reads ArtDefine_UnitMemberCombats (UnitMemberType, EnableActions) and Builds (Type, EntityEvent); EnableActions is
// split at spaces. BadLogicFile with the database's message when a table or column is missing. work_codes is left
// as it was (the files have no table).
AnimStatus loadBridgeTables(c5m::TableSource& db, BridgeTables& out);

// A work table as text: one "<EntityEvent><TAB><code>" per line, '#' comments, blank lines ignored; codes 1900-1999.
// BadLogicFile naming the line on anything else (a duplicate event included).
AnimStatus parseWorkTable(const std::string& text, std::map<std::string, int32_t>& out);

// The EnableActions word of a path-4 code, nullptr for a code with none (1800, 1195, 1990, 1910-1960, ...).
const char* actionWord(int32_t code);

// 0439: the work tool's path-4 code for a build Type, the EXE's rule (unit-controller.md 14.1, [C] by behaviour):
// farm, terrace farm, polder -> 1920 rake; mine, quarry -> 1940 pickaxe; road, railroad, scrub fallout, remove marsh,
// remove route -> 1910 shovel; remove forest, remove jungle -> 1930 axe; anything else -> 1950 hammer. An `action work`
// with an empty argument is the work stop (build -1) -> 1000 (the EXE posts IDLE). A caller's work table
// (BridgeTables::work_codes, non-empty) replaces this rule. Fortify and work requests are not checked against
// EnableActions (the EXE posts them unchecked), nor is any request to a member with no Combats row.
int32_t workTool(const std::string& build);

class ActionBridge : public EventBridge {
public:
    explicit ActionBridge(const BridgeTables& t) : t_(t) {}
    // Without the unit's art the bridge cannot check members: BadEvent (the controller calls requestsFor).
    AnimStatus requests(const TimelineEvent& ev, const units::UnitInput& unit, std::vector<Request>& out) override;
    AnimStatus requestsFor(const TimelineEvent& ev, const units::UnitInput& unit, const units::UnitArt& art,
                           std::vector<Request>& out) override;
    // 4406 r3 (additive; premise change 5, accepted by the MC #2147, 2026-11-16): figures that fall from damage
    // outside a combat are removed at once with no death clip and no dead idle (4405 B's Q7 [L, one unit]). Default
    // true; false gives 4404's first reading of X4 (1200, then dead) for comparison only. Deaths in combat come
    // through the choreography's requests, not through this event.
    bool remove_fallen = true;

private:
    const BridgeTables& t_;
};

}}  // namespace c5r::unitanim
