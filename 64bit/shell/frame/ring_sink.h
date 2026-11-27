// ring_sink.h - the ring sink (issue 3302): the producer side of vp_frame.h, x64, inside server.exe's render host
// (3301 integrates it in wave 2) or the stand-in producer. Implements vpframe::FrameSink (frame_sink.h) exactly; the
// extra members below are optional for the host (logging, server state, counters for tests).
#pragma once
#include "frame_sink.h"
#include "ring_common.h"

struct ID3D11Texture2D;
struct ID3D11Device1;
struct ID3D11Device5;
struct ID3D11DeviceContext4;
struct ID3D11Fence;
struct ID3D11Query;

namespace vpframe {

class RingSink : public FrameSink
{
public:
	RingSink();
	~RingSink() override;
	bool open(ID3D11Device* dev, ID3D11DeviceContext* ctx, const SinkConfig& cfg) override;
	void tick(int64_t nowQpc) override;
	SinkStatus begin(const FrameInfo& f, FrameTargets& out) override;
	SinkStatus end(const FrameInfo& f, const DrawReport& r) override;
	void abandon() override;
	void release() override;
	const char* name() const override { return "ring"; }
	const void* section() const override { return m_sec; }

	// ---- frame_sink.h's optional members ----
	// The server block's state (vp_frame.h VPFrameServerState); the host sets REBUILDING / FAILED. open() sets READY,
	// release() STOPPED and clears magic.
	void setState(uint32_t state, uint32_t failReason = 0) override;
	// <base>.Cam (auto-reset): the host's render thread waits on it (vp_frame.h step 2). Owned by the sink.
	HANDLE camEvent() const override { return m_cam; }
	// Slots begin() could take now (read-only; for a host that retries a Busy camera until its deadline without
	// counting every attempt in skippedBusyTotal).
	int freeSlots() const override;

	// ---- beyond frame_sink.h (optional) ----
	void setLog(ring::LogFn fn, void* user) { m_log.fn = fn; m_log.user = user; }
	uint32_t generation() const { return m_gen; }
	uint32_t syncInForce() const { return m_sync; }
	bool fenceAvailable() const { return m_fence != nullptr; }
	uint64_t fenceSignalled() const { return m_fenceValue; }   // the newest value Signal()ed
	uint64_t fenceCompleted() const;                           // the fence's completed value (0 without a fence)
	// Test switch: the CPU mode publishes READY without waiting for the GPU (to show the consumer would see stale
	// pictures; never used outside tests).
	void testSkipCpuWait(bool on) { m_testSkipCpuWait = on; }

private:
	bool regenerate(uint32_t slots, uint32_t w, uint32_t h, uint32_t sync, uint32_t exePid, uint32_t reqSeq);
	void releaseTextures();
	bool newFence();
	void resetSlotRecords();
	bool deviceLost();

	ring::Logger m_log;
	ID3D11Device* m_dev = nullptr;
	ID3D11DeviceContext* m_ctx = nullptr;
	ID3D11Device5* m_dev5 = nullptr;
	ID3D11DeviceContext4* m_ctx4 = nullptr;
	ID3D11Fence* m_fence = nullptr;
	HANDLE m_fenceShared = nullptr;     // our NT handle of the fence (duplicated into the EXE per generation)
	uint64_t m_fenceValue = 0;          // monotonic; a slot's fenceValue (vp_frame.h) is the value signalled after its draw
	ID3D11Query* m_doneQuery = nullptr; // CPU mode: GPU completion of the frame
	HANDLE m_map = nullptr;
	void* m_sec = nullptr;
	HANDLE m_cam = nullptr, m_frm = nullptr;
	wchar_t m_base[200] = {};
	SinkConfig m_cfg;

	struct Slot
	{
		ID3D11Texture2D* colour = nullptr;
		ID3D11Texture2D* depth = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		HANDLE hColour = nullptr, hDepth = nullptr;   // the named NT handles (keep the names alive)
	};
	Slot m_slots[VPFRAME_MAX_SLOTS];
	ID3D11Texture2D* m_privDepth = nullptr;      // R32_TYPELESS, DSV D32_FLOAT; copied into the slot's R32_FLOAT
	ID3D11DepthStencilView* m_dsv = nullptr;
	uint32_t m_slotCount = 0, m_w = 0, m_h = 0, m_sync = 0, m_gen = 0;
	uint32_t m_ackReqSeq = 0;
	bool m_haveRing = false;
	int m_begun = -1;                            // slot between begin() and end()/abandon()
	int64_t m_beginQpc = 0;
	int32_t m_latestSlot = -1;
	int64_t m_regenFailQpc = 0;
	bool m_testSkipCpuWait = false;
};

}  // namespace vpframe
