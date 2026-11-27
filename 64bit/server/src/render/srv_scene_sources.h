// srv_scene_sources.h - where the renderer's inputs come from in the offline scene harness: the live sources, the
// recording wrappers and the file sources (epic 0301; contract: research/06-tooling-and-workflows/offline-scene-harness.md
// sections 4 and 6, accepted by the MC on 2026-11-14, #1237).
// Written 2026-11-14 for this repository (GPL-3.0-or-later). Contract header by the lead (lead-0301); implemented by task
// 3903 (srv_scene_sources.cpp). Every API change against the lead's header is in 3903's issue log ("API: ...") so that
// 3904 and 3905 follow.
//
// The principle (contract 4): the live path keeps its code and its order exactly. srv_feed.* and srv_overlay_tap.* are
// not changed; the feed and the overlay are wrapped from outside. The water tap and the height field get a sink (the
// bytes the live reader accepted, before the unmap) and a file entry that runs the same checks (srv_water_tap.h:
// setImageSink, loadImage; srv_water_scene.h: WaterScene::loadImage; srv_render_scene.h: Scene::loadField; 3903; the
// height field's sink, Scene::setFieldSink, is 3904's). The clock (contract 6): SceneFrame::nowMs (negative = the wall
// clock, today's behaviour) and WaterClockState (srv_water_scene.h: WaterScene::clockState, setClockState). The settle
// call: ResidencySource::settle and Scene::settle (SettleResult below).
//
// The file sources take decoded data (the feed's calls, the overlay's frames and texels); the bundle forms at the end
// read a bundle folder through 3902's library (srv_scene_bundle.h: ReadMember and the member parsers) and hand the
// decoded data to them.
#pragma once
#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "feed/srv_feed.h"
#include "srv_water_scene.h"   // WaterClockState (moved there by 3903: the water tests build without server/src)
#include "srv_render_scene.h"  // OverlaySource, OverlayFrame

struct ID3D11Device;

namespace srvrender {

namespace scenebundle { struct Manifest; }   // srv_scene_bundle.h (3902)

// ---- The feed (contract 4.5) ------------------------------------------------------------------------------------------
// caller of copyPlots: who read the table (recorded in feed.bin's PLOTS records, contract 2.5).
// kPlotsFog (format version 2, epic 0301, issue 3911): the fog scene's table reads (FowPlotTable::copy at its start, at a
// new snapshot, a gap, an overflow or changed plots: srv_fow_scene.cpp readTable).
// kPlotsForest (format version 8, ticket 0495): the forest scene's table reads (ForestScene::readTable and its patches,
// through ForestPlotTableSlot), which went to the live feed directly before.
enum : uint32_t { kPlotsBaseline = 1, kPlotsChanged = 2, kPlotsFog = 3, kPlotsForest = 4 };

struct FeedSource
{
	virtual ~FeedSource() {}
	virtual bool drain(SrvFeed::Batch& out) = 0;                                        // as SrvFeed::Drain
	virtual void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h,
	                       uint32_t caller) = 0;                                       // as SrvFeed::CopyPlots
	// epic 0375, issue 4457 (decision 11, Q3): the unit scene's table copies (feed.bin's records 3 UNITS and 6 PLAYERS of
	// caller 1). The defaults are the live calls (SrvFeed::CopyUnits / CopyPlayers), so that a source written before
	// 4457 keeps today's behaviour; the recording and the file feed override them. The scene reaches them through
	// srvunits::FeedSourcePort (units/srv_units_feedport.h).
	virtual void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& unitTableGen) { SrvFeed::CopyUnits(out, unitTableGen); }
	virtual void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) { SrvFeed::CopyPlayers(out); }
};
// SrvFeed::Drain / CopyPlots, nothing else (one object for the process; no state).
FeedSource& LiveFeed();

// What the recording feed hands the recorder (3904's SceneRecorder implements it; the loop iteration and the next
// frame id of feed.bin's records are the recorder's own). Called on the render thread, after the live call returned.
struct FeedRecordSink
{
	virtual ~FeedRecordSink() {}
	virtual void onDrain(const SrvFeed::Batch& b) = 0;   // only drains that returned true (contract 2.5)
	virtual void onPlots(uint32_t caller, const std::vector<SrvFeed::PlotRecord>& table, uint32_t gen, uint32_t w, uint32_t h) = 0;
	// issue 4457: the unit scene's copies (no-ops by default: a sink written before 4457 records no unit table)
	virtual void onUnits(const std::vector<SrvFeed::UnitRecord>& table, uint32_t unitTableGen) { (void)table; (void)unitTableGen; }
	virtual void onPlayers(const std::vector<SrvFeed::PlayerRecord>& rows) { (void)rows; }
};
// Passes every call to `live` unchanged and hands each result to `rec`. Both must outlive the wrapper.
std::unique_ptr<FeedSource> MakeRecordingFeed(FeedSource& live, FeedRecordSink& rec);

// One recorded call: feed.bin's records as 3902's ParseFeed yields them (scenebundle::FeedRecord: PLOTS tables whole,
// diffs applied).
// The unit scene's copies after a drain (format version 4, issue 4457): feed.bin's records 3 UNITS and 6 PLAYERS of caller 1.
struct FeedUnitCopy
{
	uint32_t gen = 0;                                    // the unit table's generation
	std::vector<SrvFeed::UnitRecord> units;              // the whole table, sorted by (owner, id)
};
struct FeedCall
{
	uint32_t type = 0;                                   // 1 DRAIN, 2 PLOTS (scenebundle::kFeedDrain, kFeedPlots)
	uint64_t loopIter = 0;
	SrvFeed::Batch batch;                                // DRAIN (its records 4, 5, 6-of-caller-2 folded in)
	double drainMs = -1;                                 // DRAIN (format version 4: the recorder's clock at the drain)
	std::vector<FeedUnitCopy> unitCopies;                // DRAIN: the scene's CopyUnits after it, in order (issue 4457)
	std::vector<std::vector<SrvFeed::PlayerRecord>> playerCopies;   // DRAIN: its CopyPlayers, in order
	uint32_t caller = 0, gen = 0, width = 0, height = 0; // PLOTS
	std::vector<SrvFeed::PlotRecord> table;              // PLOTS: the whole table
};
struct FileFeedControl
{
	virtual ~FileFeedControl() {}
	// drain() yields a DRAIN record only while its loopIter <= this (default: no limit; the player sets the recorded
	// loop iteration before each of its iterations).
	virtual void setLoopIter(uint64_t loopIter) = 0;
	// A call that did not match the next record (out of order): every later call yields nothing; why() names both.
	virtual bool failed() const = 0;
	virtual std::string why() const = 0;
	virtual size_t remaining() const = 0;               // records not yet yielded
	// issue 4457: the drainMs of the DRAIN the last drain() yielded (-1 none or unknown), and the unit copies of that DRAIN
	// not yet taken by copyUnits / copyPlayers (a reader with units checks they are 0 before its next drain; a reader
	// without units, sceneshot, never takes them, and they are dropped at the next drain).
	virtual double lastDrainMs() const { return -1; }
	virtual size_t unitCopiesLeft() const { return 0; }
};
// Yields, call by call, exactly what the live calls returned, in the recorded order: drain() the next record when it is
// a DRAIN (else false, nothing consumed); copyPlots(caller) the next record when it is a PLOTS of that caller (else the
// feed fails: out of order, an empty table and generation 0 returned); copyUnits / copyPlayers (issue 4457) the next copy
// recorded after the DRAIN the last drain() yielded (none left: the feed fails the same way). Refuses (null, *why set): no PLOTS record (the
// table the render thread read is missing: contract 7), a loopIter that goes back, an unknown type or caller, a table
// whose size is not width x height. *control (optional) stays owned by the returned source.
std::unique_ptr<FeedSource> MakeFileFeed(std::vector<FeedCall> calls, FileFeedControl** control, std::string* why);

// ---- The overlay (contract 4.3, 2.3) ----------------------------------------------------------------------------------
// One overlay texture as tight bytes: mip 0 first, each mip's rows tight (width >> i) * 4 bytes; 2048 x 2048, 12 mips,
// 22,369,620 bytes for the live tap's texture (scenebundle::EncodeOverlay's input, ParseOverlay's texels).
struct OverlayTexels
{
	uint32_t width = 0, height = 0, mips = 0, dxgiFormat = 0;
	std::vector<uint8_t> bytes;
};
uint64_t OverlayTightBytes(uint32_t width, uint32_t height, uint32_t mips);   // 4 bytes a texel, every mip

// What the recording overlay source hands the recorder: each new pair (generation, texture or slot changed) and, two
// acquire() calls later (the readback ring), its texels. Called on the render thread.
struct OverlayRecordSink
{
	virtual ~OverlayRecordSink() {}
	virtual void onOverlayPair(const OverlayFrame& pair) = 0;                          // at the acquire that got it
	virtual void onOverlayTexels(const OverlayFrame& pair, const OverlayTexels& t) = 0;  // mapped two calls later
	virtual void onOverlayReadbackError(const OverlayFrame& pair, const std::string& why) = 0;
};
// Passes every call through and copies each new pair's texture into one of a ring of three STAGING textures on the
// texture's own device's immediate context (CopyResource, in the iteration that acquired it), mapped two acquire()
// calls later, so the render thread does not wait for the GPU. A ring entry still unmapped when its turn comes again
// (or at the wrapper's end) is mapped then. Installed by the host after Scene::init when render_test_record is set
// (3904, G1). `rec` must outlive the wrapper.
std::unique_ptr<OverlaySource> MakeRecordingOverlaySource(std::unique_ptr<OverlaySource> live, OverlayRecordSink& rec);

// Frame k's overlay as recorded (frames.bin's pair, contract 2.7): the texture by its SHA-256 (hex, 64 characters).
struct FileOverlayFrame
{
	bool bound = false;
	uint32_t generation = 0, numViews = 0;
	uint64_t presentCount = 0;
	float overlayProj[VPSHELL_OVERLAY_MAX_VIEWS][16] = {};
	std::string sha256;
};
struct FileOverlayControl
{
	virtual ~FileOverlayControl() {}
	virtual void seekFrame(uint32_t k) = 0;               // the player selects the recorded frame before each poll
	virtual uint32_t frameCount() const = 0;
};
// Frame k's pair from the bundle, each distinct texture as an IMMUTABLE texture of the recorded 12 mips with the live
// SRV description (R8G8B8A8_UNORM, TEXTURE2D, mips 0..11), so that Scene::overlayPoll and Scene::draw bind it exactly as
// they bind the live one. acquire(): true with the pair when frame k is bound (state Active), false when it is not
// (state Waiting) or k is past the frames. Refuses (null, *why): a bound frame whose texture is missing, a texture not
// 2048 x 2048 x 12 mips of R8G8B8A8_UNORM / TYPELESS or whose bytes are not the tight size, numViews outside 1..4 on a
// bound frame, a texture the device refuses. *control (optional) stays owned by the returned source.
std::unique_ptr<OverlaySource> MakeFileOverlaySource(ID3D11Device* dev, const std::vector<FileOverlayFrame>& frames,
                                                     const std::map<std::string, OverlayTexels>& textures,
                                                     FileOverlayControl** control, std::string* why);

// ---- The bundle forms (3902's library: srv_scene_bundle.h) --------------------------------------------------------------
// Each reads its member(s) through scenebundle::ReadMember (size and SHA-256 against the manifest) and the member's
// parser, then hands the decoded data to the file source above. Refusals (null or false, *why = the CheckResult's line or
// the source's reason) name the member.
// feed.bin -> MakeFileFeed(calls).
std::unique_ptr<FeedSource> MakeFileFeed(const std::wstring& bundleDir, const scenebundle::Manifest& m, FileFeedControl** control,
                                         std::string* why);
// frames.bin's overlay pairs, each bound frame's overlay/<sha16>.ovl (its full SHA-256 = the member's) ->
// MakeFileOverlaySource(frames, textures). Frame k of the control = record k of frames.bin.
std::unique_ptr<OverlaySource> MakeFileOverlaySource(ID3D11Device* dev, const std::wstring& bundleDir, const scenebundle::Manifest& m,
                                                     FileOverlayControl** control, std::string* why);
// water/load<NNNN>.wtap -> WaterScene::loadImage (the water tap's checks on the recorded image).
bool LoadWaterImageMember(WaterScene& w, const std::wstring& bundleDir, const scenebundle::Manifest& m, const std::string& path,
                          std::string* why);
// heightfield/load<NNNN>.c5hf -> Scene::loadField(bytes, loadSeq, cam).
bool LoadFieldMember(Scene& s, const std::wstring& bundleDir, const scenebundle::Manifest& m, const std::string& path, uint32_t loadSeq,
                     const VPShellCamera* cam, std::string* why);
// water_clock.bin -> WaterScene::setClockState (after the image is in use).
bool LoadWaterClockMember(WaterScene& w, const std::wstring& bundleDir, const scenebundle::Manifest& m, std::string* why);

// ---- The fog of war (epic 0168's inputs; format version 2, epic 0301, issue 3911) ----------------------------------------
// The fog scene's plot table through a feed source: copy() = feed.copyPlots(..., kPlotsFog). The host gives the fog the
// recording feed with render_test_record (FowScene::setTable before Scene::init), the player the file feed. `feed` must
// outlive the table.
// Ticket 0495: `caller` kPlotsForest gives the forest's table (ForestPlotTableSlot) the same way.
std::unique_ptr<FowPlotTable> MakeFeedSourcePlotTable(FeedSource& feed, uint32_t caller = kPlotsFog);
// The fog started as the live render thread has it, for a Scene without a section (the player, the tests): Scene::init
// ties the fog's overlay to the section (fo.overlay = render_overlay && the section), so the fog is started again here
// with the scene's options and the overlay on. Its table must already be the feed's (FowScene::setTable before
// Scene::init, whose fog start read the table: the read the live fog made at its start); the restart reads no table (a
// stand-in that yields nothing is set for it) and gives the fog `feed`'s table again. No-op when o.fow is 0.
void RestartFowWithOverlay(Scene& s, ID3D11Device* dev, const SceneOptions& o, FeedSource& feed);
// fog_clock.bin -> FowScene::setClockState (before the span's first frame).
bool LoadFogClockMember(FowScene& f, const std::wstring& bundleDir, const scenebundle::Manifest& m, std::string* why);

// ---- The decals (epic 0166's input; format version 3, issue 0364) ---------------------------------------------------------
// decals/load<NNNN>.dtap (ReadMember: size and SHA-256; ParseDecalTap) -> Scene::loadDecalImage (the scene's reader's file
// entry, then the residency's setDecals), before the span's first frame (the player: before the field's load). render_config
// render_decals not "1": true, nothing applied, *note says so. render_decals "1" without the member: true when version_notes
// carry the recorder's kDecalNoSetNote (the live span drew no decals either; *note says so), else false. False with *why
// naming the member on a refusal (the checker's, the reader's, setDecals': a name the install and the mods' folders lack).
bool LoadDecalMember(Scene& s, const std::wstring& bundleDir, const scenebundle::Manifest& m, std::string* note, std::string* why);

// ---- The model tap frames (epic 0561, ticket 4875; format version 9) ------------------------------------------------------
// LoadModelTapMembers: the bundle's modeltap/frame<NNNNNN>.mtap members by their frame index (sorted), checked against
// render_config as the checker does: render_cities and render_landmarks not "1" -> true, no members, *note says so; on
// without a member -> true when version_notes carry kModelNoFrameNote, else false. ApplyModelTapFrame, before the draw of
// frames.bin's frame `frameIndex`: the member in force (the largest NNNNNN <= frameIndex) read (ReadMember: size and
// SHA-256), checked (ParseModelTap) and handed to Scene::models()->loadImage when it is not the one applied last; nothing
// when no member is in force yet. False with *why naming the member (a refusal, no model module in the scene, the module's
// refusal). The scene must have been configured with configureModels from render_config (render_test_models_draw_all is
// the player's choice for frames made from a capture: their records carry no DROPPED flag).
// Ticket 4888: the optional modelgeom/frame<NNNNNN>.mgeo members (the CPU-built meshes the live layer held) are listed with
// them (render_cities "1" only; another refuses as the checker does). ApplyModelTapFrame hands the geometry member in
// force (the largest NNNNNN <= frameIndex) to Scene::models()->loadGeometry BEFORE the frame's tap whenever it is not the
// one applied last, and then hands the tap over again even when its member is unchanged, since the layer resolves the
// tap's CPU-built draws against the buffers it holds at the tap's accept (the live layer's order: its store is filled by
// the poll before the frame's tap is put back by beginFrame).
struct ModelTapMembers
{
	std::vector<std::pair<uint32_t, std::string>> members;   // (frame index, path), by frame index
	size_t applied = (size_t)-1;                              // the members[] index handed to the scene last
	uint32_t loads = 0;
	std::vector<std::pair<uint32_t, std::string>> geom;      // ticket 4888: (frame index, path), by frame index
	size_t geomApplied = (size_t)-1;                         // the geom[] index handed to the scene last
	uint32_t geomLoads = 0, geomBuffers = 0;                 // loads, and the buffers of the last one
};
bool LoadModelTapMembers(const std::wstring& bundleDir, const scenebundle::Manifest& m, ModelTapMembers* out, std::string* note,
                         std::string* why);
bool ApplyModelTapFrame(Scene& s, const std::wstring& bundleDir, const scenebundle::Manifest& m, ModelTapMembers& t,
                        uint32_t frameIndex, std::string* why);

// ---- The settle call (contract 6) ------------------------------------------------------------------------------------
// ResidencySource::settle (srv_residency_source.h) and Scene::settle (srv_render_scene.h) fill it. settled = false after
// maxMs: the call still returns ok() (c5r::Err has no timeout code) and the caller refuses the shot.
struct SettleResult
{
	bool settled = false;
	int calls = 0;                 // beforeDraw + idle rounds
	double ms = 0;
	uint32_t pending = 0;          // tiles needing a bake (ResidencyStats::pending, read at the end)
	bool aheadDone = false, stopped = false;
	uint32_t visibleUnbaked = 0;   // the last round's visible land tiles without a bake, + 1 if one had a stale bake
	std::string what;              // one line: settled, or what is still pending (3903: added)
};

}  // namespace srvrender
