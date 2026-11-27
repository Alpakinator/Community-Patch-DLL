// tests/test_layout.cpp - the section layout is the same in x86 and x64. Owner: epic 1200 coordinator.
// The expected hash is pinned: an x86 and an x64 build both compare against it, so a layout that differs
// between bitnesses fails one of the two builds. Update kExpectedLayoutHash only together with a version bump.
#include "check.h"
#include "vpipc/vpipc.h"

using namespace vpipc;

static const uint32_t kExpectedLayoutHash = 0x244ED16Du;  // x86 and x64, MSVC 19.43, 2026-11-10

TEST(layout_sizes) {
    CHECK_EQ(sizeof(Header), 4096u);
    CHECK_EQ(sizeof(ChannelBlock), 512u);
    CHECK_EQ(sizeof(MsgLine), 64u);
    CHECK_EQ(channel_block_bytes(256 * 1024, 64 * 1024), 512u + 256u * 1024u + 128u * 1024u);
    CHECK_EQ(ring_record_bytes(0), 8u);
    CHECK_EQ(ring_record_bytes(1), 16u);
    CHECK_EQ(ring_record_bytes(8), 16u);
}

TEST(layout_hash_pinned) {
    printf("  pointer bits %u, layout hash 0x%08X, version %u.%u\n", (unsigned)(sizeof(void*) * 8),
           (unsigned)kLayoutHash, (unsigned)kVersionMajor, (unsigned)kVersionMinor);
    if (kExpectedLayoutHash) CHECK_EQ(kLayoutHash, kExpectedLayoutHash);
}

TEST(default_channels_shape) {
    EndpointConfig cfg;
    REQUIRE(cfg.channelCount == 3);
    const ChannelConfig* ch = cfg.channels;
    CHECK(strcmp(ch[kChannelUi].name, "ui") == 0 && !ch[kChannelUi].shared);
    CHECK(strcmp(ch[kChannelCore].name, "core") == 0 && !ch[kChannelCore].shared);
    CHECK(strcmp(ch[kChannelAux].name, "aux") == 0 && ch[kChannelAux].shared);
    CHECK(strcmp(cfg.tag, "main") == 0 && cfg.sectionName[0] == 0);
    CHECK(strcmp(status_name(Status::PeerDied), "PeerDied") == 0);
}

TEST(toolchain_report) {
#if defined(_MSC_VER)
    printf("  _MSC_VER %d", (int)_MSC_VER);
#endif
#if defined(__clang__)
    printf(", clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#endif
#if defined(_CPPLIB_VER)
    printf(", Dinkumware _CPPLIB_VER %d", (int)_CPPLIB_VER);
#endif
    printf(", __cplusplus %ld\n", (long)__cplusplus);
}

VPIPC_TEST_MAIN()
