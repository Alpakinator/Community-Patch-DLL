// srv_cut_tap.h - server.exe's reader of the cut-mask tap (ticket 4553, package T4 of epic 0431; research/05-graphics-
// composition/trees.md section 14). Written 2026-11-16 for this repository (GPL-3.0-or-later), by the pattern of
// srv_ice_tap.h (epic 0169).
//
// Producer: shell_composite.dll's cut tap (shell/composite/cut_tap.cpp; layout shell/composite/cut_tap.h): the EXE's plot
// mask grid (the Terrain system's object 0xBA392C: a directory of 16 x 16-cell tiles and a pool of 256-byte blocks), copied
// whenever it changes, into an image section per generation. This reader copies the image out, checks every byte, fills a
// c5r::forest::DenseCut through its public setFromBlocks and swaps it in whole; it is the CutSource the tree placement
// reads (bits at a world point; the generation moves on every applied or dropped grid).
//
// Failure statuses (trees.md 9.5, D118, D125, D132):
//   - installFailure(): the producer published FAILED (a byte pattern, an EXE the composite did not validate, a fault, or
//     no image could be made), or the first image was refused before any was accepted. The scene (4554) takes the fatal
//     route (exit 5) on it; lastWhy() is the reason for the D118 report.
//   - in play: a torn control block, or a refused image, is counted (torn, refused, refusedBy) and logged LOUD (the first
//     20, then every 100th); the last good grid stays.
//   - off(): the producer says cut_tap=0 (the test switch): the reader holds an empty grid (nothing cut) and logs LOUD once.
//   - noGrid(): no map loaded or the EXE's grid not made (between loads): the last grid stays until a new one arrives.
// Checks, in order, each with its reason (CutRefusal) and ackResult: control block magic and version (-4); pid, image name =
// the expected one, imageBytes within the limit (BadLayout); generation not newer than the applied one (-3); the section
// opens (-2); the view holds imageBytes (Truncated); image magic, version, header size (-4); FNV-1a 64 over the image = the
// block's (-1); image and block agree on generation, pid, loadSeq, tiles, blocks and bytes (BadLayout); sizes: tiles 1..1024
// per axis, width / height = tiles x 16, roundup16(samples >> 1) = width / height, the origin finite, dirBytes = tiles x 8,
// poolBytes = blocks x 256, blocks <= tiles (BadLayout); offsets: the directory and the pool after the header, 16-byte
// aligned, in order, inside the image, imageBytes = the pool's end (Truncated / BadLayout); the directory: every state 0
// or 1, every pooled index below the blocks, the pooled tiles = the header's count (BadLayout); DenseCut::setFromBlocks
// accepts it (BadLayout).
#pragma once
#include "../../../shell/composite/cut_tap.h"
#include "c5r/forest/cut.h"
#include "c5r/status.h"
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace srvrender {

// ackResult values beside c5r::Err (> 0), as cut_tap.h names them.
enum { CUTACK_OK = 0, CUTACK_HASH = -1, CUTACK_OPEN = -2, CUTACK_STALE = -3, CUTACK_VERSION = -4 };

// Why an image was refused. Each has its counter (refusedBy) and its ackResult (CutAckResult).
enum CutRefusal : uint32_t
{
	CREF_NONE = 0,
	CREF_BLOCK_MAGIC,   // control block magic or version                                   -4
	CREF_BLOCK,         // control block: pid, limits, image name                            BadLayout
	CREF_STALE,         // generation not newer than the applied one                         -3
	CREF_OPEN,          // the image section does not open or map                            -2
	CREF_TRUNCATED,     // the view is shorter than the image / a header                     Truncated
	CREF_MAGIC,         // image magic                                                       -4
	CREF_VERSION,       // image version or header size                                      -4
	CREF_HASH,          // FNV-1a 64 of the image                                            -1
	CREF_DISAGREE,      // image and block disagree                                          BadLayout
	CREF_SIZES,         // tiles, cells, samples, origin, directory or pool sizes            BadLayout
	CREF_OFFSET,        // a block past the image's end, inside the header, misaligned, out of order   BadLayout / Truncated
	CREF_DIR,           // a directory entry's state or pool index, or the pooled count     BadLayout
	CREF_GRID,          // DenseCut::setFromBlocks refused it                                BadLayout
	CREF_MEMORY,        // an allocation failed                                              OutOfMemory
	CREF_COUNT_
};

const char* CutRefusalName(uint32_t r);
int32_t CutAckResult(uint32_t r);   // the ackResult of a refusal (0 for CREF_NONE)
c5r::Err CutErr(uint32_t r);        // the c5r::Err of a refusal

// The checks and the fill, on a copy of the image (n bytes). `block`: the control block it came from (null: no block
// checks, no hash; n must equal imageBytes). Returns CREF_NONE and fills `out` (and `hdr`), or the reason with `why` (out
// unchanged).
uint32_t CheckCutImage(const uint8_t* image, size_t n, const VPShellCutTapControl* block, c5r::forest::DenseCut& out,
                       VPShellCutImage* hdr, std::string& why);

// Ticket 4853 (epic 0528 D, research/05-graphics-composition/farm-fields-layout.md 14): bits the server adds to the EXE's
// grid. With the EXE's farm builder retired ([Renderer] ServerFarmFields = 1) the grid carries no field footprints (bit 0x01)
// any more; the server's farm producer publishes its own (c5r::farmfields::FieldFootprint) here, and every CutTapReader ORs
// the overlay's bits into bitsAt and adds its generation to generation(), so the forest rebuilds its trees when either
// changes. One per process (one Scene); render thread only: the forest's reads and polls and the farm producer's publish
// (Scene::farmDecalPoll) all run there.
class CutOverlay
{
public:
	// null: no overlay (the default). Every call moves generation() on.
	void set(std::shared_ptr<const c5r::forest::CutSource> src);
	// Ticket 4934 (FixRoadLayout, MC #7664 option (A)): the server's road footprint (ORed in as the farm's) and the EXE's own
	// route cells to clear (bit 0x02 where the tap's route curves ran on bare plots, trees.md 14.1): the reader takes the
	// clear source's bits out of the EXE's grid before the overlays are ORed in. null, null: none. Moves generation() on.
	void setRoads(std::shared_ptr<const c5r::forest::CutSource> roads, std::shared_ptr<const c5r::forest::CutSource> clear);
	uint8_t bitsAt(float worldX, float worldY) const
	{
		return (m_src ? m_src->bitsAt(worldX, worldY) : 0) | (m_roads ? m_roads->bitsAt(worldX, worldY) : 0);
	}
	uint8_t clearAt(float worldX, float worldY) const { return m_clear ? m_clear->bitsAt(worldX, worldY) : 0; }
	uint64_t generation() const { return m_gen; }
	bool active() const { return (bool)m_src; }   // the farm's source (as before ticket 4934)
	bool roadsActive() const { return m_roads || m_clear; }

private:
	std::shared_ptr<const c5r::forest::CutSource> m_src;
	std::shared_ptr<const c5r::forest::CutSource> m_roads, m_clear;
	uint64_t m_gen = 0;
};
CutOverlay& CutOverlaySlot();

class CutTapReader : public c5r::forest::CutSource
{
public:
	enum class State { Absent, Off, NoGrid, Waiting, Active, Failed };
	explicit CutTapReader(uint32_t exePid);
	CutTapReader(uint32_t exePid, const std::wstring& controlName);   // tests: another control section name
	~CutTapReader() override;
	CutTapReader(const CutTapReader&) = delete;
	CutTapReader& operator=(const CutTapReader&) = delete;

	// Every render-loop iteration (or at the scene's poll): picks up a newer published generation, checks and applies it.
	// Returns true when the grid changed (a new one applied, or dropped for the kill switch). Never throws.
	bool poll();

	// c5r::forest::CutSource: the bits of the applied grid at a world point (0 without a grid); the generation moves on
	// every applied or dropped grid (0 before the first). Both include CutOverlaySlot()'s (ticket 4853: its bits ORed in,
	// its generation added).
	uint8_t bitsAt(float worldX, float worldY) const override;
	uint64_t generation() const override;

	State state() const;
	bool installFailure() const;          // the scene's fatal route (D118 / D125); the reason is lastWhy()
	bool off() const;                     // the producer's kill switch (cut_tap=0): an empty grid, LOUD once
	bool noGrid() const;                  // the producer has no grid now (no map, between loads)
	const std::string& lastWhy() const;
	bool hasGrid() const;                 // a grid is applied
	uint32_t imageGeneration() const;     // the producer's generation of the applied grid (0: none)
	uint32_t loadSeq() const;             // the EXE load the applied grid was taken in
	const VPShellCutImage* imageHeader() const;   // the applied image's header (null: none)
	uint32_t producerState() const;       // VPShellCutTapState of the last block read (0xFFFFFFFF: none)
	int32_t lastResult() const;           // the last ackResult written (0 ok)
	uint64_t refusedBy(uint32_t reason) const;
	int64_t accepted = 0, refused = 0;
	uint64_t torn = 0, polls = 0, loudLines = 0;
	double lastApplyMs = 0;               // copy + checks + fill of the last accepted image (indication, COMMON 23)

	// An image's bytes as published (tests, an offline proof): the same checks without the block's.
	static c5r::Status loadImage(const uint8_t* image, size_t n, c5r::forest::DenseCut& out, std::string& why);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace srvrender
