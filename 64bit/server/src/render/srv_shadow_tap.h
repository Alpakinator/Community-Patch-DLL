// srv_shadow_tap.h - server.exe's reader of the model shadow map tap (epic 0169, issue 4304, ticket 0312; C1 accepted by
// the MC 2026-11-15, #1804). Written 2026-11-15 for this repository (GPL-3.0-or-later). Model: srv_overlay_tap.h.
// Producer: shell/composite/shadow_tap.cpp; layout: shell/common/shell_shadow_tap.h (its own section
// Local\VPShellShadowTap.<exe pid>). The scene's binding (terrain t3 / Arg6 / ShadowMatrix0 per wrap copy, water t13 /
// WaterFrame 0x220), the options and the D125 route are issue 4308's; this reader only hands frames over.
//
// Frame rule (0312, the overlay's decision 2 (i)): the newest published slot's texels, ShadowMatrix0 array and strength
// as one triple, never mixed with another frame's. The reader names the slot it samples in inUseSlot before it binds it
// and acks the generation (the producer never overwrites that slot). A torn slot (seq odd, valid 0, or a generation that
// is not the block's) is never taken: the last triple stays.
// States: Absent (no section yet: no producer, or an EXE without the tap), Off (shadow_tap=0), Waiting (the section is
// there but nothing is published yet), Active (a triple is bound), Failed (final: the producer could not install, or the
// section's layout is refused). Failed carries a reason for D125's route (installFailure(), lastWhy(): "the shadow tap
// could not install: ..." naming the producer's own text); 4308 turns it into the render start's error.
// In play: a refused slot -> LOUD (rate-limited), counted in `refused`, the last triple stays; a stale episode (the newest
// generation has not moved for SHADOW_READER_STALE_MAP_FRAMES map frames and SHADOW_READER_STALE_MS) is a lag, counted in
// `stale` once per episode, never a fault (D132, MC #891), as the overlay's.
// Verify (the producer's shadow_verify=N, diagnostic): a slot carrying VPSHD_SLOT_VERIFY holds the FNV-1a 64 of the EXE's
// texture read at the copy's Present; the reader reads the shared texture back on its own device, hashes it the same way
// and counts verifyOk / verifyBad (texels only: under D33 no EXE draw binds the matrices, terrain-surface.md 11 S5).
#pragma once
#include "srv_render_common.h"
#include "shell_shadow_tap.h"
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace srvrender {

static const uint32_t SHADOW_READER_STALE_MAP_FRAMES = 30;
static const uint64_t SHADOW_READER_STALE_MS = 250;
static const uint32_t SHADOW_READER_STALE_FULL_LINES = 3;

struct ShadowFrame
{
	ID3D11ShaderResourceView* srv = nullptr;   // the slot's shared texture (owned by the reader); null at quality 0
	uint32_t format = 0;                       // DXGI_FORMAT: 56 R16_UNORM (q 3, 2), 10 R16G16B16A16_FLOAT (q 1), 0 (q 0)
	uint32_t width = 0, height = 0;
	uint32_t numViews = 0;                     // ShadowMatrix0 matrices below (the EXE's g_NumViews of that frame)
	float matrices[VPSHELL_SHADOW_MAX_VIEWS][16] = {};   // row-major as the EXE stores them (0xDA1790 + 0x40 v)
	float strength = 0;                        // [0xD90FA8] of that frame (TerrainFrame.Arg6, WaterFrame 0x220); 0 at q 0
	uint64_t presentCount = 0;                 // the camera block's presentCount of the EXE Present the copy was made at
	uint32_t generation = 0;
	uint32_t quality = 0;                      // [0xD90F9C] of that frame
	uint32_t slot = VPSHELL_SHADOW_NO_SLOT;
	// ticket 4785: the slot's VPSHD_SLOT_UNIT_MASK_DROPPED (the EXE's unit mask draws were dropped from this frame: the server
	// adds its own units' mask, srv_unit_shadow.h) and ShadowRaw's size of that Present (0 from an older producer)
	bool unitMaskDropped = false;
	uint32_t rawWidth = 0, rawHeight = 0;
};

class ShadowTapReader
{
public:
	enum class State { Absent, Off, Waiting, Active, Failed };
	// exePid: the EXE whose section Local\VPShellShadowTap.<pid> is read; hdr: the mapped main section (the camera block's
	// presentCount for the stale rule; may be null); dev: the server's render device (same adapter as the EXE's).
	ShadowTapReader(uint32_t exePid, VPShellHeader* hdr, ID3D11Device* dev);
	// tests: another section name
	ShadowTapReader(const std::wstring& sectionName, uint32_t exePid, VPShellHeader* hdr, ID3D11Device* dev);
	~ShadowTapReader();
	ShadowTapReader(const ShadowTapReader&) = delete;
	ShadowTapReader& operator=(const ShadowTapReader&) = delete;
	// Every render-loop iteration, before the draws: picks up the newest published slot (opening its shared texture once
	// per slot and texEpoch), checks it (layout, pid, numViews, finite matrices, strength in [0, 1], the description the
	// quality makes, the texture's own description), names it in inUseSlot and acks its generation. Returns true and fills
	// `out` when a triple is bound (new or the previous one); false with state() telling why otherwise. Never throws.
	bool acquire(ShadowFrame& out);
	State state() const { return m_state; }
	uint32_t failReason() const { return m_failReason; }   // VPShellShadowFail when state() == Failed
	bool installFailure() const { return m_state == State::Failed; }   // D125: the reason is lastWhy()
	const std::string& lastWhy() const { return m_lastWhy; }
	int64_t accepted = 0, refused = 0, stale = 0;
	int64_t staleLongestPresents = 0;
	int64_t noMapGaps = 0;
	int64_t reopened = 0;               // shared textures opened (a new slot set after a description change re-opens)
	int64_t verifyOk = 0, verifyBad = 0;
	// Drops every texture and the section mapping and names no slot in inUseSlot (also done by the destructor).
	void release();

private:
	std::wstring m_name;
	uint32_t m_pid = 0;
	VPShellHeader* m_hdr = nullptr;
	ID3D11Device* m_dev = nullptr;
	State m_state = State::Absent;
	uint32_t m_failReason = 0;
	std::string m_lastWhy;
	struct Impl;
	Impl* m_impl = nullptr;
};

} // namespace srvrender
