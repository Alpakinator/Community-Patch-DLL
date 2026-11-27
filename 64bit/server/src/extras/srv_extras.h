// srv_extras.h - the scene's extras module: the [Extras] options that the server's renderer draws (epic 0454; the
// contribution guide's worked example, ticket 4938, is its first extra: VisibleAreaOutline).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scene (server/src/render/srv_render_scene.h) owns at most one SceneExtras, made through MakeSceneExtras(): a program
// that does not link server/src/extras/ gets null and draws as before (the factory pattern of the model module). The
// scene calls, all on the render thread:
//   configure(options, fogConfigured)  once, before the first frame: each extra's effective state (Vp64Opt::Extra), one
//                                      start line per extra that will not run
//   initDevice / releaseDevice         with the scene's device
//   onBatch(batch)                     every drained feed batch, after the fog scene took it
//   draw(ctx, frame)                   once per frame at the extras' place: after the terrain, the water, the ice and the
//                                      ranged-attack arc, before the trees, the cities and the units
//   totalsLine                         the shutdown summary
// Display only: an extra reads the feed and the scene's own state; it never calls the DLL (no DLL header is on the render
// units' include path) and writes only its own draw into the scene's colour target.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "c5r/decal/decal.h"
#include "c5r/forest/cut.h"
#include "c5r/fow/fow.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
namespace SrvFeed { struct Batch; struct PlotRecord; }
namespace Vp64Opt { struct Options; }

namespace srvextras {

// What the scene hands the extras for one frame.
struct ExtrasFrame
{
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	uint32_t width = 0, height = 0;
	float viewProj[16] = {};                              // row vectors: clip = (x, y, z, 1) viewProj (view 0 x projection)
	const c5r::heightfield::HeightGrid* grid = nullptr;   // the scene's height field; null before the first one
	// the fog of war (FowScene): the active team's per-plot states, null while the fog is not running
	const c5r::fow::PlotState* fowStates = nullptr;
	uint32_t fowW = 0, fowH = 0, fowGeneration = 0;
	int64_t frameId = 0;
};

typedef void (*ExtrasLogFn)(void* ctx, const char* line);

class SceneExtras
{
public:
	virtual ~SceneExtras() {}
	// log: every line the extras write (the scene passes its render log); null = none
	virtual void setLog(ExtrasLogFn log, void* ctx) = 0;
	virtual void configure(const Vp64Opt::Options& o, bool fogConfigured) = 0;
	virtual bool anyOn() const = 0;   // an extra in effect and not failed: the scene skips the module otherwise
	virtual c5r::Status initDevice(ID3D11Device* dev) = 0;
	virtual void releaseDevice() = 0;
	virtual void onBatch(const SrvFeed::Batch& b) = 0;
	virtual void draw(ID3D11DeviceContext* ctx, const ExtrasFrame& f) = 0;
	virtual void totalsLine(char* out, size_t n) const = 0;
};

typedef std::unique_ptr<SceneExtras> (*SceneExtrasFactory)();
void RegisterSceneExtrasFactory(SceneExtrasFactory f);   // srv_render_scene.cpp (always linked with the scene)
std::unique_ptr<SceneExtras> MakeSceneExtras();          // null without server/src/extras/
// Whether an extra this module draws is in effect (Vp64Opt::Extra of each id Extras::configure reads; today
// VisibleAreaOutline). The scene makes the module only then. The unit extras (server/src/extras/units/) and FixRoadLayout
// (SceneRoads below) are not counted: their modules decide for themselves. Defined in srv_render_scene.cpp beside the
// registry, so a program without this folder has it; a new scene extra adds its id there (mc-red, 2026-11-22).
bool SceneExtrasWanted(const Vp64Opt::Options& o);
// The module itself, without the registry (tests, and the registrar in srv_extras.cpp)
std::unique_ptr<SceneExtras> MakeSceneExtrasImpl();

// ---- RoadLayout (ticket 4934 as FixRoadLayout, ticket 4940; docs/extras/roads.md 4b, 4g): the road and railroad layout ----
// Not a drawing extra: a decal producer the scene composes into the decal tap's set, before the farm's and the forest's
// (roads -> farm -> forest), on the farm producer's pattern (server/src/render/srv_farmfields.h). The scene makes one
// through MakeSceneRoads() when render_roadlayout is 1 or 2 (RoadLayout in effect, or the bundle's record); a program that
// does not link server/src/extras/ gets null and draws the tap's roads as before.
//
// Ticket 4940: which layout the producer lays out. The scene passes render_roadlayout on as "on" only (srv_render_scene.cpp
// is not changed), so the two programs that parse the configuration put the whole choice here before the scene makes the
// producer: server.exe's ParseHostConfig (srv_render_host.cpp: RoadLayout, RoadJunction and RoadJitter in effect, or
// server.cfg's render_roadlayout, render_roadlayout_junction, render_roadlayout_jitter) and the scene player (sceneshot:
// the bundle's render_config, or --option). The producer reads it once, when it is made (RoadLayoutProducer's
// constructor); a later change of the slot changes no producer that exists. Header only (an inline function's static: one
// slot per program), so a program without server/src/extras/ needs no symbol from it.
struct RoadLayoutChoice
{
	int layout = 1;     // 1: the tidy layout, variant (e) (the ring rule); 2: all connections (every neighbouring pair joined)
	int junction = 0;   // layout 2 only: a plot centre with 4-6 links: 0 through, 1 cross, 2 ring, 3 rosette (roads::Junction)
	int jitter = 0;     // layout 2 only: each plot's pass point moved by a fixed per-plot offset of up to this (0..7 iso samples)
};
inline RoadLayoutChoice& RoadLayoutChoiceSlot()
{
	static RoadLayoutChoice c;
	return c;
}
// The ranges the parsers check (layout 1..2, junction 0..3, jitter 0..7); false with why naming the first value out of range.
inline bool ValidRoadLayoutChoice(const RoadLayoutChoice& c, std::string& why)
{
	if(c.layout < 1 || c.layout > 2)
		why = "layout " + std::to_string(c.layout) + " is not 1 or 2";
	else if(c.junction < 0 || c.junction > 3)
		why = "junction " + std::to_string(c.junction) + " is not 0..3";
	else if(c.jitter < 0 || c.jitter > 7)
		why = "jitter " + std::to_string(c.jitter) + " is not 0..7";
	else
		return true;
	return false;
}
// A change of the roads' decals: the rectangles of decals added or removed and the area tiles they touch, in a generation
// band of its own (kRoadDecalGenBase + n); the scene turns it into its DecalChange.
const uint32_t kRoadDecalGenBase = 0xD0000000u;   // below the farm's 0xE0000000 and the forest's 0xF0000000
struct RoadsChange
{
	uint32_t generation = 0, previousGeneration = 0;
	float worldRect[4] = {0, 0, 0, 0};
	std::vector<c5r::decal::DecalRect> rects;
	std::vector<c5r::decal::TileOrigin> areaTiles;
};
// The plot table (the scene's MakeFeedPlotTable, or a recording's): a copy of every record, its generation and size.
typedef std::function<void(std::vector<SrvFeed::PlotRecord>& out, uint32_t& generation, uint32_t& width, uint32_t& height)>
    RoadsTableFn;
struct SceneRoadsOptions
{
	bool threaded = true;          // produce on a worker thread (never on the render thread, as the farm's); false: in refresh()
	bool removeTapRoutes = true;   // drop the tap's route curves (the EXE keeps laying out its own)
	float footprintMargin = 3.75f; // our cut footprint: the strip's half width + this (iso samples): about the EXE's 12-sample
	                               // band (trees.md 14.1, P1)
	float stockBand = 3.75f;       // the EXE's route cells to clear: the tap's route-curve triangles widened by this (iso samples)
};
class SceneRoads
{
public:
	virtual ~SceneRoads() {}
	virtual void setLog(ExtrasLogFn log, void* ctx) = 0;
	virtual void setOptions(const SceneRoadsOptions& o) = 0;
	virtual void setTable(RoadsTableFn fn) = 0;   // null: no table (no roads of our own; the tap's set without route curves)
	// The field of a load: the decals' world rectangle (0, 0, samples - 1), the plot grid's wrap and the cut grid's frame (the
	// height field's ox / oy). A new loadSeq drops the roads in force and reads the table again.
	virtual void setWorld(const float worldRect[4], uint32_t loadSeq, bool wrapX, float cutOx, float cutOy) = 0;
	virtual bool onBatch(const SrvFeed::Batch& b) = 0;   // true when the route bands changed (a production follows)
	virtual bool refresh() = 0;   // every loop iteration: a finished production put in force (true: the decals changed)
	virtual bool settle(double ms) = 0;   // tests and the player: wait for the worker, then refresh()
	// The tap's set with its route curves dropped and ours added; on any refusal, or before the first production, the tap's
	// set without its route curves when one is in force, else as it is (LOUD, counted).
	virtual std::shared_ptr<const c5r::decal::DecalSet> composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap) = 0;
	virtual bool takeDecalChanges(std::vector<RoadsChange>& out) = 0;
	// The trees' cut (MC #7664, option (A)): our pieces' cells (bit 0x02, ORed into the EXE's grid) and the EXE's route cells
	// to clear (bit 0x02 where the tap's own route curves ran, on plots with no improvement and no city). null before the
	// first production / tap set. cutVersion() moves on whenever either changes.
	virtual std::shared_ptr<const c5r::forest::CutSource> footprint() const = 0;
	virtual std::shared_ptr<const c5r::forest::CutSource> stockClear() const = 0;
	virtual uint64_t cutVersion() const = 0;
	virtual uint64_t version() const = 0;   // moves on with every set of road decals put in force (and every new load)
	// The scene bundle: render_roadlayout_state (the input in force, one line "rl1 ..."), and the player's restore of it.
	virtual bool spanState(std::string& text, std::string& why) const = 0;
	virtual bool restoreSpanInput(const std::string& text, std::string& why) = 0;
	virtual void totalsLine(char* out, size_t n) const = 0;
};
// ---- RoadLayout's bridges (ticket 0808, docs/extras/roads.md 4h) ----
// With RoadLayout in force the EXE still draws its own CPU-built road and rail bridges, bent along ITS route curves of the
// session (landmark-renderer.md 8, city-renderer.md 13.11), while the scene draws our roads: a deck lies off our road or
// across it. The roads producer publishes a placer for the production in force (and clears it at a new load, before its
// first production and when it is destroyed); the model layer (srv_model_layer_c5r.cpp) asks it for every CPU-built bridge
// draw of a tap: Move (the mesh turned about z and moved onto our crossing of the same hex edge: new instance rows), Drop
// (our layout does not join the edge's plots) or Keep (not at a hex edge, or a mesh it cannot fit). Header only (one slot
// per program, as RoadLayoutChoiceSlot): a program without server/src/extras/ never fills it, and with RoadLayout 0 no
// producer exists, so the bridges are drawn exactly as tapped.
enum class BridgeMove : int { Keep = 0, Move = 1, Drop = 2 };
struct BridgePlaceResult
{
	BridgeMove move = BridgeMove::Keep;
	bool fitFailed = false;   // the vertex buffer gave no bridge axis (kept as is)
	uint64_t edgeKey = 0;     // the hex edge (plot * 6 + dir + 1 of its lower key; 0: none)
	float edgeDist = 0, shift = 0, turnDeg = 0;   // iso samples from the edge's middle, moved, turned about z (degrees)
	float rows[16] = {};      // Move: the new instance rows (row vectors, row 3 the origin)
};
class BridgePlacer
{
public:
	virtual ~BridgePlacer() {}
	// rowsIn: the draw's four instance rows as tapped; vb: its held vertex buffer (stride 20 or 32)
	virtual BridgePlaceResult place(const float rowsIn[16], const uint8_t* vb, size_t bytes, uint32_t stride) const = 0;
	virtual uint64_t generation() const = 0;   // moves on with every placer published (a new production or load)
};
struct BridgePlacerSlotData
{
	std::mutex mx;
	std::shared_ptr<const BridgePlacer> placer;
};
inline BridgePlacerSlotData& BridgePlacerSlotRaw()
{
	static BridgePlacerSlotData s;
	return s;
}
inline void SetBridgePlacer(std::shared_ptr<const BridgePlacer> p)
{
	BridgePlacerSlotData& s = BridgePlacerSlotRaw();
	std::lock_guard<std::mutex> lk(s.mx);
	s.placer = std::move(p);
}
inline std::shared_ptr<const BridgePlacer> GetBridgePlacer()
{
	BridgePlacerSlotData& s = BridgePlacerSlotRaw();
	std::lock_guard<std::mutex> lk(s.mx);
	return s.placer;
}
// A CPU-built draw is a road or rail bridge when its first texture is the bridge's (Bridges.gr2's material, by its fixed
// name; landmark-renderer.md 8.1), any case.
inline bool IsBridgeTexture(const std::string& t0)
{
	static const char kName[] = "bridges_diff.dds";
	if(t0.size() != sizeof(kName) - 1)
		return false;
	for(size_t i = 0; i < t0.size(); ++i)
	{
		char c = t0[i];
		if(c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if(c != kName[i])
			return false;
	}
	return true;
}

typedef std::unique_ptr<SceneRoads> (*SceneRoadsFactory)();
void RegisterSceneRoadsFactory(SceneRoadsFactory f);   // srv_render_scene.cpp holds the registry
std::unique_ptr<SceneRoads> MakeSceneRoads();          // null without server/src/extras/
std::unique_ptr<SceneRoads> MakeSceneRoadsImpl();      // the producer itself (tests, the registrar)

}  // namespace srvextras
