// srv_ice_tap.h - server.exe's reader of the ice tap and its file form (epic 0169; contract by lead-0169 2026-11-15,
// filled by task 4307).
//
// Producer: shell_composite.dll's ice tap (task 4307, shell/composite/ice_tap.cpp; layout shell/common/shell_ice_tap.h):
// once per load, after CreateTerrain built the ice mesh (polar-ice.md 3, 5, 8), it copies the EXE's IMMUTABLE ice VB / IB
// (through D3D staging copies), the chunk array and the Ice object's fields (+0xFE80..+0x1001F) into an image section and
// publishes it with a generation. This reader copies the image out, checks every byte, builds a c5r::ice::Mesh and swaps it
// in whole; a refused image is counted and the last good mesh stays (D132); an install failure (pattern mismatch, a
// refused first image) is reported through installFailure() / lastWhy() for the D125 route the scene wires (task 4308).
//
// CONTRACT for task 4308 (the scene): the public members below keep their names and meaning. Task 4307 may add members;
// it does not change or remove these. Until 4307 lands, the stub in srv_ice_tap.cpp reports Absent and no mesh.
//
// As built by 4307 (2026-11-15; polar-ice.md 9):
//   poll(): a consistent copy of the control block `Local\VPShellIceTap.<exe pid>` under its seqlock (a torn block is
//   counted and read again at the next poll, never acked); a READY generation not acked yet is opened, its image COPIED
//   out of the section first, then every check of CheckIceImage runs on the copy, the immutable Mesh is built and swapped
//   in, and the generation acked (ackResult, ackServerPid, ackGeneration last). Never throws, never crashes on any byte.
//   States: Absent (no control section yet), Off (ice_tap=0), Failed (installFailure()), Active (a mesh of the producer's
//   current object state is applied), Waiting (anything else: no load yet, a copy in flight, or noMesh()).
//   mesh() is null while the producer says the load has no ice (noMesh()) or the tap failed; a refused image keeps the
//   last good mesh. Draw the ice when mesh() is not null; take D125's route when installFailure() is true.
//   installFailure(): the producer published FAILED for any reason but CHANGED (a pattern, a description, a layout, a
//   fault, a section, the size), or an image was refused before any image was accepted.
// Checks, in order, each with its reason (IceRefusal) and ackResult: control block magic (-4), version (-4), pid, image
// name = the expected one, imageBytes within the limit (BadLayout); generation not newer than the applied one (-3); the
// section opens (-2); the view holds imageBytes (Truncated); image magic, version, header and chunk sizes (-4); FNV-1a 64
// over the image = the block's, a block without a hash refused too (-1); image and block agree on generation, loadSeq,
// pid, chunk count and bytes (BadLayout); fields base and size, stride 8, R16 (-4); counts and sizes (BadLayout); the four
// blocks inside the image after the header, 16-byte aligned, in order, not overlapping (Truncated / BadLayout); fields:
// built flag 1, the count and handles at +0xFF30 / +0xFEE4 / +0xFEE8 = the image's, every float the draw reads finite,
// the mesh box min <= max (BadLayout); chunks: box finite, min <= max, 1+ triangles, start + 3 x triangles within the IB,
// base vertex 0..VB vertices - 1, every index + base vertex below the VB's vertex count (BadLayout).
// The file form (a scene bundle's `ice` member, the image bytes as is) runs the same image checks without the block's:
// no hash (the bundle's SHA-256 covers the file), and the file is exactly imageBytes long.
#pragma once
#include "srv_render_common.h"
#include "shell_ice_tap.h"
#include "c5r/ice/ice.h"
#include "c5r/status.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace srvrender {

// ackResult values beside c5r::Err (> 0), as shell_ice_tap.h names them.
enum { ICEACK_OK = 0, ICEACK_HASH = -1, ICEACK_OPEN = -2, ICEACK_STALE = -3, ICEACK_VERSION = -4 };

// Why an image was refused (4307). Each has its counter (refusedBy) and its ackResult (IceAckResult).
enum IceRefusal : uint32_t
{
	IREF_NONE = 0,
	IREF_BLOCK_MAGIC,   // control block magic or version                                  -4
	IREF_BLOCK,         // control block: pid, limits, image name                           BadLayout
	IREF_STALE,         // generation not newer than the applied one                        -3
	IREF_OPEN,          // the image section (or the file) does not open or map             -2
	IREF_TRUNCATED,     // the view or the file is shorter than the image / a header         Truncated
	IREF_MAGIC,         // image magic                                                      -4
	IREF_VERSION,       // image version                                                    -4
	IREF_SIZES,         // header, chunk, fields, stride or index format sizes              -4
	IREF_HASH,          // FNV-1a 64 of the image                                           -1
	IREF_DISAGREE,      // image and block (or the file size) disagree                      BadLayout
	IREF_COUNT,         // a count or size out of its range                                 BadLayout
	IREF_OFFSET,        // a block past the image's end, inside the header, misaligned, out of order  BadLayout / Truncated
	IREF_FIELDS,        // the Ice fields (built flag, count, handles, a read float, the box) BadLayout
	IREF_CHUNK,         // a chunk's box, triangles, index range or base vertex             BadLayout
	IREF_INDEX,         // an index + base vertex past the VB                               BadLayout
	IREF_MEMORY,        // an allocation failed                                             OutOfMemory
	IREF_COUNT_
};

const char* IceRefusalName(uint32_t r);
int32_t IceAckResult(uint32_t r);   // the ackResult of a refusal (0 for IREF_NONE)
c5r::Err IceErr(uint32_t r);        // the c5r::Err of a refusal (the file form's Status)
uint64_t IceFnv1a64(const uint8_t* p, size_t n);

// The checks and the build, on a copy of the image (n bytes). `block`: the control block it came from (null for a file:
// no hash, n must equal imageBytes). Returns IREF_NONE and fills `out`, or the reason with `why` (out unchanged).
uint32_t CheckIceImage(const uint8_t* image, size_t n, const VPShellIceTapControl* block, c5r::ice::Mesh& out, std::string& why);

class IceTapReader
{
public:
	enum class State { Absent, Off, Waiting, Active, Failed };
	// exePid: the EXE whose ice tap section is read; hdr: the mapped main section (may be null).
	IceTapReader(uint32_t exePid, VPShellHeader* hdr);
	IceTapReader(uint32_t exePid, VPShellHeader* hdr, const std::wstring& controlName);   // 4307: tests, another name
	~IceTapReader();
	IceTapReader(const IceTapReader&) = delete;
	IceTapReader& operator=(const IceTapReader&) = delete;

	// Every render-loop iteration, before the draws: picks up a newer published generation, checks and applies it.
	// Returns true when a mesh newer than `haveGeneration` is ready (mesh() then returns it). Never throws.
	bool poll(uint32_t haveGeneration);
	// The newest applied mesh (null before the first) and its generation (0 before the first). The pointer stays valid
	// until the next poll() that returns true.
	std::shared_ptr<const c5r::ice::Mesh> mesh() const;
	uint32_t generation() const;
	uint32_t loadSeq() const;           // the EXE load the mesh belongs to

	State state() const;
	bool installFailure() const;         // D125: the reason is lastWhy()
	const std::string& lastWhy() const;
	int64_t accepted = 0, refused = 0;

	// The file form (D137, a scene bundle's `ice` member; task 4308 records and plays it): the image bytes as published.
	static c5r::Status loadFile(const std::wstring& path, c5r::ice::Mesh& out, std::string& why);

	// ---- added by 4307 ----
	static c5r::Status loadImage(const uint8_t* image, size_t n, c5r::ice::Mesh& out, std::string& why);
	// The recorder's side: the applied image's bytes as is (null before one) and a write of them (path + ".tmp", then
	// moved over path; NotFound before an image, Io on a write failure).
	std::shared_ptr<const std::vector<uint8_t>> currentImage() const;
	c5r::Status writeCurrentImage(const std::wstring& path) const;
	bool noMesh() const;                 // the producer says this load has no ice mesh (VPICE_NO_MESH)
	int32_t lastResult() const;          // the last ackResult written (0 ok)
	uint32_t producerState() const;      // the control block's state (VPShellIceTapState; 0xFFFFFFFF: none seen)
	uint64_t refusedBy(uint32_t reason) const;
	uint64_t torn = 0, polls = 0;
	double lastApplyMs = 0;              // copy + checks + build of the last accepted image (indication, COMMON 23)

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace srvrender
