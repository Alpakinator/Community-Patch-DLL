// test_forest_scene.cpp - scene tests of the trees in server.exe's renderer (server/src/render/srv_forest_scene.cpp, epic
// 0431, ticket 4554). Written 2026-11-16 for this repository (GPL-3.0-or-later). Model: server/tests/render/fow/.
//
// Each case builds a ForestScene with fake sources: a fake plot table (FowPlotTable), batches made here as the host's
// loop hands them over, a synthetic height field (8 x 10 grid plots, the 8 x 6 table, flat at u16 20000 = z 15.7, above
// the water rule), the feature ids set by the case (or published through the process-wide table), and a camera block
// made here. The real modules run on a WARP device: c5r::terrain (the EXE's terrain programs; the Screen Overlay pass)
// and c5r::forest (the EXE's ForestShader, the install's `_2` cards and textures). Install failures use a second index
// of the install with a loose folder of broken files in front (gamefs extraLooseRoots), a wrong shader VA, a missing
// EXE, no device, a target too large and a terrain started without its install.
// Output: one line per case, `CASE <name> PASS|FAIL <detail>`; exit code = the number of failed cases, 2 on usage, 99
// without a WARP device, C5R_EXE or C5R_GAME_DIR (never a skip).
//
//   test_forest_scene.exe [case ...]      (no argument: every case)
#include "forest/srv_forest_scene.h"
#include "srv_render_common.h"
#include "srv_options.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/bake/post.h"
#include "c5r/terrain/terrain.h"
#include "c5r/forest/shadows.h"
#include "srv_residency_source.h"
#include "srv_render_scene.h"
#include "srv_render_host.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvrender;
namespace c5h = c5r::heightfield;
namespace c5t = c5r::terrain;
namespace c5f = c5r::forest;

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::wstring g_exe, g_game;
c5r::gamefs::GameFiles g_files;
c5t::Renderer* g_terrain = nullptr;
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
#define CHECK(c, msg)        \
	do                       \
	{                        \
		if(!(c))             \
		{                    \
			why = (msg);     \
			return false;    \
		}                    \
	} while(0)

const uint32_t kW = 320, kH = 240;      // camera and target size
const uint32_t kMapW = 8, kMapH = 10;   // grid plots (the table is kMapW x (kMapH - 4))
const uint32_t kTabW = kMapW, kTabH = kMapH - 4;
const int kForestId = 5, kJungleId = 1, kMarshId = 2;
const uint16_t kGround = 20000;         // z = 20000 / 255 * 0.2 = 15.69 (above the water rule's 10.12)

c5h::HeightGrid MakeGrid()
{
	c5h::HeightGrid g;
	g.mapW = kMapW;
	g.mapH = kMapH;
	g.samplesX = kMapW * 64;
	g.samplesY = kMapH * 64;
	g.wrapX = true;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = kMapW * 32.0f;
	g.oy = kMapH * 32.0f * 0.866f;
	g.waterZ = 5.0f;
	float* m = g.hexToWorld;   // the textbook placement (fow.h nominalPlacement): rows (64, 0), (32, 64), 0, (32, 128)
	m[0] = 64.0f;
	m[5] = 64.0f;
	m[4] = 32.0f;
	m[12] = 32.0f;
	m[13] = 128.0f;
	m[15] = 1.0f;
	g.h.assign((size_t)g.samplesX * g.samplesY, kGround);
	return g;
}
c5h::HeightGrid g_grid;

// ---- the plot table -------------------------------------------------------------------------------------------------
struct FakeTable : FowPlotTable
{
	std::vector<SrvFeed::PlotRecord> rec;
	uint32_t gen = 1, w = kTabW, h = kTabH;
	int copies = 0;
	void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& ww, uint32_t& hh) override
	{
		copies++;
		out = rec;
		g = gen;
		ww = w;
		hh = h;
	}
};
SrvFeed::PlotRecord Plot(int feature, int plotType = 2)
{
	SrvFeed::PlotRecord r = {};
	r.terrain = 0;
	r.plotType = (int8_t)plotType;
	r.feature = (int8_t)feature;
	r.route = -1;
	r.resource = -1;
	r.improvement = -1;
	r.owner = -1;
	r.fow = 3;
	r.improvementState = -1;
	r.routeState = -1;
	return r;
}
// The table of most cases: forest on every plot of rows 0-1 (hills on x = 1), jungle on rows 2-3 (hills on x = 2), marsh
// on (0, 4), nothing elsewhere.
std::vector<SrvFeed::PlotRecord> BaseTable()
{
	std::vector<SrvFeed::PlotRecord> t(kTabW * kTabH, Plot(-1));
	for(uint32_t y = 0; y < kTabH; y++)
		for(uint32_t x = 0; x < kTabW; x++)
		{
			SrvFeed::PlotRecord& r = t[y * kTabW + x];
			if(y < 2)
				r = Plot(kForestId, x == 1 ? 1 : 2);
			else if(y < 4)
				r = Plot(kJungleId, x == 2 ? 1 : 2);
		}
	t[4 * kTabW + 0] = Plot(kMarshId);
	return t;
}
const uint32_t kBaseForest = 2 * kTabW, kBaseJungle = 2 * kTabW;

SrvFeed::Batch SnapshotBatch(uint32_t gen)
{
	SrvFeed::Batch b;
	b.snapshotGen = gen;
	b.width = kTabW;
	b.height = kTabH;
	SrvFeed::Event e = {};
	e.kind = SrvFeed::kSnapshot;
	e.plot = -1;
	e.a[0] = (int32_t)kTabW;
	e.a[1] = (int32_t)kTabH;
	e.a[2] = (int32_t)gen;
	b.events.push_back(e);
	return b;
}
SrvFeed::Event FeatureEvent(int plot, int feature)
{
	SrvFeed::Event e = {};
	e.kind = SrvFeed::kFeatureChanged;
	e.plot = plot;
	e.a[0] = feature;
	return e;
}

// ---- the camera -----------------------------------------------------------------------------------------------------
void LookAtRH(const float eye[3], const float at[3], const float up[3], float m[16])
{
	float z[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
	float l = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	for(float& v : z)
		v /= l;
	float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
	l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	for(float& v : x)
		v /= l;
	const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
	const float r[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
	                     -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
	                     -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
	memcpy(m, r, sizeof(r));
}
void PerspectiveFovRH(float fovY, float aspect, float zn, float zf, float m[16])
{
	const float ys = 1.0f / std::tan(fovY * 0.5f), xs = ys / aspect;
	const float r[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zn - zf), -1, 0, 0, zn * zf / (zn - zf), 0};
	memcpy(m, r, sizeof(r));
}
VPShellCamera MakeCam(float ex, float ey, float ez, float ax, float ay, float az, float fovDeg = 45.0f)
{
	VPShellCamera c = {};
	c.status = VPCAM_OK;
	c.numViews = 1;
	const float eye[3] = {ex, ey, ez}, at[3] = {ax, ay, az}, up[3] = {0, 0, 1};
	LookAtRH(eye, at, up, c.view[0]);
	PerspectiveFovRH(fovDeg * 3.14159265f / 180.0f, (float)kW / kH, 10.0f, 4000.0f, c.proj);
	c.wrapPeriod = c5t::wrapPeriod(g_grid.samplesX, g_grid.sx);
	c.wrapX = 1;
	return c;
}
VPShellCamera MainCam() { return MakeCam(0, -420, 420, 0, -40, 15); }
// view v = view 0 shifted by k wrap periods (the EXE's wrap views: row 3 += k P row 0)
void AddWrapView(VPShellCamera& c, int k)
{
	const uint32_t v = c.numViews++;
	memcpy(c.view[v], c.view[0], sizeof(c.view[0]));
	for(int i = 0; i < 3; i++)
		c.view[v][12 + i] += (float)k * c.wrapPeriod * c.view[0][i];
}
c5t::Camera TCam(const VPShellCamera& c)
{
	c5t::Camera t;
	memcpy(t.view, c.view[0], sizeof(t.view));
	memcpy(t.proj, c.proj, sizeof(t.proj));
	t.width = kW;
	t.height = kH;
	return t;
}

// ---- targets and textures -------------------------------------------------------------------------------------------
struct Target
{
	ID3D11Texture2D* tex = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* dtex = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	bool make(uint32_t w = kW, uint32_t h = kH)
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = w;
		d.Height = h;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &tex)) || FAILED(g_dev->CreateRenderTargetView(tex, nullptr, &rtv)))
			return false;
		d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		return SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &dtex)) && SUCCEEDED(g_dev->CreateDepthStencilView(dtex, nullptr, &dsv));
	}
	~Target()
	{
		SafeRelease(rtv);
		SafeRelease(tex);
		SafeRelease(dsv);
		SafeRelease(dtex);
	}
	void clear()
	{
		const float z[4] = {0, 0, 0, 0};
		g_ctx->ClearRenderTargetView(rtv, z);
		g_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
};
std::vector<uint32_t> ReadBack(ID3D11Texture2D* t)
{
	D3D11_TEXTURE2D_DESC d;
	t->GetDesc(&d);
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	d.MiscFlags = 0;
	ID3D11Texture2D* s = nullptr;
	std::vector<uint32_t> px;
	if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
		return px;
	g_ctx->CopyResource(s, t);
	D3D11_MAPPED_SUBRESOURCE m;
	if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &m)))
	{
		px.resize((size_t)d.Width * d.Height);
		for(uint32_t y = 0; y < d.Height; y++)
			memcpy(&px[(size_t)y * d.Width], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, d.Width * 4);
		g_ctx->Unmap(s, 0);
	}
	s->Release();
	return px;
}
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
c5t::Options TerrainOptions(ID3D11ShaderResourceView* overlay)
{
	c5t::Options o;
	o.color = c5t::ColorSource::Constant;
	o.fallback = c5t::ColorSource::Constant;
	o.constantRgba = 0xFF406080u;
	o.water = false;
	o.overlay.srv = overlay;   // proj identity: a uniform texture is the same colour at every uv
	return o;
}

// ---- the scene ------------------------------------------------------------------------------------------------------
struct Fatal
{
	static int calls, code;
	static std::string why;
	static void Fn(int c, const char* w)
	{
		calls++;
		code = c;
		why = w ? w : "";
	}
	static void Reset()
	{
		calls = 0;
		code = 0;
		why.clear();
	}
};
int Fatal::calls = 0, Fatal::code = 0;
std::string Fatal::why;

struct HookLog
{
	int built = 0, removed = 0, all = 0, drawnTrees = 0;
	std::set<std::pair<int, int>> builtPlots, removedPlots;
	static void Fn(void* u, int32_t x, int32_t y, const c5f::PlotRecord* rec, const c5f::PlotBlocks* b)
	{
		HookLog& h = *static_cast<HookLog*>(u);
		if(x < 0)
			h.all++;
		else if(b && rec)
		{
			h.built++;
			h.drawnTrees += (int)b->drawn.size();
			h.builtPlots.insert(std::make_pair(x, y));
		}
		else
		{
			h.removed++;
			h.removedPlots.insert(std::make_pair(x, y));
		}
	}
};

ForestSceneOptions Opts()
{
	ForestSceneOptions o;
	o.on = true;
	o.exe = g_exe;
	o.files = &g_files;
	o.seed = 4554;
	return o;
}

// A scene on the fake table, the field and the ids: snapshot taken. table: out (owned by the scene).
struct Rig
{
	std::unique_ptr<ForestScene> f;
	FakeTable* t = nullptr;
	bool ok = false;
	std::string why;
	explicit Rig(const ForestSceneOptions& o, bool ids = true, bool snapshot = true)
	{
		t = new FakeTable();
		t->rec = BaseTable();
		f.reset(new ForestScene(std::unique_ptr<FowPlotTable>(t)));
		f->init(g_dev, o);
		if(f->failed(why))
			return;
		f->onField(g_grid, 1);
		if(ids)
			f->setFeatureIds(kForestId, kJungleId);
		if(snapshot)
			f->onBatch(SnapshotBatch(t->gen));
		ok = !f->failed(why);
	}
};

const float kFowOff[4] = {0.002f, 0.002f, 0, 0};

// Terrain into `scene`, then the stage into `trees` (cleared, the terrain's depth): the tree pixels alone.
c5r::Status Frame(ForestScene& f, Target& scene, Target& trees, const VPShellCamera& cam, ID3D11ShaderResourceView* overlay,
                  ForestStageStats* st)
{
	scene.clear();
	const c5t::Camera tc = TCam(cam);
	const c5t::Options op = TerrainOptions(overlay);
	c5t::FrameStats fs;
	C5R_TRY(g_terrain->draw(g_ctx, scene.rtv, scene.dsv, tc, op, &fs));
	const float z[4] = {0, 0, 0, 0};
	g_ctx->ClearRenderTargetView(trees.rtv, z);
	return f.drawStage(g_ctx, trees.rtv, scene.dsv, kW, kH, cam, *g_terrain, tc, op, kFowOff, st);
}

// ---- cases ----------------------------------------------------------------------------------------------------------
bool CaseSnapshot(std::string& why)
{
	Rig r(Opts(), false, true);
	CHECK(r.ok, "rig: " + r.why);
	CHECK(r.f->placement().recordCount() == 0, "records before the ids: " + S(r.f->placement().recordCount()));
	r.f->setFeatureIds(kForestId, kJungleId);
	const c5f::Placement& p = r.f->placement();
	CHECK(p.recordCount() == kBaseForest + kBaseJungle, "records " + S(p.recordCount()));
	for(uint32_t y = 0; y < kTabH; y++)
		for(uint32_t x = 0; x < kTabW; x++)
		{
			const c5f::PlotRecord* rec = p.record((int)x, (int)y);
			c5f::Kind want = c5f::Kind::None;
			if(y < 2)
				want = x == 1 ? c5f::Kind::ForestHills : c5f::Kind::Forest;
			else if(y < 4)
				want = x == 2 ? c5f::Kind::JungleHills : c5f::Kind::Jungle;
			const c5f::Kind got = rec ? rec->kind : c5f::Kind::None;
			CHECK(got == want, "plot " + S(x) + "," + S(y) + " kind " + S((int)got) + " want " + S((int)want));
			if(rec)
				CHECK(rec->layout < c5f::kLayoutCount && rec->cube[0] == (int)x - (int)(y >> 1) && rec->cube[1] == (int)y,
				      "plot " + S(x) + "," + S(y) + " layout / cube");
		}
	CHECK(p.layouts().size() == c5f::kLayoutCount && p.params().treesPerPlot() == 180, "layouts / trees per plot (VT's forests.xml)");
	CHECK(r.f->rereads == 1 && r.f->recordsAdded == (int64_t)(kBaseForest + kBaseJungle), "counters");
	// the same session seed gives the same layouts and layout indices (the stream is the session's)
	Rig r2(Opts());
	CHECK(r2.ok, "rig2: " + r2.why);
	bool same = r2.f->placement().recordCount() == p.recordCount();
	for(const c5f::PlotRecord& a : p.records())
	{
		const c5f::PlotRecord* b = r2.f->placement().record(a.plotX, a.plotY);
		same = same && b && b->layout == a.layout;
	}
	CHECK(same, "the same seed did not give the same layout indices");
	ForestSceneOptions o3 = Opts();
	o3.seed = 777;
	Rig r3(o3);
	CHECK(r3.ok, "rig3: " + r3.why);
	int differ = 0;
	for(const c5f::PlotRecord& a : p.records())
		differ += r3.f->placement().record(a.plotX, a.plotY)->layout != a.layout;
	CHECK(differ > 0, "another seed gave the same layout indices");
	return true;
}

bool CaseFeatureChange(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	HookLog hl;
	r.f->setPlotHook(&HookLog::Fn, &hl);
	const c5f::Placement& p = r.f->placement();
	std::vector<c5f::PlotRecord> before = p.records();
	// A (3,5): none -> forest; B (4,0): forest -> none; C (5,1): forest -> jungle; D (6,0): forest -> forest on hills;
	// E (7,0): forest -> forest (an event, no change)
	const uint32_t A = 5 * kTabW + 3, B = 0 * kTabW + 4, C = 1 * kTabW + 5, D = 6, E = 7;
	r.t->rec[A] = Plot(kForestId);
	r.t->rec[B] = Plot(-1);
	r.t->rec[C] = Plot(kJungleId);
	r.t->rec[D] = Plot(kForestId, 1);
	SrvFeed::Batch b;
	b.snapshotGen = r.t->gen;
	b.width = kTabW;
	b.height = kTabH;
	b.changedPlots = {A, B, C, D, E};
	b.events = {FeatureEvent((int)A, kForestId), FeatureEvent((int)B, -1), FeatureEvent((int)C, kJungleId), FeatureEvent((int)E, kForestId),
	            FeatureEvent(9999, kForestId)};
	const int copies0 = r.t->copies;
	CHECK(r.f->onBatch(b), "onBatch said nothing changed");
	CHECK(r.t->copies == copies0 + 1 && r.f->rereads == 1, "a patch must copy the table once and not re-read the records");
	CHECK(p.record(3, 5) && p.record(3, 5)->kind == c5f::Kind::Forest && p.record(3, 5)->layout < 20, "A not added");
	CHECK(!p.record(4, 0), "B not removed");
	CHECK(p.record(5, 1) && p.record(5, 1)->kind == c5f::Kind::Jungle, "C not jungle");
	CHECK(p.record(6, 0) && p.record(6, 0)->kind == c5f::Kind::ForestHills, "D not on hills");
	CHECK(r.f->recordsAdded == (int64_t)(kBaseForest + kBaseJungle) + 1 && r.f->recordsRemoved == 1 && r.f->recordsChanged == 2,
	      "counters added " + S(r.f->recordsAdded) + " removed " + S(r.f->recordsRemoved) + " changed " + S(r.f->recordsChanged));
	CHECK(r.f->outsideEvents == 1 && CountLog("outside the 8x6 table") == 1 && r.f->featureEvents == 5, "the outside event not LOUD once");
	CHECK(hl.removed == 3 && hl.removedPlots.count(std::make_pair(4, 0)) && hl.removedPlots.count(std::make_pair(5, 1)) &&
	          hl.removedPlots.count(std::make_pair(6, 0)),
	      "hook removals " + S(hl.removed));
	int kept = 0, keptSame = 0;
	for(const c5f::PlotRecord& a : before)
	{
		if((a.plotX == 4 && a.plotY == 0) || (a.plotX == 5 && a.plotY == 1) || (a.plotX == 6 && a.plotY == 0))
			continue;
		kept++;
		const c5f::PlotRecord* n = p.record(a.plotX, a.plotY);
		keptSame += n && n->layout == a.layout && n->kind == a.kind;
	}
	CHECK(kept == keptSame, "an untouched plot changed its record");
	// a batch without changed plots and events: nothing
	SrvFeed::Batch quiet;
	quiet.snapshotGen = r.t->gen;
	quiet.width = kTabW;
	quiet.height = kTabH;
	CHECK(!r.f->onBatch(quiet), "an empty batch changed something");
	// a changed plot whose feature and plot type stayed (e.g. a road): no record change
	SrvFeed::Batch road = quiet;
	road.changedPlots = {A};
	CHECK(!r.f->onBatch(road), "a plot change without a feature change changed the record");
	return true;
}

bool CaseOverflow(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	HookLog hl;
	r.f->setPlotHook(&HookLog::Fn, &hl);
	// the table changes behind a dropped queue: the overflow event re-reads it whole
	for(uint32_t x = 0; x < kTabW; x++)
		r.t->rec[x] = Plot(-1);   // row 0 loses its forest
	SrvFeed::Batch b;
	b.snapshotGen = r.t->gen;
	b.width = kTabW;
	b.height = kTabH;
	SrvFeed::Event e = {};
	e.kind = SrvFeed::kOverflow;
	e.plot = -1;
	e.a[0] = 12;
	b.events.push_back(e);
	b.dropped = 12;
	CHECK(r.f->onBatch(b), "the overflow did not re-read");
	CHECK(r.f->rereads == 2 && r.f->overflows == 1, "rereads " + S(r.f->rereads) + " overflows " + S(r.f->overflows));
	CHECK(r.f->placement().recordCount() == kBaseForest + kBaseJungle - kTabW, "records " + S(r.f->placement().recordCount()));
	CHECK(hl.all == 1, "the hook was not told every record was dropped");
	// dropped events without the event (a counter that moved): re-read too
	r.t->rec[0] = Plot(kForestId);
	SrvFeed::Batch b2;
	b2.snapshotGen = r.t->gen;
	b2.width = kTabW;
	b2.height = kTabH;
	b2.dropped = 20;
	CHECK(r.f->onBatch(b2) && r.f->rereads == 3 && r.f->overflows == 2, "a moved drop counter did not re-read");
	CHECK(r.f->placement().record(0, 0), "plot 0,0 not back after the re-read");
	// a new snapshot generation: re-read
	r.t->gen = 2;
	r.t->rec = BaseTable();
	CHECK(r.f->onBatch(SnapshotBatch(2)) && r.f->rereads == 4 &&
	          r.f->placement().recordCount() == kBaseForest + kBaseJungle,
	      "a new snapshot did not re-read");
	return true;
}

bool CaseIdsByName(std::string& why)
{
	ForestFeatureIdsReset();
	Rig r(Opts(), false, true);
	CHECK(r.ok, "rig: " + r.why);
	CHECK(!r.f->poll() && r.f->placement().recordCount() == 0, "records without published ids");
	ForestFeatureTagSeen(kMarshId, "ART_DEF_FEATURE_MARSH");   // ignored
	ForestFeatureTagSeen(kForestId, "ART_DEF_FEATURE_FOREST");
	ForestFeatureTagSeen(kJungleId, "ART_DEF_FEATURE_JUNGLE");
	CHECK(!r.f->poll() && r.f->placement().recordCount() == 0, "records (or a change) before the ids are resolved");
	ForestFeatureIdsResolved();
	CHECK(r.f->poll(), "poll did not see the resolved ids");
	CHECK(r.f->featureIds().forest == kForestId && r.f->featureIds().jungle == kJungleId && r.f->featureIds().resolved, "ids");
	CHECK(r.f->placement().recordCount() == kBaseForest + kBaseJungle, "records " + S(r.f->placement().recordCount()));
	CHECK(!r.f->poll(), "a poll without a change changed something");
	// another id for the forest (a different database): every record again, by the new id
	ForestFeatureTagSeen(kMarshId, "ART_DEF_FEATURE_FOREST");
	CHECK(r.f->poll() && CountLog("they CHANGED") == 1, "the id change was not taken");
	CHECK(r.f->placement().recordCount() == kBaseJungle + 1 && r.f->placement().record(0, 4), "records by the new forest id");
	ForestFeatureIdsReset();
	return true;
}

bool CaseIdsNeverPublished(std::string& why)
{
	ForestFeatureIdsReset();
	ForestSceneOptions o = Opts();
	o.idsWaitMs = 1;
	Rig r(o, false, true);
	CHECK(r.ok, "rig: " + r.why);
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	Sleep(20);
	const c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, nullptr);
	SafeRelease(ovl);
	std::string w;
	CHECK(!s.ok() && r.f->failed(w) && w.find("feature ids") != std::string::npos, "no install failure: " + s.str() + " / " + w);
	Fatal::Reset();
	bool done = false;
	ForestFatalStep(r.f.get(), &Fatal::Fn, done);
	ForestFatalStep(r.f.get(), &Fatal::Fn, done);
	CHECK(Fatal::calls == 1 && Fatal::code == kForestFatalExitCode && Fatal::why == w, "fatal " + S(Fatal::calls) + " code " + S(Fatal::code));
	return true;
}

// The visibility rule against an independent computation in double: a plot centre (HexToWorld of its cube coordinates, z
// the height) inside every plane of a view by 64 units, in any view.
bool Inside64(const VPShellCamera& c, uint32_t v, const double p[3], double margin)
{
	double m[16];
	for(int i = 0; i < 4; i++)
		for(int j = 0; j < 4; j++)
		{
			double s = 0;
			for(int k = 0; k < 4; k++)
				s += (double)c.view[v][i * 4 + k] * (double)c.proj[k * 4 + j];
			m[i * 4 + j] = s;
		}
	double clip[4];
	for(int j = 0; j < 4; j++)
		clip[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
	// the six plane functions and their gradients' lengths
	const int sx[6] = {1, -1, 0, 0, 0, 0}, sy[6] = {0, 0, 1, -1, 0, 0}, sz[6] = {0, 0, 0, 0, 1, -1}, sw[6] = {1, 1, 1, 1, 0, 1};
	for(int q = 0; q < 6; q++)
	{
		double val = sx[q] * clip[0] + sy[q] * clip[1] + sz[q] * clip[2] + sw[q] * clip[3];
		double g[3];
		for(int a = 0; a < 3; a++)
			g[a] = sx[q] * m[a * 4 + 0] + sy[q] * m[a * 4 + 1] + sz[q] * m[a * 4 + 2] + sw[q] * m[a * 4 + 3];
		const double len = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
		if(val / len < -margin)
			return false;
	}
	return true;
}
std::set<std::pair<int, int>> Expected(const VPShellCamera& c, double margin)
{
	std::set<std::pair<int, int>> out;
	for(uint32_t y = 0; y < kTabH; y++)
		for(uint32_t x = 0; x < kTabW; x++)
		{
			const int hx = (int)x - (int)(y >> 1), hy = (int)y;
			const double sx = hx * 64.0 + hy * 32.0 + 32.0, sy = hy * 64.0 + 128.0;
			const double p[3] = {sx * g_grid.sx - g_grid.ox, g_grid.sy * sy - g_grid.oy, kGround / 255.0 * 0.2};
			bool vis = false;
			for(uint32_t v = 0; v < c.numViews && !vis; v++)
				vis = Inside64(c, v, p, margin);
			if(vis)
				out.insert(std::make_pair((int)x, (int)y));
		}
	return out;
}
std::set<std::pair<int, int>> AsSet(const std::vector<ForestVisiblePlot>& v, const c5f::Placement* only = nullptr)
{
	std::set<std::pair<int, int>> s;
	for(const auto& p : v)
		s.insert(std::make_pair(p.x, p.y));
	(void)only;
	return s;
}

bool CaseVisibility(std::string& why)
{
	// every plot a record (forest everywhere) so the rule is seen on the whole table
	Rig r(Opts(), true, false);
	CHECK(r.ok, "rig: " + r.why);
	r.t->rec.assign(kTabW * kTabH, Plot(kForestId));
	r.f->onBatch(SnapshotBatch(r.t->gen));
	CHECK(r.f->placement().recordCount() == kTabW * kTabH, "records");
	// a narrow camera over the middle: some plots inside, some only by the margin, some outside
	VPShellCamera c = MakeCam(0, -150, 260, 0, -60, 15, 20.0f);
	const std::set<std::pair<int, int>> got = AsSet(r.f->visiblePlots(c));
	const std::set<std::pair<int, int>> want = Expected(c, 64.0), strict = Expected(c, 0.0), wide = Expected(c, 1e6);
	CHECK(got == want, "visible set " + S(got.size()) + " != expected " + S(want.size()));
	printf("    info: visibility: plots inside the frustum %u, with the 64-unit margin %u, all %u\n", (unsigned)strict.size(),
	       (unsigned)want.size(), (unsigned)wide.size());
	CHECK(strict.size() < want.size() && want.size() < wide.size() && !strict.empty(),
	      "the camera does not separate the margin: strict " + S(strict.size()) + " margin " + S(want.size()) + " all " + S(wide.size()));
	// the stage builds exactly the visible plots, once
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, c, ovl, &st);
	CHECK(s.ok(), "stage: " + s.str());
	CHECK(st.visible == want.size() && st.builtNow == want.size() && r.f->placement().builtCount() == want.size(),
	      "built " + S(st.builtNow) + " visible " + S(st.visible) + " placement " + S(r.f->placement().builtCount()));
	CHECK(st.blocks == st.visible * 2 && st.draws == st.blocks, "blocks " + S(st.blocks) + " draws " + S(st.draws));
	s = Frame(*r.f, scene, trees, c, ovl, &st);
	CHECK(s.ok() && st.builtNow == 0 && st.visible == want.size(), "the second frame built again: " + S(st.builtNow));
	// the main camera: more plots, only the new ones built
	const VPShellCamera m = MainCam();
	const std::set<std::pair<int, int>> want2 = Expected(m, 64.0);
	size_t fresh = 0;
	for(const auto& p : want2)
		fresh += want.count(p) ? 0 : 1;
	s = Frame(*r.f, scene, trees, m, ovl, &st);
	CHECK(s.ok() && st.visible == want2.size() && st.builtNow == fresh, "main camera: visible " + S(st.visible) + " built " + S(st.builtNow) +
	                                                                         " want " + S(want2.size()) + " / " + S(fresh));
	// a wrap view: the set of two views = the union of each view's set
	VPShellCamera w1 = MakeCam(-240, -150, 260, -240, -60, 15, 20.0f);   // the map's left edge (world x -256)
	VPShellCamera w2 = w1;
	AddWrapView(w2, -1);   // the copy that shows the right edge (x + 511)
	VPShellCamera shifted = w1;
	memcpy(shifted.view[0], w2.view[1], sizeof(shifted.view[0]));
	std::set<std::pair<int, int>> u = AsSet(r.f->visiblePlots(w1)), sh = AsSet(r.f->visiblePlots(shifted));
	CHECK(!sh.empty() && !u.empty(), "wrap camera sets empty: " + S(u.size()) + " / " + S(sh.size()));
	const size_t before = u.size();
	u.insert(sh.begin(), sh.end());
	CHECK(u.size() > before, "the wrap view adds no plot");
	CHECK(AsSet(r.f->visiblePlots(w2)) == u && Expected(w2, 64.0) == u, "two views != the union of each view");
	SafeRelease(ovl);
	return true;
}

int CountAlpha(const std::vector<uint32_t>& px)
{
	int n = 0;
	for(uint32_t p : px)
		n += (p >> 24) != 0;
	return n;
}

bool CaseStageOverlay(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	const VPShellCamera c = MainCam();
	ID3D11ShaderResourceView* clear = Texture(0x00000000u);
	ID3D11ShaderResourceView* red = Texture(0xCC0000CCu);   // premultiplied red at alpha 0.8 (R in the low byte)
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, c, clear, &st);
	CHECK(s.ok() && st.drawn && st.overlayPass, "frame 1: " + s.str());
	const std::vector<uint32_t> a = ReadBack(trees.tex), ovlA = ReadBack(r.f->screenOverlayTexture());
	const int nA = CountAlpha(a);
	CHECK(nA > 200, "too few tree pixels: " + S(nA));
	int ovlNonZeroA = 0;
	for(uint32_t p : ovlA)
		ovlNonZeroA += p != 0;
	CHECK(ovlNonZeroA == 0, "the Screen Overlay holds pixels with a transparent overlay: " + S(ovlNonZeroA));
	s = Frame(*r.f, scene, trees, c, red, &st);
	CHECK(s.ok() && st.drawn, "frame 2: " + s.str());
	const std::vector<uint32_t> b = ReadBack(trees.tex), ovlB = ReadBack(r.f->screenOverlayTexture());
	int ovlRed = 0, ovlOther = 0;
	for(uint32_t p : ovlB)
	{
		if(p == 0xCC0000CCu)
			ovlRed++;
		else if(p)
			ovlOther++;
	}
	CHECK(ovlRed > (int)(kW * kH / 4) && ovlOther == 0, "the Screen Overlay of frame 2: red " + S(ovlRed) + " other " + S(ovlOther));
	int sameMask = 0, changed = 0, redder = 0, outside = 0;
	for(size_t i = 0; i < a.size(); i++)
	{
		const bool ta = (a[i] >> 24) != 0, tb = (b[i] >> 24) != 0;
		sameMask += ta == tb;
		if(!ta && !tb)
			outside += a[i] != 0 || b[i] != 0;
		if(ta && tb)
		{
			changed += a[i] != b[i];
			redder += (int)(b[i] & 0xFF) > (int)(a[i] & 0xFF);
		}
	}
	CHECK(sameMask == (int)a.size() && outside == 0, "the tree pixels moved with the overlay");
	printf("    info: stage: %d tree pixels of %u; with the red overlay: %d changed, %d redder; Screen Overlay red %d; %u blocks, %u draws\n",
	       nA, kW * kH, changed, redder, ovlRed, st.blocks, st.draws);
	CHECK(changed > nA * 9 / 10 && redder > nA * 9 / 10, "tree pixels changed " + S(changed) + " redder " + S(redder) + " of " + S(nA));
	// back to transparent: equal to frame 1 (the pass runs before the trees of the same frame, never a stale t3)
	s = Frame(*r.f, scene, trees, c, clear, &st);
	const std::vector<uint32_t> a2 = ReadBack(trees.tex);
	CHECK(s.ok() && a2 == a, "frame 3 differs from frame 1");
	CHECK(r.f->framesDrawn == 3 && r.f->overlayErrors == 0 && r.f->drawErrors == 0 && r.f->state() == ForestScene::kActive, "counters");
	char line[2048];
	r.f->secondLine(line, sizeof(line));
	CHECK(strstr(line, "forest 1s: state=ACTIVE drawn=3") != nullptr, std::string("second line: ") + line);
	r.f->totalsLine(line, sizeof(line));
	CHECK(strstr(line, "frames drawn 3") != nullptr, std::string("totals: ") + line);
	SafeRelease(clear);
	SafeRelease(red);
	return true;
}

bool CaseOff(std::string& why)
{
	ForestSceneOptions o = Opts();
	o.on = false;
	Rig r(o);
	CHECK(r.ok, "rig: " + r.why);
	CHECK(r.f->state() == ForestScene::kOff && r.f->placement().recordCount() == 0 && CountLog("render_forest=0") == 1, "not off");
	CHECK(!r.f->onBatch(SnapshotBatch(3)), "onBatch did something while off");
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0xCC0000CCu);
	ForestStageStats st;
	const c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	SafeRelease(ovl);
	const std::vector<uint32_t> px = ReadBack(trees.tex);
	CHECK(s.ok() && !st.drawn && !st.overlayPass && CountAlpha(px) == 0 && !r.f->screenOverlayTexture(), "the off scene drew");
	return true;
}

// ---- install failures -----------------------------------------------------------------------------------------------
std::wstring Scratch()
{
	wchar_t b[MAX_PATH];
	GetTempPathW(MAX_PATH, b);
	std::wstring d = std::wstring(b) + L"c5_forest_scene_test_" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(d.c_str(), nullptr);
	return d;
}
bool WriteFile(const std::wstring& dir, const char* name, const std::vector<uint8_t>& bytes)
{
	std::wstring p = dir + L"\\";
	for(const char* c = name; *c; c++)
		p.push_back((wchar_t)*c);
	FILE* f = _wfopen(p.c_str(), L"wb");
	if(!f)
		return false;
	fwrite(bytes.data(), 1, bytes.size(), f);
	fclose(f);
	return true;
}
void RemoveTree(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(fd.cFileName[0] != L'.')
				DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}
std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
// A 128 x 128 A8R8G8B8 DDS (a mask that is not L8)
std::vector<uint8_t> ArgbDds()
{
	std::vector<uint8_t> b(4 + 124 + 128 * 128 * 4, 0xFF);
	uint32_t* h = (uint32_t*)b.data();
	memset(b.data(), 0, 128);
	h[0] = 0x20534444;    // "DDS "
	h[1] = 124;
	h[2] = 0x1 | 0x2 | 0x4 | 0x1000;   // caps, height, width, pixelformat
	h[3] = 128;
	h[4] = 128;
	h[5] = 128 * 4;
	h[19] = 32;           // pixel format size
	h[20] = 0x41;         // RGB | ALPHAPIXELS
	h[22] = 32;
	h[23] = 0x00FF0000;
	h[24] = 0x0000FF00;
	h[25] = 0x000000FF;
	h[26] = 0xFF000000;
	h[27] = 0x1000;       // caps texture
	return b;
}

// One failing init: the scene's why must contain `needle`; the fatal step calls fatal(5, why) once; without a hook the
// scene goes on without trees and draws nothing.
bool ExpectInstallFailure(const char* label, const ForestSceneOptions& o, ID3D11Device* dev, const char* needle, std::string& why)
{
	FakeTable* t = new FakeTable();
	t->rec = BaseTable();
	ForestScene f{std::unique_ptr<FowPlotTable>(t)};
	f.init(dev, o);
	std::string w;
	CHECK(f.failed(w), std::string(label) + ": no install failure");
	CHECK(w.find(needle) != std::string::npos, std::string(label) + ": why '" + w + "' does not name '" + needle + "'");
	CHECK(CountLog("LOUD: forest: install failure") >= 1, std::string(label) + ": not LOUD");
	Fatal::Reset();
	bool done = false;
	ForestFatalStep(&f, &Fatal::Fn, done);
	ForestFatalStep(&f, &Fatal::Fn, done);
	CHECK(Fatal::calls == 1 && Fatal::code == 5 && Fatal::why == w, std::string(label) + ": fatal calls " + S(Fatal::calls) + " code " +
	                                                                    S(Fatal::code));
	// the same failure without a hook: LOUD, then the scene draws on without the trees
	bool done2 = false;
	ForestFatalStep(&f, nullptr, done2);
	CHECK(f.state() == ForestScene::kWithout && !f.failed(w), std::string(label) + ": not WITHOUT after the hookless step");
	return true;
}

bool CaseInstallFailures(std::string& why)
{
	{
		ForestSceneOptions o = Opts();
		o.files = nullptr;
		o.gameDir.clear();
		if(!ExpectInstallFailure("no install", o, g_dev, "no game install", why))
			return false;
		o.gameDir = L"C:\\no\\such\\install";
		if(!ExpectInstallFailure("install that does not open", o, g_dev, "did not open", why))
			return false;
	}
	{
		ForestSceneOptions o = Opts();
		if(!ExpectInstallFailure("no device", o, nullptr, "no D3D11 device", why))
			return false;
		o.exe = L"C:\\no\\such\\CivilizationV_DX11.exe";
		if(!ExpectInstallFailure("no EXE", o, g_dev, "the forest renderer did not start", why))
			return false;
		o = Opts();
		o.vsVa = c5f::kForestPsVa;   // a PS where the VS is wanted
		if(!ExpectInstallFailure("ForestShader VS VA names the PS", o, g_dev, "the forest renderer did not start", why))
			return false;
	}
	struct Broken
	{
		const char* label;
		const char* file;
		std::vector<uint8_t> bytes;
		const char* needle;
	};
	const std::string colorKey = c5r::bake::colorKeyFile(0);
	std::vector<Broken> cases;
	cases.push_back({"forests.xml num_tiles 0", "forests.xml",
	                 Bytes("<Forest num_tiles=\"0\" random_trees_per_tile=\"200\" gridded_trees_per_tile=\"0\" overlay_alpha=\"0.6\" "
	                       "space_between=\"2\"/>"),
	                 "forests.xml"});
	cases.push_back({"forests.xml without the element", "forests.xml", Bytes("<Nothing/>"), "forests.xml"});
	cases.push_back({"mask not L8", "forest_tile1.dds", ArgbDds(), "masks"});
	cases.push_back({"a `_2` model not a .gr2", "forest_europe_2.gr2", Bytes("not a granny file"), "the forest renderer did not start"});
	cases.push_back({"a `_2` texture not a DDS", "jungle_asia_2.dds", Bytes("not a dds"), "the forest renderer did not start"});
	cases.push_back({"the Generic Terrain ColorKey not a TGA", colorKey.c_str(), Bytes("xx"), "Terrain ColorKey"});
	for(const Broken& b : cases)
	{
		const std::wstring dir = Scratch();
		const bool wrote = WriteFile(dir, b.file, b.bytes);
		c5r::gamefs::GameFiles fs;
		c5r::gamefs::GameFiles::Options go;
		go.extraLooseRoots.push_back(dir);
		const c5r::Status os = fs.open(g_game, go);
		ForestSceneOptions o = Opts();
		o.files = &fs;
		const bool ok = wrote && os.ok() && ExpectInstallFailure(b.label, o, g_dev, b.needle, why);
		RemoveTree(dir);
		CHECK(wrote && os.ok(), std::string(b.label) + ": the loose folder: " + os.str());
		if(!ok)
			return false;
	}
	return true;
}

bool CaseFrameFailures(std::string& why)
{
	// the Screen Overlay target cannot be made: a frame of 0 x 0 (the creation's failure route; WARP made a 20,000 x 20,000
	// and a 2^20 x 1 target without an error on 2026-11-16, so no size provokes a real CreateTexture2D failure here)
	{
		Rig r(Opts());
		CHECK(r.ok, "rig: " + r.why);
		Target scene, trees;
		CHECK(scene.make() && trees.make(), "targets");
		const VPShellCamera c = MainCam();
		const c5t::Camera tc = TCam(c);
		const c5t::Options op = TerrainOptions(nullptr);
		const c5r::Status s = r.f->drawStage(g_ctx, trees.rtv, scene.dsv, 0, 0, c, *g_terrain, tc, op, kFowOff);
		std::string w;
		CHECK(!s.ok() && r.f->failed(w) && w.find("Screen Overlay target") != std::string::npos, "target: " + w);
		Fatal::Reset();
		bool done = false;
		ForestFatalStep(r.f.get(), &Fatal::Fn, done);
		CHECK(Fatal::calls == 1 && Fatal::code == 5, "target: fatal");
	}
	// the terrain without its install: no fog ramp
	{
		c5t::Renderer bare;
		c5r::Status s = bare.init(g_dev, g_exe, L"");
		CHECK(s.ok(), "bare terrain: " + s.str());
		s = bare.setHeightField(g_grid);
		CHECK(s.ok(), "bare field: " + s.str());
		Rig r(Opts());
		CHECK(r.ok, "rig: " + r.why);
		Target scene, trees;
		CHECK(scene.make() && trees.make(), "targets");
		const VPShellCamera c = MainCam();
		const c5t::Camera tc = TCam(c);
		const c5t::Options op = TerrainOptions(nullptr);
		s = r.f->drawStage(g_ctx, trees.rtv, scene.dsv, kW, kH, c, bare, tc, op, kFowOff);
		std::string w;
		CHECK(!s.ok() && r.f->failed(w) && w.find("fog ramp") != std::string::npos, "fog ramp: " + w);
		bare.release();
	}
	// the layouts refused for a field whose M_h2w is not invertible
	{
		c5h::HeightGrid bad = g_grid;
		memset(bad.hexToWorld, 0, sizeof(bad.hexToWorld));
		FakeTable* t = new FakeTable();
		t->rec = BaseTable();
		ForestScene f{std::unique_ptr<FowPlotTable>(t)};
		f.init(g_dev, Opts());
		f.onField(bad, 1);
		std::string w;
		CHECK(f.failed(w) && w.find("layouts were refused") != std::string::npos, "layouts: " + w);
	}
	return true;
}

bool CaseInPlayFaults(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	const VPShellCamera c = MainCam();
	const c5t::Camera tc = TCam(c);
	c5t::Options op = TerrainOptions(nullptr);
	c5t::FrameStats fs;
	scene.clear();
	c5r::Status s = g_terrain->draw(g_ctx, scene.rtv, scene.dsv, tc, op, &fs);
	CHECK(s.ok(), "terrain: " + s.str());
	op.overlay.proj[1][0] = std::nanf("");   // the Screen Overlay pass refuses it: a draw error in play
	for(int i = 0; i < 3; i++)
	{
		s = r.f->drawStage(g_ctx, trees.rtv, scene.dsv, kW, kH, c, *g_terrain, tc, op, kFowOff);
		CHECK(!s.ok(), "the bad overlay matrix was drawn");
	}
	std::string w;
	CHECK(!r.f->failed(w) && r.f->state() == ForestScene::kActive, "an in-play fault failed the install: " + w);
	CHECK(r.f->overlayErrors == 3 && CountLog("LOUD: forest: the Screen Overlay pass failed") == 3, "overlay errors not LOUD and counted");
	Fatal::Reset();
	bool done = false;
	ForestFatalStep(r.f.get(), &Fatal::Fn, done);
	CHECK(Fatal::calls == 0 && !done, "the fatal step ran on an in-play fault");
	// the next good frame draws
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	s = Frame(*r.f, scene, trees, c, ovl, &st);
	SafeRelease(ovl);
	CHECK(s.ok() && st.drawn, "the frame after the faults: " + s.str());
	return true;
}

bool CaseFieldReload(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	CHECK(s.ok() && st.builtNow > 0, "frame 1");
	const uint32_t vis = st.visible, seed1 = r.f->layoutSeed();
	const std::vector<c5f::PlotRecord> rec1 = r.f->placement().records();
	// the same load again: every block rebuilt, the layouts and records kept
	r.f->onField(g_grid, 1);
	CHECK(r.f->placement().builtCount() == 0 && r.f->layoutsMade == 1 && r.f->layoutSeed() == seed1, "same load");
	s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	CHECK(s.ok() && st.builtNow == vis, "same load: built " + S(st.builtNow));
	bool same = true;
	for(const c5f::PlotRecord& a : rec1)
		same = same && r.f->placement().record(a.plotX, a.plotY)->layout == a.layout;
	CHECK(same, "same load: the layout indices moved");
	// a new load: new layouts (another seed of the session's stream), records again
	r.f->onField(g_grid, 2);
	CHECK(r.f->layoutsMade == 2 && r.f->layoutSeed() != seed1 && r.f->loadSeq() == 2, "new load");
	CHECK(r.f->placement().recordCount() == kBaseForest + kBaseJungle, "new load: records");
	// a device rebuild keeps the records and draws again
	r.f->releaseDevice();
	r.f->init(g_dev, Opts());
	std::string w;
	CHECK(!r.f->failed(w) && r.f->placement().recordCount() == kBaseForest + kBaseJungle, "rebuild: " + w);
	s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	CHECK(s.ok() && st.drawn, "rebuild frame: " + s.str());
	SafeRelease(ovl);
	return true;
}

// The load's marks computed here independently: every record's plot built (buildPlot), its decal (shadowDecal), the
// plain squares of all of them (shadowChange's areaTiles).
std::vector<c5r::decal::TileOrigin> ExpectedMarks(const ForestScene& f)
{
	float wr[4];
	c5f::worldRectOf(g_grid, wr);
	std::vector<c5f::ShadowDecal> all;
	for(const c5f::PlotRecord& rec : f.placement().records())
	{
		c5f::PlotBlocks b;
		c5f::ShadowDecal d;
		c5f::EmptyCut cut;
		if(c5f::buildPlot(g_grid, f.placement().params(), f.placement().layouts()[rec.layout], rec, cut, c5f::BuildOptions(), b).ok() &&
		   c5f::shadowDecal(b, rec.plotX, rec.plotY, wr, d).ok() && d.quads)
			all.push_back(d);
	}
	std::vector<const c5f::ShadowDecal*> p;
	for(const auto& d : all)
		p.push_back(&d);
	c5f::ShadowChange ch;
	c5f::shadowChange(p.data(), p.size(), wr, ch);
	return ch.areaTiles;
}
bool SameTiles(const std::vector<c5r::decal::TileOrigin>& a, const std::vector<c5r::decal::TileOrigin>& b)
{
	std::set<std::pair<int, int>> x, y;
	for(const auto& t : a)
		x.insert(std::make_pair(t.x, t.y));
	for(const auto& t : b)
		y.insert(std::make_pair(t.x, t.y));
	return x == y && x.size() == a.size() && y.size() == b.size();
}
// A tap set of the field's world (no decals, generation gen), as the decal tap's reader would hand it over.
std::shared_ptr<c5r::decal::DecalSet> TapSet(uint32_t gen)
{
	std::shared_ptr<c5r::decal::DecalSet> t(new c5r::decal::DecalSet());
	t->generation = gen;
	t->loadSeq = 1;
	c5f::worldRectOf(g_grid, t->worldRect);
	return t;
}

bool CaseShadowSeam(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	HookLog hl;
	r.f->setPlotHook(&HookLog::Fn, &hl);
	// the load's marks: every forest / jungle plot's decal tiles, no decal drawn yet
	const std::vector<c5r::decal::TileOrigin> want = ExpectedMarks(*r.f);
	CHECK(!want.empty() && SameTiles(r.f->marks(), want), "marks " + S(r.f->marks().size()) + " != expected " + S(want.size()));
	CHECK(r.f->shadowCount() == 0, "a decal before any plot was in a view");
	std::vector<DecalChange> ch;
	CHECK(r.f->takeDecalChanges(ch) && ch.size() == 1, "no change for the load's marks");
	CHECK(ch[0].generation == kForestDecalGenBase + 1 && ch[0].rects.empty() && SameTiles(ch[0].areaTiles, want) && !ch[0].all,
	      "the marks' change: generation " + S(ch[0].generation) + " rects " + S(ch[0].rects.size()) + " area " +
	          S(ch[0].areaTiles.size()));
	ch.clear();
	CHECK(!r.f->takeDecalChanges(ch) && ch.empty(), "a second change without anything new");
	// a frame: the plots built in the view get their decals, one change with their rectangles
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	CHECK(s.ok() && hl.built == (int)st.builtNow && hl.built > 0, "hook builds " + S(hl.built));
	CHECK(r.f->shadowCount() == st.builtNow, "decals " + S(r.f->shadowCount()) + " built " + S(st.builtNow));
	CHECK(r.f->takeDecalChanges(ch) && ch.size() == 1 && ch[0].generation == kForestDecalGenBase + 2 &&
	          ch[0].rects.size() == st.builtNow,
	      "the builds' change");
	CHECK(SameTiles(r.f->marks(), want), "a build changed the marks");
	// composeDecals = mergeShadows: the tap's decals kept, the forest's appended, the generation the tap's
	std::shared_ptr<c5r::decal::DecalSet> tap = TapSet(5);
	std::shared_ptr<const c5r::decal::DecalSet> merged = r.f->composeDecals(tap);
	CHECK(merged && merged.get() != tap.get() && merged->decals.size() == r.f->shadowCount() &&
	          c5f::treeShadowCount(*merged) == r.f->shadowCount() && merged->generation == 5 && tap->decals.empty(),
	      "the merged set");
	CHECK(r.f->composeDecals(tap).get() == merged.get() && r.f->decalMerges == 1, "nothing changed but the merge ran again");
	// a built plot loses its forest: its decal goes, its rectangle in the next change, its tiles stay marked
	const c5f::ShadowDecal* d = r.f->shadowAt(2, 0);
	CHECK(d != nullptr, "plot 2,0 has no decal");
	const c5f::ShadowDecal old = *d;
	r.t->rec[2] = Plot(-1);
	SrvFeed::Batch b;
	b.snapshotGen = r.t->gen;
	b.width = kTabW;
	b.height = kTabH;
	b.changedPlots = {2};
	b.events = {FeatureEvent(2, -1)};
	CHECK(r.f->onBatch(b), "the removal did nothing");
	CHECK(!r.f->shadowAt(2, 0) && r.f->shadowCount() == st.builtNow - 1, "the decal of 2,0 stayed");
	ch.clear();
	CHECK(r.f->takeDecalChanges(ch) && ch.size() == 1 && ch[0].rects.size() == 1 &&
	          memcmp(ch[0].rects[0].v, old.rect, sizeof(old.rect)) == 0 && ch[0].generation == kForestDecalGenBase + 3,
	      "the drop's change");
	CHECK(SameTiles(r.f->marks(), want), "the drop changed the marks (stock's bit stays)");
	std::shared_ptr<const c5r::decal::DecalSet> merged2 = r.f->composeDecals(tap);
	CHECK(merged2.get() != merged.get() && merged2->decals.size() == r.f->shadowCount() && r.f->decalMerges == 2, "the merge after the drop");
	// a tap set that holds Tree_Shadow decals (the EXE's Forest on): refused LOUD, the tap's set as it is, never fatal
	std::shared_ptr<const c5r::decal::DecalSet> withForest = merged2;   // a set with Tree_Shadow decals, as a tap would carry it
	CHECK(r.f->composeDecals(withForest).get() == withForest.get() && r.f->tapShadowRefusals == 1 &&
	          CountLog("already holds") == 1,
	      "a tap set with Tree_Shadow decals was not refused LOUD");
	std::string w;
	CHECK(!r.f->failed(w) && r.f->state() == ForestScene::kActive, "the refusal failed the scene: " + w);
	// a new load: the marks start again (computed for the new layouts), the decals dropped
	r.f->onField(g_grid, 2);
	CHECK(r.f->shadowCount() == 0 && SameTiles(r.f->marks(), ExpectedMarks(*r.f)), "a new load's marks");
	SafeRelease(ovl);
	return true;
}

// Epic 0465, ticket 4904 (MC #6736): a live window on the same load (expectWindows, then onField) builds again only the
// built plots whose trees read the window, drops and re-pends only their Tree_Shadow decals (no whole-map drop: the hook
// never sees x = -1), leaves the other plots' blocks built, and puts the touched trees on the new ground; the same onField
// without boxes still takes the whole-map path (every block and decal dropped).
bool CaseLiveWindow(std::string& why)
{
	c5h::HeightGrid g2 = g_grid;   // declared before the rig: the scene holds a pointer to it until the rig goes
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	HookLog hl;
	r.f->setPlotHook(&HookLog::Fn, &hl);
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	SafeRelease(ovl);
	CHECK(s.ok() && st.builtNow > 1 && r.f->shadowCount() == st.builtNow, "frame: built " + S(st.builtNow) + " decals " + S(r.f->shadowCount()));
	std::vector<DecalChange> ch;
	r.f->takeDecalChanges(ch);
	ch.clear();
	const size_t built0 = r.f->placement().builtCount(), decals0 = r.f->shadowCount();
	const std::vector<c5r::decal::TileOrigin> marks0 = r.f->marks();
	// a box of 9 x 9 samples around the first drawn tree of plot 2,0, raised by 5000 (z + 3.92)
	c5f::EmptyCut cut;
	const c5f::PlotBlocks* b = nullptr;
	CHECK(r.f->shadowAt(2, 0) && r.f->placement().record(2, 0) && const_cast<c5f::Placement&>(r.f->placement()).blocks(g_grid, cut, 2, 0, &b).ok() && b &&
	          !b->drawn.empty(),
	      "plot 2,0 not built");
	const int cx = (int)((g_grid.ox + b->drawn[0].worldX) / g_grid.sx), cy = (int)((g_grid.oy + b->drawn[0].worldY) / g_grid.sy);
	liveterrain::SampleBox box;
	box.x0 = cx - 4;
	box.x1 = cx + 4;
	box.y0 = cy - 4;
	box.y1 = cy + 4;
	CHECK(box.x0 >= 0 && box.y0 >= 0 && box.x1 < (int)g2.samplesX && box.y1 < (int)g2.samplesY, "the box leaves the grid");
	for(int y = box.y0; y <= box.y1; ++y)
		for(int x = box.x0; x <= box.x1; ++x)
			g2.h[g2.index((uint32_t)x, (uint32_t)y)] = (uint16_t)(kGround + 5000);
	hl = HookLog();
	CHECK(liveterrain::DerivedAfterWindow(r.f.get(), {box}) == 1, "the entry point refused the box");
	r.f->onField(g2, 1);
	const liveterrain::ReheightStats& rs = r.f->lastReheight;
	CHECK(hl.all == 0, "a whole-map drop (hook x = -1) on a live window");
	CHECK(rs.failed == 0 && rs.rebuilt >= 1 && rs.rebuilt < st.builtNow && rs.builtSeen == built0 && hl.built == (int)rs.rebuilt,
	      "rebuilt " + S(rs.rebuilt) + " of " + S(rs.builtSeen) + ", hook builds " + S(hl.built));
	CHECK(r.f->placement().builtCount() == built0, "the untouched plots lost their blocks: built " + S(r.f->placement().builtCount()));
	CHECK(r.f->shadowCount() == decals0 || r.f->shadowCount() + rs.rebuilt >= decals0, "decals " + S(r.f->shadowCount()));
	CHECK(SameTiles(r.f->marks(), marks0), "the live window changed the marks");
	// the decal change names at most the touched plots' rectangles (the old and the new of each), never the map's
	const bool any = r.f->takeDecalChanges(ch);
	CHECK(!any || (ch.size() == 1 && ch[0].rects.size() <= 2 * (size_t)rs.rebuilt && !ch[0].all),
	      "the decal change: " + S(ch.empty() ? 0 : ch[0].rects.size()) + " rects for " + S(rs.rebuilt) + " plots");
	// the touched trees on the new ground
	const c5f::PlotBlocks* nb = nullptr;
	CHECK(const_cast<c5f::Placement&>(r.f->placement()).blocks(g2, cut, 2, 0, &nb).ok() && nb, "plot 2,0 after");
	int raised = 0;
	for(const c5f::DrawnTree& d : nb->drawn)
	{
		const int c = (int)((g2.ox + d.worldX) / g2.sx), rr = (int)((g2.oy + d.worldY) / g2.sy);
		CHECK(d.z == c5h::heightAtSample(g2, c, rr), "a tree off the new ground");
		raised += d.z > kGround / 255.0f * 0.2f + 1.0f;
	}
	CHECK(raised > 0, "no tree on the raised box");
	// the fallback: the same load again without boxes drops every block and decal (the path of a device rebuild)
	hl = HookLog();
	r.f->onField(g2, 1);
	CHECK(hl.all == 1 && r.f->placement().builtCount() == 0 && r.f->shadowCount() == 0, "the fallback path");
	printf("  live window: %u of %u built plots built again, %d trees on the raised box, %zu decal rects, %.3f ms\n", rs.rebuilt,
	       rs.builtSeen, raised, ch.empty() ? (size_t)0 : ch[0].rects.size(), rs.ms);
	return true;
}

// The forest's DecalChange through the residency's own decalChanges (srv_residency_source.cpp, the stand-in baker): a
// change with the tap's generation in force at the field is skipped (trees.md 12.4's trap); the forest's band is applied.
bool CaseResidencyGeneration(std::string& why)
{
	Rig r(Opts());
	CHECK(r.ok, "rig: " + r.why);
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	SafeRelease(ovl);
	CHECK(s.ok() && r.f->shadowCount() > 0, "no decals: " + s.str());
	std::vector<DecalChange> ch;
	r.f->takeDecalChanges(ch);   // the marks' change and the builds' change, merged into one by the scene
	CHECK(ch.size() == 1 && !ch[0].rects.empty(), "the forest's change");
	ResidencySourceConfig rc;
	rc.pipeline = false;          // the stand-in baker
	rc.install = g_game;
	rc.exe = g_exe;
	rc.decals = true;
	rc.decalAheadWaitMs = 0;
	ResidencySource res(rc);
	s = res.init(g_dev, g_ctx);
	CHECK(s.ok(), "residency init: " + s.str());
	std::shared_ptr<c5r::decal::DecalSet> tap = TapSet(5);
	std::string w;
	CHECK(res.setDecals(r.f->composeDecals(tap), w), "setDecals refused the merged set: " + w);   // P13: the set first
	res.onField(g_dev, g_grid, 1, 1);   // nothing resident, generation 5 in force: changes up to 5 need no re-bake
	DecalChange trap = ch[0];
	trap.generation = tap->generation;   // the change sent with the tap's generation
	trap.previousGeneration = 0;
	res.decalChanges(std::vector<DecalChange>(1, trap), 0);
	const DecalBakeCounters c1 = res.decalCounters();
	CHECK(c1.changesSkipped == 1 && c1.changes == 0, "the tap generation's change: skipped " + S(c1.changesSkipped) + " applied " +
	                                                     S(c1.changes));
	res.decalChanges(ch, 0);
	const DecalBakeCounters c2 = res.decalCounters();
	printf("    info: residency: the forest change (generation 0x%08x, %u rects): applied %lld, skipped %lld, tiles raised %lld\n",
	       ch[0].generation, (unsigned)ch[0].rects.size(), (long long)c2.changes, (long long)c2.changesSkipped, (long long)c2.raises);
	CHECK(c2.changes == 1 && c2.changesSkipped == 1 && c2.raises > 0, "the forest's change was not applied: raises " + S(c2.raises));
	res.release();
	return true;
}

// ---- item 3: the wiring in Scene and the host (trees.md 13.6) ----------------------------------------------------------
// Scene in the offline player's form (no section: init, loadField, the feed, poll, draw), the feed's real table
// (SrvFeed::BeginSnapshot / SetPlot / EndSnapshot / Drain), render_tiles=none, no water, no overlay, no fog.
std::vector<uint32_t> SceneFrameOf(int forest, bool ids, const SrvFeed::Batch& batch, std::string& why, int* treesPlots = nullptr)
{
	std::vector<uint32_t> px;
	ForestFeatureIdsReset();
	Scene sc;
	SceneOptions so;
	so.exe = g_exe;
	so.install = g_game;
	so.tiles = SceneOptions::kTilesNone;
	so.water = false;
	so.overlay = 0;
	so.fow = 0;
	so.forest = forest;
	so.forestSeed = 4554;
	c5r::Status s = sc.init(g_dev, g_ctx, so);
	if(!s.ok())
		return why = "Scene::init: " + s.str(), px;
	if((forest != 0) != (sc.forest() != nullptr))
		return why = "Scene::forest() does not follow render_forest", px;
	std::vector<uint8_t> tap;
	s = c5h::writeTap(g_grid, tap);
	if(s.ok())
		s = sc.loadField(tap.data(), tap.size(), 1);
	if(!s.ok())
		return why = "loadField: " + s.str(), px;
	sc.forestFeed(batch);
	if(ids)
	{
		ForestFeatureTagSeen(kForestId, kForestArtTag);
		ForestFeatureTagSeen(kJungleId, kJungleArtTag);
		ForestFeatureIdsResolved();
	}
	sc.poll();
	Target t;
	if(!t.make())
		return why = "target", px;
	t.clear();
	SceneFrame f;
	f.cam = MainCam();
	f.frameId = 1;
	f.nowMs = 0;
	c5t::FrameStats fs;
	s = sc.draw(g_ctx, t.rtv, t.dsv, kW, kH, f, fs);
	if(!s.ok())
		return why = "Scene::draw: " + s.str(), px;
	if(treesPlots)
		*treesPlots = sc.forest() ? (int)static_cast<ForestScene*>(sc.forest())->lastStage().visible : 0;
	char line[4096];
	sc.secondLine(line, sizeof(line));
	sc.totalsLine(line, sizeof(line));
	std::string w;
	if(sc.forest() && sc.forest()->failed(w))
		return why = "the forest failed: " + w, px;
	px = ReadBack(t.tex);
	return px;
}

bool CaseSceneWiring(std::string& why)
{
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch drained;
	SrvFeed::Drain(drained);   // whatever an earlier case left
	SrvFeed::BeginSnapshot(kTabW, kTabH);
	const std::vector<SrvFeed::PlotRecord> table = BaseTable();
	for(uint32_t i = 0; i < table.size(); i++)
		SrvFeed::SetPlot(i, table[i]);
	SrvFeed::EndSnapshot();
	SrvFeed::Batch batch;
	CHECK(SrvFeed::Drain(batch) && batch.width == kTabW && batch.height == kTabH, "the feed's snapshot batch");
	int plots0 = 0, plots1 = 0, plots2 = 0;
	const std::vector<uint32_t> off = SceneFrameOf(0, true, batch, why, &plots0);
	CHECK(!off.empty(), "render_forest=0: " + why);
	CHECK(CountLog("render_forest=0: no trees") >= 1, "render_forest=0 not said");
	const std::vector<uint32_t> noIds = SceneFrameOf(1, false, batch, why, &plots1);
	CHECK(!noIds.empty(), "render_forest=1 without ids: " + why);
	const std::vector<uint32_t> on = SceneFrameOf(1, true, batch, why, &plots2);
	CHECK(!on.empty(), "render_forest=1 with ids: " + why);
	int diffNoIds = 0, diffOn = 0;
	for(size_t i = 0; i < off.size(); i++)
	{
		diffNoIds += off[i] != noIds[i];
		diffOn += off[i] != on[i];
	}
	printf("    info: scene: render_forest=0 vs 1 without ids: %d of %u pixels differ; with ids: %d differ (%d plots drawn)\n", diffNoIds,
	       kW * kH, diffOn, plots2);
	CHECK(diffNoIds == 0 && plots1 == 0, "render_forest=1 without feature ids is not the render_forest=0 frame: " + S(diffNoIds));
	CHECK(diffOn > 1000 && plots2 > 0, "render_forest=1 with ids drew no trees: " + S(diffOn));
	// the 1-second line goes to render.log only (RLog); the totals line through the host's callback (RNote)
	CHECK(CountLog("forest totals:") == 2, "the forest's totals line is not in the scene's: " + S(CountLog("forest totals:")));
	SrvFeed::SetEnabled(false);
	return true;
}

bool CaseHostConfig(std::string& why)
{
	auto parse = [](const char* key, const char* value, std::string& w) {
		HostConfig hc;
		return ParseHostConfig([&](const char* k) { return strcmp(k, key) == 0 ? std::string(value) : std::string(); }, "", "", hc, w);
	};
	std::string w;
	CHECK(parse("render_forest", "1", w) && parse("render_forest", "0", w), "render_forest 0 / 1 refused: " + w);
	CHECK(!parse("render_forest", "2", w) && w.find("render_forest") != std::string::npos, "render_forest=2 accepted");
	CHECK(!parse("render_test_forest_ids_wait_ms", "-5", w) && w.find("render_test_forest_ids_wait_ms") != std::string::npos,
	      "a negative ids wait accepted");
	CHECK(parse("render_test_forest_seed", "77", w) && !parse("render_test_forest_seed", "x", w), "render_test_forest_seed");
	CHECK(parse("render_test_cut_fail", "1", w) && !parse("render_test_cut_fail", "yes", w), "render_test_cut_fail");
	CHECK(parse("render_test_cut_wait_ms", "0", w) && !parse("render_test_cut_wait_ms", "-1", w), "render_test_cut_wait_ms");
	return true;
}

// ---- item 3 (lead-0431 18:00): ticket 4553's cut tap in the forest scene ----------------------------------------------
// The fake producer: copied from server/tests/render/cut_tap/cut_tap_test.cpp (this repository, ticket 4553, Producer and
// MakeImg) and cut to what these cases need; the image is the grid of this test's field (512 x 640 samples: 256 x 320
// cells, 16 x 20 tiles), every tile a constant byte.
struct CutProducer
{
	std::wstring ctlName;
	HANDLE ctl = nullptr;
	VPShellCutTapControl* blk = nullptr;
	std::vector<HANDLE> images;
	uint32_t pid = GetCurrentProcessId();
	explicit CutProducer(const wchar_t* tag)
	{
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\VPShellCutTapForestTest.%u.%ls", pid, tag);
		ctlName = n;
		ctl = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, VPSHELL_CUTTAP_CONTROL_BYTES, n);
		blk = ctl ? (VPShellCutTapControl*)MapViewOfFile(ctl, FILE_MAP_WRITE, 0, 0, VPSHELL_CUTTAP_CONTROL_BYTES) : nullptr;
	}
	~CutProducer()
	{
		for(HANDLE h : images)
			CloseHandle(h);
		if(blk)
			UnmapViewOfFile(blk);
		if(ctl)
			CloseHandle(ctl);
	}
	static std::vector<uint8_t> Image(uint32_t generation, uint32_t pid, uint8_t cell)
	{
		const uint32_t tx = 16, ty = 20;
		VPShellCutImage h = {};
		h.magic = VPSHELL_CUT_MAGIC;
		h.version = VPSHELL_CUT_VERSION;
		h.headerBytes = sizeof(VPShellCutImage);
		h.generation = generation;
		h.loadSeq = 1;
		h.exePid = pid;
		h.widthCells = tx * 16;
		h.heightCells = ty * 16;
		h.tilesX = tx;
		h.tilesY = ty;
		h.samplesX = g_grid.samplesX;
		h.samplesY = g_grid.samplesY;
		h.ox = g_grid.ox;
		h.oy = g_grid.oy;
		h.poolBlocks = 1;
		h.pooledTiles = 0;
		h.dirOffset = sizeof(VPShellCutImage);
		h.dirBytes = tx * ty * 8;
		h.poolOffset = (h.dirOffset + h.dirBytes + 15) & ~15u;
		h.poolBytes = 256;
		h.imageBytes = h.poolOffset + h.poolBytes;
		h.gridHeader[0] = 1;
		const uint32_t used = 0;
		memcpy(h.gridHeader + VPCUT_G_USED, &used, 4);
		std::vector<uint8_t> b((size_t)h.imageBytes, 0);
		memcpy(b.data(), &h, sizeof(h));
		for(uint32_t t = 0; t < tx * ty; ++t)
		{
			uint8_t* e = b.data() + h.dirOffset + t * 8;
			const uint16_t st = 1;
			memcpy(e, &st, 2);
			e[4] = cell;
		}
		return b;
	}
	bool section(uint32_t g, const std::vector<uint8_t>& bytes)
	{
		wchar_t n[64];
		_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_CUT_IMAGE_FMT, pid, g);
		HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, (DWORD)bytes.size(), n);
		if(!h)
			return false;
		void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0);
		if(!v)
			return false;
		memcpy(v, bytes.data(), bytes.size());
		UnmapViewOfFile(v);
		images.push_back(h);
		return true;
	}
	void publish(uint32_t state, uint32_t g, const std::vector<uint8_t>& bytes, uint32_t fail = 0, const char* text = "")
	{
		VPShellCutTapControl c;
		memcpy(&c, blk, sizeof(c));
		c.magic = VPSHELL_CUT_MAGIC;
		c.version = VPSHELL_CUT_VERSION;
		c.headerBytes = sizeof(VPShellCutTapControl);
		c.state = state;
		c.failReason = fail;
		c.exePid = pid;
		c.generation = g;
		c.loadSeq = 1;
		c.tilesX = bytes.empty() ? 0 : 16;
		c.tilesY = bytes.empty() ? 0 : 20;
		c.poolBlocks = bytes.empty() ? 0 : 1;
		c.imageBytes = bytes.size();
		c.fnv1a64 = bytes.empty() ? 0 : VPCutFnv(VPCUT_FNV_INIT, bytes.data(), bytes.size());
		_snwprintf_s(c.imageName, _countof(c.imageName), _TRUNCATE, g ? VPSHELL_CUT_IMAGE_FMT : L"", pid, g);
		strncpy_s(c.failText, text, _TRUNCATE);
		InterlockedIncrement((volatile LONG*)&blk->seq);
		MemoryBarrier();
		const uint32_t seq = blk->seq;
		const uint32_t ack = blk->ackGeneration, ackPid = blk->ackServerPid;
		const int32_t ackRes = blk->ackResult;
		memcpy(blk, &c, sizeof(c));
		blk->seq = seq;
		blk->ackGeneration = ack;
		blk->ackServerPid = ackPid;
		blk->ackResult = ackRes;
		MemoryBarrier();
		InterlockedIncrement((volatile LONG*)&blk->seq);
	}
	bool good(uint32_t g, uint8_t cell)
	{
		const std::vector<uint8_t> b = Image(g, pid, cell);
		if(!section(g, b))
			return false;
		publish(VPCUT_READY, g, b);
		return true;
	}
};

ForestSceneOptions CutOpts(const CutProducer& p)
{
	ForestSceneOptions o = Opts();
	o.cutControlName = p.ctlName;
	return o;
}

bool CaseCutTapRebuild(std::string& why)
{
	CutProducer p(L"rebuild");
	CHECK(p.blk != nullptr, "the fake control section");
	CHECK(p.good(1, 0x00), "image 1");
	Rig r(CutOpts(p));
	CHECK(r.ok, "rig: " + r.why);
	r.f->setExePid(p.pid);
	r.f->poll();
	CHECK(r.f->cutReader() && r.f->cutReader()->hasGrid() && r.f->cutReader()->imageGeneration() == 1, "the reader took no grid");
	Target scene, trees;
	CHECK(scene.make() && trees.make(), "targets");
	ID3D11ShaderResourceView* ovl = Texture(0);
	ForestStageStats st;
	c5r::Status s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	const int before = CountAlpha(ReadBack(trees.tex));
	const size_t decals = r.f->shadowCount();
	CHECK(s.ok() && before > 1000 && decals == st.builtNow && decals > 0, "frame with an empty cut: " + S(before) + " pixels");
	std::vector<DecalChange> ch;
	r.f->takeDecalChanges(ch);
	// the same content again (generation 2, nothing cut): every built plot rebuilt, no decal changed
	CHECK(p.good(2, 0x00), "image 2");
	CHECK(r.f->poll(), "generation 2 was not taken");
	CHECK(r.f->cutRebuilds == (int64_t)st.builtNow && r.f->shadowCount() == decals, "an unchanged grid changed the decals");
	ch.clear();
	CHECK(!r.f->takeDecalChanges(ch), "an unchanged grid made a decal change");
	// every cell cut (bits 0-2): every built plot rebuilt with no drawn tree, its decal dropped
	CHECK(p.good(3, 0x07), "image 3");
	CHECK(r.f->poll() && r.f->cutReader()->imageGeneration() == 3, "generation 3 was not taken");
	// a decal stays only for a plot with a tree past the grid's edge (read 0, as the EXE's lookup: cut.h; on this wrapping
	// field the plots at the east edge reach past sample 512), and then it equals the plot built under the same grid
	size_t keep = 0;
	{
		c5f::DenseCut full;
		CHECK(full.set(256, 320, std::vector<uint8_t>(256 * 320, 0x07), g_grid.ox, g_grid.oy).ok(), "DenseCut");
		for(const c5f::PlotRecord& rec : r.f->placement().records())
		{
			const c5f::ShadowDecal* d = r.f->shadowAt(rec.plotX, rec.plotY);
			if(!d)
				continue;
			c5f::PlotBlocks b;
			CHECK(c5f::buildPlot(g_grid, r.f->placement().params(), r.f->placement().layouts()[rec.layout], rec, full, c5f::BuildOptions(),
			                     b)
			              .ok() &&
			          !b.drawn.empty() && d->quads == b.drawn.size(),
			      "plot " + S(rec.plotX) + "," + S(rec.plotY) + " kept a decal unlike its trees under the full cut");
			keep++;
		}
	}
	CHECK(keep < decals / 2, "decals left under a full cut: " + S(keep) + " of " + S(decals));
	ch.clear();
	CHECK(r.f->takeDecalChanges(ch) && ch.size() == 1 && ch[0].rects.size() >= decals - keep, "the dropped decals' change");
	s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	const int after = CountAlpha(ReadBack(trees.tex));
	CHECK(s.ok() && after < before / 4, "trees drawn under a full cut: " + S(after) + " of " + S(before));
	// bit 3 only: not a tree bit, the trees come back with their decals
	CHECK(p.good(4, 0x08), "image 4");
	CHECK(r.f->poll(), "generation 4 was not taken");
	CHECK(r.f->shadowCount() == decals, "decals not back with bit 3 only: " + S(r.f->shadowCount()));
	s = Frame(*r.f, scene, trees, MainCam(), ovl, &st);
	CHECK(s.ok() && CountAlpha(ReadBack(trees.tex)) == before, "the trees with bit 3 only differ from the empty cut's");
	printf("    info: cut: empty grid %d tree pixels, %u decals; full cut %d pixels, %u decals (trees past the grid's east edge read "
	       "0, as the EXE); bit 3 only %d pixels\n",
	       before, (unsigned)decals, after, (unsigned)keep, before);
	SafeRelease(ovl);
	std::string w;
	CHECK(!r.f->failed(w), "failed: " + w);
	return true;
}

bool CaseCutTapFailures(std::string& why)
{
	{   // the producer FAILED: the install failure, fatal(5) once
		CutProducer p(L"failed");
		p.publish(VPCUT_FAILED, 0, std::vector<uint8_t>(), 1, "a byte pattern of the EXE did not match");
		Rig r(CutOpts(p));
		CHECK(r.ok, "rig: " + r.why);
		r.f->setExePid(p.pid);
		r.f->poll();
		std::string w;
		CHECK(r.f->failed(w) && w.find("cut-mask tap could not install") != std::string::npos, "FAILED: " + w);
		Fatal::Reset();
		bool done = false;
		ForestFatalStep(r.f.get(), &Fatal::Fn, done);
		ForestFatalStep(r.f.get(), &Fatal::Fn, done);
		CHECK(Fatal::calls == 1 && Fatal::code == 5, "FAILED: fatal " + S(Fatal::calls));
	}
	{   // the first image refused (a hash that does not match): the install failure
		CutProducer p(L"refused");
		std::vector<uint8_t> b = CutProducer::Image(1, p.pid, 0);
		CHECK(p.section(1, b), "section");
		b[200] ^= 1;   // the block's hash is over the changed bytes; the section holds the original ones
		p.publish(VPCUT_READY, 1, b);
		Rig r(CutOpts(p));
		CHECK(r.ok, "rig: " + r.why);
		r.f->setExePid(p.pid);
		r.f->poll();
		std::string w;
		CHECK(r.f->failed(w) && w.find("cut-mask tap could not install") != std::string::npos, "refused first image: " + w);
	}
	{   // no control block within the bound after the field: the install failure naming the state
		ForestSceneOptions o = Opts();
		o.cutControlName = L"Local\\VPShellCutTapForestTest.none";
		o.cutWaitMs = 1;
		Rig r(o);
		CHECK(r.ok, "rig: " + r.why);
		r.f->setExePid(GetCurrentProcessId());
		r.f->poll();
		Sleep(20);
		r.f->poll();
		std::string w;
		CHECK(r.f->failed(w) && w.find("no cut-mask grid") != std::string::npos && w.find("ABSENT") != std::string::npos,
		      "absent tap: " + w);
	}
	{   // the tap OFF (cut_tap=0, a test switch): never an install failure, the trees drawn with an empty mask
		CutProducer p(L"off");
		p.publish(VPCUT_OFF, 0, std::vector<uint8_t>());
		ForestSceneOptions o = CutOpts(p);
		o.cutWaitMs = 1;
		Rig r(o);
		CHECK(r.ok, "rig: " + r.why);
		r.f->setExePid(p.pid);
		r.f->poll();
		Sleep(20);
		r.f->poll();
		std::string w;
		CHECK(!r.f->failed(w) && r.f->cutReader()->off(), "OFF failed the scene: " + w);
	}
	{   // render_test_cut_fail=1: the install failure on purpose
		ForestSceneOptions o = Opts();
		o.cutForceFail = true;
		Rig r(o);
		CHECK(r.ok, "rig: " + r.why);
		CHECK(!r.f->failed(r.why), "failed before a pid");
		r.f->setExePid(GetCurrentProcessId());
		r.f->poll();
		std::string w;
		CHECK(r.f->failed(w) && w.find("render_test_cut_fail=1") != std::string::npos, "forced failure: " + w);
	}
	{   // no pid (the offline player, a scene without a section): no reader, EmptyCut, never a failure
		ForestSceneOptions o = Opts();
		o.cutWaitMs = 1;
		Rig r(o);
		CHECK(r.ok, "rig: " + r.why);
		Sleep(20);
		r.f->poll();
		std::string w;
		CHECK(!r.f->failed(w) && !r.f->cutReader(), "no pid: " + w);
	}
	return true;
}

bool CaseFactory(std::string& why)
{
	// this program links server/src/forest/: the factory is registered at static initialisation
	std::unique_ptr<SceneForest> f = MakeSceneForest(std::unique_ptr<FowPlotTable>(new FakeTable()));
	CHECK(f && dynamic_cast<ForestScene*>(f.get()) != nullptr, "MakeSceneForest returned no ForestScene");
	f->init(g_dev, ForestSceneOptions());   // on false: off
	std::string w;
	CHECK(!f->failed(w), "an off scene failed");
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
};
const Case kCases[] = {
	{"snapshot", CaseSnapshot},
	{"feature_change", CaseFeatureChange},
	{"overflow", CaseOverflow},
	{"ids_by_name", CaseIdsByName},
	{"ids_never_published", CaseIdsNeverPublished},
	{"visibility", CaseVisibility},
	{"stage_overlay_before_trees", CaseStageOverlay},
	{"render_forest_off", CaseOff},
	{"install_failures", CaseInstallFailures},
	{"frame_failures", CaseFrameFailures},
	{"in_play_faults", CaseInPlayFaults},
	{"field_reload", CaseFieldReload},
	{"shadow_seam", CaseShadowSeam},
	{"live_window", CaseLiveWindow},   // epic 0465, ticket 4904
	{"residency_generation", CaseResidencyGeneration},
	{"scene_wiring", CaseSceneWiring},
	{"host_config", CaseHostConfig},
	{"cut_tap_rebuild", CaseCutTapRebuild},
	{"cut_tap_failures", CaseCutTapFailures},
	{"factory", CaseFactory},
};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	const char* e = getenv("C5R_EXE");
	const char* gd = getenv("C5R_GAME_DIR");
	if(!e || !*e || !gd || !*gd)
	{
		printf("test_forest_scene: C5R_EXE and C5R_GAME_DIR must be set (the EXE's programs, the install's files; never a skip)\n");
		return 99;
	}
	g_exe.assign(e, e + strlen(e));
	g_game.assign(gd, gd + strlen(gd));
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("test_forest_scene: no WARP device\n");
		return 99;
	}
	c5r::Status s = g_files.open(g_game);
	g_grid = MakeGrid();
	g_terrain = new c5t::Renderer();
	if(s.ok())
		s = g_terrain->init(g_dev, g_exe, g_files);
	if(s.ok())
		s = g_terrain->setHeightField(g_grid);
	if(!s.ok())
	{
		printf("test_forest_scene: the install or the terrain did not start: %s\n", s.str().c_str());
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
		ForestFeatureIdsReset();
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
		printf("test_forest_scene: no case matches\n");
		return 2;
	}
	g_terrain->release();
	delete g_terrain;
	g_ctx->Release();
	g_dev->Release();
	return fails;
}
