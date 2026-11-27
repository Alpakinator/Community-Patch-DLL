// c5r/liveterrain/producer.h - TEST STUB of the region producer's interface (ticket 4907, epic 0465).
//
// Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later); brought in line with 4903's header
// (render/include/c5r/liveterrain/producer.h at b6cf09570) on 2026-11-19: the same names and types for everything the
// server hook uses (server/src/render/srv_liveterrain_c5r.cpp), so the adapter compiles unchanged against either. It is NOT
// the producer: the "own" field it returns is a flat height per plot kind over the changed plots' 64 x 64 boxes, enough
// for the hook's tests (the window's shape, the wait reasons, the errors, the threading). The hook's tests put this
// folder first on the include path; server.exe and the suites use the real header. Anything here the real header does
// not have is marked STUB.
#pragma once
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"
#include <stdint.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace c5r {
namespace liveterrain {

const int kMaxPlotsSide = 256;
const int kRingSamples = 64;
const int kMaxWindowPlots = 256;

class PieceLibrary
{
public:
	// STUB: reads the file's first u32 only (20, else BadVersion; a missing file Io).
	static Status open(const std::wstring& path, std::shared_ptr<const PieceLibrary>& out);
	uint32_t version() const { return m_version; }   // STUB

private:
	uint32_t m_version = 0;
};

struct MapFrame
{
	int plotsW = 0, plotsH = 0;
	bool wrapX = false;
	float h2w[16] = {};
	float corner[4][4] = {};
	int32_t G[16] = {}, Ginv[16] = {};
	int gridW() const { return plotsW; }
	int gridH() const { return plotsH + 4; }
	int samplesX() const { return plotsW * 64; }
	int samplesY() const { return (plotsH + 4) * 64; }
	// STUB: wrap from 0xD90FBC, the grid size from 0xD91264 / 0xD91268 (W, H + 4), as the real one names them.
	static Status fromConstantsBlock(const uint8_t* p, size_t n, MapFrame& out);
};

struct PlotInfo
{
	heightfield::PlotInfo base;
	int continentArt = 0;
	uint8_t riverEdges = 0;
	bool lake = false;
};
struct PlotTable
{
	int w = 0, h = 0;
	std::vector<PlotInfo> plots;
};

struct TapView
{
	const uint16_t* heights = nullptr;
	const uint8_t* materials = nullptr;
	int samplesX = 0, samplesY = 0;
};

uint64_t deriveSeed(const MapFrame& f, const PlotTable& atLoad);

enum class WaitReason { None = 0, NoChange, Water, Lake, RiverEdge, Marsh, Seam, TooLarge };
const char* waitReasonName(WaitReason r);

struct WindowRect
{
	int x0 = 0, y0 = 0, w = 0, h = 0;
	std::vector<uint16_t> heights;
	std::vector<uint8_t> materials;
};

struct ChangeResult
{
	bool live = false;
	WaitReason reason = WaitReason::None;
	std::string reasonText;
	std::vector<std::pair<int, int>> changedPlots;
	std::vector<WindowRect> window;
	uint64_t samplesD = 0, samplesFull = 0, samplesRing = 0, samplesOutside = 0;
	uint32_t removedPlacements = 0, addedPlacements = 0;
	double ms = 0;
	// as the real header (4913, MC #9538 items 1 and 2); the stub fills them from s_landWaterCounts
	bool landWater = false;
	uint32_t coastLinesRedrawn = 0, coastLinesBeyondWindow = 0, coastLinesReloadMoves = 0;
	uint64_t samplesWonderKept = 0;
	uint32_t coastDegenerate = 0;
	uint64_t stripTrianglesRefused = 0;
};

class Producer
{
public:
	static Status create(std::shared_ptr<const PieceLibrary> lib, const MapFrame& frame, const PlotTable& atLoad,
	                     const TapView& tap, uint64_t seed, Producer& out);
	Status change(const PlotTable& after, ChangeResult& out);
	// as the real header (ticket 0717): the options the server passes (only the member server.exe sets); the stub records
	// them in s_lastOptions and then runs the change above
	struct Options
	{
		bool riverEdgesFromAnyFlag = false;
		bool riverKeepDisc = false;   // ticket 0717: the real header's (the producer before 0717 with landRegion false)
		bool landRegion = true;
		// ticket 0613: the real header's. STUB rule: a natural wonder whose featureType starts with "UNKNOWN_" is unknown to
		// the EXE; the stock cell draws it as the stub's Fuji (own height 40000), the neutral one as its plot
		bool neutralUnknownWonders = false;
	};
	Status change(const PlotTable& after, ChangeResult& out, const Options& opt);

	// ---- STUB test seams ----
	static Options s_lastOptions;   // STUB (0717): the options of the last change(after, out, opt)
	static int s_createDelayMs;
	static int s_changeDelayMs;
	static Err s_failCreate;
	static Err s_failChange;
	// STUB (MC #9538 items 1 and 2): every change's result carries these land/water counts (the default: all zero)
	static ChangeResult s_landWaterCounts;

private:
	MapFrame m_frame;
	PlotTable m_cur;
	std::vector<uint16_t> m_h;
	std::vector<uint8_t> m_m;
	int m_sx = 0, m_sy = 0;
	bool m_ok = false;
	bool m_neutral = false;       // STUB (0613): the cells m_cur stands for were built with the neutral rule
	bool m_neutralNext = false;   // STUB (0613): the rule of the change being made
};

}  // namespace liveterrain
}  // namespace c5r
