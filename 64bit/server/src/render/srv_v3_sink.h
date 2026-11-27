// srv_v3_sink.h - the version-3 frame sink of the renderer inside server.exe (epic 3300, issue 3301).
//
// Origin (COMMON 13, D22): the keyed-mutex publish of shell/server/shell_server.cpp (CreateTextures, PublishTextureBlock,
// EnsureDepth, PublishDepthBlock, the publish loop in wmain, DumpTarget, DumpDepth) at this repository's commit b4f7748
// (file last changed in ff223fd), ported into a vpframe::FrameSink (shell/frame/frame_sink.h). shell_server.cpp itself is
// unchanged (team 3400 owns it).
//
// What the composite DLL of today sees is exactly what shell_server.exe --scene terrain --depth 1 publishes: the
// version-3 section (shell_shared.h) with the texture block, a BGRA8 shared texture with a keyed mutex (server
// AcquireSync(0) or AcquireSync(1) when the client did not take the previous frame, CopyResource, frame block,
// ReleaseSync(1)), the depth block (R32_FLOAT NDC z, camFrameStamp = the camera block's frameStamp the frame was drawn
// with) and the resize request (reqSeq -> new texture generation -> ackSeq). In addition (vp_frame.h, for the lockstep of
// 3304 and for tests) it keeps the server block of <base>.Frame (state, heartbeat, counters), the ring block's
// latestFrameId and sets <base>.Frm after each frame; it opens <base>.Cam for the host's wait.
#pragma once
#include "srv_render_common.h"
#include "frame_sink.h"
#include "vp_frame.h"
#include <d3d11_1.h>

namespace srvrender {

struct V3Options
{
	std::wstring textureBase = L"Local\\VPShellWorld";  // shared texture names: <textureBase>_<pid>_<generation>
	uint32_t width = 1920, height = 1080;               // until the composite asks for its back-buffer size
	bool depth = true;                                  // publish the depth block and its shared texture
	bool gpuSync = true;                                // wait for the GPU before ReleaseSync(1) (shell_server's default)
};

class V3Sink : public vpframe::FrameSink
{
public:
	explicit V3Sink(const V3Options& o) : m_opt(o) {}
	~V3Sink() override { release(); }
	bool open(ID3D11Device* dev, ID3D11DeviceContext* ctx, const vpframe::SinkConfig& cfg) override;
	void tick(int64_t nowQpc) override;
	vpframe::SinkStatus begin(const vpframe::FrameInfo& f, vpframe::FrameTargets& out) override;
	vpframe::SinkStatus end(const vpframe::FrameInfo& f, const vpframe::DrawReport& r) override;
	void abandon() override {}
	void release() override;
	const char* name() const override { return "v3"; }
	const void* section() const override { return m_frame; }

	// ---- frame_sink.h's optional members ----
	void setState(uint32_t state, uint32_t failReason = 0) override;
	HANDLE camEvent() const override { return m_cam; }
	// True once after the texture was recreated (resize): the host draws again with its last camera.
	bool takeResized() override
	{
		const bool r = m_resized;
		m_resized = false;
		return r;
	}

	// ---- beyond frame_sink.h ----
	const VPShellHeader* header() const { return m_hdr; }
	VPShellHeader* header() { return m_hdr; }
	int64_t published() const { return m_published; }
	int64_t overwritten() const { return m_overwritten; }
	int64_t skipped() const { return m_skipped; }
	double acquireMaxMsAndReset()
	{
		const double r = m_acqMax;
		m_acqMax = 0;
		return r;
	}
	uint32_t generation() const { return m_generation; }
	uint32_t width() const { return m_w; }
	uint32_t height() const { return m_h; }
	// Test switch (render_test_dump): the last published colour as a 32-bit BMP and, with depth, PATH.depth.f32.
	bool dump(const wchar_t* path);

private:
	bool createTextures(uint32_t w, uint32_t h);
	void releaseTextures();
	void publishTextureBlock();
	bool ensureDepth();
	void releaseDepth();
	void publishDepthBlock(int64_t frameCounter, uint32_t state, const vpframe::FrameInfo* f);
	bool deviceLost() const;

	V3Options m_opt;
	ID3D11Device* m_dev = nullptr;
	ID3D11DeviceContext* m_ctx = nullptr;
	ID3D11Query* m_sync = nullptr;
	// version-3 section
	HANDLE m_section = nullptr;
	VPShellHeader* m_hdr = nullptr;
	std::wstring m_base;
	uint64_t m_luid = 0;
	// per generation
	ID3D11Texture2D* m_privTex = nullptr;
	ID3D11RenderTargetView* m_privRtv = nullptr;
	ID3D11Texture2D* m_sharedTex = nullptr;
	IDXGIKeyedMutex* m_km = nullptr;
	HANDLE m_sharedHandle = nullptr;
	uint32_t m_w = 0, m_h = 0, m_generation = 0;
	wchar_t m_texName[160] = {};
	// depth (VPShellDepth)
	ID3D11Texture2D* m_depthTex = nullptr;   // R32_TYPELESS, DSV D32_FLOAT
	ID3D11DepthStencilView* m_dsv = nullptr;
	ID3D11Texture2D* m_depthShared = nullptr;  // R32_FLOAT, shared by NT handle
	HANDLE m_depthHandle = nullptr;
	uint32_t m_depthGen = 0, m_depthW = 0, m_depthH = 0;
	wchar_t m_depthName[160] = {};
	vpframe::FrameInfo m_lastInfo;
	// vp_frame.h section and events
	HANDLE m_frameMap = nullptr;
	void* m_frame = nullptr;
	HANDLE m_cam = nullptr, m_frm = nullptr;
	// counters
	int64_t m_frameCounter = 0, m_published = 0, m_overwritten = 0, m_skipped = 0;
	double m_acqMax = 0;
	uint32_t m_lastClientPid = 0;
	bool m_resized = false;
};

}  // namespace srvrender
