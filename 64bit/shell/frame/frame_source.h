// frame_source.h - the consumer side of the frame hand-over, as the composite DLL (x86, inside the EXE) sees it
// (epic 3300).
//
// Written by the team-3300 lead before wave 1 (2026-11-13). Changes go through the lead. Two implementations:
//   * the version-3 source (keyed mutex + copy, today's AcquireAndCopy): issue 3304, inside shell/composite/;
//   * the ring source (vp_frame.h: slots opened by name, srvFence wait queued on the EXE's context, slot release by
//     event query): issue 3302, shell/frame/ring_source.cpp.
// The lockstep logic (when to wait, the bound, misses, give-up, per-frame records) and the draw (colour + depth into
// the EXE's targets) belong to the composite (3304) and use only this interface, so the hand-over can be switched by
// the ini key `handover=` without touching them. Every call is made on the EXE's main thread (the presenting thread)
// except poll(), which may also run on the composite's control thread (implementations lock what they share).
// Never blocks except waitFor(), and waitFor() never past its deadline.
#pragma once
#include <stdint.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace vpframe {

struct SourceFrame
{
	int64_t frameId = 0;                         // 0 = nothing taken
	int slot = -1;                               // ring slot (-1 for the version-3 source)
	ID3D11ShaderResourceView* colour = nullptr;  // straight-alpha BGRA (UNORM view; the composite picks sRGB itself)
	ID3D11ShaderResourceView* depth = nullptr;   // R32_FLOAT NDC z of the same frame, 1.0 = nothing drawn; nullptr = none
	float proj22 = 0, proj32 = 0, nearZ = 0, farZ = 0;  // the camera the frame was drawn with
	int64_t camPublishQpc = 0, wakeQpc = 0, submitQpc = 0, readyQpc = 0;  // the server's times (vp_frame.h VPFrameSlot)
	float serverCpuMs = 0, serverGpuMs = -1;
	int32_t tilesUnbaked = -1;  // the slot's land tiles drawn without their bake; -1 = not counted (or the version-3 source)
};

enum class Take
{
	Exact,  // the frame asked for
	Older,  // an older frame (the previous picture: a miss under lockstep)
	None,   // nothing drawable (no server, no frame yet, generation changing)
};

class FrameSource
{
public:
	virtual ~FrameSource() {}
	// Opens or re-opens the shared objects named from baseName (non-blocking; false = retry at a later frame).
	virtual bool open(ID3D11Device* dev, ID3D11DeviceContext* ctx, const wchar_t* baseName) = 0;
	// The composite's target size; a change asks the server for new textures (a new generation).
	virtual void request(uint32_t width, uint32_t height) = 0;
	// Newest frame id the server made ready (0 = none).
	virtual int64_t newest() = 0;
	// Waits until frameId is ready or deadlineQpc passes, whichever is first; true when ready. A deadline in the past
	// returns at once. The wait must end within about 0.5 ms of the deadline (high-resolution timer or event + spin).
	// Optional for the composite (2026-11-13, 3304): it waits with its own loop on newest() + poll(), because the ring
	// source's waitFor ended up to 0.68 ms (1.1 ms under ASan) past the deadline on the stand-in.
	virtual bool waitFor(int64_t frameId, int64_t deadlineQpc) = 0;
	// Makes frameId (or, if it is not ready, the newest ready frame older than it; frameId <= 0 = the newest ready
	// frame) drawable on ctx and claims it:
	// ring + FENCE queues ID3D11DeviceContext4::Wait(srvFence, value); version 3 acquires the keyed mutex and copies.
	virtual Take acquire(ID3D11DeviceContext* ctx, int64_t frameId, SourceFrame& out) = 0;
	// After the composite's draw that read `f` on ctx: give the frame back (ring: event query, the slot is released when
	// it completes; version 3: nothing, the copy is local).
	virtual void release(ID3D11DeviceContext* ctx, const SourceFrame& f) = 0;
	// Completes earlier releases (event queries), notices a new generation or a server restart. Never blocks.
	virtual void poll(int64_t nowQpc) = 0;
	// Releases every D3D object and handle (ResizeBuffers on the target, mode off, unload).
	virtual void close() = 0;
	virtual const char* name() const = 0;
};

}  // namespace vpframe
