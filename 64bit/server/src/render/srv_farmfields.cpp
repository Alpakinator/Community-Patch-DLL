// srv_farmfields.cpp - the server's own farm fields and farm tracks (epic 0528, ticket 4851). See srv_farmfields.h.
// Written 2026-11-18 for this repository (GPL-3.0-or-later).
#include "srv_farmfields.h"
#include "srv_residency_source.h"   // ParseFarmTypes (farm_types.xml's attributes)
#include "vp_farm_points.h"         // ticket 0657: the buildings' points for the EXE's placer (shell/common)
#include "c5r/gamefs/gamefiles.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace srvrender {

namespace cff = c5r::farmfields;

bool IsFarmImprovementName(const char* s)
{
	return s && (strcmp(s, kFarmImprovementType) == 0 || strcmp(s, kFarmImprovementArtTag) == 0);
}

bool FarmTypesFromXml(const std::string& xml, cff::FarmTypes& out, std::string& why)
{
	std::map<std::string, std::string> farm;
	if(!ParseFarmTypes(xml, farm, why))
		return false;
	std::vector<std::pair<std::string, std::string>> attrs(farm.begin(), farm.end());
	out = cff::farmTypesFromAttributes(attrs);
	return true;
}

bool FarmPlotsFromTable(const std::vector<SrvFeed::PlotRecord>& rec, uint32_t width, uint32_t height, int farmId, bool revealedOnly,
                        std::vector<cff::FarmPlot>& out, int wheatResourceId)
{
	out.clear();
	if(!width || !height || rec.size() != (size_t)width * height)
		return false;
	// which rule the table allows (the header): the revealed layout, else the fog's revealed bit, else every plot
	bool anyLayout = false, anyFow = false;
	if(revealedOnly)
		for(const SrvFeed::PlotRecord& r : rec)
		{
			anyLayout = anyLayout || (r.known & 2);
			anyFow = anyFow || (r.fow & 1);
		}
	for(uint32_t y = 0; y < height; ++y)
		for(uint32_t x = 0; x < width; ++x)
		{
			const SrvFeed::PlotRecord& r = rec[(size_t)y * width + x];
			if(revealedOnly && (anyLayout ? !(r.known & 2) : anyFow ? !(r.fow & 1) : false))
				continue;   // not laid out for the active team (its improvement there is not the revealed one)
			cff::FarmPlot p;
			if(cff::farmPlotOf((int32_t)x, (int32_t)y, farmId, r.improvement, r.improvementState, r.flags, r.fow, r.artType, false, p,
			                   r.resource, wheatResourceId))
				out.push_back(p);
		}
	return true;
}

uint32_t StaticPlotSeed(const std::vector<SrvFeed::PlotRecord>& rec)
{
	uint32_t h = 2166136261u;
	auto add = [&](uint8_t b) {
		h ^= b;
		h *= 16777619u;
	};
	for(const SrvFeed::PlotRecord& r : rec)
	{
		add((uint8_t)r.terrain);
		add((uint8_t)r.plotType);
		add((uint8_t)r.feature);
		add((uint8_t)(r.flags & 8));
	}
	return h ? h : 1u;
}

FarmFieldsProducer::FarmFieldsProducer(std::unique_ptr<FowPlotTable> table) : m_table(std::move(table)) {}

void FarmFieldsProducer::setOptions(const FarmFieldsOptions& o)
{
	const bool reveal = o.revealedOnly != m_opt.revealedOnly;
	m_opt = o;
	if(reveal && m_haveTable)
		m_dirty = farmsFromRecords() || m_dirty;
}

void FarmFieldsProducer::setFarmImprovementId(int id)
{
	if(id == m_farmId)
		return;
	RNote("farmfields: the farm improvement's id is %d (was %d)", id, m_farmId);
	m_farmId = id;
	if(m_haveTable)
		m_dirty = farmsFromRecords() || m_dirty;
}

void FarmFieldsProducer::setWheatResourceId(int id)
{
	if(id == m_wheatId)
		return;
	RNote("farmfields: the wheat resource's id is %d (was %d)", id, m_wheatId);
	m_wheatId = id;
	if(m_haveTable)
		m_dirty = farmsFromRecords() || m_dirty;
}

void FarmFieldsProducer::setFarmTypes(const cff::FarmTypes& t)
{
	m_in.types = t;
	m_haveTypes = true;
	m_dirty = true;
}

void FarmFieldsProducer::setGameSeed(uint32_t seed)
{
	if(seed == m_mapSeed)
		return;
	m_mapSeed = seed;
	m_dirty = true;
}

void FarmFieldsProducer::setWorld(const float worldRect[4], uint32_t loadSeq)
{
	if(!worldRect)
		return;
	const bool newLoad = !m_haveWorld || loadSeq != m_loadSeq;
	if(newLoad)
	{
		loads++;
		m_prod.newLoad();
		m_loadSeq = loadSeq;
		m_haveStaticSeed = false;   // taken again from this load's first table
		// ticket 0653: a new load starts from an empty building list (its own generation), filled only by this load's
		// productions; the owners' eras are read again with the load's first table
		for(int i = 0; i < 64; ++i)
			m_facts.eraOfPlayer[i] = -1;
		m_facts.owner.clear();
		newBuildingList(std::vector<cff::FarmBuilding>());
		m_buildingsDue = false;
		publishPoints();   // ticket 0657: PRODUCING for the new load (the guard holds its farm ends until DONE)
		if(m_footprint)
		{   // ticket 4853: the last load's footprint goes (the forest builds the new load's trees anyway)
			m_footprint.reset();
			m_footprintVersion++;
		}
	}
	if(!m_frozen)   // a restored input keeps its recorded world
		memcpy(m_in.worldRect, worldRect, sizeof(m_in.worldRect));
	m_haveWorld = true;
	m_dirty = true;
	if(newLoad)
		readTable("a new load");
}

void FarmFieldsProducer::setCutFrame(float ox, float oy)
{
	m_cutOx = ox;
	m_cutOy = oy;
}

void FarmFieldsProducer::BuildFootprint(Result& r, float ox, float oy)
{
	if(!r.st.ok())
		return;
	const int64_t q0 = Qpc();
	std::shared_ptr<c5r::farmfields::FieldFootprint> fp(new c5r::farmfields::FieldFootprint());
	fp->setFrame(ox, oy);
	r.footprintSt = fp->build(r.layout, r.in.worldRect);
	r.footprintMs = QpcToMs(Qpc() - q0);
	if(r.footprintSt.ok())
		r.footprint = std::move(fp);
}

bool FarmFieldsProducer::readTable(const char* why)
{
	if(m_frozen)
		return false;   // the player: the recorded farms stand
	if(!m_table)
		m_table = MakeFeedPlotTable();
	if(!m_table)
		return false;
	rereads++;
	m_table->copy(m_rec, m_tableGen, m_w, m_h);
	m_haveTable = m_w && m_h && m_rec.size() == (size_t)m_w * m_h;
	if(!m_haveTable && !m_rec.empty())
	{
		tableErrors++;
		if(loudDue(tableErrors))
			RNote("LOUD: farmfields: the plot table holds %zu records for %ux%u (%s; %lld so far): no farms until the next read",
			      m_rec.size(), m_w, m_h, why, (long long)tableErrors);
	}
	if(m_haveTable && m_firstTableMs < 0)
		m_firstTableMs = QpcToMs(Qpc());
	if(m_haveTable && !m_haveStaticSeed)
	{
		m_staticSeed = StaticPlotSeed(m_rec);
		m_haveStaticSeed = true;
		m_dirty = true;
	}
	const bool changed = farmsFromRecords();
	m_dirty = changed || m_dirty;
	// ticket 0653: the owners may have changed (a plot's owner is not in the farm list); the players' eras with the table
	if(m_opt.useFeedPlayers)
		readPlayers();
	m_buildingsDue = true;
	RLog("farmfields: plot table read (%s): %ux%u generation %u, %zu farm plots%s", why, m_w, m_h, m_tableGen, m_in.plots.size(),
	     changed ? " (changed)" : "");
	return changed;
}

bool FarmFieldsProducer::farmsFromRecords()
{
	if(m_frozen)
		return false;   // the recorded farms stand
	std::vector<cff::FarmPlot> now;
	if(m_haveTable)
		FarmPlotsFromTable(m_rec, m_w, m_h, m_farmId, m_opt.revealedOnly, now, m_wheatId);
	m_in.mapW = m_w;
	m_in.mapH = m_h;
	bool same = now.size() == m_in.plots.size();
	for(size_t i = 0; same && i < now.size(); ++i)
	{
		const cff::FarmPlot& a = now[i];
		const cff::FarmPlot& b = m_in.plots[i];
		same = a.x == b.x && a.y == b.y && a.state == b.state && a.artType == b.artType && a.wheat == b.wheat;
	}
	if(same)
		return false;
	m_in.plots.swap(now);
	return true;
}

bool FarmFieldsProducer::onBatch(const SrvFeed::Batch& b)
{
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
	}
	if(b.dropped > m_lastDropped)
	{
		if(!reread)
			overflows++;
		reread = true;
		why = "dropped events";
	}
	m_lastDropped = b.dropped;
	if(m_opt.useFeedPlayers && !m_frozen)
		for(const SrvFeed::PlayerRecord& r : b.players)   // ticket 0653: an owner's era for the farm buildings
			setOwnerEra(r.player, r.era);
	if(reread)
		return readTable(why);
	if(!b.changedPlots.empty() && m_haveTable)
	{   // a patch: the table again (farm groups reach over neighbours, so the whole list of farm plots is compared)
		plotChanges++;
		return readTable("changed plots");
	}
	return false;
}

bool FarmFieldsProducer::refresh()
{
	if(m_opt.useFeedIds)
	{   // the ids the feed's DLL side published by art define (srv_farm_ids.h)
		const FarmIds ids = FarmIdsNow();
		if(ids.generation != m_idsGen && ids.resolved)
		{
			m_idsGen = ids.generation;
			setFarmImprovementId(ids.farm);
			setWheatResourceId(ids.wheat);
		}
	}
	if(m_farmId < 0 && !idsMissing && m_firstTableMs >= 0 && QpcToMs(Qpc()) - m_firstTableMs > m_opt.idsWaitMs)
	{   // D118's spirit: never a silent world without fields (the tap's set stays in use, composeDecals)
		idsMissing = true;
		RNote("LOUD: farmfields: render_farmfields=1 but no farm improvement id (ART_DEF_IMPROVEMENT_FARM) arrived within %.0f ms of the "
		      "first plot table (srv_farm_ids.h, filled by the feed's DLL side at each snapshot): the server draws no farm fields of its "
		      "own; the decal tap's set is used as it is",
		      m_opt.idsWaitMs);
	}
	{   // farm_types.xml read on the worker
		std::unique_ptr<TypesResult> tr;
		{
			std::lock_guard<std::mutex> lk(m_mx);
			tr = std::move(m_typesRes);
		}
		if(tr && tr->ok)
		{
			RNote("farmfields: farm_types.xml from %s", tr->why.c_str());
			setFarmTypes(tr->types);
		}
		else if(tr)
		{
			m_typesFailed = true;
			RNote("LOUD: farmfields: %s; the server draws no farm fields of its own (the decal tap's set is used as it is)", tr->why.c_str());
		}
	}
	bool changed = takeResult();
	if(m_buildingsDue)
		rebuildBuildings();   // ticket 0653: in the same step as the fields put in force
	if(!m_dirty || !m_haveWorld || !m_haveTypes || !m_haveTable)
		return changed;
	if(m_farmId < 0 && m_in.plots.empty() && m_prod.decals().empty())
	{
		m_dirty = false;
		return changed;   // no farm known yet and none in force
	}
	m_dirty = false;
	if(!m_frozen)
		m_in.gameSeed = m_mapSeed ? m_mapSeed : m_staticSeed;   // farm-fields-layout.md 13.1: the map seed, else the plots' own
	if(m_in.gameSeed != m_seedLogged || m_loadSeq != m_seedLoggedLoad)
	{   // ticket 4853 (acceptance item 7): the seed in use, once per load and on every change
		m_seedLogged = m_in.gameSeed;
		m_seedLoggedLoad = m_loadSeq;
		RNote("farmfields: game seed 0x%08X for load %u (the field's mapSeed 0x%08X, the plots' static seed 0x%08X%s)%s",
		      m_in.gameSeed, m_loadSeq, m_mapSeed, m_staticSeed, m_haveStaticSeed ? "" : ", not yet taken",
		      m_frozen ? " (a restored span input)" : m_mapSeed ? "" : ": mapSeed is 0, the static seed is used");
	}
	if(m_opt.threaded)
	{
		startWorker();
		std::lock_guard<std::mutex> lk(m_mx);
		m_job.reset(new Job());   // a newer input replaces a queued one
		m_job->in = m_in;
		m_job->loadSeq = m_loadSeq;
		m_job->threads = m_opt.layoutThreads;
		m_job->cutOx = m_cutOx;
		m_job->cutOy = m_cutOy;
		m_cv.notify_all();
		return changed;
	}
	// not threaded: here, with the same cache
	if(m_cacheLoad != m_loadSeq)
	{
		m_cache = c5r::farmfields::BlockCache();
		m_cacheLoad = m_loadSeq;
	}
	m_cache.threads = m_opt.layoutThreads;
	std::unique_ptr<Result> r(new Result());
	const uint64_t miss0 = m_cache.misses;
	const int64_t q0 = Qpc();
	r->st = c5r::farmfields::produce(m_in, r->layout, r->decals, nullptr, &m_cache);
	r->ms = QpcToMs(Qpc() - q0);
	r->misses = m_cache.misses - miss0;
	r->in = m_in;
	r->loadSeq = m_loadSeq;
	BuildFootprint(*r, m_cutOx, m_cutOy);   // ticket 4853
	{
		std::lock_guard<std::mutex> lk(m_mx);
		m_result = std::move(r);
	}
	const bool took = takeResult();
	if(m_buildingsDue)
		rebuildBuildings();   // ticket 0653: the production made here, its records in the same step
	return took || changed;
}

bool FarmFieldsProducer::spanState(std::string& text, std::string& why) const
{
	text.clear();
	if(!m_prod.haveInput())
		return why = "no production in force at the span's first frame (no farm id, no farm_types.xml or no table yet)", false;
	text = cff::spanText(m_prod.input(), m_farmId, m_wheatId);
	return true;
}

bool FarmFieldsProducer::restoreSpanInput(const std::string& text, std::string& why)
{
	cff::LayoutInput in;
	int farm = -1, wheat = -1;
	const c5r::Status st = cff::parseSpanText(text, in, farm, wheat);
	if(!st.ok())
		return why = st.str(), false;
	const cff::FarmTypes types = m_in.types;   // farm_types.xml stays the install's
	m_in = in;
	m_in.types = types;
	m_frozen = true;
	m_opt.useFeedIds = false;
	m_farmId = farm;
	m_wheatId = wheat;
	m_w = in.mapW;
	m_h = in.mapH;
	m_haveTable = true;
	m_dirty = true;
	RNote("farmfields: the recorded input restored: %zu farms, seed %u, ids %d / %d", m_in.plots.size(), m_in.gameSeed, farm, wheat);
	return true;
}

FarmFieldsProducer::TypesResult FarmFieldsProducer::ReadFarmTypes(const TypesSource& s)
{
	TypesResult r;
	c5r::gamefs::GameFiles fs;
	c5r::gamefs::GameFiles::Options fo;
	fo.extraLooseRoots = s.roots;
	c5r::Status st = fs.open(s.install, fo);
	std::vector<uint8_t> bytes;
	std::string origin;
	if(st.ok())
		st = fs.read("farm_types.xml", bytes, &origin);
	if(!st.ok())
	{
		r.why = "farm_types.xml cannot be read from the install or the mods' folders: " + st.str();
		return r;
	}
	if(!FarmTypesFromXml(std::string(bytes.begin(), bytes.end()), r.types, r.why))
	{
		r.why = "farm_types.xml (" + origin + ") does not parse: " + r.why;
		return r;
	}
	r.ok = true;
	r.why = origin;
	return r;
}

void FarmFieldsProducer::loadFarmTypesFrom(const std::wstring& install, const std::vector<std::wstring>& looseRoots)
{
	std::unique_ptr<TypesSource> src(new TypesSource());
	src->install = install;
	src->roots = looseRoots;
	if(!m_opt.threaded)
	{
		std::unique_ptr<TypesResult> r(new TypesResult(ReadFarmTypes(*src)));
		std::lock_guard<std::mutex> lk(m_mx);
		m_typesRes = std::move(r);
		return;
	}
	startWorker();
	std::lock_guard<std::mutex> lk(m_mx);
	m_typesSrc = std::move(src);
	m_cv.notify_all();
}

void FarmFieldsProducer::startWorker()
{
	if(m_running)
		return;
	m_running = true;
	m_worker = std::thread([this]() { workerLoop(); });
}

void FarmFieldsProducer::workerLoop()
{
	for(;;)
	{
		std::unique_ptr<Job> job;
		std::unique_ptr<TypesSource> types;
		{
			std::unique_lock<std::mutex> lk(m_mx);
			m_cv.wait(lk, [this]() { return m_stop || m_job || m_typesSrc; });
			if(m_stop)
				return;
			types = std::move(m_typesSrc);
			if(!types)
				job = std::move(m_job);
			m_working = true;
		}
		if(types)
		{
			std::unique_ptr<TypesResult> r(new TypesResult(ReadFarmTypes(*types)));
			{
				std::lock_guard<std::mutex> lk(m_mx);
				m_typesRes = std::move(r);
				m_working = false;
			}
			m_cv.notify_all();
			continue;
		}
		if(m_cacheLoad != job->loadSeq)
		{   // a new load: its own block graphs (the cache is the worker's alone)
			m_cache = c5r::farmfields::BlockCache();
			m_cacheLoad = job->loadSeq;
		}
		m_cache.threads = job->threads;
		std::unique_ptr<Result> r(new Result());
		const uint64_t miss0 = m_cache.misses;
		const int64_t q0 = Qpc();
		r->st = c5r::farmfields::produce(job->in, r->layout, r->decals, nullptr, &m_cache);
		r->ms = QpcToMs(Qpc() - q0);
		r->misses = m_cache.misses - miss0;
		r->in = std::move(job->in);
		r->loadSeq = job->loadSeq;
		BuildFootprint(*r, job->cutOx, job->cutOy);   // ticket 4853
		{
			std::lock_guard<std::mutex> lk(m_mx);
			if(m_result)
				staleResults++;   // an unread result replaced by a newer one
			m_result = std::move(r);
			m_working = false;
		}
		m_cv.notify_all();
	}
}

bool FarmFieldsProducer::takeResult()
{
	std::unique_ptr<Result> r;
	{
		std::lock_guard<std::mutex> lk(m_mx);
		r = std::move(m_result);
	}
	if(!r)
		return false;
	if(r->loadSeq != m_loadSeq)
	{
		staleResults++;   // made for an earlier load
		return false;
	}
	lastProduceMs = r->ms;
	lastMisses = r->misses;
	if(!r->st.ok())
	{
		productionErrors++;
		m_prod.countFailure();
		const std::string e = r->st.str();
		if(loudDue(productionErrors) || e != m_lastErr)
			RNote("LOUD: farmfields: %zu farm plots not laid out (%s); the %zu farm decals in force are kept (%lld so far)", r->in.plots.size(),
			      e.c_str(), m_prod.decals().decals.size(), (long long)productionErrors);
		m_lastErr = e;
		return false;
	}
	bool changed = false;
	const size_t n = r->in.plots.size();
	const c5r::Status st = m_prod.accept(r->in, std::move(r->layout), std::move(r->decals), changed);
	if(!st.ok())
	{
		productionErrors++;
		RNote("LOUD: farmfields: a production not put in force (%s)", st.str().c_str());
		return false;
	}
	m_buildingsDue = true;   // ticket 0653: the buildings follow every production put in force
	// ticket 4853: the fields' footprints for the trees, replaced only when their cells changed
	lastFootprintMs = r->footprintMs;
	if(!r->footprint)
	{
		footprintErrors++;
		if(loudDue(footprintErrors))
			RNote("LOUD: farmfields: the fields' footprint (the trees' cut under fields) was not built (%s); the one in force is kept "
			      "(%lld so far)",
			      r->footprintSt.str().c_str(), (long long)footprintErrors);
	}
	else if(!m_footprint || m_footprint->hash() != r->footprint->hash())
	{
		r->footprint->setGeneration(++m_footprintVersion);
		footprints++;
		RLog("farmfields: footprint %llu: %llu cells of 2 x 2 samples under %zu fields (%u x %u cells, %.2f ms on the %s)",
		     (unsigned long long)m_footprintVersion, (unsigned long long)r->footprint->cellsSet(), m_prod.layout().fields.size(),
		     r->footprint->width(), r->footprint->height(), r->footprintMs, m_opt.threaded ? "worker" : "render thread");
		m_footprint = std::move(r->footprint);
	}
	if(changed)
	{
		productions++;
		RLog("farmfields: %zu farm plots -> %zu groups, %zu fields, %zu tracks (%zu decals, %zu vertices), %.2f ms on the %s, %llu block "
		     "graphs laid out",
		     n, m_prod.layout().groups.size(), m_prod.layout().fields.size(), m_prod.layout().tracks.size(), m_prod.decals().decals.size(),
		     m_prod.decals().vertices.size(), lastProduceMs, m_opt.threaded ? "worker" : "render thread", (unsigned long long)lastMisses);
	}
	return changed;
}

bool FarmFieldsProducer::busy() const
{
	std::lock_guard<std::mutex> lk(m_mx);
	return m_job != nullptr || m_result != nullptr || m_working || m_typesSrc != nullptr || m_typesRes != nullptr;
}

bool FarmFieldsProducer::settle(double ms)
{
	const int64_t q0 = Qpc();
	for(;;)
	{
		{
			std::unique_lock<std::mutex> lk(m_mx);
			if(!m_job && !m_typesSrc && !m_working && (m_result || m_typesRes || !m_dirty))
				break;
			m_cv.wait_for(lk, std::chrono::milliseconds(20));
		}
		if(QpcToMs(Qpc() - q0) > ms)
			return false;
	}
	refresh();
	return true;
}

FarmFieldsProducer::~FarmFieldsProducer()
{
	{
		std::lock_guard<std::mutex> lk(m_mx);
		m_stop = true;
	}
	m_cv.notify_all();
	if(m_worker.joinable())
		m_worker.join();
	if(m_pointsOwned)
		UnmapViewOfFile(m_points);   // ticket 0657 (the section's contents stay for a reader until its last handle closes)
	if(m_pointMap)
		CloseHandle(m_pointMap);
}

std::shared_ptr<const c5r::decal::DecalSet> FarmFieldsProducer::composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap)
{
	composes++;
	if(!tap)
		return tap;
	if(m_farmId < 0)
	{   // no farm id published yet: the tap's set as it is (its farm records, if the EXE's builder ran, stay drawn) rather
		// than a picture without farm fields; 0528 C decides the failure route when the id never comes (the forest's idsWaitMs)
		composesWithoutId++;
		return tap;
	}
	std::shared_ptr<const c5r::decal::DecalSet> out;
	cff::ComposeStats cs;
	const c5r::Status st = m_prod.compose(tap, m_opt.removeTapFarm, out, &cs);
	if(st.code == c5r::Err::Unsupported)
	{   // the EXE's farm builder ran and the tap's farm records are to be kept: the tap's set as it is (not fatal)
		tapRefusals++;
		if(m_refusedTapGen != tap->generation)
			RNote("LOUD: farmfields: the decal tap's set (generation %u) holds farm records and removeTapFarm is off: the server's are not "
			      "added (%lld sets so far)",
			      tap->generation, (long long)tapRefusals);
		m_refusedTapGen = tap->generation;
		return tap;
	}
	if(!st.ok())
	{
		composeErrors++;
		const std::string e = st.str();
		if(loudDue(composeErrors) || e != m_lastErr)
			RNote("LOUD: farmfields: the farm decals were not joined to the tap's set (generation %u): %s; the tap's set as it is (%lld so far)",
			      tap->generation, e.c_str(), (long long)composeErrors);
		m_lastErr = e;
		return tap;
	}
	if(cs.tapDecals || cs.farmDecals)
		RLog("farmfields: tap set generation %u: %u decals, %u farm records dropped, %u farm decals added (%u vertices), walk %d, area "
		     "bits +%u",
		     tap->generation, cs.tapDecals, cs.tapRemoved, cs.farmDecals, cs.farmVertices, cs.walk ? 1 : 0, cs.areaBitsSet);
	return out;
}

bool FarmFieldsProducer::takeDecalChanges(std::vector<DecalChange>& out)
{
	cff::FarmChange ch;
	if(!m_prod.takeChange(ch))
		return false;
	DecalChange c;
	c.generation = kFarmDecalGenBase + (++m_gen);
	c.previousGeneration = c.generation - 1;
	memcpy(c.worldRect, m_in.worldRect, sizeof(c.worldRect));
	c.rects = std::move(ch.rects);
	c.areaTiles = std::move(ch.areaTiles);
	out.push_back(std::move(c));
	decalChangesMade++;
	return true;
}

// ---- ticket 0653: the farm buildings ----

void FarmFieldsProducer::setFarmRows(const cff::FarmRowChoice& rows)
{
	if(m_haveRows && rows == m_rows)
		return;
	for(const cff::FarmRowRefusal& r : rows.refused)
	{   // D118: one LOUD line per row that cannot be drawn; never a stand-in
		rowRefusalLines++;
		RNote("LOUD: farmfields: farm building %s: not drawn", r.text.c_str());
	}
	if(!rows.any())
	{
		rowRefusalLines++;
		RNote("LOUD: farmfields: no FARM row of ArtDefine_Landmarks can be drawn (%u rows with LayoutHandler FARM, %zu refused): the "
		      "farm buildings' records carry no row and no farm building is drawn",
		      rows.farmHandlerRows, rows.refused.size());
	}
	else
		RNote("farmfields: farm building rows per era level 0-4: %zu %zu %zu %zu %zu (%u FARM rows, %zu refused)", rows.level[0].size(),
		      rows.level[1].size(), rows.level[2].size(), rows.level[3].size(), rows.level[4].size(), rows.farmHandlerRows,
		      rows.refused.size());
	m_rows = rows;
	m_haveRows = true;
	m_buildingsDue = true;
}

void FarmFieldsProducer::setHeightField(std::shared_ptr<const c5r::heightfield::HeightGrid> g)
{
	if(g == m_height)
		return;
	m_height = std::move(g);
	m_buildingsDue = true;
}

void FarmFieldsProducer::setOwnerEra(int player, int era)
{
	if(player < 0 || player >= 64 || m_facts.eraOfPlayer[player] == era)
		return;
	m_facts.eraOfPlayer[player] = era;
	m_buildingsDue = true;
}

void FarmFieldsProducer::readPlayers()
{
	if(m_frozen)
		return;
	std::vector<SrvFeed::PlayerRecord> rows;
	SrvFeed::CopyPlayers(rows);
	for(const SrvFeed::PlayerRecord& r : rows)
		setOwnerEra(r.player, r.era);
}

void FarmFieldsProducer::newBuildingList(std::vector<cff::FarmBuilding>&& b)
{
	std::shared_ptr<FarmBuildingList> l(new FarmBuildingList());
	l->generation = ++m_buildingsGen;
	l->loadSeq = m_loadSeq;
	l->fieldsVersion = m_prod.version();
	l->buildings = std::move(b);
	for(const cff::FarmBuilding& r : l->buildings)
	{
		l->pillaged += r.pillaged ? 1 : 0;
		l->withoutField += r.field < 0 ? 1 : 0;
		l->withoutRow += r.row < 0 ? 1 : 0;
		l->withHeight += r.hasHeight ? 1 : 0;
	}
	buildingLists++;
	RLog("farmfields: farm buildings generation %llu (load %u): %zu records, %zu pillaged, %zu without a field outline (at the plot "
	     "centre), %zu without a row, %zu with a height",
	     (unsigned long long)l->generation, l->loadSeq, l->buildings.size(), l->pillaged, l->withoutField, l->withoutRow, l->withHeight);
	m_buildings = std::move(l);
}

void FarmFieldsProducer::rebuildBuildings()
{
	m_buildingsDue = false;
	std::vector<cff::FarmBuilding> b;
	if(m_prod.haveInput())
	{
		const cff::LayoutInput& in = m_prod.input();
		m_facts.owner.clear();
		if(m_haveTable && !m_frozen && m_w == in.mapW && m_h == in.mapH && m_rec.size() == (size_t)m_w * m_h)
		{   // the owners from the plot table now (a restored span input has none: every farm then takes era level 1)
			m_facts.owner.resize(in.plots.size());
			for(size_t i = 0; i < in.plots.size(); ++i)
				m_facts.owner[i] = m_rec[(size_t)in.plots[i].y * m_w + (size_t)in.plots[i].x].owner;
		}
		const c5r::Status st = cff::placeBuildings(in, m_prod.layout(), m_facts, m_rows, m_height.get(), b);
		if(!st.ok())
		{
			productionErrors++;
			RNote("LOUD: farmfields: the farm buildings were not placed (%s); the list in force (generation %llu) is kept",
			      st.str().c_str(), (unsigned long long)m_buildings->generation);
			return;
		}
	}
	if(!(m_buildings->loadSeq == m_loadSeq && m_buildings->buildings == b))   // else nothing changed: the generation stays
		newBuildingList(std::move(b));
	publishPoints();   // ticket 0657: DONE once a production of this load is in force (also when the list did not change)
}

// ---- ticket 0657: the points' section (vp_farm_points.h) ----

void FarmFieldsProducer::setPointSection(const std::wstring& base)
{
	if(m_pointsOwned)
	{
		UnmapViewOfFile(m_points);
		m_points = nullptr;
		m_pointsOwned = false;
	}
	if(m_pointMap)
	{
		CloseHandle(m_pointMap);
		m_pointMap = nullptr;
	}
	if(base.empty())
	{
		m_points = nullptr;
		return;
	}
	const std::wstring name = base + VPFARM_SECTION_SUFFIX;
	HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPFARM_SECTION_BYTES, name.c_str());
	if(!h)
	{
		RNote("LOUD: farmfields: CreateFileMappingW('%ls') failed %lu: the EXE's farm buildings stand at their plot centres "
		      "(ticket 0657's points are not published)",
		      name.c_str(), GetLastError());
		return;
	}
	void* v = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, VPFARM_SECTION_BYTES);
	if(!v)
	{
		RNote("LOUD: farmfields: MapViewOfFile('%ls') failed %lu: the EXE's farm buildings stand at their plot centres", name.c_str(),
		      GetLastError());
		CloseHandle(h);
		return;
	}
	m_pointMap = h;
	m_points = v;
	m_pointsOwned = true;
	m_pointLastList = 0;
	m_pointLastCount = 0xFFFFFFFFu;
	RNote("farmfields: the farm buildings' points for the EXE's placer are published in '%ls' (ticket 0657, %u bytes)", name.c_str(),
	      (unsigned)VPFARM_SECTION_BYTES);
	publishPoints();
}

void FarmFieldsProducer::attachPointMemory(void* block)
{
	setPointSection(std::wstring());
	m_points = block;
	m_pointLastList = 0;
	m_pointLastCount = 0xFFFFFFFFu;
	publishPoints();
}

void FarmFieldsProducer::setPointFrame(float sx, float sy, float ox, float oy)
{
	if(m_pointFrame && sx == m_psx && sy == m_psy && ox == m_pox && oy == m_poy)
		return;
	m_psx = sx;
	m_psy = sy;
	m_pox = ox;
	m_poy = oy;
	m_pointFrame = true;
	m_pointLastList = 0;   // the points move with the frame: published again
	publishPoints();
}

void FarmFieldsProducer::publishPoints()
{
	if(!m_points)
		return;
	VPFarmHeader* h = (VPFarmHeader*)m_points;
	// DONE: the list in force was placed on a production of this load, in a known world frame
	const bool done = m_prod.haveInput() && m_buildings->loadSeq == m_loadSeq && m_pointFrame;
	const uint32_t state = done ? VPFARM_DONE : VPFARM_PRODUCING;
	if(h->magic == VPFARM_MAGIC && state == m_pointLastState && m_loadSeq == m_pointLastLoad &&
	   (!done || m_buildings->generation == m_pointLastList))
		return;
	std::vector<VPFarmEntry> e;
	uint32_t dropped = 0, points = 0;
	if(done)
	{
		e.reserve(m_buildings->buildings.size());
		for(const cff::FarmBuilding& b : m_buildings->buildings)
		{
			const int32_t hx = b.x - (b.y - (b.y & 1)) / 2, hy = b.y;   // the EXE's hex x of the plot (grid x = hx + (y - (y & 1)) / 2)
			if(hx < -32768 || hx > 32767 || hy < -32768 || hy > 32767)
			{
				dropped++;
				continue;
			}
			VPFarmEntry v;
			memset(&v, 0, sizeof(v));
			v.hexX = (vpfarm_i16)hx;
			v.hexY = (vpfarm_i16)hy;
			v.kind = b.field >= 0 ? VPFARM_KIND_POINT : VPFARM_KIND_CENTRE;
			v.wx = b.pos[0] * m_psx - m_pox;
			v.wy = b.pos[1] * m_psy - m_poy;
			points += b.field >= 0 ? 1 : 0;
			e.push_back(v);
		}
		std::sort(e.begin(), e.end(), [](const VPFarmEntry& a, const VPFarmEntry& b) {
			return VPFarmKeyCmp(a.hexX, a.hexY, b.hexX, b.hexY) < 0;
		});
		e.erase(std::unique(e.begin(), e.end(), [](const VPFarmEntry& a, const VPFarmEntry& b) {
			        return a.hexX == b.hexX && a.hexY == b.hexY;
		        }),
		        e.end());
		if(e.size() > VPFARM_CAPACITY)
		{
			dropped += (uint32_t)(e.size() - VPFARM_CAPACITY);
			e.resize(VPFARM_CAPACITY);
		}
	}
	// the seqlock: odd while writing
	const uint32_t s0 = h->seq;
	h->seq = (s0 | 1u) == s0 ? s0 + 2u : s0 + 1u;   // odd
	MemoryBarrier();
	h->magic = VPFARM_MAGIC;
	h->version = VPFARM_VERSION;
	h->loadSeq = m_loadSeq;
	h->state = state;
	h->count = (uint32_t)e.size();
	h->generation = ++m_pointGen;
	h->serverPid = GetCurrentProcessId();
	h->points = points;
	h->dropped = dropped;
	if(!e.empty())
		memcpy((char*)m_points + VPFARM_HEADER_BYTES, e.data(), e.size() * sizeof(VPFarmEntry));
	MemoryBarrier();
	h->seq = h->seq + 1u;   // even
	pointPublishes++;
	pointDropped += dropped;
	if(dropped && loudDue(pointDropped))
		RNote("LOUD: farmfields: %u farm plots not published for the EXE's placer (beyond %u entries or outside the 16-bit hex range): "
		      "their buildings stand at the plot centre",
		      dropped, (unsigned)VPFARM_CAPACITY);
	if(state != m_pointLastState || m_loadSeq != m_pointLastLoad || e.size() != m_pointLastCount)
		RNote("farmfields: points for the EXE's placer: load %u %s, %zu entries (%u at a field's centroid, %zu at the plot centre), "
		      "publish %u",
		      m_loadSeq, done ? "DONE" : "PRODUCING", e.size(), points, e.size() - points, h->generation);
	m_pointLastState = state;
	m_pointLastLoad = m_loadSeq;
	m_pointLastList = m_buildings->generation;
	m_pointLastCount = (uint32_t)e.size();
}

void FarmFieldsProducer::totalsLine(char* out, size_t n) const
{
	if(!out || !n)
		return;
	_snprintf_s(out, n, _TRUNCATE,
	            "farmfields totals: id=%d plots=%zu decals=%zu marks=%zu loads=%lld rereads=%lld changes=%lld overflows=%lld "
	            "productions=%lld prod_err=%lld composes=%lld no_id=%lld compose_err=%lld tap_refusals=%lld decal_changes=%lld table_err=%lld "
	            "footprints=%lld footprint_err=%lld footprint_cells=%llu buildings=%zu buildings_gen=%llu building_lists=%lld row_loud=%lld",
	            m_farmId, m_in.plots.size(), m_prod.decals().decals.size(), m_prod.marks().size(), (long long)loads, (long long)rereads,
	            (long long)plotChanges, (long long)overflows, (long long)productions, (long long)productionErrors, (long long)composes,
	            (long long)composesWithoutId, (long long)composeErrors, (long long)tapRefusals, (long long)decalChangesMade, (long long)tableErrors,
	            (long long)footprints, (long long)footprintErrors, (unsigned long long)(m_footprint ? m_footprint->cellsSet() : 0),
	            m_buildings->buildings.size(), (unsigned long long)m_buildings->generation, (long long)buildingLists,
	            (long long)rowRefusalLines);
}

}  // namespace srvrender
