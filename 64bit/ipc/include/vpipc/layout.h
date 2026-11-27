// vpipc/layout.h - the shared-section byte layout. Identical in x86 and x64 builds.
// Owner: epic 1200 coordinator. Any change bumps kVersionMinor and updates ipc/PROTOCOL.md in the same commit.
// Only fixed-width integers live here: no pointers, size_t, bool or C++ objects.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace vpipc {

constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

constexpr uint32_t kMagic = 0x50495056u;  // 'VPIP' little-endian
constexpr uint32_t kLine = 64;
constexpr uint32_t kHeaderBytes = 4096;
constexpr uint32_t kMaxChannels = 8;
constexpr uint32_t kInlineBytes = 32;
constexpr uint32_t kRefusalTextBytes = 1024;
constexpr uint32_t kChannelNameBytes = 16;
constexpr uint32_t kRingPadMethod = 0xFFFFFFFFu;
constexpr uint32_t kNullLength = 0xFFFFFFFFu;  // marshalled null string

// ---- enums stored in the section (as uint32_t / uint16_t) ----
enum ConnState : uint32_t {
    kConnNoServer = 0,
    kConnAttachRequested = 1,
    kConnConnected = 2,
    kConnPeerDead = 3,
    kConnClosing = 4,
    kConnRefused = 5,
};

enum SideState : uint32_t {
    kSideAbsent = 0,
    kSideStarting = 1,
    kSideReady = 2,
    kSideClosing = 3,
    kSideRefused = 4,
};

enum MsgKind : uint16_t {
    kMsgNone = 0,
    kMsgCall = 1,
    kMsgReturn = 2,
    kMsgChunk = 3,
    kMsgChunkAck = 4,
};

enum MsgFlags : uint16_t {
    kFlagInline = 1u << 0,   // payload is in MsgLine::inlineData
    kFlagChunked = 1u << 1,  // final piece of a chunked payload; assembled payload is totalLength bytes
};

enum ReturnStatus : uint32_t {
    kRetOk = 0,
    kRetUnknownMethod = 1,
    kRetHandlerError = 2,
    kRetProtocolError = 3,
};

enum RefuseReason : uint32_t {
    kRefuseNone = 0,
    kRefuseVersion = 1,
    kRefuseLayout = 2,
    kRefuseSchema = 3,
    kRefuseBusy = 4,  // another server is attached
};

enum ChannelInitiator : uint32_t {
    kInitiatorClient = 0,
    kInitiatorServer = 1,
};

enum ChannelFlags : uint32_t {
    kChannelShared = 1u << 0,  // several client threads; serialised by a client-side lock per top-level call
};

// ---- header (4096 bytes) ----
struct alignas(64) IdentityLine {
    uint32_t magic;
    uint16_t versionMajor;
    uint16_t versionMinor;
    uint32_t headerBytes;
    uint32_t totalBytes;
    uint32_t layoutHash;
    uint32_t channelCount;
    uint64_t appSchemaHash;
    uint64_t qpcFrequency;
    uint64_t createdQpc;
    uint32_t reserved[4];
};

struct alignas(64) SideLine {
    volatile uint32_t pid;
    uint32_t pointerBits;
    volatile uint32_t state;  // SideState
    uint32_t refuseReason;    // RefuseReason (server line only)
    volatile uint64_t heartbeatQpc;
    volatile uint64_t heartbeatCount;
    uint16_t versionMajor;
    uint16_t versionMinor;
    uint32_t layoutHash;
    uint64_t appSchemaHash;
    uint32_t reserved[4];
};

struct alignas(64) ConnLine {
    volatile uint32_t connState;  // ConnState
    volatile uint32_t generation;
    volatile uint32_t attachPid;
    uint32_t reserved[13];
};

struct alignas(64) ChannelDesc {
    uint32_t offset;     // byte offset of the ChannelBlock from the section start
    uint32_t initiator;  // ChannelInitiator
    uint32_t flags;      // ChannelFlags
    uint32_t dataBytes;  // multiple of 64
    uint32_t ringBytes;  // power of two, >= 4096
    uint32_t reserved[3];
    char name[kChannelNameBytes];
    uint32_t reserved2[4];
};

struct alignas(64) Header {
    IdentityLine identity;                 // line 0
    SideLine client;                       // line 1
    SideLine server;                       // line 2
    ConnLine conn;                         // line 3
    ChannelDesc channels[kMaxChannels];    // lines 4-11
    uint8_t pad[1024 - 12 * 64];           // to 1024
    char serverRefusal[kRefusalTextBytes]; // 1024-2047
    char clientRefusal[kRefusalTextBytes]; // 2048-3071
    uint8_t tail[kHeaderBytes - 3072];
};

// ---- channel block ----
struct alignas(64) MsgLine {
    volatile uint32_t seq;  // publish word, written last with a full fence
    uint16_t kind;          // MsgKind
    uint16_t flags;         // MsgFlags
    uint32_t method;
    uint32_t callId;
    uint32_t depth;
    uint32_t length;       // payload bytes in this message (a chunk: this piece)
    uint32_t totalLength;  // full payload bytes (== length unless chunked)
    uint32_t status;       // ReturnStatus for kMsgReturn
    uint8_t inlineData[kInlineBytes];
};

struct alignas(64) SideCtlLine {
    volatile uint32_t sleeping;     // 1 = this side announced it is (about to be) blocked on its wake event
    volatile uint32_t activeDepth;  // diagnostic: deepest call depth this side has reached (task 1209: written only when it grows)
    uint32_t reserved[14];
};

struct alignas(64) RingIndexLine {
    volatile uint32_t index;  // monotonically increasing byte count; position = index & (ringBytes - 1)
    uint32_t reserved[15];
};

struct alignas(64) ChannelBlock {
    MsgLine c2s;              // line 0
    MsgLine s2c;              // line 1
    SideCtlLine clientCtl;    // line 2
    SideCtlLine serverCtl;    // line 3
    RingIndexLine c2sWrite;   // line 4 (client writes)
    RingIndexLine c2sRead;    // line 5 (server writes)
    RingIndexLine s2cWrite;   // line 6 (server writes)
    RingIndexLine s2cRead;    // line 7 (client writes)
    // followed by: uint8_t data[dataBytes]; uint8_t ringC2S[ringBytes]; uint8_t ringS2C[ringBytes];
};

// Ring record header (8-byte aligned in the ring data).
struct RingRecord {
    uint32_t length;  // payload bytes (not including this header or padding)
    uint32_t method;  // kRingPadMethod = skip to the ring start
};

constexpr uint32_t kChannelBlockFixedBytes = sizeof(ChannelBlock);
inline constexpr uint32_t channel_block_bytes(uint32_t dataBytes, uint32_t ringBytes) {
    return kChannelBlockFixedBytes + dataBytes + 2u * ringBytes;
}
inline constexpr uint32_t ring_record_bytes(uint32_t payload) {
    return (uint32_t)((sizeof(RingRecord) + payload + 7u) & ~7u);
}

// ---- layout checks (same values in x86 and x64) ----
static_assert(sizeof(IdentityLine) == 64, "layout");
static_assert(sizeof(SideLine) == 64, "layout");
static_assert(sizeof(ConnLine) == 64, "layout");
static_assert(sizeof(ChannelDesc) == 64, "layout");
static_assert(sizeof(Header) == kHeaderBytes, "layout");
static_assert(offsetof(Header, serverRefusal) == 1024, "layout");
static_assert(offsetof(Header, clientRefusal) == 2048, "layout");
static_assert(sizeof(MsgLine) == 64, "layout");
static_assert(offsetof(MsgLine, inlineData) == 32, "layout");
static_assert(sizeof(SideCtlLine) == 64, "layout");
static_assert(sizeof(RingIndexLine) == 64, "layout");
static_assert(sizeof(ChannelBlock) == 8 * 64, "layout");
static_assert(sizeof(RingRecord) == 8, "layout");

// Compile-time layout hash (FNV-1a over sizes and offsets). Equal on both sides or the attach is refused.
// C++11 constexpr (single return statements): the proxy side compiles at clang-cl's VC9 default, C++11.
namespace detail {
constexpr uint32_t fnv_byte(uint32_t h, uint32_t b) { return (h ^ b) * 16777619u; }
constexpr uint32_t fnv(uint32_t h, uint32_t v) {
    return fnv_byte(fnv_byte(fnv_byte(fnv_byte(h, v & 0xFFu), (v >> 8) & 0xFFu), (v >> 16) & 0xFFu), (v >> 24) & 0xFFu);
}
constexpr uint32_t fold(const uint32_t* v, uint32_t n, uint32_t h) { return n == 0 ? h : fold(v + 1, n - 1, fnv(h, *v)); }
constexpr uint32_t kLayoutValues[] = {
    kVersionMajor, kVersionMinor, kHeaderBytes, kMaxChannels, kInlineBytes,
    (uint32_t)sizeof(Header), (uint32_t)offsetof(Header, client), (uint32_t)offsetof(Header, server),
    (uint32_t)offsetof(Header, conn), (uint32_t)offsetof(Header, channels),
    (uint32_t)offsetof(Header, serverRefusal), (uint32_t)offsetof(Header, clientRefusal),
    (uint32_t)offsetof(SideLine, heartbeatQpc), (uint32_t)offsetof(SideLine, appSchemaHash),
    (uint32_t)offsetof(ConnLine, generation), (uint32_t)offsetof(ChannelDesc, name),
    (uint32_t)sizeof(MsgLine), (uint32_t)offsetof(MsgLine, kind), (uint32_t)offsetof(MsgLine, method),
    (uint32_t)offsetof(MsgLine, callId), (uint32_t)offsetof(MsgLine, depth),
    (uint32_t)offsetof(MsgLine, length), (uint32_t)offsetof(MsgLine, totalLength),
    (uint32_t)offsetof(MsgLine, status), (uint32_t)offsetof(MsgLine, inlineData),
    (uint32_t)sizeof(ChannelBlock), (uint32_t)offsetof(ChannelBlock, s2c),
    (uint32_t)offsetof(ChannelBlock, clientCtl), (uint32_t)offsetof(ChannelBlock, serverCtl),
    (uint32_t)offsetof(ChannelBlock, c2sWrite), (uint32_t)offsetof(ChannelBlock, s2cRead),
    (uint32_t)sizeof(RingRecord),
};
}  // namespace detail

constexpr uint32_t kLayoutHash =
    detail::fold(detail::kLayoutValues, (uint32_t)(sizeof(detail::kLayoutValues) / sizeof(uint32_t)), 2166136261u);

}  // namespace vpipc
