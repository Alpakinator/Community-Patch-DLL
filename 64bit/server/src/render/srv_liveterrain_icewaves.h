// srv_liveterrain_icewaves.h - the EXE's ice and coast waves after a live terrain change: the server's side (epic 0465,
// ticket 0586). Written 2026-11-19 for this repository (GPL-3.0-or-later). The proxy's side and the section's layout:
// proxy/src/proxy_livereload.h, proxy/include/vp_livereload.h. Findings: research/05-graphics-composition/live-terrain.md
// section 10.
//
// The server draws the EXE's own ice mesh and coast-wave chunks, copied by the composite's taps once per load (the ice
// again whenever its handles change, the water when the server raises requestSeq). Both are built by the EXE from the
// DLL's plots: the ice from a per-plot grid filled at load with the plots whose feature is 0 (FEATURE_ICE), the waves
// from every plot's terrain (5 coast, 6 ocean) (live-terrain.md 4.1, polar-ice.md 3). A live change of those plots does
// not reach them. This module decides, from the DLL's plot table, what the EXE should rebuild and asks the proxy:
//
//   * ice: the grid the EXE should hold (one bit per plot) and the number of plots that differ from what the EXE holds
//     now (expectFlips; the proxy refuses when it counts others); the proxy writes the bits and calls Ice_Reload();
//   * waves: Waves_Reload() when the plots' wave classes (none / coast / ocean) differ from those the EXE's waves were
//     built from; after it the scene raises the water tap's requestSeq.
//
// The gate (MC #7251, 2026-11-19): a plot whose field change waits for the reload (the field the server draws does not
// show the DLL's terrain, plot type or wonder of it: FieldRelevantChange between the table the field was last made from
// and the DLL's) keeps the ice the EXE has, and while any plot whose wave class changed is such a plot, the waves wait
// too. So the ice and the surf never disagree with the coast the player sees; when the field goes live (option B) or at
// the reload they follow. Today (4908's option A) every land/water change and every change of a water plot's terrain
// waits in the field, so the waves follow in practice only at the reload; an ice feature added or removed on water is
// not a field change and goes live at once.
//
// IceWaves is the logic only (no IPC, no thread); LiveReloadLink is the section. Scene::liveTerrainStep drives both
// (srv_liveterrain_scene.cpp). Render thread only.
#pragma once
#include "feed/srv_feed.h"
#include <stdint.h>
#include <string>
#include <vector>

namespace srvrender {
namespace liveterrain {

// The EXE's rules (live-terrain.md 4.1 [C], 4.8 [L]): ice where the feature id is 0; waves from terrain 5 and 6.
const int kFeatureIce = 0;
const int kTerrainCoast = 5, kTerrainOcean = 6;
inline uint8_t WaveClass(const SrvFeed::PlotRecord& r)
{
	return r.terrain == kTerrainCoast ? 1 : r.terrain == kTerrainOcean ? 2 : 0;
}
inline bool IcePlot(const SrvFeed::PlotRecord& r)
{
	return r.feature == kFeatureIce;
}

struct IceWavesRequest
{
	bool ice = false, waves = false;
	uint32_t W = 0, H = 0;
	std::vector<uint8_t> grid;     // per plot (y * W + x): 1 = the EXE's grid should hold ice
	uint32_t iceCount = 0;         // plots with ice in `grid`
	uint32_t expectFlips = 0;      // plots whose bit differs from what the EXE holds now
	uint32_t wavePlots = 0;        // plots whose wave class changed since the EXE's waves were built
	std::vector<uint8_t> waveClasses;   // per plot, the classes Waves_Reload will read (WaveClass of the DLL's table)
};

struct IceWavesAnswer
{
	int32_t iceResult = 1, wavesResult = 1;   // vplr::kOk 0, kNotAsked 1, errors < 0
	uint32_t bitsSet = 0, bitsCleared = 0;
	uint32_t iceMicros = 0, wavesMicros = 0;
	std::string why;
};

struct IceWavesCounters
{
	int64_t requests = 0, iceOk = 0, wavesOk = 0, failures = 0;
	int64_t iceHeldPlots = 0, wavesHeld = 0;   // decisions held by the gate (plots / times)
	int64_t timeouts = 0;
	int64_t iceRefreshes = 0;   // field windows written beside the EXE's ice (each owes one Ice_Reload) (4914)
};

class IceWaves
{
public:
	// A new load: the table the EXE built its ice grid and waves from (the bake's table of the field's load) is both
	// what the EXE holds and what the field shows.
	void onLoad(uint32_t loadSeq, uint32_t W, uint32_t H, const std::vector<SrvFeed::PlotRecord>& baseline);
	bool haveLoad() const { return m_have; }
	uint32_t loadSeq() const { return m_load; }
	// A table the server's field now shows (a live change written, or a change the producer found nothing to draw for).
	void onDrawn(const std::vector<SrvFeed::PlotRecord>& sent);
	// Ticket 4914 (live-terrain.md 10.7's open item): the same, with the sample rectangles the live window wrote into the
	// field (sample (c, r) of the 64 W x 64 (H + 4) field; plot (x, y) covers columns 64 x .. 64 x + 63, rows
	// 64 (y + 2) .. 64 (y + 2) + 63, odd rows half a plot over). The EXE's ice builder reads the field around its ice hexes
	// (the water height and the heights it displaces against), so when a written rectangle reaches within kIceFieldRing
	// plots of a plot the EXE holds as ice, the next request asks Ice_Reload even with no bit to flip (expectFlips 0).
	struct SampleBox
	{
		uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
	};
	static const uint32_t kIceFieldRing = 1;
	void onDrawn(const std::vector<SrvFeed::PlotRecord>& sent, const std::vector<SampleBox>& written);
	// MC #9538 item 4 (live-terrain.md 10.9): the refresh above belongs to the land/water step (epic 0615). Off (the scene's
	// C5_RENDER_LIVE_LANDWATER=0), onDrawn(sent, written) is onDrawn(sent): a height-only window owes no Ice_Reload, as
	// before 0615. On by default; kept across loads.
	void setFieldRefresh(bool on) { m_fieldRefresh = on; }
	bool fieldRefresh() const { return m_fieldRefresh; }
	// A field change beside the EXE's ice is waiting for its Ice_Reload (tests).
	bool iceRefreshPending() const { return m_refreshSeq != m_refreshDone; }
	// The DLL's current table. True: `req` is what to ask now (only while no request is in flight and the kind has not
	// failed in this load). `note`: a line when the gate holds something it did not hold before (else empty).
	bool want(const std::vector<SrvFeed::PlotRecord>& cur, IceWavesRequest& req, std::string& note);
	// The request went out (want's req).
	void posted(const IceWavesRequest& req);
	bool inFlight() const { return m_inFlight; }
	// The proxy's answer to the posted request. Returns a line for the log; `loud`: a failure (D118).
	std::string answered(const IceWavesAnswer& a, bool& loud);
	// No answer for a long time: counted; the request stays in flight (the proxy answers every request it reads).
	void noteTimeout() { ++m_c.timeouts; }
	const IceWavesCounters& counters() const { return m_c; }
	// TEST ONLY (C5_RENDER_LIVE_ICEWAVES_TEST_UNGATED=1, the live check of ticket 0586): the waves follow the DLL's table
	// at once, also while their plots wait in the field. Never set by the product.
	void setWavesGateForTest(bool on) { m_wavesGate = on; }
	// What the EXE holds now (tests).
	const std::vector<uint8_t>& exeIce() const { return m_exeIce; }
	const std::vector<uint8_t>& exeWaves() const { return m_exeWaves; }

private:
	bool m_have = false;
	uint32_t m_load = 0, m_W = 0, m_H = 0;
	std::vector<SrvFeed::PlotRecord> m_drawn;   // the table the field was last made from
	std::vector<uint8_t> m_exeIce;              // the EXE's ice grid as far as the answers say
	std::vector<uint8_t> m_exeWaves;            // the wave classes the EXE's waves were built from
	bool m_inFlight = false;
	IceWavesRequest m_req;                      // the request in flight
	bool m_iceFailed = false, m_wavesFailed = false;   // a failed kind is not asked again in this load
	bool m_iceHeldNoted = false, m_wavesHeldNoted = false;
	bool m_wavesGate = true;
	bool m_fieldRefresh = true;   // MC #9538 item 4
	// 4914: windows written beside the EXE's ice (counted), the last one an answered Ice_Reload covered, and the one the
	// request in flight covers. An Ice_Reload is owed while m_refreshSeq != m_refreshDone.
	uint32_t m_refreshSeq = 0, m_refreshDone = 0, m_reqRefreshSeq = 0;
	IceWavesCounters m_c;
};

// ---- The section (vp_livereload.h), the server's side ----
class LiveReloadLink
{
public:
	~LiveReloadLink() { close(); }
	// Makes (or opens again, after a server restart in the same EXE) Local\VP64LiveReload.<exePid>. False (why) on error.
	bool open(uint32_t exePid, std::string& why);
	void close();
	bool isOpen() const { return m_sec != nullptr; }
	uint32_t exePid() const { return m_pid; }
	// Posts the request (false when one is still unanswered or the request does not fit the section).
	bool post(const IceWavesRequest& r, uint32_t loadSeq, std::string& why);
	// True once, when the posted request was answered; `a` filled.
	bool poll(IceWavesAnswer& a);
	bool waiting() const { return m_posted != 0; }

private:
	void* m_map = nullptr;
	void* m_sec = nullptr;
	uint32_t m_pid = 0;
	uint32_t m_posted = 0;   // the reqSeq in flight (0: none)
};

}  // namespace liveterrain
}  // namespace srvrender
