// srv_residency_source.cpp - the server's own baked tiles (issue 3308). Contract: srv_residency_source.h.
// The calls follow shell/server/shell_server.cpp (TerrainInit's residency part, ApplyField's onField, TerrainFrame's
// beforeDraw, TerrainPump) at this repository's commit 9a2a63e; that file is unchanged (team 3400 owns it).
#include "srv_residency_source.h"
#include "srv_scene_sources.h"   // epic 0301, issue 3903: FeedSource, SettleResult
#include "c5r/bake/pipeline.h"
#include "c5r/bake/heightpage.h"   // issue 4305: tileStyle
#include "c5r/heightfield/heightfield.h"
#include <algorithm>
#include <ctype.h>
#include <wctype.h>
#include <stdlib.h>
#include <cmath>

namespace srvrender {

namespace {
// the residency's own lines (field, bake-ahead complete, failures: rare; each starts with "residency:") into server.log too
void ResLog(void*, const char* line) { RNote("%s", line); }
// issue 0417: the pipeline's lines (warm-up, slow bakes, first uses) into render.log only: a server.log + stdout write
// from the render thread took 57-311 ms per line during a huge load (2026-11-16, render.log's "render log: the line before
// took" lines), and a first-use line is written inside a bake
void PipeLog(void*, const char* line) { RLog("%s", line); }

std::string Lower(std::string s)
{
	for(char& c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

std::string BareName(const std::string& s)
{
	const size_t k = s.find_last_of("/\\");
	return k == std::string::npos ? s : s.substr(k + 1);
}

// LOUD lines of one kind: the first 20, then every 100th (the counters hold the rest)
bool LoudDue(int64_t n) { return n <= 20 || n % 100 == 0; }
}  // namespace

// ---- issue 4105: the bake with the tile's decals ----------------------------------------------------------------------
class ResidencySource::DecalBaker : public c5r::bake::TileBaker
{
public:
	explicit DecalBaker(ResidencySource& o) : m_o(o) {}
	c5r::Status init(ID3D11Device* dev, ID3D11DeviceContext* ctx) override { return m_o.m_baker->init(dev, ctx); }
	c5r::Status setMap(const c5r::bake::MapInputs& in) override
	{
		m_o.bakeMapArt = in.plotArtTypes;   // issue 4305: what the bake reads (tests)
		m_o.bakeMapPlotsX = in.plotsX;
		m_o.bakeMapPlotsY = in.plotsY;
		return m_o.m_baker->setMap(in);
	}
	c5r::Status bake(const c5r::bake::BakeRequest& req, c5r::bake::BakedTile& out) override
	{
		// ticket 0703: every BC1 bake's wall time, for the fit's bake estimate (ResidencySourceConfig::bakeFit)
		const int64_t t0 = Qpc();
		const c5r::Status st = bakeTile(req, out);
		m_o.noteSteps(m_o.m_bakeKind);   // ticket 0712: the step times per bake kind
		m_o.m_bc1Bakes++;
		if(!m_o.m_inSettle)
		{
			m_o.m_bakeTimes.push(QpcToMs(Qpc() - t0));
			m_o.m_fit.bakeSamples++;
		}
		return st;
	}

private:
	c5r::Status bakeTile(const c5r::bake::BakeRequest& req, c5r::bake::BakedTile& out)
	{
		const std::shared_ptr<const c5r::decal::DecalSet> keep = m_o.m_decals;   // the set outlives this bake
		if(!keep)
			return m_o.m_baker->bake(req, out);
		const int ox = (int)req.tile.x * c5r::decal::kTileSamples, oy = (int)req.tile.y * c5r::decal::kTileSamples;
		const c5r::decal::TileDecalList list = c5r::decal::decalsForTile(*keep, ox, oy);
		// 4111: with decalAreaFromExe, the EXE's tile+0x300 when the set carries the area block (a stock EXE's t8 rule
		// exactly, P14), for every bake; else (the default) 4105's rule below
		bool areaKnown = false;
		const bool area = m_o.m_cfg.decalAreaFromExe && c5r::decal::areaChangedAt(*keep, (int)req.tile.x, (int)req.tile.y, areaKnown);
		areaKnown = areaKnown && m_o.m_cfg.decalAreaFromExe;
		if(areaKnown)
			m_o.m_dc.bakesAreaFromExe++;
		if(list.indices.empty())
		{
			c5r::bake::BakeRequest r0 = req;
			if(areaKnown)
				r0.areaChanged = area;
			const c5r::Status s0 = m_o.m_baker->bake(r0, out);
			if(s0.ok())
				m_o.decalTileBaked(req.tile.x, req.tile.y);
			return s0;
		}
		c5r::bake::BakeRequest rq = req;
		rq.decals = &list;
		rq.areaChanged = areaKnown ? area : true;   // tile+0x300: the EXE's bit, else 4105's rule (a decal invalidated it)
		const int64_t q0 = Qpc();
		const c5r::Status st = m_o.m_baker->bake(rq, out);
		DecalBakeCounters& c = m_o.m_dc;
		if(!st.ok())
		{
			c.bakesFailed++;
			if(st.code != c5r::Err::OutOfMemory && LoudDue(c.bakesFailed))
				RNote("LOUD: decals: bake of tile (%u, %u) with %zu decals of set generation %u failed: %s; the tile keeps its last bake "
				      "(%lld decal bakes failed so far)",
				      req.tile.x, req.tile.y, list.indices.size(), keep->generation, st.str().c_str(), (long long)c.bakesFailed);
			return st;
		}
		c.bakesWithDecals++;
		c.decalsDrawn += (int64_t)list.indices.size();
		if(m_traced < m_o.m_cfg.decalTrace)
		{
			m_traced++;
			RNote("decals: bake %d tile (%u, %u) gen %llu with %zu decals of set generation %u, areaChanged %d, t8 %d, at qpc %.3f ms "
			      "(%.3f ms after the set was applied; areaChanged from %s)",
			      m_traced, req.tile.x, req.tile.y, (unsigned long long)req.contentGeneration, list.indices.size(), keep->generation,
			      (int)rq.areaChanged, (int)out.hasT8, QpcToMs(q0), QpcToMs(q0 - m_o.m_decalsAppliedQpc),
			      areaKnown ? "the EXE's tile+0x300" : "the decal rule");
		}
		m_o.decalTileBaked(req.tile.x, req.tile.y);
		return st;
	}

	ResidencySource& m_o;
	int m_traced = 0;
};

// ---- issue 0371: the state-5 bake into a near pair ---------------------------------------------------------------------
class ResidencySource::NearBaker : public c5r::bake::NearBaker
{
public:
	explicit NearBaker(ResidencySource& o) : m_o(o) {}
	c5r::Status bakeNear(ID3D11DeviceContext* ctx, c5r::bake::TileId tile, uint64_t contentGeneration, ID3D11Texture2D* t0,
	                     ID3D11Texture2D* t8) override
	{
		if(!t0 || !ctx)
			return c5r::Status::fail(c5r::Err::BadLayout, "near bake without a context or a t0 texture");
		if(!m_o.m_pipe)
			return clearPair(ctx, tile, t0, t8);
		// the EXE's state-5 bake (terrain-bake-passes.md 3.5.1): the whole bake of the tile, CacheBake also into the pair; the
		// tile's decals as DecalBaker finds them; t8 as the tile's BC1 bake made it (tile+0x2FC is the tile's and the pair was
		// taken with it), so the pipeline's t8 rule is overridden by what the pair holds
		c5r::bake::BakeRequest rq;
		rq.tile = tile;
		rq.sizeClass = m_o.m_cfg.res.sizeClass;
		rq.contentGeneration = contentGeneration;
		rq.areaChanged = contentGeneration > 1;   // the residency's rule for its own bakes (residency.cpp, bakeOne)
		rq.nearCache[0] = t0;
		rq.nearCache[1] = t8;
		const std::shared_ptr<const c5r::decal::DecalSet> keep = m_o.m_decals;   // the set outlives this bake
		c5r::decal::TileDecalList list;
		if(keep)
		{
			list = c5r::decal::decalsForTile(*keep, (int)tile.x * c5r::decal::kTileSamples, (int)tile.y * c5r::decal::kTileSamples);
			if(!list.indices.empty())
				rq.decals = &list;
		}
		c5r::bake::PipelineBaker::Overrides ov;
		ov.needSecond = t8 ? 1 : 0;
		c5r::bake::BakedTile out;
		const c5r::Status st = m_o.m_pipe->bakeWith(rq, ov, out);
		m_o.noteSteps(kKindNear);   // ticket 0712
		// the BC1 pair the same bake made equals the tile's own (the residency's bake of the same inputs): not kept
		if(out.t0)
			out.t0->Release();
		if(out.t8)
			out.t8->Release();
		if(st.ok())
			bakes++;
		return st;
	}
	int64_t bakes = 0;

private:
	// the stand-in: the pair cleared to a colour of the tile, every mip (tests of the wiring)
	c5r::Status clearPair(ID3D11DeviceContext* ctx, c5r::bake::TileId tile, ID3D11Texture2D* t0, ID3D11Texture2D* t8)
	{
		ID3D11Device* dev = nullptr;
		ctx->GetDevice(&dev);
		if(!dev)
			return c5r::Status::fail(c5r::Err::Device, "near bake: no device");
		const float c[4] = {(float)((tile.x * 37) % 256) / 255.0f, (float)((tile.y * 53) % 256) / 255.0f, 1.0f, 1.0f};
		ID3D11Texture2D* tex[2] = {t0, t8};
		c5r::Status st;
		for(int k = 0; k < 2 && st.ok(); ++k)
		{
			if(!tex[k])
				continue;
			D3D11_TEXTURE2D_DESC d = {};
			tex[k]->GetDesc(&d);
			for(UINT m = 0; m < d.MipLevels && st.ok(); ++m)
			{
				D3D11_RENDER_TARGET_VIEW_DESC rv = {};
				rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				rv.Texture2D.MipSlice = m;
				ID3D11RenderTargetView* v = nullptr;
				if(FAILED(dev->CreateRenderTargetView(tex[k], &rv, &v)))
					st = c5r::Status::fail(c5r::Err::Device, "near bake: render target view");
				else
				{
					ctx->ClearRenderTargetView(v, c);
					v->Release();
				}
			}
		}
		dev->Release();
		if(st.ok())
			bakes++;
		return st;
	}
	ResidencySource& m_o;
};

ResidencySource::ResidencySource(const ResidencySourceConfig& c) : m_cfg(c)
{
	// ticket 0424: C5_RENDER_HELD_BAKE (1 default, 0 off; srv_residency_source.h)
	char v[16] = {};
	const DWORD n = GetEnvironmentVariableA("C5_RENDER_HELD_BAKE", v, sizeof(v));
	m_heldOn = !(n > 0 && n < sizeof(v) && v[0] == '0' && v[1] == 0);
	// ticket 0642: ResidencySourceConfig::decalLoad, else C5_RENDER_DECAL_LOADER (0, 1, 2), else 2 (hold)
	m_decalLoadMode = 2;
	if(m_cfg.decalLoad >= 0 && m_cfg.decalLoad <= 2)
		m_decalLoadMode = m_cfg.decalLoad;
	else
	{
		char e[16] = {};
		const DWORD k = GetEnvironmentVariableA("C5_RENDER_DECAL_LOADER", e, sizeof(e));
		if(k == 1 && e[0] >= '0' && e[0] <= '2')
			m_decalLoadMode = e[0] - '0';
	}
	// ticket 0698: ResidencySourceConfig::layerLoad, else C5_RENDER_LAYER_LOADER (0, 1), else 1 (the loader thread)
	m_layerLoadMode = 1;
	if(m_cfg.layerLoad == 0 || m_cfg.layerLoad == 1)
		m_layerLoadMode = m_cfg.layerLoad;
	else
	{
		char e[16] = {};
		const DWORD k = GetEnvironmentVariableA("C5_RENDER_LAYER_LOADER", e, sizeof(e));
		if(k == 1 && (e[0] == '0' || e[0] == '1'))
			m_layerLoadMode = e[0] - '0';
	}
	// ticket 0714: ResidencySourceConfig::pageWarm, else C5_RENDER_PAGE_WARM (0, 1), else 1 (the pages at the field)
	m_pageWarmMode = 1;
	if(m_cfg.pageWarm == 0 || m_cfg.pageWarm == 1)
		m_pageWarmMode = m_cfg.pageWarm;
	else
	{
		char e[16] = {};
		const DWORD k = GetEnvironmentVariableA("C5_RENDER_PAGE_WARM", e, sizeof(e));
		if(k == 1 && (e[0] == '0' || e[0] == '1'))
			m_pageWarmMode = e[0] - '0';
	}
	// ticket 0703: ResidencySourceConfig::bakeFit, else C5_RENDER_BAKE_FIT (0, 1, 2), else 0 (off: the A/B of 2026-11-23
	// found no camera gap a bake fits in camera moves, terrain-bake-residency.md 8.4-8.5); a value the variable cannot take
	// is named in init's line and the default stays
	m_fitMode = 0;
	m_gaps.cap = 16;
	if(m_cfg.bakeFit >= 0 && m_cfg.bakeFit <= 2)
		m_fitMode = m_cfg.bakeFit;
	else
	{
		char e[16] = {};
		const DWORD k = GetEnvironmentVariableA("C5_RENDER_BAKE_FIT", e, sizeof(e));
		if(k == 1 && e[0] >= '0' && e[0] <= '2')
			m_fitMode = e[0] - '0';
		else if(k > 0)
			m_fitBadEnv = true;
	}
	// ticket 0712: ResidencySourceConfig::prepAhead, else C5_RENDER_PREP_AHEAD (0, 1), else 0 (off until the laptop series);
	// C5_RENDER_PREP_VERIFY=1 (TEST ONLY) checks every hit against the inline computation
	m_prepMode = 0;
	if(m_cfg.prepAhead == 0 || m_cfg.prepAhead == 1)
		m_prepMode = m_cfg.prepAhead;
	else
	{
		char e[16] = {};
		const DWORD k = GetEnvironmentVariableA("C5_RENDER_PREP_AHEAD", e, sizeof(e));
		if(k == 1 && (e[0] == '0' || e[0] == '1'))
			m_prepMode = e[0] - '0';
		else if(k > 0)
			m_prepBadEnv = true;
	}
	{
		char e[16] = {};
		const DWORD k = GetEnvironmentVariableA("C5_RENDER_PREP_VERIFY", e, sizeof(e));
		m_prepVerify = k == 1 && e[0] == '1';
	}
}

// ---- ticket 0712: prep ahead and the step times per bake kind (srv_residency_source.h) ------------------------------------
void ResidencySource::prepJoin()
{
	if(m_pipe)
		m_pipe->prepInvalidate();
}

bool ResidencySource::prepCounters(c5r::bake::PrepCounters& out) const
{
	if(!m_pipe)
		return false;
	out = m_pipe->prepCounters();
	return true;
}

void ResidencySource::noteSteps(int kind)
{
	if(!m_pipe || kind < 0 || kind >= kKinds)
		return;
	const c5r::bake::PipelineStepTimes& s = m_pipe->lastSteps();
	StepAgg* const aggs[2] = {&m_stepSec[kind], &m_stepTot[kind]};
	for(StepAgg* a : aggs)
	{
		a->n++;
		a->total += s.totalMs;
		a->totalMax = (std::max)(a->totalMax, s.totalMs);
		a->prep += s.prepMs;
		a->prepMax = (std::max)(a->prepMax, s.prepMs);
		a->normals += s.normalsMs;
		a->page += s.pageMs;
		a->own += s.ownMs;
		a->textures += s.texturesMs;
		a->blend += s.blendMs;
		a->style += s.styleMs;
		a->post += s.postMs;
		a->copy += s.copyMs;
		a->gpuTimer += s.gpuTimerMs;
		a->wait += s.prepWaitMs;
		a->hits += s.prepAhead == 1;
		a->waits += s.prepAhead == 2;
		a->misses += s.prepAhead == 3;
		a->firstUse += s.firstUse() ? 1 : 0;
	}
}

// After beforeDraw: lane 0 = the near pairs' next up-res, lane 1 = the visible tiles beyond the burst and the idle order.
// After idle: lane 1 only (the near list is the last frame's). The offline players' settle requests nothing.
void ResidencySource::requestPrep(bool afterDraw)
{
	if(!m_prepMode || !m_pipe || m_inSettle || !m_pipe->prepOn())
		return;
	const size_t kAhead = 6;   // about the bakes of the next slices and frames (one bake a slice under lockstep, 0703)
	m_res.nextBakes(m_hintNear, m_hintAhead, kAhead);
	std::vector<c5r::bake::PrepHint> h;
	h.reserve(kAhead);
	if(afterDraw)
	{
		for(const c5r::bake::Residency::Hint& x : m_hintNear)
		{
			c5r::bake::PrepHint p;
			p.tile = x.tile;
			p.contentGeneration = x.contentGeneration;
			h.push_back(p);
		}
		m_pipe->prepRequest(0, h.data(), h.size());
		h.clear();
	}
	for(const c5r::bake::Residency::Hint& x : m_hintAhead)
	{
		c5r::bake::PrepHint p;
		p.tile = x.tile;
		p.contentGeneration = x.contentGeneration;
		h.push_back(p);
	}
	m_pipe->prepRequest(1, h.data(), h.size());
}

void ResidencySource::stepsLine(double nowMs)
{
	if(!m_pipe)
		return;
	if(m_stepLineMs < 0)
	{
		m_stepLineMs = nowMs;
		return;
	}
	if(nowMs - m_stepLineMs < 1000.0)
		return;
	int64_t n = 0;
	for(const StepAgg& a : m_stepSec)
		n += a.n;
	if(n)
	{
		static const char* const kName[kKinds] = {"predraw", "idle", "held", "near", "settle"};
		std::string line = "pipeline steps 1s: prep_ahead=" + std::to_string(m_prepMode);
		char b[640];
		for(int k = 0; k < kKinds; ++k)
		{
			const StepAgg& a = m_stepSec[k];
			if(!a.n)
				continue;
			const double d = (double)a.n;
			_snprintf_s(b, sizeof(b), _TRUNCATE,
			            " | %s n=%lld total=%.3f total_max=%.2f prep=%.3f prep_max=%.2f normals=%.3f page=%.3f own=%.3f tex=%.3f "
			            "blend=%.3f style=%.3f post=%.3f copy=%.3f gpu=%.3f wait=%.3f hit=%lld waited=%lld miss=%lld first_use=%lld",
			            kName[k], (long long)a.n, a.total / d, a.totalMax, a.prep / d, a.prepMax, a.normals / d, a.page / d, a.own / d,
			            a.textures / d, a.blend / d, a.style / d, a.post / d, a.copy / d, a.gpuTimer / d, a.wait / d, (long long)a.hits,
			            (long long)a.waits, (long long)a.misses, (long long)a.firstUse);
			line += b;
		}
		if(m_prepMode)
		{
			const c5r::bake::PrepCounters c = m_pipe->prepCounters();
			_snprintf_s(b, sizeof(b), _TRUNCATE,
			            " | prep tot requested=%llu replaced=%llu made=%llu failed=%llu discarded=%llu hits=%llu waits=%llu "
			            "timeouts=%llu claimed=%llu misses=%llu evicted_unused=%llu invalidations=%llu join_ms=%.2f join_max_ms=%.2f "
			            "worker_ms=%.1f cache=%llu cache_kb=%.0f cache_kb_max=%.0f verified=%llu mismatches=%llu",
			            (unsigned long long)c.requested, (unsigned long long)c.replaced, (unsigned long long)c.made,
			            (unsigned long long)c.failed, (unsigned long long)c.discarded, (unsigned long long)c.hits,
			            (unsigned long long)c.waits, (unsigned long long)c.waitTimeouts, (unsigned long long)c.claimed,
			            (unsigned long long)c.misses, (unsigned long long)c.evictedUnused, (unsigned long long)c.invalidations,
			            c.joinMs, c.joinMsMax, c.workerMs, (unsigned long long)c.products, c.bytes / 1024.0, c.bytesMax / 1024.0,
			            (unsigned long long)c.verified, (unsigned long long)c.verifyMismatches);
			line += b;
		}
		RLog("%s", line.c_str());
	}
	for(StepAgg& a : m_stepSec)
		a = StepAgg();
	m_stepLineMs = nowMs;
}

const c5r::bake::PipelineLoaderCounters* ResidencySource::layerLoaderCounters() const
{
	return m_pipe ? &m_pipe->loaderCounters() : nullptr;
}

c5r::decal::DecalPass* ResidencySource::loaderPass() const
{
	c5r::decal::DecalPass* dp = m_pipe ? m_pipe->decalPass() : nullptr;
	return dp && dp->loaderOn() ? dp : nullptr;
}

const c5r::decal::DecalLoaderCounters* ResidencySource::decalLoaderCounters() const
{
	c5r::decal::DecalPass* dp = m_pipe ? m_pipe->decalPass() : nullptr;
	return dp ? &dp->loaderCounters() : nullptr;
}
ResidencySource::~ResidencySource() { release(); }

c5r::Status ResidencySource::init(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
	release();
	m_dev = dev;
	m_decalInitWhy.clear();
	const int64_t q0 = Qpc();
	c5r::Status st;
	c5r::bake::PipelineBaker* pipe = nullptr;
	// issue 4105: the decal names' and textures' view: the install with the mods' folders above it (VP's farm_types.xml
	// and maize texture, terrain-decals.md 8.3); without mod folders the install's own view serves
	double decalFsMs = 0;
	if(m_cfg.decals && (!m_cfg.decalLooseRoots.empty() || !m_cfg.pipeline) && !m_decalFsOpen)
	{
		c5r::gamefs::GameFiles::Options fo;
		fo.extraLooseRoots = m_cfg.decalLooseRoots;
		const int64_t qd = Qpc();
		const c5r::Status ds = m_decalFs.open(m_cfg.install, fo);
		decalFsMs = QpcToMs(Qpc() - qd);
		m_decalFsOpen = ds.ok();
		if(!ds.ok() && m_cfg.pipeline)
			st = ds;   // the pipeline's own open of the same install fails the same way below
	}
	if(m_cfg.pipeline)
	{
		// the install (TerrainDef.csv, layer textures, style cubes, LUT) and the EXE's shader section, read-only (D83)
		if(st.ok())
			st = m_fs.open(m_cfg.install);
		if(st.ok())
			st = c5r::bake::loadTerrainDef(m_fs, m_tables);
		if(st.ok())
			st = m_shaders.open(m_cfg.exe);
		if(st.ok())
		{
			c5r::bake::PipelineConfig pc;
			pc.fs = &m_fs;
			pc.exe = &m_shaders;
			// the residency budgets by gpuMs; Deferred never makes the render thread wait for the GPU (3404 wave 3)
			pc.gpuTiming = c5r::bake::GpuTiming::Deferred;
			pc.decals = m_cfg.decals;   // issue 4105: a decal pass that cannot start fails init only when decals are on
			pc.decalFs = m_decalFsOpen ? &m_decalFs : &m_fs;
			// issue 0417: the warm-up at init (every layer texture of the install's tables, the styles, a throwaway bake per
			// shader variant) and the pipeline's own lines (warm-up report, slow bakes and first uses with their steps)
			pc.warm = m_cfg.warm ? c5r::bake::WarmUp::AtInit : c5r::bake::WarmUp::Off;
			pc.warmTables = &m_tables;
			pc.warmSizeClass = m_cfg.res.sizeClass;
			pc.log = &PipeLog;
			// ticket 0698: the layer textures and the style inputs read by the pipeline's loader thread (mode 1)
			pc.loadAhead = m_layerLoadMode == 1 ? c5r::bake::LoadAhead::Thread : c5r::bake::LoadAhead::Off;
			// ticket 0712: prep ahead (mode 1), its TEST ONLY check
			pc.prepAhead = m_prepMode == 1 ? c5r::bake::PrepAhead::Thread : c5r::bake::PrepAhead::Off;
			pc.prepVerify = m_prepVerify;
			pipe = new c5r::bake::PipelineBaker(pc);
			m_baker.reset(pipe);
			m_pipe = pipe;
		}
	}
	else
	{
		m_standin = new c5r::bake::StandinBaker(m_cfg.faults);
		m_baker.reset(m_standin);
	}
	if(st.ok())
	{
		st = m_baker->init(dev, ctx);
		if(!st.ok() && pipe && m_cfg.decals && !pipe->decalStatus().ok())
			m_decalInitWhy = "the decal pass cannot start: " + st.str();
	}
	if(st.ok())
	{
		m_decalBaker.reset(new DecalBaker(*this));
		c5r::bake::ResidencyOptions ro = m_cfg.res;
		if(ro.nearCaches)
		{   // issue 0371: the near pairs (render_bake_near)
			m_nearBaker.reset(new NearBaker(*this));
			ro.nearBaker = m_nearBaker.get();
		}
		st = m_res.init(m_decalBaker.get(), ro, &ResLog, nullptr);
	}
	if(!st.ok())
	{
		m_res.release();
		m_decalBaker.reset();
		m_nearBaker.reset();
		m_baker.reset();
		m_pipe = nullptr;
		m_standin = nullptr;
		return st;
	}
	m_ok = true;
	m_haveLastCam = false;
	// ticket 0642: the decal pass's loader thread (modes 1 and 2); without a decal pass (stand-in, render_decals=0 with a
	// pass that did not start) nothing is loaded or held
	if(m_cfg.decals && m_decalLoadMode >= 1 && m_pipe && m_pipe->decalPass())
	{
		const c5r::Status ls = m_pipe->decalPass()->startLoader();
		if(ls.ok())
			RNote("residency: decal textures: read off the render thread by the decal pass's loader, mode %d (%s; ticket 0642, "
			      "C5_RENDER_DECAL_LOADER)", m_decalLoadMode,
			      m_decalLoadMode == 2 ? "hold: a set is used once its names are in, the tiles keep their bakes meanwhile"
			                           : "wait: setDecals waits for the loader, the order of sets and frames as before");
		else
			RNote("LOUD: residency: the decal textures' loader did not start (%s): the bakes read them on the render thread",
			      ls.str().c_str());
	}
	else if(m_cfg.decals && m_pipe && m_pipe->decalPass())
		RNote("residency: decal textures: read by the bakes on the render thread (C5_RENDER_DECAL_LOADER=0, before ticket 0642)");
	// ticket 0698: the layer textures and the style inputs
	if(m_pipe && m_pipe->loaderOn())
		RNote("residency: layer textures and style inputs: read off the render thread by the pipeline's loader (ticket 0698, "
		      "C5_RENDER_LAYER_LOADER=1): the styles from init, every layer texture of the tables at each field; a bake waits "
		      "only for a name queued or in flight");
	else if(m_pipe && m_layerLoadMode == 1)
		RNote("LOUD: residency: the layer textures' loader did not start: the bakes read the layer textures and styles on the "
		      "render thread");
	else if(m_pipe)
		RNote("residency: layer textures and style inputs: read by the bakes on the render thread (C5_RENDER_LAYER_LOADER=0, "
		      "before ticket 0698)");
	// ticket 0714
	if(m_pipe && m_pageWarmMode == 1)
		RNote("residency: height pages: built at the field (ticket 0714, C5_RENDER_PAGE_WARM=1): a fresh field builds every land "
		      "tile's page before the first draw, the bakes build none");
	else if(m_pipe)
		RNote("residency: height pages: built by their first bake (C5_RENDER_PAGE_WARM=0, before ticket 0714)");
	if(m_cfg.decals)
		RNote("residency: decals on: names and textures from %s%s (%zu mod folders, index %.1f ms)",
		      m_decalFsOpen ? "the install with the mods' folders above it" : m_cfg.pipeline ? "the install" : "nothing (stand-in without an install: names not checked)",
		      "", m_cfg.decalLooseRoots.size(), decalFsMs);
	else
		RNote("residency: render_decals=0: no decals (the picture before epic 0166)");
	RNote("residency: baker %s, install '%ls', set up in %.1f ms; radius %d, rate %.0f/s, burst %d, gpu %.0f ms/s, budget %.1f ms "
	      "per call, size class %u, arrays %d (chunk %u)",
	      m_cfg.pipeline ? "pipeline" : "stand-in", m_cfg.pipeline ? m_cfg.install.c_str() : L"-", QpcToMs(Qpc() - q0), m_cfg.res.radius,
	      m_cfg.res.rate, m_cfg.res.burst, m_cfg.res.gpuMsPerS, m_cfg.budgetMs, m_cfg.res.sizeClass, (int)m_cfg.res.arrays,
	      m_cfg.res.arrayChunk);
	if(m_cfg.res.nearCaches)
	{
		const c5r::bake::NearCacheOptions& n = m_cfg.res.nearOptions;
		RNote("residency: near pairs on (render_bake_near=1, issue 0371): pools 64 x %u, 128 x %u, 256 x %u, 512 x %u (%.1f MB "
		      "when full), min class %u, up-res %u settled / %u moving, array mode %d; state-5 bake by the %s",
		      n.pools.n64, n.pools.n128, n.pools.n256, n.pools.n512, c5r::bake::nearPoolBytes(n.pools) / 1e6, n.minClass,
		      n.upresSettled, n.perFrameMoving, (int)n.arrayMode, m_pipe ? "pipeline" : "stand-in (cleared pairs)");
	}
	else
		RNote("residency: near pairs off (render_bake_near=0): every tile is drawn from its BC1 pair");
	if(m_heldOn)
		RNote("residency: held frames: on (ticket 0424: the visible set of a frame the fog holds is baked before the frame is drawn, "
		      "once the field has its table's tile styles; C5_RENDER_HELD_BAKE=0 turns it off)");
	else
		RNote("residency: held frames: off (C5_RENDER_HELD_BAKE=0): the first frame drawn after the fog's install bakes its "
		      "visible set itself (the behaviour before 0424)");
	// ticket 0712: prep ahead
	if(m_prepBadEnv)
		RNote("LOUD: residency: C5_RENDER_PREP_AHEAD is not 0 or 1: the default (0) is used");
	if(m_prepMode && !m_pipe)
		RNote("residency: prep ahead: off (the stand-in baker has no pipeline; mode %d asked; ticket 0712)", m_prepMode);
	else if(m_pipe)
		RNote("residency: prep ahead: mode %d (%s; ticket 0712, C5_RENDER_PREP_AHEAD)%s", m_prepMode,
		      m_prepMode ? "a worker makes the next bakes' layer lists, blend vertices and normals; a bake takes them or computes inline"
		                 : "off: every bake computes its CPU products inline",
		      m_prepVerify ? "; C5_RENDER_PREP_VERIFY=1: every hit checked against the inline computation (TEST ONLY)" : "");
	// ticket 0703: the fit needs the host's yield (the live path); without it the slices stay as before
	if(m_fitBadEnv)
		RNote("LOUD: residency: C5_RENDER_BAKE_FIT is not 0, 1 or 2: the default (0) is used");
	if(m_fitMode && !m_cfg.res.yield)
	{
		RNote("residency: idle bakes fitted between cameras: off (render_bake_yield=0 or no host yield; mode %d asked; ticket 0703)",
		      m_fitMode);
		m_fitMode = 0;
	}
	else
		RNote("residency: idle bakes fitted between cameras: mode %d (%s; ticket 0703, C5_RENDER_BAKE_FIT): a bake starts only when "
		      "it is expected to end %.1f ms before the next camera, one bake at the latest %.0f ms after the last admitted slice",
		      m_fitMode, m_fitMode == 0 ? "off: the slices before 0703" : m_fitMode == 1 ? "while the camera moves" : "always",
		      kBakeFitMarginMs, kBakeFitMaxDeferMs);
	return st;
}

void ResidencySource::onField(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid, uint32_t tilePlots, uint32_t loadSeq)
{
	m_field = false;
	if(!m_ok)
		return;
	prepJoin();   // ticket 0712: the writer's own join is the scene's, before it wrote; this one empties the cache
	if(dev != m_dev)
	{
		fieldsRefused++;
		RNote("LOUD: residency: onField on another device than init's: refused");
		return;
	}
	// the residency compares the new field with the last (same size: only changed tiles get a new generation), then sets
	// the caches it still holds again at the next beforeDraw (setHeightField dropped them in the renderer)
	c5r::bake::MapInputs mi;
	mi.tables = m_cfg.pipeline ? &m_tables : nullptr;
	mi.wrapX = grid.wrapX;
	// issue 4305: the plot records the field's bake stands for (read before the bake's setMap since 4305: the art types
	// come from them), then the art types of the DLL's map (the field's rows - 4). The same size as the last field keeps
	// the array (and its address, which the baker holds); another size starts at 0 (Generic) until a table of this size.
	// epic 0465, ticket 4907: a live-terrain edit of the same load's field keeps the table (no feed read: the scene bundle's
	// read order stays that of a load; keepTableOnNextField)
	if(m_keepTableOnce && m_rec.size())
		m_keepTableOnce = false;
	else
	{
		m_keepTableOnce = false;
		takeBaseline();
	}
	const std::vector<uint8_t> stylesBefore = m_tileStyle;
	const bool sameSize = grid.mapH > 4 && m_artX == grid.mapW && m_artY == grid.mapH - 4 && m_tilesY == grid.mapH;
	if(!sameSize)
	{
		m_art.clear();
		m_tileStyle.clear();
		m_artX = m_artY = m_tilesX = m_tilesY = 0;
		m_artFromTable = false;
		if(grid.mapH > 4)
		{
			m_artX = grid.mapW;
			m_artY = grid.mapH - 4;
			m_tilesX = grid.mapW;
			m_tilesY = grid.mapH;
			m_art.assign((size_t)m_artX * m_artY, 0);
		}
	}
	m_artWrap = grid.wrapX;
	// a table already taken for an earlier load (no snapshot since) may be that load's: kept, and the next generation is
	// taken (the feed's snapshot normally comes about 4 s before the field: 0368's and 0140's server.log)
	const bool newLoad = !m_artFromTable || loadSeq != m_artLoadSeq;
	const bool oldTable = m_artFromTable && m_artGen == m_recGen;
	artFromTable();
	m_artStale = newLoad && oldTable;
	m_artLoadSeq = loadSeq;
	restyle(false);
	m_stylesChanged = true;   // a new field: the renderer's styles are Generic again (setHeightField) until set
	if(!m_art.empty())
	{
		mi.plotArtTypes = m_art.data();
		mi.plotsX = m_artX;
		mi.plotsY = m_artY;
	}
	const c5r::Status st = m_res.onField(dev, grid, tilePlots, loadSeq, &mi);
	if(!st.ok())
	{
		fieldsRefused++;
		RNote("LOUD: residency refused the field (%ux%u plots, tile plots %u, load seq %u): %s", grid.mapW, grid.mapH, tilePlots, loadSeq,
		      st.str().c_str());
		return;
	}
	// ticket 4909 (live-terrain.md 8.14.2): the pipeline's height pages are kept by the grid's address; a field applied
	// whole (a live edit re-applied, a new load at the old address) is baked from its own samples
	if(m_pipe)
		m_pipe->heightsChanged(grid, nullptr, 0);
	// ticket 0714: the pages of a new load's field (or of a field of another size) before its first draw; the same load's
	// field again (a live edit applied whole) keeps the lazy build of the pages it dropped
	if(m_pipe && m_pageWarmMode == 1 && (!m_pageWarmAny || loadSeq != m_pageWarmLoad || !sameSize))
		warmFieldPages(loadSeq);
	m_field = true;
	m_pendingAt = 0;
	// ticket 0424: a new field: no held camera prepared yet, its first drawn frame not seen
	m_heldPending = false;
	m_heldDoneStamp = -1;
	m_heldSinceField = m_heldBakesSinceField = 0;
	m_heldMsSinceField = 0;
	m_heldStyleNoted = false;
	m_drawnSinceField = false;
	m_fitGapOpen = false;   // ticket 0703: the gap across a load is not a camera gap
	// issue 4305: the same field again (tiles kept) with other styles: those tiles are baked again
	if(sameSize && stylesBefore.size() == m_tileStyle.size())
		for(size_t i = 0; i < m_tileStyle.size(); ++i)
			if(stylesBefore[i] != m_tileStyle[i] && m_res.raiseTile((uint32_t)(i % m_tilesX), (uint32_t)(i / m_tilesX), QpcToMs(Qpc())))
				styleRaises++;
	{
		uint32_t n[5] = {};
		for(uint8_t s : m_tileStyle)
			n[s < 5 ? s : 0]++;
		RNote("residency: tile styles of load seq %u: art types %ux%u from %s (table generation %u), tiles Generic %u America %u "
		      "Asia %u Africa %u Europe %u, fnv64 %016llx",
		      loadSeq, m_artX, m_artY, m_artFromTable ? "the feed's table" : "none yet (all Generic)", m_artGen, n[0], n[1], n[2],
		      n[3], n[4], (unsigned long long)stylesFnv());
	}
	const c5r::bake::ResidencyStats& s = m_res.stats();
	// issue 4105: nothing resident = every bake of this field reads the set in force or a newer one, so the changes of
	// the generations up to it need no re-bake (they would bake the decal tiles twice at the load)
	m_decalGenAtField = s.resident == 0 && m_decals ? m_decals->generation : 0;
	m_pendingGens.clear();
	// 0365: nothing resident and no decal set of this load: the bake-ahead waits for the first one (bounded)
	m_aheadHold = false;
	if(m_cfg.decals && m_cfg.decalAheadWaitMs > 0 && s.resident == 0 && !(m_decals && m_decals->loadSeq == loadSeq))
	{
		m_aheadHold = true;
		m_aheadHoldFrom = Qpc();
		m_aheadHoldUntil = m_aheadHoldFrom + (int64_t)(m_cfg.decalAheadWaitMs * (double)QpcFreq() / 1000.0);
		m_aheadHoldLoad = loadSeq;
		m_dc.aheadHolds++;
		RNote("residency: bake-ahead of load %u waits up to %.0f ms for the load's first decal set (%s); the visible tiles are baked "
		      "meanwhile", loadSeq, m_cfg.decalAheadWaitMs, m_decals ? "the set in force is of another load" : "no set yet");
	}
	RNote("residency: field of load seq %u: %ux%u tiles, land %u (class 2 %u, class 3 %u), resident %u, field changes %lld tiles", loadSeq,
	      s.tilesX, s.tilesY, s.land, s.class2, s.class3, s.resident, (long long)s.fieldChangedTiles);
}

// Ticket 0714 (ResidencySourceConfig::pageWarm): every land tile's height page, built on the render thread inside onField,
// after the residency's compare (its land classes) and the baker's setMap; one line with the cost (the load-time price).
void ResidencySource::warmFieldPages(uint32_t loadSeq)
{
	const uint32_t tx = m_res.tilesX(), ty = m_res.tilesY();
	std::vector<c5r::bake::TileId> land;
	land.reserve((size_t)tx * ty);
	for(uint32_t y = 0; y < ty; ++y)
		for(uint32_t x = 0; x < tx; ++x)
			if(m_res.tileClass(x, y) >= 2)
			{
				c5r::bake::TileId t;
				t.x = x;
				t.y = y;
				land.push_back(t);
			}
	c5r::bake::PageWarmReport r;
	const c5r::Status st = m_pipe->warmPages(land.data(), land.size(), &r);
	m_lastPageWarm = r;
	m_pageWarms++;
	m_pageWarmAny = true;
	m_pageWarmLoad = loadSeq;
	if(st.ok())
		RNote("residency: height pages of load seq %u built at the field (ticket 0714): %u pages for %u land tiles, %u built, in "
		      "%.1f ms (thread CPU %.1f ms; %.1f MB of video memory the bakes would have built later)",
		      loadSeq, r.pages, r.tiles, r.built, r.ms, r.threadMs, r.built * (double)c5r::bake::kHeightPageBytes / 1e6);
	else
		RNote("LOUD: residency: height pages of load seq %u at the field: %s (%u pages for %u land tiles, %u built in %.1f ms); "
		      "the bakes build the rest", loadSeq, st.str().c_str(), r.pages, r.tiles, r.built, r.ms);
}

// Epic 0465, ticket 4909: the live edit's region (srv_residency_source.h). The parts of onField a live edit needs, in
// its order: the table kept (no takeBaseline), the art types and styles from it, the baker's inputs, the residency's
// region compare, the re-bake of tiles whose style changed.
bool ResidencySource::onFieldRegion(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid,
                                    const std::vector<c5r::terrain::SampleRect>& rects, c5r::bake::RegionFieldStats* stats,
                                    std::string& why)
{
	if(!m_ok || !m_field)
	{
		why = !m_ok ? "the residency is not set up" : "no field applied yet";
		return false;
	}
	if(dev != m_dev)
	{
		why = "another device than init's";
		return false;
	}
	const bool sameSize = grid.mapH > 4 && m_artX == grid.mapW && m_artY == grid.mapH - 4 && m_tilesY == grid.mapH;
	if(!sameSize)
	{
		why = "the art types are not of this field's size";
		return false;
	}
	prepJoin();   // ticket 0712: as onField's
	m_keepTableOnce = false;   // the table is kept as onField's live-edit case keeps it
	c5r::bake::MapInputs mi;
	mi.tables = m_cfg.pipeline ? &m_tables : nullptr;
	mi.wrapX = grid.wrapX;
	const std::vector<uint8_t> stylesBefore = m_tileStyle;
	artFromTable();
	m_artStale = false;   // onField's rule for the same load (newLoad false, or no table before: never stale)
	restyle(false);
	m_stylesChanged = true;
	if(!m_art.empty())
	{
		mi.plotArtTypes = m_art.data();
		mi.plotsX = m_artX;
		mi.plotsY = m_artY;
	}
	const c5r::Status st = m_res.onFieldRegion(dev, grid, rects, &mi, stats);
	if(!st.ok())
	{
		fieldsRefused++;
		why = "Residency::onFieldRegion: " + st.str();
		return false;
	}
	if(m_pipe)
	{   // 8.14.2: the pipeline's height pages built from the changed samples are dropped (built again at their next bake)
		std::vector<uint32_t> boxes;
		for(const c5r::terrain::SampleRect& r : rects)
		{
			boxes.push_back(r.x0);
			boxes.push_back(r.y0);
			boxes.push_back(r.x1);
			boxes.push_back(r.y1);
		}
		m_pipe->heightsChanged(grid, boxes.data(), rects.size());
	}
	if(stylesBefore.size() == m_tileStyle.size())
		for(size_t i = 0; i < m_tileStyle.size(); ++i)
			if(stylesBefore[i] != m_tileStyle[i] && m_res.raiseTile((uint32_t)(i % m_tilesX), (uint32_t)(i / m_tilesX), QpcToMs(Qpc())))
				styleRaises++;
	return true;
}

// Issue 3308 (live run r1): the feed names every plot whose record changed, mostly fog (~1,400 GameplayFOWChanged per AI
// turn on the huge save) and owner; each raised 35 tiles and the server re-baked ~700 tiles per turn (10,211 re-bakes in
// 10 turns). Only what the EXE's own tile invalidation reacts to counts (terrain-bake-residency.md 3: terrain and
// height edits, decals: routes, improvements, features); fog, owner, the city bit and the bookkeeping fields do not.
// A layout field (GameplayPlotStateChange's) counts only once a layout was seen for the plot before (the first one after a
// load describes the state the field's own bake was made for), a getter field only once the plot was read before.
static bool BakeRelevantChange(const SrvFeed::PlotRecord& a, const SrvFeed::PlotRecord& b)
{
	const bool getters = (a.known & 1) != 0, layout = (a.known & 2) != 0;
	if(getters && (a.terrain != b.terrain || a.plotType != b.plotType || a.feature != b.feature || (a.flags & 0x9) != (b.flags & 0x9)))
		return true;
	if(layout && (a.route != b.route || a.resource != b.resource || a.improvement != b.improvement ||
	              a.improvementState != b.improvementState || a.routeState != b.routeState || (a.flags & 0x6) != (b.flags & 0x6)))
		return true;
	return false;
}

void ResidencySource::takeBaseline()
{
	// epic 0301, issue 3903: through the FeedSource (live: SrvFeed::CopyPlots, as before)
	(m_cfg.feed ? *m_cfg.feed : LiveFeed()).copyPlots(m_rec, m_recGen, m_recW, m_recH, kPlotsBaseline);
}

// ---- issue 4305: the tile styles ------------------------------------------------------------------------------------
bool ResidencySource::artFromTable()
{
	if(m_art.empty() || m_recW != m_artX || m_recH != m_artY || m_rec.size() != m_art.size())
		return false;
	if(m_artFromTable && m_artGen == m_recGen)
		return false;   // this table's art types are in already (in play: followArtChanges decides per plot)
	bool changed = false;
	for(size_t i = 0; i < m_art.size(); ++i)
	{
		const uint8_t a = m_rec[i].artType <= 4 ? m_rec[i].artType : 0;
		changed = changed || m_art[i] != a;
		m_art[i] = a;
	}
	m_artFromTable = true;
	m_artGen = m_recGen;
	artFills++;
	return changed;
}

// Ticket 4555 (trees.md 15.4): a table generation the field has not taken yet (header). The same intake as
// onPlotsChanged's new-table branch, without changed plots; the line names the late table as onField's names its table.
bool ResidencySource::onFeedGeneration(uint32_t snapshotGen)
{
	if(!m_ok || !m_field || (m_artFromTable && !m_artStale) || snapshotGen == m_recGen || snapshotGen == m_artPolledGen)
		return false;
	m_artPolledGen = snapshotGen;
	std::vector<SrvFeed::PlotRecord> cur;
	uint32_t gen = 0, w = 0, h = 0;
	(m_cfg.feed ? *m_cfg.feed : LiveFeed()).copyPlots(cur, gen, w, h, kPlotsChanged);
	if(gen == m_recGen)
		return false;
	m_rec.swap(cur);
	m_recGen = gen;
	m_recW = w;
	m_recH = h;
	const bool had = m_artFromTable;
	const bool changed = artFromTable();
	if(changed || !had)
	{
		restyle(true);
		m_stylesChanged = true;
	}
	m_artStale = false;
	uint32_t n[5] = {};
	for(uint8_t st : m_tileStyle)
		n[st < 5 ? st : 0]++;
	RNote("residency: tile styles of load seq %u after the field: art types %ux%u from %s (table generation %u, %ux%u), tiles "
	      "Generic %u America %u Asia %u Africa %u Europe %u, fnv64 %016llx (ticket 4555)",
	      m_artLoadSeq, m_artX, m_artY, m_artFromTable ? "the feed's table" : "none yet (all Generic)", m_artGen, w, h, n[0], n[1],
	      n[2], n[3], n[4], (unsigned long long)stylesFnv());
	return changed || !had;
}

void ResidencySource::restyle(bool raise)
{
	if(m_art.empty())
	{
		m_tileStyle.clear();
		return;
	}
	m_tileStyle.resize((size_t)m_tilesX * m_tilesY, 0);
	const double now = raise ? QpcToMs(Qpc()) : 0;
	for(uint32_t y = 0; y < m_tilesY; ++y)
		for(uint32_t x = 0; x < m_tilesX; ++x)
		{
			c5r::bake::TileId t;
			t.x = x;
			t.y = y;
			const uint8_t s = (uint8_t)c5r::bake::tileStyle(m_art.data(), m_artX, m_artY, m_artWrap, t);
			uint8_t& cur = m_tileStyle[(size_t)y * m_tilesX + x];
			if(s != cur)
			{
				cur = s;
				if(raise && m_res.raiseTile(x, y, now))
					styleRaises++;
			}
		}
}

bool ResidencySource::artTypes(const uint8_t*& art, uint32_t& plotsX, uint32_t& plotsY, bool& wrapX) const
{
	if(m_art.empty())
		return false;
	art = m_art.data();
	plotsX = m_artX;
	plotsY = m_artY;
	wrapX = m_artWrap;
	return true;
}

uint32_t ResidencySource::tileStyleAt(uint32_t x, uint32_t y) const
{
	return x < m_tilesX && y < m_tilesY && !m_tileStyle.empty() ? m_tileStyle[(size_t)y * m_tilesX + x] : 0;
}

uint64_t ResidencySource::stylesFnv() const
{
	uint64_t h = 1469598103934665603ull;
	for(uint8_t s : m_tileStyle)
	{
		h ^= s;
		h *= 1099511628211ull;
	}
	return h;
}

void ResidencySource::onPlotsChanged(const uint32_t* plotIndices, size_t count)
{
	plotsChanged += (int64_t)count;
	if(!m_ok || !m_field || !count)
		return;
	std::vector<SrvFeed::PlotRecord> cur;
	uint32_t gen = 0, w = 0, h = 0;
	(m_cfg.feed ? *m_cfg.feed : LiveFeed()).copyPlots(cur, gen, w, h, kPlotsChanged);   // issue 3903 (live: as before)
	if(gen != m_recGen || cur.size() != m_rec.size())
	{
		// a new table (a load, a dropped queue): the baseline; its changes are the field's own state, not edits
		m_rec.swap(cur);
		m_recGen = gen;
		m_recW = w;
		m_recH = h;
		plotsFiltered += (int64_t)count;
		// issue 4305: its art types when this field has none from a table yet, or only an earlier load's (a table that came
		// after the field); a new table in play (a dropped queue) only with followArtChanges, as a changed plot (12.6):
		// the changed styles re-baked
		if(m_cfg.followArtChanges || !m_artFromTable || m_artStale)
		{
			const bool had = m_artFromTable;
			if(artFromTable() || !had)
			{
				restyle(true);
				m_stylesChanged = true;
			}
			m_artStale = false;
		}
		else if(m_recW == m_artX && m_recH == m_artY && m_recGen != m_artGen)
			artIgnored++;   // counted once per table (the plots' own changes count in the per-plot branch)
		return;
	}
	m_pass.clear();
	bool artChanged = false;
	for(size_t k = 0; k < count; ++k)
	{
		const uint32_t i = plotIndices[k];
		if(i >= cur.size())
		{
			m_pass.push_back(i);   // outside the table: the residency counts and ignores it
			continue;
		}
		if(BakeRelevantChange(m_rec[i], cur[i]))
			m_pass.push_back(i);
		// issue 4305: an art type changed in play (VP's farm): taken only with followArtChanges (the EXE keeps the load's)
		if(m_rec[i].artType != cur[i].artType && i < m_art.size() && (m_rec[i].known & 1))
		{
			const uint8_t a = cur[i].artType <= 4 ? cur[i].artType : 0;
			if(m_cfg.followArtChanges)
			{
				artPlotChanges++;
				artChanged = artChanged || m_art[i] != a;
				m_art[i] = a;
			}
			else
				artIgnored++;
		}
		m_rec[i] = cur[i];
	}
	if(artChanged)
	{
		restyle(true);
		m_stylesChanged = true;
	}
	plotsFiltered += (int64_t)(count - m_pass.size());
	plotsPassed += (int64_t)m_pass.size();
	// the feed's index is the DLL's plot index y * width + x of the game's map, the residency's index exactly (residency.h)
	if(!m_pass.empty())
		m_res.onPlotsChanged(m_pass.data(), m_pass.size(), QpcToMs(Qpc()));
}

void ResidencySource::beforeDraw(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double budgetMs)
{
	pumpHeldDecals();   // ticket 0642: a held set whose names are in is used before this frame's bakes
	if(!m_ok || !m_field || !m_res.hasField())
		return;
	// ticket 0703: a camera came: the gap since the last pass's end is a sample when no bake ran in it
	const double camMs = QpcToMs(Qpc());
	if(!m_inSettle)
	{
		if(m_fitGapOpen && m_bc1Bakes == m_fitGapBakes0 && m_fitReadyMs >= 0)
		{
			m_gaps.push(camMs - m_fitReadyMs);
			m_fit.gapSamples++;
		}
		m_fitGapOpen = false;
		m_drawSeq++;
	}
	// ticket 0424: a drawn frame makes a held camera noted before it stale. Live r1b-run1 (2026-11-18 03:48): the load
	// screen's held frame waited for the table's styles, a 4.3 s slow bake held the render thread past the click, the next
	// frame was drawn at once, and the old view was prepared after it (87 bakes, 274 ms of tiles the player did not see)
	if(m_heldPending)
	{
		m_heldPending = false;
		heldDropped++;
	}
	m_bakeKind = m_inSettle ? kKindSettle : kKindPredraw;   // ticket 0712
	if(m_drawnSinceField)
	{
		forwardDraw(ctx, r, view, budgetMs);
		if(m_lastMoving && !m_inSettle)
			m_fitMovingMs = camMs;   // ticket 0703
		requestPrep(true);         // ticket 0712: the next frames' bakes, made beside this frame's pass
		stepsLine(QpcToMs(Qpc()));
		return;
	}
	// ticket 0424: the field's first drawn frame (Scene::draw; the held frames' preparations do not count): what it baked
	// before its draw, the acceptance's "first-draw bake"
	m_drawnSinceField = true;
	const int64_t b0 = m_res.stats().bakes;
	const int64_t q0 = Qpc();
	forwardDraw(ctx, r, view, budgetMs);
	const double ms = QpcToMs(Qpc() - q0);
	const c5r::bake::ResidencyStats& s = m_res.stats();
	RNote("residency: first drawn frame of load %u (frame %lld): %lld bakes in %.1f ms before the draw; %u visible land tiles; "
	      "%lld held frame(s) prepared before it (%lld bakes in %.1f ms; ticket 0424, held frames %s; held cameras dropped so far %lld)",
	      m_artLoadSeq, (long long)view.frameId, (long long)(s.bakes - b0), ms, s.visibleLand, (long long)m_heldSinceField,
	      (long long)m_heldBakesSinceField, m_heldMsSinceField, m_heldOn ? "on" : "off", (long long)heldDropped);
}

// ticket 0424 (srv_residency_source.h): the camera of a frame the fog holds, as Scene::draw would make its ViewInfo (the
// target is the composite's back buffer, VPShellCamera::bbW x bbH, else the EXE's viewport)
void ResidencySource::noteHeldView(const VPShellCamera& cam)
{
	if(!m_heldOn || !m_ok || !m_field)
		return;
	if(m_heldPending && m_heldView.frameId == cam.frameStamp)
		return;
	ViewInfo& v = m_heldView;
	v.frameId = cam.frameStamp;
	memcpy(v.camWorld, cam.camWorld, sizeof(v.camWorld));
	memcpy(v.view, cam.view[0], sizeof(v.view));
	memcpy(v.proj, cam.proj, sizeof(v.proj));
	v.width = cam.bbW ? cam.bbW : cam.vpW;
	v.height = cam.bbH ? cam.bbH : cam.vpH;
	m_heldPending = true;
}

bool ResidencySource::prepareHeld(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r)
{
	if(!m_heldPending)
		return false;
	if(!m_heldOn || !m_ok || !m_field || !m_res.hasField() || m_heldView.frameId == m_heldDoneStamp)
	{
		m_heldPending = false;
		return false;
	}
	if(!m_artFromTable || m_artStale)
	{
		// the bake's tile styles are not this load's yet (ticket 4555): kept pending, tried again at the next idle pass
		if(!m_heldStyleNoted)
		{
			m_heldStyleNoted = true;
			RNote("residency: held frame %lld: the visible set's preparation waits for the plot table's tile styles of load %u (%s)",
			      (long long)m_heldView.frameId, m_artLoadSeq, m_artStale ? "only an earlier load's table" : "no table yet");
		}
		return false;
	}
	m_heldPending = false;
	m_heldDoneStamp = m_heldView.frameId;
	const int64_t b0 = m_res.stats().bakes;
	const int64_t q0 = Qpc();
	// No frame waits on a held one, so the per-frame CPU budget does not apply: with no more tiles without a bake than the
	// burst (no catch-up) the residency's loop stops at budgetMs, which a slow bake (the stand-in under ASan, 28 ms,
	// 2026-11-18) reaches after one tile. So a budget no bake reaches, and again while a pass baked and a visible land tile
	// is still without a bake (grayTileFrames grows by that count per pass), at most kHeldPasses passes.
	const int kHeldPasses = 8;
	const double kHeldBudgetMs = 1.0e6;
	m_bakeKind = kKindHeld;   // ticket 0712
	for(int pass = 0; pass < kHeldPasses; ++pass)
	{
		const int64_t pb = m_res.stats().bakes, pg = m_res.stats().grayTileFrames;
		forwardDraw(ctx, r, m_heldView, kHeldBudgetMs);
		const c5r::bake::ResidencyStats& ps = m_res.stats();
		if(ps.bakes == pb || ps.grayTileFrames == pg || ps.stopped)
			break;
	}
	const double ms = QpcToMs(Qpc() - q0);
	const c5r::bake::ResidencyStats& s = m_res.stats();
	const int64_t made = s.bakes - b0;
	heldPrepares++;
	heldBakes += made;
	heldMs += ms;
	m_heldSinceField++;
	m_heldBakesSinceField += made;
	m_heldMsSinceField += ms;
	if(made > 0 || m_heldSinceField <= 3)
		RNote("residency: held frame %lld (the fog holds it): the visible set prepared before the draw: %lld bakes in %.1f ms; "
		      "%u visible land tiles (ticket 0424)",
		      (long long)m_heldView.frameId, (long long)made, ms, s.visibleLand);
	return made > 0;
}

void ResidencySource::forwardDraw(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double budgetMs)
{
	c5r::bake::ResidencyView rv;
	rv.frameId = view.frameId;
	memcpy(rv.camWorld, view.camWorld, sizeof(rv.camWorld));
	memcpy(rv.view, view.view, sizeof(rv.view));
	memcpy(rv.proj, view.proj, sizeof(rv.proj));
	rv.width = view.width;
	rv.height = view.height;
	// issue 0371: the camera moves when its view or projection changed since the last draw (the shell does not publish the
	// EXE's TS+0x75FE4; terrain-bake-passes.md 3.5.4); the first draw of a source is settled
	rv.moving = m_haveLastCam && (memcmp(m_lastView, view.view, sizeof(m_lastView)) != 0 ||
	                              memcmp(m_lastProj, view.proj, sizeof(m_lastProj)) != 0);
	memcpy(m_lastView, view.view, sizeof(m_lastView));
	memcpy(m_lastProj, view.proj, sizeof(m_lastProj));
	m_haveLastCam = true;
	m_lastMoving = rv.moving;   // ticket 0703
	(rv.moving ? m_movingFrames : m_settledFrames)++;
	m_res.beforeDraw(ctx, r, rv, budgetMs);
	m_res.takeVisibleChanged();   // this draw shows them
}

void ResidencySource::idle(ID3D11DeviceContext* ctx, double budgetMs)
{
	pumpHeldDecals();   // ticket 0642
	if(m_ok && m_field && m_res.hasField() && !aheadHeld())
	{
		m_bakeKind = m_inSettle ? kKindSettle : kKindIdle;   // ticket 0712
		if(m_inSettle || m_fitMode == 0)   // ticket 0703: the offline players and mode 0 as before
			m_res.idle(ctx, budgetMs);
		else
			fitIdle(ctx, budgetMs);
		requestPrep(false);   // ticket 0712: the next slices' bakes
		stepsLine(QpcToMs(Qpc()));
	}
}

// ---- ticket 0703: idle bakes fitted between cameras (srv_residency_source.h, ResidencySourceConfig::bakeFit) -------------
double BakeFitBudget(double budgetMs, double nowMs, double readyMs, double gapMs, double bakeMs, double sinceAdmitMs, int* why)
{
	const double window = readyMs + gapMs - kBakeFitMarginMs - nowMs;   // ms left before the expected camera
	int w = 2;
	double use = 0;
	if(budgetMs > 0 && window >= bakeMs)
	{
		const double room = window - bakeMs;   // a bake may start until then (Residency::idle starts the next bake while
		                                       // the slice's time is within its budget)
		if(room >= budgetMs)
		{
			use = budgetMs;
			w = 0;
		}
		else
		{
			use = (std::max)(room, 0.001);
			w = 1;
		}
	}
	else if(budgetMs > 0 && (sinceAdmitMs < 0 || sinceAdmitMs >= kBakeFitMaxDeferMs))
	{
		use = 0.001;   // one bake: the slice's first check passes, the second finds the budget spent
		w = 3;
	}
	if(why)
		*why = w;
	return use;
}

double ResidencySource::FitRing::pct(double p) const
{
	if(n <= 0)
		return -1;
	double s[32];
	for(int i = 0; i < n; ++i)
		s[i] = v[i];
	int k = (int)(p * (n - 1) + 0.5);
	k = k < 0 ? 0 : k >= n ? n - 1 : k;
	std::nth_element(s, s + k, s + n);
	return s[k];
}

void ResidencySource::fitIdle(ID3D11DeviceContext* ctx, double budgetMs)
{
	const double now = QpcToMs(Qpc());
	if(m_idleSeq != m_drawSeq)
	{   // the first call after a drawn frame: its pass has ended, a gap opens until the next camera's beforeDraw
		m_idleSeq = m_drawSeq;
		m_fitReadyMs = now;
		m_fitGapOpen = true;
		m_fitGapBakes0 = m_bc1Bakes;
		m_fitGapDeferred = false;
	}
	if(budgetMs <= 0)
	{   // the host's own rule: a camera waits already
		m_res.idle(ctx, budgetMs);
		fitLine(now);
		return;
	}
	const int64_t b0 = m_bc1Bakes;
	const bool moving = now - m_fitMovingMs < kBakeFitHoldMs;
	const double bakeEst = m_bakeTimes.n >= kBakeFitMinSamples ? m_bakeTimes.pct(0.75) : -1;
	if((m_fitMode == 1 && !moving) || bakeEst < 0 || m_fitReadyMs < 0)
	{   // at rest (mode 1), no bake estimate yet, or no frame drawn yet: the slice as before
		m_res.idle(ctx, budgetMs);
		if(m_bc1Bakes != b0 || m_pending)
		{
			m_fit.legacy++;
			m_fitLineDue = true;
		}
		if(m_bc1Bakes != b0)
			m_fitAdmitMs = QpcToMs(Qpc());
		fitLine(now);
		return;
	}
	const double gapEst = m_gaps.n >= kBakeFitMinSamples ? m_gaps.pct(0.25) : -1;
	m_fit.gapMs = gapEst;
	m_fit.bakeMs = bakeEst;
	const double since = m_fitAdmitMs >= 0 ? now - m_fitAdmitMs : -1;
	int why = 2;
	double use = 0;
	if(gapEst < 0)
	{   // no undisturbed gap measured yet: the bakes wait (their gaps become the samples), the bound still admits one
		if(since < 0 || since >= kBakeFitMaxDeferMs)
		{
			use = 0.001;
			why = 3;
		}
	}
	else
		use = BakeFitBudget(budgetMs, now, m_fitReadyMs, gapEst, bakeEst, since, &why);
	m_res.idle(ctx, use);
	const int64_t made = m_bc1Bakes - b0;
	// the counters name calls that had work: a bake made, or background work pending (pending()'s value, which the host
	// reads every pass); after the bake-ahead with nothing raised a call decides nothing
	if(!made && !m_pending)
	{
		fitLine(now);
		return;
	}
	m_fitLineDue = true;
	switch(why)
	{
	case 0: m_fit.admitted++; break;
	case 1: m_fit.cut++; break;
	case 3: m_fit.forced++; break;
	default:
		m_fit.deferred++;
		if(!m_fitGapDeferred)
		{
			m_fitGapDeferred = true;
			m_fit.deferredGaps++;
		}
		break;
	}
	if(made)
	{
		m_fit.fitBakes += made;
		m_fitAdmitMs = QpcToMs(Qpc());
	}
	fitLine(now);
}

void ResidencySource::fitLine(double nowMs)
{
	if(m_fitLineMs < 0)
	{
		m_fitLineMs = nowMs;
		m_fitLast = m_fit;
		return;
	}
	if(nowMs - m_fitLineMs < 1000.0)
		return;
	if(m_fitLineDue)
	{
		const BakeFitCounters& c = m_fit;
		const BakeFitCounters& l = m_fitLast;
		RLog("residency fit 1s: mode=%d moving=%d gap_ms=%.2f bake_ms=%.2f admitted=%lld cut=%lld deferred=%lld deferred_gaps=%lld "
		     "forced=%lld legacy=%lld fit_bakes=%lld gap_samples=%lld bake_samples=%lld | tot admitted=%lld cut=%lld deferred=%lld "
		     "forced=%lld legacy=%lld fit_bakes=%lld",
		     m_fitMode, (int)(nowMs - m_fitMovingMs < kBakeFitHoldMs), c.gapMs, c.bakeMs, (long long)(c.admitted - l.admitted),
		     (long long)(c.cut - l.cut), (long long)(c.deferred - l.deferred), (long long)(c.deferredGaps - l.deferredGaps),
		     (long long)(c.forced - l.forced), (long long)(c.legacy - l.legacy), (long long)(c.fitBakes - l.fitBakes),
		     (long long)(c.gapSamples - l.gapSamples), (long long)(c.bakeSamples - l.bakeSamples), (long long)c.admitted,
		     (long long)c.cut, (long long)c.deferred, (long long)c.forced, (long long)c.legacy, (long long)c.fitBakes);
	}
	m_fitLineMs = nowMs;
	m_fitLast = m_fit;
	m_fitLineDue = false;
}

// 0365: the bake-ahead's wait for the load's first decal set (srv_residency_source.h, decalAheadWaitMs)
bool ResidencySource::aheadHeld()
{
	if(!m_aheadHold)
		return false;
	if(Qpc() < m_aheadHoldUntil)
		return true;
	m_dc.aheadHoldsTimedOut++;
	endAheadHold("the bound passed without a set");
	return false;
}

void ResidencySource::endAheadHold(const char* why)
{
	if(!m_aheadHold)
		return;
	m_aheadHold = false;
	const double ms = QpcToMs(Qpc() - m_aheadHoldFrom);
	m_dc.aheadHoldMsMax = (std::max)(m_dc.aheadHoldMsMax, ms);
	RNote("residency: bake-ahead of load %u resumed after %.1f ms: %s", m_aheadHoldLoad, ms, why);
}

bool ResidencySource::pending()
{
	// ticket 0642: a held set is background work: the host polls again soon (and the poll applies it once its names are in)
	if(m_ok && m_heldSet && !pumpHeldDecals())
		return true;
	if(!m_ok || !m_field || !m_res.hasField())
		return false;
	// stats() walks every tile (10,752 on the huge map): at most every 100 ms
	const int64_t now = Qpc();
	if(m_pendingAt && QpcToMs(now - m_pendingAt) < 100.0)
		return m_pending;
	m_pendingAt = now;
	const c5r::bake::ResidencyStats& s = m_res.stats();
	m_pending = !s.stopped && (!s.aheadDone || s.pending > 0);
	return m_pending;
}

// epic 0301, issue 3903: the settle call (srv_residency_source.h; offline-scene-harness.md 6)
c5r::Status ResidencySource::settle(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double maxMs,
                                    SettleResult* out)
{
	if(!out || !std::isfinite(maxMs) || maxMs < 0)
		return c5r::Status::fail(c5r::Err::BadLayout, "settle: no result, or maxMs not finite or negative");
	*out = SettleResult();
	if(!m_ok || !m_field || !m_res.hasField())
	{
		out->what = !m_ok ? "residency not started" : "no field";
		return c5r::Status::fail(c5r::Err::NotFound, "settle: " + out->what);
	}
	const int64_t q0 = Qpc();
	// ticket 0703: no fit and no fit samples while settling (the offline players' bakes stay as before 0703)
	struct SettleFlag
	{
		bool& f;
		const bool was;
		explicit SettleFlag(bool& x) : f(x), was(x) { f = true; }
		~SettleFlag() { f = was; }
	} settleFlag(m_inSettle);
	// ticket 0642: a held set is waited for (within maxMs) and used before the first bake of the settle
	while(m_heldSet && !pumpHeldDecals() && QpcToMs(Qpc() - q0) < maxMs)
		Sleep(1);
	// ticket 0495: no up-res pass while settling (the live server ran it once per drawn frame; Scene::draw runs it)
	struct NearHold
	{
		c5r::bake::Residency& r;
		explicit NearHold(c5r::bake::Residency& x) : r(x) { r.setNearHold(true); }
		~NearHold() { r.setNearHold(false); }
	} nearHold(m_res);
	for(;;)
	{
		const c5r::bake::ResidencyStats& s0 = m_res.stats();
		const int64_t gray0 = s0.grayTileFrames, stale0 = s0.staleFrames, bakes0 = s0.bakes;
		beforeDraw(ctx, r, view, m_cfg.budgetMs);
		const c5r::bake::ResidencyStats& s1 = m_res.stats();
		// the residency counts, per beforeDraw, visible land tiles without any bake (grayTileFrames) and whether one had a
		// stale bake (staleFrames + 1): together a lower bound of the visible land tiles without a current bake
		out->visibleUnbaked = (uint32_t)((s1.grayTileFrames - gray0) + (s1.staleFrames - stale0));
		endAheadHold("settle bakes without the wait (0365: the offline harness has no tap)");
		idle(ctx, m_cfg.budgetMs);
		out->calls++;
		const c5r::bake::ResidencyStats& s = m_res.stats();   // reread here: not pending()'s 100 ms cache
		out->pending = s.pending;
		out->aheadDone = s.aheadDone;
		out->stopped = s.stopped;
		out->ms = QpcToMs(Qpc() - q0);
		if(s.stopped)
			break;
		if(s.aheadDone && s.pending == 0 && out->visibleUnbaked == 0)
		{
			out->settled = true;
			break;
		}
		if(out->ms >= maxMs)
			break;
		if(s.bakes == bakes0)
			Sleep(1);   // the token bucket (rate per second) refills with time; nothing to do this round
	}
	char b[256];
	if(out->settled)
		_snprintf_s(b, sizeof(b), _TRUNCATE, "settled after %d calls in %.1f ms", out->calls, out->ms);
	else
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%s after %d calls in %.1f ms: %u tiles pending, bake-ahead %s, %u visible land tiles without a current bake",
		            out->stopped ? "STOPPED (out of memory at radius 0)" : "NOT settled", out->calls, out->ms, out->pending,
		            out->aheadDone ? "done" : "not done", out->visibleUnbaked);
	out->what = b;
	RNote("residency: settle: %s", b);
	return c5r::Status::Ok();
}

void ResidencySource::release()
{
	if(m_pipe)
		m_pipe->prepStop();   // ticket 0712: the worker reads the scene's grid, which may go before the baker does
	if(m_ok || m_baker)
		m_res.release();
	m_decalBaker.reset();
	m_nearBaker.reset();
	m_pipe = nullptr;
	m_haveLastCam = false;
	m_fitGapOpen = false;   // ticket 0703
	m_pendingGens.clear();
	m_heldSet.reset();   // ticket 0642: the loader ends with the pipeline's decal pass (m_baker)
	m_heldChanges.clear();
	m_baker.reset();
	m_standin = nullptr;
	m_ok = false;
	m_field = false;
	m_dev = nullptr;
}

void ResidencySource::statsLine(char* out, size_t n)
{
	if(!m_ok)
	{
		_snprintf_s(out, n, _TRUNCATE, "residency=off");
		return;
	}
	char line[1200];
	m_res.statsLine(line, sizeof(line));
	const c5r::bake::ResidencyStats& s = m_res.stats();
	_snprintf_s(out, n, _TRUNCATE,
	            "residency %s bakes_sec=%lld feed_plots=%lld feed_passed=%lld feed_filtered=%lld fields_refused=%lld standin_live_mb=%.1f "
	            "decal_gen=%u decal_sets=%lld decal_sets_refused=%lld decal_changes=%lld decal_raises=%lld decal_bakes=%lld "
	            "decal_bakes_failed=%lld decal_changes_skipped=%lld decal_area_exe=%lld decal_area_raises=%lld art_fills=%lld "
	            "art_plot_changes=%lld art_ignored=%lld style_raises=%lld",
	            line, (long long)(s.bakes - m_lastBakes), (long long)plotsChanged, (long long)plotsPassed, (long long)plotsFiltered,
	            (long long)fieldsRefused, m_standin ? m_standin->liveBytes() / 1048576.0 : 0.0, m_decals ? m_decals->generation : 0,
	            (long long)m_dc.sets, (long long)m_dc.setsRefused, (long long)m_dc.changes, (long long)m_dc.raises,
	            (long long)m_dc.bakesWithDecals, (long long)m_dc.bakesFailed, (long long)m_dc.changesSkipped,
	            (long long)m_dc.bakesAreaFromExe, (long long)m_dc.areaRaises, (long long)artFills, (long long)artPlotChanges,
	            (long long)artIgnored, (long long)styleRaises);
	// issue 0371: the camera frames and the near bakes are on the scene's "near 1s:" line, not here: this line goes into
	// the terrain line, which the log cuts at about 1,550 characters (three more fields cut its "| tot_drawn" tail)
	m_lastBakes = s.bakes;
}

// ---- issue 4105: decal sets, their names, their changes ----------------------------------------------------------------

bool ParseFarmTypes(const std::string& x, std::map<std::string, std::string>& farm, std::string& why)
{
	farm.clear();
	size_t i = 0;
	const size_t n = x.size();
	int depth = 0;
	std::string category;
	int child = -1;
	std::vector<std::string> open;   // element names, for the closing tags
	bool sawRoot = false;
	auto skipWs = [&]() {
		while(i < n && isspace((unsigned char)x[i]))
			++i;
	};
	auto decode = [](const std::string& v) {
		std::string o;
		for(size_t k = 0; k < v.size(); ++k)
		{
			if(v[k] == '&')
			{
				static const char* ents[][2] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
				bool hit = false;
				for(auto& e : ents)
					if(!v.compare(k, strlen(e[0]), e[0]))
					{
						o += e[1];
						k += strlen(e[0]) - 1;
						hit = true;
						break;
					}
				if(hit)
					continue;
			}
			o += v[k];
		}
		return o;
	};
	while(i < n)
	{
		const size_t lt = x.find('<', i);
		if(lt == std::string::npos)
			break;
		i = lt + 1;
		if(!x.compare(lt, 4, "<!--"))
		{
			const size_t e = x.find("-->", lt + 4);
			if(e == std::string::npos)
				return why = "a comment is not closed", false;
			i = e + 3;
			continue;
		}
		if(i < n && (x[i] == '?' || x[i] == '!'))
		{
			const size_t e = x.find('>', i);
			if(e == std::string::npos)
				return why = "a declaration is not closed", false;
			i = e + 1;
			continue;
		}
		if(i < n && x[i] == '/')
		{   // closing tag
			const size_t e = x.find('>', i);
			if(e == std::string::npos)
				return why = "a closing tag is not closed", false;
			std::string name = x.substr(i + 1, e - i - 1);
			while(!name.empty() && isspace((unsigned char)name.back()))
				name.pop_back();
			if(open.empty() || open.back() != name)
				return why = "</" + name + "> closes " + (open.empty() ? std::string("nothing") : "<" + open.back() + ">"), false;
			open.pop_back();
			depth--;
			i = e + 1;
			continue;
		}
		// an element: name, attributes, then '>' or '/>'
		size_t s = i;
		while(i < n && !isspace((unsigned char)x[i]) && x[i] != '>' && x[i] != '/')
			++i;
		const std::string name = x.substr(s, i - s);
		if(name.empty())
			return why = "an element without a name", false;
		if(depth == 0)
		{
			if(sawRoot)
				return why = "a second root element <" + name + ">", false;
			sawRoot = true;
		}
		else if(depth == 1)
		{
			category = Lower(name);
			child = -1;
		}
		else if(depth == 2)
			child++;
		bool selfClosed = false;
		for(;;)
		{
			skipWs();
			if(i >= n)
				return why = "<" + name + "> is not closed", false;
			if(x[i] == '>')
			{
				++i;
				break;
			}
			if(x[i] == '/' && i + 1 < n && x[i + 1] == '>')
			{
				i += 2;
				selfClosed = true;
				break;
			}
			s = i;
			while(i < n && x[i] != '=' && !isspace((unsigned char)x[i]) && x[i] != '>')
				++i;
			const std::string attr = x.substr(s, i - s);
			skipWs();
			if(i >= n || x[i] != '=')
				return why = "attribute '" + attr + "' of <" + name + "> has no value", false;
			++i;
			skipWs();
			if(i >= n || (x[i] != '"' && x[i] != '\''))
				return why = "attribute '" + attr + "' of <" + name + "> is not quoted", false;
			const char q = x[i++];
			const size_t e = x.find(q, i);
			if(e == std::string::npos)
				return why = "attribute '" + attr + "' of <" + name + "> is not closed", false;
			if(depth == 2)
				farm[category + "/" + std::to_string(child) + "/" + Lower(attr)] = decode(x.substr(i, e - i));
			i = e + 1;
		}
		if(!selfClosed)
		{
			open.push_back(name);
			depth++;
		}
	}
	if(!sawRoot)
		return why = "no root element", false;
	if(!open.empty())
		return why = "<" + open.back() + "> is not closed", false;
	return true;
}

bool ResolveDecalName(const std::string& name, const std::map<std::string, std::string>& farm, std::string& out, std::string& why)
{
	static const char kRef[] = "@farm_types.xml/";
	if(name.empty() || name[0] != '@')
	{
		out = name;
		return true;
	}
	if(Lower(name.substr(0, sizeof(kRef) - 1)) != kRef)
		return why = "'" + name + "' is a reference into another file than farm_types.xml", false;
	const std::string rest = name.substr(sizeof(kRef) - 1);   // <category>/<child>/<attribute>
	const size_t a = rest.find('/'), b = a == std::string::npos ? a : rest.find('/', a + 1);
	if(a == std::string::npos || b == std::string::npos || a == 0 || b == a + 1 || b + 1 >= rest.size() ||
	   rest.find('/', b + 1) != std::string::npos)
		return why = "'" + name + "' is not @farm_types.xml/<category>/<child>/<attribute>", false;
	const std::string idx = rest.substr(a + 1, b - a - 1);
	for(char c : idx)
		if(c < '0' || c > '9')
			return why = "'" + name + "': child index '" + idx + "' is not a number", false;
	const auto it = farm.find(Lower(rest.substr(0, a)) + "/" + std::to_string(atoi(idx.c_str())) + "/" + Lower(rest.substr(b + 1)));
	if(it == farm.end())
		return why = "'" + name + "' names no attribute of the farm_types.xml the game reads", false;
	out = BareName(it->second);
	if(out.empty())
		return why = "'" + name + "' is an empty attribute in farm_types.xml", false;
	return true;
}

// ---- issue 0348: the activated mods (Civ5ModsDatabase.db) --------------------------------------------------------------
// A reader of the SQLite 3 file format (https://www.sqlite.org/fileformat2.html, sections 1.3, 1.6, 2.1, 2.3) written from
// that description: the header, table b-tree pages (interior 0x05, leaf 0x0D), cell payloads with their overflow chains,
// and the record format. Nothing else (no index b-trees, no WITHOUT ROWID tables, no journal or WAL). Every offset is
// checked against the file's bytes; a malformed file is refused, never read past its end.
namespace {
struct SqlValue
{
	int kind = 0;   // 0 NULL, 1 integer, 2 real, 3 text, 4 blob
	int64_t i = 0;
	double r = 0;
	std::string s;
};

class SqlFile
{
public:
	bool open(const std::vector<uint8_t>& b, std::string& why)
	{
		m_b = &b;
		static const char kMagic[] = "SQLite format 3";
		if(b.size() < 100 || memcmp(b.data(), kMagic, 16) != 0)
			return why = "not an SQLite 3 file", false;
		uint32_t ps = Be(16, 2);
		if(ps == 1)
			ps = 65536;
		if(ps < 512 || ps > 65536 || (ps & (ps - 1)))
			return why = "page size " + std::to_string(ps) + " is not valid", false;
		const uint32_t reserved = b[20];
		if(ps - reserved < 480)
			return why = "usable page size below 480", false;
		const uint32_t enc = Be(56, 4);
		if(enc != 0 && enc != 1)
			return why = "text encoding " + std::to_string(enc) + " (only UTF-8 is read)", false;
		if(b[18] == 2 || b[19] == 2)
			return why = "a WAL-mode file (its content may be in the -wal file)", false;
		m_page = ps;
		m_usable = ps - reserved;
		m_pages = (uint32_t)(b.size() / ps);
		return true;
	}

	// Every row of the table b-tree rooted at `root`: cb(rowid, values). False (why) for a malformed tree.
	template<class F> bool scan(uint32_t root, F&& cb, std::string& why)
	{
		m_visits = 0;
		return page(root, 0, cb, why);
	}

private:
	const std::vector<uint8_t>* m_b = nullptr;
	uint32_t m_page = 0, m_usable = 0, m_pages = 0;
	size_t m_visits = 0;

	uint32_t Be(size_t at, int n) const
	{
		uint32_t v = 0;
		for(int k = 0; k < n; ++k)
			v = (v << 8) | (*m_b)[at + k];
		return v;
	}
	// a varint at `at` below `end`: false when it runs past `end`
	bool Varint(size_t& at, size_t end, uint64_t& v) const
	{
		v = 0;
		for(int k = 0; k < 9; ++k)
		{
			if(at >= end)
				return false;
			const uint8_t c = (*m_b)[at++];
			if(k == 8)
				return v = (v << 8) | c, true;
			v = (v << 7) | (c & 0x7F);
			if(!(c & 0x80))
				return true;
		}
		return true;
	}

	template<class F> bool page(uint32_t pg, int depth, F& cb, std::string& why)
	{
		if(pg < 1 || pg > m_pages)
			return why = "page " + std::to_string(pg) + " is outside the file", false;
		if(depth > 40 || ++m_visits > m_pages)
			return why = "the b-tree loops or is too deep", false;
		const size_t base = (size_t)(pg - 1) * m_page, end = base + m_usable;
		const size_t h = base + (pg == 1 ? 100 : 0);
		if(h + 12 > end)
			return why = "page " + std::to_string(pg) + " is too short", false;
		const uint8_t type = (*m_b)[h];
		if(type != 0x05 && type != 0x0D)
			return why = "page " + std::to_string(pg) + " is not a table b-tree page (type " + std::to_string(type) + ")", false;
		const uint32_t cells = Be(h + 3, 2);
		const size_t ptrs = h + (type == 0x05 ? 12 : 8);
		if(ptrs + 2 * (size_t)cells > end)
			return why = "page " + std::to_string(pg) + ": cell pointers past the page", false;
		for(uint32_t c = 0; c < cells; ++c)
		{
			const size_t at0 = base + Be(ptrs + 2 * c, 2);
			if(at0 < ptrs + 2 * (size_t)cells || at0 >= end)
				return why = "page " + std::to_string(pg) + ": a cell outside the page", false;
			size_t at = at0;
			if(type == 0x05)
			{
				if(at + 4 > end)
					return why = "page " + std::to_string(pg) + ": an interior cell past the page", false;
				if(!page(Be(at, 4), depth + 1, cb, why))
					return false;
				continue;
			}
			uint64_t payload = 0, rowid = 0;
			if(!Varint(at, end, payload) || !Varint(at, end, rowid))
				return why = "page " + std::to_string(pg) + ": a cell header past the page", false;
			std::vector<uint8_t> rec;
			if(!Payload(pg, at, end, payload, rec, why))
				return false;
			std::vector<SqlValue> vals;
			if(!Record(rec, vals))
				return why = "page " + std::to_string(pg) + ": a record that does not decode", false;
			cb((int64_t)rowid, vals);
		}
		return type == 0x0D || page(Be(h + 8, 4), depth + 1, cb, why);
	}

	bool Payload(uint32_t pg, size_t at, size_t end, uint64_t p, std::vector<uint8_t>& out, std::string& why) const
	{
		if(p > (64u << 20))
			return why = "page " + std::to_string(pg) + ": a payload of " + std::to_string(p) + " bytes", false;
		const uint64_t u = m_usable, x = u - 35, m = ((u - 12) * 32 / 255) - 23;
		uint64_t local = p;
		if(p > x)
		{
			const uint64_t k = m + ((p - m) % (u - 4));
			local = k <= x ? k : m;
		}
		if(at + local > end || (local < p && at + local + 4 > end))
			return why = "page " + std::to_string(pg) + ": a payload past the page", false;
		out.assign(m_b->begin() + at, m_b->begin() + at + (size_t)local);
		if(local == p)
			return true;
		uint32_t next = Be(at + (size_t)local, 4);
		size_t hops = 0;
		while(out.size() < p)
		{
			if(next < 1 || next > m_pages || ++hops > m_pages)
				return why = "page " + std::to_string(pg) + ": a broken overflow chain", false;
			const size_t ob = (size_t)(next - 1) * m_page;
			const size_t n = (size_t)std::min<uint64_t>(p - out.size(), u - 4);
			out.insert(out.end(), m_b->begin() + ob + 4, m_b->begin() + ob + 4 + n);
			next = Be(ob, 4);
		}
		return true;
	}

	bool Record(const std::vector<uint8_t>& r, std::vector<SqlValue>& vals) const
	{
		// varints over the record's own bytes
		auto vi = [&](size_t& at, size_t end, uint64_t& v) {
			v = 0;
			for(int k = 0; k < 9; ++k)
			{
				if(at >= end)
					return false;
				const uint8_t c = r[at++];
				if(k == 8)
					return v = (v << 8) | c, true;
				v = (v << 7) | (c & 0x7F);
				if(!(c & 0x80))
					return true;
			}
			return true;
		};
		size_t at = 0;
		uint64_t hsz = 0;
		if(!vi(at, r.size(), hsz) || hsz > r.size() || hsz < at)
			return false;
		size_t body = (size_t)hsz;
		while(at < (size_t)hsz)
		{
			uint64_t t = 0;
			if(!vi(at, (size_t)hsz, t))
				return false;
			SqlValue v;
			static const int kIntLen[] = {0, 1, 2, 3, 4, 6, 8};
			if(t == 0)
				v.kind = 0;
			else if(t <= 6)
			{
				const int n = kIntLen[t];
				if(body + n > r.size())
					return false;
				int64_t x = (int8_t)r[body];   // sign from the first byte
				for(int k = 1; k < n; ++k)
					x = (int64_t)((uint64_t)x << 8) | r[body + k];
				v.kind = 1, v.i = x, body += n;
			}
			else if(t == 7)
			{
				if(body + 8 > r.size())
					return false;
				uint64_t bits = 0;
				for(int k = 0; k < 8; ++k)
					bits = (bits << 8) | r[body + k];
				memcpy(&v.r, &bits, 8);
				v.kind = 2, body += 8;
			}
			else if(t == 8 || t == 9)
				v.kind = 1, v.i = (int64_t)(t - 8);
			else if(t < 12)
				return false;
			else
			{
				const uint64_t n = (t - 12) / 2;
				if(n > r.size() - body)
					return false;
				v.kind = (t & 1) ? 3 : 4;
				v.s.assign((const char*)r.data() + body, (size_t)n);
				body += (size_t)n;
			}
			vals.push_back(std::move(v));
		}
		return true;
	}
};

// the column names of a CREATE TABLE statement, in order (lower case, quotes removed); false for WITHOUT ROWID or no list
bool SqlColumns(const std::string& sql, std::vector<std::string>& cols)
{
	const size_t a = sql.find('('), b = sql.rfind(')');
	if(a == std::string::npos || b == std::string::npos || b < a)
		return false;
	if(Lower(sql.substr(b)).find("without") != std::string::npos)
		return false;
	std::vector<std::string> parts;
	std::string cur;
	int depth = 0;
	char q = 0;
	for(size_t i = a + 1; i < b; ++i)
	{
		const char c = sql[i];
		if(q)
		{
			if(c == q)
				q = 0;
		}
		else if(c == '\'' || c == '"' || c == '`')
			q = c;
		else if(c == '[')
			q = ']';
		else if(c == '(')
			++depth;
		else if(c == ')')
			--depth;
		else if(c == ',' && depth == 0)
		{
			parts.push_back(cur);
			cur.clear();
			continue;
		}
		cur += c;
	}
	parts.push_back(cur);
	for(const std::string& p0 : parts)
	{
		size_t s = p0.find_first_not_of(" \t\r\n");
		if(s == std::string::npos)
			continue;
		std::string name;
		const char c = p0[s];
		if(c == '\'' || c == '"' || c == '`' || c == '[')
		{
			const size_t e = p0.find(c == '[' ? ']' : c, s + 1);
			if(e == std::string::npos)
				return false;
			name = p0.substr(s + 1, e - s - 1);
		}
		else
		{
			const size_t e = p0.find_first_of(" \t\r\n", s);
			name = p0.substr(s, e == std::string::npos ? std::string::npos : e - s);
			const std::string k = Lower(name);
			if(k == "constraint" || k == "primary" || k == "foreign" || k == "unique" || k == "check")
				continue;   // a table constraint
		}
		cols.push_back(Lower(name));
	}
	return !cols.empty();
}

std::string SqlText(const SqlValue& v)
{
	if(v.kind == 1)
		return std::to_string(v.i);
	if(v.kind == 2)
		return std::to_string((long long)v.r);
	return v.kind == 3 ? v.s : std::string();
}

bool SqlTruth(const SqlValue& v)
{
	if(v.kind == 1)
		return v.i != 0;
	if(v.kind == 2)
		return v.r != 0;
	if(v.kind == 3)
		return atoi(v.s.c_str()) != 0 || Lower(v.s) == "true";
	return false;
}

std::wstring Widen(const std::string& s)
{
	if(s.empty())
		return std::wstring();
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n > 0 ? n : 0, L'\0');
	if(n > 0)
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

std::string Utf8(const std::wstring& w)
{
	if(w.empty())
		return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n : 0, ' ');
	if(n > 0)
		WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

std::wstring LowerW(std::wstring s)
{
	for(wchar_t& c : s)
		c = (wchar_t)towlower(c);
	return s;
}

bool IsDir(const std::wstring& p)
{
	const DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

const wchar_t kModsTail[] = L"\\My Games\\Sid Meier's Civilization 5\\MODS";

// every folder of root, sorted by name (issue 4105's rule)
std::vector<std::wstring> EveryFolder(const std::wstring& root)
{
	std::vector<std::wstring> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return out;
	do
	{
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
			out.push_back(root + L"\\" + fd.cFileName);
	} while(FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end());
	return out;
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out, std::string& why)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(h == INVALID_HANDLE_VALUE)
		return why = "cannot be opened (error " + std::to_string(GetLastError()) + ")", false;
	LARGE_INTEGER sz = {};
	bool ok = GetFileSizeEx(h, &sz) != 0 && sz.QuadPart >= 0 && sz.QuadPart <= (64LL << 20);
	if(!ok)
		why = "its size cannot be read or is above 64 MB";
	else
	{
		out.resize((size_t)sz.QuadPart);
		DWORD got = 0;
		ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
		if(!ok)
			why = "cannot be read";
	}
	CloseHandle(h);
	return ok;
}

bool NonEmptyFile(const std::wstring& p)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	return GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow);
}
}  // namespace

bool ReadActiveModFolders(const std::vector<uint8_t>& db, const std::wstring& modsRoot, ActiveModFolders& out, std::string& why)
{
	out = ActiveModFolders();
	SqlFile f;
	if(!f.open(db, why))
		return false;
	// sqlite_master (page 1): type, name, tbl_name, rootpage, sql
	struct Table
	{
		uint32_t root = 0;
		std::vector<std::string> cols;
	};
	std::map<std::string, Table> tables;
	std::string bad;
	if(!f.scan(1, [&](int64_t, std::vector<SqlValue>& v) {
		   if(v.size() < 5 || SqlText(v[0]) != "table")
			   return;
		   Table t;
		   t.root = v[3].kind == 1 ? (uint32_t)v[3].i : 0;
		   const std::string name = Lower(SqlText(v[1]));
		   if(!SqlColumns(SqlText(v[4]), t.cols) && (name == "mods" || name == "modfiles"))
			   bad = "table " + name + ": its CREATE TABLE is not read (WITHOUT ROWID or no column list)";
		   tables[name] = t;
	   }, why))
		return why = "the schema: " + why, false;
	if(!bad.empty())
		return why = bad, false;
	auto col = [](const Table& t, const char* n) {
		const auto it = std::find(t.cols.begin(), t.cols.end(), n);
		return it == t.cols.end() ? -1 : (int)(it - t.cols.begin());
	};
	const auto mt = tables.find("mods"), ft = tables.find("modfiles");
	if(mt == tables.end() || ft == tables.end())
		return why = "no Mods or ModFiles table", false;
	const int mId = col(mt->second, "modid"), mVer = col(mt->second, "version"), mAct = col(mt->second, "activated");
	const int fId = col(ft->second, "modid"), fVer = col(ft->second, "version"), fPath = col(ft->second, "path"),
	          fEval = col(ft->second, "evaluatedpath");
	if(mId < 0 || mVer < 0 || mAct < 0 || fId < 0 || fVer < 0 || fPath < 0 || fEval < 0)
		return why = "Mods(ModID, Version, Activated) or ModFiles(ModID, Version, Path, EvaluatedPath) lacks a column", false;
	auto at = [](const std::vector<SqlValue>& v, int i) { return i < (int)v.size() ? v[i] : SqlValue(); };
	auto key = [](const SqlValue& id, const SqlValue& ver) { return Lower(SqlText(id)) + "|" + SqlText(ver); };
	std::map<std::string, std::wstring> active;   // key -> its folder (empty: none found yet)
	if(!f.scan(mt->second.root, [&](int64_t, std::vector<SqlValue>& v) {
		   if(SqlTruth(at(v, mAct)))
			   active[key(at(v, mId), at(v, mVer))];
	   }, why))
		return why = "table Mods: " + why, false;
	if(!f.scan(ft->second.root, [&](int64_t, std::vector<SqlValue>& v) {
		   const auto it = active.find(key(at(v, fId), at(v, fVer)));
		   if(it == active.end() || !it->second.empty())
			   return;
		   std::wstring path = Widen(SqlText(at(v, fPath))), eval = Widen(SqlText(at(v, fEval)));
		   std::replace(path.begin(), path.end(), L'/', L'\\');
		   std::replace(eval.begin(), eval.end(), L'/', L'\\');
		   if(path.empty() || eval.size() <= path.size() + 1 || LowerW(eval.substr(eval.size() - path.size())) != LowerW(path) ||
		      eval[eval.size() - path.size() - 1] != L'\\')
			   return;
		   const std::wstring folder = eval.substr(0, eval.size() - path.size() - 1);
		   // re-rooted under modsRoot: the part after "...\Sid Meier's Civilization 5\MODS\", or after modsRoot itself
		   const std::wstring lf = LowerW(folder), tail = LowerW(std::wstring(kModsTail) + L"\\"), lr = LowerW(modsRoot + L"\\");
		   size_t cut = std::wstring::npos;
		   if(lf.compare(0, lr.size(), lr) == 0)
			   cut = lr.size();
		   else
		   {
			   const size_t k = lf.rfind(tail);
			   if(k != std::wstring::npos)
				   cut = k + tail.size();
		   }
		   if(cut == std::wstring::npos || cut >= folder.size())
			   return;
		   it->second = modsRoot + L"\\" + folder.substr(cut);
	   }, why))
		return why = "table ModFiles: " + why, false;
	out.known = true;
	out.activated = (int)active.size();
	std::vector<std::wstring> seen;
	for(const auto& a : active)
	{
		if(a.second.empty() || !IsDir(a.second))
		{
			++out.withoutFolder;
			continue;
		}
		const std::wstring l = LowerW(a.second);
		if(std::find(seen.begin(), seen.end(), l) != seen.end())
			continue;
		seen.push_back(l);
		out.folders.push_back(a.second);
	}
	std::sort(out.folders.begin(), out.folders.end());
	return true;
}

std::vector<std::wstring> ModFolders(std::wstring* modsRoot, ActiveModFolders* info)
{
	std::vector<std::wstring> out;
	std::wstring docs;
	wchar_t env[MAX_PATH * 2];
	const DWORD n = GetEnvironmentVariableW(L"C5_DOCUMENTS", env, MAX_PATH * 2);
	if(n > 0 && n < MAX_PATH * 2)
		docs.assign(env, n);
	else
	{
		typedef HRESULT(WINAPI * PFN)(HWND, int, HANDLE, DWORD, LPWSTR);
		HMODULE sh = LoadLibraryW(L"shell32.dll");
		PFN f = sh ? (PFN)GetProcAddress(sh, "SHGetFolderPathW") : nullptr;
		wchar_t w[MAX_PATH];
		if(f && f(nullptr, 0x0005 /* CSIDL_PERSONAL */, nullptr, 0, w) == S_OK)
			docs = w;
	}
	ActiveModFolders am;
	if(info)
		*info = am;
	if(docs.empty())
	{
		RNote("decals: mods: no Documents folder: the install's own files only");
		return out;
	}
	const std::wstring civ = docs + L"\\My Games\\Sid Meier's Civilization 5";
	const std::wstring root = docs + kModsTail;
	if(modsRoot)
		*modsRoot = root;
	if(!IsDir(root))
	{
		RNote("decals: mods: no MODS folder at '%s': the install's own files only", Utf8(root).c_str());
		return out;
	}
	// issue 0348: the activated mods' folders from the game's record of them; every folder when it cannot be read
	const std::wstring db = civ + L"\\cache\\Civ5ModsDatabase.db";
	const int64_t q0 = Qpc();
	std::vector<uint8_t> bytes;
	std::string why;
	bool ok = false;
	if(NonEmptyFile(db + L"-journal") || NonEmptyFile(db + L"-wal"))
		why = "a -journal or -wal file beside it: a write of the game is in progress or was cut off";
	else if(ReadWholeFile(db, bytes, why))
		ok = ReadActiveModFolders(bytes, root, am, why);
	const double ms = QpcToMs(Qpc() - q0);
	if(ok)
	{
		out = am.folders;
		std::wstring names;
		for(const std::wstring& f : out)
			names += (names.empty() ? L"" : L"; ") + f.substr(root.size() + 1);
		RNote("decals: mods: %d activated in '%s' (read in %.1f ms), %d without a folder under MODS; %zu folders above the "
		      "install: %s", am.activated, Utf8(db).c_str(), ms, am.withoutFolder, out.size(), names.empty() ? "(none)" : Utf8(names).c_str());
	}
	else
	{
		out = EveryFolder(root);
		am = ActiveModFolders();
		am.folders = out;
		RNote("decals: mods: the activated mods are not known ('%s': %s): every folder of MODS counts, %zu folders (the rule "
		      "before issue 0348)", Utf8(db).c_str(), why.c_str(), out.size());
	}
	if(info)
		*info = am;
	return out;
}

bool ResidencySource::loadFarmTypes(std::string& why)
{
	if(m_farmState == 1)
		return true;
	if(m_farmState == -1)
		return why = m_farmWhy, false;
	const c5r::gamefs::GameFiles* fs = m_decalFsOpen ? &m_decalFs : m_cfg.pipeline && m_ok ? &m_fs : nullptr;
	std::vector<uint8_t> bytes;
	std::string origin;
	c5r::Status st = fs ? fs->read("farm_types.xml", bytes, &origin) : c5r::Status::fail(c5r::Err::NotFound, "no install view");
	if(st.ok() && !ParseFarmTypes(std::string(bytes.begin(), bytes.end()), m_farm, m_farmWhy))
		st = c5r::Status::fail(c5r::Err::BadLayout, "farm_types.xml (" + origin + ") does not parse: " + m_farmWhy);
	if(!st.ok())
	{
		m_farmState = -1;
		m_farmWhy = "farm_types.xml cannot be read from the install or the mods' folders: " + st.str();
		return why = m_farmWhy, false;
	}
	m_farmState = 1;
	RNote("decals: farm_types.xml from %s: %zu attributes", origin.c_str(), m_farm.size());
	return true;
}

bool ResidencySource::setDecals(const std::shared_ptr<const c5r::decal::DecalSet>& raw, std::string& why)
{
	if(!raw)
		return true;
	const int64_t q0 = Qpc();
	std::shared_ptr<const c5r::decal::DecalSet> use = raw;
	bool refs = false;
	for(const std::string& t : raw->textures)
		refs = refs || (!t.empty() && t[0] == '@');
	std::vector<std::string> names = raw->textures;
	int64_t resolved = 0;
	for(std::string& t : names)
	{
		if(t.empty() || t[0] != '@')
			continue;
		std::string out;
		if(!loadFarmTypes(why) || !ResolveDecalName(t, m_farm, out, why))
		{
			m_dc.setsRefused++;
			why = "decal set generation " + std::to_string(raw->generation) + ": " + why;
			return false;
		}
		t = out;
		resolved++;
	}
	// every name against the view the decal pass reads (stand-in without an install: not checked)
	const c5r::gamefs::GameFiles* fs = m_decalFsOpen ? &m_decalFs : m_cfg.pipeline && m_ok ? &m_fs : nullptr;
	if(fs)
		for(size_t k = 0; k < names.size(); ++k)
		{
			const std::string bare = Lower(BareName(names[k]));
			if(bare.empty() || !fs->exists(bare))
			{
				m_dc.setsRefused++;
				why = "decal set generation " + std::to_string(raw->generation) + ": texture '" + names[k] + "'" +
				      (names[k] != raw->textures[k] ? " (from '" + raw->textures[k] + "')" : std::string()) +
				      " is not in the install or the mods' folders";
				return false;
			}
		}
	if(refs)
	{
		std::shared_ptr<c5r::decal::DecalSet> copy = std::make_shared<c5r::decal::DecalSet>(*raw);
		copy->textures.swap(names);
		use = copy;
	}
	// ticket 0642: every name of the set to the decal pass's loader; the set is used once they are all in (mode 2 holds
	// it until a later poll, mode 1 waits here), so that no bake reads a texture from the disk
	if(c5r::decal::DecalPass* dp = loaderPass())
	{
		dp->request(use->textures);
		if(!dp->ready(use->textures))
		{
			if(m_decalLoadMode == 1)
			{
				const int64_t w0 = Qpc();
				while(!dp->ready(use->textures) && QpcToMs(Qpc() - w0) < 60000.0)
					Sleep(1);
				const double ms = QpcToMs(Qpc() - w0);
				m_dc.setsWaited++;
				m_dc.waitedMsMax = (std::max)(m_dc.waitedMsMax, ms);
				if(!dp->ready(use->textures))
					RNote("LOUD: decals: set generation %u: its textures were not all loaded after %.0f ms; used now, the bakes read "
					      "the rest themselves", use->generation, ms);
			}
			else
			{
				if(m_heldSet)
					m_dc.setsHeldReplaced++;
				else
					m_heldSince = Qpc();
				m_dc.setsHeld++;
				m_heldSet = use;
				m_heldResolved = resolved;
				RNote("decals: set generation %u (load %u) held: %zu of its %zu names are still loading off the render thread "
				      "(ticket 0642); the set in force (generation %u) and every tile's bake stay until they are in",
				      use->generation, use->loadSeq, dp->loaderPending(), use->textures.size(), m_decals ? m_decals->generation : 0);
				return true;
			}
		}
	}
	else if(m_heldSet)
	{
		m_dc.setsHeldReplaced++;   // the loader is gone (cannot happen while m_ok): the newest set is used as before 0642
		m_heldSet.reset();
	}
	applyDecals(use, resolved, q0);
	return true;
}

// ticket 0642: a held set (setDecals, mode 2) is used once the loader has every name; the changes queued behind it follow
bool ResidencySource::pumpHeldDecals()
{
	if(!m_heldSet)
		return false;
	c5r::decal::DecalPass* dp = loaderPass();
	if(dp && !dp->ready(m_heldSet->textures))
		return false;
	const std::shared_ptr<const c5r::decal::DecalSet> use = m_heldSet;
	m_heldSet.reset();
	const double ms = QpcToMs(Qpc() - m_heldSince);
	m_dc.setsAppliedAfterHold++;
	m_dc.heldMsMax = (std::max)(m_dc.heldMsMax, ms);
	m_dc.heldMsSum += ms;
	if(dp)
	{
		const c5r::decal::DecalLoaderCounters& lc = dp->loaderCounters();
		RNote("decals: set generation %u used after %.1f ms held: its names are in (loader so far: %llu textures, %.1f MB, reads %.1f ms "
		      "in all, longest %.1f ms, %llu failed; draws that waited %llu, read themselves %llu)",
		      use->generation, ms, (unsigned long long)lc.adopted, lc.bytes / 1048576.0, lc.readMs, lc.readMsMax,
		      (unsigned long long)lc.failed, (unsigned long long)lc.waits, (unsigned long long)lc.syncLoads);
	}
	applyDecals(use, m_heldResolved, Qpc());
	std::vector<HeldChanges> queued;
	queued.swap(m_heldChanges);
	for(const HeldChanges& h : queued)
		decalChanges(h.ch, h.signalMs);
	return true;
}

void ResidencySource::applyDecals(const std::shared_ptr<const c5r::decal::DecalSet>& use, int64_t resolved, int64_t q0)
{
	if(m_decals && use->generation < m_decals->generation)
		m_decalGenAtField = 0;   // a new reader (a new EXE): its generations start again
	// 0365: the set the bake-ahead waited for; with nothing resident yet every bake reads it (or a newer one), so its change
	// re-bakes nothing (as a set before the field, 12.3)
	if(m_aheadHold)
	{
		m_dc.aheadHoldsEndedBySet++;
		if(m_ok && m_field && m_res.hasField() && m_res.stats().resident == 0)
			m_decalGenAtField = use->generation;
		char why[96];
		_snprintf_s(why, sizeof(why), _TRUNCATE, "decal set generation %u (load %u) arrived", use->generation, use->loadSeq);
		endAheadHold(why);
	}
	m_decals = use;
	m_decalsAppliedQpc = Qpc();
	m_dc.sets++;
	m_dc.namesResolved += resolved;
	RNote("decals: set generation %u (load %u) in use: %zu decals, %zu names (%lld farm references resolved), %.1f ms", use->generation,
	      use->loadSeq, use->decals.size(), use->textures.size(), (long long)resolved, QpcToMs(Qpc() - q0));
}

void ResidencySource::decalChanges(const std::vector<DecalChange>& ch, double signalMs)
{
	// ticket 0642: the changes of a held set's generations raise their tiles when the set is used (pumpHeldDecals), so
	// that their re-bakes read it (P13)
	if(m_heldSet)
	{
		m_dc.heldChangeBatches++;
		HeldChanges h;
		h.ch = ch;
		h.signalMs = signalMs;
		m_heldChanges.push_back(std::move(h));
		return;
	}
	if(!m_ok || !m_field || !m_res.hasField())
		return;
	const uint32_t tx = m_res.tilesX(), ty = m_res.tilesY();
	for(const DecalChange& c : ch)
	{
		if(!c.all && c.generation <= m_decalGenAtField)
		{
			m_dc.changesSkipped++;
			continue;
		}
		m_dc.changes++;
		int64_t raised = 0;
		PendingGen pg;
		pg.generation = c.generation;
		pg.signalMs = signalMs;
		pg.raisedMs = QpcToMs(Qpc());
		auto raise = [&](uint32_t x, uint32_t y) {
			const bool hadBake = m_res.tileHasBake(x, y);   // 0365: a re-bake (else the tile's first bake reads this set)
			if(!m_res.raiseTile(x, y, signalMs))
				return;
			raised++;
			if(m_res.tileClass(x, y) >= 2)   // land: the residency bakes it (water tiles are never baked)
			{
				pg.tiles.push_back(y * tx + x);
				if(hadBake)
					pg.rebake.push_back(y * tx + x);
			}
		};
		if(c.all)
		{
			m_dc.changesAll++;
			for(uint32_t y = 0; y < ty; ++y)
				for(uint32_t x = 0; x < tx; ++x)
					raise(x, y);
		}
		else
		{
			const std::vector<c5r::decal::TileOrigin> tiles = c5r::decal::tilesForRects(c.rects.data(), c.rects.size(), c.worldRect);
			for(const c5r::decal::TileOrigin& t : tiles)
				if(t.x >= 0 && t.y >= 0)
					raise((uint32_t)(t.x / c5r::decal::kTileSamples), (uint32_t)(t.y / c5r::decal::kTileSamples));
			// 4111: the tiles whose area state (the EXE's tile+0x300) changed; a tile raised twice is baked once
			const int64_t before = raised;
			for(const c5r::decal::TileOrigin& t : c.areaTiles)
				if(m_cfg.decalAreaFromExe && t.x >= 0 && t.y >= 0 && (uint32_t)t.x < tx && (uint32_t)t.y < ty)
					raise((uint32_t)t.x, (uint32_t)t.y);
			m_dc.areaRaises += raised - before;
		}
		m_dc.raises += raised;
		RNote("decals: generation %u (after %u): %zu changed rectangles%s raised %lld tiles (%zu land); %zu tiles with a changed area "
		      "state%s",
		      c.generation, c.previousGeneration, c.rects.size(), c.all ? " (queue overflow: every tile)" : "", (long long)raised,
		      pg.tiles.size(), c.areaTiles.size(), m_cfg.decalAreaFromExe ? " (raised)" : " (not used: decalAreaFromExe off)");
		if(!pg.tiles.empty())
		{
			std::sort(pg.tiles.begin(), pg.tiles.end());
			pg.total = pg.tiles.size();
			std::sort(pg.rebake.begin(), pg.rebake.end());
			pg.rebake.erase(std::unique(pg.rebake.begin(), pg.rebake.end()), pg.rebake.end());
			pg.rebakeTotal = pg.rebake.size();
			m_dc.rebakeRaises += (int64_t)pg.rebakeTotal;
			if(m_pendingGens.size() >= 64)
			{
				RNote("decals: generation %u: %zu of its %zu tiles not re-baked yet; no longer followed (64 generations pending)",
				      m_pendingGens.front().generation, m_pendingGens.front().tiles.size(), m_pendingGens.front().total);
				m_pendingGens.erase(m_pendingGens.begin());
			}
			m_pendingGens.push_back(std::move(pg));
		}
	}
}

// A tile was baked (with the set in force, never older than a raise's generation): every pending generation that raised
// it is one tile closer; the last tile of a generation gives its line (the D92 latency from the tap's publication).
void ResidencySource::decalTileBaked(uint32_t x, uint32_t y)
{
	if(m_pendingGens.empty())
		return;
	const uint32_t i = y * m_res.tilesX() + x;
	const double now = QpcToMs(Qpc());
	for(size_t k = 0; k < m_pendingGens.size();)
	{
		PendingGen& g = m_pendingGens[k];
		const auto it = std::lower_bound(g.tiles.begin(), g.tiles.end(), i);
		if(it != g.tiles.end() && *it == i)
			g.tiles.erase(it);
		// 0365: the tiles that had a bake at the raise: their own line when the last is re-baked (the stale-picture latency)
		const auto rt = std::lower_bound(g.rebake.begin(), g.rebake.end(), i);
		if(rt != g.rebake.end() && *rt == i)
		{
			g.rebake.erase(rt);
			if(g.rebake.empty())
				RNote("decals: generation %u: its %zu re-baked land tiles (a bake before the raise) done, the last %.1f ms after the "
				      "publication (%.1f ms after the raise)", g.generation, g.rebakeTotal, now - g.signalMs, now - g.raisedMs);
		}
		if(g.tiles.empty())
		{
			RNote("decals: generation %u: its %zu land tiles re-baked, the last %.1f ms after the publication (%.1f ms after the raise)",
			      g.generation, g.total, now - g.signalMs, now - g.raisedMs);
			m_pendingGens.erase(m_pendingGens.begin() + (ptrdiff_t)k);
		}
		else
			++k;
	}
}

}  // namespace srvrender
