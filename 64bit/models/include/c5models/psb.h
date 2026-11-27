// c5models/psb.h - the .psb particle file reader (E2 of C4b; ticket 4787, epic 0042, 2026-11-23).
// Own code, GPL-3.0-or-later like the rest of the repository. Written from the knowledge base
// (research/05-graphics-composition/effects-formats.md 4 and section 16 "E2 as built"); nothing of the game's files
// is held here (D83): the files are read from the player's install at run time through a FileProvider.
//
// The layout (effects-formats.md 4, [C] over all 384 files of the install):
//   file header, 0x40 bytes   "PSB\0", u32 version (100), u32 payload size (= file size - 0x40), u32 0x40 (the
//                             header size), u32 at 0x10 (16 in every file, meaning not known), zero to 0x40
//   emitter blocks            one after another to the exact end of the file:
//                               +0x000  char name[256], NUL-terminated, zero-padded
//                               +0x100  u32 block size; the next block starts there
//                               +0x104  u32 (272 in every block, meaning not known)
//                               +0x108  the emitter's parameters: NOT decoded here, kept raw (PsbEmitter::raw) for
//                                       the particle tap's fitting (4788), which gives the fields their meaning
//   string slots inside a block (effects-formats.md 16.2): a u32 flag (0 or 1) at a 4-aligned block offset and a
//   256-byte name field whose text is printable ASCII, NUL-terminated, and either "Custom Data" or ends in .dds / .tga
//   (a texture) or .txt (a Fork shader-set name: FakeVolume.txt, Brightness.txt, Mask.txt). Flag 1: a texture or
//   shader slot (0x120 bytes in all: flag, name, 28 bytes). Flag 0: the "Custom Data" slot, which holds an art-tree
//   texture path instead of its default text in 350 blocks. The slots are found by this rule, not by a decoded field;
//   the rule finds exactly 0320's 3,100 real texture references and none of its two false hits (16.2).
//
// Textures are found by bare lower-case name (basename_lower of the text) through a FileProvider, as every other
// effect file (effects-formats.md 1).
#pragma once

#include "c5models/art.h"   // FileProvider, basename_lower, Bytes, Err, Error

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace c5m {

constexpr uint32_t kPsbHeaderSize = 0x40;
constexpr uint32_t kPsbVersion = 100;
constexpr uint32_t kPsbNameSize = 256;        // emitter name and slot name fields
constexpr uint32_t kPsbBlockFixed = 0x108;    // name, size, the u32 at +0x104
constexpr uint32_t kPsbSlotSize = 0x120;      // flag, name, 28 bytes (flag-1 slots; [C] by stride, 16.2)
// Limits against hostile input: the install's largest file is 104,240 bytes with 18 blocks (effects-formats.md 4).
constexpr size_t kPsbMaxFileSize = 64u << 20;
constexpr size_t kPsbMaxBlocks = 4096;

enum class PsbSlotKind { Texture = 0, Shader = 1, CustomData = 2 };
const char* psb_slot_kind_name(PsbSlotKind k);   // "texture", "shader", "custom"

struct PsbSlot {
    uint32_t offset = 0;        // block-relative offset of the flag (the text starts at offset + 4)
    uint32_t flag = 0;          // 1 texture / shader slot, 0 the Custom Data slot
    PsbSlotKind kind = PsbSlotKind::CustomData;
    std::string text;           // as written (a bare name, or an art-tree path in a Custom Data slot)
    std::string bare;           // basename_lower(text) for textures and shaders, "" for "Custom Data"
    bool found = false;         // resolve_psb_textures(): the texture exists by bare name
    std::string origin;         // ... and where
};

struct PsbEmitter {
    std::string name;           // the block's name field up to its NUL ("FXP_Ring")
    uint32_t file_offset = 0;   // where the block starts in the file
    uint32_t size = 0;          // the block size (+0x100)
    uint32_t field_104 = 0;     // the u32 at +0x104 (272 on the install, meaning not known)
    Bytes raw;                  // the whole block, block-relative offsets as in effects-formats.md 4; the
                                // parameter bytes are raw[kPsbBlockFixed .. size)
    std::vector<PsbSlot> slots; // in block order
    size_t texture_count() const;   // slots of kind Texture (flag 0 or 1)
};

struct PsbFile {
    uint32_t version = 0;       // 100
    uint32_t payload_size = 0;  // file size - 0x40
    uint32_t header_size = 0;   // 0x40
    uint32_t field_10 = 0;      // 16 on the install, meaning not known
    bool header_zero = false;   // bytes 0x14 .. 0x40 are all zero (true on the install)
    std::vector<PsbEmitter> emitters;
};

// Parses a whole .psb. Refusals (out is cleared): Err::Truncated (shorter than the header, or a block that runs past
// the end), Err::BadMagic, Err::BadHeader (version not 100, header size not 0x40, payload size not file size - 0x40,
// no block, a block size below 0x108 or not a multiple of 4, a name without its NUL), Err::ReadBudget (more than
// kPsbMaxFileSize bytes or kPsbMaxBlocks blocks). `what` names the offset.
bool parse_psb(const uint8_t* data, size_t size, PsbFile& out, Error* err);
inline bool parse_psb(const Bytes& b, PsbFile& out, Error* err) { return parse_psb(b.data(), b.size(), out, err); }

// The string slots of one block by the rule above (also used by parse_psb()).
std::vector<PsbSlot> find_psb_slots(const uint8_t* block, size_t size);

// Looks up every Texture slot by bare name (FileProvider::exists) and sets found / origin. Shader slots and Custom
// Data are not looked up. Returns the number of texture slots not found.
size_t resolve_psb_textures(PsbFile& f, FileProvider& files);

}  // namespace c5m
