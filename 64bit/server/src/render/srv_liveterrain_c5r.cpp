// srv_liveterrain_c5r.cpp - the adapter from the region hook (srv_liveterrain.h) to 4903's producer, c5r::liveterrain
// (research/05-graphics-composition/live-terrain.md section 8.1; render/include/c5r/liveterrain/producer.h). Ticket 4907.
// Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later).
#include "srv_liveterrain.h"
#include "c5r/liveterrain/producer.h"
#include <mutex>

namespace srvrender {
namespace liveterrain {

namespace c5l = c5r::liveterrain;

namespace {

Wait FromReason(c5l::WaitReason r)
{
	switch(r)
	{
	case c5l::WaitReason::None: return Wait::None;
	case c5l::WaitReason::NoChange: return Wait::NoChange;
	case c5l::WaitReason::Water: return Wait::Water;
	case c5l::WaitReason::Lake: return Wait::Lake;
	case c5l::WaitReason::RiverEdge: return Wait::RiverEdge;
	case c5l::WaitReason::Marsh: return Wait::Marsh;
	case c5l::WaitReason::Seam: return Wait::Seam;
	case c5l::WaitReason::TooLarge: return Wait::TooLarge;
	}
	return Wait::Error;
}

void ToTable(const PlotTableIn& in, c5l::PlotTable& out)
{
	out.w = (int)in.W;
	out.h = (int)in.H;
	out.plots.resize(in.plots.size());
	for(size_t i = 0; i < in.plots.size(); ++i)
	{
		const PlotIn& p = in.plots[i];
		c5l::PlotInfo& q = out.plots[i];
		q.base.terrain = p.terrain;
		q.base.hills = p.plotType == 1;
		q.base.mountain = p.plotType == 0;
		q.base.feature = p.feature;
		q.base.naturalWonder = p.naturalWonder;
		q.base.featureType = p.featureType;
		q.continentArt = p.continent;
		q.riverEdges = p.riverEdges;
		q.lake = p.lake;
	}
}

// One library per factory, opened by the first create (on the worker) and shared by the later loads' producers.
struct LibraryHolder
{
	std::wstring path;
	std::mutex mu;
	std::shared_ptr<const c5l::PieceLibrary> lib;
	c5r::Status get(std::shared_ptr<const c5l::PieceLibrary>& out)
	{
		std::lock_guard<std::mutex> lk(mu);
		if(!lib)
		{
			std::shared_ptr<const c5l::PieceLibrary> l;
			C5R_TRY(c5l::PieceLibrary::open(path, l));
			lib = l;
		}
		out = lib;
		return c5r::Status::Ok();
	}
};

class C5rProducer : public RegionProducer
{
public:
	C5rProducer(std::shared_ptr<LibraryHolder> h, LandWaterEnabler lw) : m_h(std::move(h)), m_lw(std::move(lw)) {}
	bool landWaterOn() const override { return m_landWaterOn; }
	c5r::Status create(const LoadIn& in) override
	{
		C5R_TRY(m_h->get(m_lib));
		c5l::MapFrame frame;
		C5R_TRY(c5l::MapFrame::fromConstantsBlock(in.constants.data(), in.constants.size(), frame));
		if(frame.plotsW != (int)in.table.W || frame.plotsH != (int)in.table.H)
			return c5r::Status::fail(c5r::Err::BadLayout, "grid size in the constants block " + std::to_string(frame.plotsW) + " x " +
			                                                  std::to_string(frame.plotsH) + " plots, the plot table " +
			                                                  std::to_string(in.table.W) + " x " + std::to_string(in.table.H));
		c5l::PlotTable t;
		ToTable(in.table, t);
		c5l::TapView tap;
		tap.heights = in.heights.data();
		tap.materials = in.materials.empty() ? nullptr : in.materials.data();
		tap.samplesX = (int)in.samplesX;
		tap.samplesY = (int)in.samplesY;
		const uint64_t seed = c5l::deriveSeed(frame, t);
		m_haveMapSeed = in.haveMapSeed;   // ticket 4912
		m_landWaterWanted = in.landWater;   // MC #9538 item 4
		m_riverBand = in.riverBand;         // ticket 0717: the journal's mark on the player's side
		m_neutralPending = in.neutralWonders;   // ticket 0613: the next change is the load-time pass
		m_neutralOn = false;
		m_prod = in.table.plots;
		C5R_TRY(c5l::Producer::create(m_lib, frame, t, tap, seed, m_p));
		// Ticket 4914 (1): the land/water step (4913), with the load's map seed. A failure is the enabler's LOUD line; this
		// load's land/water changes then wait with LandWaterInputs (change below), every other change goes on as before.
		m_landWaterOn = false;
		m_landWaterWhy.clear();
		if(m_lw && in.landWater && in.haveMapSeed)
		{
			std::string why;
			m_landWaterOn = m_lw(m_p, in.mapSeed, why);
			if(!m_landWaterOn)
				m_landWaterWhy = why.empty() ? std::string("the land/water step could not be turned on") : why;
		}
		return c5r::Status::Ok();
	}
	c5r::Status change(const PlotTableIn& after, ChangeOut& out) override
	{
		// Ticket 4912 (the MC's #8552): without the load's map seed a plot that turns between land and water is never
		// drawn live (the coastline's jitter comes from the seed, 8.10: never a coast from seed 0); the producer is not
		// asked, so it keeps its own table, as for every wait
		// Ticket 4914 (1): the same wait, with its own reason, when the land/water step could not be turned on for this load
		// MC #9538 item 4: with the step not wanted (C5_RENDER_LIVE_LANDWATER=0) no seed is read, and the producer (without
		// enableLandWater) makes a land/water change wait with its own Water, as before epic 0615
		const bool lwFailed = !m_landWaterWhy.empty();
		if(((!m_haveMapSeed && m_landWaterWanted) || lwFailed) && after.plots.size() == m_prod.size())
		{
			out = ChangeOut();
			bool landWater = false;
			for(size_t i = 0; i < after.plots.size(); ++i)
			{
				const PlotIn& a = m_prod[i];
				const PlotIn& b = after.plots[i];
				landWater = landWater || (a.plotType == 3) != (b.plotType == 3);
				// every plot that differs from the producer's table is held, as the producer's own waits list them
				if(a.terrain != b.terrain || a.plotType != b.plotType || a.feature != b.feature || a.featureType != b.featureType ||
				   a.naturalWonder != b.naturalWonder || a.continent != b.continent || a.riverEdges != b.riverEdges || a.lake != b.lake)
					out.changedPlots.push_back(std::make_pair((int32_t)(i % after.W), (int32_t)(i / after.W)));
			}
			if(landWater)
			{
				out.live = false;
				if(!m_haveMapSeed)
				{
					out.reason = Wait::Water;
					out.reasonText = "no map seed for this load (ticket 4912): a land/water change is never drawn without it";
				}
				else
				{
					out.reason = Wait::LandWaterInputs;
					out.reasonText = "the land/water step is off for this load (ticket 4914): " + m_landWaterWhy;
				}
				return c5r::Status::Ok();
			}
		}
		c5l::PlotTable t;
		ToTable(after, t);
		c5l::ChangeResult r;
		// Ticket 0717: the river band takes its edges from the plot table: the plots' own edge bits since ticket 0631. A
		// table with records that carried no edges (a journal recorded before 0631: PlotTableFromFeed gave every plot with a
		// river on any edge all three bits) is read as the flag: an edge is a river edge when both plots beside it carry it.
		// A journal without the 0717 mark (LoadIn::riverBand false) replays the producer before 0717: the 4908 river disc
		// and the water keep for a land change (live-terrain-mountain-partial.md 4 item 4).
		c5l::Producer::Options opt;
		opt.riverEdgesFromAnyFlag = after.oldRuleRecords > 0;
		if(!m_riverBand)
		{
			opt.riverKeepDisc = true;
			opt.landRegion = false;
		}
		// Ticket 0613 (live-terrain.md 8.13.2 (b)): with FixUnknownWonderBase the first change after create is the load-time
		// pass (LiveTerrain posts the load's own table right behind the create; a newer table merged into it is that pass
		// too). Live or NoChange: the producer holds the neutral cells and every later change keeps the rule. Any wait: the
		// producer kept the EXE's cells, and the rule stays off for the rest of the load (a later change would otherwise
		// carry the unknown wonders' cells and wait with them).
		opt.neutralUnknownWonders = m_neutralPending || m_neutralOn;
		C5R_TRY(m_p.change(t, r, opt));
		if(m_neutralPending)
		{
			m_neutralPending = false;
			m_neutralOn = r.live || r.reason == c5l::WaitReason::NoChange;
		}
		if(r.live)   // the producer took this table
			m_prod = after.plots;
		out = ChangeOut();
		out.live = r.live;
		out.reason = r.live ? Wait::None : FromReason(r.reason);
		out.reasonText = r.reasonText;
		out.changedPlots.reserve(r.changedPlots.size());
		for(const std::pair<int, int>& p : r.changedPlots)
			out.changedPlots.push_back(std::make_pair((int32_t)p.first, (int32_t)p.second));
		out.window.resize(r.window.size());
		for(size_t k = 0; k < r.window.size(); ++k)
		{
			if(r.window[k].x0 < 0 || r.window[k].y0 < 0 || r.window[k].w <= 0 || r.window[k].h <= 0)
				return c5r::Status::fail(c5r::Err::BadLayout, "window rectangle " + std::to_string(k) + " with a negative origin or no size");
			out.window[k].x0 = (uint32_t)r.window[k].x0;
			out.window[k].y0 = (uint32_t)r.window[k].y0;
			out.window[k].w = (uint32_t)r.window[k].w;
			out.window[k].h = (uint32_t)r.window[k].h;
			out.window[k].heights = std::move(r.window[k].heights);
			out.window[k].materials = std::move(r.window[k].materials);
		}
		if(r.samplesOutside)   // 8.1: always 0 (the producer returns an error instead); checked again here
			return c5r::Status::fail(c5r::Err::BadLayout, std::to_string(r.samplesOutside) + " samples changed outside the window");
		out.inD = r.samplesD;
		out.inW1 = r.samplesFull;
		out.inRing = r.samplesRing;
		out.producerMs = r.ms;
		out.coastDegenerate = r.coastDegenerate;   // MC #9538 item 1
		out.stripTrianglesRefused = r.stripTrianglesRefused;
		out.landWater = r.landWater;   // MC #9538 item 2
		out.coastLinesRedrawn = r.coastLinesRedrawn;
		out.coastLinesBeyondWindow = r.coastLinesBeyondWindow;
		out.coastLinesReloadMoves = r.coastLinesReloadMoves;
		out.samplesWonderKept = r.samplesWonderKept;
		return c5r::Status::Ok();
	}

private:
	std::shared_ptr<LibraryHolder> m_h;
	std::shared_ptr<const c5l::PieceLibrary> m_lib;
	c5l::Producer m_p;
	bool m_haveMapSeed = false;    // ticket 4912
	bool m_landWaterWanted = true; // MC #9538 item 4: LoadIn::landWater
	bool m_riverBand = true;       // ticket 0717: LoadIn::riverBand
	bool m_neutralPending = false; // ticket 0613: LoadIn::neutralWonders, until the load-time pass has run
	bool m_neutralOn = false;      // ticket 0613: the pass was live or found nothing: the neutral base for the whole load
	std::vector<PlotIn> m_prod;    // the producer's table (the load's, then each live change's)
	LandWaterEnabler m_lw;         // ticket 4914 (1); empty: land/water waits with the producer's Water, as before
	bool m_landWaterOn = false;
	std::string m_landWaterWhy;    // non-empty: the step was wanted and could not be turned on for this load
};

}  // namespace

ProducerFactory MakeC5rProducerFactory(const std::wstring& libraryPath, LandWaterEnabler landWater)
{
	std::shared_ptr<LibraryHolder> h = std::make_shared<LibraryHolder>();
	h->path = libraryPath;
	return [h, landWater]() { return std::unique_ptr<RegionProducer>(new C5rProducer(h, landWater)); };
}

}  // namespace liveterrain
}  // namespace srvrender
