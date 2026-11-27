// test_fow_scene.cpp - stand-in tests of the fog of war in server.exe's terrain scene (server/src/render/srv_fow_scene.cpp,
// epic 0168, task 3954). Written 2026-11-14 for this repository (GPL-3.0-or-later).
//
// Each case builds its own main section (a fake camera block and a height-field tap, as 0167's test_scene_overlay.cpp
// does), starts a Scene on a WARP device with the EXE's terrain programs (C5R_EXE, read-only), replaces its overlay
// source by a fake that hands out a pair, and replaces the fog's seams before init(): FakeFow for c5r::fow (a mask
// texture of its own; "clouds" = the whole target cleared to red when any plot of its input is unexplored, so a cloud
// pass that runs before the terrain's clear leaves no red) and FakeTable for the feed's plot table. Batches are handed
// to Scene::fowFeed as the host's loop does. Issue 4305 (epic 0169) adds the tile_styles cases: render_tiles=bake with
// the stand-in baker on a fake FeedSource (the art types to the bake's setMap and the renderer's styles). Cases install_failure_fatal_once (a missing EXE path) and real_modules (the
// EXE's FOW programs and the install's cloud files, C5R_GAME_DIR) run the real c5r::fow modules of 3952 / 3955.
// Output: one line per case, `CASE <name> PASS|FAIL <detail>`; exit code = the number of failed cases, 2 on usage, 99
// without a WARP device or C5R_EXE.
//
//   test_fow_scene.exe [case ...]      (no argument: every case)
#include "srv_render_scene.h"
#include "srv_render_host.h"
#include "c5r/heightfield/heightfield.h"
#include "srv_options.h"
#include "srv_residency_source.h"   // issue 4305
#include "srv_scene_sources.h"
#include "srv_scene_bundle.h"   // ticket 0403: Sha256Hex for the particles_sequence lines
#include "c5r/bake/heightpage.h"
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
namespace cf = c5r::fow;

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
std::string S(long long v) { return std::to_string(v); }

const uint32_t kW = 160, kH = 120;     // camera and target size
const float kWrap = 255.0f;            // 64 W - 1 for the 4-plot field below
const uint32_t kPlots = 4;             // the field is 4 x 4 plots

ID3D11ShaderResourceView* Texture(uint32_t rgba)
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

// ---- the fakes -------------------------------------------------------------------------------------------------------
struct FakeSource : OverlaySource
{
	OverlayTapReader::State st = OverlayTapReader::State::Active;
	bool give = true;
	OverlayFrame pair;
	bool acquire(OverlayFrame& out) override
	{
		out = OverlayFrame();
		if(!give)
			return false;
		out = pair;
		return true;
	}
	OverlayTapReader::State state() const override { return st; }
	uint32_t failReason() const override { return 0; }
	std::string why() const override { return ""; }
};

struct FakeFow : FowModules
{
	c5r::Status maskInit, cloudInit, buildSt, cloudSt;
	int builds = 0, clouds = 0, releases = 0;
	uint32_t size = 0;
	std::vector<cf::PlotState> lastStates;
	cf::MaskInput lastIn;
	cf::CloudWorld lastWorld;
	ID3D11DepthStencilView* depthGiven = nullptr;
	float lastAnim[4] = {};
	ID3D11ShaderResourceView* maskSrv = nullptr;
	ID3D11ShaderResourceView* cloudSrv = nullptr;
	~FakeFow() override { release(); }
	c5r::Status initMask(ID3D11Device*, const std::wstring&, uint32_t sz) override
	{
		size = sz;
		if(!maskSrv)
			maskSrv = Texture(0xFFFF00FFu);   // (1, 0, 1, 1): visible everywhere
		return maskInit;
	}
	c5r::Status initClouds(ID3D11Device*, const std::wstring&, const std::wstring&) override
	{
		if(!cloudSrv)
			cloudSrv = Texture(0xFF808080u);
		return cloudInit;
	}
	c5r::Status buildMask(ID3D11DeviceContext*, const cf::MaskInput& in, cf::MaskStats*) override
	{
		builds++;
		lastIn = in;
		lastStates.assign(in.map.states, in.map.states + (size_t)in.map.width * in.map.height);
		return buildSt;
	}
	c5r::Status drawClouds(ID3D11DeviceContext* ctx, const cf::CloudInput& in, cf::CloudStats*) override
	{
		clouds++;
		lastWorld = in.world;
		// as FOW CULL: depth 0 into the depth the scene gives the pass (which must not be the scene's own)
		if(in.depth)
			ctx->ClearDepthStencilView(in.depth, D3D11_CLEAR_DEPTH, 0.0f, 0);
		depthGiven = in.depth;
		memcpy(lastAnim, in.fowAnim, sizeof(lastAnim));
		bool any = false;
		for(size_t i = 0; i < (size_t)in.geometry.map.width * in.geometry.map.height; ++i)
			any = any || in.geometry.map.states[i] == cf::PlotState::Unexplored;
		if(any && cloudSt.ok())
		{
			const float red[4] = {1, 0, 0, 1};
			ctx->ClearRenderTargetView(in.colour, red);
		}
		return cloudSt;
	}
	ID3D11ShaderResourceView* mask() const override { return maskSrv; }
	ID3D11ShaderResourceView* cloudBase() const override { return cloudSrv; }
	// ticket 0340: the particles (default: not provided, as the fakes before 0340)
	c5r::Status partInit = c5r::Status::fail(c5r::Err::Unsupported, "fake: no particles");
	int partDraws = 0, partResets = 0;
	c5r::Status initParticles(ID3D11Device*, const std::wstring&, const std::wstring&) override { return partInit; }
	c5r::Status resetParticles(float, float, uint32_t) override
	{
		partResets++;
		return c5r::Status::Ok();
	}
	c5r::Status drawParticles(ID3D11DeviceContext*, const cf::ParticleInput&, cf::ParticleStats*) override
	{
		partDraws++;
		return c5r::Status::Ok();
	}
	void release() override
	{
		releases++;
		SafeRelease(maskSrv);
		SafeRelease(cloudSrv);
	}
};

struct FakeTable : FowPlotTable
{
	std::vector<SrvFeed::PlotRecord> rec;
	uint32_t gen = 0, w = 0, h = 0;
	int copies = 0;
	void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& ww, uint32_t& hh) override
	{
		copies++;
		out = rec;
		g = gen;
		ww = w;
		hh = h;
	}
	// a new snapshot: every plot's fow bits = bits[i] (or bits[0] for all)
	void snapshot(const std::vector<uint8_t>& bits, uint32_t width = kPlots, uint32_t height = kPlots)
	{
		w = width;
		h = height;
		rec.assign((size_t)w * h, SrvFeed::PlotRecord());
		for(size_t i = 0; i < rec.size(); ++i)
			rec[i].fow = bits.size() == 1 ? bits[0] : bits[i];
		gen++;
	}
};

// ---- the main section, camera and field (as test_scene_overlay.cpp) --------------------------------------------------
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
	g.mapW = kPlots;
	g.mapH = kPlots + 4;   // the grid: the DLL's height + 4 margin rows (heightfield.h)
	g.samplesX = 256;
	g.samplesY = 512;
	g.wrapX = true;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = 128.0f;
	g.oy = 256.0f * 0.866f;
	g.waterZ = 10.0f;
	g.h.assign((size_t)g.samplesX * g.samplesY, (uint16_t)(20 << 8));
	return g;
}

// The camera block: a camera at (cx, 0, 120) looking straight down, numViews 1, the field's grid constants and the
// world box of 3951's Tiny map (x 0..3583: k = 7 / 3583, fog-of-war.md 2.5).
void WriteCamera(Main& m, float cx, int64_t frame, uint32_t gridSeq = 1)
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
	c->gridSeq = gridSeq;   // ticket 0691 (cand4): another load's grid with gridSeq > 1
	c->gridValid = 1;
	c->wrapX = 1;
	c->wrapPeriod = kWrap;
	c->aabbMin[0] = 0.0f;
	c->aabbMax[0] = 3583.0f;
	c->sx = 1.0f;
	c->sy = 0.866f;
	c->heightScale = 0.2f;
	c->ox = 128.0f;
	c->oy = 256.0f * 0.866f;
	c->samplesX = 256;
	c->samplesY = 512;
	MemoryBarrier();
	c->seq = (s | 1) + 1;
}

bool MakeMain(Main& m, std::string& why)
{
	wchar_t nm[96];
	swprintf_s(nm, L"Local\\VPFowSceneTest.%u.%d", GetCurrentProcessId(), ++g_serial);
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
	swprintf_s(tn, L"Local\\VPFowSceneTestTap.%u.%d", GetCurrentProcessId(), g_serial);
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
	t->mapW = kPlots;
	t->mapH = kPlots + 4;
	t->samplesX = 256;
	t->samplesY = 512;
	t->flags = 1;
	t->payloadBytes = img.size();
	t->copySeq = 1;
	wcscpy_s(t->sectionName, tn);
	MemoryBarrier();
	t->seq = 2;
	return true;
}

// An OverlayProj as the EXE builds it (overlay-system.md 4)
void ExeProj(float m[16], float wx, float wy, float x0, float y0)
{
	const float sx = 2.0f / wx, sy = 2.0f / wy;
	const float v[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -0.5f, 0, -1.0f - x0 * sx, -1.0f - y0 * sy, 0.5f, 1};
	memcpy(m, v, sizeof(v));
}

struct RigOpt
{
	int fow = 1;
	int overlay = 1;
	bool fakeFow = true;      // false: the real c5r::fow modules (the stubs until 3952 / 3955)
	bool fakeSource = true;   // false: the tap's reader of the tree (no tap block in the test section: ABSENT)
	bool givePair = true;
	double waitMs = 300;
	std::vector<uint8_t> bits = {3};   // the first snapshot (empty: no table)
	// issue 4305: the tile source (none, or bake with the stand-in baker on this feed) and the residency's art rule
	int tiles = SceneOptions::kTilesNone;
	FeedSource* feed = nullptr;
	bool followArt = false;
};

// A scene on the WARP device with the field applied, its overlay source and fog seams replaced by fakes.
struct Rig
{
	Main m;
	Scene scene;
	FakeSource* src = nullptr;
	FakeFow* fow = nullptr;
	FakeTable* table = nullptr;
	ID3D11ShaderResourceView* ovlTex = nullptr;
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* ds = nullptr;          // the scene's depth, as the product's sinks give it (the cloud pass needs it)
	ID3D11DepthStencilView* dsv = nullptr;
	int64_t frameNo = 100;
	uint32_t gridSeq = 1;   // ticket 0691 (cand4): the camera block's grid seq (a reload: 2)
	c5r::terrain::FrameStats lastFs;   // the newest draw's
	uint32_t seq = 0;
	~Rig()
	{
		scene.releaseDevice();
		scene.detach();
		SafeRelease(dsv);
		SafeRelease(ds);
		SafeRelease(rtv);
		SafeRelease(rt);
		SafeRelease(ovlTex);
	}
	bool start(const RigOpt& ro, std::string& why)
	{
		if(!MakeMain(m, why))
			return false;
		if(!scene.attach(m.name))
		{
			why = "attach";
			return false;
		}
		table = new FakeTable();
		if(!ro.bits.empty())
			table->snapshot(ro.bits);
		scene.fow().setTable(std::unique_ptr<FowPlotTable>(table));
		if(ro.fakeFow)
		{
			fow = new FakeFow();
			scene.fow().setModules(std::unique_ptr<FowModules>(fow));
		}
		SceneOptions o;
		o.exe = g_exe;
		o.tiles = ro.tiles;
		o.bake.pipeline = false;          // issue 4305: the stand-in baker (with render_tiles=bake)
		o.bake.feed = ro.feed;
		o.bake.followArtChanges = ro.followArt;
		o.bake.res.radius = -1;
		o.water = false;
		o.color = ro.tiles == SceneOptions::kTilesBake ? c5r::terrain::ColorSource::Caches : c5r::terrain::ColorSource::Constant;
		o.fallback = c5r::terrain::ColorSource::Constant;
		o.overlay = ro.overlay;
		o.fow = ro.fow;
		o.fowLevel = 3;
		o.fowWaitMs = ro.waitMs;
		o.waterFreeze = true;   // FOWAnim's T held at 0: deterministic pictures
		const c5r::Status st = scene.init(g_dev, g_ctx, o);
		if(!st.ok())
		{
			why = "init: " + st.str();
			return false;
		}
		if(ro.fakeSource && ro.overlay)
		{
			src = new FakeSource();
			ovlTex = Texture(0);
			src->give = ro.givePair;
			src->pair.srv = ovlTex;
			src->pair.numViews = 1;
			src->pair.generation = 1;
			src->pair.slot = 0;
			// ticket 0674: the EXE's window for the rig's cameras (x -43..47, y -32..32 at z = 0): [-128, 64] x [-128, 64], so the
			// view stays inside the pair's window (was [-32, 32]^2, which the view leaves: every frame a camera window now)
			ExeProj(src->pair.overlayProj[0], 192, 192, -128, -128);
			scene.setOverlaySource(std::unique_ptr<OverlaySource>(src));
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
	// poll, decide, draw; returns decide()'s answer, sets ok to the draw's result
	bool frame(bool& ok, bool newCamera = true)
	{
		if(newCamera)
		{
			const int64_t fr = frameNo++;
			WriteCamera(m, (float)(fr % 5), fr, gridSeq);
		}
		scene.poll();
		SceneFrame f;
		if(!scene.decide(f))
		{
			ok = true;
			return false;
		}
		c5r::terrain::FrameStats fs;
		ok = scene.draw(g_ctx, rtv, dsv, kW, kH, f, fs).ok();
		lastFs = fs;
		return true;
	}
	bool drawn(bool newCamera = true)
	{
		bool ok = false;
		return frame(ok, newCamera) && ok;
	}
	// ticket 0491: the install's wait ends after installWaitMs only once the EXE presented 30 map frames (or at the slow-load
	// bound): a new camera (an EXE Present) every 20 ms until the fog failed or `limit` frames; false when one was drawn
	bool drawUntilFailed(int limit = 300)
	{
		std::string t;
		for(int i = 0; i < limit && !scene.fow().failed(t); ++i)
		{
			if(drawn())
				return false;
			Sleep(20);
		}
		return true;
	}
	// a feed batch as the host drains it
	void batch(std::vector<SrvFeed::Event> evs, std::vector<uint32_t> changed = {}, uint64_t dropped = 0)
	{
		SrvFeed::Batch b;
		for(auto& e : evs)
			if(!e.seq)
				e.seq = ++seq;
			else
				seq = e.seq;
		b.events = evs;
		b.changedPlots = changed;
		b.snapshotGen = table->gen;
		b.width = table->w;
		b.height = table->h;
		b.dropped = dropped;
		scene.fowFeed(b);
	}
	// texels of the scene's depth (D24 of D24S8) that are exactly 0 (FOW CULL's value; the terrain never writes 0)
	int depthZeros()
	{
		D3D11_TEXTURE2D_DESC d;
		ds->GetDesc(&d);
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* s = nullptr;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
			return -1;
		g_ctx->CopyResource(s, ds);
		int n = 0;
		D3D11_MAPPED_SUBRESOURCE map;
		if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &map)))
		{
			for(uint32_t y = 0; y < kH; ++y)
				for(uint32_t x = 0; x < kW; ++x)
					n += ((((const uint32_t*)((const uint8_t*)map.pData + (size_t)y * map.RowPitch))[x] & 0xFFFFFFu) == 0) ? 1 : 0;
			g_ctx->Unmap(s, 0);
		}
		else
			n = -1;
		s->Release();
		return n;
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

SrvFeed::Event Ev(uint16_t kind, int32_t plot = -1, int32_t a0 = 0, int32_t a1 = 0, int32_t a2 = 0, int32_t a3 = 0)
{
	SrvFeed::Event e = {};
	e.kind = kind;
	e.plot = plot;
	e.turn = 1;
	e.a[0] = a0;
	e.a[1] = a1;
	e.a[2] = a2;
	e.a[3] = a3;
	return e;
}

uint32_t RedPixels(const std::vector<uint8_t>& px)
{
	uint32_t n = 0;
	for(size_t i = 0; i + 3 < px.size(); i += 4)
		n += (px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 0 && px[i + 3] == 255) ? 1 : 0;
	return n;
}

int g_fatalCalls = 0, g_fatalCode = 0;
std::string g_fatalWhy;
void FatalHook(int code, const char* why)
{
	g_fatalCalls++;
	g_fatalCode = code;
	g_fatalWhy = why ? why : "";
}

#define CHECK(cond, text)    \
	do                       \
	{                        \
		if(!(cond))          \
		{                    \
			why = (text);    \
			return false;    \
		}                    \
	} while(0)

// ---- cases -----------------------------------------------------------------------------------------------------------

// Every plot visible: the picture of render_fow=1 is render_fow=0's bit for bit (the clouds draw nothing, the mask is
// bound to the water and handed to the terrain inputs, FOWAnim from the world box), and no LOUD line.
bool CaseAllVisible(std::string& why)
{
	std::vector<uint8_t> px[2];
	for(int k = 0; k < 2; ++k)
	{
		Rig r;
		RigOpt ro;
		ro.fow = k;
		if(!r.start(ro, why))
			return false;
		CHECK(r.drawn(), "fow=" + S(k) + ": no frame drawn");
		px[k] = r.pixels();
		CHECK(r.lastFs.fowOn == (k == 1), "the terrain's fog (3953's Options::fow) " + std::string(r.lastFs.fowOn ? "on" : "off") +
		                                      " at render_fow=" + S(k));
		if(k == 1)
		{
			FowScene& f = r.scene.fow();
			CHECK(f.state() == FowScene::kActive, std::string("state ") + FowScene::StateName(f.state()));
			CHECK(r.fow->builds == 1 && r.fow->clouds == 1 && f.masksBuilt == 1 && f.cloudDraws == 1 && f.framesWith == 1,
			      "builds " + S(r.fow->builds) + " clouds " + S(r.fow->clouds));
			CHECK(r.fow->size == 1024, "mask size " + S(r.fow->size) + " at FOWLevel 3");
			CHECK(WaterSceneFowMask(&r.scene.water()) == r.fow->maskSrv, "the water's t10 is not the mask");
			// the cloud pass gets a copy of the scene's depth: the fake's depth 0 (as FOW CULL's) stays out of the scene's
			// depth, which the composite hands to the EXE's models (live 2026-11-14 23:31: the zeros hid the models)
			const int zeros = r.depthZeros();
			CHECK(r.fow->depthGiven && r.fow->depthGiven != r.dsv && zeros == 0,
			      "the cloud pass wrote into the scene's depth (" + S(zeros) + " texels 0)");
			const FowTerrainInputs& t = f.terrainInputs();
			CHECK(t.on && t.mask == r.fow->maskSrv && t.cloudBase == r.fow->cloudSrv, "terrain inputs");
			// the world from the field (worldFromGrid): 256 samples x sx 1 -> width 255: k = floor(0.51) / 255 = 0; the
			// formula on 3951's Tiny (W 3583): 7 / 3583 = 1 / 511.857
			float a[4];
			cf::fowAnim(3583.0f, 0.0, a);
			CHECK(a[0] == 7.0f / 3583.0f && t.fowAnim[0] == 0.0f && t.fowAnim[1] == 0.0f && t.fowAnim[2] == 0.0f && t.fowAnim[3] == 0.0f,
			      "FOWAnim " + std::to_string(t.fowAnim[0]) + " " + std::to_string(t.fowAnim[2]));
			CHECK(r.fow->lastIn.numViews == 1 && memcmp(r.fow->lastIn.views[0].overlayProj, r.src->pair.overlayProj[0], 64) == 0,
			      "the mask's view is not the pair's OverlayProj");
			const cf::MapInput& mi = r.fow->lastIn.map;
			CHECK(mi.width == kPlots && mi.height == kPlots && mi.wrapX && mi.worldWidth == 255.0f && mi.stateCount == kPlots * kPlots &&
			          fabsf(mi.worldHeight - 511.0f * 0.866f) < 1e-3f && mi.hexToWorld[0] == 64.0f,
			      "map input: world " + std::to_string(mi.worldWidth) + " x " + std::to_string(mi.worldHeight) + ", M_h2w[0] " +
			          std::to_string(mi.hexToWorld[0]));
			// the view's camera matrix (plot range by stock's rule) and the cloud world from the same field
			CHECK(r.fow->lastIn.views[0].viewProj[0] != 0.0f && r.fow->lastIn.views[0].x1 <= r.fow->lastIn.views[0].x0, "viewProj / range");
			const cf::CloudWorld& w = r.fow->lastWorld;
			CHECK(w.minX == -127.5f && w.maxX == 127.5f && w.samplesX == 256 && w.samplesY == 512 && w.hexToWorld[0] == 64.0f &&
			          w.sx == mi.gridScale[0] && w.ox == mi.gridOrigin[0],
			      "cloud world " + std::to_string(w.minX) + ".." + std::to_string(w.maxX));
			CHECK(CountLog("nominalPlacement used") == 1 && f.nominalFrames == 1, "the nominal placement not noted once");
			// a still camera, nothing new: no redraw, no rebuild
			CHECK(!r.drawn(false) && r.fow->builds == 1, "a redraw without a change");
		}
	}
	CHECK(!px[0].empty() && px[0] == px[1], "render_fow=1 with every plot visible is not render_fow=0's picture");
	CHECK(RedPixels(px[1]) == 0, "clouds drawn over visible plots");
	CHECK(CountLog("LOUD") == 0, "a LOUD line");
	CHECK(CountLog("render_fow=0: no fog of war") == 1, "render_fow=0 not logged");
	return true;
}

// One plot per class: fow bits 3 / 2 -> visible, 1 -> seen, 0 -> unexplored; the cloud pass runs after the terrain
// (its red survives into the picture).
bool CaseOnePlotPerClass(std::string& why)
{
	Rig r;
	RigOpt ro;
	ro.bits.assign(kPlots * kPlots, 3);
	ro.bits[1] = 2;
	ro.bits[2] = 1;
	ro.bits[3] = 0;
	if(!r.start(ro, why))
		return false;
	CHECK(r.drawn(), "no frame drawn");
	const std::vector<cf::PlotState>& s = r.fow->lastStates;
	CHECK(s.size() == kPlots * kPlots, "states " + S((long long)s.size()));
	CHECK(s[0] == cf::PlotState::Visible && s[1] == cf::PlotState::Visible && s[2] == cf::PlotState::Seen &&
	          s[3] == cf::PlotState::Unexplored && s[4] == cf::PlotState::Visible,
	      "the mapping of the fow bits");
	const std::vector<uint8_t> px = r.pixels();
	CHECK(RedPixels(px) == kW * kH, "the clouds did not cover the frame after the terrain (red " + S(RedPixels(px)) + ")");
	return CountLog("LOUD") == 0 ? true : (why = "a LOUD line", false);
}

// A snapshot replaces the table (a fog change asks for a redraw with a still camera and rebuilds the mask); changed plots
// re-read it; an unchanged fog neither redraws nor rebuilds; a new table of another size holds frames until the field
// matches (a load), never fatal.
bool CaseSnapshot(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	CHECK(r.drawn(), "no first frame");
	const uint32_t g0 = r.scene.fow().fogGeneration();
	// a snapshot with plot 5 unexplored
	std::vector<uint8_t> bits(kPlots * kPlots, 3);
	bits[5] = 0;
	r.table->snapshot(bits);
	r.batch({Ev(SrvFeed::kSnapshot, -1, kPlots, kPlots, (int32_t)r.table->gen)});
	CHECK(r.scene.fow().fogGeneration() == g0 + 1, "the fog generation did not move");
	CHECK(r.drawn(false), "no redraw with a still camera after a fog change");
	CHECK(r.fow->builds == 2 && r.fow->lastStates[5] == cf::PlotState::Unexplored, "the mask was not rebuilt from the snapshot");
	CHECK(RedPixels(r.pixels()) == kW * kH, "no clouds for the unexplored plot");
	// changed plots: plot 5 revealed (fow event + the table patched)
	r.table->rec[5].fow = 1;
	r.batch({Ev(SrvFeed::kFowChanged, 5, 2, 0, 1, 1)}, {5});
	CHECK(r.scene.fow().states()[5] == cf::PlotState::Seen && r.scene.fow().fogGeneration() == g0 + 2, "changed plot not read");
	CHECK(r.drawn(false) && r.fow->builds == 3 && RedPixels(r.pixels()) == 0, "no redraw / rebuild after the plot changed");
	// changed plots without a fog change (an owner change): no redraw, no rebuild
	r.table->rec[6].owner = 2;
	r.batch({Ev(SrvFeed::kBorderChanged, 6, 2)}, {6});
	CHECK(r.scene.fow().fogGeneration() == g0 + 2 && !r.drawn(false) && r.fow->builds == 3, "a non-fog change redrew the fog");
	// a new load's table of another size: frames held (never fatal) until the field matches
	r.table->snapshot({3}, 8, 8);
	r.batch({Ev(SrvFeed::kSnapshot, -1, 8, 8, (int32_t)r.table->gen)});
	bool done = false;
	g_fatalCalls = 0;
	for(int i = 0; i < 3; ++i)
	{
		CHECK(!r.drawn(), "a frame drawn with a table of another size");
		FowFatalStep(r.scene, &FatalHook, done);
	}
	CHECK(!done && g_fatalCalls == 0 && r.scene.fow().framesHeld >= 3, "a size change in play was fatal or not held");
	CHECK(CountLog("the plot table is 8x8, the field's map 4x4 (grid 4x8)") == 1, "the hold not noted once");
	r.table->snapshot({3});
	r.batch({Ev(SrvFeed::kSnapshot, -1, kPlots, kPlots, (int32_t)r.table->gen)});
	CHECK(r.drawn() && CountLog("fog: drawing again after 3 held frames") == 1, "not drawing again after the table matched");
	return CountLog("LOUD") == 0 ? true : (why = "a LOUD line", false);
}

// Overflow, a sequence gap and dropped events re-read the table (LOUD, counted); a fog event outside the grid and fog
// bits above bit 1 are LOUD and counted; drawing goes on, never fatal.
bool CaseOverflow(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	CHECK(r.drawn(), "no first frame");
	bool done = false;
	g_fatalCalls = 0;
	const int64_t reads0 = r.scene.fow().rereads;
	// the table changed behind an overflow: plot 0 unexplored
	r.table->rec[0].fow = 0;
	r.batch({Ev(SrvFeed::kOverflow, -1, 12)});
	CHECK(r.scene.fow().overflows == 1 && r.scene.fow().rereads == reads0 + 1 && r.scene.fow().states()[0] == cf::PlotState::Unexplored,
	      "an overflow did not re-read the table");
	CHECK(CountLog("LOUD: fog: the feed overflowed (12 events dropped") == 1, "overflow not LOUD");
	CHECK(r.drawn(false), "no redraw after the overflow's re-read");
	FowFatalStep(r.scene, &FatalHook, done);
	// a sequence gap
	SrvFeed::Event e = Ev(SrvFeed::kYieldChanged, 3);
	e.seq = r.seq + 5;
	r.table->rec[0].fow = 3;
	r.batch({e});
	CHECK(r.scene.fow().seqGaps == 1 && r.scene.fow().states()[0] == cf::PlotState::Visible, "a gap did not re-read");
	CHECK(CountLog("LOUD: fog: feed sequence gap") == 1, "gap not LOUD");
	// dropped events (the batch's counter moved)
	const int64_t reads1 = r.scene.fow().rereads;
	r.batch({}, {}, 7);
	CHECK(r.scene.fow().rereads == reads1 + 1, "dropped events did not re-read");
	// a fog event outside the grid
	r.batch({Ev(SrvFeed::kFowChanged, -1, 2, 0, 9, 1)});
	CHECK(r.scene.fow().outsideEvents == 1 && CountLog("LOUD: fog: a fog event for plot (9, 1) outside the 4x4 grid") == 1,
	      "outside event");
	// a setAll event (x, y not a plot) is not a fault
	r.batch({Ev(SrvFeed::kFowChanged, -1, 2, 1, -1, -1)});
	CHECK(r.scene.fow().outsideEvents == 1, "a setAll event counted as outside");
	// fog bits above bit 1
	r.table->rec[9].fow = 0x81;
	r.batch({Ev(SrvFeed::kFowChanged, 9, 2, 0, 1, 2)}, {9});
	CHECK(r.scene.fow().badBits == 1 && r.scene.fow().states()[9] == cf::PlotState::Seen &&
	          CountLog("LOUD: fog: 1 plot(s) carry fog bits above bit 1 (first plot 9: 0x81)") == 1,
	      "bad bits");
	for(int i = 0; i < 3; ++i)
	{
		CHECK(r.drawn(), "drawing stopped after a fault in play");
		FowFatalStep(r.scene, &FatalHook, done);
	}
	CHECK(!done && g_fatalCalls == 0, "a fault in play was fatal");
	CHECK(CountLog("LOUD") == 4, "LOUD lines " + S(CountLog("LOUD")));
	char line[4096];
	r.scene.totalsLine(line, sizeof(line));
	CHECK(strstr(line, "seq gaps 1 overflows 1 outside events 1 bad bits 1") != nullptr, std::string("totals: ") + line);
	return true;
}

// Ticket 0691 (MC #11169): the hold report (FowScene::holdFrameId / holdReasons, which the host writes into vp_frame.h's
// VPFrameServer for the composite): set only while admit() holds a frame for inputs later EXE frames bring, with the frame
// id and the reason bits of the wait (kHoldInstall while installing), cleared by an admitted frame; a failed fog waits for
// nothing and reports no hold (the composite's hung route stays).
bool CaseHoldReport(std::string& why)
{
	const uint32_t inst = FowScene::kHoldInstall;
	{
		Rig r;
		RigOpt ro;
		ro.bits.clear();   // no plot table yet
		ro.givePair = false;
		ro.waitMs = 60000;
		if(!r.start(ro, why))
			return false;
		r.src->st = OverlayTapReader::State::Waiting;
		const FowScene& f = r.scene.fow();
		CHECK(f.holdFrameId() == 0 && f.holdReasons() == 0, "a hold before the first frame");
		int64_t id = r.frameNo;
		CHECK(!r.drawn(), "a frame drawn without the table");
		CHECK(f.holdFrameId() == id && f.holdReasons() == (FowScene::kHoldNoTable | inst),
		      "no table: frame " + S(f.holdFrameId()) + " of " + S(id) + ", reasons " + S(f.holdReasons()));
		id = r.frameNo;
		CHECK(!r.drawn() && f.holdFrameId() == id, "the next held frame not reported: " + S(f.holdFrameId()) + " of " + S(id));
		// the table arrives; the pair does not yet
		r.table->snapshot({3});
		r.batch({Ev(SrvFeed::kSnapshot, -1, kPlots, kPlots, (int32_t)r.table->gen)});
		id = r.frameNo;
		CHECK(!r.drawn(), "a frame drawn without the pair");
		CHECK(f.holdFrameId() == id && f.holdReasons() == (FowScene::kHoldNoPair | inst), "no pair: reasons " + S(f.holdReasons()));
		// the pair: admitted, drawn, the report cleared
		r.src->st = OverlayTapReader::State::Active;
		r.src->give = true;
		CHECK(r.drawn() && f.state() == FowScene::kActive, "the pair did not install");
		CHECK(f.holdFrameId() == 0 && f.holdReasons() == 0, "the hold not cleared by the drawn frame: " + S(f.holdReasons()));
		// in play: a new load's table of another size holds the frames, without kHoldInstall
		r.table->snapshot({3}, 8, 8);
		r.batch({Ev(SrvFeed::kSnapshot, -1, 8, 8, (int32_t)r.table->gen)});
		id = r.frameNo;
		CHECK(!r.drawn(), "a frame drawn with a table of another size");
		CHECK(f.holdFrameId() == id && f.holdReasons() == FowScene::kHoldTableSize, "size: reasons " + S(f.holdReasons()));
		r.table->snapshot({3});
		r.batch({Ev(SrvFeed::kSnapshot, -1, kPlots, kPlots, (int32_t)r.table->gen)});
		CHECK(r.drawn() && f.holdFrameId() == 0 && f.holdReasons() == 0, "the hold not cleared after the size matched");
	}
	{
		// the install fails (no pair within the wait and 30 EXE frames): failed, no hold reported from then on
		g_log.clear();
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		if(!r.start(ro, why))
			return false;
		r.src->st = OverlayTapReader::State::Waiting;
		CHECK(!r.drawn() && r.scene.fow().holdReasons() == (FowScene::kHoldNoPair | inst), "no pair: " + S(r.scene.fow().holdReasons()));
		CHECK(r.drawUntilFailed(), "a frame drawn while waiting for the pair");
		std::string t;
		CHECK(r.scene.fow().failed(t), "the install did not fail");
		CHECK(!r.drawn() && r.scene.fow().holdFrameId() == 0 && r.scene.fow().holdReasons() == 0,
		      "a failed fog reported a hold: " + S(r.scene.fow().holdReasons()));
		g_log.clear();   // the install failure is LOUD by design
	}
	return true;
}

// Ticket 0691 (cand4's hang, 2026-11-23): a map reload. The camera block's grid turns to the new load while the server's
// field is the old load's: Scene::decide refuses the frames (terrain NO_FIELD) and must REPORT them held (kHoldNoField, the
// camera's frame id; the host writes it for the composite), or exact lockstep waits for a frame that only comes once the
// composite's field tap has seen 30 more EXE Presents (the live deadlock). The new load's field: drawn, the hold cleared.
bool CaseFieldHold(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	CHECK(r.drawn(), "the first load's frame not drawn");
	CHECK(r.scene.holdFrameId() == 0 && r.scene.holdReasons() == 0, "a hold after a drawn frame");
	// the reload: the camera's grid is load 2, the field still load 1
	r.gridSeq = 2;
	int64_t id = r.frameNo;
	CHECK(!r.drawn(), "a frame drawn with the old load's field");
	CHECK(r.scene.holdFrameId() == id && r.scene.holdReasons() == Scene::kHoldNoField,
	      "reload: held frame " + S(r.scene.holdFrameId()) + " of " + S(id) + ", reasons " + S(r.scene.holdReasons()));
	id = r.frameNo;
	CHECK(!r.drawn() && r.scene.holdFrameId() == id && r.scene.holdReasons() == Scene::kHoldNoField,
	      "the next refused frame not reported: " + S(r.scene.holdFrameId()) + " of " + S(id));
	// the new load's field (load seq 2, copy 2) in the same section
	VPShellTap* t = VPSHELL_TAP(r.m.hdr);
	const uint32_t s0 = t->seq;
	t->seq = s0 | 1;
	MemoryBarrier();
	t->state = VPTAP_READY;
	t->loadSeq = 2;
	t->copySeq = 2;
	MemoryBarrier();
	t->seq = (s0 | 1) + 1;
	CHECK(r.drawn(), "the new load's field: no frame drawn");
	CHECK(r.scene.holdFrameId() == 0 && r.scene.holdReasons() == 0,
	      "the field hold not cleared by the new field: " + S(r.scene.holdReasons()));
	return true;
}

// Decision 5 (ii): the fog needs the overlay pair. render_overlay=0 and a tap that is OFF are install failures at the
// first frame; a tap without a pair yet holds the frames and fails after the wait; a pair within the wait installs; in
// play a lost pair keeps the last good one (counted) and a bad pair is LOUD once and the last good one kept.
bool CaseNoOverlayPair(std::string& why)
{
	{
		Rig r;
		RigOpt ro;
		ro.overlay = 0;
		if(!r.start(ro, why))
			return false;
		CHECK(!r.drawn(), "a frame drawn with render_overlay=0");
		std::string t;
		CHECK(r.scene.fow().failed(t) && t.find("render_overlay=0 (decision 5 (ii))") != std::string::npos, "overlay=0: " + t);
	}
	{
		Rig r;
		RigOpt ro;
		if(!r.start(ro, why))
			return false;
		r.src->st = OverlayTapReader::State::Off;
		r.src->give = false;
		CHECK(!r.drawn(), "a frame drawn with the tap OFF");
		std::string t;
		CHECK(r.scene.fow().failed(t) && t.find("the overlay tap is OFF") != std::string::npos, "tap off: " + t);
	}
	{
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		if(!r.start(ro, why))
			return false;
		r.src->st = OverlayTapReader::State::Waiting;
		CHECK(!r.drawn() && !r.drawn(), "frames drawn without a pair");
		std::string t;
		CHECK(!r.scene.fow().failed(t) && r.scene.fow().framesHeld == 2, "not held while waiting");
		Sleep(400);
		CHECK(!r.drawn(), "a frame drawn after the wait");
		// ticket 0491: past the wait with 3 EXE frames is a load going on, not a failure; 30 frames end it
		CHECK(!r.scene.fow().failed(t) && CountLog("fog: the install's wait passed 300 ms with only 3 map frame(s) presented by the EXE") == 1,
		      "failed after the wait with 3 EXE frames: " + t);
		CHECK(r.drawUntilFailed(), "a frame drawn while waiting for the pair");
		CHECK(r.scene.fow().failed(t) && t.find("no overlay pair yet (overlay WAITING)") != std::string::npos &&
		          t.find("30 map frame(s) presented by the EXE in the wait") != std::string::npos,
		      "wait: " + t);
	}
	{
		g_log.clear();   // the three install failures above are LOUD by design
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		if(!r.start(ro, why))
			return false;
		r.src->st = OverlayTapReader::State::Waiting;
		CHECK(!r.drawn(), "a frame drawn without a pair");
		r.src->st = OverlayTapReader::State::Active;
		r.src->give = true;
		CHECK(r.drawn() && r.scene.fow().state() == FowScene::kActive && CountLog("fog: installed at frame") == 1,
		      "the pair within the wait did not install");
		// in play: the pair lost -> the last good one kept, counted, not LOUD
		r.src->give = false;
		r.src->st = OverlayTapReader::State::Waiting;
		CHECK(r.drawn() && r.drawn(), "frames not drawn after the pair was lost");
		CHECK(r.scene.fow().keptPairFrames == 2 && memcmp(r.fow->lastIn.views[0].overlayProj, r.src->pair.overlayProj[0], 64) == 0,
		      "the last good pair not kept");
		// the terrain and the water still get the mask, placed by the kept pair (no overlay texture bound)
		CHECK(r.lastFs.fowOn && !r.lastFs.overlayBound && WaterSceneFowMask(&r.scene.water()) == r.fow->maskSrv,
		      "without the overlay's pair the terrain / water lost the fog");
		// a bad pair: LOUD once, the last good one kept, the frame drawn
		r.src->give = true;
		r.src->st = OverlayTapReader::State::Active;
		float good[16];
		memcpy(good, r.src->pair.overlayProj[0], 64);
		r.src->pair.overlayProj[0][0] = 0.0f;
		r.src->pair.generation = 9;
		CHECK(r.drawn() && r.drawn(), "frames not drawn with a bad pair");
		CHECK(r.scene.fow().badPairs == 1 && CountLog("LOUD: fog: overlay pair of generation 9 not used") == 1 &&
		          memcmp(r.fow->lastIn.views[0].overlayProj, good, 64) == 0,
		      "bad pair: " + S(r.scene.fow().badPairs));
		// (the overlay's own LOUD line for the bad pair is 0167's: not counted here)
		CHECK(CountLog("LOUD: fog") == 1, "LOUD fog lines " + S(CountLog("LOUD: fog")));
	}
	return true;
}

// Ticket 0429: the install wait is the fog's own time, not wall time. The fog's step is driven through Scene::fowAdmit
// with a made-up clock (the product's 10-s wait, admit every 100 ms) so that a stall of the server's render thread can be
// injected as a jump of the clock (0415's run a5: 12.7 s without a check, then the failure 34 ms before the first pair).
//  (a) 15 s without a check before the first pair: no failure at the check after it, the hook not called; the pair one
//      check later installs, the install's note names the gap;
//  (b) no pair at all, one 15-s stall inside the wait: fails once the fog's own time passes 10 s (not before), within the
//      limit plus the stall, the text naming the stall (the hook called once, code 5);
//  (c) a thread that checks only every 3 s (the EXE's 3.0-s steps of 0407): every check counts 1 s, so the install still
//      fails, never a hang (after 30 checks since ticket 0491: each check is one EXE frame, and the wait ends only once the
//      EXE presented 30);
//  (d) in play (the hold branch): a 15-s stall in a held episode is not counted toward the hold's LOUD line, which comes
//      once the fog's own time passes 10 s.
bool CaseStallNotCounted(std::string& why)
{
	const double kStep = 100, kStall = 15000, kWait = 10000;
	auto cam = [](Rig& r) {
		const int64_t fr = r.frameNo++;
		WriteCamera(r.m, (float)(fr % 5), fr);
		r.scene.poll();
		return *VPSHELL_CAMERA(r.m.hdr);
	};
	{   // (a)
		g_log.clear();
		g_fatalCalls = 0;
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		ro.waitMs = kWait;
		if(!r.start(ro, why))
			return false;
		bool done = false;
		double t = 50000;
		for(int i = 0; i < 10; ++i, t += kStep)
		{
			CHECK(!r.scene.fowAdmit(cam(r), t), "(a) admitted without a pair");
			FowFatalStep(r.scene, &FatalHook, done);
		}
		t += kStall;   // the render thread does not come back for 15 s; the pair is not bound yet at the check after it
		CHECK(!r.scene.fowAdmit(cam(r), t), "(a) admitted without a pair after the stall");
		FowFatalStep(r.scene, &FatalHook, done);
		std::string ft;
		CHECK(!r.scene.fow().failed(ft) && !done && g_fatalCalls == 0, "(a) the stall failed the install: " + ft);
		CHECK(CountLog("fog: the render thread came back after 15100 ms") == 1, "(a) no note of the gap");
		r.src->give = true;
		t += kStep;
		CHECK(r.scene.fowAdmit(cam(r), t) && r.scene.fow().state() == FowScene::kActive, "(a) the pair after the stall did not install");
		FowFatalStep(r.scene, &FatalHook, done);
		CHECK(!done && g_fatalCalls == 0, "(a) fatal after the install");
		CHECK(CountLog("fog: installed at frame") == 1 && CountLog("1 render-thread gap(s) above 1000 ms") == 1 &&
		          CountLog("the longest 15100 ms") == 1,
		      "(a) the install's note does not name the gap");
		CHECK(CountLog("LOUD") == 0, "(a) a LOUD line");
	}
	{   // (b)
		g_log.clear();
		g_fatalCalls = 0;
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		ro.waitMs = kWait;
		if(!r.start(ro, why))
			return false;
		bool done = false;
		const double t0 = 50000;
		double t = t0;
		double failedAt = -1;
		int checks = 0;
		for(; checks < 1000 && failedAt < 0; ++checks)
		{
			if(checks == 20)
				t += kStall;
			r.scene.fowAdmit(cam(r), t);
			FowFatalStep(r.scene, &FatalHook, done);
			if(done)
				failedAt = t;
			else
				t += kStep;
		}
		// own time: 19 steps (1900) + the stall's gap (15100 ms) counted as 1000 at wall t0 + 17000, then steps of 100:
		// above 10000 (10100) after 72 more, at wall t0 + 24200; the limit plus the stall: t0 + 25000
		CHECK(failedAt >= 0 && g_fatalCalls == 1 && g_fatalCode == 5, "(b) no failure, hook calls " + S(g_fatalCalls));
		CHECK(failedAt - t0 > kWait + kStall - 1000 - kStep && failedAt - t0 <= kWait + kStall,
		      "(b) failed at " + S((long long)(failedAt - t0)) + " ms of wall time");
		CHECK(g_fatalWhy.find("no overlay pair yet (overlay ACTIVE) 10100 ms of the fog's own wait after the first frame of the map") !=
		              std::string::npos &&
		          g_fatalWhy.find("14100 ms of it not counted: 1 render-thread gap(s) above 1000 ms") != std::string::npos &&
		          g_fatalWhy.find("the longest 15100 ms") != std::string::npos,
		      "(b) text: " + g_fatalWhy);
	}
	{   // (c)
		g_log.clear();
		g_fatalCalls = 0;
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		ro.waitMs = kWait;
		if(!r.start(ro, why))
			return false;
		bool done = false;
		int checks = 0;
		for(double t = 50000; checks < 100 && !done; t += 3000)
		{
			checks++;
			r.scene.fowAdmit(cam(r), t);
			FowFatalStep(r.scene, &FatalHook, done);
		}
		// check 1 starts the wait; checks 2..11 count 1000 each (own 10000, not above); check 12 = 11000 is past the wait but
		// with 12 EXE frames (ticket 0491: fewer than 30, a load going on); check 30 (own 29000, 30 frames) fails
		CHECK(done && g_fatalCalls == 1 && checks == 30, "(c) failed after " + S(checks) + " checks (done " + S(done) + ")");
		CHECK(g_fatalWhy.find("29 render-thread gap(s) above 1000 ms") != std::string::npos &&
		          g_fatalWhy.find("30 map frame(s) presented by the EXE in the wait") != std::string::npos,
		      "(c) text: " + g_fatalWhy);
	}
	{   // (d)
		g_log.clear();
		g_fatalCalls = 0;
		Rig r;
		RigOpt ro;
		ro.waitMs = kWait;
		if(!r.start(ro, why))
			return false;
		bool done = false;
		double t = 50000;
		CHECK(r.scene.fowAdmit(cam(r), t) && r.scene.fow().state() == FowScene::kActive, "(d) not installed");
		// in play the table becomes another size (a new load's table before its field): frames held
		r.table->snapshot({3}, kPlots + 2, kPlots);
		r.batch({});
		for(int i = 0; i < 5; ++i)
			CHECK(!r.scene.fowAdmit(cam(r), t += kStep), "(d) admitted with a table of another size");
		t += kStall;
		CHECK(!r.scene.fowAdmit(cam(r), t), "(d) admitted after the stall");
		CHECK(CountLog("LOUD") == 0 && CountLog("fog: the render thread came back after 15000 ms (frame wanted while held in play)") == 1,
		      "(d) the stall counted toward the hold's LOUD line (LOUD " + S(CountLog("LOUD")) + ")");
		// own time of the episode (from its first held check): 4 steps + the stall counted as 1000 = 1400 so far; above
		// 10000 (10100) after 87 more steps
		int steps = 0;
		while(CountLog("LOUD") == 0 && steps < 500)
		{
			r.scene.fowAdmit(cam(r), t += kStep);
			steps++;
		}
		CHECK(steps == 87 && CountLog("LOUD: fog: frames held for 10100 ms of the fog's own time in play") == 1,
		      "(d) LOUD after " + S(steps) + " steps");
		FowFatalStep(r.scene, &FatalHook, done);
		CHECK(!done && g_fatalCalls == 0, "(d) fatal in play");
	}
	return true;
}

// Ticket 0491: on 9074's huge late loads (T1100, 180 x 112, 43 civs) the EXE presents only now and then after the map's
// first frame, and the overlay's pair is taken at a Present; the 10-s wait failed with the overlay WAITING. The wait now
// ends after 10 s only once the EXE presented 30 map frames, else at the slow-load bound (12 x the wait, 120 s). Made-up
// clock as CaseStallNotCounted: a check every 100 ms (the host's loop), an EXE frame every `every` checks (0 = only the first).
//  (a) a frame every 2.5 s, the pair at 40 s: no failure, one note when the 10 s pass (5 frames by then), the install;
//  (b) a frame every 5 s, no pair: fails at the bound (own 120,100 ms, 25 frames), the text naming the frames and the bound;
//  (c) a frame at every check, no pair: fails at 10.1 s as before (102 frames);
//  (d) no frame after the first (an EXE that never comes back), no pair: fails at the bound with 1 frame;
//  (e) a frame every 2.5 s, no pair: fails once the EXE presented 30 (at 72.5 s), before the bound.
bool CaseSlowLoad(std::string& why)
{
	const double kStep = 100, kWait = 10000, t0 = 50000;
	struct Arm
	{
		const char* name;
		int every;        // checks between two EXE frames (0: only the first)
		int pairAt;       // the check the pair comes at (-1: never)
		int failAt;       // the check that fails (-1: none)
		int64_t frames;   // EXE frames in the failure's text
	};
	const Arm arms[] = {
		{"(a)", 25, 400, -1, 0},
		{"(b)", 50, -1, 1201, 25},
		{"(c)", 1, -1, 101, 102},
		{"(d)", 0, -1, 1201, 1},
		{"(e)", 25, -1, 725, 30},
	};
	for(const Arm& a : arms)
	{
		g_log.clear();
		g_fatalCalls = 0;
		Rig r;
		RigOpt ro;
		ro.givePair = false;
		ro.waitMs = kWait;
		if(!r.start(ro, why))
			return false;
		const std::string n = a.name;
		bool done = false;
		int k = 0;
		for(; k < 2000 && !done; ++k)
		{
			if(k == 0 || (a.every > 0 && k % a.every == 0))
			{
				const int64_t fr = r.frameNo++;
				WriteCamera(r.m, (float)(fr % 5), fr);
			}
			if(k == a.pairAt)
				r.src->give = true;
			r.scene.poll();   // takes the pair (the host's poll before decide)
			const bool admitted = r.scene.fowAdmit(*VPSHELL_CAMERA(r.m.hdr), t0 + kStep * k);
			FowFatalStep(r.scene, &FatalHook, done);
			if(admitted)
			{
				CHECK(k == a.pairAt && r.scene.fow().state() == FowScene::kActive, n + " admitted at check " + S(k));
				break;
			}
		}
		if(a.failAt < 0)
		{
			CHECK(!done && g_fatalCalls == 0 && r.scene.fow().state() == FowScene::kActive && CountLog("fog: installed at frame") == 1,
			      n + " not installed (checks " + S(k) + ")");
			CHECK(CountLog("fog: the install's wait passed 10000 ms with only 5 map frame(s) presented by the EXE") == 1, n + " no note");
			CHECK(CountLog("LOUD") == 0, n + " a LOUD line");
			continue;
		}
		const int failedAt = k - 1;
		CHECK(done && g_fatalCalls == 1 && g_fatalCode == 5 && failedAt == a.failAt,
		      n + " failed at check " + S(failedAt) + " (hook calls " + S(g_fatalCalls) + ")");
		CHECK(g_fatalWhy.find("no overlay pair yet (overlay ACTIVE)") != std::string::npos &&
		          g_fatalWhy.find(S(a.frames) + " map frame(s) presented by the EXE in the wait; the wait ends after 10000 ms once the "
		                                        "EXE has presented 30, else after the slow-load bound of 120000 ms") != std::string::npos,
		      n + " text: " + g_fatalWhy);
		// the slow-load note: once in every arm whose EXE had fewer than 30 frames when the 10 s passed (all but (c))
		CHECK(CountLog("fog: the install's wait passed 10000 ms with only") == (a.every == 1 ? 0 : 1), n + " note count");
	}
	return true;
}

// render_fow and render_fow_level in server.cfg: 0 / 1 and -1..3 accepted, anything else refused with the key named.
bool CaseOption(std::string& why)
{
	const char* fv[] = {"0", "1", "2", "yes", "-1"};
	for(int i = 0; i < 5; ++i)
	{
		HostConfig c;
		std::string w;
		auto get = [&](const char* key) -> std::string { return !strcmp(key, "render_fow") ? fv[i] : ""; };
		const bool ok = ParseHostConfig(get, "C:\\game", "C:\\run", c, w);
		CHECK(ok == (i < 2) && (ok || w.find("render_fow=") != std::string::npos) && (!ok || RenderFowOption() == i),
		      std::string("render_fow=") + fv[i] + ": " + (ok ? "accepted" : w));
	}
	{
		HostConfig c;
		std::string w;
		auto get = [&](const char*) -> std::string { return ""; };
		CHECK(ParseHostConfig(get, "C:\\game", "C:\\run", c, w) && RenderFowOption() == 1 && RenderFowLevelOption() == -1,
		      "the defaults are not render_fow=1, render_fow_level=-1");
	}
	const char* lv[] = {"-1", "0", "3", "4", "-2", "x"};
	for(int i = 0; i < 6; ++i)
	{
		HostConfig c;
		std::string w;
		auto get = [&](const char* key) -> std::string { return !strcmp(key, "render_fow_level") ? lv[i] : ""; };
		const bool ok = ParseHostConfig(get, "C:\\game", "C:\\run", c, w);
		CHECK(ok == (i < 3) && (ok || w.find("render_fow_level=") != std::string::npos) &&
		          (!ok || RenderFowLevelOption() == atoi(lv[i])),
		      std::string("render_fow_level=") + lv[i] + ": " + (ok ? "accepted" : w));
	}
	// render_fow=0: no fog object does anything, today's picture (CaseAllVisible compares the pictures)
	Rig r;
	RigOpt ro;
	ro.fow = 0;
	ro.bits.clear();   // no table at all: render_fow=0 does not care
	if(!r.start(ro, why))
		return false;
	CHECK(r.drawn() && r.fow->builds == 0 && r.fow->clouds == 0 && r.scene.fow().state() == FowScene::kOff &&
	          WaterSceneFowMask(&r.scene.water()) == nullptr && !r.scene.fow().terrainInputs().on,
	      "render_fow=0 touched the frame");
	return true;
}

// The fog restarted on the device with an EXE path that does not exist: the real MaskRenderer cannot read its programs.
void MissingExe(Rig& r)
{
	FowSceneOptions fo;
	fo.on = true;
	fo.level = 3;
	fo.exe = L"C:\\no-such-folder-3954\\CivilizationV_DX11.exe";
	fo.installWaitMs = 300;
	r.scene.fow().init(g_dev, fo);
}

// The installed game (read-only): C5R_GAME_DIR, which test_fow_scene.py sets from the setting render_game_dir of
// tools/release/settings.py when it is not given. Unset, a path that does not exist, so the cases that need the install
// fail on it (ticket 4716, D174: was a literal path in a user folder, the value the setting gives on the workstation).
std::wstring GameDir()
{
	const char* e = getenv("C5R_GAME_DIR");
	const std::string s = e && *e ? e : "<C5R_GAME_DIR not set>";
	return std::wstring(s.begin(), s.end());
}

// The real c5r::fow modules (3952's mask, 3955's cloud planes) on WARP with the EXE's programs (C5R_EXE) and the
// install's cloud files (C5R_GAME_DIR, default the owner's pure_vp copy): installed at the first frame, the mask built with
// hexagons, the clouds drawn (cull quads and plane quads counted) over an unexplored plot, no error, no LOUD line; the
// frame differs from every-plot-visible's.
bool CaseRealModules(std::string& why)
{
	std::vector<uint8_t> px[2];
	for(int k = 0; k < 2; ++k)
	{
		Rig r;
		RigOpt ro;
		ro.fakeFow = false;
		ro.bits.assign(kPlots * kPlots, 3);
		if(k == 1)
			for(uint32_t i = 0; i < kPlots * kPlots; i += 2)
				ro.bits[i] = 0;
		if(!r.start(ro, why))
			return false;
		FowSceneOptions fo;
		fo.on = true;
		fo.level = 3;
		fo.exe = g_exe;
		fo.gameDir = GameDir();
		fo.installWaitMs = 300;
		fo.freeze = true;
		r.scene.fow().init(g_dev, fo);
		CHECK(r.drawn(), "k=" + S(k) + ": no frame drawn");
		FowScene& f = r.scene.fow();
		const cf::MaskStats& ms = f.lastMaskStats();
		const cf::CloudStats& cs = f.lastCloudStats();
		const uint32_t hex = ms.hexagons[0] + ms.hexagons[1] + ms.hexagons[2];
		CHECK(f.state() == FowScene::kActive && f.masksBuilt == 1 && f.buildErrors == 0 && f.cloudDraws == 1 && f.cloudErrors == 0,
		      "k=" + S(k) + " state " + FowScene::StateName(f.state()) + " built " + S(f.masksBuilt) + " build errors " +
		          S(f.buildErrors) + " clouds " + S(f.cloudDraws) + " cloud errors " + S(f.cloudErrors));
		CHECK(hex > 0 && ms.draws > 0 && cs.draws > 0, "k=" + S(k) + " hexagons " + S(hex) + " mask draws " + S(ms.draws) +
		                                                   " cloud draws " + S(cs.draws));
		CHECK(f.terrainInputs().on && f.terrainInputs().mask && f.terrainInputs().cloudBase &&
		          WaterSceneFowMask(&r.scene.water()) == f.terrainInputs().mask,
		      "inputs not handed on");
		CHECK(CountLog("LOUD") == 0, "a LOUD line");
		// the scene's depth (the composite's, which the EXE's models test against) carries no FOW CULL zero: the cloud
		// pass ran on a copy (live 2026-11-14 23:31: the zeros hid the models)
		const int zeros = r.depthZeros();
		CHECK(zeros == 0, "k=" + S(k) + ": " + S(zeros) + " depth texels 0 in the scene's depth (cull quads " + S(cs.cullQuads) + ")");
		px[k] = r.pixels();
		g_log.clear();
	}
	CHECK(!px[0].empty() && px[0] != px[1], "the unexplored plots changed nothing in the frame");
	return true;
}

// The real c5r::fow modules with a missing EXE / install (3952 / 3955 merged): the first frame is the install,
// it fails once with the text naming the module; the hook is called once with code 5; no frame is drawn after it with
// a hook (never a game frame without the fog). Without a hook: LOUD once, then the scene draws on without the fog. A
// cloud module that cannot start names the cloud planes. No table / no revealed plot within the wait is a failure too.
bool CaseInstallFailureFatalOnce(std::string& why)
{
	g_fatalCalls = 0;
	{
		Rig r;
		RigOpt ro;
		ro.fakeFow = false;
		if(!r.start(ro, why))
			return false;
		MissingExe(r);
		bool done = false;
		for(int i = 0; i < 5; ++i)
		{
			CHECK(!r.drawn(), "a frame drawn after an install failure (frame " + S(i) + ")");
			FowFatalStep(r.scene, &FatalHook, done);
		}
		CHECK(done && g_fatalCalls == 1 && g_fatalCode == kFowFatalExitCode && kFowFatalExitCode == 5,
		      "hook calls " + S(g_fatalCalls) + ", code " + S(g_fatalCode));
		CHECK(g_fatalWhy.find("fog of war: the FOW mask") != std::string::npos && g_fatalWhy.find("could not start: ") != std::string::npos,
		      "text: " + g_fatalWhy);
		CHECK(CountLog("LOUD") == 1 && CountLog("the server ends (exit code 5)") == 1, "LOUD " + S(CountLog("LOUD")));
		CHECK(r.scene.drawnTotal == 0, "frames drawn " + S(r.scene.drawnTotal));
	}
	{
		g_log.clear();
		Rig r;
		RigOpt ro;
		ro.fakeFow = false;
		if(!r.start(ro, why))
			return false;
		MissingExe(r);
		bool done = false;
		CHECK(!r.drawn(), "the failing first frame was drawn");
		FowFatalStep(r.scene, nullptr, done);
		CHECK(done && r.scene.fow().state() == FowScene::kWithout && CountLog("no fatal hook") == 1, "without a hook");
		CHECK(r.drawn() && r.drawn() && r.scene.fow().framesWithout == 2 && WaterSceneFowMask(&r.scene.water()) == nullptr,
		      "no drawing on without the fog");
		CHECK(CountLog("LOUD") == 1, "LOUD " + S(CountLog("LOUD")));
	}
	{
		g_log.clear();
		Rig r;
		RigOpt ro;
		if(!r.start(ro, why))
			return false;
		// (the fake's cloud init failure is set after start(): restart the fog on the device with it)
		r.fow->cloudInit = c5r::Status::fail(c5r::Err::NotFound, "fogcloudtile_dif_03.dds");
		FowSceneOptions fo;
		fo.on = true;
		fo.level = 1;
		fo.installWaitMs = 300;
		r.scene.fow().init(g_dev, fo);
		CHECK(r.fow->size == 512, "mask size " + S(r.fow->size) + " at FOWLevel 1");
		CHECK(!r.drawn(), "a frame drawn without the cloud planes");
		std::string t;
		CHECK(r.scene.fow().failed(t) && t.find("the cloud planes") != std::string::npos && t.find("fogcloudtile_dif_03.dds") != std::string::npos,
		      "cloud text: " + t);
	}
	{
		g_log.clear();
		Rig r;
		RigOpt ro;
		ro.bits = {0};   // a table without a revealed plot (the active team below 0)
		if(!r.start(ro, why))
			return false;
		CHECK(!r.drawn(), "a frame drawn without fog bits");
		Sleep(400);
		CHECK(r.drawUntilFailed(), "a frame drawn after the wait");
		std::string t;
		CHECK(r.scene.fow().failed(t) && t.find("no revealed plot") != std::string::npos, "no bits: " + t);
	}
	{
		g_log.clear();
		Rig r;
		RigOpt ro;
		ro.bits.clear();   // no table at all
		if(!r.start(ro, why))
			return false;
		CHECK(!r.drawn(), "a frame drawn without a table");
		Sleep(400);
		CHECK(r.drawUntilFailed(), "a frame drawn after the wait");
		std::string t;
		CHECK(r.scene.fow().failed(t) && t.find("no plot table from the feed") != std::string::npos, "no table: " + t);
	}
	return true;
}

// In play (after the install) nothing is fatal: a failed mask build and a failed cloud draw are LOUD once per reason and
// counted, the frame is drawn, the build is retried; the hook is never called.
bool CaseInPlayFaultNeverFatal(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	CHECK(r.drawn(), "no first frame");
	bool done = false;
	g_fatalCalls = 0;
	r.fow->buildSt = c5r::Status::fail(c5r::Err::Device, "CreateBuffer failed");
	r.src->pair.generation = 2;   // a new pair: a rebuild
	for(int i = 0; i < 4; ++i)
	{
		CHECK(r.drawn(), "a frame not drawn after a build failure");
		FowFatalStep(r.scene, &FatalHook, done);
	}
	CHECK(r.scene.fow().buildErrors == 4 && r.fow->builds == 5 && r.fow->clouds == 1, "build retries " + S(r.fow->builds));
	CHECK(CountLog("LOUD: fog: the mask build failed") == 1, "build failure LOUD " + S(CountLog("LOUD")));
	CHECK(WaterSceneFowMask(&r.scene.water()) == nullptr && !r.scene.fow().terrainInputs().on, "a failed mask was bound");
	r.fow->buildSt = c5r::Status::Ok();
	r.fow->cloudSt = c5r::Status::fail(c5r::Err::Device, "Draw failed");
	for(int i = 0; i < 3; ++i)
	{
		CHECK(r.drawn(), "a frame not drawn after a cloud failure");
		FowFatalStep(r.scene, &FatalHook, done);
	}
	CHECK(r.scene.fow().cloudErrors == 3 && CountLog("LOUD: fog: the cloud planes failed") == 1, "cloud errors");
	CHECK(!done && g_fatalCalls == 0, "a fault in play was fatal");
	CHECK(CountLog("LOUD") == 2, "LOUD lines " + S(CountLog("LOUD")));
	return true;
}

// Ticket 0339: the mask handed to the EXE's model draws (srv_fow_mask_pub.h). The section appears once the EXE's pid is
// known; nothing is copied until the consumer says `wanted`; then every new mask goes into a slot that is neither the
// newest nor the consumer's inUseSlot and is published with its pair and FOWAnim; the slot opens by name with the mask's
// description; the consumer's FAILED state fails the fog's install (the D125 route, once); not wanted any more: the
// slot textures go.
bool CaseModelFogHandover(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	wchar_t secName[96];
	swprintf_s(secName, L"Local\\VPFowSceneTestMask.%u.%d", GetCurrentProcessId(), g_serial);
	r.scene.fow().maskPublisher().setSectionName(secName);
	CHECK(r.drawn(), "no first frame");
	HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, secName);
	CHECK(!h, "the section exists without the EXE's pid");
	const uint32_t kPid = 4242;
	VPShellTap* t = VPSHELL_TAP(r.m.hdr);
	t->seq++;
	MemoryBarrier();
	t->exePid = kPid;
	MemoryBarrier();
	t->seq++;
	r.src->pair.generation = 2;
	CHECK(r.drawn(), "no frame with the pid");
	h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, secName);
	CHECK(h, "no section with the EXE's pid");
	VPShellFowMaskSection* s = (VPShellFowMaskSection*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_FOWMASK_SECTION_BYTES);
	CloseHandle(h);
	CHECK(s, "the section does not map");
	struct Unmap
	{
		void* p;
		~Unmap() { UnmapViewOfFile(p); }
	} um{s};
	CHECK(s->magic == VPSHELL_FOWMASK_MAGIC && s->exePid == kPid && s->serverPid == GetCurrentProcessId() &&
	          s->slotCount == VPSHELL_FOWMASK_SLOTS && s->producerState == VPFMSK_NONE && s->newestSlot == VPSHELL_FOWMASK_NONE,
	      "the section's head");
	r.src->pair.generation = 3;
	CHECK(r.drawn() && r.scene.fow().maskPublisher().copies == 0, "copied without `wanted`");
	// the consumer wants it
	s->wanted = 1;
	s->inUseSlot = VPSHELL_FOWMASK_NONE;
	FowMaskPublisher& pub = r.scene.fow().maskPublisher();
	for(int i = 0; i < 60 && pub.published < 3; ++i)   // WARP signals a copy's query some frames later
	{
		r.src->pair.generation = 4 + i;
		CHECK(r.drawn(), "a frame not drawn while handing over");
		g_ctx->Flush();
		Sleep(20);
	}
	CHECK(pub.faults == 0, "faults " + S(pub.faults) + " (shared textures on this device?)");
	CHECK(pub.copies >= 3 && pub.published >= 2 && s->producerState == VPFMSK_ACTIVE && s->newestSlot < VPSHELL_FOWMASK_SLOTS,
	      "copies " + S(pub.copies) + " published " + S(pub.published));
	const VPShellFowMaskSlot* sl = VPSHELL_FOWMASK_SLOT(s, s->newestSlot);
	CHECK(sl->valid == 1 && sl->generation == s->generation && sl->numViews == 1 && sl->pairGeneration >= 4 && sl->texEpoch == s->epoch &&
	          memcmp(sl->overlayProj[0], r.src->pair.overlayProj[0], 64) == 0,
	      "the newest slot's record (pair generation " + S(sl->pairGeneration) + ")");
	CHECK(memcmp(sl->fowAnim, r.scene.fow().terrainInputs().fowAnim, 16) == 0, "FOWAnim handed over is not the terrain's");
	// the slot's texture by its name, with the mask's description
	ID3D11Device1* d1 = nullptr;
	g_dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&d1);
	CHECK(d1, "no ID3D11Device1");
	wchar_t tn[96];
	swprintf_s(tn, VPSHELL_FOWMASK_TEXTURE_FMT, kPid, sl->texEpoch, s->newestSlot);
	ID3D11Texture2D* st = nullptr;
	const HRESULT hr = d1->OpenSharedResourceByName(tn, DXGI_SHARED_RESOURCE_READ, __uuidof(ID3D11Texture2D), (void**)&st);
	d1->Release();
	CHECK(SUCCEEDED(hr) && st, "the slot texture does not open by name");
	D3D11_TEXTURE2D_DESC sd;
	st->GetDesc(&sd);
	st->Release();
	CHECK(sd.Width == 4 && sd.Height == 4 && sd.Format == DXGI_FORMAT_R8G8B8A8_UNORM && sl->width == 4, "the slot's description");
	// the consumer names a slot: it is never written while named (its generation stays) and never the newest after
	const uint32_t named = s->newestSlot;
	const uint32_t namedGen = sl->generation;
	s->inUseSlot = named;
	for(int i = 0; i < 80 && s->generation < namedGen + 3; ++i)
	{
		r.src->pair.generation = 100 + i;
		CHECK(r.drawn(), "a frame not drawn with a named slot");
		g_ctx->Flush();
		Sleep(20);
		CHECK(VPSHELL_FOWMASK_SLOT(s, named)->generation == namedGen, "the named slot was written");
	}
	CHECK(s->newestSlot != named && s->generation > namedGen, "nothing published around the named slot");
	s->inUseSlot = VPSHELL_FOWMASK_NONE;
	// the consumer FAILED: the install fails once with its reason
	const char text[] = "test: the view-table entry holds 0x1234";
	memcpy(s->failText, text, sizeof(text));
	s->failReason = VPFMCF_MISMATCH;
	MemoryBarrier();
	s->consumerState = VPFMC_FAILED;
	bool done = false;
	g_fatalCalls = 0;
	r.src->pair.generation = 40;
	r.drawn();
	FowFatalStep(r.scene, &FatalHook, done);
	FowFatalStep(r.scene, &FatalHook, done);
	CHECK(g_fatalCalls == 1 && g_fatalWhy.find("MISMATCH: test: the view-table entry holds 0x1234") != std::string::npos,
	      "the consumer's failure was not fatal once: " + g_fatalWhy);
	CHECK(CountLog("fog models: consumer FAILED") == 1, "the consumer's state not logged");
	return true;
}

// Ticket 0339: the consumer stops wanting the mask (model_fow=0, OFF): the slot textures go, nothing more is copied.
bool CaseModelFogNotWanted(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	wchar_t secName[96];
	swprintf_s(secName, L"Local\\VPFowSceneTestMask2.%u.%d", GetCurrentProcessId(), g_serial);
	r.scene.fow().maskPublisher().setSectionName(secName);
	VPShellTap* t = VPSHELL_TAP(r.m.hdr);
	t->seq++;
	t->exePid = 4243;
	t->seq++;
	CHECK(r.drawn(), "no first frame");
	HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, secName);
	CHECK(h, "no section");
	VPShellFowMaskSection* s = (VPShellFowMaskSection*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_FOWMASK_SECTION_BYTES);
	CloseHandle(h);
	CHECK(s, "map");
	struct Unmap
	{
		void* p;
		~Unmap() { UnmapViewOfFile(p); }
	} um{s};
	s->wanted = 1;
	for(int i = 0; i < 3; ++i)
	{
		r.src->pair.generation = 2 + i;
		r.drawn();
		g_ctx->Flush();
	}
	FowMaskPublisher& pub = r.scene.fow().maskPublisher();
	CHECK(pub.copies >= 1, "nothing copied while wanted");
	s->wanted = 0;
	s->consumerState = VPFMC_OFF;
	const int64_t c0 = pub.copies;
	for(int i = 0; i < 3; ++i)
	{
		r.src->pair.generation = 10 + i;
		r.drawn();
	}
	CHECK(pub.copies == c0 && s->newestSlot == VPSHELL_FOWMASK_NONE && s->producerState == VPFMSK_NONE, "copies after OFF");
	CHECK(CountLog("the consumer does not want the mask (OFF): slot textures released") == 1, "the release not logged");
	bool done = false;
	g_fatalCalls = 0;
	FowFatalStep(r.scene, &FatalHook, done);
	CHECK(g_fatalCalls == 0, "OFF was fatal");
	return true;
}

// FOWLevel from the player's GraphicsSettingsDX11.ini under C5_DOCUMENTS: the key read, a missing file / key and a bad
// value give -1 (the scene then takes 3); the mask size per level.
bool CaseFowLevel(std::string& why)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	wchar_t dir[MAX_PATH];
	swprintf_s(dir, L"%sfowlevel_test_%u", tmp, GetCurrentProcessId());
	const std::wstring docs = dir;
	const std::wstring sub = docs + L"\\My Games\\Sid Meier's Civilization 5";
	CreateDirectoryW(docs.c_str(), nullptr);
	CreateDirectoryW((docs + L"\\My Games").c_str(), nullptr);
	CreateDirectoryW(sub.c_str(), nullptr);
	const std::wstring ini = sub + L"\\GraphicsSettingsDX11.ini";
	SetEnvironmentVariableW(L"C5_DOCUMENTS", docs.c_str());
	auto write = [&](const char* text) {
		FILE* f = nullptr;
		_wfopen_s(&f, ini.c_str(), L"wb");
		if(f)
		{
			fputs(text, f);
			fclose(f);
		}
	};
	std::string from;
	bool ok = true;
	write("[DX11]\r\nFullScreen = 0\r\nFOWLevel = 1\r\nWaterQuality = 2\r\n");
	ok = ok && ReadFowLevel(from) == 1;
	write("FOWLevel=2\n");
	ok = ok && ReadFowLevel(from) == 2;
	write("FOWLevel = 7\r\n");
	ok = ok && ReadFowLevel(from) == -1 && from.find("not 0..3") != std::string::npos;
	write("WaterQuality = 2\r\n");
	ok = ok && ReadFowLevel(from) == -1 && from.find("no FOWLevel key") != std::string::npos;
	DeleteFileW(ini.c_str());
	ok = ok && ReadFowLevel(from) == -1 && from.find("not readable") != std::string::npos;
	SetEnvironmentVariableW(L"C5_DOCUMENTS", nullptr);
	RemoveDirectoryW(sub.c_str());
	RemoveDirectoryW((docs + L"\\My Games").c_str());
	RemoveDirectoryW(docs.c_str());
	CHECK(ok, "ReadFowLevel: " + from);
	CHECK(FowMaskSize(0) == 512 && FowMaskSize(1) == 512 && FowMaskSize(2) == 1024 && FowMaskSize(3) == 1024, "mask sizes");
	return true;
}

// FOWAnim's T: the server's clock while the map is drawn (z = -2 T); a pause between two drawn frames counts at most
// 250 ms; render_water_freeze holds it at 0 (the rigs).
bool CaseClock(std::string& why)
{
	Rig r;
	RigOpt ro;
	ro.bits.assign(kPlots * kPlots, 3);
	ro.bits[0] = 0;   // clouds drawn: the fake records FOWAnim
	if(!r.start(ro, why))
		return false;
	FowSceneOptions fo;
	fo.on = true;
	fo.level = 3;
	fo.installWaitMs = 300;
	fo.freeze = false;
	r.scene.fow().init(g_dev, fo);
	CHECK(r.drawn(), "first frame");
	const float z0 = r.fow->lastAnim[2];
	Sleep(60);
	CHECK(r.drawn(), "second frame");
	const float z1 = r.fow->lastAnim[2];
	Sleep(600);
	CHECK(r.drawn(), "third frame");
	const float z2 = r.fow->lastAnim[2];
	CHECK(z0 == 0.0f && z1 < -0.1f && z1 >= -0.5f, "z after 60 ms: " + std::to_string(z1));
	CHECK(z2 - z1 >= -0.501f && z2 - z1 < -0.49f, "a 600 ms pause counted " + std::to_string(z1 - z2) + " (want 0.5 = 2 x 250 ms)");
	return true;
}

// ---- issue 4305 (epic 0169): the tile styles in the scene and the bake ------------------------------------------------
// The residency (render_tiles=bake, the stand-in baker) reads the plots' continent art types from its FeedSource (a fake
// here) and hands the same array to its bake's setMap (MapInputs::plotArtTypes) and to the renderer (setTileStyles,
// observed through the draw's FrameStats::fowStyleTiles: the EXE-PS tiles per t6 style). terrain-surface.md 12.6.

struct FakeFeed : FeedSource
{
	std::vector<SrvFeed::PlotRecord> rec;
	uint32_t gen = 0, w = 0, h = 0;
	int copies = 0;
	bool drain(SrvFeed::Batch&) override { return false; }
	void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& ww, uint32_t& hh, uint32_t) override
	{
		copies++;
		out = rec;
		g = gen;
		ww = w;
		hh = h;
	}
	// a new table of width x height plots with these art types (one value for all, or one per plot); getters read
	void table(const std::vector<uint8_t>& art, uint32_t width = kPlots, uint32_t height = kPlots)
	{
		w = width;
		h = height;
		rec.assign((size_t)w * h, SrvFeed::PlotRecord());
		for(size_t i = 0; i < rec.size(); ++i)
		{
			rec[i].artType = art.size() == 1 ? art[0] : art[i];
			rec[i].known = 1;
			rec[i].fow = 3;
		}
		gen++;
	}
};

// The rule over the scene's field: tile (x, y) of the 4 x 8-tile field (kPlots x kPlots plots + 4 margin rows)
uint32_t RuleStyle(const std::vector<uint8_t>& art, uint32_t x, uint32_t y)
{
	c5r::bake::TileId t;
	t.x = x;
	t.y = y;
	return c5r::bake::tileStyle(art.data(), kPlots, kPlots, true, t);
}

bool StylesAre(ResidencySource& res, const std::vector<uint8_t>& art, std::string& why)
{
	for(uint32_t y = 0; y < kPlots + 4; ++y)
		for(uint32_t x = 0; x < kPlots; ++x)
			if(res.tileStyleAt(x, y) != RuleStyle(art, x, y))
			{
				why = "tile (" + S(x) + ", " + S(y) + "): style " + S(res.tileStyleAt(x, y)) + ", the rule " + S(RuleStyle(art, x, y));
				return false;
			}
	return true;
}

// Draws until the stand-in's caches are in (the EXE PS draws only tiles with a cache); the frame's per-style tile counts
bool DrawStyled(Rig& r, uint32_t out[5], std::string& why)
{
	for(int k = 0; k < 40; ++k)
	{
		if(!r.drawn())
			continue;
		uint32_t sum = 0;
		for(int s = 0; s < 5; ++s)
			sum += r.lastFs.fowStyleTiles[s];
		if(sum > 0 && sum == r.lastFs.tilesPass0 + r.lastFs.tilesPass1 && r.lastFs.tilesWithoutCache == 0)
		{
			memcpy(out, r.lastFs.fowStyleTiles, sizeof(uint32_t) * 5);
			return true;
		}
		Sleep(20);
	}
	why = "no frame with every drawn tile cached (tiles drawn " + S(r.lastFs.tilesDrawn) + ", without cache " +
	      S(r.lastFs.tilesWithoutCache) + ")";
	return false;
}

// The art types at the field, the bake's setMap gets the same array, the renderer's tiles take the rule's style, and
// in play (followArtChanges off, the EXE's behaviour) a changed art type and a new table are not taken.
bool CaseTileStyles(std::string& why)
{
	FakeFeed feed;
	std::vector<uint8_t> art((size_t)kPlots * kPlots, 3);   // every plot Africa
	feed.table(art);
	Rig r;
	RigOpt ro;
	ro.tiles = SceneOptions::kTilesBake;
	ro.feed = &feed;
	if(!r.start(ro, why))
		return false;
	ResidencySource* res = dynamic_cast<ResidencySource*>(r.scene.source());
	CHECK(res && res->ok() && res->fieldReady(), "no residency with a field");
	const uint8_t* a = nullptr;
	uint32_t ax = 0, ay = 0;
	bool wrap = false;
	CHECK(res->artTypes(a, ax, ay, wrap), "no art types after the field");
	CHECK(ax == kPlots && ay == kPlots && wrap, "art types " + S(ax) + "x" + S(ay) + " wrap " + S(wrap) + " (want 4x4, wrap)");
	CHECK(std::vector<uint8_t>(a, a + ax * ay) == art, "the art types are not the feed's");
	CHECK(res->bakeMapArt == a && res->bakeMapPlotsX == kPlots && res->bakeMapPlotsY == kPlots,
	      "the bake's setMap got another array (" + S(res->bakeMapPlotsX) + "x" + S(res->bakeMapPlotsY) + ")");
	CHECK(res->artFills == 1, "art fills " + S(res->artFills));
	if(!StylesAre(*res, art, why))
		return false;
	CHECK(CountLog("residency: tile styles of load seq 1: art types 4x4 from the feed's table") == 1, "no tile-style line");
	uint32_t n[5] = {};
	if(!DrawStyled(r, n, why))
		return false;
	CHECK(n[3] > 0 && n[1] == 0 && n[2] == 0 && n[4] == 0,
	      "drawn tiles per t6 style " + S(n[0]) + "/" + S(n[1]) + "/" + S(n[2]) + "/" + S(n[3]) + "/" + S(n[4]) + " (want Africa and Generic only)");
	// in play: plot 5 turns Europe (a farm) -> not taken, counted
	feed.rec[5].artType = 4;
	const uint32_t idx = 5;
	r.scene.plotsChanged(&idx, 1);
	CHECK(res->artIgnored == 1 && res->artPlotChanges == 0 && res->styleRaises == 0, "in play: ignored " + S(res->artIgnored) +
	      ", taken " + S(res->artPlotChanges) + ", raises " + S(res->styleRaises));
	CHECK(a[5] == 3 && !res->takeStylesChanged(), "in play: the art type was taken");
	// a new table in play (a dropped queue): not taken either
	feed.table({1});
	r.scene.plotsChanged(&idx, 1);
	CHECK(a[0] == 3 && res->artFills == 1 && res->styleRaises == 0, "a new table in play was taken");
	if(!StylesAre(*res, art, why))
		return false;
	return true;
}

// followArtChanges on (tests): a changed art type is taken, the tiles whose style changed re-baked, the renderer's
// styles set again by the next poll (and the frame redrawn).
bool CaseTileStylesFollow(std::string& why)
{
	FakeFeed feed;
	std::vector<uint8_t> art((size_t)kPlots * kPlots, 0);
	feed.table(art);
	Rig r;
	RigOpt ro;
	ro.tiles = SceneOptions::kTilesBake;
	ro.feed = &feed;
	ro.followArt = true;
	if(!r.start(ro, why))
		return false;
	ResidencySource* res = dynamic_cast<ResidencySource*>(r.scene.source());
	CHECK(res && res->fieldReady(), "no residency with a field");
	uint32_t n[5] = {};
	if(!DrawStyled(r, n, why))
		return false;
	CHECK(n[1] + n[2] + n[3] + n[4] == 0, "a styled tile on an all-Generic map");
	for(uint32_t i = 0; i < kPlots * kPlots; ++i)
		feed.rec[i].artType = 2;   // every plot Asia
	std::vector<uint32_t> all;
	for(uint32_t i = 0; i < kPlots * kPlots; ++i)
		all.push_back(i);
	r.scene.plotsChanged(all.data(), all.size());
	art.assign(art.size(), 2);
	CHECK(res->artPlotChanges == (int64_t)all.size() && res->styleRaises > 0, "taken " + S(res->artPlotChanges) + ", raises " +
	      S(res->styleRaises));
	if(!StylesAre(*res, art, why))
		return false;
	if(!DrawStyled(r, n, why))   // the poll sets the renderer's styles; the raised tiles bake again
		return false;
	CHECK(n[2] > 0 && n[1] == 0 && n[3] == 0 && n[4] == 0, "after the change: drawn per style " + S(n[0]) + "/" + S(n[1]) + "/" +
	      S(n[2]) + "/" + S(n[3]) + "/" + S(n[4]) + " (want Asia and Generic)");
	CHECK(!res->takeStylesChanged(), "the poll did not take the styles");
	return true;
}

// No table at the field (the snapshot comes later): all Generic; the first table of the map's size fills the art types
// and re-bakes the styled tiles; a table of another size is not taken (negative).
bool CaseTileStylesLateTable(std::string& why)
{
	FakeFeed feed;   // no table
	Rig r;
	RigOpt ro;
	ro.tiles = SceneOptions::kTilesBake;
	ro.feed = &feed;
	if(!r.start(ro, why))
		return false;
	ResidencySource* res = dynamic_cast<ResidencySource*>(r.scene.source());
	CHECK(res && res->fieldReady(), "no residency with a field");
	const uint8_t* a = nullptr;
	uint32_t ax = 0, ay = 0;
	bool wrap = false;
	CHECK(res->artTypes(a, ax, ay, wrap) && ax == kPlots && ay == kPlots, "no art array at the field");
	for(uint32_t i = 0; i < ax * ay; ++i)
		CHECK(a[i] == 0, "art type at plot " + S(i) + " without a table");
	CHECK(res->artFills == 0 && CountLog("from none yet (all Generic)") == 1, "fills " + S(res->artFills));
	CHECK(res->bakeMapArt == a, "the bake has no art array");
	uint32_t n[5] = {};
	if(!DrawStyled(r, n, why))
		return false;
	// negative: a table of another size
	feed.table({4}, kPlots + 1, kPlots);
	const uint32_t idx = 1;
	r.scene.plotsChanged(&idx, 1);
	CHECK(res->artFills == 0 && a[1] == 0, "a 5x4 table was taken for a 4x4 map");
	// the table of the map's size
	std::vector<uint8_t> art((size_t)kPlots * kPlots, 0);
	art[1 * kPlots + 2] = 1;   // plot (2, 1) America
	art[2 * kPlots + 1] = 4;   // plot (1, 2) Europe
	feed.table(art);
	r.scene.plotsChanged(&idx, 1);
	CHECK(res->artFills == 1 && a[6] == 1 && a[9] == 4, "the late table was not taken");
	CHECK(res->styleRaises > 0, "no tile re-baked for its new style");
	if(!StylesAre(*res, art, why))
		return false;
	if(!DrawStyled(r, n, why))
		return false;
	CHECK(n[1] > 0 && n[4] > 0, "drawn per style " + S(n[0]) + "/" + S(n[1]) + "/" + S(n[2]) + "/" + S(n[3]) + "/" + S(n[4]) +
	      " (want America and Europe)");
	CHECK(!res->takeStylesChanged(), "the poll did not take the styles");
	return true;
}

// ---- ticket 0424 (R1): the visible set of a frame the fog holds ---------------------------------------------------------
// The fog holds every frame until its plot table arrives (no table here at the start); meanwhile Scene::idle has the
// residency bake the held camera's visible set (ResidencySource::prepareHeld), so the first frame drawn after the install
// bakes nothing. `arm`: 0 = the rule (the residency's art types from a table at the field), 1 = C5_RENDER_HELD_BAKE=0 (off:
// the first drawn frame pays the bakes, the behaviour before 0424), 2 = no table for the art types at the field (all
// Generic): no preparation until the table's styles are in, then one at the next held frame. Every arm counts its log
// lines; the idle calls get budget 0, so no bake-ahead bakes anything behind the preparation's back.
int LogNum(const char* needle, const char* key)
{
	for(const auto& l : g_log)
	{
		const size_t p = l.find(needle);
		if(p == std::string::npos)
			continue;
		const size_t k = l.find(key, p);
		if(k == std::string::npos)
			return -1;
		return atoi(l.c_str() + k + strlen(key));
	}
	return -1;
}

bool HeldRun(int arm, std::string& why)
{
	FakeFeed feed;
	std::vector<uint8_t> art((size_t)kPlots * kPlots, 3);   // every plot Africa
	if(arm != 2)
		feed.table(art);
	if(arm == 1)
		SetEnvironmentVariableA("C5_RENDER_HELD_BAKE", "0");
	Rig r;
	RigOpt ro;
	ro.tiles = SceneOptions::kTilesBake;
	ro.feed = &feed;
	ro.bits.clear();   // no plot table for the fog: it holds the frames
	ro.waitMs = 60000;
	const bool started = r.start(ro, why);
	if(arm == 1)
		SetEnvironmentVariableA("C5_RENDER_HELD_BAKE", nullptr);
	if(!started)
		return false;
	ResidencySource* res = dynamic_cast<ResidencySource*>(r.scene.source());
	CHECK(res && res->fieldReady(), "no residency with a field");
	CHECK(CountLog(arm == 1 ? "held frames: off (C5_RENDER_HELD_BAKE=0)" : "held frames: on") == 1, "no option line");
	const int64_t bakes0 = res->stats()->bakes;
	// three held frames, each followed by an idle pass
	for(int k = 0; k < 3; ++k)
	{
		bool ok = false;
		CHECK(!r.frame(ok), "a frame was drawn without the fog's table (k=" + S(k) + ")");
		r.scene.idle(g_ctx, 0.0);
	}
	CHECK(r.scene.fow().state() == FowScene::kWaiting && r.scene.fow().framesHeld == 3, "fog state " +
	      S(r.scene.fow().state()) + ", held " + S(r.scene.fow().framesHeld));
	const int64_t heldBakes = res->stats()->bakes - bakes0;
	if(arm == 0)
	{
		CHECK(CountLog("(the fog holds it): the visible set prepared") == 3 && heldBakes > 0 && res->heldPrepares == 3,
		      "prepares " + S(res->heldPrepares) + ", lines " + S(CountLog("the visible set prepared")) + ", bakes " + S(heldBakes));
		CHECK(res->heldBakes == heldBakes, "held bakes " + S(res->heldBakes) + " of " + S(heldBakes));
		CHECK(LogNum("the visible set prepared", "prepared before the draw: ") > 0, "the first preparation baked nothing");
	}
	else
	{
		CHECK(CountLog("the visible set prepared") == 0 && heldBakes == 0 && res->heldPrepares == 0,
		      "arm " + S(arm) + ": prepares " + S(res->heldPrepares) + ", bakes " + S(heldBakes));
		if(arm == 2)
		{
			CHECK(CountLog("waits for the plot table's tile styles") == 1, "no wait line (once)");
			// the table's art types arrive (the residency takes them at the drain), then one more held frame
			feed.table(art);
			const uint32_t idx = 1;
			r.scene.plotsChanged(&idx, 1);
			CHECK(res->artFills == 1, "the table was not taken");
			bool ok = false;
			CHECK(!r.frame(ok), "a frame was drawn without the fog's table");
			r.scene.idle(g_ctx, 0.0);
			CHECK(res->heldPrepares == 1 && CountLog("the visible set prepared") == 1, "no preparation after the table: " +
			      S(res->heldPrepares));
		}
	}
	// the fog's table arrives: the frame of the same camera is drawn; the first drawn frame's line
	r.table->snapshot({3});
	r.batch({});
	CHECK(r.drawn(false), "no frame drawn after the fog's table");
	CHECK(CountLog("fog: installed at frame") == 1, "the fog did not install");
	CHECK(CountLog("residency: first drawn frame of load 1") == 1, "no first-drawn line");
	const int drawnBakes = LogNum("residency: first drawn frame of load 1", "): ");
	if(arm == 1)
		CHECK(drawnBakes > 0, "the first drawn frame baked nothing with the preparation off (" + S(drawnBakes) + ")");
	else
		CHECK(drawnBakes == 0, "the first drawn frame baked " + S(drawnBakes) + " tile(s) after its preparation");
	// later draws: no second first-drawn line, no preparation (nothing held)
	const int64_t prep = res->heldPrepares;
	for(int k = 0; k < 3; ++k)
	{
		CHECK(r.drawn(), "a later frame was not drawn");
		r.scene.idle(g_ctx, 0.0);
	}
	CHECK(CountLog("residency: first drawn frame of load 1") == 1 && res->heldPrepares == prep, "after the install: prepares " +
	      S(res->heldPrepares) + " (was " + S(prep) + ")");
	CHECK(CountLog("LOUD: residency") == 0, "LOUD residency lines " + S(CountLog("LOUD: residency")));
	return true;
}

// Live r1b-run1 (2026-11-18): a held camera that waited for the table's styles was prepared after a later frame had been
// drawn (a stall kept the render thread away until the fog could install). Here: no art table, one held frame noted and
// left pending (its idle pass waits for the styles), then the art table and the fog's table arrive and a frame is drawn
// before any idle pass: that draw bakes its own set, the pending camera is dropped (heldDropped 1), and the idle pass
// after it prepares nothing.
bool CaseHeldFrameStaleDropped(std::string& why)
{
	FakeFeed feed;
	std::vector<uint8_t> art((size_t)kPlots * kPlots, 3);
	Rig r;
	RigOpt ro;
	ro.tiles = SceneOptions::kTilesBake;
	ro.feed = &feed;
	ro.bits.clear();
	ro.waitMs = 60000;
	if(!r.start(ro, why))
		return false;
	ResidencySource* res = dynamic_cast<ResidencySource*>(r.scene.source());
	CHECK(res && res->fieldReady(), "no residency with a field");
	bool ok = false;
	CHECK(!r.frame(ok), "a frame was drawn without the fog's table");
	r.scene.idle(g_ctx, 0.0);
	CHECK(CountLog("waits for the plot table's tile styles") == 1 && res->heldPrepares == 0, "no wait, or a preparation");
	feed.table(art);
	const uint32_t idx = 1;
	r.scene.plotsChanged(&idx, 1);
	r.table->snapshot({3});
	r.batch({});
	CHECK(r.drawn(), "no frame drawn after both tables");
	CHECK(res->heldDropped == 1, "held cameras dropped " + S(res->heldDropped));
	CHECK(LogNum("residency: first drawn frame of load 1", "): ") > 0, "the drawn frame baked nothing (no preparation ran)");
	r.scene.idle(g_ctx, 0.0);
	CHECK(res->heldPrepares == 0 && CountLog("the visible set prepared") == 0, "the stale camera was prepared after the draw");
	return true;
}

bool CaseHeldFramePrepares(std::string& why) { return HeldRun(0, why); }
bool CaseHeldFrameOff(std::string& why) { return HeldRun(1, why); }
bool CaseHeldFrameWaitsForStyles(std::string& why) { return HeldRun(2, why); }

// ---- ticket 0340: the FOWLevel-3 particles ------------------------------------------------------------------------------
// The real modules (C5R_EXE, C5R_GAME_DIR): at FOWLevel 3 the particles start, spawn once for the field's world, draw a
// sprite list each frame after the cloud planes (counted) with no error and no LOUD line, on the cloud pass's depth copy
// (the scene's depth keeps no zero); with unexplored plots the picture differs from the same frames without particles
// (C5_RENDER_FOW_PARTICLES=0), and that picture equals FOWLevel 2's (FOWLevel 0-2 unchanged: the particles are the only
// difference); two rigs with the same seed draw byte-equal pictures over 8 frames; the state round-trips; the offline
// player's clock seed turns them off.
bool ParticleRun(int level, bool partEnv, int frames, std::vector<uint8_t>& px, Rig& r, std::string& why)
{
	RigOpt ro;
	ro.fakeFow = false;
	ro.bits.assign(kPlots * kPlots, 3);
	for(uint32_t i = 0; i < kPlots * kPlots; i += 2)
		ro.bits[i] = 0;
	if(!r.start(ro, why))
		return false;
	SetEnvironmentVariableW(L"C5_RENDER_FOW_PARTICLES", partEnv ? nullptr : L"0");
	FowSceneOptions fo;
	fo.on = true;
	fo.level = level;
	fo.exe = g_exe;
	fo.gameDir = GameDir();
	fo.installWaitMs = 300;
	fo.freeze = true;
	r.scene.fow().init(g_dev, fo);
	SetEnvironmentVariableW(L"C5_RENDER_FOW_PARTICLES", nullptr);
	for(int i = 0; i < frames; ++i)
		CHECK(r.drawn(true), "level " + S(level) + " frame " + S(i) + " not drawn");
	px = r.pixels();
	CHECK(frames == 0 || r.depthZeros() == 0, "the particles or clouds wrote into the scene's depth");
	return true;
}

bool CaseParticles(std::string& why)
{
	std::vector<uint8_t> on, off, lvl2, again;
	{
		Rig r;
		if(!ParticleRun(3, true, 8, on, r, why))
			return false;
		FowScene& f = r.scene.fow();
		const cf::ParticleStats& ps = f.lastParticleStats();
		CHECK(f.particlesOn() && f.particleResets == 1 && f.particleFrames == 8 && f.particleErrors == 0,
		      "particles: on " + S(f.particlesOn()) + " resets " + S(f.particleResets) + " frames " + S(f.particleFrames) +
		          " errors " + S(f.particleErrors));
		CHECK(ps.sprites > 0 && ps.particles > 0 && ps.steps == 7,   // the 8th step runs on its worker thread after the 8th draw
		      "sprites " + S(ps.sprites) + " particles " + S(ps.particles) + " steps " + S(ps.steps));
		CHECK(CountLog("LOUD") == 0, "a LOUD line");
		CHECK(CountLog("particles on (seed 1)") == 1 && CountLog("particles spawned for a") == 1, "the start and spawn notes");
		char line[1024];
		f.secondLine(line, sizeof(line));
		CHECK(strstr(line, " particles=") != nullptr, std::string("the second line has no particles: ") + line);
		cf::ParticleSim* sim = f.particleSim();
		CHECK(sim && sim->ready(), "no sim");
		if(sim)
		{
			const std::vector<uint8_t> st = sim->saveState();
			CHECK(sim->loadState(st.data(), st.size()).ok() && sim->saveState() == st, "state round trip");
		}
		FowClockState cs;
		CHECK(f.setClockState(cs) && !f.particlesOn(), "the player's clock seed left the particles on");
		const int64_t before = f.particleFrames;
		CHECK(r.drawn(true) && f.particleFrames == before, "particles drawn after the player's seed");
		g_log.clear();
	}
	{
		Rig r;
		if(!ParticleRun(3, true, 8, again, r, why))
			return false;
	}
	g_log.clear();
	{
		Rig r;
		if(!ParticleRun(3, false, 8, off, r, why))
			return false;
		CHECK(CountLog("particles off (C5_RENDER_FOW_PARTICLES=0)") == 1, "the switch's note");
	}
	g_log.clear();
	{
		Rig r;
		if(!ParticleRun(2, true, 8, lvl2, r, why))
			return false;
		CHECK(CountLog("particles off (FOWLevel below 3)") == 1, "the level 2 note");
	}
	CHECK(!on.empty() && on != off, "the particles changed nothing in the frame");
	CHECK(on == again, "two runs with the same seed differ");
	CHECK(off == lvl2, "FOWLevel 3 without particles differs from FOWLevel 2");
	return true;
}

// ---- ticket 0403 ------------------------------------------------------------------------------------------------------
// particles_sequence: the live particles over 24 frames with a seed of 7 (C5_RENDER_FOW_PARTICLE_SEED set through the
// whole run), one line per frame "SEQ <k> <SHA-256 of the frame's pixels> <SHA-256 of ParticleSim::saveState>" and a
// final "SEQEND". Uses only the API main had before 0403, so that the same case built with main's srv_fow_scene.{h,cpp}
// gives main's sequence: server/tests/render/scenebundle/test_particles_unchanged.py compares the two (MC #3092: the
// spawn seed read once at init leaves the live particles byte-identical).
bool CaseParticlesSequence(std::string& why)
{
	SetEnvironmentVariableW(L"C5_RENDER_FOW_PARTICLE_SEED", L"7");
	struct SeedScope
	{
		~SeedScope() { SetEnvironmentVariableW(L"C5_RENDER_FOW_PARTICLE_SEED", nullptr); }
	} seedScope;
	Rig r;
	std::vector<uint8_t> px;
	if(!ParticleRun(3, true, 0, px, r, why))
		return false;
	FowScene& f = r.scene.fow();
	CHECK(f.particlesOn(), "particles not on");
	for(int k = 0; k < 24; ++k)
	{
		CHECK(r.drawn(true), "frame " + S(k) + " not drawn");
		px = r.pixels();
		cf::ParticleSim* sim = f.particleSim();
		CHECK(sim && sim->ready(), "no sim after frame " + S(k));
		const std::vector<uint8_t> st = sim->saveState();
		printf("SEQ %d %s %s\n", k, scenebundle::Sha256Hex(px.data(), px.size()).c_str(), scenebundle::Sha256Hex(st.data(), st.size()).c_str());
	}
	CHECK(f.particleResets == 1 && f.particleFrames == 24 && f.particleErrors == 0,
	      "resets " + S(f.particleResets) + " frames " + S(f.particleFrames) + " errors " + S(f.particleErrors));
	CHECK(CountLog("seed 7") >= 2, "the seed 7 was not used (start and spawn notes)");
	printf("SEQEND\n");
	return true;
}

#ifdef SRV_FOW_PARTICLE_STATE
// particle_state: the recorder's and the player's calls (FowScene::particleState / setParticleState). A live rig draws 8
// frames, its state is taken (spawned), then frames 9-12 are kept; a fresh rig (the player) seeded with the clock and
// that state draws the same four frames byte-equal; the same rig without the state (clock seed only) differs. A state
// taken before the first frame (not spawned) replays the live first frames. Negatives: particleState with the particles
// off (FOWLevel 2, the switch, after the clock seed), setParticleState at FOWLevel 2 or with a damaged state (false, the
// particles left off, nothing drawn).
bool CaseParticleState(std::string& why)
{
	std::vector<std::vector<uint8_t>> live;
	FowParticleState ps, ps0;
	std::string pw;
	{
		Rig r;
		std::vector<uint8_t> px;
		if(!ParticleRun(3, true, 0, px, r, why))
			return false;
		FowScene& f = r.scene.fow();
		CHECK(f.particleState(ps0, pw) && !ps0.spawned && ps0.sim.empty() && ps0.seed == 1, "the state before the first frame: " + pw);
		for(int k = 0; k < 4; ++k)
		{
			CHECK(r.drawn(true), "live frame " + S(k));
			live.push_back(r.pixels());
		}
		for(int k = 4; k < 8; ++k)
			CHECK(r.drawn(true), "live frame " + S(k));
		CHECK(f.particleState(ps, pw) && ps.spawned && !ps.sim.empty() && ps.worldW > 0 && ps.worldH > 0, "the state after 8 frames: " + pw);
		const cf::ParticleSim* sim = f.particleSim();
		CHECK(sim && ps.sim == sim->saveState(), "particleState is not saveState");
		for(int k = 8; k < 12; ++k)
		{
			CHECK(r.drawn(true), "live frame " + S(k));
			live.push_back(r.pixels());
		}
		FowClockState cs = f.clockState();
		CHECK(f.setClockState(cs), "clock seed");
		FowParticleState no;
		CHECK(!f.particleState(no, pw) && pw == "the offline player", "particleState after the clock seed: '" + pw + "'");
	}
	// the player: spawned state after 8 frames -> frames 9-12 byte-equal
	for(int variant = 0; variant < 3; ++variant)   // 0 the state, 1 no state (the clock seed only), 2 not spawned from frame 0
	{
		Rig r;
		std::vector<uint8_t> px;
		if(!ParticleRun(3, true, 0, px, r, why))
			return false;
		FowScene& f = r.scene.fow();
		FowClockState cs = f.clockState();
		CHECK(f.setClockState(cs) && !f.particlesOn(), "v" + S(variant) + ": the clock seed left the particles on");
		if(variant == 0)
			CHECK(f.setParticleState(ps, pw) && f.particlesOn(), "v0: setParticleState: " + pw);
		if(variant == 2)
			CHECK(f.setParticleState(ps0, pw) && f.particlesOn(), "v2: setParticleState (not spawned): " + pw);
		r.frameNo = variant == 2 ? 100 : 108;   // the live run's cameras (Rig::frameNo starts at 100)
		int equal = 0;
		for(int k = 0; k < 4; ++k)
		{
			CHECK(r.drawn(true), "v" + S(variant) + " frame " + S(k));
			equal += r.pixels() == live[variant == 2 ? k : 4 + k] ? 1 : 0;
		}
		if(variant == 1)
			CHECK(equal == 0 && f.particleFrames == 0, "v1: without the state " + S(equal) + " of 4 frames equal the live ones (particle frames " +
			                                              S(f.particleFrames) + ")");
		else
			CHECK(equal == 4 && f.particleFrames == 4 && f.particleErrors == 0 && f.particleResets == (variant == 2 ? 1 : 0),
			      "v" + S(variant) + ": " + S(equal) + " of 4 frames equal the live ones; particle frames " + S(f.particleFrames) + ", resets " +
			          S(f.particleResets) + ", errors " + S(f.particleErrors));
	}
	// negatives
	{
		Rig r;
		std::vector<uint8_t> px;
		if(!ParticleRun(2, true, 0, px, r, why))
			return false;
		FowScene& f = r.scene.fow();
		FowParticleState no;
		CHECK(!f.particleState(no, pw) && pw == "FOWLevel below 3", "level 2 particleState: '" + pw + "'");
		CHECK(!f.setParticleState(ps, pw) && !f.particlesOn() && pw.find("FOWLevel below 3") == 0, "level 2 setParticleState: '" + pw + "'");
	}
	{
		Rig r;
		std::vector<uint8_t> px;
		if(!ParticleRun(3, false, 0, px, r, why))
			return false;
		FowScene& f = r.scene.fow();
		FowParticleState no;
		CHECK(!f.particleState(no, pw), "the switch off: particleState gave a state");
		CHECK(!f.setParticleState(ps, pw) && !f.particlesOn(), "the switch off: setParticleState took the state");
	}
	for(int k = 0; k < 3; ++k)   // 0 a damaged magic, 1 a cut state, 2 a world not finite
	{
		Rig r;
		std::vector<uint8_t> px;
		if(!ParticleRun(3, true, 0, px, r, why))
			return false;
		FowScene& f = r.scene.fow();
		FowClockState cs = f.clockState();
		CHECK(f.setClockState(cs), "clock seed");
		FowParticleState bad = ps;
		if(k == 0)
			bad.sim[0] = 'X';
		else if(k == 1)
			bad.sim.resize(bad.sim.size() - 3);
		else
			bad.worldW = std::nanf("");
		CHECK(!f.setParticleState(bad, pw) && !f.particlesOn(), "damaged state " + S(k) + " taken");
		CHECK(k != 0 || pw.find("BadMagic") != std::string::npos, "the magic's reason: '" + pw + "'");
		CHECK(k != 1 || pw.find("Truncated") != std::string::npos, "the cut state's reason: '" + pw + "'");
		CHECK(r.drawn(true) && f.particleFrames == 0, "particles drawn after a refused state");
	}
	return true;
}
#endif

// The fakes: a module set without particles (Unsupported) runs as at FOWLevel 2 without a LOUD line; a particle module
// that cannot start is LOUD once and the fog is drawn without particles, never an install failure (stock falls back to
// FOWLevel 2 when its particle files are missing, fog-of-war.md 10.1).
bool CaseParticlesFail(std::string& why)
{
	for(int k = 0; k < 2; ++k)
	{
		Rig r;
		RigOpt ro;
		ro.bits.assign(kPlots * kPlots, 3);
		ro.bits[0] = 0;
		if(!r.start(ro, why))
			return false;
		if(k == 1)
			r.fow->partInit = c5r::Status::fail(c5r::Err::NotFound, "fx_fog_dif.dds not readable");
		FowSceneOptions fo;
		fo.on = true;
		fo.level = 3;
		fo.installWaitMs = 300;
		fo.freeze = true;
		g_log.clear();
		r.scene.fow().init(g_dev, fo);
		CHECK(r.drawn(true) && r.drawn(true), "k=" + S(k) + ": frames not drawn");
		FowScene& f = r.scene.fow();
		CHECK(f.state() == FowScene::kActive && !f.particlesOn() && r.fow->partDraws == 0 && f.cloudDraws == 2,
		      "k=" + S(k) + ": state " + FowScene::StateName(f.state()) + " particles " + S(f.particlesOn()) + " clouds " +
		          S(f.cloudDraws));
		CHECK(CountLog("LOUD") == (k == 1 ? 1 : 0), "k=" + S(k) + ": LOUD lines " + S(CountLog("LOUD")));
		CHECK(k == 0 || CountLog("drawn as FOWLevel 2") >= 1, "the fallback's note");
	}
	return true;
}

// ---- ticket 0674: the mask's window made from the camera (pan-coherence.md 3) ----------------------------------------

// render_fow_mask_window for one case: set before Scene::init (Scene reads it there), back to the default after
struct MaskWindowOption
{
	explicit MaskWindowOption(const char* v)
	{
		std::string w;
		ParseRenderFowMaskWindow(v, w);
	}
	~MaskWindowOption()
	{
		std::string w;
		ParseRenderFowMaskWindow("", w);
	}
};

// mip `mip` of a texture view as RGBA8 rows (out empty on failure)
std::vector<uint8_t> ReadMip(ID3D11ShaderResourceView* srv, uint32_t mip, uint32_t& w, uint32_t& h)
{
	std::vector<uint8_t> out;
	w = h = 0;
	if(!srv)
		return out;
	ID3D11Resource* r = nullptr;
	srv->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	if(r)
		r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t);
	SafeRelease(r);
	if(!t)
		return out;
	D3D11_TEXTURE2D_DESC d;
	t->GetDesc(&d);
	D3D11_TEXTURE2D_DESC s = d;
	s.Width = std::max(1u, d.Width >> mip);
	s.Height = std::max(1u, d.Height >> mip);
	s.MipLevels = s.ArraySize = 1;
	s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	s.Usage = D3D11_USAGE_STAGING;
	s.BindFlags = 0;
	s.MiscFlags = 0;
	s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* st = nullptr;
	if(SUCCEEDED(g_dev->CreateTexture2D(&s, nullptr, &st)))
	{
		g_ctx->CopySubresourceRegion(st, 0, 0, 0, 0, t, mip, nullptr);
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			w = s.Width;
			h = s.Height;
			out.resize((size_t)w * h * 4);
			for(uint32_t y = 0; y < h; ++y)
				memcpy(&out[(size_t)y * w * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w * 4);
			g_ctx->Unmap(st, 0);
		}
		st->Release();
	}
	t->Release();
	return out;
}

const uint8_t* TexelAt(const std::vector<uint8_t>& px, uint32_t w, uint32_t h, double u, double v)
{
	static const uint8_t none[4] = {77, 77, 77, 77};
	const int x = (int)std::floor(u * w), y = (int)std::floor(v * h);
	if(px.empty() || x < 0 || y < 0 || x >= (int)w || y >= (int)h)
		return none;
	return &px[((size_t)y * w + (size_t)x) * 4];
}

void UvOf(const float op[16], double x, double y, double& u, double& v)
{
	u = 0.5 * (x * op[0] + y * op[4] + op[12]) + 0.5;
	v = 0.5 - 0.5 * (x * op[1] + y * op[5] + op[13]);
}

// a screen point (NDC) of a row-vector view-projection to the ground z = 0
// the inverse of a 4 x 4 (Gauss-Jordan with pivoting, double); false when singular
bool Inv4(const float m[16], double inv[16])
{
	double a[4][8];
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 8; ++c)
			a[r][c] = c < 4 ? (double)m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	for(int c = 0; c < 4; ++c)
	{
		int piv = c;
		for(int r = c + 1; r < 4; ++r)
			if(std::fabs(a[r][c]) > std::fabs(a[piv][c]))
				piv = r;
		if(std::fabs(a[piv][c]) < 1e-15)
			return false;
		for(int k = 0; k < 8; ++k)
			std::swap(a[c][k], a[piv][k]);
		const double d = a[c][c];
		for(int k = 0; k < 8; ++k)
			a[c][k] /= d;
		for(int r = 0; r < 4; ++r)
			if(r != c)
			{
				const double f = a[r][c];
				for(int k = 0; k < 8; ++k)
					a[r][k] -= f * a[c][k];
			}
	}
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
			inv[r * 4 + c] = a[r][c + 4];
	return true;
}

bool ToGround(const float vp[16], double nx, double ny, double& x, double& y)
{
	double inv[16];
	if(!Inv4(vp, inv))
		return false;
	double p[2][3];
	for(int k = 0; k < 2; ++k)
	{
		const double in[4] = {nx, ny, (double)k, 1.0};
		double o[4] = {0, 0, 0, 0};
		for(int j = 0; j < 4; ++j)
			for(int i = 0; i < 4; ++i)
				o[j] += in[i] * inv[i * 4 + j];
		if(std::fabs(o[3]) < 1e-12)
			return false;
		for(int j = 0; j < 3; ++j)
			p[k][j] = o[j] / o[3];
	}
	const double t = -p[0][2] / (p[1][2] - p[0][2]);
	x = p[0][0] + t * (p[1][0] - p[0][0]);
	y = p[0][1] + t * (p[1][1] - p[0][1]);
	return std::isfinite(x) && std::isfinite(y);
}

// The FowModules with the real FOW mask (the EXE's FOWDrawMask programs, C5R_EXE) and no clouds
struct RealMaskFow : FowModules
{
	cf::MaskRenderer mr;
	ID3D11ShaderResourceView* cloudSrv = nullptr;
	~RealMaskFow() override { SafeRelease(cloudSrv); }
	c5r::Status initMask(ID3D11Device* dev, const std::wstring& exe, uint32_t size) override { return mr.init(dev, exe, size); }
	c5r::Status initClouds(ID3D11Device*, const std::wstring&, const std::wstring&) override
	{
		if(!cloudSrv)
			cloudSrv = Texture(0xFF808080u);
		return c5r::Status::Ok();
	}
	c5r::Status buildMask(ID3D11DeviceContext* ctx, const cf::MaskInput& in, cf::MaskStats* st) override { return mr.build(ctx, in, st); }
	c5r::Status drawClouds(ID3D11DeviceContext*, const cf::CloudInput&, cf::CloudStats*) override { return c5r::Status::Ok(); }
	ID3D11ShaderResourceView* mask() const override { return mr.srv(); }
	ID3D11ShaderResourceView* cloudBase() const override { return cloudSrv; }
	void release() override { SafeRelease(cloudSrv); }
};

// stock's Tiny camera 1 (render/tests/fow/mask_test.cpp, 3951's capture) and its window [-1600, -768] x [-64, 640]
const float kCam1[16] = {2.135113f, 0, 0, 0, 0, 2.73274922f, 0.716057539f, 0.707106829f,
                         0, 2.73274922f, -0.716057539f, -0.707106829f, 2458.02808f, -792.057678f, 349.420624f, 395.052856f};

// One frame of a FowScene alone on the Tiny map (56 x 36, every plot seen, nominal placement, the world box of the Tiny
// map): the pair's window `pairProj`, camera N `vp` (view 0 = vp, projection = identity). Out: the mask's mip 0, the
// terrain inputs, the camera-window frames and the fog 1s line.
struct AloneOut
{
	std::vector<uint8_t> mask;
	uint32_t w = 0, h = 0;
	FowTerrainInputs ti;
	int64_t camwin = 0;
	std::string line;
	cf::MaskStats stats;
};
bool FowAlone(bool camera, const float vp[16], const float pairProj[16], AloneOut& out, std::string& why)
{
	FakeTable* t = new FakeTable();
	t->snapshot({1}, 56, 36);
	FowScene fs(std::unique_ptr<FowModules>(new RealMaskFow()), std::unique_ptr<FowPlotTable>(t));
	FowSceneOptions o;
	o.on = true;
	o.overlay = true;
	o.level = 2;   // a 1024 mask, no particles
	o.exe = g_exe;
	o.freeze = true;
	o.maskWindowCamera = camera;
	fs.init(g_dev, o);
	VPShellCamera cam = {};
	cam.status = VPCAM_OK;
	cam.numViews = 1;
	cam.frameStamp = 1;
	memcpy(cam.view[0], vp, 64);
	const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	memcpy(cam.proj, id, 64);
	cam.wrapX = 1;
	cam.wrapPeriod = 3583.0f;
	cam.aabbMin[0] = -1791.5f;
	cam.aabbMax[0] = 1791.5f;
	cam.aabbMin[1] = -1108.047f;
	cam.aabbMax[1] = 1108.047f;
	OverlayFrame pair;
	pair.numViews = 1;
	pair.generation = 1;
	pair.slot = 0;
	memcpy(pair.overlayProj[0], pairProj, 64);
	const OverlayTapReader::State st = OverlayTapReader::State::Active;
	if(!fs.admit(cam, 56, 40, &st, &pair, 1000.0))
	{
		why = "admit refused the frame (state " + std::string(FowScene::StateName(fs.state())) + ")";
		return false;
	}
	ID3D11ShaderResourceView* m = fs.beforeWorld(g_ctx, cam, &pair, 1000.0, nullptr);
	if(!m)
	{
		why = "no mask";
		return false;
	}
	out.ti = fs.terrainInputs();
	out.camwin = fs.cameraWindowFrames;
	out.stats = fs.lastMaskStats();
	out.mask = ReadMip(m, 0, out.w, out.h);
	char l[640];
	fs.secondLine(l, sizeof(l));
	out.line = l;
	if(out.mask.empty())
	{
		why = "mask read-back";
		return false;
	}
	return true;
}

// A zoom-out step (the reporter's 0674 frame): the pair's window is camera 1's, camera N is camera 1 zoomed out (clip x, y
// x 0.8), every plot seen. With the camera window every ground point of an 8 x 8 grid over the screen (its edges included)
// lies inside the bound window and reads a mask texel that is not zero; with `pair` the edge points fall outside it.
bool CaseZoomOutStep(std::string& why)
{
	float pairProj[16], z[16];
	ExeProj(pairProj, 832, 704, -1600, -64);
	memcpy(z, kCam1, 64);
	for(int r = 0; r < 4; ++r)
	{
		z[r * 4 + 0] *= 0.8f;
		z[r * 4 + 1] *= 0.8f;
	}
	for(int arm = 0; arm < 2; ++arm)
	{
		const bool camera = arm == 0;
		AloneOut a;
		if(!FowAlone(camera, z, pairProj, a, why))
		{
			why = std::string(camera ? "camera: " : "pair: ") + why;
			return false;
		}
		const float* win = a.ti.windowProj[0];
		CHECK(a.ti.on && a.ti.windowViews == 1, "the mask is not on");
		CHECK(a.ti.cameraWindow == camera && a.camwin == (camera ? 1 : 0), "camera window " + S(a.ti.cameraWindow) + " frames " + S(a.camwin));
		CHECK(a.line.find(camera ? "camwin=1" : "camwin=0") != std::string::npos, "the fog 1s line: " + a.line);
		if(camera)
		{
			cf::View v;
			memcpy(v.viewProj, z, 64);
			float want[16];
			CHECK(cf::windowFromCamera(v, want).ok() && memcmp(want, win, 64) == 0, "the bound window is not the camera's");
		}
		else
			CHECK(memcmp(win, pairProj, 64) == 0, "pair: the bound window is not the pair's");
		int outside = 0, zero = 0, points = 0;
		for(int iy = 0; iy < 8; ++iy)
			for(int ix = 0; ix < 8; ++ix)
			{
				double x, y, u, v;
				CHECK(ToGround(z, -1.0 + 2.0 * ix / 7.0, -1.0 + 2.0 * iy / 7.0, x, y), "a screen point misses the ground");
				UvOf(win, x, y, u, v);
				points++;
				if(u < 0 || u > 1 || v < 0 || v > 1)
				{
					outside++;
					continue;
				}
				const uint8_t* p = TexelAt(a.mask, a.w, a.h, u, v);
				if(!(p[0] | p[1] | p[2] | p[3]))
					zero++;
			}
		printf("  %s: %d points, %d outside the window, %d on a zero mask texel; hexagons %u/%u/%u\n", camera ? "camera" : "pair", points, outside,
		       zero, a.stats.hexagons[0], a.stats.hexagons[1], a.stats.hexagons[2]);
		if(camera)
			CHECK(outside == 0 && zero == 0, "camera: " + S(outside) + " points outside, " + S(zero) + " on a zero texel");
		else
			CHECK(outside > 0, "pair: no edge point outside the pair's window (the step is not one that leaves it)");
	}
	return true;
}

// A far jump (0666): camera N is camera 1 moved 1,500 units east, entirely outside the pair's window. The pair's window
// leaves the mask all zero (every hexagon placed outside it: the frame without fog); the camera window holds them.
bool CaseFarJump(std::string& why)
{
	float pairProj[16], j[16];
	ExeProj(pairProj, 832, 704, -1600, -64);
	memcpy(j, kCam1, 64);
	for(int c = 0; c < 4; ++c)
		j[12 + c] -= 1500.0f * j[c];   // Translation(-1500) x vp: the view moved +1500 in x
	int nonzero[2] = {};
	for(int arm = 0; arm < 2; ++arm)
	{
		AloneOut a;
		if(!FowAlone(arm == 0, j, pairProj, a, why))
			return false;
		for(size_t i = 0; i < a.mask.size(); i += 4)
			nonzero[arm] += (a.mask[i] | a.mask[i + 1] | a.mask[i + 2] | a.mask[i + 3]) ? 1 : 0;
		if(arm == 0)
		{
			double x, y, u, v;
			CHECK(ToGround(j, 0, 0, x, y), "the view's centre misses the ground");
			UvOf(a.ti.windowProj[0], x, y, u, v);
			const uint8_t* p = TexelAt(a.mask, a.w, a.h, u, v);
			CHECK(p[0] == 255 && p[3] == 255, "camera: the view centre's texel is not seen");
			CHECK(a.ti.cameraWindow && a.camwin == 1, "camera: no camera window");
		}
	}
	printf("  non-zero mask texels: camera %d, pair %d\n", nonzero[0], nonzero[1]);
	CHECK(nonzero[0] > 10000 && nonzero[1] == 0, "non-zero texels camera " + S(nonzero[0]) + " pair " + S(nonzero[1]));
	return true;
}

// The pair's window holds the view (the rig's default): nothing of 0674 runs. The mask is built and bound with the pair's
// window, the overlay bound is the pair's own texture, no overlay is moved, the counters stay 0; the same with `pair`.
bool CaseAtRestUnchanged(std::string& why)
{
	for(int arm = 0; arm < 2; ++arm)
	{
		MaskWindowOption opt(arm == 0 ? "camera" : "pair");
		Rig r;
		RigOpt ro;
		if(!r.start(ro, why))
			return false;
		for(int i = 0; i < 4; ++i)
		{
			r.src->pair.generation = 2 + i;
			CHECK(r.drawn(), "a frame not drawn");
		}
		const FowTerrainInputs& ti = r.scene.fow().terrainInputs();
		CHECK(r.scene.fow().cameraWindowFrames == 0 && !ti.cameraWindow, "camera window frames " + S(r.scene.fow().cameraWindowFrames));
		CHECK(r.scene.ovlResampled == 0 && r.scene.ovlResampleFails == 0 && r.scene.ovlCamWinNoOverlay == 0, "an overlay was moved");
		CHECK(r.fow->lastIn.numViews == 1 && memcmp(r.fow->lastIn.views[0].overlayProj, r.src->pair.overlayProj[0], 64) == 0,
		      "the mask was not built with the pair's window");
		CHECK(r.scene.boundOverlay().srv == r.ovlTex && r.scene.lastOverlay().srv == r.ovlTex, "the bound overlay is not the pair's");
		CHECK(memcmp(r.scene.boundOverlay().proj, r.scene.lastOverlay().proj, sizeof(r.scene.lastOverlay().proj)) == 0 &&
		          memcmp(r.scene.boundOverlay().proj[1], r.src->pair.overlayProj[0], 64) == 0,
		      "the bound matrices are not the pair's");
		CHECK(r.lastFs.overlayBound && r.lastFs.fowOn, "the terrain drew without the overlay or the fog");
		// the rig's camera footprint lies inside the pair's window (what makes this frame "at rest")
		cf::View v;
		const VPShellCamera* c = VPSHELL_CAMERA(r.m.hdr);
		float vp[16];
		for(int i = 0; i < 4; ++i)
			for(int k = 0; k < 4; ++k)
			{
				double s = 0;
				for(int q = 0; q < 4; ++q)
					s += (double)c->view[0][i * 4 + q] * c->proj[q * 4 + k];
				vp[i * 4 + k] = (float)s;
			}
		memcpy(v.viewProj, vp, 64);
		cf::Footprint fp;
		CHECK(cf::footprintFromCamera(v, fp).ok() && cf::footprintInWindow(fp, r.src->pair.overlayProj[0]), "the rig's view leaves its pair");
	}
	return true;
}

// A texture of n x n with its mips: 8 x 8 blocks (r 30 bx + 20, g 30 by + 20, b 128, a 255), mips by GenerateMips
ID3D11ShaderResourceView* BlockTexture(uint32_t n)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = n;
	d.MipLevels = 0;
	d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	ID3D11Texture2D* t = nullptr;
	ID3D11ShaderResourceView* v = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &t)))
		return nullptr;
	if(FAILED(g_dev->CreateShaderResourceView(t, nullptr, &v)))
	{
		t->Release();
		return nullptr;
	}
	std::vector<uint8_t> px((size_t)n * n * 4);
	const uint32_t b = n / 8;
	for(uint32_t y = 0; y < n; ++y)
		for(uint32_t x = 0; x < n; ++x)
		{
			uint8_t* p = &px[((size_t)y * n + x) * 4];
			p[0] = (uint8_t)(30 * (x / b) + 20);
			p[1] = (uint8_t)(30 * (y / b) + 20);
			p[2] = 128;
			p[3] = 255;
		}
	g_ctx->UpdateSubresource(t, 0, nullptr, px.data(), n * 4, 0);
	g_ctx->GenerateMips(v);
	t->Release();
	return v;
}

// The rig's camera sees ground outside a small pair window ([-32, 32] x [-32, 32] against the view's [-43, 47] x [-32, 32]):
// the mask is built and bound in the camera's window, the overlay of the pair is moved into it (each texel the pair's
// texel at the same world point, transparent in the strip the pair's window does not reach), the recorder's overlay stays
// the pair's; with `pair` nothing of it.
bool CaseCameraWindowMovesOverlay(std::string& why)
{
	ID3D11ShaderResourceView* blocks = BlockTexture(256);
	CHECK(blocks, "the block texture");
	struct Rel
	{
		ID3D11ShaderResourceView* p;
		~Rel() { SafeRelease(p); }
	} rel{blocks};
	for(int arm = 0; arm < 2; ++arm)
	{
		const bool camera = arm == 0;
		MaskWindowOption opt(camera ? "camera" : "pair");
		Rig r;
		RigOpt ro;
		if(!r.start(ro, why))
			return false;
		ExeProj(r.src->pair.overlayProj[0], 64, 64, -32, -32);
		r.src->pair.srv = blocks;
		r.src->pair.generation = 2;
		CHECK(r.drawn(), "a frame not drawn");
		const FowTerrainInputs& ti = r.scene.fow().terrainInputs();
		const c5r::terrain::Overlay& bound = r.scene.boundOverlay();
		CHECK(r.scene.lastOverlay().srv == blocks && memcmp(r.scene.lastOverlay().proj[1], r.src->pair.overlayProj[0], 64) == 0,
		      "the recorder's overlay is not the received pair");
		if(!camera)
		{
			CHECK(!ti.cameraWindow && r.scene.fow().cameraWindowFrames == 0 && r.scene.ovlResampled == 0, "pair: a camera window");
			CHECK(bound.srv == blocks && memcmp(r.fow->lastIn.views[0].overlayProj, r.src->pair.overlayProj[0], 64) == 0, "pair: not the pair's");
			continue;
		}
		CHECK(ti.cameraWindow && r.scene.fow().cameraWindowFrames == 1 && r.scene.ovlResampled == 1 && r.scene.ovlResampleFails == 0,
		      "camera window " + S(ti.cameraWindow) + " frames " + S(r.scene.fow().cameraWindowFrames) + " moved " + S(r.scene.ovlResampled));
		float want[16];
		ExeProj(want, 192, 192, -128, -128);   // the EXE's rule for the rig's camera: x [-128, 64], y [-128, 64]
		const float* win = ti.windowProj[0];
		double b0 = (-1.0 - win[12]) / win[0], b1 = (1.0 - win[12]) / win[0], b2 = (-1.0 - win[13]) / win[5], b3 = (1.0 - win[13]) / win[5];
		CHECK(std::fabs(b0 + 128) < 1e-3 && std::fabs(b1 - 64) < 1e-3 && std::fabs(b2 + 128) < 1e-3 && std::fabs(b3 - 64) < 1e-3,
		      "the camera window is not [-128, 64] x [-128, 64]");
		CHECK(memcmp(r.fow->lastIn.views[0].overlayProj, win, 64) == 0, "the mask was not built in the camera window");
		CHECK(memcmp(bound.proj[1], win, 64) == 0, "the terrain's overlay matrix is not the camera window");
		CHECK(bound.srv && bound.srv != blocks, "the bound overlay is not the moved one");
		CHECK(r.lastFs.overlayBound && r.lastFs.fowOn, "the terrain drew without the overlay or the fog");
		uint32_t w = 0, h = 0, pw = 0, ph = 0;
		const std::vector<uint8_t> moved = ReadMip(bound.srv, 0, w, h);
		const std::vector<uint8_t> src = ReadMip(blocks, 0, pw, ph);
		CHECK(w == 256 && h == 256 && pw == 256, "the moved overlay's size " + S(w));
		const float* old = r.src->pair.overlayProj[0];
		int centres = 0, bad = 0, strip = 0, stripBad = 0;
		for(int by = 0; by < 8; ++by)
			for(int bx = 0; bx < 8; ++bx)
			{
				// the world point of the pair's block centre, read in both
				const double uo = (32.0 * bx + 16) / 256, vo = (32.0 * by + 16) / 256;
				const double x = ((2 * uo - 1) - old[12]) / old[0], y = ((1 - 2 * vo) - old[13]) / old[5];
				double un, vn;
				UvOf(win, x, y, un, vn);
				const uint8_t* p = TexelAt(moved, w, h, un, vn);
				const uint8_t* q = TexelAt(src, pw, ph, uo, vo);
				centres++;
				bad += (std::abs(p[0] - q[0]) > 3 || std::abs(p[1] - q[1]) > 3 || std::abs(p[2] - q[2]) > 3 || std::abs(p[3] - q[3]) > 3) ? 1 : 0;
			}
		// the strip: world points of the camera window outside the pair's [-32, 32]^2 by a texel or more
		for(int i = 0; i < 16; ++i)
		{
			const double x = -120 + 11.0 * i, y = -120.0;   // the window's bottom rows: y -120 is outside [-32, 32]
			double un, vn;
			UvOf(win, x, y, un, vn);
			const uint8_t* p = TexelAt(moved, w, h, un, vn);
			strip++;
			stripBad += (p[0] | p[1] | p[2] | p[3]) ? 1 : 0;
		}
		printf("  camera window: %d block centres (%d differ), %d strip points (%d not transparent)\n", centres, bad, strip, stripBad);
		CHECK(bad == 0 && stripBad == 0, S(bad) + " block centres differ, " + S(stripBad) + " strip points not transparent");
		// the move failing (a source of another format): no overlay that frame, the mask still placed, counted
		ID3D11Texture2D* ft = nullptr;
		ID3D11ShaderResourceView* fv = nullptr;
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = d.Height = 16;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		CHECK(SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &ft)), "texture");
		D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
		vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;   // not a Texture2D view: the move refuses it
		vd.Texture2DArray.MipLevels = 1;
		vd.Texture2DArray.ArraySize = 1;
		const bool made = SUCCEEDED(g_dev->CreateShaderResourceView(ft, &vd, &fv));
		SafeRelease(ft);
		CHECK(made, "the array view");
		r.src->pair.srv = fv;
		r.src->pair.generation = 3;
		const bool drew = r.drawn();
		r.src->pair.srv = blocks;
		const c5r::terrain::Overlay b2nd = r.scene.boundOverlay();
		SafeRelease(fv);
		CHECK(drew && r.scene.ovlResampleFails == 1 && !b2nd.srv && r.lastFs.fowOn && !r.lastFs.overlayBound,
		      "a failed move: drawn " + S(drew) + " fails " + S(r.scene.ovlResampleFails) + " fog " + S(r.lastFs.fowOn));
		CHECK(CountLog("the move into the fog's camera window failed") == 1, "the failed move's LOUD line");
	}
	return true;
}

// Ticket 0793: render_fow_overlay_history for one case (Scene::init reads it), back to the default after
struct OverlayHistoryOption
{
	explicit OverlayHistoryOption(const char* v)
	{
		std::string w;
		ParseRenderFowOverlayHistory(v, w);
	}
	~OverlayHistoryOption()
	{
		std::string w;
		ParseRenderFowOverlayHistory("", w);
	}
};

// Ticket 0793 (research/05-graphics-composition/overlay-history.md): the leading edge from the overlay's history. Frame 1
// at rest with the pair's window [-128, 64]^2 (the rig's default: the view inside it) offers the overlay to the history;
// frame 2's pair has the small window [-32, 32]^2, which the view leaves: a camera window [-128, 64]^2, and its strip
// outside [-32, 32]^2 is frame 1's overlay at the same world point (before 0793, and with render_fow_overlay_history=0:
// transparent). The rig's camera block carries Tiny's world box in y for this case (the rig's default leaves y 0..0, a box
// the history refuses quietly).
bool CaseCameraWindowHistory(std::string& why)
{
	ID3D11ShaderResourceView* blocks = BlockTexture(256);
	CHECK(blocks, "the block texture");
	struct Rel
	{
		ID3D11ShaderResourceView* p;
		~Rel() { SafeRelease(p); }
	} rel{blocks};
	for(int arm = 0; arm < 3; ++arm)
	{
		// arm 0: on; arm 1: render_fow_overlay_history=0; arm 2: on, but the camera block without a world box in y
		OverlayHistoryOption opt(arm == 1 ? "0" : "1");
		Rig r;
		RigOpt ro;
		if(!r.start(ro, why))
			return false;
		VPShellCamera* c = VPSHELL_CAMERA(r.m.hdr);
		if(arm != 2)
		{
			c->aabbMin[1] = -1108.047f;
			c->aabbMax[1] = 1108.047f;
		}
		float big[16];
		ExeProj(big, 192, 192, -128, -128);
		memcpy(r.src->pair.overlayProj[0], big, 64);
		r.src->pair.srv = blocks;
		// at rest for kRefreshCalls + 1 pairs: the first window after a load is drawn into the history then
		const int rest = (int)c5r::fow::OverlayResampler::kRefreshCalls + 1;
		for(int i = 0; i < rest; ++i)
		{
			r.src->pair.generation = 2 + i;
			CHECK(r.drawn(), "a frame at rest not drawn");
		}
		CHECK(r.scene.fow().cameraWindowFrames == 0, "a frame at rest took a camera window");
		ExeProj(r.src->pair.overlayProj[0], 64, 64, -32, -32);
		r.src->pair.generation = 2 + rest;
		CHECK(r.drawn(), "frame 2 not drawn");
		CHECK(r.scene.fow().cameraWindowFrames == 1 && r.scene.ovlResampled == 1, "frame 2: no camera window or no move");
		const c5r::terrain::Overlay& bound = r.scene.boundOverlay();
		CHECK(bound.srv && bound.srv != blocks, "frame 2: the bound overlay is not the moved one");
		const float* win = bound.proj[1];
		uint32_t w = 0, h = 0, pw = 0, ph = 0;
		const std::vector<uint8_t> moved = ReadMip(bound.srv, 0, w, h);
		const std::vector<uint8_t> src = ReadMip(blocks, 0, pw, ph);
		CHECK(w == 256 && h == 256 && pw == 256, "sizes");
		int strip = 0, kept = 0, clear = 0;
		for(int row = 0; row < 3; ++row)
			for(int i = 0; i < 16; ++i)
			{
				const double x = -120 + 11.0 * i, y = -120.0 + 30.0 * row;   // y -120, -90, -60: outside [-32, 32]
				const double bu = (x + 128) / 24.0, bv = (64 - y) / 24.0;      // frame 1's blocks: 24 world units
				if(std::fabs(bu - std::floor(bu + 0.5)) * 24.0 < 3.0 || std::fabs(bv - std::floor(bv + 0.5)) * 24.0 < 3.0)
					continue;   // within 3 units of a block's edge
				double un, vn, uo, vo;
				UvOf(win, x, y, un, vn);
				UvOf(big, x, y, uo, vo);
				const uint8_t* p = TexelAt(moved, w, h, un, vn);
				const uint8_t* q = TexelAt(src, pw, ph, uo, vo);
				strip++;
				kept += (std::abs(p[0] - q[0]) <= 3 && std::abs(p[1] - q[1]) <= 3 && std::abs(p[2] - q[2]) <= 3 && p[3] >= 250) ? 1 : 0;
				clear += (p[0] | p[1] | p[2] | p[3]) ? 0 : 1;
			}
		const c5r::fow::OverlayHistoryStats hs = r.scene.ovlHistory();
		printf("  %s: %d strip points, %d from the history, %d transparent; offers %lld refused %lld absorbed %llu edges %llu filled %llu holes %llu\n",
		       arm == 0 ? "history on" : arm == 1 ? "history off" : "no world box", strip, kept, clear, (long long)r.scene.ovlHistOffers,
		       (long long)r.scene.ovlHistFails, (unsigned long long)hs.absorbed, (unsigned long long)hs.strips, (unsigned long long)hs.stripFilled,
		       (unsigned long long)hs.stripHoles);
		CHECK(strip >= 24, "too few strip points: " + S(strip));
		if(arm == 0)
		{
			CHECK(kept == strip && clear == 0, S(kept) + " of " + S(strip) + " strip points from the history");
			CHECK(r.scene.ovlHistoryOn() && r.scene.ovlHistOffers == rest + 1 && r.scene.ovlHistFails == 0 && hs.absorbed == 2, "the offers");
			CHECK(hs.strips == 1 && hs.stripFilled == 1 && hs.stripHoles == 0, "the edge counters");
			CHECK(CountLog("render_fow_overlay_history=1") == 1, "the init note");
		}
		else
		{
			CHECK(clear == strip, S(clear) + " of " + S(strip) + " strip points transparent");
			CHECK(hs.absorbed == 0 && hs.stripFilled == 0, "a history without input");
			if(arm == 1)
				CHECK(!r.scene.ovlHistoryOn() && r.scene.ovlHistOffers == 0 && CountLog("render_fow_overlay_history=0") == 1, "off");
			else
				CHECK(r.scene.ovlHistOffers == rest + 1 && r.scene.ovlHistFails == rest + 1 && CountLog("no world box in the camera block") == 1 &&
				          CountLog("LOUD: overlay history") == 0,
				      "no world box: offers " + S(r.scene.ovlHistOffers) + " refused " + S(r.scene.ovlHistFails));
		}
	}
	return true;
}

// The mask handed to the EXE's model draws carries the window it was built in: the camera window on such frames.
bool CaseCameraWindowHandover(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	ExeProj(r.src->pair.overlayProj[0], 64, 64, -32, -32);   // the view leaves it: camera windows
	wchar_t secName[96];
	swprintf_s(secName, L"Local\\VPFowSceneTestMask0674.%u.%d", GetCurrentProcessId(), g_serial);
	r.scene.fow().maskPublisher().setSectionName(secName);
	CHECK(r.drawn(), "no first frame");
	VPShellTap* t = VPSHELL_TAP(r.m.hdr);
	t->seq++;
	MemoryBarrier();
	t->exePid = 4243;
	MemoryBarrier();
	t->seq++;
	r.src->pair.generation = 2;
	CHECK(r.drawn(), "no frame with the pid");
	HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, secName);
	CHECK(h, "no section with the EXE's pid");
	VPShellFowMaskSection* s = (VPShellFowMaskSection*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_FOWMASK_SECTION_BYTES);
	CloseHandle(h);
	CHECK(s, "the section does not map");
	struct Unmap
	{
		void* p;
		~Unmap() { UnmapViewOfFile(p); }
	} um{s};
	s->wanted = 1;
	s->inUseSlot = VPSHELL_FOWMASK_NONE;
	FowMaskPublisher& pub = r.scene.fow().maskPublisher();
	for(int i = 0; i < 60 && pub.published < 2; ++i)
	{
		r.src->pair.generation = 4 + i;
		CHECK(r.drawn(), "a frame not drawn while handing over");
		g_ctx->Flush();
		Sleep(20);
	}
	CHECK(pub.faults == 0 && pub.published >= 2 && s->newestSlot < VPSHELL_FOWMASK_SLOTS, "published " + S(pub.published));
	const VPShellFowMaskSlot* sl = VPSHELL_FOWMASK_SLOT(s, s->newestSlot);
	const FowTerrainInputs& ti = r.scene.fow().terrainInputs();
	CHECK(ti.cameraWindow && sl->valid == 1 && sl->numViews == 1, "the newest slot's record");
	CHECK(memcmp(sl->overlayProj[0], ti.windowProj[0], 64) == 0 && memcmp(sl->overlayProj[0], r.src->pair.overlayProj[0], 64) != 0,
	      "the handed-over window is not the mask's camera window");
	return true;
}

// ---- ticket 5052 (T): the units' plot table (unit-combat-server.md 18.25.3) ---------------------------------------------
// A unit scene that keeps what the terrain scene hands it before each beginFrame (the order of the calls, the plot table).
struct PlotTableUnits : srvunits::SceneUnits
{
	std::string order;                       // one letter per call: s setPlotStates, t setPlotTable, b beginFrame
	std::vector<SrvFeed::PlotRecord> plots;  // the newest setPlotTable's records
	bool null = true;
	size_t n = 0;
	uint32_t w = 0, h = 0, key = 0, statesGen = 0;
	int tables = 0;
	void setDevice(ID3D11Device*) override {}
	bool feed(const SrvFeed::Batch&, double) override { return false; }
	void useFeedSource(FeedSource*) override {}
	void poll() override {}
	void setPlotStates(const uint8_t*, size_t, uint32_t, uint32_t, uint32_t gen) override
	{
		order += 's';
		statesGen = gen;
	}
	void setPlotTable(const SrvFeed::PlotRecord* p, size_t nn, uint32_t ww, uint32_t hh, uint32_t k) override
	{
		order += 't';
		tables++;
		null = p == nullptr;
		n = nn;
		w = ww;
		h = hh;
		key = k;
		plots.assign(p, p ? p + nn : p);
	}
	void beginFrame(const srvunits::UnitsCamera&, const c5r::heightfield::HeightGrid*) override { order += 'b'; }
	void drawReflection(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, const float*) override {}
	void drawScene(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, uint32_t, const srvunits::UnitsCamera&,
	               const c5r::heightfield::HeightGrid*) override
	{
	}
	bool ready() const override { return false; }
	bool failed(std::string&) const override { return false; }
	bool animating() const override { return false; }
	void secondLine(char* out, size_t nn) override
	{
		if(nn)
			out[0] = 0;
	}
	void totalsLine(char* out, size_t nn) override
	{
		if(nn)
			out[0] = 0;
	}
};

// Scene::draw hands the units the fog scene's plot table (FowScene::plotTable) after setPlotStates and before beginFrame:
// every plot's record as read, the grid, and the key = the fog scene's read count. A feature change without a fog change
// (a forest cut: a changed plot, no new snapshot generation, no new fog generation) is a new key with the new feature; a
// frame without a read keeps the key; a read refused for its size hands no table (null, n 0); the next good read hands it
// again. The fog's own outputs do not change (the units are not its input).
bool CasePlotTable5052(std::string& why)
{
	Rig r;
	RigOpt ro;
	if(!r.start(ro, why))
		return false;
	PlotTableUnits* u = new PlotTableUnits();
	r.scene.setUnits(std::unique_ptr<srvunits::SceneUnits>(u));
	CHECK(r.drawn(), "no first frame");
	CHECK(u->order == "stb", "the calls of the first frame: " + u->order);
	const FowScene& fw = r.scene.fow();
	CHECK(!u->null && u->n == (size_t)kPlots * kPlots && u->w == kPlots && u->h == kPlots && u->key == (uint32_t)fw.rereads,
	      "the first table: n " + S((int)u->n) + ", " + S((int)u->w) + "x" + S((int)u->h) + ", key " + S((int)u->key));
	CHECK(memcmp(u->plots.data(), r.table->rec.data(), u->n * sizeof(SrvFeed::PlotRecord)) == 0, "the records are not the table's");
	// a forest cut on plot 6 (a SetPlot patch after the snapshot: the batch's changed plots, the same snapshot generation)
	const uint32_t key0 = u->key, fogGen0 = fw.fogGeneration(), snap0 = r.table->gen;
	r.table->rec[6].feature = 5;
	r.batch({Ev(SrvFeed::kBorderChanged, 6, 2)}, {6});
	CHECK(r.table->gen == snap0 && fw.fogGeneration() == fogGen0, "the cut moved a generation (the case wants neither to move)");
	CHECK(r.drawn(), "no frame after the cut");
	CHECK(u->order == "stbstb" && u->statesGen == fogGen0, "the calls of the second frame: " + u->order);
	CHECK(u->key == key0 + 1 && !u->null && u->plots[6].feature == 5, "the cut not handed: key " + S((int)u->key) + ", feature " +
	                                                                      S((int)(u->plots.empty() ? -9 : u->plots[6].feature)));
	// a frame without a read: the same key (the unit host copies nothing again)
	CHECK(r.drawn(), "no third frame");
	CHECK(u->key == key0 + 1 && u->tables == 3, "a frame without a read moved the key");
	// a read refused for its size (15 records for 4x4 plots, behind dropped events): no table, a new key
	r.table->rec.resize(15);
	r.batch({}, {}, 1);
	CHECK(fw.rereads == (int64_t)key0 + 2, "dropped events did not re-read");
	CHECK(r.drawn(), "no frame after the refused read");
	CHECK(u->null && u->n == 0 && u->key == key0 + 2, "a refused read handed a table: n " + S((int)u->n));
	// a new snapshot: the table again, and the fog drew on throughout
	r.table->snapshot({3});
	r.batch({Ev(SrvFeed::kSnapshot, -1, kPlots, kPlots, (int32_t)r.table->gen)});
	CHECK(r.drawn(), "no frame after the snapshot");
	CHECK(!u->null && u->n == (size_t)kPlots * kPlots && u->key == key0 + 3 && u->plots[6].feature == 0,
	      "the snapshot's table not handed");
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
};
const Case kCases[] = {
	{"all_visible_todays_picture", CaseAllVisible},
	{"one_plot_per_class", CaseOnePlotPerClass},
	{"snapshot_replaces_table", CaseSnapshot},
	{"overflow_rereads", CaseOverflow},
	{"no_overlay_pair", CaseNoOverlayPair},
	{"hold_report", CaseHoldReport},                  // ticket 0691
	{"field_hold_on_reload", CaseFieldHold},          // ticket 0691 (cand4)
	{"stall_not_counted", CaseStallNotCounted},       // ticket 0429
	{"slow_load_waits", CaseSlowLoad},               // ticket 0491
	{"option", CaseOption},
	{"install_failure_fatal_once", CaseInstallFailureFatalOnce},
	{"in_play_fault_never_fatal", CaseInPlayFaultNeverFatal},
	{"fow_level", CaseFowLevel},
	{"clock", CaseClock},
	{"real_modules", CaseRealModules},
	{"tile_styles", CaseTileStyles},                  // issue 4305
	{"tile_styles_follow", CaseTileStylesFollow},
	{"tile_styles_late_table", CaseTileStylesLateTable},
	{"held_frame_prepares", CaseHeldFramePrepares},   // ticket 0424
	{"held_frame_off", CaseHeldFrameOff},
	{"held_frame_waits_for_styles", CaseHeldFrameWaitsForStyles},
	{"held_frame_stale_dropped", CaseHeldFrameStaleDropped},
	{"model_fog_handover", CaseModelFogHandover},     // ticket 0339
	{"model_fog_not_wanted", CaseModelFogNotWanted},
	{"particles", CaseParticles},                     // ticket 0340
	{"particles_fail", CaseParticlesFail},
	{"particles_sequence", CaseParticlesSequence},    // ticket 0403
	{"zoom_out_step_leaves_no_pixel_without_mask", CaseZoomOutStep},   // ticket 0674
	{"far_jump_mask_not_empty", CaseFarJump},
	{"at_rest_unchanged", CaseAtRestUnchanged},
	{"camera_window_moves_overlay", CaseCameraWindowMovesOverlay},
	{"camera_window_handover", CaseCameraWindowHandover},
	{"camera_window_history", CaseCameraWindowHistory},   // ticket 0793
	{"units_plot_table_5052", CasePlotTable5052},     // ticket 5052 (T)
#ifdef SRV_FOW_PARTICLE_STATE
	{"particle_state", CaseParticleState},            // ticket 0403
#endif
};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	const char* e = getenv("C5R_EXE");
	if(!e || !*e)
	{
		printf("test_fow_scene: C5R_EXE not set (the terrain programs come from the EXE)\n");
		return 99;
	}
	g_exe.assign(e, e + strlen(e));
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("test_fow_scene: no WARP device\n");
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
		printf("test_fow_scene: no case matches\n");
		return 2;
	}
	g_ctx->Release();
	g_dev->Release();
	return fails;
}
