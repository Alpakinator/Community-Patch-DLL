// status.cpp - AnimCode names (epic 0374, issue 4403).
#include "c5r/unitanim/status.h"

namespace c5r { namespace unitanim {

const char* name(AnimCode c) {
    switch (c) {
    case AnimCode::Ok: return "Ok";
    case AnimCode::MachineNotFound: return "MachineNotFound";
    case AnimCode::GraphNotFound: return "GraphNotFound";
    case AnimCode::TriggersNotFound: return "TriggersNotFound";
    case AnimCode::BadLogicFile: return "BadLogicFile";
    case AnimCode::UnknownCondition: return "UnknownCondition";
    case AnimCode::UnknownNode: return "UnknownNode";
    case AnimCode::UnknownTrigger: return "UnknownTrigger";
    case AnimCode::EcNotBound: return "EcNotBound";
    case AnimCode::BadSocket: return "BadSocket";
    case AnimCode::NoProgress: return "NoProgress";
    case AnimCode::ForcedChoiceMissing: return "ForcedChoiceMissing";
    case AnimCode::BadEvent: return "BadEvent";
    case AnimCode::UnitFailed: return "UnitFailed";
    case AnimCode::TrackMaskNotBound: return "TrackMaskNotBound";
    }
    return "?";
}

}}  // namespace c5r::unitanim
