// srv_units_core.h - the CPU part of the server's unit scene (epic 0375, issue 4454): the tables, the allocator's feed,
// the fades, the cull, the colour / art style / era lookups and the UnitScene of a frame. Written 2026-11-15 for this
// repository (GPL-3.0-or-later). Design: research/05-graphics-composition/unit-server-scene.md sections 3, 5, 9, 10.
//
// No device here: srv_units_scene.cpp wraps it with the pre-check, the drawing renderer and the dump (SceneUnits), and the
// tests (server/tests/units/) drive it with synthetic batches. Every call on one thread (the render thread).
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "srv_units_scene.h"
#include "srv_units_seed.h"
#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/units.h"

namespace srveffects { class TriggerStream; }   // srv_trigger_stream.h (ticket 5062, W107: the probe's triggerStream)

namespace srvunits {

// The scene's named statuses (note section 10; the allocator's four are counted here as the allocator reports them).
// Statuses after kFrameFailed were added by 4454 (2026-11-15, note 10): every input the scene cannot map is named.
enum class Status : uint8_t
{
	kPrecheckFailed = 0,   // a pre-check entry not Ok, the module's open failing, the database unreadable: D125's route
	kUnitRefused,          // prepare gave a unit a status other than Ok: that unit not drawn this frame
	kColourUnknown,        // a player's colour index has no PlayerColors row: that player's units not drawn
	kFeedOutOfOrder,       // a unit event's seq not above the last taken, a path index outside the batch, a table record
	                       // newer than the batch (the feed's CopyUnits contract): the event skipped
	kUnknownRemoval,       // (allocator) kind 14 for a key not held outside a load
	kKnownCreation,        // (allocator) kind 11 for a held key outside an era change
	kCombatHoldAmbiguous,  // (allocator) decision 14
	kAllocatorOutOfStep,   // (allocator) a dropped event or an event the rules cannot place
	kPlayerUnknown,        // a unit whose owner has no player record: not drawn
	kUnitTypeUnknown,      // a unit type index with no Units row: not drawn
	kPlayerArtUnknown,     // a player's civilization / art style / era index with no row: that player's units not drawn
	kSeedMissing,          // a listed unit the allocator holds no seed for: not drawn (never a seed of ours)
	kPlotUnknown,          // a unit plot outside the feed's grid (or no grid yet): not drawn
	kFrameFailed,          // prepare or draw returned a frame-level error: no units this frame, readiness cleared
	kReflectionMatrixMissing,   // (4455) render_units_passes=1 but no ShadowMatrix1 for the frame: no reflection pass
	kAnimFailed,           // (4456) the controller gave a unit a status other than Ok (unit-controller.md 5): no program, so
	                       // the module refuses its figures and it is not drawn; counted once per episode of the unit
	kAnimUnavailable,      // (4456) the controller cannot open on the database (string databases, bridge tables, Builds) or
	                       // refused a frame: D125's route at start (as PrecheckFailed), never figures of our choosing
	kCount
};
const char* statusName(Status s);
// The allocator's statuses as the scene counts them (kOk -> kCount: not a status).
Status statusOfSeed(SeedStatus s);

typedef std::function<void(const char* line)> LogFn;
class AnimFeed;   // srv_units_anim.h (issue 4456)

// The feed as the scene reads it (SrvFeed in server.exe, a fake in the tests).
struct FeedPort
{
	virtual ~FeedPort() {}
	virtual void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& unitTableGen) = 0;
	virtual void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) = 0;
	virtual bool unitDatabase(std::wstring& path, uint32_t& gen) = 0;
};
FeedPort& LiveFeedPort();   // SrvFeed::CopyUnits / CopyPlayers / GetUnitDatabase

// The database's type index -> Type maps (the DLL's type index = the table's ID column; Units, PlayerColors,
// Civilizations, Eras, ArtStyleTypes). load(): NotFound naming the table when one cannot be read.
struct Lookups
{
	std::map<int32_t, std::string> units, colours, civs, eras, artStyles;
	std::map<int32_t, std::string> minorCivs;   // issue 4460 (A1): MinorCivilizations (ID, Type), a city-state's art key
	// ---- issue 4469: the embarked art (unit-server-scene.md 3.2, the embarked row; unit-controller.md 17.3) ----
	// Civilizations ID -> the embarked graphic override the DLL gives a player of that civilization (CvPlayer::init: a
	// leader trait with EmbarkedAllWater -> ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE, else EmbarkedToLandFlatCost ->
	// ART_DEF_UNIT_U_DANISH_LONGBOAT; through Civilization_Leaders and Leader_Traits).
	std::map<int32_t, std::string> embarkOverride;
	int32_t lastEmbarkArtEra = -1;   // Defines LAST_EMBARK_ART_ERA (VP: 4); -1 = not read
	int32_t allWaterEra = -1;        // the lowest Eras ID of a technology with EmbarkedAllWaterPassage (VP: 3); -1 = none
	std::string embarkWhy;           // non-empty: the embarked art tables could not be read (then no embarked unit is drawn)
	// Issue 0439: ArtDefine_UnitInfos Type by srv_feed.h EmbarkArtHash24 (the feed's override hash, kind 21 a[3]); a
	// hash two Types share maps to "" (ambiguous: refused, never guessed). Empty when the table could not be read.
	std::map<uint32_t, std::string> artByHash24;
	std::string artHashWhy;          // non-empty: ArtDefine_UnitInfos could not be read (an override from the feed is refused)
	// Issue 4986 (hunk H1): Units ID -> AirInterceptRange (a NULL cell: 0), for the unit extras through UnitExtrasFrame;
	// read by load, optional there (a failure leaves airInterceptWhy set and the map empty).
	std::map<int32_t, int32_t> airInterceptRange;
	std::string airInterceptWhy;
	// Issue 4986 (MC #11389): UnitPromotions.Type of every promotion with AirInterceptRangeChange != 0 (none in VP; a mod's),
	// which the range above does not include; airInterceptPromotionsWhy: the column could not be read (not checked).
	std::vector<std::string> airInterceptPromotions;
	std::string airInterceptPromotionsWhy;
	bool load(c5m::TableSource& db, std::string& why);
	// Issue 4469: the embarked art tables (called by load; optional there: a failure leaves embarkWhy set, load succeeds).
	bool loadEmbark(c5m::TableSource& db, std::string& why);
	bool empty() const { return units.empty(); }
};

// Issue 4469: the art row the EXE draws for an embarked unit (unit-server-scene.md 3.2, the embarked row [C, the EXE's
// GameplayUnitEmbark handler read 2026-11-16; L at the water camera]): the owner's embarked graphic override when it has
// one (in every era); else ART_DEF_UNIT_TRANSPORT from the team's era LAST_EMBARK_ART_ERA on; else
// ART_DEF_UNIT_GALLEON when the team can embark across all water (a technology with EmbarkedAllWaterPassage); else
// ART_DEF_UNIT_GALLEY. The feed does not carry the team's all-water flag, so an era from allWaterEra up to
// LAST_EMBARK_ART_ERA is refused (false, `why` names it), never guessed; below allWaterEra no team holds such a technology
// (researching one moves the team's era to the technology's). `civilization` < 0 = no override (a city-state, the
// barbarians). False with `why` when the tables are missing.
bool EmbarkedRow(const Lookups& l, int32_t civilization, int32_t era, std::string& row, std::string& why);
// Issue 0439: the same with the inputs the feed read at the unit's embark (kind 21 a[3], srv_feed.h EmbarkArtBits). With
// kEmbarkArtRead set the EXE's rule runs on them in full: the override when it names an ArtDefine_UnitInfos Type (by
// its hash; a hash no Type has is an override that names no art define, which the EXE ignores; a hash two Types share
// is refused); else TRANSPORT from LAST_EMBARK_ART_ERA on; else GALLEON with the all-water passage, GALLEY without. Bits
// without kEmbarkArtRead: EmbarkedRow above (the database's override, the Renaissance gap refused).
bool EmbarkedRow(const Lookups& l, int32_t civilization, int32_t era, uint32_t bits, std::string& row, std::string& why);

// What one player gives its units (resolved from its PlayerRecord through the Lookups).
struct PlayerArt
{
	bool have = false;           // a player record exists
	bool colourOk = false, artOk = false;
	c5r::units::PlayerInput in;  // colour, art_style, era (valid where the flags say)
	std::string why;             // the first unknown index (for the LOUD line)
};

// One unit's visibility fade (note 3.2): from its last kUnitVisibility.
struct Fade
{
	bool have = false;           // a kUnitVisibility seen since the unit's creation
	bool visible = false;
	double t0Ms = 0;             // the drain time of that event (the frame clock)
	float blendMs = 0;           // blendTime (seconds in the event) x 1000
};
// The listed state and palette row 1 of a unit at time tMs: listed false = not drawn. No event: listed = flagVisible,
// (1, 0, 0). Visible, blend > 0: (0, 1, p), p = clamp((t - t0) / blend, 0, 1), staying (0, 1, 1) after (stock's row 1 of a
// unit that came into view, unit-renderer.md 10.3 [L]); visible, blend 0: (1, 0, 0) at once [I]. Hidden, blend > 0:
// (1, 0, p) while p < 1, then not listed; hidden, blend 0: not listed at once.
bool fadeState(const Fade& f, bool flagVisible, double tMs, float& from, float& to, float& p, bool& running);

// The camera of the cull and of the pass: vp = view x proj (row vectors), wrap = the wrap period (0: none).
struct CullCamera
{
	float vp[16] = {};
	float wrap = 0;
	bool valid = false;
};
// Issue 4457 / 4456: the cull camera of a frame (vp = view[0] x proj; valid = every element finite), as UnitsHost::beginFrame
// forms it; unitfeed calls the same function.
CullCamera CullOf(const UnitsCamera& cam);
// The box [x0, x1] x [y0, y1] x [z0, z1] is not entirely outside one clip plane of vp (conservative).
bool boxInView(const float vp[16], float x0, float y0, float z0, float x1, float y1, float z1);
// The cull's box per plot: the plot centre +- kCullRadius in x and y, z from 0 to kCullTop (the terrain's highest
// sample is 255 x sz = 51 world units at sz 0.2; a unit's figures stand within a plot and below 60 units over it) [I].
const float kCullRadius = 64.0f;
const float kCullTop = 160.0f;
// The hold table of decision 14 (a): issue 4462 (2026-11-16), the table that covers both paths' measurements
// (unit-formations.md 12.7.7 (1): 7 melee samples 3.76-4.94 s, 7 ranged 3.61-4.18 s, from the dying unit seen; stock
// path 12.7.6 (5) by 4453, product path by 4458). Was 4460's stock-path table, melee 4.6 s, ranged 3.7 s, band 0.4 s.
const double kHoldMeleeSec = 4.3, kHoldMeleeBandSec = 0.6;
const double kHoldRangedSec = 3.9, kHoldRangedBandSec = 0.35;

// Issue 4981: a unit extra's turn and scale of one listed unit, applied to its prepared plan (ApplyUnitXforms).
struct UnitXform
{
	bool heading = false;
	float yaw = 0;                // radians about +z (the placement's yaw)
	float scale = 1;
	float lift = 0;               // issue 4986 (H2): world units added to z after the turn and the scale
};

// One frame's lists: copies[k + 1] = the units drawn in wrap copy k (k = -1, 0, +1; a unit shows in copy k at x + k W).
struct BuiltFrame
{
	c5r::units::UnitScene copies[3];
	size_t listed = 0, culled = 0, notDrawn = 0;
	bool fading = false;          // a fade is running in this frame
	std::map<int64_t, UnitXform> xforms;   // issue 4981: by UnitKeyOf; empty without a unit extra
};

// Issue 4981: each prepared unit of `plan` with an entry in `xforms` turned to its yaw and scaled about its pivot (the
// placement's root, z the mean of its figures' z): every bone row triple of its palette blocks and the world rows of its
// material-87 draws, w -> P + s Rz(yaw - placement yaw) (w - P). Returns the units changed.
size_t ApplyUnitXforms(c5r::units::FramePlan& plan, const std::map<int64_t, UnitXform>& xforms);

// ---- epic 0040, issue 4755: the root position source exe (unit-server-scene.md 17) --------------------------------------
// In mode exe (server.cfg render_units_positions=exe, UnitsOptions::positionsExe) a unit's root is its newest kind-6 position
// record (SrvFeed::kUiMirror with flags 6: plot = the player, turn 0 a position / 1 an arrival, a[0] the unit id, a[1..3] x,
// y, z as float bits; contract 3.3, proxy-design.md 24.6); an arrival snaps it to its plot centre; a unit with no record
// stands at its plot, except a walking unit before its first position, which stands at the plot it left. A kind 12 opens a
// walk (the controller's `move`), its arrival record closes it (`arrive`, never in the move's frame). F9's rule: a walk with
// no position for kWalkQuietFrames drawn EXE frames (kWalkStartQuietFrames before its first one) is ended (`arrive`, the
// unit at its plot) and counted; a teleport or a rebase (kinds 13 / 30: the controller's `place`) ends it too. Frames are
// the composite's frame ids (the EXE's Present count), so the server and unitfeed count them alike.
const int64_t kWalkQuietFrames = 30;
const int64_t kWalkStartQuietFrames = 120;
// SrvFeed::kUiMirror's record kinds this scene takes (proxy_ui_bridge.h UiBridge::Kind 6 / 7).
const uint16_t kUiKindUnitPosition = 6, kUiKindFrameMark = 7;

struct PosCounters
{
	uint64_t positions = 0;       // kind-6 positions applied (mode exe)
	uint64_t arrivals = 0;        // kind-6 arrivals applied (mode exe)
	uint64_t inWalk = 0;          // of the positions, those of a unit with a walk open
	uint64_t noWalk = 0;          // of the positions, those of a unit with no walk open (teleports, quick moves)
	uint64_t beforeMove = 0;      // F7: a position whose seq is below its unit's last kind 12: counted, not applied (LOUD)
	uint64_t unknownUnit = 0;     // a kind-6 record of a unit the table does not hold: counted, not applied
	uint64_t bad = 0;             // a kind-6 record with a player outside 0..63 or a non-finite x, y or z: refused (LOUD)
	uint64_t outOfOrder = 0;      // a kind-6 / 7 record whose seq is not above the last one taken: skipped (LOUD)
	uint64_t ignoredOwn = 0;      // kind-6 records while the position source is own (not applied)
	uint64_t frameMarks = 0;      // kind-7 records seen
	uint64_t walks = 0;           // kind 12 that opened a walk (a kind 12 of a walking unit continues it: moves)
	uint64_t moves = 0;           // every kind 12 taken in mode exe
	uint64_t arrived = 0;         // walks ended by their arrival record (the controller's arrive)
	uint64_t arrivalsIdle = 0;    // arrival records of a unit with no walk open: a snap, no arrive
	uint64_t quietEnds = 0;       // walks ended by F9's rule (no position for kWalkQuietFrames / kWalkStartQuietFrames)
	uint64_t placeEnds = 0;       // walks ended by a teleport or a rebase (kinds 13 / 30)
	uint64_t removeEnds = 0;      // walks ended by the unit's removal (kind 14)
	uint64_t loadEnds = 0;        // walks ended by a load (kind 32)
	uint64_t holdFrames = 0;      // unit-frames drawn at the plot a walking unit left, before its first position
	uint64_t loadsCleared = 0;    // loads that cleared the positions
	int64_t firstPosFramesMax = -1;   // the most drawn frames between a kind 12 and the walk's first position
	uint64_t firstPosOver2 = 0;   // walks whose first position came more than 2 drawn frames after the kind 12 (F7)
	uint64_t lastExeFrame = 0;    // the last kind-7 record's EXE frame (the bridge's drain number)
};

// Issue 4755: one move of a unit as the ledger keeps it (unitfeed prints it; F7 and F9 are read from it). kind 12 a walk,
// 13 a teleport, 30 a rebase. Frames are the last drawn frame id before the event was taken (-1 before the first frame).
struct WalkRecord
{
	int32_t owner = -1, id = -1;
	uint16_t kind = 0;
	uint32_t seq = 0;             // the kind 12 / 13 / 30
	int64_t frame = -1;
	uint32_t firstPosSeq = 0;     // the first kind-6 position after it (0: none)
	int64_t firstPosFrame = -1;
	uint32_t positions = 0;       // positions of the unit while this record was open
	uint32_t lastPosBeforeSeq = 0;    // the unit's newest position taken before the move (0: none since its creation / the load)
	int64_t lastPosBeforeFrame = -1;
	uint32_t moves = 1;           // kind 12s in this walk
	const char* end = "open";     // "arrival", "quiet", "place", "removed", "load", "next" (a teleport's record ends at the
	                              // unit's next move or teleport), "created", "open"
	uint32_t endSeq = 0;
	int64_t endFrame = -1;
};

// Ticket 0502 (unit-server-scene.md 18.3 [L]): a load's setup, the DLL's SetupGraphical calls that follow kLoadBegin
// (per unit Created or Embark(true), ShouldDimFlag, Fortify, SetDamage, Work, Visibility(false) + Garrison(true); player
// by player, 0 to 63). The EXE allocates every creation of the setup in feed order, however many drains the setup spans
// (two loads of the huge t350 save: 853 of 853 in feed order), so the allocator's frame grouping (and rule (e) with it)
// is off from kLoadBegin until the setup ends. It ends at the first unit event that is not a setup call (a move, a
// teleport, a removal, a combat, an era change, ...), at an event of a player below the setup's highest player so far,
// or at an event more than kLoadSetupGapSec after the setup's last one.
struct LoadSetup
{
	bool active = false;
	int32_t maxOwner = -1;        // the highest owner of a setup event so far
	double lastSec = 0;           // the drain time of the setup's last event
	uint64_t events = 0;          // setup events taken since kLoadBegin
};
const double kLoadSetupGapSec = 5.0;
// True when the unit event e (a kind IsUnitKind takes, or kind 28) at drain time tSec still belongs to an active setup
// (s is updated); false ends it (s.active false). kLoadBegin starts a setup (true).
bool LoadSetupGoesOn(LoadSetup& s, const SrvFeed::Event& e, double tSec);

struct CoreCounters
{
	uint64_t batches = 0, unitEvents = 0, tableCopies = 0, frames = 0, listed = 0, culled = 0, drawnUnits = 0;
	uint64_t combatNotes = 0;        // issue 4460: kinds 25 / 28 handed to the allocator (SeedAllocator::NoteCombat)
	uint64_t combatGameUnread = 0;   // of them, combats whose game flags were not read (kCombatGameRead clear): no hold
	uint64_t loadSetups = 0;         // ticket 0502: setups begun (kLoadBegin)
	uint64_t loadSetupEvents = 0;    // unit events applied inside a setup (frame grouping off)
	uint64_t byStatus[(int)Status::kCount] = {};
	// issue 4981: what the unit extras changed, in unit-frames (before the cull and the fog filter)
	uint64_t xShown = 0;             // hidden by stock, listed for an extra
	uint64_t xShowRefused = 0;       // a show refused (a dead unit, an invisible unit of another team)
	uint64_t xRoot = 0, xHeading = 0, xScaled = 0;
	uint64_t xFoot = 0;              // issue 4982: a foot radius given (FixUnitPlacement)
	uint64_t xBad = 0;               // an override the module let through outside its ranges: ignored for that unit
	uint64_t xHeld = 0;              // an override of a unit held in a fight: ignored (epic 0042's contract)
};

class UnitsCore
{
public:
	UnitsCore(FeedPort& port, LogFn log);
	~UnitsCore();

	// The database's lookups and the art resolver of the era-change test (null: era changes cannot be tested: LOUD).
	void setDatabase(const Lookups& l, const c5r::units::ArtResolver* art);
	bool haveDatabase() const { return !m_look.empty(); }

	// One drained batch at the frame clock tMs: tables, allocator (Apply per event in seq order), fades, checks.
	// True when the unit list may have changed.
	bool onBatch(const SrvFeed::Batch& b, double tMs);
	// The frame's lists at tMs (Advance on the allocator first). cam null or invalid: no cull, everything in copy 0.
	// frameId (issue 4755): the drawn frame's id (the composite's frame id), the clock of F9's rule in mode exe; < 0: the
	// last one + 1.
	void build(const CullCamera* cam, const c5r::heightfield::HeightGrid* grid, double tMs, BuiltFrame& out, int64_t frameId = -1);
	// Issue 4755: a drawn frame's id before its build (the host calls it for every frame, also one it does not build, so
	// that its frame stamps are unitfeed's, which builds every frame); the next build then takes it instead of frameId.
	void noteFrame(int64_t frameId);
	// Issue 4755: the root position source (false own, the default; true exe). Set before the first batch.
	void setPositionsExe(bool on);
	bool positionsExe() const { return m_posExe; }
	const PosCounters& posCounters() const { return m_pc; }
	size_t walksOpen() const;
	// The unit's root in mode exe: true with x, y, z when its newest record is a position (not snapped by an arrival).
	bool rootOf(int32_t owner, int32_t id, float& x, float& y, float& z) const;
	bool walking(int32_t owner, int32_t id) const;
	// The move ledger (off by default; unitfeed turns it on): every walk / teleport / rebase taken in mode exe, in order.
	void setLedger(bool on) { m_ledgerOn = on; }
	const std::vector<WalkRecord>& ledger() const { return m_ledger; }
	// The module's per-unit statuses of a prepared copy (UnitRefused LOUD once per (unit type, code), warnings once).
	void afterPrepare(const c5r::units::UnitScene& sc, const c5r::units::FramePlan& plan);
	// A frame-level failure of prepare / draw (kFrameFailed); clears tableReady-independent readiness in the host.
	void frameFailed(const std::string& why);

	void note(Status s, const std::string& onceKey, const char* fmt, ...);   // count + LOUD (once per key; "" = always,
	                                                                          // the first 20 then every 100th)
	const CoreCounters& counters() const { return m_c; }
	SeedCounters seedCounters() const { return m_alloc ? m_alloc->Counters() : SeedCounters(); }
	bool tableTaken() const { return m_tableTaken; }
	uint32_t lastSeq() const { return m_lastSeq; }
	size_t tableSize() const { return m_units.size(); }
	const std::vector<SrvFeed::UnitRecord>& table() const { return m_units; }   // issue 4460 (the table file)
	uint32_t tableGen() const { return m_tableGen; }
	const std::vector<SrvFeed::PlayerRecord>& players() const { return m_players; }
	PlayerArt playerArt(int32_t player) const;
	// issue 4460: a combat record as the allocator's hold model takes it (counts unread game flags)
	CombatNote combatNoteOf(const SrvFeed::CombatRecord& r);
	uint32_t width() const { return m_width; }
	bool seedOf(int8_t owner, int32_t id, uint32_t& seed) const;
	// issue 4456: the controller's input. Every unit event onBatch takes (after the allocator) is handed to `a` (null: none).
	// Issue 4755: also given the position source (AnimFeed::setPositionsExe) and, in mode exe, the arrivals (AnimFeed::arrive).
	void setAnimFeed(AnimFeed* a);
	// issue 4981: the unit extras (not owned; null: none, the build as before). onBatch hands them each batch after taking
	// it, build calls their beginFrame and unit() per table unit.
	void setExtras(UnitsExtras* x) { m_extras = x; }
	UnitsExtras* extras() const { return m_extras; }
	// Issue 4981 for epic 0042 (4774, its visual queue): a unit held by a fight. The seam hands the flag to the extras and
	// ignores every override of such a unit (an extra never moves a fighter). Nothing sets it yet.
	void setHeldInFight(int32_t owner, int32_t id, bool on);
	bool heldInFight(int32_t owner, int32_t id) const;
	const Lookups& lookups() const { return m_look; }
	// The table's record of (owner, id) (false: not held).
	bool record(int32_t owner, int32_t id, SrvFeed::UnitRecord& out) const;
	// A record as the module's UnitInput, the fields build() gives it (type, plot, facing, seed (0 while the allocator holds
	// none), damage, max HP, flags; fade (1, 0, 0)). False when its owner, owner's art, unit type or plot is unknown.
	bool inputOf(const SrvFeed::UnitRecord& u, c5r::units::UnitInput& out) const;
	// Issue 4469: the embarked row of an embarked unit's owner (EmbarkedRow with its player record; with an art resolver
	// the row must exist). False with `why`: no record, the tables, the era window the feed cannot decide, no such row.
	bool embarkedRowOf(const SrvFeed::UnitRecord& u, std::string& row, std::string& why) const;

private:
	void takeTable(const char* why);
	bool artChanges(const SrvFeed::UnitRecord& u, int oldEra, int newEra);
	// issue 4755: the positions (mode exe)
	struct PosState
	{
		bool have = false;            // the newest record is a position (an arrival or a snap clears it)
		float x = 0, y = 0, z = 0;
		uint32_t seq = 0;             // the newest position's seq
		int64_t posFrame = -1;        // F9's clock: the first frame noted after the newest position (or the walk's kind 12)
		bool fresh = false;           // a position (or the kind 12) came since the last noted frame: noteFrame sets posFrame
		int64_t moveFrame = -1;       // the frame stamp of the walk's kind 12 (the last noted frame before it)
		bool walking = false;
		bool walkHasPos = false;      // a position came since the walk's kind 12
		uint32_t moveSeq = 0;         // the unit's last kind 12
		int32_t fromPlot = -1;        // the plot the unit stood on before the walk's first kind 12 (the table before it)
		int ledger = -1;              // the open ledger record (index), -1 none
	};
	void posEvent(const SrvFeed::Event& e, const std::map<int64_t, int32_t>& oldPlots);
	void posRecord(const SrvFeed::Event& e);
	void endWalk(int64_t key, PosState& s, const char* why, uint32_t seq, bool giveArrive);
	void closeLedger(PosState& s, const char* end, uint32_t seq);
	void openLedger(int64_t key, PosState& s, uint16_t kind, uint32_t seq);
	void posLine(bool loud, const char* fmt, ...);
	bool m_posExe = false;
	std::map<int64_t, PosState> m_pos;
	PosCounters m_pc;
	uint32_t m_lastUiSeq = 0;
	int64_t m_frameId = -1;           // the last build's frame id (the stamp of what onBatch takes after it)
	bool m_haveFrame = false;
	bool m_frameNoted = false;        // noteFrame ran since the last build
	void buildList(const CullCamera* cam, const c5r::heightfield::HeightGrid* grid, double tMs, BuiltFrame& out);
	bool m_ledgerOn = false;
	std::vector<WalkRecord> m_ledger;
	uint64_t m_posLines = 0;
	FeedPort& m_port;
	LogFn m_log;
	AnimFeed* m_anim = nullptr;
	UnitsExtras* m_extras = nullptr;   // issue 4981
	std::set<int64_t> m_fightHeld;     // issue 4981 (set by epic 0042)
	// The scene's own plot and root of a unit (issue 4755's rule: mode exe, the newest position, or the plot a walking unit
	// left before its first position; px, py -1 for a plot outside the grid). True: held at the plot it left.
	bool sceneRoot(const SrvFeed::UnitRecord& u, int64_t key, int32_t& px, int32_t& py, bool& hasRoot, float& rx, float& ry,
	               float& rz) const;
	// the extras' override of one unit (show kept only for a unit stock hides and may be shown); false: an override
	// outside its ranges (counted xBad; the caller drops it)
	bool extraOverride(const SrvFeed::UnitRecord& u, int64_t key, bool stockListed, UnitOverride& ov);
	std::unique_ptr<SeedAllocator> m_alloc;
	Lookups m_look;
	const c5r::units::ArtResolver* m_art = nullptr;
	std::vector<SrvFeed::UnitRecord> m_units;      // sorted by (owner, id)
	std::vector<SrvFeed::PlayerRecord> m_players;  // by player index (player -1 = no record)
	std::map<int64_t, Fade> m_fades;               // key = owner << 32 | id
	std::map<int64_t, uint32_t> m_embarkArt;        // issue 0439: kind 21 a[3] of the unit's last embark (key as m_fades)
	uint32_t m_tableGen = 0, m_lastSeq = 0, m_width = 0, m_height = 0;
	bool m_tableTaken = false, m_haveGen = false;
	LoadSetup m_loadSetup;                         // ticket 0502
	CoreCounters m_c;
	std::set<std::string> m_once;
	uint64_t m_statusLogged[(int)Status::kCount] = {};
};

// ---- the environment of the module (install, mods, database copy, EXE) and the host with its pre-check ----------------
struct Env
{
	c5r::gamefs::GameFiles gf;
	std::unique_ptr<c5m::TableSource> db;
	std::wstring exe, install;
};
// Opens an environment over the unit database `dbPath` (called on the render thread and on the pre-check's worker: each
// call makes its own objects). False: why names the input.
typedef std::function<bool(const std::wstring& dbPath, Env& out, std::string& why)> EnvOpener;
// server.exe's: GameFiles of o.install with the MODS folders (or o.mods) as extra loose roots, SqliteTables of dbPath.
EnvOpener LiveEnvOpener(const UnitsOptions& o);
typedef std::function<double()> ClockFn;   // the frame clock in ms (QpcToMs(Qpc()) in server.exe)

class AnimRunner;   // srv_units_anim.h (ticket 5062: the probe's animRunner)
// What the tests see of the host behind SceneUnits (dynamic_cast from MakeSceneUnitsWith's result).
class UnitsHostProbe
{
public:
	virtual ~UnitsHostProbe() {}
	virtual UnitsCore& core() = 0;
	virtual const BuiltFrame& lastBuilt() const = 0;
	virtual bool precheckRunning() const = 0;
	virtual bool precheckPassed() const = 0;
	virtual void waitPrecheck() = 0;              // joins a running pre-check and evaluates it (as poll() does)
	virtual size_t precheckEntries() const = 0;
	virtual uint64_t passDraws() const = 0;       // module draw() calls that returned Ok
	virtual uint64_t dumpsWritten() const = 0;    // queued for the TEST files' worker (ticket 5054)
	// Ticket 5054: waits (at most 10 s) until the TEST files' worker has written every file queued so far (the dump, the
	// table, the c5anim), then says its failures as the next second line would. True when written.
	virtual bool waitTestFiles() { return true; }
	virtual uint64_t prepares() const = 0;        // UnitRenderer::prepare calls (4455: one per copy and frame)
	virtual uint64_t reflectionDraws() const = 0; // reflection-pass draw() calls that returned Ok
	virtual uint64_t maskDraws() const { return 0; }   // ticket 4785: mask-pass draw() calls that returned Ok
	// Issue 4981: the unit extras (server.exe's factory passes MakeUnitsExtras(), SrvOptions() and ServerUnits = 1): null
	// keeps the scene as before; a module with no extra in effect after configure is dropped (no per-unit call).
	virtual void attachExtras(std::unique_ptr<UnitsExtras> x, const Vp64Opt::Options& o, bool unitsAreOurs) = 0;
	virtual UnitsExtras* extras() const = 0;
	// Ticket 5062: the unit animation controller of the current database (null: not open, or render_units_anim 0).
	virtual const AnimRunner* animRunner() const { return nullptr; }
	// Ticket 5062 (W107): the trigger stream of the current database (null: not open; its routes() count the hits' routes).
	virtual srveffects::TriggerStream* triggerStream() { return nullptr; }
};
std::unique_ptr<SceneUnits> MakeSceneUnitsWith(const UnitsOptions& o, FeedPort& port, EnvOpener env, LogFn log, ClockFn clock);

inline int64_t UnitKeyOf(int32_t owner, int32_t id) { return ((int64_t)owner << 32) | (uint32_t)id; }
// The kinds the scene takes as unit events (11-25, 29-31 units, 32 load, 33 era; 26-28 city kinds are 0042's).
bool IsUnitKind(uint16_t kind);

// The pass camera of the scene pass for wrap copy k (cb1, unit-draws.md 3; note section 7): r3 / r11 the eye, r4-r7 the
// rows of Translation(k wrap, 0, 0) x view x proj, r8.x the water level, r9 / r10 the terrain's DistanceFog block at the
// same eye (c5r::terrain::distanceFogBlock). eye = camWorld's row 3 when finite and not all 0, else the inverse of view's.
bool passCamera(const UnitsCamera& cam, int k, float waterLevel, c5r::units::PassCamera& out);

}  // namespace srvunits
