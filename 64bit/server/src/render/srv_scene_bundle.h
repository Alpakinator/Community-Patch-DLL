// srv_scene_bundle.h - the scene bundle of the offline scene harness: manifest, member files, reader, writer, checker
// (epic 0301; contract: research/06-tooling-and-workflows/offline-scene-harness.md sections 1, 2 and 10).
// Written 2026-11-14 for this repository (GPL-3.0-or-later).
//
// Contract header by the lead (lead-0301), implemented by task 3902 (srv_scene_bundle.cpp). Every API change against
// the lead's header is in 3902's issue log ("API: ...") so that 3903 and 3904 follow. The file forms as built are in
// the contract's sections 1 and 2 ("as built" lines).
//
// A bundle is a folder: scene.json plus its members. It holds game data (EXE memory, pictures from the game's files)
// and is never committed (D83). The checker and every reader REFUSE (never skip) a bundle with a missing key, an unknown
// key, an unknown format, an older format_version, a member whose SHA-256 or size differs, a missing member or an
// unlisted file (plan 4.1 rule 3). Nothing here throws; the readers write nothing.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "c5r/status.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/water/waves.h"
#include "c5r/ice/ice.h"   // issue 4308: the ice member
#include "feed/srv_feed.h"
#include "units/srv_units_records.h"   // epic 0375, issue 4457: feed.bin's records 3-6 (inline codec, no source to add)
#include "shell_shared.h"

namespace srvrender {
namespace scenebundle {

constexpr const char* kFormat = "c5-scene-bundle";
constexpr uint32_t kFormatVersion = 11;          // raised with every new member kind, changed file form or feed read order
// The first format_version whose bundles carry water_frames.bin (ticket 0622; the block at WaterFrameRecord). A renumber
// (another change taking a version first) moves this line, kFormatVersion and FormatVersionChange's line together.
constexpr uint32_t kWaterFramesFormatVersion = 11;
// The oldest format_version this reader still reads (ticket 4749): a version-9 bundle replays as before (shadow.bin of file
// form 1, no unit shadow fields); an older one is refused as OlderVersion.
constexpr uint32_t kOldestReadFormatVersion = 9;
// What format version v (2..kFormatVersion) changed against v - 1, in one line (the OlderVersion refusal names every change
// between a bundle's version and this reader's); "" for any other v.
const char* FormatVersionChange(uint32_t v);
// Version 2 (epic 0301, issue 3911: epic 0168's fog inputs, D137 rule A): the member fog_clock.bin (FOWAnim's clock at
// the span's first frame), feed.bin's PLOTS caller 3 (the fog's plot-table reads), frames.bin's flag bit 3 (the fog
// drawn); the other forms unchanged. A version-1 bundle is refused as OlderVersion.
// Version 3 (issue 0364, 2026-11-15: epic 0166's decals and epic 0169's shadow and ice, D137 rule A): the member kind
// decal_tap (decals/load<NNNN>.dtap, contract 2.10) and the kinds shadow, shadow_texels, ice_mesh (contract 2.12, issue
// 4308's forms unchanged); render_config carries render_decals and render_decal_mods. A version-2 bundle is refused as
// OlderVersion ("format_version 2 is older than 3").
// Version 4 (epic 0375, issue 4457, 2026-11-16: the unit feed, decision 11 of unit-server-scene.md, MC #2174): feed.bin's
// file form 2 (kFeedFormVersion): DRAIN carries the batch's unitTableGen and the recorder's drain clock, and the records
// 3 UNITS, 4 PATHS, 5 COMBATS, 6 PLAYERS (srvunits::rec, units/srv_units_records.h); the other forms unchanged. A
// version-3 bundle is refused as OlderVersion ("format_version 3 is older than 4"), a feed.bin of form 1 as BadVersion
// ("file form version 1, this reader reads 2").
// Version 5 (issue 0440, 2026-11-16: the unit scene offline, MC #2931): the member kinds units_db (units/gen<10 digits>.db,
// the unit database the scene opened) and units_history (units_history.bin: the controller's state at the span's first
// frame as its exact input history), both with render_units=1 only (the block at UnitsHistoryRecord); the other forms
// unchanged. A version-4 bundle is refused as OlderVersion ("format_version 4 is older than 5").
// Version 6 (ticket 0403, 2026-11-16: the FOWLevel-3 fog particles of 0340): the member kind fog_particles (particles.bin:
// c5r::fow::ParticleSim's byte-exact state at the span's first frame, the block at FogParticlesFile) and the render_config
// key render_fow_particles; the other forms unchanged. A version-5 bundle is refused as OlderVersion ("format_version 5 is
// older than 6").
// Version 7 (ticket 0493, 2026-11-17): no new member and no changed file form; the FEED'S READ ORDER changed. f916762a5
// (ticket 4555's follow-up, MC #3242) made Scene::fowFeed call ResidencySource::onFeedGeneration, which reads the plot table
// through copyPlots(caller 2) at each new snapshot generation the residency has not taken. feed.bin replays the reads in
// the recorded order and refuses any other ("out of order"), so a bundle recorded before that change cannot be replayed.
// A version-6 bundle is refused as OlderVersion at load, the message naming the change (FormatVersionChange).
// Version 8 (ticket 0495, 2026-11-17: the trees of epic 0431 and the near-camera pairs of issue 0371, both drawn by default
// since 2026-11-17): feed.bin's PLOTS caller 4 (the forest's plot-table reads, which went to the live feed before), the
// members forest.bin (kind forest_state, the block at ForestStateFile) and near.bin (kind near_pairs, the block at
// NearPairsFile), render_config render_forest. A version-7 bundle is refused as OlderVersion.
// Version 9 (ticket 4875, epic 0561, 2026-11-18; MC #6158: a new member kind raises the version, rules A and B): the member
// kind model_tap (modeltap/frame<NNNNNN>.mtap, the block at ModelTapFile) and render_config render_cities,
// render_landmarks, render_models_source. A version-8 bundle is refused as OlderVersion; the library is recorded again as
// the epic's own step (lead2-0561 #6158), not by 4875.
// Version 10 (ticket 4749, epic 0042, 2026-11-22: 4785's gap, offline-scene-harness.md 38 and 40): shadow.bin's file form 2,
// whose ShadowRecord (352 B) carries the shadow slot's VPSHD_SLOT_UNIT_MASK_DROPPED flag (kUnitMaskDropped) and ShadowRaw's
// size, so that the player merges the server's unit shadow mask as the live render thread does. NOT refused as older: a
// version-9 bundle is still read (kOldestReadFormatVersion; its shadow.bin of form 1, 344-byte records, read with the flag
// clear and a raw size of 0, which is what the player drew before), so the library needs no re-record for this change;
// a bundle recorded with ServerUnits = 1 and unit shadows on is right only from version 10.
// Version 11 (ticket 0622, 2026-11-23: epic 0615's rule A gap, live-terrain.md 9.4): the member kind water_frames
// (water_frames.bin, the block at WaterFrameRecord): per recorded frame, the water copy the render thread drew with, so
// that the player switches copies at the frame where the live server took a later copy of the load (a Waves_Reload).
// NOT refused as older: versions 9 and 10 are still read and replay as before (the load's last copy for every frame).
// THE RULE (offline-scene-harness.md section 24): a change to the render thread's feed reads (a new copyPlots, copyUnits,
// copyPlayers or drain call, a call moved or dropped, another caller id) raises kFormatVersion with a FormatVersionChange
// line in the same change, and the library is recorded again (rule B), exactly as a new member kind does (rule A).

// The 16-byte header of every binary member of contract 2.2-2.7 (C5HF keeps its own 128-byte header): magic, version,
// header bytes (always 16), reserved 0. Little-endian. Each form's own fixed fields follow it.
struct FileHeader
{
	char magic[4];
	uint32_t version;
	uint32_t headerBytes;
	uint32_t reserved;
};
static_assert(sizeof(FileHeader) == 16, "FileHeader layout");

constexpr char kMagicWaterTap[4]   = {'C', '5', 'W', 'T'};   // water/load<NNNN>.wtap        (2.2)
constexpr char kMagicOverlay[4]    = {'C', '5', 'O', 'V'};   // overlay/<sha16>.ovl          (2.3)
constexpr char kMagicFeed[4]       = {'C', '5', 'F', 'D'};   // feed.bin                     (2.5)
constexpr char kMagicCameras[4]    = {'C', '5', 'C', 'B'};   // cameras.bin, cameras/<name>.vpcam (2.6)
constexpr char kMagicFrames[4]     = {'C', '5', 'F', 'R'};   // frames.bin                   (2.7)
constexpr char kMagicWaterClock[4] = {'C', '5', 'W', 'C'};   // water_clock.bin              (2.7)
constexpr char kMagicFogClock[4]   = {'C', '5', 'F', 'C'};   // fog_clock.bin                (2.11; 3911)
constexpr char kMagicShadow[4]     = {'C', '5', 'S', 'G'};   // shadow.bin                   (4308)
constexpr char kMagicShadowTex[4]  = {'C', '5', 'S', 'T'};   // shadow/<sha16>.shtx          (4308)
constexpr char kMagicIceMesh[4]    = {'C', '5', 'I', 'C'};   // ice/gen<NNNNNNNNNN>.ice      (4308)
constexpr char kMagicUnitsHistory[4] = {'C', '5', 'U', 'H'}; // units_history.bin            (format version 5, 0440)
constexpr char kMagicFogParticles[4] = {'C', '5', 'P', 'S'}; // particles.bin                (format version 6, 0403)
constexpr char kMagicForestState[4] = {'C', '5', 'T', 'R'};  // forest.bin                   (format version 8, 0495)
constexpr char kMagicNearPairs[4] = {'C', '5', 'N', 'P'};    // near.bin                     (format version 8, 0495)
constexpr char kMagicWaterFrames[4] = {'C', '5', 'W', 'F'};  // water_frames.bin             (format version 11, 0622)

enum class MemberKind : uint32_t
{
	Heightfield = 1, WaterTap, OverlayTexture, Feed, Frames, CameraBlocks, WaterClock,
	DumpColour, DumpDepth, NamedCamera, NamedCameraBlock, StockShot, StockCapture, StockExport,
	FogClock,   // format version 2 (issue 3911)
	Shadow, ShadowTexels, IceMesh,   // issue 4308 (format version 3 since issue 0364: see the block at ShadowRecord)
	DecalTap,   // format version 3 (issue 0364): decals/load<NNNN>.dtap
	UnitsDb, UnitsHistory,   // format version 5 (issue 0440): units/gen<NNNNNNNNNN>.db, units_history.bin
	FogParticles,            // format version 6 (ticket 0403): particles.bin
	ForestState, NearPairs,  // format version 8 (ticket 0495): forest.bin, near.bin
	ModelTap,                // format version 9 (ticket 4875): modeltap/frame<NNNNNN>.mtap
	ModelGeom,               // format version 9, optional (ticket 4888): modelgeom/frame<NNNNNN>.mgeo
	WaterFrames,             // format version 11 (ticket 0622): water_frames.bin
};
// The manifest's "kind" strings of contract 1: heightfield, water_tap, overlay_texture, feed, frames, camera_blocks,
// water_clock, dump_colour, dump_depth, named_camera, named_camera_block, stock_shot, stock_capture, stock_export,
// fog_clock (format version 2); shadow, shadow_texels, ice_mesh (issue 4308), decal_tap (format version 3, issue 0364);
// units_db, units_history (format version 5, issue 0440); fog_particles (format version 6, ticket 0403); forest_state,
// near_pairs (format version 8, ticket 0495); model_tap (format version 9, ticket 4875); model_geom (format version 9,
// optional, ticket 4888); water_frames (format version 11, ticket 0622).
const char* MemberKindName(MemberKind k);
bool MemberKindFromName(const std::string& s, MemberKind* out);
// The version of each kind's file form this library reads and writes (1 for every kind in format_version 1).
uint32_t MemberKindVersion(MemberKind k);
// The kind's file form a bundle of format_version v carries (ticket 4749: shadow 1 below version 10, else
// MemberKindVersion); the manifest's member version and the file's header must say it.
uint32_t MemberKindVersionFor(MemberKind k, uint32_t formatVersion);

struct Member
{
	MemberKind kind = MemberKind::Heightfield;
	uint32_t version = 1;
	uint64_t bytes = 0;
	std::string sha256;                            // 64 lowercase hex
};

// scene.json (contract 1). Every key there is a field here; unknown keys are refused by the checker. String maps hold
// the values as JSON strings (scene.json has no numbers in recorder.keys, pins or render_config: as built).
struct Manifest
{
	std::string format = kFormat;
	uint32_t formatVersion = kFormatVersion;
	std::string bundleId;                          // <save>_<yyyymmdd-hhmmss>_<machine>
	std::string created;                           // ISO 8601 with offset, from the clock
	std::map<std::string, std::string> recorderKeys;   // "recorder"."keys": the render_test_record* keys as given
	std::string serverExeSha256, repoCommit, buildConfig;   // "recorder"."server_exe_sha256", "repo_commit", "build_config"
	// "pins": nested one level in scene.json ({"i0_lock": {"commit": ...}, "server_sha256": ...}); here flat with
	// dotted names ("i0_lock.commit", "server_sha256"). COMMON 18.
	std::map<std::string, std::string> pins;
	std::string saveName, saveSha256;              // "save"."name", "sha256"
	uint32_t mapW = 0, mapH = 0, turn = 0, loadSeq = 0;   // "save"."map_w", "map_h", "turn", "load_seq"
	// "asset_set": exactly the dict of tools/visualcheck/visualcheck.py asset_set() (D94): vt ("present", "absent",
	// "packages only", "present (other hash)", "unknown"), vt_hash, vt_files, install, machine, why ("" when none).
	// ComputeAssetSet below is the C++ port of it; the test suite checks it against the Python function.
	std::string vt = "unknown", vtHash;
	uint32_t vtFiles = 0;
	std::string install, machine, assetWhy;
	std::string adapterLuid, adapterDescription, adapterDriver;   // "adapter"."luid", "description", "driver"
	std::map<std::string, std::string> renderConfig;   // every resolved HostConfig value the scene reads
	int64_t firstFrameId = 0;                      // "frames"."first_id", "count", "dumped"
	uint32_t frameCount = 0;
	std::vector<int64_t> dumpedFrames;
	std::map<std::string, Member> members;         // relative path (forward slashes) -> member
	std::vector<std::string> versionNotes;         // every renderer input on main without a member (contract 3)
	// water_refused, overlay_refused, overlay_stale, feed_overflow_events, record_errors: all five, no other
	std::map<std::string, int64_t> counters;
};
// The five counter names of contract 1, in that order.
extern const char* const kCounterNames[5];

// Result codes of the checker and the readers; the tools map them to their exit codes with RefusalExitCode.
enum class Refusal : uint32_t
{
	None = 0, NoManifest, BadJson, MissingKey, UnknownKey, UnknownFormat, OlderVersion, NewerVersion,
	MissingMember, UnlistedFile, ShaMismatch, BadMagic, BadVersion, Truncated, WrongSize, EmptyFeed,
	PlotsDiffBeforeFull, BadMipCount, Incomplete, Io,
	BadPath,      // a member path that is absolute, has "..", "." or empty segments, a backslash or ':' (3902)
	BadRecord,    // a record inside a member that is out of order or out of range (feed, frames, dumps; 3902)
	BadValue,     // a manifest value out of range (a bad sha256 string, a dumped frame outside the span; 3902)
};
const char* RefusalName(Refusal r);
// 0 for None, else 20 + the code (21..42): the exit code of every tool that refuses a bundle (3905, 3906, the tests).
int RefusalExitCode(Refusal r);

struct CheckResult
{
	Refusal refusal = Refusal::None;
	std::string member;                            // the member the refusal names ("" for the manifest)
	std::string why;                               // one line for the log and the tool's output
	// "scenebundle: REFUSED <RefusalName> <member or scene.json>: <why>" (or "scenebundle: OK")
	std::string line() const;
};

// ---- small helpers (also for 3903 and 3904) --------------------------------------------------------------------------
std::string Sha256Hex(const void* data, size_t bytes);            // "" if bcrypt.dll's SHA256 is not available
c5r::Status Sha256FileHex(const std::wstring& path, std::string* hex, uint64_t* bytes);
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& w);                        // UTF-8
// D94's asset set, ported from visualcheck.py asset_set() and HashCache.vt (the method of 9047: SHA-256 over the sorted
// lines "<path below Assets/DLC/VT, forward slashes, lower case> <SHA-256 of the file>" joined by "\n", no final
// newline). Fills vt, vtHash, vtFiles, install, machine, assetWhy of *m. Fails only when hashing is unavailable.
c5r::Status ComputeAssetSet(const std::wstring& install, const std::string& machine, Manifest* m);

// ---- the manifest -----------------------------------------------------------------------------------------------------
// scene.json's text (UTF-8, two-space indent, keys sorted inside each object, LF line ends) and back. ParseManifest
// refuses (res set, Status not ok) a missing or unknown key, a wrong JSON type, an unknown format, an older or newer
// format_version, a member path that is not a clean relative path, a bad sha256 string. The format and the version are
// checked before the keys, so an older bundle is refused as OlderVersion and not as a key it lacks.
std::string ManifestToJson(const Manifest& m);
c5r::Status ParseManifest(const std::string& json, Manifest* out, CheckResult* res);

// ---- reading --------------------------------------------------------------------------------------------------------
c5r::Status ReadManifest(const std::wstring& bundleDir, Manifest* out, CheckResult* res);
// The whole check: the manifest (as ParseManifest), "incomplete" in version_notes (Incomplete), every listed file
// present (MissingMember) with its size and SHA-256 (ShaMismatch), no unlisted file (UnlistedFile; scene.json is the
// only file not listed), every member parsed by its kind's reader below, and the cross-checks: frames.bin and
// cameras.bin hold frames.count records starting at first_id, every dumped frame is in the span with both dumps, each
// dump's size matches its frame's target size, every bound overlay of frames.bin names an overlay member, an overlay
// file's name is the first 16 hex of its SHA-256. Writes nothing. The Status is ok() exactly when res->refusal is None.
c5r::Status CheckBundle(const std::wstring& bundleDir, CheckResult* res);
// Reads one member whole after checking its size and SHA-256 against the manifest (the parsers below take the bytes).
c5r::Status ReadMember(const std::wstring& bundleDir, const Manifest& m, const std::string& path,
                       std::vector<uint8_t>* bytes, CheckResult* res);

// ---- the member forms: parse (checks, refusal in res) and encode -------------------------------------------------------
// 2.1 heightfield/load<NNNN>.c5hf: C5HF through c5h::readTap; the file is exactly tapSize() bytes (WrongSize else).
c5r::Status ParseHeightfield(const std::vector<uint8_t>& b, c5r::heightfield::HeightGrid* out, CheckResult* res);

// 2.2 water/load<NNNN>.wtap: header, u32 tapBlockBytes (= sizeof(VPShellWaterTap) = 320), the block, haveCam, gridSeq,
// samplesX, samplesY (u32 each), then the image (tb.imageBytes bytes, VPShellWaterImage first). The parser checks the
// sizes, the image header's magic, version, headerBytes and imageBytes against the block; the image's records are
// checked by the water tap's own file entry (3903: WaterTapReader::loadImage runs CheckImage).
struct WaterTapFile
{
	VPShellWaterTap block = {};
	uint32_t haveCam = 0, gridSeq = 0, samplesX = 0, samplesY = 0;
	size_t imageOffset = 0, imageBytes = 0;        // the image inside the member's bytes
};
constexpr uint32_t kWaterTapFixedBytes = 16 + 4 + (uint32_t)sizeof(VPShellWaterTap) + 16;   // 356: image at this offset
c5r::Status ParseWaterTap(const std::vector<uint8_t>& b, WaterTapFile* out, CheckResult* res);
void EncodeWaterTap(const VPShellWaterTap& block, uint32_t haveCam, uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY,
                    const void* image, size_t imageBytes, std::vector<uint8_t>* out);

// 2.3 overlay/<first 16 hex of its SHA-256>.ovl: header, u32 width, height, mips, dxgiFormat, u32 rowPitch[12],
// u32 offset[12] (from the start of the file), then the 12 mips tight, mip 0 first. 128 B of header and table, then
// 22,369,620 B of texels: 22,369,748 B. Refused: mips != 12 (BadMipCount), width or height != 2048, a format other than
// R8G8B8A8_UNORM (28), a row pitch or offset that is not the tight layout (WrongSize), a short file (Truncated).
constexpr uint32_t kOverlaySize = 2048, kOverlayMips = 12, kOverlayFormat = 28;   // DXGI_FORMAT_R8G8B8A8_UNORM
constexpr uint32_t kOverlayTableBytes = 16 + 16 + 4 * kOverlayMips * 2;            // 128
constexpr uint64_t kOverlayTexelBytes = 22369620;
struct OverlayFile
{
	uint32_t width = 0, height = 0, mips = 0, dxgiFormat = 0;
	uint32_t rowPitch[kOverlayMips] = {}, offset[kOverlayMips] = {};
	uint32_t mipWidth(uint32_t i) const { return (width >> i) ? (width >> i) : 1; }
	uint32_t mipHeight(uint32_t i) const { return (height >> i) ? (height >> i) : 1; }
};
c5r::Status ParseOverlay(const std::vector<uint8_t>& b, OverlayFile* out, CheckResult* res);
// texels: the 12 mips tight, mip 0 first (kOverlayTexelBytes); BadLayout if the size differs.
c5r::Status EncodeOverlay(const void* texels, size_t bytes, std::vector<uint8_t>* out);

// 2.5 feed.bin (file form 2 since format version 4): header, then records {u32 type, u32 payload bytes, payload}.
// DRAIN (1): u64 loopIter, i64 nextFrameId, u32 snapshotGen, width, height, u64 dropped, u32 unitTableGen, f64 drainMs (the
// recorder's clock, QpcToMs scale, when the drain returned; -1 unknown), u32 nEvents, Event[nEvents], u32 nChanged,
// u32[nChanged]. PLOTS (2): u64 loopIter, u32 caller (1 baseline, 2 changed, 3 the fog: format version 2), snapshotGen,
// width, height, full; full = 1: PlotRecord[w*h]; full = 0: u32 nDiff, then {u32 index, PlotRecord}[nDiff] against the
// previous PLOTS record (same width and height).
// Records 3-6 (format version 4, issue 4457; payloads in units/srv_units_records.h, decoded by srvunits::rec::Decoder):
// 4 PATHS, 5 COMBATS and 6 PLAYERS of caller 2 belong to the DRAIN right before them (same loopIter, at most one of each,
// nothing else in between) and are folded into that DRAIN's FeedRecord::batch (paths, pathPlots, combats, combatMembers,
// players). 3 UNITS and 6 PLAYERS of caller 1 (the unit scene's CopyUnits / CopyPlayers, taken while it takes that drain's
// batch) belong to the newest DRAIN of the same loopIter (PLOTS records may stand between: the residency's and the fog's
// reads come first) and are folded into its unitCopies / playerCopies, in order. So ParseFeed yields DRAIN and PLOTS
// records only, as before format version 4, and a reader without units (sceneshot) replays a version-4 feed unchanged.
// Refused: no record (EmptyFeed), a diff before any full table (PlotsDiffBeforeFull), an unknown type, a caller other
// than 1, 2 or 3, a loopIter that goes back, a diff index outside the table or not increasing, a diff of another size, a
// drainMs that is not finite, a record 3-6 the codec refuses (its code named: UnitsDiffBeforeFull, LoopBack, ...), a
// record 4 / 5 / 6-of-caller-2 not right after its DRAIN or twice for one DRAIN, a record 3 / 6-of-caller-1 with no DRAIN
// of its loopIter before it (BadRecord), a payload whose size
// differs from what its counts announce (WrongSize), a record past the end (Truncated).
constexpr uint32_t kFeedFormVersion = 2;          // feed.bin's file form (1 until format version 3)
enum : uint32_t { kFeedDrain = 1, kFeedPlots = 2, kFeedUnits = 3, kFeedPaths = 4, kFeedCombats = 5, kFeedPlayers = 6 };
// One UNITS record (format version 4): a CopyUnits the unit scene took.
struct UnitCopy
{
	uint32_t caller = 0;                           // srvunits::rec::kUnitsFirstCopy / kUnitsLaterCopy
	uint32_t tableGen = 0;
	bool full = false;                             // as stored
	std::vector<SrvFeed::UnitRecord> units;        // the whole table after the record (diffs applied), sorted by (owner, id)
};
struct FeedRecord
{
	uint32_t type = 0;                             // kFeedDrain or kFeedPlots (records 3-6 are folded into a DRAIN)
	uint64_t loopIter = 0;
	int64_t nextFrameId = -1;                      // DRAIN
	double drainMs = -1;                           // DRAIN (format version 4)
	SrvFeed::Batch batch;                          // DRAIN (with its records 4, 5, 6-of-caller-2 folded in)
	std::vector<UnitCopy> unitCopies;              // DRAIN: its records 3 UNITS, in order (format version 4)
	std::vector<std::vector<SrvFeed::PlayerRecord>> playerCopies;   // DRAIN: its records 6 PLAYERS of caller 1, in order
	uint32_t caller = 0, snapshotGen = 0, width = 0, height = 0;   // PLOTS
	bool full = false;                             // PLOTS: as stored
	std::vector<SrvFeed::PlotRecord> plots;        // PLOTS: the whole table after this record (diffs applied)
};
c5r::Status ParseFeed(const std::vector<uint8_t>& b, std::vector<FeedRecord>* out, CheckResult* res);
// The recorder's side: each call returns one record's bytes for Writer::append("feed.bin", ...). addPlots stores a
// full table the first time and whenever width, height or the generation changed, else a diff against the previous
// table (a full table again when the diff would not be smaller). addDrain writes the DRAIN and, when the batch has
// them, its PATHS, COMBATS and PLAYERS (caller 2) records; a batch the unit codec refuses writes nothing and fails.
// addUnits / addPlayers write the scene's CopyUnits / CopyPlayers (srvunits::rec::Encoder's rules).
class FeedEncoder
{
public:
	c5r::Status addDrain(uint64_t loopIter, int64_t nextFrameId, const SrvFeed::Batch& b, std::vector<uint8_t>* out,
	                     double drainMs = -1.0);
	void addPlots(uint64_t loopIter, uint32_t caller, uint32_t snapshotGen, uint32_t width, uint32_t height,
	              const std::vector<SrvFeed::PlotRecord>& table, std::vector<uint8_t>* out, bool forceFull = false);
	// issue 4457: caller srvunits::rec::kUnitsFirstCopy (the scene's first copy) or kUnitsLaterCopy
	c5r::Status addUnits(uint64_t loopIter, uint32_t caller, uint32_t tableGen, const std::vector<SrvFeed::UnitRecord>& table,
	                     std::vector<uint8_t>* out);
	c5r::Status addPlayers(uint64_t loopIter, const std::vector<SrvFeed::PlayerRecord>& rows, std::vector<uint8_t>* out);   // caller 1
private:
	srvunits::rec::Encoder m_units;
	std::vector<SrvFeed::PlotRecord> m_prev;
	uint32_t m_gen = 0, m_w = 0, m_h = 0;
	bool m_have = false;
};

// 2.6 cameras.bin and cameras/<name>.vpcam: header, u32 blockBytes (= sizeof(VPShellCamera) = 712), then the blocks.
// cameras.bin holds one block per recorded frame; a .vpcam exactly one. Refused: blockBytes != 712 (WrongSize), a
// partial block (Truncated), a .vpcam with another count (WrongSize).
constexpr uint32_t kCameraBlockBytes = (uint32_t)sizeof(VPShellCamera);
static_assert(sizeof(VPShellCamera) == 712, "VPShellCamera: cameras.bin's block size (contract 2.6)");
static_assert(sizeof(VPShellWaterTap) == 320, "VPShellWaterTap: the .wtap block size (contract 2.2)");
c5r::Status ParseCameras(const std::vector<uint8_t>& b, std::vector<VPShellCamera>* out, CheckResult* res);

// 2.7 frames.bin: header, u32 recordBytes (= sizeof(FrameRecord) = 344), then one record per drawn frame of the span.
// Refused: recordBytes != 344 (WrongSize), a partial record (Truncated), a frameId that does not increase, nowMs not
// finite, numViews above 4, flags above bit 3 (BadRecord).
#pragma pack(push, 8)
struct FrameRecord
{
	int64_t frameId;              // the camera block's frameStamp
	uint64_t loopIter;
	double nowMs;                 // the value Scene::draw passes to WaterScene::decide (contract 6)
	uint32_t width, height;       // the sink's targets
	uint32_t overlayBound;        // the overlay pair
	uint32_t overlayGeneration;
	uint64_t overlayPresentCount;
	uint32_t overlayNumViews;
	float overlayProj[4][16];
	uint8_t overlaySha256[32];    // the texture's SHA-256 (binary); all zero when not bound
	uint32_t flags;               // bit 0 dumped, bit 1 water drawn with the module, bit 2 waves drawn, bit 3 the fog drawn
};
#pragma pack(pop)
static_assert(sizeof(FrameRecord) == 344, "FrameRecord layout");
// kFrameFog (format version 2, issue 3911): the fog's mask was bound in the draw (FowScene::terrainInputs().on after it)
enum : uint32_t { kFrameDumped = 1, kFrameWaterModule = 2, kFrameWaves = 4, kFrameFog = 8 };
c5r::Status ParseFrames(const std::vector<uint8_t>& b, std::vector<FrameRecord>* out, CheckResult* res);

// Header plus the record size of a stream member (feed.bin: the header alone): what Writer::appendStart writes.
void EncodeStreamHead(MemberKind kind, std::vector<uint8_t>* out);

// 2.7 water_clock.bin: header, f64 clockStartMs, waveLastMs, lastNowMs, u32 nWaves, u32 waveRecordBytes (16), then
// WaveRecord[nWaves]. Refused: waveRecordBytes != 16 or bytes after the waves (WrongSize), short (Truncated), a
// non-finite value (BadRecord).
struct WaterClockFile
{
	double clockStartMs = -1, waveLastMs = -1, lastNowMs = -1;
	std::vector<c5r::water::WaveRecord> waves;
};
c5r::Status ParseWaterClock(const std::vector<uint8_t>& b, WaterClockFile* out, CheckResult* res);
void EncodeWaterClock(const WaterClockFile& c, std::vector<uint8_t>* out);

// 2.11 fog_clock.bin (format version 2, epic 0301, issue 3911): FOWAnim's clock of the fog scene at the span's first
// frame, before that frame's draw (srv_fow_scene.h FowClockState): header, f64 clockS (T in seconds, >= 0), f64
// clockLastMs (the nowMs of the last frame the fog drew, -1 when none). 32 bytes. Refused: short (Truncated), longer
// (WrongSize), a value not finite or clockS below 0 (BadRecord).
struct FogClockFile
{
	double clockS = 0, clockLastMs = -1;
};
constexpr uint32_t kFogClockBytes = 32;
c5r::Status ParseFogClock(const std::vector<uint8_t>& b, FogClockFile* out, CheckResult* res);
void EncodeFogClock(const FogClockFile& c, std::vector<uint8_t>* out);

// ---- epic 0169, issue 4308: the models' shadow and the polar ice (D137; the contract text is in tracker issue 4308,
// "Bundle members", until the lead moves it into offline-scene-harness.md) ---------------------------------------------
// shadow.bin (kind shadow): header, u32 recordBytes (= sizeof(ShadowRecord) = 352 in file form 2, format version 10, ticket
// 4749; 344 in form 1, format version 9: the record without rawWidth / rawHeight), then one record per frame of frames.bin,
// in its order and with its frame ids: what the scene bound in that frame's draw (Scene::lastShadowFrame, lastIceDrawn).
// shadow/<first 16 hex of its SHA-256>.shtx (kind shadow_texels): header, u32 width, height, dxgiFormat (56 R16_UNORM or 10
// R16G16B16A16_FLOAT), bytesPerTexel (2 or 8), then the texels tight, row 0 first (the acquired slot's copy, staged on the
// server's device); deduplicated by content as the overlay's .ovl files (equal maps share one file).
// ice/gen<generation, 10 digits>.ice (kind ice_mesh): header, u32 generation, loadSeq, vbBytes, ibBytes, chunkCount,
// fieldsBytes (= c5r::ice::kFieldsSize, 0x1A0), fieldsValid (0 / 1), then the fields' bytes (Ice+0xFE80..+0x1001F), per
// chunk 7 x 4 bytes (box min x, min y, max x, max y as float; triangles, startIndex as u32; baseVertex as i32), then the VB
// and the IB bytes: the c5r::ice::Mesh the scene applied (IceTapReader::mesh()), written once per generation. *Not* the
// tap's image form (IceTapReader::loadFile's): the reader's contract hands over the Mesh, not the image bytes; the mesh
// carries everything loadFile yields.
// Format version: these three kinds are members of format version 3 (issue 0364, 2026-11-15; 4308 had made them optional
// members of version 2, which is now refused as older). The checker keeps shadow.bin optional (a recording without the
// frame hook's seam-G call: the recorder's own tests); the player refuses a bundle whose render_config has render_shadow=1
// or render_ice=1 without shadow.bin (LoadSeamGMembers).
#pragma pack(push, 8)
struct ShadowRecord
{
	int64_t frameId;              // = frames.bin's record at the same index
	uint32_t flags;               // kShadowBound | kShadowQ0 | kIceDrawn
	uint32_t generation;          // the shadow slot's (0 when nothing bound)
	uint64_t presentCount;
	uint32_t quality, dxgiFormat, width, height, numViews;
	float strength;
	float matrices[4][16];        // ShadowMatrix0 per EXE view, row-major as the EXE stores them
	uint8_t texelSha256[32];      // kShadowBound: the .shtx file's SHA-256 (binary), else zero
	uint32_t iceGeneration;       // kIceDrawn: the ice/gen<...>.ice member of the mesh drawn, else 0
	uint32_t iceLoadSeq;
	// file form 2 (format version 10, ticket 4749): ShadowRaw's size of the slot's Present (ShadowFrame.rawWidth / rawHeight,
	// 0 from an older producer); in form 1 not stored, read as 0
	uint32_t rawWidth, rawHeight;
};
#pragma pack(pop)
static_assert(sizeof(ShadowRecord) == 352, "ShadowRecord layout");
constexpr uint32_t kShadowRecordBytesForm1 = 344;   // format version 9: the record up to iceLoadSeq
// kUnitMaskDropped (form 2, ticket 4749): the bound triple's slot carried VPSHD_SLOT_UNIT_MASK_DROPPED (the compositor
// dropped the EXE's unit mask draws from that map; ShadowFrame.unitMaskDropped), with kShadowBound or kShadowQ0 only
enum : uint32_t { kShadowBound = 1, kShadowQ0 = 2, kIceDrawn = 4, kUnitMaskDropped = 8 };
// Refused: a file form other than the one formatVersion carries (BadVersion: 1 below format version 10, 2 from it),
// recordBytes other than that form's 344 / 352 (WrongSize), a partial record (Truncated), flags above bit 2 (form 1) or
// bit 3 (form 2), kUnitMaskDropped without kShadowBound or kShadowQ0, a raw size above 16384 or with one side 0, both
// kShadowBound and kShadowQ0, a frameId that does not increase, quality above 3, numViews above 4 (or 0 with a shadow
// bound), a strength not finite or outside [0, 1], a bound matrix not finite, a bound format other than 56 / 10 or a size
// of 0, a bound record without a texel hash, kShadowQ0 with a texture or a strength other than 0, kIceDrawn with
// generation 0 (BadRecord). Form-1 records come back with rawWidth = rawHeight = 0.
c5r::Status ParseShadow(const std::vector<uint8_t>& b, std::vector<ShadowRecord>* out, CheckResult* res,
                        uint32_t formatVersion = kFormatVersion);
struct ShadowTexFile
{
	uint32_t width = 0, height = 0, dxgiFormat = 0, bytesPerTexel = 0;
	size_t texelOffset = 0;
};
// Refused: a format other than 56 (2 bytes) / 10 (8 bytes) or a bytesPerTexel that does not fit it, a size of 0 or above
// 16384 (BadRecord), texels not exactly width x height x bytesPerTexel (WrongSize / Truncated).
c5r::Status ParseShadowTexels(const std::vector<uint8_t>& b, ShadowTexFile* out, CheckResult* res);
void EncodeShadowTexels(uint32_t width, uint32_t height, uint32_t dxgiFormat, const void* texels, size_t bytes,
                        std::vector<uint8_t>* out);
// Refused: sizes that do not add up (WrongSize / Truncated), fieldsBytes != 0x1A0, vb not whole 8-byte vertices, ib not
// whole R16 indices, 0 or more than 4096 chunks, a chunk box not finite or min > max, 3 x triangles past the IB (BadRecord).
// The module's own checks (c5r::ice::Renderer::setMesh) run when the player applies it.
c5r::Status ParseIceMesh(const std::vector<uint8_t>& b, c5r::ice::Mesh* out, uint32_t* generation, uint32_t* loadSeq,
                         CheckResult* res);
void EncodeIceMesh(const c5r::ice::Mesh& m, uint32_t generation, uint32_t loadSeq, std::vector<uint8_t>* out);
std::string IceMeshPath(uint32_t generation);     // "ice/gen0000000007.ice"
std::string ShadowTexPath(const std::string& sha256Hex);   // "shadow/<first 16 hex>.shtx"

// ---- ticket 0622 (format version 11, kWaterFramesFormatVersion): water_frames.bin (kind water_frames) ---------------------
// header (magic C5WF, file form 1), u32 recordBytes (= sizeof(WaterFrameRecord) = 56), then one record per frame of
// frames.bin, in its order and with its frame ids: the water copy the render thread drew that frame with. The live reader
// takes a copy inside Scene::poll (WaterScene::poll -> WaterTapReader::poll, whose image sink is the recorder's
// onWaterImage, then WaterScene::useImage), before the frame's draw; so the copy in force at a frame is the newest copy the
// sink saw before that frame's afterDraw. Before format version 11 the player drew the load's last copy (the highest
// copySeq) for every frame, which is wrong for a span holding a Waves_Reload (epic 0615, live-terrain.md 9.4).
// The record names the copy by its water/*.wtap member's SHA-256 (the manifest's sha256 of that member) and repeats the
// member's block.loadSeq and block.copySeq. A frame drawn before any copy was accepted: flags 0 and the rest zero.
#pragma pack(push, 8)
struct WaterFrameRecord
{
	int64_t frameId;              // = frames.bin's record at the same index
	uint32_t flags;               // kWaterCopyInForce
	uint32_t loadSeq;             // kWaterCopyInForce: the copy's VPShellWaterTap loadSeq, else 0
	uint32_t copySeq;             // kWaterCopyInForce: its copySeq, else 0
	uint32_t reserved;            // 0
	uint8_t wtapSha256[32];       // kWaterCopyInForce: the .wtap member's SHA-256 (binary), else zero
};
#pragma pack(pop)
static_assert(sizeof(WaterFrameRecord) == 56, "WaterFrameRecord layout");
enum : uint32_t { kWaterCopyInForce = 1 };
// Refused (BadRecord): flags above bit 0, reserved not 0, a record without kWaterCopyInForce whose other fields are not
// zero, a record with it and a zero hash, a frameId that does not increase, a copy in force followed by none, a copy of the
// same load whose copySeq goes back; recordBytes other than 56 (WrongSize), a partial record (Truncated). The checker adds:
// the record count and frame ids are frames.bin's; each named hash is a water_tap member whose block has the record's load
// and copy seq; a format-11 bundle with water_tap members and frames has the member (MissingMember); a bundle below format
// 11 with it (BadValue).
c5r::Status ParseWaterFrames(const std::vector<uint8_t>& b, std::vector<WaterFrameRecord>* out, CheckResult* res);
std::string Sha256HexOf(const uint8_t sha[32]);   // 64 lowercase hex digits of a binary SHA-256

// ---- 2.10 decals/load<NNNN>.dtap (kind decal_tap, format version 3, issue 0364; epic 0166's file form,
// research/05-graphics-composition/terrain-decals.md 10.4) -----------------------------------------------------------------
// The decal tap's image as the reader accepted it (shell/common/shell_decal_tap.h VPShellDecalImage first: magic C5DT,
// version 1, headerBytes 112, decalBytes 48, exactly imageBytes long; no 16-byte member header: the image carries its own),
// the set in force at the span's first frame (DecalTapReader::writeCurrentImage). NNNN = the image's loadSeq, 4 digits.
// At most one per bundle. Checked here: the file at least 112 bytes (Truncated), magic (BadMagic), version (BadVersion),
// headerBytes 112 and decalBytes 48 (WrongSize), imageBytes = the file size (shorter: Truncated, longer: WrongSize); then
// every other check by the reader's own file entry (DecalTapReader::loadImage, which loadFile runs on the file's bytes:
// BadRecord with the reader's reason). The file name's NNNN is the image's loadSeq (BadRecord). Cross-checks (CheckBundle):
// render_config render_decals "1" and no decal_tap member without the recorder's note "decals: no decal set in force..."
// (MissingMember); a decal_tap member while render_decals is not "1", or a second one (BadValue).
constexpr uint32_t kDecalHeaderBytes = 112, kDecalRecordBytes = 48;
constexpr const char* kDecalNoSetNote = "decals: no decal set in force at the span's first frame";   // version_notes prefix
struct DecalTapFile
{
	uint32_t generation = 0, loadSeq = 0, decalCount = 0, nameCount = 0, vertexCount = 0;
	uint64_t imageBytes = 0;
};
c5r::Status ParseDecalTap(const std::vector<uint8_t>& b, DecalTapFile* out, CheckResult* res);
std::string DecalTapPath(uint32_t loadSeq);       // "decals/load0001.dtap" (loadSeq 0..9999; "" above)

// ---- format version 9 (ticket 4875, epic 0561): modeltap/frame<NNNNNN>.mtap (kind model_tap) ---------------------------
// The compositor's model tap frame the server's model module used, as is (shell/common/shell_model_tap.h: the frame
// image VPModelTapFrame first, magic C5MT, version 1, headerBytes 128, drawBytes 128, exactly imageBytes long; no 16-byte
// member header: the image carries its own). NNNNNN = the index in frames.bin of the first frame drawn with it in force
// (6 digits, < the frame count); a member is written only when the module's image changed since the previous member
// (byte compare by hash): a frame of the span draws with the member of the largest NNNNNN <= its index. Every frame would
// cost up to the slot size (4 MB by default) per frame; a still camera repeats the same frame bytes, and a moving one
// changes VS cb1 (the view) in every frame, so a pan records one member per frame and a still span one in all.
// Checked here: the file at least 128 bytes (Truncated), magic (BadMagic), version (BadVersion), headerBytes and drawBytes
// 128 (WrongSize), imageBytes = the file size (shorter: Truncated, longer: WrongSize), the image hash (VPModelHash64 of
// [headerBytes, imageBytes)) equal to the header's (BadRecord); then, where the build has it, every other check of
// c5r::landmarks::readTap (4873; BadRecord with its Status). Cross-checks: render_config render_cities or
// render_landmarks "1" and no model_tap member without the recorder's note kModelNoFrameNote (MissingMember); a
// model_tap member while both are not "1" (BadValue); NNNNNN not below frames.bin's frame count (BadRecord).
constexpr uint32_t kModelTapHeaderBytes = 128, kModelTapDrawBytes = 128;
constexpr const char* kModelNoFrameNote = "models: no model tap frame in force at the span's first frame";   // version_notes prefix
// Ticket 4883: the recorder's gap note when the model layer held CPU-built meshes in the span (ticket 4879's geometry
// section, Local\VPShellModelGeom.<pid>): the bundle has no member for them, so the replay draws the models without them
// and cannot equal live (the standing check reports such a bundle as an expected failure, keyed on this prefix). The gap
// is the offline library's only: the product draws them live. A member for the section is a follow-up of epic 0561.
// Ticket 4888: the member below replaces the note; the recorder writes the note only when a model_geom member cannot be
// written (the bundle is then also marked incomplete), and a bundle recorded before 4888 may still carry it.
constexpr const char* kModelGeomNote = "gap: models: CPU-built meshes";   // version_notes prefix
struct ModelTapFile
{
	uint64_t frameSeq = 0, presentCount = 0;
	uint32_t drawCount = 0, imageBytes = 0, latchedBits = 0, setGeneration = 0;
	uint32_t dropped[2] = {0, 0};
};
c5r::Status ParseModelTap(const std::vector<uint8_t>& b, ModelTapFile* out, CheckResult* res);
std::string ModelTapPath(uint32_t frameIndex);    // "modeltap/frame000000.mtap" (0..999999; "" above)
bool ModelTapIndex(const std::string& path, uint32_t* frameIndex);   // the NNNNNN of a member path

// ---- format version 9, optional member (ticket 4888, epic 0561): modelgeom/frame<NNNNNN>.mgeo (kind model_geom) ---------
// The CPU-built meshes the server's model layer held (ticket 4879's geometry section, Local\VPShellModelGeom.<pid>: the
// walls, the Great Wall, the road and rail bridge; city-renderer.md 13), with their bytes as the layer's store holds them
// (the section's entries the layer accepted, each checked by its content hash on entry). NNNNNN = the frames.bin index from
// which the set is in force; the recorder writes one at the span's first frame when the layer holds any, and another at
// every later frame where the held set changed (an empty list when it then holds none). A frame of the span replays with
// the member of the largest NNNNNN <= its index; with none in force the layer holds nothing. The format version stays 9:
// the member is optional, and a bundle without it is one whose layer held no such buffer (every format-9 bundle recorded
// before 4888 either held none or carries kModelGeomNote).
// Form: the 16-byte header (magic C5GB, version 1, headerBytes 16), u32 count, u32 entryBytes (32), u64 dataBytes, then
// `count` entries of 32 bytes {u64 hash, u32 kind (1 index R32, 2 vertex), u32 stride, u64 offset, u64 bytes}, then the
// data (each entry's bytes at its offset from the data's start, back to back in entry order).
// Checked here: the file shorter than 32 bytes or than its entries and data (Truncated), magic (BadMagic), version or
// headerBytes not 16 (BadVersion, every header's rule), entryBytes not 32 (WrongSize), more than kModelGeomMaxEntries entries or a tail after the
// data (WrongSize); per entry (BadRecord): kind 1 or 2, stride 4 for an index buffer and 20 or 32 for a vertex buffer, bytes
// not 0, a multiple of the stride, at least 8 for an index buffer and at most kModelGeomMaxBuffer, offset = the sum of the
// entries before it, the content hash (VPModelGeomHash) equal to the entry's, entries in ascending (kind, hash) order with
// no duplicate. Cross-checks (CheckBundle): a model_geom member while render_cities is not "1" (BadValue: CPU-built meshes
// are city art); NNNNNN not below frames.bin's frame count (BadRecord).
constexpr char kMagicModelGeom[4] = {'C', '5', 'G', 'B'};   // modelgeom/frame<NNNNNN>.mgeo (format version 9, ticket 4888)
constexpr uint32_t kModelGeomEntryBytes = 32;
constexpr uint32_t kModelGeomMaxEntries = 512;          // VPSHELL_MODELGEOM_MAX_ENTRIES (the section's table)
constexpr uint64_t kModelGeomMaxBuffer = 8ull << 20;    // VPSHELL_MODELGEOM_MAX_BUFFER (c5r CpuMeshStore::kMaxBytes)
struct ModelGeomBuffer
{
	uint32_t kind = 0, stride = 0;
	uint64_t hash = 0;
	std::vector<uint8_t> bytes;
};
struct ModelGeomFile
{
	std::vector<ModelGeomBuffer> buffers;
	uint64_t dataBytes = 0;
};
c5r::Status ParseModelGeom(const std::vector<uint8_t>& b, ModelGeomFile* out, CheckResult* res);
// Encodes `buffers` (sorted here by kind, hash); the result passes ParseModelGeom when every buffer is well-formed.
void EncodeModelGeom(std::vector<ModelGeomBuffer> buffers, std::vector<uint8_t>* out);
std::string ModelGeomPath(uint32_t frameIndex);   // "modelgeom/frame000000.mgeo" (0..999999; "" above)
bool ModelGeomIndex(const std::string& path, uint32_t* frameIndex);

// ---- format version 5 (issue 0440, 2026-11-16): the unit scene's database and the controller's state -------------------
// The unit scene (render_units=1, server/src/units/) draws from the feed (feed.bin's records 3-6), its unit database and
// the state its animation controller reached before the span: the controller starts at the first frame the scene builds
// after its pre-check passed (730-801 frames after the start in 4467's library, offline-scene-harness.md 21), and every
// figure's clip and time at the span's first frame follow from every frame since. That state is carried as its exact input
// history, which the player replays through the scene's own calls before the span (SceneUnits::feed, poll, beginFrame,
// drawScene): the drains are feed.bin's (every drain since the server's start), the frames are units_history.bin's.
// units/gen<the database generation, 10 digits>.db (kind units_db): the SQLite file the unit scene opened (SrvFeed's
// GetUnitDatabase, or render_test_units_db), copied whole when the bundle is finished. Refused: shorter than 100 bytes
// (Truncated), not starting with "SQLite format 3" and a zero byte (BadMagic).
// units_history.bin (kind units_history): header, u32 recordBytes (= sizeof(UnitsHistoryRecord) = 744), u32 dbGeneration
// (the database generation in force at the span's first frame), u32 flags (bit 0: SceneUnits::ready() before the span's
// first frame was drawn), u32 reserved 0, then one record per frame the server drew from the controller's first frame up to
// the span (not the span's frames): the frame before the first frame drawn with ready() true (the controller starts in the
// first beginFrame after its pre-check passed, and ready() turns true once that frame drew), and every frame after it.
// Each record: the frame id, the loop iteration it was drawn in, the clock value it was drawn with (the recorder's, which
// the host passes in SceneFrame::nowMs for every frame while units are recorded), ready() before its draw, the camera
// block. Refused: recordBytes != 744 (WrongSize), a partial record (Truncated), reserved not 0, flags above bit 0, no
// record, a loop iteration that goes back, a nowMs not finite or going back, the first record's ready bit set, or the
// ready state after the first record (the second record's bit, else the header's bit 0) not set (BadRecord).
// Cross-checks (CheckBundle): with render_config render_units "1", a units_db member or a note starting kUnitsNoDbNote,
// and a units_history member or a note starting kUnitsNoHistoryNote (MissingMember); either member while render_units
// is not "1", a second units_db, a units_db whose generation is not the history's dbGeneration (BadValue); the history's
// last loop iteration above frames.bin's first (BadRecord).
#pragma pack(push, 8)
struct UnitsHistoryRecord
{
	int64_t frameId;              // SceneFrame::frameId (the camera block's frameStamp)
	uint64_t loopIter;            // the host's loop iteration of the draw (feed.bin's records up to it come first)
	double nowMs;                 // SceneFrame::nowMs the frame was drawn with (QpcToMs scale)
	uint32_t flags;               // kUnitsHistReady: SceneUnits::ready() before this frame's draw
	uint32_t reserved;            // 0
	VPShellCamera cam;            // SceneFrame::cam
};
#pragma pack(pop)
static_assert(sizeof(UnitsHistoryRecord) == 744, "UnitsHistoryRecord layout");
enum : uint32_t { kUnitsHistReady = 1 };
struct UnitsHistoryFile
{
	uint32_t dbGeneration = 0;
	uint32_t flags = 0;           // kUnitsHistReady: ready() before the span's first frame
	std::vector<UnitsHistoryRecord> records;
};
constexpr const char* kUnitsNoDbNote = "gap: units: no unit database";            // version_notes prefix
constexpr const char* kUnitsNoHistoryNote = "gap: units: no controller state";    // version_notes prefix
// Issue 0440 (MC #3003): the recorder's note when the server drew the FOWLevel-3 particles (0340) in the span and their
// state is not in the bundle (since ticket 0403, format version 6: only when particles.bin could not be written, e.g. the
// particles were not on at the span's first frame but drew later), so the player (which then draws none) cannot equal such
// a span; the checker accepts the bundle (a gap, not "incomplete"), the player says it before it draws.
constexpr const char* kFogParticlesNote = "gap: fog particles";                  // version_notes prefix
c5r::Status ParseUnitsHistory(const std::vector<uint8_t>& b, UnitsHistoryFile* out, CheckResult* res);
void EncodeUnitsHistory(const UnitsHistoryFile& f, std::vector<uint8_t>* out);
c5r::Status ParseUnitsDb(const std::vector<uint8_t>& b, CheckResult* res);
std::string UnitsDbPath(uint32_t generation);     // "units/gen0000000001.db"

// ---- format version 6 (ticket 0403, 2026-11-16): the FOWLevel-3 fog particles (ticket 0340) ------------------------------
// The particles carry history: one simulation step per drawn frame since their spawn (seeded), so a span that starts
// mid-game needs their state at its first frame. particles.bin (kind fog_particles, magic C5PS, form version 1): header,
// u32 flags (bit 0 kFogParticlesSpawned: spawned before the span's first frame), u32 seed (the spawn's seed), f32 worldW,
// f32 worldH (the world they were spawned for; 0 when not spawned), u32 stateBytes, u32 reserved 0, then stateBytes bytes:
// c5r::fow::ParticleSim::saveState() before the span's first draw ("C5FP" version 1: the settings, the flow field's size and
// hash, the grid, the generator, every particle). Not spawned: stateBytes 0 (the player's first particle frame spawns with
// the seed, as the live one did). Refused: short (Truncated), reserved not 0 or flags above bit 0, a world not finite (or
// not > 0 when spawned), spawned with stateBytes 0 or not spawned with stateBytes > 0 (BadRecord), stateBytes not the rest
// of the file (WrongSize); in the state: not "C5FP" (BadMagic), a version other than 1 (BadVersion), fields, cell counts or
// particles past its end (Truncated), bytes after its last cell (WrongSize), a grid outside 1..4096 a side or a cell above
// 32 particles (BadRecord). The flow field's hash is checked by the player (ParticleSim::loadState against the install's).
// Cross-checks (CheckBundle): render_config render_fow_particles, when present, "0" or "1" (BadValue); "1" needs
// particles.bin or a note starting kFogParticlesNote (MissingMember); particles.bin while render_fow_particles is not "1"
// (BadValue).
enum : uint32_t { kFogParticlesSpawned = 1 };
constexpr uint32_t kFogParticlesFixedBytes = 16 + 6 * 4;   // 40: the state starts here
struct FogParticlesFile
{
	uint32_t flags = 0;           // kFogParticlesSpawned
	uint32_t seed = 1;
	float worldW = 0, worldH = 0;
	std::vector<uint8_t> state;   // ParticleSim::saveState() (empty when not spawned)
	uint32_t particles = 0;       // parsed: the particles in the state
	int32_t cellsX = 0, cellsY = 0;
};
c5r::Status ParseFogParticles(const std::vector<uint8_t>& b, FogParticlesFile* out, CheckResult* res);
void EncodeFogParticles(const FogParticlesFile& f, std::vector<uint8_t>* out);

// ---- format version 8 (ticket 0495, 2026-11-17): the trees (epic 0431) and the near-camera pairs (issue 0371) ----------
// forest.bin (kind forest_state, magic C5TR, form version 1): what the trees hold at the span's first frame that the feed
// does not give again (srvrender::ForestSpanState, srv_forest_scene.h). Header, then u32 flags (bit 0 kForestHaveCut, bit 1
// kForestIdsResolved), u32 sessionSeed, u32 layoutSeed, u32 loads, u32 loadSeq, i32 forestId, i32 jungleId, u32
// streamState, u32 tableGen, u32 tableW, u32 tableH, u32 records, u32 built, u32 marks, u32 cutW, u32 cutH, f32 cutOx, f32
// cutOy, u64 cutGeneration, u32 reserved 0 (100 bytes), then the records (i32 x, i32 y, u32 kind, u32 layout each), the
// built plots (i32 x, i32 y each, the Tree_Shadow decals' order first), the marks (i32 x, i32 y each: tile coordinates,
// as ForestScene::marks()), and cutW x cutH bytes of the cut mask (row-major) when bit 0 is set. Refused: short (Truncated), reserved not
// 0 or flags above bit 1 (BadRecord), sessionSeed 0, a record's kind not 1..4 or layout not below 20, records not
// ascending by (y, x), a built plot that is no record or listed twice, marks negative or not ascending, cutW /
// cutH 0 or above 16384 with the cut (or not 0 without it), cutOx / cutOy not finite (BadRecord), bytes after the end
// (WrongSize).
// near.bin (kind near_pairs, magic C5NP, form version 1): header, u32 flags (bit 0 kNearHasImages), u32 tilesX, u32 tilesY,
// u32 stateBytes, then stateBytes bytes: c5r::bake::NearCaches::saveState() before the span's first draw ("C5NC" version
// 1); with bit 0, then u32 images and per image u32 tile, u32 size, u32 second (0 the t0 texture, 1 the t8 one), u32 bytes
// and the texels: a held pair whose content is behind its tile (NearCaches::contentBehind: a rebake asked for and not
// taken, its pool full), read back whole (RGBA8, mip 0 first, tight rows, NearImageBytes(size)): the replay cannot bake
// what its older inputs gave, so it uploads these. Refused: short (Truncated), flags above bit 0, tilesX or tilesY 0
// (BadRecord), stateBytes not the rest without bit 0 or past the end with it (WrongSize), the state's magic not C5NC
// (BadMagic) or version not 1 (BadVersion), its tile count not tilesX x tilesY (BadRecord); bit 0 with no image, an
// image's tile out of range, size not 64/128/256/512, second above 1, bytes not NearImageBytes(size), images not
// ascending by (tile, second) (BadRecord), bytes after the last image (WrongSize). The rest of the state is checked by the
// player (NearCaches::loadState, Residency::restoreNear).
// Cross-checks (CheckBundle): render_config render_forest and render_bake_near, when present, "0" or "1" (BadValue);
// render_forest "1" needs forest.bin or a note starting kForestNote, render_bake_near "1" with render_tiles "bake" needs
// near.bin or a note starting kNearNote (MissingMember); forest.bin while render_forest is not "1", near.bin while
// render_bake_near is not "1" (BadValue).
constexpr const char* kForestNote = "gap: forest";       // version_notes prefix
constexpr const char* kNearNote = "gap: near pairs";     // version_notes prefix
// Ticket 4852 (epic 0528): the farm producer's input is render_config, not a member (format version 8 unchanged):
// render_farmfields "0" / "1" and render_farmfields_state (c5r::farmfields::spanText, "fv1 ..."); CheckBundle refuses a
// value other than 0 / 1 and a state without "1" or of another form (BadValue), and "1" with neither the state nor a note
// starting kFarmNote (MissingMember).
constexpr const char* kFarmNote = "gap: farm fields";    // version_notes prefix
constexpr const char* kFarmStateForm = "fv1 ";
// Ticket 4934 (epic 0454, FixRoadLayout): the road producer's input, the same way: render_roadlayout "0" / "1" (written
// only when 1) and render_roadlayout_state (srvextras::RoadSpanText, "rl1 ..."); the same refusals with kRoadNote.
constexpr const char* kRoadNote = "gap: road layout";    // version_notes prefix
constexpr const char* kRoadStateForm = "rl1 ";
enum : uint32_t { kForestHaveCut = 1, kForestIdsResolved = 2 };
enum : uint32_t { kNearHasImages = 1 };
uint64_t NearImageBytes(uint32_t size);   // RGBA8, the whole chain of a size x size texture (0 for another size)
constexpr uint32_t kForestStateFixedBytes = 100;
struct ForestStateRecord
{
	int32_t x = 0, y = 0;
	uint32_t kind = 0, layout = 0;
};
struct ForestStateFile
{
	uint32_t flags = 0;
	uint32_t sessionSeed = 0, layoutSeed = 0, loads = 0, loadSeq = 0;
	int32_t forestId = -1, jungleId = -1;
	uint32_t streamState = 0, tableGen = 0, tableW = 0, tableH = 0;
	std::vector<ForestStateRecord> records;
	std::vector<std::pair<int32_t, int32_t>> built, marks;
	uint32_t cutW = 0, cutH = 0;
	float cutOx = 0, cutOy = 0;
	uint64_t cutGeneration = 0;
	std::vector<uint8_t> cutCells;
};
c5r::Status ParseForestState(const std::vector<uint8_t>& b, ForestStateFile* out, CheckResult* res);
void EncodeForestState(const ForestStateFile& f, std::vector<uint8_t>* out);
struct NearPairImage
{
	uint32_t tile = 0, size = 0, second = 0;
	std::vector<uint8_t> texels;
};
struct NearPairsFile
{
	uint32_t tilesX = 0, tilesY = 0;
	std::vector<uint8_t> state;          // NearCaches::saveState()
	std::vector<NearPairImage> images;   // kNearHasImages: the pairs whose content was behind their tile
};
c5r::Status ParseNearPairs(const std::vector<uint8_t>& b, NearPairsFile* out, CheckResult* res);
void EncodeNearPairs(const NearPairsFile& f, std::vector<uint8_t>* out);

// 2.8 dumps/<frameId>.bmp: 32-bit top-down BMP as V3Sink::dump writes it (BITMAPFILEHEADER, BITMAPINFOHEADER with a
// negative height, BI_RGB, then w*h*4 bytes); dumps/<frameId>.depth.f32: w*h float32, no header.
c5r::Status ParseDumpColour(const std::vector<uint8_t>& b, uint32_t* w, uint32_t* h, CheckResult* res);
void EncodeDumpColour(uint32_t w, uint32_t h, const void* bgra, std::vector<uint8_t>* out);   // tight rows

// ---- writing (the recorder, 3904) ------------------------------------------------------------------------------------
// Members are written into <dir>/.partial/<path>, hashed while written. finish() moves them into place and writes
// scene.json last (through scene.json.tmp). A failed write (and the failAfterBytes seam) makes the writer incomplete:
// finish() then moves nothing, writes scene.json with an "incomplete: <why>" note and no members (the checker refuses
// it: Refusal::Incomplete) and returns a failed Status. markIncomplete does the same on the recorder's word.
class Writer
{
public:
	Writer();
	~Writer();
	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;
	c5r::Status open(const std::wstring& bundleDir);            // the folder must be absent or empty; nothing else made
	c5r::Status addMember(const std::string& path, MemberKind kind, const void* data, size_t bytes);
	// A stream member (feed, frames, camera_blocks): appendStart writes its head (EncodeStreamHead), append adds records.
	c5r::Status appendStart(const std::string& path, MemberKind kind);
	c5r::Status append(const std::string& path, const void* data, size_t bytes);
	c5r::Status finish(Manifest& m);                            // fills m.members, writes scene.json
	void markIncomplete(const std::string& why);
	bool incomplete() const;
	// Test seam (contract 10, 3904's disk-full stand-in): every member write past this many bytes in total fails with
	// Io and makes the writer incomplete (0: off). scene.json is not counted.
	void failAfterBytes(uint64_t n);
private:
	struct Impl;
	Impl* m_impl;
};

}  // namespace scenebundle
}  // namespace srvrender
