// test_scene_shadow.cpp - stand-in tests of the models' shadow and the polar ice in server.exe's terrain scene
// (server/src/render/srv_render_scene.cpp, epic 0169, issue 4308). Written 2026-11-15 for this repository
// (GPL-3.0-or-later). Model: server/tests/render/overlay/test_scene_overlay.cpp (its main section, camera and field).
//
// Each case builds its own main section (a fake camera block and a height-field tap published through the VPShellTap
// block), starts a Scene on a WARP device with the EXE's terrain programs (C5R_EXE, read-only) and replaces the scene's
// shadow / ice sources by fakes that hand the scene what the case needs (triples of each quality, refusals, states,
// failures; a synthetic ice mesh of one quad, file-form-free: the ice reader is 4307's). The ice cases also need the
// install (C5R_GAME_DIR, read-only: the ice's five textures, the fog ramp). The checks read what the scene bound for the
// draw (Scene::lastShadow, lastShadowFrame, lastIceDrawn, counters), the pixels and the log.
// Output: one line per case, `CASE <name> PASS|FAIL <detail>`; exit code = the number of failed cases, 2 on usage, 99
// without a WARP device, C5R_EXE or (ice cases) C5R_GAME_DIR.
//
//   test_scene_shadow.exe [case ...]      (no argument: every case)
#include "srv_render_scene.h"
#include "srv_scene_recorder.h"
#include "srv_scene_bundle.h"
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

static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvrender;
namespace c5h = c5r::heightfield;

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::wstring g_exe, g_game;
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

const uint32_t kW = 160, kH = 120;
const float kWrap = 255.0f;   // 64 W - 1 for the 4-plot field below

// ---- fakes -------------------------------------------------------------------------------------------------------------
struct FakeShadow : ShadowSource
{
	ShadowTapReader::State st = ShadowTapReader::State::Waiting;
	uint32_t reason = 0;
	std::string text;
	bool give = false;
	ShadowFrame frame;
	int64_t refused = 0;
	bool acquire(ShadowFrame& out) override
	{
		out = ShadowFrame();
		if(!give)
			return false;
		out = frame;
		return true;
	}
	ShadowTapReader::State state() const override { return st; }
	uint32_t failReason() const override { return reason; }
	std::string why() const override { return text; }
	int64_t refusedCount() const override { return refused; }
};

struct FakeIce : IceSource
{
	IceTapReader::State st = IceTapReader::State::Waiting;
	bool failed = false;
	std::string text;
	std::shared_ptr<const c5r::ice::Mesh> m;
	uint32_t gen = 0, load = 0;
	int64_t refused = 0;
	int polls = 0;
	bool poll(uint32_t have) override
	{
		polls++;
		return m && gen > have;
	}
	std::shared_ptr<const c5r::ice::Mesh> mesh() const override { return m; }
	uint32_t generation() const override { return gen; }
	uint32_t loadSeq() const override { return load; }
	IceTapReader::State state() const override { return st; }
	bool installFailure() const override { return failed; }
	std::string why() const override { return text; }
	int64_t refusedCount() const override { return refused; }
};

// ---- the main section, camera and field (as test_scene_overlay.cpp) ----------------------------------------------------
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

bool MakeMain(Main& m, std::string& why, uint32_t exePid)
{
	wchar_t nm[96];
	swprintf_s(nm, L"Local\\VPShdSceneTest.%u.%d", GetCurrentProcessId(), ++g_serial);
	m.name = nm;
	m.h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, nm);
	m.hdr = m.h ? (VPShellHeader*)MapViewOfFile(m.h, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES) : nullptr;
	if(!m.hdr)
	{
		why = "main section";
		return false;
	}
	WriteCamera(m, 0.0f, 10);
	std::vector<uint8_t> img;
	const c5r::Status st = c5h::writeTap(Field(), img);
	if(!st.ok())
	{
		why = "writeTap: " + st.str();
		return false;
	}
	wchar_t tn[64];
	swprintf_s(tn, L"Local\\VPShdSceneTestTap.%u.%d", GetCurrentProcessId(), g_serial);
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
	t->exePid = exePid;
	wcscpy_s(t->sectionName, tn);
	MemoryBarrier();
	t->seq = 2;
	return true;
}

struct RigOpts
{
	int shadow = 1, ice = 0;
	bool fakeShadow = true, fakeIce = false, install = false;
	double shadowWaitMs = 20000, iceWaitMs = 0;
	uint32_t exePid = 0;
};

struct Rig
{
	Main m;
	Scene scene;
	FakeShadow* sh = nullptr;
	FakeIce* ice = nullptr;
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* ds = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	~Rig()
	{
		scene.releaseDevice();
		scene.detach();
		SafeRelease(dsv);
		SafeRelease(ds);
		SafeRelease(rtv);
		SafeRelease(rt);
	}
	bool start(const RigOpts& ro, std::string& why)
	{
		if(!MakeMain(m, why, ro.exePid))
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
		o.overlay = 0;
		o.shadow = ro.shadow;
		o.shadowWaitMs = ro.shadowWaitMs;
		o.ice = ro.ice;
		o.iceWaitMs = ro.iceWaitMs;
		if(ro.install)
			o.install = g_game;
		const c5r::Status st = scene.init(g_dev, g_ctx, o);
		if(!st.ok())
		{
			why = "init: " + st.str();
			return false;
		}
		if(ro.fakeShadow)
		{
			sh = new FakeShadow();
			scene.setShadowSource(std::unique_ptr<ShadowSource>(sh));
		}
		if(ro.fakeIce)
		{
			ice = new FakeIce();
			scene.setIceSource(std::unique_ptr<IceSource>(ice));
		}
		scene.poll();
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
		d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &ds)) || FAILED(g_dev->CreateDepthStencilView(ds, nullptr, &dsv)))
		{
			why = "depth";
			return false;
		}
		return true;
	}
	bool frame(c5r::terrain::FrameStats& fs, bool& ok)
	{
		scene.poll();
		scene.markDirty();
		SceneFrame f;
		if(!scene.decide(f))
		{
			ok = true;
			return false;
		}
		g_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		ok = scene.draw(g_ctx, rtv, dsv, kW, kH, f, fs).ok();
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
	// draws one frame and returns its pixels (empty when nothing was drawn or the draw failed)
	std::vector<uint8_t> shot()
	{
		c5r::terrain::FrameStats fs;
		bool ok = false;
		if(!frame(fs, ok) || !ok)
			return std::vector<uint8_t>();
		return pixels();
	}
};

ID3D11ShaderResourceView* Tex(DXGI_FORMAT f, uint32_t w, uint32_t h, const void* px, uint32_t bpp)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w;
	d.Height = h;
	d.MipLevels = d.ArraySize = 1;
	d.Format = f;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA s = {px, w * bpp, 0};
	ID3D11Texture2D* t = nullptr;
	ID3D11ShaderResourceView* v = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, &s, &t)))
		return nullptr;
	g_dev->CreateShaderResourceView(t, nullptr, &v);
	t->Release();
	return v;
}
ID3D11ShaderResourceView* R16(uint16_t v)
{
	std::vector<uint16_t> px(16, v);
	return Tex(DXGI_FORMAT_R16_UNORM, 4, 4, px.data(), 2);
}
ID3D11ShaderResourceView* F16(uint16_t r)
{
	std::vector<uint16_t> px(64, 0);
	for(int i = 0; i < 16; ++i)
		px[i * 4] = r;
	return Tex(DXGI_FORMAT_R16G16B16A16_FLOAT, 4, 4, px.data(), 8);
}

// A ShadowMatrix0 as 4302 read it live (camera 1 rows 0, 2, 3; row 1 an orthographic light's) [terrain-surface.md 9]
void LiveMatrix(float m[16])
{
	const float v[16] = {0.001563f, 0.000285f, 7.2e-6f, 0, -0.000391f, 0.001407f, 3.6e-5f, 0, -0.000782f, 0.000569f, 1.44e-5f, 0,
	                     -0.5516f, 0.2932f, 0.4135f, 1};
	memcpy(m, v, sizeof(v));
}
// view 0's matrix with row 3 moved by shift times row 0 (the EXE's wrap view, terrain-surface.md 11 S3)
void Moved(const float* p, double shift, float* out)
{
	memcpy(out, p, 64);
	for(int j = 0; j < 4; ++j)
		out[12 + j] = (float)(shift * p[j] + p[12 + j]);
}
bool Same(const float* a, const float* b) { return memcmp(a, b, 64) == 0; }

ShadowFrame Triple(ID3D11ShaderResourceView* srv, uint32_t q, uint32_t gen, float strength)
{
	ShadowFrame f;
	f.srv = srv;
	f.format = q == 0 ? 0 : q == 1 ? 10 : 56;
	f.width = f.height = srv ? 4 : 0;
	f.numViews = 1;
	LiveMatrix(f.matrices[0]);
	f.strength = strength;
	f.presentCount = 100 + gen;
	f.generation = gen;
	f.quality = q;
	f.slot = gen % 3;
	return f;
}

// ---- shadow cases ------------------------------------------------------------------------------------------------------

// ShadowCopiesFromFrame: every rule and refusal (no device).
bool CaseShadowCopies(std::string& why)
{
	ShadowFrame f = Triple(nullptr, 3, 1, 1.0f);
	ShadowCopies sc;
	if(!ShadowCopiesFromFrame(f, 3583.0f, sc, why))
		return false;
	float m[16];
	Moved(f.matrices[0], -3583.0, m);
	if(!Same(sc.matrix[1], f.matrices[0]) || sc.fromExe[0] || sc.fromExe[2] || !Same(sc.matrix[0], m))
	{
		why = "one view: copy 0 = view 0's bytes, copies -1 / +1 derived by -+3583";
		return false;
	}
	// the EXE's wrap view at +3583 (row 3 moved by W times row 0): copy +1 as written, also with 4302's float noise (2.2e-7)
	Moved(f.matrices[0], 3583.0, f.matrices[1]);
	f.matrices[1][12] += 2.2e-7f;
	f.matrices[1][15] = 0.99999994f;
	f.numViews = 2;
	if(!ShadowCopiesFromFrame(f, 3583.0f, sc, why) || !sc.fromExe[2] || sc.fromExe[0] || !Same(sc.matrix[2], f.matrices[1]) || sc.unmatchedViews)
	{
		why = "view 1 at +3583 must be copy +1 (" + why + ")";
		return false;
	}
	Moved(f.matrices[0], -3583.0, f.matrices[1]);
	if(!ShadowCopiesFromFrame(f, 3583.0f, sc, why) || !sc.fromExe[0] || !Same(sc.matrix[0], f.matrices[1]))
	{
		why = "view 1 at -3583 must be copy -1";
		return false;
	}
	// 3584 is not the EXE's shift: unmatched, copy +1 derived
	Moved(f.matrices[0], 3584.0, f.matrices[1]);
	if(!ShadowCopiesFromFrame(f, 3583.0f, sc, why) || sc.unmatchedViews != 1 || sc.fromExe[2])
	{
		why = "a view at +3584 must be unmatched";
		return false;
	}
	// another light (row 0 changed by 10 %, above the 1e-5 of the largest element): unmatched
	Moved(f.matrices[0], 3583.0, f.matrices[1]);
	f.matrices[1][0] *= 1.1f;
	if(!ShadowCopiesFromFrame(f, 3583.0f, sc, why) || sc.unmatchedViews != 1)
	{
		why = "a view of another light must be unmatched";
		return false;
	}
	// no wrap: every copy view 0's
	if(!ShadowCopiesFromFrame(f, 0.0f, sc, why) || !Same(sc.matrix[0], f.matrices[0]) || !Same(sc.matrix[2], f.matrices[0]))
	{
		why = "no wrap: every copy must be view 0's";
		return false;
	}
	// refusals
	f.numViews = 0;
	if(ShadowCopiesFromFrame(f, 3583.0f, sc, why) || why.find("numViews 0") == std::string::npos)
	{
		why = "numViews 0 must be refused";
		return false;
	}
	f.numViews = 5;
	if(ShadowCopiesFromFrame(f, 3583.0f, sc, why) || why.find("numViews 5") == std::string::npos)
	{
		why = "numViews 5 must be refused";
		return false;
	}
	f.numViews = 2;
	f.matrices[1][7] = NAN;
	if(ShadowCopiesFromFrame(f, 3583.0f, sc, why) || why.find("view 1") == std::string::npos)
	{
		why = "a NaN in view 1 must be refused";
		return false;
	}
	why.clear();
	return true;
}

// No shadow (render_shadow=0, or a source with nothing yet) is the picture before 4308, bit for bit; quality 0 too.
bool CaseNoShadowIsTodaysPicture(std::string& why)
{
	std::vector<uint8_t> off, waiting, q0;
	{
		Rig r;
		RigOpts ro;
		ro.shadow = 0;
		ro.fakeShadow = false;
		if(!r.start(ro, why))
			return false;
		off = r.shot();
		if(r.scene.lastShadow().srv || CountLog("render_shadow=0") != 1)
		{
			why = "render_shadow=0: a shadow bound, or the init line missing";
			return false;
		}
	}
	{
		Rig r;
		if(!r.start(RigOpts(), why))
			return false;
		waiting = r.shot();
		ShadowFrame got;
		if(r.scene.lastShadow().srv || r.scene.lastShadowFrame(got) || r.scene.shadowCounters().noneFrames != 1)
		{
			why = "a waiting source: nothing may be bound";
			return false;
		}
		r.sh->st = ShadowTapReader::State::Active;
		r.sh->give = true;
		r.sh->frame = Triple(nullptr, 0, 5, 0.0f);   // quality 0: no texture, strength 0
		q0 = r.shot();
		if(r.scene.lastShadow().srv || !r.scene.lastShadowFrame(got) || got.quality != 0 || r.scene.shadowCounters().q0Frames != 1)
		{
			why = "quality 0: the no-shadow path, the frame named as bound at q 0";
			return false;
		}
	}
	if(off.empty() || off != waiting || off != q0)
	{
		why = "pictures differ: off / waiting / q0 must be bit-equal (" + std::to_string(off.size()) + " B)";
		return false;
	}
	why.clear();
	return true;
}

// q3 / q2 / q1 triples bound as said: t3 = the triple's view, Arg6 = its strength, ShadowMatrix0 per wrap copy from its
// own views; every field of the bound frame from one triple (never mixed with the previous one); the q1 float map drawn.
bool CaseQualitiesBound(std::string& why)
{
	Rig r;
	if(!r.start(RigOpts(), why))
		return false;
	ID3D11ShaderResourceView* a = R16(0xFFFF);
	ID3D11ShaderResourceView* b = R16(0x8000);
	ID3D11ShaderResourceView* c = F16(0x3C00);
	if(!a || !b || !c)
	{
		why = "textures";
		return false;
	}
	struct Q
	{
		ID3D11ShaderResourceView* srv;
		uint32_t q;
		float strength;
	} qs[3] = {{a, 3, 1.0f}, {b, 2, 0.5f}, {c, 1, 0.25f}};
	bool ok = true;
	for(uint32_t i = 0; i < 3 && ok; ++i)
	{
		r.sh->st = ShadowTapReader::State::Active;
		r.sh->give = true;
		r.sh->frame = Triple(qs[i].srv, qs[i].q, 10 + i, qs[i].strength);
		r.sh->frame.matrices[0][12] += 0.01f * (float)i;   // each triple's own view 0
		Moved(r.sh->frame.matrices[0], kWrap, r.sh->frame.matrices[1]);
		r.sh->frame.numViews = 2;
		c5r::terrain::FrameStats fs;
		bool dok = false;
		if(!r.frame(fs, dok) || !dok)
		{
			why = "q" + std::to_string(qs[i].q) + ": not drawn or the draw failed (" + (g_log.empty() ? "" : g_log.back()) + ")";
			ok = false;
			break;
		}
		const c5r::terrain::Shadow& s = r.scene.lastShadow();
		ShadowFrame got;
		const bool have = r.scene.lastShadowFrame(got);
		float derived[16];
		Moved(r.sh->frame.matrices[0], -kWrap, derived);
		if(s.srv != qs[i].srv || s.strength != qs[i].strength || !Same(s.matrix[1], r.sh->frame.matrices[0]) ||
		   !Same(s.matrix[2], r.sh->frame.matrices[1]) || !Same(s.matrix[0], derived) || !have || got.generation != 10 + i ||
		   got.quality != qs[i].q || !fs.shadowOn)
		{
			why = "q" + std::to_string(qs[i].q) + ": the bound shadow is not the triple's own (srv, strength, matrices, generation)";
			ok = false;
		}
	}
	if(ok && (r.scene.shadowCounters().boundFrames != 3 || r.scene.shadowCounters().triples != 3 || r.scene.shadowCounters().quality != 1 ||
	          r.scene.shadowCounters().derivedCopies != 3))
	{
		why = "counters: bound " + std::to_string(r.scene.shadowCounters().boundFrames) + " triples " +
		      std::to_string(r.scene.shadowCounters().triples) + " derived " + std::to_string(r.scene.shadowCounters().derivedCopies);
		ok = false;
	}
	a->Release();
	b->Release();
	c->Release();
	if(ok)
		why.clear();
	return ok;
}

// A refused or torn slot: the reader keeps handing over the last good triple (its contract) and counts the refusal; the
// scene binds that triple, draws on, never fails. A source that hands nothing over (Waiting again) draws no shadow.
bool CaseRefusedKeepsLastGood(std::string& why)
{
	Rig r;
	if(!r.start(RigOpts(), why))
		return false;
	ID3D11ShaderResourceView* a = R16(0xFFFF);
	r.sh->st = ShadowTapReader::State::Active;
	r.sh->give = true;
	r.sh->frame = Triple(a, 3, 7, 1.0f);
	if(r.shot().empty())
	{
		why = "first frame";
		return false;
	}
	r.sh->refused = 3;   // three slots refused since: the same triple handed over
	const std::vector<uint8_t> p = r.shot();
	ShadowFrame got;
	std::string fw;
	const bool ok = !p.empty() && r.scene.lastShadow().srv == a && r.scene.lastShadowFrame(got) && got.generation == 7 &&
	                !r.scene.shadowFailed(fw) && r.scene.shadowCounters().triples == 1;
	r.sh->give = false;
	r.sh->st = ShadowTapReader::State::Waiting;
	r.shot();
	const bool none = !r.scene.lastShadow().srv && !r.scene.shadowFailed(fw);
	a->Release();
	if(!ok || !none)
	{
		why = !ok ? "the last good triple must stay bound" : "a source with nothing must draw no shadow";
		return false;
	}
	return true;
}

int g_fatalCalls = 0;
std::string g_fatalWhy;
int g_fatalCode = 0;
void Fatal(int code, const char* w)
{
	g_fatalCalls++;
	g_fatalCode = code;
	g_fatalWhy = w;
}

// Failed (an install failure): LOUD once, shadowFailed with the reader's text, ShadowFatalStep calls fatal once with
// code 5; without a fatal hook the scene draws on without the shadow.
bool CaseShadowFailedIsFatalOnce(std::string& why)
{
	Rig r;
	if(!r.start(RigOpts(), why))
		return false;
	r.sh->st = ShadowTapReader::State::Failed;
	r.sh->reason = 1;
	r.sh->text = "the shadow tap could not install: VPSHD_FAILED PATTERN (reason 1: pattern at VA 0x523CA6 does not match)";
	g_fatalCalls = 0;
	bool done = false;
	for(int i = 0; i < 3; ++i)
	{
		r.scene.poll();
		ShadowFatalStep(r.scene, Fatal, done);
	}
	std::string fw;
	if(!r.scene.shadowFailed(fw) || fw.find("0x523CA6") == std::string::npos || g_fatalCalls != 1 || g_fatalCode != 5 ||
	   g_fatalWhy != fw || CountLog("LOUD: the models' shadow cannot install") != 1)
	{
		why = "fatal calls " + std::to_string(g_fatalCalls) + ", text '" + fw + "'";
		return false;
	}
	bool done2 = false;
	ShadowFatalStep(r.scene, nullptr, done2);   // no hook: a note, no call
	if(g_fatalCalls != 1 || CountLog("no fatal hook") != 1)
	{
		why = "without a hook nothing may be called";
		return false;
	}
	return true;
}

// OFF (the composite's kill switch): LOUD once, no shadow, never fatal.
bool CaseShadowOffLoudOnce(std::string& why)
{
	Rig r;
	if(!r.start(RigOpts(), why))
		return false;
	r.sh->st = ShadowTapReader::State::Off;
	bool done = false;
	g_fatalCalls = 0;
	for(int i = 0; i < 3; ++i)
	{
		r.shot();
		ShadowFatalStep(r.scene, Fatal, done);
	}
	std::string fw;
	if(CountLog("LOUD: shadow: the tap is OFF") != 1 || r.scene.shadowFailed(fw) || g_fatalCalls || r.scene.lastShadow().srv)
	{
		why = "OFF must be LOUD once and never fatal";
		return false;
	}
	return true;
}

// The tap readers as made by the scene from the EXE's pid (no fake): no shadow section for this pid -> Absent; after the
// bound (here 200 ms) from the first field with a valid grid -> an install failure naming the bound; the ice (the stub /
// no section) is never failed without its own bound, and is with render_test_ice_wait_ms.
bool CaseAbsentBound(std::string& why)
{
	Rig r;
	RigOpts ro;
	ro.fakeShadow = false;
	ro.ice = 0;
	ro.shadowWaitMs = 200;
	ro.exePid = GetCurrentProcessId();   // no Local\VPShellShadowTap.<pid> exists
	if(!r.start(ro, why))
		return false;
	std::string fw;
	r.scene.poll();
	if(r.scene.shadowFailed(fw))
	{
		why = "failed before the bound";
		return false;
	}
	Sleep(300);
	r.scene.poll();
	if(!r.scene.shadowFailed(fw) || fw.find("no shadow tap section within 200 ms") == std::string::npos ||
	   r.scene.shadowCounters().readers != 1 || CountLog("shadow: reader for EXE pid") != 1)
	{
		why = "after the bound: '" + fw + "'";
		return false;
	}
	why.clear();
	return true;
}

bool CaseShadowOption(std::string& why)
{
	const char* keys[] = {"render_shadow", "render_ice"};
	const char* vals[] = {"0", "1", "2", "yes", ""};
	for(const char* key : keys)
		for(int i = 0; i < 5; ++i)
		{
			HostConfig c;
			std::string w;
			auto get = [&](const char* k) -> std::string { return !strcmp(k, key) ? vals[i] : ""; };
			const bool ok = ParseHostConfig(get, "C:\\game", "C:\\run", c, w);
			const bool want = i < 2 || i == 4;
			if(ok != want || (!ok && w.find(key) == std::string::npos))
			{
				why = std::string(key) + "=" + vals[i] + ": " + (ok ? "accepted" : w);
				return false;
			}
		}
	for(const char* key : {"render_test_shadow_wait_ms", "render_test_ice_wait_ms"})
	{
		const char* bad[] = {"-1", "600001", "x"};
		for(const char* v : bad)
		{
			HostConfig c;
			std::string w;
			auto get = [&](const char* k) -> std::string { return !strcmp(k, key) ? v : ""; };
			if(ParseHostConfig(get, "C:\\game", "C:\\run", c, w) || w.find(key) == std::string::npos)
			{
				why = std::string(key) + "=" + v + " must be refused";
				return false;
			}
		}
	}
	return true;
}

// ---- ice cases (need C5R_GAME_DIR) -------------------------------------------------------------------------------------

// One quad over the camera's ground (both windings), floating at z 55 (alpha 1: water 30 - 8 + 33).
std::shared_ptr<c5r::ice::Mesh> QuadMesh(bool broken = false)
{
	std::shared_ptr<c5r::ice::Mesh> m(new c5r::ice::Mesh());
	const int16_t s = 32767;
	const int16_t xy[4][2] = {{(int16_t)-s, (int16_t)-s}, {s, (int16_t)-s}, {s, s}, {(int16_t)-s, s}};
	for(int i = 0; i < 4; ++i)
	{
		uint8_t v[8];
		memcpy(v, &xy[i][0], 2);
		memcpy(v + 2, &xy[i][1], 2);
		v[4] = 128;
		v[5] = 128;
		v[6] = 255;
		v[7] = 255;
		m->vb.insert(m->vb.end(), v, v + 8);
	}
	const uint16_t idx[12] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
	m->ib.assign((const uint8_t*)idx, (const uint8_t*)idx + sizeof(idx));
	c5r::ice::Chunk c;
	c.box[0] = -40;
	c.box[1] = -30;
	c.box[2] = 40;
	c.box[3] = 30;
	c.triangles = broken ? 5 : 4;   // 5: start + 3 x 5 past the 12 indices: setMesh refuses it
	m->chunks.push_back(c);
	c5r::ice::IceFields& f = m->fields;
	f.valid = true;
	namespace fd = c5r::ice::field;
	f.set(fd::kBox, -40);
	f.set(fd::kBox + 4, -30);
	f.set(fd::kBox + 8, 40);
	f.set(fd::kBox + 12, 30);
	f.set(fd::kWaterHeight, 30);
	f.set(fd::kHeightAbove, 25);
	f.set(fd::kDepthBelow, 8);
	f.set(fd::kDirA + 8, 1);
	f.set(fd::kDirB + 8, 1);
	f.set(fd::kTextureRepeat, 0.005f);
	f.set(fd::kBumpRepeat, 0.009f);
	f.set(fd::kSpecularPower, 150);
	f.set(fd::kColorRamp, 0.1f);
	f.set(fd::kAlphaRamp, 0.5f);
	return m;
}

size_t Differ(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
	if(a.size() != b.size())
		return (size_t)-1;
	size_t n = 0;
	for(size_t i = 0; i < a.size(); i += 4)
		n += memcmp(&a[i], &b[i], 4) != 0;
	return n;
}

// The ice drawn at its place: render_ice=0 and ice on without a mesh are the same picture bit for bit; with the quad the
// ice covers pixels (drawn once per frame: copies 3 on the wrapping map, one draw each), and the next frame equals it.
bool CaseIceDrawn(std::string& why)
{
	std::vector<uint8_t> off, noMesh, on, again;
	{
		Rig r;
		RigOpts ro;
		ro.install = true;
		ro.ice = 0;
		if(!r.start(ro, why))
			return false;
		off = r.shot();
		if(r.scene.lastIceDrawn() || CountLog("render_ice=0") != 1)
		{
			why = "render_ice=0: ice drawn or the init line missing";
			return false;
		}
	}
	{
		Rig r;
		RigOpts ro;
		ro.install = true;
		ro.ice = 1;
		ro.fakeIce = true;
		if(!r.start(ro, why))
			return false;
		std::string fw;
		if(r.scene.iceFailed(fw))
		{
			why = "the ice did not start: " + fw;
			return false;
		}
		noMesh = r.shot();
		r.ice->st = IceTapReader::State::Active;
		r.ice->m = QuadMesh();
		r.ice->gen = 1;
		r.ice->load = 1;
		on = r.shot();
		const c5r::ice::FrameStats st = r.scene.lastIceStats();
		const bool drawn = r.scene.lastIceDrawn() && r.scene.iceGeneration() == 1 && st.draws >= 1 && st.copies == 3;
		again = r.shot();
		if(!drawn || r.scene.iceCounters().drawnFrames != 2 || r.scene.iceCounters().meshes != 1)
		{
			why = "ice not drawn: drawn " + std::to_string(r.scene.lastIceDrawn()) + " copies " + std::to_string(st.copies) + " draws " +
			      std::to_string(st.draws) + " frames " + std::to_string(r.scene.iceCounters().drawnFrames) +
			      (g_log.empty() ? "" : " last log: " + g_log.back());
			return false;
		}
	}
	const size_t dOff = Differ(off, noMesh), dOn = Differ(noMesh, on), dAgain = Differ(on, again);
	printf("  ice: off against on-without-mesh %zu px differ, with the quad %zu px of %u, the next frame %zu\n", dOff, dOn, kW * kH, dAgain);
	if(off.empty() || dOff != 0 || dOn == 0 || dOn == (size_t)-1 || dAgain != 0)
	{
		why = "pictures: off/no mesh " + std::to_string(dOff) + ", quad " + std::to_string(dOn) + ", again " + std::to_string(dAgain);
		return false;
	}
	why.clear();
	return true;
}

// A mesh the module refuses (a chunk past the IB) in play: LOUD, counted, the last good mesh stays and is drawn; never
// fatal. A draw error is never fatal either.
bool CaseIceRefusedKeepsLastGood(std::string& why)
{
	Rig r;
	RigOpts ro;
	ro.install = true;
	ro.ice = 1;
	ro.fakeIce = true;
	if(!r.start(ro, why))
		return false;
	r.ice->st = IceTapReader::State::Active;
	r.ice->m = QuadMesh();
	r.ice->gen = 1;
	const std::vector<uint8_t> good = r.shot();
	r.ice->m = QuadMesh(true);
	r.ice->gen = 2;
	r.ice->refused = 1;
	const std::vector<uint8_t> after = r.shot();
	const int polls = r.ice->polls;
	r.shot();   // the refused generation is not offered again
	std::string fw;
	bool done = false;
	g_fatalCalls = 0;
	IceFatalStep(r.scene, Fatal, done);
	if(r.scene.iceCounters().meshRefused != 1 || r.scene.iceGeneration() != 1 || Differ(good, after) != 0 ||
	   CountLog("LOUD: ice: in play: the module refused the mesh of generation 2") != 1 || r.scene.iceFailed(fw) || g_fatalCalls ||
	   !r.scene.lastIceDrawn() || r.ice->polls != polls + 1)
	{
		why = "refused " + std::to_string(r.scene.iceCounters().meshRefused) + " gen " + std::to_string(r.scene.iceGeneration()) +
		      " differ " + std::to_string(Differ(good, after));
		return false;
	}
	return true;
}

// Install failures: (a) the source's installFailure() -> LOUD once, fatal once with its text; (b) the ice module that
// cannot start (no install given) -> the same route with the reason; an in-play fault (above) never.
bool CaseIceInstallFatalOnce(std::string& why)
{
	{
		Rig r;
		RigOpts ro;
		ro.install = true;
		ro.ice = 1;
		ro.fakeIce = true;
		if(!r.start(ro, why))
			return false;
		r.ice->st = IceTapReader::State::Failed;
		r.ice->failed = true;
		r.ice->text = "the ice tap could not install: pattern at VA 0x70AF16 does not match";
		g_fatalCalls = 0;
		bool done = false;
		for(int i = 0; i < 3; ++i)
		{
			r.scene.poll();
			IceFatalStep(r.scene, Fatal, done);
		}
		std::string fw;
		if(!r.scene.iceFailed(fw) || fw.find("0x70AF16") == std::string::npos || g_fatalCalls != 1 || g_fatalCode != 5 ||
		   CountLog("LOUD: the polar ice cannot install") != 1)
		{
			why = "(a) fatal calls " + std::to_string(g_fatalCalls) + " text '" + fw + "'";
			return false;
		}
	}
	{
		Rig r;
		RigOpts ro;
		ro.install = false;   // the module cannot read its textures
		ro.ice = 1;
		ro.fakeIce = true;
		if(!r.start(ro, why))
			return false;
		g_fatalCalls = 0;
		bool done = false;
		r.scene.poll();
		IceFatalStep(r.scene, Fatal, done);
		IceFatalStep(r.scene, Fatal, done);
		std::string fw;
		if(!r.scene.iceFailed(fw) || fw.find("no game install") == std::string::npos || g_fatalCalls != 1)
		{
			why = "(b) '" + fw + "' calls " + std::to_string(g_fatalCalls);
			return false;
		}
	}
	return true;
}

// The ice tap's Absent state with render_test_ice_wait_ms: an install failure after the bound (the stub reader here).
bool CaseIceAbsentBound(std::string& why)
{
	Rig r;
	RigOpts ro;
	ro.install = true;
	ro.ice = 1;
	ro.fakeShadow = false;
	ro.shadow = 0;
	ro.iceWaitMs = 200;
	ro.exePid = GetCurrentProcessId();
	if(!r.start(ro, why))
		return false;
	std::string fw;
	r.scene.poll();
	if(r.scene.iceFailed(fw))
	{
		why = "failed before the bound: " + fw;
		return false;
	}
	Sleep(300);
	r.scene.poll();
	if(!r.scene.iceFailed(fw) || fw.find("no ice tap section within 200 ms") == std::string::npos)
	{
		why = "after the bound: '" + fw + "'";
		return false;
	}
	why.clear();
	return true;
}

// ---- the bundle members (issue 4308): record forms, parse, file sources, play into the scene -----------------------------
namespace sb = srvrender::scenebundle;

std::vector<uint8_t> ReadTex(ID3D11ShaderResourceView* v, uint32_t bpp)
{
	std::vector<uint8_t> out;
	ID3D11Resource* r = nullptr;
	v->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t);
	r->Release();
	D3D11_TEXTURE2D_DESC d;
	t->GetDesc(&d);
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* s = nullptr;
	if(SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &s)))
	{
		g_ctx->CopyResource(s, t);
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &m)))
		{
			out.resize((size_t)d.Width * d.Height * bpp);
			for(uint32_t y = 0; y < d.Height; ++y)
				memcpy(&out[(size_t)y * d.Width * bpp], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)d.Width * bpp);
			g_ctx->Unmap(s, 0);
		}
		s->Release();
	}
	t->Release();
	return out;
}

void SetSha(sb::ShadowRecord& r, const std::string& hex)
{
	for(int i = 0; i < 32; ++i)
		r.texelSha256[i] = (uint8_t)std::stoi(hex.substr(2 * i, 2), nullptr, 16);
}

sb::ShadowRecord* RecAt(std::vector<uint8_t>& b, size_t k)
{
	return (sb::ShadowRecord*)(b.data() + 20 + k * sizeof(sb::ShadowRecord));
}

// shadow.bin (q3 map, q1 float map, q0, nothing; the ice drawn in two frames), two .shtx files and one .ice file: encoded,
// parsed back equal byte for byte; every parser refuses a damaged copy (one per rule); the file sources hand the scene each
// frame's triple (the map's texels equal the recorded ones) and the mesh; played into a scene, each frame binds what was
// recorded and the recorded ice mesh is drawn.
bool CaseBundleRoundTrip(std::string& why)
{
	std::vector<uint16_t> r16(16);
	for(int i = 0; i < 16; ++i)
		r16[i] = (uint16_t)(i * 4000);
	std::vector<uint16_t> f16(64, 0);
	for(int i = 0; i < 16; ++i)
		f16[i * 4] = (uint16_t)(0x3800 + i);
	std::vector<uint8_t> t1, t2;
	sb::EncodeShadowTexels(4, 4, 56, r16.data(), r16.size() * 2, &t1);
	sb::EncodeShadowTexels(4, 4, 10, f16.data(), f16.size() * 2, &t2);
	const std::string h1 = sb::Sha256Hex(t1.data(), t1.size()), h2 = sb::Sha256Hex(t2.data(), t2.size());
	std::shared_ptr<c5r::ice::Mesh> mesh = QuadMesh();
	std::vector<uint8_t> iceFile;
	sb::EncodeIceMesh(*mesh, 7, 3, &iceFile);
	std::vector<sb::ShadowRecord> recs(4);
	memset(recs.data(), 0, recs.size() * sizeof(sb::ShadowRecord));
	for(int k = 0; k < 4; ++k)
	{
		sb::ShadowRecord& r = recs[k];
		r.frameId = 100 + k;
		if(k < 3)
		{
			r.numViews = 2;
			LiveMatrix(r.matrices[0]);
			Moved(r.matrices[0], kWrap, r.matrices[1]);
			r.generation = 50 + k;
			r.presentCount = 900 + k;
		}
	}
	recs[0].flags = sb::kShadowBound | sb::kIceDrawn;
	recs[0].quality = 3;
	recs[0].dxgiFormat = 56;
	recs[0].width = recs[0].height = 4;
	recs[0].strength = 1.0f;
	SetSha(recs[0], h1);
	recs[0].iceGeneration = 7;
	recs[0].iceLoadSeq = 3;
	recs[1].flags = sb::kShadowBound | sb::kIceDrawn;
	recs[1].quality = 1;
	recs[1].dxgiFormat = 10;
	recs[1].width = recs[1].height = 4;
	recs[1].strength = 0.25f;
	SetSha(recs[1], h2);
	recs[1].iceGeneration = 7;
	recs[1].iceLoadSeq = 3;
	recs[2].flags = sb::kShadowQ0;
	std::vector<uint8_t> shadowBin;
	sb::EncodeStreamHead(sb::MemberKind::Shadow, &shadowBin);
	shadowBin.insert(shadowBin.end(), (const uint8_t*)recs.data(), (const uint8_t*)recs.data() + recs.size() * sizeof(sb::ShadowRecord));
	std::vector<sb::ShadowRecord> back;
	sb::CheckResult cr;
	if(!sb::ParseShadow(shadowBin, &back, &cr).ok() || back.size() != 4 || memcmp(back.data(), recs.data(), 4 * sizeof(sb::ShadowRecord)))
	{
		why = "shadow.bin round trip: " + cr.line();
		return false;
	}
	sb::ShadowTexFile tf;
	if(!sb::ParseShadowTexels(t2, &tf, &cr).ok() || tf.width != 4 || tf.dxgiFormat != 10 || tf.bytesPerTexel != 8 ||
	   memcmp(t2.data() + tf.texelOffset, f16.data(), 128))
	{
		why = "shtx round trip: " + cr.line();
		return false;
	}
	c5r::ice::Mesh mb;
	uint32_t gen = 0, load = 0;
	if(!sb::ParseIceMesh(iceFile, &mb, &gen, &load, &cr).ok() || gen != 7 || load != 3 || mb.vb != mesh->vb || mb.ib != mesh->ib ||
	   mb.chunks.size() != 1 || memcmp(&mb.chunks[0], &mesh->chunks[0], sizeof(c5r::ice::Chunk)) ||
	   memcmp(mb.fields.raw, mesh->fields.raw, c5r::ice::kFieldsSize) || !mb.fields.valid || sb::IceMeshPath(7) != "ice/gen0000000007.ice")
	{
		why = "ice round trip: " + cr.line();
		return false;
	}
	// the recorder's writer takes shadow.bin as a stream member (appendStart + append, as SceneRecorder::doFinish; a live
	// recording found it refused once) and the three new kinds as plain members
	{
		wchar_t tmp[MAX_PATH];
		GetTempPathW(MAX_PATH, tmp);
		const std::wstring dir = std::wstring(tmp) + L"c5_4308_writer_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount());
		sb::Writer w;
		c5r::Status ws = w.open(dir);
		if(ws.ok())
			ws = w.appendStart("shadow.bin", sb::MemberKind::Shadow);
		if(ws.ok())
			ws = w.append("shadow.bin", recs.data(), recs.size() * sizeof(sb::ShadowRecord));
		if(ws.ok())
			ws = w.addMember(sb::ShadowTexPath(h1), sb::MemberKind::ShadowTexels, t1.data(), t1.size());
		if(ws.ok())
			ws = w.addMember(sb::IceMeshPath(7), sb::MemberKind::IceMesh, iceFile.data(), iceFile.size());
		w.markIncomplete("test: the writer's path only");
		sb::Manifest wm;
		w.finish(wm);
		std::wstring cmd = L"cmd /c rd /s /q \"" + dir + L"\" >nul 2>nul";
		_wsystem(cmd.c_str());
		if(!ws.ok())
		{
			why = "the writer refused a 4308 member: " + ws.str();
			return false;
		}
	}
	struct Bad
	{
		const char* what;
		std::vector<uint8_t> bytes;
		int kind;   // 0 shadow.bin, 1 shtx, 2 ice
		sb::Refusal want;
	};
	std::vector<Bad> bads;
	auto add = [&](const char* what, const std::vector<uint8_t>& base, int kind, sb::Refusal want) -> std::vector<uint8_t>& {
		bads.push_back(Bad{what, base, kind, want});
		return bads.back().bytes;
	};
	RecAt(add("flags above bit 2", shadowBin, 0, sb::Refusal::BadRecord), 3)->flags = 8;
	RecAt(add("bound and q0", shadowBin, 0, sb::Refusal::BadRecord), 0)->flags |= sb::kShadowQ0;
	RecAt(add("frame id not increasing", shadowBin, 0, sb::Refusal::BadRecord), 1)->frameId = 100;
	RecAt(add("strength 1.5", shadowBin, 0, sb::Refusal::BadRecord), 0)->strength = 1.5f;
	RecAt(add("NaN matrix", shadowBin, 0, sb::Refusal::BadRecord), 0)->matrices[1][3] = NAN;
	memset(RecAt(add("bound without a hash", shadowBin, 0, sb::Refusal::BadRecord), 0)->texelSha256, 0, 32);
	RecAt(add("bound format 28", shadowBin, 0, sb::Refusal::BadRecord), 0)->dxgiFormat = 28;
	RecAt(add("ice drawn, generation 0", shadowBin, 0, sb::Refusal::BadRecord), 0)->iceGeneration = 0;
	RecAt(add("q0 with a strength", shadowBin, 0, sb::Refusal::BadRecord), 2)->strength = 0.5f;
	add("a partial record", shadowBin, 0, sb::Refusal::Truncated).pop_back();
	{
		std::vector<uint8_t>& b = add("record size 343", shadowBin, 0, sb::Refusal::WrongSize);
		b[16] = 87;
		b[17] = 1;
	}
	add("bad magic", shadowBin, 0, sb::Refusal::BadMagic)[0] = 'X';
	add("shtx format 28", t1, 1, sb::Refusal::BadRecord)[24] = 28;
	add("shtx short", t1, 1, sb::Refusal::Truncated).pop_back();
	add("shtx long", t1, 1, sb::Refusal::WrongSize).push_back(0);
	add("ice fieldsBytes", iceFile, 2, sb::Refusal::BadRecord)[16 + 20] = 0x9F;
	add("ice chunk past the IB", iceFile, 2, sb::Refusal::BadRecord)[16 + 28 + c5r::ice::kFieldsSize + 16] = 5;
	add("ice short", iceFile, 2, sb::Refusal::Truncated).pop_back();
	add("ice vb not whole vertices", iceFile, 2, sb::Refusal::BadRecord)[16 + 8] = 31;
	for(Bad& b : bads)
	{
		sb::CheckResult r;
		const c5r::Status st = b.kind == 0 ? sb::ParseShadow(b.bytes, nullptr, &r)
		                                   : b.kind == 1 ? sb::ParseShadowTexels(b.bytes, nullptr, &r)
		                                                 : sb::ParseIceMesh(b.bytes, nullptr, nullptr, nullptr, &r);
		if(st.ok() || r.refusal != b.want)
		{
			why = std::string("not refused as wanted: ") + b.what + " (" + r.line() + ")";
			return false;
		}
	}
	std::map<std::string, std::vector<uint8_t>> files;
	files[h1] = t1;
	files[h2] = t2;
	std::map<uint32_t, std::shared_ptr<const c5r::ice::Mesh>> meshes;
	meshes[7] = std::make_shared<c5r::ice::Mesh>(mb);
	std::string w;
	{
		std::map<std::string, std::vector<uint8_t>> some;
		some[h1] = t1;
		if(MakeFileShadowSource(g_dev, recs, some, nullptr, &w) || w.find("missing") == std::string::npos)
		{
			why = "a missing shadow map must be refused";
			return false;
		}
		std::map<uint32_t, std::shared_ptr<const c5r::ice::Mesh>> none;
		if(MakeFileIceSource(recs, none, nullptr, &w) || w.find("missing") == std::string::npos)
		{
			why = "a missing ice mesh must be refused";
			return false;
		}
	}
	FileSeamGControl* shCtl = nullptr;
	FileSeamGControl* iceCtl = nullptr;
	std::unique_ptr<ShadowSource> ss = MakeFileShadowSource(g_dev, recs, files, &shCtl, &w);
	std::unique_ptr<IceSource> is = MakeFileIceSource(recs, meshes, &iceCtl, &w);
	if(!ss || !is)
	{
		why = "file sources: " + w;
		return false;
	}
	Rig r;
	RigOpts ro;
	ro.install = true;
	ro.ice = 1;
	ro.fakeShadow = false;
	if(!r.start(ro, why))
		return false;
	r.scene.setShadowSource(std::move(ss));
	r.scene.setIceSource(std::move(is));
	for(uint32_t k = 0; k < 4; ++k)
	{
		shCtl->seekFrame(k);
		iceCtl->seekFrame(k);
		c5r::terrain::FrameStats fs;
		bool ok = false;
		if(!r.frame(fs, ok) || !ok)
		{
			why = "frame " + std::to_string(k) + " not drawn";
			return false;
		}
		ShadowFrame got;
		const bool have = r.scene.lastShadowFrame(got);
		const bool wantHave = (recs[k].flags & (sb::kShadowBound | sb::kShadowQ0)) != 0, wantMap = (recs[k].flags & sb::kShadowBound) != 0;
		if(have != wantHave || (r.scene.lastShadow().srv != nullptr) != wantMap)
		{
			why = "frame " + std::to_string(k) + ": bound " + std::to_string(have) + " map " + std::to_string(r.scene.lastShadow().srv != nullptr);
			return false;
		}
		if(have && (got.generation != recs[k].generation || got.quality != recs[k].quality || got.strength != recs[k].strength ||
		            got.numViews != recs[k].numViews || memcmp(got.matrices, recs[k].matrices, sizeof(got.matrices)) ||
		            got.presentCount != recs[k].presentCount))
		{
			why = "frame " + std::to_string(k) + ": the bound triple is not the recorded one";
			return false;
		}
		if(wantMap)
		{
			const std::vector<uint8_t> px = ReadTex(got.srv, k == 0 ? 2 : 8);
			const std::vector<uint8_t>& f = k == 0 ? t1 : t2;
			if(px.size() + 32 != f.size() || memcmp(px.data(), f.data() + 32, px.size()))
			{
				why = "frame " + std::to_string(k) + ": the map's texels differ from the recorded ones";
				return false;
			}
		}
		if(!r.scene.lastIceDrawn() || r.scene.iceGeneration() != 7 || r.scene.iceLoadSeq() != 3)
		{
			why = "frame " + std::to_string(k) + ": the recorded ice mesh not drawn";
			return false;
		}
	}
	why.clear();
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
	bool needsGame;
};
const Case kCases[] = {
	{"shadow_copies", CaseShadowCopies, false},
	{"shadow_none_is_todays_picture", CaseNoShadowIsTodaysPicture, false},
	{"shadow_qualities_bound", CaseQualitiesBound, false},
	{"shadow_refused_keeps_last_good", CaseRefusedKeepsLastGood, false},
	{"shadow_failed_is_fatal_once", CaseShadowFailedIsFatalOnce, false},
	{"shadow_off_loud_once", CaseShadowOffLoudOnce, false},
	{"shadow_absent_bound", CaseAbsentBound, false},
	{"options", CaseShadowOption, false},
	{"ice_drawn", CaseIceDrawn, true},
	{"ice_refused_keeps_last_good", CaseIceRefusedKeepsLastGood, true},
	{"ice_install_fatal_once", CaseIceInstallFatalOnce, true},
	{"ice_absent_bound", CaseIceAbsentBound, true},
	{"bundle_round_trip", CaseBundleRoundTrip, true},
};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	const char* e = getenv("C5R_EXE");
	if(!e || !*e)
	{
		printf("test_scene_shadow: C5R_EXE not set (the terrain and ice programs come from the EXE)\n");
		return 99;
	}
	g_exe.assign(e, e + strlen(e));
	const char* gd = getenv("C5R_GAME_DIR");
	if(gd && *gd)
	{
		const int n = MultiByteToWideChar(CP_UTF8, 0, gd, -1, nullptr, 0);
		g_game.resize(n > 0 ? n - 1 : 0);
		if(n > 1)
			MultiByteToWideChar(CP_UTF8, 0, gd, -1, &g_game[0], n);
	}
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("test_scene_shadow: no WARP device\n");
		return 99;
	}
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			if(!strcmp(argv[i], c.name) || (!strcmp(argv[i], "shadow") && !c.needsGame) || (!strcmp(argv[i], "ice") && c.needsGame))
				want = true;
		if(!want)
			continue;
		if(c.needsGame && g_game.empty())
		{
			printf("test_scene_shadow: C5R_GAME_DIR not set (the ice's textures come from the install): case %s cannot run\n", c.name);
			return 99;
		}
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
		printf("test_scene_shadow: no case matches\n");
		return 2;
	}
	g_ctx->Release();
	g_dev->Release();
	return fails;
}
