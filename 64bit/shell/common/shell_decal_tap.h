// shell_decal_tap.h - the decal tap's shared sections (epic 0166, issue 4102; research/05-graphics-composition/
// terrain-decals.md sections 0.2, 1 and 6). Layout committed by lead-0166 before wave 2 (E2); owner from then on: 4102,
// which may add fields only in `reserved` and new enum values; the static_asserts stay. Read by the server (4103,
// server/src/render/srv_decal_tap.*) and by the file form `decals/load<NNNN>.dtap` (magic C5DT, the image below as is).
//
// Why a section of its own: the main section VPShellSection (shell_shared.h, 4096 bytes) is full up to the overlay tap.
//
// Producer: shell_composite.dll (ini decal_tap=1). After byte-pattern checks of the EXE, it reads the EXE's
// TerrainDecalSystem (0xD165E0) with SEH-guarded data reads only (D132 R2) and keeps:
//   - the control block `Local\VPShellDecalTap.<exe pid>` (VPSHELL_DECALTAP_CONTROL_BYTES), mapped for the process life;
//   - one image section per generation `Local\VPShellDecal.<exe pid>.<generation>` (VPShellDecalImage + tables + vertices),
//     written through a short-lived view (no address space kept in the 32-bit process), its handle kept until the
//     server acks that generation or a newer one is ready.
// A generation is published when the decal records changed (terrain-decals.md 6.3: the counters of TDS and the batch
// vector each Present; the batches that moved re-read; a 1 s hash of the live records as a safety net).
// Nothing of the image is written to disk by the product or committed (D83): the bytes come from the player's EXE.
// Since 4111 the image also carries the EXE's tile+0x300 of every tile (the area block, VPShellDecalImage::areaOffset):
// version 1 still, in fields that were reserved; a reader of 4102's layout ignores it. Since 0366 also the walk block
// (VPShellDecalImage::walkOffset): each record's place in the EXE's spatial index or large list, the draw's tie order.
//
// Ack protocol (the water tap's, shell_shared.h VPShellWaterTap): the producer writes the image, then the control block
// under its seqlock (seq odd while writing) with state READY; the server opens `imageName`, checks magic, version,
// sizes and fnv1a64, applies the image whole or not at all, then writes ackResult and ackGeneration (last).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define VPSHELL_DECALTAP_CONTROL_FMT L"Local\\VPShellDecalTap.%u"       /* EXE pid */
#define VPSHELL_DECAL_IMAGE_FMT L"Local\\VPShellDecal.%u.%u"            /* EXE pid, generation */
#define VPSHELL_DECALTAP_CONTROL_BYTES 4096u
#define VPSHELL_DECAL_MAGIC 0x54443543u          /* 'C5DT' (image and file form) */
#define VPSHELL_DECAL_IMAGE_VERSION 1u
#define VPSHELL_DECAL_MAX_BYTES (32ull * 1024 * 1024) /* huge t350: about 7.2 MB + names (terrain-decals.md 6.2) */
#define VPSHELL_DECAL_MAX_DECALS 262144u
#define VPSHELL_DECAL_MAX_NAMES 4096u
#define VPSHELL_DECAL_NAME_CHARS 192u            /* bytes of one UTF-8 path, zero-terminated */
#define VPSHELL_DECAL_NO_TEXTURE 0xFFFFFFFFu     /* the record's handle was 0: the engine's default texture (7.3) */

enum VPShellDecalTapState
{
	VPDTAP_NONE = 0, VPDTAP_WRITING = 1, VPDTAP_READY = 2, VPDTAP_FAILED = 3, VPDTAP_OFF = 4,
};

enum VPShellDecalTapFail   // details in the DLL log
{
	VPDTAP_FAIL_NONE = 0, VPDTAP_FAIL_PATTERN = 1, VPDTAP_FAIL_SEH = 2, VPDTAP_FAIL_LAYOUT = 3, VPDTAP_FAIL_TOO_BIG = 4,
	VPDTAP_FAIL_SECTION = 5, VPDTAP_FAIL_NAMES = 6, VPDTAP_FAIL_ACK_TIMEOUT = 7,
};

#pragma pack(push, 8)
struct VPShellDecalTapControl   // at offset 0 of the control section
{
	// ---- producer (DLL), seqlock ----
	volatile uint32_t seq;
	uint32_t magic;              // VPSHELL_DECAL_MAGIC
	uint32_t state;              // VPShellDecalTapState
	uint32_t failReason;         // VPShellDecalTapFail
	uint32_t exePid;
	uint32_t generation;         // 1, 2, ... ; part of imageName
	uint32_t loadSeq;            // the main section's camera gridSeq of the load this generation belongs to
	uint32_t decalCount;         // = image decalCount
	uint64_t imageBytes;
	uint64_t fnv1a64;            // FNV-1a 64 over imageBytes of the image
	int64_t readyQpc;
	float copyMs;                // composite CPU wall time of the last publication
	float pollMs;                // mean per-Present cost of the change check (indication, COMMON 23)
	uint32_t publishCount, refusedReads, sehCount, pad0;
	wchar_t imageName[64];
	// ---- consumer (server) ----
	volatile uint32_t ackGeneration;  // written last
	volatile int32_t ackResult;       // 0 ok; > 0 a c5r::Err of the reader; -1 hash; -2 open; -3 stale; -4 version
	volatile uint32_t ackServerPid;
	// ---- added by 4102 in reserved (2026-11-15); producer fields, written under the seqlock ----
	uint32_t changedSince;       // = the image's changedSince
	uint32_t added, removed;     // decals added and removed against changedSince (the image's changed block holds their rects)
	uint32_t heldSections;       // image sections the producer still holds (0 after the ack of the current generation)
	// ---- added by 4111 in reserved (2026-11-15) ----
	uint32_t areaSet;            // tiles whose bit is set in the image's area block (0 without a block)
	uint32_t areaTilesX, areaTilesY;   // = the image's (0 without a block)
	// ---- added by 0366 in reserved (2026-11-15) ----
	uint32_t walkSmall, walkLarge;     // the current image's walk block: small and large decals (both 0 without a block)
	uint32_t walkMisses;               // walk checks that missed (the index did not match the snapshot's records), each
	                                   // logged; a miss publishes without the walk block, or (0406) holds the build
	// ---- added by 0406 in reserved (2026-11-16; terrain-decals-walk.md) ----
	uint32_t walkHolds;                // misses that held the build instead of publishing without the block: the published
	                                   // generation had the walk and the snapshot was intact by its counters, so the batch
	                                   // the miss names is read again and the build repeated (at most decal_walk_holds in a row, default 4)
	uint32_t reserved[18];
};

struct VPShellDecalImage   // at offset 0 of an image section and of a .dtap file
{
	uint32_t magic;              // VPSHELL_DECAL_MAGIC
	uint32_t version;            // VPSHELL_DECAL_IMAGE_VERSION
	uint32_t headerBytes;        // sizeof(VPShellDecalImage)
	uint32_t decalBytes;         // sizeof(VPShellDecalRecord)
	uint32_t generation, loadSeq;
	uint32_t decalCount;
	uint32_t nameCount;
	uint32_t vertexCount;        // 8-byte vertices
	uint32_t changedCount;       // rectangles added or removed since the previous generation (the tiles to re-bake)
	uint32_t decalOffset, nameOffset, vertexOffset, changedOffset;   // from the image start, 16-byte aligned
	float worldRect[4];          // TDS+0x6C..+0x78 (height-field samples)
	uint64_t imageBytes;
	// ---- added by 4102 in reserved (2026-11-15) ----
	uint32_t changedSince;       // the changed block lists the rectangles added and removed since this generation, the
	                             // last one the server acknowledged with ackResult 0; 0 = none acknowledged yet: the
	                             // changed block then holds every decal's rectangle (re-bake all). A consumer whose last
	                             // applied generation is >= changedSince applies the rectangles (a superset of its own
	                             // changes); one whose last applied generation is older re-bakes everything.
	uint32_t hashOffset;         // a uint64_t per decal (record order): FNV-1a 64 of its content (pass, priority, the
	                             // EXE's three handles, rectangle, triangles, vertex bytes); 16-byte aligned
	// ---- added by 4111 in reserved (2026-11-15; terrain-decals.md 14) ----
	uint32_t areaOffset;         // the area block: the EXE's tile+0x300 ("area changed since load", written 1 by the
	uint32_t areaTilesX;         // invalidation 0x4387D0) of every tile of the EXE's tile grid (TR+0x2310 x TR+0x2314
	uint32_t areaTilesY;         // tiles; tile (x, y) has the height-field origin (64 x, 64 y)), one bit per tile: bit
	                             // (y * areaTilesX + x) % 8 of byte (y * areaTilesX + x) / 8, the unused bits of the last
	                             // byte 0; ceil(areaTilesX * areaTilesY / 8) bytes at areaOffset, 16-byte aligned. All
	                             // three 0: no block (the tap could not see the tile grid, or an image of 4102's tap):
	                             // the server keeps its own rule. A generation is also published when only these bits
	                             // changed (the decal records the same).
	// ---- added by 0366 in reserved (2026-11-15; terrain-decals.md 18) ----
	uint32_t walkOffset;         // the walk block (VPShellDecalWalkHeader, then one VPShellDecalWalk per record, record
	                             // order), 16-byte aligned; 0 = none (an image of an older tap, or the index did not match
	                             // the records): the server keeps its own tie rule
	uint32_t reserved[2];
};

// The walk block (0366): what the EXE's draw needs to order a tile's decals exactly (terrain-decals.md 18). The small
// decals sit in the spatial index TDS+0x6C (a grid of cells, each a list newest add first); the large ones in the large
// list (add order). The draw walks the cells of the tile rectangle grown by 256 rows outer, columns inner, each list from
// its head, takes the points inside the query, appends the large list, and sorts by priority with the library introsort.
#define VPSHELL_DECAL_WALK_LARGE 0xFFFFFFFFu     /* VPShellDecalWalk::cell of a decal in the large list */
#define VPSHELL_DECAL_WALK_MAX_CELLS (1u << 20)  /* cols x rows */
struct VPShellDecalWalkHeader   // 32 bytes at walkOffset
{
	float origin[2];             // TDS+0x6C, +0x70: the index's minimum (the world rectangle's min)
	float inverse[2];            // TDS+0x7C, +0x80: cells per sample (cols / (max - min)), as the EXE holds them
	uint32_t cols, rows;         // TDS+0x88, +0x8C
	uint32_t smallCount, largeCount;   // records in the index and in the large list (sum = decalCount)
};
struct VPShellDecalWalk   // 16 bytes per record, in record order
{
	uint32_t cell;               // small: cy * cols + cx of the cell whose list holds it; VPSHELL_DECAL_WALK_LARGE: large
	uint32_t rank;               // small: its place in that cell's list from the head (0 = the newest add); large: its
	                             // place in the large list from its head
	float point[2];              // small: the index item's point (the rectangle's centre as AddDecal stored it); large: 0
};

struct VPShellDecalRecord   // one live decal (record bit 31 of +0x04); 48 bytes
{
	float rect[4];               // record +0x24..+0x30: min x, min y, max x, max y (height-field samples)
	float priority;              // record +0x20 (= the draw's register 6.x; the draw order is ascending)
	uint32_t flags;              // bit 0: the pass bit (record +0x08 bit 1): 0 blend targets, 1 macro shadow
	uint32_t texture[3];         // albedo, height, spec: index into the name table, or VPSHELL_DECAL_NO_TEXTURE
	uint32_t firstVertex;        // into the vertex block; the decal owns 3 * triangles vertices
	uint32_t triangles;
	uint32_t listOrder;          // the order the EXE's draw collects (terrain-decals.md 6.1 point 3); ties keep it
};

// A path is either a file name as the engine loaded it (resolve by file name, case-insensitively, through the game's
// file system) or a reference into farm_types.xml, which the EXE loads without keeping a name (terrain-decals.md 8.3):
// "@farm_types.xml/<category element>/<child index>/<attribute>", e.g. "@farm_types.xml/Crops_Africa/2/albedo" = the
// attribute `albedo` of the third child of <Crops_Africa> in the farm_types.xml the EXE read (mods override the stock
// file in Resource/Common/MiscData.fpk: Vox Populi imports its own). (Added by 4102, 2026-11-15.)
struct VPShellDecalName   // one texture: the EXE's handle and the file name it was loaded from
{
	uint32_t handle;
	uint32_t pad0;
	char path[VPSHELL_DECAL_NAME_CHARS];   // as the engine loaded it (e.g. "roadsandrails_new_d.dds"); resolved by c5r::gamefs
};

// The vertex block holds the EXE's 8-byte vertices unchanged: half2 position normalised to the decal's rect, half2 uv.
// The changed block holds float[4] rectangles.
#pragma pack(pop)

static_assert(sizeof(VPShellDecalTapControl) <= VPSHELL_DECALTAP_CONTROL_BYTES, "control block too big");
static_assert(offsetof(VPShellDecalTapControl, imageBytes) == 32, "layout");
static_assert(offsetof(VPShellDecalTapControl, imageName) == 80, "layout");
static_assert(sizeof(VPShellDecalImage) == 112, "layout");
static_assert(sizeof(VPShellDecalRecord) == 48, "layout");
static_assert(sizeof(VPShellDecalName) == 8 + VPSHELL_DECAL_NAME_CHARS, "layout");
static_assert(offsetof(VPShellDecalTapControl, changedSince) == 220 && sizeof(VPShellDecalTapControl) == 336, "layout (4102)");
static_assert(offsetof(VPShellDecalImage, changedSince) == 80, "layout (4102)");
static_assert(offsetof(VPShellDecalImage, areaOffset) == 88 && offsetof(VPShellDecalImage, areaTilesY) == 96, "layout (4111)");
static_assert(offsetof(VPShellDecalTapControl, areaSet) == 236, "layout (4111)");
static_assert(offsetof(VPShellDecalTapControl, walkSmall) == 248 && sizeof(VPShellDecalTapControl) == 336, "layout (0366)");
static_assert(offsetof(VPShellDecalImage, walkOffset) == 100 && sizeof(VPShellDecalWalkHeader) == 32 &&
              sizeof(VPShellDecalWalk) == 16, "layout (0366)");
static_assert(offsetof(VPShellDecalTapControl, walkHolds) == 260 && sizeof(VPShellDecalTapControl) == 336, "layout (0406)");
#define VPSHELL_DECAL_MAX_TILES_XY 1024u         /* per axis of the area block (the EXE's grid: 128 x 84 on huge) */
