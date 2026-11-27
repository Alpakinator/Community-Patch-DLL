// srv_forest_scene.cpp - the forest and jungle trees of the renderer inside server.exe (epic 0431, ticket 4554).
// Written 2026-11-16 for this repository (GPL-3.0-or-later). What it does: srv_forest_scene.h; the design and the
// measurements behind each rule: research/05-graphics-composition/trees.md 2, 9, 10, 11 and 13.
#include "srv_forest_scene.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/bake/post.h"
#include "c5r/shaders/shadersection.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <unordered_map>

namespace srvrender {

namespace c5t = c5r::terrain;
namespace c5f = c5r::forest;
namespace c5h = c5r::heightfield;

static_assert(sizeof(c5f::Instance) == 16, "cb7 row: 4 floats, no padding");

// ---- helpers --------------------------------------------------------------------------------------------------------
namespace {

// (hx, hy, hz, 1) x M, x and y (row vectors, D3DX row-major), in float as c5r::forest's placement computes it.
void TransformXY(const float m[16], float hx, float hy, float hz, float& x, float& y)
{
	x = ((hx * m[0] + hy * m[4]) + hz * m[8]) + m[12];
	y = ((hx * m[1] + hy * m[5]) + hz * m[9]) + m[13];
}

void Mul44(const float* a, const float* b, float* out)
{
	for(int r = 0; r < 4; r++)
		for(int c = 0; c < 4; c++)
			out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

bool Finite(const float* v, int n)
{
	for(int i = 0; i < n; i++)
		if(!std::isfinite(v[i]))
			return false;
	return true;
}

// The six planes of viewProj (row vectors: clip = (p, 1) x M), each (a, b, c, d) with (a, b, c) of unit length, inside =
// a x + b y + c z + d >= 0: left w + x, right w - x, bottom w + y, top w - y, near z (D3D: z >= 0), far w - z.
bool Planes(const float* m, float planes[6][4])
{
	float col[4][4];
	for(int j = 0; j < 4; j++)
		for(int i = 0; i < 4; i++)
			col[j][i] = m[i * 4 + j];
	const float* x = col[0];
	const float* y = col[1];
	const float* z = col[2];
	const float* w = col[3];
	for(int i = 0; i < 4; i++)
	{
		planes[0][i] = w[i] + x[i];
		planes[1][i] = w[i] - x[i];
		planes[2][i] = w[i] + y[i];
		planes[3][i] = w[i] - y[i];
		planes[4][i] = z[i];
		planes[5][i] = w[i] - z[i];
	}
	for(int p = 0; p < 6; p++)
	{
		const float l = std::sqrt(planes[p][0] * planes[p][0] + planes[p][1] * planes[p][1] + planes[p][2] * planes[p][2]);
		if(!(l > 0) || !std::isfinite(l))
			return false;
		for(int i = 0; i < 4; i++)
			planes[p][i] /= l;
	}
	return true;
}

bool InsidePlanes(const float planes[6][4], const float c[3], float margin)
{
	for(int p = 0; p < 6; p++)
		if(planes[p][0] * c[0] + planes[p][1] * c[1] + planes[p][2] * c[2] + planes[p][3] < -margin)
			return false;
	return true;
}

// The wrap copy k (-1, 0, +1) view v of the camera block shows: rows 0-2 equal to view 0's and row 3 = view 0's row 3 +
// k x wrapPeriod x row 0 (the EXE's wrap views, trees.md 11.1; ShadowCopiesFromFrame's rule). Unmatched: 0, *matched false.
int CopyOfView(const VPShellCamera& c, uint32_t v, bool* matched)
{
	*matched = true;
	if(v == 0)
		return 0;
	const float* a = c.view[0];
	const float* b = c.view[v];
	const float P = c.wrapPeriod;
	float big = 1.0f;
	for(int i = 0; i < 16; i++)
		big = std::max(big, std::fabs(a[i]));
	if(std::isfinite(P) && P > 0)
		big = std::max(big, P * std::max(std::fabs(a[0]), std::max(std::fabs(a[1]), std::fabs(a[2]))));
	const float tol = 1e-5f * big;
	for(int k = -1; k <= 1; k += 2)
	{
		if(!(std::isfinite(P) && P > 0))
			break;
		bool ok = true;
		for(int i = 0; i < 12 && ok; i++)
			ok = std::fabs(a[i] - b[i]) <= tol;
		for(int i = 0; i < 3 && ok; i++)
			ok = std::fabs(a[12 + i] + (float)k * P * a[i] - b[12 + i]) <= tol;
		if(ok && std::fabs(a[15] - b[15]) <= tol)
			return k;
	}
	*matched = false;
	return 0;
}

ID3D11ShaderResourceView* OnePixel(ID3D11Device* dev, uint32_t rgba)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = 1;
	d.MipLevels = d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA s = {&rgba, 4, 4};
	ID3D11Texture2D* t = nullptr;
	ID3D11ShaderResourceView* v = nullptr;
	if(FAILED(dev->CreateTexture2D(&d, &s, &t)))
		return nullptr;
	if(FAILED(dev->CreateShaderResourceView(t, nullptr, &v)))
		v = nullptr;
	t->Release();
	return v;
}

uint64_t Key(int32_t x, int32_t y) { return ((uint64_t)(uint32_t)y << 32) | (uint32_t)x; }

}  // namespace

// the factory (srv_forest_scene.h SceneForest): registered when this unit is linked
static std::unique_ptr<SceneForest> MakeForestScene(std::unique_ptr<FowPlotTable> table)
{
	return std::unique_ptr<SceneForest>(new ForestScene(std::move(table)));
}
static const bool s_forestFactory = (RegisterSceneForestFactory(&MakeForestScene), true);

const char* ForestScene::StateName(int s)
{
	static const char* const k[] = {"OFF", "WAITING", "ACTIVE", "FAILED", "WITHOUT"};
	return s >= 0 && s < kStateCount ? k[s] : "?";
}

ForestScene::ForestScene(std::unique_ptr<FowPlotTable> table) : m_table(std::move(table)) {}

ForestScene::~ForestScene()
{
	releaseDevice();
}

void ForestScene::fail(const std::string& why)
{
	if(m_state == kFailed || m_state == kWithout)
		return;
	m_state = kFailed;
	m_failWhy = why;
	RNote("LOUD: forest: install failure: %s (render_forest=1 cannot draw the trees; the host ends the game, D118 / D125)", why.c_str());
}

void ForestScene::drawWithout()
{
	if(m_state != kFailed)
		return;
	m_state = kWithout;
	RNote("LOUD: forest: no fatal hook: the scene draws on WITHOUT the trees after: %s", m_failWhy.c_str());
}

void ForestScene::hook(int32_t x, int32_t y, const c5f::PlotRecord* rec, const c5f::PlotBlocks* b)
{
	// the Tree_Shadow decals follow the blocks: built -> made, removed -> dropped, all dropped -> all dropped
	if(x < 0)
		dropAllShadows();
	else if(rec && b)
		addShadow(x, y, *b);
	else
		dropShadow(x, y);
	if(m_hook)
		m_hook(m_hookUser, x, y, rec, b);
}

void ForestScene::dropAll()
{
	const bool had = m_place.recordCount() > 0;
	m_place.clearPlots();
	m_built.clear();
	if(had)
		hook(-1, -1, nullptr, nullptr);
}

void ForestScene::init(ID3D11Device* dev, const ForestSceneOptions& o)
{
	releaseDevice();
	m_opt = o;
	m_dev = dev;
	if(!o.on)
	{
		m_state = kOff;
		RNote("forest: render_forest=0: no trees (the picture before epic 0431)");
		return;
	}
	if(m_state == kFailed || m_state == kWithout)
	{
		RNote("forest: the install failed before (%s): not started again on this device", m_failWhy.c_str());
		return;
	}
	const int64_t q0 = Qpc();
	if(!m_table)   // ticket 0495: a recording or replaying host's table (ForestPlotTableSlot), else the live feed
		m_table = ForestPlotTableSlot() ? ForestPlotTableSlot()() : MakeFeedPlotTable();
	if(!m_table)
		m_table = MakeFeedPlotTable();
	if(!m_sessionSeed)
	{
		// stock's sequence depends on every rand() of the EXE before the game start: random per session in practice
		m_sessionSeed = o.seed ? o.seed : (uint32_t)(Qpc() ^ ((uint64_t)GetCurrentProcessId() * 2654435761u));
		if(!m_sessionSeed)
			m_sessionSeed = 1;
	}
	if(!dev)
	{
		fail("no D3D11 device for the forest renderer");
		return;
	}
	c5r::gamefs::GameFiles own;
	const c5r::gamefs::GameFiles* fs = o.files;
	if(!fs)
	{
		if(o.gameDir.empty())
		{
			fail("no game install to read forests.xml, the masks, the `_2` cards and textures and the ColorKeys from");
			return;
		}
		const c5r::Status st = own.open(o.gameDir);
		if(!st.ok())
		{
			fail("the game install '" + Narrow(o.gameDir) + "' did not open: " + st.str());
			return;
		}
		fs = &own;
	}
	if(!m_haveInstall)
	{
		const c5r::Status st = c5f::loadInstall(*fs, m_params, m_masks, &m_xmlOrigin);
		if(!st.ok())
		{
			fail("forests.xml or the placement masks (forest_tile1..N.dds) refused: " + st.str());
			return;
		}
		m_haveInstall = true;
	}
	{
		// the card choice: the session's stream (the same cards after a device rebuild of the session)
		c5f::CrtRand r(m_sessionSeed);
		const uint32_t cardSeed = (uint32_t)r.next() << 15 | (uint32_t)r.next();
		c5r::shaders::ShaderSection sec;
		c5r::Status st = sec.open(o.exe);
		if(st.ok())
		{
			c5f::InitOptions io;
			if(o.vsVa)
				io.vsVa = o.vsVa;
			if(o.psVa)
				io.psVa = o.psVa;
			io.randSeed = cardSeed;
			st = m_renderer.init(dev, sec, *fs, io);
		}
		if(!st.ok())
		{
			fail("the forest renderer did not start (ForestShader of '" + Narrow(o.exe) + "', the `_2` models and textures): " + st.str());
			return;
		}
	}
	for(uint32_t s = 0; s < 5; s++)
	{
		const std::string name = c5r::bake::colorKeyFile(s);
		std::vector<uint8_t> tga, lut;
		c5r::Status st = fs->read(name, tga);
		if(st.ok())
			st = c5r::bake::buildColorKeyLut(tga.data(), tga.size(), lut);
		if(st.ok())
		{
			D3D11_TEXTURE3D_DESC d = {32, 32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
			D3D11_SUBRESOURCE_DATA init = {lut.data(), 32 * 4, 32 * 32 * 4};
			ID3D11Texture3D* t = nullptr;
			HRESULT hr = dev->CreateTexture3D(&d, &init, &t);
			if(SUCCEEDED(hr))
			{
				hr = dev->CreateShaderResourceView(t, nullptr, &m_colorKey[s]);
				t->Release();
			}
			if(FAILED(hr))
				st = c5r::Status::fail(c5r::Err::Device, "CreateTexture3D / view (hr 0x" + std::to_string((unsigned long)hr) + ")");
		}
		if(!st.ok())
		{
			fail("the Terrain ColorKey '" + name + "' (the trees' t5) refused: " + st.str());
			return;
		}
	}
	m_white = OnePixel(dev, 0xFFFFFFFFu);
	m_black = OnePixel(dev, 0xFF000000u);
	if(!m_white || !m_black)
	{
		fail("the fog-off constants (1 x 1 textures) could not be made");
		return;
	}
	if(m_state == kOff)
		m_state = kWaiting;
	RNote("forest: render_forest=1: %u trees per plot (%u blocks), %u masks, overlay_alpha %.3f, space %.3f from '%s'; session seed %08x; "
	      "ready in %.1f ms",
	      m_params.treesPerPlot(), m_params.blocksPerPlot(), (unsigned)m_masks.size(), m_params.overlayAlpha, m_params.spaceBetween,
	      m_xmlOrigin.c_str(), m_sessionSeed, QpcToMs(Qpc() - q0));
}

void ForestScene::releaseTarget()
{
	SafeRelease(m_ovlSrv);
	SafeRelease(m_ovlRtv);
	SafeRelease(m_ovlTex);
	m_ovlW = m_ovlH = 0;
}

void ForestScene::releaseDevice()
{
	m_renderer.release();
	m_blocks.clear();
	// the cut reader holds no D3D object: it is kept (a device rebuild reads on)
	for(auto& v : m_colorKey)
		SafeRelease(v);
	SafeRelease(m_white);
	SafeRelease(m_black);
	releaseTarget();
	m_dev = nullptr;
}

bool ForestScene::ensureTarget(uint32_t w, uint32_t h, std::string& why)
{
	if(m_ovlTex && w == m_ovlW && h == m_ovlH)
		return true;
	releaseTarget();
	if(!m_dev)
		return why = "no device", false;
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w;
	d.Height = h;
	d.MipLevels = 1;   // the tree PS samples mip 0 only (trees.md 11.4: equal pixels with one mip)
	d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	HRESULT hr = w && h ? m_dev->CreateTexture2D(&d, nullptr, &m_ovlTex) : E_INVALIDARG;
	if(SUCCEEDED(hr))
	{
		D3D11_RENDER_TARGET_VIEW_DESC rv = {};
		rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		hr = m_dev->CreateRenderTargetView(m_ovlTex, &rv, &m_ovlRtv);
	}
	if(SUCCEEDED(hr))
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;
		hr = m_dev->CreateShaderResourceView(m_ovlTex, &sv, &m_ovlSrv);
	}
	if(FAILED(hr))
	{
		char b[160];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "the Screen Overlay target %u x %u (R8G8B8A8, t3) could not be made: hr 0x%08lx", w, h,
		            (unsigned long)hr);
		why = b;
		releaseTarget();
		return false;
	}
	m_ovlW = w;
	m_ovlH = h;
	m_ovlMade++;
	return true;
}

bool ForestScene::makeLayouts()
{
	m_layoutsOk = false;
	if(!m_haveInstall || !m_grid)
		return false;
	m_loads++;
	// the load's stream: the session's seed moved per load (stock makes the 20 layouts at every game start)
	c5f::CrtRand r(m_sessionSeed ^ (m_loads * 0x9E3779B9u));
	m_layoutSeed = (uint32_t)r.next() << 15 | (uint32_t)r.next();
	if(!m_layoutSeed)
		m_layoutSeed = 1;
	dropAll();
	// a new load: stock's tile+0x300 bits start again; the field re-bakes every tile, so no pending change is needed
	m_marks.clear();
	m_pending.clear();
	m_pendingMarks.clear();
	m_shadowVersion++;
	const int64_t q0 = Qpc();
	const c5r::Status st = m_place.initWith(m_params, m_masks, m_grid->hexToWorld, m_layoutSeed);
	if(!st.ok())
	{
		fail("the 20 layouts were refused for the field of load " + std::to_string(m_loadSeq) + ": " + st.str());
		return false;
	}
	m_layoutsOk = true;
	layoutsMade++;
	RNote("forest: load %u: %u layouts of %u trees in %.1f ms (seed %08x)", m_loadSeq, (unsigned)m_place.layouts().size(),
	      m_params.treesPerPlot(), QpcToMs(Qpc() - q0), m_layoutSeed);
	applyAll("a new load");
	return true;
}

void ForestScene::onField(const c5h::HeightGrid& g, uint32_t loadSeq)
{
	const bool newLoad = !m_haveField || loadSeq != m_loadSeq || memcmp(g.hexToWorld, m_fieldH2w, sizeof(m_fieldH2w)) != 0;
	m_grid = &g;
	m_haveField = true;
	m_loadSeq = loadSeq;
	memcpy(m_fieldH2w, g.hexToWorld, sizeof(m_fieldH2w));
	c5f::worldRectOf(g, m_worldRect);   // TDS+0x6C..+0x78 (shadows.h): the decal rectangles' clamp
	std::vector<liveterrain::SampleBox> live;
	live.swap(m_liveBoxes);   // ticket 4904: consumed by this onField whatever its path
	if(m_state == kOff || m_state == kFailed || m_state == kWithout)
		return;
	if(newLoad || !m_layoutsOk)
		makeLayouts();
	else if(!live.empty() && reheightWindows(live))
		;   // a live window (ticket 4904): only the built plots whose trees read it were built again
	else
	{
		// the same load's field again (a device rebuild, a re-applied tap): every block rebuilt from it
		m_place.invalidateAll();
		m_built.clear();
		hook(-1, -1, nullptr, nullptr);
	}
}

// Ticket 4904: a live window on the same load. False (a build failed): the caller rebuilds every block, so no tree keeps a
// stale height (D118).
bool ForestScene::reheightWindows(const std::vector<liveterrain::SampleBox>& boxes)
{
	const c5f::CutSource& cut = m_cut ? *m_cut : (const c5f::CutSource&)m_emptyCut;
	std::vector<liveterrain::BuiltPlot> built;
	built.reserve(m_built.size());
	for(const auto& e : m_built)
		built.push_back({(int32_t)(uint32_t)(e.first & 0xFFFFFFFFu), (int32_t)(uint32_t)(e.first >> 32), e.second});
	lastReheight = liveterrain::ReheightBuilt(m_place, *m_grid, cut, built, boxes,
	                                          [this, &cut](int32_t x, int32_t y, const c5f::PlotRecord* rec, const c5f::PlotBlocks* b) {
		                                          if(b)
			                                          m_built[Key(x, y)] = cut.generation();
		                                          else
			                                          m_built.erase(Key(x, y));
		                                          hook(x, y, b ? rec : nullptr, b);
	                                          });
	RNote("%sforest: live window (%zu boxes): %u of %u built plots built again, %u failed, %.2f ms%s%s",
	      lastReheight.failed ? "LOUD: " : "", boxes.size(), lastReheight.rebuilt, lastReheight.builtSeen, lastReheight.failed,
	      lastReheight.ms, lastReheight.failed ? "; every block again: " : "", lastReheight.firstError.c_str());
	return lastReheight.failed == 0;
}

void ForestScene::readTable(const char* why)
{
	if(!m_table)
		return;
	uint32_t gen = 0, w = 0, h = 0;
	m_table->copy(m_rec, gen, w, h);
	m_tableGen = gen;
	m_w = w;
	m_h = h;
	m_haveTable = w && h && m_rec.size() == (size_t)w * h;
	rereads++;
	if(m_haveTable && m_firstTableMs < 0)
		m_firstTableMs = QpcToMs(Qpc());
	if(m_layoutsOk)
		applyAll(why);
}

bool ForestScene::applyPlot(uint32_t idx)
{
	if(!m_haveTable || idx >= m_rec.size() || !m_w)
		return false;
	const int32_t x = (int32_t)(idx % m_w), y = (int32_t)(idx / m_w);
	const SrvFeed::PlotRecord& r = m_rec[idx];
	c5f::PlotInput p;
	p.x = x;
	p.y = y;
	p.forest = m_ids.forest >= 0 && r.feature == m_ids.forest;
	p.jungle = m_ids.jungle >= 0 && r.feature == m_ids.jungle;
	p.hills = r.plotType == kPlotTypeHills;
	const c5f::PlotRecord* b = m_place.record(x, y);
	const c5f::Kind kb = b ? b->kind : c5f::Kind::None;
	const uint32_t lb = b ? b->layout : 0;
	const c5r::Status st = m_place.setPlot(p);
	if(!st.ok())
	{
		recordErrors++;
		if(loudDue(recordErrors))
			RNote("LOUD: forest: plot %d,%d (feature %d, plotType %d) refused by the placement: %s (%lld so far)", x, y, r.feature, r.plotType,
			      st.str().c_str(), (long long)recordErrors);
		return false;
	}
	const c5f::PlotRecord* a = m_place.record(x, y);
	if(!b && !a)
		return false;
	if(b && a && a->kind == kb && a->layout == lb)
		return false;
	m_built.erase(Key(x, y));
	if(b && !a)
		recordsRemoved++;
	else if(!b && a)
		recordsAdded++;
	else
		recordsChanged++;
	if(b)
		hook(x, y, nullptr, nullptr);   // its blocks (and 4552's decal) are gone
	return true;
}

void ForestScene::applyAll(const char* why)
{
	if(!m_layoutsOk)
		return;
	dropAll();
	if(!m_haveTable || !m_ids.resolved)
		return;
	const int64_t a0 = recordsAdded;
	for(uint32_t i = 0; i < (uint32_t)m_rec.size(); i++)
		applyPlot(i);
	RNote("forest: %u records from the plot table (%ux%u, generation %u; forest id %d, jungle id %d) after %s", (unsigned)m_place.recordCount(),
	      m_w, m_h, m_tableGen, m_ids.forest, m_ids.jungle, why);
	(void)a0;
	loadMarks();   // stock's tile+0x300 of every record at the load (trees.md 12.4)
}

bool ForestScene::onBatch(const SrvFeed::Batch& b)
{
	if(m_state == kOff)
		return false;
	bool reread = !m_haveTable ? b.width && b.height : b.snapshotGen != m_tableGen;
	const char* why = "a new snapshot";
	for(const SrvFeed::Event& e : b.events)
	{
		if(e.kind == SrvFeed::kSnapshot)
			reread = true;
		else if(e.kind == SrvFeed::kOverflow)
		{
			overflows++;
			reread = true;
			why = "an overflow";
		}
		else if(e.kind == SrvFeed::kFeatureChanged)
		{
			featureEvents++;
			const size_t n = b.width && b.height ? (size_t)b.width * b.height : m_rec.size();
			if(n && (e.plot < 0 || (size_t)e.plot >= n))
			{
				outsideEvents++;
				if(loudDue(outsideEvents))
					RNote("LOUD: forest: a feature event for plot %d outside the %ux%u table (feature %d; %lld so far): ignored", e.plot, b.width,
					      b.height, e.a[0], (long long)outsideEvents);
			}
		}
	}
	if(b.dropped > m_lastDropped)
	{
		if(!reread)
			overflows++;
		reread = true;
		why = "dropped events";
	}
	m_lastDropped = b.dropped;
	if(reread)
	{
		readTable(why);
		return true;
	}
	if(b.changedPlots.empty() || !m_haveTable || !m_table)
		return false;
	std::vector<SrvFeed::PlotRecord> now;
	uint32_t gen = 0, w = 0, h = 0;
	m_table->copy(now, gen, w, h);
	if(gen != m_tableGen || w != m_w || h != m_h || now.size() != m_rec.size())
	{
		readTable("a table that moved under a patch");
		return true;
	}
	std::vector<uint32_t> moved;
	for(uint32_t idx : b.changedPlots)
		if(idx < now.size() && (now[idx].feature != m_rec[idx].feature || now[idx].plotType != m_rec[idx].plotType))
			moved.push_back(idx);
	m_rec.swap(now);
	bool changed = false;
	if(m_layoutsOk && m_ids.resolved)
		for(uint32_t idx : moved)
			changed |= applyPlot(idx);
	return changed;
}

bool ForestScene::pollCut()
{
	if(!m_opt.cutTap || !m_exePid || m_state == kOff || m_state == kFailed || m_state == kWithout)
		return false;
	if(!m_cutReader || m_cutPid != m_exePid)
	{   // a new EXE (or the first pid): a reader of its control section; the cut source moves to it
		m_cutReader.reset(m_opt.cutControlName.empty() ? new CutTapReader(m_exePid) : new CutTapReader(m_exePid, m_opt.cutControlName));
		m_cutPid = m_exePid;
		m_cutGenSeen = 0;
		m_cutEverGrid = false;
		m_cutWaitFromMs = m_haveField ? QpcToMs(Qpc()) : -1;
		cutReaders++;
		setCutSource(m_cutReader.get());
		RNote("forest: the cut tap's reader for EXE pid %u (the trees' cut mask, ticket 4553)", m_exePid);
		if(m_opt.cutForceFail)
		{
			fail("render_test_cut_fail=1: the cut-mask tap's reader fails on purpose (a negative launch of the install failure)");
			return false;
		}
	}
	bool changed = false;
	if(m_cutReader->poll() || m_cutReader->generation() != m_cutGenSeen)
	{
		m_cutGenSeen = m_cutReader->generation();
		cutChanges++;
		rebuildBuilt();
		changed = true;
	}
	if(m_cutReader->hasGrid())
		m_cutEverGrid = true;
	if(m_cutReader->installFailure())
	{
		fail("the cut-mask tap could not install: " + m_cutReader->lastWhy());
		return false;
	}
	// install bound: no grid this long after the first field (the tap OFF, a test switch, is never one)
	if(!m_cutEverGrid && !m_cutReader->off() && m_opt.cutWaitMs > 0)
	{
		if(m_cutWaitFromMs < 0 && m_haveField)
			m_cutWaitFromMs = QpcToMs(Qpc());
		if(m_cutWaitFromMs >= 0 && QpcToMs(Qpc()) - m_cutWaitFromMs > m_opt.cutWaitMs)
		{
			static const char* const kStates[] = {"ABSENT (no control block: a composite without the cut tap?)", "OFF", "NO_GRID",
			                                      "WAITING", "ACTIVE", "FAILED"};
			const int st = (int)m_cutReader->state();
			char b[400];
			_snprintf_s(b, sizeof(b), _TRUNCATE, "no cut-mask grid from the cut tap within %.0f ms after the field (EXE pid %u, state %s%s%s)",
			            m_opt.cutWaitMs, m_exePid, st >= 0 && st < 6 ? kStates[st] : "?", m_cutReader->lastWhy().empty() ? "" : "; last: ",
			            m_cutReader->lastWhy().c_str());
			fail(b);
		}
	}
	return changed;
}

// A new cut grid: every plot built so far is built again with it now (stock refreshes every record on a cut change,
// trees.md 2.2), and its Tree_Shadow decal follows (addShadow keeps a decal whose content did not change).
void ForestScene::rebuildBuilt()
{
	if(!m_grid || !m_layoutsOk || m_built.empty())
		return;
	const c5f::CutSource& cut = m_cut ? *m_cut : (const c5f::CutSource&)m_emptyCut;
	std::vector<uint64_t> keys;
	keys.reserve(m_built.size());
	for(const auto& e : m_built)
		keys.push_back(e.first);
	for(uint64_t k : keys)
	{
		const int32_t x = (int32_t)(uint32_t)(k & 0xFFFFFFFFu), y = (int32_t)(uint32_t)(k >> 32);
		const c5f::PlotBlocks* pb = nullptr;
		if(!m_place.blocks(*m_grid, cut, x, y, &pb).ok() || !pb)
		{
			m_built.erase(k);
			continue;
		}
		m_built[k] = cut.generation();
		cutRebuilds++;
		hook(x, y, m_place.record(x, y), pb);
	}
}

bool ForestScene::poll()
{
	const bool cutChanged = pollCut();
	if(m_state == kOff || m_idsExternal)
		return cutChanged;
	const ForestFeatureIds now = ForestFeatureIdsNow();
	if(now.generation == m_ids.generation)
		return cutChanged;
	const bool was = m_ids.resolved && (m_ids.forest != now.forest || m_ids.jungle != now.jungle);
	// records exist only with resolved ids: a change between two unresolved states changes none
	const bool affects = now.resolved || m_ids.resolved;
	m_ids = now;
	RNote("forest: feature ids %s: forest %d, jungle %d (by art define, published by the feed's DLL side)%s",
	      now.resolved ? "resolved" : "seen", now.forest, now.jungle, was ? "; they CHANGED: every record again" : "");
	if(affects && m_layoutsOk && m_haveTable)
	{
		applyAll("new feature ids");
		return true;
	}
	return cutChanged;
}

void ForestScene::setFeatureIds(int forest, int jungle)
{
	m_idsExternal = true;
	m_ids.forest = forest;
	m_ids.jungle = jungle;
	m_ids.resolved = true;
	m_ids.generation++;
	if(m_layoutsOk && m_haveTable)
		applyAll("feature ids set by the host");
}

void ForestScene::clearFeatureIds()
{
	m_idsExternal = false;
	m_ids = ForestFeatureIds();
	m_ids.generation = 0xFFFFFFFFu;   // differs from the published table's: the next poll() reads it
	dropAll();
}

void ForestScene::setCutSource(const c5f::CutSource* cut)
{
	m_cut = cut;
	m_built.clear();
	m_place.invalidateAll();
	hook(-1, -1, nullptr, nullptr);
}

bool ForestScene::centre(int32_t x, int32_t y, float out[3]) const
{
	if(!m_grid)
		return false;
	const c5h::HeightGrid& g = *m_grid;
	const int32_t hx = x - (y >> 1), hy = y, hz = -(hx + hy);
	float sx = 0, sy = 0;
	TransformXY(g.hexToWorld, (float)hx, (float)hy, (float)hz, sx, sy);
	const float wx = sx * g.sx - g.ox, wy = g.sy * sy - g.oy;
	const float bx = (g.ox + wx) / g.sx, by = (g.oy + wy) / g.sy;
	if(!std::isfinite(bx) || !std::isfinite(by) || std::fabs(bx) > 1e9f || std::fabs(by) > 1e9f)
		return false;
	out[0] = wx;
	out[1] = wy;
	out[2] = c5h::heightAtSample(g, (int)bx, (int)by);
	return std::isfinite(out[2]);
}

uint32_t ForestScene::styleAt(const c5t::Renderer& terrain, int32_t x, int32_t y) const
{
	if(!m_grid)
		return 0;
	const c5t::TileGrid tg = terrain.tileGrid();
	if(!tg.tileSamples || !tg.tilesX || !tg.tilesY)
		return 0;
	const int32_t hx = x - (y >> 1), hy = y, hz = -(hx + hy);
	float sx = 0, sy = 0;
	TransformXY(m_grid->hexToWorld, (float)hx, (float)hy, (float)hz, sx, sy);
	if(!std::isfinite(sx) || !std::isfinite(sy))
		return 0;
	int32_t tx = (int32_t)std::floor(sx / (float)tg.tileSamples), ty = (int32_t)std::floor(sy / (float)tg.tileSamples);
	if(m_grid->wrapX)
		tx = ((tx % (int32_t)tg.tilesX) + (int32_t)tg.tilesX) % (int32_t)tg.tilesX;
	tx = std::max(0, std::min(tx, (int32_t)tg.tilesX - 1));
	ty = std::max(0, std::min(ty, (int32_t)tg.tilesY - 1));
	return terrain.tileStyleAt((uint32_t)tx, (uint32_t)ty);
}

std::vector<ForestVisiblePlot> ForestScene::visiblePlots(const VPShellCamera& cam) const
{
	std::vector<ForestVisiblePlot> out;
	if(!m_grid || !m_layoutsOk)
		return out;
	const uint32_t nv = std::max(1u, std::min(cam.numViews, c5f::kMaxViews));
	float planes[c5f::kMaxViews][6][4];
	bool ok[c5f::kMaxViews] = {};
	for(uint32_t v = 0; v < nv; v++)
	{
		float vp[16];
		Mul44(cam.view[v], cam.proj, vp);
		ok[v] = Finite(vp, 16) && Planes(vp, planes[v]);
	}
	for(const c5f::PlotRecord& r : m_place.records())
	{
		float c[3];
		if(!centre(r.plotX, r.plotY, c))
			continue;
		bool vis = false;
		for(uint32_t v = 0; v < nv && !vis; v++)
			vis = ok[v] && InsidePlanes(planes[v], c, kForestViewMargin);
		if(vis)
		{
			ForestVisiblePlot p;
			p.x = r.plotX;
			p.y = r.plotY;
			out.push_back(p);
		}
	}
	return out;
}

c5r::Status ForestScene::drawStage(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
                                   uint32_t h, const VPShellCamera& cam, c5t::Renderer& terrain, const c5t::Camera& tcam,
                                   const c5t::Options& top, const float fowAnimOff[4], ForestStageStats* stats)
{
	m_last = ForestStageStats();
	if(stats)
		*stats = m_last;
	if(m_state == kOff || m_state == kFailed || m_state == kWithout)
		return c5r::Status::Ok();
	const int64_t q0 = Qpc();
	if(!m_haveField || !m_layoutsOk)
	{
		framesHeld++;
		return c5r::Status::fail(c5r::Err::NotFound, "forest: no field (or no layouts) yet");
	}
	if(!m_haveTable || !m_ids.resolved)
	{
		framesHeld++;
		if(m_haveTable && !m_ids.resolved && m_firstTableMs >= 0 && QpcToMs(Qpc()) - m_firstTableMs > m_opt.idsWaitMs)
		{
			char b[320];
			_snprintf_s(b, sizeof(b), _TRUNCATE,
			            "the feature ids of FEATURE_FOREST / FEATURE_JUNGLE were not published within %.0f ms of the first plot table "
			            "(forest %d, jungle %d: the feed's DLL side must call ForestFeatureTagSeen / ForestFeatureIdsResolved, trees.md 13.6)",
			            m_opt.idsWaitMs, m_ids.forest, m_ids.jungle);
			fail(b);
		}
		return c5r::Status::fail(c5r::Err::NotFound, !m_haveTable ? "forest: no plot table yet" : "forest: feature ids not resolved yet");
	}
	if(!ctx || !rtv)
		return c5r::Status::fail(c5r::Err::BadLayout, "forest: no context or target");
	if(!terrain.fogRamp())
	{
		fail("the terrain has no distance-fog ramp (fogramp.dds, the trees' t4): it was started without the install");
		return c5r::Status::fail(c5r::Err::NotFound, m_failWhy);
	}
	{
		std::string why;
		if(!ensureTarget(w, h, why))
		{
			fail(why);
			return c5r::Status::fail(c5r::Err::Device, why);
		}
	}
	if(m_state == kWaiting)
	{
		m_state = kActive;
		RNote("forest: active: %u records, the first frame %u x %u", (unsigned)m_place.recordCount(), w, h);
	}
	// 1. the visible plots (trees.md 2.2: the plot centre against every view's frustum, 64-unit margin), their blocks
	const c5f::CutSource& cut = m_cut ? *m_cut : (const c5f::CutSource&)m_emptyCut;
	std::unordered_map<uint64_t, uint64_t>& built = m_built;
	m_blocks.clear();
	const std::vector<ForestVisiblePlot> vis = visiblePlots(cam);
	ForestStageStats st;
	st.records = (uint32_t)m_place.recordCount();
	for(const ForestVisiblePlot& vp : vis)
	{
		const c5f::PlotBlocks* pb = nullptr;
		const uint64_t key = Key(vp.x, vp.y);
		auto it = built.find(key);
		const bool builtNow = it == built.end() || it->second != cut.generation();
		const c5r::Status bs = m_place.blocks(*m_grid, cut, vp.x, vp.y, &pb);
		if(!bs.ok() || !pb)
		{
			buildErrors++;
			const std::string e = bs.str();
			if(loudDue(buildErrors) || e != m_lastBuildErr)
				RNote("LOUD: forest: plot %d,%d not built: %s (skipped this frame; %lld so far)", vp.x, vp.y, e.c_str(), (long long)buildErrors);
			m_lastBuildErr = e;
			continue;
		}
		built[key] = cut.generation();
		const c5f::PlotRecord* rec = m_place.record(vp.x, vp.y);
		if(builtNow)
		{
			st.builtNow++;
			plotsBuilt++;
			m_builtSec++;
			hook(vp.x, vp.y, rec, pb);
		}
		const uint32_t entry = styleAt(terrain, vp.x, vp.y);
		const uint32_t kind = rec && c5f::isJungle(rec->kind) ? 1u : 0u;
		for(uint32_t b = 0; b < pb->blocks; b++)
		{
			c5f::Block bl;
			bl.entry = entry;
			bl.kind = kind;
			memcpy(bl.trees, &pb->instances[(size_t)b * c5f::kTreesPerBlock].x, sizeof(bl.trees));
			m_blocks.push_back(bl);
		}
		st.visible++;
	}
	m_visibleLast = st.visible;
	st.blocks = (uint32_t)m_blocks.size();
	if(!st.blocks)
	{
		framesEmpty++;
		st.cpuMs = QpcToMs(Qpc() - q0);
		m_last = st;
		if(stats)
			*stats = st;
		return c5r::Status::Ok();
	}
	// 2. the Screen Overlay pass (trees.md 11.2) with this frame's terrain camera and Options
	{
		c5t::FrameStats ofs;
		const c5r::Status os = terrain.drawScreenOverlay(ctx, m_ovlRtv, tcam, top, &ofs);
		if(!os.ok())
		{
			overlayErrors++;
			const std::string e = os.str();
			if(loudDue(overlayErrors) || e != m_lastOvlErr)
				RNote("LOUD: forest: the Screen Overlay pass failed: %s (the frame stands without trees; %lld so far)", e.c_str(),
				      (long long)overlayErrors);
			m_lastOvlErr = e;
			m_last = st;
			if(stats)
				*stats = st;
			return os;
		}
		st.overlayPass = true;
	}
	// 3. the trees into the scene's colour and depth, t3 = the Screen Overlay
	c5f::FrameInputs fr;
	memcpy(fr.proj, cam.proj, sizeof(fr.proj));
	fr.width = w;
	fr.height = h;
	fr.numViews = std::max(1u, std::min(cam.numViews, c5f::kMaxViews));
	for(uint32_t v = 0; v < fr.numViews; v++)
	{
		memcpy(fr.views[v].view, cam.view[v], sizeof(fr.views[v].view));
		fr.views[v].haveCamPos = false;   // row 3 of the view's inverse (draw.h View)
		bool matched = true;
		const int k = CopyOfView(cam, v, &matched);
		memcpy(fr.views[v].overlayProj, top.overlay.proj[k + 1], sizeof(fr.views[v].overlayProj));
	}
	if(top.fow.enabled && top.fow.mask && top.fow.cloudBase)
	{
		fr.fowMask = top.fow.mask;
		fr.cloudBase = top.fow.cloudBase;
		memcpy(fr.fowAnim, top.fow.anim, sizeof(fr.fowAnim));
	}
	else
	{   // the terrain's fog-off picture: the mask visible, no cloud
		fr.fowMask = m_white;
		fr.cloudBase = m_black;
		if(fowAnimOff)
			memcpy(fr.fowAnim, fowAnimOff, sizeof(fr.fowAnim));
	}
	if(top.fog.enabled)
	{
		memcpy(fr.fog.color, top.fog.color, sizeof(fr.fog.color));
		fr.fog.density = top.fog.density;
		fr.fog.scale = top.fog.scale;
		fr.fog.w = top.fog.w;
	}
	fr.overlayAlpha = m_params.overlayAlpha;
	fr.screenOverlay = m_ovlSrv;
	fr.fogRamp = terrain.fogRamp();
	for(uint32_t i = 0; i < 5; i++)
	{
		fr.colorKeyLut[i] = m_colorKey[i];
		fr.fowLut[i] = terrain.fowLut(i);
	}
	fr.terrainOverlay = top.overlay.srv;
	c5f::FrameStats ds;
	const c5r::Status s = m_renderer.draw(ctx, rtv, dsv, fr, m_blocks.data(), m_blocks.size(), &ds);
	st.cpuMs = QpcToMs(Qpc() - q0);
	if(!s.ok())
	{
		drawErrors++;
		const std::string e = s.str();
		if(loudDue(drawErrors) || e != m_lastErr)
			RNote("LOUD: forest: the tree draw failed: %s (the frame stands without trees; %lld so far)", e.c_str(), (long long)drawErrors);
		m_lastErr = e;
		m_last = st;
		if(stats)
			*stats = st;
		return s;
	}
	m_lastErr.clear();
	st.draws = ds.draws;
	st.drawn = true;
	framesDrawn++;
	blocksDrawn += st.blocks;
	drawCalls += ds.draws;
	m_drawnSec++;
	m_blocksSec += st.blocks;
	m_cpuSec += st.cpuMs;
	m_last = st;
	if(stats)
		*stats = st;
	return s;
}

// ---- the Tree_Shadow decals (ticket 4552's module; srv_forest_scene.h's block comment, trees.md 12, 13.8) -------------

namespace {
uint64_t TileKey(const c5r::decal::TileOrigin& t) { return ((uint64_t)(uint32_t)t.y << 32) | (uint32_t)t.x; }
}  // namespace

void ForestScene::addMarks(const std::vector<c5r::decal::TileOrigin>& tiles)
{
	if(tiles.empty())
		return;
	std::set<uint64_t> have;
	for(const c5r::decal::TileOrigin& t : m_marks)
		have.insert(TileKey(t));
	bool added = false;
	for(const c5r::decal::TileOrigin& t : tiles)
		if(have.insert(TileKey(t)).second)
		{
			m_marks.push_back(t);
			m_pendingMarks.push_back(t);
			added = true;
		}
	if(added)
	{
		std::sort(m_marks.begin(), m_marks.end(), [](const c5r::decal::TileOrigin& a, const c5r::decal::TileOrigin& b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		});
		m_shadowVersion++;
	}
}

void ForestScene::addShadow(int32_t x, int32_t y, const c5f::PlotBlocks& b)
{
	std::unique_ptr<c5f::ShadowDecal> d(new c5f::ShadowDecal());
	const c5r::Status st = c5f::shadowDecal(b, x, y, m_worldRect, *d);
	if(!st.ok())
	{
		shadowErrors++;
		const std::string e = st.str();
		if(loudDue(shadowErrors) || e != m_lastShadowErr)
			RNote("LOUD: forest: plot %d,%d: no Tree_Shadow decal: %s (%lld so far)", x, y, e.c_str(), (long long)shadowErrors);
		m_lastShadowErr = e;
		return;
	}
	const c5f::ShadowDecal* old = shadowAt(x, y);
	if(old && d->quads && old->hash == d->hash && old->quads == d->quads)
		return;   // a rebuild (a cut change elsewhere) that changed nothing of this plot's decal: no change to bake
	dropShadow(x, y);   // a rebuild replaces the plot's decal
	if(!d->quads)
		return;   // no drawn tree: no decal (stock's census never shows one, shadows.h)
	m_pending.push_back(*d);
	m_shadows.push_back(std::move(d));
	m_shadowVersion++;
}

void ForestScene::dropShadow(int32_t x, int32_t y)
{
	for(size_t i = 0; i < m_shadows.size(); i++)
		if(m_shadows[i]->plotX == x && m_shadows[i]->plotY == y)
		{
			const c5f::ShadowDecal* one = m_shadows[i].get();
			c5f::ShadowChange ch;
			if(c5f::shadowChange(&one, 1, m_worldRect, ch).ok())
				addMarks(ch.areaTiles);   // stock's bit stays until the next load
			m_pending.push_back(*m_shadows[i]);
			m_shadows.erase(m_shadows.begin() + (std::ptrdiff_t)i);
			m_shadowVersion++;
			return;
		}
}

void ForestScene::dropAllShadows()
{
	while(!m_shadows.empty())
	{
		const c5f::ShadowDecal& d = *m_shadows.back();
		dropShadow(d.plotX, d.plotY);
	}
}

const c5f::ShadowDecal* ForestScene::shadowAt(int32_t x, int32_t y) const
{
	for(const auto& d : m_shadows)
		if(d->plotX == x && d->plotY == y)
			return d.get();
	return nullptr;
}

void ForestScene::loadMarks()
{
	if(!m_grid || !m_layoutsOk)
		return;
	const int64_t q0 = Qpc();
	const c5f::CutSource& cut = m_cut ? *m_cut : (const c5f::CutSource&)m_emptyCut;
	std::vector<c5f::ShadowDecal> all;
	all.reserve(m_place.recordCount());
	uint32_t errors = 0;
	for(const c5f::PlotRecord& r : m_place.records())
	{
		c5f::PlotBlocks b;
		c5f::ShadowDecal d;
		if(!c5f::buildPlot(*m_grid, m_place.params(), m_place.layouts()[r.layout], r, cut, c5f::BuildOptions(), b).ok() ||
		   !c5f::shadowDecal(b, r.plotX, r.plotY, m_worldRect, d).ok())
		{
			errors++;
			continue;
		}
		if(d.quads)
			all.push_back(std::move(d));
	}
	std::vector<const c5f::ShadowDecal*> ptrs;
	for(const c5f::ShadowDecal& d : all)
		ptrs.push_back(&d);
	c5f::ShadowChange ch;
	const size_t before = m_marks.size();
	if(c5f::shadowChange(ptrs.data(), ptrs.size(), m_worldRect, ch).ok())
		addMarks(ch.areaTiles);
	else
		errors++;
	markPlots += (int64_t)all.size();
	if(errors)
	{
		shadowErrors += errors;
		RNote("LOUD: forest: %u plots gave no decal for the load's tile marks (%lld so far)", errors, (long long)shadowErrors);
	}
	RNote("forest: the load's Tree_Shadow marks: %u plots with drawn trees, %u tiles marked (%u new), %.1f ms", (unsigned)all.size(),
	      (unsigned)m_marks.size(), (unsigned)(m_marks.size() - before), QpcToMs(Qpc() - q0));
}

bool ForestScene::takeDecalChanges(std::vector<DecalChange>& out)
{
	if(m_pending.empty() && m_pendingMarks.empty())
		return false;
	std::vector<const c5f::ShadowDecal*> ptrs;
	for(const c5f::ShadowDecal& d : m_pending)
		ptrs.push_back(&d);
	c5f::ShadowChange ch;
	const c5r::Status st = c5f::shadowChange(ptrs.data(), ptrs.size(), m_worldRect, ch);
	DecalChange c;
	c.generation = kForestDecalGenBase + (++m_forestGen);
	c.previousGeneration = c.generation - 1;
	memcpy(c.worldRect, m_worldRect, sizeof(c.worldRect));
	if(st.ok())
	{
		c.rects = std::move(ch.rects);
		c.areaTiles = std::move(ch.areaTiles);
	}
	else
	{   // a rectangle the module refuses: every tile that has or had decals is baked again (the overflow's rule)
		c.all = true;
		shadowErrors++;
		RNote("LOUD: forest: the change of %u Tree_Shadow decals refused (%s): every decal tile re-baked", (unsigned)m_pending.size(),
		      st.str().c_str());
	}
	std::set<uint64_t> have;
	for(const c5r::decal::TileOrigin& t : c.areaTiles)
		have.insert(TileKey(t));
	for(const c5r::decal::TileOrigin& t : m_pendingMarks)
		if(have.insert(TileKey(t)).second)
			c.areaTiles.push_back(t);
	m_pending.clear();
	m_pendingMarks.clear();
	out.push_back(std::move(c));
	decalChangesMade++;
	return true;
}

std::shared_ptr<const c5r::decal::DecalSet> ForestScene::composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap)
{
	decalComposes++;
	if(!tap || m_state == kOff)
		return tap;
	if(m_merged && m_mergedTap == tap && m_mergedVersion == m_shadowVersion)
		return m_merged;
	if(c5f::treeShadowCount(*tap) > 0)
	{   // the EXE's Forest is on: its own Tree_Shadow decals would draw twice; the tap's set as it is (not fatal)
		tapShadowRefusals++;
		if(m_refusedTapGen != tap->generation)
			RNote("LOUD: forest: the decal tap's set (generation %u) already holds %zu Tree_Shadow decals (the EXE's Forest is on): the "
			      "server's are not added (%lld sets so far)",
			      tap->generation, c5f::treeShadowCount(*tap), (long long)tapShadowRefusals);
		m_refusedTapGen = tap->generation;
		return tap;
	}
	std::vector<const c5f::ShadowDecal*> ptrs;
	ptrs.reserve(m_shadows.size());
	for(const auto& d : m_shadows)
		ptrs.push_back(d.get());
	std::shared_ptr<c5r::decal::DecalSet> out = std::make_shared<c5r::decal::DecalSet>();
	c5f::MergeStats ms;
	const int64_t q0 = Qpc();
	const c5r::Status st = c5f::mergeShadows(*tap, ptrs.data(), ptrs.size(), &m_marks, *out, &ms);
	if(!st.ok())
	{
		mergeErrors++;
		const std::string e = st.str();
		if(loudDue(mergeErrors) || e != m_lastMergeErr)
			RNote("LOUD: forest: the Tree_Shadow decals were not merged into the tap's set (generation %u): %s; the tap's set as it is "
			      "(%lld so far)",
			      tap->generation, e.c_str(), (long long)mergeErrors);
		m_lastMergeErr = e;
		return tap;
	}
	decalMerges++;
	RLog("forest: merged %u Tree_Shadow decals (%u vertices) and %u marks into the tap's set generation %u (%u decals): area bits +%u, "
	     "%.1f ms",
	     ms.shadowDecals, ms.shadowVertices, (unsigned)m_marks.size(), tap->generation, ms.tapDecals, ms.areaBitsSet, QpcToMs(Qpc() - q0));
	m_mergedTap = tap;
	m_merged = out;
	m_mergedVersion = m_shadowVersion;
	return m_merged;
}

void ForestScene::secondLine(char* out, size_t n)
{
	_snprintf_s(out, n, _TRUNCATE,
	            "forest 1s: state=%s drawn=%d visible=%u built=%d blocks=%lld draws=%u cpu_ms=%.3f records=%u built_total=%u load=%u "
	            "ids=%d/%d%s target=%ux%u | adds=%lld removes=%lld changes=%lld rereads=%lld overflows=%lld feature_events=%lld outside=%lld "
	            "build_err=%lld rec_err=%lld ovl_err=%lld draw_err=%lld held=%lld empty=%lld failed=%d",
	            StateName(m_state), m_drawnSec, m_visibleLast, m_builtSec, (long long)m_blocksSec, m_last.draws,
	            m_drawnSec ? m_cpuSec / m_drawnSec : -1.0, (unsigned)m_place.recordCount(), (unsigned)m_place.builtCount(), m_loadSeq,
	            m_ids.forest, m_ids.jungle, m_ids.resolved ? "" : "(unresolved)", m_ovlW, m_ovlH, (long long)recordsAdded,
	            (long long)recordsRemoved, (long long)recordsChanged, (long long)rereads, (long long)overflows, (long long)featureEvents,
	            (long long)outsideEvents, (long long)buildErrors, (long long)recordErrors, (long long)overlayErrors, (long long)drawErrors,
	            (long long)framesHeld, (long long)framesEmpty, m_state == kFailed || m_state == kWithout ? 1 : 0);
	{   // the Tree_Shadow decals (4552)
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE,
		            " | shadows=%u marks=%u changes=%lld merges=%lld tap_shadow_refused=%lld merge_err=%lld shadow_err=%lld | cut=%s gen=%u "
		            "accepted=%lld refused=%lld changes=%lld rebuilds=%lld",
		            (unsigned)m_shadows.size(), (unsigned)m_marks.size(), (long long)decalChangesMade, (long long)decalMerges,
		            (long long)tapShadowRefusals, (long long)mergeErrors, (long long)shadowErrors,
		            !m_cutReader ? (m_opt.cutTap ? "NO_PID" : "NONE") : m_cutReader->hasGrid() ? "ACTIVE" : m_cutReader->off() ? "OFF" : "WAITING",
		            m_cutReader ? m_cutReader->imageGeneration() : 0u, (long long)(m_cutReader ? m_cutReader->accepted : 0),
		            (long long)(m_cutReader ? m_cutReader->refused : 0), (long long)cutChanges, (long long)cutRebuilds);
	}
	m_drawnSec = m_builtSec = 0;
	m_blocksSec = 0;
	m_cpuSec = 0;
}

void ForestScene::totalsLine(char* out, size_t n)
{
	_snprintf_s(out, n, _TRUNCATE,
	            "forest totals: state %s frames drawn %lld empty %lld held %lld plots built %lld blocks drawn %lld draw calls %lld records %u "
	            "added %lld removed %lld changed %lld rereads %lld overflows %lld feature events %lld outside %lld build errors %lld record "
	            "errors %lld overlay errors %lld draw errors %lld layouts %lld targets made %lld decal composes %lld failed %d",
	            StateName(m_state), (long long)framesDrawn, (long long)framesEmpty, (long long)framesHeld, (long long)plotsBuilt,
	            (long long)blocksDrawn, (long long)drawCalls, (unsigned)m_place.recordCount(), (long long)recordsAdded, (long long)recordsRemoved,
	            (long long)recordsChanged, (long long)rereads, (long long)overflows, (long long)featureEvents, (long long)outsideEvents,
	            (long long)buildErrors, (long long)recordErrors, (long long)overlayErrors, (long long)drawErrors, (long long)layoutsMade,
	            (long long)m_ovlMade, (long long)decalComposes, m_state == kFailed || m_state == kWithout ? 1 : 0);
	const size_t used = strlen(out);
	_snprintf_s(out + used, n - used, _TRUNCATE,
	            "; tree shadows: decals %u marks %u (plots %lld) changes %lld merges %lld tap sets refused %lld merge errors %lld shadow errors "
	            "%lld",
	            (unsigned)m_shadows.size(), (unsigned)m_marks.size(), (long long)markPlots, (long long)decalChangesMade, (long long)decalMerges,
	            (long long)tapShadowRefusals, (long long)mergeErrors, (long long)shadowErrors);
}

// ---- ticket 0495: the state at the span's first frame (the offline scene harness's forest.bin) ---------------------------
bool ForestScene::spanState(ForestSpanState& s, std::string& why) const
{
	s = ForestSpanState();
	if(m_state == kOff || m_state == kFailed || m_state == kWithout)
	{
		why = std::string("the forest scene is ") + StateName(m_state);
		return false;
	}
	if(!m_haveField || !m_layoutsOk || !m_grid)
	{
		why = "no field or no layouts yet";
		return false;
	}
	s.sessionSeed = m_sessionSeed;
	s.layoutSeed = m_layoutSeed;
	s.loads = m_loads;
	s.loadSeq = m_loadSeq;
	s.idsResolved = m_ids.resolved;
	s.forestId = m_ids.forest;
	s.jungleId = m_ids.jungle;
	s.streamState = m_place.streamState();
	s.tableGen = m_tableGen;
	s.tableW = m_haveTable ? m_w : 0;
	s.tableH = m_haveTable ? m_h : 0;
	for(const c5f::PlotRecord& r : m_place.records())   // the placement's map order: ascending by y, then x
	{
		ForestSpanRecord o;
		o.x = r.plotX;
		o.y = r.plotY;
		o.kind = (uint32_t)r.kind;
		o.layout = r.layout;
		s.records.push_back(o);
	}
	std::set<uint64_t> seen;
	for(const auto& d : m_shadows)   // the decals' order (composeDecals merges them oldest first)
		if(m_built.count(Key(d->plotX, d->plotY)) && seen.insert(Key(d->plotX, d->plotY)).second)
			s.built.push_back(std::make_pair(d->plotX, d->plotY));
	std::vector<uint64_t> rest;
	for(const auto& kv : m_built)
		if(!seen.count(kv.first))
			rest.push_back(kv.first);
	std::sort(rest.begin(), rest.end());
	for(uint64_t k : rest)
		s.built.push_back(std::make_pair((int32_t)(uint32_t)(k & 0xFFFFFFFFu), (int32_t)(uint32_t)(k >> 32)));
	for(const c5r::decal::TileOrigin& t : m_marks)
		s.marks.push_back(std::make_pair((int32_t)t.x, (int32_t)t.y));
	// the cut mask: sampled per cell at its centre through the source's own lookup (the reader keeps no image copy)
	const VPShellCutImage* h = m_cut && m_cut == m_cutReader.get() && m_cutReader->hasGrid() ? m_cutReader->imageHeader() : nullptr;
	const c5f::DenseCut* dense = m_cut && m_cut == m_restoredCut.get() ? m_restoredCut.get() : nullptr;
	if(h || dense)
	{
		s.haveCut = true;
		s.cutW = h ? h->widthCells : dense->width();
		s.cutH = h ? h->heightCells : dense->height();
		s.cutOx = h ? h->ox : m_restoredOx;
		s.cutOy = h ? h->oy : m_restoredOy;
		s.cutGeneration = m_cut->generation();
		s.cutCells.assign((size_t)s.cutW * s.cutH, 0);
		for(uint32_t row = 0; row < s.cutH; row++)
		{
			const float wy = (float)(2 * row + 1) / c5f::kCutRowScale - s.cutOy;
			for(uint32_t col = 0; col < s.cutW; col++)
			{
				const float wx = (float)(2 * col + 1) - s.cutOx;
				const c5f::CutCell c = c5f::cutCell(wx, wy, s.cutOx, s.cutOy);
				if(c.col != (int32_t)col || c.row != (int32_t)row)
				{
					why = "the cut mask's cell (" + std::to_string(col) + ", " + std::to_string(row) + ") does not map back to itself";
					return false;
				}
				s.cutCells[(size_t)row * s.cutW + col] = m_cut->bitsAt(wx, wy);
			}
		}
	}
	return true;
}

bool ForestScene::restoreSpanState(const ForestSpanState& s, std::string& why)
{
	if(m_state == kOff || m_state == kFailed || m_state == kWithout)
	{
		why = std::string("the forest scene is ") + StateName(m_state);
		return false;
	}
	if(!m_haveField || !m_layoutsOk || !m_grid)
	{
		why = "no field or no layouts before the span (the replay loaded none)";
		return false;
	}
	if(s.tableW != (m_haveTable ? m_w : 0) || s.tableH != (m_haveTable ? m_h : 0) || (s.tableW && s.tableGen != m_tableGen))
	{
		why = "the replayed plot table (" + std::to_string(m_w) + "x" + std::to_string(m_h) + ", generation " + std::to_string(m_tableGen) +
		      ") is not the recorded one (" + std::to_string(s.tableW) + "x" + std::to_string(s.tableH) + ", generation " + std::to_string(s.tableGen) + ")";
		return false;
	}
	// the session's stream: the cards were chosen at init with the recorded seed (SceneOptions::forestSeed); the layouts of
	// the load again when the replay's load count gave another seed
	if(s.sessionSeed != m_sessionSeed)
	{
		why = "the session seed " + std::to_string(m_sessionSeed) + " is not the recorded " + std::to_string(s.sessionSeed) +
		      " (the player sets render_test_forest_seed from forest.bin)";
		return false;
	}
	if(s.layoutSeed != m_layoutSeed)
	{
		const c5r::Status st = m_place.initWith(m_params, m_masks, m_grid->hexToWorld, s.layoutSeed);
		if(!st.ok())
		{
			why = "the layouts of the recorded seed were refused: " + st.str();
			return false;
		}
		RNote("forest: replay: the layouts made again with the recorded seed %08x (the replay's was %08x)", s.layoutSeed, m_layoutSeed);
		m_layoutSeed = s.layoutSeed;
	}
	if(s.idsResolved)
	{
		m_idsExternal = true;
		m_ids.forest = s.forestId;
		m_ids.jungle = s.jungleId;
		m_ids.resolved = true;
		m_ids.generation++;
	}
	// the cut mask
	if(s.haveCut)
	{
		std::unique_ptr<c5f::DenseCut> c(new c5f::DenseCut());
		const c5r::Status st = c->set(s.cutW, s.cutH, s.cutCells, s.cutOx, s.cutOy);
		if(!st.ok())
		{
			why = "the recorded cut mask was refused: " + st.str();
			return false;
		}
		m_restoredCut = std::move(c);
		m_restoredOx = s.cutOx;
		m_restoredOy = s.cutOy;
		m_cut = m_restoredCut.get();
	}
	else
	{
		m_restoredCut.reset();
		m_cut = nullptr;   // the empty mask
	}
	// the records with their layouts, then the stream as it was after them
	dropAll();
	m_shadows.clear();
	m_pending.clear();
	m_pendingMarks.clear();
	for(const ForestSpanRecord& r : s.records)
	{
		const c5f::Kind k = (c5f::Kind)r.kind;
		c5f::PlotInput p;
		p.x = r.x;
		p.y = r.y;
		p.forest = k == c5f::Kind::Forest || k == c5f::Kind::ForestHills;
		p.jungle = c5f::isJungle(k);
		p.hills = c5f::isHills(k);
		const c5r::Status st = m_place.setPlotWithLayout(p, r.layout);
		if(!st.ok())
		{
			why = "record " + std::to_string(r.x) + "," + std::to_string(r.y) + " refused: " + st.str();
			return false;
		}
	}
	m_place.setStreamState(s.streamState);
	// the load's marks (the decals made below join them as live did), then the built plots in the decals' order
	m_marks.clear();
	for(const auto& t : s.marks)
	{
		c5r::decal::TileOrigin o;
		o.x = t.first;
		o.y = t.second;
		m_marks.push_back(o);
	}
	m_pendingMarks = m_marks;
	m_shadowVersion++;
	const c5f::CutSource& cut = m_cut ? *m_cut : (const c5f::CutSource&)m_emptyCut;
	for(const auto& b : s.built)
	{
		const c5f::PlotBlocks* pb = nullptr;
		const c5r::Status st = m_place.blocks(*m_grid, cut, b.first, b.second, &pb);
		if(!st.ok() || !pb)
		{
			why = "built plot " + std::to_string(b.first) + "," + std::to_string(b.second) + " does not build: " + st.str();
			return false;
		}
		m_built[Key(b.first, b.second)] = cut.generation();
		hook(b.first, b.second, m_place.record(b.first, b.second), pb);
	}
	RNote("forest: replay: the span's state restored: %u records, %u built (%u decals), %u marks, cut %s; seeds %08x / %08x, stream %08x",
	      (unsigned)s.records.size(), (unsigned)s.built.size(), (unsigned)m_shadows.size(), (unsigned)m_marks.size(),
	      s.haveCut ? (std::to_string(s.cutW) + "x" + std::to_string(s.cutH)).c_str() : "empty", s.sessionSeed, s.layoutSeed, s.streamState);
	return true;
}

}  // namespace srvrender
