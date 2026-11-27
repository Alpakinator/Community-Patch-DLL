// srv_overlay_tap.h - server.exe's reader of the Terrain Overlay target tap (epic 0167, issue 3703; decision 1 route a,
// approved by the MC 2026-11-14, MC #696).
//
// Interface committed by the team-0167 lead (E2); filled in by issue 3703 (owner of srv_overlay_tap.h / .cpp); used by
// issue 3704 (the scene binds what acquire() returns as terrain t1 and water t9 with the matching OverlayProj). Producer:
// shell/composite/overlay_tap.cpp. Layout: VPShellOverlayTap / VPShellOverlaySection / VPShellOverlaySlot in
// shell/common/shell_shared.h. Frame rule (decision 2 (i)): the newest published slot, its texels and its OverlayProj
// array as one pair, never mixed with another frame's matrices. The reader names the slot it samples in inUseSlot
// before it binds it and acks the generation (the producer never overwrites that slot).
// Failures (decision 5, D132): the block in VPOVL_FAILED (the tap could not install), or a section whose layout this
// reader refuses (VPOVLF_VERSION) -> state() == Failed with the reason; MC #701 (a): the server then fails its render
// start (or the next call) with an error whose text names the reason (lastWhy()), which the proxy turns into Fatal(3) and
// D125's route; nothing in proxy/ changes. A refused slot during play -> LOUD log (rate-limited), counted in `refused`;
// a stale copy (the newest generation has not moved for VPOVL_READER_STALE_MAP_FRAMES map frames and VPOVL_READER_STALE_MS) is
// a lag, not a fault (MC #891, 2026-11-14): counted in `stale` once per episode, logged in full for the first
// VPOVL_READER_STALE_FULL_LINES episodes only, the longest in `staleLongestPresents`. The last good pair stays bound;
// acquire() still returns true. Presents without map frames (an AI leader screen, the load screen) are no lag: counted
// in `noMapGaps` once per gap, noted for the first VPOVL_READER_STALE_FULL_LINES (issue 0194).
// Cross-device note: the producer copies on the EXE's device and the server samples on its own, with no GPU fence
// between them; the slot protocol keeps the producer off the slot named in inUseSlot, so a slot is only rewritten after
// the server moved on from it (research/05-graphics-composition/overlay-system.md section 7, as built).
#pragma once
#include "srv_render_common.h"
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace srvrender {

// Map frames of the target (Presents that executed the scene-clear list; the block's copiesTotal + missedTotal, since at
// each one after the install the DLL copies or counts a missed copy) without a new generation, while the block is ACTIVE,
// AND at least VPOVL_READER_STALE_MS milliseconds, after which the bound pair counts as stale (one `stale` count per
// episode; a log line for the first VPOVL_READER_STALE_FULL_LINES). The time floor (lead-0167, 2026-11-14, after 3706
// item 6): in AI turns the EXE presents about 250 times a second, so 31 frames is 0.12 s, a normal copy latency.
// Issue 0194 (2026-11-14): counted in map frames, not in Presents of the target. Replaced: VPOVL_READER_STALE_PRESENTS
// (30 Presents), which counted every AI leader screen (frames without the scene-clear list, 0.6-1.0 s) as an episode:
// all 43 episodes of 3706 item 6's quiet ON arm and all 40 of its loaded arm (overlay-system.md 11.1).
static const uint32_t VPOVL_READER_STALE_MAP_FRAMES = 30;
static const uint64_t VPOVL_READER_STALE_MS = 250;
static const uint32_t VPOVL_READER_STALE_FULL_LINES = 3;

struct OverlayFrame
{
	ID3D11ShaderResourceView* srv = nullptr;   // R8G8B8A8_UNORM over all mips of the slot's shared texture (owned by the reader)
	uint32_t numViews = 0;                     // OverlayProj matrices below (the EXE's g_NumViews of that frame)
	float overlayProj[VPSHELL_OVERLAY_MAX_VIEWS][16] = {};   // row-major as the EXE stores them (0xDA1690 + 0x40 v)
	uint64_t presentCount = 0;                 // the camera block's presentCount of the EXE Present the copy was made at
	uint32_t generation = 0;
	uint32_t slot = 0xFFFFFFFFu;
};

class OverlayTapReader
{
public:
	enum class State { Absent, Off, Waiting, Active, Failed };
	// hdr: the mapped main section; dev: the server's render device (same adapter as the EXE's).
	OverlayTapReader(VPShellHeader* hdr, ID3D11Device* dev) : m_hdr(hdr), m_dev(dev) {}
	~OverlayTapReader();
	OverlayTapReader(const OverlayTapReader&) = delete;
	OverlayTapReader& operator=(const OverlayTapReader&) = delete;
	// Every render-loop iteration, before the draws: picks up the newest published slot (opening its shared texture once
	// per slot), checks it (magic, version, sizes, pid, numViews, finite matrices, 2048 x 2048, 12 mips, format family),
	// names it in inUseSlot and acks its generation. Returns true and fills `out` when a pair is bound (new or the
	// previous one); false with state() telling why otherwise. Never throws.
	bool acquire(OverlayFrame& out);
	State state() const { return m_state; }
	uint32_t failReason() const { return m_failReason; }   // VPShellOverlayFail when state() == Failed (VPOVLF_VERSION: the
	                                                        // reader refused the section's layout; lastWhy() says how)
	const std::string& lastWhy() const { return m_lastWhy; }
	int64_t accepted = 0, refused = 0, stale = 0;
	int64_t staleLongestPresents = 0;   // the longest ended stale episode, in Presents of the target
	int64_t noMapGaps = 0;              // gaps of more than 30 Presents and 250 ms without map frames (not stale; 0194)
	// Drops every texture and the section mapping and names no slot in inUseSlot (also done by the destructor). The
	// next acquire() starts again from the block.
	void release();

private:
	VPShellHeader* m_hdr = nullptr;
	ID3D11Device* m_dev = nullptr;
	State m_state = State::Absent;
	uint32_t m_failReason = 0;
	std::string m_lastWhy;
	struct Impl;            // srv_overlay_tap.cpp: the section mapping, the opened slot textures, the bound pair
	Impl* m_impl = nullptr;
};

} // namespace srvrender
