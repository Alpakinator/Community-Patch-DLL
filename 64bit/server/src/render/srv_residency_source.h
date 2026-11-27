// srv_residency_source.h - the server's own baked tiles as a srvrender::TileCacheSource: an adapter over epic 3400's
// c5r::bake::Residency (render/include/c5r/bake/residency.h, D92) and a c5r::bake::TileBaker (the pipeline of issue 3404,
// or the stand-in for tests), for the renderer inside server.exe (epic 3300, issue 3308).
//
// The seam's calls are forwarded one to one (onField, onPlotsChanged, beforeDraw, idle, release, statsLine); the
// residency wants the EXE's tile of one 64-sample plot (wantedTilePlots() = 1). The baker's inputs come from the player's
// install (TerrainDef.csv, layer textures; c5r::gamefs) and the EXE's shader section, both read-only (D83); the radius is
// [Renderer] BakedTileCameraRadius of VP64Options.ini through SrvOptions() (srv_render_options.cpp), unless server.cfg
// render_bake_radius names one. The same calls shell_server.exe --scene terrain --bake makes (shell/server/shell_server.cpp
// TerrainInit / ApplyField / TerrainFrame / TerrainPump at 9a2a63e, team 3400's; that file is unchanged).
// Every call on the render thread, with its device and immediate context. No DLL access.
#pragma once
#include "srv_render_common.h"
#include "srv_tile_source.h"
#include "c5r/bake/residency.h"
#include "c5r/bake/layers.h"
#include "c5r/bake/standin.h"
#include "c5r/bake/pipeline.h"   // ticket 0714: PageWarmReport
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "feed/srv_feed.h"
#include "srv_decal_tap.h"     // epic 0166, issue 4105: DecalChange
#include "c5r/decal/decal.h"
#include <map>
#include <memory>
#include <vector>

namespace c5r { namespace bake { class PipelineBaker; struct PipelineLoaderCounters; struct PrepCounters; } }   // 0371, 0698, 0712

namespace srvrender {

// ---- Decals (epic 0166, issue 4105; research/05-graphics-composition/terrain-decals.md 12) ---------------------------
// The scene owns the decal tap's reader and hands each applied set to setDecals() and each generation's changes to
// decalChanges(), in that order (the re-bakes read the new set: P13). Every bake asks the set for the tile's decals
// (c5r::decal::decalsForTile at the tile's origin 64 x, 64 y) and passes them in BakeRequest::decals. areaChanged (the
// EXE's tile+0x300, issue 0305): 4105's rule, a tile with decals is baked with areaChanged (AddDecal invalidates the
// decal's rectangle); with ResidencySourceConfig::decalAreaFromExe (4111, off by default: see there) the bit of the
// set's area block when the tap carries it (the EXE's own value of every tile, decals or not), for first bakes and
// re-bakes alike. A generation's changed rectangles raise their tiles (c5r::decal::tilesForRects); with the switch on,
// so do the tiles whose area state changed (DecalChange::areaTiles: a generation may change only the bits).
// Names: the tap's farm-field references "@farm_types.xml/<category>/<child>/<attribute>" (terrain-decals.md 8.3) are
// resolved through farm_types.xml as the game's file system gives it (the mods' folders above the install: Vox Populi
// replaces the stock file), to the file name of the attribute; every name of a set is checked against that view before
// the set is used, and a name that does not resolve refuses the set (an install failure, decision 6).
struct DecalBakeCounters
{
	int64_t sets = 0, setsRefused = 0;       // setDecals accepted / refused (a name that does not resolve)
	int64_t changes = 0, changesAll = 0;     // generations applied by decalChanges; those that raised every tile (overflow)
	int64_t changesSkipped = 0;              // generations every bake of a new field already drew (the field came after the set)
	int64_t raises = 0;                      // tiles raised by decal changes
	int64_t bakesWithDecals = 0, decalsDrawn = 0, bakesFailed = 0;   // bakes that carried decals; their decals; failed ones
	int64_t namesResolved = 0;               // farm-field references resolved (per set)
	// 4111: areaChanged from the EXE's tile+0x300 (the set's area block) instead of the decal rule
	int64_t bakesAreaFromExe = 0;            // bakes whose areaChanged came from the set's area block
	int64_t areaRaises = 0;                  // tiles raised because their area state changed (DecalChange::areaTiles)
	// 0365: the bake-ahead held after a field for the load's first decal set (ResidencySourceConfig::decalAheadWaitMs)
	int64_t aheadHolds = 0, aheadHoldsEndedBySet = 0, aheadHoldsTimedOut = 0;
	double aheadHoldMsMax = 0;
	int64_t rebakeRaises = 0;                // 0365: raised tiles that had a bake at the raise (the re-bakes a generation costs)
	// ticket 0642: the decal textures read off the render thread (ResidencySourceConfig::decalLoad)
	int64_t setsHeld = 0;                    // sets held because a name was still loading (mode 2)
	int64_t setsHeldReplaced = 0;            // held sets a newer set replaced before they were applied
	int64_t setsAppliedAfterHold = 0;        // held sets applied once the loader had every name
	double heldMsMax = 0, heldMsSum = 0;     // from the first hold to the apply
	int64_t setsWaited = 0;                  // sets setDecals waited for (mode 1)
	double waitedMsMax = 0;                  // the longest such wait
	int64_t heldChangeBatches = 0;           // decalChanges calls queued behind a held set
};

// Resolves one name of a decal set: a plain name stays itself; "@farm_types.xml/<category>/<child>/<attribute>" (child
// 0-based) becomes the bare file name of that attribute in `farm` (the parsed farm_types.xml: key "<category>/<child>/
// <attribute>" in lower case -> the attribute's value). False (why) for a reference without its entry or a bad form.
bool ResolveDecalName(const std::string& name, const std::map<std::string, std::string>& farm, std::string& out, std::string& why);
// farm_types.xml's text -> the map above (categories = the root's children, children = theirs, every attribute). False (why)
// when the text does not parse as that XML (no root element, an element not closed).
bool ParseFarmTypes(const std::string& xml, std::map<std::string, std::string>& farm, std::string& why);
// Issue 0348: the activated mods, from the game's own record of them: <Documents>\My Games\Sid Meier's Civilization 5\
// cache\Civ5ModsDatabase.db (the folder of config.ini; the proxy's report reads the same file, proxy_report_mods.cpp).
// Read as a file, read-only (the render units have no SQLite: a reader of the SQLite 3 file format's table b-trees in
// srv_residency_source.cpp). Mods rows with Activated = 1; each mod's folder from its ModFiles rows (EvaluatedPath without
// its Path: the folder of the mod's .modinfo), re-rooted under modsRoot by its part after "...\Sid Meier's Civilization 5\
// MODS\" (a copied instance's database still names the source's Documents); a folder that is not there is not counted.
struct ActiveModFolders
{
	bool known = false;                    // the database was read: `folders` are the activated mods'
	int activated = 0;                     // Mods rows with Activated = 1
	int withoutFolder = 0;                 // activated mods with no folder under modsRoot (no ModFiles row, outside MODS, gone)
	std::vector<std::wstring> folders;     // sorted by name, each once
};
// db = the database file's bytes. False (why) when they are not an SQLite 3 file this reader takes (UTF-8, rowid tables)
// or have no Mods / ModFiles table with the columns above; `out` is then not known. Pure apart from the folder checks.
bool ReadActiveModFolders(const std::vector<uint8_t>& db, const std::wstring& modsRoot, ActiveModFolders& out, std::string& why);
// The mods' folders the decal names and textures are read from: the activated mods' folders (ReadActiveModFolders) under
// <Documents>\My Games\Sid Meier's Civilization 5\MODS (Documents from C5_DOCUMENTS, a harness instance, else
// SHGetFolderPathW(CSIDL_PERSONAL)), sorted by name; empty when there is no MODS folder. When the database is missing or
// cannot be read (a "-journal" or "-wal" beside it: a write of the game in progress), every folder of MODS (issue 4105's
// rule, the state before 0348) and the log says why. One log line per call ("decals: mods: ..."), naming the activated
// count. info (optional) gets what was read.
std::vector<std::wstring> ModFolders(std::wstring* modsRoot = nullptr, ActiveModFolders* info = nullptr);

// ---- ticket 0703: idle bakes fitted between cameras (ResidencySourceConfig::bakeFit) --------------------------------------
const double kBakeFitHoldMs = 300;       // mode 1: the fit applies this long after the last moving frame
const double kBakeFitMaxDeferMs = 250;   // one bake is admitted at the latest this long after the last admitted slice
const double kBakeFitMarginMs = 0.3;     // the window ends this much before the expected camera
const int kBakeFitMinSamples = 4;        // fewer gap or bake samples: no fit (the slice as before)
// One idle() call's decision (pure; the .cpp's tests drive it through the host). nowMs: the call's time; readyMs: the end
// of the last drawn frame's pass; gapMs / bakeMs: the estimates above; sinceAdmitMs: since the last admitted slice (< 0:
// none yet). Returns the budget to pass to Residency::idle: budgetMs or less when the window allows a bake (at least
// 0.001, so the slice makes one bake), 0 when the bake waits. *why: 0 admitted whole, 1 admitted with the budget cut, 2
// deferred, 3 forced by kBakeFitMaxDeferMs.
double BakeFitBudget(double budgetMs, double nowMs, double readyMs, double gapMs, double bakeMs, double sinceAdmitMs, int* why);
struct BakeFitCounters
{
	int64_t legacy = 0;        // idle() calls with a budget outside the fit (mode 0, at rest in mode 1, too few samples)
	int64_t admitted = 0;      // calls admitted with their whole budget
	int64_t cut = 0;           // calls admitted with the budget cut to the window
	int64_t deferred = 0;      // calls whose bake waited (no bake)
	int64_t deferredGaps = 0;  // camera gaps with at least one deferred call
	int64_t forced = 0;        // calls admitted by kBakeFitMaxDeferMs
	int64_t fitBakes = 0;      // bakes made in admitted, cut and forced calls
	int64_t gapSamples = 0, bakeSamples = 0;   // samples taken into the estimates
	double gapMs = -1, bakeMs = -1;            // the estimates at the last fitted call (-1: not enough samples)
};

struct FeedSource;     // srv_scene_sources.h (epic 0301, issue 3903)
struct SettleResult;   // srv_scene_sources.h

struct ResidencySourceConfig
{
	bool pipeline = true;            // render_baker: pipeline (3404, the EXE's rules) | standin (flat colour per tile, tests)
	std::wstring install;            // the install root (Assets/, Resource/): server.cfg game_dir unless render_bake_install
	std::wstring exe;                // CivilizationV_DX11.exe: the bake's shader section (the same file as render_exe)
	c5r::bake::ResidencyOptions res; // radius resolved by the host (option or override); rate, burst, gpu ms per s
	double budgetMs = 4.0;           // render_bake_budget_ms: CPU per beforeDraw / idle call
	c5r::bake::StandinFaults faults; // stand-in test seams (render_test_bake_fail=X,Y)
	// issue 4105: render_decals (1: the pipeline draws the tap's decals; its decal pass failing is an install failure; 0:
	// no decal pass, no reader, the picture before 0166) and render_test_decal_trace (log the first N bakes with decals)
	bool decals = true;
	int decalTrace = 0;
	// issue 0417: render_bake_warm (0, the default: first uses inside the bakes, the behaviour before 0417; 1: the pipeline
	// baker warms up at init, c5r::bake::WarmUp::AtInit, with the install's TerrainDef tables at res.sizeClass, a test arm).
	// The pipeline's slow bakes (>= 500 ms) and first uses are logged with their steps either way ("pipeline: ..." lines,
	// render.log only).
	bool warm = false;
	// issue 4111: areaChanged from the EXE's tile+0x300 (the decal set's area block) instead of the decal rule. Off by
	// default: under D33 (the product's configuration) the EXE sets tile+0x300 on far fewer tiles than a stock EXE on the
	// same save (Tiny t0008: 61 against 460, terrain-decals.md 14.1), so the bit equals the stock picture's t8 set only in a
	// stock session (the recordings); the decal rule stays the product's. No host key yet (srv_render_host.cpp).
	bool decalAreaFromExe = false;
	std::vector<std::wstring> decalLooseRoots;   // the mods' folders for the decal names and textures (ModFolders())
	// issue 0365 (terrain-decals.md 19.3): after a field with nothing resident and no decal set of the field's load in
	// force, the bake-ahead (idle(), not the visible tiles of beforeDraw) waits for the first set at most this long, so a
	// set published after the field does not bake the bake-ahead's tiles twice (once without it, once with it). The wait
	// ends when any set arrives (setDecals) or at the bound. Why 2,000 ms: on the huge save the load's first set came 4.2 to
	// 10.2 s before the field in five arms of 4112 and 0.84 s after it in one (dry-on), on Tiny 3.9 s before; a set later
	// than the bound costs what it cost before 0365 (its tiles baked twice), a wait that runs to the bound costs 2 s of
	// bake-ahead. 0: no wait (before 0365). settle() does not wait (the offline harness has no tap).
	double decalAheadWaitMs = 2000;
	// issue 0365: c5r::bake::ResidencyOptions::staleFirst is in `res` (default false, terrain-decals.md 19.4)

	// ticket 0642 (research/05-graphics-composition/terrain-bake-slow-first-use.md 7): where the decal textures are read.
	// Before 0642 a bake read each name on its first use, on the render thread (4,835 ms for 21 names on a loaded disk,
	// 0641). Now the pipeline's decal pass has a loader thread, and setDecals requests every name of the set from it:
	//   2 (hold, the product's default): a set whose names are all in is used at once, as before; otherwise it is HELD
	//     (with the decalChanges behind it) and used at the first beforeDraw / idle / pending / settle after the loader has
	//     every name. Meanwhile the set in force stays and every tile keeps its last bake; no bake reads the disk.
	//   1 (wait): setDecals waits (at most 60 s) for the loader, then uses the set: the order of sets and frames of mode 0,
	//     without the reads in the bakes. The offline players (sceneshot) run this, so a replay keeps its recording's order.
	//   0 (sync): no loader, the reads in the bakes (before 0642).
	// -1 = the environment variable C5_RENDER_DECAL_LOADER (0, 1 or 2; a test and tool switch as C5_RENDER_HELD_BAKE),
	// else 2. The stand-in baker and render_decals=0 have no decal pass: nothing is held or loaded.
	int decalLoad = -1;
	// ticket 0698 (terrain-bake-slow-first-use.md 8): where the layer textures and the style inputs are read. 1 (the
	// product's default): the pipeline's own loader thread (c5r::bake::LoadAhead::Thread) reads the five styles from init
	// and every layer texture of the TerrainDef tables from each field's setMap; a bake takes what is in and waits only for
	// a name queued or in flight, so its output is the same bit for bit. 0: the bakes read them on the render thread
	// (before 0698). -1 = the environment variable C5_RENDER_LAYER_LOADER (0 or 1), else 1. The stand-in has no pipeline.
	int layerLoad = -1;
	// ticket 0714 (frame pacing B2, terrain-bake-slow-first-use.md 9): the height pages at the field. 1 (the default):
	// onField of a new load's field (a load sequence other than the last warmed one, or a field of another size) builds
	// the height page of every land tile before it returns (PipelineBaker::warmPages, on the render thread, no
	// thread), so the bakes after it build none: after 0642 and 0698 a page build was the only first use left in a load's
	// bakes (54 of 54 `pipeline: first use` lines of a huge load on the laptop, 37 of them after the first drawn frame,
	// page 3.7-12.8 ms). The pages are the bytes the bakes would have built (a function of the grid), so the picture does
	// not change. The same field again (tiles kept, a live edit applied whole) keeps the lazy build of the pages it drops.
	// 0: the pages built by their first bake (before 0714). -1 = the environment variable C5_RENDER_PAGE_WARM (0 or 1),
	// else 1. The stand-in has no pages.
	int pageWarm = -1;

	// epic 0301, issue 3903: where takeBaseline and onPlotsChanged read the plot table (FeedSource::copyPlots). Null (the
	// default) = LiveFeed(): SrvFeed::CopyPlots, today's call. The recorder and the player put theirs here.
	FeedSource* feed = nullptr;

	// ticket 0703 (research/05-graphics-composition/terrain-bake-residency.md 8): idle bakes fitted between cameras. Under
	// exact lockstep (D242) a camera that arrives while an idle bake runs waits for the bake's end (the residency checks
	// for a camera only between bakes; 0689: wake delays up to 15-19 ms on the workstation). With the fit on, an idle()
	// slice starts a bake only when the bake is expected to end before the next camera: the next camera is expected at the
	// end of the last drawn frame's pass (the first idle() call after the draw) plus the gap the cameras have kept after
	// a pass (the 25th percentile of the last 16 gaps that no bake disturbed), a bake is expected to take the 75th
	// percentile of the last 32 bakes; the slice's budget is cut to (window - one bake). A bake that does not fit waits;
	// at most kBakeFitMaxDeferMs after the last admitted slice one bake is admitted anyway, so the bake-ahead never stops.
	//   0 (the default): off (the slices before 0703). The A/B of 2026-11-23 (terrain-bake-residency.md 8.4-8.5): under
	//     exact lockstep the EXE's next camera comes 0.1-3 ms after the pass in camera moves (workstation uncapped and
	//     the laptop at its 30 fps cap alike), so no bake fits; mode 1 only moved the bakes after the move and did not
	//     shorten the moving frames (the laptop's moving ready - publish p99 47.2 -> 51.6 ms).
	//   1: while the camera moves (a moving frame within kBakeFitHoldMs); at rest the slices are as before.
	//   2: always (an A/B arm: at rest the bake-ahead goes at the camera gaps' pace).
	// Only on the live path: the fit needs the host's yield (ResidencyOptions::yield, render_bake_yield=1), and settle()
	// (the offline players) never fits. -1 = the environment variable C5_RENDER_BAKE_FIT (0, 1 or 2; a test and tool
	// switch as C5_RENDER_HELD_BAKE), else 0. What a frame draws is decided as before: a visible tile without a bake is
	// baked before the draw (beforeDraw); only when a background bake (bake-ahead, re-bake of a stale tile) runs moves,
	// and that was already a matter of timing (which frame first shows a re-baked tile).
	int bakeFit = -1;

	// ticket 0712 (research/05-graphics-composition/terrain-bake-residency.md 9): prep ahead. The pipeline's worker computes
	// a bake's CPU products (layer list, slope classes, blend vertices and indices, grid normals: about 2.1 of a 3-4 ms bake)
	// for the tiles the residency expects to bake next (Residency::nextBakes: the near pairs' next up-res beyond this frame's
	// cap, the visible tiles beyond the burst, the idle order), and a bake takes the product or computes inline; the bytes
	// are the same (c5r::bake::prepCpu is the one code path). The GPU steps stay in the frame that needs them. The grid's
	// writers (the scene) call prepJoin before they write. 0 (the default until the laptop series): off. 1: on. -1 = the
	// environment variable C5_RENDER_PREP_AHEAD (0 or 1), else 0. The stand-in baker has no pipeline: off. settle() (the
	// offline players' residency path) requests nothing; a bake there takes a product only when one is cached.
	// C5_RENDER_PREP_VERIFY=1 (TEST ONLY): every hit is computed inline too and compared (PipelineConfig::prepVerify).
	int prepAhead = -1;

	// issue 4305: a plot's continent art type that changes in play (VP's farm, CvPlot::setImprovementType) changes the
	// tile style the bake and the terrain draw use. The EXE keeps the style it took at the load (terrain-surface.md 12.6
	// [L]), so the default is false: in play only a new table (a snapshot) is taken. true: every change is taken and the
	// tiles whose style changes are re-baked (tests). No host key.
	bool followArtChanges = false;
};

class ResidencySource : public TileCacheSource
{
public:
	explicit ResidencySource(const ResidencySourceConfig& c);   // in the .cpp since 4105 (m_decalBaker's type)
	~ResidencySource() override;
	const char* name() const override { return m_cfg.pipeline ? "residency-pipeline" : "residency-standin"; }
	// The baker and the residency on this device. A failure is logged (LOUD) by the caller; the source is then not used.
	c5r::Status init(ID3D11Device* dev, ID3D11DeviceContext* ctx);
	void onField(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid, uint32_t tilePlots, uint32_t loadSeq) override;
	// Epic 0465, ticket 4909 (live-terrain.md 8.14): a live terrain edit of the current field inside rects (after the
	// renderer's updateHeightRegion, which keeps its caches): the table is kept (no feed read, as keepTableOnNextField),
	// the art types and styles taken from it as onField's, and the residency compares only the tiles the rects reach
	// (Residency::onFieldRegion). What onField restarts for a new field (held frames, the decal generations pending, the
	// bake-ahead hold) runs on. False (why) when the residency refused it; the caller re-applies the whole field.
	bool onFieldRegion(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid, const std::vector<c5r::terrain::SampleRect>& rects,
	                   c5r::bake::RegionFieldStats* stats, std::string& why);
	void onPlotsChanged(const uint32_t* plotIndices, size_t count) override;
	void beforeDraw(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double budgetMs) override;
	void idle(ID3D11DeviceContext* ctx, double budgetMs) override;
	void release() override;
	void statsLine(char* out, size_t n) override;
	uint32_t wantedTilePlots() const override { return 1; }   // the bake's tile is the EXE's: one 64-sample plot

	// ---- beyond srv_tile_source.h ----
	bool takeVisibleChanged() { return m_ok && m_res.takeVisibleChanged(); }
	// Background work is waiting (bake-ahead, re-bakes): the host waits briefly instead of up to 100 ms.
	bool pending();
	double budgetMs() const { return m_cfg.budgetMs; }
	const c5r::bake::ResidencyStats* stats() { return m_ok ? &m_res.stats() : nullptr; }
	bool ok() const { return m_ok; }
	// issue 0371: the near pairs are on (render_bake_near), and the frames beforeDraw saw with the camera moving / settled
	bool nearOn() const { return m_ok && m_cfg.res.nearCaches; }
	int64_t movingFrames() const { return m_movingFrames; }
	int64_t settledFrames() const { return m_settledFrames; }
	// plotsChanged: every plot the feed named; plotsPassed: those with a change the bake reacts to (BakeRelevantChange in
	// the .cpp: not fog, owner, the city bit, a first layout); plotsFiltered: the rest
	int64_t plotsChanged = 0, plotsPassed = 0, plotsFiltered = 0, fieldsRefused = 0;
	bool fieldReady() const { return m_ok && m_field; }
	// epic 0465, ticket 4907 (live terrain): the plot table as the bake last took it (baseline or changed plots), its
	// generation and size; read on the render thread by Scene::liveTerrainStep
	const std::vector<SrvFeed::PlotRecord>& plotRecords(uint32_t& gen, uint32_t& w, uint32_t& h) const
	{
		gen = m_recGen;
		w = m_recW;
		h = m_recH;
		return m_rec;
	}
	// The next onField is a live-terrain edit of the same load's field: it keeps the table it has (no copyPlots, so the
	// scene bundle's feed read order is that of a load). Ignored when there is no table yet.
	void keepTableOnNextField() { m_keepTableOnce = true; }
	bool m_keepTableOnce = false;

	// ---- ticket 0424 (R1; research/05-graphics-composition/terrain-surface-verification.md 14.2, 14.3) ----
	// The fog holds every frame until its plot table and its first overlay pair are in (srv_fow_scene.h admit), and after a
	// load the pair is the copy of the first map frame's own Present, while that frame's camera was published 1.2 s
	// earlier (huge save). So the frame drawn after the install used to bake its whole visible set first (0409's catch-up,
	// 256-261 ms on the huge save) and reached the screen 2-3 EXE frames late. Now Scene::decide notes the camera of every
	// frame the fog holds (noteHeldView) and Scene::idle calls prepareHeld, which runs beforeDraw for that camera: the
	// residency's visible set with the catch-up (every visible land tile without a bake, and the view's edge), the caches
	// handed to the renderer, the frame counted as a residency frame. Without the per-frame CPU budget (no frame waits on a
	// held one), repeated while a pass baked and a visible land tile is still without a bake (at most 8 passes: the burst
	// path below the catch-up's threshold). Once per camera stamp. It waits while the field has
	// no tile styles from a plot table of its own load (m_artFromTable && !m_artStale, ticket 4555): a bake before the
	// table is Generic and would be re-baked when the table restyles it (one note per field). Switch: the environment
	// variable C5_RENDER_HELD_BAKE (1, the default; 0 = off, the behaviour before 0424), read at construction (a test and
	// tool switch, as C5_RENDER_FOW_PARTICLES: srv_render_host.cpp's ParseHostConfig is not 0424's file). True when it baked.
	void noteHeldView(const VPShellCamera& cam);
	bool prepareHeld(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r);
	bool heldOn() const { return m_heldOn; }
	// preparations made, their bakes; held cameras dropped because a frame was drawn first (a drawn frame makes a pending
	// held camera stale: beforeDraw drops it)
	int64_t heldPrepares = 0, heldBakes = 0, heldDropped = 0;
	double heldMs = 0;                         // their time

	// ---- ticket 0703: idle bakes fitted between cameras (ResidencySourceConfig::bakeFit) ----
	// The mode in force (0 off, 1 during moves, 2 always; 0 also when the host set no yield) and the counters. The source
	// writes one "residency fit 1s:" line into render.log per second in which idle() was called with a budget.
	int bakeFitMode() const { return m_fitMode; }
	const BakeFitCounters& bakeFit() const { return m_fit; }

	// ---- ticket 0712: prep ahead (ResidencySourceConfig::prepAhead) and the bakes' step times per kind ----
	// prepJoin: every writer of the grid's samples or material bytes (Scene::tapPoll's new field, loadField, a live
	// terrain window's in-place write) calls it before it writes: the worker's job in flight is waited for, its queue and
	// cache dropped (PipelineBaker::prepInvalidate). Cheap when nothing runs; a no-op with prep ahead off.
	void prepJoin();
	int prepAheadMode() const { return m_prepMode; }
	bool prepCounters(c5r::bake::PrepCounters& out) const;   // false without a pipeline
	// The bake kinds of the "pipeline steps 1s:" line (render.log, once a second with bakes): predraw (the residency's
	// visible bakes in beforeDraw), idle (bake-ahead and re-bakes), held (ticket 0424's held frames), near (the near pairs'
	// up-res, state-5 bakes), settle (the offline players' settle)
	enum BakeKind { kKindPredraw = 0, kKindIdle, kKindHeld, kKindNear, kKindSettle, kKinds };
	struct StepAgg
	{
		int64_t n = 0, hits = 0, waits = 0, misses = 0, firstUse = 0;
		double total = 0, totalMax = 0, prep = 0, prepMax = 0, normals = 0, page = 0, own = 0, textures = 0, blend = 0, style = 0,
		       post = 0, copy = 0, gpuTimer = 0, wait = 0;
	};
	const StepAgg& stepTotals(int kind) const { return m_stepTot[kind < 0 || kind >= kKinds ? 0 : kind]; }

	// ---- decals (issue 4105; the block comment above ResidencySourceConfig) ----
	// init failed because the decal pass could not start (the EXE's TerrainDecalShader blobs; decision 6: an install
	// failure, D118); why = the pipeline's text
	bool decalInitFailed(std::string& why) const
	{
		if(!m_decalInitWhy.empty())
			why = m_decalInitWhy;
		return !m_decalInitWhy.empty();
	}
	// A set the reader applied: names resolved and checked, then the set every bake reads. False (why): a name that does
	// not resolve from the install (the previous set stays; the caller takes the install-failure route).
	bool setDecals(const std::shared_ptr<const c5r::decal::DecalSet>& raw, std::string& why);
	// The changes of the generations applied since the last call, after setDecals of the newest: their tiles raised.
	void decalChanges(const std::vector<DecalChange>& ch, double signalMs);
	std::shared_ptr<const c5r::decal::DecalSet> decals() const { return m_decals; }
	const DecalBakeCounters& decalCounters() const { return m_dc; }
	// ticket 0642: the mode in force (ResidencySourceConfig::decalLoad resolved), the set held for its textures (null when
	// none), and the decal pass's loader counters (null without a pipeline decal pass)
	int decalLoadMode() const { return m_decalLoadMode; }
	// ticket 0698: the layer and style loader's mode in force (ResidencySourceConfig::layerLoad resolved) and its counters
	// (null without a pipeline)
	int layerLoadMode() const { return m_layerLoadMode; }
	const c5r::bake::PipelineLoaderCounters* layerLoaderCounters() const;
	// ticket 0714: the page warm-up's mode in force (ResidencySourceConfig::pageWarm resolved), the fields it ran for, and
	// the last one's report
	int pageWarmMode() const { return m_pageWarmMode; }
	int64_t pageWarms() const { return m_pageWarms; }
	const c5r::bake::PageWarmReport& lastPageWarm() const { return m_lastPageWarm; }
	std::shared_ptr<const c5r::decal::DecalSet> heldDecals() const { return m_heldSet; }
	const c5r::decal::DecalLoaderCounters* decalLoaderCounters() const;

	// ---- issue 4305: the tile styles (research/05-graphics-composition/terrain-surface.md 12.6) ----
	// The plots' continent art types (PlotRecord::artType), the DLL's map row-major: plotsX = the field's plots in x,
	// plotsY = its rows - 4 (the margin rows). The bake reads them through MapInputs::plotArtTypes (bake::tileStyle per
	// tile: the tile's Sunlight / Shadowlight cubes and Terrain LUT) and the scene gives them to Renderer::setTileStyles
	// (t6 / t7). Filled at onField from the feed's table when it has the field's size (else all 0, Generic, until such a
	// table arrives); a new table generation fills them again; a changed plot's art type in play only with
	// ResidencySourceConfig::followArtChanges. Every change re-bakes the tiles whose style changed (raiseTile).
	// False (out untouched) before a field or for a field of fewer than 5 rows.
	bool artTypes(const uint8_t*& art, uint32_t& plotsX, uint32_t& plotsY, bool& wrapX) const;
	// True once after the art types changed (the scene sets the renderer's styles again, then draws).
	bool takeStylesChanged()
	{
		const bool c = m_stylesChanged;
		m_stylesChanged = false;
		return c;
	}
	// Ticket 4555 (trees.md 15.4): the scene's drained batch names the feed's table generation. When it moved and this field
	// has no art types from a table yet (the table came after the field) or only an earlier load's (m_artStale), the table
	// is taken now, as onPlotsChanged takes a new table, and the tiles restyled (takeStylesChanged). Before 4555 only
	// onPlotsChanged did it, so a load whose table came after the field kept every tile Generic until a plot changed.
	// Nothing else changes: a field that has its table leaves later generations to onPlotsChanged (followArtChanges).
	// One copy of the table per generation at most. True when the styles changed.
	bool onFeedGeneration(uint32_t snapshotGen);
	uint32_t tileStyleAt(uint32_t x, uint32_t y) const;   // the bake's style of a tile (0 outside or before a field)
	// artFills: fills from a table (onField or a new generation); artPlotChanges: plots whose art type was taken in play;
	// artIgnored: plots whose art type changed in play and was not taken (followArtChanges off); styleRaises: tiles
	// re-baked because their style changed; stylesFnv: FNV-1a 64 over every tile's style byte (row-major, the field's
	// tiles), for the live comparison with the EXE's tile+0x138 (render/evidence/0169/4305/)
	int64_t artFills = 0, artPlotChanges = 0, artIgnored = 0, styleRaises = 0;
	uint64_t stylesFnv() const;
	// what the baker's last setMap received (tests: the bake's array is the scene's)
	const uint8_t* bakeMapArt = nullptr;
	uint32_t bakeMapPlotsX = 0, bakeMapPlotsY = 0;

	// epic 0301, issue 3903 (offline-scene-harness.md 6): beforeDraw(view) and idle(budgetMs()) in turn, reading
	// m_res.stats() itself (not pending()'s 100 ms cache), until the residency is not stopped, its bake-ahead is done, no
	// tile is pending and the last beforeDraw saw no visible land tile without a current bake; then settled = true. A
	// stopped residency (out of memory at radius 0) returns at once with settled = false. After maxMs: ok() with settled =
	// false and `what` naming what is still pending (c5r::Err has no timeout code; the caller refuses the shot). Errors:
	// NotFound (not started or no field), BadLayout (out null, maxMs not finite or negative).
	// Ticket 0495: the near pairs' state goes into the bundle (near.bin) and back. The settle call holds the up-res pass
	// (Residency::setNearHold) while it calls beforeDraw, so that a replay's frame runs the pass once, in Scene::draw, as
	// the live server's did. nearState: false while the near pairs are off or before a field.
	bool nearState(std::vector<uint8_t>& out) const { return m_ok && m_res.nearState(out); }
	c5r::Status restoreNear(ID3D11DeviceContext* ctx, const uint8_t* data, size_t size, uint32_t* pairs,
	                        const std::vector<c5r::bake::Residency::NearPairImage>* images = nullptr)
	{
		if(!m_ok)
			return c5r::Status::fail(c5r::Err::NotFound, "residency not started");
		return m_res.restoreNear(ctx, data, size, pairs, images);
	}
	// The texels of the pairs whose content is behind their tile (Residency::nearStaleImages): near.bin carries them.
	c5r::Status nearStaleImages(ID3D11DeviceContext* ctx, std::vector<c5r::bake::Residency::NearPairImage>& out) const
	{
		if(!m_ok)
			return c5r::Status::fail(c5r::Err::NotFound, "residency not started");
		return m_res.nearStaleImages(ctx, out);
	}
	c5r::Status settle(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double maxMs, SettleResult* out);

private:
	class DecalBaker;   // the TileBaker the residency calls: adds the tile's decals and areaChanged, then the pipeline
	friend class DecalBaker;
	std::unique_ptr<DecalBaker> m_decalBaker;
	// issue 0371: the state-5 bake into a near pair (ResidencyOptions::nearBaker): the pipeline's whole bake with the tile's
	// decals and PostInputs::nearCache, t8 as the tile's BC1 bake made it; the stand-in clears the pair to its tile colour
	class NearBaker;
	friend class NearBaker;
	std::unique_ptr<NearBaker> m_nearBaker;
	c5r::bake::PipelineBaker* m_pipe = nullptr;   // m_baker when it is the pipeline (not owned here)
	// issue 0371: the camera-moving signal (ResidencyView::moving): the frame's view or projection differs from the last
	// beforeDraw's. The shell does not publish the EXE's TS+0x75FE4; research/05 terrain-bake-passes.md 3.5.4 says what
	// this misses.
	bool m_haveLastCam = false;
	float m_lastView[16] = {}, m_lastProj[16] = {};
	void forwardDraw(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double budgetMs);
	// ticket 0424: the held frame's camera (noteHeldView), the stamp prepared last, the field's first drawn frame
	bool m_heldOn = true;
	bool m_heldPending = false;
	ViewInfo m_heldView;
	int64_t m_heldDoneStamp = -1;
	int64_t m_heldSinceField = 0, m_heldBakesSinceField = 0;
	double m_heldMsSinceField = 0;
	bool m_heldStyleNoted = false;            // the wait for the table's styles noted (once per field)
	bool m_drawnSinceField = false;           // the field's first drawn frame was logged
	int64_t m_movingFrames = 0, m_settledFrames = 0;
	bool m_lastMoving = false;                // the last forwardDraw's ResidencyView::moving
	// ticket 0703: the fit's state. m_inSettle: settle() runs (no fit, no samples). A gap: from the first idle() call after a
	// drawn frame (its pass's end, m_fitReadyMs) to the next live beforeDraw; clean while no bake ran in it.
	int m_fitMode = 0;
	bool m_fitBadEnv = false;                 // C5_RENDER_BAKE_FIT held a value it cannot take (named at init)
	bool m_inSettle = false;
	BakeFitCounters m_fit;
	struct FitRing
	{
		double v[32] = {};
		int n = 0, next = 0, cap = 32;
		void push(double x)
		{
			v[next] = x;
			next = (next + 1) % cap;
			if(n < cap)
				n++;
		}
		double pct(double p) const;   // the p-th percentile (0..1) of the samples held, -1 when empty
	};
	FitRing m_gaps, m_bakeTimes;
	int64_t m_bc1Bakes = 0;                   // bakes through DecalBaker (every BC1 bake: idle, before the draw, held)
	double m_fitReadyMs = -1;                 // the pass's end of the last drawn frame (-1: no draw yet)
	bool m_fitGapOpen = false, m_fitGapDeferred = false;
	int64_t m_fitGapBakes0 = 0;               // m_bc1Bakes when the gap opened (the gap is clean while it is unchanged)
	double m_fitMovingMs = -1e18;             // the last moving frame's beforeDraw
	double m_fitAdmitMs = -1;                 // the last admitted slice (-1: none)
	int64_t m_drawSeq = 0, m_idleSeq = 0;     // live beforeDraw calls; the draw the last idle() call followed
	double m_fitLineMs = -1;                  // the last "residency fit 1s:" line
	BakeFitCounters m_fitLast;                // the counters at that line
	bool m_fitLineDue = false;                // idle() had a budget since that line
	void fitIdle(ID3D11DeviceContext* ctx, double budgetMs);
	void fitLine(double nowMs);
	// ticket 0712: prep ahead's mode (0 off, 1 on), verify (TEST ONLY), the hints, the kind of the bakes being made and the
	// step times per kind (the second's and since init), the "pipeline steps 1s:" line
	int m_prepMode = 0;
	bool m_prepVerify = false, m_prepBadEnv = false;
	int m_bakeKind = kKindIdle;
	std::vector<c5r::bake::Residency::Hint> m_hintNear, m_hintAhead;
	StepAgg m_stepSec[kKinds], m_stepTot[kKinds];
	double m_stepLineMs = -1;
	void noteSteps(int kind);                  // after a pipeline bake: its PipelineStepTimes into the kind's sums
	void requestPrep(bool afterDraw);          // after beforeDraw (both lanes) or idle (the ahead lane)
	void stepsLine(double nowMs);
	c5r::gamefs::GameFiles m_decalFs;       // the install with the mods' folders above it (decal names and textures)
	bool m_decalFsOpen = false;
	std::shared_ptr<const c5r::decal::DecalSet> m_decals;   // resolved names
	std::map<std::string, std::string> m_farm;
	int m_farmState = 0;                    // 0 not read, 1 read, -1 failed (m_farmWhy)
	std::string m_farmWhy, m_decalInitWhy;
	DecalBakeCounters m_dc;
	int64_t m_decalsAppliedQpc = 0;
	uint32_t m_decalGenAtField = 0;          // a new field (nothing resident) arrived with this set in force
	// a generation's raised land tiles not re-baked yet: one line when the last is (the D92 latency from the publication)
	struct PendingGen
	{
		uint32_t generation = 0;
		double signalMs = 0, raisedMs = 0;
		size_t total = 0;
		std::vector<uint32_t> tiles;         // y * tilesX + x
		// 0365: the raised land tiles that had a bake at the raise (a tile without one gets its first bake with the set in
		// force: not a re-bake); one line when the last of them is re-baked
		size_t rebakeTotal = 0;
		std::vector<uint32_t> rebake;
	};
	std::vector<PendingGen> m_pendingGens;   // at most kMaxPendingGens, oldest first
	void decalTileBaked(uint32_t x, uint32_t y);
	// ticket 0642: the loader's mode, the held set and the changes behind it (ResidencySourceConfig::decalLoad)
	int m_decalLoadMode = 2;
	int m_layerLoadMode = 1;   // ticket 0698
	int m_pageWarmMode = 1;    // ticket 0714
	int64_t m_pageWarms = 0;
	bool m_pageWarmAny = false;     // a field's pages were built (m_pageWarmLoad is its load)
	uint32_t m_pageWarmLoad = 0;
	c5r::bake::PageWarmReport m_lastPageWarm;
	void warmFieldPages(uint32_t loadSeq);   // ticket 0714: onField's page warm-up
	std::shared_ptr<const c5r::decal::DecalSet> m_heldSet;
	int64_t m_heldSince = 0;                  // qpc of the first hold of the sets held since the last apply
	int64_t m_heldResolved = 0;               // the held set's farm references resolved
	struct HeldChanges
	{
		std::vector<DecalChange> ch;
		double signalMs = 0;
	};
	std::vector<HeldChanges> m_heldChanges;
	c5r::decal::DecalPass* loaderPass() const;   // the pipeline's decal pass when its loader runs, else null
	void applyDecals(const std::shared_ptr<const c5r::decal::DecalSet>& use, int64_t resolved, int64_t q0);
	bool pumpHeldDecals();                    // applies the held set once its names are in; true when it did
	// 0365: the bake-ahead's wait for the load's first decal set (ResidencySourceConfig::decalAheadWaitMs)
	bool m_aheadHold = false;
	int64_t m_aheadHoldFrom = 0, m_aheadHoldUntil = 0;
	uint32_t m_aheadHoldLoad = 0;
	bool aheadHeld();                         // true while the wait lasts (ends it at the bound)
	void endAheadHold(const char* why);
	bool loadFarmTypes(std::string& why);
	void takeBaseline();
	std::vector<SrvFeed::PlotRecord> m_rec;   // the feed's records as last compared (the baseline of the filter)
	uint32_t m_recGen = 0;
	uint32_t m_recW = 0, m_recH = 0;          // issue 4305: the table's grid
	// issue 4305: the art types (artTypes()), their table generation, the tiles' styles by the rule
	std::vector<uint8_t> m_art, m_tileStyle;
	uint32_t m_artX = 0, m_artY = 0, m_tilesX = 0, m_tilesY = 0, m_artGen = 0;
	bool m_artWrap = false, m_artFromTable = false, m_stylesChanged = false;
	// the field's load seq the art types were taken for; stale: a new load's field found only the previous load's table
	// (no snapshot since), so the next table generation is taken whatever followArtChanges says
	uint32_t m_artLoadSeq = 0;
	bool m_artStale = false;
	uint32_t m_artPolledGen = 0;             // ticket 4555: the last generation onFeedGeneration copied
	bool artFromTable();                      // m_art from m_rec when it has the map's size; true when m_art changed
	void restyle(bool raise);                 // m_tileStyle from m_art; raise = re-bake the tiles whose style changed
	std::vector<uint32_t> m_pass;
	ResidencySourceConfig m_cfg;
	ID3D11Device* m_dev = nullptr;
	c5r::gamefs::GameFiles m_fs;
	c5r::shaders::ShaderSection m_shaders;
	c5r::bake::LayerTables m_tables;
	std::unique_ptr<c5r::bake::TileBaker> m_baker;
	c5r::bake::StandinBaker* m_standin = nullptr;
	c5r::bake::Residency m_res;
	bool m_ok = false, m_field = false, m_pending = false;
	int64_t m_lastBakes = 0, m_pendingAt = 0;
};

// [Renderer] BakedTileCameraRadius as the server read it (SrvOptions(), srv_render_options.cpp); -1 = whole map.
int RenderOptionBakedTileCameraRadius();

}  // namespace srvrender
