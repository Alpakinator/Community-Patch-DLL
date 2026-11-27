// c5r/farmfields/farmfields.h - the server's own farm fields and farm tracks (epic 0528, decision D186; ticket 4851 the
// frame, 0528 C the layout). Pure CPU, no D3D. Written 2026-11-18 for this repository (GPL-3.0-or-later).
//
// What the EXE makes, as the knowledge base describes it (D83: own code from the description, nothing from the EXE):
//   * Farm fields: one decal per field polygon, a triangle fan stored as a triangle list (every triangle repeats the
//     first corner), pass 0, priority 0.5 (the Crops variants'), albedo and heightmap of one child of a farm_types.xml
//     category, spec that child's `spec` attribute or the farm system's default spec blank_texture.dds
//     (research/05-graphics-composition/terrain-decals.md 1.2, 2.2, 4.2, 8.3).
//   * Farm tracks: strips along polylines, pass 0, priority 0.6 (variant 0 Detail_Road), albedo and heightmap handle 0
//     (decal::kNoTexture: the EXE's 8 x 8 default texture), spec blank_texture.dds (terrain-decals.md 2.2, 8.4).
//   * A decal's vertices: position (p - rect min) x (1 / extent) per axis in single precision, then binary16 round to
//     nearest even; texture coordinates as binary16; the rectangle the box of every vertex (terrain-decals.md 1.2).
//   * Texture names as the decal tap publishes them: "@farm_types.xml/<category>/<child>/<attribute>" (child 0-based),
//     resolved by the residency through the farm_types.xml the game reads (terrain-decals.md 8.3, 12.2).
//
// The flow (srvrender::FarmFieldsProducer drives it; tests call each step):
//   LayoutInput (the farm plots of the feed's plot table, the game's seed, the world, farm_types.xml's shape)
//     -> layoutFarms()  THE LAYOUT. A PLACEHOLDER in ticket 4851 (one square field and one track per farm plot);
//                       0528 C replaces its body with the EXE's behaviour from farm-fields-layout.md (piece A, 4850).
//     -> checkLayout()  the layout's invariants (owners, categories, children, finite corners inside the world, sizes)
//     -> makeDecals()   decal records + vertices in c5r::decal form (FarmDecals)
//     -> checkDecals()  the ticket's invariants on the records; names the first broken one
//   produce() runs all four and never hands out a set that fails a check.
//   composeFarmSet() joins FarmDecals to the decal tap's set (the tap's farm records dropped when asked), with the tap's
//   walk block (terrain-decals.md 18) and area block (14) carried and extended; farmChange() gives the rectangles and tiles
//   a change between two FarmDecals re-bakes and marks.
//
// Errors: every call returns a c5r::Status; nothing throws; a failed call leaves its output empty.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "c5r/decal/decal.h"
#include "c5r/status.h"

namespace c5r { namespace farmfields {

// ---- farm_types.xml's categories, in the farm system's order (terrain-decals.md 8.3: the EXE's string array) ----
enum Category : uint32_t {
    kHalfBuilt = 0,        // Crops_Half_Built
    kAfrica = 1,           // Crops_Africa
    kAmerica = 2,          // Crops_America
    kAsia = 3,             // Crops_Asia
    kEurope = 4,           // Crops_Europe
    kWheat = 5,            // Crops_Wheat
    kWheatHalfBuilt = 6,   // Crops_Wheat_HB
    kDamage = 7,           // Crops_Damage
    kCategoryCount = 8,
};
// The category's element name in farm_types.xml ("Crops_Africa"); null for c >= kCategoryCount.
const char* categoryName(uint32_t c);
// The category of an element name (compared without case); false when it is none of the eight.
bool categoryOf(const std::string& name, uint32_t& c);

extern const char* const kDefaultSpec;   // "blank_texture.dds": the farm system's default spec (+0x454), by name
const float kFieldPriority = 0.5f;       // Crops' priority (terrain-decals.md 2.2, 4.2 register 6.x)
const float kTrackPriority = 0.6f;       // Detail_Road's
const uint32_t kFarmPass = 0;            // the blend's three targets
const uint32_t kMaxDecalVertices = 0xFFFE / 3 * 3;   // AddDecal: 3 x triangles <= 0xFFFE (terrain-decals.md 1.2)

// "@farm_types.xml/<category name>/<child>/<attribute>"; empty for a category outside the eight.
std::string farmTypesRef(uint32_t category, uint32_t child, const char* attribute);
// The inverse: false when `name` is not such a reference (another file, a bad form, an unknown category, a child index
// that is not a decimal number below 2^31). Category names compare without case; `attribute` comes back in lower case.
bool parseFarmTypesRef(const std::string& name, uint32_t& category, uint32_t& child, std::string& attribute);

// The shape of the farm_types.xml the game reads, what the producer needs of it: per category how many children it has,
// and per child whether it carries a `spec` attribute (else the default spec). The server fills it from the parsed file
// (srvrender::FarmTypesFromMap); tests by hand.
struct FarmTypes {
    std::vector<uint8_t> hasSpec[kCategoryCount];   // size = the category's children; 1 = the child has `spec`
    uint32_t children(uint32_t c) const { return c < kCategoryCount ? static_cast<uint32_t>(hasSpec[c].size()) : 0; }
};

// ---- the producer's input: the farm plots of one load ----
enum class FarmState : uint8_t { HalfBuilt = 1, Built = 2, Pillaged = 3 };   // PlotRecord::improvementState's values

struct FarmPlot {
    int32_t x = 0, y = 0;            // the DLL's plot coordinates
    FarmState state = FarmState::Built;
    uint8_t artType = 0;             // PlotRecord::artType: 0 none, 1 America, 2 Asia, 3 Africa, 4 Europe
    bool wheat = false;              // the plot's resource draws as ART_DEF_RESOURCE_WHEAT (Crops_Wheat, 4852)
};

struct LayoutInput {
    uint32_t gameSeed = 0;           // a value of the game that a save keeps (the server passes the field's PreGame map
                                     // random seed, HeightGrid::mapSeed); with the plot it seeds every random choice
    uint32_t mapW = 0, mapH = 0;     // the DLL's map in plots (the plot table's width and height, no margin rows)
    float worldRect[4] = {0, 0, 0, 0};   // the decals' world rectangle (the tap set's, or (0, 0, samples - 1) of the field)
    FarmTypes types;
    std::vector<FarmPlot> plots;     // every farm plot, ascending by y then x, each once, inside the map
};

// The seed of one plot: a 32-bit mix of the game's seed and the plot's coordinates (same input, same value; no state).
uint32_t plotSeed(uint32_t gameSeed, int32_t x, int32_t y);
// The plot's centre in the sample frame of the decals and the height field: x = 64 hx + 32 y + 32 with hx = x - (y >> 1)
// (the cube coordinate HexToWorld takes), y = 64 y + 128 (the field's two margin rows below the map) [terrain-decals.md 2.1
// and trees.md: the census join's centre; I that it is the farm builder's centre too, 4850 says].
void plotCentre(int32_t x, int32_t y, float out[2]);

// ---- the layout: one load's farm groups, field polygons and tracks ----
struct FarmGroup {
    std::vector<uint32_t> plots;     // indices into LayoutInput::plots (ascending), at least one
};
struct FieldPolygon {
    uint32_t plot = 0;               // the owning farm plot (index into LayoutInput::plots)
    uint32_t group = 0;              // index into FarmLayout::groups; the group holds `plot`
    uint32_t category = 0, child = 0;    // the farm_types.xml child whose textures it draws
    std::vector<float> corners;      // x, y per corner in the sample frame: a simple polygon, >= 3 corners, that the farm
                                     // manager's ear clipping (layout.h earClip) triangulates; convex: the fan from 0
    std::vector<float> uv;           // u, v per corner (unbounded: the field's texture repeats, the sampler wraps)
};
struct TrackStrip {
    uint32_t plot = 0;               // the owning farm plot
    uint32_t group = 0;
    std::vector<float> xyuv;         // x, y, u, v per vertex, three vertices per triangle (a triangle list)
};
struct FarmLayout {
    std::vector<FarmGroup> groups;
    std::vector<FieldPolygon> fields;   // in the order their decals are added (the walk's add order)
    std::vector<TrackStrip> tracks;     // added after every field
};

// THE LAYOUT (ticket 4852: farm-fields-layout.md sections 1-8, the stages in layout.h). The same input gives the same
// layout byte for byte; every random choice comes from the game's seed, a block's index or plotSeed() of a plot (no
// global state, no clock); the result passes checkLayout().
//   * groups: one per block of kBlockPlots x kBlockPlots plots that holds farms (its farm plots ascending), blocks by y
//     then x. Each block's graph is laid out from the seed alone (layoutBlock), whatever plots are farms.
//   * fields: the block's faces whose centroid lies on one of its farm plots, ear-clipped (a face that does not clip is
//     dropped, as stock makes no decal); per plot in centroid order (y, then x), each with its own stream
//     CrtRand(plotSeed(plot) mixed with the field's ordinal): rand() % 3 == 0 turns the uv by pi / 2, then the variant
//     rand() % children of fieldCategory's category (none: no field, no draw); a pillaged farm adds a Crops_Damage copy
//     right after the field (another draw) [I: state 8 of the text is the feed's pillaged state].
//   * tracks: per graph grid cell (2 x 2 per plot: 32 x 27.71 L), one strip for every edge that borders a kept field, has
//     every point inside some kept field's margin (pushed 5.0 from its centroid) and starts in that cell [I: the cell an
//     edge is filed in]; owner: the farm under the cell's centre if it has fields, else the owner of the first field whose
//     margin holds the first strip's first point.
// Errors: those of checkInput and layoutBlock.
// cache (optional): the block graphs of earlier calls, reused while the game seed and the world are the same (a graph
// depends on nothing else), so a change in play lays out no block again; Producer keeps one per load.
struct BlockCache;
Status layoutFarms(const LayoutInput& in, FarmLayout& out, BlockCache* cache = nullptr);

// The input's own checks: mapW, mapH not 0; worldRect finite with min < max; plots inside the map, ascending by y then
// x, each once; a state of 1..3; an art type of 0..4. BadLayout naming the first.
Status checkInput(const LayoutInput& in);

// ---- the invariants ----
// Each check names the first broken invariant: in the Status's message ("invariant <name>: ...") and, when `which` is
// given, as this enum (kNone when it holds).
enum class Invariant : uint32_t {
    kNone = 0,
    kInput,             // checkInput failed
    kGroup,             // a group empty, a plot index outside the input, not ascending, or a plot in two groups
    kOwner,             // a field or track whose plot is outside the input or not in its group, or a group outside the layout
    kCategory,          // a field whose category is not one of the eight, or whose child the category does not have
    kShape,             // fewer than 3 corners, a corner count or uv count that does not match, a track not 3 x 4 floats per
                        // triangle, or more than kMaxDecalVertices vertices
    kFinite,            // a corner, uv or rectangle value not finite
    kInsideWorld,       // a corner outside the world rectangle, or a rectangle not inside it
    kEmptyRect,         // a decal's rectangle with min >= max on an axis (nothing to draw into)
    kTables,            // FarmDecals' arrays disagree in length
    kVertexRun,         // a decal's vertex run empty, not a multiple of 3, over kMaxDecalVertices or past the vertices
    kVertexInRect,      // a vertex position (binary16) outside 0..1 of its decal's rectangle, or NaN
    kPriority,          // a field not at 0.5 or a track not at 0.6, or a pass other than 0
    kTexture,           // a name that is not the right @farm_types.xml reference, kNoTexture or the default spec
    kHash,              // a decal's hash is not its content hash
    kListOrder,         // list orders not 0 .. n - 1 in decal order
};
const char* invariantName(Invariant v);

// The layout's invariants (kInput, kGroup, kOwner, kCategory, kShape, kFinite, kInsideWorld, kEmptyRect: a field's or
// track's box must have min < max on both axes). BadLayout on the first broken one.
Status checkLayout(const LayoutInput& in, const FarmLayout& layout, Invariant* which = nullptr);

// ---- the records ----
// The decals of a layout in c5r::decal form, ready for composeFarmSet: their own name table, fields first (layout
// order) then tracks, list order 0 .. n - 1, firstVertex into `vertices`, hash = the decal tap's content hash
// (contentHash). `owner` and `track` are per decal.
struct FarmDecals {
    std::vector<std::string> textures;
    std::vector<decal::Decal> decals;
    std::vector<decal::DecalVertex> vertices;
    std::vector<uint32_t> owner;     // index into LayoutInput::plots
    std::vector<uint8_t> track;      // 1 = a farm track, 0 = a field
    bool empty() const { return decals.empty(); }
};

// The content hash of a decal as the decal tap's reader computes it (srvrender::DecalContentHash, terrain-decals.md 10.1):
// FNV-1a 64 over the rectangle (16 bytes), priority (4), pass (4), each of the three names with its terminating zero (a
// byte 0xFF for kNoTexture or an index outside `names`), then the vertex run's bytes when it lies inside `vertices`.
uint64_t contentHash(const decal::Decal& d, const std::vector<std::string>& names,
                     const std::vector<decal::DecalVertex>& vertices);

// The records of a layout. Runs checkLayout first (its Status on failure). Fields: the fan as a list (c0, ci, ci+1);
// tracks: their triangles as given. Rectangle = the box of the decal's own vertices (inside the world, by checkLayout).
Status makeDecals(const LayoutInput& in, const FarmLayout& layout, FarmDecals& out);

// The ticket's invariants on records: kTables, kVertexRun, kFinite, kEmptyRect, kInsideWorld (finite rectangles inside
// the world), kPriority (0.5 fields / 0.6 tracks, pass 0), kTexture (a field: albedo / heightmap the references of one
// child that `in.types` has, spec that child's spec reference when it has one, else kDefaultSpec; a track: albedo and
// heightmap kNoTexture, spec kDefaultSpec), kVertexInRect (every vertex position 0..1 in binary16), kOwner (every decal
// owned by a farm plot of the input), kHash, kListOrder. BadLayout on the first broken one.
Status checkDecals(const LayoutInput& in, const FarmDecals& d, Invariant* which = nullptr);

// layoutFarms + makeDecals (with checkLayout) + checkDecals. On any failure both outputs are empty and the Status names
// the step and the invariant. The producer's only entry: a set that fails a check is never handed out.
Status produce(const LayoutInput& in, FarmLayout& layout, FarmDecals& out, Invariant* which = nullptr, BlockCache* cache = nullptr);

// ---- the tap's set ----
// The farm records of a tap set (the EXE's farm builder still running): a field = priority 0.5, pass 0, albedo an
// @farm_types.xml reference; a track = priority 0.6, pass 0, albedo and heightmap kNoTexture. Everything else is not farm.
enum class TapClass : uint8_t { kOther = 0, kField = 1, kTrack = 2 };
TapClass tapClass(const decal::DecalSet& set, size_t decalIndex);
struct TapFarmCount { size_t fields = 0, tracks = 0; };
TapFarmCount tapFarmCount(const decal::DecalSet& set);

struct ComposeStats {
    uint32_t tapDecals = 0;          // decals of the tap's set
    uint32_t tapRemoved = 0;         // of them, farm records dropped (removeTapFarm)
    uint32_t farmDecals = 0;         // farm decals appended
    uint32_t farmVertices = 0;
    uint32_t namesAdded = 0;
    bool walk = false;               // the tap's walk was carried (its dropped decals out, the farm decals in)
    uint32_t walkLarge = 0;          // farm decals in the large list (a side over 256 samples)
    bool area = false;               // the tap's area block was carried with the farm decals' and marks' bits
    uint32_t areaBitsSet = 0;
};

// A new set: the tap's decals in their order (its farm records left out when removeTapFarm), then the farm decals in
// their order, each with the next list order after the tap's largest. Kept tap decals keep their vertex runs (the tap's
// vertex array is carried whole); the farm vertices follow it. Names: the farm decals' names joined to the tap's table
// (equal text reuses the tap's entry).
// With the tap's walk (decal::hasWalk): every kept decal keeps its cell and point; ranks are closed up in each cell and in
// the large list (the dropped ones removed, the order of the rest kept: the EXE's remove unlinks, 18.1); each farm decal
// joins as AddDecal adds (18.1): point = the rectangle's centre in single precision, cell trunc((point - origin) x
// inverse) clamped to the grid, at the head of its cell (the last given farm decal first), or at the end of the large list
// when a side exceeds 256 samples. With the tap's area block: the bits of each farm decal's plain tile square
// (floor(min / 64) .. floor(max / 64)) and of `marks` (tile coordinates; stock never clears a bit before the next load,
// 14.6) are set; dropped decals' bits stay. generation and loadSeq are the tap's; the grid is built (decal::buildIndex).
// Errors: Unsupported (removeTapFarm false and the tap holds farm records: they would draw twice); BadLayout (the tap's
// world rectangle not finite or empty, a farm decal whose run, names or rectangle checkDecals' structural rules refuse or
// whose rectangle lies outside the tap's world, more than 2^32 - 1 vertices, list orders past 2^32 - 1, a walk the
// result cannot keep).
Status composeFarmSet(const decal::DecalSet& tap, bool removeTapFarm, const FarmDecals& farm,
                      const std::vector<decal::TileOrigin>* marks, decal::DecalSet& out, ComposeStats* stats = nullptr);

// What a change from `before` to `after` re-bakes: the rectangles of the decals in one and not the other (by hash, as
// multisets: an unchanged farm moves nothing), the tiles decal::tilesForRects gives for them, and the area tiles (tile
// coordinates, sorted by y then x, unique) of the added decals' plain squares and of the removed ones' (the caller keeps
// the removed ones' as marks). Errors: BadLayout (worldRect not finite or empty, a decal rectangle not finite).
struct FarmChange {
    std::vector<decal::DecalRect> rects;
    std::vector<decal::TileOrigin> bakeTiles;
    std::vector<decal::TileOrigin> areaTiles;
    uint32_t added = 0, removed = 0;
};
Status farmChange(const FarmDecals& before, const FarmDecals& after, const float worldRect[4], FarmChange& out);

// ---- the inputs from the server's tables (plain fields, so that the server's types stay out of render/) ----
// farm_types.xml's shape from its parsed attributes (srvrender::ParseFarmTypes' map: key "<category>/<child>/<attribute>"
// in lower case -> value): per category, children = 1 + the largest child index that has an attribute; a child has a
// spec when its "spec" attribute is there and not empty. Categories outside the eight are ignored.
FarmTypes farmTypesFromAttributes(const std::vector<std::pair<std::string, std::string>>& attributes);

// One plot record (srvrender's SrvFeed::PlotRecord fields): a farm when farmId >= 0 and improvement == farmId (and, with
// revealedOnly, fow bit 0 set). State: pillaged when flags bit 1 is set or improvementState is 3; half-built when
// improvementState is 1; else built (2, or -1 = no GameplayPlotStateChange seen yet: the getter's improvement is a built
// one). The art type: artType, 0 when outside 0..4. False when it is no farm (out untouched).
// wheat: resource == wheatResourceId (the resource type whose art define is ART_DEF_RESOURCE_WHEAT; -1 unknown: never).
bool farmPlotOf(int32_t x, int32_t y, int farmId, int16_t improvement, int8_t improvementState, uint8_t flags, uint8_t fow,
                uint8_t artType, bool revealedOnly, FarmPlot& out, int16_t resource = -1, int wheatResourceId = -1);

// ---- the producer's state across a load (srvrender::FarmFieldsProducer wraps it with the feed and the logs) ----
// Holds the last good production, the load's area marks (stock never clears tile+0x300 before the next load, 14.6: every
// area tile a farm decal ever had this load), the change since the last take, and the last compose (cached per tap set).
class Producer {
public:
    // A new load: the decals, the layout, the marks and the pending change dropped (the residency bakes the new load's
    // tiles anyway); the next update produces.
    void newLoad();
    // The load's input now. When it equals the input of the last production (and that one succeeded), nothing happens
    // (changed false). Else produce(): on success the decals are replaced, the change against the previous decals joins
    // the pending change, the area tiles join the marks, changed = (any decal added or removed). On failure the previous
    // decals stay in force (a failing set is never handed out), the Status is returned and counted in failures().
    Status update(const LayoutInput& in, bool& changed);
    // update()'s second half: a production of `in` made elsewhere (a worker thread's produce(), which passed every check)
    // put in force, with its change and marks. Errors: farmChange's (a rectangle not finite).
    Status accept(const LayoutInput& in, FarmLayout&& layout, FarmDecals&& decals, bool& changed);
    // Failures of a production made elsewhere, counted here (failures()).
    void countFailure() { ++failures_; }
    const FarmDecals& decals() const { return decals_; }
    const FarmLayout& layout() const { return layout_; }
    const LayoutInput& input() const { return input_; }   // the input of the decals in force
    bool haveInput() const { return haveInput_; }
    const std::vector<decal::TileOrigin>& marks() const { return marks_; }   // tile coordinates, sorted by y then x
    uint64_t version() const { return version_; }   // +1 whenever the decals or the marks change
    uint64_t failures() const { return failures_; }
    uint64_t productions() const { return productions_; }
    // The rectangles and area tiles of every change since the last call (bakeTiles from the world of the last good
    // input); false and `out` empty when nothing changed.
    bool takeChange(FarmChange& out);
    // composeFarmSet of the tap's set with the decals and marks in force; the result is cached per (tap set, version), so a
    // repeated call with the same tap set returns the same object. On a refusal `out` is the tap's set itself and the
    // Status says why (Unsupported: the tap holds farm records and removeTapFarm is false).
    Status compose(const std::shared_ptr<const decal::DecalSet>& tap, bool removeTapFarm,
                   std::shared_ptr<const decal::DecalSet>& out, ComposeStats* stats = nullptr);

private:
    LayoutInput input_;
    bool haveInput_ = false;
    FarmLayout layout_;
    FarmDecals decals_;
    std::vector<decal::TileOrigin> marks_;
    std::vector<decal::DecalRect> pendingRects_;
    std::vector<decal::TileOrigin> pendingArea_;
    uint64_t version_ = 0, failures_ = 0, productions_ = 0;
    std::shared_ptr<const decal::DecalSet> composedTap_, composed_;
    uint64_t composedVersion_ = 0;
    bool composedRemove_ = false;
    std::shared_ptr<BlockCache> cache_;   // the load's block graphs (layoutFarms' cache)
};

// Two inputs equal in every field (seed, map, world bit for bit, types, plots).
bool sameInput(const LayoutInput& a, const LayoutInput& b);

// ---- the producer's input as one line of text (ticket 4852: the scene bundle's render_config render_farmfields_state) ----
// "fv1 seed=<u32> farm=<int> wheat=<int> map=<W>,<H> world=<x0>,<y0>,<x1>,<y1> farms=<x>.<y>.<state>.<art>.<wheat>,..."
// (floats as C99 hex, exact; farms in the input's order). farm_types.xml is not in it: the reader takes it from its install.
std::string spanText(const LayoutInput& in, int farmId, int wheatId);
// The inverse (types left empty). Errors: BadLayout (another form, a field missing or malformed, a value out of range, an
// input checkInput refuses); BadVersion (another form version than fv1). Outputs untouched on failure.
Status parseSpanText(const std::string& text, LayoutInput& in, int& farmId, int& wheatId);

}} // namespace c5r::farmfields
