// test_scene_overlay.cpp - stand-in tests of the overlay in server.exe's terrain scene (server/src/render/srv_render_scene.cpp,
// epic 0167, issue 3704). Written 2026-11-14 for this repository (GPL-3.0-or-later).
//
// Each case builds its own main section (a fake camera block and a height-field tap section published through the
// VPShellTap block as shell_composite.dll does, shell_shared.h), starts a Scene on a WARP device with the EXE's terrain
// programs (C5R_EXE, read-only), and replaces the scene's overlay source by a fake that hands the scene what each case
// needs (pairs, states, failures). The checks read what the scene bound for the draw (Scene::lastOverlay, FrameStats),
// its counters and its log. The real reader against a fake overlay section is 3703's (its own tests); this program also
// runs the scene once with the reader as it is in the tree (the stub until 3703 lands) to show today's picture.
// Output: one line per case, `CASE <name> PASS|FAIL <detail>`; exit code = the number of failed cases, 2 on usage, 99
// without a WARP device or C5R_EXE.
//
//   test_scene_overlay.exe [case ...]      (no argument: every case)
#include "srv_render_scene.h"
#include "srv_render_host.h"
#include "c5r/heightfield/heightfield.h"
#include "srv_options.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvrender;
namespace c5h = c5r::heightfield;

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::wstring g_exe;
std::vector<std::string> g_log;
void HostLog(const char* line) { g_log.push_back(line); }
int CountLog(const char* needle)
{
	int n = 0;
	for(const auto& l : g_log)
		if(l.find(needle) != std::string::npos)
			n++;
	return n;
}

const uint32_t kW = 160, kH = 120;     // camera and target size
const float kWrap = 255.0f;            // 64 W - 1 for the 4-plot field below (the EXE's 0xD90FE4)

// ---- the fake overlay source ---------------------------------------------------------------------------------------
struct FakeSource : OverlaySource
{
	OverlayTapReader::State st = OverlayTapReader::State::Waiting;
	uint32_t reason = 0;
	std::string text;
	bool give = false;      // acquire() returns the pair below
	OverlayFrame pair;
	int acquires = 0;
	bool acquire(OverlayFrame& out) override
	{
		acquires++;
		out = OverlayFrame();
		if(!give)
			return false;
		out = pair;
		return true;
	}
	OverlayTapReader::State state() const override { return st; }
	uint32_t failReason() const override { return reason; }
	std::string why() const override { return text; }
};

// ---- the main section, camera and field ----------------------------------------------------------------------------
struct Main
{
	HANDLE h = nullptr;
	VPShellHeader* hdr = nullptr;
	std::wstring name;
	HANDLE tapSec = nullptr;
	~Main()
	{
		if(hdr)
			UnmapViewOfFile(hdr);
		if(h)
			CloseHandle(h);
		if(tapSec)
			CloseHandle(tapSec);
	}
};

int g_serial = 0;

c5h::HeightGrid Field()
{
	c5h::HeightGrid g;
	g.mapW = 4;
	g.mapH = 4;
	g.samplesX = 256;
	g.samplesY = 256;
	g.wrapX = true;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = 128.0f;
	g.oy = 128.0f * 0.866f;
	g.waterZ = 10.0f;
	g.h.assign((size_t)g.samplesX * g.samplesY, (uint16_t)(20 << 8));
	return g;
}

// The camera block: a camera at (cx, 0, 120) looking straight down, numViews 1, the field's grid constants.
void WriteCamera(Main& m, float cx, int64_t frame)
{
	VPShellCamera* c = VPSHELL_CAMERA(m.hdr);
	const uint32_t s = c->seq;
	c->seq = s | 1;
	MemoryBarrier();
	c->status = VPCAM_OK;
	c->presentCount = frame;
	c->frameStamp = frame;
	c->numViews = 1;
	const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, 0, -120, 1};
	const float w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, cx, 0, 120, 1};
	memcpy(c->view[0], v, sizeof(v));
	memcpy(c->camWorld, w, sizeof(w));
	const float n = 1.0f, f = 1000.0f;
	const float ys = 1.0f / tanf(15.0f * 3.14159265f / 180.0f), xs = ys * (float)kH / (float)kW;
	const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
	memcpy(c->proj, p, sizeof(p));
	c->bbW = kW;
	c->bbH = kH;
	c->gridSeq = 1;
	c->gridValid = 1;
	c->wrapX = 1;
	c->wrapPeriod = kWrap;
	c->sx = 1.0f;
	c->sy = 0.866f;
	c->heightScale = 0.2f;
	c->ox = 128.0f;
	c->oy = 128.0f * 0.866f;
	c->samplesX = 256;
	c->samplesY = 256;
	MemoryBarrier();
	c->seq = (s | 1) + 1;
}

bool MakeMain(Main& m, std::string& why)
{
	wchar_t nm[96];
	swprintf_s(nm, L"Local\\VPOvlSceneTest.%u.%d", GetCurrentProcessId(), ++g_serial);
	m.name = nm;
	m.h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, nm);
	m.hdr = m.h ? (VPShellHeader*)MapViewOfFile(m.h, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES) : nullptr;
	if(!m.hdr)
	{
		why = "main section";
		return false;
	}
	WriteCamera(m, 0.0f, 10);
	// the field through the tap block, READY
	std::vector<uint8_t> img;
	const c5r::Status st = c5h::writeTap(Field(), img);
	if(!st.ok())
	{
		why = "writeTap: " + st.str();
		return false;
	}
	wchar_t tn[64];
	swprintf_s(tn, L"Local\\VPOvlSceneTestTap.%u.%d", GetCurrentProcessId(), g_serial);
	m.tapSec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)img.size(), tn);
	uint8_t* tv = m.tapSec ? (uint8_t*)MapViewOfFile(m.tapSec, FILE_MAP_ALL_ACCESS, 0, 0, img.size()) : nullptr;
	if(!tv)
	{
		why = "tap section";
		return false;
	}
	memcpy(tv, img.data(), img.size());
	UnmapViewOfFile(tv);
	VPShellTap* t = VPSHELL_TAP(m.hdr);
	t->seq = 1;
	t->state = VPTAP_READY;
	t->loadSeq = 1;
	t->mapW = 4;
	t->mapH = 4;
	t->samplesX = 256;
	t->samplesY = 256;
	t->flags = 1;
	t->payloadBytes = img.size();
	t->copySeq = 1;
	wcscpy_s(t->sectionName, tn);
	MemoryBarrier();
	t->seq = 2;
	return true;
}

// A scene on the WARP device with the field applied; the overlay source replaced by `fake` when not null.
struct Rig
{
	Main m;
	Scene scene;
	FakeSource* fake = nullptr;
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	~Rig()
	{
		scene.releaseDevice();
		scene.detach();
		SafeRelease(rtv);
		SafeRelease(rt);
	}
	bool start(int overlayOpt, bool useFake, std::string& why)
	{
		if(!MakeMain(m, why))
			return false;
		if(!scene.attach(m.name))
		{
			why = "attach";
			return false;
		}
		SceneOptions o;
		o.exe = g_exe;
		o.tiles = SceneOptions::kTilesNone;
		o.water = false;
		o.color = c5r::terrain::ColorSource::Constant;
		o.fallback = c5r::terrain::ColorSource::Constant;
		o.overlay = overlayOpt;
		const c5r::Status st = scene.init(g_dev, g_ctx, o);
		if(!st.ok())
		{
			why = "init: " + st.str();
			return false;
		}
		if(useFake)
		{
			fake = new FakeSource();
			scene.setOverlaySource(std::unique_ptr<OverlaySource>(fake));
		}
		scene.poll();   // the field
		if(!scene.haveField())
		{
			why = "no field after the first poll";
			return false;
		}
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = kW;
		d.Height = kH;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &rt)) || FAILED(g_dev->CreateRenderTargetView(rt, nullptr, &rtv)))
		{
			why = "target";
			return false;
		}
		return true;
	}
	// poll, decide, draw; returns decide()'s answer (false: nothing drawn), sets ok to the draw's result
	bool frame(c5r::terrain::FrameStats& fs, bool& ok)
	{
		scene.poll();
		SceneFrame f;
		if(!scene.decide(f))
		{
			ok = true;
			return false;
		}
		ok = scene.draw(g_ctx, rtv, nullptr, kW, kH, f, fs).ok();
		return true;
	}
	std::vector<uint8_t> pixels()
	{
		std::vector<uint8_t> out;
		D3D11_TEXTURE2D_DESC d;
		rt->GetDesc(&d);
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* s = nullptr;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
			return out;
		g_ctx->CopyResource(s, rt);
		D3D11_MAPPED_SUBRESOURCE map;
		if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &map)))
		{
			out.resize((size_t)kW * kH * 4);
			for(uint32_t y = 0; y < kH; ++y)
				memcpy(&out[(size_t)y * kW * 4], (const uint8_t*)map.pData + (size_t)y * map.RowPitch, kW * 4);
			g_ctx->Unmap(s, 0);
		}
		s->Release();
		return out;
	}
};

ID3D11ShaderResourceView* OverlayTexture(uint32_t rgba)
{
	std::vector<uint32_t> px(16, rgba);
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = 4;
	d.MipLevels = d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA s = {px.data(), 16, 0};
	ID3D11Texture2D* t = nullptr;
	ID3D11ShaderResourceView* v = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, &s, &t)))
		return nullptr;
	g_dev->CreateShaderResourceView(t, nullptr, &v);
	t->Release();
	return v;
}

// An OverlayProj as the EXE builds it (overlay-system.md 4): diag(2 / Wx, 2 / Wy, -0.5, 1), last row (tx, ty, 0.5, 1).
void ExeProj(float m[16], float wx, float wy, float x0, float y0)
{
	const float sx = 2.0f / wx, sy = 2.0f / wy;
	const float v[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -0.5f, 0, -1.0f - x0 * sx, -1.0f - y0 * sy, 0.5f, 1};
	memcpy(m, v, sizeof(v));
}
void Moved(const float* p, float shift, float* out)
{
	memcpy(out, p, 64);
	for(int j = 0; j < 4; ++j)
		out[12 + j] = (float)((double)shift * p[j] + p[12 + j]);
}
bool Same(const float* a, const float* b) { return memcmp(a, b, 64) == 0; }
bool Near(const float* a, const float* b)
{
	for(int i = 0; i < 16; ++i)
		if(fabsf(a[i] - b[i]) > 1e-5f * std::max(1.0f, fabsf(b[i])))
			return false;
	return true;
}

// ---- cases ---------------------------------------------------------------------------------------------------------

// OverlayCopiesFromFrame, every rule and every refusal (no device needed).
bool CaseCopies(std::string& why)
{
	OverlayFrame f;
	ExeProj(f.overlayProj[0], 832, 640, 1024, -576);   // 3701 stock A's view 0 window
	f.numViews = 1;
	OverlayCopies oc;
	if(!OverlayCopiesFromFrame(f, 3583.0f, oc, why))
		return false;
	float m[16];
	Moved(f.overlayProj[0], -3583.0f, m);
	if(!Same(oc.proj[1], f.overlayProj[0]) || oc.fromExe[0] || oc.fromExe[2] || !Near(oc.proj[0], m))
	{
		why = "one view: copy 0 must be view 0's bytes, copies -1 / +1 derived by -+3583";
		return false;
	}
	// the EXE's wrap view at +3583 (3701 stock A: tx -3.46154 -> 5.15144) is copy +1, its bytes as written
	Moved(f.overlayProj[0], 3583.0f, f.overlayProj[1]);
	f.numViews = 2;
	if(!OverlayCopiesFromFrame(f, 3583.0f, oc, why) || !oc.fromExe[2] || oc.fromExe[0] || !Same(oc.proj[2], f.overlayProj[1]) ||
	   oc.unmatchedViews)
	{
		why = "view 1 at +3583 must be copy +1 (" + why + ")";
		return false;
	}
	if(fabsf(f.overlayProj[1][12] - 5.15144f) > 1e-3f || fabsf(f.overlayProj[0][12] + 3.46154f) > 1e-3f)
	{
		why = "the test's matrices are not 3701 stock A's values";
		return false;
	}
	// the EXE's own bytes: 3701's live read of 0xDA1690 and 0xDA1690 + 0x40 (stock A, c5h-a, 2026-11-14 13:14,
	// C:/c5-64bit-work/0167/3701/probe_stock.json): the wrap view differs from view 0 in z / w by up to 1.5e-5 too
	{
		OverlayFrame live;
		const float v0[16] = {0.0024038462433964014f, 0, 0, 0, 0, 0.0031250000465661287f, 0, 0, 0, 0, -0.5f, 0,
		                      -3.461538553237915f, 0.800000011920929f, 0.5f, 1.0f};
		const float v1[16] = {0.0024038460105657578f, 0, 0, 0, 0, 0.003124999813735485f, 0, 0, 0, 0, -0.4999999701976776f, 0,
		                      5.151442527770996f, 0.7999997735023499f, 0.4999847114086151f, 0.9999999403953552f};
		memcpy(live.overlayProj[0], v0, 64);
		memcpy(live.overlayProj[1], v1, 64);
		live.numViews = 2;
		if(!OverlayCopiesFromFrame(live, 3583.0f, oc, why) || !oc.fromExe[2] || !Same(oc.proj[2], v1) || oc.unmatchedViews)
		{
			why = "the EXE's live wrap view (stock A) is not copy +1 (" + why + ")";
			return false;
		}
	}
	Moved(f.overlayProj[0], -3583.0f, f.overlayProj[1]);
	if(!OverlayCopiesFromFrame(f, 3583.0f, oc, why) || !oc.fromExe[0] || !Same(oc.proj[0], f.overlayProj[1]))
	{
		why = "view 1 at -3583 must be copy -1";
		return false;
	}
	// 3584 (the server's samplesX * sx) is not the EXE's shift: unmatched, and copy +1 derived from view 0
	Moved(f.overlayProj[0], 3584.0f, f.overlayProj[1]);
	if(!OverlayCopiesFromFrame(f, 3583.0f, oc, why) || oc.unmatchedViews != 1 || oc.fromExe[2])
	{
		why = "a view at +3584 must be unmatched";
		return false;
	}
	// another scale: unmatched
	ExeProj(f.overlayProj[1], 896, 704, 1024 - 3583, -576);
	if(!OverlayCopiesFromFrame(f, 3583.0f, oc, why) || oc.unmatchedViews != 1)
	{
		why = "a view of another scale must be unmatched";
		return false;
	}
	// two views claim copy +1: the second is unmatched
	Moved(f.overlayProj[0], 3583.0f, f.overlayProj[1]);
	Moved(f.overlayProj[0], 3583.2f, f.overlayProj[2]);
	f.numViews = 3;
	if(!OverlayCopiesFromFrame(f, 3583.0f, oc, why) || oc.unmatchedViews != 1 || !Same(oc.proj[2], f.overlayProj[1]))
	{
		why = "a second view for copy +1 must be unmatched";
		return false;
	}
	// no wrap: every copy is view 0's
	f.numViews = 2;
	if(!OverlayCopiesFromFrame(f, 0.0f, oc, why) || !Same(oc.proj[0], f.overlayProj[0]) || !Same(oc.proj[2], f.overlayProj[0]) ||
	   oc.unmatchedViews != 1)
	{
		why = "wrap period 0: every copy view 0's";
		return false;
	}
	// refusals
	OverlayFrame b = f;
	b.numViews = 0;
	std::string w;
	if(OverlayCopiesFromFrame(b, 3583.0f, oc, w) || w.find("numViews") == std::string::npos)
	{
		why = "numViews 0 not refused";
		return false;
	}
	b.numViews = VPSHELL_OVERLAY_MAX_VIEWS + 1;
	if(OverlayCopiesFromFrame(b, 3583.0f, oc, w))
	{
		why = "numViews 5 not refused";
		return false;
	}
	b = f;
	b.overlayProj[1][5] = NAN;
	if(OverlayCopiesFromFrame(b, 3583.0f, oc, w) || w.find("view 1") == std::string::npos)
	{
		why = "a NaN matrix not refused";
		return false;
	}
	b = f;
	b.overlayProj[0][0] = 0;
	if(OverlayCopiesFromFrame(b, 3583.0f, oc, w))
	{
		why = "a zero scale not refused";
		return false;
	}
	b = f;
	b.overlayProj[3][0] = NAN;   // past numViews: not read
	if(!OverlayCopiesFromFrame(b, 3583.0f, oc, w))
	{
		why = "a matrix past numViews was read: " + w;
		return false;
	}
	return true;
}

// The reader as it is in the tree (the E2 stub until 3703 lands: never a pair) and render_overlay=0 draw the same
// picture, bit for bit, with nothing bound and no LOUD line: today's picture.
bool CaseTodaysPicture(std::string& why)
{
	std::vector<uint8_t> px[2];
	for(int k = 0; k < 2; ++k)
	{
		Rig r;
		if(!r.start(k == 0 ? 1 : 0, false, why))
			return false;
		c5r::terrain::FrameStats fs;
		bool ok = false;
		if(!r.frame(fs, ok) || !ok)
		{
			why = "no frame drawn";
			return false;
		}
		if(fs.overlayBound || r.scene.lastOverlay().srv || r.scene.ovlBoundFrames != 0 || r.scene.ovlNoneFrames != 1)
		{
			why = "an overlay was bound";
			return false;
		}
		px[k] = r.pixels();
	}
	if(px[0].empty() || px[0] != px[1])
	{
		why = "render_overlay=1 without a pair is not render_overlay=0's picture";
		return false;
	}
	if(CountLog("LOUD"))
	{
		why = "a LOUD line";
		return false;
	}
	if(!CountLog("render_overlay=0"))
	{
		why = "render_overlay=0 not logged";
		return false;
	}
	return true;
}

// A pair is bound with each copy's matrix: copy 0 = view 0, copy +1 = the EXE's wrap view, copy -1 derived; a new
// generation asks for a redraw with a still camera; the same generation does not; a stale pair (the reader keeps
// returning it) stays bound.
bool CasePairBound(std::string& why)
{
	Rig r;
	if(!r.start(1, true, why))
		return false;
	ID3D11ShaderResourceView* t = OverlayTexture(0x80402010u);
	if(!t)
	{
		why = "texture";
		return false;
	}
	r.fake->st = OverlayTapReader::State::Active;
	r.fake->give = true;
	r.fake->pair.srv = t;
	r.fake->pair.numViews = 2;
	r.fake->pair.generation = 7;
	r.fake->pair.slot = 1;
	ExeProj(r.fake->pair.overlayProj[0], 64, 64, -32, -32);
	Moved(r.fake->pair.overlayProj[0], kWrap, r.fake->pair.overlayProj[1]);
	c5r::terrain::FrameStats fs;
	bool ok = false;
	bool drawn = r.frame(fs, ok);
	const c5r::terrain::Overlay& o = r.scene.lastOverlay();
	float d[16];
	Moved(r.fake->pair.overlayProj[0], -kWrap, d);
	if(!drawn || !ok || !fs.overlayBound || o.srv != t || !Same(o.proj[1], r.fake->pair.overlayProj[0]) ||
	   !Same(o.proj[2], r.fake->pair.overlayProj[1]) || !Near(o.proj[0], d))
	{
		why = "the pair is not bound as copies 0 / +1 (EXE) and -1 (derived)";
		t->Release();
		return false;
	}
	if(r.scene.ovlPairs != 1 || r.scene.ovlBoundFrames != 1 || r.scene.ovlDerivedCopies != 1)
	{
		why = "counters: pairs " + std::to_string(r.scene.ovlPairs) + " bound " + std::to_string(r.scene.ovlBoundFrames) +
		      " derived " + std::to_string(r.scene.ovlDerivedCopies);
		t->Release();
		return false;
	}
	// the same generation with a still camera: nothing to draw
	drawn = r.frame(fs, ok);
	if(drawn)
	{
		why = "a redraw without a new generation";
		t->Release();
		return false;
	}
	// a new generation, camera still: a redraw
	r.fake->pair.generation = 8;
	r.fake->pair.slot = 2;
	drawn = r.frame(fs, ok);
	if(!drawn || !ok || !fs.overlayBound || r.scene.ovlPairs != 2)
	{
		why = "a new generation did not ask for a redraw";
		t->Release();
		return false;
	}
	// a stale pair (the reader returns the last good one): still bound at the next camera
	WriteCamera(r.m, 2.0f, 11);
	drawn = r.frame(fs, ok);
	if(!drawn || !ok || !fs.overlayBound || r.scene.lastOverlay().srv != t || r.scene.ovlPairs != 2)
	{
		why = "the stale pair is not kept bound";
		t->Release();
		return false;
	}
	// the pair goes (acquire false, e.g. a new load): the next frame is drawn without it
	r.fake->give = false;
	r.fake->st = OverlayTapReader::State::Waiting;
	drawn = r.frame(fs, ok);
	if(!drawn || !ok || fs.overlayBound || r.scene.lastOverlay().srv)
	{
		why = "a lost pair is still bound";
		t->Release();
		return false;
	}
	char line[4096];
	r.scene.secondLine(line, sizeof(line));
	if(!strstr(line, "overlay=WAITING") || !strstr(line, "pairs=2"))
	{
		why = std::string("second line: ") + line;
		t->Release();
		return false;
	}
	r.scene.totalsLine(line, sizeof(line));
	t->Release();
	if(!strstr(line, "overlay totals: frames with 3 without 1 pairs 2"))
	{
		why = std::string("totals: ") + line;
		return false;
	}
	return CountLog("LOUD") == 0 ? true : (why = "a LOUD line", false);
}

// Decision 5, MC #701: a Failed source is LOUD once with the reason's name and the reader's text, overlayFailed()
// carries the error text for the fatal route; the scene goes on drawing without the overlay.
bool CaseFailedIsFatalOnce(std::string& why)
{
	Rig r;
	if(!r.start(1, true, why))
		return false;
	r.fake->st = OverlayTapReader::State::Failed;
	r.fake->reason = VPOVLF_PATTERN;
	r.fake->text = "byte pattern at 0x6EAD50 differs";
	std::string text;
	if(r.scene.overlayFailed(text))
	{
		why = "failed before a poll";
		return false;
	}
	c5r::terrain::FrameStats fs;
	bool ok = false;
	for(int i = 0; i < 5; ++i)
	{
		WriteCamera(r.m, (float)i, 20 + i);
		if(!r.frame(fs, ok) || !ok || fs.overlayBound)
		{
			why = "the scene stopped drawing or bound an overlay";
			return false;
		}
	}
	if(!r.scene.overlayFailed(text) || text.find("VPOVL_FAILED reason 1 PATTERN") == std::string::npos ||
	   text.find("byte pattern at 0x6EAD50 differs") == std::string::npos)
	{
		why = "overlayFailed text: " + text;
		return false;
	}
	if(CountLog("LOUD: overlay tap FAILED") != 1 || CountLog("LOUD") != 1)
	{
		why = "LOUD lines: " + std::to_string(CountLog("LOUD"));
		return false;
	}
	// every reason has a name (VPOVLF_VERSION = 8 is 3703's)
	for(uint32_t k = 0; k <= 8; ++k)
		if(!strcmp(OverlayFailName(k), "?"))
		{
			why = "reason " + std::to_string(k) + " has no name";
			return false;
		}
	return true;
}

// Absent, Off and Waiting draw the transparent default, each state change logged once, never LOUD.
bool CaseStatesTransparent(std::string& why)
{
	Rig r;
	if(!r.start(1, true, why))
		return false;
	const OverlayTapReader::State sts[] = {OverlayTapReader::State::Absent, OverlayTapReader::State::Off,
	                                       OverlayTapReader::State::Waiting};
	int frame = 30;
	for(OverlayTapReader::State s : sts)
	{
		r.fake->st = s;
		for(int i = 0; i < 3; ++i)
		{
			WriteCamera(r.m, (float)(frame % 7), frame);
			frame++;
			c5r::terrain::FrameStats fs;
			bool ok = false;
			if(!r.frame(fs, ok) || !ok || fs.overlayBound)
			{
				why = std::string("state ") + OverlayStateName(s) + ": not drawn or bound";
				return false;
			}
		}
	}
	if(CountLog("overlay: source state") != 4 || CountLog("LOUD"))   // - -> WAITING at start, then the three
	{
		why = "state lines " + std::to_string(CountLog("overlay: source state")) + ", LOUD " + std::to_string(CountLog("LOUD"));
		return false;
	}
	std::string t;
	return !r.scene.overlayFailed(t) ? true : (why = "failed", false);
}

// A pair the scene cannot bind (numViews 0, a NaN matrix: a reader fault) is LOUD once per reason, counted, and the frame
// is drawn without the overlay; a later good pair is bound again.
bool CaseBadPair(std::string& why)
{
	Rig r;
	if(!r.start(1, true, why))
		return false;
	ID3D11ShaderResourceView* t = OverlayTexture(0);
	r.fake->st = OverlayTapReader::State::Active;
	r.fake->give = true;
	r.fake->pair.srv = t;
	r.fake->pair.numViews = 0;
	int frame = 40;
	c5r::terrain::FrameStats fs;
	bool ok = false;
	for(int i = 0; i < 3; ++i)
	{
		r.fake->pair.generation = 100 + i;
		WriteCamera(r.m, (float)i, frame++);
		if(!r.frame(fs, ok) || !ok || fs.overlayBound)
		{
			why = "a bad pair was bound or the frame not drawn";
			t->Release();
			return false;
		}
	}
	if(r.scene.ovlBadPairs != 3 || CountLog("LOUD: overlay: pair of generation") != 1)
	{
		why = "bad pairs " + std::to_string(r.scene.ovlBadPairs) + ", LOUD " + std::to_string(CountLog("LOUD"));
		t->Release();
		return false;
	}
	r.fake->pair.numViews = 1;
	ExeProj(r.fake->pair.overlayProj[0], 64, 64, -32, -32);
	r.fake->pair.overlayProj[0][13] = NAN;
	r.fake->pair.generation = 200;
	WriteCamera(r.m, 5, frame++);
	if(!r.frame(fs, ok) || !ok || fs.overlayBound || CountLog("not finite") != 1)
	{
		why = "a NaN matrix was bound or not logged";
		t->Release();
		return false;
	}
	ExeProj(r.fake->pair.overlayProj[0], 64, 64, -32, -32);
	r.fake->pair.generation = 201;
	WriteCamera(r.m, 6, frame++);
	const bool good = r.frame(fs, ok) && ok && fs.overlayBound;
	t->Release();
	return good ? true : (why = "a good pair after bad ones was not bound", false);
}

// MC #713's conditions for the fatal hook (HostConfig::fatal through OverlayFatalStep, as the host's loop runs it): it
// fires at most once, with exit code 5 and the reason's text, for an install failure (State::Failed); never for a stale
// pair during play (the reader keeps returning the last good pair, Active), nor for Absent / Off / Waiting; without a
// hook the failure is LOUD once and drawing goes on.
int g_fatalCalls = 0, g_fatalCode = 0;
std::string g_fatalWhy;
void FatalHook(int code, const char* why)
{
	g_fatalCalls++;
	g_fatalCode = code;
	g_fatalWhy = why ? why : "";
}

bool CaseFatalHook(std::string& why)
{
	g_fatalCalls = 0;
	{
		// play with a stale pair (no new generation for many frames) and the transparent states: never fatal
		Rig r;
		if(!r.start(1, true, why))
			return false;
		ID3D11ShaderResourceView* t = OverlayTexture(0x80000000u);
		r.fake->st = OverlayTapReader::State::Active;
		r.fake->give = true;
		r.fake->pair.srv = t;
		r.fake->pair.numViews = 1;
		r.fake->pair.generation = 3;
		ExeProj(r.fake->pair.overlayProj[0], 64, 64, -32, -32);
		bool done = false;
		c5r::terrain::FrameStats fs;
		bool ok = false;
		for(int i = 0; i < 40; ++i)
		{
			if(i == 30)
			{
				r.fake->give = false;
				r.fake->st = (i % 2) ? OverlayTapReader::State::Waiting : OverlayTapReader::State::Absent;
			}
			WriteCamera(r.m, (float)(i % 5), 50 + i);
			r.frame(fs, ok);
			OverlayFatalStep(r.scene, &FatalHook, done);
		}
		t->Release();
		if(g_fatalCalls || done)
		{
			why = "the hook fired during play without an install failure";
			return false;
		}
	}
	{
		// an install failure: once, code 5, the reason named; the scene draws on
		Rig r;
		if(!r.start(1, true, why))
			return false;
		r.fake->st = OverlayTapReader::State::Failed;
		r.fake->reason = VPOVLF_HANDLE;
		r.fake->text = "[0xDA12A4] is not a texture view";
		bool done = false;
		c5r::terrain::FrameStats fs;
		bool ok = false;
		for(int i = 0; i < 5; ++i)
		{
			WriteCamera(r.m, (float)i, 60 + i);
			r.frame(fs, ok);
			OverlayFatalStep(r.scene, &FatalHook, done);
		}
		if(g_fatalCalls != 1 || g_fatalCode != kOverlayFatalExitCode || kOverlayFatalExitCode != 5 ||
		   g_fatalWhy.find("VPOVL_FAILED reason 2 HANDLE") == std::string::npos ||
		   g_fatalWhy.find("[0xDA12A4] is not a texture view") == std::string::npos)
		{
			why = "hook calls " + std::to_string(g_fatalCalls) + ", code " + std::to_string(g_fatalCode) + ", text: " + g_fatalWhy;
			return false;
		}
		if(CountLog("LOUD") != 1 || !CountLog("the server ends (exit code 5)"))
		{
			why = "LOUD lines " + std::to_string(CountLog("LOUD"));
			return false;
		}
	}
	{
		// no hook: LOUD once, the scene draws on
		Rig r;
		if(!r.start(1, true, why))
			return false;
		r.fake->st = OverlayTapReader::State::Failed;
		r.fake->reason = VPOVLF_KILLED;
		bool done = false;
		c5r::terrain::FrameStats fs;
		bool ok = false;
		for(int i = 0; i < 3; ++i)
		{
			WriteCamera(r.m, (float)i, 70 + i);
			const bool drawn = r.frame(fs, ok);
			OverlayFatalStep(r.scene, nullptr, done);
			if(!drawn || !ok)
			{
				why = "no draw after the failure without a hook";
				return false;
			}
		}
		if(!done || g_fatalCalls != 1 || !CountLog("no fatal hook"))
		{
			why = "without a hook";
			return false;
		}
	}
	return true;
}

// render_overlay in server.cfg: 0 and 1 accepted into HostConfig::overlay, anything else refused with the key named.
bool CaseOption(std::string& why)
{
	const char* vals[] = {"0", "1", "2", "yes", "on"};
	for(int i = 0; i < 5; ++i)
	{
		HostConfig c;
		std::string w;
		auto get = [&](const char* key) -> std::string { return !strcmp(key, "render_overlay") ? vals[i] : ""; };
		const bool ok = ParseHostConfig(get, "C:\\game", "C:\\run", c, w);
		if(ok != (i < 2) || (!ok && w.find("render_overlay") == std::string::npos) || (ok && c.overlay != i))
		{
			why = std::string("render_overlay=") + vals[i] + ": " + (ok ? "accepted" : w);
			return false;
		}
	}
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
};
const Case kCases[] = {
	{"copies", CaseCopies},
	{"todays_picture", CaseTodaysPicture},
	{"pair_bound", CasePairBound},
	{"failed_is_fatal_once", CaseFailedIsFatalOnce},
	{"states_transparent", CaseStatesTransparent},
	{"bad_pair", CaseBadPair},
	{"option", CaseOption},
	{"fatal_hook", CaseFatalHook},
};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	const char* e = getenv("C5R_EXE");
	if(!e || !*e)
	{
		printf("test_scene_overlay: C5R_EXE not set (the terrain programs come from the EXE)\n");
		return 99;
	}
	g_exe.assign(e, e + strlen(e));
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("test_scene_overlay: no WARP device\n");
		return 99;
	}
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			if(!strcmp(argv[i], c.name))
				want = true;
		if(!want)
			continue;
		g_log.clear();
		std::string why;
		const bool ok = c.fn(why);
		run++;
		fails += ok ? 0 : 1;
		printf("CASE %s %s%s%s\n", c.name, ok ? "PASS" : "FAIL", ok ? "" : " ", why.c_str());
		if(!ok)
			for(const auto& l : g_log)
				printf("    log: %s\n", l.c_str());
		fflush(stdout);
	}
	if(!run)
	{
		printf("test_scene_overlay: no case matches\n");
		return 2;
	}
	g_ctx->Release();
	g_dev->Release();
	return fails;
}
