// shell_model_tap.h - the model tap's shared sections: what the compositor's model filter drops and taps for the server
// (epic 0561, ticket 4874; research/05-graphics-composition/city-renderer.md sections 2.2, 3.2-3.4 and 10). Written
// 2026-11-18 for this repository (GPL-3.0-or-later). Plain C layout and inline C functions only: shell_composite.dll (x86)
// writes the tap and reads the identity set, the server (x64, 4875) reads the tap and writes the identity set.
//
// Three sections, all named with the EXE's pid:
//   Local\VPShellModelTap.<pid>      control block (VPSHELL_MODELTAP_CONTROL_BYTES), created by the compositor, mapped by
//                                    both for their life: producer fields under a seqlock, consumer fields written by the
//                                    server (want bits, the identity set's generation, the ack).
//   Local\VPShellModelTapData.<pid>  the frames: `slots` slots of `slotBytes`, created by the compositor. Frame n goes to
//                                    slot n % slots, written through a short-lived view at the Present that ends the EXE
//                                    frame (no address space kept in the 32-bit process); the producer never waits for the
//                                    server, it overwrites the oldest slot.
//   Local\VPShellModelSet.<pid>      the identity set, created and written by the server: the mesh keys its library holds
//                                    resident, each with the mode bits it may be dropped under. The compositor opens it
//                                    read-only when the control block's setGeneration changes, copies it whole or not at
//                                    all (seqlock, hash, bounds), and closes it.
//
// Producer protocol (the decal tap's, shell_decal_tap.h, with a slot ring instead of one section per generation): the
// slot is written in full (its header last but for nothing: the header's own frameSeq is checked against the control
// block), then the control block under its seqlock (seq odd while writing) with frameSeq, slot, frameBytes and fnv1a64.
// Consumer protocol: read the control block (seq even and unchanged around the copy), map the slot, copy frameBytes,
// check magic, version, headerBytes, the slot's frameSeq equal to the control block's, every count and offset inside
// frameBytes, and the hash; use the copy whole or not at all; then write ackResult, ackServerPid and ackFrameSeq (last).
// The compositor uses consumer fields only from the live server it latched (ackServerPid / VPModelSetHeader::serverPid
// equal to the server pid of its liveness check, city-renderer.md 10.4).
//
// What is tapped (city-renderer.md 10.5): every colour-pass draw of a DROP pair (shell_model_scene.h) into a target with
// a depth view while the server wants its class's bit (`wantBits`), the latch holds the bit, or model_tap_passed=1 (TEST
// ONLY): the draw's mesh key, its instances (blocks and instanced landmarks: the four float4 rows per instance of vertex
// slot 1, as the EXE wrote them; single models: VS cb0 rows 0-3; skinned parts: the 96 palette rows of VS t27 per
// instance id of slot 1), its VS and PS cb1 (one constant entry per distinct content in the frame) and its target.
// Completeness is per draw (lead2-0561 #6099 (c)): a draw is dropped only when its whole record (key, rows, constant
// references) is in the frame; a draw whose record cannot be complete (a buffer not copied, a bound reached, the slot
// full) is passed and counted, never dropped. So every DROPPED record of a published frame is complete, and a frame
// whose slot overflowed still holds every dropped draw of that frame.
// Layout part: stdint/stddef/string only (no windows.h), so render/ includes it for the .mtap file form (#6099 (a)).
//
// Nothing of the EXE or the game's files is written to disk by the product or committed (D83): the bytes are the
// player's own, in memory. The file form `.mtap` (4873/4875) is one frame image as is (magic C5MT).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define VPSHELL_MODELTAP_CONTROL_FMT L"Local\\VPShellModelTap.%u"       /* EXE pid */
#define VPSHELL_MODELTAP_DATA_FMT L"Local\\VPShellModelTapData.%u"      /* EXE pid */
#define VPSHELL_MODELSET_FMT L"Local\\VPShellModelSet.%u"               /* EXE pid */
#define VPSHELL_MODELTAP_CONTROL_BYTES 4096u
#define VPSHELL_MODELTAP_MAGIC 0x544D3543u        /* 'C5MT' (control block, frame image and file form) */
#define VPSHELL_MODELSET_MAGIC 0x534D3543u        /* 'C5MS' */
#define VPSHELL_MODELTAP_VERSION 1u
#define VPSHELL_MODELSET_VERSION 1u

// ---- bounds (city-renderer.md 10.5; a frame that would pass one is truncated: the draws beyond it are passed and
// counted, never dropped) ----
#define VPSHELL_MODELTAP_SLOTS 3u
#define VPSHELL_MODELTAP_SLOT_KB_DEFAULT 4096u     /* model_tap_slot_kb */
#define VPSHELL_MODELTAP_SLOT_KB_MIN 256u
#define VPSHELL_MODELTAP_SLOT_KB_MAX 16384u
#define VPSHELL_MODELTAP_MAX_DRAWS 16384u          /* per frame (3910: at most about 300 family colour draws per frame) */
#define VPSHELL_MODELTAP_MAX_CONSTS 4096u          /* per frame */
#define VPSHELL_MODELTAP_MAX_TARGETS 16u
#define VPSHELL_MODELTAP_MAX_INSTANCES 4096u       /* per draw */
#define VPSHELL_MODELTAP_CB_MAX_BYTES 4096u        /* a constant buffer bigger than this is not tapped (the draw passes) */
#define VPSHELL_MODELTAP_PALETTE_ROWS 96u          /* rows per skinned instance id: 32 bones x 3 (4870 5.4) */
#define VPSHELL_MODELTAP_INST_ROWS 4u              /* rows per instance of slot 1 (stride 64) and of VS cb0 rows 0-3 */
#define VPSHELL_MODELSET_MAX_KEYS 65536u
#define VPSHELL_MODELSET_MAX_BYTES (64u + 64u * VPSHELL_MODELSET_MAX_KEYS)

enum VPShellModelTapState
{
	VPMTAP_NONE = 0, VPMTAP_RUNNING = 1, VPMTAP_FAILED = 2, VPMTAP_OFF = 3,
};

enum VPShellModelTapFail   // the producer's failReason (details in the composite log)
{
	VPMTAP_FAIL_NONE = 0, VPMTAP_FAIL_SECTION = 1, VPMTAP_FAIL_VIEW = 2, VPMTAP_FAIL_SEH = 3,
};

// VPShellModelTapControl::setResult: the compositor's verdict on the last identity set it read
enum VPShellModelSetResult
{
	VPMSET_NONE = 0, VPMSET_OK = 1, VPMSET_OPEN = 2, VPMSET_MAGIC = 3, VPMSET_VERSION = 4, VPMSET_BOUNDS = 5,
	VPMSET_HASH = 6, VPMSET_TORN = 7, VPMSET_PID = 8, VPMSET_GENERATION = 9, VPMSET_NOMEM = 10, VPMSET_RESULT_COUNT = 11,
};

// The draw's flags (VPModelTapDraw::flags)
#define VPMTAP_DRAW_DROPPED 0x01u      /* not passed to d3d11: the server draws it */
#define VPMTAP_DRAW_DEFERRED 0x02u     /* recorded on a deferred context (placed at its ExecuteCommandList) */
#define VPMTAP_DRAW_NOT_IN_SET 0x04u   /* passed: the key is not in the identity set (or not for this class's bit) */
#define VPMTAP_DRAW_BIT_OFF 0x08u      /* passed: the latch does not hold its bit (tapped for wantBits or model_tap_passed) */
#define VPMTAP_DRAW_UNHASHED 0x10u     /* passed: a buffer or texture of the key was not hashed at its creation */
#define VPMTAP_DRAW_HELD 0x20u         /* passed: droppable, but its key was not droppable in the frame before (ticket 0788): the
                                          EXE draws it in this frame, it is dropped from the next one, and the server draws this
                                          record in the frame it pairs this tap with (lategame-model-flash.md) */
// the key's flags (VPModelKey::flags)
#define VPMKEY_INDEX32 0x01u           /* R32 indices: ib is the hash from byte 4 (else R16, from byte 2) */

#pragma pack(push, 8)
// A mesh key (4870's rule, city-renderer.md 3.2 and 5.5): one index buffer and one vertex buffer per model file, a draw
// is its range of them, and two files can share both buffers (the European and Mediterranean Ancient sets), so the
// textures t0 and t1 are part of the key. Hashes are VPModelHash64 (below); 0 = none (t1 not bound).
typedef struct VPModelKey   // 48 bytes
{
	uint64_t ib;          // the index buffer's content from index 1 to its end (the pad index 0 is not stable)
	uint64_t vb;          // vertex slot 0's buffer, its whole content
	uint64_t t0, t1;      // PS t0 / t1 texture identity (VPModelTextureHash), 0 = no view bound
	uint32_t startIndex, indexCount;
	int32_t baseVertex;
	uint32_t flags;       // VPMKEY_*
} VPModelKey;

// ---- the identity set (written by the server) ----
typedef struct VPModelSetHeader   // at offset 0 of Local\VPShellModelSet.<pid>; 64 bytes
{
	volatile uint32_t seq;        // odd while the server writes
	uint32_t magic;               // VPSHELL_MODELSET_MAGIC
	uint32_t version;             // VPSHELL_MODELSET_VERSION
	uint32_t headerBytes;         // sizeof(VPModelSetHeader)
	uint32_t keyBytes;            // sizeof(VPModelSetKey)
	uint32_t generation;          // 1, 2, ...; equal to the control block's setGeneration when complete
	uint32_t keyCount;            // <= VPSHELL_MODELSET_MAX_KEYS
	uint32_t keyOffset;           // from the section start, 16-byte aligned (64)
	uint32_t serverPid;
	uint32_t pad0;
	uint64_t fnv1a64;             // VPModelHash64 of keyCount * keyBytes bytes at keyOffset
	uint32_t reserved[4];
} VPModelSetHeader;

typedef struct VPModelSetKey   // 64 bytes
{
	VPModelKey key;
	uint8_t bits;                 // the mode bits it may be dropped under (VPMODE_CITIES 2, VPMODE_LANDMARKS 4); a
	                              // skinned key must carry exactly one (the class's bit comes from it)
	uint8_t cls;                  // VPMODEL_CLASS_* the server draws it as, or VPMODEL_CLASS_NONE = any class
	uint8_t pad[14];
} VPModelSetKey;

// ---- the control block ----
typedef struct VPShellModelTapControl   // at offset 0 of Local\VPShellModelTap.<pid>
{
	// ---- producer (compositor), seqlock ----
	volatile uint32_t seq;
	uint32_t magic;               // VPSHELL_MODELTAP_MAGIC
	uint32_t version;             // VPSHELL_MODELTAP_VERSION
	uint32_t state;               // VPShellModelTapState
	uint32_t failReason;          // VPShellModelTapFail
	uint32_t exePid;
	uint32_t slots;               // VPSHELL_MODELTAP_SLOTS
	uint32_t slotBytes;           // model_tap_slot_kb * 1024
	uint64_t frameSeq;            // the last published frame (1, 2, ...); 0 = none yet
	uint32_t slot;                // = frameSeq % slots
	uint32_t frameBytes;          // = that frame image's imageBytes
	uint64_t fnv1a64;             // = that frame image's fnv1a64
	uint64_t presentCount;        // the camera block's presentCount of the Present that ended the frame
	int64_t publishedQpc;
	uint32_t setGenerationApplied;   // the identity set generation in use (0 = none)
	uint32_t setKeysApplied;
	uint32_t setResult;           // VPShellModelSetResult of the last read
	uint32_t setReads;            // identity sets read (any result)
	uint32_t framesTruncated;     // frames with draws beyond a bound (those draws passed)
	uint32_t framesUnacked;       // published while the server's ack was more than `slots` frames behind (indication)
	float tapUsPerFrame;          // mean CPU time of the tap per EXE frame over the last 1 s (records, copies, publish)
	float publishUs;              // the last publication's time (view, copy, hash)
	uint32_t reserved0[6];
	// ---- consumer (server) ----
	volatile uint32_t wantBits;       // VPMODE_CITIES | VPMODE_LANDMARKS: tap these classes (also while not latched)
	volatile uint32_t setGeneration;  // written after the set section is complete (generation of VPModelSetHeader)
	volatile uint32_t ackServerPid;
	volatile int32_t ackResult;       // 0 ok; > 0 a c5r::Err of the reader; -1 hash; -2 open; -3 torn; -4 version
	volatile uint64_t ackFrameSeq;    // written last
	uint32_t reserved1[16];
} VPShellModelTapControl;

// ---- one frame image (a slot; the .mtap file form) ----
typedef struct VPModelTapFrame   // 128 bytes
{
	uint32_t magic;               // VPSHELL_MODELTAP_MAGIC
	uint32_t version;             // VPSHELL_MODELTAP_VERSION
	uint32_t headerBytes;         // sizeof(VPModelTapFrame)
	uint32_t drawBytes;           // sizeof(VPModelTapDraw)
	uint64_t frameSeq;
	uint64_t presentCount;
	uint32_t drawCount, rowCount, constCount, targetCount;
	uint32_t drawOffset, rowOffset, constOffset, targetOffset;   // from the image start, 16-byte aligned
	uint32_t imageBytes;          // <= the control block's slotBytes
	uint32_t setGeneration;       // the identity set the drop decisions used
	uint32_t latchedBits;         // the latch of this frame (VPMODE_* bits 1 and 2)
	uint32_t wantBits;            // the server's want bits this frame used
	uint32_t dropped[2];          // [0] cities, [1] landmarks: dropped draws in this frame (= the DROPPED records)
	uint32_t passedTapped;        // tapped and passed (not in the set, bit not latched, test key)
	uint32_t truncated;           // draws not tapped because of a bound (all passed)
	uint64_t fnv1a64;             // VPModelHash64 of the bytes [headerBytes, imageBytes)
	uint32_t reserved[6];
} VPModelTapFrame;

typedef struct VPModelTapDraw   // 128 bytes, in execution order (deferred draws at their ExecuteCommandList)
{
	VPModelKey key;
	uint8_t cls;                  // VPMODEL_CLASS_*
	uint8_t bits;                 // the identity set's bits for the key (0 = not in the set)
	uint8_t flags;                // VPMTAP_DRAW_*
	uint8_t pad0;
	uint32_t target;              // index into the target table
	uint32_t instanceCount;       // the draw's InstanceCount (1 for non-instanced entries)
	uint32_t startInstance;       // StartInstanceLocation
	uint32_t instRow;             // first row of its instances in the row block
	uint32_t instRows;            // rows PER INSTANCE (#6146): VPSHELL_MODELTAP_INST_ROWS (blocks, landmarks: slot 1's
	                              // 4 rows; single models: VS cb0 rows 0-3, the same block repeated for each instance) or
	                              // VPSHELL_MODELTAP_PALETTE_ROWS (skinned); the draw's rows are instRows * instanceCount
	uint32_t vsCb1, psCb1;        // index into the constant table (0xFFFFFFFF = none bound)
	uint32_t order;               // the draw's place among this frame's family colour draws
	uint32_t skinId0;             // skinned: the first instance's id in slot 1 (diagnostic); else 0
	uint32_t reserved0;
	uint64_t env[3];              // PS t2, t3, t4 texture identity (VPModelTextureHash of slice 0; 0 = none): the
	                              // environment cubes, which differ by class and pass (4873, #6104)
	uint64_t reserved1;
} VPModelTapDraw;

typedef struct VPModelTapConst   // 16 bytes: one constant buffer's content as the draw saw it
{
	uint32_t firstRow;            // in the row block
	uint32_t rows;                // byteWidth / 16
	uint32_t stage;               // VPMODEL_STAGE_VS or VPMODEL_STAGE_PS (where it was first seen bound as cb1)
	uint32_t uses;                // draws that reference it
} VPModelTapConst;

typedef struct VPModelTapTarget   // 32 bytes: the colour target (RTV 0) and depth view of a tapped draw
{
	uint32_t width, height, samples, format;     // the RTV's texture (format: its DXGI_FORMAT number)
	uint32_t dsvWidth, dsvHeight;
	uint32_t draws;               // tapped draws into it this frame
	uint32_t reserved;
} VPModelTapTarget;
// The row block: float[4] rows, 16 bytes each.
#pragma pack(pop)

// ---- the geometry section (ticket 4879, city-renderer.md 13): the CPU-built meshes' buffers ----
// Local\VPShellModelGeom.<pid>, created by the compositor (model_geom_kb of data, default 32 MB, pagefile-backed): the
// buffers of the family draws that no model file holds - an R32 index buffer and its vertex buffer, both IMMUTABLE
// (walls, the Great Wall, the road and rail bridge) - copied once from the EXE (the read-back at a Present) and kept while
// the EXE draws them. The server copies an entry when it first sees it, checks its bytes against its hash (the key's:
// Index32 from byte 4, Vertex all bytes) and publishes the keys of the draws it can then make in the identity set; the
// compositor drops such a draw only while the set has its key AND both its buffers are entries here (else it passes).
// Writer protocol (the compositor, render thread, at a Present): seq odd, the data first, then the entry, generation + 1,
// seq even. An entry's data is never rewritten while the entry is valid; a freed entry (kind 0) may have its range reused
// by a later entry. Reader protocol (the server): header and entries copied with seq even and unchanged around the copy;
// an entry's bytes copied, then seq checked unchanged again (else read again later), then the hash checked.
#define VPSHELL_MODELGEOM_FMT L"Local\\VPShellModelGeom.%u"         /* EXE pid */
#define VPSHELL_MODELGEOM_MAGIC 0x474D3543u      /* 'C5MG' */
#define VPSHELL_MODELGEOM_VERSION 1u
#define VPSHELL_MODELGEOM_MAX_ENTRIES 512u
/* model_geom_kb: the composite's key (shell_composite.ini [composite] or VPSHELL_MODEL_GEOM_KB), read by unit_filter.cpp
   since ticket 0597 (clamped to MIN..MAX; not a number = the default); when the data or the entries are used up and no
   entry is unnamed for 120 Presents, a new buffer is refused `full` (logged once per buffer) and its draw passes to the EXE
   (city-renderer.md 13.10) */
#define VPSHELL_MODELGEOM_KB_DEFAULT 32768u
#define VPSHELL_MODELGEOM_KB_MIN 1024u
#define VPSHELL_MODELGEOM_KB_MAX 131072u
#define VPSHELL_MODELGEOM_MAX_BUFFER (8u << 20)  /* a buffer bigger than this is refused (oversize): the draw passes */
#define VPSHELL_MODELGEOM_DATA_OFFSET 65536u     /* header + entries, rounded to the allocation granularity */
#define VPMGEOM_KIND_FREE 0u
#define VPMGEOM_KIND_INDEX32 1u
#define VPMGEOM_KIND_VERTEX 2u

#pragma pack(push, 8)
typedef struct VPModelGeomHeader   // at offset 0; 64 bytes
{
	volatile uint32_t seq;        // odd while the compositor writes
	uint32_t magic;               // VPSHELL_MODELGEOM_MAGIC
	uint32_t version;             // VPSHELL_MODELGEOM_VERSION
	uint32_t headerBytes;         // sizeof(VPModelGeomHeader)
	uint32_t entryBytes;          // sizeof(VPModelGeomEntry)
	uint32_t maxEntries;          // VPSHELL_MODELGEOM_MAX_ENTRIES; the entries follow the header
	uint32_t dataOffset;          // VPSHELL_MODELGEOM_DATA_OFFSET
	uint32_t dataBytes;           // model_geom_kb * 1024
	uint32_t generation;          // + 1 at every change of an entry
	uint32_t exePid;
	uint32_t entriesUsed;         // valid entries (diagnostic)
	uint32_t refused;             // buffers the compositor refused (oversize, full, changed; diagnostic)
	uint32_t reserved[4];
} VPModelGeomHeader;

typedef struct VPModelGeomEntry   // 32 bytes
{
	uint64_t hash;                // VPModelGeomHash of the bytes (the wire key's ib or vb)
	uint32_t kind;                // VPMGEOM_KIND_*
	uint32_t stride;              // VERTEX: the stride the EXE bound; INDEX32: 4
	uint32_t offset;              // from dataOffset
	uint32_t bytes;               // <= VPSHELL_MODELGEOM_MAX_BUFFER
	uint32_t generation;          // the header's generation when the entry was written
	uint32_t reserved;
} VPModelGeomEntry;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(VPModelGeomHeader) == 64, "layout");
static_assert(sizeof(VPModelGeomEntry) == 32, "layout");
static_assert(64u + VPSHELL_MODELGEOM_MAX_ENTRIES * 32u <= VPSHELL_MODELGEOM_DATA_OFFSET, "entries fit");
static_assert(sizeof(VPModelKey) == 48, "layout");
static_assert(sizeof(VPModelSetHeader) == 64, "layout");
static_assert(sizeof(VPModelSetKey) == 64, "layout");
static_assert(sizeof(VPModelTapFrame) == 128, "layout");
static_assert(sizeof(VPModelTapDraw) == 128 && offsetof(VPModelTapDraw, env) == 96, "layout");
static_assert(sizeof(VPModelTapConst) == 16, "layout");
static_assert(sizeof(VPModelTapTarget) == 32, "layout");
static_assert(offsetof(VPShellModelTapControl, frameSeq) == 32, "layout");
static_assert(offsetof(VPShellModelTapControl, wantBits) == 128, "layout");
static_assert(offsetof(VPShellModelTapControl, ackFrameSeq) == 144, "layout");
static_assert(sizeof(VPShellModelTapControl) <= VPSHELL_MODELTAP_CONTROL_BYTES, "control block too big");
#endif

// ---- the hash (both sides compute it the same way; the server from its repack of the .gr2 and the .dds files) ----
// VPModelHashUpdate takes the bytes as little-endian 64-bit words, then the tail bytes one by one, FNV-1a style;
// VPModelHashFinal mixes in the total length and avalanches (fmix64). A buffer: Final(Update(OFFSET, p, n), n).
// A texture: one Update per row of the chosen mip (VPModelTextureMip), then Final with the total bytes.
#define VPMODEL_HASH_OFFSET 0xcbf29ce484222325ull
#define VPMODEL_HASH_PRIME 0x100000001b3ull
static inline uint64_t VPModelHashUpdate(uint64_t h, const void* data, size_t n)
{
	const uint8_t* b = (const uint8_t*)data;
	size_t i = 0;
	for (; i + 8 <= n; i += 8)
	{
		uint64_t w;
		memcpy(&w, b + i, 8);
		h ^= w;
		h *= VPMODEL_HASH_PRIME;
	}
	for (; i < n; ++i)
	{
		h ^= b[i];
		h *= VPMODEL_HASH_PRIME;
	}
	return h;
}
static inline uint64_t VPModelHashFinal(uint64_t h, uint64_t totalBytes)
{
	h ^= totalBytes;
	h *= VPMODEL_HASH_PRIME;
	h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
	return h ? h : 1u;   // 0 means "none" in a key
}
static inline uint64_t VPModelHash64(const void* data, size_t n) { return VPModelHashFinal(VPModelHashUpdate(VPMODEL_HASH_OFFSET, data, n), n); }
// The hash a geometry entry carries (ticket 4879): INDEX32 from byte 4 (the wire key's rule: element 0 is not hashed),
// VERTEX all bytes; 0 = none.
static inline uint64_t VPModelGeomHash(uint32_t kind, const void* data, size_t n)
{
	if (!data) return 0;
	if (kind == VPMGEOM_KIND_INDEX32) return n >= 8 ? VPModelHash64((const uint8_t*)data + 4, n - 4) : 0;
	return n ? VPModelHash64(data, n) : 0;
}

// A texture's identity (VPModelKey::t0/t1, VPModelTapDraw::env): the hash of one mip level of array slice 0 (a cube's
// +X face), so that the EXE's texture
// quality setting (which may leave out the top mips) does not change it: the first level whose width and height are
// both <= 64 (or the last level the texture has). Rows: block rows for BC formats (4 x 4 blocks), pixel rows otherwise;
// each row's bytes are ceil(width / blockWidth) * bytesPerBlock (the pitch's padding is not hashed). Formats without a
// row size here have no identity (0): such a draw is never dropped. The bytes are hashed exactly as the EXE passes them
// in CreateTexture2D's initial data, whatever the format (#6146): the format only gives the row size; no channel is
// swapped or converted by the hash. The BGRA rule (4873 #6269, lead2-0561 #6270): the EXE uploads an A8R8G8B8 .dds
// swizzled to R8G8B8A8, so the bytes hashed are those R8G8B8A8 bytes as CreateTexture2D receives them; the server
// reproduces the identity by applying the same swizzle to the file's pixels before hashing.
// Only resources whose content cannot change after the hash get an identity: IMMUTABLE buffers; DEFAULT or IMMUTABLE
// shader-resource textures without render-target, depth or unordered-access binding, whose identity is cleared by any
// later write the compositor sees (UpdateSubresource, CopyResource, CopySubresourceRegion into it) (#6273).
// bytes per block (BC) or per pixel; blockWidth 4 or 1; 0 = no identity
static inline uint32_t VPModelFormatBlock(uint32_t dxgiFormat, uint32_t* blockWidth)
{
	*blockWidth = 1;
	switch (dxgiFormat)
	{
	case 70: case 71: case 72: case 79: case 80: case 81: *blockWidth = 4; return 8;        // BC1, BC4
	case 73: case 74: case 75: case 76: case 77: case 78: case 82: case 83: case 84:        // BC2, BC3, BC5
	case 94: case 95: case 96: case 97: case 98: case 99: *blockWidth = 4; return 16;      // BC6H, BC7
	case 27: case 28: case 29: case 30: case 31: case 32:                                   // R8G8B8A8
	case 87: case 88: case 90: case 91: case 92: case 93: return 4;                         // B8G8R8A8, B8G8R8X8
	case 60: case 61: case 62: case 63: case 64: case 65: return 1;                         // R8
	case 48: case 49: case 50: case 51: case 52: return 2;                                  // R8G8
	case 9: case 10: case 11: case 12: case 13: case 14: return 8;                          // R16G16B16A16
	case 1: case 2: case 3: case 4: return 16;                                              // R32G32B32A32
	default: return 0;
	}
}
static inline uint32_t VPModelTextureMip(uint32_t width, uint32_t height, uint32_t mipLevels)
{
	uint32_t m = 0;
	while (m + 1 < mipLevels && ((width >> m) > 64u || (height >> m) > 64u)) ++m;
	return m;
}
// The identity of one mip's data (rows `rowPitch` apart, as D3D11_SUBRESOURCE_DATA or a DDS gives them); mipWidth and
// mipHeight are that level's size in pixels (at least 1). 0 = no identity (a format without a row size).
static inline uint64_t VPModelTextureHash(const void* data, uint32_t rowPitch, uint32_t mipWidth, uint32_t mipHeight,
                                          uint32_t dxgiFormat)
{
	uint32_t bw = 1;
	const uint32_t bb = VPModelFormatBlock(dxgiFormat, &bw);
	if (!bb || !data || !mipWidth || !mipHeight) return 0;
	const uint32_t rowBytes = (mipWidth + bw - 1) / bw * bb, rows = (mipHeight + bw - 1) / bw;
	if (rowPitch < rowBytes) return 0;
	uint64_t h = VPMODEL_HASH_OFFSET;
	for (uint32_t r = 0; r < rows; ++r) h = VPModelHashUpdate(h, (const uint8_t*)data + (size_t)r * rowPitch, rowBytes);
	return VPModelHashFinal(h, (uint64_t)rowBytes * rows);
}
