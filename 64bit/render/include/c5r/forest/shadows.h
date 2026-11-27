// c5r/forest/shadows.h - the Tree_Shadow decals of route A: the server makes, for every forest or jungle plot it has
// built, the shadow decal the EXE's Forest adds (0x770EE0 through the variant wrapper 0x60F180, category 3, into
// AddDecal), and joins them to the decal tap's set so that the tile bake draws them into the macro shadow and marks their
// tiles for t8 (epic 0431, ticket 4552, package T3).
//
// Facts it rests on: research/05-graphics-composition/trees.md 2.5, 8.5, 8.8, 10.3 and section 12 (this module as built,
// with the record form read from stock recordings); terrain-decals.md 1.2 (records, vertices, the rectangle), 1.3 (the
// category 3 variant: Tree_Shadow_2.dds, priority 0.1), 4.4 (the macro-shadow pass), 14.6 (stock's tile+0x300 rule), 18
// (the EXE's walk order). Own code from those descriptions (D83). Pure CPU, no D3D.
//
// Who calls what (the scene, ticket 4554, owns when; this module only makes records and sets):
//   1. A plot is built (its blocks, c5r::forest::Placement::blocks, the first time it is in a view, as stock): make its
//      decal with shadowDecal(). A plot with no drawn tree has no decal (quads 0) [I: stock's census never shows one].
//   2. Whenever the forest decals or the tap's set change: mergeShadows(tap set, the forest decals in add order, marks)
//      gives the set the residency bakes from (ResidencySource::setDecals). The tap's set is never modified.
//   3. For the decals just built or dropped: shadowChange() gives the rectangles (a DecalChange's `rects`), the tiles they
//      re-bake and the tiles stock's tile+0x300 marks (DecalChange::areaTiles, and the scene's running `marks`).
// A plot's decal is dropped when its trees change (feature, cut stamp over it, a new height field) and made again when the
// plot is next in a view (trees.md 8.8).
//
// Errors: every call returns a c5r::Status; nothing throws; a failed call leaves its output empty.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/decal/decal.h"
#include "c5r/forest/placement.h"
#include "c5r/status.h"

namespace c5r { namespace heightfield { struct HeightGrid; } }

namespace c5r { namespace forest {

// The category 3 variant of decal_filenames.xml as the tap names it in stock sessions [L, terrain-decals.md 1.3, trees.md
// 12.1]: albedo and height Tree_Shadow_2.dds, spec the default blank_texture_spec_decal.dds; priority 0.1; pass 1 (the
// macro-shadow target).
extern const char* const kShadowAlbedo;      // "Tree_Shadow_2.dds" (albedo and height)
extern const char* const kShadowSpec;        // "blank_texture_spec_decal.dds"
const float kShadowPriority = 0.1f;
const uint32_t kShadowPass = 1;
// AddDecal's limit: 3 x triangles <= 0xFFFE per decal (terrain-decals.md 1.2), so at most 10,922 quads.
const uint32_t kMaxShadowQuads = 0xFFFE / 6;

// The world rectangle every decal rectangle is clamped to (TDS+0x6C..+0x78): (0, 0, samplesX - 1, samplesY - 1) [L: the
// tap's rectangle on Tiny 3583 x 2559 and huge 8191 x 5375]. Use the tap set's own worldRect when there is one.
void worldRectOf(const heightfield::HeightGrid& g, float out[4]);

// One plot's Tree_Shadow decal, independent of any set: the record form AddDecal stores.
struct ShadowDecal {
    int32_t plotX = 0, plotY = 0;
    uint32_t quads = 0;                       // one per drawn tree, in layout order; 0 = no decal
    float half = 0;                           // 6.0, or 3.6 on kinds 3 / 4
    float rect[4] = {0, 0, 0, 0};             // the box of every quad corner, clamped to the world rectangle
    std::vector<decal::DecalVertex> vertices; // 6 per quad (2 triangles): corners (min, min) (max, min) (max, max)
                                              // (min, min) (max, max) (min, max) with uv (0, 1) (1, 1) (1, 0) (0, 1)
                                              // (1, 0) (0, 0); position (p - rect min) / rect extent as binary16 (round
                                              // to nearest even), uv as binary16
    uint64_t hash = 0;                        // FNV-1a 64 of the content (pass, priority, rect, quads, vertex bytes)
};

// (1) The decal of one built plot (blocks from buildPlot / Placement::blocks; plotX, plotY name it). Each drawn tree gives
// the quad centre (sampleX, sampleY) +- half in the sample frame. Errors: BadLayout (worldRect not finite or empty; half
// not finite or <= 0; a tree centre not finite; more than kMaxShadowQuads trees; the box empty after the clamp, i.e. the
// plot outside the world). No drawn tree: Ok with quads 0 and no vertices.
Status shadowDecal(const PlotBlocks& blocks, int32_t plotX, int32_t plotY, const float worldRect[4], ShadowDecal& out);

// How many Tree_Shadow decals a set already holds (albedo Tree_Shadow_2.dds, pass 1): the EXE's own when its Forest is on.
size_t treeShadowCount(const decal::DecalSet& set);

struct MergeStats {
    uint32_t tapDecals = 0;          // decals of the tap's set (kept at their indices 0 .. tapDecals - 1)
    uint32_t shadowDecals = 0;       // forest decals appended (quads > 0)
    uint32_t shadowVertices = 0;
    uint32_t namesAdded = 0;         // texture names appended to the tap's table (0, 1 or 2)
    bool walk = false;               // the tap's set had a walk block and the merged set carries it, extended
    uint32_t walkLarge = 0;          // forest decals in the large list (a side over 256 samples; never seen)
    bool area = false;               // the tap's set had an area block and the merged set carries it, with the marks
    uint32_t areaBitsSet = 0;        // area bits the forest decals and marks set that were 0 in the tap's block
};

// (2) A new set: the tap's set with the forest decals appended after its own decals (so every tap decal keeps its index),
// in the order given (`shadows` oldest first: the order the plots were built), each with the next list order after the
// tap's largest (newest), priority 0.1, pass 1, the two names (added to the table when absent, compared without case).
// The order among decals of the macro-shadow pass is all that the forest decals' place changes (each pass is selected
// and sorted on its own; pass 1 holds only tree shadows in every census) [C terrain-decals.md 18.3, L]; within it the
// forest decals are newer than every tap decal [I: stock interleaves them by time; a pass-1 tap decal with priority 0.1
// was never seen].
// With the tap's walk block (0366): each forest decal joins the EXE's index as AddDecal would put it [C 18.1]: the point
// (min + max) x 0.5 of its rectangle in single precision, the cell trunc((point - origin) x inverse) per axis clamped to
// the grid, at the head of the cell's list (the last given decal first, then the earlier ones, then the cell's tap
// decals); a decal with a side over 256 samples at the end of the large list instead. With the tap's area block (4111):
// the bits of the plain tile square floor(min / 64) .. floor(max / 64) of every forest decal and of every tile in `marks`
// (the tiles of forest decals dropped since the load: stock's bit is never cleared until the next load [C 14.6]) are set;
// without a block the residency's own rule applies (a tile with decals). The grid and the walk lists are built
// (decal::buildIndex). generation and loadSeq are the tap's.
// Errors: Unsupported (the tap's set already holds Tree_Shadow decals: the EXE's Forest is on, the decals would draw
// twice); BadLayout (a null shadow or list, a shadow whose vertex count is not 6 x quads or whose rectangle is not finite
// or lies outside the tap's world rectangle, the tap's world rectangle not finite or empty, more than 2^32 - 1 vertices,
// a list order past 2^32 - 1). A mark outside the area grid is ignored.
Status mergeShadows(const decal::DecalSet& tap, const ShadowDecal* const* shadows, size_t count,
                    const std::vector<decal::TileOrigin>* marks, decal::DecalSet& out, MergeStats* stats = nullptr);

// (3) What a change of forest decals (built or dropped, each given once) re-bakes and marks.
struct ShadowChange {
    std::vector<decal::DecalRect> rects;      // their rectangles: a srvrender::DecalChange's `rects`
    std::vector<decal::TileOrigin> bakeTiles; // tile origins decal::tilesForRects gives for them: the tiles whose draw
                                              // list can change (the residency raises exactly these for `rects`)
    std::vector<decal::TileOrigin> areaTiles; // tile coordinates (not origins) of the plain squares floor(min / 64) ..
                                              // floor(max / 64) inside the world's tiles, sorted by y then x, unique:
                                              // the tiles stock's tile+0x300 marks; a DecalChange's `areaTiles`, and
                                              // what the scene keeps as mergeShadows' `marks`
};
// Errors: BadLayout (worldRect not finite or empty, a null decal or list, a rectangle not finite). Decals with quads 0 add
// nothing. A trap for the caller (trees.md 12.4): ResidencySource::decalChanges skips a DecalChange whose generation is
// at or below the generation in force when the field arrived; the merged set keeps the tap's generation, so a change of
// forest decals needs a generation the residency does not skip (this module does not choose one).
Status shadowChange(const ShadowDecal* const* changed, size_t count, const float worldRect[4], ShadowChange& out);

}} // namespace c5r::forest
