// srv_fow_mask_pub.h - server.exe hands its fog-of-war mask to the EXE's model draws (ticket 0339, follow-up of epic
// 0168, decision 3 (ii); MC answer #2058). Written 2026-11-15 for this repository (GPL-3.0-or-later). Layout and protocol:
// shell/common/shell_fowmask.h; consumer: shell/composite/fow_models.cpp; behaviour: fog-of-war.md section 9.
//
// The fog scene (srv_fow_scene.cpp) calls frame() once per drawn frame after its mask build. The publisher creates the
// section `Local\VPShellFowMask.<EXE pid>` once it knows the EXE's pid, makes its slot textures only while the consumer
// says `wanted`, copies each new mask into a free slot (never the newest published one, never the consumer's inUseSlot)
// and publishes it once the copy's event query has signalled. Never throws; every failure is logged (rate-limited) and
// counted; it never ends the game itself. The consumer's FAILED state (a checked EXE write did not match, the model-fog
// install could not finish) is handed to the fog scene through consumerFailed(), which takes the D125 route.
// Needs only D3D11 and the section layout, so a test producer can link it alone (server/tests/render/fow/).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include <string>
#include "shell_fowmask.h"

namespace srvrender {

struct FowMaskPubInput
{
	uint32_t numViews = 0;                                 // the pair the mask was built with
	float overlayProj[VPSHELL_FOWMASK_MAX_VIEWS][16] = {};
	uint32_t pairGeneration = 0;
	uint64_t pairPresentCount = 0;
	uint32_t fogGeneration = 0;
	float fowAnim[4] = {0, 0, 0, 0};
};

class FowMaskPublisher
{
public:
	typedef void (*LogFn)(const char* fmt, ...);
	explicit FowMaskPublisher(LogFn log = nullptr) : m_log(log) {}
	~FowMaskPublisher() { release("the publisher ends"); }
	FowMaskPublisher(const FowMaskPublisher&) = delete;
	FowMaskPublisher& operator=(const FowMaskPublisher&) = delete;

	// Tests: another section name (the texture names stay VPSHELL_FOWMASK_TEXTURE_FMT with the given pid).
	void setSectionName(const std::wstring& name) { m_nameOverride = name; }
	// Once per drawn frame. exePid 0: nothing (no section yet). mask: the newest mask texture (null: none this frame);
	// newMask: it was rebuilt this frame (a copy is made only then, or when the consumer has nothing yet).
	void frame(ID3D11DeviceContext* ctx, uint32_t exePid, ID3D11Texture2D* mask, bool newMask, const FowMaskPubInput& in);
	// The consumer reported FAILED: why = "<reason name>: <its text>". Cheap (one read of the mapped section).
	bool consumerFailed(std::string& why) const;
	uint32_t consumerState() const;            // VPShellFowConsumerState (VPFMC_NONE without a section)
	bool wanted() const;
	// The device goes (resize, lost) or the fog stops: producerState STOPPED, the slot textures released; the section stays.
	void stop(const char* why);
	// Everything, the section too.
	void release(const char* why);

	int64_t published = 0, skipped = 0, faults = 0, copies = 0;
	static const char* ConsumerStateName(uint32_t s);
	static const char* FailName(uint32_t r);

private:
	struct Slot
	{
		ID3D11Texture2D* tex = nullptr;
		HANDLE nt = nullptr;
		ID3D11Query* q = nullptr;
		bool inflight = false;
		FowMaskPubInput meta;
		int64_t qpc = 0;
	};
	bool openSection(uint32_t exePid);
	bool makeSlots(ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& src);
	void dropSlots();
	void pollInflight(ID3D11DeviceContext* ctx);
	void note(const char* fmt, ...);
	LogFn m_log;
	std::wstring m_nameOverride;
	HANDLE m_secH = nullptr;
	VPShellFowMaskSection* m_sec = nullptr;
	uint32_t m_exePid = 0;
	uint32_t m_epoch = 0;
	Slot m_slot[VPSHELL_FOWMASK_SLOTS];
	D3D11_TEXTURE2D_DESC m_desc = {};
	bool m_haveDesc = false;
	int m_inflight = -1;
	int m_polls = 0;                 // polls of the copy in flight so far
	uint32_t m_gen = 0;
	uint32_t m_newest = VPSHELL_FOWMASK_NONE;
	bool m_pending = false;          // a new mask that could not be copied yet (no free slot): copied at the next chance
	bool m_slotsFailed = false;      // makeSlots failed for this description: not retried until the description changes
	int m_logs = 0;
	uint32_t m_lastConsumer = 0xFFFFFFFFu;
};

}  // namespace srvrender
