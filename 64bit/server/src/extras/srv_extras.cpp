// srv_extras.cpp - the scene's extras module (ticket 4938). See srv_extras.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_extras.h"
#include <stdio.h>
#include "feed/srv_feed.h"
#include "srv_extra_roads.h"
#include "srv_extra_visible_outline.h"
#include "vp64_options.h"

namespace srvextras {

namespace {

class Extras : public SceneExtras
{
public:
	Extras() : m_outline(&Extras::Forward, this) {}

	void setLog(ExtrasLogFn log, void* ctx) override
	{
		m_log = log;
		m_logCtx = ctx;
	}

	void configure(const Vp64Opt::Options& o, bool fogConfigured) override
	{
		// the options' start log already names every extra's state and why (srv_options.cpp LogExtras); this module adds a
		// line only for what the options cannot know (the fog switched off for this server, an input missing at run time)
		// the ids read here are the list of srvextras::SceneExtrasWanted (srv_render_scene.cpp): keep the two in step
		const Vp64Opt::ExtraState s = Vp64Opt::Extra(o, Vp64Opt::kVisibleAreaOutline);
		m_outline.configure(s.on, nullptr, fogConfigured);
	}

	bool anyOn() const override { return m_outline.on(); }

	c5r::Status initDevice(ID3D11Device* dev) override { return m_outline.initDevice(dev); }

	void releaseDevice() override { m_outline.releaseDevice(); }

	void onBatch(const SrvFeed::Batch& b) override
	{
		// VisibleAreaOutline reads the fog scene's states, which took this batch's kind-3 events already; an extra that
		// reads events of its own takes them here
		m_batches++;
		m_events += (int64_t)b.events.size();
	}

	void draw(ID3D11DeviceContext* ctx, const ExtrasFrame& f) override
	{
		if(!m_outline.on())
			return;
		OutlineFrame of;
		of.grid = f.grid;
		of.states = f.fowStates;
		of.w = f.fowW;
		of.h = f.fowH;
		of.fogGeneration = f.fowGeneration;
		of.rtv = f.rtv;
		of.dsv = f.dsv;
		of.width = f.width;
		of.height = f.height;
		for(int i = 0; i < 16; ++i)
			of.viewProj[i] = f.viewProj[i];
		m_outline.frame(ctx, of);
	}

	void totalsLine(char* out, size_t n) const override { m_outline.totalsLine(out, n); }

	VisibleOutline& outline() { return m_outline; }

private:
	static void Forward(void* ctx, const char* line)
	{
		Extras* self = static_cast<Extras*>(ctx);
		if(self->m_log)
			self->m_log(self->m_logCtx, line);
	}
	ExtrasLogFn m_log = nullptr;
	void* m_logCtx = nullptr;
	VisibleOutline m_outline;
	int64_t m_batches = 0, m_events = 0;
};

std::unique_ptr<SceneExtras> MakeImpl() { return std::unique_ptr<SceneExtras>(new Extras()); }
// ticket 4934: FixRoadLayout's producer (srv_extra_roads.h)
std::unique_ptr<SceneRoads> MakeRoadsImpl() { return std::unique_ptr<SceneRoads>(new RoadLayoutProducer()); }

// The registry entries: a program that links this file makes the module (srv_render_scene.cpp holds the registry)
struct Registrar
{
	Registrar()
	{
		RegisterSceneExtrasFactory(&MakeImpl);
		RegisterSceneRoadsFactory(&MakeRoadsImpl);
	}
} s_registrar;

}  // namespace

std::unique_ptr<SceneExtras> MakeSceneExtrasImpl() { return MakeImpl(); }
std::unique_ptr<SceneRoads> MakeSceneRoadsImpl() { return MakeRoadsImpl(); }

}  // namespace srvextras
