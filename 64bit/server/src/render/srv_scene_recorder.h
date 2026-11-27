// srv_scene_recorder.h - the scene recorder of the offline scene harness inside server.exe (epic 0301, task 3904).
// Contract: research/06-tooling-and-workflows/offline-scene-harness.md sections 2, 5, 9, 12, 13 and the recorder's own
// section 15 (as built). Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// Behind the test keys render_test_record=DIR, render_test_record_after, render_test_record_frames and
// render_test_record_dump (ParseRecorderKeys; the host reads them beside render_test_dump). Without render_test_record no
// recorder object exists and the live path is today's (contract 13). With it, the recorder sits at the readers' hand-off
// points (contract 5, P7):
//   * the height-field tap: Scene::setFieldSink (the C5HF payload tapPoll accepted, before the unmap);
//   * the water tap: WaterTapReader::setImageSink (the block, the image and the grid values poll() accepted);
//   * the feed: MakeRecordingFeed (every drain that returned true, every CopyPlots table; FeedRecordSink);
//   * the overlay: MakeRecordingOverlaySource (each new pair at once, its texels two acquire() calls later;
//     OverlayRecordSink);
//   * the frame hook in the host's DrawFrame: beforeDraw (the span, the clock value, the water clock state at the span's
//     first frame), afterDraw (the frame's record; the chosen frames' colour and depth targets copied into staging
//     textures before the sink's end), afterEnd (those copies mapped and written after the sink's end).
// It writes the bundle through 3902's scenebundle::Writer (members into DIR/.partial, scene.json last). Every call is on
// the render thread; the writes are synchronous there (a test run's cost, never a product figure: contract 13).
//
// What the server cannot know (the save's SHA-256 and turn, the COMMON 18 pins, the repository commit, the build
// configuration) is left empty in scene.json and stamped afterwards by recorder_test.exe --stamp (section 15). The save
// name and the machine come from the bundle folder's name when it has the form <save>_<yyyymmdd-hhmmss>_<machine>.
#pragma once
namespace srvextras { class SceneRoads; }   // ticket 4934 (extras/srv_extras.h)
#include <stdint.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "srv_scene_sources.h"
#include "srv_scene_bundle.h"

struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Texture2D;
namespace srvmodels { class ModelsScene; }   // ticket 4875: landmarks/srv_models_scene.h

namespace srvrender {

// The four keys (contract 9). on = render_test_record was given; the other three are refused without it.
struct RecorderKeys
{
	bool on = false;
	std::wstring dir;              // render_test_record: an absolute folder, at most 200 characters
	int after = 1;                 // render_test_record_after: the span starts at the draw of the N-th published frame
	int frames = 8;                // render_test_record_frames: frames in the span, 1..100000
	std::string dump = "first,last";   // render_test_record_dump: first,last | all | every:N
	int dumpEvery = 0;             // 0 first,last; -1 all; N > 0 every N-th frame of the span (k % N == 0)
	std::map<std::string, std::string> given;   // the keys as written (scene.json recorder.keys)
	bool dumpsFrame(uint32_t k) const;          // frame k (0-based) of the span gets the server's dump
	uint32_t dumpCount() const;
};
// Reads the four keys through get(key) ("" = not set). False, why = "key=value: what is wanted", on: a relative path
// or one above 200 characters, render_test_record_after outside 1..1000000, render_test_record_frames outside
// 1..100000, render_test_record_dump not first,last / all / every:N (N 1..100000), a dump choice that gives fewer than
// two dumped frames, or one of the three without render_test_record.
bool ParseRecorderKeys(const std::function<std::string(const char* key)>& get, RecorderKeys& out, std::string& why);

// What the host knows of the run, for scene.json.
struct RecorderContext
{
	std::map<std::string, std::string> renderConfig;   // every resolved HostConfig value the scene reads (strings)
	std::wstring install;          // the game install the server reads (asset set, D94); "" = not computed
	std::wstring exe;              // CivilizationV_DX11.exe the shaders come from (pins.exe_sha256); "" = not hashed
	std::string machine;           // "" = from the folder name, else the computer's name
	bool hashBinaries = true;      // server_exe_sha256, pins.server_sha256 and pins.exe_sha256 (tests: false)
	std::vector<std::string> versionNotes;   // the gaps of contract 3 the host knows of
};

// Counters the host's readers keep (contract 1's counters), handed in at each step.
struct RecorderCounters
{
	int64_t waterRefused = 0, overlayRefused = 0, overlayStale = 0;
};

class SceneRecorder : public FeedRecordSink, public OverlayRecordSink
{
public:
	// Opens the bundle folder (absent or empty; created). Null, why set, when it cannot: the caller logs it LOUD and runs
	// without a recorder (the game goes on).
	static std::unique_ptr<SceneRecorder> Open(const RecorderKeys& k, const RecorderContext& c, std::string& why);
	~SceneRecorder() override;

	// ---- the loop ----
	void beginIteration() { m_loopIter++; }    // once per host loop iteration, before the drain
	uint64_t loopIter() const { return m_loopIter; }
	// After every iteration's work: finishes the bundle once the span is drawn and every overlay it bound has its texels
	// (or 600 iterations after the span's end at the latest).
	void step(const RecorderCounters& c);
	// The host's end (after the device and the overlay wrapper are released, so pending texels are in): finishes with what
	// there is; a span not complete makes the bundle incomplete. Idempotent.
	void finish(const RecorderCounters& c, const char* why);
	bool finished() const { return m_finished; }
	bool spanStarted() const { return m_spanStarted; }
	bool spanDone() const { return m_spanDone; }

	// ---- the inputs (the readers' hand-off points) ----
	static void FieldSink(void* ctx, uint32_t loadSeq, const uint8_t* c5hf, size_t bytes);
	static void WaterSink(void* ctx, const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam,
	                      uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY);
	void onFieldCopy(uint32_t loadSeq, const uint8_t* c5hf, size_t bytes);
	void onWaterImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam, uint32_t gridSeq,
	                  uint32_t samplesX, uint32_t samplesY);
	void onDrain(const SrvFeed::Batch& b) override;
	// Epic 0375, issue 4456 (MC grant 2026-11-16): the drainMs the last onDrain recorded (feed.bin's DRAIN clock, QpcToMs);
	// -1 when that drain was not recorded (the recorder finished or its feed member closed) or before the first. The host
	// gives it to the unit scene, so that the scene's batch clock is the recorded one.
	double lastDrainMs() const { return m_lastDrainMs; }
	void onPlots(uint32_t caller, const std::vector<SrvFeed::PlotRecord>& table, uint32_t gen, uint32_t w, uint32_t h) override;
	// epic 0375, issue 4457 (format version 4): the unit scene's CopyUnits / CopyPlayers through the recording feed
	// (feed.bin's records 3 UNITS, 6 PLAYERS of caller 1); the drained batch's unit side tables go with its DRAIN.
	void onUnits(const std::vector<SrvFeed::UnitRecord>& table, uint32_t unitTableGen) override;
	void onPlayers(const std::vector<SrvFeed::PlayerRecord>& rows) override;
	void onOverlayPair(const OverlayFrame& pair) override;
	void onOverlayTexels(const OverlayFrame& pair, const OverlayTexels& t) override;
	void onOverlayReadbackError(const OverlayFrame& pair, const std::string& why) override;
	void setAdapter(uint64_t luid, const std::string& description, const std::string& driver);
	// epic 0301, issue 3911 (format version 2): the fog scene whose clock state is taken at the span's first frame
	// (fog_clock.bin, when its state is not OFF; its plot-table reads come through the recording feed as PLOTS caller 3:
	// the host sets FowScene::setTable(MakeFeedSourcePlotTable(feed)) before Scene::init); a render_config value the host
	// knows only after Scene::init (render_fow_level as the fog resolved it).
	// Ticket 0403 (format version 6): at the same frame the FOWLevel-3 particles' state (FowScene::particleState) for
	// particles.bin and render_config render_fow_particles ("1" when they were on there, else "0"); particles drawn in the
	// span without a state: the note scenebundle::kFogParticlesNote.
	void setFog(const FowScene* f) { m_fog = f; }
	void setRenderConfig(const std::string& key, const std::string& value) { m_c.renderConfig[key] = value; }
	// Issue 0364 (format version 3): the decal tap's reader as the scene has it now (the host: Scene::decalReader; null
	// while there is none). At the span's first frame, before its draw, the reader's set in force is written with
	// DecalTapReader::writeCurrentImage as decals/load<NNNN>.dtap (no set: the note scenebundle::kDecalNoSetNote); at
	// every later frame of the span a generation other than the first one's is noted in version_notes ("decals: decal
	// generation changed inside the span ..."; version 1 of the member holds one set). Not set = render_decals off.
	void setDecalReader(std::function<const DecalTapReader*()> get) { m_decalGet = std::move(get); }
	// Ticket 0495 (format version 8): the forest scene (null while render_forest=0 or before Scene::init) and the near
	// pairs' state (false: none; `on` = the residency's near pairs are on). At the span's first frame, before its draw:
	// SceneForest::spanState for forest.bin and render_config render_forest ("1" when the scene has a forest), the near
	// state for near.bin; a state that cannot be taken is a gap note (kForestNote / kNearNote), and a cut-mask generation
	// that moves inside the span is a kForestNote as well.
	void setForest(std::function<const SceneForest*()> get) { m_forestGet = std::move(get); }
	// Ticket 4852 (epic 0528): the farm producer (null while render_farmfields=0). At the span's first frame
	// render_config render_farmfields ("1" when the scene has one) and render_farmfields_state (FarmFieldsProducer::
	// spanState: the input of the decals in force, one line); no state: a gap note (scenebundle::kFarmNote). A production
	// put in force inside the span is a kFarmNote as well (the line holds the first frame's). No member: format 8 stays.
	void setFarm(std::function<const FarmFieldsProducer*()> get) { m_farmGet = std::move(get); }
	// Ticket 4934 (epic 0454): FixRoadLayout's producer (null while render_roadlayout=0), as the farm's: at the span's first
	// frame render_roadlayout "1" and render_roadlayout_state (SceneRoads::spanState) when the scene has one, nothing when it
	// has none (a bundle without the road layout keeps its keys); no state, or new road decals inside the span: a kRoadNote.
	void setRoads(std::function<const srvextras::SceneRoads*()> get) { m_roadsGet = std::move(get); }
	// Ticket 4875 (format version 9): the model module (null while render_cities and render_landmarks are 0). Before each
	// frame of the span whose module image differs from the last one written (ModelsScene::imageHash), the image as
	// modeltap/frame<this frame's frames.bin index, 6 digits>.mtap; no image at the span's first frame: the note
	// scenebundle::kModelNoFrameNote. A frame that cannot be written marks the bundle incomplete. Ticket 4888: after the
	// frame's draw also the CPU-built meshes the layer held (ModelsScene::geometrySnapshot) as modelgeom/frame<NNNNNN>.mgeo,
	// at the first frame where it holds any and again at each frame where the held set changed (heldSetHash); one that
	// cannot be written: the note scenebundle::kModelGeomNote (a record error, the bundle stays usable as a known gap).
	void setModels(std::function<const srvmodels::ModelsScene*()> get) { m_modelsGet = std::move(get); }
	// images: the texels of the pairs whose content is behind their tile (near.bin's kNearHasImages).
	void setNearState(std::function<bool(std::vector<uint8_t>& state, uint32_t& tilesX, uint32_t& tilesY, bool& on,
	                                     std::vector<scenebundle::NearPairImage>& images)> get)
	{
		m_nearGet = std::move(get);
	}

	// ---- the frame hook (DrawFrame) ----
	// Right before Scene::draw. Flushes the feed records waiting for this draw's frame id. True when this draw is in the
	// span: the host then passes the wall clock read here (*nowMs, QpcToMs scale) in SceneFrame::nowMs. `published` = the
	// frames the sink published so far; the span starts at the draw that can become publish number `after`. `water`: its
	// clock state is taken at the span's first frame, before that frame's draw.
	// Issue 0440 (format version 5): with the unit scene set (setUnits), every frame before the span gets the clock too
	// (*nowMs >= 0 while false is returned: the host draws that frame with it, so that the scene's controller steps by
	// recorded values), and `frame` (its camera block) goes into units_history.bin from the controller's first frame on.
	bool beforeDraw(int64_t frameId, int64_t published, const WaterScene& water, double* nowMs, const SceneFrame* frame = nullptr);
	// Issue 0440 (format version 5, the block at scenebundle::UnitsHistoryRecord): the unit scene while render_units=1 (its
	// ready() state per frame before the span) and the database it opens (SrvFeed's GetUnitDatabase, or the host's
	// render_test_units_db), so that the bundle carries units/gen<NNNNNNNNNN>.db and units_history.bin, or a gap: note
	// for each it cannot. Not set (render_units=0): neither member, no note.
	struct Units
	{
		const srvunits::SceneUnits* scene = nullptr;
		std::function<bool(std::wstring& path, uint32_t& gen)> database;
	};
	void setUnits(const Units& u) { m_units = u; }
	size_t unitsHistoryFrames() const { return m_hist.size(); }   // tests
	// After Scene::draw returned ok, before the sink's end, for a frame beforeDraw put in the span: the frame's record
	// (camera block, clock value, target size, the overlay pair `bound` names, flags) and, for a chosen frame, the colour
	// and depth targets copied into staging textures. flags: bit 1 the water module drew, bit 2 waves drawn.
	void afterDraw(ID3D11DeviceContext* ctx, const SceneFrame& f, double nowMs, uint32_t w, uint32_t h, ID3D11RenderTargetView* rtv,
	               ID3D11DepthStencilView* dsv, const c5r::terrain::Overlay& bound, uint32_t flags);
	// Scene::draw failed for a frame in the span: the span ends there and the bundle is marked incomplete.
	void drawFailed(int64_t frameId, const std::string& why);
	// After the sink's end: maps the staged copies and writes dumps/<frameId>.bmp and .depth.f32 (and, issue 4308, the
	// staged shadow maps as shadow/<sha16>.shtx).
	void afterEnd(ID3D11DeviceContext* ctx);
	// Epic 0169, issue 4308: right after afterDraw for the same frame (a frame afterDraw did not record is ignored): the
	// shadow triple the scene bound (null: none; srv null: quality 0), its texture copied into a staging texture on ctx's
	// device now and written in afterEnd (deduplicated by content), and the ice mesh drawn in the frame (null: none),
	// written once per generation (ice/gen<...>.ice). shadow.bin gets one record per recorded frame at the finish.
	void afterDrawSeamG(ID3D11DeviceContext* ctx, const ShadowFrame* shadow, const c5r::ice::Mesh* ice, uint32_t iceGeneration,
	                    uint32_t iceLoadSeq);

	// ---- for the host's summary and the tests ----
	std::string summary() const;
	const std::wstring& dir() const { return m_dir; }
	scenebundle::Writer& writer() { return m_w; }            // the disk-full seam: writer().failAfterBytes(N)
	int64_t recordErrors() const { return m_recordErrors; }
	uint32_t framesRecorded() const { return (uint32_t)m_frames.size(); }
	const std::vector<int64_t>& dumped() const { return m_dumped; }
	const std::string& finishWhy() const { return m_finishWhy; }  // "" ok, else why the bundle is incomplete
	bool complete() const { return m_finished && m_finishWhy.empty(); }

private:
	SceneRecorder() = default;
	struct PairKey
	{
		uint32_t generation = 0, slot = 0;
		uint64_t presentCount = 0;
		bool operator<(const PairKey& o) const
		{
			return generation != o.generation ? generation < o.generation : slot != o.slot ? slot < o.slot : presentCount < o.presentCount;
		}
		bool operator==(const PairKey& o) const { return generation == o.generation && slot == o.slot && presentCount == o.presentCount; }
	};
	static PairKey KeyOf(const OverlayFrame& p);
	struct Staged
	{
		int64_t frameId = 0;
		size_t frameIndex = 0;
		ID3D11Texture2D* colour = nullptr;
		ID3D11Texture2D* depth = nullptr;
		uint32_t w = 0, h = 0;
		bool depthFloat = false;
	};
	void error(const std::string& why);              // record_errors++, LOUD once per kind of text
	void writeFeedPending(int64_t nextFrameId);
	void writeOverlay(const PairKey& k, const OverlayTexels& t);
	bool overlaysResolved() const;
	void doFinish(const RecorderCounters& c, const std::string& why);
	void releaseStaged(Staged& s);

	scenebundle::Writer m_w;
	RecorderKeys m_k;
	RecorderContext m_c;
	std::wstring m_dir;
	std::string m_bundleId, m_created, m_saveName, m_machine;
	uint64_t m_loopIter = 0;
	bool m_finished = false, m_spanStarted = false, m_spanDone = false;
	uint64_t m_spanDoneIter = 0;
	std::string m_finishWhy, m_spanBroken;
	int64_t m_recordErrors = 0, m_feedOverflowEvents = 0, m_feedDrains = 0, m_feedTables = 0;
	int64_t m_feedUnitCopies = 0, m_feedPlayerCopies = 0;   // issue 4457
	std::set<std::string> m_errorsSeen;
	std::vector<std::string> m_notes;
	// feed.bin: records waiting for the next draw's frame id (DRAIN's nextFrameId, patched in place)
	scenebundle::FeedEncoder m_enc;
	std::vector<uint8_t> m_feedPending;
	std::vector<size_t> m_feedPatch;      // offsets of nextFrameId fields in m_feedPending
	bool m_feedOpen = false;
	double m_lastDrainMs = -1;   // issue 4456: lastDrainMs()
	int64_t m_feedEarlyFlushes = 0;
	// height field and water
	std::map<uint32_t, std::string> m_fieldSha;   // loadSeq -> SHA-256 of the first file of that load
	std::set<std::string> m_written;              // every member path written
	uint32_t m_mapW = 0, m_mapH = 0, m_loadSeq = 0;
	int64_t m_fieldCopies = 0, m_waterCopies = 0;
	// ticket 0622 (format version 11): the water copy in force (the newest copy the sink saw: the render thread uses it from
	// its next draw on) and water_frames.bin's records, one per frame of m_frames
	scenebundle::WaterFrameRecord m_waterNow = {};
	std::vector<scenebundle::WaterFrameRecord> m_waterRecs;
	// the span
	std::vector<scenebundle::FrameRecord> m_frames;
	std::vector<VPShellCamera> m_cams;
	std::vector<PairKey> m_framePair;             // the pair each frame bound (valid when its overlayBound)
	std::vector<int64_t> m_dumped;
	int64_t m_lastFrameId = 0;
	scenebundle::WaterClockFile m_clock;
	bool m_haveClock = false;
	const FowScene* m_fog = nullptr;              // issue 3911
	scenebundle::FogClockFile m_fogClock;
	bool m_haveFogClock = false;
	std::vector<Staged> m_staged;
	// the overlay
	OverlayFrame m_curPair;
	bool m_havePair = false;
	std::map<PairKey, OverlayTexels> m_texCache;  // texels not (yet) needed, the newest few
	std::vector<PairKey> m_texOrder;
	std::set<PairKey> m_needed;                   // bound by a frame of the span, texels not yet in
	std::map<PairKey, std::string> m_pairSha;     // written: the .ovl file's SHA-256
	std::map<PairKey, std::string> m_pairFailed;  // a readback error
	int64_t m_ovlPairs = 0, m_ovlTexels = 0, m_ovlFiles = 0, m_ovlDeduped = 0;
	// the adapter
	std::string m_luid, m_adapterDesc, m_adapterDriver;
	// issue 4308: shadow.bin's records (one per frame of m_frames once afterDrawSeamG ran), the staged shadow maps
	struct StagedShadow
	{
		size_t index = 0;
		ID3D11Texture2D* tex = nullptr;
		uint32_t w = 0, h = 0, format = 0;
	};
	std::vector<scenebundle::ShadowRecord> m_shadowRecs;
	std::vector<StagedShadow> m_shStaged;
	std::set<std::string> m_shTexWritten;
	std::set<uint32_t> m_iceWritten;
	int64_t m_shFiles = 0, m_shDeduped = 0, m_iceFiles = 0;
	// issue 0364: the decal member
	void writeDecalSet(int64_t frameId);
	void watchDecalGeneration(int64_t frameId);
	std::function<const DecalTapReader*()> m_decalGet;
	bool m_decalWritten = false, m_decalChangeNoted = false;
	uint32_t m_decalGen = 0;
	std::string m_decalPath;
	// issue 0440: the unit scene's history and database (format version 5)
	void noteUnitsFrame(int64_t frameId, double nowMs, const SceneFrame* frame);
	void writeUnitsMembers();
	Units m_units;
	std::vector<scenebundle::UnitsHistoryRecord> m_hist;
	scenebundle::UnitsHistoryRecord m_histPrev = {};
	bool m_histHavePrev = false, m_histStarted = false, m_histOver = false, m_histNoFrame = false;
	bool m_unitsReadyAtSpan = false, m_unitsHaveDb = false;
	std::wstring m_unitsDbPath;
	uint32_t m_unitsDbGen = 0;
	std::string m_histWhy;   // why there is no history (the gap note's reason)
	int64_t m_fogPartFrames0 = 0;      // issue 0440 (MC #3003): FowScene::particleFrames at the span's first frame
	bool m_fogPartOnAtSpan = false;    // FowScene::particlesOn() there
	// ticket 0403 (format version 6): FowScene::particleState at the span's first frame, before its draw (particles.bin)
	FowParticleState m_partState;
	bool m_havePartState = false;
	std::string m_partWhy;             // why the particles were on but gave no state (the gap note's reason)
	// ticket 0495 (format version 8): forest.bin and near.bin
	void takeForestAndNear();
	std::function<const SceneForest*()> m_forestGet;
	std::function<bool(std::vector<uint8_t>&, uint32_t&, uint32_t&, bool&, std::vector<scenebundle::NearPairImage>&)> m_nearGet;
	scenebundle::ForestStateFile m_forest;
	bool m_haveForest = false;
	uint64_t m_forestCutGen0 = 0;
	std::string m_forestWhy, m_forestCutNote;
	scenebundle::NearPairsFile m_near;
	bool m_haveNear = false, m_nearOnAtSpan = false;
	std::string m_nearWhy;
	// ticket 4852: the farm producer's span input (render_config)
	void takeFarm();
	std::function<const FarmFieldsProducer*()> m_farmGet;
	bool m_farmOnAtSpan = false;
	uint64_t m_farmVersion0 = 0;
	std::string m_farmNote;
	// ticket 4934: the road producer's span input (render_config)
	void takeRoads();
	std::function<const srvextras::SceneRoads*()> m_roadsGet;
	bool m_roadsOnAtSpan = false;
	uint64_t m_roadsVersion0 = 0;
	std::string m_roadsNote;
	// ticket 4875 (format version 9): the model tap members
	void takeModelTap(int64_t frameId);
	std::function<const srvmodels::ModelsScene*()> m_modelsGet;
	uint64_t m_modelHash = 0, m_modelMembers = 0;
	bool m_modelNoFrameNoted = false, m_modelFailed = false;
	bool m_modelGeomNoted = false;   // ticket 4883: sb::kModelGeomNote written (ticket 4888: only when a member failed)
	// ticket 4888: the CPU-built meshes as modelgeom/frame<NNNNNN>.mgeo (the held set's hash when the last one was written)
	void takeModelGeometry(const srvmodels::ModelsScene& ms, uint32_t idx, int64_t frameId);
	uint64_t m_geomSetHash = 0, m_geomMembers = 0;
	// epic 0465, ticket 4907: live terrain (srv_liveterrain_bundle.cpp): the field generation drawn per frame, and at the
	// end render_config render_live_terrain / render_live_terrain_journal (liveterrain::EncodeSpanText; format 9 unchanged)
	void noteLiveTerrainFrame(int64_t frameId);
	void takeLiveTerrain();
	std::function<const liveterrain::LiveTerrain*()> m_liveGet;
	std::vector<liveterrain::FrameGen> m_liveFrames;
	bool m_liveOnAtSpan = false, m_liveSpanNoted = false;
	uint32_t m_liveLoadAtSpan = 0;
public:
	void setLiveTerrain(std::function<const liveterrain::LiveTerrain*()> get) { m_liveGet = std::move(get); }
};

// ---- epic 0169, issue 4308: the player's side of shadow.bin, shadow/<sha16>.shtx and ice/gen<...>.ice ----------------------
// Frame k's shadow and ice as recorded, for Scene::setShadowSource / setIceSource (sceneshot; the tests). The control
// selects the recorded frame before each Scene::poll (as FileOverlayControl).
struct FileSeamGControl
{
	virtual ~FileSeamGControl() {}
	virtual void seekFrame(uint32_t k) = 0;
};
// Each distinct shadow map as an IMMUTABLE texture of its recorded format (R16_UNORM or R16G16B16A16_FLOAT), one mip, with
// a default SRV, so that the scene binds it as it binds the reader's slot. acquire(): frame k's triple (state Active)
// when the record has a map or quality 0, else false (state Waiting). Refuses (null, *why): a bound record whose texels
// are missing, a file the parser refuses, a texture the device refuses.
std::unique_ptr<ShadowSource> MakeFileShadowSource(ID3D11Device* dev, const std::vector<scenebundle::ShadowRecord>& recs,
                                                   const std::map<std::string, std::vector<uint8_t>>& texelFiles,   // SHA-256 hex -> file
                                                   FileSeamGControl** control, std::string* why);
// poll(): true when frame k drew an ice mesh of another generation than `have` (mesh() then returns it). Refuses (null,
// *why): a record whose generation has no mesh.
std::unique_ptr<IceSource> MakeFileIceSource(const std::vector<scenebundle::ShadowRecord>& recs,
                                             const std::map<uint32_t, std::shared_ptr<const c5r::ice::Mesh>>& meshes,
                                             FileSeamGControl** control, std::string* why);
// The bundle form: shadow.bin and every shadow_texels and ice_mesh member (ReadMember: size and SHA-256 against the
// manifest; the parsers) -> the two sources. A bundle without shadow.bin (recorded before 4308): true, both null and
// *note says so. False with *why naming the member on a refusal.
bool LoadSeamGMembers(ID3D11Device* dev, const std::wstring& bundleDir, const scenebundle::Manifest& m,
                      std::unique_ptr<ShadowSource>* shadow, FileSeamGControl** shadowCtl, std::unique_ptr<IceSource>* ice,
                      FileSeamGControl** iceCtl, std::vector<scenebundle::ShadowRecord>* recs, std::string* note, std::string* why);

}  // namespace srvrender
