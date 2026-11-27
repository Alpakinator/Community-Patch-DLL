// c5r/units/unit_scene.h - the input of the renderer's unit module: one frame's unit list in renderer terms (epic 0041,
// C1 accepted by the MC 2026-11-15 14:08, queue #1845). Design, field meanings and the mapping from the unit feed
// contract: research/05-graphics-composition/unit-renderer.md sections 2-4. U6 (ticket 0375) fills it from the feed;
// the offline tool render/tools/unitscene reads it from a .c5units file (scene_file.h, 4355).
// Changing a field here is a contract change: ask the lead (and through it the MC) first.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace c5r { namespace units {

// UnitInput::flags bits (the contract's UnitRecord flags, unit-feed-contract.md 2.2, in that order).
enum UnitFlag : uint8_t {
    kFlagEmbarked = 1u << 0,
    kFlagGarrisoned = 1u << 1,
    kFlagFortified = 1u << 2,
    kFlagVisible = 1u << 3,       // visible to the active team
    kFlagInvisible = 1u << 4,
    kFlagDelayedDeath = 1u << 5,
    kFlagDead = 1u << 6,
};

struct PlayerInput {              // one per player that owns a unit in the scene
    int32_t player = -1;          // 0..63 (DLL PlayerTypes)
    std::string colour;           // PlayerColors.Type; the module takes its SecondaryColor (unit-draws.md 5)
    std::string art_style;        // the owner's ArtStyleType (cultural variation of the art row)
    std::string era;              // the owner's current era Type (era variation of the art row)
};

struct FigureInput {              // optional per figure; absent = the default clip at t 0 (unit-renderer.md 6.2)
    uint16_t figure = 0;          // index into the unit's figures in member order (unit-renderer.md 6.3)
    std::string clip;             // animation name as the member's .fxsxml names it (until U5)
    float t = 0;                  // clip time in seconds
};

struct UnitInput {
    int32_t player = -1;          // owner; (player, id) keys a unit
    int32_t id = -1;
    std::string unit_type;        // Units.Type
    std::string art_row;          // ArtDefine_UnitInfos.Type, or "" = resolve from unit_type and the owner
    int32_t plot_x = 0, plot_y = 0;
    bool has_root = false;        // true: root_* given (position source exe); false: the plot centre (source own)
    float root_x = 0, root_y = 0, root_z = 0;
    float foot_radius = 0;        // issue 4982 (FixUnitPlacement): PlacementInput::footRadius; 0 = the EXE's rule. Not in
                                  // the .c5units file (set by the server's unit extras each frame)
    int8_t facing = 0;           // DirectionTypes 0..5; yaw = 150 - 60 * facing degrees. -1..7 in the .c5units file
                                  // (issue 4464): 6, 7 (the fork's NO_DIRECTION) and -1 give 1 rad (facingYaw)
    uint32_t formation_seed = 0;  // the EXE's art record index (unit-formations.md 12.1)
    int16_t damage = 0, max_hp = 100;
    int16_t alive = -1;           // -1 = from damage (0x4B4B80 rule); else the number of living figures
    uint8_t flags = 0;            // UnitFlag bits
    float fade_from = 1, fade_to = 0, fade = 0;   // palette header row 1 x, y, z; (1, 0, 0) = opaque
    std::vector<FigureInput> figures;
};

struct UnitScene {
    uint32_t format = 1;
    std::string position_source = "own";   // "exe" | "own"
    float water_level = 0;                 // the height field's floor; also cb1[8].x
    std::vector<PlayerInput> players;
    std::vector<UnitInput> units;          // draw order = this order
};

}}  // namespace c5r::units
