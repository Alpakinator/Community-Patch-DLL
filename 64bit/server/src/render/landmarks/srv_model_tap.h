// srv_model_tap.h - server.exe's side of the model tap (epic 0561, ticket 4875): the reader of the compositor's tap frames
// and the publisher of the identity set. Written 2026-11-18 for this repository (GPL-3.0-or-later), by the pattern of
// srv_decal_tap.h. Layout and protocol: shell/common/shell_model_tap.h (4874); the plan: research/05-graphics-composition/
// city-renderer.md 2.2-2.4, 3.2-3.4; this side: section 11.
//
// ModelTapReader (render thread): maps the control block Local\VPShellModelTap.<exe pid> (compositor-created, read-write:
// the consumer fields are the server's) and the frame section Local\VPShellModelTapData.<exe pid> (read-only, mapped
// whole once). poll(): a seqlocked copy of the control block; when it names a frame newer than the last one acked, the
// slot is checked and COPIED out first, then the section-level checks run on the copy (lead2-0561 #6146: the content
// checks live once, in c5r::landmarks::readTap, which the caller's Validator runs on the copy):
//   control block: magic (-4), version (-4), exePid, slots = VPSHELL_MODELTAP_SLOTS, slotBytes within the bounds
//   (BadLayout, MREF_BLOCK); the frame section opens and holds slots x slotBytes (-2, MREF_OPEN); slot = frameSeq % slots,
//   headerBytes <= frameBytes <= slotBytes (BadLayout, MREF_SLOT); after the copy the control block was not torn under it:
//   the producer had not published frameSeq + 2 (which starts the overwrite of our slot) and its seq was even (-3,
//   MREF_TORN); the copy's header frameSeq equals the block's (-3, MREF_SEQ) and its fnv1a64 the block's (-1, MREF_HASH);
//   then the Validator (its c5r::Err as ackResult > 0, MREF_CONTENT).
// A refused frame is acked with its code, counted by reason, and the last good frame stays in use; an accepted one
// replaces it whole. Ack fields: ackServerPid, ackResult, then ackFrameSeq last. Never throws.
//
// The file form (`modeltap/frame<NNNNNN>.mtap` in a scene bundle, the frame image as is): loadImage() runs the Validator
// alone (no block: the bundle's SHA-256 covers the file).
//
// ModelSetPublisher: creates Local\VPShellModelSet.<exe pid> (VPSHELL_MODELSET_MAX_BYTES, pagefile-backed), writes a
// generation under its seqlock (seq odd while writing; header, keys, hash), then the control block's setGeneration last;
// the compositor's verdict comes back in the control block (setGenerationApplied, setResult), read by acked().
#pragma once
#include "srv_render_common.h"
#include "shell_model_tap.h"
#include "shell_model_scene.h"
#include "c5r/status.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace srvmodels {

// ackResult values beside c5r::Err (> 0), as shell_model_tap.h names them.
enum { MTACK_OK = 0, MTACK_HASH = -1, MTACK_OPEN = -2, MTACK_TORN = -3, MTACK_VERSION = -4 };

enum ModelTapRefusal : uint32_t
{
	MREF_NONE = 0,
	MREF_BLOCK_MAGIC,   // control block magic                                     -4
	MREF_BLOCK_VERSION, // control block version                                   -4
	MREF_BLOCK,         // control block: exePid, slots, slotBytes                 BadLayout
	MREF_OPEN,          // the frame section does not open, map or hold the slots  -2
	MREF_SLOT,          // slot index, frameBytes against headerBytes / slotBytes  BadLayout
	MREF_TORN,          // the producer moved on into our slot while we copied     -3
	MREF_SEQ,           // the copy's frameSeq is not the block's                   -3
	MREF_HASH,          // the copy's fnv1a64 is not the block's                    -1
	MREF_CONTENT,       // the Validator (readTap's checks) refused it              its c5r::Err
	MREF_MEMORY,        // the copy could not be allocated                          OutOfMemory
	MREF_COUNT_
};
const char* ModelTapRefusalName(uint32_t r);
int32_t ModelTapAckResult(uint32_t r, c5r::Err content);

// The content checks and whatever the caller builds from an accepted image (the layer's readTap + resolve). Called on
// the copy; must not keep the pointer.
typedef c5r::Status (*ModelTapValidator)(void* ctx, const uint8_t* image, size_t n);

struct ModelTapCounters
{
	uint64_t polls = 0;
	uint64_t controlOpens = 0, dataOpens = 0;
	uint64_t never = 0;               // polls with the control block never written (seq 0)
	uint64_t busy = 0;                // control block busy or changing under the seqlock: read again at the next poll
	uint64_t notRunning = 0;          // polls with state != RUNNING
	uint64_t accepted = 0, refused = 0;
	uint64_t refusedBy[MREF_COUNT_] = {};
	uint64_t contentBy[16] = {};      // MREF_CONTENT by c5r::Err
	uint64_t skipped = 0;             // published frames never read (the producer was faster than our polls)
	uint64_t fileLoads = 0, fileRefused = 0;
	uint64_t bytesCopied = 0;
	double lastCopyMs = 0, maxCopyMs = 0;   // copy + checks + Validator of the last / slowest frame (indication)
};

class ModelTapReader
{
public:
	explicit ModelTapReader(uint32_t exePid);
	// tests: other section names
	ModelTapReader(uint32_t exePid, const std::wstring& controlName, const std::wstring& dataName);
	~ModelTapReader();
	ModelTapReader(const ModelTapReader&) = delete;
	ModelTapReader& operator=(const ModelTapReader&) = delete;

	void setValidator(ModelTapValidator v, void* ctx)
	{
		m_val = v;
		m_valCtx = ctx;
	}
	// Every loop iteration. True when a new frame was accepted (image() replaced).
	bool poll();
	// The control block opened (retried every 250 ms) and the want bits written, no frame read. True when it is open.
	bool openOnly() { return m_block || openControl(); }
	// The file form: the image bytes as is, through the Validator only. Applies like poll().
	c5r::Status loadImage(const uint8_t* image, size_t n);

	// The newest accepted image (null before one), its frameSeq and the QPC of its acceptance.
	std::shared_ptr<const std::vector<uint8_t>> image() const { return m_image; }
	uint64_t frameSeq() const { return m_frameSeq; }
	int64_t acceptedQpc() const { return m_acceptedQpc; }
	bool haveImage() const { return (bool)m_image; }
	// The recorder: the current image as is (NotFound before one; Io on a write failure); path + ".tmp", then moved.
	c5r::Status writeCurrentImage(const std::wstring& path) const;

	// The consumer fields (null control block: remembered and written when it opens)
	void setWantBits(uint32_t bits);
	uint32_t wantBits() const { return m_want; }
	VPShellModelTapControl* control() { return m_block; }   // the publisher's setGeneration and verdict
	bool controlOpen() const { return m_block != nullptr; }
	// The last control block snapshot (zero before one): state, failReason, counters of the producer
	const VPShellModelTapControl& lastBlock() const { return m_last; }
	bool haveBlock() const { return m_haveLast; }

	const ModelTapCounters& counters() const { return m_c; }
	int32_t lastResult() const { return m_lastResult; }
	const std::string& lastWhy() const { return m_lastWhy; }

private:
	bool openControl();
	bool openData(const VPShellModelTapControl& tb, std::string& why);
	void refuse(uint32_t reason, c5r::Err content, const std::string& why, bool fromFile);
	void writeAck(uint64_t frameSeq, int32_t result);
	void accept(std::vector<uint8_t>&& raw, uint64_t frameSeq);

	uint32_t m_pid = 0;
	std::wstring m_controlName, m_dataName;
	HANDLE m_control = nullptr;
	VPShellModelTapControl* m_block = nullptr;
	HANDLE m_data = nullptr;
	const uint8_t* m_view = nullptr;
	size_t m_viewBytes = 0;
	int64_t m_nextOpenQpc = 0;
	uint32_t m_loggedState = 0xFFFFFFFFu;
	uint32_t m_want = 0;
	uint64_t m_ackedSeq = 0;
	VPShellModelTapControl m_last = {};
	bool m_haveLast = false;
	ModelTapValidator m_val = nullptr;
	void* m_valCtx = nullptr;

	std::shared_ptr<const std::vector<uint8_t>> m_image;
	uint64_t m_frameSeq = 0;
	int64_t m_acceptedQpc = 0;
	ModelTapCounters m_c;
	int32_t m_lastResult = 0;
	std::string m_lastWhy;
};

// The file form's writer: the bytes to path + ".tmp", then moved over path (Io on a failure).
c5r::Status WriteModelImage(const std::vector<uint8_t>& image, const std::wstring& path);

// ---- the identity set ----------------------------------------------------------------------------------------------

// What the layer says about one drawable library part (resident, GPU buffers and textures made: city-renderer.md 3.2).
enum class KeyFamily : uint8_t { CityBlock, CityPiece, Landmark };       // the file's ModelClass (library.h)
// the model's Source (library.h); CpuCopied (ticket 4879): a CPU-built mesh's key from a tap record whose buffers the
// server holds from the geometry section (walls: family CityBlock; the Great Wall and bridges: CityPiece); bit cities
enum class KeySource : uint8_t { LandmarkRow, Wonder, Wall, GreatWall, CitySet, CpuCopied };
struct KeyInfo
{
	VPModelKey key;
	KeyFamily family = KeyFamily::Landmark;
	KeySource source = KeySource::LandmarkRow;
	bool skinned = false;
};

struct SetBuildReport
{
	uint32_t in = 0, out = 0;
	uint32_t cities = 0, landmarks = 0;     // keys published per bit
	uint32_t cpuBuilt = 0;                  // the library's wall and Great Wall parts: the EXE builds those meshes itself, so
	                                        // no file key names its draws (left out)
	uint32_t cpuCopied = 0;                 // 4879: CPU-built keys published from the geometry section's buffers
	uint32_t classOff = 0;                  // keys of a bit not wanted (its option 0)
	uint32_t duplicates = 0;                // the same key with the same entry, kept once
	uint32_t conflicts = 0;                 // the same key with another bit or class: left out (the EXE draws it)
	uint32_t noKey = 0;                     // ib or vb 0 (no identity): left out
	uint32_t overLimit = 0;                 // past VPSHELL_MODELSET_MAX_KEYS: left out
};
// The rule of city-renderer.md 3.2 / ticket 4875 item 3: CityBlock -> cities, class BLOCK; CityPiece -> cities, SINGLE;
// Landmark -> landmarks, INST; a skinned part -> exactly one bit by its source (Wonder, CitySet: cities; LandmarkRow:
// landmarks), class SKINNED; Wall and GreatWall sources are CPU-built in the EXE and never in the set (4879). Only bits
// in `bits` (VPMODE_CITIES | VPMODE_LANDMARKS) are published. Sorted by key bytes (deterministic generations).
std::vector<VPModelSetKey> BuildIdentitySet(const std::vector<KeyInfo>& in, uint32_t bits, SetBuildReport* rep);

struct ModelSetCounters
{
	uint64_t creates = 0, createFailed = 0;
	uint64_t published = 0;           // generations written
	uint32_t generation = 0, keys = 0;
	uint64_t verdicts = 0;            // generations the compositor answered
	uint64_t refusedBy[VPMSET_RESULT_COUNT] = {};   // its setResult for our generations, other than OK
	uint32_t lastResult = 0;          // VPShellModelSetResult of the newest verdict for our generation
};

class ModelSetPublisher
{
public:
	explicit ModelSetPublisher(uint32_t exePid);
	ModelSetPublisher(uint32_t exePid, const std::wstring& name);   // tests
	~ModelSetPublisher();
	ModelSetPublisher(const ModelSetPublisher&) = delete;
	ModelSetPublisher& operator=(const ModelSetPublisher&) = delete;

	// Writes a new generation (1, 2, ...) and then control->setGeneration (control null: the section only, the tests'
	// reader side). Refuses more than VPSHELL_MODELSET_MAX_KEYS keys [BadLayout] and a section that cannot be made [Io].
	c5r::Status publish(const std::vector<VPModelSetKey>& keys, VPShellModelTapControl* control);
	// The compositor applied our newest generation with VPMSET_OK (setGenerationApplied = it, setResult OK). Reads the
	// control block's producer fields (a plain read: the verdict is two 32-bit words, re-read until stable).
	bool acked(const VPShellModelTapControl* control);
	uint32_t generation() const { return m_gen; }
	const ModelSetCounters& counters() const { return m_c; }
	const std::wstring& name() const { return m_name; }
	const uint8_t* view() const { return m_view; }   // tests

private:
	bool create();
	std::wstring m_name;
	uint32_t m_pid = 0;
	HANDLE m_sec = nullptr;
	uint8_t* m_view = nullptr;
	uint32_t m_gen = 0;
	uint32_t m_verdictGen = 0;
	uint32_t m_readsAtPublish = 0;
	ModelSetCounters m_c;
};

// The check the compositor runs on a set (seqlock, magic, version, sizes, bounds, hash), for the tests and diagnostics:
// VPMSET_OK or the VPShellModelSetResult that refuses it.
uint32_t CheckModelSet(const uint8_t* section, size_t bytes, uint32_t serverPid, std::vector<VPModelSetKey>* out);

}  // namespace srvmodels
