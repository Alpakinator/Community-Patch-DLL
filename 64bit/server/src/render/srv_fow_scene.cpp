// srv_fow_scene.cpp - the fog of war of the renderer inside server.exe (epic 0168, task 3954). Origin: srv_fow_scene.h.
// Written 2026-11-14 for this repository (GPL-3.0-or-later).
#include "srv_fow_scene.h"
#include "srv_render_scene.h"
#include "c5r/water/water.h"   // worldFromGrid: the world box bit-equal to the EXE's globals
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace srvrender {

namespace cf = c5r::fow;

namespace {

const double kClockMaxStepMs = 250.0;   // FOWAnim's T: a pause between two drawn frames counts at most this much
const uint32_t kFieldMarginRows = 4;    // HeightGrid::mapH = the DLL's map height + 4 (heightfield.h)
// Ticket 0491: the install's wait counts the EXE's progress, not only time. The overlay's pair is taken by the composite at
// an EXE Present of a map frame and published at a later one, so a pair cannot come before the EXE has presented a few map
// frames. On 9074's huge late loads the EXE's main thread is still busy after the map's first frame and presents only now
// and then (T650, 2026-11-17: Presents 38 -> 43 in 9.4 s, the pair the copy of Present 41; T1100: fewer than that in 10 s,
// so the 10-s wait failed with the overlay WAITING). So installWaitMs ends the wait only once the EXE has presented at least
// kInstallPresents map frames since the first wanted frame (the composite's tap gives up on its own after 30 unresolved map
// frames, overlay_fail_frames: a tap that had that many and still gave no pair is a fault); an EXE that presents fewer is a
// load still going on and is waited for up to installWaitMs * kInstallSlowFactor of the fog's own time (the slow-load bound,
// 120 s at the product's 10 s; fog-of-war.md 7.6 has the measurements it comes from). A real fault (an EXE that presents
// normally, or one that never comes back) still ends the game by D118's route, at 10 s or at the bound.
const int64_t kInstallPresents = 30;
const double kInstallSlowFactor = 12.0;

bool Finite(const float* p, int n)
{
	for(int i = 0; i < n; ++i)
		if(!std::isfinite(p[i]))
			return false;
	return true;
}

// a * b, row-major 4 x 4 (row vectors: x * a * b)
void Mul(const float* a, const float* b, float* out)
{
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
		{
			double s = 0;
			for(int k = 0; k < 4; ++k)
				s += (double)a[r * 4 + k] * (double)b[k * 4 + c];
			out[r * 4 + c] = (float)s;
		}
}

// c5r::fow behind FowModules
class C5rFowModules : public FowModules
{
public:
	c5r::Status initMask(ID3D11Device* dev, const std::wstring& exe, uint32_t size) override
	{
		m_mask.reset(new cf::MaskRenderer());
		return m_mask->init(dev, exe, size);
	}
	c5r::Status initClouds(ID3D11Device* dev, const std::wstring& exe, const std::wstring& gameDir) override
	{
		m_cloud.reset(new cf::CloudRenderer());
		return m_cloud->init(dev, exe, gameDir);
	}
	c5r::Status buildMask(ID3D11DeviceContext* ctx, const cf::MaskInput& in, cf::MaskStats* st) override
	{
		return m_mask ? m_mask->build(ctx, in, st) : c5r::Status::fail(c5r::Err::NotFound, "the mask renderer is not initialised");
	}
	c5r::Status drawClouds(ID3D11DeviceContext* ctx, const cf::CloudInput& in, cf::CloudStats* st) override
	{
		return m_cloud ? m_cloud->draw(ctx, in, st) : c5r::Status::fail(c5r::Err::NotFound, "the cloud renderer is not initialised");
	}
	ID3D11ShaderResourceView* mask() const override { return m_mask ? m_mask->srv() : nullptr; }
	ID3D11ShaderResourceView* cloudBase() const override { return m_cloud ? m_cloud->cloudBase() : nullptr; }
	// ticket 0340: the FOWLevel-3 particles
	c5r::Status initParticles(ID3D11Device* dev, const std::wstring& exe, const std::wstring& gameDir) override
	{
		m_part.reset(new cf::ParticleRenderer());
		return m_part->init(dev, exe, gameDir);
	}
	c5r::Status resetParticles(float worldWidth, float worldHeight, uint32_t seed) override
	{
		return m_part ? m_part->reset(worldWidth, worldHeight, seed)
		              : c5r::Status::fail(c5r::Err::NotFound, "the particle renderer is not initialised");
	}
	c5r::Status drawParticles(ID3D11DeviceContext* ctx, const cf::ParticleInput& in, cf::ParticleStats* st) override
	{
		return m_part ? m_part->frame(ctx, in, st) : c5r::Status::fail(c5r::Err::NotFound, "the particle renderer is not initialised");
	}
	cf::ParticleSim* particleSim() override { return m_part ? &m_part->sim() : nullptr; }
	void release() override
	{
		m_mask.reset();
		m_cloud.reset();
		m_part.reset();
	}

private:
	std::unique_ptr<cf::MaskRenderer> m_mask;
	std::unique_ptr<cf::CloudRenderer> m_cloud;
	std::unique_ptr<cf::ParticleRenderer> m_part;
};

class FeedPlotTable : public FowPlotTable
{
public:
	void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h) override
	{
		SrvFeed::CopyPlots(out, gen, w, h);
	}
};

}  // namespace

std::unique_ptr<FowModules> MakeC5rFowModules() { return std::unique_ptr<FowModules>(new C5rFowModules()); }
std::unique_ptr<FowPlotTable> MakeFeedPlotTable() { return std::unique_ptr<FowPlotTable>(new FeedPlotTable()); }

int ReadFowLevel(std::string& from)
{
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
	if(docs.empty())
	{
		from = "no Documents folder (C5_DOCUMENTS not set, SHGetFolderPathW failed)";
		return -1;
	}
	const std::wstring path = docs + L"\\My Games\\Sid Meier's Civilization 5\\GraphicsSettingsDX11.ini";
	from = Narrow(path);
	FILE* fp = nullptr;
	if(_wfopen_s(&fp, path.c_str(), L"rb") || !fp)
	{
		from += " (not readable)";
		return -1;
	}
	char line[512];
	int level = -1;
	while(fgets(line, sizeof(line), fp))
	{
		const char* p = line;
		while(*p == ' ' || *p == '\t')
			++p;
		if(_strnicmp(p, "FOWLevel", 8) != 0)
			continue;
		p += 8;
		while(*p == ' ' || *p == '\t')
			++p;
		if(*p != '=')
			continue;
		++p;
		char* end = nullptr;
		const long v = strtol(p, &end, 10);
		while(end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
			++end;
		level = (end && end != p && !*end && v >= 0 && v <= 3) ? (int)v : -2;
		break;
	}
	fclose(fp);
	if(level == -1)
		from += " (no FOWLevel key)";
	else if(level == -2)
	{
		from += " (FOWLevel not 0..3)";
		level = -1;
	}
	return level;
}

const char* FowScene::StateName(int s)
{
	static const char* kNames[kStateCount] = {"OFF", "WAITING", "ACTIVE", "FAILED", "WITHOUT"};
	return s >= 0 && s < kStateCount ? kNames[s] : "?";
}

FowScene::FowScene(std::unique_ptr<FowModules> m, std::unique_ptr<FowPlotTable> t) : m_mod(std::move(m)), m_table(std::move(t)) {}

FowScene::~FowScene() { releaseDevice(); }

void FowScene::init(ID3D11Device* dev, const FowSceneOptions& o)
{
	releaseDevice();
	const int before = m_state;
	m_opt = o;
	if(!o.on)
	{
		m_state = kOff;
		RNote("fog: render_fow=0: no fog of war (terrain t4 / t5 and water t10 keep their constants, no cloud planes)");
		return;
	}
	if(!m_mod)
		m_mod = MakeC5rFowModules();
	if(!m_table)
		m_table = MakeFeedPlotTable();
	// FOWLevel: render_fow_level, else the player's GraphicsSettingsDX11.ini, else 3 (stock's level on every install
	// measured, fog-of-war.md 3.6 [I that a missing key means 3])
	if(o.level >= 0)
	{
		m_level = o.level;
		m_levelFrom = "server.cfg render_fow_level";
	}
	else
	{
		m_level = ReadFowLevel(m_levelFrom);
		if(m_level < 0)
		{
			m_level = 3;
			m_levelFrom += ": 3 taken";
		}
	}
	m_maskSize = FowMaskSize(m_level);
	const int64_t q0 = Qpc();
	m_maskInit = m_mod->initMask(dev, o.exe, m_maskSize);
	m_cloudInit = m_mod->initClouds(dev, o.exe, o.gameDir);
	// ticket 0340: the particles at FOWLevel 3 (not an install failure when they cannot start: stock falls back to level 2)
	m_partOn = false;
	m_partReset = false;
	m_partModule = false;
	std::string partNote = "particles off (FOWLevel below 3)";
	if(m_level >= 3)
	{
		if(!RenderFowParticlesOption())
			partNote = "particles off (C5_RENDER_FOW_PARTICLES=0)";
		else if(m_partPlayer)
			partNote = "particles off (the offline player after its clock seed: a device rebuild does not restart them)";
		else if(!m_maskInit.ok() || !m_cloudInit.ok())
			partNote = "particles not started (the mask or cloud module failed: the install's failure)";
		else
		{
			m_partSeed = RenderFowParticleSeed();   // ticket 0403: read once (the recorder takes it with the state)
			m_partInit = m_mod->initParticles(dev, o.exe, o.gameDir);
			m_partOn = m_partInit.ok();
			m_partModule = m_partOn;
			if(m_partOn)
			{
				char b[96];
				_snprintf_s(b, sizeof(b), _TRUNCATE, "particles on (seed %u)", m_partSeed);
				partNote = b;
			}
			else if(m_partInit.code == c5r::Err::Unsupported)
				partNote = "particles off (" + m_partInit.str() + ")";
			else
			{
				partNote = "particles off: the module could not start, drawn as FOWLevel 2 as stock does without its particle files";
				RNote("LOUD: fog: the particle module could not start (%s): the fog is drawn as at FOWLevel 2 (fog-of-war.md 10.1)",
				      m_partInit.str().c_str());
			}
		}
	}
	m_modInit = true;
	// a failure here is reported at the first frame that wants to be drawn (the install, design 5.5), not at the
	// renderer's start: the front end and the load screen draw no map
	RNote("fog: FOWLevel %d (%s): mask %ux%u, cloud planes on (level 0 is drawn as level 1), %s; "
	      "mask module %s, cloud module %s (%.1f ms)",
	      m_level, m_levelFrom.c_str(), m_maskSize, m_maskSize, partNote.c_str(), m_maskInit.ok() ? "ready" : m_maskInit.str().c_str(),
	      m_cloudInit.ok() ? "ready" : m_cloudInit.str().c_str(), QpcToMs(Qpc() - q0));
	if(before == kFailed || before == kWithout || before == kActive)
		m_state = before;   // a device rebuild keeps the install's outcome
	else
		m_state = kWaiting;
	m_built = false;
	m_maskOk = false;
	if(!m_haveTable)
		readTable("the renderer's start");
}

void FowScene::releaseDevice()
{
	SafeRelease(m_scratchDsv);
	SafeRelease(m_scratchTex);
	for(PartQuery& q : m_pq)
	{
		SafeRelease(q.dis);
		SafeRelease(q.t0);
		SafeRelease(q.t1);
		q.pending = false;
	}
	m_partReset = false;   // a new device: the particles start again (their module is rebuilt)
	m_partModule = false;  // ticket 0403
	m_pub.stop("the fog's device objects are released");   // ticket 0339 (before the mask it copies from goes)
	if(m_mod)
		m_mod->release();
	m_modInit = false;
	m_built = false;
	m_maskOk = false;
	m_terrain = FowTerrainInputs();
}

void FowScene::loud(std::string& last, const std::string& now, const char* fmt, ...)
{
	if(now == last)
		return;
	last = now;
	char b[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	RNote("LOUD: fog: %s", b);
}

void FowScene::readTable(const char* why)
{
	if(!m_table)
		return;
	uint32_t gen = 0, w = 0, h = 0;
	m_table->copy(m_rec, gen, w, h);
	rereads++;
	m_recUsed = false;   // ticket 5052 (T): plotTable() hands m_rec out only after a read that was used
	if(m_rec.empty() || (uint64_t)w * h != m_rec.size())
	{
		if(!m_rec.empty())
			loud(m_lastSize, "size", "the plot table holds %zu records for %ux%u plots (%s): not used", m_rec.size(), w, h, why);
		return;
	}
	const bool sizeChanged = !m_haveTable || w != m_w || h != m_h;
	if(sizeChanged)
		m_states.assign((size_t)w * h, cf::PlotState::Unexplored);
	bool changed = sizeChanged || gen != m_tableGen;
	uint32_t revealed = 0, bad = 0, firstBad = 0;
	for(size_t i = 0; i < m_rec.size(); ++i)
	{
		const uint8_t bits = m_rec[i].fow;
		if(bits > 3)
		{
			if(!bad++)
				firstBad = (uint32_t)i;
		}
		const cf::PlotState s = cf::stateFromFowBits(bits);
		if(s != m_states[i])
		{
			m_states[i] = s;
			changed = true;
			stateChanges++;
		}
		if(s != cf::PlotState::Unexplored)
			revealed++;
	}
	if(bad)
	{
		badBits += bad;
		char t[64];
		_snprintf_s(t, sizeof(t), _TRUNCATE, "bits %u", bad);
		loud(m_lastBits, t, "%u plot(s) carry fog bits above bit 1 (first plot %u: 0x%02X); bits 0-1 used (%s)", bad, firstBad,
		     m_rec[firstBad].fow, why);
	}
	if(!m_haveTable || sizeChanged)
		RNote("fog: plot table %ux%u (generation %u, %u plots revealed) read at %s", w, h, gen, revealed, why);
	m_w = w;
	m_h = h;
	m_tableGen = gen;
	m_haveTable = true;
	m_recUsed = true;
	m_revealed = revealed;
	if(changed)
		m_fogGen++;
}

bool FowScene::onBatch(const SrvFeed::Batch& b)
{
	if(m_state == kOff)
		return false;
	const uint32_t genBefore = m_fogGen;
	bool reread = !m_haveTable || b.snapshotGen != m_tableGen;
	const char* why = !m_haveTable ? "a feed batch before any table" : "a new snapshot";
	for(const SrvFeed::Event& e : b.events)
	{
		if(m_lastSeq && e.seq != m_lastSeq + 1)
		{
			seqGaps++;
			char t[96];
			_snprintf_s(t, sizeof(t), _TRUNCATE, "gap %u", seqGaps <= 3 ? (uint32_t)seqGaps : 0u);
			loud(m_lastGap, t, "feed sequence gap: event %u after %u (%lld so far): the plot table is read again", e.seq, m_lastSeq,
			     (long long)seqGaps);
			reread = true;
			why = "a sequence gap";
		}
		m_lastSeq = e.seq;
		if(e.kind == SrvFeed::kOverflow)
		{
			overflows++;
			if(overflows <= 3)
				RNote("LOUD: fog: the feed overflowed (%d events dropped; %lld overflows so far): the plot table is read again",
				      e.a[0], (long long)overflows);
			reread = true;
			why = "an overflow";
		}
		else if(e.kind == SrvFeed::kFowChanged)
		{
			fowEvents++;
			const int32_t x = e.a[2], y = e.a[3];
			if(m_haveTable && !e.a[1] && (x < 0 || y < 0 || (uint32_t)x >= m_w || (uint32_t)y >= m_h))
			{
				outsideEvents++;
				char t[64];
				_snprintf_s(t, sizeof(t), _TRUNCATE, "outside %d", outsideEvents <= 3 ? (int)outsideEvents : 0);
				loud(m_lastOutside, t, "a fog event for plot (%d, %d) outside the %ux%u grid (mode %d; %lld so far): ignored", x, y, m_w,
				     m_h, e.a[0], (long long)outsideEvents);
			}
		}
	}
	if(b.dropped != m_lastDropped)
	{
		m_lastDropped = b.dropped;
		reread = true;
		why = "dropped events";
	}
	if(reread || !b.changedPlots.empty())
		readTable(reread ? why : "changed plots");
	return m_fogGen != genBefore;
}

void FowScene::fail(const std::string& why)
{
	if(m_state == kFailed || m_state == kWithout)
		return;
	m_state = kFailed;
	m_failWhy = "fog of war: " + why + "; unrevealed plots cannot be hidden, no frame is drawn (design 5.5, D118, D125)";
	RNote("LOUD: %s", m_failWhy.c_str());
}

void FowScene::drawWithout()
{
	if(m_state != kFailed)
		return;
	m_state = kWithout;
	RNote("fog: %s; no fatal hook (HostConfig::fatal): the server draws on WITHOUT the fog (tests and test hosts only)",
	      m_failWhy.c_str());
}

bool FowScene::pairUsable(const OverlayFrame& p, std::string& why) const
{
	if(p.numViews == 0 || p.numViews > VPSHELL_OVERLAY_MAX_VIEWS || p.numViews > cf::kMaxViews)
	{
		why = "numViews " + std::to_string(p.numViews) + " outside 1.." + std::to_string(VPSHELL_OVERLAY_MAX_VIEWS);
		return false;
	}
	for(uint32_t v = 0; v < p.numViews; ++v)
	{
		if(!Finite(p.overlayProj[v], 16))
		{
			why = "the OverlayProj of view " + std::to_string(v) + " is not finite";
			return false;
		}
		if(p.overlayProj[v][0] == 0.0f || p.overlayProj[v][5] == 0.0f)
		{
			why = "the OverlayProj of view " + std::to_string(v) + " has a zero x or y scale";
			return false;
		}
	}
	return true;
}

// Ticket 0429: the fog's own clock. The install wait (and the in-play hold's LOUD bound) counted wall time, so the
// server's own post-field render-thread stall (0407: 8.7-23.4 s in the NVIDIA driver on a loaded board) ate it: 0415's
// run a5 (2026-11-16 12:15) held from 12:14:51.834, the thread did not come back from 12:14:52.83 to 12:15:05.49, and the
// install failed 'no overlay pair yet (overlay ACTIVE) 13657 ms after the first frame of the map' 34 ms before the
// overlay's first pair was bound. Now each admit() to admit() gap counts up to stallGapMs; the rest is a render-thread
// gap (a stall, or no frame wanted: a hidden camera), kept apart and named in the failure's text.
void FowScene::tickClock(double nowMs)
{
	if(m_lastAdmitMs >= 0 && nowMs > m_lastAdmitMs)
	{
		const double d = nowMs - m_lastAdmitMs;
		const double cap = m_opt.stallGapMs > 0 ? m_opt.stallGapMs : 1000.0;
		if(d > cap)
		{
			m_ownMs += cap;
			m_gapMs += d - cap;
			m_gaps++;
			if(d > m_gapMaxMs)
				m_gapMaxMs = d;
			if(m_state == kWaiting || m_holding)
				RNote("fog: the render thread came back after %.0f ms (frame wanted %s): %.0f ms of it not counted in the fog's %s wait "
				      "(above %.0f ms between two checks: the server stalled or no frame was wanted; ticket 0429)",
				      d, m_state == kWaiting ? "for the install" : "while held in play", d - cap,
				      m_state == kWaiting ? "install" : "hold", cap);
		}
		else
			m_ownMs += d;
	}
	m_lastAdmitMs = nowMs;
}

std::string FowScene::waitedText(double nowMs) const
{
	const double gap = m_gapMs - m_firstWantGapMs;
	const int64_t n = m_gaps - m_firstWantGaps;
	char b[320];
	if(n == 0)
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%.0f ms wall time, no render-thread gap above %.0f ms", nowMs - m_firstWantMs,
		            m_opt.stallGapMs);
	else
		_snprintf_s(b, sizeof(b), _TRUNCATE,
		            "%.0f ms wall time; %.0f ms of it not counted: %lld render-thread gap(s) above %.0f ms between two checks, the "
		            "longest %.0f ms (the server stalled, or no frame was wanted)",
		            nowMs - m_firstWantMs, gap, (long long)n, m_opt.stallGapMs, m_gapMaxMs);
	return b;
}

bool FowScene::admit(const VPShellCamera& cam, uint32_t mapW, uint32_t mapH, const OverlayTapReader::State* ovl,
                     const OverlayFrame* pair, double nowMs)
{
	m_holdBits = 0;   // ticket 0691: set again below only when this frame is held for inputs
	m_holdFrame = 0;
	if(m_state == kOff || m_state == kWithout)
		return true;
	tickClock(nowMs);
	// the pair of this frame: a usable one replaces the kept one, a bad one is counted and the last good one kept
	if(pair && (!m_havePair || pair->generation != m_pairGen || pair->slot != m_pairSlot ||
	            memcmp(pair->overlayProj, m_pairProj, sizeof(m_pairProj)) != 0 || pair->numViews != m_pairViews))
	{
		std::string bad;
		if(pairUsable(*pair, bad))
		{
			m_havePair = true;
			m_pairViews = pair->numViews;
			m_pairGen = pair->generation;
			m_pairSlot = pair->slot;
			m_pairPresent = pair->presentCount;
			memcpy(m_pairProj, pair->overlayProj, sizeof(m_pairProj));
			m_lastBadPair.clear();
		}
		else if(pair->generation != m_pairGen || pair->slot != m_pairSlot || !m_havePair)
		{
			badPairs++;
			loud(m_lastBadPair, bad, "overlay pair of generation %u not used for the mask: %s (%s; %lld so far)", pair->generation,
			     bad.c_str(), m_havePair ? "the last good pair kept" : "no good pair yet", (long long)badPairs);
			m_pairGen = pair->generation;   // counted once per generation
			m_pairSlot = pair->slot;
		}
	}
	const bool held = cam.frameStamp != m_heldStamp;
	if(m_state == kFailed)
	{
		if(held)
		{
			m_heldStamp = cam.frameStamp;
			framesHeld++;
			m_heldSec++;
		}
		return false;
	}
	const bool installing = m_state == kWaiting;
	if(installing)
	{
		if(m_firstWantMs < 0)
		{
			m_firstWantMs = nowMs;
			m_firstWantOwnMs = m_ownMs;
			m_firstWantGapMs = m_gapMs;
			m_firstWantGaps = m_gaps;
			m_gapMaxMs = 0;
		}
		if(!m_modInit)
		{
			fail("the fog modules were not started (no device)");
			return false;
		}
		if(!m_maskInit.ok())
		{
			fail("the FOW mask (c5r::fow::MaskRenderer: the EXE's FOWDrawMask programs, " + std::to_string(m_maskSize) + " target) could not start: " +
			     m_maskInit.str());
			return false;
		}
		if(!m_cloudInit.ok())
		{
			fail("the cloud planes (c5r::fow::CloudRenderer: FOWBaseLayer, fogcloudtile_dif_01..03.dds) could not start: " + m_cloudInit.str());
			return false;
		}
		if(!m_opt.overlay || !ovl)
		{
			fail("render_fow=1 needs the overlay pair (the mask shares its window), but render_overlay=0 (decision 5 (ii))");
			return false;
		}
		if(*ovl == OverlayTapReader::State::Off)
		{
			fail("render_fow=1 needs the overlay pair (the mask shares its window), but the overlay tap is OFF (overlay_tap=0 or "
			     "its kill switch; decision 5 (ii))");
			return false;
		}
	}
	std::string wait;
	uint32_t holdBits = 0;   // ticket 0691: the reason bits of the wait below (holdReasons)
	if(!m_haveTable)
	{
		wait = "no plot table from the feed";
		holdBits = kHoldNoTable;
	}
	else if(m_w != mapW || m_h + kFieldMarginRows != mapH)
	{
		holdBits = kHoldTableSize;
		// the field's grid has the DLL's height + 4 margin rows (heightfield.h HeightGrid::mapH [L]: 56x40 for 56x36 on
		// 3951's Tiny, 3954's live run 2026-11-14 23:10, which first compared the two directly and failed the install)
		wait = "the plot table is " + std::to_string(m_w) + "x" + std::to_string(m_h) + ", the field's map " + std::to_string(mapW) +
		       "x" + std::to_string(mapH >= kFieldMarginRows ? mapH - kFieldMarginRows : 0) + " (grid " + std::to_string(mapW) + "x" +
		       std::to_string(mapH) + ")";
	}
	else if(installing && m_revealed == 0)
	{
		wait = "no revealed plot in the table (no fog bits: the active team below 0?)";
		holdBits = kHoldNoRevealed;
	}
	else if(!m_havePair)
	{
		wait = std::string("no overlay pair yet (overlay ") + (ovl ? OverlayStateName(*ovl) : "NONE") + ")";
		holdBits = kHoldNoPair;
	}
	if(wait.empty())
	{
		if(m_holding && !installing)
			RNote("fog: drawing again after %lld held frames", (long long)(framesHeld - m_holdFrom));
		m_holding = false;
		if(installing)
		{
			m_state = kActive;
			RNote("fog: installed at frame %lld after %.0f ms of the fog's own wait (%s): table %ux%u (%u revealed, fog generation %u), "
			      "pair generation %u (%u views), %lld frames held before",
			      (long long)cam.frameStamp, m_ownMs - m_firstWantOwnMs, waitedText(nowMs).c_str(), m_w, m_h, m_revealed, m_fogGen,
			      m_pairGen, m_pairViews, (long long)framesHeld);
		}
		return true;
	}
	// ticket 0429: the fog's own time since the first wanted frame (render-thread gaps above stallGapMs not counted);
	// ticket 0491: installWaitMs ends it only after kInstallPresents map frames of the EXE, else the slow-load bound does
	if(installing && m_ownMs - m_firstWantOwnMs > m_opt.installWaitMs)
	{
		const double own = m_ownMs - m_firstWantOwnMs;
		const double slowMs = m_opt.installWaitMs * kInstallSlowFactor;
		// the EXE's map frames held since the first wanted frame, this one included (the hold began at that frame)
		const int64_t presents = (m_holding ? framesHeld - m_holdFrom : 0) + (held ? 1 : 0);
		char pres[400];
		_snprintf_s(pres, sizeof(pres), _TRUNCATE,
		            "%lld map frame(s) presented by the EXE in the wait; the wait ends after %.0f ms once the EXE has presented %lld, "
		            "else after the slow-load bound of %.0f ms (ticket 0491)",
		            (long long)presents, m_opt.installWaitMs, (long long)kInstallPresents, slowMs);
		if(presents >= kInstallPresents || own > slowMs)
		{
			fail(wait + " " + std::to_string((long long)own) + " ms of the fog's own wait after the first frame of the map (" + waitedText(nowMs) +
			     "; " + pres + ")");
			return false;
		}
		if(!m_holdLoud)
		{
			// m_holdLoud marks the in-play hold's LOUD line; while installing it marks this note (once per install)
			m_holdLoud = true;
			RNote("fog: the install's wait passed %.0f ms with only %lld map frame(s) presented by the EXE (its load goes on: the "
			      "overlay's pair is taken at a Present): waiting on (%s) while %s",
			      m_opt.installWaitMs, (long long)presents, pres, wait.c_str());
		}
	}
	if(!m_holding)
	{
		// one note per episode (a load: the new map's table or pair not there yet); in play LOUD once it outlasts the wait
		m_holding = true;
		m_holdFrom = framesHeld;
		m_holdSinceMs = nowMs;
		m_holdOwnMs = m_ownMs;
		m_holdLoud = false;
		RNote("fog: frames held from frame %lld: %s", (long long)cam.frameStamp, wait.c_str());
	}
	else if(!installing && !m_holdLoud && m_ownMs - m_holdOwnMs > m_opt.installWaitMs)
	{
		m_holdLoud = true;
		RNote("LOUD: fog: frames held for %.0f ms of the fog's own time in play (%.0f ms wall time): %s (%lld frames so far; never "
		      "fatal in play)",
		      m_ownMs - m_holdOwnMs, nowMs - m_holdSinceMs, wait.c_str(), (long long)(framesHeld - m_holdFrom));
	}
	if(held)
	{
		m_heldStamp = cam.frameStamp;
		framesHeld++;
		m_heldSec++;
	}
	// ticket 0691 (MC #11169): this frame is held for inputs later EXE frames bring: the host reports it to the composite
	m_holdBits = holdBits | (installing ? (uint32_t)kHoldInstall : 0u);
	m_holdFrame = cam.frameStamp;
	return false;
}

ID3D11ShaderResourceView* FowScene::beforeWorld(ID3D11DeviceContext* ctx, const VPShellCamera& cam, const OverlayFrame* pair, double nowMs,
                                                const c5r::heightfield::HeightGrid* grid)
{
	if(m_state == kWithout)
	{
		framesWithout++;
		m_terrain = FowTerrainInputs();
		return nullptr;
	}
	if(m_state != kActive || !m_mod || !m_havePair)
	{
		m_terrain = FowTerrainInputs();
		return nullptr;
	}
	// the pair was taken in admit() for this frame (decide() runs it just before the draw); none now = the last good one
	// kept (the tap stale or lost, design 5.5: counted, not LOUD)
	if(!pair)
		keptPairFrames++;
	// FOWAnim (k, k, -2 T, 0): T the server's clock while the map is drawn (only its phase differs from stock's)
	if(!m_opt.freeze)
	{
		if(m_clockLastMs >= 0)
			m_clockS += std::min(std::max(0.0, nowMs - m_clockLastMs), kClockMaxStepMs) / 1000.0;
		m_clockLastMs = nowMs;
	}
	// The world box: the field's (c5r::water::worldFromGrid, bit-equal to the EXE's 0xD90FCC..0xD90FDC, width 3583 and
	// height 2216.094 on Tiny), else the camera block's copy of the box (aabbMin / aabbMax, worldHeight 0xD90FE8)
	cf::CloudWorld cw;
	float ww = 0, wh = 0;
	const bool haveGrid = grid && grid->samplesX > 1 && grid->samplesY > 1;
	if(haveGrid)
	{
		const c5r::water::World wo = c5r::water::worldFromGrid(*grid);
		ww = wo.width;
		wh = wo.height;
		cw.minX = wo.minX;
		cw.minY = wo.minY;
		cw.maxX = wo.maxX;
		cw.maxY = wo.maxY;
		cw.samplesX = grid->samplesX;
		cw.samplesY = grid->samplesY;
		cw.sx = grid->sx;
		cw.sy = grid->sy;
		cw.ox = grid->ox;
		cw.oy = grid->oy;
		memcpy(cw.hexToWorld, grid->hexToWorld, sizeof(cw.hexToWorld));
	}
	else
	{
		ww = cam.aabbMax[0] - cam.aabbMin[0];
		wh = cam.aabbMax[1] - cam.aabbMin[1];
		cw.minX = cam.aabbMin[0];
		cw.minY = cam.aabbMin[1];
		cw.maxX = cam.aabbMax[0];
		cw.maxY = cam.aabbMax[1];
		cw.samplesX = cam.samplesX;
		cw.samplesY = cam.samplesY;
		cw.sx = cam.sx;
		cw.sy = cam.sy;
		cw.ox = cam.ox;
		cw.oy = cam.oy;
		memcpy(cw.hexToWorld, cam.h2w, sizeof(cw.hexToWorld));
	}
	if(!(std::isfinite(ww) && ww > 0))
		ww = std::isfinite(cam.wrapPeriod) && cam.wrapPeriod > 0 ? cam.wrapPeriod : 0.0f;
	if(!(std::isfinite(wh) && wh > 0))
		wh = std::isfinite(cam.worldHeight) && cam.worldHeight > 0 ? cam.worldHeight : 0.0f;
	m_worldWidth = ww;
	FowTerrainInputs t;
	cf::fowAnim(ww, m_clockS, t.fowAnim);
	// the views: the mask's window (the pair's OverlayProj, or ticket 0674's made from camera N when its footprint left the
	// pair's window), the camera block's view x projection (the plot range by stock's rule, the clouds)
	const uint32_t camViews = cam.numViews >= 1 && cam.numViews <= 4 ? cam.numViews : 1;
	if(camViews != m_pairViews)
		viewMismatch++;
	const bool camWin = chooseWindow(cam, camViews);
	if(camWin)
	{
		cameraWindowFrames++;
		m_camWinSec++;
		if(!m_camWinNoted)   // once per scene (the offline player's replays say so too); then counted in camwin=
		{
			m_camWinNoted = true;
			const float* p = m_pairProj[0];
			const float* c = m_winProj[0];
			RNote("fog: the mask's window made from the camera at frame %lld (camera N's view left the pair's window "
			      "[%.1f, %.1f] x [%.1f, %.1f]; camera window [%.1f, %.1f] x [%.1f, %.1f]; the first, later ones counted as camwin=, ticket 0674)",
			      (long long)cam.frameStamp, (-1.0 - p[12]) / p[0], (1.0 - p[12]) / p[0], (-1.0 - p[13]) / p[5], (1.0 - p[13]) / p[5],
			      (-1.0 - c[12]) / c[0], (1.0 - c[12]) / c[0], (-1.0 - c[13]) / c[5], (1.0 - c[13]) / c[5]);
		}
	}
	for(uint32_t v = 0; v < m_winViews; ++v)
	{
		cf::View& vw = m_views[v];
		vw = cf::View();
		memcpy(vw.overlayProj, m_winProj[v], sizeof(vw.overlayProj));
		Mul(cam.view[std::min(v, camViews - 1)], cam.proj, vw.viewProj);
	}
	cf::MaskInput in;
	in.map.states = m_states.data();
	in.map.width = m_w;
	in.map.height = m_h;
	in.map.wrapX = cam.wrapX != 0;
	in.map.worldWidth = ww;
	in.map.worldHeight = wh;
	in.map.stateCount = m_states.size();
	// the placement (M_h2w, sx, sy, ox, oy): the field's, else nominalPlacement (counted; noted once: a test or tool)
	bool placed = false;
	for(int i = 0; i < 16; ++i)
		placed = placed || cw.hexToWorld[i] != 0.0f;
	placed = placed && Finite(cw.hexToWorld, 16) && std::isfinite(cw.sx) && std::isfinite(cw.sy) && std::isfinite(cw.ox) &&
	         std::isfinite(cw.oy) && cw.sx != 0.0f && cw.sy != 0.0f;
	if(placed)
	{
		memcpy(in.map.hexToWorld, cw.hexToWorld, sizeof(in.map.hexToWorld));
		in.map.gridScale[0] = cw.sx;
		in.map.gridScale[1] = cw.sy;
		in.map.gridOrigin[0] = cw.ox;
		in.map.gridOrigin[1] = cw.oy;
	}
	else
	{
		cf::nominalPlacement(in.map);
		memcpy(cw.hexToWorld, in.map.hexToWorld, sizeof(cw.hexToWorld));
		cw.sx = in.map.gridScale[0];
		cw.sy = in.map.gridScale[1];
		cw.ox = in.map.gridOrigin[0];
		cw.oy = in.map.gridOrigin[1];
		nominalFrames++;
		if(!m_nominalNoted)
			RNote("fog: the field carries no hex-to-world matrix (%s): nominalPlacement used (a test or tool field)",
			      haveGrid ? "the height field's is all zero or not finite" : "no height field, the camera block's is not usable");
		m_nominalNoted = true;
	}
	in.views = m_views;
	in.numViews = m_winViews;
	m_lastIn = in;
	m_cloudWorld = cw;
	bool builtNow = false;   // ticket 0339: a new mask this frame (handed over to the EXE's model draws)
	// ticket 0674: the window is part of the key (a camera window changes with the camera inside one pair generation)
	if(!m_built || !m_maskOk || m_builtFogGen != m_fogGen || m_builtPairGen != m_pairGen || m_builtPairSlot != m_pairSlot ||
	   m_builtViews != m_winViews || memcmp(m_builtProj, m_winProj, sizeof(m_builtProj)) != 0)
	{
		m_maskStats = cf::MaskStats();
		const c5r::Status st = m_mod->buildMask(ctx, in, &m_maskStats);
		m_built = true;
		m_builtFogGen = m_fogGen;
		m_builtPairGen = m_pairGen;
		m_builtPairSlot = m_pairSlot;
		m_builtViews = m_winViews;
		memcpy(m_builtProj, m_winProj, sizeof(m_builtProj));
		m_maskOk = st.ok();
		if(st.ok())
		{
			builtNow = true;
			masksBuilt++;
			m_builtSec++;
			m_lastBuildErr.clear();
		}
		else
		{
			buildErrors++;
			loud(m_lastBuildErr, st.str(), "the mask build failed (fog generation %u, pair generation %u; %lld so far): %s", m_fogGen,
			     m_pairGen, (long long)buildErrors, st.str().c_str());
		}
	}
	t.on = m_maskOk;
	t.mask = m_maskOk ? m_mod->mask() : nullptr;
	t.cloudBase = m_mod->cloudBase();
	t.cameraWindow = camWin;   // ticket 0674
	t.windowViews = m_builtViews;
	memcpy(t.windowProj, m_builtProj, sizeof(t.windowProj));
	m_terrain = t;
	handOver(ctx, builtNow);
	framesWith++;
	m_withSec++;
	return t.mask;
}

// Ticket 0674 (pan-coherence.md 3): the mask's window of this frame. The pair's (frame N-1's), unless a view of camera N
// sees ground outside it (a zoom-out step, a pan step past the window's margin, a far jump) and render_fow_mask_window is
// camera: then windows made from camera N by the EXE's rule (c5r::fow::windowFromCamera of view 0), each further view's
// window view 0's moved as that camera view's footprint is moved against view 0's (the wrap view: by the wrap period,
// as the EXE's wrap OverlayProj is view 0's moved, overlay-system.md 4). A view the pair does not have counts as outside.
// True when the window was made from the camera.
bool FowScene::chooseWindow(const VPShellCamera& cam, uint32_t camViews)
{
	m_winViews = m_pairViews;
	memcpy(m_winProj, m_pairProj, sizeof(m_winProj));
	if(!m_opt.maskWindowCamera)
		return false;
	cf::Footprint fp[VPSHELL_OVERLAY_MAX_VIEWS];
	bool outside = false;
	for(uint32_t v = 0; v < camViews && v < VPSHELL_OVERLAY_MAX_VIEWS; ++v)
	{
		cf::View vw;
		Mul(cam.view[v], cam.proj, vw.viewProj);
		const c5r::Status st = cf::footprintFromCamera(vw, fp[v]);
		if(!st.ok())
		{
			footprintErrors++;
			loud(m_lastFootprintErr, st.str(), "the camera's footprint of view %u could not be made (%lld so far): %s; the pair's window kept",
			     v, (long long)footprintErrors, st.str().c_str());
			return false;
		}
		if(v >= m_pairViews || !cf::footprintInWindow(fp[v], m_pairProj[v]))
			outside = true;
	}
	if(!outside)
		return false;
	cf::View v0;
	Mul(cam.view[0], cam.proj, v0.viewProj);
	float w0[16];
	if(!cf::windowFromCamera(v0, w0).ok())
		return false;   // not reached: view 0's footprint was made above
	m_winViews = std::min<uint32_t>(camViews, VPSHELL_OVERLAY_MAX_VIEWS);
	memcpy(m_winProj[0], w0, sizeof(w0));
	const double wp = cam.wrapPeriod;
	for(uint32_t v = 1; v < m_winViews; ++v)
	{
		double d = 0.5 * ((fp[v].x0 + fp[v].x1) - (fp[0].x0 + fp[0].x1));   // the view's x shift in world units
		if(std::isfinite(wp) && wp > 0)
		{
			const double k = std::floor(d / wp + 0.5);
			if(k != 0 && std::fabs(d - k * wp) <= 0.5)
				d = k * wp;
		}
		memcpy(m_winProj[v], w0, sizeof(w0));
		m_winProj[v][12] = (float)((double)w0[12] - d * (double)w0[0]);   // the window moved by d: NDC x = (x - d) sx + tx
	}
	return true;
}

// ticket 0339: the newest mask to the EXE's model draws (srv_fow_mask_pub.h). The consumer's FAILED state (a checked EXE
// write did not match, its install could not finish; MC #2058) fails the fog's install: the D125 route through
// FowFatalStep, never a game frame that goes on with a model fog half installed.
void FowScene::handOver(ID3D11DeviceContext* ctx, bool builtNow)
{
	if(m_exePid == 0)
		return;
	FowMaskPubInput in;
	ID3D11Texture2D* tex = nullptr;
	if(m_maskOk && m_terrain.mask)
	{
		ID3D11Resource* r = nullptr;
		m_terrain.mask->GetResource(&r);
		if(r)
			r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
		SafeRelease(r);
		// ticket 0674: the window the mask was built in (the pair's, or one made from the camera): the composite moves the
		// mask from it to the EXE frame's window (fog-of-war.md 9.5)
		in.numViews = std::min<uint32_t>(m_builtViews, VPSHELL_FOWMASK_MAX_VIEWS);
		memcpy(in.overlayProj, m_builtProj, sizeof(float) * 16 * in.numViews);
		in.pairGeneration = m_pairGen;
		in.pairPresentCount = m_pairPresent;
		in.fogGeneration = m_fogGen;
		memcpy(in.fowAnim, m_terrain.fowAnim, sizeof(in.fowAnim));
	}
	m_pub.frame(ctx, m_exePid, tex, builtNow, in);
	SafeRelease(tex);
	std::string why;
	if(m_pub.consumerFailed(why))
		fail(why);
}

void FowScene::afterWorld(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h)
{
	if(m_state != kActive || !m_maskOk || !m_mod)
		return;
	// The cloud pass writes depth (FOW CULL: 0 where no unexplored plot is near) for its planes' depth test only. Stock
	// runs it after the models, so that depth is never read again; in the joined frame the EXE draws its models after the
	// server's image against the server's depth, and FOW CULL's zeros hid every model on visible ground [L, 3954's live
	// fog arm 2026-11-14 23:31: the units and Madrid's buildings missing against the render_fow=0 arm]. So the pass runs
	// on a copy of the scene's depth; the scene's own depth (handed to the composite) stays the world's.
	ID3D11DepthStencilView* cloudDepth = scratchDepth(ctx, dsv);
	if(dsv && !cloudDepth)
		return;   // counted and LOUD in scratchDepth: no clouds this frame rather than hidden models
	cf::CloudInput in;
	in.colour = rtv;
	in.depth = cloudDepth;
	in.width = w;
	in.height = h;
	in.mask = m_terrain.mask;
	in.geometry = m_lastIn;
	in.world = m_cloudWorld;
	memcpy(in.fowAnim, m_terrain.fowAnim, sizeof(in.fowAnim));
	m_cloudStats = cf::CloudStats();
	const c5r::Status st = m_mod->drawClouds(ctx, in, &m_cloudStats);
	if(st.ok())
	{
		cloudDraws++;
		m_lastCloudErr.clear();
		drawParticles(ctx, rtv, cloudDepth, w, h);   // ticket 0340: after the planes, on their depth (FOW CULL's zeros)
		return;
	}
	cloudErrors++;
	loud(m_lastCloudErr, st.str(), "the cloud planes failed (%lld so far): %s", (long long)cloudErrors, st.str().c_str());
}

// ---- ticket 0340: the FOWLevel-3 particles ----

int RenderFowParticlesOption()
{
	char v[16] = {};
	const DWORD n = GetEnvironmentVariableA("C5_RENDER_FOW_PARTICLES", v, sizeof(v));
	return (n > 0 && n < sizeof(v) && v[0] == '0' && v[1] == 0) ? 0 : 1;
}

uint32_t RenderFowParticleSeed()
{
	char v[32] = {};
	const DWORD n = GetEnvironmentVariableA("C5_RENDER_FOW_PARTICLE_SEED", v, sizeof(v));
	if(n == 0 || n >= sizeof(v))
		return 1;
	char* end = nullptr;
	const unsigned long s = strtoul(v, &end, 10);
	return end && end != v && !*end ? (uint32_t)s : 1u;
}

void FowScene::listTiming(ID3D11DeviceContext* immediate, bool begin)
{
	particleTiming(immediate, begin);   // m_recording is off at the execute
	if(!begin)
		m_listTimedSec++;
}

void FowScene::particleTiming(ID3D11DeviceContext* ctx, bool begin)
{
	if(m_recording)
	{   // ticket 0753 (A5): no query on a deferred context; Scene brackets the executed list with listTiming
		m_recTimed = true;
		return;
	}
	PartQuery& q = m_pq[m_pqAt];
	if(begin)
	{
		// read the slot's previous result first (it was issued 4 frames ago); never wait for it
		if(q.pending)
		{
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
			UINT64 a = 0, b = 0;
			if(ctx->GetData(q.dis, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
			   ctx->GetData(q.t0, &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
			   ctx->GetData(q.t1, &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint && dj.Frequency && b >= a)
			{
				m_partGpuMsSec += (double)(b - a) * 1000.0 / (double)dj.Frequency;
				m_partGpuN++;
			}
			q.pending = false;
		}
		if(!q.dis)
		{
			ID3D11Device* dev = nullptr;
			ctx->GetDevice(&dev);
			D3D11_QUERY_DESC d = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
			if(dev && SUCCEEDED(dev->CreateQuery(&d, &q.dis)))
			{
				d.Query = D3D11_QUERY_TIMESTAMP;
				if(FAILED(dev->CreateQuery(&d, &q.t0)) || FAILED(dev->CreateQuery(&d, &q.t1)))
				{
					SafeRelease(q.dis);
					SafeRelease(q.t0);
					SafeRelease(q.t1);
				}
			}
			SafeRelease(dev);
		}
		if(q.dis)
		{
			ctx->Begin(q.dis);
			ctx->End(q.t0);
		}
		return;
	}
	if(q.dis)
	{
		ctx->End(q.t1);
		ctx->End(q.dis);
		q.pending = true;
	}
	m_pqAt = (m_pqAt + 1) % 4;
}

// The statistics dump (ticket 0340's acceptance, fog-of-war.md 10.3; a measurement switch, off unless
// C5_FOW_PARTICLE_DUMP names a folder): per particle frame while the file <folder>\on exists, up to
// C5_FOW_PARTICLE_DUMP_FRAMES (default 600), one record
// in <folder>/particles_<pid>.c5ps: u32 bytes of the record, f64 wall ms, u32 steps, f32 footprint of view 0 (min x, min
// y, max x, max y), f32 Speed, u32 n, n x {f32 x, y, z, vx, vy, mass, size, rot; u32 id}: the particles of the cells view
// 0 draws (after this frame's step). The file starts with "C5PS" u32 1. shell/evidence/0340/pstats.py reads it and the
// stock reader's samples alike.
void FowScene::dumpParticles()
{
	if(m_dumpState == 0)
	{
		wchar_t dir[MAX_PATH] = {};
		const DWORD n = GetEnvironmentVariableW(L"C5_FOW_PARTICLE_DUMP", dir, MAX_PATH);
		m_dumpState = (n > 0 && n < MAX_PATH) ? 1 : 2;
		if(m_dumpState == 1)
		{
			char lim[32] = {};
			const DWORD k = GetEnvironmentVariableA("C5_FOW_PARTICLE_DUMP_FRAMES", lim, sizeof(lim));
			m_dumpLeft = (k > 0 && k < sizeof(lim)) ? atoi(lim) : 600;
			wchar_t name[64];
			swprintf_s(name, L"\\particles_%u.c5ps", GetCurrentProcessId());
			m_dumpPath = std::wstring(dir) + name;
			FILE* fp = nullptr;
			if(_wfopen_s(&fp, m_dumpPath.c_str(), L"wb") == 0 && fp)
			{
				const uint32_t v = 1;
				fwrite("C5PS", 1, 4, fp);
				fwrite(&v, 4, 1, fp);
				fclose(fp);
				RNote("fog: particle statistics dump to %s (%d frames)", Narrow(m_dumpPath).c_str(), m_dumpLeft);
			}
			else
			{
				RNote("fog: particle statistics dump: %s not writable", Narrow(m_dumpPath).c_str());
				m_dumpState = 2;
			}
		}
	}
	if(m_dumpState != 1 || m_dumpLeft <= 0 || !m_mod || m_lastIn.numViews == 0)
		return;
	// only while <folder>\on exists (the driver opens a window per camera)
	const std::wstring on = m_dumpPath.substr(0, m_dumpPath.find_last_of(L'\\')) + L"\\on";
	if(GetFileAttributesW(on.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	const cf::ParticleSim* sim = m_mod->particleSim();
	if(!sim || !sim->ready())
		return;
	c5r::water::World ww;
	ww.minX = m_cloudWorld.minX;
	ww.minY = m_cloudWorld.minY;
	ww.maxX = m_cloudWorld.maxX;
	ww.maxY = m_cloudWorld.maxY;
	ww.width = ww.maxX - ww.minX;
	ww.height = ww.maxY - ww.minY;
	ww.sx = m_cloudWorld.sx;
	ww.sy = m_cloudWorld.sy;
	ww.samplesX = m_cloudWorld.samplesX;
	ww.samplesY = m_cloudWorld.samplesY;
	ww.valid = true;
	c5r::water::Footprint fp;
	if(!c5r::water::viewFootprint(m_lastIn.views[0].viewProj, ww, fp).ok())
		return;
	int32_t ax, ay, bx, by;
	sim->cellOf(fp.minX, fp.minY, ax, ay);
	sim->cellOf(fp.maxX, fp.maxY, bx, by);
	std::vector<uint8_t> rec;
	auto put = [&](const void* p, size_t n) { rec.insert(rec.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
	const uint32_t zero = 0;
	put(&zero, 4);
	const double t = QpcToMs(Qpc());
	put(&t, 8);
	const uint32_t steps = sim->stats().steps;
	put(&steps, 4);
	const float f[5] = {fp.minX, fp.minY, fp.maxX, fp.maxY, sim->settings().speed};
	put(f, sizeof(f));
	const size_t countAt = rec.size();
	put(&zero, 4);
	uint32_t n = 0;
	for(int32_t x = std::max(ax, 0); x < std::min(bx + 1, sim->cellsX() - 1); ++x)
		for(int32_t y = std::max(ay, 0); y < std::min(by + 2, sim->cellsY() - 1); ++y)
			for(const cf::Particle& p : sim->cell(x, y))
			{
				const float v[8] = {p.x, p.y, p.z, p.vx, p.vy, p.mass, p.size, p.rot};
				put(v, sizeof(v));
				put(&p.id, 4);
				n++;
			}
	memcpy(&rec[countAt], &n, 4);
	const uint32_t bytes = (uint32_t)rec.size();
	memcpy(&rec[0], &bytes, 4);
	FILE* fpw = nullptr;
	if(_wfopen_s(&fpw, m_dumpPath.c_str(), L"ab") == 0 && fpw)
	{
		fwrite(rec.data(), 1, rec.size(), fpw);
		fclose(fpw);
	}
	m_dumpLeft--;
}

void FowScene::drawParticles(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, uint32_t w, uint32_t h)
{
	if(!m_partOn || !m_mod || !depth)
		return;
	const float ww = m_cloudWorld.maxX - m_cloudWorld.minX, wh = m_cloudWorld.maxY - m_cloudWorld.minY;
	if(!m_partReset || ww != m_partW || wh != m_partH)
	{
		// a new map (the world box changed) or a new device: the lattice spawn of stock's per-game init (fog-of-war.md 10.1)
		const c5r::Status rs = m_mod->resetParticles(ww, wh, m_partSeed);
		if(!rs.ok())
		{
			particleErrors++;
			loud(m_lastPartErr, rs.str(), "the particles could not be spawned for a %.0f x %.0f world (%lld so far): %s", ww, wh,
			     (long long)particleErrors, rs.str().c_str());
			return;
		}
		m_partReset = true;
		m_partW = ww;
		m_partH = wh;
		particleResets++;
		const c5r::fow::ParticleSim* sim = m_mod->particleSim();
		RNote("fog: particles spawned for a %.0f x %.0f world: %u particles, seed %u", ww, wh, sim ? sim->stats().particles : 0u,
		      m_partSeed);
	}
	cf::ParticleInput in;
	in.colour = rtv;
	in.depth = depth;
	in.width = w;
	in.height = h;
	in.mask = m_terrain.mask;
	in.views = m_lastIn.views;
	in.numViews = m_lastIn.numViews;
	in.world = m_cloudWorld;
	const int64_t q0 = Qpc();
	particleTiming(ctx, true);
	const c5r::Status st = m_mod->drawParticles(ctx, in, &m_partStats);
	particleTiming(ctx, false);
	m_partCpuMsSec += QpcToMs(Qpc() - q0);
	m_partSec++;
	if(st.ok())
	{
		particleFrames++;
		m_lastPartErr.clear();
		dumpParticles();
		return;
	}
	particleErrors++;
	loud(m_lastPartErr, st.str(), "the particles failed (%lld so far): %s", (long long)particleErrors, st.str().c_str());
}

ID3D11DepthStencilView* FowScene::scratchDepth(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv)
{
	if(!dsv)
		return nullptr;
	ID3D11Resource* res = nullptr;
	dsv->GetResource(&res);
	ID3D11Texture2D* src = nullptr;
	if(res)
		res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src);
	SafeRelease(res);
	std::string err;
	if(!src)
		err = "the scene's depth view is not a Texture2D";
	else
	{
		D3D11_TEXTURE2D_DESC d;
		src->GetDesc(&d);
		d.Usage = D3D11_USAGE_DEFAULT;
		d.CPUAccessFlags = 0;
		d.MiscFlags = 0;   // the sink's may be shared; the copy is ours
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		D3D11_DEPTH_STENCIL_VIEW_DESC vd;
		dsv->GetDesc(&vd);
		if(m_scratchTex && memcmp(&d, &m_scratchDesc, sizeof(d)) != 0)
		{
			SafeRelease(m_scratchDsv);
			SafeRelease(m_scratchTex);
		}
		if(!m_scratchTex)
		{
			ID3D11Device* dev = nullptr;
			ctx->GetDevice(&dev);
			HRESULT hr = dev ? dev->CreateTexture2D(&d, nullptr, &m_scratchTex) : E_FAIL;
			if(SUCCEEDED(hr))
				hr = dev->CreateDepthStencilView(m_scratchTex, &vd, &m_scratchDsv);
			SafeRelease(dev);
			if(FAILED(hr))
			{
				SafeRelease(m_scratchDsv);
				SafeRelease(m_scratchTex);
				char b[96];
				_snprintf_s(b, sizeof(b), _TRUNCATE, "the cloud pass's depth copy (%ux%u, format %u) not created: 0x%08X", d.Width, d.Height,
				            (unsigned)d.Format, (unsigned)hr);
				err = b;
			}
			else
				m_scratchDesc = d;
		}
		if(m_scratchTex)
			ctx->CopyResource(m_scratchTex, src);
	}
	SafeRelease(src);
	if(!err.empty())
	{
		cloudErrors++;
		loud(m_lastCloudErr, err, "%s (%lld so far): no cloud planes this frame", err.c_str(), (long long)cloudErrors);
		return nullptr;
	}
	return m_scratchDsv;
}

// epic 0301, issue 3911 (MC #1450): the offline player's seed of FOWAnim's clock (srv_fow_scene.h FowClockState)
bool FowScene::setClockState(const FowClockState& s)
{
	if(!std::isfinite(s.clockS) || !std::isfinite(s.clockLastMs) || s.clockS < 0)
	{
		RNote("LOUD: fog: clock state refused (T %g s, last frame at %g ms): not finite or T below 0", s.clockS, s.clockLastMs);
		return false;
	}
	m_clockS = s.clockS;
	m_clockLastMs = s.clockLastMs;
	// ticket 0340: the player replays a span without the particles' state unless the bundle carries it (ticket 0403:
	// setParticleState after this call), so it draws none here: fresh particles would not match the recorded frames
	if(m_partOn)
		RNote("fog: the offline player seeded the clock: particles off until a recorded particle state is set (particles.bin, 0403)");
	m_partOn = false;
	m_partPlayer = true;
	return true;
}

// ticket 0403: the particles' state for the recorder (srv_fow_scene.h FowParticleState)
bool FowScene::particleState(FowParticleState& out, std::string& why) const
{
	out = FowParticleState();
	if(!m_partOn)
	{
		why = m_level < 3 ? "FOWLevel below 3" : m_partPlayer ? "the offline player" : "the particles are off (switched off or not started)";
		return false;
	}
	c5r::fow::ParticleSim* sim = m_mod ? m_mod->particleSim() : nullptr;
	if(!sim)
	{
		why = "the particle module has no simulation";
		return false;
	}
	out.seed = m_partSeed;
	out.spawned = m_partReset && sim->ready();
	if(out.spawned)
	{
		out.worldW = m_partW;
		out.worldH = m_partH;
		out.sim = sim->saveState();   // joins a step in flight (ParticleRenderer::sim)
	}
	return true;
}

// ticket 0403: the offline player's particles from a recorded state
bool FowScene::setParticleState(const FowParticleState& s, std::string& why)
{
	if(!m_partModule || !m_mod)
	{
		why = m_level < 3 ? "FOWLevel below 3: no particles in this scene" :
		      !RenderFowParticlesOption() ? "C5_RENDER_FOW_PARTICLES=0 in the player's environment" :
		      "the particle module did not start (" + (m_partInit.ok() ? std::string("not initialised") : m_partInit.str()) + ")";
		return false;
	}
	if(s.spawned)
	{
		if(!std::isfinite(s.worldW) || !std::isfinite(s.worldH) || s.worldW <= 0 || s.worldH <= 0)
		{
			why = "the recorded world is not finite or not > 0";
			return false;
		}
		const c5r::Status rs = m_mod->resetParticles(s.worldW, s.worldH, s.seed);
		if(!rs.ok())
		{
			why = "the spawn for the recorded world: " + rs.str();
			m_partReset = false;
			return false;
		}
		c5r::fow::ParticleSim* sim = m_mod->particleSim();
		const c5r::Status ls = sim ? sim->loadState(s.sim.data(), s.sim.size()) : c5r::Status::fail(c5r::Err::NotFound, "no simulation");
		if(!ls.ok())
		{
			why = "the recorded state: " + ls.str();
			m_partReset = false;   // the spawn above is not the recorded state: never drawn as if it were
			return false;
		}
		m_partReset = true;
		m_partW = s.worldW;
		m_partH = s.worldH;
	}
	else
		m_partReset = false;   // the next particle frame spawns, as the live one did
	m_partSeed = s.seed;
	m_partOn = true;
	const c5r::fow::ParticleSim* sim = m_mod->particleSim();
	RNote("fog: the offline player's particles from the recorded state: %s, seed %u, %u particles, %u steps since the spawn",
	      s.spawned ? "spawned" : "not spawned yet (the first particle frame spawns)", s.seed,
	      s.spawned && sim ? sim->stats().particles : 0u, s.spawned && sim ? sim->stats().steps : 0u);
	return true;
}

void FowScene::secondLine(char* out, size_t n)
{
	_snprintf_s(out, n, _TRUNCATE,
	            "fow=%s level=%d with=%d held=%d built=%d gen=%u pair=%u revealed=%u T=%.2f hex=%u/%u/%u drop=%u cull=%u planes=%u/%u/%u",
	            StateName(m_state), m_level, m_withSec, m_heldSec, m_builtSec, m_fogGen, m_havePair ? m_pairGen : 0u, m_revealed,
	            m_clockS, m_maskStats.hexagons[0], m_maskStats.hexagons[1], m_maskStats.hexagons[2], m_maskStats.dropped,
	            m_cloudStats.cullQuads, m_cloudStats.planeQuads[0], m_cloudStats.planeQuads[1], m_cloudStats.planeQuads[2]);
	{   // ticket 0674: frames of the second whose mask window was made from the camera (render_fow_mask_window=camera)
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE, " camwin=%d", m_camWinSec);
	}
	if(m_partOn)
	{
		// ticket 0340: sprites of the newest frame, particles moved by its step, alive, lost to the cell cap; CPU and GPU ms
		// a particle frame (means over the second; indications, COMMON 23)
		const size_t used = strlen(out);
		_snprintf_s(out + used, n - used, _TRUNCATE, " particles=%u/%u/%u lost=%u cpu=%.2f gpu=%.3f", m_partStats.sprites,
		            m_partStats.moved, m_partStats.particles, m_partStats.lost, m_partSec ? m_partCpuMsSec / m_partSec : 0.0,
		            m_partGpuN ? m_partGpuMsSec / m_partGpuN : 0.0);
		if(m_listTimedSec)   // ticket 0753: the GPU time was taken around the executed list (cloud planes, particles, path)
		{
			const size_t used2 = strlen(out);
			_snprintf_s(out + used2, n - used2, _TRUNCATE, " gpu_scope=list");
		}
	}
	m_listTimedSec = 0;
	m_partCpuMsSec = m_partGpuMsSec = 0;
	m_partSec = m_partGpuN = 0;
	m_withSec = m_heldSec = m_builtSec = m_camWinSec = 0;
}

void FowScene::totalsLine(char* out, size_t n)
{
	_snprintf_s(out, n, _TRUNCATE,
	            "fow totals: state %s frames with %lld held %lld without %lld masks %lld clouds %lld build errors %lld cloud errors %lld "
	            "bad pairs %lld kept pair frames %lld view mismatches %lld fog events %lld state changes %lld table reads %lld seq gaps %lld "
	            "overflows %lld outside events %lld bad bits %lld nominal placement frames %lld; model fog (0339): consumer %s, masks "
	            "copied %lld published %lld skipped %lld faults %lld; particles (0340): %s frames %lld resets %lld errors %lld lost %u",
	            StateName(m_state), (long long)framesWith, (long long)framesHeld, (long long)framesWithout, (long long)masksBuilt,
	            (long long)cloudDraws, (long long)buildErrors, (long long)cloudErrors, (long long)badPairs, (long long)keptPairFrames,
	            (long long)viewMismatch,
	            (long long)fowEvents, (long long)stateChanges, (long long)rereads, (long long)seqGaps, (long long)overflows,
	            (long long)outsideEvents, (long long)badBits, (long long)nominalFrames, FowMaskPublisher::ConsumerStateName(m_pub.consumerState()), (long long)m_pub.copies, (long long)m_pub.published,
	            (long long)m_pub.skipped, (long long)m_pub.faults, m_partOn ? "on" : "off", (long long)particleFrames,
	            (long long)particleResets, (long long)particleErrors, m_partStats.lost);
}

void FowFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !s.fow().failed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("fog: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), kFowFatalExitCode);
		fatal(kFowFatalExitCode, why.c_str());
	}
	else
		s.fow().drawWithout();
}

}  // namespace srvrender
