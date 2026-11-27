// srv_units_feedport.h - the unit scene's FeedPort over a scene-bundle FeedSource (epic 0375, issue 4457; decision 11 of
// research/05-graphics-composition/unit-server-scene.md). Written 2026-11-16 for this repository (GPL-3.0-or-later).
//
// The unit scene (srv_units_core.h UnitsCore) reads its tables through a FeedPort; the host drains through a FeedSource
// (srv_scene_sources.h: LiveFeed, the recording feed of render_test_record, the file feed of the offline player). With
// this port the scene's CopyUnits / CopyPlayers go through the same source as Loop's drains, so a recording holds the
// tables the scene really took (feed.bin's records 3 UNITS and 6 PLAYERS of caller 1). Header only (no new source in the
// build lists). The host's wiring (MakeSceneUnitsWith(o, port, ...) with this port over h.feed instead of LiveFeedPort())
// is srv_units_scene.cpp's and the host's, named for its owner in issue 4457's hand-back.
#pragma once
#include "srv_units_core.h"
#include "render/srv_scene_sources.h"

namespace srvunits {

class FeedSourcePort : public FeedPort
{
public:
	explicit FeedSourcePort(srvrender::FeedSource& src) : m_src(src) {}
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& unitTableGen) override { m_src.copyUnits(out, unitTableGen); }
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override { m_src.copyPlayers(out); }
	// the database copy is not a feed record (the bundle names the database on the command line): the live one
	bool unitDatabase(std::wstring& path, uint32_t& gen) override { return LiveFeedPort().unitDatabase(path, gen); }
private:
	srvrender::FeedSource& m_src;
};

}  // namespace srvunits
