// status.cpp - names of the unit module's status codes and passes (epic 0041, issue 4352).
#include "c5r/units/status.h"
#include "c5r/units/draw.h"

namespace c5r { namespace units {

const char* unitCodeName(UnitCode c) {
    switch (c) {
    case UnitCode::Ok: return "Ok";
    case UnitCode::NoArtRow: return "NoArtRow";
    case UnitCode::NoMembers: return "NoMembers";
    case UnitCode::ModelNotFound: return "ModelNotFound";
    case UnitCode::BadModel: return "BadModel";
    case UnitCode::TooManyBones: return "TooManyBones";
    case UnitCode::UnknownLayout: return "UnknownLayout";
    case UnitCode::CurveUnsupported: return "CurveUnsupported";
    case UnitCode::ClipNotFound: return "ClipNotFound";
    case UnitCode::TextureNotFound: return "TextureNotFound";
    case UnitCode::ProgramNotFound: return "ProgramNotFound";
    case UnitCode::PlacementFailed: return "PlacementFailed";
    case UnitCode::BadInput: return "BadInput";
    }
    return "?";
}

const char* passName(PassKind k) {
    switch (k) {
    case PassKind::ShadowMask: return "mask";
    case PassKind::Reflection: return "reflection";
    case PassKind::Scene: return "scene";
    }
    return "?";
}

}}  // namespace c5r::units
