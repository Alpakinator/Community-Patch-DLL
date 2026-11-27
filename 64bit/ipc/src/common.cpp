// src/common.cpp - small shared definitions. Owner: epic 1200 coordinator.
#include <string.h>

#include "internal.h"

namespace vpipc {

const char* status_name(Status s) {
    switch (s) {
    case Status::Ok: return "Ok";
    case Status::RemoteError: return "RemoteError";
    case Status::PeerDied: return "PeerDied";
    case Status::PeerClosed: return "PeerClosed";
    case Status::PeerHung: return "PeerHung";
    case Status::Timeout: return "Timeout";
    case Status::Disconnected: return "Disconnected";
    case Status::VersionMismatch: return "VersionMismatch";
    case Status::LayoutMismatch: return "LayoutMismatch";
    case Status::SchemaMismatch: return "SchemaMismatch";
    case Status::ProtocolError: return "ProtocolError";
    case Status::PayloadTooLarge: return "PayloadTooLarge";
    case Status::SystemError: return "SystemError";
    case Status::InvalidArgument: return "InvalidArgument";
    }
    return "Unknown";
}

void default_channels(ChannelConfig* channels, uint32_t* count) {
    static const char* const kNames[3] = {"ui", "core", "aux"};
    for (uint32_t i = 0; i < 3; ++i) {
        channels[i] = ChannelConfig();
        strncpy(channels[i].name, kNames[i], kChannelNameBytes - 1);
    }
    channels[kChannelAux].shared = true;
    *count = 3;
}

}  // namespace vpipc
