// shell_ice_tap.h - the ice tap's shared sections (epic 0169, issue 4307; research/05-graphics-composition/polar-ice.md
// sections 3, 5, 7, 8 and 9). Written 2026-11-15 for this repository (GPL-3.0-or-later). Owner: 4307 (producer
// shell/composite/ice_tap.cpp, reader server/src/render/srv_ice_tap.cpp); later changes add fields only in `reserved` and
// new enum values; the static_asserts stay (they hold on x86 and x64 alike).
//
// Why sections of their own: the main section VPShellSection (shell_shared.h, 4096 bytes) is full up to the overlay tap
// (as the decal and shadow taps).
//   - the control block `Local\VPShellIceTap.<exe pid>` (VPSHELL_ICETAP_CONTROL_BYTES), made by the producer when the
//     tap starts (also with ice_tap=0, to publish OFF) and kept for the process life;
//   - one image section per generation `Local\VPShellIce.<exe pid>.<generation>` (VPShellIceImage, the fields, the chunk
//     records, the VB and IB bytes), written through short-lived views (no address space kept in the 32-bit process).
//     The producer keeps the current generation's section until a newer generation replaces it, the kill switch is set or
//     the process ends (about 1.0 MB on Tiny, 4.5 MiB on huge), so a server started later still reads it.
//
// What is copied (polar-ice.md 3, 7, 8): once per load (the camera block's gridSeq) and again after an `Ice_Reload`
// (the object's handles change), once the built flag Ice+0xFEA5 is 1 and the object has been unchanged for
// ice_settle_frames Presents: the EXE's IMMUTABLE ice VB (handle Ice+0xFEE4, vertex format 0x16, stride 8) and R16 IB
// (handle Ice+0xFEE8) through CopyResource into staging buffers on the EXE's device, the chunk array Ice+0xFF2C (count
// Ice+0xFF30, 0x14 bytes each) with each chunk's draw record +0x0C triangles, +0x10 start index, +0x14 base vertex, and
// the object's fields Ice+0xFE80 .. Ice+0x1001F (0x1A0 bytes) as bytes.
//
// Ack protocol (as the decal tap's): the producer writes the image, then the control block under its seqlock (seq odd
// while writing) with state READY; the server opens `imageName`, copies the image out, checks magic, version, sizes,
// fnv1a64 and every count, applies the mesh whole or not at all, then writes ackResult and ackGeneration (last).
// Nothing of the image is written to disk by the product or committed (D83): the bytes come from the player's EXE. The
// file form (a scene bundle's `ice` member, D137) is the image bytes as is.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define VPSHELL_ICETAP_CONTROL_FMT L"Local\\VPShellIceTap.%u"         /* EXE pid */
#define VPSHELL_ICE_IMAGE_FMT L"Local\\VPShellIce.%u.%u"              /* EXE pid, generation */
#define VPSHELL_ICETAP_CONTROL_BYTES 4096u
#define VPSHELL_ICE_MAGIC 0x43493543u            /* 'C5IC' (control block, image and file form) */
#define VPSHELL_ICE_VERSION 1u                   /* the layout version: a reader refuses any other */
#define VPSHELL_ICE_MAX_BYTES (64ull * 1024 * 1024)   /* huge: 4.77 MB (polar-ice.md 8); room for a bigger map */
#define VPSHELL_ICE_MAX_CHUNKS 4096u             /* huge: 8 */
#define VPSHELL_ICE_FIELDS_BASE 0xFE80u          /* Ice+0xFE80 .. Ice+0x1001F (polar-ice.md 7, 8) */
#define VPSHELL_ICE_FIELDS_BYTES 0x1A0u
#define VPSHELL_ICE_STRIDE 8u                    /* TEXCOORD0 R16G16_SNORM @0, TEXCOORD1 R8G8B8A8_UNORM @4 */
#define VPSHELL_ICE_INDEX_FORMAT 57u             /* DXGI_FORMAT_R16_UINT */
#define VPSHELL_ICE_FAILTEXT 160u

enum VPShellIceTapState
{
	VPICE_NONE = 0,       // nothing published yet (no load, the object not built or still changing)
	VPICE_WRITING = 1,    // a copy is in flight (the previous READY image, if any, stays readable)
	VPICE_READY = 2,      // imageName holds generation `generation` (acked or not)
	VPICE_FAILED = 3,     // failReason, failText: an install failure (PATTERN, DESC) for D125's route, or this load's
	VPICE_OFF = 4,        // ice_tap=0 (the kill switch)
	VPICE_NO_MESH = 5,    // the load has no ice mesh: built flag 0 or chunk count 0 after the settle time (no ice plots)
};

enum VPShellIceTapFail   // details in failText and the DLL log
{
	VPICEF_NONE = 0,
	VPICEF_PATTERN = 1,       // a byte-pattern check of the EXE failed (or the composite did not validate the EXE): for good
	VPICEF_EXCEPTION = 2,     // access violation while reading the EXE's memory: the tap is off for the process
	VPICEF_LAYOUT = 3,        // the object, a chunk, a draw record or a handle is not as polar-ice.md 3 says (this load)
	VPICEF_DESC = 4,          // a buffer's description (the EXE's or D3D's) is not the ice VB's / IB's (this load)
	VPICEF_D3D = 5,           // a staging buffer, the copy's map or the reference buffers failed
	VPICEF_SECTION = 6,       // the control or the image section could not be made or written
	VPICEF_TOO_BIG = 7,       // the image is above ice_max_kb
	VPICEF_CHANGED = 8,       // the object changed while the copy was in flight (a load or Ice_Reload): copied again
	VPICEF_KILLED = 9,        // the kill switch took effect during a copy
};

#pragma pack(push, 8)
struct VPShellIceTapControl   // at offset 0 of the control section
{
	// ---- producer (DLL), seqlock ----
	volatile uint32_t seq;
	uint32_t magic;              // VPSHELL_ICE_MAGIC
	uint32_t version;            // VPSHELL_ICE_VERSION
	uint32_t headerBytes;        // sizeof(VPShellIceTapControl)
	uint32_t state;              // VPShellIceTapState
	uint32_t failReason;         // VPShellIceTapFail
	uint32_t exePid;
	uint32_t generation;         // 1, 2, ... of the image in imageName (0: none yet); part of imageName
	uint32_t loadSeq;            // the camera block's gridSeq of the load the image belongs to
	uint32_t chunkCount;         // = the image's
	uint64_t imageBytes;
	uint64_t fnv1a64;            // FNV-1a 64 over imageBytes of the image
	int64_t readyQpc;
	float copyMs;                // wall time from the copy's start to READY (indication, COMMON 23)
	float copyCpuMs;             // composite CPU time of the copy on the render thread
	uint32_t copies;             // copies started since the DLL started
	uint32_t vbHandle, ibHandle; // the EXE's handles of the copied buffers (Ice+0xFEE4, +0xFEE8)
	uint32_t pad0;
	wchar_t imageName[64];
	char failText[VPSHELL_ICE_FAILTEXT];   // the producer's reason in words (zero-terminated; the D125 report quotes it)
	// ---- consumer (server) ----
	volatile uint32_t ackGeneration;  // written last
	volatile int32_t ackResult;       // 0 ok; > 0 a c5r::Err of the reader; -1 hash; -2 open; -3 stale; -4 version
	volatile uint32_t ackServerPid;
	uint32_t reserved[29];
};

struct VPShellIceImage   // at offset 0 of an image section and of the file form
{
	uint32_t magic;              // VPSHELL_ICE_MAGIC
	uint32_t version;            // VPSHELL_ICE_VERSION
	uint32_t headerBytes;        // sizeof(VPShellIceImage)
	uint32_t chunkBytes;         // sizeof(VPShellIceChunk)
	uint32_t generation, loadSeq, exePid;
	uint32_t chunkCount;
	uint32_t fieldsBase;         // VPSHELL_ICE_FIELDS_BASE
	uint32_t fieldsBytes;        // VPSHELL_ICE_FIELDS_BYTES
	uint32_t vbStride;           // VPSHELL_ICE_STRIDE (the stride table's entry of vertex format 0x16)
	uint32_t ibFormat;           // VPSHELL_ICE_INDEX_FORMAT
	uint32_t vbBytes, ibBytes;   // the buffers' ByteWidth
	uint32_t fieldsOffset, chunkOffset, vbOffset, ibOffset;   // from the image start, 16-byte aligned, in this order
	uint64_t imageBytes;
	uint32_t vbHandle, ibHandle; // the EXE's handles (information; a reader does not use them)
	uint32_t reserved[6];
};

struct VPShellIceChunk   // one chunk: the chunk array's entry and its draw record's three fields; 32 bytes
{
	float box[4];                // chunk +0x00..+0x0C: min x, min y, max x, max y (the draw's cull box)
	uint32_t triangles;          // draw record +0x0C
	uint32_t startIndex;         // draw record +0x10
	int32_t baseVertex;          // draw record +0x14
	uint32_t recordHandle;       // chunk +0x10 (information)
};
#pragma pack(pop)

static_assert(offsetof(VPShellIceTapControl, imageBytes) == 40, "layout");
static_assert(offsetof(VPShellIceTapControl, readyQpc) == 56, "layout");
static_assert(offsetof(VPShellIceTapControl, imageName) == 88, "layout");
static_assert(offsetof(VPShellIceTapControl, failText) == 216, "layout");
static_assert(offsetof(VPShellIceTapControl, ackGeneration) == 376, "layout");
static_assert(sizeof(VPShellIceTapControl) == 504, "layout (x86 and x64 alike)");
static_assert(sizeof(VPShellIceTapControl) <= VPSHELL_ICETAP_CONTROL_BYTES, "control block too big");
static_assert(offsetof(VPShellIceImage, imageBytes) == 72, "layout");
static_assert(sizeof(VPShellIceImage) == 112, "layout (x86 and x64 alike)");
static_assert(sizeof(VPShellIceChunk) == 32, "layout");

// FNV-1a 64, the image hash of both sides
static inline uint64_t VPIceFnv(uint64_t h, const void* p, size_t n)
{
	const unsigned char* b = (const unsigned char*)p;
	for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001B3ull; }
	return h;
}
#define VPICE_FNV_INIT 0xCBF29CE484222325ull
