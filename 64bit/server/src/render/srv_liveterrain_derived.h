// srv_liveterrain_derived.h - the server-drawn trees and decals on the new ground after a live terrain change (epic 0465,
// ticket 4904). Written 2026-11-19 by w7-4904 for this repository (GPL-3.0-or-later).
// Findings and proof: research/05-graphics-composition/live-terrain.md 8.12.
//
// What follows a live window by itself (no code here):
//   * Decals (the EXE's tapped roads, farm fields, improvement grounds, the river bed; the server's farm fields, epic 0528;
//     the Tree_Shadow decals, ticket 4552) carry no height: a rectangle and half2 positions in the field's sample frame,
//     drawn into the tile bake's blend targets (terrain-decals.md 4.1-4.2). The residency hashes each tile's heights and
//     materials over a 3-sample apron at every onField (render/src/bakeresidency/residency.cpp, Residency::onField) and
//     bakes the changed tiles again with the decal set in force, so every decal on a changed tile lies on the new ground.
//   * Trees: Scene::applyField after a live window calls SceneForest::onField with the same load; without windows pending
//     the forest drops every built block and every Tree_Shadow decal of the map (invalidateAll) and builds them again
//     from the edited field as plots become visible. Correct heights, but a whole-map shadow re-bake per change.
//
// What this unit adds: the window-local path. The hook (4907, Scene::liveTerrainStep) calls DerivedAfterWindow with the
// sample rectangles it wrote, after ApplyWindow and before applyField. The forest keeps them (expectWindows) and its next
// same-load onField builds again only the built plots whose trees read a sample inside a rectangle (a tree's height
// sample or the slope rule's 5 x 5 stencil, with a margin for the two ways the build rounds the sample position). Their
// Tree_Shadow decals follow through the forest's hook (a decal whose content did not change stays). Plots not built yet
// are built from the edited field at their first visibility, as before. If any rebuild fails, the forest falls back to
// the whole-map path for that field, so no tree keeps a stale height (D118: never a half-updated picture).
#pragma once

#include <stdint.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "c5r/forest/placement.h"
#include "c5r/heightfield/heightfield.h"

namespace srvrender {

class SceneForest;

namespace liveterrain {

// A rectangle of the field written by a live window, in samples, inclusive on both ends: 0 <= x0 <= x1 < samplesX,
// 0 <= y0 <= y1 < samplesY (one unwrapped rectangle, as the producer writes it; ApplyStats' x0, y0, x1, y1).
struct SampleBox
{
	int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// One written rectangle (x0, y0, w, h; w, h > 0) as a box. MC #9538 item 3: the hook passes every rectangle of a window
// as its own box (a window across the x seam is two rectangles at the field's two ends; their bounding box, ApplyStats',
// is the whole width and would rebuild the trees of the whole map's rows).
inline SampleBox SampleBoxOfRect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
	SampleBox b;
	b.x0 = (int32_t)x0;
	b.y0 = (int32_t)y0;
	b.x1 = (int32_t)(x0 + w) - 1;
	b.y1 = (int32_t)(y0 + h) - 1;
	return b;
}

// Samples added on every side of a box when a tree is tested: 2 for the slope rule's 5 x 5 stencil, 1 for the build's
// two roundings of the tree's sample position (the height sample from the world position taken back, the stencil's
// centre from the hex transform).
constexpr int32_t kTreeReadMargin = 3;

// The entry point for 4907's hook: after ApplyWindow wrote the boxes into the grid the forest holds, before applyField.
// forest null (render_forest=0, a program without server/src/forest/): nothing. Boxes outside any field size are kept
// as given; the forest clamps nothing (a box beyond the grid simply touches no tree). Returns boxes.size() when they were
// handed over, 0 otherwise.
size_t DerivedAfterWindow(SceneForest* forest, const std::vector<SampleBox>& boxes);

// True when a tree of b (built for g) reads a sample inside one of boxes, widened by kTreeReadMargin; x wraps when g
// wraps. Every instance counts, drawn or culled (a raised plot can lift a culled tree above the water).
bool BlocksReadBoxes(const c5r::heightfield::HeightGrid& g, const c5r::forest::PlotBlocks& b, const std::vector<SampleBox>& boxes);

struct ReheightStats
{
	uint32_t builtSeen = 0;   // built plots looked at
	uint32_t touched = 0;     // of them, plots whose trees read a box (or whose blocks were of another cut generation)
	uint32_t rebuilt = 0;     // touched plots built again
	uint32_t failed = 0;      // touched plots whose build failed (the caller falls back to the whole-map path)
	double ms = 0;            // the whole call
	std::string firstError;
};

// One built plot as the forest keeps it: its plot and the cut generation its blocks were built with.
struct BuiltPlot
{
	int32_t x = 0, y = 0;
	uint64_t cutGeneration = 0;
};

// The window-local re-height: for each built plot whose cached blocks read a box (or were built with another cut
// generation than cut's), the placement's blocks are dropped and built again from g; onRebuilt(x, y, record, blocks) is
// called for each, blocks null when the build failed. Plots that read no box are not touched (their blocks stay
// bit-equal). place's record of a listed plot missing: counted as failed.
typedef std::function<void(int32_t x, int32_t y, const c5r::forest::PlotRecord* rec, const c5r::forest::PlotBlocks* blocks)> RebuiltFn;
ReheightStats ReheightBuilt(c5r::forest::Placement& place, const c5r::heightfield::HeightGrid& g, const c5r::forest::CutSource& cut,
                            const std::vector<BuiltPlot>& built, const std::vector<SampleBox>& boxes, const RebuiltFn& onRebuilt);

}  // namespace liveterrain
}  // namespace srvrender
