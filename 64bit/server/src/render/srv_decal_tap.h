// srv_decal_tap.h - server.exe's reader of the decal tap and its file form (epic 0166, issue 4103).
//
// Producer: shell_composite.dll (issue 4102). Layout and ack protocol: shell/common/shell_decal_tap.h (control block
// `Local\VPShellDecalTap.<exe pid>`, one image section per generation `Local\VPShellDecal.<exe pid>.<generation>`). What
// the records hold: research/05-graphics-composition/terrain-decals.md sections 0.2, 1.2 and 6; this reader: section 10.
//
// poll(): a consistent copy of the control block under its seqlock (a torn or in-progress block is counted and read again
// at the next poll, never acked); a READY generation not acked yet is opened, its image COPIED out of the section first
// (so a writer cannot change a byte after its check), then every check below runs on the copy, the immutable
// c5r::decal::DecalSet is built and swapped in whole, and the generation acked (ackResult, ackServerPid, ackGeneration
// last). A refused image is acked with its code, counted by reason, and the last good set stays. Never throws, never
// crashes on any byte content.
//
// Checks, in order, each with its reason (DecalRefusal) and its ackResult:
//   control block: magic (-4), exePid, decalCount and imageBytes within the limits, imageName = the expected name of
//   (pid, generation) (BadLayout); generation not newer than the applied one (-3, stale); the section opens (-2); the
//   view holds imageBytes (Truncated); image: magic (-4), version (-4), header and record sizes (-4), the image hash
//   FNV-1a 64 over imageBytes = the block's, a block without a hash refused too (-1); image and block agree on bytes,
//   generation, loadSeq, decalCount (BadLayout); counts within the limits (BadLayout); every block (decals, names,
//   vertices, changed) inside the image after the header (Truncated), 16-byte aligned and not overlapping (BadLayout);
//   world rectangle finite and min <= max; every record: rectangle finite, min <= max, inside the world rectangle with a
//   margin of 1 sample, priority finite, flags only bit 0, 1 <= triangles <= 21,844 (3 x triangles <= 0xFFFE), the vertex
//   range inside the vertex block, texture indices < nameCount or VPSHELL_DECAL_NO_TEXTURE; every name zero-terminated
//   and not empty; every changed rectangle finite, min <= max, inside the world rectangle with the margin (BadLayout);
//   4111: the area block (optional: offset and both tile counts 0 = none, an image of 4102's tap): its grid equal to the
//   world rectangle's tiles (floor(max / 64) - floor(min / 64) + 1 per axis, at most 1024), inside the image after the
//   header, aligned, not overlapping another block, and no bit set past the last tile (BadLayout, DREF_AREA; past the
//   image: Truncated).
//   0366: the walk block (optional: walkOffset 0 = none, an image of an older tap or one whose index did not match): its
//   header and one entry per record inside the image after the header, aligned, not overlapping (Truncated / BadLayout);
//   1 .. 2^20 cells, origin and inverse finite, inverse > 0, small + large = decalCount; each small record in a cell of
//   the grid with a finite point; the ranks of every cell and of the large list exactly 0 .. count - 1 (BadLayout,
//   DREF_WALK). An accepted walk makes decalsForTile use the EXE's own order (terrain-decals.md 18).
// The file form (`decals/load<NNNN>.dtap`, the image bytes as is) runs the same image checks without the block's:
// no hash (the bundle's SHA-256 covers the file), and the file is exactly imageBytes long.
#pragma once
#include "srv_render_common.h"
#include "shell_decal_tap.h"
#include "c5r/decal/decal.h"
#include "c5r/status.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace srvrender {

// ackResult values beside c5r::Err (> 0), as the header shell_decal_tap.h names them.
enum { DTACK_OK = 0, DTACK_HASH = -1, DTACK_OPEN = -2, DTACK_STALE = -3, DTACK_VERSION = -4 };

// Why an image was refused. Each has its counter (DecalTapCounters::refusedBy) and its ackResult (DecalAckResult).
enum DecalRefusal : uint32_t
{
	DREF_NONE = 0,
	DREF_BLOCK_MAGIC,   // control block magic                                          -4
	DREF_BLOCK,         // control block: pid, limits, image name                         BadLayout
	DREF_STALE,         // generation not newer than the applied one                      -3
	DREF_OPEN,          // the image section does not open or map                         -2
	DREF_TRUNCATED,     // the view (or the file) is shorter than the image               Truncated
	DREF_MAGIC,         // image magic                                                    -4
	DREF_VERSION,       // image version                                                  -4
	DREF_SIZES,         // header or record size                                          -4
	DREF_HASH,          // FNV-1a 64 of the image                                         -1
	DREF_DISAGREE,      // image and block (or file size) disagree                        BadLayout
	DREF_COUNT,         // a count above its limit                                        BadLayout
	DREF_OFFSET,        // a block past the image's end or inside the header              Truncated
	DREF_ALIGN,         // a block not 16-byte aligned, or two blocks overlap             BadLayout
	DREF_WORLD,         // world rectangle                                                BadLayout
	DREF_RECT,          // a record's rectangle                                           BadLayout
	DREF_RECORD,        // a record's priority, flags or triangle count                  BadLayout
	DREF_VERTEX,        // a record's vertex range                                        BadLayout
	DREF_TEXTURE,       // a record's texture index                                       BadLayout
	DREF_NAME,          // a name not terminated or empty                                 BadLayout
	DREF_CHANGED,       // a changed rectangle                                            BadLayout
	DREF_MEMORY,        // an allocation failed                                           OutOfMemory
	DREF_AREA,          // 4111: the area block's grid, its size or a bit past the map's tiles  BadLayout
	DREF_WALK,          // 0366: the walk block's grid, counts, cells, ranks or points           BadLayout
	DREF_COUNT_
};

const char* DecalRefusalName(uint32_t r);
int32_t DecalAckResult(uint32_t r);   // the ackResult of a refusal (0 for DREF_NONE)
c5r::Err DecalErr(uint32_t r);        // the c5r::Err of a refusal (the file form's Status)

uint64_t DecalFnv1a64(const uint8_t* p, size_t n);

// One applied generation's changes: the tiles to re-bake (4105) are c5r::decal::tilesForRects(rects, worldRect).
struct DecalChange
{
	uint32_t generation = 0;
	uint32_t previousGeneration = 0;     // 0: no set before this one
	bool all = false;                    // the queue overflowed: re-bake every tile that has or had decals
	float worldRect[4] = {0, 0, 0, 0};
	std::vector<c5r::decal::DecalRect> rects;   // the image's changed rectangles, plus every added or removed decal's
	                                            // rectangle the image did not list (a skipped generation, a first set)
	// 4111: the tiles whose area state (DecalAreaAt) differs between the previous set and this one: the EXE's tile+0x300
	// that changed (a generation may change only these), or the rule that changed with it (a set with an area block after
	// one without). Tile coordinates, not origins. Re-bake them too.
	std::vector<c5r::decal::TileOrigin> areaTiles;
	// 0393: of `rects`, the last tieTiles are one point rectangle each (the centre of a tile, so tilesForRects gives that
	// tile alone): the tiles whose draw list changed because the tie rule did (the walk block appeared or went between
	// the previous set and this one, c5r::decal::tieRuleTiles). 0 when the rule is the same.
	uint32_t tieTiles = 0;
};

// 4111: a tile's area state under a set: the EXE's tile+0x300 when the set carries the area block, else the server's own
// rule (4105: the tile has decals). False for a null set.
bool DecalAreaAt(const c5r::decal::DecalSet* set, int tileX, int tileY);

struct DecalTapCounters
{
	uint64_t polls = 0;
	uint64_t controlOpens = 0;      // the control section was opened (once per producer)
	uint64_t torn = 0;              // control block busy or changing under the seqlock: read again at the next poll
	uint64_t accepted = 0, refused = 0;
	uint64_t refusedBy[DREF_COUNT_] = {};
	uint64_t fileLoads = 0, fileRefused = 0;
	uint64_t changesQueued = 0, changeOverflow = 0;
	uint64_t rectsNotListed = 0;    // added or removed decals whose rectangle the image's changed block did not hold
	uint64_t decals = 0;            // of the current set
	uint64_t areaSets = 0;          // 4111: accepted sets with the area block (the EXE's tile+0x300)
	uint64_t walkSets = 0;          // 0366: accepted sets with the walk block (decalsForTile in the EXE's own order)
	uint64_t areaTilesChanged = 0;  // 4111: tiles whose area state changed between sets (DecalChange::areaTiles)
	uint32_t areaSet = 0;           // 4111: tiles of the current set with tile+0x300 = 1
	double lastApplyMs = 0;         // copy + checks + build of the last accepted image (indication, COMMON 23)
	// 0365: accepted sets built from the previous one's (the delta), built whole, and deltas that fell back to the whole
	// path (deltaRefused: the image did not match its changed rectangles; the others: not tried, deltaWhy says why)
	uint64_t deltaSets = 0, wholeSets = 0, deltaRefused = 0, deltaGridWhole = 0;
	double lastTotalMs = 0;         // 0365: the whole apply of the last accepted image (lastApplyMs plus the change queue)
	// 0393: sets applied after one with the other tie rule (the walk block appeared or went), the tiles they raised for
	// it (DecalChange::tieTiles) and the time of the last comparison (c5r::decal::tieRuleTiles, inside lastTotalMs)
	uint64_t tieRuleChanges = 0, tieRuleTiles = 0;
	double lastTieRuleMs = 0;
};

// 0365: what the delta did for one image (terrain-decals.md 19).
struct DecalDeltaInfo
{
	bool tried = false;             // eligible: the same load, layout and a changed list based on the previous set
	bool used = false;              // hashes and the index came from the previous set
	bool gridFromPrev = false;      // the grid too (else buildIndex's whole grid, gridWhy)
	uint32_t touchedPrev = 0, touchedNew = 0;   // decals whose rectangle is one of the changed rectangles
	uint32_t reordered = 0;         // kept decals found out of their order (matched by the fallback search)
	uint32_t cellsSorted = 0;
	std::string why;                // not tried / refused: why (empty when used)
	std::string gridWhy;
};

// The checks and the build, on a copy of the image (n bytes). `block`: the control block it came from (null for a
// file: no hash, n must equal imageBytes). Returns DREF_NONE and fills `set` (index built) and `changed`, or the
// reason with `why`. Never throws.
uint32_t CheckDecalImage(const uint8_t* image, size_t n, const VPShellDecalTapControl* block, c5r::decal::DecalSet& set,
                         std::vector<c5r::decal::DecalRect>& changed, std::string& why);
// 0365: the same checks; with `prev` (the set in force) the content hashes and the index of the decals the image's
// changed rectangles do not name are taken from prev instead of computed (the delta), when the image is of prev's load
// and layout, its changedSince is in (0, prev's generation], it is newer than prev and its changes are at most
// kDecalDeltaMaxShare of its decals. Every decal the changed rectangles do not name must equal one of prev's that they do
// not name either (rectangle, priority, pass, texture names, vertex bytes), one to one; else the delta is refused and
// the whole path runs (the same result, info->why says why). The set is the same as without prev, field for field.
uint32_t CheckDecalImage(const uint8_t* image, size_t n, const VPShellDecalTapControl* block, c5r::decal::DecalSet& set,
                         std::vector<c5r::decal::DecalRect>& changed, std::string& why, const c5r::decal::DecalSet* prev,
                         DecalDeltaInfo* info);
constexpr double kDecalDeltaMaxShare = 0.25;   // above it the whole path is cheaper than matching (and a load's layout)

// The identity of a decal across generations: FNV-1a 64 of its rectangle, priority, pass, texture names and vertex bytes.
uint64_t DecalContentHash(const c5r::decal::DecalSet& set, const c5r::decal::Decal& d);

class DecalTapReader
{
public:
	explicit DecalTapReader(uint32_t exePid);
	DecalTapReader(uint32_t exePid, const std::wstring& controlName);   // tests: another control section name
	~DecalTapReader();
	DecalTapReader(const DecalTapReader&) = delete;
	DecalTapReader& operator=(const DecalTapReader&) = delete;

	// Every loop iteration (cheap: one seqlocked copy of the control block). True when a new set was applied. poll(),
	// loadFile() and loadImage() run on one thread (the render thread); the getters below may be called from any thread.
	bool poll();
	// The file form: the image bytes as is, through the same image checks (no hash, no ack). Applies like poll().
	c5r::Status loadFile(const std::wstring& path);
	c5r::Status loadImage(const uint8_t* image, size_t n);
	// The recorder's side: the current set's image bytes as is (NotFound before a set; Io on a write failure). Written
	// to path + ".tmp", then moved over path.
	c5r::Status writeCurrentImage(const std::wstring& path) const;

	std::shared_ptr<const c5r::decal::DecalSet> current() const;   // null before the first accepted image
	uint32_t generation() const;                                   // of current(); 0 before one
	// Moves the queued changes (oldest first) into `out`; false when none. Bounded: past 256 entries the queue collapses
	// into one entry with all = true.
	bool drainChanges(std::vector<DecalChange>& out);
	// The per-tile query (c5r::decal::decalsForTile) on the current set; the returned pointer keeps out.set alive (null
	// and an empty list before a set).
	std::shared_ptr<const c5r::decal::DecalSet> decalsForTile(int originX, int originY, c5r::decal::TileDecalList& out) const;
	DecalTapCounters counters() const;
	int32_t lastResult() const;
	std::string lastWhy() const;
	// 0365: the delta on (default) or off (every image built whole, the reader before 0365; tests and comparisons)
	void setDelta(bool on) { m_delta = on; }
	DecalDeltaInfo lastDelta() const;

private:
	bool openControl();
	void apply(c5r::decal::DecalSet&& set, std::vector<c5r::decal::DecalRect>&& changed, std::vector<uint8_t>&& raw,
	           double ms, const DecalDeltaInfo& delta, int64_t q0);
	bool m_delta = true;
	void refuse(uint32_t reason, const std::string& why, bool fromFile);
	void writeAck(uint32_t generation, int32_t result);

	uint32_t m_pid = 0;
	std::wstring m_controlName;
	HANDLE m_control = nullptr;
	VPShellDecalTapControl* m_block = nullptr;
	int64_t m_nextOpenQpc = 0;
	uint32_t m_loggedState = 0xFFFFFFFFu;
	bool m_ackedAny = false;
	uint32_t m_ackedGeneration = 0;

	mutable std::mutex m_mu;   // guards everything below
	std::shared_ptr<const c5r::decal::DecalSet> m_set;
	std::shared_ptr<const std::vector<uint8_t>> m_raw;   // the current set's image bytes (for writeCurrentImage)
	std::vector<DecalChange> m_changes;
	DecalTapCounters m_counters;
	int32_t m_lastResult = 0;
	std::string m_lastWhy;
	DecalDeltaInfo m_lastDelta;
};

}  // namespace srvrender
