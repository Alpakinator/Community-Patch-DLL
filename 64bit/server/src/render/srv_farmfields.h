// srv_farmfields.h - the server's own farm fields and farm tracks in server.exe (epic 0528, decision D186; ticket 4851 the
// frame, 0528 C the layout and the wiring). Written 2026-11-18 for this repository (GPL-3.0-or-later).
//
// What it does: from the feed's plot table (FowPlotTable / SrvFeed::PlotRecord: improvement, improvementState, the
// pillaged bit of flags, artType, fow) it keeps the load's farm plots, has c5r::farmfields produce their field and track
// decal records (the layout behind c5r::farmfields::layoutFarms), and joins them to the decal tap's set the way the
// forest joins its Tree_Shadow decals (srv_forest_scene.h: composeDecals / takeDecalChanges): composeDecals(tap) is the
// tap's set with its farm records dropped (removeTapFarm, the EXE's farm builder still running) and the server's added;
// takeDecalChanges() is the rectangles and area tiles of the farms that changed, as one DecalChange in a generation band of
// its own (kFarmDecalGenBase + n: above every tap generation, so the residency never skips it, trees.md 12.4's trap).
//
// NOT WIRED (0528 C, after the MC frees srv_render_scene.*): Scene would own one FarmFieldsProducer, call onBatch with
// each drained batch, setWorld / setGameSeed with each field (HeightGrid: worldRect (0, 0, samplesX - 1, samplesY - 1),
// mapSeed), setFarmTypes once the residency's view has read farm_types.xml (FarmTypesFromXml), setFarmImprovementId once
// the feed's DLL side has published the farm's id by name, then refresh() every loop iteration and, when the tap's set or
// the producer changed, hand the residency setDecals(forest.composeDecals(farm.composeDecals(tap))) and decalChanges.
//
// Inputs:
//   * the farm improvement's id and the wheat resource's: by name, never fixed numbers (as the forest takes
//     FEATURE_FOREST). The render thread never reads the DLL (bug 0103); the DLL side of the feed reads every improvement's
//     and resource's art define at each snapshot into srv_farm_ids.h (ticket 4852), which refresh() polls
//     (useFeedIds, the default); setFarmImprovementId / setWheatResourceId override it (tests, a host with its own source).
//     -1: no farm known, no farms.
//   * the game's seed: a value a save keeps, so a save shows the same fields on every load: HeightGrid::mapSeed (the
//     PreGame map random seed CreateTerrain seeds with, carried by the height-field tap) [I that a save keeps it; the two
//     huge recordings of 0366 and 4111 carry mapSeed 0 (render/evidence/0528/compare_huge.txt): a map seed of 0 gives
//     every game the same pattern, deterministic but not per game; 0528 D checks what the field tap reads there].
//   * farm_types.xml's shape (the children of each category and their spec attributes), from the file the residency reads
//     with the mods above the install (terrain-decals.md 12.2).
// Every call on the render thread. Failures in play are LOUD (the first 20, then every 100th), counted, never fatal: a
// production the invariants refuse keeps the farms in force; a compose the module refuses hands on the tap's set as is.
#pragma once
#include "srv_render_common.h"
#include "srv_fow_scene.h"      // FowPlotTable, MakeFeedPlotTable (the feed's plot table seam)
#include "srv_decal_tap.h"     // DecalChange (a plain struct)
#include "srv_farm_ids.h"      // the farm's and wheat's ids by art define (ticket 4852)
#include "feed/srv_feed.h"
#include "c5r/decal/decal.h"
#include "c5r/farmfields/buildings.h"   // ticket 0653: the farm buildings' records
#include "c5r/farmfields/farmfields.h"
#include "c5r/farmfields/footprint.h"   // ticket 4853: the fields' cut mask
#include "c5r/farmfields/layout.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace c5r { namespace heightfield { struct HeightGrid; } }

namespace srvrender {

// Ticket 0653 (D230): the farm buildings as one plain list per change, for the model scene (ticket 0654 draws them).
// The contract (farm-fields-layout.md 16.4): a list is never changed after it is handed out (a change makes a new list);
// `generation` moves on with every change of any record (a farm built, pillaged, repaired, removed or changing owner or
// era, a new row table, a new height field) and with every new load, whose first list is empty; nothing else moves it.
// The records are in the order of the production in force (ascending by y then x), one per farm plot of the producer's
// revealed-only input (a farm built in the fog has no record until it is revealed, as its fields).
struct FarmBuildingList
{
	uint64_t generation = 0;
	uint32_t loadSeq = 0;           // setWorld's loadSeq of the load it belongs to
	uint64_t fieldsVersion = 0;     // c5r::farmfields::Producer::version() of the fields it was placed on
	std::vector<c5r::farmfields::FarmBuilding> buildings;
	size_t pillaged = 0, withoutField = 0, withoutRow = 0, withHeight = 0;   // counts over `buildings`
};

// The generations of the farm producer's DecalChanges: a band above every tap generation (the tap counts from 1 per EXE
// run) and below the forest's (kForestDecalGenBase 0xF0000000), so neither band reaches the other in practice.
const uint32_t kFarmDecalGenBase = 0xE0000000u;

// The farm improvement's names: its type IMPROVEMENT_FARM (Civ5Improvements.xml) and its art define
// ART_DEF_IMPROVEMENT_FARM [I: the art define's text, as the forest's ART_DEF_FEATURE_FOREST].
const char* const kFarmImprovementType = "IMPROVEMENT_FARM";
const char* const kFarmImprovementArtTag = "ART_DEF_IMPROVEMENT_FARM";
// True for either name (exact text).
bool IsFarmImprovementName(const char* typeOrArtTag);

// farm_types.xml's text -> its shape (ParseFarmTypes, then c5r::farmfields::farmTypesFromAttributes). False (why) when
// the text does not parse.
bool FarmTypesFromXml(const std::string& xml, c5r::farmfields::FarmTypes& out, std::string& why);

// The farm plots of a plot table (width x height records, index y * width + x), ascending by y then x. A table whose size
// is not width x height gives none (false).
// revealedOnly (farm-fields-layout.md 2.1, MC #5072): the EXE lays out only plots whose REVEALED improvement for the
// active team is a farm. PlotRecord.improvement is that revealed value once a GameplayPlotStateChange was seen for the plot
// (known bit 1: the EXE asked for the plot's layout; at a load it does so for every revealed, developed plot,
// terrain-decals.md 5.F); before that it is the getter's true value. So a plot counts when known bit 1 is set and its
// improvement is the farm. Fallbacks, so that a missing input never empties the world: a table in which no plot has known
// bit 1 (headless, no EXE: the notification never fires) takes the getter's value on plots with fow bit 0 (revealed), and a
// table in which no plot has fow bit 0 either (no fog read: no active team) takes every plot.
bool FarmPlotsFromTable(const std::vector<SrvFeed::PlotRecord>& rec, uint32_t width, uint32_t height, int farmId, bool revealedOnly,
                        std::vector<c5r::farmfields::FarmPlot>& out, int wheatResourceId = -1);

// The fallback game seed (farm-fields-layout.md 13.1, MC): FNV-1a 32 over every plot's static fields (terrain, plot type,
// feature, the natural-wonder bit), taken once per load; used when the field's mapSeed is 0. The same save gives the same
// value on every load; a later save of the same game may differ (its features) [I].
uint32_t StaticPlotSeed(const std::vector<SrvFeed::PlotRecord>& rec);

struct FarmFieldsOptions
{
	bool removeTapFarm = true;   // drop the tap's farm records (the EXE's builder still running); false: a tap set with farm
	                             // records is handed on as is (LOUD once per tap generation), as the forest does
	bool revealedOnly = true;    // only revealed farms, as the EXE (FarmPlotsFromTable)
	bool useFeedIds = true;      // poll srv_farm_ids.h in refresh() (false: only setFarmImprovementId / setWheatResourceId)
	bool threaded = true;        // produce on a worker thread (MC #5058: never on the render thread); false: inside refresh()
	unsigned layoutThreads = 0;  // threads of a load's block layout (0: the hardware's less one, 1 to 8)
	double idsWaitMs = 20000;    // no farm id this long after the first plot table: LOUD once (never a silent world without
	                             // fields, D118's spirit); the tap's set stays in use
	bool useFeedPlayers = true;  // ticket 0653: the owners' eras from the feed's player rows (SrvFeed::CopyPlayers at each table
	                             // read, Batch::players in onBatch); false: only setOwnerEra (tests)
};

class FarmFieldsProducer
{
public:
	explicit FarmFieldsProducer(std::unique_ptr<FowPlotTable> table = nullptr);   // null: MakeFeedPlotTable at the first read
	~FarmFieldsProducer();   // stops and joins the worker
	FarmFieldsProducer(const FarmFieldsProducer&) = delete;
	FarmFieldsProducer& operator=(const FarmFieldsProducer&) = delete;

	void setOptions(const FarmFieldsOptions& o);
	// The inputs; each one that changes makes the next refresh() produce again.
	void setFarmImprovementId(int id);                        // -1: unknown (no farms)
	void setWheatResourceId(int id);                          // -1: unknown (no wheat fields)
	void setFarmTypes(const c5r::farmfields::FarmTypes& t);
	// farm_types.xml from the install with the mods' folders above it (the decal names' view, terrain-decals.md 12.2: the
	// residency reads the same file), read on the worker (indexing the view takes about 0.4 s) or here when not threaded;
	// refresh() puts the result in force (setFarmTypes) or writes a LOUD line (no fields of the server's own then).
	void loadFarmTypesFrom(const std::wstring& install, const std::vector<std::wstring>& looseRoots);
	void setGameSeed(uint32_t seed);
	// The field of a load: its world rectangle and map size in plots (the plot table's). A new loadSeq is a new load: the
	// farms, the marks, the pending change and the footprint are dropped and the table is read again.
	void setWorld(const float worldRect[4], uint32_t loadSeq);
	// Ticket 4853: the height field's ox / oy (HeightGrid::ox / oy), the frame of the footprint's bitsAt (cutCell's rule).
	void setCutFrame(float ox, float oy);

	// Ticket 4853 (farm-fields-layout.md 10.1, 14): the fields' footprints (bit 0x01) of the production in force, for the
	// forest's cut source (Scene publishes it through CutOverlaySlot, srv_cut_tap.h). Built on the worker with each
	// production; replaced only when its cells changed (hash), so an unchanged layout rebuilds no tree. null before the
	// load's first production. footprintVersion() moves on with every replacement and every new load.
	std::shared_ptr<const c5r::farmfields::FieldFootprint> footprint() const { return m_footprint; }
	uint64_t footprintVersion() const { return m_footprintVersion; }

	// The host's drained batch (every one): a new snapshot, an overflow or dropped events re-read the table; changed plots
	// copy it again and compare the farm plots. True when the farm plots changed (refresh() will produce).
	bool onBatch(const SrvFeed::Batch& b);
	// Every loop iteration: puts a finished production in force (true when the decals changed: then compose again and take
	// the change) and, when an input changed, hands the new input to the worker (threaded) or produces here. Never fatal.
	bool refresh();
	// tests: wait (at most ms) until the worker has no job and no unread result, then refresh(); true when it got idle
	bool settle(double ms);
	// a production is queued or running on the worker
	bool busy() const;

	// The set the residency bakes from: the tap's set with the farm decals (the forest's composeDecals takes this one).
	// Before the farm id is known (setFarmImprovementId), and on any refusal, the tap's set as it is (LOUD, counted).
	std::shared_ptr<const c5r::decal::DecalSet> composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap);
	// The farm decals' changes since the last call (one DecalChange appended): true when there is one.
	bool takeDecalChanges(std::vector<DecalChange>& out);

	// ---- the scene bundle (ticket 4852; render_config render_farmfields_state, farm-fields-layout.md 13.3) ----
	// The input of the decals in force as c5r::farmfields::spanText, with the ids (false, why: no production in force yet).
	bool spanState(std::string& text, std::string& why) const;
	// The player: that input restored. The producer then reads no plot table (onBatch and setWorld's read do nothing), takes
	// the recorded seed and ids, and produces from the recorded farms once farm_types.xml is read. False (why): the text
	// refused (parseSpanText).
	bool restoreSpanInput(const std::string& text, std::string& why);
	bool restored() const { return m_frozen; }
	// The producer's version (c5r::farmfields::Producer::version): the recorder notes a change inside the span.
	uint64_t version() const { return m_prod.version(); }

	// ---- ticket 0653: the farm buildings (farm-fields-layout.md 16; drawn by ticket 0654) ----
	// The list in force: never null (before the first production an empty list of generation 0). Placed on the render thread
	// in the refresh() that puts a production in force, so a farm built, pillaged, repaired or removed changes its record in
	// the same step as its fields (c5r::farmfields::placeBuildings: a few hundred farms take well under a millisecond [I]).
	std::shared_ptr<const FarmBuildingList> buildings() const { return m_buildings; }
	uint64_t buildingsGeneration() const { return m_buildings->generation; }
	// The FARM rows the buildings choose from (c5r::landmarks::ModelLibrary::farmRows, by the scene once its library is
	// loaded: ticket 0654). Each refused row is one LOUD line naming it (D118), written once per distinct table; a table
	// without any row is one LOUD line too (every record then has row -1: nothing to draw, never a stand-in).
	void setFarmRows(const c5r::farmfields::FarmRowChoice& rows);
	// The hook for the records' height (ticket 0654 hands the scene's field of the load): null = none, the records say
	// hasHeight false and the drawer samples the height itself.
	void setHeightField(std::shared_ptr<const c5r::heightfield::HeightGrid> g);
	// An owner's era (EraTypes; -1 unknown). The feed's player rows set it too (FarmFieldsOptions::useFeedPlayers).
	void setOwnerEra(int player, int era);
	int ownerEra(int player) const { return player >= 0 && player < 64 ? m_facts.eraOfPlayer[player] : -1; }
	int64_t buildingLists = 0, rowRefusalLines = 0;

	// ---- ticket 0657: the buildings' points for the EXE's own placer (shell/common/vp_farm_points.h, <base>.Farm) ----
	// With ServerFarmFields = 1 the farm guard in the EXE hands each farm's point to the EXE's placer (farm_guard.h), so the
	// EXE's building stands where this producer's record says (c5r::farmfields::stockPlacement). Published on the render
	// thread: PRODUCING with the new loadSeq at every new load (setWorld), DONE with one entry per record of the list in
	// force after every placement of a production of that load (rebuildBuildings). Never fatal: a section that cannot be
	// made is one LOUD line, and the EXE's buildings then stand at their plot centres (as before 0657).
	// base: the version-3 section's name (<base>.Farm is made beside it); "" = none.
	void setPointSection(const std::wstring& base);
	// The EXE's world frame of the points: the height field's sx, sy, ox, oy (world = sample x sx - o).
	void setPointFrame(float sx, float sy, float ox, float oy);
	// Tests: publish into a caller's block of VPFARM_SECTION_BYTES (zeroed by the caller) instead of a section; null = none.
	void attachPointMemory(void* block);
	int64_t pointPublishes = 0, pointDropped = 0;

	const c5r::farmfields::Producer& producer() const { return m_prod; }
	const c5r::farmfields::LayoutInput& input() const { return m_in; }
	void totalsLine(char* out, size_t n) const;

	// totals
	int64_t rereads = 0, overflows = 0, plotChanges = 0, productions = 0, productionErrors = 0, composes = 0, composeErrors = 0;
	int64_t tapRefusals = 0, decalChangesMade = 0, tableErrors = 0, loads = 0, composesWithoutId = 0, staleResults = 0;
	bool idsMissing = false;            // the LOUD line of idsWaitMs was written
	double lastProduceMs = 0;           // the last production's time on the worker (or in refresh)
	uint64_t lastMisses = 0;            // block graphs laid out by it (0: every block from the load's cache)
	int64_t footprints = 0, footprintErrors = 0;   // ticket 4853: footprints put in force; builds refused (LOUD)
	double lastFootprintMs = 0;         // the last footprint's build time (on the worker)

private:
	bool loudDue(int64_t n) const { return n <= 20 || n % 100 == 0; }
	bool readTable(const char* why);   // true when the farm plots changed
	bool farmsFromRecords();   // m_rec -> m_in.plots; true when they changed
	void startWorker();
	void workerLoop();
	bool takeResult();         // a finished production put in force; true when the decals changed

	// the worker (MC #5058): one job slot (a newer input replaces a queued one), one result slot
	struct Job
	{
		c5r::farmfields::LayoutInput in;
		uint32_t loadSeq = 0;
		unsigned threads = 0;
		float cutOx = 0, cutOy = 0;   // ticket 4853: the footprint's frame
	};
	struct Result
	{
		c5r::farmfields::LayoutInput in;
		uint32_t loadSeq = 0;
		c5r::Status st;
		c5r::farmfields::FarmLayout layout;
		c5r::farmfields::FarmDecals decals;
		double ms = 0;
		uint64_t misses = 0;
		std::shared_ptr<c5r::farmfields::FieldFootprint> footprint;   // ticket 4853 (null: not built)
		c5r::Status footprintSt;
		double footprintMs = 0;
	};
	// ticket 4853: the footprint of a successful production (on the worker, or in refresh when not threaded)
	static void BuildFootprint(Result& r, float ox, float oy);
	std::thread m_worker;
	mutable std::mutex m_mx;
	std::condition_variable m_cv;
	bool m_stop = false, m_running = false, m_working = false;
	std::unique_ptr<Job> m_job;
	std::unique_ptr<Result> m_result;
	// farm_types.xml: the source the worker reads, then its result
	struct TypesSource
	{
		std::wstring install;
		std::vector<std::wstring> roots;
	};
	struct TypesResult
	{
		bool ok = false;
		c5r::farmfields::FarmTypes types;
		std::string why;
	};
	std::unique_ptr<TypesSource> m_typesSrc;
	std::unique_ptr<TypesResult> m_typesRes;
	static TypesResult ReadFarmTypes(const TypesSource& s);
	bool m_typesFailed = false;
	c5r::farmfields::BlockCache m_cache;   // the worker's (and refresh()'s when not threaded)
	uint32_t m_cacheLoad = 0xFFFFFFFFu;
	// the seed: the field's mapSeed, else the load's static plot seed
	uint32_t m_mapSeed = 0, m_staticSeed = 0;
	bool m_haveStaticSeed = false;
	bool m_frozen = false;   // restoreSpanInput: the recorded input, no table reads
	double m_firstTableMs = -1;

	FarmFieldsOptions m_opt;
	std::unique_ptr<FowPlotTable> m_table;
	std::vector<SrvFeed::PlotRecord> m_rec;
	uint32_t m_w = 0, m_h = 0, m_tableGen = 0;
	bool m_haveTable = false;
	uint64_t m_lastDropped = 0;
	int m_farmId = -1, m_wheatId = -1;
	uint32_t m_idsGen = 0xFFFFFFFFu;   // the srv_farm_ids.h generation last applied
	bool m_haveWorld = false, m_haveTypes = false;
	uint32_t m_loadSeq = 0;
	c5r::farmfields::LayoutInput m_in;
	c5r::farmfields::Producer m_prod;
	bool m_dirty = false;
	uint32_t m_gen = 0;                                 // DecalChanges made (the band's offset)
	float m_cutOx = 0, m_cutOy = 0;                     // ticket 4853: setCutFrame
	uint32_t m_seedLogged = 0, m_seedLoggedLoad = 0xFFFFFFFFu;   // ticket 4853: the seed line's last values
	std::shared_ptr<const c5r::farmfields::FieldFootprint> m_footprint;
	uint64_t m_footprintVersion = 0;
	uint32_t m_refusedTapGen = 0xFFFFFFFFu;
	std::string m_lastErr;
	// ticket 0653: the farm buildings
	void rebuildBuildings();          // places the records of the production in force; a new list only when they changed
	void newBuildingList(std::vector<c5r::farmfields::FarmBuilding>&& b);
	void readPlayers();               // SrvFeed::CopyPlayers -> the owners' eras
	std::shared_ptr<const FarmBuildingList> m_buildings{std::make_shared<FarmBuildingList>()};
	uint64_t m_buildingsGen = 0;
	bool m_buildingsDue = false;
	c5r::farmfields::BuildingFacts m_facts;
	c5r::farmfields::FarmRowChoice m_rows;
	bool m_haveRows = false;
	std::shared_ptr<const c5r::heightfield::HeightGrid> m_height;
	// ticket 0657: the points' section
	void publishPoints();
	HANDLE m_pointMap = nullptr;
	void* m_points = nullptr;          // the mapped view or the test's block
	bool m_pointsOwned = false;        // m_points is a view of m_pointMap
	float m_psx = 1.0f, m_psy = 0.866f, m_pox = 0.0f, m_poy = 0.0f;
	bool m_pointFrame = false;
	uint32_t m_pointGen = 0;
	uint32_t m_pointLastState = 0, m_pointLastLoad = 0, m_pointLastCount = 0xFFFFFFFFu;
	uint64_t m_pointLastList = 0;
};

}  // namespace srvrender
