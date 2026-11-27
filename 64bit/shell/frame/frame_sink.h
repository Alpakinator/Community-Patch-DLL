// frame_sink.h - the producer side of the frame hand-over, as the renderer inside server.exe sees it (epic 3300).
//
// Written by the team-3300 lead before wave 1 (2026-11-13). Changes go through the lead. Two implementations:
//   * the version-3 sink (keyed mutex + copy into the shared texture of shell_shared.h, the depth block): issue 3301,
//     server/src/render/, so that the renderer joins server.exe with the composite DLL as it is today;
//   * the ring sink (vp_frame.h: ring of shared textures, shared fence or CPU sync, no keyed mutex): issue 3302,
//     shell/frame/ring_sink.cpp.
// The render host (3301) owns the device, the context and the loop; it draws through whichever sink server.cfg names.
// Everything here runs on the render thread only. Compiles as C++14 (the server's own code) and C++17 (render/).
#pragma once
#include <stdint.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

namespace vpframe {

struct SinkConfig
{
	const wchar_t* baseName = nullptr;  // the version-3 section name (server.cfg render_section=); vp_frame.h derives the rest
	uint32_t slots = 4;                 // ring sink (4 since 3302, vp_frame.h): VPFRAME_MIN_SLOTS..VPFRAME_MAX_SLOTS (the composite's request wins)
	uint32_t sync = 1;                  // ring sink: VPFrameSync wanted (VPSYNC_FENCE; VPSYNC_CPU when the fence fails)
	uint64_t adapterLuid = 0;           // the device's adapter (published; the composite checks it against the EXE's)
};

// The camera being drawn: copied by the host from the camera block (shell_shared.h) and the publish block (vp_frame.h).
struct FrameInfo
{
	int64_t frameId = 0;        // VPShellCamera::frameStamp (= VPFramePublish::frameId under lockstep)
	int64_t publishQpc = 0;     // VPFramePublish::publishQpc of that camera (0 when the composite publishes at Present)
	int64_t wakeQpc = 0;        // when the render thread woke for it
	float proj22 = 0, proj32 = 0, nearZ = 0, farZ = 0;   // the camera's projection terms (depth conversion)
};

// Where the host draws one frame. Owned by the sink; valid from begin() until end() or abandon().
struct FrameTargets
{
	ID3D11RenderTargetView* colour = nullptr;  // B8G8R8A8_UNORM, width x height; the host clears it to transparent black
	ID3D11DepthStencilView* depth = nullptr;   // D32_FLOAT, same size; the host clears it to 1.0 and draws with it; the
	                                           // sink turns it into the published R32_FLOAT NDC z (1.0 = nothing drawn)
	uint32_t width = 0, height = 0;            // the consumer's requested size (its back buffer)
};

struct DrawReport
{
	uint32_t result = 0;       // VPFrameDrawResult
	float cpuMs = 0;           // CPU time of the draw
	float gpuMs = -1;          // GPU time of the newest timed draw, -1 when not available
	uint32_t tilesDrawn = 0;
	int32_t tilesUnbaked = -1; // land tiles drawn without their bake (FrameStats::landTilesWithoutCache); -1 = not counted
};

enum class SinkStatus
{
	Ok,          // targets valid (begin) / frame published (end)
	Busy,        // no free slot, or the consumer has not taken the last frame and overwriting is not allowed: skip this camera
	NotReady,    // no consumer request yet (size unknown) or the shared objects are being recreated: skip, retry later
	DeviceLost,  // DXGI_ERROR_DEVICE_REMOVED / RESET seen: the host rebuilds the device, then calls release() and open()
	Error,       // anything else (logged by the sink); the host counts it and goes on
};

class FrameSink
{
public:
	virtual ~FrameSink() {}
	// Creates or opens the sink's sections and events; non-blocking. False = log and retry at the next tick.
	virtual bool open(ID3D11Device* dev, ID3D11DeviceContext* ctx, const SinkConfig& cfg) = 0;
	// Every loop iteration: heartbeat, the consumer's resize request (recreates the textures under a new generation),
	// slot releases. Never blocks.
	virtual void tick(int64_t nowQpc) = 0;
	// Targets for one frame (the ring sink picks a slot by vp_frame.h's rule). The host then draws and calls end().
	virtual SinkStatus begin(const FrameInfo& f, FrameTargets& out) = 0;
	// Publishes the frame begun: depth copy, then (ring) fence signal + flush or GPU wait, slot record READY, latest
	// frame, <base>.Frm; (version 3) AcquireSync + copy + frame block + ReleaseSync. Returns Ok when published.
	virtual SinkStatus end(const FrameInfo& f, const DrawReport& r) = 0;
	// A begun frame that is not drawn (the slot is given back unchanged).
	virtual void abandon() = 0;
	// Releases every D3D object and handle (device lost, shutdown). open() may be called again afterwards.
	virtual void release() = 0;
	// A short name for logs ("v3", "ring").
	virtual const char* name() const = 0;
	// The section mapping this sink publishes into, for the host's per-second line (may be nullptr).
	virtual const void* section() const = 0;

	// ---- optional, with defaults (issue 3306, 2026-11-13: the host calls these on any sink, no down-cast) ----
	// The server block's state (vp_frame.h VPFrameServerState) and fail reason, written into the sink's section.
	virtual void setState(uint32_t state, uint32_t failReason) { (void)state; (void)failReason; }
	// <base>.Cam (auto-reset), owned by the sink, as a HANDLE; nullptr = the sink has none (the host then polls).
	virtual void* camEvent() const { return nullptr; }
	// Slots begin() could take now (read-only; a host retries a Busy camera until its deadline while this is 0 and
	// counts the retry once). A sink without a slot rule answers 1: begin() decides.
	virtual int freeSlots() const { return 1; }
	// True once after the sink recreated its targets (a resize, a new generation): the host draws its last camera again.
	virtual bool takeResized() { return false; }
};

}  // namespace vpframe
