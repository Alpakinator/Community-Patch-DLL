// units/garrison_perimeter.h - GarrisonVisible's look (d), "perimeter" (epic 0455, issue 4983 stage d): a city's garrison
// drawn with its figures spread in a ring along the city plot's land edges, facing out. Written 2026-11-23 for this
// repository (GPL-3.0-or-later). Design and results: research/05-graphics-composition/extras-units.md section 4983.14.
//
// The geometry is our own, from the height field's plot lattice only:
//   - the lattice: e = centre(1, 0) - centre(0, 0) (east neighbour) and a = centre(0, 1) - centre(0, 0) (north-east
//     neighbour of an even row); the six neighbours of any plot centre C are C + e, C + a, C + a - e, C - e, C - a,
//     C + e - a, in that order: edge k = 0..5 is E, NE, NW, W, SW, SE (bit k of a land mask);
//   - a hex vertex is the centroid of the three plot centres that meet there; edge k runs from the vertex it shares with
//     neighbour k - 1 to the one it shares with neighbour k + 1;
//   - the ring is the hexagon scaled about C by `share` (kPerimeterShare: inside the plot, clear of its rim);
//   - a land edge: the neighbour's centre lies on the field and its height is above the field's water level;
//   - n figures are spread evenly along the land edges' total length (edges in the order above, each a piece of the ring),
//     each at the middle of its share, facing out of the plot across its edge (yaw = the edge's outward angle + pi / 2,
//     the module's placement convention: the front along local -y).
// GarrisonPlacement is a PlacementSource over another: a unit it has a mask for is placed by the inner source and then
// each living figure moved to its spot by c5r::units::applyFigureOffsets (height sampled again, ZOffset kept); every
// other unit, and a unit whose mask is 0, is exactly the inner source's placement.
// Errors: c5r::Status / UnitStatus; nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"
#include "c5r/units/placement.h"
#include "c5r/units/units.h"

namespace c5r {
namespace extras {
namespace units {

const float kPerimeterShare = 0.85f;   // the ring's distance from the centre, as a share of the plot's own hexagon
const int kPerimeterEdges = 6;

struct Lattice
{
	float ex = 0, ey = 0;   // centre(1, 0) - centre(0, 0)
	float ax = 0, ay = 0;   // centre(0, 1) - centre(0, 0)
};

struct PerimeterSpot
{
	float x = 0, y = 0;   // world units
	float yaw = 0;        // radians about +z, the placement's convention
	int edge = -1;        // 0..5
};

// The lattice from the field (plot centres (0, 0), (1, 0), (0, 1)); fails when the field gives none of them or they
// are degenerate.
Status latticeOf(const heightfield::HeightGrid& g, Lattice& out);
// The six neighbour centres of a plot centre, in edge order.
void neighbourCentres(float cx, float cy, const Lattice& l, float nx[kPerimeterEdges], float ny[kPerimeterEdges]);
// Bit k set when neighbour k is land: its centre on the field and its height above max(waterLevel, the field's waterZ).
uint8_t landEdges(const heightfield::HeightGrid& g, float waterLevel, float cx, float cy, const Lattice& l);
// n spots along the land edges of mask (bits 0..5) of the plot centred at (cx, cy), at `share` of the hexagon. An empty
// mask or n 0 gives no spot (Ok); a non-finite input or a share outside (0, 1] fails.
Status perimeterSpots(float cx, float cy, const Lattice& l, uint8_t mask, float share, size_t n,
                      std::vector<PerimeterSpot>& out);

// The decorator. set() for each unit to spread (mask != 0) before the renderer places it; clear() at a new frame.
class GarrisonPlacement : public c5r::units::PlacementSource
{
public:
	explicit GarrisonPlacement(c5r::units::PlacementSource* inner) : inner_(inner) {}
	void setInner(c5r::units::PlacementSource* inner) { inner_ = inner; }
	c5r::units::PlacementSource* inner() const { return inner_; }
	void set(int32_t player, int32_t id, uint8_t mask);
	void clear() { masks_.clear(); }
	size_t pending() const { return masks_.size(); }
	c5r::units::UnitStatus place(const c5r::units::PlacementRequest& r, c5r::units::UnitPlacement& out) override;
	uint64_t spread() const { return spread_; }   // units placed on a perimeter since the start
	uint64_t failed() const { return failed_; }   // units whose spread failed (left as the inner source placed them)

private:
	c5r::units::PlacementSource* inner_ = nullptr;
	std::map<std::pair<int32_t, int32_t>, uint8_t> masks_;
	uint64_t spread_ = 0, failed_ = 0;
};

}  // namespace units
}  // namespace extras
}  // namespace c5r
