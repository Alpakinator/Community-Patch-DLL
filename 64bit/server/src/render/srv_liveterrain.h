// srv_liveterrain.h - live terrain in server.exe's renderer: the server's region hook (epic 0465, Extras C, ticket 4907).
// Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later).
//
// Design: the Architect's way 3 (research/07-architecture-decisions/architect-2026-11-18-live-terrain-route.md 3.3, MC
// #6524, D198). The producer is 4903's c5r::liveterrain (research/05-graphics-composition/live-terrain.md section 8.1);
// this file is what the server does around it (live-terrain.md section 9, written with this code):
//
//   * once per load (a field the scene accepted): the producer is made on this module's worker thread from the piece
//     library (read from the player's install), the map frame (the EXE's grid constants block, read as data), the plot
//     table the server's bake holds for the field and the field itself (a copy). Never on the render thread.
//   * in play: when a plot's terrain, plot type or natural-wonder feature changes in the bake's plot table (P2: the feed
//     refreshes it at every LeaveUpdate), the newest table goes to the worker, which calls Producer::change. Changes that
//     arrive while the worker is busy are merged (the producer compares with its own current table, so the newest table
//     carries every earlier change).
//   * the answer: live -> the window (tap + w * (own - tap), materials own where w >= 0.5) is handed to the render thread,
//     which writes it into the scene's field and re-applies the field (the residency re-bakes only the tiles whose hash
//     changed); wait for reload -> the field stays as it is, one log line names the reason and the plots.
//
// Errors are never fatal and never silent: a producer that cannot be made (library missing or of another version, the
// constants block unreadable, a size mismatch) is one LOUD line per load and the load stays as stock draws it (no live
// change: the picture is stock's, D118 is not a degraded world); an error of one change is one LOUD line and that change
// waits for the reload.
//
// No D3D, no IPC, no DLL access here: Scene (srv_render_scene.cpp) owns one LiveTerrain and calls it on the render thread
// (Scene::liveTerrainStep, srv_liveterrain_scene.cpp). The producer is behind RegionProducer so that the tests can use a
// fake; MakeC5rProducer is the adapter over 4903's interface.
#pragma once
#include "c5r/status.h"
#include "c5r/heightfield/heightfield.h"
#include "feed/srv_feed.h"
#include "srv_liveterrain_exefield.h"   // ticket 4905: the EXE's own field (ExeLink below)
#include <stdint.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace c5r {
namespace liveterrain {
class Producer;   // c5r/liveterrain/producer.h (4903): the LandWaterEnabler's argument (ticket 4914)
}
}  // namespace c5r

namespace srvrender {
namespace liveterrain {

// The scene's hold of 4905's EXE field target for one load (closed with the next load or the scene).
struct ExeLink
{
	ExeFieldTarget t;
	bool open = false;
	ExeLink() {}
	~ExeLink()
	{
		if(open)
			CloseExeFieldTarget(t);
	}
	ExeLink(const ExeLink&) = delete;
	ExeLink& operator=(const ExeLink&) = delete;
};

// ---- The producer's inputs and outputs in this module's own terms (filled from / into c5r::liveterrain by the adapter) ----
struct PlotIn
{
	int32_t terrain = -1;      // TerrainTypes (PlotRecord::terrain)
	int32_t plotType = -1;     // PlotTypes: 0 mountain, 1 hills, 2 land, 3 ocean (PlotRecord::plotType)
	int32_t feature = -1;      // FeatureTypes
	std::string featureType;   // the feature's Type ("FEATURE_ULURU", "FEATURE_MARSH"); empty: none or not known
	bool naturalWonder = false;   // PlotRecord::flags bit 3
	uint8_t continent = 0;     // PlotRecord::artType (0..4)
	uint8_t riverEdges = 0;    // bit 0 W, bit 1 NW, bit 2 NE of river: the plot's own edges, PlotRecord::riverEdges (ticket
	                           // 0631). A record without them (known bit 2 clear: a scene bundle recorded before 0631) takes
	                           // the old rule: "a river on any edge" (flags bit 0) gives all three bits (oldRuleRecords)
	bool lake = false;         // the feed has no lake bit; a lake is a water plot, so the Water reason covers it
};
struct PlotTableIn
{
	uint32_t W = 0, H = 0;     // the DLL's map (the field has H + 4 rows of plots)
	std::vector<PlotIn> plots; // y * W + x
	// ticket 0631: records that carried no river edges (known bit 2 clear) and took the old rule, and of those the ones
	// with the river flag (riverEdges 7). 0 in a live server since 0631; every record of a journal recorded before it.
	uint32_t oldRuleRecords = 0, oldRuleRiverPlots = 0;
};

// Feature Types by id (the feed carries ids only; srv_liveterrain_features.h). Null or an empty name: the feature is
// not named for the producer, which applies its rule for that (8.1: an unnamed wonder; the marsh by id 2).
typedef std::function<std::string(int32_t featureId)> FeatureNameFn;

// The feed's table -> the producer's. False (why) when the table is not W x H.
bool PlotTableFromFeed(const std::vector<SrvFeed::PlotRecord>& recs, uint32_t W, uint32_t H, const FeatureNameFn& names,
                       PlotTableIn& out, std::string& why);
// True when a plot changed in what the field is made from: terrain, plot type, the natural-wonder bit, a wonder's
// feature id, or its river edges (ticket 0631: the edge bits; the river flag only for a record without them). Other feature changes (forest, marsh, ...) do not reach the producer from here: the marsh is a reason of
// its own (the producer's Marsh) only when it is in a window.
bool FieldRelevantChange(const SrvFeed::PlotRecord& a, const SrvFeed::PlotRecord& b);
// Ticket 0631, rule A: which river-edge rule PlotTableFromFeed used for a table, as one clause for a log line ("the
// plots' own edges", or how many records took the old any-edge rule because they were recorded before 0631).
std::string RiverEdgeRuleText(const PlotTableIn& t);

struct LoadIn
{
	uint32_t loadSeq = 0;
	std::vector<uint8_t> constants;   // the EXE's grid constants block 0xD90FBC..0xD9132C (880 bytes)
	PlotTableIn table;                // the bake's table of the field's load
	uint32_t samplesX = 0, samplesY = 0;
	std::vector<uint16_t> heights;    // the field (a copy: the worker owns it)
	std::vector<uint8_t> materials;   // empty: the tap had no byte layer (the producer refuses it: BadLayout)
	// ticket 4912: CvPreGame::mapRandomSeed of the load (CreateTerrain's srand seed, live-terrain.md 8.10), read from the
	// DLL the server hosts when the field is handed over (MapSeedSource below); haveMapSeed false: not known (a test, a
	// tool without the DLL, a journal recorded before 4912)
	bool haveMapSeed = false;
	uint32_t mapSeed = 0;
	// ticket 4914 (1): the land/water step (4913's Producer::enableLandWater) is wanted for this load. The live scene: true
	// unless C5_RENDER_LIVE_LANDWATER=0 (a test switch); the player: the journal's mark (Journal::landWater), so a bundle
	// replays what its live server did. It is used only with haveMapSeed and a factory made with a LandWaterEnabler.
	bool landWater = true;
	// ticket 0717: the producer's river keep is the band along the river edges and a land change goes through the land/
	// water step's region (live-terrain-mountain-partial.md 4). The live scene: true; the player: the journal's mark
	// (Journal::riverBand), so a bundle recorded before 0717 replays the 4908 disc and water keep its live server drew.
	bool riverBand = true;
	// ticket 0613 ([Extras] FixUnknownWonderBase, option id 14; live-terrain.md 8.13.2 (b)): a natural wonder unknown to the
	// EXE (c5r::liveterrain::unknownToExe) gets the plot's own terrain as its base instead of the EXE's Mount Fuji, live and
	// at every load. onField then posts the load-time pass right behind the create: a change job with the load's own table
	// that the producer answers with the neutral base (its window goes the way of every live window: the EXE's field first,
	// then the scene's field). The live scene: the option's effective state; the player: the journal's mark
	// (Journal::neutralWonders). False: today's producer, bit for bit.
	bool neutralWonders = false;
};
const size_t kConstantsBlockBytes = 880;

// ---- Ticket 4912: the map seed's source ----
// A function that reads the hosted DLL's CvPreGame::mapRandomSeed. server.exe registers one (server/src/
// srv_liveterrain_seed.cpp, a file only server.exe compiles: it needs the DLL's interface headers, which the render
// builders do not have); every other builder has none, and the scene logs the seed as not available. Called on the
// render thread. False (why): no DLL, no game context.
typedef bool (*MapSeedFn)(uint32_t& seed, std::string& why);
void SetMapSeedSource(MapSeedFn fn);
MapSeedFn GetMapSeedSource();
// The D118 report of a seed fault (the MC's #8552 condition 1): server.exe registers a writer beside the reader (the
// non-fatal server report of srv_exe_crash.h: the game goes on); null elsewhere.
typedef void (*MapSeedFaultFn)(const std::string& why);
void SetMapSeedFaultReport(MapSeedFaultFn fn);
MapSeedFaultFn GetMapSeedFaultReport();
// True in server.exe (the process exports the host functions of srv_exports.cpp; no test or tool binary does).
bool HostIsServerExe();
// What the scene does with the seed of one load (pure: tested). haveSource: a reader is registered; readOk, seed, why:
// its answer. A seed of 0 is not a seed (never a coast from seed 0). faultNoted: kept by the caller per session; the
// fault's LOUD line and report come once. Outcomes:
//   seed read                      -> have, one plain line
//   no reader, not server.exe      -> not have, one plain line (the render test builders' silent default)
//   no reader in server.exe        -> not have, LOUD + report once per session (a build fault), plain lines after
//   reader failed or gave 0        -> not have, LOUD every load, report once per session
// Without the seed the load's land/water changes are never drawn live (the producer adapter makes them wait).
struct MapSeedVerdict
{
	bool have = false;
	bool loud = false;
	bool report = false;      // call the fault report now (with `line`)
	std::string line;         // the log line (LOUD ones start with "LOUD: ")
};
MapSeedVerdict DecideMapSeed(uint32_t loadSeq, bool haveSource, bool readOk, uint32_t seed, const std::string& why,
                             bool isServerExe, bool& faultNoted);
// MC #9538 item 4: a load whose land/water step is not wanted (LoadIn::landWater false, C5_RENDER_LIVE_LANDWATER=0) reads
// no seed: not have, one plain line, never LOUD, never a report (the seed is the land/water step's input alone)
MapSeedVerdict MapSeedNotWanted(uint32_t loadSeq);
const uint32_t kConstantsBlockVa = 0xD90FBCu;   // .. 0xD9132C

// LandWaterInputs (ticket 4914): a land/water change of a load whose land/water step could not be turned on (the coast
// styles or the TerrainDef could not be read from the install, or enableLandWater failed: one LOUD line per factory).
enum class Wait { None = 0, NoChange, Water, Lake, RiverEdge, Marsh, Seam, TooLarge, LandWaterInputs, Error };
const char* WaitName(Wait w);

struct Rect
{
	uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
	std::vector<uint16_t> heights;   // w * h
	std::vector<uint8_t> materials;  // w * h
};
struct ChangeOut
{
	bool live = false;
	Wait reason = Wait::None;
	std::string reasonText;
	std::vector<std::pair<int32_t, int32_t>> changedPlots;
	std::vector<Rect> window;
	uint64_t inD = 0, inW1 = 0, inRing = 0;
	double producerMs = 0;
	// MC #9538 item 1 (live-terrain.md 8.10.3): the producer's ChangeResult::coastDegenerate and stripTrianglesRefused.
	// Any of them: one LOUD line for the change (LiveTerrain::takeResult), live or not
	uint32_t coastDegenerate = 0;
	uint64_t stripTrianglesRefused = 0;
	// MC #9538 item 2 (live-terrain.md 8.17): a land/water change's coast parts the producer named but did not draw, from
	// its ChangeResult. Any of the last three: one plain line for the change (LiveTerrain::takeResult; not LOUD, not a fault)
	bool landWater = false;
	uint32_t coastLinesRedrawn = 0;       // lines whose pre-jitter shape changed (drawn inside the window)
	uint32_t coastLinesBeyondWindow = 0;  // of those, lines that move farther than the window too (a reload moves that part)
	uint32_t coastLinesReloadMoves = 0;   // unchanged lines whose jitter a reload would shift
	uint64_t samplesWonderKept = 0;       // window samples kept on natural wonders' own hexes
};

// The producer seen by this module (one per load; one thread).
struct RegionProducer
{
	virtual ~RegionProducer() {}
	virtual c5r::Status create(const LoadIn& in) = 0;
	virtual c5r::Status change(const PlotTableIn& after, ChangeOut& out) = 0;
	// ticket 4914 (1): after a good create, true when this load's land/water changes can go live (the journal's mark)
	virtual bool landWaterOn() const { return false; }
};
// libraryPath: <install>\Resource\Common\TerrainPiecesLibrary.fxsb (8.1). The library is opened once (the first create)
// and kept for the life of the factory's producers.
typedef std::function<std::unique_ptr<RegionProducer>()> ProducerFactory;
// Ticket 4914 (1): turns on the land/water step of one load's producer (on the worker, right after its create): reads its
// inputs (the install's coast styles and the TerrainDef's strip materials) once per factory and calls
// Producer::enableLandWater with the load's map seed. False (why): this load's land/water changes wait with
// Wait::LandWaterInputs. The server's one is MakeInstallLandWaterEnabler (srv_liveterrain_scene.cpp: it needs the real
// c5render headers, which the hook's test build replaces with a stub).
typedef std::function<bool(c5r::liveterrain::Producer& producer, uint32_t mapSeed, std::string& why)> LandWaterEnabler;
// landWater empty: land/water changes wait with the producer's Water, as before 4914 (the tests, lt_hook_run).
ProducerFactory MakeC5rProducerFactory(const std::wstring& libraryPath, LandWaterEnabler landWater = LandWaterEnabler());
// install: the game install the scene reads (SceneOptions::install). log: the LOUD line of a failed input read and the
// cost line (once per factory), and one line per enabled load; null: none.
LandWaterEnabler MakeInstallLandWaterEnabler(const std::wstring& install, void (*log)(const char* line));
std::wstring LibraryPathFor(const std::wstring& install);

// ---- What the render thread gets back ----
struct Result
{
	uint32_t loadSeq = 0;
	uint64_t job = 0;                 // the change job's number (1, 2, ... per LiveTerrain)
	ChangeOut out;
	double queuedMs = 0, workerMs = 0;   // from the job's post to its end; the worker's time for it
	std::vector<SrvFeed::PlotRecord> sent;   // the table the producer was given (held plots as it had them)
};

// The window written into a field. Returns the samples whose value changed and the box of the written rectangles (x0,
// y0 inclusive, x1, y1 exclusive; x1 <= samplesX: a window split at the seam gives the whole width). False (why) when a
// rectangle lies outside the field or its arrays have the wrong size: nothing is written then.
struct ApplyStats
{
	uint64_t written = 0, heightsChanged = 0, materialsChanged = 0;
	uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
bool ApplyWindow(c5r::heightfield::HeightGrid& g, const std::vector<Rect>& window, ApplyStats& st, std::string& why);
// SHA-256 (64 lowercase hex) of the box's heights (u16 LE, row-major) then its materials (u8) in g: the region hash of
// a field generation (live-terrain.md 6.5, item 1). "" when bcrypt has no SHA-256 or the box is outside the field.
std::string RegionSha256(const c5r::heightfield::HeightGrid& g, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);

// ---- The journal of one load: what the producer was given and which field generations the scene drew (the scene
// bundle's record of live terrain, live-terrain.md 6.5 and 9.5). A player rebuilds the producer from the load's field
// copy, `constants` and `baseline`, then gives it each job's table (the previous table with the job's diffs) in order;
// a job whose generation is listed must be live and give the listed hash over its box after its window is written.
struct JournalJob
{
	uint64_t id = 0;
	std::vector<std::pair<uint32_t, SrvFeed::PlotRecord>> diffs;   // plot index, the record sent (against the job before)
};
struct Generation
{
	uint32_t gen = 0;       // 1, 2, ... in the load (0 = the load's copy)
	uint64_t job = 0;       // the job whose window made it
	uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the box written (samples; x1, y1 exclusive)
	std::string sha;        // RegionSha256 over the box right after this job's window was written
};
struct Journal
{
	bool open = false;      // a producer was requested for this load (onField); false after onFieldUnavailable
	uint32_t loadSeq = 0;
	std::vector<uint8_t> constants;
	uint32_t W = 0, H = 0;
	std::vector<SrvFeed::PlotRecord> baseline;
	std::vector<JournalJob> jobs;          // in the order the producer runs them
	std::vector<Generation> generations;   // in the order the scene wrote them
	std::vector<std::string> featureNames; // by feature id (DecodeSpanText fills it; live: the FeatureNameFn)
	// ticket 4912: the load's map seed (LoadIn's). Encoded as a 12-byte trailer after the feature names ("SEED", u32 1,
	// u32 seed) only when known, so a journal without it is byte for byte the form before 4912 (rule A: the library's
	// bundles decode as before)
	bool haveMapSeed = false;
	uint32_t mapSeed = 0;
	// ticket 4914 (1): the load's producer had the land/water step on (RegionProducer::landWaterOn after its create).
	// Encoded as an 8-byte trailer after the seed's ("LWAT", u32 1) only when true, so every journal before it decodes as
	// before and replays without the step (what its live server did)
	bool landWater = false;
	// ticket 0717: the load's producer drew with the river band and the land region (LoadIn::riverBand). Encoded as an
	// 8-byte trailer right after the feature names ("RBND", u32 1), before the seed's, only when true, so every journal
	// before 0717 decodes as before and replays with the 4908 keeps (what its live server did); the live scene sets it always
	bool riverBand = false;
	// ticket 0613: the load's producer drew unknown wonders with the neutral base (LoadIn::neutralWonders); its first job is
	// the load-time pass (no diffs). Encoded as an 8-byte trailer ("NWON", u32 1) right after the river band's (or the names),
	// before the seed's, only when true, so every journal of a server with the option off is byte for byte as before 0613
	bool neutralWonders = false;
};
// The job's diffs applied to `table` (the table of the job before). False (why): a size or index outside.
bool ApplyJournalJob(const JournalJob& job, uint32_t W, uint32_t H, std::vector<SrvFeed::PlotRecord>& table, std::string& why);

// ---- The scene bundle (rule A inside format 9, the farm producer's pattern of ticket 4852: render_config, no member) ----
// render_live_terrain "0" / "1" (the scene had live terrain on at the span), and with "1"
// render_live_terrain_journal = kJournalForm + base64 of the journal of the span's load and, per recorded frame where the
// field generation drawn changed (the span's first frame always), {frame id, generation}. A journal that cannot be taken
// is a version note starting kLiveTerrainNote instead (the player then draws the load's field only).
constexpr const char* kJournalForm = "lj1 ";
constexpr const char* kLiveTerrainNote = "gap: live terrain";
struct FrameGen
{
	int64_t frameId = 0;
	uint32_t gen = 0;
};
// names: the feature names the live tables were named with (null: j.featureNames), stored for every id the tables use.
std::string EncodeSpanText(const Journal& j, const std::vector<FrameGen>& frames, const FeatureNameFn& names);
// False (why): not kJournalForm, bad base64, a size or count that does not fit, a generation or frame list out of order.
bool DecodeSpanText(const std::string& text, Journal& j, std::vector<FrameGen>& frames, std::string& why);
// The bundle checker's rules for render_config's two keys (srv_scene_bundle.cpp): render_live_terrain absent (every bundle
// before 4907), "0" or "1"; the journal only with "1" and decodable; "1" needs the journal or a note starting
// kLiveTerrainNote (missing = true then: MissingMember; any other false: BadValue).
bool CheckLiveTerrainConfig(const std::map<std::string, std::string>& rc, const std::vector<std::string>& notes, std::string& why,
                            bool& missing);

// The player's side: a producer rebuilt from the journal and the load's field, advanced job by job.
class Replay
{
public:
	// The load's field (the bundle's heightfield member, before any generation). Errors: the producer's, or BadLayout
	// when the field does not fit the journal's map.
	c5r::Status init(const ProducerFactory& factory, const Journal& j, const c5r::heightfield::HeightGrid& loadField);
	// Runs the journal's jobs in order until generation `gen` is written into g (each listed generation's window applied
	// and its hash checked; a live result the live scene did not write is not written either). Errors: NotFound (gen not
	// in the journal), BadLayout (a listed job not live, a hash mismatch: the replay is not the recording), the
	// producer's. `changed`: something was written.
	// written (optional): the box of each window written, in order (ticket 4904's trees take them)
	// rects (optional): every rectangle of the windows written, in order, without their samples (ticket 0587: the player's
	// region step takes them as the live scene takes its windows' rectangles)
	c5r::Status advanceTo(uint32_t gen, c5r::heightfield::HeightGrid& g, bool& changed, std::vector<ApplyStats>* written = nullptr,
	                      std::vector<Rect>* rects = nullptr);
	uint32_t generation() const { return m_gen; }
	// Ticket 0631 (rule A): the river-edge rule the journal's baseline gave (RiverEdgeRuleText), after init
	const std::string& riverEdgeRule() const { return m_riverRule; }
	// The generation drawn at frameId by the frame list (the last entry at or before it; 0 before the first)
	static uint32_t GenerationAt(const std::vector<FrameGen>& frames, int64_t frameId);

private:
	std::unique_ptr<RegionProducer> m_p;
	Journal m_j;
	std::vector<SrvFeed::PlotRecord> m_tab;
	size_t m_nextJob = 0, m_nextGen = 0;
	uint32_t m_gen = 0;
	std::string m_riverRule;
};

struct Counters
{
	int64_t loads = 0, creates = 0, createErrors = 0;
	int64_t tablesSeen = 0, relevantPlots = 0, changesPosted = 0, changesMerged = 0;
	int64_t live = 0, waits = 0, changeErrors = 0, noChange = 0, staleResults = 0;
	int64_t applied = 0, applyErrors = 0;
	int64_t held = 0, heldReleased = 0;   // plots held after a "wait for reload" (sent as the producer has them); released
	int64_t loadPasses = 0, loadPassesLive = 0;   // ticket 0613: load-time passes answered; of them live (a window)
	double loadPassMsLast = 0;                    // the last pass's time in the producer (the worker, off the render thread)
	double createMsLast = 0, changeMsMax = 0, applyMsMax = 0;
	uint32_t fieldGeneration = 0;     // field generations of the current load (0 = the load's copy)
};

class LiveTerrain
{
public:
	struct Config
	{
		ProducerFactory factory;
		bool threaded = true;               // false: every job runs inside the posting call (the offline player, tests)
		void (*log)(const char* line) = nullptr;   // every line (LOUD ones start with "LOUD: "); null: none
		int maxWaitLines = 50;              // "wait for reload" lines per load (then every 100th)
	};
	explicit LiveTerrain(const Config& c);
	~LiveTerrain();   // stops the worker (a running producer call ends first)

	// ---- render thread ----
	// A new field (a new load, or the same load's field again: a device rebuild). A load seq other than the current
	// one starts a new producer; the same one does nothing (live edits already in the scene's field are kept).
	// `baseline`: the feed records in.table was made from (the change filter's start; same size as in.table).
	void onField(LoadIn&& in, const std::vector<SrvFeed::PlotRecord>& baseline);
	// A new load whose producer cannot be made (the constants block unreadable, no plot table of the field's size): one
	// LOUD line with `why`; the load's terrain changes wait for the reload.
	void onFieldUnavailable(uint32_t loadSeq, const std::string& why);
	// The bake's plot table (every time it may have changed). Compared with the last table seen for this load: a relevant
	// change posts the table to the worker. A table of another size than the load's is ignored (counted in why-lines).
	void onTable(const std::vector<SrvFeed::PlotRecord>& recs, uint32_t W, uint32_t H, const FeatureNameFn& names);
	// The next finished change of the current load (oldest first). Results of an earlier load are dropped (counted).
	bool takeResult(Result& out);
	// The render thread wrote a live result into its field (ApplyWindow + the field's re-apply): counted, logged, and the
	// field generation advanced. Returns the new generation.
	uint32_t noteApplied(const Result& r, const ApplyStats& st, double applyMs, const std::string& sha);
	void noteApplyError(const Result& r, const std::string& why);

	uint32_t loadSeq() const { return m_loadSeq; }
	bool ready() const;                    // the current load's producer was made
	bool busy() const;                     // a job is queued or running
	// Waits until no job is queued or running (tests, the offline player), at most maxMs. True: idle.
	bool waitIdle(double maxMs);
	Counters counters() const;
	void statsLine(char* out, size_t n) const;
	// The current load's journal (render thread; the recorder reads it).
	const Journal& journal() const;

private:
	struct Job
	{
		enum Kind { kCreate, kChange } kind = kChange;
		uint32_t loadSeq = 0;
		uint64_t id = 0;
		LoadIn load;
		PlotTableIn table;
		std::vector<SrvFeed::PlotRecord> sent;
		double postedMs = 0;
	};
	bool post(Job&& j);   // true: merged into a queued change (its journal entry is replaced)
	Journal m_j;
	std::vector<SrvFeed::PlotRecord> m_jLastSent, m_jPrevSent;   // the tables of the last two journal jobs
	void run(Job& j);
	void worker();
	void logf(const char* fmt, ...) const;

	Config m_cfg;
	// render thread state
	uint32_t m_loadSeq = 0;
	bool m_haveLoad = false;
	std::vector<SrvFeed::PlotRecord> m_last;   // the last table sent for this load (the filter's baseline)
	std::vector<SrvFeed::PlotRecord> m_prodTable;   // the producer's current table as far as the results said
	std::vector<SrvFeed::PlotRecord> m_actual;      // the last table the bake had
	std::vector<uint8_t> m_held;                    // per plot: held after a wait (sent as m_prodTable has it)
	std::vector<SrvFeed::PlotRecord> m_heldRec;     // the plot's record when it was held
	uint32_t m_lastW = 0, m_lastH = 0;
	uint64_t m_nextJob = 1;
	uint64_t m_loadPassJob = 0;   // ticket 0613: the current load's load-time pass (its job id; 0: none)
	int m_waitLines = 0;
	int64_t m_waitsThisLoad = 0;
	// shared with the worker
	mutable std::mutex m_mu;
	std::condition_variable m_cv, m_idleCv;
	std::deque<Job> m_jobs;
	std::deque<Result> m_results;
	bool m_running = false, m_stop = false;
	bool m_landWaterOn = false;     // ticket 4914: the producer of load m_landWaterLoad has the land/water step on
	uint32_t m_landWaterLoad = 0;
	Counters m_c;
	bool m_ready = false;
	uint32_t m_readyLoad = 0;
	std::unique_ptr<RegionProducer> m_prod;   // worker only (and the constructor / destructor)
	uint32_t m_prodLoad = 0;
	std::thread m_thread;
};

// ---- Ticket 4909 (live-terrain.md 8.14): a live apply between its two render-thread steps ----
// Defined in srv_liveterrain_scene.cpp (the server only; this header stays free of D3D11 and the terrain module, which
// its tests build without). The deleter makes a unique_ptr of it usable where the type is incomplete.
struct ApplyJob;
struct ApplyJobDelete
{
	void operator()(ApplyJob* j) const;
};

}  // namespace liveterrain
}  // namespace srvrender
