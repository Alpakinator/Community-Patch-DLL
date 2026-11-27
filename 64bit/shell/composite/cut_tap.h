// cut_tap.h - the cut-mask tap inside shell_composite.dll and its shared sections (ticket 4553, package T4 of epic 0431:
// the stand-in for the tree "cut" mask of research/05-graphics-composition/trees.md 2.3, 10.3 and section 14). Written
// 2026-11-16 for this repository (GPL-3.0-or-later). Producer: shell/composite/cut_tap.cpp; reader:
// server/src/render/srv_cut_tap.cpp (it fills c5r::forest::DenseCut through setFromBlocks). The layout part of this
// header has no Windows dependency and holds on x86 and x64 alike (the static_asserts); later changes add fields only in
// `reserved` and new enum values.
//
// What is copied (trees.md 14.2): the EXE's plot mask grid, the Terrain system's static object cut_grid (made by
// CreateTerrain, freed by the Terrain shutdown): its header (+0x00..+0x2F), the directory of tilesX x tilesY 8-byte
// entries (u16 state: 1 = the whole 16 x 16-cell tile holds the byte at +4, 0 = pooled, u32 pool index at +4) and the pool
// of 256-byte blocks, as the EXE holds them, plus the GridHeightmap's sample counts and the cell rule's origin (ox, oy at
// GridHeightmap +0xB4 / +0xB8). Nothing of the EXE's code is copied; the bytes are the session's map data.
//
// The EXE posts an event (type 0x20) for every stamp, but listening to its queue would mean calling EXE code. So the tap
// watches the grid itself: a worker thread of the tap reads the header, the directory and the pool every cut_poll_ms,
// checks them (the header the same before and after the copy, every state 0 or 1, every pool index below the pool's block
// count, the pooled tiles = the EXE's in-use count), and publishes a new generation when the bytes differ from the last
// published image and have stayed the same for cut_settle_polls polls (or the load changed).
//
// Sections (as the ice tap's, shell/common/shell_ice_tap.h):
//   - the control block `Local\VPShellCutTap.<exe pid>` (VPSHELL_CUTTAP_CONTROL_BYTES), made when the tap starts (also with
//     cut_tap=0, to publish OFF) and kept for the process life;
//   - one image section per generation `Local\VPShellCut.<exe pid>.<generation>` (VPShellCutImage, the directory, the pool),
//     written through short-lived views. The producer keeps the newest generation and the one before it.
// Ack protocol: the producer writes the image, then the control block under its seqlock (seq odd while writing) with
// state READY; the server opens `imageName`, copies the image out, checks it, applies it whole or not at all, then writes
// ackResult and ackGeneration (last). Nothing of an image is written to disk by the product or committed (D83).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define VPSHELL_CUTTAP_CONTROL_FMT L"Local\\VPShellCutTap.%u"   /* EXE pid */
#define VPSHELL_CUT_IMAGE_FMT L"Local\\VPShellCut.%u.%u"        /* EXE pid, generation */
#define VPSHELL_CUTTAP_CONTROL_BYTES 4096u
#define VPSHELL_CUT_MAGIC 0x54433543u             /* 'C5CT' (control block, image) */
#define VPSHELL_CUT_VERSION 1u                    /* the layout version: a reader refuses any other */
#define VPSHELL_CUT_MAX_BYTES (64ull * 1024 * 1024)   /* huge: about 0.35 MB of directory plus the pool */
#define VPSHELL_CUT_MAX_TILES 1024u               /* per axis (c5r::forest::DenseCut's limit) */
#define VPSHELL_CUT_TILE 16u                      /* cells per tile edge */
#define VPSHELL_CUT_BLOCK 256u                    /* bytes per pooled block */
#define VPSHELL_CUT_DIR_ENTRY 8u                  /* bytes per directory entry */
#define VPSHELL_CUT_GRID_HDR 0x30u                /* the grid object's header bytes copied (+0x00..+0x2F) */
#define VPSHELL_CUT_FAILTEXT 160u

// The grid object's fields (trees.md 14.2), offsets into VPShellCutImage::gridHeader.
#define VPCUT_G_MADE 0x00u        /* byte: 1 = made by CreateTerrain (the grid's make), 0 = freed (its free) */
#define VPCUT_G_WIDTH 0x08u       /* cells, a multiple of 16 */
#define VPCUT_G_HEIGHT 0x0Cu
#define VPCUT_G_TILESX 0x10u
#define VPCUT_G_TILESY 0x14u
#define VPCUT_G_DIR 0x18u         /* pointer (EXE address; information in the image) */
#define VPCUT_G_FREE 0x1Cu        /* the pool's free-list head, 0x0FFFFFFF = none */
#define VPCUT_G_USED 0x20u        /* pooled blocks in use = pooled tiles */
#define VPCUT_G_POOL 0x24u        /* pointer (information) */
#define VPCUT_G_BLOCKS 0x28u      /* blocks made (the pool's size) */
#define VPCUT_G_CAPACITY 0x2Cu
#define VPCUT_NIL 0x0FFFFFFFu

enum VPShellCutTapState
{
	VPCUT_NONE = 0,      // nothing published yet
	VPCUT_READY = 2,     // imageName holds generation `generation` (acked or not)
	VPCUT_FAILED = 3,    // failReason, failText: PATTERN or EXCEPTION are an install failure for D125's route
	VPCUT_OFF = 4,       // cut_tap=0 (the kill switch): the server draws with an empty mask, LOUD
	VPCUT_NO_GRID = 5,   // no map loaded, or the grid is not made (GridHeightmap pointer 0, made byte 0)
};

enum VPShellCutTapFail   // details in failText and the DLL log
{
	VPCUTF_NONE = 0,
	VPCUTF_PATTERN = 1,     // a byte-pattern check of the EXE failed (or the composite did not validate the EXE): for good
	VPCUTF_EXCEPTION = 2,   // access violation while reading the EXE's memory: the tap is off for the process
	VPCUTF_SECTION = 6,     // the control or the image section could not be made or written (this image)
	VPCUTF_TOO_BIG = 7,     // the image is above cut_max_kb (this image)
};

#pragma pack(push, 8)
struct VPShellCutTapControl   // at offset 0 of the control section
{
	// ---- producer (DLL), seqlock ----
	volatile uint32_t seq;
	uint32_t magic;              // VPSHELL_CUT_MAGIC
	uint32_t version;            // VPSHELL_CUT_VERSION
	uint32_t headerBytes;        // sizeof(VPShellCutTapControl)
	uint32_t state;              // VPShellCutTapState
	uint32_t failReason;         // VPShellCutTapFail
	uint32_t exePid;
	uint32_t generation;         // 1, 2, ... of the image in imageName (0: none); part of imageName
	uint32_t loadSeq;            // the camera block's gridSeq of the load the image was taken in
	uint32_t tilesX, tilesY;     // = the image's
	uint32_t poolBlocks;         // = the image's
	uint64_t imageBytes;
	uint64_t fnv1a64;            // FNV-1a 64 over imageBytes of the image
	int64_t readyQpc;
	uint32_t polls;              // polls of the worker since the tap started
	uint32_t refusedReads;       // polls whose copy failed a check (a torn read: read again at the next poll)
	float lastPollMs;            // the worker's last poll (read, checks, compare), indication (COMMON 23)
	float maxPollMs;
	wchar_t imageName[64];
	char failText[VPSHELL_CUT_FAILTEXT];   // the producer's reason in words (zero-terminated; the D125 report quotes it)
	// ---- consumer (server) ----
	volatile uint32_t ackGeneration;  // written last
	volatile int32_t ackResult;       // 0 ok; > 0 a c5r::Err of the reader; -1 hash; -2 open; -3 stale; -4 version
	volatile uint32_t ackServerPid;
	uint32_t reserved[29];
};

struct VPShellCutImage   // at offset 0 of an image section
{
	uint32_t magic;              // VPSHELL_CUT_MAGIC
	uint32_t version;            // VPSHELL_CUT_VERSION
	uint32_t headerBytes;        // sizeof(VPShellCutImage)
	uint32_t generation, loadSeq, exePid;
	uint32_t widthCells, heightCells;   // grid +0x08 / +0x0C (multiples of 16)
	uint32_t tilesX, tilesY;            // grid +0x10 / +0x14
	uint32_t samplesX, samplesY;        // GridHeightmap +0x0C / +0x10 (width = roundup16(samplesX >> 1))
	float ox, oy;                       // GridHeightmap +0xB4 / +0xB8: the cell rule's origin (cut.h)
	uint32_t poolBlocks;                // grid +0x28
	uint32_t pooledTiles;               // directory entries of state 0 (= grid +0x20)
	uint32_t dirOffset, dirBytes;       // tilesX x tilesY x 8, from the image start, 16-byte aligned
	uint32_t poolOffset, poolBytes;     // poolBlocks x 256, after the directory, 16-byte aligned
	uint64_t imageBytes;
	uint32_t treeCells;                 // cells with any of bits 0-2 (information: what hides trees)
	uint32_t setCells;                  // cells not 0 (information)
	uint8_t gridHeader[VPSHELL_CUT_GRID_HDR];   // the grid object's +0x00..+0x2F as read (pointers are EXE addresses)
	uint32_t reserved[4];
};
#pragma pack(pop)

static_assert(offsetof(VPShellCutTapControl, imageBytes) == 48, "layout");
static_assert(offsetof(VPShellCutTapControl, readyQpc) == 64, "layout");
static_assert(offsetof(VPShellCutTapControl, imageName) == 88, "layout");
static_assert(offsetof(VPShellCutTapControl, failText) == 216, "layout");
static_assert(offsetof(VPShellCutTapControl, ackGeneration) == 376, "layout");
static_assert(sizeof(VPShellCutTapControl) == 504, "layout (x86 and x64 alike)");
static_assert(sizeof(VPShellCutTapControl) <= VPSHELL_CUTTAP_CONTROL_BYTES, "control block too big");
static_assert(offsetof(VPShellCutImage, imageBytes) == 80, "layout");
static_assert(offsetof(VPShellCutImage, gridHeader) == 96, "layout");
static_assert(sizeof(VPShellCutImage) == 160, "layout (x86 and x64 alike)");

// FNV-1a 64, the image hash of both sides
static inline uint64_t VPCutFnv(uint64_t h, const void* p, size_t n)
{
	const unsigned char* b = (const unsigned char*)p;
	for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001B3ull; }
	return h;
}
#define VPCUT_FNV_INIT 0xCBF29CE484222325ull

// ---- the producer's entry points (shell_composite.dll only) -------------------------------------------------------------
// The composite calls these on its render thread. CutTap_OnPresent is cheap (a few stores and, once, the worker's start);
// the reads, checks, hash and section writes run on the tap's own worker thread (below-normal priority). The worker reads
// EXE memory only through SEH-guarded loops after the byte patterns matched, calls no EXE function and writes nothing into
// the EXE.
//
// Keys it reads itself from [composite] of shell_composite.ini (or VPSHELL_<KEY>), once at its start:
//   cut_poll_ms (500)       the time between two polls of the worker
//   cut_settle_polls (2)    polls with the same bytes before a changed grid is published
//   cut_max_kb (65536)      the largest image published (above: FAILED TOO_BIG for that image, the last one stays)
//   cut_loud_polls (4)      the same failed check this many polls in a row is LOUD (once per run; not a torn read)
//   cut_torn_loud_s (120)   torn reads at every poll for this long are LOUD (once per run: the EXE never settles; 0500)
//   cut_test=<mode>         test only: a synthetic grid in this DLL's memory (cut_tap.cpp, kTestNames)
// The switch is cut_tap (Config::cutTap of shell_composite.cpp): 1 = on; 0 = OFF published, nothing read (the server's
// reader is LOUD once about it).
struct CutTapEnv
{
	uint32_t exeBase;             // module base of the EXE (addresses are rebased on it)
	uint32_t exeValidated;        // 1 when the composite validated the EXE image (exe_camera.h patterns)
	uint32_t gridValid;           // the camera block's gridValid: a map is loaded
	uint32_t gridSeq;             // the camera block's gridSeq: the load
	int enabled;                  // cut_tap=1 in the ini
	void (*log)(const char* fmt, ...);
};

// Render thread, every Present of the target swap chain, after the camera block was published.
void CutTap_OnPresent(const CutTapEnv& env);

// The composite releases its resources: nothing of the tap depends on the device; the worker goes on.
void CutTap_Shutdown(const char* why);

// Test hosts only: stop the worker and wait for it (at most `ms`), so that the counters are final.
void CutTap_StopForTest(uint32_t ms);
