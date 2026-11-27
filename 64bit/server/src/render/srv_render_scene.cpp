// srv_render_scene.cpp - the terrain scene (issue 3301). Origin: srv_render_scene.h (ported from
// shell/server/shell_server.cpp --scene terrain at commit b4f7748; that file is unchanged).
#include "srv_render_scene.h"
#include "srv_scene_sources.h"   // epic 0301, issue 3903: SettleResult
#include "srv_render_host.h"     // ticket 0674: render_fow_mask_window
#include "exe_camera.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"   // issue 4308: the ice's programs
#include "srv_options.h"                 // ticket 4938: SrvOptions() for the extras' effective state
#include "vp64_options.h"
#include "vp_pacing.h"                   // ticket 0688: the pass's stage marks (shell/common)
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

namespace srvrender {

namespace c5t = c5r::terrain;
namespace c5h = c5r::heightfield;

namespace {
// Ticket 5052 (T): the fog scene's plot table for the units' $(TERRAIN), beside each setPlotStates before beginFrame (the
// same width and height as the states; key = the fog scene's read count). No fog scene or no good read: null (every unit
// DIRT, counted plotNoTable). The units copy only with UnitsOptions::triggerTerrain on. unit-combat-server.md 18.25.3.
void HandPlotTable(srvunits::SceneUnits& units, const FowScene& fow)
{
	size_t n = 0;
	uint32_t key = 0;
	const SrvFeed::PlotRecord* p = fow.plotTable(n, key);
	units.setPlotTable(p, n, fow.tableWidth(), fow.tableHeight(), key);
}

enum { TS_NO_CAMERA, TS_OK, TS_STALE, TS_HIDDEN, TS_SRV_INCONSISTENT, TS_NO_FIELD, TS_COUNT };
const char* kTsNames[TS_COUNT] = {"NO_CAMERA", "OK", "STALE", "HIDDEN", "SRV_INCONSISTENT", "NO_FIELD"};
const char* kTapStateNames[] = {"NONE", "WRITING", "READY", "RELEASED", "FAILED", "OFF"};
// ackResult values (shell_shared.h VPShellTap: 0 ok, > 0 c5r::Err, -1 hash, -2 open, -3 stale)
enum { TAPACK_OK = 0, TAPACK_HASH = -1, TAPACK_OPEN = -2, TAPACK_STALE = -3 };
void RoadsLog(void*, const char* line) { RNote("%s", line); }   // ticket 4934: the road producer's lines

uint64_t Fnv1a64(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

bool ServerConsistent(const VPShellCamera& c, float& rot, float& trans, float& wrapErr)
{
	VPCamConsistency(c.camWorld, c.view[0], rot, trans);
	wrapErr = 0;
	for(uint32_t i = 1; i < c.numViews && i < 4; ++i)
		wrapErr = std::max(wrapErr, VPCamWrapError(c.view[0], c.view[i], c.wrapPeriod));
	bool finite = true;
	for(int k = 0; k < 16; ++k)
		if(!(fabsf(c.proj[k]) < 1e30f))
			finite = false;
	// issue 3015: the server's own check of every matrix it draws with
	const uint32_t nv = c.numViews >= 1 && c.numViews <= 4 ? c.numViews : 1;
	if(!VPCamAllFinite(c.camWorld, 16) || !VPCamAllFinite(&c.view[0][0], 16 * (int)nv))
		finite = false;
	return finite && c.numViews >= 1 && c.numViews <= 4 && rot <= VPCAM_TOL_ROT && trans <= VPCAM_TOL_TRANS && wrapErr <= VPCAM_TOL_WRAP;
}
ID3D11ShaderResourceView* OnePixel(ID3D11Device* dev, uint32_t rgba);   // issue 4308 (below)

// issue 3704: the tap's reader as the scene's overlay source
class TapOverlaySource : public OverlaySource
{
public:
	TapOverlaySource(VPShellHeader* hdr, ID3D11Device* dev) : m_r(hdr, dev) {}
	bool acquire(OverlayFrame& out) override { return m_r.acquire(out); }
	OverlayTapReader::State state() const override { return m_r.state(); }
	uint32_t failReason() const override { return m_r.failReason(); }
	std::string why() const override { return m_r.lastWhy(); }
	int64_t staleCount() const override { return m_r.stale; }
	int64_t staleLongestPresents() const override { return m_r.staleLongestPresents; }
	int64_t refusedCount() const override { return m_r.refused; }

private:
	OverlayTapReader m_r;
};

bool Finite16(const float* m)
{
	for(int i = 0; i < 16; ++i)
		if(!std::isfinite(m[i]))
			return false;
	return true;
}

// p moved by `shift` world units in x: P'(x) = P(x + shift), i.e. Translation(shift) * P (row vectors)
void MoveX(const float* p, double shift, float* out)
{
	memcpy(out, p, 16 * sizeof(float));
	for(int j = 0; j < 4; ++j)
		out[12 + j] = (float)(shift * p[j] + (double)p[12 + j]);
}
}  // namespace

std::unique_ptr<OverlaySource> MakeTapOverlaySource(VPShellHeader* hdr, ID3D11Device* dev)
{
	return std::unique_ptr<OverlaySource>(new TapOverlaySource(hdr, dev));
}

const char* OverlayStateName(OverlayTapReader::State s)
{
	switch(s)
	{
	case OverlayTapReader::State::Absent: return "ABSENT";
	case OverlayTapReader::State::Off: return "OFF";
	case OverlayTapReader::State::Waiting: return "WAITING";
	case OverlayTapReader::State::Active: return "ACTIVE";
	case OverlayTapReader::State::Failed: return "FAILED";
	}
	return "?";
}

const char* OverlayFailName(uint32_t reason)
{
	static const char* kNames[] = {"NONE", "PATTERN", "HANDLE", "DESC", "SHARED", "EXCEPTION", "VIEWS", "KILLED", "VERSION"};
	return reason < sizeof(kNames) / sizeof(kNames[0]) ? kNames[reason] : "?";
}

bool OverlayCopiesFromFrame(const OverlayFrame& f, float wrapPeriod, OverlayCopies& out, std::string& why)
{
	out = OverlayCopies();
	if(f.numViews == 0 || f.numViews > VPSHELL_OVERLAY_MAX_VIEWS)
	{
		why = "numViews " + std::to_string(f.numViews) + " outside 1.." + std::to_string(VPSHELL_OVERLAY_MAX_VIEWS);
		return false;
	}
	for(uint32_t v = 0; v < f.numViews; ++v)
		if(!Finite16(f.overlayProj[v]))
		{
			why = "the OverlayProj of view " + std::to_string(v) + " is not finite";
			return false;
		}
	const float* p0 = f.overlayProj[0];
	if(p0[0] == 0.0f || p0[5] == 0.0f)
	{
		why = "view 0's OverlayProj has a zero x or y scale";
		return false;
	}
	memcpy(out.proj[1], p0, sizeof(out.proj[1]));
	const bool wraps = std::isfinite(wrapPeriod) && wrapPeriod > 0;
	for(uint32_t v = 1; v < f.numViews; ++v)
	{
		const float* p = f.overlayProj[v];
		int k = 0;
		if(wraps)
		{
			// the same x / y mapping but for the x translation: the four xy members of the upper 2 x 2 and ty within 1e-4
			// relative. The z / w members are not compared: the EXE's wrap view differs there by up to 1.5e-5 (3701's live
			// read of 0xDA1690 + 0x40, 2026-11-14: [14] 0.4999847 against 0.5, [10] -0.49999997, [15] 0.99999994).
			bool same = true;
			const int xy[5] = {0, 1, 4, 5, 13};
			for(int i : xy)
				same = same && std::fabs(p[i] - p0[i]) <= 1e-4f * std::max(std::fabs(p0[i]), i == 13 ? 1.0f : std::fabs(p0[0]));
			const double d = ((double)p[12] - (double)p0[12]) / (double)p0[0];   // world units
			const double kk = std::floor(d / wrapPeriod + 0.5);
			if(same && (kk == 1.0 || kk == -1.0) && std::fabs(d - kk * wrapPeriod) <= 0.5)
				k = (int)kk;
		}
		if(k == 0 || out.fromExe[k + 1])
		{
			out.unmatchedViews++;
			continue;
		}
		memcpy(out.proj[k + 1], p, sizeof(out.proj[k + 1]));
		out.fromExe[k + 1] = true;
	}
	for(int k = -1; k <= 1; k += 2)
		if(!out.fromExe[k + 1])
			MoveX(p0, wraps ? (double)k * wrapPeriod : 0.0, out.proj[k + 1]);
	return true;
}

Scene::Scene() {}

Scene::~Scene()
{
	releaseDevice();
	detach();
	if(m_farmCutSets && CutOverlaySlot().active())
		CutOverlaySlot().set(nullptr);   // ticket 4853: this scene's footprints leave the process-wide cut overlay
	if(m_roadsCutSeen && CutOverlaySlot().roadsActive())
		CutOverlaySlot().setRoads(nullptr, nullptr);   // ticket 4934: the same for the roads' cut
}

bool Scene::attach(const std::wstring& section)
{
	detach();
	m_section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, section.c_str());
	if(!m_section)
	{
		RNote("LOUD: scene: CreateFileMapping('%ls') failed %lu", section.c_str(), GetLastError());
		return false;
	}
	m_hdr = (VPShellHeader*)MapViewOfFile(m_section, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if(!m_hdr)
	{
		RNote("LOUD: scene: MapViewOfFile('%ls') failed %lu", section.c_str(), GetLastError());
		CloseHandle(m_section);
		m_section = nullptr;
		return false;
	}
	m_water.attach(m_hdr);   // issue 3607
	m_sectionName = section;
	if(m_farm)
		m_farm->setPointSection(section);   // ticket 0657: <section>.Farm, the buildings' points for the EXE's placer
	if(m_unitsMode)   // epic 0040, issue 4753 (a writer made later attaches in unitsModeOn); writes only while on
		m_unitsMode->attach(VPSHELL_MODE(m_hdr));
	if(m_models)      // epic 0561, ticket 4875: the model bits' writer (a module made later attaches in configureModels)
		m_models->attachMode(VPSHELL_MODE(m_hdr));
	return true;
}

void Scene::detach()
{
	m_decalReader.reset();   // issue 4105
	decalCloseControl();
	m_decalPid = 0;
	m_water.attach(nullptr);
	if(m_unitsMode)   // issue 4753
		m_unitsMode->attach(nullptr);
	if(m_models)      // ticket 4875
		m_models->attachMode(nullptr);
	if(m_hdr)
		UnmapViewOfFile(m_hdr);
	m_hdr = nullptr;
	if(m_section)
		CloseHandle(m_section);
	m_section = nullptr;
}

c5r::Status Scene::init(ID3D11Device* dev, ID3D11DeviceContext* ctx, const SceneOptions& o)
{
	releaseDevice();
	m_opt = o;
	m_dev = dev;
	if(!m_jobs)   // ticket 0711 (J1): the frame-jobs pool, once per Scene (it lives across device rebuilds)
	{
		m_jobs.reset(new FrameJobs());
		FrameJobs::Options jo;
		jo.workers = o.frameJobs;
		jo.jitter = o.frameJobsJitter;
		std::string jwhy;
		if(!m_jobs->start(jo, jwhy))
			RNote("LOUD: %s (ticket 0711)", jwhy.c_str());
		if(o.frameJobsJitter)
			FrameJobsSetOwnerChecksFatal(true);   // R2: a TEST key of the jobs makes every owner violation fatal
		RNote("frame jobs: %d worker%s (render_frame_jobs, ticket 0711): the units' begin %s%s", m_jobs->workers(),
		      m_jobs->workers() == 1 ? "" : "s", m_jobs->workers() ? "forked after the camera is fixed, joined before the units' cast"
		                                                           : "runs inline at its join (no worker)",
		      o.frameJobsJitter ? "; LOUD: TEST KEY render_test_frame_jobs_jitter=1: 0-5 ms sleeps at each fork and job start, owner "
		                          "checks fatal" : "");
	}
	{   // ticket 0753 (J3): record ahead (SceneOptions::recordAhead), resolved on this device; the deferred context made here
		D3D11_FEATURE_DATA_THREADING th = {};
		m_raDriverLists = dev && SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_THREADING, &th, sizeof(th))) && th.DriverCommandLists;
		const int jobs = m_jobs ? m_jobs->workers() : 0;
		const bool want = o.recordAhead == 1 || (o.recordAhead < 0 && m_raDriverLists && jobs > 0);
		m_raOn = false;
		if(want && dev)
		{
			const HRESULT hr = dev->CreateDeferredContext(0, &m_raCtx);
			if(SUCCEEDED(hr) && m_raCtx)
				m_raOn = true;
			else
				RNote("LOUD: record ahead: CreateDeferredContext failed 0x%08X: the draw stage's end is drawn inline at its place "
				      "(ticket 0753)", (unsigned)hr);
		}
		RNote("record ahead: %s (render_record_ahead=%s, ticket 0753): driver command lists %s, frame-jobs workers %d%s%s",
		      !m_raOn ? "off"
		      : o.recordAheadEndOnly ? "on, the end only (TEST KEY render_test_record_ahead_end_only=1): the draw stage's end (cloud "
		                               "planes, particles, path) recorded while the units' job runs, executed at its place"
		                             : "on: the terrain draw, the arc to the trees, and the draw stage's end (cloud planes, particles, "
		                               "path) recorded while the units' job runs, each executed at its place",
		      o.recordAhead < 0 ? "auto" : o.recordAhead ? "1" : "0", m_raDriverLists ? "yes" : "no (the runtime replays a list's calls)",
		      jobs, o.recordAhead == 1 && !m_raDriverLists ? "; LOUD: on by the key without driver command lists" : "",
		      m_raOn && o.recordAheadAlways ? "; LOUD: TEST KEY render_test_record_ahead_always=1: recorded on every draw" : "");
	}
	if(m_units)   // epic 0375, issue 4454
		m_units->setDevice(dev);
	if(m_models)  // epic 0561, ticket 4875: the model renderer is made on this device at the module's next poll
		m_models->setDevice(dev);
	// epic 0040, issue 4753: the mode writer's options (its state is kept across devices)
	if(m_unitsMode)
		m_unitsMode->setOptions(o.unitsReadyHoldMs, o.unitsPositions == SceneOptions::kUnitsPositionsExe);
	m_r = new c5t::Renderer();
	const int64_t q0 = Qpc();
	// issue 0188: the install gives the terrain its distance-fog ramp (fogramp.dds as t2), issue 0189 its spec map
	// (specscreenmap.dds as t9 of pass 0). An install without either is LOUD and the terrain is drawn without distance fog
	// and without spec, as the residency goes on without an install (3308).
	// issue 0198: one index of the install for this init, read by the terrain (fog ramp, spec map, shadow ColorKey) and
	// the water module's waves, instead of one each (gamefs::GameFiles::open costs 1.3-1.5 s on an install,
	// terrain-surface.md 6); it lives until the end of this init only, as the modules' own did.
	c5r::gamefs::GameFiles files;
	double indexMs = 0;
	bool filesOk = false;
	c5r::Status st = c5r::Status::Ok();
	if(!o.install.empty())
	{
		const int64_t qi = Qpc();
		st = files.open(o.install);
		indexMs = QpcToMs(Qpc() - qi);
		filesOk = st.ok();
		if(st.ok())
			st = m_r->init(dev, o.exe, files);
		if(!st.ok())
			RNote("LOUD: terrain: no distance fog and no spec: the install '%ls' gave no fog ramp or spec map: %s", o.install.c_str(),
			      st.str().c_str());
	}
	if(o.install.empty() || !st.ok())
		st = m_r->init(dev, o.exe, L"");
	if(!st.ok())
	{
		RNote("LOUD: terrain: renderer init with '%ls' failed: %s", o.exe.c_str(), st.str().c_str());
		return st;
	}
	m_rInit = true;
	if(o.install.empty())   // a test or tool configuration: a plain note (the product always names its install)
		RNote("terrain: no install given: the terrain is drawn without distance fog and spec (issues 0188, 0189)");
	RNote("terrain scene: EXE '%ls' (shaders read in %.1f ms, the install's index %.1f ms of it, shared with the waves %d), colour %d, fallback %d, pass %d, tess %.2f, water %d, tile plots %d, tile d4 %d, distance fog %d, spec map %d",
	      o.exe.c_str(), QpcToMs(Qpc() - q0), indexMs, filesOk ? 1 : 0, (int)o.color, (int)o.fallback, o.pass, o.tess, (int)o.water, o.tilePlots, o.tileD4,
	      m_r->fogRamp() ? 1 : 0, m_r->specMap() ? 1 : 0);
	if(o.tiles == SceneOptions::kTilesTap)
		m_tap.reset(new ExeCacheTap(m_hdr, (uint32_t)o.tilePlots));
	else if(o.tiles == SceneOptions::kTilesBake)
	{
		// issue 3308: the server's own bake (epic 3400's Residency over the pipeline baker)
		ResidencySourceConfig bc = o.bake;
		bc.decals = o.decals != 0;   // issue 4105: render_decals
		bc.decalTrace = o.decalTrace;
		if(bc.decals && bc.decalLooseRoots.empty())
			bc.decalLooseRoots = ModFolders();
		m_res.reset(new ResidencySource(bc));
		const c5r::Status rs = m_res->init(dev, ctx);
		std::string dwhy;
		if(!rs.ok() && m_res->decalInitFailed(dwhy))
			decalFail(dwhy + " (the EXE's TerrainDecalShader blobs; '" + Narrow(o.exe) + "')");   // issue 4105: an install failure
		if(rs.ok() && m_decalReader)
		{   // issue 4105: a device rebuild: the new residency takes the set in force
			std::string why;
			if(!m_res->setDecals(composedDecals(m_decalReader->current()), why))   // epic 0431: with the Tree_Shadow decals
				decalFail(why);
		}
		if(!rs.ok())
		{
			m_sourceFailures++;
			RNote("LOUD: residency: not started (baker %s, install '%ls'): %s; the terrain is drawn without caches (fallback colour)",
			      o.bake.pipeline ? "pipeline" : "stand-in", o.bake.install.c_str(), rs.str().c_str());
			m_res.reset();
		}
		else if(o.tilePlots && o.tilePlots != 1)
			RNote("LOUD: residency: render_tile_plots=%d fixes another tile size than the bake's 1: the residency will refuse the field",
			      o.tilePlots);
	}
	else
		RNote("terrain scene: render_tiles=none: no tile caches (fallback colour)");
	{   // issue 3607: the water module (a failure leaves the placeholder plane; logged once, not LOUD)
		WaterSceneOptions wo;
		wo.mode = o.water ? o.waterMode : WaterSceneOptions::kOff;
		wo.freeze = o.waterFreeze;
		wo.exe = o.exe;
		wo.gameDir = o.install;
		wo.files = filesOk ? &files : nullptr;   // issue 0198: the waves read the scene's index (else they open their own)
		m_water.init(dev, wo);
	}
	// issue 3704: the overlay source (render_overlay=1)
	if(o.overlay && m_hdr)
		m_ovl = MakeTapOverlaySource(m_hdr, dev);
	else
		RNote("terrain scene: render_overlay=0: no overlay (terrain t1 and water t9 transparent)");
	m_ovlHave = false;
	m_ovlLoggedState = -1;
	{   // issue 3954: the fog of war (render_fow); a module that cannot start is reported at the first frame (the install)
		FowSceneOptions fo;
		fo.on = o.fow != 0;
		fo.overlay = o.overlay != 0 && m_hdr;
		fo.level = o.fowLevel;
		fo.exe = o.exe;
		fo.gameDir = o.install;
		fo.freeze = o.waterFreeze;
		fo.installWaitMs = o.fowWaitMs;
		fo.maskWindowCamera = RenderFowMaskWindowOption() != kFowMaskWindowPair;   // ticket 0674: render_fow_mask_window
		m_fow.init(dev, fo);
		if(fo.on)
			RNote("fog: render_fow_mask_window=%s (ticket 0674)", RenderFowMaskWindowName(RenderFowMaskWindowOption()));
		// ticket 0793: the overlay's history (render_fow_overlay_history as ParseHostConfig read it; the environment's
		// C5_RENDER_FOW_OVERLAY_HISTORY overrides it)
		char hv[8] = "";
		const DWORD hn = GetEnvironmentVariableA("C5_RENDER_FOW_OVERLAY_HISTORY", hv, (DWORD)sizeof(hv));
		const bool hEnv = hn > 0 && hn < sizeof(hv);
		if(hEnv)
		{
			std::string why;
			if(!ParseRenderFowOverlayHistory(hv, why))
				RNote("LOUD: fog: %s (C5_RENDER_FOW_OVERLAY_HISTORY ignored)", why.c_str());
		}
		m_ovlHistOn = RenderFowOverlayHistoryOption();
		if(fo.on)
			RNote("fog: render_fow_overlay_history=%s%s (ticket 0793: the overlay's leading edge from the whole-map history)",
			      RenderFowOverlayHistoryName(m_ovlHistOn), hEnv ? " by C5_RENDER_FOW_OVERLAY_HISTORY" : "");
	}
	{   // issue 4308: the models' shadow (a reader per EXE pid on this device, made in poll()) and the polar ice
		m_shHave = false;
		m_shLoggedState = -1;
		m_shPid = 0;
		if(o.shadow)
			RNote("terrain scene: render_shadow=1: the shadow tap's triple as terrain t3 / Arg6 / ShadowMatrix0 and water t13 / 0x220 / "
			      "ShadowMatrix0 (install bound %.0f ms)",
			      o.shadowWaitMs);
		else
			RNote("terrain scene: render_shadow=0: no models' shadow (terrain t3 and water t13 as before 4308)");
		if(o.ice)
		{
			m_ice = new c5r::ice::Renderer();
			std::string why;
			if(!filesOk)
				why = "no game install to read the ice's textures from ('" + Narrow(o.install) + "')";
			else
			{
				c5r::shaders::ShaderSection sec;
				c5r::Status s1 = sec.open(o.exe);
				if(s1.ok())
					s1 = m_ice->init(dev, sec, files);
				if(!s1.ok())
					why = "the ice module did not start (IceShader programs of '" + Narrow(o.exe) + "', the install's ice textures): " + s1.str();
			}
			if(why.empty())
			{
				m_iceWhite = OnePixel(dev, 0xFFFFFFFFu);
				m_iceBlack = OnePixel(dev, 0xFF000000u);
				if(!m_iceWhite || !m_iceBlack)
					why = "the ice's fog-off constants could not be made";
			}
			if(why.empty())
			{
				m_iceReady = true;
				iceApply();   // a device rebuild: the last good mesh again
				RNote("terrain scene: render_ice=1: the ice tap's mesh drawn after the ocean, before the waves and the cloud planes "
				      "(install bound %.0f ms, 0 = none)%s",
				      o.iceWaitMs, m_iceMesh ? "; the kept mesh re-applied" : "");
			}
			else
				iceFail(why);
		}
		else
			RNote("terrain scene: render_ice=0: no polar ice");
	}
	{   // epic 0431, ticket 4554: the trees (render_forest); made once, kept across device rebuilds (records, layouts, decals)
		if(o.forest && !m_forest)
		{
			m_forest = MakeSceneForest();
			if(!m_forest)
				RNote("LOUD: forest: render_forest=1 but this program does not link server/src/forest/: no trees");
		}
		else if(!o.forest)
			m_forest.reset();
		if(m_forest)
		{
			ForestSceneOptions fo;
			fo.on = true;
			fo.exe = o.exe;
			fo.files = filesOk ? &files : nullptr;   // issue 0198's shared index (init only)
			fo.gameDir = o.install;
			fo.seed = o.forestSeed;
			fo.idsWaitMs = o.forestIdsWaitMs;
			fo.cutWaitMs = o.forestCutWaitMs;     // ticket 4553's reader (made once the EXE's pid is known)
			fo.cutForceFail = o.forestCutFail;
			m_forest->init(dev, fo);
		}
		else
			RNote("terrain scene: render_forest=0: no trees");
	}
	{   // epic 0528, ticket 4852: the server's farm fields (render_farmfields); made once, its worker and cache kept
		if(o.farmfields && !m_farm)
		{
			m_farm.reset(new FarmFieldsProducer());
			FarmFieldsOptions fo;
			fo.removeTapFarm = !o.farmKeepTap;
			fo.idsWaitMs = o.farmIdsWaitMs;
			fo.threaded = o.farmThreaded;
			m_farm->setOptions(fo);
			m_farm->loadFarmTypesFrom(o.install, ModFolders());   // farm_types.xml on the producer's worker
			if(!m_sectionName.empty())
				m_farm->setPointSection(m_sectionName);   // ticket 0657
			RNote("terrain scene: render_farmfields=1: the server lays out the farm fields (the tap's farm records %s)",
			      o.farmKeepTap ? "kept" : "dropped");
		}
		else if(!o.farmfields)
		{
			m_farm.reset();
			if(CutOverlaySlot().active())
				CutOverlaySlot().set(nullptr);   // ticket 4853: no footprints of the server's own
			m_farmCutSeen = 0;
			RNote("terrain scene: render_farmfields=0: the decal tap's farm fields as they are");
		}
	}
	{   // ticket 4934 (epic 0454): FixRoadLayout (render_roadlayout); made once, its worker kept
		if(o.roadLayout && !m_roads)
		{
			m_roads = srvextras::MakeSceneRoads();
			if(!m_roads)
				RNote("LOUD: terrain scene: render_roadlayout=1 but this program does not link server/src/extras/: the tap's roads as they are");
			else
			{
				m_roads->setLog(&RoadsLog, nullptr);
				srvextras::SceneRoadsOptions ro;
				ro.threaded = o.roadThreaded;
				m_roads->setOptions(ro);
				std::shared_ptr<FowPlotTable> table(MakeFeedPlotTable().release());
				m_roads->setTable([table](std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h) {
					if(table)
						table->copy(out, gen, w, h);
				});
				RNote("terrain scene: render_roadlayout=1: the server lays out the roads and railroads (FixRoadLayout, style (e)); the tap's "
				      "route curves dropped");
			}
		}
		else if(!o.roadLayout && m_roads)
		{
			m_roads.reset();
			if(CutOverlaySlot().roadsActive())
				CutOverlaySlot().setRoads(nullptr, nullptr);
			m_roadsCutSeen = 0;
			RNote("terrain scene: render_roadlayout=0: the decal tap's roads as they are");
		}
	}
	{   // ticket 0199 part 2: the ranged-attack arc (render_arc): ArrowSettings.xml from the shared index, the EXE's ArrowShader
		if(o.arc && !m_arcSettingsRead && filesOk)
		{
			std::vector<uint8_t> bytes;
			std::string origin, note;
			c5r::arc::Settings s;
			c5r::Status as = files.read(c5r::arc::kSettingsFile, bytes, &origin);
			if(as.ok())
				as = c5r::arc::parseSettings(reinterpret_cast<const char*>(bytes.data()), bytes.size(), s, &note);
			if(as.ok())
			{
				m_arc.setSettings(s);
				m_arcSettingsRead = true;
				RNote("arc: %s from %s (target_height %.2f, anim_speed %.2f s, alpha %.2f, ambient %.2f)%s%s", c5r::arc::kSettingsFile,
				      origin.c_str(), s.targetHeight, s.animSpeed, s.alpha, s.ambient, note.empty() ? "" : "; defaults kept for: ",
				      note.c_str());
			}
			else
				RNote("LOUD: arc: %s not read from the install '%ls': %s; the arc is not drawn", c5r::arc::kSettingsFile,
				      o.install.c_str(), as.str().c_str());
		}
		else if(o.arc && !m_arcSettingsRead)
			RNote("LOUD: arc: render_arc=1 but the install's index is not open ('%ls'): no %s, the arc is not drawn", o.install.c_str(),
			      c5r::arc::kSettingsFile);
		arcInit(dev, o);
	}
	pathInit(dev, o, filesOk ? &files : nullptr);   // ticket 0300 part 2: the move-order path's models (render_path)
	// epic 0454, ticket 4938: the extras' effective state once per Scene (SrvOptions(): the server's options file, the
	// offline player's defaults and --option), then their device (a failure is the extra's own line; it is off for the session)
	if(!m_extrasConfigured)
		configureExtras(SrvOptions(), o.fow != 0);
	if(m_extras)
		m_extras->initDevice(dev);
	if(m_haveGrid)
	{
		RNote("terrain: re-applying the kept field (%ux%u plots, load seq %u) on the new device", m_grid.mapW, m_grid.mapH, m_fieldLoadSeq);
		applyField(m_fieldTilePlots ? m_fieldTilePlots : wantTilePlots());
	}
	m_dirty = true;
	return st;
}

void Scene::releaseSource()
{
	if(m_res)
	{
		if(const c5r::bake::ResidencyStats* s = m_res->stats())
		{
			m_resPrev[0] += s->bakes;
			m_resPrev[1] += s->rebakes;
			m_resPrev[2] += s->failures;
			m_resPrev[3] += s->grayFrames;
			m_resPrev[4] += s->grayFramesAfterAhead;
		}
		m_resPrev[5] += m_res->plotsChanged;
		{   // issue 4105
			const DecalBakeCounters& d = m_res->decalCounters();
			m_decalPrev.sets += d.sets;
			m_decalPrev.setsRefused += d.setsRefused;
			m_decalPrev.changes += d.changes;
			m_decalPrev.changesAll += d.changesAll;
			m_decalPrev.changesSkipped += d.changesSkipped;
			m_decalPrev.raises += d.raises;
			m_decalPrev.bakesWithDecals += d.bakesWithDecals;
			m_decalPrev.decalsDrawn += d.decalsDrawn;
			m_decalPrev.bakesFailed += d.bakesFailed;
			m_decalPrev.namesResolved += d.namesResolved;
		}
		m_res->release();
	}
	m_res.reset();
}

void Scene::releaseDevice()
{
	joinUnitsJob(false);       // ticket 0711 (R3): a forked begin ends before the module loses its device
	dropRecorded();            // ticket 0753 (J3): a recorded list and the deferred context go with the device
	SafeRelease(m_raCtx);
	m_raOn = false;
	if(m_units)                // epic 0375, issue 4454: the drawing renderer goes with the device (tables, allocator kept)
		m_units->setDevice(nullptr);
	if(m_models)               // epic 0561, ticket 4875: the model renderer goes with the device (library, tap kept)
		m_models->setDevice(nullptr);
	m_water.releaseDevice();   // issue 3607
	m_fow.releaseDevice();     // issue 3954 (the plot table and the install's outcome are kept)
	if(m_forest)               // epic 0431, ticket 4554 (records, layouts and decals kept)
		m_forest->releaseDevice();
	m_arcR.reset();            // ticket 0199 part 2 (the arrow record and its settings kept; the mesh is uploaded again)
	m_arcC.ready = false;
	if(m_extras)               // epic 0454, ticket 4938 (each extra's state kept; its geometry is uploaded again)
		m_extras->releaseDevice();
	m_pathR.reset();           // ticket 0300 part 2 (the models, the path and the instance list kept; uploaded again at init)
	m_pathC.ready = false;
	SafeRelease(m_pathDepth);
	SafeRelease(m_pathDepthTex);
	m_ovl.reset();            // issue 3704: the reader's textures belong to this device
	m_shSrc.reset();           // issue 4308: the shadow reader's textures belong to this device
	m_shHave = false;
	m_shExternal = false;
	m_shPid = 0;
	m_lastShadow = c5t::Shadow();
	m_lastShHave = false;
	WaterSceneShadow(&m_water, nullptr, 0.0f, nullptr);
	m_unitShadow.reset();      // ticket 4785: its programs and targets belong to this device (made again at the next frame)
	m_unitShadowTried = false;
	if(m_ice)                  // issue 4308: the ice module (its source and the last good mesh are kept)
	{
		m_ice->release();
		delete m_ice;
		m_ice = nullptr;
	}
	m_iceReady = false;
	SafeRelease(m_iceWhite);
	SafeRelease(m_iceBlack);
	m_ovlHave = false;
	m_lastOverlay = c5t::Overlay();
	m_boundOverlay = c5t::Overlay();
	m_ovlResample.release();   // ticket 0674
	releaseSource();
	if(m_tap)
	{
		// the source's counters outlive it (device rebuilds, the totals line at the end)
		m_tapPrev[0] += m_tap->cacheOpened;
		m_tapPrev[1] += m_tap->cacheMissing;
		m_tapPrev[2] += m_tap->cacheRefused;
		m_tapPrev[3] += m_tap->manifestErrors;
		m_tapPrev[4] += m_tap->plotsChanged;
		m_tap->release();
	}
	m_tap.reset();
	if(m_r)
	{
		m_r->release();
		delete m_r;
		m_r = nullptr;
	}
	m_rInit = false;
	m_haveField = false;
	m_dev = nullptr;
}

uint32_t Scene::wantTilePlots() const
{
	if(m_opt.tilePlots)
		return (uint32_t)m_opt.tilePlots;
	if(m_res)
		return m_res->wantedTilePlots();   // issue 3308: the bake's tile, one plot
	VPShellBakeTap bt;
	if(m_hdr && SnapBlock(VPSHELL_BAKETAP(m_hdr), bt) && bt.state == VPBAKE_ACTIVE && bt.tilePlotsX == bt.tilePlotsY &&
	   bt.tilePlotsX >= 1 && bt.tilePlotsX <= c5t::kMaxTilePlots)
		return bt.tilePlotsX;
	return c5t::kDefaultTilePlots;
}

// Issue 4305: each terrain tile's style (t6 / t7) from the residency's art types, the same array its bake reads
// (srv_residency_source.h artTypes; the DLL's map = the field's rows - 4). False: no art types yet (the styles stay as
// they are: Generic after a new field) or setTileStyles refused them (LOUD).
static bool ApplyTileStyles(c5r::terrain::Renderer& r, const ResidencySource& res)
{
	const uint8_t* art = nullptr;
	uint32_t ax = 0, ay = 0;
	bool wrap = false;
	if(!res.artTypes(art, ax, ay, wrap))
		return false;
	const c5r::Status st = r.setTileStyles(art, ax, ay, wrap);
	if(!st.ok())
		RNote("LOUD: terrain: setTileStyles(%ux%u plots) failed: %s; the tiles keep their styles", ax, ay, st.str().c_str());
	return st.ok();
}

// Builds the renderer's tiles from the kept grid. Errors are logged and counted (the field is then not drawn).
bool Scene::applyField(uint32_t tilePlots)
{
	if(!m_rInit)
		return false;
	const int64_t q0 = Qpc();
	const c5r::Status st = m_r->setHeightField(m_grid, tilePlots);
	if(!st.ok())
	{
		fieldErrors++;
		m_haveField = false;
		RNote("LOUD: terrain: setHeightField(%ux%u plots, tile plots %u) failed: %s", m_grid.mapW, m_grid.mapH, tilePlots, st.str().c_str());
		return false;
	}
	const c5t::TileGrid tg = m_r->tileGrid();
	m_haveField = true;
	m_fieldTilePlots = tilePlots;
	m_dirty = true;
	const int64_t q1 = Qpc();   // epic 0465, ticket 4909: the parts' times (live-terrain.md 8.14)
	if(TileCacheSource* s = source())
		s->onField(m_dev, m_grid, tilePlots, m_fieldLoadSeq);
	const int64_t q2 = Qpc();
	if(m_res)   // issue 4305: the tile styles of the art types the residency's bake takes for this field
	{
		ApplyTileStyles(*m_r, *m_res);
		m_res->takeStylesChanged();
	}
	const int64_t q3 = Qpc();
	m_water.onField(m_grid, m_fieldLoadSeq);   // issue 3607
	const int64_t q4 = Qpc();
	if(m_forest)
		m_forest->onField(m_grid, m_fieldLoadSeq);   // epic 0431, ticket 4554
	const int64_t q5 = Qpc();
	if(m_farm)
	{   // epic 0528, ticket 4852: the decals' world rectangle (TDS+0x6C, (0, 0, samples - 1)) and the game's seed
		const float wr[4] = {0.0f, 0.0f, (float)m_grid.samplesX - 1.0f, (float)m_grid.samplesY - 1.0f};
		m_farm->setGameSeed(m_grid.mapSeed);
		m_farm->setCutFrame(m_grid.ox, m_grid.oy);   // ticket 4853: the footprint's frame for the forest's cut lookups
		m_farm->setPointFrame(m_grid.sx, m_grid.sy, m_grid.ox, m_grid.oy);   // ticket 0657: the EXE's world frame of the points
		m_farm->setWorld(wr, m_fieldLoadSeq);
	}
	if(m_roads)
	{   // ticket 4934: the same world rectangle, the plot grid's wrap and the cut grid's frame
		const float wr[4] = {0.0f, 0.0f, (float)m_grid.samplesX - 1.0f, (float)m_grid.samplesY - 1.0f};
		m_roads->setWorld(wr, m_fieldLoadSeq, m_grid.wrapX != 0, m_grid.ox, m_grid.oy);
	}
	RNote("terrain: field %ux%u plots (%ux%u samples, wrap %d, water z %.3f) as %ux%u tiles of %u plots in %.1f ms (setHeightField %.1f, "
	      "tile source %.1f, styles %.1f, water %.1f, trees %.1f, farm %.1f)",
	      m_grid.mapW, m_grid.mapH, m_grid.samplesX, m_grid.samplesY, (int)m_grid.wrapX, m_grid.waterZ, tg.tilesX, tg.tilesY, tilePlots,
	      QpcToMs(Qpc() - q0), QpcToMs(q1 - q0), QpcToMs(q2 - q1), QpcToMs(q3 - q2), QpcToMs(q4 - q3), QpcToMs(q5 - q4),
	      QpcToMs(Qpc() - q5));
	return true;
}

void Scene::writeTapAck(uint32_t loadSeq, uint32_t copySeq, int32_t result)
{
	VPShellTap* tap = VPSHELL_TAP(m_hdr);
	tap->ackServerPid = GetCurrentProcessId();
	InterlockedExchange((volatile LONG*)&tap->ackResult, result);
	// issue 3206: the DLL accepts an ack only when ackCopySeq == copySeq
	InterlockedExchange((volatile LONG*)&tap->ackCopySeq, (LONG)copySeq);
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&tap->ackLoadSeq, (LONG)loadSeq);   // written last
}

// The tap block: a READY copy of a load not acknowledged yet is opened, checked, copied, closed and acknowledged
// (ackResult first, then ackLoadSeq), then its tiles are built (shell_server.cpp TapPoll).
void Scene::tapPoll()
{
	VPShellTap tb;
	if(!SnapBlock(VPSHELL_TAP(m_hdr), tb))
		return;
	if(tb.state != m_tapLoggedState)
	{
		RNote("terrain: tap block state %s (load seq %u, fail reason %u, %llu payload bytes, section '%.64ls')",
		      tb.state < 6 ? kTapStateNames[tb.state] : "?", tb.loadSeq, tb.failReason, (unsigned long long)tb.payloadBytes,
		      tb.sectionName);
		m_tapLoggedState = tb.state;
	}
	// Step 4 of the protocol (issue 3206): no field of the current load while the DLL's copy is gone: ask once per copy.
	if((tb.state == VPTAP_RELEASED || (tb.state == VPTAP_FAILED && tb.failReason == VPTAPF_ACK_TIMEOUT)) &&
	   !(m_haveGrid && m_fieldLoadSeq == tb.loadSeq) &&
	   !(m_tapRequested && m_tapRequestLoad == tb.loadSeq && m_tapRequestCopy == tb.copySeq))
	{
		VPShellCamera c2;
		if(SnapCamera(m_hdr, c2) && c2.gridValid == 1 && c2.gridSeq == tb.loadSeq)
		{
			const uint32_t r = (uint32_t)InterlockedIncrement((volatile LONG*)&VPSHELL_TAP(m_hdr)->requestSeq);
			m_tapRequested = true;
			m_tapRequestLoad = tb.loadSeq;
			m_tapRequestCopy = tb.copySeq;
			tapRequests++;
			RNote("terrain: tap of load seq %u is %s (copy %u) and this server has no field of it: new copy requested (requestSeq %u)",
			      tb.loadSeq, tb.state == VPTAP_RELEASED ? "RELEASED" : "FAILED ACK_TIMEOUT", tb.copySeq, r);
		}
	}
	if(tb.state != VPTAP_READY)
		return;
	if(m_tapAckedAny && tb.loadSeq == m_tapAcked && tb.copySeq == m_tapAckedCopy)
		return;
	const int64_t q0 = Qpc();
	int32_t result = TAPACK_OK;
	std::string why;
	tb.sectionName[63] = 0;
	VPShellCamera cam;
	const bool haveCam = SnapCamera(m_hdr, cam) && cam.gridValid == 1;
	if(haveCam && cam.gridSeq != tb.loadSeq)
	{
		result = TAPACK_STALE;
		why = "stale: the camera block's grid seq is " + std::to_string(cam.gridSeq) + ", the tap's load seq " + std::to_string(tb.loadSeq);
	}
	HANDLE h = nullptr;
	const uint8_t* view = nullptr;
	size_t size = 0;
	c5h::HeightGrid g;
	if(result == TAPACK_OK)
	{
		h = OpenFileMappingW(FILE_MAP_READ, FALSE, tb.sectionName);
		view = h ? (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0) : nullptr;
		if(!view)
		{
			const DWORD le = GetLastError();
			if(h)
				CloseHandle(h);
			h = nullptr;
			result = TAPACK_OPEN;
			why = "cannot open section '" + Narrow(tb.sectionName) + "' (error " + std::to_string(le) + ")";
		}
		else
		{
			MEMORY_BASIC_INFORMATION mbi = {};
			size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
		}
	}
	if(result == TAPACK_OK && (tb.payloadBytes > size || tb.payloadBytes < c5h::kTapHeaderSize))
	{
		result = (int32_t)c5r::Err::Truncated;
		why = "payload " + std::to_string(tb.payloadBytes) + " bytes, section view " + std::to_string(size) + " bytes";
	}
	if(result == TAPACK_OK && tb.fnv1a64)
	{
		const uint64_t hsh = Fnv1a64(view, (size_t)tb.payloadBytes);
		if(hsh != tb.fnv1a64)
		{
			result = TAPACK_HASH;
			char b[96];
			_snprintf_s(b, sizeof(b), _TRUNCATE, "FNV-1a 64 %016llX, the block says %016llX", (unsigned long long)hsh,
			            (unsigned long long)tb.fnv1a64);
			why = b;
		}
	}
	if(result == TAPACK_OK)
	{
		const c5r::Status st = c5h::readTap(view, (size_t)tb.payloadBytes, g);
		if(!st.ok())
		{
			result = (int32_t)st.code;
			why = st.str();
		}
		// epic 0301, issue 3904: the recorder's hand-off point, for a copy the checks below will accept (the same tests, made
		// before the unmap), with the payload cut to the tap's size
		else if(m_fieldSink && g.mapW == tb.mapW && g.mapH == tb.mapH && g.samplesX == tb.samplesX && g.samplesY == tb.samplesY &&
		        (!haveCam || (cam.samplesX == g.samplesX && cam.samplesY == g.samplesY)))
			m_fieldSink(m_fieldSinkCtx, tb.loadSeq, view, c5h::tapSize(g.mapW, g.mapH, !g.mat.empty()));
	}
	if(view)
		UnmapViewOfFile(view);
	if(h)
		CloseHandle(h);
	if(result == TAPACK_OK && (g.mapW != tb.mapW || g.mapH != tb.mapH || g.samplesX != tb.samplesX || g.samplesY != tb.samplesY))
	{
		result = (int32_t)c5r::Err::BadLayout;
		why = "map size: C5HF " + std::to_string(g.mapW) + "x" + std::to_string(g.mapH) + " plots, the tap block " +
		      std::to_string(tb.mapW) + "x" + std::to_string(tb.mapH) + " (samples " + std::to_string(tb.samplesX) + "x" +
		      std::to_string(tb.samplesY) + ")";
	}
	if(result == TAPACK_OK && haveCam && (cam.samplesX != g.samplesX || cam.samplesY != g.samplesY))
	{
		result = (int32_t)c5r::Err::BadLayout;
		why = "map size: C5HF " + std::to_string(g.samplesX) + "x" + std::to_string(g.samplesY) + " samples, the camera block's grid " +
		      std::to_string(cam.samplesX) + "x" + std::to_string(cam.samplesY);
	}
	writeTapAck(tb.loadSeq, tb.copySeq, result);   // the DLL may close its section now
	m_tapAckedAny = true;
	m_tapAcked = tb.loadSeq;
	m_tapAckedCopy = tb.copySeq;
	m_tapLastResult = result;
	if(result != TAPACK_OK)
	{
		tapErrors++;
		RNote("terrain: tap load seq %u refused (ackResult %d): %s", tb.loadSeq, result, why.c_str());
		return;
	}
	tapLoads++;
	RNote("terrain: tap load seq %u read: %ux%u plots, %llu bytes%s, copied and acknowledged in %.1f ms", tb.loadSeq, g.mapW, g.mapH,
	      (unsigned long long)tb.payloadBytes, tb.fnv1a64 ? ", hash ok" : ", no hash", QpcToMs(Qpc() - q0));
	liveTerrainJoin();   // epic 0465, ticket 4909: a live apply's page worker reads m_grid until it ends
	m_grid = std::move(g);
	m_haveGrid = true;
	m_fieldLoadSeq = tb.loadSeq;
	applyField(wantTilePlots());
}

void Scene::poll()
{
	if(m_units)   // epic 0375, issue 4454: the unit database's generation, the pre-check's end
	{
		m_units->poll();
		// epic 0040, issue 4753: a readiness lost between frames clears the unit bit at once; the loss bound (D125's step)
		if(m_unitsMode)
			m_unitsMode->onPoll(m_units->ready(), Qpc());
	}
	if(!m_hdr)
	{
		// epic 0301, issue 3903: the offline player's form (no section, the inputs from a bundle): the overlay source and
		// the residency's redraw flag only. server.exe always attaches, so its path below is unchanged.
		overlayPoll();
		shadowPoll();   // issue 4308 (the player's sources)
		icePoll();
		if(m_forest && m_forest->poll())   // epic 0431, ticket 4554: the published feature ids
			m_dirty = true;
		forestDecalPoll();
		roadsDecalPoll();  // ticket 4934 (before the farm: roads -> farm -> forest)
		farmDecalPoll();   // epic 0528, ticket 4852
		if(m_models && m_models->poll(Qpc()))   // ticket 4875: the layer's open (the player's frames come by loadImage)
			m_dirty = true;
		if(m_rInit && m_res && m_res->takeVisibleChanged())
			m_dirty = true;
		if(m_rInit && m_res && m_haveField && m_res->takeStylesChanged() && ApplyTileStyles(*m_r, *m_res))   // issue 4305
			m_dirty = true;
		return;
	}
	tapPoll();
	m_water.poll();   // issue 3607: the water tap (acked in every mode)
	overlayPoll();    // issue 3704
	decalPoll();      // issue 4105
	shadowPoll();     // issue 4308
	icePoll();
	if(m_forest)   // epic 0431, ticket 4554: the cut tap's reader for this EXE (ticket 4553), the published feature ids
	{
		m_forest->setExePid(exePid());
		if(m_forest->poll())
			m_dirty = true;
	}
	forestDecalPoll();
	roadsDecalPoll();  // ticket 4934
	farmDecalPoll();   // epic 0528, ticket 4852
	if(m_models)       // epic 0561, ticket 4875: the model tap for this EXE, the identity set, the model bits
	{
		m_models->setExePid(exePid());
		if(m_models->poll(Qpc()))
			m_dirty = true;
	}
	if(!m_rInit)
		return;
	if(m_res)
	{
		// issue 3308: the residency's visible tile got its cache in idle(): draw again with the last camera
		if(m_res->takeVisibleChanged())
			m_dirty = true;
		// issue 4305: the art types changed after the field (a table that came later, a new load's): the styles again
		if(m_haveField && m_res->takeStylesChanged() && ApplyTileStyles(*m_r, *m_res))
			m_dirty = true;
		liveTerrainStep();   // epic 0465, ticket 4907: live terrain (srv_liveterrain_scene.cpp)
		return;
	}
	if(!m_tap)
		return;
	if(m_tap->poll(*m_r))
		m_dirty = true;
	// issue 3306: the source asks for another tile size (srv_tile_source.h wantedTilePlots); rebuild from the kept field,
	// then let the source apply what it waited for in the same iteration. A fixed render_tile_plots is never overridden.
	const uint32_t want = m_tap->wantedTilePlots();
	if(want && want != m_fieldTilePlots && !m_opt.tilePlots && m_haveGrid && m_haveField)
	{
		if(applyField(want) && m_tap->poll(*m_r))
			m_dirty = true;
	}
}

bool Scene::decide(SceneFrame& out)
{
	if(!m_hdr)
		return false;
	VPShellCamera c;
	const bool got = SnapCamera(m_hdr, c);
	const int64_t pc = InterlockedCompareExchange64(&VPSHELL_CAMERA(m_hdr)->presentCount, 0, 0);
	int st = TS_NO_CAMERA;
	if(got)
	{
		if(c.seq != m_lastCamSeq)
		{
			m_lastCamSeq = c.seq;
			camBlocksTotal++;
			m_blocksSec++;
		}
		float rot = 0, trans = 0, wrapErr = 0;
		const int64_t lag = pc - c.frameStamp;
		if(lag > m_opt.staleFrames)
			st = TS_STALE;
		else if(c.status != VPCAM_OK)
			st = TS_HIDDEN;
		else if(!ServerConsistent(c, rot, trans, wrapErr))
		{
			st = TS_SRV_INCONSISTENT;
			if(m_state != TS_SRV_INCONSISTENT)
				RNote("terrain: camera block says OK but fails the server's own check: rot %.2e trans %.3f wrap %.3f views %u (frame %lld)",
				      rot, trans, wrapErr, c.numViews, (long long)c.frameStamp);
		}
		else
		{
			st = TS_OK;
			m_cam = c;
			m_haveCam = true;
		}
	}
	else if(m_haveCam)
		st = m_state;   // torn snapshot: keep the state for one iteration
	if(st == TS_OK && !m_haveField)
		st = TS_NO_FIELD;
	// a field from the tap belongs to one load: once the camera block's grid is another load's (map change), the old
	// field is not drawn until the new load's tap arrives
	if(st == TS_OK && m_fieldLoadSeq && m_cam.gridValid == 1 && m_cam.gridSeq != m_fieldLoadSeq)
		st = TS_NO_FIELD;
	// ticket 0691 (cand4's hang, 2026-11-23): a camera refused for the field is HELD (reported to the composite, which does
	// not wait for it): the new load's field comes from the composite's tap only after more EXE Presents (its reload gate:
	// 30 with the camera OK), which an exact lockstep wait for this frame would never let happen
	m_fieldHoldFrame = (st == TS_NO_FIELD && m_haveCam && m_cam.frameStamp > 0) ? m_cam.frameStamp : 0;
	// presents the game showed with a kept (old) frame
	if(m_lastPc >= 0 && pc > m_lastPc && (st == TS_STALE || st == TS_HIDDEN || st == TS_SRV_INCONSISTENT))
	{
		const int64_t d = pc - m_lastPc;
		if(st == TS_STALE)
		{
			staleTotal += d;
			m_staleSec += (int)d;
		}
		else
		{
			hiddenTotal += d;
			m_hiddenSec += (int)d;
		}
	}
	m_lastPc = pc;
	if(st != m_state)
	{
		RNote("terrain: state %s -> %s (camera status %s, present %lld, block frame %lld, seq %u)", m_state < 0 ? "-" : kTsNames[m_state],
		      kTsNames[st], got ? VPCamStatusName(c.status) : "-", (long long)pc, got ? (long long)c.frameStamp : -1LL, got ? c.seq : 0);
		m_state = st;
	}
	if(st != TS_OK)
		return false;
	if(m_cam.frameStamp == m_lastDrawnStamp && !m_dirty)
		return false;
	{   // issue 3954: the fog holds the frame until it can be drawn; never a game frame without it (srv_fow_scene.h admit)
		const OverlayTapReader::State os = m_ovl ? m_ovl->state() : OverlayTapReader::State::Absent;
		m_fow.setExePid(exePid());   // ticket 0339: the mask handed to the EXE's model draws (srv_fow_mask_pub.h)
		if(!m_fow.admit(m_cam, m_grid.mapW, m_grid.mapH, m_ovl ? &os : nullptr, m_ovlHave ? &m_ovlFrame : nullptr, QpcToMs(Qpc())))
		{
			if(m_res && m_rInit)   // ticket 0424: Scene::idle bakes this held camera's visible set (srv_residency_source.h)
				m_res->noteHeldView(m_cam);
			return false;
		}
	}
	out.cam = m_cam;
	out.frameId = m_cam.frameStamp;
	return true;
}

// epic 0375, issue 4454: the unit scene's camera of a draw
static srvunits::UnitsCamera UnitsCameraOf(const SceneFrame& f)
{
	srvunits::UnitsCamera uc;
	memcpy(uc.view, f.cam.view[0], sizeof(uc.view));
	memcpy(uc.proj, f.cam.proj, sizeof(uc.proj));
	memcpy(uc.camWorld, f.cam.camWorld, sizeof(uc.camWorld));
	uc.wrapPeriod = f.cam.wrapPeriod;
	uc.frameId = f.frameId;
	uc.nowMs = f.nowMs;
	return uc;
}

c5r::Status Scene::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
                        const SceneFrame& f, c5t::FrameStats& fs)
{
	if(!m_rInit)
		return c5r::Status::fail(c5r::Err::NotFound, "renderer not initialised");
	// ticket 0711 (J1): the units' begin forked once the camera is fixed: by the host right after decide(), else here (the
	// players, a recording host: the same fork and join, R8); joined before the units' first use below, and on every return
	// of this draw by the guard (R3; its join does not throw)
	if(m_unitsForked && (m_unitsForkFrame != f.frameId || !(m_unitsForkNowMs == f.nowMs)) && !m_unitsForkMismatchSaid)
	{
		m_unitsForkMismatchSaid = true;
		RNote("LOUD: frame jobs: the units' begin was forked for frame %lld (clock %.3f) but frame %lld (clock %.3f) is drawn: the "
		      "fork's camera is used (ticket 0711; said once)",
		      (long long)m_unitsForkFrame, m_unitsForkNowMs, (long long)f.frameId, f.nowMs);
	}
	if(!m_unitsForked)
		forkFrameJobs(f);
	struct JoinAtEnd
	{
		Scene& s;
		~JoinAtEnd()
		{
			s.dropRecorded();   // ticket 0753: a list recorded ahead whose place this draw did not reach (counted)
			s.joinUnitsJob(false);
		}
	} joinAtEnd{*this};
	m_drawHeadTicks = m_drawMidTicks = m_raRecordTicks = 0;
	m_raExecFrameMs = 0;
	m_pathDepthAhead = false;   // ticket 0753 (set again by this draw's record ahead)
	if(TileCacheSource* src = source())
	{
		ViewInfo vi;
		vi.frameId = f.frameId;
		memcpy(vi.camWorld, f.cam.camWorld, sizeof(vi.camWorld));
		memcpy(vi.view, f.cam.view[0], sizeof(vi.view));
		memcpy(vi.proj, f.cam.proj, sizeof(vi.proj));
		vi.width = w;
		vi.height = h;
		// the tap's budget as before (2 ms); the residency's from render_bake_budget_ms (shell_server's --bake-pump-ms)
		vppacing::StageScope pace(vppacing::kStPreDraw, &Qpc);   // ticket 0688: the pre-draw bakes and copies
		src->beforeDraw(ctx, *m_r, vi, m_res ? m_res->budgetMs() : 2.0);
	}
	c5t::Camera cam;
	memcpy(cam.view, f.cam.view[0], sizeof(cam.view));
	memcpy(cam.proj, f.cam.proj, sizeof(cam.proj));
	memcpy(cam.camWorld, f.cam.camWorld, sizeof(cam.camWorld));
	cam.width = w;
	cam.height = h;   // the target; the projection maps the EXE's back buffer onto it
	c5t::Options op;
	op.pass = m_opt.pass;
	op.tessFactor = (float)m_opt.tess;
	op.tileD4 = (uint32_t)m_opt.tileD4;
	// issue 0191: the EXE's LOD (c5r::terrain kLodExe, the default) scales by the EXE's back-buffer width [0x301056C]: the
	// camera block's viewport width (D3DVIEWPORT9 0x2DD1FB8, back-buffer pixels) [I that the two are equal], else the target's
	op.lodWidth = f.cam.vpW ? f.cam.vpW : w;
	op.color = m_opt.color;
	op.fallback = m_opt.fallback;
	op.constantRgba = m_opt.constantRgba;
	// issue 3607: the module's water, or the placeholder plane when it cannot draw (the reason logged once per load)
	// epic 0301, issue 3903: the clock passed in (SceneFrame::nowMs); negative = the wall clock, read here as before
	const bool waterDraws = m_water.decide(f.cam, f.nowMs >= 0 ? f.nowMs : QpcToMs(Qpc()));
	op.water = m_water.placeholder();
	op.clearRgba = 0;
	op.clearDepth = true;
	// issue 3704: the overlay pair of decision 2 (i), each wrap copy with its EXE view's matrix
	if(m_ovlHave)
	{
		OverlayCopies oc;
		std::string why;
		if(OverlayCopiesFromFrame(m_ovlFrame, f.cam.wrapPeriod, oc, why))
		{
			op.overlay.srv = m_ovlFrame.srv;
			memcpy(op.overlay.proj, oc.proj, sizeof(op.overlay.proj));
			ovlDerivedCopies += (oc.fromExe[0] ? 0 : 1) + (oc.fromExe[2] ? 0 : 1);
			// ticket 0793: every received overlay into the whole-map history (view 0's window as the EXE wrote it; the map
			// keyed by the camera block's world box, wrap and grid seq), read by the move below on a camera-window frame
			if(m_ovlHistOn && m_ovlFrame.srv && m_fow.state() != FowScene::kOff && m_fow.maskWindowCamera())
			{
				c5r::fow::OverlayWorld hw;
				hw.x0 = f.cam.aabbMin[0];
				hw.x1 = f.cam.aabbMax[0];
				hw.y0 = f.cam.aabbMin[1];
				hw.y1 = f.cam.aabbMax[1];
				hw.wrapPeriod = f.cam.wrapX && std::isfinite(f.cam.wrapPeriod) && f.cam.wrapPeriod > 0 ? f.cam.wrapPeriod : 0.0;
				hw.key = f.cam.gridSeq;
				ovlHistOffers++;
				const bool noWorld = !(std::isfinite(hw.x0) && std::isfinite(hw.x1) && std::isfinite(hw.y0) && std::isfinite(hw.y1) &&
				                       hw.x1 > hw.x0 && hw.y1 > hw.y0);
				const c5r::Status hs = noWorld ? c5r::Status::fail(c5r::Err::BadLayout, "no world box in the camera block")
				                               : m_ovlResample.remember(ctx, m_ovlFrame.srv, oc.proj[1], hw, m_ovlFrame.generation);
				if(!hs.ok())
				{
					ovlHistFails++;
					if(hs.str() != m_ovlHistLastErr)
						RNote("%soverlay history: overlay of generation %u not kept (%lld so far): %s; the leading edge of a camera-window "
						      "frame stays transparent where the pair does not reach (ticket 0793)",
						      noWorld ? "" : "LOUD: ", m_ovlFrame.generation, (long long)ovlHistFails, hs.str().c_str());
					m_ovlHistLastErr = hs.str();
				}
				else
					m_ovlHistLastErr.clear();
			}
			ovlUnmatchedViews += oc.unmatchedViews;
			if(oc.unmatchedViews && m_ovlLastBad != "unmatched")
				RNote("LOUD: overlay: %u of %u EXE views fit no wrap copy (generation %u, wrap period %.3f): drawn with view 0's matrix moved",
				      oc.unmatchedViews, m_ovlFrame.numViews, m_ovlFrame.generation, f.cam.wrapPeriod);
			m_ovlLastBad = oc.unmatchedViews ? "unmatched" : "";
		}
		else
		{
			ovlBadPairs++;
			if(why != m_ovlLastBad)
				RNote("LOUD: overlay: pair of generation %u not bound: %s (frame drawn without the overlay; %lld so far)", m_ovlFrame.generation,
				      why.c_str(), (long long)ovlBadPairs);
			m_ovlLastBad = why;
		}
	}
	m_lastOverlay = op.overlay;   // the received pair (the recorder writes it; ticket 0674 keeps it so)
	// issue 3954: the fog's mask of this frame (rebuilt when the fog or the pair changed) as water and river t10 and, with
	// the cloud base and FOWAnim, as the terrain's c5t::Options::fow (3953's t4 / t5 / FOWAnim; fog-of-war.md 7.4). The mask
	// is placed by the overlay's window: the bound pair's copy matrices, else (the tap lost or stale) the fog's kept pair's.
	// Ticket 0674 (pan-coherence.md 3): on a frame whose mask window was made from camera N (its view left the pair's window)
	// the readers that share one matrix for the overlay and the mask (terrain t1 / t4, water and river t9 / t10, the trees)
	// take that window, and the overlay of frame N-1 is moved into it (c5r::fow::OverlayResampler, view 0's two windows;
	// where the pair's window does not reach, ticket 0793's history of earlier overlays where it holds the ground, else
	// transparent); a failed move binds no overlay for that frame (counted, LOUD
	// once per reason). The water's overlay is set after this block so that it gets the same window.
	{
		// epic 0301, issue 3911 (MC #1450): the clock passed in (SceneFrame::nowMs, as the water's above); negative = the wall
		// clock, read here as before
		const int paceBefore = vppacing::PassMark(vppacing::kStFog, &Qpc);   // ticket 0688
		ID3D11ShaderResourceView* fowMask = m_fow.beforeWorld(ctx, f.cam, m_ovlHave ? &m_ovlFrame : nullptr, f.nowMs >= 0 ? f.nowMs : QpcToMs(Qpc()),
		                                                      m_haveGrid ? &m_grid : nullptr);
		vppacing::PassMark(paceBefore, &Qpc);
		const FowTerrainInputs& fi = m_fow.terrainInputs();
		bool fowPlaced = false;
		if(fi.on && fi.cameraWindow)
		{
			OverlayFrame win;
			win.numViews = fi.windowViews;
			memcpy(win.overlayProj, fi.windowProj, sizeof(win.overlayProj));
			OverlayCopies oc;
			std::string why;
			if(OverlayCopiesFromFrame(win, f.cam.wrapPeriod, oc, why))
			{
				ID3D11ShaderResourceView* moved = nullptr;
				if(op.overlay.srv)
				{
					const c5r::Status rs = m_ovlResample.resample(ctx, op.overlay.srv, op.overlay.proj[1], oc.proj[1]);
					if(rs.ok())
					{
						moved = m_ovlResample.srv();
						ovlResampled++;
						m_ovlResampleSec++;
						m_ovlResampleLastErr.clear();
					}
					else
					{
						ovlResampleFails++;
						if(rs.str() != m_ovlResampleLastErr)
							RNote("LOUD: overlay: the move into the fog's camera window failed (frame %lld, %lld so far): %s; drawn without the "
							      "overlay this frame, the fog's mask placed by its window (ticket 0674)",
							      (long long)f.frameId, (long long)ovlResampleFails, rs.str().c_str());
						m_ovlResampleLastErr = rs.str();
					}
				}
				else
					ovlCamWinNoOverlay++;
				op.overlay.srv = moved;
				memcpy(op.overlay.proj, oc.proj, sizeof(op.overlay.proj));
				fowPlaced = true;
			}
		}
		if(fowPlaced)
		{
			// ticket 0674: placed by the camera window above
		}
		else if(fi.on && op.overlay.srv)
			fowPlaced = true;
		else if(fi.on)
		{
			OverlayFrame kept;
			OverlayCopies oc;
			std::string why;
			if(m_fow.keptPair(kept) && OverlayCopiesFromFrame(kept, f.cam.wrapPeriod, oc, why))
			{
				memcpy(op.overlay.proj, oc.proj, sizeof(op.overlay.proj));   // t1 stays the transparent default
				fowPlaced = true;
			}
		}
		if(fi.on && fowPlaced)
		{
			op.fow.enabled = true;
			op.fow.mask = fi.mask;
			op.fow.cloudBase = fi.cloudBase;
			memcpy(op.fow.anim, fi.fowAnim, sizeof(op.fow.anim));
		}
		WaterSceneFow(&m_water, fowPlaced ? fowMask : nullptr, fowPlaced ? op.overlay.proj : nullptr);
	}
	m_boundOverlay = op.overlay;   // ticket 0674
	WaterSceneOverlay(&m_water, op.overlay.srv, op.overlay.proj);
	// issue 4308: the shadow triple of this frame (texels, matrices and strength of one EXE frame) for the terrain and the
	// water; quality 0 (no texture) = the modules' no-shadow path with strength 0
	m_lastShHave = false;
	if(m_shHave)
	{
		if(m_shFrame.srv)
		{
			ShadowCopies sc;
			std::string why;
			if(ShadowCopiesFromFrame(m_shFrame, f.cam.wrapPeriod, sc, why))
			{
				op.shadow.srv = m_shFrame.srv;
				op.shadow.strength = m_shFrame.strength;
				memcpy(op.shadow.matrix, sc.matrix, sizeof(op.shadow.matrix));
				m_shc.derivedCopies += (sc.fromExe[0] ? 0 : 1) + (sc.fromExe[2] ? 0 : 1);
				m_shc.unmatchedViews += sc.unmatchedViews;
				if(sc.unmatchedViews && m_shLastBad != "unmatched")
					RNote("LOUD: shadow: %u of %u EXE views fit no wrap copy (generation %u, wrap period %.3f): drawn with view 0's matrix moved",
					      sc.unmatchedViews, m_shFrame.numViews, m_shFrame.generation, f.cam.wrapPeriod);
				m_shLastBad = sc.unmatchedViews ? "unmatched" : "";
				m_lastShHave = true;
			}
			else
			{
				m_shc.badTriples++;
				if(why != m_shLastBad)
					RNote("LOUD: shadow: triple of generation %u not bound: %s (frame drawn without the shadow; %lld so far)", m_shFrame.generation,
					      why.c_str(), (long long)m_shc.badTriples);
				m_shLastBad = why;
			}
		}
		else
			m_lastShHave = true;   // quality 0
	}
	if(m_lastShHave)
	{
		m_lastShFrame = m_shFrame;
		WaterSceneShadow(&m_water, op.shadow.srv, op.shadow.srv ? op.shadow.strength : 0.0f, op.shadow.matrix);
	}
	else
		WaterSceneShadow(&m_water, nullptr, 0.0f, nullptr);
	m_lastShadow = op.shadow;
	// issue 4308: the ice's inputs of this frame (drawn by IceHook at its place below)
	m_iceWanted = false;
	m_iceDrawnFrame = false;
	if(m_opt.ice && m_iceReady && m_ice->hasMesh() && m_haveGrid)
	{
		c5r::ice::FrameInputs& ii = m_iceIn;
		ii = c5r::ice::FrameInputs();
		ii.cam = cam;
		memcpy(ii.overlayProj, op.overlay.proj[1], sizeof(ii.overlayProj));
		const FowTerrainInputs& fi2 = m_fow.terrainInputs();
		if(op.fow.enabled)
		{
			ii.fowMask = op.fow.mask;
			ii.cloudBase = op.fow.cloudBase;
			memcpy(ii.fowAnim, op.fow.anim, sizeof(ii.fowAnim));
		}
		else
		{   // the terrain's fog-off picture: the mask visible, no cloud
			ii.fowMask = m_iceWhite;
			ii.cloudBase = m_iceBlack;
			memcpy(ii.fowAnim, fi2.fowAnim, sizeof(ii.fowAnim));
		}
		const c5r::water::World wd = c5r::water::worldFromGrid(m_grid);
		ii.worldWidth = wd.width;
		ii.wrapX = m_grid.wrapX && wd.width > 0;
		ii.fogRamp = m_r->fogRamp();
		ii.fowLut = m_r->fowLut(0);   // [0xD90E60]: the Generic TerrainFOW volume
		m_iceWanted = true;
	}
	// epic 0042, ticket 4785: the server's units cast into the tapped map before the terrain and the water sample it (the
	// units' frame begun here when the step runs)
	// ticket 0707: the units' frame begun here (plot copies, beginFrame) and their cast into the shadow map count to the
	// `units` mark, as when the units are begun below; `terrain` holds only the clear and the terrain draw (pan-coherence.md
	// 5.12: the begin was 70-80 % of the `terrain` stage on the laptop)
	vppacing::PassMark(vppacing::kStUnits, &Qpc);   // ticket 0688: the layers' marks (the host's pass; none offline)
	if(const vppacing::PassTimer* pt = vppacing::CurrentPass())   // ticket 0711: the draw stage's head (drawHeadTicks)
		m_drawHeadTicks = pt->t[vppacing::kStDraw];
	// ticket 0753 (J3, research/07 architect-2026-11-25-frame-gap-route.md 5): while the units' job is in flight (its join is
	// in unitShadowStep or, without the shadow step, before the reflection pass below), the draw stage's end is recorded into
	// a command list on this thread; drawEnd executes it at the stage's place. Its CPU inputs are final here (the fog's after
	// beforeWorld above, the path's from the feed before decide; pan-coherence.md 5.20's table)
	// Step 2 (5.20.3): before it, the terrain draw and the middle stretch (arc, extras, the path's depth copy, forest) into
	// two lists, with the Options the shadow step will produce; checked after the step
	if(m_raOn)
		recordAhead(rtv, dsv, w, h, f, cam, op);
	const bool unitsBegun = unitShadowStep(ctx, f, op);
	if(m_raTerrainList || m_raMidList)
		raCheckPrediction(op, f.frameId);   // ticket 0753: a missed prediction drops both lists (drawn inline below)
	vppacing::PassMark(vppacing::kStTerrain, &Qpc);
	c5r::Status ds = c5r::Status::Ok();
	if(m_raTerrainList)
	{   // ticket 0753: the clear and the terrain draw as recorded, with the draw's GPU timing around the execute
		const int64_t q0 = Qpc();
		m_r->gpuTimingBegin(ctx);
		ctx->ExecuteCommandList(m_raTerrainList, FALSE);
		m_r->gpuTimingEnd(ctx);
		SafeRelease(m_raTerrainList);
		fs = m_raTerrainFs;
		ds = m_raTerrainDs;
		m_rac.terrainLists++;
		const double ms = QpcToMs(Qpc() - q0);
		m_rac.execMsTotal += ms;
		m_rac.execMsSec += ms;
		m_raExecFrameMs += ms;
	}
	else
	{
		const float clear[4] = {0, 0, 0, 0};   // transparent outside the terrain
		ctx->ClearRenderTargetView(rtv, clear);
		ds = m_r->draw(ctx, rtv, dsv, cam, op, &fs);
	}
	m_lastDrawnStamp = f.frameId;
	m_dirty = false;
	if(!ds.ok())
	{
		drawErrTotal++;
		m_drawErrSec++;
		const std::string e = ds.str();
		if(e != m_lastDrawError || drawErrTotal <= 3)
			RNote("terrain: draw failed (frame %lld): %s", (long long)f.frameId, e.c_str());
		m_lastDrawError = e;
		return ds;
	}
	m_lastDrawError.clear();
	vppacing::PassMark(vppacing::kStUnits, &Qpc);   // ticket 0688
	// epic 0375, issue 4455 (decision 7 (c)): the units' frame (lists and plans) before the water; with render_units_passes=1
	// their reflection pass is drawn by the water scene's models hook into t4 with 0382's reflection camera (the EXE's
	// 0xDA1990 / 0xDA1A90, view-projection 0xDA1890 = WaterFrame 0x160: water-system.md 13.6; unit-server-scene.md 7)
	if(m_units && !unitsBegun)   // ticket 4785: begun before the terrain when the units cast into the shadow map
	{
		// issue 4463: other teams' units only on plots the active team sees (the EXE's drawn set, unit-server-scene.md 3.2);
		// no fog states (render_fow=0): no filter, said once by the unit scene. Ticket 0711 (J1): the plot states and table
		// were handed and the begin forked by forkFrameJobs at the top of draw(); joined here, before the reflection pass
		if(!m_unitsForked)
			forkFrameJobs(f);
		joinUnitsJob(true);
	}
	// epic 0561, ticket 4875 (R1): the model tap of this frame's Present (its VS cb1 holds this camera), before the water's
	// reflection pass and the scene pass draw it. Ticket 4882: the records are drawn at this frame's camera and fog placement
	// (the tap is a Present behind under lockstep, city-renderer.md 11.11): view 0 x projection as D3DXMatrixMultiply, and
	// with the fog placed the OverlayProj of view 0 and FOWAnim that the terrain's fog uses this frame
	vppacing::PassMark(vppacing::kStModels, &Qpc);   // ticket 0688
	if(m_models)
	{
		srvmodels::FrameTextures& mc = m_modelsCam;
		mc = srvmodels::FrameTextures();
		for(int i = 0; i < 4; ++i)
			for(int j = 0; j < 4; ++j)
			{
				float a = cam.view[i * 4 + 0] * cam.proj[0 * 4 + j];
				a += cam.view[i * 4 + 1] * cam.proj[1 * 4 + j];
				a += cam.view[i * 4 + 2] * cam.proj[2 * 4 + j];
				a += cam.view[i * 4 + 3] * cam.proj[3 * 4 + j];
				mc.viewProj[i * 4 + j] = a;
			}
		mc.haveViewProj = true;
		mc.haveEye = srvmodels::FrameEye(cam.camWorld, mc.viewProj, mc.eye);   // ticket 0598: view 0's eye (PS cb1 row 16), both passes
		if(op.fow.enabled)
		{
			mc.haveFog = true;
			memcpy(mc.overlayProj, op.overlay.proj[1], sizeof(mc.overlayProj));
			memcpy(mc.overlayProjWrap[0], op.overlay.proj[0], sizeof(mc.overlayProjWrap[0]));   // ticket 0652: copies -1, +1
			memcpy(mc.overlayProjWrap[1], op.overlay.proj[2], sizeof(mc.overlayProjWrap[1]));
			memcpy(mc.fowAnim, op.fow.anim, sizeof(mc.fowAnim));
		}
		mc.wrapPeriod = f.cam.wrapPeriod;   // ticket 0652: the wrap copies of the tapped records (city-renderer.md 17)
		m_models->beginFrame(m_hdr ? f.cam.frameStamp : 0);
	}
	vppacing::PassMark(vppacing::kStWater, &Qpc);   // ticket 0688 (the ice with it)
	if(waterDraws)
	{
		// after the terrain, into its colour and depth (water-system.md 13); an error is counted by the water scene and
		// leaves this frame without water (the terrain frame stands)
		c5r::water::FrameStats wfs;
		WaterSceneAfterOcean(&m_water, m_iceWanted ? &Scene::IceHook : nullptr, this);   // issue 4308: the ice's place
		// issue 0382: the ice into the water's reflection target with its camera (polar-ice.md 13; srv_water_scene.h)
		const WaterReflectionFn reflIce = [](void* u, ID3D11DeviceContext* c, ID3D11RenderTargetView* r, ID3D11DepthStencilView* d,
		                                     const c5r::terrain::Camera& rc, float camZ, uint32_t* n) {
			Scene& s = *static_cast<Scene*>(u);
			c5r::ice::FrameInputs in = s.m_iceIn;   // the frame's inputs, view 0's OverlayProj; the reflection camera and target
			in.cam = rc;
			in.haveFogCamZ = true;                  // row 29.y takes the scene camera's z (polar-ice.md 7)
			in.fogCamZ = camZ;
			c5r::ice::FrameStats st;
			const c5r::Status ds2 = s.m_ice->draw(c, r, d, in, &st);
			*n = st.draws;
			return ds2;
		};
		WaterSceneReflection(&m_water, m_iceWanted ? reflIce : nullptr, this);
		// epic 0375: the units into the same target with the same camera, before the ice (srv_water_scene.h)
		const WaterReflectionFn reflUnits = [](void* u, ID3D11DeviceContext* c, ID3D11RenderTargetView* r, ID3D11DepthStencilView* d,
		                                       const c5r::terrain::Camera& rc, float, uint32_t* n) {
			Scene& s = *static_cast<Scene*>(u);
			float vp[16];   // view x proj, as D3DXMatrixMultiply (0xDA1890)
			for(int i = 0; i < 4; ++i)
				for(int j = 0; j < 4; ++j)
				{
					float a = rc.view[i * 4 + 0] * rc.proj[0 * 4 + j];
					a += rc.view[i * 4 + 1] * rc.proj[1 * 4 + j];
					a += rc.view[i * 4 + 2] * rc.proj[2 * 4 + j];
					a += rc.view[i * 4 + 3] * rc.proj[3 * 4 + j];
					vp[i * 4 + j] = a;
				}
			if(s.m_units && s.m_opt.unitsOpt.passes)
				s.m_units->drawReflection(c, r, d, rc.width, vp);   // failures are counted and LOUD by the unit scene
			// epic 0561, ticket 4875: the cities and landmarks the compositor dropped from the reflection target (their
			// own records carry the EXE's reflection camera in VS cb1); depth decides against the units' reflection
			if(s.m_models)
			{
				srvmodels::FrameTextures ft = s.modelsFrameTextures();
				memcpy(ft.viewProj, vp, sizeof(ft.viewProj));   // ticket 4882: the reflection camera (the EXE's 0xDA1890)
				ft.haveViewProj = true;
				s.m_models->drawReflection(c, r, d, rc.width, rc.height, ft);
			}
			*n = 0;
			return c5r::Status::Ok();
		};
		WaterSceneReflectionModels(&m_water, ((m_units && m_opt.unitsOpt.passes) || m_models) ? reflUnits : nullptr, this);
		m_water.draw(ctx, rtv, dsv, cam, &wfs);
		WaterSceneReflectionModels(&m_water, nullptr, nullptr);
		WaterSceneReflection(&m_water, nullptr, nullptr);
		WaterSceneAfterOcean(&m_water, nullptr, nullptr);
	}
	// issue 4308: no water drawn (or it failed before its place): the ice after the terrain, before the cloud planes
	if(m_iceWanted && !m_iceDrawnFrame)
		IceHook(this, ctx, rtv, dsv);
	vppacing::PassMark(vppacing::kStDraw, &Qpc);   // ticket 0688: the arc, the extras, the path's depth
	const bool midListed = m_raMidList != nullptr;   // ticket 0753 (step 2): the arc to the trees as recorded
	if(midListed)
	{
		const int64_t q0 = Qpc();
		ctx->ExecuteCommandList(m_raMidList, FALSE);
		SafeRelease(m_raMidList);
		m_rac.midLists++;
		const double ms = QpcToMs(Qpc() - q0);
		m_rac.execMsTotal += ms;
		m_rac.execMsSec += ms;
		m_raExecFrameMs += ms;
	}
	else
	{
		// ticket 0199 part 2: the ranged-attack arc after the water and the ice, before the trees and the units (stock's pass
		// B: after Terrain, Waves and the 0x20 system, before Forest 0x80, City and Unit; arrows-and-movement-path.md 1.7),
		// into the same colour and depth; a failure is LOUD and counted, the frame stands
		arcDraw(ctx, rtv, dsv, w, h, f);
		// epic 0454, ticket 4938: the extras on the ground, after the arc and before the trees, the cities and the units (so
		// that those stand on them), into the same colour and depth; nothing without an extra in effect
		extrasDraw(ctx, rtv, dsv, w, h, f);
		// ticket 0300 part 2: the depth of the terrain, the water and the ice, for the path's models (drawn at the end of the
		// world pass but not hidden by the trees or the units, as stock's; path-models.md 7.4)
		pathSaveDepth(ctx, dsv);
	}
	// epic 0431, ticket 4554: the Screen Overlay pass and the trees after the water and the ice, before the units and the
	// cloud planes (stock's order: trees.md 11.1), with this frame's terrain camera and Options (op: the overlay pair, the fog
	// of war); failures in play are counted and LOUD by the forest scene, the frame stands
	vppacing::PassMark(vppacing::kStForest, &Qpc);   // ticket 0688
	if(const vppacing::PassTimer* pt = vppacing::CurrentPass())   // ticket 0711: the draw stage after the water (drawMidTicks)
		m_drawMidTicks = pt->t[vppacing::kStDraw] - m_drawHeadTicks - m_raRecordTicks;   // ticket 0753: the end's recording is the end's
	if(m_forest && !midListed)
		m_forest->drawStage(ctx, rtv, dsv, w, h, f.cam, *m_r, cam, op, m_fow.terrainInputs().fowAnim);
	vppacing::PassMark(vppacing::kStModels, &Qpc);   // ticket 0688
	// epic 0561, ticket 4875: the cities and landmarks the compositor dropped, after the trees and before the units (stock's
	// world pass B: Forest 0x80, then City, then Unit; arrows-and-movement-path.md 1.7), with the server's own fog (PS t7,
	// t8); failures are counted and LOUD by the module, the frame stands
	if(m_models)
		m_models->drawScene(ctx, rtv, dsv, w, h, modelsFrameTextures());
	// epic 0375, issue 4454: the units' scene pass after the water and the ice, before the cloud planes (stock draws models
	// before them, fog-of-war.md 5.4; unit-server-scene.md 7), into the sink's colour and depth; failures are counted and
	// LOUD by the unit scene, the frame stands
	vppacing::PassMark(vppacing::kStUnits, &Qpc);   // ticket 0688
	if(m_units)
	{
		m_units->drawScene(ctx, rtv, dsv, w, h, UnitsCameraOf(f), m_haveGrid ? &m_grid : nullptr);
		unitsFrameDone();   // epic 0040, issue 4753: the mode word's unit bit from this frame's readiness
	}
	vppacing::PassMark(vppacing::kStDraw, &Qpc);   // ticket 0688: the cloud planes, the path
	if(m_models)   // ticket 4875: the model bits from this frame's readiness (srv_models_mode.h)
		m_models->frameDone(Qpc());
	drawEnd(ctx, rtv, dsv, w, h, f);   // the cloud planes and the path (ticket 0753: recorded ahead or drawn here)
	m_lastStats = fs;
	if(m_lastShHave && op.shadow.srv)   // issue 4308
	{
		m_shc.boundFrames++;
		m_shBoundSec++;
	}
	else if(m_lastShHave)
		m_shc.q0Frames++;
	else
		m_shc.noneFrames++;
	if(m_lastShHave)
	{
		m_shc.generation = m_shFrame.generation;
		m_shc.quality = m_shFrame.quality;
		m_shc.strength = m_shFrame.strength;
		m_shc.numViews = m_shFrame.numViews;
		m_shc.format = m_shFrame.format;
	}
	if(fs.overlayBound)
	{
		ovlBoundFrames++;
		m_ovlBoundSec++;
	}
	else
		ovlNoneFrames++;
	drawnTotal++;
	m_drawnSec++;
	m_cpuSumSec += fs.cpuMs;
	if(fs.gpuMs >= 0)
	{
		m_gpuSumSec += fs.gpuMs;
		m_gpuMaxSec = std::max(m_gpuMaxSec, fs.gpuMs);
		m_gpuNSec++;
	}
	return ds;
}

void Scene::secondLine(char* out, size_t n)
{
	VPShellTap tb = {};
	const bool tapOk = m_hdr && SnapBlock(VPSHELL_TAP(m_hdr), tb);
	char tapLine[1600] = "bake=-";
	if(TileCacheSource* s = source())
		s->statsLine(tapLine, sizeof(tapLine));
	_snprintf_s(out, n, _TRUNCATE,
	            "terrain 1s: state=%s drawn=%d blocks=%d stale=%d hidden=%d draw_err=%d gpu_ms=%.3f gpu_max_ms=%.3f cpu_ms=%.3f "
	            "tiles=%u culled=%u nocache=%u | tap=%s load_seq=%u loads=%lld tap_err=%lld ack=%d field_err=%lld | %s | tot_drawn=%lld "
	            "tot_draw_err=%lld tot_stale=%lld tot_hidden=%lld tot_blocks=%lld",
	            m_state < 0 ? "-" : kTsNames[m_state], m_drawnSec, m_blocksSec, m_staleSec, m_hiddenSec, m_drawErrSec,
	            m_gpuNSec ? m_gpuSumSec / m_gpuNSec : -1.0, m_gpuNSec ? m_gpuMaxSec : -1.0, m_drawnSec ? m_cpuSumSec / m_drawnSec : -1.0,
	            m_lastStats.tilesDrawn, m_lastStats.tilesCulled, m_lastStats.tilesWithoutCache,
	            tapOk && tb.state < 6 ? kTapStateNames[tb.state] : "-", tapOk ? tb.loadSeq : 0, (long long)tapLoads, (long long)tapErrors,
	            m_tapLastResult, (long long)fieldErrors, tapLine, (long long)drawnTotal, (long long)drawErrTotal, (long long)staleTotal,
	            (long long)hiddenTotal, (long long)camBlocksTotal);
	{   // issue 3607
		const size_t used = strlen(out);
		if(used + 4 < n)
		{
			memcpy(out + used, " | ", 4);
			m_water.secondLine(out + used + 3, n - used - 3);
		}
	}
	{   // issue 3704
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE, " | overlay=%s bound=%d gen=%u pairs=%lld stale=%lld refused=%lld derived=%lld unmatched=%lld bad=%lld",
		            m_ovl ? OverlayStateName(m_ovl->state()) : "NONE", m_ovlBoundSec, m_ovlHave ? m_ovlFrame.generation : 0, (long long)ovlPairs,
		            (long long)(m_ovl ? m_ovl->staleCount() : 0), (long long)(m_ovl ? m_ovl->refusedCount() : 0), (long long)ovlDerivedCopies,
		            (long long)ovlUnmatchedViews, (long long)ovlBadPairs);
	}
	{   // issue 0191: the LOD of the last draw (c5r::terrain::FrameStats; terrain-surface.md 7)
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE, " | lod L=%d mip=%.0f step=%.4g tess=%.0f min=%.4f value=%.4f visible=%u",
		            m_lastStats.lodLevel, m_lastStats.lodMip, m_lastStats.lodStep, m_lastStats.tessFactorUsed, m_lastStats.lodMin,
		            m_lastStats.lodValue, m_lastStats.lodVisible);
	}
	{   // issue 0189: the pass of the last draw's tiles (pass 0 where the tile has a t8) and the spec switch FlagB
		const size_t used = strlen(out);
		// list_*: the pass of each tile of the EXE's visible list (the tiles the EXE draws), -1 when not counted
		_snprintf_s(out + used, n - used, _TRUNCATE, " | pass p0=%u p1=%u spec=%d list_p0=%d list_p1=%d list_nocache=%d list_p0_hash=%08x",
		            m_lastStats.tilesPass0, m_lastStats.tilesPass1, m_lastStats.specOn ? 1 : 0,
		            m_lastStats.listCounted ? (int)m_lastStats.listPass0 : -1, m_lastStats.listCounted ? (int)m_lastStats.listPass1 : -1,
		            m_lastStats.listCounted ? (int)m_lastStats.listNoCache : -1, m_lastStats.listCounted ? m_lastStats.listPass0Hash : 0u);
	}
	if(m_res)
	{   // issue 0371: a line of its own (the terrain line's residency part is cut at its buffer): the near pairs and what each
		// drawn tile's t0 was (c5r::terrain::CacheKind: BC1, the near pair of 256 / 512); list_*: over the EXE's visible list
		// (-1 when not counted), the census of terrain-bake-passes.md 3.5.1 / 3.5.2
		const c5r::terrain::FrameStats& t = m_lastStats;
		const c5r::bake::ResidencyStats* s = m_res ? m_res->stats() : nullptr;
		RLog("near 1s: on=%d held256=%u held512=%u drawn256=%u drawn512=%u free256=%u free512=%u tex=%u mb=%.1f upres=%lld "
		     "short=%lld bake_failed=%lld cam_moving_frames=%lld cam_settled_frames=%lld | t0 bc1=%u n256=%u n512=%u list_bc1=%d "
		     "list_n256=%d list_n512=%d list_nocache=%d",
		     m_res && m_res->nearOn() ? 1 : 0, s ? s->nearHeld256 : 0u, s ? s->nearHeld512 : 0u, s ? s->nearDrawn256 : 0u,
		     s ? s->nearDrawn512 : 0u, s ? s->nearFree256 : 0u, s ? s->nearFree512 : 0u, s ? s->nearTextures : 0u,
		     s ? s->nearBytes / 1048576.0 : 0.0, s ? (long long)s->nearUpres : 0LL, s ? (long long)s->nearShort : 0LL,
		     s ? (long long)s->nearBakeFailures : 0LL, m_res ? (long long)m_res->movingFrames() : 0LL,
		     m_res ? (long long)m_res->settledFrames() : 0LL, t.drawnKind[1], t.drawnKind[2], t.drawnKind[3],
		     t.listCounted ? (int)t.listKind[1] : -1, t.listCounted ? (int)t.listKind[2] : -1,
		     t.listCounted ? (int)t.listKind[3] : -1, t.listCounted ? (int)t.listNoCache : -1);
	}
	{   // issue 3954: a line of its own (appended here it was cut off by the host's 4,096-byte line, live 2026-11-14 23:19)
		char fl[640];   // ticket 0674: camwin= and the overlay's moves added
		m_fow.secondLine(fl, sizeof(fl));
		// ticket 0674: overlays moved into the fog's camera window this second, failed moves in total
		const size_t used = strlen(fl);
		_snprintf_s(fl + used, sizeof(fl) - used, _TRUNCATE, " ovl_moved=%d ovl_move_fail=%lld", m_ovlResampleSec, (long long)ovlResampleFails);
		m_ovlResampleSec = 0;
		{   // ticket 0793: this second's leading edges filled whole from the history / with ground it never held
			const c5r::fow::OverlayHistoryStats hs = m_ovlResample.historyStats();
			const size_t u2 = strlen(fl);
			_snprintf_s(fl + u2, sizeof(fl) - u2, _TRUNCATE, " ovl_hist=%llu ovl_hole=%llu", (unsigned long long)(hs.stripFilled - m_ovlHistFilledMark),
			            (unsigned long long)(hs.stripHoles - m_ovlHistHolesMark));
			m_ovlHistFilledMark = hs.stripFilled;
			m_ovlHistHolesMark = hs.stripHoles;
			m_ovlHistStripsMark = hs.strips;
		}
		RLog("fog 1s: %s", fl);
	}
	if(m_raOn)   // ticket 0753 (J3): record ahead, a line of its own
	{
		SceneRecordAheadCounters& r = m_rac;
		RLog("record ahead 1s: lists=%lld not_forked=%lld record_ms_mean=%.3f record_ms_max=%.3f exec_ms_mean=%.3f exec_ms_max=%.3f | "
		     "total lists=%lld not_forked=%lld finish_failed=%lld dropped=%lld timed=%lld driver_lists=%d terrain_lists=%lld "
		     "mid_lists=%lld no_prediction=%lld mispredicted=%lld end_only=%d",
		     (long long)r.listsSec, (long long)r.notForkedSec, r.listsSec ? r.recordMsSec / r.listsSec : 0.0, r.recordMsMaxSec,
		     r.listsSec ? r.execMsSec / r.listsSec : 0.0, r.execMsMaxSec, (long long)r.lists, (long long)r.notForked,
		     (long long)r.finishFailed, (long long)r.dropped, (long long)r.timedLists, m_raDriverLists ? 1 : 0,
		     (long long)r.terrainLists, (long long)r.midLists, (long long)r.noPrediction, (long long)r.mispredicted,
		     m_opt.recordAheadEndOnly ? 1 : 0);
		r.listsSec = r.notForkedSec = 0;
		r.recordMsSec = r.recordMsMaxSec = r.execMsSec = r.execMsMaxSec = 0;
	}
	if(m_units)   // epic 0375, issue 4454: a line of its own
	{
		char ul[2048];
		m_units->secondLine(ul, sizeof(ul));
		if(m_unitsMode)   // epic 0040, issue 4753: bit= and the loss counters
			m_unitsMode->appendLine(ul, sizeof(ul));
		RLog("%s", ul);
	}
	if(m_models && m_models->on())   // epic 0561, ticket 4875: a line of its own
	{
		char ml[2048];   // ticket 4882: 1536 -> 2048 (the camera and input counters)
		m_models->line(ml, sizeof(ml));
		RLog("%s", ml);
		m_models->storeLine(ml, sizeof(ml));   // ticket 0789: the model store's counters, a line of their own
		if(ml[0])
			RLog("%s", ml);
	}
	if(m_forest)   // epic 0431, ticket 4554: a line of its own
	{
		char fl[1024];
		m_forest->secondLine(fl, sizeof(fl));
		RLog("%s", fl);
	}
	{   // issue 4308: the shadow and the ice, a line of their own
		RLog("shadow 1s: shadow=%s bound=%d q=%u strength=%.4f views=%u fmt=%u gen=%u triples=%lld refused=%lld stale=%lld derived=%lld "
		     "unmatched=%lld bad=%lld frames_bound=%lld frames_q0=%lld frames_none=%lld failed=%d | ice=%s drawn=%d gen=%u load=%u chunks=%u "
		     "copies=%u draws=%u indices=%llu meshes=%lld mesh_refused=%lld draw_err=%lld frames=%lld failed=%d",
		     !m_opt.shadow ? "NONE(render_shadow=0)" : m_shSrc ? ShadowStateName(m_shSrc->state()) : "NONE", m_shBoundSec, m_shc.quality,
		     m_shc.strength, m_shc.numViews, m_shc.format, m_shc.generation, (long long)m_shc.triples,
		     (long long)(m_shSrc ? m_shSrc->refusedCount() : 0), (long long)(m_shSrc ? m_shSrc->staleCount() : 0), (long long)m_shc.derivedCopies,
		     (long long)m_shc.unmatchedViews, (long long)m_shc.badTriples, (long long)m_shc.boundFrames, (long long)m_shc.q0Frames,
		     (long long)m_shc.noneFrames, (int)m_shFailed, !m_opt.ice ? "NONE(render_ice=0)" : m_iceSrc ? IceStateName(m_iceSrc->state()) : "NONE",
		     m_iceDrawnSec, m_iceGen, m_iceLoadSeq, m_icec.chunks, m_icec.copies, m_icec.draws, (unsigned long long)m_icec.indices,
		     (long long)m_icec.meshes, (long long)m_icec.meshRefused, (long long)m_icec.drawErrors, (long long)m_icec.drawnFrames,
		     (int)m_iceFailed);
	}
	if(m_units && m_opt.unitsShadow && m_opt.shadow)   // ticket 4785: the units' shadow, a line of its own
	{
		const UnitShadow* us = m_unitShadow.get();
		RLog("unitshadow 1s: ready=%d bit=%u merged=%lld merged_total=%lld q1=%lld flag_frames=%lld not_merged=%lld mask_fails=%lld "
		     "refused=%llu targets=%llu raw=%ux%u cpu_ms_last=%.3f cpu_ms_max=%.3f cpu_ms_mean=%.3f why=%s",
		     us && us->ready() ? 1 : 0, m_unitsMode && m_unitsMode->unitShadowBit() ? 1u : 0u, (long long)m_usc.mergedSec,
		     (long long)m_usc.mergedFrames, (long long)m_usc.mergedQ1, (long long)m_usc.flagFrames, (long long)m_usc.notMerged,
		     (long long)m_usc.maskFails, (unsigned long long)(us ? us->refused : 0), (unsigned long long)(us ? us->targetsMade : 0),
		     us ? us->rawW() : 0u, us ? us->rawH() : 0u, us ? us->lastCpuMs : 0.0, us ? us->maxCpuMs : 0.0,
		     us && us->applied ? us->sumCpuMs / (double)us->applied : 0.0, m_usc.lastWhy.empty() ? "-" : m_usc.lastWhy.c_str());
		m_usc.mergedSec = 0;
	}
	m_shBoundSec = 0;
	m_iceDrawnSec = 0;
	m_ovlBoundSec = 0;
	m_drawnSec = m_drawErrSec = m_staleSec = m_hiddenSec = m_blocksSec = 0;
	m_gpuSumSec = m_gpuMaxSec = m_cpuSumSec = 0;
	m_gpuNSec = 0;
}

void Scene::totalsLine(char* out, size_t n)
{
	if(m_raOn || m_rac.lists || m_rac.finishFailed)   // ticket 0753 (J3)
		RNote("record ahead totals: lists %lld not forked %lld finish failed %lld dropped %lld timed %lld record ms %.1f exec ms %.1f "
		      "driver lists %d terrain lists %lld mid lists %lld no prediction %lld mispredicted %lld", (long long)m_rac.lists,
		      (long long)m_rac.notForked, (long long)m_rac.finishFailed, (long long)m_rac.dropped, (long long)m_rac.timedLists,
		      m_rac.recordMsTotal, m_rac.execMsTotal, m_raDriverLists ? 1 : 0, (long long)m_rac.terrainLists,
		      (long long)m_rac.midLists, (long long)m_rac.noPrediction, (long long)m_rac.mispredicted);
	if(m_units)   // epic 0375, issue 4454: a line of its own (the host's line is 4,096 bytes)
	{
		char ul[2048];
		m_units->totalsLine(ul, sizeof(ul));
		RNote("%s", ul);
		// epic 0040, issue 4753: a line of its own (the totals line is near the log's 1,536-byte body already)
		if(m_unitsMode)
		{
			char ml[1024] = "units mode totals:";
			m_unitsMode->appendLine(ml, sizeof(ml));
			RNote("%s", ml);
		}
	}
	if(m_models && m_models->on())   // epic 0561, ticket 4875
	{
		char ml[2048] = "";
		m_models->line(ml, sizeof(ml));
		RNote("totals %s", ml);
		m_models->storeLine(ml, sizeof(ml));   // ticket 0789
		if(ml[0])
			RNote("totals %s", ml);
	}
	if(m_extras)   // epic 0454, ticket 4938: a line of its own
	{
		char el[1024] = "";
		m_extras->totalsLine(el, sizeof(el));
		RNote("totals %s", el);
	}
	if(m_fow.state() != FowScene::kOff)   // ticket 0674: a line of its own (the terrain totals line is near its 4,096 bytes)
		RNote("mask window totals (0674): render_fow_mask_window %s, camera window frames %lld, footprint errors %lld, overlays moved "
		      "into the camera window %lld, move failures %lld, camera window frames without an overlay %lld",
		      m_fow.maskWindowCamera() ? "camera" : "pair", (long long)m_fow.cameraWindowFrames, (long long)m_fow.footprintErrors,
		      (long long)ovlResampled, (long long)ovlResampleFails, (long long)ovlCamWinNoOverlay);
	if(m_fow.state() != FowScene::kOff)   // ticket 0793: a line of its own
	{
		const c5r::fow::OverlayHistoryStats hs = m_ovlResample.historyStats();
		RNote("overlay history totals (0793): render_fow_overlay_history %s, offers %lld, refused %lld, absorbed %llu, skipped %llu, "
		      "remember failures %llu, clears %llu, leading edges %llu, filled whole %llu, with a hole %llu, edge cells %llu held %llu, "
		      "history %ux%u (%.3f texels per world unit)",
		      RenderFowOverlayHistoryName(m_ovlHistOn), (long long)ovlHistOffers, (long long)ovlHistFails, (unsigned long long)hs.absorbed,
		      (unsigned long long)hs.skipped, (unsigned long long)hs.rememberFails, (unsigned long long)hs.clears,
		      (unsigned long long)hs.strips, (unsigned long long)hs.stripFilled, (unsigned long long)hs.stripHoles,
		      (unsigned long long)hs.stripCells, (unsigned long long)hs.stripCellsHeld, hs.width, hs.height, hs.texelsPerUnit);
	}
	if(m_forest)   // epic 0431, ticket 4554: a line of its own
	{
		char fl[1024];
		m_forest->totalsLine(fl, sizeof(fl));
		RNote("%s", fl);
	}
	if(m_farm)   // epic 0528, ticket 4852: a line of its own
	{
		char fl[1024];
		m_farm->totalsLine(fl, sizeof(fl));
		RNote("%s farm_cut_sets=%lld", fl, (long long)m_farmCutSets);   // ticket 4853: footprints handed to the forest's cut
	}
	if(m_roads)   // ticket 4934: a line of its own
	{
		char rl[1024];
		m_roads->totalsLine(rl, sizeof(rl));
		RNote("%s", rl);
	}
	_snprintf_s(out, n, _TRUNCATE,
	            "terrain totals: drawn %lld draw errors %lld stale presents %lld hidden presents %lld camera blocks %lld tap loads %lld "
	            "tap errors %lld tap requests %lld field errors %lld cache opened %lld cache missing %lld cache refused %lld manifest errors %lld "
	            "source plots changed %lld lost plots changed %lld",
	            (long long)drawnTotal, (long long)drawErrTotal, (long long)staleTotal, (long long)hiddenTotal, (long long)camBlocksTotal,
	            (long long)tapLoads, (long long)tapErrors, (long long)tapRequests, (long long)fieldErrors,
	            (long long)(m_tapPrev[0] + (m_tap ? m_tap->cacheOpened : 0)), (long long)(m_tapPrev[1] + (m_tap ? m_tap->cacheMissing : 0)),
	            (long long)(m_tapPrev[2] + (m_tap ? m_tap->cacheRefused : 0)), (long long)(m_tapPrev[3] + (m_tap ? m_tap->manifestErrors : 0)),
	            (long long)(m_tapPrev[4] + (m_tap ? m_tap->plotsChanged : 0) + m_resPrev[5] + (m_res ? m_res->plotsChanged : 0)),
	            (long long)m_plotsChangedNoSource);
	{   // issue 3607
		const size_t used = strlen(out);
		if(used + 4 < n)
		{
			memcpy(out + used, "; ", 3);
			m_water.totalsLine(out + used + 2, n - used - 2);
		}
	}
	{   // issue 3704
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE,
		            "; overlay totals: frames with %lld without %lld pairs %lld derived copies %lld unmatched views %lld bad pairs %lld failed %d "
		            "stale episodes %lld longest %lld Presents refused %lld",
		            (long long)ovlBoundFrames, (long long)ovlNoneFrames, (long long)ovlPairs, (long long)ovlDerivedCopies,
		            (long long)ovlUnmatchedViews, (long long)ovlBadPairs, (int)m_ovlFailed,
		            (long long)(m_ovl ? m_ovl->staleCount() : 0), (long long)(m_ovl ? m_ovl->staleLongestPresents() : 0),
		            (long long)(m_ovl ? m_ovl->refusedCount() : 0));
	}
	{   // issue 3954
		const size_t used = strlen(out);
		if(used + 4 < n)
		{
			memcpy(out + used, "; ", 3);
			m_fow.totalsLine(out + used + 2, n - used - 2);
		}
	}
	{   // issue 4308
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE,
		            "; shadow totals: frames bound %lld quality 0 %lld without %lld triples %lld derived copies %lld unmatched views %lld "
		            "bad triples %lld readers %lld failed %d; ice totals: meshes %lld refused %lld frames drawn %lld draw errors %lld "
		            "generation %u readers %lld failed %d",
		            (long long)m_shc.boundFrames, (long long)m_shc.q0Frames, (long long)m_shc.noneFrames, (long long)m_shc.triples,
		            (long long)m_shc.derivedCopies, (long long)m_shc.unmatchedViews, (long long)m_shc.badTriples, (long long)m_shc.readers,
		            (int)m_shFailed, (long long)m_icec.meshes, (long long)m_icec.meshRefused, (long long)m_icec.drawnFrames,
		            (long long)m_icec.drawErrors, m_iceGen, (long long)m_icec.readers, (int)m_iceFailed);
	}
	if(m_units && m_opt.unitsShadow && m_opt.shadow)   // ticket 4785
	{
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE,
		            "; unit shadow totals: merged %lld quality 1 %lld flag frames %lld not merged %lld mask fails %lld",
		            (long long)m_usc.mergedFrames, (long long)m_usc.mergedQ1, (long long)m_usc.flagFrames, (long long)m_usc.notMerged,
		            (long long)m_usc.maskFails);
	}
	// issue 3308: the residency's totals (all sources of the run: device rebuilds start new ones)
	if(m_opt.tiles == SceneOptions::kTilesBake)
	{
		const c5r::bake::ResidencyStats* s = m_res ? m_res->stats() : nullptr;
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE,
		            "; residency totals: bakes %lld rebakes %lld failed %lld gray frames %lld gray after ahead %lld source failures %lld",
		            (long long)(m_resPrev[0] + (s ? s->bakes : 0)), (long long)(m_resPrev[1] + (s ? s->rebakes : 0)),
		            (long long)(m_resPrev[2] + (s ? s->failures : 0)), (long long)(m_resPrev[3] + (s ? s->grayFrames : 0)),
		            (long long)(m_resPrev[4] + (s ? s->grayFramesAfterAhead : 0)), (long long)m_sourceFailures);
	}
	if(m_opt.decals && m_opt.tiles == SceneOptions::kTilesBake)
	{   // issue 4105
		const DecalBakeCounters cur = m_res ? m_res->decalCounters() : DecalBakeCounters();
		const DecalBakeCounters& p = m_decalPrev;
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE,
		            "; decal totals: generation %u sets %lld refused %lld in-play faults %lld changes %lld raises %lld skipped %lld bakes with "
		            "decals %lld decal bakes failed %lld sets refused by names %lld install failure %d readers %lld",
		            m_dcount.generation, (long long)m_dcount.sets, (long long)m_dcount.refused, (long long)m_dcount.inPlayFaults,
		            (long long)m_dcount.changesDrained, (long long)(p.raises + cur.raises), (long long)(p.changesSkipped + cur.changesSkipped),
		            (long long)(p.bakesWithDecals + cur.bakesWithDecals), (long long)(p.bakesFailed + cur.bakesFailed),
		            (long long)(p.setsRefused + cur.setsRefused), (int)m_decalFailed, (long long)m_dcount.readers);
	}
}

void OverlayFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.overlayFailed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("overlay: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), kOverlayFatalExitCode);
		fatal(kOverlayFatalExitCode, why.c_str());
	}
	else
		RNote("overlay: %s; no fatal hook (HostConfig::fatal): the server draws on without the overlay", why.c_str());
}

// issue 3704: the newest pair; a new one asks for a redraw (the camera may stand still while a highlight changes)
void Scene::overlayPoll()
{
	if(!m_ovl)
		return;
	OverlayFrame f;
	const bool got = m_ovl->acquire(f) && f.srv;
	const OverlayTapReader::State st = m_ovl->state();
	if((int)st != m_ovlLoggedState)
	{
		if(st != OverlayTapReader::State::Failed)
			RNote("overlay: source state %s -> %s%s%s", m_ovlLoggedState < 0 ? "-" : OverlayStateName((OverlayTapReader::State)m_ovlLoggedState),
			      OverlayStateName(st), m_ovl->why().empty() ? "" : ": ", m_ovl->why().c_str());
		m_ovlLoggedState = (int)st;
	}
	if(st == OverlayTapReader::State::Failed && !m_ovlFailed)
	{
		// MC #701 (a), decision 5: the tap could not install; the host ends the server with this text (the fatal route)
		char b[160];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "overlay tap FAILED (VPOVL_FAILED reason %u %s): ", m_ovl->failReason(),
		            OverlayFailName(m_ovl->failReason()));
		m_ovlFailWhy = std::string(b) + (m_ovl->why().empty() ? std::string("(no text)") : m_ovl->why()) +
		               "; the borders, highlights and selection cannot be drawn (decision 5, D125)";
		m_ovlFailed = true;
		RNote("LOUD: %s", m_ovlFailWhy.c_str());
	}
	if(got)
	{
		if(!m_ovlHave || f.generation != m_ovlFrame.generation || f.srv != m_ovlFrame.srv || f.slot != m_ovlFrame.slot)
		{
			ovlPairs++;
			m_dirty = true;
		}
		m_ovlFrame = f;
		m_ovlHave = true;
	}
	else
	{
		if(m_ovlHave)
			m_dirty = true;
		m_ovlHave = false;
	}
}

void Scene::idle(ID3D11DeviceContext* ctx, double budgetMs)
{
	if(m_models)   // epic 0561, ticket 4875: more library files made drawable (the identity set grows with them)
		m_models->idle(budgetMs);
	if(!m_res || !m_rInit)
		return;
	m_res->prepareHeld(ctx, *m_r);   // ticket 0424: the visible set of a frame the fog holds (no per-frame budget)
	m_res->idle(ctx, budgetMs);
	if(m_res->takeVisibleChanged())
		m_dirty = true;
}

// ---- issue 4105: the decals (srv_render_scene.h, the block comment above DecalFatalStep) ----------------------------------

namespace {
const char* DecalStateName(uint32_t s)
{
	static const char* k[] = {"NONE", "WRITING", "READY", "FAILED", "OFF"};
	return s < 5 ? k[s] : "?";
}
const char* DecalFailName(uint32_t f)
{
	static const char* k[] = {"NONE", "PATTERN", "SEH", "LAYOUT", "TOO_BIG", "SECTION", "NAMES", "ACK_TIMEOUT"};
	return f < 8 ? k[f] : "?";
}
bool DecalLoudDue(int64_t n) { return n <= 20 || n % 100 == 0; }
}  // namespace

void Scene::decalCloseControl()
{
	if(m_decalCtl)
		UnmapViewOfFile((LPCVOID)m_decalCtl);
	m_decalCtl = nullptr;
	if(m_decalCtlMap)
		CloseHandle(m_decalCtlMap);
	m_decalCtlMap = nullptr;
}

void Scene::decalFail(const std::string& why)
{
	if(m_decalFailed)
		return;
	m_decalFailed = true;
	m_decalFailWhy = "decals cannot install: " + why +
	                 "; roads, railroads, farm fields and river beds cannot be drawn (decision 6 of epic 0166, D118, D125)";
	RNote("LOUD: %s", m_decalFailWhy.c_str());
}

void Scene::decalPoll()
{
	if(!m_opt.decals || !m_hdr || m_opt.tiles != SceneOptions::kTilesBake)
		return;
	const int64_t now = Qpc();
	// the EXE's pid: the field tap's block (written by the DLL inside the EXE at the load), before it the overlay tap's
	// (written by the composite from its first Present), so that the reader takes the generations of the load as they come
	VPShellTap tb;
	VPShellOverlayTap ob;
	uint32_t pid = SnapBlock(VPSHELL_TAP(m_hdr), tb) ? tb.exePid : 0;
	if(!pid && SnapBlock(VPSHELL_OVERLAYTAP(m_hdr), ob))
		pid = ob.exePid;
	if(pid && pid != m_decalPid)
	{
		if(m_decalPid)
			RNote("decals: the EXE's pid changed (%u -> %u): a new reader", m_decalPid, pid);
		m_decalReader.reset(new DecalTapReader(pid));
		decalCloseControl();
		m_decalCtlNextOpen = 0;
		m_decalPid = pid;
		m_decalRefusedSeen = 0;
		m_decalLoggedState = m_decalLoggedFail = 0xFFFFFFFFu;
		m_dcount.readers++;
		RNote("decals: reader for EXE pid %u (control section Local\\VPShellDecalTap.%u), install bound %.0f ms", m_decalPid, m_decalPid,
		      m_opt.decalWaitMs);
	}
	VPShellDecalTapControl cb = {};
	bool haveBlock = false;
	if(m_decalReader)
	{
		const bool got = m_decalReader->poll();
		// the control block's state and fail reason (a read-only view of our own, looked for every 250 ms while absent)
		if(!m_decalCtl && now >= m_decalCtlNextOpen)
		{
			wchar_t name[64];
			_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_DECALTAP_CONTROL_FMT, m_decalPid);
			m_decalCtlMap = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
			m_decalCtl = m_decalCtlMap ? (const VPShellDecalTapControl*)MapViewOfFile(m_decalCtlMap, FILE_MAP_READ, 0, 0,
			                                                                         sizeof(VPShellDecalTapControl))
			                           : nullptr;
			if(!m_decalCtl)
			{
				decalCloseControl();
				m_decalCtlNextOpen = now + QpcFreq() / 4;
			}
		}
		haveBlock = m_decalCtl && SnapBlock(m_decalCtl, cb) && cb.magic == VPSHELL_DECAL_MAGIC;
		if(haveBlock)
		{
			m_dcount.state = cb.state;
			m_dcount.failReason = cb.failReason;
			if(cb.state != m_decalLoggedState || (cb.state == VPDTAP_FAILED && cb.failReason != m_decalLoggedFail))
			{
				const bool install = cb.state == VPDTAP_FAILED && (cb.failReason == VPDTAP_FAIL_PATTERN || cb.failReason == VPDTAP_FAIL_SEH);
				if(cb.state == VPDTAP_FAILED && !install)
				{
					m_dcount.inPlayFaults++;
					RNote("LOUD: decals: in play: the tap says FAILED %s (generation %u in force, the tap retries): the last good set "
					      "stays (%lld in-play faults so far)",
					      DecalFailName(cb.failReason), m_dcount.generation, (long long)m_dcount.inPlayFaults);
				}
				else
					RNote("decals: tap state %s%s%s (generation %u, load %u)", DecalStateName(cb.state), cb.state == VPDTAP_FAILED ? " " : "",
					      cb.state == VPDTAP_FAILED ? DecalFailName(cb.failReason) : "", cb.generation, cb.loadSeq);
				m_decalLoggedState = cb.state;
				m_decalLoggedFail = cb.failReason;
			}
			if(cb.state == VPDTAP_FAILED && (cb.failReason == VPDTAP_FAIL_PATTERN || cb.failReason == VPDTAP_FAIL_SEH))
				decalFail(std::string("the decal tap is FAILED ") + DecalFailName(cb.failReason) +
				          (cb.failReason == VPDTAP_FAIL_PATTERN ? " (the EXE's byte patterns do not match: this EXE cannot be read)"
				                                                 : " (an access fault while reading the EXE: the tap is off for the life of the EXE)"));
			if(cb.state == VPDTAP_OFF && !m_decalOffLogged)
			{
				m_decalOffLogged = true;
				RNote("LOUD: decals: the tap is OFF (the composite's decal_tap=0, a test switch): no decals are drawn");
			}
		}
		if(got)
		{
			const std::shared_ptr<const c5r::decal::DecalSet> cur = m_decalReader->current();
			if(cur)
			{
				m_dcount.sets++;
				m_dcount.generation = cur->generation;
				m_decalEverSet = true;
				if(haveBlock && cb.generation == cur->generation && cb.readyQpc)
					m_dcount.lastReadyToAppliedMs = QpcToMs(now - cb.readyQpc);
				RNote("decals: generation %u applied by the reader (published at qpc %.3f ms, %.1f ms before)", cur->generation,
				      haveBlock && cb.generation == cur->generation ? QpcToMs(cb.readyQpc) : -1.0, m_dcount.lastReadyToAppliedMs);
				std::string why;
				if(m_res && !m_res->setDecals(composedDecals(cur), why))   // epic 0431: with the Tree_Shadow decals
					decalFail(why);
			}
		}
		// in play: images the reader refused (torn, hash, a bad record): the last good set stays
		const DecalTapCounters c = m_decalReader->counters();
		if(c.refused > m_decalRefusedSeen)
		{
			m_dcount.inPlayFaults += (int64_t)(c.refused - m_decalRefusedSeen);
			m_dcount.refused = (int64_t)c.refused;
			m_decalRefusedSeen = c.refused;
			if(DecalLoudDue(m_dcount.refused))
				RNote("LOUD: decals: in play: an image was refused (ackResult %d: %s); the last good set (generation %u) stays (%lld refused so far)",
				      m_decalReader->lastResult(), m_decalReader->lastWhy().c_str(), m_dcount.generation, (long long)m_dcount.refused);
		}
		// the changes, once the residency has a field; until then they wait in the reader's queue (bounded)
		if(m_res && m_res->fieldReady() && m_decalReader->drainChanges(m_decalChanges))
		{
			m_dcount.changesDrained += (int64_t)m_decalChanges.size();
			const double signal = haveBlock && cb.readyQpc ? QpcToMs(cb.readyQpc) : QpcToMs(now);
			m_res->decalChanges(m_decalChanges, signal);
			m_decalChanges.clear();
		}
	}
	// install: a decal set within decalWaitMs after the first field with a valid camera grid (not with the tap OFF).
	// Ticket 0491 (fog-of-war.md 7.6): the wait counts from the EXE's progress, not only from the field. On 9074's huge late
	// loads the EXE is still loading after the map's first frame: its first map scene (the overlay tap's first copy of this
	// load) came 12.4-13.7 s after it, and the decal tap's builds restarted while the EXE's decal batches still moved; T1100
	// failed here 20.0 s after the field twice (runs a1, d1), and with a long bound (d2) the first set came 22.7 s after the
	// field, 9.0 s after the first map scene. So with an overlay source decalWaitMs counts from the EXE's first map scene of
	// this load (a pair of another generation than at the wait's start); until that scene comes the wait lasts up to
	// decalWaitMs * kDecalSlowFactor from the field (120 s at the product's 20 s), one note when decalWaitMs passes. Without an
	// overlay source the wait counts from the field as before. A tap that never publishes still fails: decalWaitMs after the
	// first map scene, or at the bound. The wait's record lives here per Scene (Scene's members are in srv_render_scene.h,
	// outside 0491's files: move it there when that header is next changed).
	if(!m_decalEverSet && !m_decalFailed && m_haveField)
	{
		static const double kDecalSlowFactor = 6.0;
		struct WaitProgress
		{
			uint32_t pairGen0 = 0;   // the overlay pair's generation when the wait began (0: none)
			int64_t sceneFrom = 0;   // the qpc the EXE's first map scene of this load was seen (0: not yet)
			bool noted = false;      // the slow-load note of this wait was written
		};
		static std::mutex s_waitLock;
		static std::map<const Scene*, WaitProgress> s_wait;
		WaitProgress* wp = nullptr;
		{
			std::lock_guard<std::mutex> g(s_waitLock);
			wp = &s_wait[this];   // node-based: the address stays valid
		}
		VPShellCamera cam;
		if(!m_decalWaitFrom && SnapCamera(m_hdr, cam) && cam.gridValid == 1)
		{
			m_decalWaitFrom = now;
			*wp = WaitProgress();
			wp->pairGen0 = m_ovlHave ? m_ovlFrame.generation : 0;
		}
		if(m_decalWaitFrom && m_ovl && !wp->sceneFrom && m_ovlHave && m_ovlFrame.generation != wp->pairGen0)
		{
			wp->sceneFrom = now;
			if(wp->noted)
				RNote("decals: the EXE drew its first map scene of the load %.0f ms after the field (overlay pair generation %u): the "
				      "decal wait's %.0f ms count from here (ticket 0491)", QpcToMs(now - m_decalWaitFrom), m_ovlFrame.generation,
				      m_opt.decalWaitMs);
		}
		const double waited = m_decalWaitFrom ? QpcToMs(now - m_decalWaitFrom) : 0.0;
		const double slowMs = m_opt.decalWaitMs * kDecalSlowFactor;
		// the wait's own start: the first map scene with an overlay source, else the field
		const bool counting = !m_ovl || wp->sceneFrom != 0;
		const double sinceStart = !m_ovl ? waited : (wp->sceneFrom ? QpcToMs(now - wp->sceneFrom) : 0.0);
		if(m_decalWaitFrom && !(haveBlock && cb.state == VPDTAP_OFF) &&
		   ((counting && sinceStart > m_opt.decalWaitMs) || waited > slowMs))
		{
			char b[700];
			_snprintf_s(b, sizeof(b), _TRUNCATE,
			            "no decal set within %.0f ms after the load (EXE pid %u; control block %s, state %s %s, generation %u; the reader: "
			            "%llu refused, last: %s; %.0f ms after the field, %s; the slow-load bound %.0f ms, ticket 0491)",
			            m_opt.decalWaitMs, m_decalPid, haveBlock ? "present" : "absent (a composite without the decal tap?)",
			            haveBlock ? DecalStateName(cb.state) : "-", haveBlock && cb.state == VPDTAP_FAILED ? DecalFailName(cb.failReason) : "",
			            haveBlock ? cb.generation : 0, m_decalReader ? (unsigned long long)m_decalReader->counters().refused : 0ull,
			            m_decalReader && !m_decalReader->lastWhy().empty() ? m_decalReader->lastWhy().c_str() : "-", waited,
			            !m_ovl ? "counted from the field (no overlay source)"
			                   : (wp->sceneFrom ? "counted from the EXE's first map scene of the load" : "the EXE drew no map scene of the load"),
			            slowMs);
			decalFail(b);
		}
		else if(m_decalWaitFrom && m_ovl && !wp->sceneFrom && !wp->noted && waited > m_opt.decalWaitMs)
		{
			wp->noted = true;
			RNote("decals: no decal set %.0f ms after the field, and the EXE has drawn no map scene of the load yet (its load goes on): "
			      "the %.0f ms count from that scene, at most %.0f ms from the field (decalWaitMs x %.0f; ticket 0491)",
			      waited, m_opt.decalWaitMs, slowMs, kDecalSlowFactor);
		}
	}
}

// epic 0431, ticket 4554 (trees.md 13.8): the forest's Tree_Shadow decals built or dropped since the last call. The
// residency gets the composed set first, then the change (P13); the change's generation lies in the forest's band
// (kForestDecalGenBase + n), which ResidencySource::decalChanges never skips. Without a residency or a tap set the change
// is dropped: the decals join the set when the tap's first set arrives (decalPoll composes it), and a new field bakes
// every tile from the set in force.
void Scene::forestDecalPoll()
{
	if(!m_forest)
		return;
	std::vector<DecalChange> fch;
	if(!m_forest->takeDecalChanges(fch))
		return;
	const std::shared_ptr<const c5r::decal::DecalSet> cur = m_decalReader ? m_decalReader->current() : nullptr;
	if(!m_res || !cur)
		return;
	std::string why;
	if(!m_res->setDecals(composedDecals(cur), why))
	{
		decalFail(why);   // a name the install cannot resolve: as decalPoll's refusal
		return;
	}
	if(m_res->fieldReady())
		m_res->decalChanges(fch, QpcToMs(Qpc()));
	m_dirty = true;
}

// epic 0528, ticket 4852 (farm-fields-layout.md 13): the farm producer's step (ids, farm_types.xml, a finished
// production from its worker, a new input to it), then as forestDecalPoll: the composed set to the residency, then the
// change in the producer's band (kFarmDecalGenBase + n). A production that changed the decals without a change to send
// (the first one of a load before the residency has a field) still hands the residency the composed set.
void Scene::farmDecalPoll()
{
	if(!m_farm)
		return;
	const bool changed = m_farm->refresh();
	if(m_farm->footprintVersion() != m_farmCutSeen)
	{   // ticket 4853 (farm-fields-layout.md 10.1, 14): the fields' footprints join the forest's cut source (bit 0x01, as the
		// EXE's farm manager stamps them); the forest's next poll sees the cut generation move and rebuilds its built plots
		m_farmCutSeen = m_farm->footprintVersion();
		CutOverlaySlot().set(m_farm->footprint());
		m_farmCutSets++;
	}
	std::vector<DecalChange> fch;
	const bool any = m_farm->takeDecalChanges(fch);
	if(!changed && !any)
		return;
	const std::shared_ptr<const c5r::decal::DecalSet> cur = m_decalReader ? m_decalReader->current() : nullptr;
	if(!m_res || !cur)
		return;
	std::string why;
	if(!m_res->setDecals(composedDecals(cur), why))
	{
		decalFail(why);   // a name the install cannot resolve: as decalPoll's refusal
		return;
	}
	if(any && m_res->fieldReady())
		m_res->decalChanges(fch, QpcToMs(Qpc()));
	m_dirty = true;
}

// ticket 4934 (docs/extras/roads.md 4b): the road producer's step, as farmDecalPoll: a finished production from its worker,
// a new input to it, then the composed set to the residency and the change in the roads' band (kRoadDecalGenBase + n).
void Scene::roadsDecalPoll()
{
	if(!m_roads)
		return;
	const bool changed = m_roads->refresh();
	if(m_roads->cutVersion() != m_roadsCutSeen)
	{   // MC #7664 option (A): our roads' cells cut the trees (bit 0x02, ORed in) and the EXE's own route cells on bare plots
		// are cleared from its grid; the forest's next poll sees the cut generation move and rebuilds its built plots
		m_roadsCutSeen = m_roads->cutVersion();
		CutOverlaySlot().setRoads(m_roads->footprint(), m_roads->stockClear());
	}
	std::vector<srvextras::RoadsChange> rch;
	const bool any = m_roads->takeDecalChanges(rch);
	if(!changed && !any)
		return;
	const std::shared_ptr<const c5r::decal::DecalSet> cur = m_decalReader ? m_decalReader->current() : nullptr;
	if(!m_res || !cur)
		return;
	std::string why;
	if(!m_res->setDecals(composedDecals(cur), why))
	{
		decalFail(why);   // a name the install cannot resolve: as decalPoll's refusal
		return;
	}
	if(any && m_res->fieldReady())
	{
		std::vector<DecalChange> dch;
		for(srvextras::RoadsChange& r : rch)
		{
			DecalChange c;
			c.generation = r.generation;
			c.previousGeneration = r.previousGeneration;
			memcpy(c.worldRect, r.worldRect, sizeof(c.worldRect));
			c.rects = std::move(r.rects);
			c.areaTiles = std::move(r.areaTiles);
			dch.push_back(std::move(c));
		}
		m_res->decalChanges(dch, QpcToMs(Qpc()));
	}
	m_dirty = true;
}

c5r::Status Scene::loadDecalImage(const uint8_t* image, size_t n, std::string* why)
{
	if(!m_res)
	{
		if(why)
			*why = "no residency (render_tiles=bake with render_decals=1 is needed for decals)";
		return c5r::Status::fail(c5r::Err::NotFound, "no residency");
	}
	if(!m_decalReader)
		m_decalReader.reset(new DecalTapReader(0));   // a reader without a tap: only its file entry is used
	const c5r::Status st = m_decalReader->loadImage(image, n);
	if(!st.ok())
	{
		if(why)
			*why = "the decal reader refuses the image: " + st.str();
		return st;
	}
	const std::shared_ptr<const c5r::decal::DecalSet> cur = m_decalReader->current();
	m_dcount.sets++;
	m_dcount.generation = cur ? cur->generation : 0;
	m_decalEverSet = cur != nullptr;
	std::string w;
	if(!m_res->setDecals(composedDecals(cur), w))   // epic 0431: with the Tree_Shadow decals
	{
		if(why)
			*why = w;
		return c5r::Status::fail(c5r::Err::BadLayout, w);
	}
	if(m_decalReader->drainChanges(m_decalChanges))
	{
		if(m_res->fieldReady())
		{
			m_dcount.changesDrained += (int64_t)m_decalChanges.size();
			m_res->decalChanges(m_decalChanges, QpcToMs(Qpc()));
		}
		m_decalChanges.clear();
	}
	RNote("decals: generation %u (load %u) applied from a file (%zu bytes): %zu decals", m_dcount.generation, cur ? cur->loadSeq : 0u, n,
	      cur ? cur->decals.size() : (size_t)0);
	return c5r::Status::Ok();
}

void DecalFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.decalFailed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("decals: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), kDecalFatalExitCode);
		fatal(kDecalFatalExitCode, why.c_str());
	}
	else
		RNote("decals: %s; no fatal hook (HostConfig::fatal): the server draws on without new decals", why.c_str());
}

// ---- epic 0301, issue 3903: the height field's file entry and the settle call (srv_render_scene.h) ---------------------
c5r::Status Scene::loadField(const uint8_t* c5hf, size_t bytes, uint32_t loadSeq, const VPShellCamera* cam)
{
	const bool haveCam = cam && cam->gridValid == 1;
	if(haveCam && cam->gridSeq != loadSeq)
		return c5r::Status::fail(c5r::Err::BadLayout, "stale: the camera block's grid seq is " + std::to_string(cam->gridSeq) +
		                                                  ", the field's load seq " + std::to_string(loadSeq));
	if(!c5hf || bytes < c5h::kTapHeaderSize)
		return c5r::Status::fail(c5r::Err::Truncated, "payload " + std::to_string(bytes) + " bytes, below the C5HF header");
	c5h::HeightGrid g;
	const c5r::Status st = c5h::readTap(c5hf, bytes, g);
	if(!st.ok())
		return st;
	if(haveCam && (cam->samplesX != g.samplesX || cam->samplesY != g.samplesY))
		return c5r::Status::fail(c5r::Err::BadLayout, "map size: C5HF " + std::to_string(g.samplesX) + "x" + std::to_string(g.samplesY) +
		                                                  " samples, the camera block's grid " + std::to_string(cam->samplesX) + "x" +
		                                                  std::to_string(cam->samplesY));
	RNote("terrain: recorded field of load seq %u loaded: %ux%u plots, %llu bytes", loadSeq, g.mapW, g.mapH, (unsigned long long)bytes);
	if(m_res)
		m_res->prepJoin();   // ticket 0712: the bake's prep worker reads m_grid until it is joined
	m_grid = std::move(g);
	m_haveGrid = true;
	m_fieldLoadSeq = loadSeq;
	if(!applyField(wantTilePlots()))
		return c5r::Status::fail(c5r::Err::NotFound, "the field was not applied (renderer not initialised or setHeightField failed; logged)");
	return c5r::Status::Ok();
}

c5r::Status Scene::settle(ID3D11DeviceContext* ctx, const SceneFrame& f, uint32_t w, uint32_t h, double maxMs, SettleResult* out)
{
	if(!out)
		return c5r::Status::fail(c5r::Err::BadLayout, "settle: no result");
	*out = SettleResult();
	if(!m_res)
	{
		out->settled = true;   // render_tiles=none or tap: nothing of the server's own to wait for
		out->what = m_tap ? "tile source: the EXE's caches (tap), nothing to settle" : "no tile source, nothing to settle";
		return c5r::Status::Ok();
	}
	if(!m_rInit)
		return c5r::Status::fail(c5r::Err::NotFound, "settle: renderer not initialised");
	ViewInfo vi;
	vi.frameId = f.frameId;
	memcpy(vi.camWorld, f.cam.camWorld, sizeof(vi.camWorld));
	memcpy(vi.view, f.cam.view[0], sizeof(vi.view));
	memcpy(vi.proj, f.cam.proj, sizeof(vi.proj));
	vi.width = w;
	vi.height = h;
	return m_res->settle(ctx, *m_r, vi, maxMs, out);
}

// ---- epic 0301, issue 3911: decide()'s fog step for the offline player (srv_render_scene.h fowAdmit) ------------------
bool Scene::fowAdmit(const VPShellCamera& cam, double nowMs)
{
	const OverlayTapReader::State os = m_ovl ? m_ovl->state() : OverlayTapReader::State::Absent;
	return m_fow.admit(cam, m_grid.mapW, m_grid.mapH, m_ovl ? &os : nullptr, m_ovlHave ? &m_ovlFrame : nullptr, nowMs);
}

bool Scene::cameraPending() const
{
	if(!m_hdr)
		return false;
	const uint32_t s = *(const volatile uint32_t*)&VPSHELL_CAMERA(m_hdr)->seq;
	return s != 0 && !(s & 1) && s != m_lastCamSeq;
}

// ---- issue 4308 (epic 0169): the models' shadow and the polar ice (srv_render_scene.h, the block comment above
// ShadowSource; terrain-surface.md 11.10, polar-ice.md 10) ----------------------------------------------------------------

namespace {
class TapShadowSource : public ShadowSource
{
public:
	TapShadowSource(uint32_t pid, VPShellHeader* hdr, ID3D11Device* dev) : m_r(pid, hdr, dev) {}
	bool acquire(ShadowFrame& out) override { return m_r.acquire(out); }
	ShadowTapReader::State state() const override { return m_r.state(); }
	uint32_t failReason() const override { return m_r.failReason(); }
	std::string why() const override { return m_r.lastWhy(); }
	int64_t refusedCount() const override { return m_r.refused; }
	int64_t staleCount() const override { return m_r.stale; }

private:
	ShadowTapReader m_r;
};

class TapIceSource : public IceSource
{
public:
	TapIceSource(uint32_t pid, VPShellHeader* hdr) : m_r(pid, hdr) {}
	bool poll(uint32_t have) override { return m_r.poll(have); }
	std::shared_ptr<const c5r::ice::Mesh> mesh() const override { return m_r.mesh(); }
	uint32_t generation() const override { return m_r.generation(); }
	uint32_t loadSeq() const override { return m_r.loadSeq(); }
	IceTapReader::State state() const override { return m_r.state(); }
	bool installFailure() const override { return m_r.installFailure(); }
	std::string why() const override { return m_r.lastWhy(); }
	int64_t refusedCount() const override { return m_r.refused; }

private:
	IceTapReader m_r;
};

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
	dev->CreateShaderResourceView(t, nullptr, &v);
	t->Release();
	return v;
}
}  // namespace

std::unique_ptr<ShadowSource> MakeTapShadowSource(uint32_t exePid, VPShellHeader* hdr, ID3D11Device* dev)
{
	return std::unique_ptr<ShadowSource>(new TapShadowSource(exePid, hdr, dev));
}

std::unique_ptr<IceSource> MakeTapIceSource(uint32_t exePid, VPShellHeader* hdr)
{
	return std::unique_ptr<IceSource>(new TapIceSource(exePid, hdr));
}

namespace {
class StaticIceSource : public IceSource
{
public:
	std::shared_ptr<const c5r::ice::Mesh> m;
	uint32_t gen = 1, load = 0;
	bool poll(uint32_t have) override { return m && gen != have; }
	std::shared_ptr<const c5r::ice::Mesh> mesh() const override { return m; }
	uint32_t generation() const override { return gen; }
	uint32_t loadSeq() const override { return load; }
	IceTapReader::State state() const override { return IceTapReader::State::Active; }
	bool installFailure() const override { return false; }
	std::string why() const override { return "render_test_ice_file (a test key)"; }
};
}  // namespace

std::unique_ptr<IceSource> MakeStaticIceSource(std::shared_ptr<const c5r::ice::Mesh> mesh, uint32_t generation, uint32_t loadSeq)
{
	std::unique_ptr<StaticIceSource> s(new StaticIceSource());
	s->m = mesh;
	s->gen = generation ? generation : 1;
	s->load = loadSeq;
	return std::unique_ptr<IceSource>(s.release());
}

const char* ShadowStateName(ShadowTapReader::State s)
{
	switch(s)
	{
	case ShadowTapReader::State::Absent: return "ABSENT";
	case ShadowTapReader::State::Off: return "OFF";
	case ShadowTapReader::State::Waiting: return "WAITING";
	case ShadowTapReader::State::Active: return "ACTIVE";
	case ShadowTapReader::State::Failed: return "FAILED";
	}
	return "?";
}

const char* IceStateName(IceTapReader::State s)
{
	switch(s)
	{
	case IceTapReader::State::Absent: return "ABSENT";
	case IceTapReader::State::Off: return "OFF";
	case IceTapReader::State::Waiting: return "WAITING";
	case IceTapReader::State::Active: return "ACTIVE";
	case IceTapReader::State::Failed: return "FAILED";
	}
	return "?";
}

bool ShadowCopiesFromFrame(const ShadowFrame& f, float wrapPeriod, ShadowCopies& out, std::string& why)
{
	out = ShadowCopies();
	if(f.numViews == 0 || f.numViews > VPSHELL_SHADOW_MAX_VIEWS)
	{
		why = "numViews " + std::to_string(f.numViews) + " outside 1.." + std::to_string(VPSHELL_SHADOW_MAX_VIEWS);
		return false;
	}
	for(uint32_t v = 0; v < f.numViews; ++v)
		if(!Finite16(f.matrices[v]))
		{
			why = "the ShadowMatrix0 of view " + std::to_string(v) + " is not finite";
			return false;
		}
	const float* m0 = f.matrices[0];
	memcpy(out.matrix[1], m0, sizeof(out.matrix[1]));
	const bool wraps = std::isfinite(wrapPeriod) && wrapPeriod > 0;
	// row 0's largest element estimates k W from row 3's move (terrain-surface.md 11 S3: row 3 moved by k W times row 0)
	int jMax = 0;
	for(int j = 1; j < 4; ++j)
		if(std::fabs(m0[j]) > std::fabs(m0[jMax]))
			jMax = j;
	for(uint32_t v = 1; v < f.numViews; ++v)
	{
		const float* p = f.matrices[v];
		int k = 0;
		if(wraps && m0[jMax] != 0.0f)
		{
			double scale = 0;
			for(int i = 0; i < 16; ++i)
				scale = std::max(scale, std::max(std::fabs((double)m0[i]), std::fabs((double)p[i])));
			const double tol = 1e-5 * scale;
			const double d = ((double)p[12 + jMax] - (double)m0[12 + jMax]) / (double)m0[jMax];   // world units
			const double kk = std::floor(d / wrapPeriod + 0.5);
			bool same = kk == 1.0 || kk == -1.0;
			for(int i = 0; same && i < 12; ++i)
				same = std::fabs((double)p[i] - (double)m0[i]) <= tol;
			for(int j = 0; same && j < 4; ++j)
				same = std::fabs((double)p[12 + j] - ((double)m0[12 + j] + kk * wrapPeriod * (double)m0[j])) <=
				       std::max(tol, 1e-5 * std::fabs(kk * wrapPeriod * (double)m0[j]));
			if(same)
				k = (int)kk;
		}
		if(k == 0 || out.fromExe[k + 1])
		{
			out.unmatchedViews++;
			continue;
		}
		memcpy(out.matrix[k + 1], p, sizeof(out.matrix[k + 1]));
		out.fromExe[k + 1] = true;
	}
	for(int k = -1; k <= 1; k += 2)
		if(!out.fromExe[k + 1])
			MoveX(m0, wraps ? (double)k * wrapPeriod : 0.0, out.matrix[k + 1]);
	return true;
}

uint32_t Scene::exePid() const
{
	if(!m_hdr)
		return 0;
	VPShellTap tb;
	VPShellOverlayTap ob;
	uint32_t pid = SnapBlock(VPSHELL_TAP(m_hdr), tb) ? tb.exePid : 0;
	if(!pid && SnapBlock(VPSHELL_OVERLAYTAP(m_hdr), ob))
		pid = ob.exePid;
	return pid;
}

void Scene::shadowFail(const std::string& why)
{
	if(m_shFailed)
		return;
	m_shFailed = true;
	m_shFailWhy = "the models' shadow cannot install: " + why +
	              "; the shadows of units, cities and improvements on the ground cannot be drawn (epic 0169, D118, D125)";
	RNote("LOUD: %s", m_shFailWhy.c_str());
}

void Scene::iceFail(const std::string& why)
{
	if(m_iceFailed)
		return;
	m_iceFailed = true;
	m_iceFailWhy = "the polar ice cannot install: " + why + "; the ice at the poles cannot be drawn (epic 0169, D118, D125)";
	RNote("LOUD: %s", m_iceFailWhy.c_str());
}

void Scene::shadowPoll()
{
	if(!m_opt.shadow)
		return;
	const int64_t now = Qpc();
	if(!m_shExternal && m_dev)
	{
		const uint32_t pid = exePid();
		if(pid && pid != m_shPid)
		{
			if(m_shPid)
				RNote("shadow: the EXE's pid changed (%u -> %u): a new reader", m_shPid, pid);
			m_shSrc = MakeTapShadowSource(pid, m_hdr, m_dev);
			m_shPid = pid;
			m_shHave = false;
			m_shLoggedState = -1;
			m_shRefusedSeen = 0;
			m_shc.readers++;
			RNote("shadow: reader for EXE pid %u (section Local\\VPShellShadowTap.%u), install bound %.0f ms", pid, pid, m_opt.shadowWaitMs);
		}
	}
	if(!m_shSrc)
		return;
	ShadowFrame f;
	const bool got = m_shSrc->acquire(f);
	const ShadowTapReader::State st = m_shSrc->state();
	if((int)st != m_shLoggedState)
	{
		if(st != ShadowTapReader::State::Failed)
			RNote("shadow: source state %s -> %s%s%s", m_shLoggedState < 0 ? "-" : ShadowStateName((ShadowTapReader::State)m_shLoggedState),
			      ShadowStateName(st), m_shSrc->why().empty() ? "" : ": ", m_shSrc->why().c_str());
		m_shLoggedState = (int)st;
	}
	if(st == ShadowTapReader::State::Failed)
		shadowFail(m_shSrc->why().empty() ? std::string("the shadow tap is FAILED (no text)") : m_shSrc->why());
	if(st == ShadowTapReader::State::Off && !m_shOffLogged)
	{
		m_shOffLogged = true;
		RNote("LOUD: shadow: the tap is OFF (the composite's shadow_tap=0, a test switch): no models' shadows are drawn");
	}
	// in play: slots the reader refused (its own LOUD line names each); the last triple stays (D132)
	if(m_shSrc->refusedCount() > m_shRefusedSeen)
		m_shRefusedSeen = m_shSrc->refusedCount();
	if(got)
	{
		if(!m_shHave || f.generation != m_shFrame.generation || f.srv != m_shFrame.srv || f.slot != m_shFrame.slot)
		{
			m_shc.triples++;
			m_dirty = true;
		}
		m_shFrame = f;
		m_shHave = true;
	}
	else
	{
		if(m_shHave)
			m_dirty = true;
		m_shHave = false;
	}
	// install: a section within shadowWaitMs after the first field with a valid camera grid (Absent = no section)
	if(m_opt.shadowWaitMs > 0 && !m_shFailed && !m_shExternal && m_haveField && st == ShadowTapReader::State::Absent)
	{
		VPShellCamera cam;
		if(!m_shWaitFrom && m_hdr && SnapCamera(m_hdr, cam) && cam.gridValid == 1)
			m_shWaitFrom = now;
		if(m_shWaitFrom && QpcToMs(now - m_shWaitFrom) > m_opt.shadowWaitMs)
		{
			char b[200];
			_snprintf_s(b, sizeof(b), _TRUNCATE, "no shadow tap section within %.0f ms after the load (EXE pid %u; a composite without the shadow tap?)",
			            m_opt.shadowWaitMs, m_shPid);
			shadowFail(b);
		}
	}
	else if(st != ShadowTapReader::State::Absent)
		m_shWaitFrom = 0;
}

void Scene::iceApply()
{
	if(!m_iceReady || !m_iceMesh)
		return;
	const c5r::Status st = m_ice->setMesh(*m_iceMesh);
	if(!st.ok())
	{
		m_icec.meshRefused++;
		RNote("LOUD: ice: in play: the module refused the mesh of generation %u: %s (%lld refused so far)", m_iceGen, st.str().c_str(),
		      (long long)m_icec.meshRefused);
	}
}

void Scene::icePoll()
{
	if(!m_opt.ice)
		return;
	const int64_t now = Qpc();
	if(!m_iceExternal)
	{
		const uint32_t pid = exePid();
		if(pid && pid != m_icePid)
		{
			if(m_icePid)
				RNote("ice: the EXE's pid changed (%u -> %u): a new reader", m_icePid, pid);
			m_iceSrc = MakeTapIceSource(pid, m_hdr);
			m_icePid = pid;
			m_iceSrcGen = 0;
			m_iceLoggedState = -1;
			m_icec.readers++;
			RNote("ice: reader for EXE pid %u, install bound %.0f ms (0: none)", pid, m_opt.iceWaitMs);
		}
	}
	if(!m_iceSrc)
		return;
	const bool got = m_iceSrc->poll(m_iceSrcGen);
	const IceTapReader::State st = m_iceSrc->state();
	if((int)st != m_iceLoggedState)
	{
		if(st != IceTapReader::State::Failed)
			RNote("ice: source state %s -> %s%s%s", m_iceLoggedState < 0 ? "-" : IceStateName((IceTapReader::State)m_iceLoggedState),
			      IceStateName(st), m_iceSrc->why().empty() ? "" : ": ", m_iceSrc->why().c_str());
		m_iceLoggedState = (int)st;
	}
	if(m_iceSrc->installFailure() || st == IceTapReader::State::Failed)
		iceFail(m_iceSrc->why().empty() ? std::string("the ice tap is FAILED (no text)") : m_iceSrc->why());
	if(st == IceTapReader::State::Off && !m_iceOffLogged)
	{
		m_iceOffLogged = true;
		RNote("LOUD: ice: the tap is OFF (the composite's ice_tap=0, a test switch): no polar ice is drawn");
	}
	if(got)
	{
		const std::shared_ptr<const c5r::ice::Mesh> mesh = m_iceSrc->mesh();
		const uint32_t gen = m_iceSrc->generation();
		m_iceSrcGen = gen;   // asked once: a mesh the module refuses is not offered again
		if(mesh)
		{
			c5r::Status st2 = c5r::Status::Ok();
			if(m_iceReady)
				st2 = m_ice->setMesh(*mesh);
			if(st2.ok())
			{
				m_iceMesh = mesh;
				m_iceGen = gen;
				m_iceLoadSeq = m_iceSrc->loadSeq();
				m_icec.meshes++;
				m_icec.generation = gen;
				m_icec.chunks = (uint32_t)mesh->chunks.size();
				m_dirty = true;
				RNote("ice: mesh of generation %u (load %u) applied: %zu chunks, VB %zu B, IB %zu B", gen, m_iceLoadSeq, mesh->chunks.size(),
				      mesh->vb.size(), mesh->ib.size());
			}
			else
			{
				m_icec.meshRefused++;
				RNote("LOUD: ice: in play: the module refused the mesh of generation %u: %s; the last good mesh (generation %u) stays (%lld refused so far)",
				      gen, st2.str().c_str(), m_iceGen, (long long)m_icec.meshRefused);
			}
		}
	}
	if(m_opt.iceWaitMs > 0 && !m_iceFailed && !m_iceExternal && m_haveField && st == IceTapReader::State::Absent)
	{
		VPShellCamera cam;
		if(!m_iceWaitFrom && m_hdr && SnapCamera(m_hdr, cam) && cam.gridValid == 1)
			m_iceWaitFrom = now;
		if(m_iceWaitFrom && QpcToMs(now - m_iceWaitFrom) > m_opt.iceWaitMs)
		{
			char b[200];
			_snprintf_s(b, sizeof(b), _TRUNCATE, "no ice tap section within %.0f ms after the load (EXE pid %u; a composite without the ice tap?)",
			            m_opt.iceWaitMs, m_icePid);
			iceFail(b);
		}
	}
	else if(st != IceTapReader::State::Absent)
		m_iceWaitFrom = 0;
}

void Scene::IceHook(void* user, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv)
{
	Scene& s = *static_cast<Scene*>(user);
	if(!s.m_iceWanted || s.m_iceDrawnFrame)
		return;
	s.m_iceDrawnFrame = true;   // tried once per frame, drawn or not
	c5r::ice::FrameStats st;
	const c5r::Status ds = s.m_ice->draw(ctx, rtv, dsv, s.m_iceIn, &st);
	if(!ds.ok())
	{
		s.m_icec.drawErrors++;
		s.m_iceDrawnFrame = false;
		s.m_iceWanted = false;
		const std::string e = ds.str();
		if(e != s.m_iceLastErr)
			RNote("LOUD: ice: draw failed: %s (frame drawn without the ice; %lld so far)", e.c_str(), (long long)s.m_icec.drawErrors);
		s.m_iceLastErr = e;
		return;
	}
	s.m_iceLastErr.clear();
	s.m_iceStats = st;
	s.m_icec.drawnFrames++;
	s.m_icec.copies = st.copies;
	s.m_icec.draws = st.draws;
	s.m_icec.indices = st.indices;
	s.m_iceDrawnSec++;
}

void ShadowFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.shadowFailed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("shadow: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), kShadowFatalExitCode);
		fatal(kShadowFatalExitCode, why.c_str());
	}
	else
		RNote("shadow: %s; no fatal hook (HostConfig::fatal): the server draws on without the models' shadow", why.c_str());
}

void UnitsFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.unitsFailed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("units: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), srvunits::kUnitsFatalExitCode);
		fatal(srvunits::kUnitsFatalExitCode, why.c_str());
	}
	else
		RNote("units: %s; no fatal hook (HostConfig::fatal): the server draws on without units", why.c_str());
}

void ModelsFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.modelsFailed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("models: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), srvmodels::kModelsFatalExitCode);
		fatal(srvmodels::kModelsFatalExitCode, why.c_str());
	}
	else
		RNote("models: %s; no fatal hook (HostConfig::fatal): the server draws on without its models", why.c_str());
}

// ---- epic 0561, ticket 4875: the model module (landmarks/srv_models_scene.h) ----
bool Scene::configureModels(const srvmodels::ModelsOptions& o, std::string& why)
{
	if(!o.any())
	{
		if(m_models)
			m_models->configure(o);   // off: no reader, no writes
		return true;
	}
	if(!m_models)
	{
		m_models = srvmodels::MakeModelsScene();
		if(!m_models)
		{
			why = "render_cities / render_landmarks = 1 but this program does not link the model module "
			      "(server/src/render/landmarks/)";
			return false;
		}
	}
	srvmodels::ModelsOptions mo = o;
	if(mo.looseRoots.empty() && !mo.install.empty())
		mo.looseRoots = ModFolders();   // the mods' art above the install, as the decals' names (srv_residency_source.h)
	m_models->configure(mo);
	m_models->attachMode(m_hdr ? VPSHELL_MODE(m_hdr) : nullptr);
	if(m_dev)
		m_models->setDevice(m_dev);
	RNote("models: render_cities=%d render_landmarks=%d render_models_source=%s (no_frame_ms=%.0f hold_ms=%.0f install_ms=%.0f%s)",
	      o.cities ? 1 : 0, o.landmarks ? 1 : 0, o.source == srvmodels::kModelsSourceExe ? "exe" : "own", o.noFrameMs, o.holdMs,
	      o.installMs, o.drawAll ? ", TEST KEY render_test_models_draw_all=1" : "");
	return true;
}

void Scene::modelsFeed(const SrvFeed::Batch& b)
{
	if(!m_models)
		return;
	for(const SrvFeed::Event& e : b.events)
		if(e.kind == SrvFeed::kLoadBegin)
		{
			m_models->onLoadBegin(Qpc());
			m_dirty = true;
		}
}

srvmodels::FrameTextures Scene::modelsFrameTextures() const
{
	// the server's own fog for the BuildingShader PS (fog-of-war.md 2: the mask in t7, the cloud base in t8); the EXE's
	// white mask (D136) stays in the EXE's own model draws only
	srvmodels::FrameTextures ft = m_modelsCam;   // ticket 4882: the frame's camera rows (set in draw())
	const FowTerrainInputs& fi = m_fow.terrainInputs();
	ft.fogOn = fi.on;
	if(fi.on)
	{
		ft.t[srvmodels::kFrameFogMask] = fi.mask;
		ft.t[srvmodels::kFrameCloudBase] = fi.cloudBase;
	}
	return ft;
}

bool Scene::configureUnits(bool on, const srvunits::UnitsOptions& o, std::string& why)
{
	if(!on)
	{
		joinUnitsJob(false);   // ticket 0711 (R3)
		m_units.reset();
		unitsModeOff();   // issue 4753: render_units=0 writes nothing
		return true;
	}
	if(m_units)
		return true;
	m_units = srvunits::MakeSceneUnits(o);
	if(!m_units)
	{
		why = "render_units=1: this program does not link the unit scene (server/src/units/)";
		return false;
	}
	if(m_dev)
		m_units->setDevice(m_dev);
	unitsModeOn();
	return true;
}

void Scene::setUnits(std::unique_ptr<srvunits::SceneUnits> u)
{
	joinUnitsJob(false);   // ticket 0711 (R3): the old scene's begin ends before it goes
	m_units = std::move(u);
	if(m_units && m_dev)
		m_units->setDevice(m_dev);
	if(m_units)
		unitsModeOn();
	else
		unitsModeOff();
}

// ---- epic 0040, issue 4753: the mode word's unit bit (srv_units_mode.h; unit-server-scene.md 8.1) ----------------------
namespace {
void UnitsModeLog(void*, const char* line) { RNote("%s", line); }
}  // namespace

void Scene::unitsModeOn()
{
	if(!m_unitsMode)
	{
		m_unitsMode = srvunits::MakeModeWriter();
		if(!m_unitsMode)
		{
			RNote("LOUD: units: mode: this program does not link server/src/units/srv_units_mode.cpp: no mode word is written");
			return;
		}
		m_unitsMode->attach(m_hdr ? VPSHELL_MODE(m_hdr) : nullptr);
	}
	if(m_unitsMode->on())
		return;
	m_unitsMode->setOptions(m_opt.unitsReadyHoldMs, m_opt.unitsPositions == SceneOptions::kUnitsPositionsExe);
	m_unitsMode->configure(true, (uint32_t)GetCurrentProcessId(), QpcFreq(), &UnitsModeLog, nullptr);
}

void Scene::unitsModeOff()
{
	if(m_unitsMode)
		m_unitsMode->configure(false, 0, 1, nullptr, nullptr);
}

void Scene::unitsModeFeed(const SrvFeed::Batch& b)
{
	if(!m_unitsMode)
		return;
	for(const SrvFeed::Event& e : b.events)
		if(e.kind == SrvFeed::kLoadBegin)
			m_unitsMode->onLoadBegin(Qpc());
}

void Scene::unitsFrameDone()
{
	if(m_units && m_unitsMode)
		m_unitsMode->onFrame(m_units->ready(), Qpc());
}

// ---- epic 0042, ticket 4785: the units' shadow (srv_unit_shadow.h; research unit-combat-server.md 13) ----------------------
// The mode writer's bit 4 follows this: the switch, the shadow tap read, a unit scene and a working UnitShadow on this device.
void Scene::unitShadowReadiness()
{
	if(!m_unitsMode)
		return;
	const bool wanted = m_units && m_opt.unitsShadow && m_opt.shadow && m_unitShadow && m_unitShadow->ready() && !m_shFailed;
	m_unitsMode->setUnitShadow(wanted, Qpc());
}

// Before the terrain draw: makes the UnitShadow once per device (a failure is the D118 path: shadowFail, the host's
// ShadowFatalStep), tells the mode writer, and when the bound triple's slot says the EXE's unit mask draws were dropped,
// begins the units' frame, draws their mask and binds the merged map for the terrain (op.shadow.srv) and the water (t13).
// Returns true when the units' frame was begun here (the caller does not begin it again).
bool Scene::unitShadowStep(ID3D11DeviceContext* ctx, const SceneFrame& f, c5t::Options& op)
{
	if(!m_units || !m_opt.unitsShadow || !m_opt.shadow || !m_dev)
	{
		unitShadowReadiness();
		return false;
	}
	if(!m_unitShadow && !m_unitShadowTried)
	{
		m_unitShadowTried = true;
		std::unique_ptr<UnitShadow> u(new UnitShadow());
		std::string why;
		if(!u->init(m_dev, m_opt.exe, why))
			shadowFail(why);   // D118: the server cannot cast its units' shadows; LOUD, the host's ShadowFatalStep ends it
		else
		{
			m_unitShadow = std::move(u);
			RNote("unit shadow: on (render_units_shadow=1): the EXE's ShadowShader resample found in the install; the mode word's "
			      "bit 4 follows the unit bit");
		}
	}
	unitShadowReadiness();
	if(!m_lastShHave || !op.shadow.srv)
		return false;   // quality 0, or no triple bound this frame
	const ShadowFrame& sf = m_shFrame;
	if(sf.unitMaskDropped)
		m_usc.flagFrames++;
	const char* why = nullptr;
	if(!UnitShadowWanted(true, true, sf.unitMaskDropped, sf.quality, op.shadow.srv != nullptr, sf.rawWidth, sf.rawHeight, &why) ||
	   !m_unitShadow)
	{
		if(sf.unitMaskDropped)
		{
			// the EXE dropped its unit mask but the server adds none: a frame without unit shadows (counted, LOUD on a change)
			m_usc.notMerged++;
			const std::string w = m_unitShadow ? why : "the unit shadow is not available";
			if(w != m_usc.lastWhy)
				RNote("LOUD: unit shadow: the slot of generation %u says the EXE's unit mask was dropped, but the server's is not "
				      "added: %s (%lld frames so far)", sf.generation, w.c_str(), (long long)m_usc.notMerged);
			m_usc.lastWhy = w;
		}
		return false;
	}
	// ticket 0711 (J1): the units' frame was begun by forkFrameJobs (its plot copies and head on the render thread, its body
	// as the units' job); joined here, before the mask pass is its first use (R3)
	if(!m_unitsForked)
		forkFrameJobs(f);   // not forked by the host or draw() (a test that calls this step alone): begun here
	joinUnitsJob(true);
	srvunits::SceneUnits* su = m_units.get();
	const float* sm0 = sf.matrices[0];
	bool maskOk = true;
	std::string w;
	bool fatal = false;
	ID3D11ShaderResourceView* merged = m_unitShadow->apply(
	    ctx, op.shadow.srv, sf.quality, sf.rawWidth, sf.rawHeight,
	    [&](ID3D11RenderTargetView* r, uint32_t rw, uint32_t rh) {
		    if(!su->drawMask(ctx, r, rw, rh, sm0))
			    maskOk = false;
	    },
	    w, &fatal);
	if(fatal)
		shadowFail(w);
	if(!maskOk)
		m_usc.maskFails++;   // the unit scene counted and logged the pass's error; the merged map holds what was drawn
	if(merged)
	{
		op.shadow.srv = merged;
		m_lastShadow.srv = merged;
		WaterSceneShadow(&m_water, merged, op.shadow.strength, op.shadow.matrix);
		m_usc.mergedFrames++;
		m_usc.mergedSec++;
		if(sf.quality == 1)
			m_usc.mergedQ1++;
		if(!m_usc.lastWhy.empty())
			RNote("unit shadow: merged again (generation %u, quality %u)", sf.generation, sf.quality);
		m_usc.lastWhy.clear();
	}
	else
	{
		m_usc.notMerged++;
		if(w != m_usc.lastWhy)
			RNote("LOUD: unit shadow: not merged (generation %u, quality %u): %s (%lld frames so far)", sf.generation, sf.quality, w.c_str(),
			      (long long)m_usc.notMerged);
		m_usc.lastWhy = w;
	}
	return true;
}

// ---- ticket 0711 (J1): frame jobs, the units' begin as the first (research/07 architect-2026-11-23-frame-jobs.md 4-5) ----
// The fork: the fog's plot states and plot table are final before decide (they change only in the feed's drain and the fog's
// init, pan-coherence.md 5.16 item 1), so handing them here copies the values the begin got at its place before 0711. Then
// the units' head on this thread and their body as the job, with the camera and ONE clock value by value (R1, R9).
void Scene::forkFrameJobs(const SceneFrame& f)
{
	if(!m_units || m_unitsForked)
		return;
	vppacing::StageScope pace(vppacing::kStUnits, &Qpc);   // the head counts to the units' stage
	m_unitsForked = true;
	m_unitsForkFrame = f.frameId;
	m_unitsForkNowMs = f.nowMs;
	const std::vector<c5r::fow::PlotState>& fs = m_fow.states();
	m_units->setPlotStates(fs.empty() ? nullptr : reinterpret_cast<const uint8_t*>(fs.data()), fs.size(), m_fow.tableWidth(),
	                       m_fow.tableHeight(), m_fow.fogGeneration());
	HandPlotTable(*m_units, m_fow);   // ticket 5052 (T)
	if(!m_units->beginHead(UnitsCameraOf(f), m_haveGrid ? &m_grid : nullptr))
		return;   // not ready, no grid, not open: nothing more this frame (drawScene's own begin as before 0711)
	srvunits::SceneUnits* u = m_units.get();
	if(!m_jobs)
	{
		u->beginBody(nullptr);   // no pool (a scene drawn before init): the body here, at once
		return;
	}
	m_jobs->fork(
	    m_unitsJob,
	    [u](FrameJob& job) {
		    std::vector<std::string> lines;
		    u->beginBody(&lines);   // R5: beginFrame's calls in beginFrame's order; R6: its lines collected
		    for(std::string& l : lines)
			    job.log(std::move(l));
	    },
	    {u->jobOwner()});
}

// The join (R3, R4): the render thread claims the job if no worker has (and runs it), else waits for the worker; then the
// body's lines are written in order and the job's numbers counted (beginJoined). rethrow: a job that threw ends this draw
// with the exception, as an exception of the begin did before 0711 (the guard and the clean-up paths only log it).
bool Scene::joinUnitsJob(bool rethrow)
{
	if(!m_unitsForked)
		return false;
	m_unitsForked = false;
	if(!m_unitsJob.forked() || !m_jobs)
		return true;   // the head ended the begin (or there was no pool): nothing forked
	const FrameJobResult& r = m_jobs->join(m_unitsJob);
	if(m_units)
		m_units->beginJoined(r);
	for(const std::string& l : FrameJobsTakeLoudLines())
		RNote("%s", l.c_str());
	if(r.failed && rethrow)
		throw std::runtime_error("ticket 0711: the units' begin (frame job) threw: " + (r.lines.empty() ? std::string("?") : r.lines.back()));
	return true;
}

void Scene::joinFrameJobs()
{
	joinUnitsJob(false);
}

// ---- ticket 0753 (J3): record ahead (research/07 architect-2026-11-25-frame-gap-route.md 5, rules A1-A6) ----
// A1: the list holds exactly the calls FowScene::afterWorld and pathDraw make today, executed where they stand today
// (drawEnd). A2: their CPU inputs are final when draw() records them (pan-coherence.md 5.20, the table); the path's depth
// copy decision is taken here and its copy stays at its place. A3: the deferred context starts every list in the default
// state and both stages set all they use; ExecuteCommandList(.., FALSE) leaves the immediate context in the default state
// and the host's hand-over and every stage of the next pass set their own. A5: no query is read on the deferred context
// (FowScene::setRecording); a list FinishCommandList refuses is drawn inline at its place, LOUD and counted.
// The record point (before unitShadowStep): step 2's terrain and middle lists, then step 1's end list, in the stages' own
// order (the CPU side effects keep their relative order: the terrain's LOD before the trees' Screen Overlay, the middle's
// path depth decision before the end's path draw).
void Scene::recordAhead(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h, const SceneFrame& f,
                        const c5t::Camera& cam, const c5t::Options& op)
{
	dropRecorded();   // never a list of an earlier draw (a draw that left one behind is counted there)
	m_raEndInline = false;
	m_raEndTimed = false;
	m_raMidPathDepth = false;
	m_raShadowPredicted = nullptr;
	if(!m_raCtx)
		return;
	if((!m_unitsForked || !m_unitsJob.forked()) && !m_opt.recordAheadAlways)
	{   // nothing in flight to hide (no units, the head ended the begin, no pool): drawn at their places
		m_rac.notForked++;
		m_rac.notForkedSec++;
		return;
	}
	const int64_t q0 = Qpc();
	try
	{
		const bool go = m_opt.recordAheadEndOnly || recordStep2(rtv, dsv, w, h, f, cam, op);
		if(go)   // false: the recorded terrain draw failed, so the draw ends at the terrain's place (nothing after it recorded)
			recordDrawEnd(rtv, dsv, w, h, f);
	}
	catch(...)
	{   // a stage threw while recording (as it would have thrown inline): no partial recording is left for the next draw
		m_fow.setRecording(false);
		ID3D11CommandList* partial = nullptr;
		m_raCtx->FinishCommandList(FALSE, &partial);
		SafeRelease(partial);
		throw;
	}
	const double ms = QpcToMs(Qpc() - q0);   // every list of this draw (record_ms of the second's line: per drawn frame)
	m_rac.recordMsTotal += ms;
	m_rac.recordMsSec += ms;
	m_rac.recordMsMaxSec = (std::max)(m_rac.recordMsMaxSec, ms);
}

// The merged shadow map's srv the shadow step will hand the terrain this frame, without the step's side effects
// (unitShadowStep's own conditions in its order). false: not predictable (the step makes its UnitShadow this frame, or no
// merged map was made yet): step 2 is not recorded.
bool Scene::raPredictShadow(const c5t::Options& op, ID3D11ShaderResourceView*& srv) const
{
	srv = op.shadow.srv;
	if(!m_units || !m_opt.unitsShadow || !m_opt.shadow || !m_dev)
		return true;
	if(!m_unitShadow)
		return m_unitShadowTried;   // tried and failed: the step merges nothing; not tried: the step makes it
	if(!m_lastShHave || !op.shadow.srv)
		return true;
	const ShadowFrame& sf = m_shFrame;
	if(!UnitShadowWanted(true, true, sf.unitMaskDropped, sf.quality, true, sf.rawWidth, sf.rawHeight, nullptr))
		return true;
	srv = m_unitShadow->mergedSrv();
	return srv != nullptr;
}

bool Scene::raFinish(ID3D11CommandList** out, const char* what, int64_t frameId)
{
	*out = nullptr;
	const HRESULT hr = m_raCtx->FinishCommandList(FALSE, out);
	if(SUCCEEDED(hr) && *out)
		return true;
	SafeRelease(*out);
	m_rac.finishFailed++;
	char e[64];
	_snprintf_s(e, sizeof(e), _TRUNCATE, "0x%08X", (unsigned)hr);
	if(m_raLastErr != e || m_rac.finishFailed <= 3)
		RNote("LOUD: record ahead: FinishCommandList failed %s for %s (frame %lld; %lld so far): drawn inline at its place this "
		      "frame (its CPU part runs again)", e, what, (long long)frameId, (long long)m_rac.finishFailed);
	m_raLastErr = e;
	return false;
}

bool Scene::recordStep2(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h, const SceneFrame& f,
                        const c5t::Camera& cam, const c5t::Options& op)
{
	ID3D11ShaderResourceView* pred = nullptr;
	if(!raPredictShadow(op, pred))
	{
		m_rac.noPrediction++;
		return true;
	}
	c5t::Options opT = op;
	opT.shadow.srv = pred;
	opT.gpuTiming = false;   // A5: no query on the deferred context; gpuTimingBegin / End around the execute
	m_raShadowPredicted = pred;
	{   // the terrain list: the clear and the terrain draw
		vppacing::StageScope pace(vppacing::kStTerrain, &Qpc);
		const float clear[4] = {0, 0, 0, 0};   // transparent outside the terrain
		m_raCtx->ClearRenderTargetView(rtv, clear);
		m_raTerrainFs = c5t::FrameStats();
		m_raTerrainDs = m_r->draw(m_raCtx, rtv, dsv, cam, opT, &m_raTerrainFs);
	}
	if(!raFinish(&m_raTerrainList, "the terrain", f.frameId))
		return true;   // the terrain (and so the middle) drawn inline at their places; the end still recorded
	if(!m_raTerrainDs.ok())
		return false;   // the list holds what the failed draw issued; the draw ends at the terrain's place, as inline
	// the middle list: the arc, the extras, the path's depth copy (its decision too), the trees
	{
		vppacing::StageScope pace(vppacing::kStDraw, &Qpc);
		const int64_t q0 = Qpc();
		arcDraw(m_raCtx, rtv, dsv, w, h, f);
		extrasDraw(m_raCtx, rtv, dsv, w, h, f);
		pathSaveDepth(m_raCtx, dsv);
		m_raMidPathDepth = true;
		m_raRecordTicks += Qpc() - q0;
	}
	{
		vppacing::StageScope pace(vppacing::kStForest, &Qpc);
		if(m_forest)
			m_forest->drawStage(m_raCtx, rtv, dsv, w, h, f.cam, *m_r, cam, opT, m_fow.terrainInputs().fowAnim);
	}
	raFinish(&m_raMidList, "the arc to the trees", f.frameId);   // on failure: drawn inline at its place
	return true;
}

// After unitShadowStep: the terrain and the trees were recorded with the map it would hand them; another map (apply()
// made a new one or none) drops both lists, and both are drawn inline at their places (their CPU part runs again: the
// terrain's LOD rule is idempotent for one camera, the arc's and the path's clocks add 0)
void Scene::raCheckPrediction(const c5t::Options& op, int64_t frameId)
{
	if(op.shadow.srv == m_raShadowPredicted)
		return;
	m_rac.mispredicted++;
	if(m_rac.mispredicted <= 3 || m_rac.mispredicted % 100 == 0)
		RNote("LOUD: record ahead: the shadow step handed the terrain another map than predicted (frame %lld; %lld so far): the "
		      "terrain and the arc-to-trees lists are dropped and drawn inline", (long long)frameId, (long long)m_rac.mispredicted);
	raDropStep2();
}

void Scene::raDropStep2()
{
	if(m_raTerrainList)
	{
		SafeRelease(m_raTerrainList);
		m_rac.dropped++;
	}
	if(m_raMidList)
	{
		SafeRelease(m_raMidList);
		m_rac.dropped++;
	}
	m_raMidPathDepth = false;
}

void Scene::recordDrawEnd(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h, const SceneFrame& f)
{
	vppacing::StageScope pace(vppacing::kStDraw, &Qpc);   // the recording is the draw stage's end, done earlier
	const int64_t q0 = Qpc();
	if(!m_raMidPathDepth)
	{   // the middle list did not take the path depth decision: take it here, the copy stays at its place
		m_pathDepthSaved = pathDepthPrepare(dsv);
		m_pathDepthAhead = true;
	}
	m_fow.setRecording(true);
	try
	{
		m_fow.afterWorld(m_raCtx, rtv, dsv, w, h);
		pathDraw(m_raCtx, rtv, dsv, w, h, f);
	}
	catch(...)
	{
		m_fow.setRecording(false);
		ID3D11CommandList* partial = nullptr;   // the partial recording is discarded (the context back to its default state)
		m_raCtx->FinishCommandList(FALSE, &partial);
		SafeRelease(partial);
		throw;
	}
	m_fow.setRecording(false);
	m_raEndTimed = m_fow.recordedTimed();
	// on failure the cloud planes and the path are drawn inline at their place (their CPU part runs again: the particles
	// step once more)
	if(!raFinish(&m_raEndList, "the cloud planes and the path", f.frameId))
		m_raEndInline = true;
	m_raRecordTicks += Qpc() - q0;
}

void Scene::drawEnd(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
                    const SceneFrame& f)
{
	if(m_raEndList)
	{
		const int64_t q0 = Qpc();
		if(m_raEndTimed)
			m_fow.listTiming(ctx, true);
		ctx->ExecuteCommandList(m_raEndList, FALSE);
		if(m_raEndTimed)
		{
			m_fow.listTiming(ctx, false);
			m_rac.timedLists++;
		}
		SafeRelease(m_raEndList);
		const double ms = QpcToMs(Qpc() - q0);
		m_rac.lists++;
		m_rac.listsSec++;
		m_rac.execMsTotal += ms;
		m_rac.execMsSec += ms;
		m_raExecFrameMs += ms;   // the max is per drawn frame: every list of the draw
		m_rac.execMsMaxSec = (std::max)(m_rac.execMsMaxSec, m_raExecFrameMs);
		return;
	}
	m_raEndInline = false;
	// issue 3954: the cloud planes after the terrain and the water, into the same colour and depth (fog-of-war.md 2.3, 5.1);
	// a failure is LOUD and counted by the fog scene, the frame stands
	m_fow.afterWorld(ctx, rtv, dsv, w, h);
	// ticket 0300 part 2: the move-order path's models at the end of the world pass, after the cloud planes (stock: FluidFOW
	// 0x602B10 before MovementPath's render 0x519B90, the last call of world pass B; path-models.md 7.2), into the same colour
	// and depth; a failure is LOUD and counted, the frame stands
	pathDraw(ctx, rtv, dsv, w, h, f);
}

void Scene::dropRecorded()
{
	raDropStep2();
	if(!m_raEndList)
		return;
	SafeRelease(m_raEndList);
	m_rac.dropped++;
}

// ---- ticket 0199 part 2: the ranged-attack arc (research/05-graphics-composition/arrows-and-path-line.md 13) ----
// UiBridge::Kind (proxy/src/proxy_ui_bridge.h), repeated here: the proxy's header is not included on this side.
static const uint16_t kUiKindSpawnArrow = 1, kUiKindRemoveAllArrows = 2;

void Scene::arcInit(ID3D11Device* dev, const SceneOptions& o)
{
	m_arcR.reset();
	m_arcC.ready = false;
	if(!o.arc)
	{
		RNote("terrain scene: render_arc=0: no ranged-attack arc (the EXE draws its own unless its Arrows view bit is cleared)");
		return;
	}
	if(!m_arcSettingsRead)
		return;   // LOUD above
	std::unique_ptr<c5r::arc::Renderer> r(new c5r::arc::Renderer());
	const int64_t q0 = Qpc();
	const c5r::Status st = r->init(dev, o.exe);
	if(!st.ok())
	{
		RNote("LOUD: arc: render_arc=1 but the renderer did not start (ArrowShader from '%ls'): %s; the arc is not drawn", o.exe.c_str(),
		      st.str().c_str());
		return;
	}
	m_arcR = std::move(r);
	m_arcC.ready = true;
	RNote("terrain scene: render_arc=1: the ranged-attack arc (c5r::arc, ArrowShader from the EXE, %.1f ms), drawn after the water and "
	      "the ice, before the trees and the units",
	      QpcToMs(Qpc() - q0));
}

void Scene::arcFeed(const SrvFeed::Batch& b)
{
	if(!m_opt.arc)
		return;
	for(const SrvFeed::Event& e : b.events)
	{
		if(e.kind == SrvFeed::kLoadBegin || (e.kind == SrvFeed::kUiMirror && e.flags == kUiKindRemoveAllArrows))
		{
			m_arcC.removes++;
			m_arcPending = false;
			if(m_arc.visible())
				m_dirty = true;
			m_arc.removeAll();
		}
		else if(e.kind == SrvFeed::kUiMirror && e.flags == kUiKindSpawnArrow)
		{
			m_arcC.spawns++;
			memcpy(m_arcPend, e.a, sizeof(m_arcPend));
			m_arcPending = true;
			m_dirty = true;
		}
	}
}

void Scene::arcDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
                    const SceneFrame& f)
{
	if(!m_opt.arc || !m_arcR)
		return;
	const double now = f.nowMs >= 0 ? f.nowMs : QpcToMs(Qpc());
	c5r::arc::DrawInput in;
	in.colour = rtv;
	in.depth = dsv;
	in.width = w;
	in.height = h;
	for(int i = 0; i < 4; ++i)   // view 0 x projection (row vectors), as the EXE's 0xDA1310 x 0xDA12D0 (arrows-and-movement-path.md 1.7)
		for(int j = 0; j < 4; ++j)
		{
			float a = f.cam.view[0][i * 4 + 0] * f.cam.proj[0 * 4 + j];
			a += f.cam.view[0][i * 4 + 1] * f.cam.proj[1 * 4 + j];
			a += f.cam.view[0][i * 4 + 2] * f.cam.proj[2 * 4 + j];
			a += f.cam.view[0][i * 4 + 3] * f.cam.proj[3 * 4 + j];
			in.viewProj[i * 4 + j] = a;
		}
	auto fail = [&](int64_t& counter, const char* what, const c5r::Status& st) {
		counter++;
		const std::string e = std::string(what) + ": " + st.str();
		if(e != m_arcLastErr || counter <= 3)
			RNote("LOUD: arc: %s (frame %lld; %lld so far): the frame is drawn without the arc", e.c_str(), (long long)f.frameId,
			      (long long)counter);
		m_arcLastErr = e;
	};
	if(m_arcPending)
	{
		m_arcPending = false;
		float planes[6][4], start[3], endXY[2];
		c5r::Status st = m_haveGrid ? c5r::arc::frustumPlanes(in.viewProj, planes)
		                            : c5r::Status::fail(c5r::Err::NotFound, "no height field yet");
		if(st.ok())
			st = c5r::arc::spawnPoints(m_grid, m_arcPend[0], m_arcPend[1], m_arcPend[2], m_arcPend[3], planes, start, endXY);
		if(st.ok())
			st = m_arc.spawn(start, endXY);
		if(st.ok())
		{
			m_arcC.applied++;
			m_arcLastMs = now;   // the growth starts with this frame
			if(m_arcC.applied <= 3)
				RNote("arc: spawn %lld: plots (%d, %d) -> (%d, %d), start (%.2f, %.2f, %.2f), end (%.2f, %.2f, %.2f)", (long long)m_arcC.applied,
				      m_arcPend[0], m_arcPend[1], m_arcPend[2], m_arcPend[3], start[0], start[1], start[2], endXY[0], endXY[1],
				      m_arc.settings().targetHeight);
		}
		else
		{
			char what[96];
			snprintf(what, sizeof(what), "spawn of plots (%d, %d) -> (%d, %d) refused", m_arcPend[0], m_arcPend[1], m_arcPend[2], m_arcPend[3]);
			fail(m_arcC.spawnErrors, what, st);
		}
	}
	double dt = m_arcLastMs >= 0 ? (now - m_arcLastMs) / 1000.0 : 0.0;
	m_arcLastMs = now;
	if(!(dt >= 0))
		dt = 0;   // a clock that went back (another recording's span) or not finite
	if(dt > 0.25)
		dt = 0.25;
	const c5r::Status us = m_arc.update((float)dt);
	if(!us.ok())
	{
		fail(m_arcC.drawErrors, "update", us);
		return;
	}
	if(!m_arc.visible())
		return;
	c5r::arc::DrawStats ds;
	const c5r::Status st = m_arcR->draw(ctx, in, m_arc, &ds);
	if(!st.ok())
	{
		fail(m_arcC.drawErrors, "draw", st);
		return;
	}
	m_arcC.drawnFrames++;
	m_arcC.uploads += ds.uploads;
	if(m_arc.growing())
		m_dirty = true;   // the next frame grows it further, also when the camera holds still
}

// ---- ticket 0300 part 2: the move-order path's models (research/05-graphics-composition/path-models.md 7) ----
// UiBridge::Kind and the kPath* bits (proxy/src/proxy_ui_bridge.h), repeated here: the proxy's header is not included on
// this side.
static const uint16_t kUiKindDisplayMovement = 4, kUiKindPathNode = 8;
static const uint32_t kUiPathIndexMask = 0xFF, kUiPathCountShift = 8, kUiPathTested = 1u << 16, kUiPathEmbark = 1u << 17,
                      kUiPathAttack = 1u << 18, kUiPathSwap = 1u << 19;
// The placement's floor: the EXE's constant 10.116731 (.rdata 0xA7A040, stored at +0x5E90 by the constructor 0x5D0D30;
// path-models.md 7.1) [C].
static const float kPathFloorZ = 10.116731f;

void Scene::pathInit(ID3D11Device* dev, const SceneOptions& o, const c5r::gamefs::GameFiles* files)
{
	m_pathR.reset();
	m_pathC.ready = false;
	if(!o.path)
	{
		RNote("terrain scene: render_path=0: no move-order path markers (the EXE draws its own unless its MovementPath view bit is cleared)");
		return;
	}
	const int64_t q0 = Qpc();
	if(!m_pathM)
	{
		if(!files)
		{
			RNote("LOUD: path: render_path=1 but the install's index is not open ('%ls'): no models, the path is not drawn", o.install.c_str());
			return;
		}
		std::unique_ptr<c5r::pathmodels::PathModels> m(new c5r::pathmodels::PathModels());
		const c5r::Status ls = m->load(*files);
		if(!ls.ok())
		{
			RNote("LOUD: path: render_path=1 but the 18 models of assets/UI/Overlays did not load from '%ls': %s; the path is not drawn",
			      o.install.c_str(), ls.str().c_str());
			return;
		}
		m_pathM = std::move(m);
	}
	if(!files)
	{
		RNote("LOUD: path: render_path=1 but the install's index is not open ('%ls'): no textures, the path is not drawn", o.install.c_str());
		return;
	}
	std::unique_ptr<c5r::pathmodels::PathRenderer> r(new c5r::pathmodels::PathRenderer());
	c5r::Status st = r->openPrograms(o.exe);
	if(st.ok())
		st = r->create(dev, *m_pathM, c5r::pathmodels::gameFilesReader(*files));
	if(!st.ok())
	{
		RNote("LOUD: path: render_path=1 but the renderer did not start (UIShader from '%ls', the textures from the install): %s; the "
		      "path is not drawn",
		      o.exe.c_str(), st.str().c_str());
		return;
	}
	m_pathR = std::move(r);
	m_pathC.ready = true;
	m_pathChanged = true;   // a device rebuild with a path shown draws it again
	RNote("terrain scene: render_path=1: the move-order path's models (c5r::pathmodels, 18 assets, UIShader from the EXE, %.1f ms), "
	      "drawn at the end of the world pass",
	      QpcToMs(Qpc() - q0));
}

void Scene::pathFeed(const SrvFeed::Batch& b)
{
	if(!m_opt.path)
		return;
	for(const SrvFeed::Event& e : b.events)
	{
		if(e.kind == SrvFeed::kLoadBegin)
		{
			m_pathC.loads++;
			m_pathAsm.clear();
			m_pathNodes.clear();
			m_pathBits = 0;
			m_pathMouseX = m_pathMouseY = -1;
			if(m_pathShown || !m_pathInst.empty())
				m_dirty = true;
			m_pathShown = false;
			m_pathChanged = true;
		}
		else if(e.kind == SrvFeed::kUiMirror && e.flags == kUiKindDisplayMovement)
		{
			const bool show = e.a[0] != 0;
			(show ? m_pathC.shows : m_pathC.hides)++;
			if(show || m_pathShown)
				m_dirty = true;
			m_pathShown = show;
			m_pathChanged = true;   // stock rebuilds its list at every show and clears it at every hide
		}
		else if(e.kind == SrvFeed::kUiMirror && e.flags == kUiKindPathNode)
		{
			m_pathC.nodeRecords++;
			const uint32_t bits = (uint32_t)e.a[2];
			const uint32_t idx = bits & kUiPathIndexMask, count = (bits >> kUiPathCountShift) & 0xFF;
			PathNodeIn n;
			n.x = (int16_t)(uint16_t)((uint32_t)e.a[0] & 0xFFFF);
			n.y = (int16_t)(uint16_t)((uint32_t)e.a[0] >> 16);
			n.turn = (int16_t)e.a[1];   // the EXE keeps the turn value as 16 bits (arrows-and-movement-path.md 3)
			n.embark = (bits & kUiPathEmbark) != 0;
			if(count == 0)   // an empty path: the record carries the mouse plot and the cursor's tests
			{
				m_pathAsm.clear();
				m_pathNodes.clear();
				m_pathBits = bits;
				m_pathMouseX = n.x;
				m_pathMouseY = n.y;
				m_pathC.paths++;
				m_pathChanged = true;
				if(m_pathShown)
					m_dirty = true;
				continue;
			}
			if(idx == 0)
			{
				m_pathAsm.clear();
				m_pathAsmUpdate = (uint32_t)e.a[3];
				m_pathAsmCount = count;
			}
			else if((uint32_t)e.a[3] != m_pathAsmUpdate || count != m_pathAsmCount || idx != m_pathAsm.size())
			{
				m_pathC.broken++;
				if(m_pathC.broken <= 3)
					RNote("LOUD: path: a node record that does not continue the path being assembled (update %d, node %u of %u; assembling "
					      "update %u, %zu of %u): dropped",
					      e.a[3], idx, count, m_pathAsmUpdate, m_pathAsm.size(), m_pathAsmCount);
				m_pathAsm.clear();
				m_pathAsmCount = 0;
				continue;
			}
			m_pathAsm.push_back(n);
			if(m_pathAsm.size() == m_pathAsmCount)
			{
				m_pathNodes.swap(m_pathAsm);
				m_pathAsm.clear();
				m_pathBits = bits;   // the last node's record: kPathAttack belongs to it
				m_pathC.paths++;
				m_pathChanged = true;
				if(m_pathShown)
					m_dirty = true;
			}
		}
	}
}

void Scene::pathFail(int64_t& counter, const char* what, const c5r::Status& st, int64_t frameId)
{
	counter++;
	const std::string e = std::string(what) + ": " + st.str();
	if(e != m_pathLastErr || counter <= 3)
		RNote("LOUD: path: %s (frame %lld; %lld so far): the frame is drawn without the path", e.c_str(), (long long)frameId,
		      (long long)counter);
	m_pathLastErr = e;
}

// The camera's world matrix of a view (row vectors: p_view = p_world x V, V = [R 0; t 1]): [R^T 0; -t R^T 1].
static c5r::pathmodels::Mat4 PathCameraWorld(const float* v)
{
	c5r::pathmodels::Mat4 c;
	for(int i = 0; i < 3; ++i)
		for(int j = 0; j < 3; ++j)
			c.m[i * 4 + j] = v[j * 4 + i];
	for(int j = 0; j < 3; ++j)
		c.m[12 + j] = -(v[12] * v[j * 4 + 0] + v[13] * v[j * 4 + 1] + v[14] * v[j * 4 + 2]);
	c.m[3] = c.m[7] = c.m[11] = 0;
	c.m[15] = 1;
	return c;
}

void Scene::pathSaveDepth(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv)
{
	// ticket 0753 (J3): record ahead took the decision (and made the texture) before it recorded pathDraw; only the copy is
	// left for this place
	if(!m_pathDepthAhead)
		m_pathDepthSaved = pathDepthPrepare(dsv);
	m_pathDepthAhead = false;
	if(!m_pathDepthSaved)
		return;
	ID3D11Resource* res = nullptr;
	dsv->GetResource(&res);
	if(res)
		ctx->CopyResource(m_pathDepthTex, res);
	SafeRelease(res);
}

bool Scene::pathDepthPrepare(ID3D11DepthStencilView* dsv)
{
	// only while a path is shown (the list may be rebuilt at this frame's pathDraw: shown is enough)
	if(!m_opt.path || !m_pathR || !m_pathShown || !dsv || !m_dev)
		return false;
	ID3D11Resource* res = nullptr;
	dsv->GetResource(&res);
	ID3D11Texture2D* src = nullptr;
	if(res)
		res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&src));
	SafeRelease(res);
	if(!src)
		return false;
	D3D11_TEXTURE2D_DESC sd;
	src->GetDesc(&sd);
	SafeRelease(src);
	if(m_pathDepthTex)
	{
		D3D11_TEXTURE2D_DESC od;
		m_pathDepthTex->GetDesc(&od);
		if(od.Width != sd.Width || od.Height != sd.Height || od.Format != sd.Format || od.SampleDesc.Count != sd.SampleDesc.Count)
		{
			SafeRelease(m_pathDepth);
			SafeRelease(m_pathDepthTex);
		}
	}
	if(!m_pathDepthTex)
	{
		D3D11_TEXTURE2D_DESC d = sd;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		d.CPUAccessFlags = 0;
		d.MiscFlags = 0;
		d.Usage = D3D11_USAGE_DEFAULT;
		D3D11_DEPTH_STENCIL_VIEW_DESC vd;
		dsv->GetDesc(&vd);
		HRESULT hr = m_dev->CreateTexture2D(&d, nullptr, &m_pathDepthTex);
		if(SUCCEEDED(hr))
			hr = m_dev->CreateDepthStencilView(m_pathDepthTex, &vd, &m_pathDepth);
		if(FAILED(hr))
		{
			SafeRelease(m_pathDepth);
			SafeRelease(m_pathDepthTex);
			pathFail(m_pathC.drawErrors, "depth copy", c5r::Status::fail(c5r::Err::Device, "the depth copy could not be made"), 0);
			return false;
		}
	}
	return true;
}

void Scene::pathDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
                     const SceneFrame& f)
{
	if(!m_opt.path || !m_pathR || !m_pathM)
		return;
	if(m_pathDepthSaved && m_pathDepth)
		dsv = m_pathDepth;   // the depth before the trees and the units (pathSaveDepth)
	namespace pm = c5r::pathmodels;
	// the SelectedAni clock (the EXE's +0x5E88: the frames' dt summed, path-models.md 7.1), clamped per frame as the arc's
	const double now = f.nowMs >= 0 ? f.nowMs : QpcToMs(Qpc());
	double dt = m_pathLastMs >= 0 ? (now - m_pathLastMs) / 1000.0 : 0.0;
	m_pathLastMs = now;
	if(!(dt >= 0))
		dt = 0;
	if(dt > 0.25)
		dt = 0.25;
	m_pathClock += dt;
	if(m_pathClock > 3600.0)
		m_pathClock = fmod(m_pathClock, 3600.0);   // the clip loops; keep the float time precise
	if(m_pathChanged && m_haveGrid)
	{
		m_pathChanged = false;
		m_pathInst.clear();
		m_pathC.rebuilds++;
		if(m_pathShown)
		{
			const std::vector<c5r::fow::PlotState>& fs = m_fow.states();
			const uint32_t fw = m_fow.tableWidth(), fh = m_fow.tableHeight();
			auto unexplored = [&](int x, int y) -> uint8_t {
				if(fs.empty() || x < 0 || y < 0 || (uint32_t)x >= fw || (uint32_t)y >= fh || (size_t)y * fw + x >= fs.size())
					return 0;
				return fs[(size_t)y * fw + x] == c5r::fow::PlotState::Unexplored ? 1 : 0;
			};
			pm::PathInput in;
			std::vector<uint8_t> fog;
			c5r::Status st = c5r::Status::Ok();
			bool place = true;
			if(!m_pathNodes.empty())
			{
				for(const PathNodeIn& n : m_pathNodes)
				{
					pm::PathNode pn;
					pn.x = n.x;
					pn.y = n.y;
					pn.turn = n.turn;
					in.nodes.push_back(pn);
					in.embark.push_back(n.embark ? 1 : 0);
					fog.push_back(unexplored(n.x, n.y));
				}
				in.lastIsAttack = (m_pathBits & kUiPathAttack) != 0;
				st = pm::nodePositions(m_grid, in.nodes, fog, std::vector<uint8_t>(), kPathFloorZ, in.pos);
			}
			else if(m_pathMouseX >= 0 && m_pathMouseY >= 0)
			{
				pm::PathNode c;
				c.x = m_pathMouseX;
				c.y = m_pathMouseY;
				std::vector<pm::Vec3> cp;
				st = pm::nodePositions(m_grid, std::vector<pm::PathNode>(1, c), std::vector<uint8_t>(1, unexplored(c.x, c.y)),
				                       std::vector<uint8_t>(), kPathFloorZ, cp);
				if(st.ok())
					in.cursor = cp[0];
				in.cursorKind = (m_pathBits & kUiPathSwap) ? pm::CursorKind::Embark
				                                           : ((m_pathBits & kUiPathAttack) ? pm::CursorKind::Attack : pm::CursorKind::Inaccessible);
			}
			else
				place = false;   // no path and no mouse plot known: nothing to place
			pm::PlacementStats ps;
			if(place && st.ok())
				st = pm::placePath(in, m_pathInst, &ps);
			if(!st.ok())
			{
				m_pathInst.clear();
				pathFail(m_pathC.placeErrors, "placement", st, f.frameId);
			}
			else if(place && ++m_pathPlacedLogs <= 40)
			{
				// the first 40 placed lists: their nodes' embark bits and the instances' assets (the EXE's index)
				std::string emb, assets;
				for(const PathNodeIn& n : m_pathNodes)
					emb += n.embark ? '1' : '0';
				for(const pm::Instance& i : m_pathInst)
					assets += std::to_string((int)i.asset) + " ";
				RNote("path: list %lld: %zu node(s) (first (%d, %d), last (%d, %d)), embark bits %s, %zu instance(s) (dropped %u), "
				      "tested %d, last attack %d, cursor (%d, %d), assets %s",
				      (long long)m_pathC.rebuilds, m_pathNodes.size(), m_pathNodes.empty() ? -1 : m_pathNodes.front().x,
				      m_pathNodes.empty() ? -1 : m_pathNodes.front().y, m_pathNodes.empty() ? -1 : m_pathNodes.back().x,
				      m_pathNodes.empty() ? -1 : m_pathNodes.back().y, emb.c_str(), m_pathInst.size(), ps.dropped,
				      (m_pathBits & kUiPathTested) ? 1 : 0, (m_pathBits & kUiPathAttack) ? 1 : 0, m_pathMouseX, m_pathMouseY,
				      assets.c_str());
			}
		}
		m_pathC.lastInstances = (uint32_t)m_pathInst.size();
	}
	if(m_pathInst.empty())
		return;
	// the EXE's two lists (path-models.md 7.1): each instance with the first view that holds it, at most two views; here
	// "holds" is the view whose clip-space position of the instance is nearest the screen's middle, within 1.25 [I: the
	// EXE tests world boxes widened by 64 units]
	const uint32_t nViews = f.cam.numViews == 0 ? 1u : (f.cam.numViews > 2 ? 2u : f.cam.numViews);
	pm::Mat4 vp[2];
	for(uint32_t v = 0; v < nViews; ++v)
		for(int i = 0; i < 4; ++i)
			for(int j = 0; j < 4; ++j)
			{
				float a = f.cam.view[v][i * 4 + 0] * f.cam.proj[0 * 4 + j];
				a += f.cam.view[v][i * 4 + 1] * f.cam.proj[1 * 4 + j];
				a += f.cam.view[v][i * 4 + 2] * f.cam.proj[2 * 4 + j];
				a += f.cam.view[v][i * 4 + 3] * f.cam.proj[3 * 4 + j];
				vp[v].m[i * 4 + j] = a;
			}
	std::vector<pm::Instance> lists[2];
	bool animated = false;
	for(const pm::Instance& in : m_pathInst)
	{
		const float px = in.world.m[12], py = in.world.m[13], pz = in.world.m[14];
		int best = -1;
		float bestD = 1.25f;
		for(uint32_t v = 0; v < nViews; ++v)
		{
			const float* m = vp[v].m;
			const float cw = px * m[3] + py * m[7] + pz * m[11] + m[15];
			if(!(cw > 1e-6f))
				continue;
			const float cx = (px * m[0] + py * m[4] + pz * m[8] + m[12]) / cw;
			const float cy = (px * m[1] + py * m[5] + pz * m[9] + m[13]) / cw;
			const float d = std::max(std::fabs(cx), std::fabs(cy));
			if(d <= bestD)
			{
				bestD = d;
				best = (int)v;
			}
		}
		if(best < 0)
		{
			m_pathC.unplaced++;
			continue;
		}
		lists[best].push_back(in);
		const uint8_t a = (uint8_t)in.asset;
		if(a >= (uint8_t)pm::Asset::SelectedAniOneTurn && a <= (uint8_t)pm::Asset::SelectedAniAttack)
			animated = true;
	}
	D3D11_VIEWPORT vpt = {0, 0, (float)w, (float)h, 0, 1};
	ctx->OMSetRenderTargets(1, &rtv, dsv);
	ctx->RSSetViewports(1, &vpt);
	bool drew = false;
	for(uint32_t v = 0; v < nViews; ++v)
	{
		if(lists[v].empty())
			continue;
		std::vector<pm::DrawItem> items;
		c5r::Status st = pm::buildDraws(*m_pathM, lists[v], (float)m_pathClock, items);
		float cb1[32];
		pm::Mat4 cw;
		if(v == 0)
			memcpy(cw.m, f.cam.camWorld, sizeof(cw.m));
		else
			cw = PathCameraWorld(f.cam.view[v]);
		pm::frameConstants(cw, vp[v], cb1);
		if(st.ok())
			st = m_pathR->draw(ctx, items, cb1);
		if(!st.ok())
		{
			pathFail(m_pathC.drawErrors, "draw", st, f.frameId);
			return;
		}
		m_pathC.draws += (int64_t)items.size();
		drew = true;
	}
	if(drew)
		m_pathC.drawnFrames++;
	if(animated)
		m_dirty = true;   // the destination ring turns: the next frame is drawn also when the camera holds still
}

void IceFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.iceFailed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("ice: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), kIceFatalExitCode);
		fatal(kIceFatalExitCode, why.c_str());
	}
	else
		RNote("ice: %s; no fatal hook (HostConfig::fatal): the server draws on without the ice", why.c_str());
}

// ---- epic 0454, ticket 4938: the extras module (extras/srv_extras.h) ----
namespace {
void ExtrasLog(void*, const char* line) { RNote("%s", line); }
}  // namespace

void Scene::configureExtras(const Vp64Opt::Options& o, bool fogConfigured)
{
	m_extrasConfigured = true;
	// mc-red 2026-11-22: only the extras this module draws count (srvextras::SceneExtrasWanted). The loop over every
	// [Extras] row it replaced also counted the unit extras and FixRoadLayout, which other modules own: once FixAirBases
	// defaulted to 1 (D235) every scene made the module for nothing and a program without server/src/extras/ was LOUD.
	if(!srvextras::SceneExtrasWanted(o))
	{
		m_extras.reset();   // no extra of this module in effect: no module, no extras code runs
		return;
	}
	m_extras = srvextras::MakeSceneExtras();
	if(!m_extras)
	{
		RNote("LOUD: extras: an [Extras] option is in effect but this program does not link server/src/extras/: no extra is drawn");
		return;
	}
	m_extras->setLog(&ExtrasLog, nullptr);
	m_extras->configure(o, fogConfigured);
	if(!m_extras->anyOn())
	{
		m_extras.reset();   // each extra that cannot run said why (its configure line)
		return;
	}
	if(m_dev)
		m_extras->initDevice(m_dev);
}

void Scene::extrasDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
                       const SceneFrame& f)
{
	if(!m_extras)
		return;
	srvextras::ExtrasFrame ef;
	ef.rtv = rtv;
	ef.dsv = dsv;
	ef.width = w;
	ef.height = h;
	for(int i = 0; i < 4; ++i)   // view 0 x projection (row vectors), as arcDraw
		for(int j = 0; j < 4; ++j)
		{
			float a = f.cam.view[0][i * 4 + 0] * f.cam.proj[0 * 4 + j];
			a += f.cam.view[0][i * 4 + 1] * f.cam.proj[1 * 4 + j];
			a += f.cam.view[0][i * 4 + 2] * f.cam.proj[2 * 4 + j];
			a += f.cam.view[0][i * 4 + 3] * f.cam.proj[3 * 4 + j];
			ef.viewProj[i * 4 + j] = a;
		}
	ef.grid = m_haveGrid ? &m_grid : nullptr;
	const std::vector<c5r::fow::PlotState>& st = m_fow.states();
	if(m_fow.state() == FowScene::kActive && !st.empty() &&
	   st.size() == (size_t)m_fow.tableWidth() * (size_t)m_fow.tableHeight())
	{
		ef.fowStates = st.data();
		ef.fowW = m_fow.tableWidth();
		ef.fowH = m_fow.tableHeight();
		ef.fowGeneration = m_fow.fogGeneration();
	}
	ef.frameId = f.frameId;
	m_extras->draw(ctx, ef);
}

}  // namespace srvrender

// epic 0454, ticket 4938: the extras module's factory (extras/srv_extras.h), the pattern of the model module's below
namespace srvextras {
namespace {
SceneExtrasFactory g_extrasFactory = nullptr;
}
void RegisterSceneExtrasFactory(SceneExtrasFactory f) { g_extrasFactory = f; }
std::unique_ptr<SceneExtras> MakeSceneExtras() { return g_extrasFactory ? g_extrasFactory() : std::unique_ptr<SceneExtras>(); }
// mc-red 2026-11-22: the ids Extras::configure (srv_extras.cpp) reads; a new scene extra is added here too
bool SceneExtrasWanted(const Vp64Opt::Options& o)
{
	static const Vp64Opt::Id kIds[] = {Vp64Opt::kVisibleAreaOutline};
	for(Vp64Opt::Id id : kIds)
		if(Vp64Opt::Extra(o, id).on)
			return true;
	return false;
}
// ticket 4934: FixRoadLayout's producer, the same way
namespace {
SceneRoadsFactory g_roadsFactory = nullptr;
}
void RegisterSceneRoadsFactory(SceneRoadsFactory f) { g_roadsFactory = f; }
std::unique_ptr<SceneRoads> MakeSceneRoads() { return g_roadsFactory ? g_roadsFactory() : std::unique_ptr<SceneRoads>(); }
}  // namespace srvextras

// epic 0375, issue 4454: the unit scene's factory (srv_units_scene.h). Here, in a render unit every program links, so
// that a program without server/src/units/ links too and says so (MakeSceneUnits null) instead of failing to link.
namespace srvunits {
namespace {
SceneUnitsFactory g_unitsFactory = nullptr;
}
void RegisterSceneUnitsFactory(SceneUnitsFactory f) { g_unitsFactory = f; }
bool SceneUnitsAvailable() { return g_unitsFactory != nullptr; }
std::unique_ptr<SceneUnits> MakeSceneUnits(const UnitsOptions& o)
{
	return g_unitsFactory ? g_unitsFactory(o) : std::unique_ptr<SceneUnits>();
}
// epic 0040, issue 4753: the mode word's writer (srv_units_mode.h), the same pattern
namespace {
ModeWriterFactory g_modeFactory = nullptr;
}
void RegisterModeWriterFactory(ModeWriterFactory f) { g_modeFactory = f; }
std::unique_ptr<ModeWriter> MakeModeWriter() { return g_modeFactory ? g_modeFactory() : std::unique_ptr<ModeWriter>(); }
}  // namespace srvunits

// epic 0561, ticket 4875: the model module's factory (landmarks/srv_models_scene.h), the same pattern
namespace srvmodels {
namespace {
ModelsSceneFactory g_modelsFactory = nullptr;
}
void RegisterModelsSceneFactory(ModelsSceneFactory f) { g_modelsFactory = f; }
std::unique_ptr<ModelsScene> MakeModelsScene() { return g_modelsFactory ? g_modelsFactory() : std::unique_ptr<ModelsScene>(); }
}  // namespace srvmodels
