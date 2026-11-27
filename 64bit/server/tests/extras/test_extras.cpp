// test_extras.cpp - unit tests of the extras module and its first extra, VisibleAreaOutline (ticket 4938, epic 0454).
// SPDX-License-Identifier: GPL-3.0-or-later
// Built and run by run_extras_test.py (clang-cl x64 /W4 /WX; and once under MSVC /fsanitize=address). The GPU parts use
// a WARP device (no adapter needed, no window). Every check has a negative counterpart (COMMON 9).
#include <d3d11.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "extras/srv_extra_roads.h"   // ticket 4940: RoadStyleFor, the producer's layout choice
#include "extras/srv_extra_visible_outline.h"
#include "extras/srv_extras.h"
#include "feed/srv_feed.h"
#include "vp64_options.h"

#pragma comment(lib, "d3d11.lib")

using namespace srvextras;
using c5r::fow::PlotState;
using c5r::heightfield::HeightGrid;

static int g_pass = 0, g_fail = 0;
#define CHECK(x) Check((x), #x, __LINE__)
static void Check(bool ok, const char* what, int line)
{
	if(ok)
		g_pass++;
	else
	{
		g_fail++;
		printf("FAIL line %d: %s\n", line, what);
	}
}

// ---- the registry: srv_render_scene.cpp holds it in server.exe; this program links no scene, so it holds its own ----
static SceneExtrasFactory g_factory = nullptr;
static int g_registrations = 0;
namespace srvextras {
void RegisterSceneExtrasFactory(SceneExtrasFactory f)
{
	g_factory = f;
	g_registrations++;
}
std::unique_ptr<SceneExtras> MakeSceneExtras() { return g_factory ? g_factory() : std::unique_ptr<SceneExtras>(); }
// ticket 4934: the road producer's registry, the same way (its tests: server/tests/render/test_roads_producer.cpp)
static SceneRoadsFactory g_roadsFactory = nullptr;
void RegisterSceneRoadsFactory(SceneRoadsFactory f) { g_roadsFactory = f; }
std::unique_ptr<SceneRoads> MakeSceneRoads() { return g_roadsFactory ? g_roadsFactory() : std::unique_ptr<SceneRoads>(); }
}  // namespace srvextras

// ---- helpers ----
struct Log
{
	std::vector<std::string> lines;
	static void Fn(void* c, const char* l) { static_cast<Log*>(c)->lines.push_back(l); }
	bool has(const char* s) const
	{
		for(const std::string& l : lines)
			if(l.find(s) != std::string::npos)
				return true;
		return false;
	}
	size_t count(const char* s) const
	{
		size_t n = 0;
		for(const std::string& l : lines)
			if(l.find(s) != std::string::npos)
				n++;
		return n;
	}
};

// A w x h map's grid with the textbook placement (c5r::fow::nominalPlacement's values: M_h2w rows (64, 0), (32, 64), 0,
// (32, 128); sx 1, sy 0.866; ox 32 w, oy 32 (h + 4) 0.866), every height `height`.
static HeightGrid Grid(uint32_t w, uint32_t h, bool wrap, uint16_t height = 255)
{
	HeightGrid g;
	g.mapW = w;
	g.mapH = h + 4;
	g.samplesX = g.mapW * 64;
	g.samplesY = g.mapH * 64;
	g.wrapX = wrap;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = 32.0f * (float)w;
	g.oy = 32.0f * (float)(h + 4) * 0.866f;
	memset(g.hexToWorld, 0, sizeof(g.hexToWorld));
	g.hexToWorld[0] = 64;
	g.hexToWorld[4] = 32;
	g.hexToWorld[5] = 64;
	g.hexToWorld[10] = 1;
	g.hexToWorld[12] = 32;
	g.hexToWorld[13] = 128;
	g.hexToWorld[15] = 1;
	g.h.assign((size_t)g.samplesX * g.samplesY, height);
	return g;
}

static void PlotCentre(const HeightGrid& g, int x, int y, float out[2])
{
	const float hx = (float)(x - (y >> 1)), hy = (float)y;
	out[0] = (hx * g.hexToWorld[0] + hy * g.hexToWorld[4] + g.hexToWorld[12]) * g.sx - g.ox;
	out[1] = (hx * g.hexToWorld[1] + hy * g.hexToWorld[5] + g.hexToWorld[13]) * g.sy - g.oy;
}

static std::vector<PlotState> States(uint32_t w, uint32_t h, PlotState s)
{
	return std::vector<PlotState>((size_t)w * h, s);
}

// The edge's centre line: the middle of the two sides of every vertex pair (vertex 0/1, 4/3 of each segment's six)
static void CentreLine(const std::vector<OutlineVertex>& v, std::vector<float>& pts)
{
	pts.clear();
	for(size_t i = 0; i + 5 < v.size(); i += 6)
	{
		pts.push_back((v[i].x + v[i + 1].x) * 0.5f);
		pts.push_back((v[i].y + v[i + 1].y) * 0.5f);
		pts.push_back((v[i + 2].x + v[i + 4].x) * 0.5f);
		pts.push_back((v[i + 2].y + v[i + 4].y) * 0.5f);
	}
}

// ---- BuildOutline ----
static void TestBuild()
{
	OutlineParams p;
	OutlineStats st;
	std::vector<OutlineVertex> v;
	// one visible plot in the middle of a 6 x 4 map, every other plot seen: its six edges
	{
		const HeightGrid g = Grid(6, 4, false, 100);
		std::vector<PlotState> s = States(6, 4, PlotState::Seen);
		s[2 * 6 + 3] = PlotState::Visible;
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok());
		CHECK(st.visible == 1 && st.edges == 6 && st.borderSkips == 0);
		CHECK(v.size() == (size_t)6 * p.segments * 6);
		// the centre line lies on the hexagon's boundary: between the apothem (32) and the corner radius (64 / sqrt 3)
		float c[2];
		PlotCentre(g, 3, 2, c);
		std::vector<float> pts;
		CentreLine(v, pts);
		float dmin = 1e9f, dmax = 0;
		for(size_t i = 0; i + 1 < pts.size(); i += 2)
		{
			const float d = sqrtf((pts[i] - c[0]) * (pts[i] - c[0]) + (pts[i + 1] - c[1]) * (pts[i + 1] - c[1]));
			dmin = d < dmin ? d : dmin;
			dmax = d > dmax ? d : dmax;
		}
		CHECK(dmin > 31.5f && dmax < 37.2f);
		if(!(dmin > 31.5f && dmax < 37.2f))
			printf("  centre line distances %.3f .. %.3f\n", dmin, dmax);
		// the ribbon's width: the two sides of a vertex pair are 2 halfWidth apart
		const float wx = v[1].x - v[0].x, wy = v[1].y - v[0].y;
		CHECK(fabsf(sqrtf(wx * wx + wy * wy) - 2 * p.halfWidth) < 1e-3f);
		CHECK(v[0].side == -1.0f && v[1].side == 1.0f);
		// the heights: the ground (100 / 255 x sz) plus the lift, at every vertex
		const float z = 100.0f / 255.0f * g.sz + p.lift;
		bool zok = true;
		for(const OutlineVertex& o : v)
			zok = zok && fabsf(o.z - z) < 1e-4f;
		CHECK(zok);
		// negative: the same plot with a bigger halfWidth gives a wider ribbon (the check above sees the parameter)
		OutlineParams wide = p;
		wide.halfWidth = 3.0f;
		std::vector<OutlineVertex> v2;
		CHECK(BuildOutline(g, s.data(), 6, 4, wide, v2, nullptr).ok());
		const float w2x = v2[1].x - v2[0].x, w2y = v2[1].y - v2[0].y;
		CHECK(fabsf(sqrtf(w2x * w2x + w2y * w2y) - 6.0f) < 1e-3f);
	}
	// two neighbouring visible plots: 10 edges (the shared one is not drawn)
	{
		const HeightGrid g = Grid(6, 4, false);
		std::vector<PlotState> s = States(6, 4, PlotState::Seen);
		s[1 * 6 + 2] = s[1 * 6 + 3] = PlotState::Visible;
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.visible == 2 && st.edges == 10);
	}
	// a ring of six visible plots around a seen one (a hole): every ring plot has the hole, two ring neighbours and three
	// outer neighbours: 4 edges each, 24 in all
	{
		const HeightGrid g = Grid(8, 6, false);
		std::vector<PlotState> s = States(8, 6, PlotState::Seen);
		const int q0 = 4 - (3 >> 1), r0 = 3;   // the hole at (4, 3)
		for(int k = 0; k < 6; ++k)
		{
			const int q = q0 + kAxialDirs[k][0], r = r0 + kAxialDirs[k][1];
			s[(size_t)r * 8 + (q + (r >> 1))] = PlotState::Visible;
		}
		CHECK(BuildOutline(g, s.data(), 8, 6, p, v, &st).ok() && st.visible == 6 && st.edges == 24);
		if(st.edges != 24)
			printf("  ring: %u visible, %u edges\n", st.visible, st.edges);
	}
	// negatives: no visible plot; every plot visible; a seen plot beside an unexplored one: no edge
	{
		const HeightGrid g = Grid(6, 4, false);
		std::vector<PlotState> s = States(6, 4, PlotState::Seen);
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.edges == 0 && v.empty());
		s = States(6, 4, PlotState::Visible);
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.visible == 24 && st.edges == 0 && v.empty() &&
		      st.borderSkips > 0);
		s = States(6, 4, PlotState::Unexplored);
		s[5] = PlotState::Seen;
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.edges == 0 && v.empty());
		// a visible plot amid unexplored ones: no edge (the line runs only toward revealed plots out of sight; an
		// unexplored plot lies under the fog's clouds); one seen neighbour among them: that one edge
		s = States(6, 4, PlotState::Unexplored);
		s[2 * 6 + 3] = PlotState::Visible;
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.visible == 1 && st.edges == 0 && v.empty());
		s[2 * 6 + 4] = PlotState::Seen;
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.edges == 1 && v.size() == (size_t)p.segments * 6);
	}
	// the map's west edge: no edge where there is no neighbour (a map that does not wrap); six where it wraps, and the
	// ribbon stays at the plot's own place (no jump across the map)
	{
		const HeightGrid g = Grid(6, 4, false);
		std::vector<PlotState> s = States(6, 4, PlotState::Seen);
		s[2 * 6 + 0] = PlotState::Visible;   // (0, 2): an even row, so its west, north-west and south-west neighbours are off
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok() && st.edges == 3 && st.borderSkips == 3);
		const HeightGrid gw = Grid(6, 4, true);
		CHECK(BuildOutline(gw, s.data(), 6, 4, p, v, &st).ok() && st.edges == 6 && st.borderSkips == 0);
		float c[2];
		PlotCentre(gw, 0, 2, c);
		bool closeBy = true;
		for(const OutlineVertex& o : v)
			closeBy = closeBy && fabsf(o.x - c[0]) < 40.0f && fabsf(o.y - c[1]) < 40.0f;
		CHECK(closeBy);
		CHECK(OutlineWrapWidth(gw) == (float)(gw.samplesX - 1) * gw.sx && OutlineWrapWidth(g) == 0.0f);
		// wrapping: the west neighbour is the last column; when it is visible, that edge is not drawn
		s[2 * 6 + 5] = PlotState::Visible;
		CHECK(BuildOutline(gw, s.data(), 6, 4, p, v, &st).ok() && st.edges == 10);
	}
	// the top and bottom rows: no neighbour above row 0 or below the last row, wrapping or not
	{
		const HeightGrid gw = Grid(6, 4, true);
		std::vector<PlotState> s = States(6, 4, PlotState::Seen);
		s[0 * 6 + 3] = PlotState::Visible;
		CHECK(BuildOutline(gw, s.data(), 6, 4, p, v, &st).ok() && st.edges == 4 && st.borderSkips == 2);
	}
	// heights follow the ground: a grid with a raised column of samples under part of the plot
	{
		HeightGrid g = Grid(6, 4, false, 0);
		for(uint32_t y = 0; y < g.samplesY; ++y)
			for(uint32_t x = 0; x < 200; ++x)   // the plot (3, 2) reaches x 192 (its west corners): 200 covers them
				g.h[g.index(x, y)] = 1000;
		std::vector<PlotState> s = States(6, 4, PlotState::Seen);
		s[2 * 6 + 3] = PlotState::Visible;
		CHECK(BuildOutline(g, s.data(), 6, 4, p, v, &st).ok());
		bool low = false, high = false;
		for(const OutlineVertex& o : v)
		{
			low = low || fabsf(o.z - p.lift) < 1e-4f;
			high = high || fabsf(o.z - (1000.0f / 255.0f * g.sz + p.lift)) < 1e-3f;
		}
		CHECK(low && high);
	}
	// refusals: nothing built, BadLayout
	{
		const HeightGrid g = Grid(6, 4, false);
		std::vector<PlotState> s = States(6, 4, PlotState::Visible);
		s[0] = PlotState::Seen;
		v.assign(3, OutlineVertex());
		c5r::Status r = BuildOutline(g, nullptr, 6, 4, p, v, &st);
		CHECK(!r.ok() && r.code == c5r::Err::BadLayout && v.empty());
		r = BuildOutline(g, s.data(), 0, 4, p, v, &st);
		CHECK(!r.ok() && r.code == c5r::Err::BadLayout);
		r = BuildOutline(g, s.data(), 6, 5, p, v, &st);   // the table one row taller than the grid's
		CHECK(!r.ok() && r.str().find("6x5") != std::string::npos);
		HeightGrid bad = g;
		bad.hexToWorld[0] = NAN;
		r = BuildOutline(bad, s.data(), 6, 4, p, v, &st);
		CHECK(!r.ok() && r.str().find("not finite") != std::string::npos);
		bad = g;
		bad.h.resize(10);
		CHECK(!BuildOutline(bad, s.data(), 6, 4, p, v, &st).ok());
		bad = g;
		bad.hexToWorld[0] = 0;
		bad.hexToWorld[4] = 0;
		bad.hexToWorld[1] = 0;
		bad.hexToWorld[5] = 0;
		r = BuildOutline(bad, s.data(), 6, 4, p, v, &st);
		CHECK(!r.ok() && r.str().find("zero length") != std::string::npos && v.empty());
		const float widths[] = {0.0f, -1.0f, 17.0f, NAN};
		for(float wv : widths)
		{
			OutlineParams q = p;
			q.halfWidth = wv;
			CHECK(!BuildOutline(g, s.data(), 6, 4, q, v, &st).ok());
		}
		const int segs[] = {0, 17, -1};
		for(int sv : segs)
		{
			OutlineParams q = p;
			q.segments = sv;
			CHECK(!BuildOutline(g, s.data(), 6, 4, q, v, &st).ok());
		}
		OutlineParams q = p;
		q.lift = NAN;
		CHECK(!BuildOutline(g, s.data(), 6, 4, q, v, &st).ok());
		// the controls: the limits themselves are taken
		q = p;
		q.segments = 16;
		q.halfWidth = 16.0f;
		CHECK(BuildOutline(g, s.data(), 6, 4, q, v, &st).ok());
		q.segments = 1;
		CHECK(BuildOutline(g, s.data(), 6, 4, q, v, &st).ok());
	}
}

// ---- the GPU pass and the extra, on WARP ----
struct Target
{
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	ID3D11Texture2D* colour = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* depth = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	ID3D11Texture2D* staging = nullptr;
	uint32_t w = 256, h = 256;
	bool make()
	{
		D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
		if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
			return false;
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = w;
		d.Height = h;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET;
		if(FAILED(dev->CreateTexture2D(&d, nullptr, &colour)) || FAILED(dev->CreateRenderTargetView(colour, nullptr, &rtv)))
			return false;
		d.BindFlags = 0;
		d.Usage = D3D11_USAGE_STAGING;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		if(FAILED(dev->CreateTexture2D(&d, nullptr, &staging)))
			return false;
		D3D11_TEXTURE2D_DESC z = {};
		z.Width = w;
		z.Height = h;
		z.MipLevels = 1;
		z.ArraySize = 1;
		z.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		z.SampleDesc.Count = 1;
		z.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		return SUCCEEDED(dev->CreateTexture2D(&z, nullptr, &depth)) && SUCCEEDED(dev->CreateDepthStencilView(depth, nullptr, &dsv));
	}
	void clear(float depthValue)
	{
		const float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		ctx->ClearRenderTargetView(rtv, c);
		ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depthValue, 0);
	}
	// pixels that are not the clear colour; alphaOk: every pixel's alpha is still 255
	uint32_t lit(bool* alphaOk)
	{
		ctx->CopyResource(staging, colour);
		D3D11_MAPPED_SUBRESOURCE m = {};
		uint32_t n = 0;
		bool aok = true;
		if(SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m)))
		{
			for(uint32_t y = 0; y < h; ++y)
			{
				const uint8_t* row = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
				for(uint32_t x = 0; x < w; ++x)
				{
					if(row[x * 4 + 0] || row[x * 4 + 1] || row[x * 4 + 2])
						n++;
					if(row[x * 4 + 3] != 255)
						aok = false;
				}
			}
			ctx->Unmap(staging, 0);
		}
		if(alphaOk)
			*alphaOk = aok;
		return n;
	}
	~Target()
	{
		if(dsv) dsv->Release();
		if(depth) depth->Release();
		if(staging) staging->Release();
		if(rtv) rtv->Release();
		if(colour) colour->Release();
		if(ctx) ctx->Release();
		if(dev) dev->Release();
	}
};

// A top-down orthographic camera over the world box of g (row vectors; depth 0.5 for every z)
static void Ortho(const HeightGrid& g, float vp[16])
{
	float a[2], b[2];
	PlotCentre(g, 0, 0, a);
	PlotCentre(g, (int)g.mapW - 1, (int)g.mapH - 5, b);
	const float x0 = a[0] - 64, x1 = b[0] + 64, y0 = a[1] - 64, y1 = b[1] + 64;
	memset(vp, 0, 16 * sizeof(float));
	vp[0] = 2.0f / (x1 - x0);
	vp[5] = 2.0f / (y1 - y0);
	vp[10] = 0.0f;
	vp[12] = -(x1 + x0) / (x1 - x0);
	vp[13] = -(y1 + y0) / (y1 - y0);
	vp[14] = 0.5f;
	vp[15] = 1.0f;
}

static void TestOutlineGpu()
{
	Target t;
	if(!t.make())
	{
		printf("FAIL: no WARP device\n");
		g_fail++;
		return;
	}
	const HeightGrid g = Grid(6, 4, false);
	std::vector<PlotState> s = States(6, 4, PlotState::Seen);
	s[2 * 6 + 3] = PlotState::Visible;
	OutlineFrame f;
	f.grid = &g;
	f.states = s.data();
	f.w = 6;
	f.h = 4;
	f.fogGeneration = 1;
	f.rtv = t.rtv;
	f.dsv = t.dsv;
	f.width = t.w;
	f.height = t.h;
	Ortho(g, f.viewProj);

	// off: nothing at all (no line, no draw, no frame counted)
	{
		Log log;
		VisibleOutline o(&Log::Fn, &log);
		o.configure(false, nullptr, true);
		CHECK(!o.on() && o.initDevice(t.dev).ok());
		t.clear(1.0f);
		o.frame(t.ctx, f);
		CHECK(t.lit(nullptr) == 0 && o.frames == 0 && log.lines.empty());
	}
	// the fog not drawn by this server: off for the session, one line
	{
		Log log;
		VisibleOutline o(&Log::Fn, &log);
		o.configure(true, nullptr, false);
		CHECK(!o.on() && o.failed() && log.count("off for this session") == 1 && log.has("render_fow=0"));
		t.clear(1.0f);
		o.initDevice(t.dev);
		o.frame(t.ctx, f);
		CHECK(t.lit(nullptr) == 0 && o.drawn == 0);
	}
	// on: drawn; the destination's alpha kept; nothing drawn where the depth is nearer (depth cleared to 0)
	Log log;
	VisibleOutline o(&Log::Fn, &log);
	o.configure(true, nullptr, true);
	CHECK(o.on() && log.has("in effect"));
	CHECK(o.initDevice(t.dev).ok());
	t.clear(1.0f);
	o.frame(t.ctx, f);
	bool alphaOk = false;
	const uint32_t litOn = t.lit(&alphaOk);
	CHECK(o.drawn == 1 && o.rebuilds == 1 && o.lastEdges == 6 && litOn > 50 && alphaOk);
	if(!(litOn > 50))
		printf("  lit pixels %u\n", litOn);
	t.clear(0.0f);
	o.frame(t.ctx, f);
	CHECK(t.lit(nullptr) == 0 && o.drawn == 2 && o.rebuilds == 1);   // same generation: no rebuild
	// every plot visible: no edge, nothing drawn (a new generation: rebuilt)
	std::vector<PlotState> all = States(6, 4, PlotState::Visible);
	OutlineFrame fa = f;
	fa.states = all.data();
	fa.fogGeneration = 2;
	t.clear(1.0f);
	o.frame(t.ctx, fa);
	CHECK(t.lit(nullptr) == 0 && o.rebuilds == 2 && o.lastEdges == 0);
	// E6: the fog lost in the middle of a session: nothing drawn, one line; back: one line, drawn again
	OutlineFrame fl = f;
	fl.states = nullptr;
	fl.fogGeneration = 3;
	const size_t before = log.lines.size();
	t.clear(1.0f);
	o.frame(t.ctx, fl);
	o.frame(t.ctx, fl);
	CHECK(t.lit(nullptr) == 0 && log.lines.size() == before + 1 && log.has("not drawn: the fog of war is not running") &&
	      o.waits[VisibleOutline::kWaitFog] == 2);
	OutlineFrame fg = f;
	fg.grid = nullptr;
	o.frame(t.ctx, fg);
	CHECK(log.has("not drawn: the height field is not there yet") && o.waits[VisibleOutline::kWaitGrid] == 1);
	OutlineFrame ft = f;
	ft.h = 3;
	o.frame(t.ctx, ft);
	CHECK(log.has("does not fit the height field") && o.waits[VisibleOutline::kWaitTable] == 1);
	t.clear(1.0f);
	o.frame(t.ctx, f);
	CHECK(log.has("drawn again") && t.lit(nullptr) == litOn && !o.failed());
	// E6: a draw that fails (no target): off for the session with one line; later frames draw nothing
	OutlineFrame fx = f;
	fx.rtv = nullptr;
	o.frame(t.ctx, fx);
	CHECK(o.failed() && !o.on() && o.drawErrors == 1 && log.count("off for this session") == 1);
	t.clear(1.0f);
	const int64_t drawnBefore = o.drawn;
	o.frame(t.ctx, f);
	CHECK(t.lit(nullptr) == 0 && o.drawn == drawnBefore);
	char line[512];
	o.totalsLine(line, sizeof(line));
	CHECK(strstr(line, "off (failed)") != nullptr && strstr(line, "errors build 0 draw 1") != nullptr);
	// a wrapping map: the pass draws three copies (one world width either side) and the frame still shows the line
	{
		Log l2;
		VisibleOutline w(&Log::Fn, &l2);
		w.configure(true, nullptr, true);
		w.initDevice(t.dev);
		const HeightGrid gw = Grid(6, 4, true);
		OutlineFrame fw = f;
		fw.grid = &gw;
		t.clear(1.0f);
		w.frame(t.ctx, fw);
		CHECK(w.drawn == 1 && t.lit(nullptr) > 50);
		w.releaseDevice();
		w.frame(t.ctx, fw);   // no device: nothing, no failure
		CHECK(w.drawn == 1 && !w.failed());
		CHECK(w.initDevice(t.dev).ok());
		w.frame(t.ctx, fw);
		CHECK(w.drawn == 2);
	}
	// the renderer refuses a matrix that is not finite, and a draw before init
	{
		OutlineRenderer r;
		const float rgba[4] = {1, 1, 1, 1};
		CHECK(!r.draw(t.ctx, t.rtv, t.dsv, t.w, t.h, f.viewProj, 0, rgba).ok());
		CHECK(r.init(t.dev).ok());
		std::vector<OutlineVertex> v;
		CHECK(BuildOutline(g, s.data(), 6, 4, OutlineParams(), v, nullptr).ok() && r.upload(t.ctx, v).ok());
		float bad[16];
		memcpy(bad, f.viewProj, sizeof(bad));
		bad[3] = INFINITY;
		CHECK(!r.draw(t.ctx, t.rtv, t.dsv, t.w, t.h, bad, 0, rgba).ok());
		CHECK(r.draw(t.ctx, t.rtv, t.dsv, t.w, t.h, f.viewProj, 0, rgba).ok());
		CHECK(!r.draw(t.ctx, t.rtv, t.dsv, 0, t.h, f.viewProj, 0, rgba).ok());
		// the context's state is put back: the render target bound before the draw is bound after it
		ID3D11RenderTargetView* none = nullptr;
		t.ctx->OMSetRenderTargets(1, &none, nullptr);
		CHECK(r.draw(t.ctx, t.rtv, t.dsv, t.w, t.h, f.viewProj, 0, rgba).ok());
		ID3D11RenderTargetView* after = nullptr;
		t.ctx->OMGetRenderTargets(1, &after, nullptr);
		CHECK(after == nullptr);
		if(after)
			after->Release();
	}
}

// ---- the module: the effective state from the options, the registry ----
static unsigned P(const char* text, Vp64Opt::Options& o) { return Vp64Opt::Parse(text, strlen(text), o, nullptr, nullptr); }

static void TestModule()
{
	CHECK(g_registrations == 1 && g_factory != nullptr);   // srv_extras.cpp's registrar ran at start-up
	std::unique_ptr<SceneExtras> m = MakeSceneExtras();
	CHECK(m != nullptr);
	Log log;
	m->setLog(&Log::Fn, &log);
	struct Case
	{
		const char* text;
		bool fog;
		bool on;
	};
	const Case cases[] = {
		{"", true, false},                                                              // every default: off
		{"[Extras]\nVisibleAreaOutline = 1\n", true, false},                            // the category switch at 0
		{"[Extras]\nEnableVisualAdditions = 1\n", true, false},                         // its own flag at 0
		{"[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", true, true},  // both on
		{"[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", false, false},   // the fog off: off (E6)
		// E5: alone and with every other extra and switch on
		{"[Extras]\nEnableBugfixes = 0\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", true, true},
		{"[Extras]\nEnableBugfixes = 1\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", true, true},
	};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		Vp64Opt::Options o;
		P(cases[i].text, o);
		m->configure(o, cases[i].fog);
		const bool ok = m->anyOn() == cases[i].on;
		if(!ok)
			printf("  module case %u: anyOn %d\n", (unsigned)i, (int)m->anyOn());
		CHECK(ok);
	}
	// a batch is taken whatever it holds; a frame with every input missing draws nothing and does not fail
	SrvFeed::Batch b;
	SrvFeed::Event e = {};
	e.kind = SrvFeed::kFowChanged;
	b.events.push_back(e);
	m->onBatch(b);
	Target t;
	CHECK(t.make());
	CHECK(m->initDevice(t.dev).ok());
	ExtrasFrame f;
	f.rtv = t.rtv;
	f.width = t.w;
	f.height = t.h;
	t.clear(1.0f);
	m->draw(t.ctx, f);
	CHECK(m->anyOn() && t.lit(nullptr) == 0 && log.has("not drawn: the fog of war is not running"));
	// the frame passed through: grid and states given, the line drawn
	const HeightGrid g = Grid(6, 4, false);
	std::vector<PlotState> s = States(6, 4, PlotState::Seen);
	s[2 * 6 + 3] = PlotState::Visible;
	f.dsv = t.dsv;
	f.grid = &g;
	f.fowStates = s.data();
	f.fowW = 6;
	f.fowH = 4;
	f.fowGeneration = 5;
	Ortho(g, f.viewProj);
	t.clear(1.0f);
	m->draw(t.ctx, f);
	CHECK(t.lit(nullptr) > 50);
	char line[512];
	m->totalsLine(line, sizeof(line));
	CHECK(strstr(line, "extras: VisibleAreaOutline: on;") != nullptr && strstr(line, "drawn 1") != nullptr);
	// off again: draw does nothing
	Vp64Opt::Options off;
	m->configure(off, true);
	t.clear(1.0f);
	m->draw(t.ctx, f);
	CHECK(!m->anyOn() && t.lit(nullptr) == 0);
	m->releaseDevice();
}

// ---- ticket 4940: the road layout choice (RoadLayoutChoiceSlot, RoadStyleFor) and the producer that reads it ----
static SrvFeed::PlotRecord RoadPlot(bool road)
{
	SrvFeed::PlotRecord r;
	memset(&r, 0, sizeof(r));
	r.route = road ? 0 : -1;
	r.routeState = road ? 0 : -1;
	r.improvement = -1;
	r.resource = -1;
	r.feature = -1;
	r.owner = -1;
	r.improvementState = -1;
	r.fow = 3;
	return r;
}

// One production of a producer made with `c` in the slot, on a W x H map where every plot has a road (the dense case):
// the decals and the edges the ring rule left out. The slot is put back as it was.
static bool ProduceDense(const RoadLayoutChoice& c, size_t& decals, uint32_t& rejected, std::string& said)
{
	// static: MSVC cl (the --asan build) rejects naming a non-static local in a lambda without capturing it (C3493),
	// even a constant the standard lets it use uncaptured; clang-cl (the plain build) accepts both (ticket 0726)
	static const uint32_t W = 8, H = 8;
	const RoadLayoutChoice keep = RoadLayoutChoiceSlot();
	RoadLayoutChoiceSlot() = c;
	RoadLayoutProducer p;
	RoadLayoutChoiceSlot() = keep;
	Log log;
	p.setLog(&Log::Fn, &log);
	SceneRoadsOptions o;
	o.threaded = false;
	p.setOptions(o);
	std::vector<SrvFeed::PlotRecord> rec(W * H, RoadPlot(true));
	p.setTable([rec](std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& w, uint32_t& h) {
		out = rec;
		g = 1;
		w = W;
		h = H;
	});
	const float world[4] = {0.0f, 0.0f, 64.0f * W + 64.0f, 64.0f * H + 256.0f};
	p.setWorld(world, 1, false, 0.0f, 0.0f);
	if(!p.settle(10000) || !p.haveDecals())
		return false;
	decals = p.decals().decals.size();
	rejected = p.lastRejected;
	said.clear();
	for(const std::string& l : log.lines)
		if(l.find("roadlayout: field of load") != std::string::npos)
			said = l;
	return true;
}

static void TestRoadLayoutChoice()
{
	namespace rx = c5r::extras::roads;
	// the slot's default: the tidy layout (what ticket 4934's producer always laid out)
	const RoadLayoutChoice def;
	CHECK(def.layout == 1 && def.junction == 0 && def.jitter == 0);
	// the style of each choice
	const rx::LayoutOptions tidy = RoadStyleFor(def), prod = ProductRoadStyle();
	CHECK(tidy.style == prod.style && tidy.connect == rx::Connect::Tidy && tidy.junction == rx::Junction::Through &&
	      tidy.lineJitter == 0.0f && tidy.filletRadius == 22.0f && tidy.mergeLength == 22.0f);
	RoadLayoutChoice c;
	c.layout = 1;
	c.junction = 2;
	c.jitter = 5;   // layout 1 does not use the shape
	const rx::LayoutOptions t2 = RoadStyleFor(c);
	CHECK(t2.connect == rx::Connect::Tidy && t2.junction == rx::Junction::Through && t2.lineJitter == 0.0f);
	const rx::Junction want[4] = {rx::Junction::Through, rx::Junction::Cross, rx::Junction::Ring, rx::Junction::Rosette};
	for(int j = 0; j <= 3; ++j)
		for(int t = 0; t <= 7; ++t)
		{
			c.layout = 2;
			c.junction = j;
			c.jitter = t;
			const rx::LayoutOptions a = RoadStyleFor(c);
			CHECK(a.connect == rx::Connect::All && a.junction == want[j] && a.lineJitter == (float)t && a.style == prod.style &&
			      a.filletRadius == 22.0f && a.mergeLength == 22.0f);
		}
	// clamped (the parsers refuse these first)
	c.junction = 9;
	c.jitter = 99;
	CHECK(RoadStyleFor(c).junction == rx::Junction::Rosette && RoadStyleFor(c).lineJitter == 7.0f);
	c.junction = -1;
	c.jitter = -1;
	CHECK(RoadStyleFor(c).junction == rx::Junction::Through && RoadStyleFor(c).lineJitter == 0.0f);
	// the ranges, with the first bad value named
	std::string why;
	RoadLayoutChoice v;
	CHECK(ValidRoadLayoutChoice(v, why));
	v.layout = 2;
	v.junction = 3;
	v.jitter = 7;
	CHECK(ValidRoadLayoutChoice(v, why));
	const struct
	{
		int layout, junction, jitter;
		const char* why;
	} bad[] = {{0, 0, 0, "layout 0 is not 1 or 2"}, {3, 0, 0, "layout 3"}, {2, 4, 0, "junction 4 is not 0..3"},
	           {2, -1, 0, "junction -1"}, {2, 0, 8, "jitter 8 is not 0..7"}, {2, 0, -1, "jitter -1"}};
	for(const auto& b : bad)
	{
		RoadLayoutChoice x;
		x.layout = b.layout;
		x.junction = b.junction;
		x.jitter = b.jitter;
		why.clear();
		const bool ok = !ValidRoadLayoutChoice(x, why) && why.find(b.why) != std::string::npos;
		if(!ok)
			printf("  ValidRoadLayoutChoice %d %d %d: %s\n", b.layout, b.junction, b.jitter, why.c_str());
		CHECK(ok);
	}
	// the start line's text
	CHECK(RoadLayoutChoiceText(def) == "layout 1 (tidy, variant (e))");
	c.layout = 2;
	c.junction = 1;
	c.jitter = 3;
	CHECK(RoadLayoutChoiceText(c) == "layout 2 (all connections), junction cross, jitter 3");
	// the producer reads the slot once, when it is made: a later change of the slot changes no producer that exists
	{
		const RoadLayoutChoice keep = RoadLayoutChoiceSlot();
		RoadLayoutChoiceSlot() = c;
		RoadLayoutProducer p;
		RoadLayoutChoiceSlot() = def;
		CHECK(p.choice().layout == 2 && p.choice().junction == 1 && p.choice().jitter == 3 &&
		      p.style().connect == rx::Connect::All && p.style().junction == rx::Junction::Cross && p.style().lineJitter == 3.0f);
		RoadLayoutProducer q;
		CHECK(q.choice().layout == 1 && q.style().connect == rx::Connect::Tidy);
		RoadLayoutChoiceSlot() = keep;
	}
	// a production of each on the dense map: all connections leaves no edge out and draws more decals than the tidy layout,
	// and the producer names its layout in the load's line; the same choice twice gives the same decals (deterministic)
	{
		size_t d1 = 0, d2 = 0, d2b = 0;
		uint32_t r1 = 0, r2 = 0, r2b = 0;
		std::string s1, s2, s2b;
		CHECK(ProduceDense(def, d1, r1, s1));
		CHECK(ProduceDense(c, d2, r2, s2));
		CHECK(ProduceDense(c, d2b, r2b, s2b));
		const bool ok = r1 > 0 && r2 == 0 && d2 > d1 && d2b == d2 && r2b == 0 &&
		                s1.find("layout 1 (tidy, variant (e))") != std::string::npos &&
		                s2.find("layout 2 (all connections), junction cross, jitter 3") != std::string::npos;
		if(!ok)
			printf("  dense: tidy %zu decals %u left out (%s); all %zu decals %u left out (%s); again %zu\n", d1, r1, s1.c_str(), d2,
			       r2, s2.c_str(), d2b);
		CHECK(ok);
	}
}

int main()
{
	TestBuild();
	TestOutlineGpu();
	TestModule();
	TestRoadLayoutChoice();
#if defined(EXTRAS_TEST_INJECT_FAIL)
	CHECK(false);   // the runner's negative: this build must fail
#endif
	printf("test_extras: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
