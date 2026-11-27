// srv_units_scene.cpp - the server's unit scene behind SceneUnits (epic 0375, issue 4454). Contract: srv_units_scene.h,
// srv_units_core.h. Written 2026-11-15 for this repository (GPL-3.0-or-later). Design: unit-server-scene.md 3.3, 7-10.
//
// Threads: the render thread calls every SceneUnits method; the pre-check runs on its own std::thread with its own
// CPU-only UnitRenderer and its own environment (the module is not thread-safe, pose.h), joined in poll(). Ticket 0711:
// beginBody may run on a frame-jobs worker while the job owns the module (m_owner; the render thread calls nothing here
// between the fork and the join, R2); its lines are collected and written at the join (R6).
#include "srv_units_core.h"
#include "srv_units_anim.h"   // issue 4456: the controller
#include "effects/srv_trigger_stream.h"   // ticket 4722: the trigger stream

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <set>
#include <thread>

#include "srv_render_common.h"
#include "srv_frame_jobs.h"         // ticket 0711 (J1): FrameJobOwner, FrameJobResult
#include "srv_residency_source.h"   // srvrender::ModFolders (read only; 0365's file)
#include "../feed/srv_feed_server.h"   // SrvFeed::GetCounters2 (issue 4460: the table file)
#include "srv_scene_sources.h"         // FeedSource (issue 4457 / 4456: the recording port)
#include "srv_render_scene.h"          // issue 4755: RenderUnitsPositionsOption, SceneOptions::kUnitsPositionsExe
#include "c5r/units/draw.h"   // issue 4468: loadStringDatabases (the clip durations read ahead)
#include "c5r/units/scene_file.h"
#include "c5r/fow/fow.h"   // issue 4463: PlotState (setPlotStates' values)
#include "srv_options.h"      // issue 4981: SrvOptions() for the unit extras (every program linking the render units has it)

namespace srvunits {

namespace cu = c5r::units;

// Issue 4981: the unit extras' factory (server/src/extras/units/ registers it at static initialisation)
static UnitsExtrasFactory g_unitsExtrasFactory = nullptr;
void RegisterUnitsExtrasFactory(UnitsExtrasFactory f) { g_unitsExtrasFactory = f; }
std::unique_ptr<UnitsExtras> MakeUnitsExtras() { return g_unitsExtrasFactory ? g_unitsExtrasFactory() : nullptr; }

namespace {

std::string Utf8(const std::wstring& w)
{
	if(w.empty())
		return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)(n > 0 ? n : 0), '\0');
	if(n > 0)
		WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

// Ticket 5054 (research unit-combat-server.md 18.27): the TEST keys' files (render_test_units_dump, _table, _anim) are
// written by this worker, never on the render thread (a file create on a stalled disk held W70's render thread 11 s).
// At most kMaxPending files wait; past that a file is skipped (the caller counts it and says the episode once). A job's
// failure comes back to the render thread (collect) with its kind, to be said as before.
class TestFiles
{
public:
	typedef std::function<bool(std::string& why)> Job;
	static constexpr size_t kMaxPending = 64;
	~TestFiles()
	{
		if(!m_thread)
			return;
		AcquireSRWLockExclusive(&m_s->lock);
		m_s->stop = true;
		ReleaseSRWLockExclusive(&m_s->lock);
		SetEvent(m_s->wake);
		// at most 5 s; a worker still stuck on the disk keeps its state (leaked, the process is ending or the scene closing)
		if(WaitForSingleObject(m_thread, 5000) == WAIT_OBJECT_0)
		{
			CloseHandle(m_s->wake);
			delete m_s;
		}
		CloseHandle(m_thread);
	}
	// false: not queued (kMaxPending files wait already, or no worker)
	bool post(int kind, Job job)
	{
		if(!m_thread && !start())
			return false;
		AcquireSRWLockExclusive(&m_s->lock);
		const bool ok = m_s->q.size() < kMaxPending;
		if(ok)
		{
			m_s->q.push_back(std::make_pair(kind, std::move(job)));
			m_s->posted++;
		}
		ReleaseSRWLockExclusive(&m_s->lock);
		if(ok)
			SetEvent(m_s->wake);
		return ok;
	}
	// every file queued so far written (at most ms; the tests' probe)
	bool wait(DWORD ms)
	{
		if(!m_thread)
			return true;
		const ULONGLONG end = GetTickCount64() + ms;
		AcquireSRWLockExclusive(&m_s->lock);
		const unsigned long long target = m_s->posted;
		bool ok = true;
		while(m_s->done < target)
		{
			const ULONGLONG now = GetTickCount64();
			if(now >= end)
			{
				ok = false;
				break;
			}
			SleepConditionVariableSRW(&m_s->written, &m_s->lock, (DWORD)(end - now), 0);
		}
		ReleaseSRWLockExclusive(&m_s->lock);
		return ok;
	}
	void collect(std::vector<std::pair<int, std::string>>& fails)
	{
		fails.clear();
		if(!m_thread)
			return;
		AcquireSRWLockExclusive(&m_s->lock);
		fails.swap(m_s->fails);
		ReleaseSRWLockExclusive(&m_s->lock);
	}

private:
	struct State
	{
		SRWLOCK lock = SRWLOCK_INIT;
		CONDITION_VARIABLE written = CONDITION_VARIABLE_INIT;
		unsigned long long posted = 0, done = 0;
		HANDLE wake = nullptr;
		std::deque<std::pair<int, Job>> q;
		std::vector<std::pair<int, std::string>> fails;
		bool stop = false;
	};
	State* m_s = nullptr;
	HANDLE m_thread = nullptr;
	bool start()
	{
		if(!m_s)
		{
			m_s = new State();
			m_s->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		}
		if(!m_s->wake)
			return false;
		m_thread = CreateThread(nullptr, 0, &TestFiles::run, m_s, 0, nullptr);
		return m_thread != nullptr;
	}
	static DWORD WINAPI run(void* pv)
	{
		State* s = (State*)pv;
		for(;;)
		{
			WaitForSingleObject(s->wake, 200);
			for(;;)
			{
				std::pair<int, Job> j;
				AcquireSRWLockExclusive(&s->lock);
				const bool stop = s->stop, have = !s->q.empty();
				if(have)
				{
					j = std::move(s->q.front());
					s->q.pop_front();
				}
				ReleaseSRWLockExclusive(&s->lock);
				if(!have)
				{
					if(stop)
						return 0;
					break;
				}
				std::string why;
				const bool ok = j.second(why);
				AcquireSRWLockExclusive(&s->lock);
				if(!ok)
					s->fails.push_back(std::make_pair(j.first, why));
				s->done++;
				ReleaseSRWLockExclusive(&s->lock);
				WakeAllConditionVariable(&s->written);
			}
		}
	}
};

// The pre-check of one database on its own thread (note 3.3, decision 10): open a CPU-only renderer, precheck().
struct PrecheckRun
{
	std::thread th;
	std::atomic<bool> done{false};
	uint32_t gen = 0;
	std::wstring db;
	std::string envWhy;               // the environment could not open (database unreadable, install)
	c5r::Status openSt, st;           // UnitRenderer::open, precheck
	std::vector<cu::UnitTypeStatus> out;
	double ms = 0;
	// issue 4468 (unit-server-scene.md 6.1): with the controller on, after a passed pre-check, the duration of every clip
	// of every art row it reached, read here (off the render thread) as the controller's graph factory reads one, for
	// AnimRunner::seedClipDurations: the controller's start then reads no clip file on the render thread
	bool anim = false;
	c5r::unitanim::ClipGraphFactory::Durations durations;
	size_t durRows = 0, durFailed = 0;
	size_t durCached = 0, durRead = 0;   // issue 0441: taken from the pre-check's clip cache / read here
	double durMs = 0;
	std::string durWhy;
	// ticket 0704: the environment, kept after the run (the renderer that used it is gone): the host hands it to the drawing
	// renderer's worker (OpenRun) instead of opening a second one (0.7-2.8 s live, unit-renderer.md 15.3)
	std::unique_ptr<Env> env;
	void run(EnvOpener opener)
	{
		const int64_t t0 = srvrender::Qpc();
		try
		{
			env.reset(new Env());
			std::string why;
			if(!opener(db, *env, why))
			{
				envWhy = why.empty() ? "the environment did not open" : why;
				env.reset();
			}
			else
			{
				cu::UnitRenderer r;
				openSt = r.open(nullptr, env->gf, *env->db, env->exe, env->install);
				if(openSt.ok())
					st = r.precheck(out);
				ms = srvrender::QpcToMs(srvrender::Qpc() - t0);
				if(anim && openSt.ok() && st.ok())
					readDurations(*env, r);
			}
		}
		catch(const std::exception& e)
		{
			envWhy = std::string("exception: ") + e.what();
			env.reset();
		}
		catch(...)
		{
			envWhy = "exception";
			env.reset();
		}
		if(ms <= 0)
			ms = srvrender::QpcToMs(srvrender::Qpc() - t0);
		done = true;
	}
	void readDurations(Env& env, cu::UnitRenderer& r)
	{
		const int64_t d0 = srvrender::Qpc();
		cu::GameFilesProvider prov(env.gf);
		c5m::StringDatabase strings;
		const cu::UnitStatus ss = cu::loadStringDatabases(env.gf.names(), prov, strings);
		if(!ss.ok())
			durWhy = "the unit string databases: " + ss.str();
		else
		{
			std::set<std::string> rows;
			for(const cu::UnitTypeStatus& t : out)
				if(!t.art_row.empty())
					rows.insert(t.art_row);
			std::vector<std::string> clips;
			for(const std::string& row : rows)
			{
				cu::UnitArt art;
				if(!r.art().resolve(row, art).ok())
					continue;
				durRows++;
				for(const cu::MemberArt& m : art.members)
					for(const c5m::ResolvedAnimation& a : m.files.animations)
						clips.push_back(a.file.name);
			}
			// issue 0441: first the durations the pre-check's own clip cache already read (its bindAll bound every shipped
			// clip of every row it checked, from the same files: animation 0's duration, the value the second read gives,
			// byte-equal by test), then a read of only the clips it lacks (missing, failed to bind, no positive duration)
			if(const cu::ModulePose* mp = dynamic_cast<const cu::ModulePose*>(r.poseSource()))
				durCached = c5r::unitanim::ClipGraphFactory::takeFromCache(mp->cache(), clips, durations);
			c5r::unitanim::ClipGraphFactory::readDurations(cu::clipLoaderFromProvider(prov, &strings), clips, durations);
			durRead = durations.size() - durCached;
			for(const auto& kv : durations)
				durFailed += kv.second.first > 0 ? 0 : 1;
		}
		durMs = srvrender::QpcToMs(srvrender::Qpc() - d0);
	}
};

// Ticket 0704 (unit-renderer.md 15; pan-coherence.md 5.8): the drawing renderer opened on its own thread with the passed
// pre-check's environment (else one opened here: after a device change), on the render device (an ID3D11Device is free-threaded: the server's device is not created
// D3D11_CREATE_DEVICE_SINGLETHREADED, srv_render_host.cpp), then the art rows of the table's units loaded (L0 and L1 of
// UnitRenderer::loadUnit: the art, the models, the textures) and uploaded (ModelCache::upload), so that the render thread's
// first units frame finds the renderer open and its rows cached: the 140-600 ms the open and the first prepare cost on the
// render thread after a load (one Present of that length under D242) run here, before the first map frame that draws units.
// The renderer and the environment are touched only by this thread until `done`; the render thread joins before it reads
// anything. The renderer keeps pointers into `env` (its GameFilesProvider), so the host keeps this object as long as it
// keeps the renderer.
struct OpenRun
{
	std::thread th;
	std::atomic<bool> done{false};
	std::atomic<bool> cancel{false};   // set by the render thread: the rows not yet loaded are skipped (a new database, device)
	ID3D11Device* dev = nullptr;       // AddRef'd by startOpen, released by the destructor
	std::wstring db;
	uint32_t gen = 0;
	// one unit per distinct (unit type, art style, era) of the table at the start, with its owner's art
	std::vector<std::pair<cu::UnitInput, cu::PlayerInput>> warm;
	size_t tableUnits = 0;
	std::unique_ptr<Env> env;          // given by startOpen (the passed pre-check's), else opened here
	bool envReused = false;
	std::unique_ptr<cu::UnitRenderer> r;
	std::string envWhy;
	c5r::Status openSt, uploadSt;
	size_t rowsOk = 0, rowsFailed = 0, rowsSkipped = 0;
	double envMs = 0, openMs = 0, rowsMs = 0, uploadMs = 0, ms = 0;
	int64_t startQpc = 0;
	~OpenRun()
	{
		if(th.joinable())
			th.join();
		r.reset();   // before env: the renderer points into it
		env.reset();
		if(dev)
			dev->Release();
	}
	void run(EnvOpener opener)
	{
		const int64_t t0 = srvrender::Qpc();
		try
		{
			std::string why;
			envReused = env != nullptr;
			if(!env)
			{
				env.reset(new Env());
				if(!opener(db, *env, why))
				{
					envWhy = why.empty() ? "the environment did not open" : why;
					env.reset();
				}
			}
			if(env)
			{
				const int64_t t1 = srvrender::Qpc();
				envMs = srvrender::QpcToMs(t1 - t0);
				r.reset(new cu::UnitRenderer());
				openSt = r->open(dev, env->gf, *env->db, env->exe, env->install);
				const int64_t t2 = srvrender::Qpc();
				openMs = srvrender::QpcToMs(t2 - t1);
				if(openSt.ok())
				{
					for(const auto& w : warm)
					{
						if(cancel)
						{
							rowsSkipped++;
							continue;
						}
						cu::UnitArt art;
						std::vector<const cu::UnitModel*> models;
						// the same call prepare makes per unit (rowOf + the row cache); a failed row is cached with its status,
						// as prepare would cache it, and counted here only
						if(r->loadUnit(w.first, &w.second, art, models).ok())
							rowsOk++;
						else
							rowsFailed++;
					}
					const int64_t t3 = srvrender::Qpc();
					rowsMs = srvrender::QpcToMs(t3 - t2);
					if(!cancel && r->models())
						uploadSt = r->models()->upload(dev);
					uploadMs = srvrender::QpcToMs(srvrender::Qpc() - t3);
				}
			}
		}
		catch(const std::exception& e)
		{
			envWhy = std::string("exception: ") + e.what();
			r.reset();
		}
		catch(...)
		{
			envWhy = "exception";
			r.reset();
		}
		ms = srvrender::QpcToMs(srvrender::Qpc() - t0);
		done = true;
	}
};

// Issue 4457 / 4456: the scene's port: the table copies through the host's recording feed when one is given
// (useFeedSource), the unit database always from the port the scene was made with.
struct SwitchPort : FeedPort
{
	FeedPort& base;
	srvrender::FeedSource* src = nullptr;
	explicit SwitchPort(FeedPort& b) : base(b) {}
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& gen) override
	{
		if(src)
			src->copyUnits(out, gen);
		else
			base.copyUnits(out, gen);
	}
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override
	{
		if(src)
			src->copyPlayers(out);
		else
			base.copyPlayers(out);
	}
	bool unitDatabase(std::wstring& path, uint32_t& gen) override { return base.unitDatabase(path, gen); }
};

class UnitsHost : public SceneUnits, public UnitsHostProbe
{
public:
	UnitsHost(const UnitsOptions& o, FeedPort& port, EnvOpener env, LogFn log, ClockFn clock)
	    : m_opt(o), m_port(port), m_switch(port), m_open(std::move(env)), m_log(std::move(log)), m_clock(std::move(clock)),
	      // ticket 0711 (R6): the core's lines go through say(), so that those of a frame job's beginBody are collected
	      m_core(m_switch, m_log ? LogFn([this](const char* line) { say(line); }) : LogFn())
	{
		m_core.setPositionsExe(m_opt.positionsExe);   // issue 4755
		if(m_opt.anim)
			m_core.setAnimFeed(&m_animFeed);
	}
	~UnitsHost() override
	{
		if(m_pre && m_pre->th.joinable())
			m_pre->th.join();
		if(m_openRun)
			m_openRun->cancel = true;   // ticket 0704: joined by its destructor (the rows not yet loaded are skipped)
	}

	// ---- SceneUnits ----
	// Ticket 0711 (R2): every entry point below asks the owner flag first; while a frame job runs beginBody, a call from any
	// other thread is a violation (counted, LOUD, fatal in test runs: srv_frame_jobs.h FrameJobOwner)
	void setDevice(ID3D11Device* dev) override
	{
		m_owner.mayCall("UnitsHost::setDevice");
		if(dev == m_dev)
			return;
		dropPlans();
		dropDraw();   // the drawing renderer's GPU objects go with the device; re-opened at the next draw
		if(m_openRun)
			m_openRun->cancel = true;   // ticket 0704: a worker's open on the old device ends early; openDraw drops it and starts again
		m_dev = dev;
	}
	// issue 4457 / 4456: the drain's clock (the recorder's when recording: feed.bin's drainMs), else read here
	bool feed(const SrvFeed::Batch& b, double drainMs) override
	{
		m_owner.mayCall("UnitsHost::feed");
		srvrender::FrameJobsOneWriter::Scope trigWrite(m_trigWriter, "UnitsHost::feed");   // MC #12834
		return m_core.onBatch(b, drainMs >= 0 ? drainMs : now());
	}
	void useFeedSource(srvrender::FeedSource* src) override
	{
		m_owner.mayCall("UnitsHost::useFeedSource");
		m_switch.src = src;
		say(src ? "units: the scene's table copies go through the recording feed (feed.bin records 3 and 6 of caller 1)"
		        : "units: the scene's table copies go to the live feed");
	}
	void poll() override
	{
		m_owner.mayCall("UnitsHost::poll");
		srvrender::FrameJobsOneWriter::Scope trigWrite(m_trigWriter, "UnitsHost::poll");   // MC #12834: the stream made or dropped
		std::wstring db;
		uint32_t gen = 0;
		bool have;
		if(!m_opt.dbOverride.empty())
		{
			db = m_opt.dbOverride;
			gen = 1;
			have = true;
		}
		else
			have = m_port.unitDatabase(db, gen);
		if(have && (!m_haveDb || gen != m_dbGen))
			startDatabase(db, gen);
		if(m_pre && m_pre->done)
			evaluate();
	}
	void setPlotStates(const uint8_t* states, size_t n, uint32_t width, uint32_t height, uint32_t gen) override   // issue 4463
	{
		m_owner.mayCall("UnitsHost::setPlotStates");
		if(!states || !n)
		{
			m_havePlotStates = false;
			return;
		}
		if(m_havePlotStates && gen == m_plotStatesGen && width == m_plotStatesW && height == m_plotStatesH && n == m_plotStates.size())
			return;
		m_plotStates.assign(states, states + n);
		m_plotStatesW = width;
		m_plotStatesH = height;
		m_plotStatesGen = gen;
		m_havePlotStates = true;
	}
	void setPlotTable(const SrvFeed::PlotRecord* plots, size_t n, uint32_t width, uint32_t height, uint32_t gen) override   // 5052 (T)
	{
		m_owner.mayCall("UnitsHost::setPlotTable");
		if(!m_opt.triggerTerrain)
			return;   // the knob off: nothing kept (the scene as before)
		m_plotTable.set(plots, n, width, height, gen);
	}
	// 5052 (T): the trigger stream's UnitTerrainFn: the unit's plot (its record) in the copy above, the tag by stock's rule.
	// False (the stream's DIRT, counted terrainStandIn and plotNoTable): no table, no record, a plot outside the table.
	bool unitPlotTag(int32_t player, int32_t unit, std::string& out)
	{
		int32_t terrain = -1, feature = -1;
		if(!m_trig || !m_plotTable.unitPlot(m_core, player, unit, terrain, feature))
			return false;
		return m_trig->plotTag(terrain, feature, out);
	}
	void drawScene(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	               const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid) override;
	void beginFrame(const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid) override;
	bool beginHead(const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid) override;   // ticket 0711
	void beginBody(std::vector<std::string>* lines) override;                                     // ticket 0711
	void beginJoined(const srvrender::FrameJobResult& r) override;                                 // ticket 0711
	srvrender::FrameJobOwner* jobOwner() override { return &m_owner; }                             // ticket 0711 (R2)
	void drawReflection(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t size,
	                    const float* shadowMatrix1) override;
	bool drawMask(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h, const float* shadowMatrix0) override;   // 4785
	bool ready() const override
	{
		m_owner.mayCall("UnitsHost::ready");
		return m_passed && !m_failed && m_draw && m_draw->isOpen() && m_core.tableTaken() && m_lastFrameOk;
	}
	bool failed(std::string& why) const override
	{
		m_owner.mayCall("UnitsHost::failed");
		if(m_failed)
			why = m_failWhy;
		return m_failed;
	}
	bool animating() const override   // 4981: an extra's motion
	{
		m_owner.mayCall("UnitsHost::animating");
		return m_built.fading || (m_extras && m_extras->animating());
	}
	void secondLine(char* out, size_t n) override
	{
		m_owner.mayCall("UnitsHost::secondLine");
		if(!m_opt.tableFile.empty())
			writeTable();   // issue 4460: TEST ONLY (render_test_units_table)
		if(!m_opt.animFile.empty() && m_animRecord.dirty() && m_animRecord.frames() != m_animPostedFrames)   // issue 4456: TEST ONLY (render_test_units_anim)
		{
			// ticket 5054: a copy of the record, written by the TEST files' worker
			std::shared_ptr<AnimRecord> rec = std::make_shared<AnimRecord>(m_animRecord);
			const std::wstring path = m_opt.animFile;
			if(testPost(3, [rec, path](std::string& why) { return rec->write(path, why); }))
				m_animPostedFrames = m_animRecord.frames();
		}
		testCollect();
		const CoreCounters& c = m_core.counters();
		snprintf(out, n,
		         "units 1s: ready=%d precheck=%s table=%zu listed=%zu culled=%zu not_drawn=%zu drawn_frames=%lld pass_draws=%llu "
		         "not_ready_frames=%lld refused=%llu colour_unknown=%llu out_of_step=%llu fog_hidden=%zu fog=%s anim_drawn=%zu anim_quiet=%zu "
		         "anim_ms_max=%.1f frame_ms_max=%.1f slow_frames=%llu anim_unread=%llu anim_revived=%llu anim_not_formed=%llu "
		         "anim_recreated=%llu anim_rereads=%llu",
		         ready() ? 1 : 0, precheckState(), m_core.tableSize(), m_built.listed, m_built.culled, m_built.notDrawn,
		         (long long)m_drawnFrames, (unsigned long long)m_passDraws, (long long)m_notReadyFrames,
		         (unsigned long long)c.byStatus[(int)Status::kUnitRefused], (unsigned long long)c.byStatus[(int)Status::kColourUnknown],
		         (unsigned long long)c.byStatus[(int)Status::kAllocatorOutOfStep], m_fogHidden, m_fogState, m_animDrawn, m_animQuiet,
		         m_animMsMax, m_frameMsMax, (unsigned long long)m_slowFrames, m_animUnread, m_animRevived,
		         (unsigned long long)m_animFeed.counters().createsNotFormed, (unsigned long long)m_animFeed.counters().recreated,
		         (unsigned long long)m_animFeed.counters().rereads);   // issue 4739
		m_animMsMax = 0;   // issue 4468: the maxima are per line (per second)
		m_frameMsMax = 0;
		// issue 4755: the position source and its counts (since the start)
		const size_t used = strlen(out);
		if(used < n)
		{
			const PosCounters& p = m_core.posCounters();
			snprintf(out + used, n - used,
			         " pos=%s positions=%llu arrivals=%llu walks=%llu open=%zu arrived=%llu quiet_ends=%llu place_ends=%llu before_move=%llu "
			         "pos_bad=%llu pos_unknown=%llu exe_frame=%llu",
			         m_opt.positionsExe ? "exe" : "own", (unsigned long long)p.positions, (unsigned long long)p.arrivals,
			         (unsigned long long)p.walks, m_core.walksOpen(), (unsigned long long)p.arrived, (unsigned long long)p.quietEnds,
			         (unsigned long long)p.placeEnds, (unsigned long long)p.beforeMove, (unsigned long long)p.bad,
			         (unsigned long long)p.unknownUnit, (unsigned long long)p.lastExeFrame);
		}
		// ticket 0707: the second's begun frames and the MEAN of beginFrame's parts per begun frame (ms): build + fog filter,
		// the controller (with route S's poll), prepare, the whole begin
		const size_t used3 = strlen(out);
		if(used3 < n)
		{
			const double bn = m_beginSecN > 0 ? (double)m_beginSecN : 1.0;
			snprintf(out + used3, n - used3, " begun=%lld begin_ms=%.3f/%.3f/%.3f/%.3f", (long long)m_beginSecN, m_beginSecSum[0] / bn,
			         m_beginSecSum[1] / bn, m_beginSecSum[2] / bn, m_beginSecSum[3] / bn);
		}
		m_beginSecN = 0;
		for(double& s : m_beginSecSum)
			s = 0;
		// ticket 0711 (J1): the second's begins that ran as a frame job (jobs), the mean of the job's own time (job_ms, on
		// whichever thread ran it), the render thread's wait for a worker's run at the join (join_wait_ms mean/max) and the
		// jobs no worker had claimed at the join, run there by the render thread (inline, R4)
		const size_t used4 = strlen(out);
		if(used4 < n)
		{
			const double jn = m_jobSecN > 0 ? (double)m_jobSecN : 1.0;
			snprintf(out + used4, n - used4, " jobs=%lld job_ms=%.3f join_wait_ms=%.3f/%.3f inline=%lld", (long long)m_jobSecN,
			         m_jobSecMs / jn, m_jobSecWait / jn, m_jobSecWaitMax, (long long)m_jobSecInline);
		}
		m_jobSecN = m_jobSecInline = 0;
		m_jobSecMs = m_jobSecWait = m_jobSecWaitMax = 0;
		trigLine("units trigger 1s", true);   // ticket 4728
		extrasText(out, n);   // issue 4981
	}
	void totalsLine(char* out, size_t n) override
	{
		m_owner.mayCall("UnitsHost::totalsLine");
		const CoreCounters& c = m_core.counters();
		const SeedCounters sc = m_core.seedCounters();
		std::string st;
		for(int i = 0; i < (int)Status::kCount; ++i)
			st += std::string(" ") + statusName((Status)i) + "=" + std::to_string(c.byStatus[i]);
		snprintf(out, n,
		         "units totals: ready=%d precheck=%s entries=%zu precheck_ms=%.0f batches=%llu unit_events=%llu table_copies=%llu frames=%llu "
		         "listed=%llu culled=%llu drawn_units=%llu pass_draws=%llu not_ready_frames=%lld no_grid_frames=%lld dumps=%llu | seeds "
		         "created=%llu freed=%llu held=%llu episodes=%llu | anim %s frames=%llu events=%llu creates=%llu removes=%llu held=%zu "
		         "not_held=%llu not_formed=%llu work_stops=%llu work_unknown=%llu redraws=%llu player_changes=%llu resyncs=%llu start_frame=%lld | "
		         "fog hidden_units=%llu unfiltered_frames=%lld |%s",
		         ready() ? 1 : 0, precheckState(), m_preEntries, m_preMs, (unsigned long long)c.batches, (unsigned long long)c.unitEvents,
		         (unsigned long long)c.tableCopies, (unsigned long long)c.frames, (unsigned long long)c.listed, (unsigned long long)c.culled,
		         (unsigned long long)c.drawnUnits, (unsigned long long)m_passDraws, (long long)m_notReadyFrames, (long long)m_noGridFrames,
		         (unsigned long long)m_dumps, (unsigned long long)sc.created, (unsigned long long)sc.freed, (unsigned long long)sc.heldRemovals,
		         (unsigned long long)sc.outOfStepEpisodes, !m_opt.anim ? "off" : m_anim && m_anim->isOpen() ? "on" : "not-open",
		         (unsigned long long)m_animFeed.counters().frames, (unsigned long long)m_animFeed.counters().events,
		         (unsigned long long)m_animFeed.counters().creates, (unsigned long long)m_animFeed.counters().removes, m_animFeed.held(),
		         (unsigned long long)m_animFeed.counters().droppedNotHeld, (unsigned long long)m_animFeed.counters().createsNotFormed,
		         (unsigned long long)m_animFeed.counters().workStops, (unsigned long long)m_animFeed.counters().workUnknown,
		         (unsigned long long)m_animFeed.counters().redraws, (unsigned long long)m_animFeed.counters().playerChanges,
		         (unsigned long long)m_animFeed.counters().resyncs, (long long)m_animStartFrame, (unsigned long long)m_fogHiddenTotal,
		         (long long)m_fogUnfilteredFrames, st.c_str());
		// issue 4755: the positions' totals (compact: the line is near the log's line body)
		const size_t used = strlen(out);
		if(used < n)
		{
			const PosCounters& p = m_core.posCounters();
			const AnimCounters& ac = m_animFeed.counters();
			snprintf(out + used, n - used,
			         " | pos %s p=%llu a=%llu in=%llu out=%llu w=%llu mv=%llu arr=%llu idle=%llu quiet=%llu place=%llu rm=%llu load=%llu "
			         "f7=%llu unk=%llu bad=%llu ooo=%llu own=%llu hold=%llu first_max=%lld first_over2=%llu marks=%llu arrives=%llu deferred=%llu",
			         m_opt.positionsExe ? "exe" : "own", (unsigned long long)p.positions, (unsigned long long)p.arrivals,
			         (unsigned long long)p.inWalk, (unsigned long long)p.noWalk, (unsigned long long)p.walks, (unsigned long long)p.moves,
			         (unsigned long long)p.arrived, (unsigned long long)p.arrivalsIdle, (unsigned long long)p.quietEnds,
			         (unsigned long long)p.placeEnds, (unsigned long long)p.removeEnds, (unsigned long long)p.loadEnds,
			         (unsigned long long)p.beforeMove, (unsigned long long)p.unknownUnit, (unsigned long long)p.bad,
			         (unsigned long long)p.outOfOrder, (unsigned long long)p.ignoredOwn, (unsigned long long)p.holdFrames,
			         (long long)p.firstPosFramesMax, (unsigned long long)p.firstPosOver2, (unsigned long long)p.frameMarks,
			         (unsigned long long)ac.arrives, (unsigned long long)ac.arrivesDeferred);
			// ticket 5065: the hold behind walks (only while its table holds a kind: off, the line as before 5065)
			const size_t used2 = strlen(out);
			if(m_animFeed.holdsAnyAfterWalk() && used2 < n)
				snprintf(out + used2, n - used2, " after_walk=%llu/%llu/%llu", (unsigned long long)ac.afterWalkHeld,
				         (unsigned long long)ac.afterWalkReleased, (unsigned long long)ac.afterWalkNoWalk);
			// ticket 5066 (a): the mover's combats behind walks (only while the table holds kind 25: off, the line as before)
			const size_t used3 = strlen(out);
			if(m_animFeed.holdsAfterWalk(SrvFeed::kUnitCombat) && used3 < n)
				snprintf(out + used3, n - used3, " fight_walk=%llu/%llu/%llu", (unsigned long long)ac.fightWalkHeld,
				         (unsigned long long)ac.fightWalkBehind, (unsigned long long)ac.fightWalkDropped);
		}
		const size_t used2 = strlen(out);   // ticket 0711: the begins run as frame jobs, and those run inline at the join
		if(used2 < n)
			snprintf(out + used2, n - used2, " | jobs %lld inline=%lld", (long long)m_jobsTotal, (long long)m_jobsInlineTotal);
		trigLine("units trigger totals", false);   // ticket 4728
		extrasText(out, n);   // issue 4981
	}
	// ticket 4728: the trigger stream's records and the effect routes, a line of its own (changedOnly: the 1 s line's, only
	// when a count moved)
	void trigLine(const char* head, bool changedOnly)
	{
		if(!m_trig || !m_trig->emitter())
			return;
		const srveffects::EmitterCounters& e = m_trig->emitter()->counters();
		const srveffects::GeometryCounters* g = m_trig->geometry() ? &m_trig->geometry()->counters() : nullptr;
		char tl[640];
		// 4783 (W33 #8801, MC #8807 (3)): late = the late combat results taken / applied through the ring (to a record not
		// yet added + to a fight of the clock) / for no fight held; oz = fights' OwnerEnds still without an event id;
		// 4732: tail = records sent in a victory tail (R7's unit-owned form, owner 0 and fightSeq 0: sounds and effects)
		const AnimCounters& ac = m_animFeed.counters();
		snprintf(tl, sizeof tl, "fights=%llu ss=%llu es=%llu em=%llu est=%llu oe=%llu redraws=%llu back=%llu routes=%d planned=%llu "
		         "bones=%llu/%llu late=%llu applied=%llu late_nofight=%llu oz=%llu tail=%llu tier=%llu",
		         (unsigned long long)e.fights, (unsigned long long)e.kinds[VPTRIG_SOUND_START], (unsigned long long)e.kinds[VPTRIG_EFFECT_START],
		         (unsigned long long)e.kinds[VPTRIG_EFFECT_MOVE], (unsigned long long)e.kinds[VPTRIG_EFFECT_STOP],
		         (unsigned long long)e.kinds[VPTRIG_OWNER_END], (unsigned long long)e.redraws, (unsigned long long)e.framesBack,
		         m_trig->routes() ? 1 : 0, (unsigned long long)(g ? g->planned : 0), (unsigned long long)(g ? g->boneOk : 0),
		         (unsigned long long)(g ? g->boneAsks : 0), (unsigned long long)ac.lateResults,
		         (unsigned long long)(ac.lateToRecord + ac.lateToFight), (unsigned long long)ac.lateNoFight,
		         (unsigned long long)ac.ownerZeroEnds, (unsigned long long)e.victoryTail,
		         (unsigned long long)e.droppedTier);   // 4737: Sound triggers the tier gate kept back
		if(m_anim)   // ticket 4742: units still moved after their fight's end (the walk home inside the fight left a gap)
		{
			const size_t used = strlen(tl);
			if(used < sizeof tl)
				snprintf(tl + used, sizeof tl - used, " resid=%llu/%llu", (unsigned long long)m_anim->motionCounters().residualWalks,
				         (unsigned long long)m_anim->motionCounters().returns);
		}
		if(m_opt.triggerStrikerWeapon || m_opt.triggerTerrain)   // ticket 5052: only with a knob on (the line as before off)
		{
			const srveffects::NameCounters nc = m_trig->nameCounters();
			const size_t used = strlen(tl);
			if(used < sizeof tl)
				snprintf(tl + used, sizeof tl - used, " sw=%llu/%llu/%llu terr=%llu/%llu/%llu/%llu",
				         (unsigned long long)nc.strikerNamed, (unsigned long long)nc.strikerCity,
				         (unsigned long long)nc.strikerNoWeapon, (unsigned long long)nc.plotFeature,
				         (unsigned long long)nc.plotTerrain, (unsigned long long)nc.plotDirt, (unsigned long long)nc.plotNoTable);
		}
		if(m_opt.triggerStrikerWeapon && m_opt.triggerCityEra)   // ticket 5058: the city-era names (absent off)
		{
			const size_t used = strlen(tl);
			if(used < sizeof tl)
				snprintf(tl + used, sizeof tl - used, " ce=%llu", (unsigned long long)m_trig->nameCounters().strikerCityEra);
		}
		if(m_opt.triggerProjectilePose && m_trig->routes())   // ticket 5064: held starts posed / scaled, launch yaws (absent off)
		{
			const srveffects::RouteCounters& rc = m_trig->routes()->counters();
			const size_t used = strlen(tl);
			if(used < sizeof tl)
				snprintf(tl + used, sizeof tl - used, " pp=%llu/%llu/%llu/%llu yaw=%llu/%llu", (unsigned long long)rc.heldBoneTransform,
				         (unsigned long long)rc.heldNoBoneTransform, (unsigned long long)rc.heldScaleMember,
				         (unsigned long long)rc.heldScaleUnknown, (unsigned long long)(g ? g->planYawUnknown : 0),
				         (unsigned long long)(g ? g->planYawToTarget : 0));
		}
		if(m_opt.triggerCityVolley && m_trig->routes() && m_trig->routes()->counters().cityLaunches)   // ticket 0742: city
			// shots at launch / flown / not / landings flown; only once a city has shot (absent off)
		{
			const srveffects::RouteCounters& rc = m_trig->routes()->counters();
			const size_t used = strlen(tl);
			if(used < sizeof tl)
				snprintf(tl + used, sizeof tl - used, " cv=%llu/%llu/%llu/%llu", (unsigned long long)rc.cityLaunches,
				         (unsigned long long)rc.cityLaunchFlights, (unsigned long long)rc.cityLaunchNotFlown,
				         (unsigned long long)rc.hitCityVolleyFlown);
		}
		if(changedOnly && m_trigLast == tl)
			return;
		m_trigLast = tl;
		say(std::string(head) + ": " + tl);
	}
	std::string m_trigLast;   // ticket 4728

	// ---- UnitsHostProbe ----
	UnitsCore& core() override { return m_core; }
	const BuiltFrame& lastBuilt() const override { return m_built; }
	bool precheckRunning() const override { return m_pre != nullptr; }
	bool precheckPassed() const override { return m_passed; }
	void waitPrecheck() override
	{
		if(m_pre && m_pre->th.joinable())
			m_pre->th.join();
		if(m_pre)
			evaluate();
	}
	size_t precheckEntries() const override { return m_preEntries; }
	uint64_t passDraws() const override { return m_passDraws; }
	uint64_t dumpsWritten() const override { return m_dumps; }
	uint64_t prepares() const override { return m_prepares; }
	uint64_t reflectionDraws() const override { return m_reflectionDraws; }
	uint64_t maskDraws() const override { return m_maskDraws; }   // ticket 4785
	void attachExtras(std::unique_ptr<UnitsExtras> x, const Vp64Opt::Options& o, bool unitsAreOurs) override   // issue 4981
	{
		m_core.setExtras(nullptr);
		m_extras.reset();
		if(!x)
		{
			say("units: extras: module not linked (the scene as before)");
			return;
		}
		x->setLog(&ExtrasLog, this);
		x->configure(o, unitsAreOurs);
		const size_t n = x->inEffect();
		say("units: extras: module linked, " + std::to_string(n) + " in effect: " + (n ? "attached" : "dropped (the scene as before)"));
		if(!n)
			return;   // each extra that will not run said why in configure
		m_extras = std::move(x);
		m_core.setExtras(m_extras.get());
		if(m_draw && m_extras->wantsPlacement())   // issue 4983 (d): the placement source is chosen when the renderer opens
			dropDraw();                            // (re-opened at the next draw, as after a device loss)
	}
	UnitsExtras* extras() const override { return m_extras.get(); }
	const AnimRunner* animRunner() const override { return m_anim.get(); }   // ticket 5062
	srveffects::TriggerStream* triggerStream() override { return m_trig.get(); }   // ticket 5062 (W107)

private:
	static void ExtrasLog(void* ctx, const char* line) { static_cast<UnitsHost*>(ctx)->say(line); }
	std::unique_ptr<UnitsExtras> m_extras;   // issue 4981 (declared before m_core, so destroyed after it: the core points at it)
	void extrasText(char* out, size_t n)      // issue 4981: the seam's counts and the module's, appended to a line
	{
		if(!m_extras)
			return;
		const size_t used = strlen(out);
		if(used + 1 >= n)
			return;
		const CoreCounters& c = m_core.counters();
		snprintf(out + used, n - used, " | x shown=%llu show_refused=%llu root=%llu heading=%llu scaled=%llu foot=%llu held=%llu bad=%llu",
		         (unsigned long long)c.xShown, (unsigned long long)c.xShowRefused, (unsigned long long)c.xRoot,
		         (unsigned long long)c.xHeading, (unsigned long long)c.xScaled, (unsigned long long)c.xFoot, (unsigned long long)c.xHeld,
		         (unsigned long long)c.xBad);
		const size_t used2 = strlen(out);
		if(used2 + 1 < n)
			m_extras->countsText(out + used2, n - used2);
	}
	double now() const { return m_clock ? m_clock() : srvrender::QpcToMs(srvrender::Qpc()); }
	const char* precheckState() const
	{
		return m_failed ? "FAILED" : m_passed ? "passed" : m_pre ? "running" : m_haveDb ? "none" : "no-database";
	}
	void say(const std::string& s)
	{
		// ticket 0711 (R6): inside a frame job's beginBody the line is collected (written at the join, in order)
		if(m_bodyLines && GetCurrentThreadId() == m_bodyThread)
		{
			m_bodyLines->push_back(s);
			return;
		}
		if(m_log)
			m_log(s.c_str());
	}
	void fail(const std::string& why, Status st = Status::kPrecheckFailed)
	{
		m_core.note(st, "fatal", "%s", why.c_str());
		if(!m_failed)
		{
			m_failed = true;
			m_failWhy = "units: " + why;
		}
	}
	void startDatabase(const std::wstring& db, uint32_t gen);
	void evaluate();
	void writeTable();
	bool openDraw();
	void dump(int64_t frameId, const cu::UnitScene& sc);
	void dropPlans()   // the plans point into the drawing renderer's model cache
	{
		for(int c = 0; c < 3; ++c)
		{
			m_plans[c].clear();
			m_planOk[c] = false;
		}
		m_frameBegun = false;
	}
	void dropDraw()   // ticket 0704: the renderer, then the worker's environment it points into (null when opened here)
	{
		m_draw.reset();
		m_drawRun.reset();
	}
	bool startOpen();      // ticket 0704: the worker's open of the drawing renderer (openAsync)
	bool takeOpen();       // ticket 0704: its result into m_draw once done; false: not (yet) open
	bool drawPass(ID3D11DeviceContext* ctx, int c, cu::PassKind kind, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	              uint32_t h, const float* viewProj);

	UnitsOptions m_opt;
	FeedPort& m_port;
	SwitchPort m_switch;
	EnvOpener m_open;
	LogFn m_log;
	ClockFn m_clock;
	UnitsCore m_core;
	ID3D11Device* m_dev = nullptr;
	// the database (render thread): its environment, lookups and the art resolver of the era-change test
	bool m_haveDb = false;
	uint32_t m_dbGen = 0;
	std::wstring m_dbPath;
	std::unique_ptr<Env> m_env;
	std::unique_ptr<cu::GameFilesProvider> m_provider;
	std::unique_ptr<cu::ArtResolver> m_resolver;
	// the pre-check
	std::unique_ptr<PrecheckRun> m_pre;
	bool m_passed = false, m_failed = false;
	std::string m_failWhy;
	size_t m_preEntries = 0;
	double m_preMs = 0;
	// drawing
	std::unique_ptr<cu::PlacementSource> m_extrasInnerPlace;   // issue 4983 (d): the module's own placement under the extras'
	                                                           // decorator when the controller gives none (before m_draw:
	                                                           // destroyed after the renderer that points at it)
	// ticket 0704: the worker's open (running or done, not yet taken), and the taken one's environment (before m_draw:
	// destroyed after the renderer that points into it; null when the renderer was opened on the render thread)
	std::unique_ptr<OpenRun> m_openRun, m_drawRun;
	std::unique_ptr<Env> m_preEnv;                // ticket 0704: the passed pre-check's environment, for the next OpenRun
	std::map<int32_t, std::string> m_unitTypes;   // ticket 0704: Lookups::units (index -> Units.Type) for the warm list
	int64_t m_openWaitFrames = 0;                 // ticket 0704: frames not drawn while the worker opened (this open)
	uint64_t m_openRuns = 0, m_openStale = 0;     // ticket 0704: worker opens started, dropped (another device or database)
	bool m_openAsyncFailed = false;               // ticket 0704: the worker thread could not start: opened here from then on
	std::unique_ptr<cu::UnitRenderer> m_draw;
	BuiltFrame m_built;
	bool m_lastFrameOk = false;
	int64_t m_drawnFrames = 0, m_notReadyFrames = 0, m_noGridFrames = 0;
	uint64_t m_passDraws = 0, m_dumps = 0;
	std::string m_lastDumpText;
	bool m_dumpCapLogged = false;
	// issue 4455: the frame begun by beginFrame (its plans per wrap copy, used by the reflection and the scene pass)
	cu::FramePlan m_plans[3];
	bool m_planOk[3] = {false, false, false};
	bool m_frameBegun = false, m_frameOk = true;
	UnitsCamera m_cam;
	float m_waterZ = 0;
	uint64_t m_prepares = 0, m_reflectionDraws = 0;
	std::unique_ptr<srveffects::TriggerStream> m_trig;   // ticket 4722 (before m_anim: the runner holds its tap)

	uint64_t m_maskDraws = 0;   // ticket 4785: mask-pass draw() calls that returned Ok
	bool m_testOffsetSaid = false;   // ticket 4785: render_test_units_offset said once

	// issue 4456: the controller (unit-server-scene.md 6): its input from the feed, the runner, the TEST ONLY record
	AnimFeed m_animFeed;
	std::unique_ptr<AnimRunner> m_anim;
	AnimRecord m_animRecord;
	bool m_animFileFailLogged = false;
	int64_t m_animStartFrame = -1;
	void animFrame(double t, int64_t frameId);
	// issue 4468: the frame's drawn set given to the controller, its last counts and time, the slow frames' timings
	std::set<c5r::unitanim::Controller::UnitKey> m_drawnSet;
	size_t m_animDrawn = 0, m_animQuiet = 0;
	unsigned long long m_animUnread = 0, m_animRevived = 0;   // issue 0439: since the start (Controller::ApplyCounts)
	double m_animMs = 0, m_animMsMax = 0, m_frameMsMax = 0;
	uint64_t m_slowFrames = 0;
	double m_beginMs[4] = {0, 0, 0, 0};   // the last beginFrame: build + fog, the controller, prepare, total
	double m_beginSecSum[4] = {0, 0, 0, 0};   // ticket 0707: the sums of m_beginMs over the second's begun frames
	int64_t m_beginSecN = 0;
	// ticket 0711 (J1): beginHead's inputs for beginBody, by value (R1); the body's line sink while it runs (R6); the module's
	// owner flag (R2); the jobs of the second (beginJoined) and since the start
	UnitsCamera m_bodyCam;
	const c5r::heightfield::HeightGrid* m_bodyGrid = nullptr;
	double m_bodyT = 0;
	std::vector<std::string>* m_bodyLines = nullptr;
	DWORD m_bodyThread = 0;
	srvrender::FrameJobOwner m_owner{"units"};
	// MC #12834: route S's trigger section has one writer thread at a time; its writer paths in this host claim it
	srvrender::FrameJobsOneWriter m_trigWriter{"route S's trigger section"};
	int64_t m_jobSecN = 0, m_jobSecInline = 0, m_jobsTotal = 0, m_jobsInlineTotal = 0;
	double m_jobSecMs = 0, m_jobSecWait = 0, m_jobSecWaitMax = 0;
	// issue 4460: the table file's last written state
	uint64_t m_tableKey = ~0ull, m_tableWrites = 0;
	bool m_tableFailLogged = false;
	// ticket 5054: the TEST files' writer (kinds: 1 dump, 2 table, 3 anim), the files it skipped in the episode not yet said
	TestFiles m_testFiles;
	uint64_t m_testSkipped = 0, m_testSkippedTotal = 0;
	size_t m_animPostedFrames = 0;
	bool testPost(int kind, TestFiles::Job job);
	void testCollect();
	bool waitTestFiles() override
	{
		const bool ok = m_testFiles.wait(10000);
		testCollect();
		return ok;
	}
	// issue 4463: the active team's fog per plot (setPlotStates) and the drawn-list filter by it
	std::vector<uint8_t> m_plotStates;
	uint32_t m_plotStatesW = 0, m_plotStatesH = 0, m_plotStatesGen = 0;
	bool m_havePlotStates = false;
	// ticket 5052 (T): each plot's terrain and feature (setPlotTable, only with UnitsOptions::triggerTerrain)
	PlotTerrainCopy m_plotTable;
	size_t m_fogHidden = 0;               // units the last frame did not list for the fog (unique keys)
	uint64_t m_fogHiddenTotal = 0;        // unit-frames
	int64_t m_fogUnfilteredFrames = 0;    // frames without a filter (no states, no active team, another grid)
	const char* m_fogState = "none";      // the last frame's: "on", "none" (no states), "no-team", "grid"
	std::set<std::string> m_fogSaid;
	void filterByFog();
};

}  // namespace (issue 4470: the fog filter's two functions are srvunits', declared in srv_units_scene.h)

// Issue 4470: 4463's filter as one function (srv_units_scene.h), moved here unchanged from UnitsHost::filterByFog.
FogFilterResult FilterByFog(BuiltFrame& built, const std::vector<SrvFeed::PlayerRecord>& players, uint32_t feedWidth,
                            const uint8_t* states, size_t n, uint32_t width, uint32_t height)
{
	static_assert((int)c5r::fow::PlotState::Visible == 0, "setPlotStates documents 0 = visible");
	FogFilterResult r;
	int activeTeam = -1;
	for(const SrvFeed::PlayerRecord& p : players)
		if(p.player >= 0)
		{
			activeTeam = p.activeTeam;
			break;
		}
	if(!states || !n)
	{
		r.state = "none";
		r.why = "units: no plot states from the fog scene: other players' units are listed on every plot (unit-server-scene.md 12.1)";
	}
	else if(activeTeam < 0)
	{
		r.state = "no-team";
		r.why = "units: the player rows name no active team: other players' units are listed on every plot";
	}
	else if(width != feedWidth || (size_t)width * height != n)
	{
		r.state = "grid";
		r.why = "units: the fog's plot states are not of the feed's grid: other players' units are listed on every plot";
	}
	if(r.why)
		return r;
	r.state = "on";
	std::set<int64_t> hidden;
	for(cu::UnitScene& sc : built.copies)
	{
		std::vector<cu::UnitInput>& v = sc.units;
		v.erase(std::remove_if(v.begin(), v.end(),
		                       [&](const cu::UnitInput& u) {
			                       const bool row = u.player >= 0 && u.player < (int32_t)players.size() &&
			                                        players[(size_t)u.player].player == u.player;
			                       if(row && players[(size_t)u.player].team == activeTeam)
				                       return false;
			                       const size_t i = (size_t)u.plot_y * width + (size_t)u.plot_x;
			                       const bool seen = u.plot_x >= 0 && u.plot_y >= 0 && (uint32_t)u.plot_x < width && i < n && states[i] == 0;
			                       if(!seen)
				                       hidden.insert(UnitKeyOf(u.player, u.id));
			                       return !seen;
		                       }),
		        v.end());
		std::vector<cu::PlayerInput>& ps = sc.players;
		ps.erase(std::remove_if(ps.begin(), ps.end(),
		                        [&](const cu::PlayerInput& p) {
			                        for(const cu::UnitInput& u : v)
				                        if(u.player == p.player)
					                        return false;
			                        return true;
		                        }),
		         ps.end());
	}
	r.hidden = hidden.size();
	built.listed -= (std::min)(built.listed, hidden.size());
	return r;
}

bool FogStatesOfTable(const std::vector<SrvFeed::PlotRecord>& table, uint32_t width, uint32_t height, std::vector<uint8_t>& states)
{
	if(table.empty() || (uint64_t)width * height != table.size())
		return false;
	states.resize(table.size());
	for(size_t i = 0; i < table.size(); ++i)
		states[i] = (uint8_t)c5r::fow::stateFromFowBits(table[i].fow);
	return true;
}

// ticket 5052 (T): srv_units_scene.h PlotTerrainCopy
bool PlotTerrainCopy::set(const SrvFeed::PlotRecord* plots, size_t n, uint32_t width, uint32_t height, uint32_t key)
{
	if(!plots || !n || (uint64_t)width * height != n)
	{
		m_have = false;
		return false;
	}
	if(m_have && key == m_key && width == m_w && n == m_tf.size() / 2)
		return false;
	m_tf.resize(n * 2);
	for(size_t i = 0; i < n; ++i)
	{
		m_tf[2 * i] = plots[i].terrain;
		m_tf[2 * i + 1] = plots[i].feature;
	}
	m_w = width;
	m_key = key;
	m_have = true;
	m_copies++;
	return true;
}

bool PlotTerrainCopy::unitPlot(const UnitsCore& core, int32_t player, int32_t unit, int32_t& terrain, int32_t& feature) const
{
	if(!m_have)
		return false;
	SrvFeed::UnitRecord r;
	if(!core.record(player, unit, r) || r.plot < 0 || (size_t)r.plot >= m_tf.size() / 2 || (core.width() != 0 && core.width() != m_w))
		return false;
	terrain = m_tf[2 * (size_t)r.plot];
	feature = m_tf[2 * (size_t)r.plot + 1];
	return true;
}

namespace {

// Issue 4463 (unit-server-scene.md 12.1 [L]): the EXE does not draw another team's unit on a plot the active team does
// not see (after a load the DLL sends such units no Visibility call, note 2.5 (a)). After build, before the module sees
// the frame. Issue 4470: the rule is FilterByFog (srv_units_scene.h), which unitfeed calls too; this keeps the counters
// and says each reason for an unfiltered frame once.
void UnitsHost::filterByFog()
{
	const FogFilterResult r = FilterByFog(m_built, m_core.players(), m_core.width(), m_havePlotStates ? m_plotStates.data() : nullptr,
	                                      m_havePlotStates ? m_plotStates.size() : 0, m_plotStatesW, m_plotStatesH);
	m_fogState = r.state;
	m_fogHidden = r.hidden;
	if(r.why)
	{
		m_fogUnfilteredFrames++;
		if(m_fogSaid.insert(r.why).second)
			say(r.why);
		return;
	}
	m_fogHiddenTotal += r.hidden;
}

// Issue 4460 (TEST ONLY, render_test_units_table=FILE): the unit table as the scene holds it (after the last batch), each
// unit's seed, the player rows and the feed's events per kind 11-33, written when the scene's last seq, table generation
// or the batch count with player rows changed (checked once a second); FILE.tmp then a replacing rename, so a reader
// never sees half a file. Lines: "# c5-units-table 1", "state gen G last_seq S units N batches B time_ms T",
// "kinds k:n ..." (SrvFeed::GetCounters2 perKind 11..33), "P player team alive flags colour civilization era artStyle state",
// "U owner id unitType plot facing flags damage maxHP domain era artStyle seq seed" (seed -1: none held).
void UnitsHost::writeTable()
{
	// the seeds held count too: a drain's creations get their seeds at the next drawn frame (the allocator's Advance), so
	// a file written before the first frame would otherwise keep seed -1 until the next batch (live run 2026-11-16)
	uint64_t seeded = 0;
	for(const SrvFeed::UnitRecord& u : m_core.table())
	{
		uint32_t s = 0;
		seeded += m_core.seedOf(u.owner, u.id, s) ? 1 : 0;
	}
	const uint64_t key = ((uint64_t)m_core.lastSeq() << 32) ^ ((uint64_t)m_core.tableGen() << 20) ^ m_core.counters().tableCopies ^
	                     ((uint64_t)m_core.counters().batches << 40) ^ (seeded << 52);
	if(key == m_tableKey)
		return;
	// ticket 5054: the text here, the file by the TEST files' worker
	std::string text;
	char b[256];
	const SrvFeed::Counters2 c2 = SrvFeed::GetCounters2();
	text += "# c5-units-table 1\n";
	snprintf(b, sizeof(b), "state gen %u last_seq %u units %zu batches %llu time_ms %.0f\n", m_core.tableGen(), m_core.lastSeq(),
	         m_core.tableSize(), (unsigned long long)m_core.counters().batches, now());
	text += b;
	text += "kinds";
	for(int k = 11; k <= 33; ++k)
	{
		snprintf(b, sizeof(b), " %d:%llu", k, (unsigned long long)c2.perKind[k]);
		text += b;
	}
	text += "\n";
	for(const SrvFeed::PlayerRecord& p : m_core.players())
		if(p.player >= 0)
		{
			snprintf(b, sizeof(b), "P %d %d %u %u %d %d %d %d %u\n", p.player, p.team, p.alive, p.flags, p.colour, p.civilization, p.era,
			         p.artStyle, p.state);
			text += b;
		}
	for(const SrvFeed::UnitRecord& u : m_core.table())
	{
		uint32_t seed = 0;
		const bool hs = m_core.seedOf(u.owner, u.id, seed);
		snprintf(b, sizeof(b), "U %d %d %d %d %d %u %d %d %d %d %d %u %lld\n", u.owner, u.id, u.unitType, u.plot, u.facing, u.flags, u.damage,
		         u.maxHP, u.domain, u.era, u.artStyle, u.seq, hs ? (long long)seed : -1ll);
		text += b;
	}
	const std::wstring path = m_opt.tableFile;
	const bool posted = testPost(2, [path, text](std::string& why) {
		const std::wstring tmp = path + L".tmp";
		FILE* f = _wfopen(tmp.c_str(), L"wb");
		if(!f)
		{
			why = "cannot write " + Utf8(tmp);
			return false;
		}
		const bool wrote = fwrite(text.data(), 1, text.size(), f) == text.size();
		const bool ok = fclose(f) == 0 && wrote;
		if(!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			why = "cannot replace " + Utf8(path);
			return false;
		}
		return true;
	});
	if(!posted)
		return;   // skipped (counted): the next second tries again
	m_tableKey = key;
	m_tableWrites++;
}

void UnitsHost::startDatabase(const std::wstring& db, uint32_t gen)
{
	if(m_pre && m_pre->th.joinable())
		m_pre->th.join();   // a newer database while one is checked: the old check ends first (rare: a mod change)
	m_pre.reset();
	m_haveDb = true;
	m_dbGen = gen;
	m_dbPath = db;
	m_passed = false;
	dropPlans();
	dropDraw();
	if(m_openRun)   // ticket 0704: a worker's open of the old database: its rows not yet loaded skipped, joined, dropped
	{
		m_openRun->cancel = true;
		m_openRun.reset();
		m_openStale++;
	}
	m_preEnv.reset();
	m_unitTypes.clear();
	say("units: database " + Utf8(db) + " (generation " + std::to_string(gen) + "): pre-check started on a worker thread");
	// the render thread's own environment: lookups (index -> Type) and the era-change art test
	m_env.reset(new Env());
	std::string why;
	if(!m_open(db, *m_env, why))
	{
		m_env.reset();
		fail("pre-check: the unit database " + Utf8(db) + " cannot be opened: " + why);
		return;
	}
	Lookups l;
	if(!l.load(*m_env->db, why))
	{
		fail("pre-check: the unit database " + Utf8(db) + ": " + why);
		return;
	}
	m_provider.reset(new cu::GameFilesProvider(m_env->gf));
	m_resolver.reset(new cu::ArtResolver());
	const cu::UnitStatus rs = m_resolver->open(*m_env->db, *m_provider);
	if(!rs.ok())
	{
		fail("pre-check: the art tables: " + rs.str());
		return;
	}
	m_core.setDatabase(l, m_resolver.get());
	m_unitTypes = l.units;   // ticket 0704
	// issue 4456: the controller on this database (a new generation, a load: it starts again at the next drawn frame)
	m_anim.reset();
	m_animFeed = AnimFeed();
	m_animFeed.setPositionsExe(m_opt.positionsExe);   // issue 4755: the new feed takes the position source again
	m_animFeed.setOrdersAfterWalk(m_opt.ordersAfterWalk);   // ticket 5065 (rule A)
	m_animFeed.setFightAfterWalk(m_opt.fightAfterWalk);     // ticket 5066 (rule A; (a) holds combats only with a fight player)
	m_animStartFrame = -1;
	if(m_opt.anim)
	{
		std::unique_ptr<AnimRunner> r(new AnimRunner());
		if(!r->open(m_env->gf, *m_env->db, *m_resolver, why))
		{
			fail("the unit animation controller cannot open on " + Utf8(db) + ": " + why, Status::kAnimUnavailable);
			return;
		}
		m_animFeed.setBuilds(r->builds());
		m_animFeed.setRunner(r.get());   // issue 4774: the fights run on this controller
		r->setChargeAny(m_opt.chargeAny);   // ticket 4737 (rule A)
		r->setTurnFirst(m_opt.turnFirst);   // ticket 4735 (rule A)
		r->setFightDrawnFigures(m_opt.fightDrawnFigures);   // ticket 4744 (rule A)
		r->setReformFirst(m_opt.reformFirst);               // tickets 4745 + 4747 (rule A)
		r->setWalkHome(m_opt.walkHome);                     // ticket 4742 (rule A)
		r->setCityInPlace(m_opt.cityInPlace);               // ticket 4748 (rule A)
		r->setReadyAfterMove(m_opt.readyAfterMove);         // ticket 4746 (rule A)
		r->setDefendFromStance(m_opt.defendFromStance);     // ticket 4750 (rule A)
		r->setReformEndsAtArrival(m_opt.reformEndsAtArrival);   // ticket 5050 (rule A)
		r->setAttackFromStance(m_opt.attackFromStance);     // ticket 5050 (rule A)
		r->setRushRideThrough(m_opt.rushRideThrough);       // ticket 5057 (rule A)
		r->setDefenderWaitsFirstLanding(m_opt.defenderWaitsFirstLanding);   // ticket 5057 (rule A)
		r->setDeathByVariant(m_opt.deathByVariant);         // ticket 5053 (rule A)
		r->setEndAfterDying(m_opt.endAfterDying);           // ticket 5053 (rule A)
		r->setMeleeReach(m_opt.meleeReach);                 // ticket 0743 (rule A)
		r->setDefenderStep(m_opt.defenderStep);             // ticket 5061 (rule A)
		r->setVictoryNeedsWord(m_opt.victoryNeedsWord);     // ticket 5059 (rule A)
		r->setType2Phase2(m_opt.type2Phase2);               // ticket 5067 S1 (rule A)
		r->setCityShot(m_opt.triggerCityShot);              // ticket 5062 (rule A)
		r->useCore(&m_core);                                // ticket 5062: the city owner's era
		if(m_opt.triggerCityShot)   // ticket 5062: once per load, not LOUD (absent off: the log as before)
			say("units: city shot (5062): " + std::to_string(r->cityBombardTags()) + " era tags");
		if(!r->combatStrikeRule().empty())   // 4783 (W33, C1; MC #8906): which strike rule this load uses, once, not LOUD
			say("units: combat tables: " + r->combatStrikeRule());
		if(m_opt.quickCombat == 1)   // ticket 0796: V2 (off: the runner's default, every fight at real time)
		{
			r->setQuickCombat(c5r::unitanim::QuickCombatV2());
			say("units: quick combat (0796): V2");
		}
		m_trig.reset();   // ticket 4722: the trigger stream on this database (a new generation: the shell ends the old owners)
		if(!m_opt.triggerSection.empty() || !m_opt.triggerLog.empty())
		{
			srveffects::TriggerStreamConfig tc;
			tc.section = m_opt.triggerSection;
			tc.logPath = m_opt.triggerLog;
			tc.install = m_opt.install;
			tc.params.tierGate = m_opt.triggerTierGate;   // ticket 4737
			tc.files = &m_env->gf;   // ticket 4728: the effect routes (the install's effects, the bones' .gr2)
			tc.provider = m_provider.get();
			tc.strings = r->strings();
			tc.strikerWeapon = m_opt.triggerStrikerWeapon;   // ticket 5052 (rule A, W)
			tc.plotTerrain = m_opt.triggerTerrain;           // ticket 5052 (rule A, T)
			tc.strikerMelee = m_opt.triggerStrikerMelee;     // ticket 5055 (rule A): in effect only with strikerWeapon
			tc.cityEra = m_opt.triggerCityEra;               // ticket 5058 (rule A)
			tc.routes.projectilePose = m_opt.triggerProjectilePose;   // ticket 5064 (rule A)
			tc.routes.cityVolley = m_opt.triggerCityVolley;           // ticket 0742 (rule A)
			m_trig.reset(new srveffects::TriggerStream());
			if(m_trig->open(tc, *m_env->db, why))
			{
				m_trig->useCore(&m_core);
				if(m_opt.triggerTerrain)   // ticket 5052 (T): $(TERRAIN) from the unit's plot in setPlotTable's copy
				{
					m_trig->setUnitTerrain([this](int32_t player, int32_t unit, std::string& tag) { return unitPlotTag(player, unit, tag); });
					if(!m_trig->plotTagsWhy().empty())
						// 5052 part D (W89): no longer "TEST KEY": the key is on by default (MC #11356), so this LOUD line
						// reports a fault of the product's default (the database's Terrains / Features rows), not a test.
						say("LOUD: units: the plots' tags not read (render_test_units_trigger_terrain, every plot DIRT): " +
						    m_trig->plotTagsWhy());
				}
				if(m_opt.triggerStrikerWeapon || m_opt.triggerTerrain)   // ticket 5052: once per load, not LOUD
					say(std::string("units: trigger names (5052): striker_weapon ") + (m_opt.triggerStrikerWeapon ? "1" : "0") +
					    ", terrain " + (m_opt.triggerTerrain ? "1" : "0") +
					    (m_trig->strikerMelee() ? ", striker_melee 1 (5055)" : "") +   // 5055: only when in effect (the line as before off)
					    (m_trig->cityEra() ? ", city_era 1 (5058)" : ""));          // 5058: only when in effect (the line as before off)
				r->setTriggerTap(m_trig->tap());
				if(!m_trig->routes())   // ticket 4728
					say("LOUD: units: the effect routes did not open (sounds only, no combat effect is sent): " + m_trig->effectsWhy());
			}
			else
			{
				say("LOUD: units: the trigger stream cannot open (no combat sound or effect is sent): " + why);
				m_trig.reset();
			}
		}
		m_anim = std::move(r);
	}
	m_pre.reset(new PrecheckRun());
	m_pre->gen = gen;
	m_pre->db = db;
	m_pre->anim = m_anim != nullptr;   // issue 4468: the clip durations read ahead for the controller
	PrecheckRun* p = m_pre.get();
	EnvOpener op = m_open;
	p->th = std::thread([p, op]() { p->run(op); });
}

void UnitsHost::evaluate()
{
	if(!m_pre)
		return;
	if(m_pre->th.joinable())
		m_pre->th.join();
	std::unique_ptr<PrecheckRun> p = std::move(m_pre);
	m_preMs = p->ms;
	m_preEntries = p->out.size();
	if(!p->envWhy.empty())
	{
		fail("pre-check: database " + Utf8(p->db) + " unreadable: " + p->envWhy);
		return;
	}
	if(!p->openSt.ok())
	{
		fail("pre-check: the unit module did not open: " + p->openSt.str());
		return;
	}
	if(!p->st.ok())
	{
		fail("pre-check: " + p->st.str());
		return;
	}
	size_t bad = 0, warn = 0;
	std::map<std::string, size_t> warnKinds;   // issue 4460: per warning kind (TextureFallback, ClipMissingInInstall)
	std::string first;
	for(const cu::UnitTypeStatus& s : p->out)
	{
		if(!s.status.ok())
		{
			if(!bad)
				first = s.unit_type + " (" + s.art_row + ", style '" + s.art_style + "', era '" + s.era + "'): " + s.status.str();
			++bad;
			if(bad <= 20)
				say("units: pre-check: " + s.unit_type + " " + s.art_row + " style '" + s.art_style + "' era '" + s.era + "': " + s.status.str());
		}
		for(const std::string& w : s.status.warnings)
		{
			++warn;
			warnKinds[w.substr(0, w.find(':'))]++;
			say("units: pre-check warning (the unit is drawn): " + s.art_row + ": " + w);
		}
	}
	std::string kinds;
	for(const auto& kv : warnKinds)
		kinds += (kinds.empty() ? " (" : ", ") + kv.first + " " + std::to_string(kv.second);
	if(!kinds.empty())
		kinds += ")";
	char line[512];
	snprintf(line, sizeof(line), "units: pre-check of generation %u: %zu art rows, %zu failed, %zu warnings%s, %.0f ms", p->gen, p->out.size(),
	         bad, warn, kinds.c_str(), p->ms);
	say(line);
	if(bad)
	{
		fail("pre-check: " + std::to_string(bad) + " of " + std::to_string(p->out.size()) + " art rows failed: " + first);
		return;
	}
	if(p->out.empty())
	{
		fail("pre-check: no art row reached (the database has no unit type with art)");
		return;
	}
	if(m_anim && p->anim)   // issue 4468: the controller's start reads no clip file on the render thread
	{
		const size_t taken = m_anim->seedClipDurations(p->durations);
		char dl[512];
		snprintf(dl, sizeof(dl),
		         "units: the controller's clip durations read ahead on the pre-check's thread: %zu clips of %zu art rows (%zu without a "
		         "duration; %zu from the pre-check's clip cache, %zu read), %.0f ms; %zu taken%s%s",
		         p->durations.size(), p->durRows, p->durFailed, p->durCached, p->durRead, p->durMs, taken,
		         p->durWhy.empty() ? "" : "; not read: ", p->durWhy.c_str());
		say(dl);
	}
	m_passed = true;
	if(m_opt.openAsync)   // ticket 0704: the worker's open takes the pre-check's environment (no second open)
		m_preEnv = std::move(p->env);
}

bool UnitsHost::openDraw()
{
	if(m_draw && m_draw->isOpen())
		return true;
	if(!m_env || !m_dev)
		return false;
	if(m_opt.openAsync && !m_openAsyncFailed)   // ticket 0704: opened on a worker thread; until then the frame is not drawn
	{
		if(!takeOpen())
			return false;
	}
	else
	{
		m_draw.reset(new cu::UnitRenderer());
		const c5r::Status s = m_draw->open(m_dev, m_env->gf, *m_env->db, m_env->exe, m_env->install);
		if(!s.ok())
		{
			m_draw.reset();
			fail("the unit module did not open on the render device: " + s.str());
			return false;
		}
	}
	m_draw->setPoseSource(m_anim ? m_anim->pose() : nullptr);   // issue 4456: the controller's programs (null: the module's)
	cu::PlacementSource* place = m_anim ? m_anim->fightPlacement(m_env->gf, m_env->install) : nullptr;   // issue 4724: a fight's figure motion over the module's placement (null: the module's own)
	if(m_extras && m_extras->wantsPlacement())   // issue 4983 (d): an extra that places figures itself, over that source
	{
		if(!place)
		{
			c5r::Status ps;
			m_extrasInnerPlace = cu::makeModulePlacement(m_env->gf, m_env->install, &ps);
			place = m_extrasInnerPlace.get();
		}
		cu::PlacementSource* over = place ? m_extras->placementOver(place) : nullptr;
		if(over)
			place = over;
		else
			say("LOUD: units: extras: the placement decorator was not made: the figures are placed as before");
	}
	m_draw->setPlacementSource(place);
	say(m_anim ? "units: the drawing renderer is open on the render device, figures from the controller"
	           : "units: the drawing renderer is open on the render device, figures at the module's default clip (render_units_anim=0)");
	return true;
}

// Ticket 0704: the worker's open. The warm list is the table as the render thread holds it now: one unit per distinct (Units.Type,
// art style, era) of the units whose owner's art resolves (UnitsCore::playerArt, the rule buildList uses). An embarked unit's
// row (EmbarkedRow) and a unit created after this point are not in it: prepare loads such a row at its first frame, as before.
bool UnitsHost::startOpen()
{
	std::unique_ptr<OpenRun> run(new OpenRun());
	run->dev = m_dev;
	m_dev->AddRef();
	run->db = m_dbPath;
	run->gen = m_dbGen;
	run->tableUnits = m_core.tableSize();
	std::set<std::string> seen;
	std::map<int32_t, PlayerArt> arts;
	for(const SrvFeed::UnitRecord& u : m_core.table())
	{
		auto t = m_unitTypes.find(u.unitType);
		if(t == m_unitTypes.end())
			continue;
		const int32_t owner = (int32_t)u.owner;
		auto a = arts.find(owner);
		if(a == arts.end())
			a = arts.emplace(owner, m_core.playerArt(owner)).first;
		const PlayerArt& pa = a->second;
		if(!pa.have || !pa.artOk)
			continue;
		if(!seen.insert(t->second + '\n' + pa.in.art_style + '\n' + pa.in.era).second)
			continue;
		cu::UnitInput ui;
		ui.player = owner;
		ui.id = u.id;
		ui.unit_type = t->second;
		run->warm.emplace_back(ui, pa.in);
	}
	run->env = std::move(m_preEnv);   // the passed pre-check's (null after a device change: the worker opens one)
	run->startQpc = srvrender::Qpc();
	OpenRun* p = run.get();
	EnvOpener op = m_open;
	try
	{
		p->th = std::thread([p, op]() { p->run(op); });
	}
	catch(const std::exception& e)
	{
		m_openAsyncFailed = true;
		say(std::string("LOUD: units: the drawing renderer's worker thread did not start (") + e.what() +
		    "): it opens on the render thread from now on (ticket 0704)");
		return false;   // run's destructor releases the device
	}
	m_openRuns++;
	m_openWaitFrames = 0;
	char line[256];
	snprintf(line, sizeof(line),
	         "units: the drawing renderer opens on a worker thread (ticket 0704): %zu art keys of the table's %zu units loaded first",
	         p->warm.size(), p->tableUnits);
	say(line);
	m_openRun = std::move(run);
	return true;
}

// Ticket 0704: true once the worker's renderer is in m_draw (then openDraw sets its sources as for an open here). A result for
// another device or database is dropped and the open starts again; a failure is D125's, as the open here.
bool UnitsHost::takeOpen()
{
	if(!m_openRun)
	{
		startOpen();
		return false;   // never taken in the frame that started it
	}
	if(!m_openRun->done)
	{
		m_openWaitFrames++;
		return false;
	}
	std::unique_ptr<OpenRun> run = std::move(m_openRun);
	if(run->th.joinable())
		run->th.join();
	const double sinceStart = srvrender::QpcToMs(srvrender::Qpc() - run->startQpc);
	if(run->dev != m_dev || run->gen != m_dbGen || run->db != m_dbPath || run->cancel)
	{
		m_openStale++;
		say("units: the worker's open of the drawing renderer was for another device or database: dropped, opened again (ticket 0704)");
		run.reset();
		startOpen();
		return false;
	}
	if(!run->envWhy.empty())
	{
		fail("the unit module's environment did not open on the drawing renderer's worker thread: " + run->envWhy);
		return false;
	}
	if(!run->openSt.ok())
	{
		fail("the unit module did not open on the render device: " + run->openSt.str());
		return false;
	}
	if(!run->uploadSt.ok())   // not fatal: the scene pass's draw uploads what is not uploaded, and fails the frame as before
		say("LOUD: units: the worker's upload of the loaded art rows failed (the scene pass uploads them): " + run->uploadSt.str());
	m_draw = std::move(run->r);
	char line[512];
	snprintf(line, sizeof(line),
	         "units: the drawing renderer was opened on a worker thread (ticket 0704): environment %s%.0f ms, open %.0f ms, %zu art "
	         "keys loaded (%zu failed, %zu skipped) in %.0f ms, upload %.0f ms; worker %.0f ms, taken %.0f ms after its start, %lld "
	         "frames not drawn meanwhile; %zu art rows cached",
	         run->envReused ? "the pre-check's " : "", run->envMs, run->openMs, run->rowsOk, run->rowsFailed, run->rowsSkipped, run->rowsMs, run->uploadMs, run->ms, sinceStart,
	         (long long)m_openWaitFrames, m_draw->cachedRows());
	say(line);
	m_drawRun = std::move(run);
	return true;
}

void UnitsHost::dump(int64_t frameId, const cu::UnitScene& sc)
{
	if(m_opt.dumpDir.empty())
		return;
	std::string text;
	const cu::SceneFileStatus ws = cu::writeScene(sc, text);
	if(!ws.ok())
	{
		say("LOUD: units: dump of frame " + std::to_string(frameId) + " refused by the .c5units writer: " + ws.str());
		return;
	}
	if(text == m_lastDumpText)
		return;
	if((int64_t)m_dumps >= (int64_t)m_opt.dumpMax)
	{
		if(!m_dumpCapLogged)
			say("LOUD: units: render_test_units_dump: " + std::to_string(m_opt.dumpMax) + " files written, no more");
		m_dumpCapLogged = true;
		return;
	}
	// ticket 5054: written by the TEST files' worker (W70's 11 s render-thread stall was this file's create on a stalled C:)
	const std::wstring path = m_opt.dumpDir + L"\\frame_" + std::to_wstring(frameId) + L".c5units";
	if(!testPost(1, [path, text](std::string& why) {
		   FILE* f = _wfopen(path.c_str(), L"wb");
		   const bool wrote = f && fwrite(text.data(), 1, text.size(), f) == text.size();
		   const bool ok = f && fclose(f) == 0 && wrote;
		   if(!ok)
			   why = "dump " + Utf8(path) + " not written";
		   return ok;
	   }))
		return;   // skipped (counted); the next frame's differing scene is dumped
	m_lastDumpText = text;
	m_dumps++;
}

bool UnitsHost::testPost(int kind, TestFiles::Job job)
{
	if(!m_testFiles.post(kind, std::move(job)))
	{
		m_testSkipped++;
		m_testSkippedTotal++;
		return false;
	}
	if(m_testSkipped)
	{
		say("LOUD: units: TEST files: " + std::to_string(m_testSkipped) + " files not written, the writer was behind (" +
		    std::to_string(TestFiles::kMaxPending) + " pending; " + std::to_string(m_testSkippedTotal) + " in all; ticket 5054)");
		m_testSkipped = 0;
	}
	return true;
}

void UnitsHost::testCollect()
{
	std::vector<std::pair<int, std::string>> fails;
	m_testFiles.collect(fails);
	for(const std::pair<int, std::string>& f : fails)
	{
		if(f.first == 1)
			say("LOUD: units: " + f.second);
		else if(f.first == 2 && !m_tableFailLogged)
		{
			m_tableFailLogged = true;
			say("LOUD: units: TEST KEY render_test_units_table: " + f.second);
		}
		else if(f.first == 3 && !m_animFileFailLogged)
		{
			m_animFileFailLogged = true;
			say("LOUD: units: TEST KEY render_test_units_anim: " + Utf8(m_opt.animFile) + " not written: " + f.second);
		}
	}
}

void UnitsHost::beginFrame(const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid)
{
	m_owner.mayCall("UnitsHost::beginFrame");
	if(beginHead(cam, grid))
		beginBody(nullptr);
}

// Ticket 0711 (J1): the serial head of the begin, on the render thread (the Architect's section 5): the frame stamp, the
// readiness checks, the drawing renderer's open (0704's take of the worker's open), route S's grid and poll (IPC, R6).
// Route S's poll ran after the build and the fog filter before 0711; it reads only the shell's answer in the trigger section
// and the build reads nothing it writes, so the move changes the order of the log lines, not a value (pan-coherence.md 5.16).
bool UnitsHost::beginHead(const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid)
{
	m_owner.mayCall("UnitsHost::beginHead");
	m_frameBegun = false;
	m_frameOk = true;
	m_bodyT = cam.nowMs >= 0 ? cam.nowMs : now();   // R1: the one clock value of the begin, read here
	m_core.noteFrame(cam.frameId);   // issue 4755: every frame stamps (F9's clock), also one not built (unitfeed builds each)
	if(m_failed || !m_passed || !m_core.haveDatabase())
	{
		m_notReadyFrames++;
		m_lastFrameOk = false;
		return false;
	}
	if(!grid)
	{
		m_noGridFrames++;
		return false;
	}
	if(!openDraw())
	{
		m_notReadyFrames++;
		m_lastFrameOk = false;
		return false;
	}
	if(m_trig)
	{
		srvrender::FrameJobsOneWriter::Scope trigWrite(m_trigWriter, "UnitsHost::beginHead");   // MC #12834
		m_trig->setGrid(grid);   // ticket 4722: the units' plot centres of this frame
		std::vector<std::string> lines;   // ticket 4783 (R9): route S taken only when the shell answers ARMED; one line each
		m_trig->pollRouteS(lines);
		for(const std::string& l : lines)
			say(l);
	}
	m_bodyCam = cam;
	m_bodyGrid = grid;
	return true;
}

// Ticket 0711 (J1): the rest of the begin, in beginFrame's order (R5), on whichever thread runs the frame job. It reads
// what beginHead stored (the camera and the clock by value, the grid the render thread does not write before the join)
// and the module it owns. Route S's trigger records of the controller's frame go to the trigger section from here
// (the shell reads them after the frame's slot is READY, which the host marks after the join: 4722's R1 holds).
void UnitsHost::beginBody(std::vector<std::string>* lines)
{
	struct Sink
	{
		UnitsHost& h;
		Sink(UnitsHost& host, std::vector<std::string>* l) : h(host)
		{
			h.m_bodyLines = l;
			h.m_bodyThread = l ? GetCurrentThreadId() : 0;
		}
		~Sink()
		{
			h.m_bodyLines = nullptr;
			h.m_bodyThread = 0;
		}
	} sink(*this, lines);
	// MC #12834: the controller's frame writes route S's records into the trigger section from here (the job's thread)
	srvrender::FrameJobsOneWriter::Scope trigWrite(m_trigWriter, "UnitsHost::beginBody");
	const UnitsCamera& cam = m_bodyCam;
	const c5r::heightfield::HeightGrid* grid = m_bodyGrid;
	const double t = m_bodyT;
	const double b0 = srvrender::QpcToMs(srvrender::Qpc());   // issue 4468: the slow frames' timings
	const CullCamera cc = CullOf(cam);   // issue 4457 / 4456: one function, unitfeed calls it too
	m_core.build(&cc, grid, t, m_built);   // issue 4755: the frame id noted above is F9's clock
	filterByFog();   // issue 4463: other teams' units on plots the active team does not see
	const double b1 = srvrender::QpcToMs(srvrender::Qpc());
	m_animMs = 0;
	if(m_anim)
	{
		m_anim->setGrid(grid);   // issue 4724: the fights' start positions on this frame's height field
		animFrame(t, cam.frameId);
		if(m_failed)
		{
			m_lastFrameOk = false;
			return;
		}
	}
	const double b2 = srvrender::QpcToMs(srvrender::Qpc());
	for(int c = 0; c < 3; ++c)
	{
		m_plans[c].clear();
		m_planOk[c] = false;
		const cu::UnitScene& sc = m_built.copies[c];
		if(sc.units.empty())
			continue;
		const c5r::Status s = m_draw->prepare(sc, grid, m_plans[c]);
		m_prepares++;
		if(!s.ok())
		{
			m_core.frameFailed("prepare (wrap copy " + std::to_string(c - 1) + "): " + s.str());
			m_frameOk = false;
			continue;
		}
		m_core.afterPrepare(sc, m_plans[c]);
		ApplyUnitXforms(m_plans[c], m_built.xforms);   // issue 4981: the extras' turn and scale (nothing without an entry)
		m_planOk[c] = m_plans[c].drawn() != 0;
	}
	const double b3 = srvrender::QpcToMs(srvrender::Qpc());
	m_beginMs[0] = b1 - b0;
	m_beginMs[1] = b2 - b1;
	m_beginMs[2] = b3 - b2;
	m_beginMs[3] = b3 - b0;
	for(int i = 0; i < 4; ++i)
		m_beginSecSum[i] += m_beginMs[i];   // ticket 0707: the `units 1s` line's begin_ms means
	m_beginSecN++;
	m_animMsMax = (std::max)(m_animMsMax, b2 - b1);
	m_cam = cam;
	m_waterZ = grid->waterZ;
	m_frameBegun = true;
}

// Ticket 0711: after the join, on the render thread: the body's lines in their order (R6), then the job's numbers
void UnitsHost::beginJoined(const srvrender::FrameJobResult& r)
{
	m_owner.mayCall("UnitsHost::beginJoined");
	for(const std::string& l : r.lines)
		say(l);
	if(!r.ran)
		return;
	m_jobSecN++;
	m_jobsTotal++;
	m_jobSecMs += r.jobMs;
	m_jobSecWait += r.joinWaitMs;
	m_jobSecWaitMax = (std::max)(m_jobSecWaitMax, r.joinWaitMs);
	if(r.inlineRun)
	{
		m_jobSecInline++;
		m_jobsInlineTotal++;
	}
}

// Issue 4456 (decision 6): one controller frame per drawn frame of the scene, after build (the allocator's Advance, so the
// creates carry their seeds): the feed's events since the last frame and the step of the frame clock; the programs go to
// the drawing renderer through ControllerPose, the single-sample ones also into the frame's figure lines (the dump).
void UnitsHost::animFrame(double t, int64_t frameId)
{
	const bool first = !m_animFeed.started();
	if(first)
	{
		m_animFeed.start();
		m_animStartFrame = frameId;
		say("units: the controller starts at frame " + std::to_string(frameId) + " with the table's " + std::to_string(m_core.tableSize()) +
		    " units (render_units_anim=1)");
	}
	const double t0 = srvrender::QpcToMs(srvrender::Qpc());
	c5r::unitanim::TimelineFrame f;
	std::vector<c5r::units::PlayerInput> players;
	m_animFeed.close(m_core, t, frameId, f, players);
	const double t1 = srvrender::QpcToMs(srvrender::Qpc());
	// issue 4468 (unit-controller.md 18): the drawn set, every wrap copy's listed units after the cull and the fog filter;
	// only the drawn units take their clock and steps (issue 0450, unit-controller.md 18.1: the EXE does not advance a unit it does
	// not draw; a culled or fogged unit holds its clock) and only they get programs
	m_drawnSet.clear();
	for(const cu::UnitScene& sc : m_built.copies)
		for(const cu::UnitInput& u : sc.units)
			m_drawnSet.insert(std::make_pair(u.player, u.id));
	std::string why;
	if(!m_anim->apply(f, players, m_core, m_opt.animAll ? nullptr : &m_drawnSet, why))
	{
		fail("the unit animation controller refused frame " + std::to_string(frameId) + ": " + why, Status::kAnimUnavailable);
		return;
	}
	for(const std::string& l : m_anim->takeCityLines())   // ticket 5062: a city strike without its city's plot, not LOUD
		say(l);
	for(const std::string& l : m_anim->takeWalkLines())   // 0685 (diagnostic, C5_RENDER_UNITS_WALK_LOG=1): with the QPC time
	{
		char q[48];
		snprintf(q, sizeof(q), " | qpc_ms %.3f", srvrender::QpcToMs(srvrender::Qpc()));
		say(l + q);
	}
	const double t2 = srvrender::QpcToMs(srvrender::Qpc());
	const c5r::unitanim::Controller::ApplyCounts& ac = m_anim->controller().lastApply();
	m_animDrawn = ac.drawn;
	m_animQuiet = ac.quiet;
	m_animUnread += ac.unread_requests;
	m_animRevived += ac.revived;
	m_animMs = t2 - t0;
	if(first)
	{
		char line[400];
		snprintf(line, sizeof(line),
		         "units: the controller's first frame %lld: %zu units held (%zu events), %zu drawn, %zu not drawn; the feed's close %.1f ms, "
		         "the controller %.1f ms; clip files read on the render thread %zu",
		         (long long)frameId, m_animFeed.held(), f.events.size(), ac.drawn, ac.quiet, t1 - t0, t2 - t1, m_anim->clipLazyReads());
		say(line);
	}
	if(!m_opt.animFile.empty())
		m_animRecord.add(f, players);
	for(cu::UnitScene& sc : m_built.copies)
		m_anim->fillFigures(sc);
	m_anim->mergeWalkTurns(m_built);   // 0685: walking units turn over time (4981's turn, ApplyUnitXforms after prepare)
}

bool UnitsHost::drawPass(ID3D11DeviceContext* ctx, int c, cu::PassKind kind, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                         uint32_t w, uint32_t h, const float* viewProj)
{
	cu::PassCamera pc;
	if(!passCamera(m_cam, c - 1, m_waterZ, pc))
	{
		m_core.frameFailed("the frame's camera is not finite or its view not invertible");
		m_frameOk = false;
		return false;
	}
	if(viewProj)
	{
		// the reflection pass: ShadowMatrix1 moved by k W for wrap copy k (row 3 += k W row 0), as the scene pass's matrix
		const float s = (float)(c - 1) * m_cam.wrapPeriod;
		for(int r = 0; r < 4; ++r)
			pc.viewProj[r] = {viewProj[r * 4 + 0] + (r == 3 ? s * viewProj[0] : 0.0f), viewProj[r * 4 + 1] + (r == 3 ? s * viewProj[1] : 0.0f),
			                  viewProj[r * 4 + 2] + (r == 3 ? s * viewProj[2] : 0.0f), viewProj[r * 4 + 3] + (r == 3 ? s * viewProj[3] : 0.0f)};
	}
	if(m_opt.testOffset[0] != 0 || m_opt.testOffset[1] != 0)
	{
		// ticket 4785, TEST ONLY (render_test_units_offset): the units moved by (dx, dy): row 3 += dx row 0 + dy row 1 (row vectors)
		cu::Float4* v = pc.viewProj;
		const float dx = m_opt.testOffset[0], dy = m_opt.testOffset[1];
		v[3].x += dx * v[0].x + dy * v[1].x;
		v[3].y += dx * v[0].y + dy * v[1].y;
		v[3].z += dx * v[0].z + dy * v[1].z;
		v[3].w += dx * v[0].w + dy * v[1].w;
		if(!m_testOffsetSaid)
		{
			m_testOffsetSaid = true;
			char line[200];
			snprintf(line, sizeof(line), "LOUD: units: TEST KEY render_test_units_offset=%.1f,%.1f: every server unit is drawn moved", dx, dy);
			say(line);
		}
	}
	cu::PassSet ps;
	cu::PassDesc d;
	d.kind = kind;
	d.rtv = rtv;
	d.dsv = dsv;
	d.viewport.TopLeftX = 0;
	d.viewport.TopLeftY = 0;
	d.viewport.Width = (float)w;
	d.viewport.Height = (float)h;
	d.viewport.MinDepth = 0;
	d.viewport.MaxDepth = 1;
	d.cb1 = cu::makeCb1(pc);
	ps.passes.push_back(d);
	// the module binds VS / PS only; the terrain's hull and domain shaders may still be bound (the reflection pass runs
	// right after the terrain, and the scene pass after it when the water did not draw)
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	const c5r::Status s = m_draw->draw(ctx, m_plans[c], ps);
	if(!s.ok())
	{
		m_core.frameFailed(std::string(cu::passName(kind)) + " pass (wrap copy " + std::to_string(c - 1) + "): " + s.str());
		m_frameOk = false;
		return false;
	}
	return true;
}

void UnitsHost::drawReflection(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t size,
                               const float* shadowMatrix1)
{
	m_owner.mayCall("UnitsHost::drawReflection");
	if(!m_frameBegun)
		return;
	if(!shadowMatrix1)
	{
		m_core.note(Status::kReflectionMatrixMissing, "sm1",
		            "render_units_passes=1 but the frame has no ShadowMatrix1 (no reflection camera given): no reflection pass, "
		            "the water's t4 holds no units this frame");
		return;
	}
	for(int c = 0; c < 3; ++c)
		if(m_planOk[c] && drawPass(ctx, c, cu::PassKind::Reflection, rtv, dsv, size, size, shadowMatrix1))
			m_reflectionDraws++;
}

// Ticket 4785 (unit-combat-server.md 9 and 13): the mask pass with the tapped ShadowMatrix0 moved by k W per wrap copy (as
// the reflection pass); no depth view (the EXE's mask draws have none)
bool UnitsHost::drawMask(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h, const float* shadowMatrix0)
{
	m_owner.mayCall("UnitsHost::drawMask");
	if(!m_frameBegun || !shadowMatrix0 || !rtv)
		return false;
	bool all = true;
	for(int c = 0; c < 3; ++c)
		if(m_planOk[c])
		{
			if(drawPass(ctx, c, cu::PassKind::ShadowMask, rtv, nullptr, w, h, shadowMatrix0))
				m_maskDraws++;
			else
				all = false;
		}
	return all;
}

void UnitsHost::drawScene(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
                          const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid)
{
	m_owner.mayCall("UnitsHost::drawScene");
	if(!m_frameBegun)
		beginFrame(cam, grid);
	if(!m_frameBegun)
		return;
	m_frameBegun = false;   // the frame's plans are used once
	const double d0 = srvrender::QpcToMs(srvrender::Qpc());
	for(int c = 0; c < 3; ++c)
		if(m_planOk[c] && drawPass(ctx, c, cu::PassKind::Scene, rtv, dsv, w, h, nullptr))
			m_passDraws++;
	m_lastFrameOk = m_frameOk;
	m_drawnFrames++;
	// issue 4468: a unit frame (beginFrame + the scene pass) over 250 ms is logged with its parts (the first 20, then counted)
	const double drawMs = srvrender::QpcToMs(srvrender::Qpc()) - d0;
	const double frameMs = m_beginMs[3] + drawMs;
	m_frameMsMax = (std::max)(m_frameMsMax, frameMs);
	if(frameMs > 250.0 && ++m_slowFrames <= 20)
	{
		size_t listed = 0;
		for(const cu::UnitScene& sc : m_built.copies)
			listed += sc.units.size();
		char line[400];
		snprintf(line, sizeof(line),
		         "units: slow frame %lld (drawn frame %lld): %.1f ms = build and fog %.1f + the controller %.1f (%zu drawn, %zu not drawn) + "
		         "prepare %.1f (%zu listed, %zu art rows cached) + the scene pass %.1f%s",
		         (long long)m_cam.frameId, (long long)m_drawnFrames, frameMs, m_beginMs[0], m_beginMs[1], m_animDrawn, m_animQuiet, m_beginMs[2],
		         listed, m_draw ? m_draw->cachedRows() : (size_t)0, drawMs, m_slowFrames == 20 ? " (the last such line; counted from now)" : "");
		say(line);
	}
	dump(m_cam.frameId, m_built.copies[1]);
}

// server.exe's factory (HostStart with render_units=1, before the render thread's first drain): the scene, then the DLL
// side's unit writers on (decision 8; SrvFeed::SetUnitsEnabled, srv_feed.h), so that no unit event of the process is lost
std::unique_ptr<SceneUnits> MakeLive(const UnitsOptions& o)
{
	// issue 4755 (lead-0040's answer to queue #4007): the position source is the value ParseHostConfig read for
	// render_units_positions, the one SceneOpts also copies into SceneOptions::unitsPositions for the mode word's writer, so
	// the scene and the writer always agree (srv_render_host.cpp is not this ticket's file)
	UnitsOptions lo = o;
	lo.positionsExe = o.positionsExe || srvrender::RenderUnitsPositionsOption() == srvrender::SceneOptions::kUnitsPositionsExe;
	lo.openAsync = true;   // ticket 0704: the open and the first rows off the render thread
	{
		// ticket 0704, TEST ONLY: C5_RENDER_UNITS_OPEN_SYNC=1 opens on the render thread as before (the before arm of one build)
		char v[8] = {};
		if(GetEnvironmentVariableA("C5_RENDER_UNITS_OPEN_SYNC", v, sizeof(v)) && v[0] == '1')
		{
			lo.openAsync = false;
			srvrender::RNote("LOUD: units: TEST ENV C5_RENDER_UNITS_OPEN_SYNC=1: the drawing renderer opens on the render thread "
			                 "(ticket 0704's before arm)");
		}
	}
	std::unique_ptr<SceneUnits> u =
	    MakeSceneUnitsWith(lo, LiveFeedPort(), LiveEnvOpener(lo), [](const char* line) { srvrender::RNote("%s", line); }, ClockFn());
	if(lo.positionsExe)
		srvrender::RNote("units: position source exe (render_units_positions=exe): each unit's root at its newest EXE position record, "
		                 "move / arrive timed by the EXE's records (issue 4755)");
	// issue 4981: the unit extras (null without server/src/extras/units/: the scene as before); units are ours with ServerUnits = 1
	if(UnitsHostProbe* probe = dynamic_cast<UnitsHostProbe*>(u.get()))
		probe->attachExtras(MakeUnitsExtras(), SrvOptions(), SrvOptions().ServerUnits() == 1);
	SrvFeed::SetUnitsEnabled(true);
	return u;
}

struct Registrar
{
	Registrar() { RegisterSceneUnitsFactory(&MakeLive); }
} g_registrar;

}  // namespace

std::unique_ptr<SceneUnits> MakeSceneUnitsLive(const UnitsOptions& o) { return MakeLive(o); }   // issue 4755

EnvOpener LiveEnvOpener(const UnitsOptions& o)
{
	UnitsOptions opt = o;
	return [opt](const std::wstring& dbPath, Env& out, std::string& why) {
		c5r::gamefs::GameFiles::Options go;
		go.extraLooseRoots = opt.modsAuto ? srvrender::ModFolders() : opt.mods;
		const c5r::Status s = out.gf.open(opt.install, go);
		if(!s.ok())
		{
			why = "the install " + Utf8(opt.install) + ": " + s.str();
			return false;
		}
		if(!c5m::SqliteTables::available())
		{
			why = "winsqlite3.dll is not available";
			return false;
		}
		std::unique_ptr<c5m::SqliteTables> t(new c5m::SqliteTables());
		c5m::Error e;
		if(!t->open(Utf8(dbPath), &e))
		{
			why = "the database " + Utf8(dbPath) + ": " + e.what;
			return false;
		}
		out.db = std::move(t);
		out.exe = opt.exe;
		out.install = opt.install;
		return true;
	};
}

std::unique_ptr<SceneUnits> MakeSceneUnitsWith(const UnitsOptions& o, FeedPort& port, EnvOpener env, LogFn log, ClockFn clock)
{
	return std::unique_ptr<SceneUnits>(new UnitsHost(o, port, std::move(env), std::move(log), std::move(clock)));
}

}  // namespace srvunits
