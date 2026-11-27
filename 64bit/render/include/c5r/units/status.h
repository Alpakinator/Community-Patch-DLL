// c5r/units/status.h - the per-unit status of the unit module (epic 0041, U1 / issue 4352). One code per unit, the
// first that applies, with a text naming the row, file or mesh (research/05-graphics-composition/unit-renderer.md 7).
// A unit whose status is not Ok is not drawn at all: no placeholder, no partial unit (D118, D125, R6/D132).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace c5r { namespace units {

enum class UnitCode : uint8_t {
    Ok = 0,
    NoArtRow,          // no ArtDefine_UnitInfos row for the type and owner (unknown Units.Type included)
    NoMembers,         // an art row with no ArtDefine_UnitInfoMemberInfos rows, or a member info row missing
    ModelNotFound,     // the .fxsxml or the .gr2 in neither the install nor the active mods
    BadModel,          // the .gr2 or .fxsxml reader refused it, or a mesh the repack refuses
    TooManyBones,      // a mesh binding more than 32 bones (K8: none in world models)
    UnknownLayout,     // a mesh's vertex layout or shader set outside materials 87 / 89 / 90
    CurveUnsupported,  // a curve format the sampler does not decode (pose step, 4353)
    ClipNotFound,      // a clip name the member does not have (pose step, 4353)
    TextureNotFound,   // a texture the model names is in neither the install nor the mods, or does not decode
    ProgramNotFound,   // a unit program hash not in the EXE file
    PlacementFailed,   // a formation not in UnitFormations.xml, a plot outside the height field (placement, 4354)
    BadInput,          // the input itself: owner not in the scene's players, a colour Type not in PlayerColors,
                       // a figure index past the unit's figures (added by 4352, unit-renderer.md 10)
};
constexpr int kUnitCodeCount = 13;

const char* unitCodeName(UnitCode c);   // "Ok", "NoArtRow", ...

struct UnitStatus {
    UnitCode code = UnitCode::Ok;
    std::string text;
    // Named warnings of a unit that is drawn (code Ok): "TextureFallback: <gr2> <mesh>: <file>" when a texture the
    // material names is in no install or mod file and the EXE's own 8 x 8 fallback is bound (4356, unit-renderer.md
    // 10.8). The pre-check lists them; U6 logs them.
    std::vector<std::string> warnings;
    bool ok() const { return code == UnitCode::Ok; }
    static UnitStatus Ok() { return UnitStatus(); }
    static UnitStatus fail(UnitCode c, std::string t) { UnitStatus s; s.code = c; s.text = std::move(t); return s; }
    std::string str() const { return std::string(unitCodeName(code)) + (text.empty() ? "" : ": " + text); }
};

}}  // namespace c5r::units
