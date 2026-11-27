// render/src/common/status.cpp (epic 2900)
#include "c5r/status.h"

namespace c5r {

const char* errName(Err e) {
    switch (e) {
    case Err::Ok: return "Ok";
    case Err::Io: return "Io";
    case Err::Truncated: return "Truncated";
    case Err::BadMagic: return "BadMagic";
    case Err::BadVersion: return "BadVersion";
    case Err::BadLayout: return "BadLayout";
    case Err::Unsupported: return "Unsupported";
    case Err::NotFound: return "NotFound";
    case Err::Device: return "Device";
    case Err::OutOfMemory: return "OutOfMemory";
    }
    return "?";
}

} // namespace c5r
