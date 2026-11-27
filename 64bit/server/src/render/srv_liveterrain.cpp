// srv_liveterrain.cpp - live terrain in server.exe's renderer: the region hook's core (ticket 4907). See the header.
// Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include "srv_liveterrain.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "bcrypt.lib")

namespace srvrender {
namespace liveterrain {

namespace {
double NowMs()
{
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
const uint8_t kFlagRiver = 0x1, kFlagNaturalWonder = 0x8;
}  // namespace

const char* WaitName(Wait w)
{
	switch(w)
	{
	case Wait::None: return "none";
	case Wait::NoChange: return "NoChange";
	case Wait::Water: return "Water";
	case Wait::Lake: return "Lake";
	case Wait::RiverEdge: return "RiverEdge";
	case Wait::Marsh: return "Marsh";
	case Wait::Seam: return "Seam";
	case Wait::TooLarge: return "TooLarge";
	case Wait::LandWaterInputs: return "LandWaterInputs";
	case Wait::Error: return "Error";
	}
	return "?";
}

bool PlotTableFromFeed(const std::vector<SrvFeed::PlotRecord>& recs, uint32_t W, uint32_t H, const FeatureNameFn& names,
                       PlotTableIn& out, std::string& why)
{
	if(!W || !H || recs.size() != (size_t)W * H)
	{
		why = "plot table of " + std::to_string(recs.size()) + " records is not " + std::to_string(W) + " x " + std::to_string(H);
		return false;
	}
	out.W = W;
	out.H = H;
	out.plots.assign(recs.size(), PlotIn());
	out.oldRuleRecords = out.oldRuleRiverPlots = 0;
	for(size_t i = 0; i < recs.size(); ++i)
	{
		const SrvFeed::PlotRecord& r = recs[i];
		PlotIn& p = out.plots[i];
		p.terrain = r.terrain;
		p.plotType = r.plotType;
		p.feature = r.feature;
		p.naturalWonder = (r.flags & kFlagNaturalWonder) != 0;
		if(r.feature >= 0 && names)
			p.featureType = names(r.feature);
		p.continent = r.artType <= 4 ? r.artType : 0;
		if(r.known & SrvFeed::kPlotKnownRiverEdges)   // ticket 0631: the DLL's own three edge bits
			p.riverEdges = r.riverEdges & 0x7;
		else   // a record without them (a bundle recorded before 0631): the old rule, named by the caller's log
		{
			p.riverEdges = (r.flags & kFlagRiver) ? 0x7 : 0;
			out.oldRuleRecords++;
			if(p.riverEdges)
				out.oldRuleRiverPlots++;
		}
		p.lake = false;
	}
	return true;
}

std::string RiverEdgeRuleText(const PlotTableIn& t)
{
	if(!t.oldRuleRecords)
		return "the plots' own edges (ticket 0631)";
	char b[256];
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "%u of %zu record(s) carry no river edges (recorded before ticket 0631): those take the old rule, all three "
	            "edges on the %u with the any-edge river flag",
	            t.oldRuleRecords, t.plots.size(), t.oldRuleRiverPlots);
	return b;
}

bool FieldRelevantChange(const SrvFeed::PlotRecord& a, const SrvFeed::PlotRecord& b)
{
	if(a.terrain != b.terrain || a.plotType != b.plotType)
		return true;
	if((a.flags & kFlagNaturalWonder) != (b.flags & kFlagNaturalWonder))
		return true;
	if((a.flags & kFlagNaturalWonder) && a.feature != b.feature)
		return true;
	// the window rule (RiverEdge) and the river keep depend on the edges (ticket 0631); the any-edge flag only where a
	// record has no edges (the old rule of PlotTableFromFeed)
	const bool ea = (a.known & SrvFeed::kPlotKnownRiverEdges) != 0, eb = (b.known & SrvFeed::kPlotKnownRiverEdges) != 0;
	if(ea && eb)
		return (a.riverEdges & 0x7) != (b.riverEdges & 0x7);
	if(ea != eb)
		return true;
	return (a.flags & kFlagRiver) != (b.flags & kFlagRiver);
}

bool ApplyWindow(c5r::heightfield::HeightGrid& g, const std::vector<Rect>& window, ApplyStats& st, std::string& why)
{
	st = ApplyStats();
	const bool hasMat = !g.mat.empty();
	if(g.h.size() != (size_t)g.samplesX * g.samplesY || (hasMat && g.mat.size() != g.h.size()))
	{
		why = "the field's layers do not have its size";
		return false;
	}
	for(size_t k = 0; k < window.size(); ++k)   // every check before any write
	{
		const Rect& r = window[k];
		if(!r.w || !r.h || (uint64_t)r.x0 + r.w > g.samplesX || (uint64_t)r.y0 + r.h > g.samplesY)
		{
			char b[160];
			_snprintf_s(b, sizeof(b), _TRUNCATE, "rectangle %u (%u,%u %ux%u) outside the field %ux%u", (unsigned)k, r.x0, r.y0, r.w,
			            r.h, g.samplesX, g.samplesY);
			why = b;
			return false;
		}
		if(r.heights.size() != (size_t)r.w * r.h || r.materials.size() != (size_t)r.w * r.h)
		{
			why = "rectangle " + std::to_string(k) + ": arrays are not w x h";
			return false;
		}
	}
	bool first = true;
	for(const Rect& r : window)
	{
		for(uint32_t y = 0; y < r.h; ++y)
		{
			const size_t row = (size_t)(r.y0 + y) * g.samplesX + r.x0;
			for(uint32_t x = 0; x < r.w; ++x)
			{
				const size_t i = row + x, j = (size_t)y * r.w + x;
				if(g.h[i] != r.heights[j])
				{
					g.h[i] = r.heights[j];
					st.heightsChanged++;
				}
				if(hasMat && g.mat[i] != r.materials[j])
				{
					g.mat[i] = r.materials[j];
					st.materialsChanged++;
				}
				st.written++;
			}
		}
		if(first)
		{
			st.x0 = r.x0;
			st.y0 = r.y0;
			st.x1 = r.x0 + r.w;
			st.y1 = r.y0 + r.h;
			first = false;
		}
		else
		{
			st.x0 = std::min(st.x0, r.x0);
			st.y0 = std::min(st.y0, r.y0);
			st.x1 = std::max(st.x1, r.x0 + r.w);
			st.y1 = std::max(st.y1, r.y0 + r.h);
		}
	}
	return true;
}

std::string RegionSha256(const c5r::heightfield::HeightGrid& g, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
	if(x0 >= x1 || y0 >= y1 || x1 > g.samplesX || y1 > g.samplesY || g.h.size() != (size_t)g.samplesX * g.samplesY)
		return std::string();
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hh = nullptr;
	if(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
		return std::string();
	std::string hex;
	if(BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0)
	{
		bool ok = true;
		for(uint32_t y = y0; y < y1 && ok; ++y)
			ok = BCryptHashData(hh, (PUCHAR)&g.h[(size_t)y * g.samplesX + x0], (ULONG)((x1 - x0) * 2), 0) == 0;
		if(!g.mat.empty())
			for(uint32_t y = y0; y < y1 && ok; ++y)
				ok = BCryptHashData(hh, (PUCHAR)&g.mat[(size_t)y * g.samplesX + x0], (ULONG)(x1 - x0), 0) == 0;
		uint8_t d[32];
		if(ok && BCryptFinishHash(hh, d, sizeof(d), 0) == 0)
		{
			static const char* hx = "0123456789abcdef";
			hex.resize(64);
			for(int i = 0; i < 32; ++i)
			{
				hex[2 * i] = hx[d[i] >> 4];
				hex[2 * i + 1] = hx[d[i] & 15];
			}
		}
		BCryptDestroyHash(hh);
	}
	BCryptCloseAlgorithmProvider(alg, 0);
	return hex;
}

std::wstring LibraryPathFor(const std::wstring& install)
{
	std::wstring p = install;
	if(!p.empty() && p.back() != L'\\' && p.back() != L'/')
		p += L'\\';
	return p + L"Resource\\Common\\TerrainPiecesLibrary.fxsb";
}

// ---- LiveTerrain ------------------------------------------------------------------------------------------------------
LiveTerrain::LiveTerrain(const Config& c) : m_cfg(c)
{
	if(m_cfg.threaded)
		m_thread = std::thread([this] { worker(); });
}

LiveTerrain::~LiveTerrain()
{
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_stop = true;
		m_jobs.clear();
	}
	m_cv.notify_all();
	if(m_thread.joinable())
		m_thread.join();
	m_prod.reset();
}

void LiveTerrain::logf(const char* fmt, ...) const
{
	if(!m_cfg.log)
		return;
	char b[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	m_cfg.log(b);
}

void LiveTerrain::onField(LoadIn&& in, const std::vector<SrvFeed::PlotRecord>& baseline)
{
	if(m_haveLoad && in.loadSeq == m_loadSeq)
		return;   // the same load's field again: the producer and the live edits stay
	m_haveLoad = true;
	m_loadSeq = in.loadSeq;
	m_waitLines = 0;
	m_waitsThisLoad = 0;
	// the filter's baseline is the table the producer is made from (a change after it is posted behind the create job)
	m_last = baseline;
	m_prodTable = baseline;
	m_actual = baseline;
	m_held.assign(baseline.size(), 0);
	m_heldRec.assign(baseline.size(), SrvFeed::PlotRecord());
	m_lastW = in.table.W;
	m_lastH = in.table.H;
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.loads++;
		m_c.fieldGeneration = 0;
		m_ready = false;
	}
	m_j = Journal();
	m_j.open = true;
	m_j.loadSeq = in.loadSeq;
	m_j.constants = in.constants;
	m_j.W = in.table.W;
	m_j.H = in.table.H;
	m_j.baseline = baseline;
	m_j.haveMapSeed = in.haveMapSeed;   // ticket 4912
	m_j.mapSeed = in.mapSeed;
	m_j.riverBand = in.riverBand;       // ticket 0717: the journal's mark (true in the live scene)
	m_j.neutralWonders = in.neutralWonders;   // ticket 0613: the journal's mark
	m_jLastSent = baseline;
	m_jPrevSent = baseline;
	m_loadPassJob = 0;
	const bool pass = in.neutralWonders;
	PlotTableIn passTable;
	if(pass)
		passTable = in.table;
	Job j;
	j.kind = Job::kCreate;
	j.loadSeq = in.loadSeq;
	j.load = std::move(in);
	j.postedMs = NowMs();
	logf("live terrain: load seq %u: producer requested (%ux%u plots, %ux%u samples); river edges: %s", j.loadSeq, j.load.table.W,
	     j.load.table.H, j.load.samplesX, j.load.samplesY, RiverEdgeRuleText(j.load.table).c_str());
	const uint32_t seq = j.loadSeq;
	post(std::move(j));
	if(!pass)
		return;
	// Ticket 0613 (live-terrain.md 8.13.2 (b)): the load-time pass, right behind the create on the same worker. The load's
	// own table: the producer (made with the EXE's rule, as the EXE baked the field) answers it with the neutral base of
	// every unknown wonder of the load (a window), or NoChange when there is none. Its result is a change like any other
	// (the EXE's field first, then the scene's field; the journal's first job, without diffs). A table change that arrives
	// before it runs merges into it (post), as for any queued change.
	Job p;
	p.kind = Job::kChange;
	p.loadSeq = seq;
	p.id = m_nextJob++;
	p.table = std::move(passTable);
	p.sent = baseline;
	p.postedMs = NowMs();
	m_loadPassJob = p.id;
	JournalJob e;
	e.id = p.id;
	m_j.jobs.push_back(e);   // no diffs: m_jLastSent stays the baseline
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.changesPosted--;   // post counts changes; the pass is counted as a load pass (takeResult)
	}
	logf("live terrain: load seq %u: load-time pass requested (FixUnknownWonderBase, ticket 0613: unknown natural wonders on "
	     "their own ground), job %llu",
	     seq, (unsigned long long)p.id);
	post(std::move(p));
}

void LiveTerrain::onFieldUnavailable(uint32_t loadSeq, const std::string& why)
{
	if(m_haveLoad && loadSeq == m_loadSeq)
		return;
	m_haveLoad = true;
	m_loadSeq = loadSeq;
	m_last.clear();
	m_prodTable.clear();
	m_actual.clear();
	m_held.clear();
	m_heldRec.clear();
	m_lastW = m_lastH = 0;
	m_j = Journal();
	m_j.loadSeq = loadSeq;
	m_jLastSent.clear();
	m_jPrevSent.clear();
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.loads++;
		m_c.createErrors++;
		m_c.fieldGeneration = 0;
		m_ready = false;
		m_jobs.clear();   // nothing of an earlier load is still useful
	}
	logf("LOUD: live terrain: load seq %u: no producer (%s); terrain changes in this load wait for the reload, as in stock",
	     loadSeq, why.c_str());
}

void LiveTerrain::onTable(const std::vector<SrvFeed::PlotRecord>& recs, uint32_t W, uint32_t H, const FeatureNameFn& names)
{
	if(!m_haveLoad || !W || !H || recs.size() != (size_t)W * H)
		return;
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.tablesSeen++;
	}
	if(W != m_lastW || H != m_lastH)
		return;   // another map than the load's (a table before its field): the next field's onField takes it
	if(m_last.size() != recs.size() || m_prodTable.size() != recs.size())
		return;   // cannot happen after onField (its baseline has the load's size)
	m_actual = recs;
	// Held plots (a change the producer answered "wait for reload" for): the producer keeps their old cell, so every later
	// diff would carry them and wait too. They are sent as the producer has them (its current table) until the reload;
	// a held plot that changes again (or back) is released and goes out with the next table.
	std::vector<SrvFeed::PlotRecord> sent = recs;
	for(size_t i = 0; i < recs.size(); ++i)
	{
		if(!m_held[i])
			continue;
		if(!FieldRelevantChange(m_prodTable[i], recs[i]) || FieldRelevantChange(m_heldRec[i], recs[i]))
		{
			m_held[i] = 0;
			std::lock_guard<std::mutex> lk(m_mu);
			m_c.heldReleased++;
			continue;
		}
		sent[i] = m_prodTable[i];
	}
	int64_t relevant = 0;
	for(size_t i = 0; i < sent.size(); ++i)
		if(FieldRelevantChange(m_last[i], sent[i]))
			relevant++;
	m_last = sent;
	if(!relevant)
		return;
	Job j;
	j.kind = Job::kChange;
	j.loadSeq = m_loadSeq;
	j.id = m_nextJob++;
	j.sent = std::move(sent);
	std::string why;
	if(!PlotTableFromFeed(j.sent, W, H, names, j.table, why))
		return;   // cannot happen: the size was checked above
	j.postedMs = NowMs();
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.relevantPlots += relevant;
	}
	// the journal (the scene bundle's record of what the producer was given): the job's table as a diff against the table
	// of the job it follows; a merged job replaces the queued one's entry (that one never ran)
	const uint64_t id = j.id;
	std::vector<SrvFeed::PlotRecord> sentCopy = j.sent;
	const bool merged = post(std::move(j));
	if(m_j.open)
	{
		if(merged && !m_j.jobs.empty())
		{
			if(m_loadPassJob && m_j.jobs.back().id == m_loadPassJob)
				m_loadPassJob = id;   // ticket 0613: the table merged into the queued load-time pass, which runs with it
			m_j.jobs.pop_back();
			m_jLastSent = m_jPrevSent;
		}
		JournalJob e;
		e.id = id;
		for(size_t i = 0; i < sentCopy.size() && i < m_jLastSent.size(); ++i)
			if(memcmp(&sentCopy[i], &m_jLastSent[i], sizeof(SrvFeed::PlotRecord)) != 0)
				e.diffs.push_back(std::make_pair((uint32_t)i, sentCopy[i]));
		m_j.jobs.push_back(std::move(e));
		m_jPrevSent.swap(m_jLastSent);
		m_jLastSent.swap(sentCopy);
	}
}

const Journal& LiveTerrain::journal() const
{
	return m_j;
}

bool ApplyJournalJob(const JournalJob& job, uint32_t W, uint32_t H, std::vector<SrvFeed::PlotRecord>& table, std::string& why)
{
	if(table.size() != (size_t)W * H)
	{
		why = "table of " + std::to_string(table.size()) + " records is not W x H";
		return false;
	}
	for(const std::pair<uint32_t, SrvFeed::PlotRecord>& d : job.diffs)
	{
		if(d.first >= table.size())
		{
			why = "job " + std::to_string(job.id) + ": plot index " + std::to_string(d.first) + " outside the table";
			return false;
		}
		table[d.first] = d.second;
	}
	return true;
}

bool LiveTerrain::post(Job&& j)
{
	{
		std::lock_guard<std::mutex> lk(m_mu);
		if(j.kind == Job::kCreate)
		{
			m_jobs.clear();   // jobs of an earlier load are of no use now
		}
		else
		{
			m_c.changesPosted++;
			// merge: a queued change of the same load is replaced by the newer table (the producer compares with its own
			// current table, so the newest one carries every change)
			for(Job& q : m_jobs)
				if(q.kind == Job::kChange && q.loadSeq == j.loadSeq)
				{
					q.table = std::move(j.table);
					q.sent = std::move(j.sent);
					q.id = j.id;
					m_c.changesMerged++;
					return true;
				}
		}
		m_jobs.push_back(std::move(j));
	}
	if(m_cfg.threaded)
		m_cv.notify_one();
	else
	{
		// inline: run every queued job now, on this thread
		for(;;)
		{
			Job cur;
			{
				std::lock_guard<std::mutex> lk(m_mu);
				if(m_jobs.empty())
					break;
				cur = std::move(m_jobs.front());
				m_jobs.pop_front();
				m_running = true;
			}
			run(cur);
			std::lock_guard<std::mutex> lk(m_mu);
			m_running = false;
		}
	}
	return false;
}

void LiveTerrain::worker()
{
	for(;;)
	{
		Job cur;
		{
			std::unique_lock<std::mutex> lk(m_mu);
			m_cv.wait(lk, [this] { return m_stop || !m_jobs.empty(); });
			if(m_stop)
				return;
			cur = std::move(m_jobs.front());
			m_jobs.pop_front();
			m_running = true;
		}
		run(cur);
		{
			std::lock_guard<std::mutex> lk(m_mu);
			m_running = false;
		}
		m_idleCv.notify_all();
	}
}

void LiveTerrain::run(Job& j)
{
	const double t0 = NowMs();
	if(j.kind == Job::kCreate)
	{
		m_prod.reset();
		m_prodLoad = 0;
		std::unique_ptr<RegionProducer> p = m_cfg.factory ? m_cfg.factory() : nullptr;
		c5r::Status st = p ? p->create(j.load) : c5r::Status::fail(c5r::Err::NotFound, "no producer factory");
		const double ms = NowMs() - t0;
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.createMsLast = ms;
		m_landWaterOn = st.ok() && p->landWaterOn();   // ticket 4914: the journal's mark (takeResult copies it on the render thread)
		m_landWaterLoad = j.loadSeq;
		if(!st.ok())
		{
			m_c.createErrors++;
			m_ready = false;
			logf("LOUD: live terrain: load seq %u: the producer could not be made (%s) in %.0f ms; terrain changes in this load "
			     "wait for the reload, as in stock",
			     j.loadSeq, st.str().c_str(), ms);
			return;
		}
		m_c.creates++;
		m_prod = std::move(p);
		m_prodLoad = j.loadSeq;
		m_ready = true;
		m_readyLoad = j.loadSeq;
		logf("live terrain: load seq %u: producer made in %.0f ms (%.0f ms after the field)%s", j.loadSeq, ms, NowMs() - j.postedMs,
		     m_landWaterOn ? "; land/water changes can go live (ticket 4914)" : "");
		return;
	}
	Result r;
	r.loadSeq = j.loadSeq;
	r.job = j.id;
	r.sent = std::move(j.sent);
	if(!m_prod || m_prodLoad != j.loadSeq)
	{
		r.out.live = false;
		r.out.reason = Wait::Error;
		r.out.reasonText = "no producer for this load";
	}
	else
	{
		c5r::Status st = m_prod->change(j.table, r.out);
		if(!st.ok())
		{
			r.out = ChangeOut();
			r.out.live = false;
			r.out.reason = Wait::Error;
			r.out.reasonText = st.str();
		}
	}
	r.workerMs = NowMs() - t0;
	r.queuedMs = NowMs() - j.postedMs;
	std::lock_guard<std::mutex> lk(m_mu);
	m_c.changeMsMax = std::max(m_c.changeMsMax, r.workerMs);
	m_results.push_back(std::move(r));
}

bool LiveTerrain::takeResult(Result& out)
{
	for(;;)
	{
		{
			std::lock_guard<std::mutex> lk(m_mu);
			// ticket 4914: the journal's land/water mark, once the load's producer is made (before any of its results)
			if(m_j.open && m_landWaterOn && m_landWaterLoad == m_j.loadSeq)
				m_j.landWater = true;
			if(m_results.empty())
				return false;
			out = std::move(m_results.front());
			m_results.pop_front();
			if(out.loadSeq != m_loadSeq)
			{
				m_c.staleResults++;
				continue;
			}
			if(out.out.live)
				m_c.live++;
			else if(out.out.reason == Wait::NoChange)
				m_c.noChange++;
			else if(out.out.reason == Wait::Error)
				m_c.changeErrors++;
			else
				m_c.waits++;
			if(m_loadPassJob && out.job == m_loadPassJob)
			{   // ticket 0613
				m_c.loadPasses++;
				if(out.out.live)
					m_c.loadPassesLive++;
				m_c.loadPassMsLast = out.workerMs;
			}
		}
		if(m_loadPassJob && out.job == m_loadPassJob)   // ticket 0613: one line per load (the extra producer call, timed)
		{
			const std::string what = out.out.live ? "live: " + std::to_string(out.out.changedPlots.size()) + " plot(s) re-drawn on their own ground"
			                         : out.out.reason == Wait::NoChange ? std::string("no unknown natural wonder in this load (no window)")
			                                                           : std::string("not drawn (") + WaitName(out.out.reason) +
			                                                                 "): unknown wonders keep the EXE's Fuji base in this load";
			logf("live terrain: load seq %u: load-time pass (job %llu, FixUnknownWonderBase): %s; %.1f ms in the producer's worker, "
			     "%.1f ms from the post (off the render thread)",
			     out.loadSeq, (unsigned long long)out.job, what.c_str(), out.workerMs, out.queuedMs);
		}
		// the producer's current table (live: it took the table; otherwise it kept its own) and the held plots
		if(out.out.live && out.sent.size() == m_prodTable.size())
			m_prodTable = out.sent;
		else if(!out.out.live && out.out.reason != Wait::NoChange && m_lastW)
		{
			int64_t held = 0;
			for(const std::pair<int32_t, int32_t>& p : out.out.changedPlots)
			{
				if(p.first < 0 || p.second < 0 || (uint32_t)p.first >= m_lastW || (uint32_t)p.second >= m_lastH)
					continue;
				const size_t i = (size_t)p.second * m_lastW + (size_t)p.first;
				if(i >= m_held.size() || m_held[i])
					continue;
				m_held[i] = 1;
				m_heldRec[i] = m_actual[i];
				m_last[i] = m_prodTable[i];   // the next table sends it as the producer has it: no repost for it alone
				held++;
			}
			std::lock_guard<std::mutex> lk(m_mu);
			m_c.held += held;
		}
		if(!out.out.live)
		{
			std::string plots;
			for(size_t k = 0; k < out.out.changedPlots.size() && k < 12; ++k)
				plots += (k ? " " : "") + std::to_string(out.out.changedPlots[k].first) + "," + std::to_string(out.out.changedPlots[k].second);
			if(out.out.changedPlots.size() > 12)
				plots += " ... (" + std::to_string(out.out.changedPlots.size()) + " plots)";
			m_waitsThisLoad++;
			const bool line = out.out.reason == Wait::Error || m_waitLines < m_cfg.maxWaitLines || (m_waitsThisLoad % 100) == 0;
			if(line && out.out.reason != Wait::NoChange)
			{
				m_waitLines++;
				logf("%slive terrain: load seq %u change %llu: %s: %s%s%s; the field stays until the reload (%.1f ms in the producer)",
				     out.out.reason == Wait::Error ? "LOUD: " : "", out.loadSeq, (unsigned long long)out.job,
				     out.out.reason == Wait::Error ? "error" : "wait for reload", WaitName(out.out.reason),
				     out.out.reasonText.empty() ? "" : (": " + out.out.reasonText).c_str(),
				     plots.empty() ? "" : (" [plots " + plots + "]").c_str(), out.workerMs);
			}
		}
		// MC #9538 item 2 (live-terrain.md 8.17): the coast parts a land/water change names but does not draw, one plain line
		// per change that has any (what a reload would move besides, and the wonder samples kept); not a fault
		if(out.out.coastLinesBeyondWindow || out.out.coastLinesReloadMoves || out.out.samplesWonderKept)
			logf("live terrain: load seq %u change %llu: coast (%s): %u line(s) redrawn in the window, %u of them also reach beyond it; "
			     "%u unchanged line(s) a reload would re-jitter (named, not drawn: a reload moves them, live-terrain.md 8.10.1); "
			     "%llu sample(s) kept on natural wonders' hexes",
			     out.loadSeq, (unsigned long long)out.job, out.out.live ? "live" : "not drawn", out.out.coastLinesRedrawn,
			     out.out.coastLinesBeyondWindow, out.out.coastLinesReloadMoves, (unsigned long long)out.out.samplesWonderKept);
		// MC #9538 item 1 (live-terrain.md 8.10.3): one LOUD line per change whose coast met a zero-length segment or tangent
		// (stepped over, never a NaN point) or a strip triangle that was not finite (refused, not drawn)
		if(out.out.coastDegenerate || out.out.stripTrianglesRefused)
			logf("LOUD: live terrain: load seq %u change %llu: the coast had %u zero-length segment(s) or tangent(s), stepped over (no "
			     "NaN point), and %llu strip triangle(s) refused (not finite); the change is %s (live-terrain.md 8.10.3)",
			     out.loadSeq, (unsigned long long)out.job, out.out.coastDegenerate, (unsigned long long)out.out.stripTrianglesRefused,
			     out.out.live ? "drawn without them" : "not drawn");
		return true;
	}
}

uint32_t LiveTerrain::noteApplied(const Result& r, const ApplyStats& st, double applyMs, const std::string& sha)
{
	uint32_t gen;
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.applied++;
		m_c.applyMsMax = std::max(m_c.applyMsMax, applyMs);
		gen = ++m_c.fieldGeneration;
	}
	if(m_j.open && r.loadSeq == m_j.loadSeq)
	{
		Generation g;
		g.gen = gen;
		g.job = r.job;
		g.x0 = st.x0;
		g.y0 = st.y0;
		g.x1 = st.x1;
		g.y1 = st.y1;
		g.sha = sha;
		m_j.generations.push_back(g);
	}
	logf("live terrain: load seq %u change %llu: live, field generation %u: %zu plots, %zu rectangles, box %u,%u-%u,%u, %llu samples "
	     "written (%llu heights, %llu materials changed; D %llu, w=1 %llu, ring %llu); producer %.1f ms, queued %.1f ms, applied "
	     "in %.1f ms; sha256 %s",
	     r.loadSeq, (unsigned long long)r.job, gen, r.out.changedPlots.size(), r.out.window.size(), st.x0, st.y0, st.x1, st.y1,
	     (unsigned long long)st.written, (unsigned long long)st.heightsChanged, (unsigned long long)st.materialsChanged,
	     (unsigned long long)r.out.inD, (unsigned long long)r.out.inW1, (unsigned long long)r.out.inRing, r.workerMs, r.queuedMs,
	     applyMs, sha.empty() ? "-" : sha.c_str());
	return gen;
}

void LiveTerrain::noteApplyError(const Result& r, const std::string& why)
{
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_c.applyErrors++;
	}
	logf("LOUD: live terrain: load seq %u change %llu: the window was not written (%s); the field stays until the reload",
	     r.loadSeq, (unsigned long long)r.job, why.c_str());
}

bool LiveTerrain::ready() const
{
	std::lock_guard<std::mutex> lk(m_mu);
	return m_ready && m_readyLoad == m_loadSeq;
}

bool LiveTerrain::busy() const
{
	std::lock_guard<std::mutex> lk(m_mu);
	return m_running || !m_jobs.empty();
}

bool LiveTerrain::waitIdle(double maxMs)
{
	std::unique_lock<std::mutex> lk(m_mu);
	return m_idleCv.wait_for(lk, std::chrono::milliseconds((int64_t)maxMs), [this] { return !m_running && m_jobs.empty(); });
}

Counters LiveTerrain::counters() const
{
	std::lock_guard<std::mutex> lk(m_mu);
	return m_c;
}

void LiveTerrain::statsLine(char* out, size_t n) const
{
	const Counters c = counters();
	_snprintf_s(out, n, _TRUNCATE,
	            "live terrain: loads %lld, producers %lld (errors %lld, last %.0f ms), changes posted %lld (merged %lld), live %lld "
	            "(applied %lld, apply errors %lld), waits %lld, no change %lld, errors %lld, stale %lld, generation %u, max change "
	            "%.1f ms, max apply %.1f ms",
	            (long long)c.loads, (long long)c.creates, (long long)c.createErrors, c.createMsLast, (long long)c.changesPosted,
	            (long long)c.changesMerged, (long long)c.live, (long long)c.applied, (long long)c.applyErrors, (long long)c.waits,
	            (long long)c.noChange, (long long)c.changeErrors, (long long)c.staleResults, c.fieldGeneration, c.changeMsMax,
	            c.applyMsMax);
}

// ---- The scene bundle's text form of the journal ----------------------------------------------------------------------
namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string B64(const std::vector<uint8_t>& in)
{
	std::string o;
	o.reserve((in.size() + 2) / 3 * 4);
	for(size_t i = 0; i < in.size(); i += 3)
	{
		const uint32_t n = (uint32_t)in[i] << 16 | (i + 1 < in.size() ? (uint32_t)in[i + 1] << 8 : 0) |
		                   (i + 2 < in.size() ? (uint32_t)in[i + 2] : 0);
		o += kB64[(n >> 18) & 63];
		o += kB64[(n >> 12) & 63];
		o += i + 1 < in.size() ? kB64[(n >> 6) & 63] : '=';
		o += i + 2 < in.size() ? kB64[n & 63] : '=';
	}
	return o;
}

bool UnB64(const char* s, size_t n, std::vector<uint8_t>& out)
{
	if(n % 4)
		return false;
	out.clear();
	out.reserve(n / 4 * 3);
	for(size_t i = 0; i < n; i += 4)
	{
		uint32_t v = 0;
		int pad = 0;
		for(int k = 0; k < 4; ++k)
		{
			const char c = s[i + k];
			int d;
			if(c == '=')
			{
				if(i + 4 != n || k < 2)
					return false;
				d = 0;
				pad++;
			}
			else
			{
				if(pad)
					return false;
				const char* p = strchr(kB64, c);
				if(!c || !p)
					return false;
				d = (int)(p - kB64);
			}
			v = v << 6 | (uint32_t)d;
		}
		out.push_back((uint8_t)(v >> 16));
		if(pad < 2)
			out.push_back((uint8_t)(v >> 8));
		if(pad < 1)
			out.push_back((uint8_t)v);
	}
	return true;
}

struct Writer
{
	std::vector<uint8_t> b;
	void put(const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
	void u32(uint32_t v) { put(&v, 4); }
	void u64(uint64_t v) { put(&v, 8); }
};
struct Reader
{
	const std::vector<uint8_t>& b;
	size_t at = 0;
	bool ok = true;
	explicit Reader(const std::vector<uint8_t>& v) : b(v) {}
	bool get(void* p, size_t n)
	{
		if(!ok || n > b.size() - at)
			return ok = false;
		memcpy(p, &b[at], n);
		at += n;
		return true;
	}
	uint32_t u32()
	{
		uint32_t v = 0;
		get(&v, 4);
		return v;
	}
	uint64_t u64()
	{
		uint64_t v = 0;
		get(&v, 8);
		return v;
	}
};

bool HexTo32(const std::string& hex, uint8_t out[32])
{
	if(hex.size() != 64)
		return false;
	for(int i = 0; i < 32; ++i)
	{
		auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
		const int a = nib(hex[2 * i]), b = nib(hex[2 * i + 1]);
		if(a < 0 || b < 0)
			return false;
		out[i] = (uint8_t)(a << 4 | b);
	}
	return true;
}
std::string ToHex(const uint8_t* d, size_t n)
{
	static const char* hx = "0123456789abcdef";
	std::string s(n * 2, '0');
	for(size_t i = 0; i < n; ++i)
	{
		s[2 * i] = hx[d[i] >> 4];
		s[2 * i + 1] = hx[d[i] & 15];
	}
	return s;
}
const uint32_t kJournalMagic = 0x314A544Cu;   // "LTJ1"
const uint32_t kJournalSeedTag = 0x44454553u;   // "SEED" (ticket 4912)
const uint32_t kJournalLandWaterTag = 0x5441574Cu;   // "LWAT" (ticket 4914)
const uint32_t kJournalRiverBandTag = 0x444E4252u;   // "RBND" (ticket 0717)
const uint32_t kJournalNeutralWondersTag = 0x4E4F574Eu;   // "NWON" (ticket 0613)
}  // namespace

// ticket 4912: the map seed's source (srv_liveterrain.h); server.exe's srv_liveterrain_seed.cpp sets it at static
// initialisation, before any thread runs; read on the render thread afterwards
namespace {
MapSeedFn s_mapSeedFn = nullptr;
MapSeedFaultFn s_mapSeedFaultFn = nullptr;
}
void SetMapSeedSource(MapSeedFn fn)
{
	s_mapSeedFn = fn;
}
MapSeedFn GetMapSeedSource()
{
	return s_mapSeedFn;
}
void SetMapSeedFaultReport(MapSeedFaultFn fn)
{
	s_mapSeedFaultFn = fn;
}
MapSeedFaultFn GetMapSeedFaultReport()
{
	return s_mapSeedFaultFn;
}
bool HostIsServerExe()
{
	return GetProcAddress(GetModuleHandleW(nullptr), "FJobSetAddJobImpl") != nullptr;   // srv_exports.cpp
}

MapSeedVerdict MapSeedNotWanted(uint32_t loadSeq)
{
	MapSeedVerdict v;
	char b[256];
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "live terrain: load seq %u: map seed not read (C5_RENDER_LIVE_LANDWATER=0: the land/water step is off, its "
	            "land/water changes wait for the reload as before epic 0615)",
	            loadSeq);
	v.line = b;
	return v;
}

MapSeedVerdict DecideMapSeed(uint32_t loadSeq, bool haveSource, bool readOk, uint32_t seed, const std::string& why,
                             bool isServerExe, bool& faultNoted)
{
	MapSeedVerdict v;
	char b[512];
	if(haveSource && readOk && seed != 0)
	{
		v.have = true;
		_snprintf_s(b, sizeof(b), _TRUNCATE,
		            "live terrain: load seq %u: map seed %u (0x%08X), CvPreGame::mapRandomSeed of the hosted DLL at the field "
		            "hand-over (ticket 4912)", loadSeq, seed, seed);
		v.line = b;
		return v;
	}
	if(!haveSource && !isServerExe)
	{
		_snprintf_s(b, sizeof(b), _TRUNCATE,
		            "live terrain: load seq %u: no map seed source in this binary (not server.exe); the journal carries none and "
		            "land/water changes wait for the reload", loadSeq);
		v.line = b;
		return v;
	}
	const std::string what = !haveSource ? std::string("server.exe has no map seed reader (a build fault: "
	                                                    "server/src/srv_liveterrain_seed.cpp not linked)")
	                                     : readOk ? std::string("the hosted DLL's mapRandomSeed is 0 (not a seed)")
	                                              : "the map seed cannot be read (" + why + ")";
	if(!haveSource && faultNoted)
	{   // the build fault: LOUD once per session, then a plain line per load
		_snprintf_s(b, sizeof(b), _TRUNCATE,
		            "live terrain: load seq %u: no map seed (the fault above); land/water changes wait for the reload", loadSeq);
		v.line = b;
		return v;
	}
	v.loud = true;
	v.report = !faultNoted;
	faultNoted = true;
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "LOUD: live terrain: load seq %u: %s; this load's land/water changes are never drawn live (no coast from seed 0, "
	            "D118: they wait for the reload); the journal carries no seed (ticket 4912)%s", loadSeq, what.c_str(),
	            v.report ? "; writing the D118 report" : "");
	v.line = b;
	return v;
}

std::string EncodeSpanText(const Journal& j, const std::vector<FrameGen>& frames, const FeatureNameFn& names)
{
	// the feature names of every id the load's tables use (the player has no DLL side to name them)
	int maxId = -1;
	for(const SrvFeed::PlotRecord& r : j.baseline)
		maxId = std::max(maxId, (int)r.feature);
	for(const JournalJob& job : j.jobs)
		for(const std::pair<uint32_t, SrvFeed::PlotRecord>& d : job.diffs)
			maxId = std::max(maxId, (int)d.second.feature);
	Writer w;
	w.u32(kJournalMagic);
	w.u32(j.loadSeq);
	w.u32(j.W);
	w.u32(j.H);
	w.u32((uint32_t)j.constants.size());
	w.put(j.constants.data(), j.constants.size());
	w.u32((uint32_t)j.baseline.size());
	w.put(j.baseline.data(), j.baseline.size() * sizeof(SrvFeed::PlotRecord));
	w.u32((uint32_t)j.jobs.size());
	for(const JournalJob& job : j.jobs)
	{
		w.u64(job.id);
		w.u32((uint32_t)job.diffs.size());
		for(const std::pair<uint32_t, SrvFeed::PlotRecord>& d : job.diffs)
		{
			w.u32(d.first);
			w.put(&d.second, sizeof(SrvFeed::PlotRecord));
		}
	}
	w.u32((uint32_t)j.generations.size());
	for(const Generation& g : j.generations)
	{
		w.u32(g.gen);
		w.u64(g.job);
		w.u32(g.x0);
		w.u32(g.y0);
		w.u32(g.x1);
		w.u32(g.y1);
		uint8_t d[32] = {};
		HexTo32(g.sha, d);
		w.put(d, 32);
	}
	w.u32((uint32_t)frames.size());
	for(const FrameGen& f : frames)
	{
		w.u64((uint64_t)f.frameId);
		w.u32(f.gen);
	}
	w.u32((uint32_t)(maxId + 1));
	for(int id = 0; id <= maxId; ++id)
	{
		const std::string n = names ? names(id) : (size_t)id < j.featureNames.size() ? j.featureNames[id] : std::string();
		w.u32((uint32_t)n.size());
		w.put(n.data(), n.size());
	}
	if(j.riverBand)
	{   // ticket 0717: the river band's mark, a trailer only when on, before the seed's (Journal's note)
		w.u32(kJournalRiverBandTag);
		w.u32(1);
	}
	if(j.neutralWonders)
	{   // ticket 0613: the neutral base's mark, a trailer only when on, after the river band's, before the seed's
		w.u32(kJournalNeutralWondersTag);
		w.u32(1);
	}
	if(j.haveMapSeed)
	{   // ticket 4912: the load's map seed, a trailer only when known (Journal's note)
		w.u32(kJournalSeedTag);
		w.u32(1);
		w.u32(j.mapSeed);
	}
	if(j.landWater && j.haveMapSeed)
	{   // ticket 4914: the land/water step's mark, a trailer only when on (Journal's note)
		w.u32(kJournalLandWaterTag);
		w.u32(1);
	}
	return std::string(kJournalForm) + B64(w.b);
}

bool DecodeSpanText(const std::string& text, Journal& j, std::vector<FrameGen>& frames, std::string& why)
{
	const size_t pre = strlen(kJournalForm);
	if(text.compare(0, pre, kJournalForm) != 0)
	{
		why = std::string("the journal does not start with \"") + kJournalForm + "\"";
		return false;
	}
	std::vector<uint8_t> b;
	if(!UnB64(text.data() + pre, text.size() - pre, b))
	{
		why = "the journal is not base64";
		return false;
	}
	Reader r(b);
	j = Journal();
	frames.clear();
	if(r.u32() != kJournalMagic)
	{
		why = "the journal's magic is not LTJ1";
		return false;
	}
	j.open = true;
	j.loadSeq = r.u32();
	j.W = r.u32();
	j.H = r.u32();
	const uint32_t nc = r.u32();
	if(!r.ok || nc > 4096 || !j.W || !j.H || j.W > 256 || j.H > 256)
	{
		why = "the journal's header is out of range";
		return false;
	}
	j.constants.resize(nc);
	r.get(j.constants.data(), nc);
	const uint32_t nb = r.u32();
	if(!r.ok || nb != j.W * j.H)
	{
		why = "the journal's baseline is not W x H records";
		return false;
	}
	j.baseline.resize(nb);
	r.get(j.baseline.data(), (size_t)nb * sizeof(SrvFeed::PlotRecord));
	const uint32_t njobs = r.u32();
	for(uint32_t k = 0; r.ok && k < njobs; ++k)
	{
		JournalJob job;
		job.id = r.u64();
		const uint32_t nd = r.u32();
		if(!r.ok || nd > nb || (!j.jobs.empty() && job.id <= j.jobs.back().id))
		{
			why = "journal job " + std::to_string(k) + ": diff count or id out of order";
			return false;
		}
		for(uint32_t d = 0; r.ok && d < nd; ++d)
		{
			std::pair<uint32_t, SrvFeed::PlotRecord> e;
			e.first = r.u32();
			r.get(&e.second, sizeof(SrvFeed::PlotRecord));
			if(e.first >= nb)
			{
				why = "journal job " + std::to_string(k) + ": a plot index outside the table";
				return false;
			}
			job.diffs.push_back(e);
		}
		j.jobs.push_back(std::move(job));
	}
	const uint32_t ng = r.u32();
	for(uint32_t k = 0; r.ok && k < ng; ++k)
	{
		Generation g;
		g.gen = r.u32();
		g.job = r.u64();
		g.x0 = r.u32();
		g.y0 = r.u32();
		g.x1 = r.u32();
		g.y1 = r.u32();
		uint8_t d[32];
		r.get(d, 32);
		g.sha = ToHex(d, 32);
		if(g.gen != k + 1 || g.x0 >= g.x1 || g.y0 >= g.y1 || g.x1 > j.W * 64 || g.y1 > (j.H + 4) * 64)
		{
			why = "journal generation " + std::to_string(k) + ": not the next generation or its box outside the field";
			return false;
		}
		bool found = false;
		for(const JournalJob& job : j.jobs)
			found = found || job.id == g.job;
		if(!found || (!j.generations.empty() && g.job <= j.generations.back().job))
		{
			why = "journal generation " + std::to_string(g.gen) + ": its job is not in the journal or out of order";
			return false;
		}
		j.generations.push_back(g);
	}
	const uint32_t nf = r.u32();
	for(uint32_t k = 0; r.ok && k < nf; ++k)
	{
		FrameGen f;
		f.frameId = (int64_t)r.u64();
		f.gen = r.u32();
		if(f.gen > ng || (!frames.empty() && (f.frameId <= frames.back().frameId || f.gen < frames.back().gen)))
		{
			why = "journal frame " + std::to_string(k) + ": out of order or a generation not in the journal";
			return false;
		}
		frames.push_back(f);
	}
	const uint32_t nn = r.u32();
	if(!r.ok || nn > 4097)
	{
		why = "the journal's feature names are out of range";
		return false;
	}
	for(uint32_t k = 0; r.ok && k < nn; ++k)
	{
		const uint32_t len = r.u32();
		if(!r.ok || len > 128)
		{
			why = "journal feature name " + std::to_string(k) + ": too long";
			return false;
		}
		std::string n(len, ' ');
		if(len)
			r.get(&n[0], len);
		j.featureNames.push_back(n);
	}
	if(r.ok && b.size() - r.at >= 8)
	{   // ticket 0717: the river band's mark ("RBND", u32 1), first after the names; any other tag is left for the seed's
		const size_t at = r.at;
		const uint32_t tag = r.u32(), ver = r.u32();
		if(r.ok && tag == kJournalRiverBandTag)
		{
			if(ver != 1)
			{
				why = "the journal's river band mark has version " + std::to_string(ver);
				return false;
			}
			j.riverBand = true;
		}
		else
			r.at = at;
	}
	if(r.ok && b.size() - r.at >= 8)
	{   // ticket 0613: the neutral base's mark ("NWON", u32 1), after the river band's; any other tag is left for the seed's
		const size_t at = r.at;
		const uint32_t tag = r.u32(), ver = r.u32();
		if(r.ok && tag == kJournalNeutralWondersTag)
		{
			if(ver != 1)
			{
				why = "the journal's neutral wonder base mark has version " + std::to_string(ver);
				return false;
			}
			j.neutralWonders = true;
		}
		else
			r.at = at;
	}
	if(r.ok && r.at < b.size())
	{   // ticket 4912: the map seed's trailer ("SEED", u32 1, u32 seed); anything else after the names stays a refusal
		const uint32_t tag = r.u32(), ver = r.u32(), seed = r.u32();
		if(!r.ok || tag != kJournalSeedTag || ver != 1)
		{
			why = r.ok ? "the journal has bytes after its end that are not the map seed's trailer" : "the journal's map seed trailer is truncated";
			return false;
		}
		j.haveMapSeed = true;
		j.mapSeed = seed;
	}
	if(r.ok && b.size() - r.at >= 8)
	{   // ticket 4914: the land/water mark's trailer ("LWAT", u32 1), only after the seed's (fewer bytes: "after its end" below)
		const uint32_t tag = r.u32(), ver = r.u32();
		if(!r.ok || tag != kJournalLandWaterTag || ver != 1)
		{
			why = "the journal has bytes after its end that are not the land/water mark's trailer (after the map seed's)";
			return false;
		}
		j.landWater = true;
	}
	if(!r.ok || r.at != b.size())
	{
		why = r.ok ? "the journal has bytes after its end" : "the journal is truncated";
		return false;
	}
	return true;
}

bool CheckLiveTerrainConfig(const std::map<std::string, std::string>& rc, const std::vector<std::string>& notes, std::string& why,
                            bool& missing)
{
	missing = false;
	auto on = rc.find("render_live_terrain");
	auto jt = rc.find("render_live_terrain_journal");
	if(on != rc.end() && on->second != "0" && on->second != "1")
	{
		why = "render_config's render_live_terrain is '" + on->second + "', not 0 or 1";
		return false;
	}
	const bool isOn = on != rc.end() && on->second == "1";
	if(jt != rc.end())
	{
		if(!isOn)
		{
			why = "render_config's render_live_terrain_journal while render_live_terrain is not 1";
			return false;
		}
		Journal j;
		std::vector<FrameGen> f;
		std::string w;
		if(!DecodeSpanText(jt->second, j, f, w))
		{
			why = "render_config's render_live_terrain_journal: " + w;
			return false;
		}
		return true;
	}
	if(isOn)
	{
		const size_t n = strlen(kLiveTerrainNote);
		for(const std::string& s : notes)
			if(s.compare(0, n, kLiveTerrainNote) == 0)
				return true;
		missing = true;
		why = std::string("render_live_terrain is 1 and render_config has no render_live_terrain_journal and the bundle no note \"") +
		      kLiveTerrainNote + "...\"";
		return false;
	}
	return true;
}

uint32_t Replay::GenerationAt(const std::vector<FrameGen>& frames, int64_t frameId)
{
	uint32_t g = 0;
	for(const FrameGen& f : frames)
		if(f.frameId <= frameId)
			g = f.gen;
	return g;
}

c5r::Status Replay::init(const ProducerFactory& factory, const Journal& j, const c5r::heightfield::HeightGrid& loadField)
{
	m_p.reset();
	m_j = j;
	m_tab = j.baseline;
	m_nextJob = m_nextGen = 0;
	m_gen = 0;
	if(loadField.mapW != j.W || loadField.mapH != j.H + 4 || loadField.h.size() != (size_t)loadField.samplesX * loadField.samplesY)
		return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: the load's field is " + std::to_string(loadField.mapW) + " x " +
		                                                  std::to_string(loadField.mapH) + " plots, the journal's map " +
		                                                  std::to_string(j.W) + " x " + std::to_string(j.H) + " (+4)");
	LoadIn in;
	in.loadSeq = j.loadSeq;
	in.constants = j.constants;
	in.haveMapSeed = j.haveMapSeed;   // ticket 4912
	in.mapSeed = j.mapSeed;
	in.landWater = j.landWater;       // ticket 4914: what the live server's producer had
	in.riverBand = j.riverBand;       // ticket 0717: the river keep and land rule the live server's producer had
	in.neutralWonders = j.neutralWonders;   // ticket 0613: its first job is the load-time pass (replayed as any job)
	std::string why;
	const std::vector<std::string>& names = m_j.featureNames;
	if(!PlotTableFromFeed(j.baseline, j.W, j.H,
	                      [&names](int32_t id) { return id >= 0 && (size_t)id < names.size() ? names[id] : std::string(); }, in.table, why))
		return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: " + why);
	m_riverRule = RiverEdgeRuleText(in.table);   // ticket 0631, rule A: the player's log names the rule the journal gives
	in.samplesX = loadField.samplesX;
	in.samplesY = loadField.samplesY;
	in.heights = loadField.h;
	in.materials = loadField.mat;
	std::unique_ptr<RegionProducer> p = factory ? factory() : nullptr;
	if(!p)
		return c5r::Status::fail(c5r::Err::NotFound, "live terrain replay: no producer factory");
	C5R_TRY(p->create(in));
	m_p = std::move(p);
	return c5r::Status::Ok();
}

c5r::Status Replay::advanceTo(uint32_t gen, c5r::heightfield::HeightGrid& g, bool& changed, std::vector<ApplyStats>* written,
                              std::vector<Rect>* rects)
{
	changed = false;
	if(!m_p)
		return c5r::Status::fail(c5r::Err::NotFound, "live terrain replay: not initialised");
	if(gen > m_j.generations.size())
		return c5r::Status::fail(c5r::Err::NotFound, "live terrain replay: generation " + std::to_string(gen) + " is not in the journal (" +
		                                                 std::to_string(m_j.generations.size()) + ")");
	while(m_gen < gen)
	{
		if(m_nextJob >= m_j.jobs.size())
			return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: the jobs ran out before generation " + std::to_string(gen));
		const JournalJob& job = m_j.jobs[m_nextJob++];
		std::string why;
		if(!ApplyJournalJob(job, m_j.W, m_j.H, m_tab, why))
			return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: " + why);
		PlotTableIn t;
		if(!PlotTableFromFeed(m_tab, m_j.W, m_j.H, nullptr, t, why))
			return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: " + why);
		// the names: the journal's records carry feature ids only; the replay names them as the live process did
		for(size_t i = 0; i < t.plots.size(); ++i)
			t.plots[i].featureType =
			    m_tab[i].feature >= 0 && (size_t)m_tab[i].feature < m_j.featureNames.size() ? m_j.featureNames[m_tab[i].feature] : std::string();
		ChangeOut o;
		C5R_TRY(m_p->change(t, o));
		const Generation* listed = m_nextGen < m_j.generations.size() && m_j.generations[m_nextGen].job == job.id ? &m_j.generations[m_nextGen] : nullptr;
		if(listed && !o.live)
			return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: job " + std::to_string(job.id) + " made generation " +
			                                                  std::to_string(listed->gen) + " live, the replay's answer is " +
			                                                  WaitName(o.reason) + " (" + o.reasonText + ")");
		if(!listed)
			continue;   // a wait, or a live window the live scene did not write (an apply error): not written either
		ApplyStats st;
		if(!ApplyWindow(g, o.window, st, why))
			return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: generation " + std::to_string(listed->gen) + ": " + why);
		const std::string sha = RegionSha256(g, st.x0, st.y0, st.x1, st.y1);
		if(st.x0 != listed->x0 || st.y0 != listed->y0 || st.x1 != listed->x1 || st.y1 != listed->y1 || sha != listed->sha)
			return c5r::Status::fail(c5r::Err::BadLayout, "live terrain replay: generation " + std::to_string(listed->gen) +
			                                                  " MISMATCH: box " + std::to_string(st.x0) + "," + std::to_string(st.y0) + "-" +
			                                                  std::to_string(st.x1) + "," + std::to_string(st.y1) + " sha256 " + sha +
			                                                  ", recorded " + listed->sha);
		changed = true;
		if(written)
			written->push_back(st);
		if(rects)   // ticket 0587: the rectangles only
			for(const Rect& wr : o.window)
			{
				Rect r;
				r.x0 = wr.x0;
				r.y0 = wr.y0;
				r.w = wr.w;
				r.h = wr.h;
				rects->push_back(r);
			}
		m_gen = listed->gen;
		m_nextGen++;
	}
	return c5r::Status::Ok();
}

}  // namespace liveterrain
}  // namespace srvrender
