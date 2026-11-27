// srv_forest_scene.h - the forest and jungle trees of the renderer inside server.exe (epic 0431, ticket 4554; route A).
// Written 2026-11-16 for this repository (GPL-3.0-or-later). Design: research/05-graphics-composition/trees.md 9 (9.1 the
// place in the frame, 9.3 the inputs, 9.4 residency, 9.5 the failure statuses); as built: trees.md 13.
//
// What it owns: the placement (c5r::forest::Placement, ticket 4550: forests.xml, the four masks, the 20 layouts of a
// load, one record per forest / jungle plot), the renderer (c5r::forest::Renderer, ticket 4551: the
// EXE's ForestShader, the `_2` cards and textures), the five Terrain ColorKey volumes (t5), the Screen Overlay target
// (t3: the frame's size, R8G8B8A8, one mip, trees.md 11.2 / 11.4) and the cut source (c5r::forest::EmptyCut until 4553's
// reader of the EXE's grid replaces it through setCutSource).
//
// Inputs (no DLL access, every call on the render thread):
//   * the event feed: onBatch() with the host's drained batch. A new snapshot generation, a kSnapshot or kOverflow event
//     or a rise of `dropped` re-reads the plot table (FowPlotTable, MakeFeedPlotTable = SrvFeed::CopyPlots) and rebuilds
//     every record; otherwise the table is copied when the batch names changed plots and each changed plot whose feature or
//     plotType moved gets its record patched: a plot that becomes forest or jungle gets a record with a new rand() % 20
//     layout (stock's listener), one that stops being either loses its record and blocks, a kind change (hills) a new
//     record. kFeatureChanged events of a plot outside the table are LOUD and counted.
//   * the feature ids of FEATURE_FOREST and FEATURE_JUNGLE: by name, never a fixed number (design 9.3). The render side
//     has no name source, so a small process-wide table is filled by the DLL side of the feed (the wiring of trees.md
//     13.6): ForestFeatureTagSeen(id, ICvFeatureInfo1::GetArtDefineTag()) for every feature type it reads at a snapshot or
//     a refresh (pure getters on the D30 list) and ForestFeatureIdsResolved() at the end of each snapshot. Tags
//     "ART_DEF_FEATURE_FOREST" / "ART_DEF_FEATURE_JUNGLE" [I: the art defines of Civ5Features.xml; 4550 measured ids 5 /
//     1 on its saves]. setFeatureIds() overrides the table (tests, a host with its own source).
//   * hills: PlotRecord::plotType == 1 (PLOT_HILLS of the DLL's PlotTypes; 4550 trees.md 10.1: kinds 3 / 4 = hills on
//     870 of 870 records) -> kinds 3 / 4.
//   * the height field: onField() with the scene's kept grid (a new load: the 20 layouts again with this session's
//     stream, every record again; the same load re-applied: every block rebuilt).
//   * per drawn frame, drawStage() at its place (after the terrain, the water and the ice; before the cloud planes):
//     the camera block (views 0..numViews-1, wrap copies), the terrain renderer and the camera and Options of that
//     frame's terrain draw (the overlay pair, the fog of war, the fog ramp, the TerrainFOW volumes, the tile styles).
//
// Per frame (drawStage): (1) each record's plot centre (HexToWorld of its cube coordinates, z the height there) is tested
// against the six planes of every view with a 64-unit margin (the EXE's 0x654420 rule, trees.md 2.2); a plot visible in
// any view is drawn in every view (stock draws the frame's whole block list per view, trees.md 11.1); its blocks are
// built on its first visibility (as stock) and kept until a feature change, a new field or a cut change; (2) the Screen
// Overlay pass (c5r::terrain::Renderer::drawScreenOverlay with that frame's camera and Options) into the scene's own
// target; (3) the trees (c5r::forest::Renderer::draw) into the scene's colour and depth with t3 = that target. No
// visible plot: neither (2) nor (3) runs (t3 is read by the trees only).
//
// Failure statuses (design 9.5, D118 / D125 / D132):
//   * install -> failed() with the text, LOUD once, the host's ForestFatalStep: fatal(5, why) once. At init: no install
//     index, forests.xml or a mask refused (c5r::forest::loadInstall), the renderer refused (ForestShader blobs, the `_2`
//     models and textures, the device), a Terrain ColorKey volume missing or refused, the fog-off constants. At the first
//     frame: the Screen Overlay target not made, the terrain without its fog ramp, the layouts refused for the field's
//     matrix, the feature ids not published within idsWaitMs of the first plot table. Never a picture without trees.
//   * in play -> LOUD (the first 20, then every 100th), counted, never fatal: a feature event outside the table, a plot
//     whose blocks fail to build (skipped that frame), a failed Screen Overlay pass or tree draw (the frame stands without
//     trees), a record the placement refuses.
//   * render_forest=0 (on false): no stage, no records (today's picture).
//
// The Tree_Shadow decals (ticket 4552's c5r/forest/shadows.h; trees.md 12, 13.8): a plot built in a view gets its decal
// (shadowDecal), dropped with the plot's blocks (a feature change, a new field, a cut change) and made again at its next
// visibility. At every plot table applied (a load, a re-read, new ids) every forest / jungle plot's decal is computed once
// for its tile marks only (stock's tile+0x300 of every record at the load: 3,020 bits on the huge save, trees.md 12.4) and
// the marks are kept for the load; a dropped decal's tiles join them (stock never clears the bit before the next load).
// composeDecals(tap) = mergeShadows(tap, the built decals oldest first, the marks): the set Scene hands the residency.
// takeDecalChanges() = the rectangles and area tiles of the decals built or dropped since the last call, as one
// DecalChange whose generation lies in a band of its own (kForestDecalGenBase + n): the residency skips a change at or
// below the tap generation in force when its field arrived (trees.md 12.4's trap), and the merged set keeps the tap's
// generation, so a forest change sent with a tap generation could be skipped. A tap set that already holds Tree_Shadow
// decals (the EXE's Forest on) is refused by the merge: LOUD, counted, the tap's set used as it is, never fatal.
#pragma once
#include "srv_render_common.h"
#include "srv_fow_scene.h"      // FowPlotTable, MakeFeedPlotTable (the feed's plot table seam)
#include "srv_forest_ids.h"
#include "srv_decal_tap.h"     // DecalChange (a plain struct)
#include "srv_cut_tap.h"       // CutTapReader (ticket 4553): the cut source of the product path
#include "feed/srv_feed.h"
#include "srv_liveterrain_derived.h"   // epic 0465, ticket 4904: SampleBox (expectWindows)
#include "c5r/forest/placement.h"
#include "c5r/forest/draw.h"
#include "c5r/forest/shadows.h"
#include "c5r/forest/cut.h"
#include "c5r/terrain/terrain.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/decal/decal.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace c5r { namespace gamefs { class GameFiles; } }

namespace srvrender {

const int kPlotTypeHills = 1;           // PLOT_HILLS (the DLL's PlotTypes: 0 mountain, 1 hills, 2 land, 3 ocean)
const float kForestViewMargin = 64.0f;  // the plot-centre frustum test's margin in world units (0x654420, trees.md 2.2)
// The generations of the forest's DecalChanges: a band above every tap generation (the tap counts from 1 per EXE run;
// 2^28 sets would take years), so ResidencySource::decalChanges never skips one (trees.md 12.4, 13.8).
const uint32_t kForestDecalGenBase = 0xF0000000u;

struct ForestSceneOptions
{
	bool on = false;                 // render_forest: false (tools and the scene tests that build a Scene directly)
	std::wstring exe;                // CivilizationV_DX11.exe: the ForestShader blobs (read-only)
	// The install index for init only (Scene shares its one, issue 0198); null: init opens gameDir itself.
	const c5r::gamefs::GameFiles* files = nullptr;
	std::wstring gameDir;
	uint32_t seed = 0;               // the session's random stream; 0 = a new one per session (QPC and the pid), as stock
	uint32_t vsVa = 0, psVa = 0;     // 0 = c5r::forest's (the negative tests name others)
	double idsWaitMs = 10000;        // no published feature ids this long after the first plot table = an install failure
	// The cut mask (ticket 4553, trees.md 13.9): a CutTapReader once the EXE's pid is known (setExePid), EmptyCut before
	// that and where no pid comes (the offline player, the tests that build a scene without a section). cutWaitMs: no grid
	// from the tap this long after the first field = an install failure (0: no bound; the tap OFF is never one).
	// cutControlName: tests only (another control section name; empty = the product's Local\VPShellCutTap.<pid>).
	// cutForceFail: render_test_cut_fail=1, the reader's install failure on purpose (a negative launch for 4555).
	bool cutTap = true;
	double cutWaitMs = 20000;
	std::wstring cutControlName;
	bool cutForceFail = false;
};

// A plot of the newest frame's visible list (tests, 4552).
struct ForestVisiblePlot
{
	int32_t x = 0, y = 0;
	uint32_t entry = 0;              // the style entry (the terrain tile's style at the plot centre)
	uint32_t blocks = 0;
	bool builtNow = false;           // its blocks were built in this frame
};

// The seam for ticket 4552 (Tree_Shadow decals): called when a plot's blocks were built (first visibility or a rebuild:
// rec and blocks valid during the call), when a plot lost its record (blocks null), and with x = y = -1 and both null when
// every record was dropped (a new table, a new load, new feature ids, releaseDevice).
typedef void (*ForestPlotHook)(void* user, int32_t x, int32_t y, const c5r::forest::PlotRecord* rec,
                               const c5r::forest::PlotBlocks* blocks);

struct ForestStageStats
{
	uint32_t records = 0, visible = 0, builtNow = 0, blocks = 0, draws = 0;
	bool overlayPass = false, drawn = false;
	double cpuMs = 0;
};

// ---- ticket 0495: the offline scene harness (offline-scene-harness.md section 25) --------------------------------------
// The forest's plot table: the default is the live feed (MakeFeedPlotTable, SrvFeed::CopyPlots). A host that records or
// replays a bundle sets this slot before Scene::init (server.exe: the recording feed's PLOTS caller 4; sceneshot: the file
// feed's), so that the forest's table reads are in feed.bin like the residency's and the fog's. Read once by
// ForestScene::init when no table was given; empty = the live feed.
inline std::function<std::unique_ptr<FowPlotTable>()>& ForestPlotTableSlot()
{
	static std::function<std::unique_ptr<FowPlotTable>()> f;
	return f;
}

// What the trees hold at the span's first frame that the feed does not give again (forest.bin): the session's random
// stream (the cards and the layouts), the feature ids, the records with their layouts and the stream's state after them,
// the plots built so far in the order their Tree_Shadow decals were made (the merge order), the load's tile marks, and the
// cut mask (sampled per cell from the cut tap's reader; haveCut false = the empty mask). tableGen / tableW / tableH: the
// plot table the records came from (the player checks its own replayed table against them).
struct ForestSpanRecord
{
	int32_t x = 0, y = 0;
	uint32_t kind = 0, layout = 0;   // c5r::forest::Kind 1..4, layout index < 20
};
struct ForestSpanState
{
	uint32_t sessionSeed = 0, layoutSeed = 0, loads = 0, loadSeq = 0;
	bool idsResolved = false;
	int32_t forestId = -1, jungleId = -1;
	uint32_t streamState = 0;
	uint32_t tableGen = 0, tableW = 0, tableH = 0;
	std::vector<ForestSpanRecord> records;            // ascending by y, then x
	std::vector<std::pair<int32_t, int32_t>> built;   // the decals' order first (oldest first), then built plots without one
	std::vector<std::pair<int32_t, int32_t>> marks;   // tile coordinates (ForestScene::marks()), ascending by y, then x
	bool haveCut = false;
	uint32_t cutW = 0, cutH = 0;
	float cutOx = 0, cutOy = 0;
	uint64_t cutGeneration = 0;                       // the live source's (information)
	std::vector<uint8_t> cutCells;                    // cutW x cutH, row-major
};

// ---- What Scene calls (the wiring of trees.md 13.6) ---------------------------------------------------------------------
// Abstract, with a factory registered at static initialisation by srv_forest_scene.cpp, so that a program that compiles
// server/src/render/*.cpp without server/src/forest/ (the render test builders, a server built before the wiring) still
// links: MakeSceneForest then returns null (epic 0375's pattern for server/src/units/, srv_units_scene.h). The forest units
// pull c5r::forest::Renderer out of c5render.lib, which needs c5r_models.lib (the .gr2 reader): a program that links them
// links both libraries (server/build_server.py since 0375; test_forest_build.py).
class SceneForest
{
public:
	virtual ~SceneForest() {}
	virtual void init(ID3D11Device* dev, const ForestSceneOptions& o) = 0;
	virtual void releaseDevice() = 0;
	virtual void onField(const c5r::heightfield::HeightGrid& g, uint32_t loadSeq) = 0;
	virtual bool onBatch(const SrvFeed::Batch& b) = 0;
	virtual bool poll() = 0;
	// The EXE's pid (the main section's tap blocks; 0 = not known): the cut tap's reader is made for it (ticket 4553).
	virtual void setExePid(uint32_t pid) = 0;
	virtual c5r::Status drawStage(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	                              uint32_t h, const VPShellCamera& cam, c5r::terrain::Renderer& terrain, const c5r::terrain::Camera& tcam,
	                              const c5r::terrain::Options& top, const float fowAnimOff[4], ForestStageStats* stats = nullptr) = 0;
	virtual bool failed(std::string& why) const = 0;
	virtual void drawWithout() = 0;
	virtual std::shared_ptr<const c5r::decal::DecalSet> composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap) = 0;
	// The forest decals' changes since the last call (one DecalChange, appended to out): true when there is one; then
	// Scene hands the residency setDecals(composeDecals(the tap's set in force)) first, then decalChanges(out) (P13).
	virtual bool takeDecalChanges(std::vector<DecalChange>& out) = 0;
	virtual void secondLine(char* out, size_t n) = 0;
	virtual void totalsLine(char* out, size_t n) = 0;
	// Ticket 0495 (the offline scene harness): the state at the span's first frame (false, why set: off, failed, no field
	// or no layouts); the same state restored in a replay after the feed up to that frame (false, why set: the scene not
	// ready, a record the placement refuses, a plot that does not build, the cut refused; the replayed table differs from
	// the recorded one); the feature ids as recorded (setFeatureIds); the cut source's generation now (0: the empty mask).
	virtual bool spanState(ForestSpanState& out, std::string& why) const = 0;
	virtual bool restoreSpanState(const ForestSpanState& s, std::string& why) = 0;
	virtual void setRecordedFeatureIds(int forest, int jungle) = 0;
	virtual uint64_t cutGeneration() const = 0;
	// Epic 0465, ticket 4904: sample rectangles a live window wrote into the kept field since the last onField; the next
	// same-load onField builds again only the built plots whose trees read them (srv_liveterrain_derived.h).
	virtual void expectWindows(const std::vector<liveterrain::SampleBox>& boxes) { (void)boxes; }
};
typedef std::unique_ptr<SceneForest> (*SceneForestFactory)(std::unique_ptr<FowPlotTable> table);
inline SceneForestFactory& SceneForestFactorySlot()
{
	static SceneForestFactory f = nullptr;
	return f;
}
inline void RegisterSceneForestFactory(SceneForestFactory f) { SceneForestFactorySlot() = f; }
// The forest scene, or null when this program does not link server/src/forest/. table null: the feed's (MakeFeedPlotTable).
inline std::unique_ptr<SceneForest> MakeSceneForest(std::unique_ptr<FowPlotTable> table = nullptr)
{
	SceneForestFactory f = SceneForestFactorySlot();
	return f ? f(std::move(table)) : std::unique_ptr<SceneForest>();
}

class ForestScene : public SceneForest
{
public:
	enum State { kOff = 0, kWaiting, kActive, kFailed, kWithout, kStateCount };
	static const char* StateName(int s);

	explicit ForestScene(std::unique_ptr<FowPlotTable> table = nullptr);
	~ForestScene() override;
	ForestScene(const ForestScene&) = delete;
	ForestScene& operator=(const ForestScene&) = delete;
	void setTable(std::unique_ptr<FowPlotTable> t) { m_table = std::move(t); }   // tests: before init()

	// The modules on this device: the install read (forests.xml, masks, ColorKeys), the renderer. An install failure is
	// kept in failed() at once. Off: nothing (state kOff).
	void init(ID3D11Device* dev, const ForestSceneOptions& o) override;
	// Device lost or shutdown: the D3D objects; the plot table, the records and the layouts are kept (their blocks are
	// CPU data), so a rebuilt device draws the same trees.
	void releaseDevice() override;
	// The scene's kept field of a load (must outlive the next drawStage; Scene keeps it). A new loadSeq or another M_h2w:
	// the layouts of a new load; else every block rebuilt.
	void onField(const c5r::heightfield::HeightGrid& g, uint32_t loadSeq) override;
	// The host's drained batch (every one): true when a record changed (a redraw).
	bool onBatch(const SrvFeed::Batch& b) override;
	// Every loop iteration: the published feature ids (a change re-applies every record). True when records changed.
	bool poll() override;
	void setExePid(uint32_t pid) override { m_exePid = pid; }
	const CutTapReader* cutReader() const { return m_cutReader.get(); }   // tests
	// Tests and hosts with their own name source: the ids, as if published and resolved (the published table is then not
	// read until clearFeatureIds).
	void setFeatureIds(int forest, int jungle);
	void clearFeatureIds();
	// 4553: the cut mask the placement reads (null: EmptyCut). Not owned; must outlive the scene's use.
	void setCutSource(const c5r::forest::CutSource* cut);

	// The stage at its place in Scene::draw (after the water and the ice, before FowScene::afterWorld): rtv / dsv the
	// scene's colour and depth (w x h), cam the frame's camera block, terrain / tcam / top the renderer, camera and Options
	// of this frame's terrain draw, fowAnimOff the FOWAnim to use when top.fow is off (FowTerrainInputs::fowAnim). Errors
	// are counted and logged here (in play); the status is returned for tests. State kOff / kFailed / kWithout: nothing.
	c5r::Status drawStage(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	                      const VPShellCamera& cam, c5r::terrain::Renderer& terrain, const c5r::terrain::Camera& tcam,
	                      const c5r::terrain::Options& top, const float fowAnimOff[4], ForestStageStats* stats = nullptr) override;

	// The visibility rule alone (drawStage's step 1 without building): the records whose plot centre is inside a view's
	// frustum by the 64-unit margin. Needs a field (else empty).
	std::vector<ForestVisiblePlot> visiblePlots(const VPShellCamera& cam) const;

	// The install failure (once true, stays): why = the text for the report.
	bool failed(std::string& why) const override
	{
		if(m_state == kFailed || m_state == kWithout)
			why = m_failWhy;
		return m_state == kFailed;
	}
	// No fatal hook (tests, test hosts): LOUD once, then the scene draws on without the trees (state kWithout).
	void drawWithout() override;
	int state() const { return m_state; }

	// ---- the Tree_Shadow decals (4552's module; the block comment at the top) ----
	void setPlotHook(ForestPlotHook fn, void* user)
	{
		m_hook = fn;
		m_hookUser = user;
	}
	const c5r::forest::Placement& placement() const { return m_place; }
	std::shared_ptr<const c5r::decal::DecalSet> composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap) override;
	bool takeDecalChanges(std::vector<DecalChange>& out) override;
	size_t shadowCount() const { return m_shadows.size(); }                       // decals of plots built in a view
	const c5r::forest::ShadowDecal* shadowAt(int32_t x, int32_t y) const;          // null: none
	const std::vector<c5r::decal::TileOrigin>& marks() const { return m_marks; }   // tile coordinates, sorted by y then x
	const float* worldRect() const { return m_worldRect; }

	// ---- tests ----
	const ForestFeatureIds& featureIds() const { return m_ids; }
	ID3D11ShaderResourceView* screenOverlaySrv() const { return m_ovlSrv; }
	ID3D11Texture2D* screenOverlayTexture() const { return m_ovlTex; }
	uint32_t loadSeq() const { return m_loadSeq; }
	uint32_t layoutSeed() const { return m_layoutSeed; }
	const ForestStageStats& lastStage() const { return m_last; }

	void secondLine(char* out, size_t n) override;
	void totalsLine(char* out, size_t n) override;
	bool spanState(ForestSpanState& out, std::string& why) const override;          // ticket 0495
	bool restoreSpanState(const ForestSpanState& s, std::string& why) override;
	void setRecordedFeatureIds(int forest, int jungle) override { setFeatureIds(forest, jungle); }
	uint64_t cutGeneration() const override { return m_cut ? m_cut->generation() : 0; }

	// totals
	int64_t framesDrawn = 0, framesEmpty = 0, framesHeld = 0, plotsBuilt = 0, blocksDrawn = 0, drawCalls = 0;
	int64_t recordsAdded = 0, recordsRemoved = 0, recordsChanged = 0, rereads = 0, overflows = 0, featureEvents = 0;
	int64_t outsideEvents = 0, buildErrors = 0, recordErrors = 0, overlayErrors = 0, drawErrors = 0, layoutsMade = 0;
	int64_t decalComposes = 0, decalMerges = 0, tapShadowRefusals = 0, mergeErrors = 0, shadowErrors = 0, decalChangesMade = 0;
	int64_t markPlots = 0;
	int64_t cutChanges = 0, cutRebuilds = 0, cutReaders = 0;

private:
	void fail(const std::string& why);
	bool loudDue(int64_t n) const { return n <= 20 || n % 100 == 0; }
	void readTable(const char* why);
	void applyAll(const char* why);   // every record from m_rec (placement must be ready)
	bool applyPlot(uint32_t idx);     // one plot's record from m_rec; true when it changed
	bool makeLayouts();
	void dropAll();
	bool ensureTarget(uint32_t w, uint32_t h, std::string& why);
	void releaseTarget();
	uint32_t styleAt(const c5r::terrain::Renderer& terrain, int32_t x, int32_t y) const;
	bool centre(int32_t x, int32_t y, float out[3]) const;
	void hook(int32_t x, int32_t y, const c5r::forest::PlotRecord* rec, const c5r::forest::PlotBlocks* b);

	ID3D11Device* m_dev = nullptr;
	ForestSceneOptions m_opt;
	int m_state = kOff;
	std::string m_failWhy;

	// install
	c5r::forest::ForestParams m_params;
	std::vector<c5r::forest::TileMask> m_masks;
	bool m_haveInstall = false;
	std::string m_xmlOrigin;
	c5r::forest::Renderer m_renderer;
	std::vector<c5r::forest::Block> m_blocks;   // the frame's blocks (cb7 copies)
	ID3D11ShaderResourceView* m_colorKey[5] = {};
	ID3D11ShaderResourceView* m_white = nullptr;   // the fog-off FOW mask (visible)
	ID3D11ShaderResourceView* m_black = nullptr;   // the fog-off cloud base
	// the Screen Overlay target
	ID3D11Texture2D* m_ovlTex = nullptr;
	ID3D11RenderTargetView* m_ovlRtv = nullptr;
	ID3D11ShaderResourceView* m_ovlSrv = nullptr;
	uint32_t m_ovlW = 0, m_ovlH = 0;
	int64_t m_ovlMade = 0;

	// session random stream
	uint32_t m_sessionSeed = 0, m_layoutSeed = 0;
	uint32_t m_loads = 0;

	// the field
	const c5r::heightfield::HeightGrid* m_grid = nullptr;
	uint32_t m_loadSeq = 0;
	bool m_haveField = false;
	float m_fieldH2w[16] = {};

	// the plot table
	std::unique_ptr<FowPlotTable> m_table;
	std::vector<SrvFeed::PlotRecord> m_rec;
	uint32_t m_w = 0, m_h = 0, m_tableGen = 0;
	bool m_haveTable = false;
	uint64_t m_lastDropped = 0;
	double m_firstTableMs = -1;

	// the feature ids
	ForestFeatureIds m_ids;
	bool m_idsExternal = false;

	// the placement
	c5r::forest::Placement m_place;
	c5r::forest::EmptyCut m_emptyCut;
	const c5r::forest::CutSource* m_cut = nullptr;
	bool m_layoutsOk = false;
	// the cut tap's reader (ticket 4553): made once the pid is known, kept per pid
	std::unique_ptr<CutTapReader> m_cutReader;
	std::unique_ptr<c5r::forest::DenseCut> m_restoredCut;   // ticket 0495: the recorded mask (restoreSpanState)
	float m_restoredOx = 0, m_restoredOy = 0;
	uint32_t m_exePid = 0, m_cutPid = 0;
	uint64_t m_cutGenSeen = 0;
	double m_cutWaitFromMs = -1;
	bool m_cutEverGrid = false;
	bool pollCut();
	void rebuildBuilt();
	// epic 0465, ticket 4904: live windows pending for the next same-load onField (empty: every block rebuilt)
	std::vector<liveterrain::SampleBox> m_liveBoxes;
	bool reheightWindows(const std::vector<liveterrain::SampleBox>& boxes);
public:
	void expectWindows(const std::vector<liveterrain::SampleBox>& boxes) override
	{
		m_liveBoxes.insert(m_liveBoxes.end(), boxes.begin(), boxes.end());
	}
	liveterrain::ReheightStats lastReheight;   // tests and the log
private:
	// the plots the scene saw built (key y << 32 | x -> the cut generation): the placement builds on first use and again on
	// a cut generation change or an invalidate; the scene mirrors that to know which plots a frame built (hook, counters)
	std::unordered_map<uint64_t, uint64_t> m_built;

	// the Tree_Shadow decals (4552)
	ForestPlotHook m_hook = nullptr;
	void* m_hookUser = nullptr;
	float m_worldRect[4] = {0, 0, 0, 0};
	std::vector<std::unique_ptr<c5r::forest::ShadowDecal>> m_shadows;   // built plots' decals (quads > 0), oldest first
	std::vector<c5r::forest::ShadowDecal> m_pending;                    // built or dropped since takeDecalChanges
	std::vector<c5r::decal::TileOrigin> m_marks;                        // the load's area marks (tile coordinates)
	std::vector<c5r::decal::TileOrigin> m_pendingMarks;                 // marks added since takeDecalChanges
	uint32_t m_forestGen = 0;                                          // DecalChanges made (the band's offset)
	uint64_t m_shadowVersion = 0;                                      // +1 on any change of m_shadows or m_marks
	std::shared_ptr<const c5r::decal::DecalSet> m_mergedTap, m_merged; // the last merge's input and output
	uint64_t m_mergedVersion = 0;
	uint32_t m_refusedTapGen = 0xFFFFFFFFu;
	std::string m_lastShadowErr, m_lastMergeErr;
	void addShadow(int32_t x, int32_t y, const c5r::forest::PlotBlocks& b);
	void dropShadow(int32_t x, int32_t y);
	void dropAllShadows();
	void addMarks(const std::vector<c5r::decal::TileOrigin>& tiles);
	void loadMarks();

	// per frame / second
	ForestStageStats m_last;
	int m_drawnSec = 0, m_builtSec = 0;
	int64_t m_blocksSec = 0;
	uint32_t m_visibleLast = 0;
	double m_cpuSec = 0;
	std::string m_lastErr, m_lastOvlErr, m_lastBuildErr, m_lastOutside, m_lastRecErr;
};

// One step of the fatal route, run by the host after every Scene::poll() (OverlayFatalStep's pattern; inline on the
// interface so that the host links without server/src/forest/): when the forest's install failed and `done` is false:
// done = true, then fatal(kForestFatalExitCode, why) (server.exe: SrvRemote::ReportAndExit, which does not return).
// fatal null: LOUD once, the scene draws on without the trees (drawWithout). Never for a fault in play. f null: nothing.
const int kForestFatalExitCode = 5;   // the overlay's code (0167): the 64-bit path cannot start its picture
inline void ForestFatalStep(SceneForest* f, void (*fatal)(int code, const char* why), bool& done)
{
	std::string why;
	if(done || !f || !f->failed(why))
		return;
	done = true;
	if(fatal)
	{
		RNote("forest: the 64-bit path cannot go on: %s; the server ends (exit code %d)", why.c_str(), kForestFatalExitCode);
		fatal(kForestFatalExitCode, why.c_str());
	}
	else
	{
		RNote("forest: %s; no fatal hook (HostConfig::fatal): the server draws on without the trees", why.c_str());
		f->drawWithout();
	}
}

}  // namespace srvrender
