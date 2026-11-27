// srv_units_anim.h - 0374's unit animation controller fed from the server's unit scene (epic 0375, issue 4456; decision 6).
// Written 2026-11-16 for this repository (GPL-3.0-or-later). Design: research/05-graphics-composition/unit-server-scene.md
// section 6 (as built); the controller, its input and its file form: unit-controller.md 1-4 (c5r::unitanim, read only).
//
// Two pieces, both on the render thread (and in server/tools/unitfeed, which links the same code: D137 rule A):
//   AnimFeed    the feed's unit events as the scene took them (UnitsCore::onBatch hands every taken event) mapped to the
//               controller's timeline events (unit-controller.md 3, unchanged in form: c5r::unitanim::TimelineEvent), closed
//               into one TimelineFrame per drawn frame with dt = the scene's frame clock step (decision 6), seq = the
//               controller's frame count (1, 2, ...), exe_frame = the composite's frame id (a redraw of one frame id gives
//               two frames with the same exe_frame), present_time = the frame clock in s.
//   AnimRunner  the controller (ActionBridge, ClipGraphFactory, the unit string databases) on the scene's environment; its
//               programs go to the drawing renderer through ControllerPose (units.h PoseSource); per-unit statuses other
//               than Ok are the scene's AnimFailed (counted once per unit episode, LOUD once per unit type and code).
// The mapping (unit-controller.md 3; choices of this scene marked [I] in the note's section 6):
//   the table when the controller starts  create per unit (in table order)
//   11 created          create (fields from the table after its batch); a held key: remove + create (era change, S3)
//   12 moved            move <facing> <path length>, then arrive <facing> in the same frame: position mode own places the
//                       unit at its destination at once (decision 5), so 0042's walk and arrival do not exist yet [I].
//                       Mode exe (issue 4755): move only; arrive <facing> at the EXE's arrival record (or F9's rule), in a
//                       later frame (unit-server-scene.md 17); between them `turn <facing> <left|right>` where the walk's
//                       heading from the EXE's position deltas changes direction (issue 4772, unit-server-scene.md 19)
//   13 teleported       place <plot> <facing>;   30 rebased: place <to plot> <facing>
//   14 destroyed        remove (the scene stops listing it at once; the queued death visuals are 0042's) [I]
//   15 died in combat   destroyed 1;   16 damage <damage> <previous>;   17 fortify <0|1>;   21 embark <0|1> (a key not
//   held: create);   22 garrison <0|1>;   24 visibility <bit 0> <blend s>;   31 reset;   19 action paradrop;
//   20 action activate;   18 action work <Builds.Type> (a[2] -1, the work stop, has no event: counted, 14.3)
//   32 load begin       remove every held unit;   23, 25-29, 33: no event (25 / 28 are 0042's; 33 changes the player row)
// A unit whose create cannot be formed (its record gone by the frame's end, an unknown type, owner or plot) is not held:
// its later events are dropped and counted (the timeline reader refuses events for units it does not hold).
// Issue 4774 (epic 0042, route S; unit-combat-server.md 6): the fights. AnimFeed takes the combat records (25 / 28) in the
// DLL's order, adds them to its fight clock (srv_units_fights.h) and holds a fighter's waiting ops (srv_units_queue.h) until
// its fight ends; the clock's player is the runner's (setRunner): the 4775 core (CombatRun) stepped against this
// controller's figures (ControllerFeedback), its events appended to the same timeline frame. The runner sets the scene's
// 'held in a fight' field (UnitsCore::setHeldInFight), always steps a fighter (a fighter the cull leaves out would never
// play its clip and the fight would run to its limit), and draws a held unit from its view (HoldView).
// Where 4772 and 4774 meet (unit-combat-server.md 6.4): a unit a fight names is drawn from its view (fillFigures applies the
// walk facings first, the view over them), and its open walk gives no sample and no turn until the hold ends; an arrive
// the hold gives back is applied in its release frame, never deferred (a deferred one would be held again by a next fight).
// Issue 4724 (unit-combat-server.md 6.5): the fights' figure motion (4776). The runner's placement source (fightPlacement,
// set on the drawing renderer by the host) is 4776's FightPlacement over the module's own placement; the fight player
// gives each fight's units start positions (combatPositions on the frame's height field, setGrid, with the unit's
// formation, ZOffsets and drawn input as its last placement request had them), rebuilds the fight's FightMotion from the
// run's result as it grows and sets it per frame; at the fight's end the final FightMotion keeps its clock running for the
// walk back, and the unit's frozen view is kept until its figures stand in their slots (returning()).
#pragma once
#include <functional>   // 0685
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "srv_units_core.h"
#include "srv_units_fights.h"   // issue 4774
#include "srv_units_queue.h"    // issue 4774
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/controller_pose.h"
#include "c5r/unitanim/driver.h"
#include "c5r/unitanim/motion.h"   // issue 4724
#include "c5r/unitanim/quick_combat.h"   // ticket 0796
#include "c5r/unitanim/timeline.h"

namespace srveffects { class AnimTriggerTap; }   // issue 4722 (server/src/effects/srv_trigger_tap.h)

namespace srvunits {

// Issue 4724: the fights' figure motion, counted (AnimRunner::motionCounters).
const double kReturnLimit = 10.0;   // seconds after a fight's end: a walk back still running is ended (counted)
struct MotionCounters
{
	uint64_t positioned = 0;     // fights started with start positions for every side (the core times its approach)
	uint64_t noPositions = 0;    // fights started without (no grid, no formations, a unit side not placed the frame before)
	uint64_t positionFails = 0;  // combatPositions or the city's plot centre refused (LOUD; the fight runs without positions)
	uint64_t builds = 0;         // FightMotion builds (each growth of a run's result, and the final one)
	uint64_t buildFails = 0;     // FightMotion::build refused (LOUD; the unit is placed as by the module)
	uint64_t returns = 0;        // units that walked back after a fight's end
	uint64_t settled = 0;        // of them: every figure back in its slot
	uint64_t notDrawn = 0;       // of them: not placed in the frame before (nothing to show: released at once)
	uint64_t returnLimit = 0;    // of them: still walking kReturnLimit s after the end (ended)
	uint64_t stopped = 0;        // of them: stopped by the unit's next fight, a removal or a move in mode exe
	uint64_t residualWalks = 0;  // ticket 4742: of them, units a placement still moved after the fight's end (the walk
	                             // home inside the fight left a gap: walk_home off, the limit cut it, a victory figure,
	                             // a living count or a root / facing that differs from the core's after the fight)
	// issue 4726: the other participants (the interceptor, the damage members that are units)
	uint64_t othersWith = 0;     // taken with their figures (the controller holds them)
	uint64_t othersWithout = 0;  // the controller does not hold them (culled before, not created, refused): no figures
	uint64_t othersUnplaced = 0; // with figures but not placed the frame before: they fight in their slots (no motion)
};

struct AnimCounters
{
	uint64_t frames = 0, events = 0, creates = 0, removes = 0;
	uint64_t droppedNotHeld = 0;      // events for a unit the controller does not hold (its create could not be formed)
	uint64_t createsNotFormed = 0;    // creates skipped: the record gone by the frame's end, or type / owner / plot unknown
	uint64_t workStops = 0;           // kind 18 with build -1: no timeline event (unit-controller.md 14.3)
	uint64_t workUnknown = 0;         // kind 18 with a build index that has no Builds row: no event, counted
	uint64_t redraws = 0;             // a frame whose frame id is not above the last one (a redraw, a new composite)
	uint64_t playerChanges = 0;       // a player row changed after its first frame (era): given to the controller live
	uint64_t resyncs = 0;             // closes that followed the table after a kOverflow
	uint64_t arrives = 0;             // issue 4755 (mode exe): arrive events given (the EXE's arrival, or F9's rule)
	uint64_t arrivesDeferred = 0;     // issue 4755: arrives carried to the next frame because their unit's move was in this one
	// issue 4772 (mode exe): the walk's heading from the EXE's position deltas
	uint64_t turns = 0;               // turn events given (the heading left the unit's facing by more than kWalkTurnDeg)
	uint64_t turnsRight = 0;          // of them, clockwise (right = 1)
	uint64_t walkSamples = 0;         // walk roots that gave a heading (a step of kWalkMinStep or more from the last one)
	uint64_t walkJumps = 0;           // walk steps longer than kWalkMaxStep: not a heading (the wrap seam, a jump), start again
	uint64_t walksNoHeading = 0;      // walks that ended with no heading at all (a single record, an arrival right after the move)
	// issue 4774: the fights
	uint64_t combats = 0;             // combat records taken (kinds 25 / 28)
	uint64_t combatsBad = 0;          // a combat event whose record is not in its batch: skipped (the core says so LOUD)
	uint64_t combatsRefused = 0;      // records the fight clock refused (seq order, kind): no fight, nothing held
	uint64_t fightEvents = 0;         // timeline events the fights made
	uint64_t ownerEnds = 0;           // OwnerEnd records of fights (not of clip instances)
	uint64_t gone = 0;                // fights ended because a fighter's removal passed (a broken hold)
	uint64_t clockRefusals = 0;       // frames the fight clock refused (cannot happen: the frame seq increases)
	// issue 4783 (W33 #8801): the EXE's event ids that came after their combat record was drained (Batch::lateResults)
	uint64_t lateResults = 0;         // late results taken
	uint64_t lateToRecord = 0;        // of them, set in a record not yet added to the clock (taken, no frame since)
	uint64_t lateToFight = 0;         // ... set in a fight of the clock (queued or playing: FightClock::setOwner Applied)
	uint64_t lateNoFight = 0;         // ... for no fight held (Quick Combat, not visualised, ended, or already known)
	uint64_t ownerZeroEnds = 0;       // a fight's OwnerEnd sent with its event id still unknown (LOUD; the shell cannot pass it)
	// issue 4739: the controller follows the table at each close for the units the frame touched (AnimFeed::close)
	uint64_t recreated = 0;           // a unit the table lists and the controller did not hold, created again
	uint64_t rereads = 0;             // a held unit whose embarked bit differs from the table's, removed and created again
	uint64_t retriesWaiting = 0;      // closes where a unit whose create was not formed waited for its record to change
	uint64_t returnsFreed = 0;        // ticket 5066 (b): kept walk-back views freed by a kind 12 given back at the fight's end
	// ticket 5066 (a) (mode exe, AnimFeed::setFightAfterWalk): the mover's combat waits behind its open walk
	uint64_t fightWalkHeld = 0;       // combat records put behind an open walk (counted again when held again)
	uint64_t fightWalkBehind = 0;     // later ops of a waiting combat's units put behind it (feed order)
	uint64_t fightWalkDropped = 0;    // waiting combats and ops behind them dropped by a load
	// ticket 5065 (mode exe, AnimFeed::setOrdersAfterWalk): a walking unit's work and fortify ops wait for its walk's end
	uint64_t afterWalkHeld = 0;       // ops put behind an open walk
	uint64_t afterWalkReleased = 0;   // ops given back at the walk's end (arrive, place, the unit's removal or creation)
	uint64_t afterWalkNoWalk = 0;     // of them, given back because no walk of the unit opened (its kind 12 was dropped)
};

// ---- Issue 4772 (epic 0042): the walk's facing in mode exe (unit-server-scene.md 19) -------------------------------------
// World axes of the EXE's position records and of the placement: +x east, +y north. DirectionTypes d (0 NE, 1 E, 2 SE,
// 3 SW, 4 W, 5 NW) points along the world heading 60 - 60 d degrees (the placement's yaw 150 - 60 d is that heading + 90:
// the models face -y, unit-formations.md 11.2).
// A walking unit (mode exe, from its `move` to its `arrive` or `place`) faces the hex direction of its root's movement: per
// drawn frame the step from the walk's last sample to the newest position record (UnitsCore::rootOf) is taken when it is at
// least kWalkMinStep long (shorter steps accumulate), refused as a jump when longer than kWalkMaxStep (the sample starts
// again there), and when the step's heading leaves the unit's facing by more than kWalkTurnDeg the unit turns to the
// direction nearest to it: a `turn <d> <left|right>` event (right: clockwise, the heading falls). The walk's first sample has
// no step; before its first turn the unit keeps the facing its last create / arrive / place / turn gave it. The `arrive`
// gives the record's (end) facing as before (issue 4755). Quick movement (kinds 13, no walk) never turns.
const float kWalkTurnDeg = 30.0f;     // half the 60-degree step between directions: the nearest direction [I: 19.1]
const float kWalkMinStep = 1.0f;      // world units (a plot is about 58 across; a walk moves about 2.2 per EXE frame [L 4755])
const float kWalkMaxStep = 256.0f;    // world units (about 4.4 plots in one drawn frame)
// The DirectionTypes value whose heading is nearest to (dx, dy) (a tie, exactly between two, goes to the counter-clockwise
// one: (0, 1), north, gives 5); -1 for a zero or non-finite vector.
int WalkDirection(float dx, float dy);
// The signed angle in degrees from facing d's heading to (dx, dy), in (-180, 180] (positive: counter-clockwise, left).
float WalkOffsetDeg(int facing, float dx, float dy);

// ---- Ticket 0685 (epic 0673): the walk's turn over time (unit-server-scene.md 19.6) -----------------------------------
// The facing rule above is kept (the same events, the same discrete facing for the placement). Each unit also has a drawn
// yaw (radians, the placement's convention: facingYaw) that follows its facing's yaw over time instead of jumping: per
// drawn frame it moves towards it by 2 pi x rate x dt, the short way (exactly 180 degrees: the turn event's side), rate in
// turns per second = (TurnFacingRateMin + 0.5 x (TurnFacingRateMax - TurnFacingRateMin)) / 2 pi of the unit's first
// member, the facing rates read as radians per second (EXE defaults 15 / 20: 17.5 rad/s, 60 degrees in 60 ms). Stock turns
// a walking formation 120 degrees in 1-2 frames and 60 in 2 at 30 fps (unit-server-scene.md 19.9 [L]); which rate the
// EXE uses is not established [I]. (19.6's first rate, TurnRate at 0.75 turns per second, was 5-8 times too slow.) It turns
// when a walk's `turn` and an `arrive` change the facing (also before the walk's first step); a create or a `place`
// (teleport) sets it at once. Drawn by 4981's rigid turn about the root (MergeWalkTurns, ApplyUnitXforms). Off
// (C5_RENDER_UNITS_WALK_TURN=0 in the server's environment): the facing snaps, issue 4772's frames.
const float kWalkTurnRateDefault = 17.5f / 6.28318530717958647692f;   // turns per second: TurnFacingRate 15 / 20 at 0.5
const double kWalkTurnMaxDt = 0.1;          // seconds: a longer frame step (a stall, a load) turns by this much
// (TurnFacingRateMin + 0.5 (Max - Min)) / 2 pi turns per second (the facing rates in radians per second);
// kWalkTurnRateDefault when that is not finite or not above 0
float WalkTurnRate(float facingRateMin, float facingRateMax);

// The drawn side of the rule: per unit the facing the timeline gave it (create, arrive, place, turn) and whether a walk is
// open (`move` applied, no arrive / place / create / remove since), followed from the frames in order. AnimRunner holds
// one and applies it in fillFigures; the tests drive it directly (no install needed).
class WalkFacings
{
public:
	void follow(const c5r::unitanim::TimelineFrame& f);
	// true with the facing a walking unit is drawn with (its last turn's, else the one it had before the walk)
	bool facing(int32_t player, int32_t id, int& facing) const;
	size_t walking() const;
	// every walking unit of sc drawn with its walk facing; the others keep the build's (the record's) facing
	void apply(c5r::units::UnitScene& sc) const;
	void clear()
	{
		m_h.clear();
		m_turnYaws.clear();
	}

	// 0685: after follow, each frame: every unit's drawn yaw towards its facing's yaw by dt seconds (clamped to
	// [0, kWalkTurnMaxDt]) at rateOf(key) turns per second (no function, or a value not above 0: kWalkTurnRateDefault).
	// turnYaws() then holds the units drawn at a yaw other than their facing's this frame. Off: nothing turns.
	void advance(double dt, const std::function<float(int64_t)>& rateOf);
	void setTurnOn(bool on);
	bool turnOn() const { return m_turnOn; }
	// 0783 (unit-server-scene.md 19.13): before advance, each frame: every walking unit's root (rootOf: the root the frame
	// draws, false when it has none) is sampled as 4772's AnimFeed samples it (a step of kWalkMinStep or more, a jump over
	// kWalkMaxStep starts the sample again); the step's heading is the walk's tangent, and while the unit walks advance
	// turns its drawn yaw towards the tangent's yaw (heading + 90 degrees, the placement's convention) instead of its
	// facing's, so the formation turns along the EXE's curved root path (stock's arc) at most at the unit's rate. The
	// discrete facing and its events are unchanged. Off (setCurveOn(false), C5_RENDER_UNITS_WALK_CURVE=0): 0685's frames.
	void track(const std::function<bool(int64_t, float&, float&)>& rootOf);
	void setCurveOn(bool on) { m_curveOn = on; }
	bool curveOn() const { return m_curveOn; }
	// 0783: the drawn yaw of a unit whose drawn yaw is not its facing's this frame (turning, or on the walk's tangent);
	// false otherwise. facing: the facing that yaw belongs to (the placement must have been laid at it)
	bool drawnYaw(int64_t key, int& facing, float& yaw) const;
	// key (UnitKeyOf) -> the drawn yaw of every unit still turning this frame
	const std::map<int64_t, float>& turnYaws() const { return m_turnYaws; }
	// 0685: the facing (0..5) a unit's drawn yaw turns towards; false for a unit not followed or without a facing
	bool target(int64_t key, int& facing) const;
	// 0685 (diagnostic, unit-server-scene.md 19.9): "facing F side S walking W yaw Y goal G turning T" (degrees), or
	// "not followed"
	std::string describe(int64_t key) const;
	struct TurnCounters
	{
		uint64_t turns = 0;           // turns started (the drawn yaw left its facing's)
		uint64_t turnFrames = 0;      // frames a unit was drawn turned (the sum over units)
		double turnMsMax = 0;         // the longest finished turn, from its first frame to the frame it reached the facing
		uint64_t tanSamples = 0;      // 0783: walk root steps that set a unit's tangent
		uint64_t tanFrames = 0;       // 0783: frames a walking unit was drawn on its tangent (the sum over units)
	};
	const TurnCounters& turnCounters() const { return m_tc; }

private:
	struct Heading
	{
		int8_t facing = -1;
		bool walking = false;
		bool hasYaw = false;          // 0685: the drawn yaw below is set
		float yaw = 0;                // 0685: radians, the placement's convention
		int8_t side = 0;              // 0685: +1 left (counter-clockwise), -1 right, 0 the short way (the last turn's)
		bool turning = false;
		double turnMs = 0;
		bool hasSample = false;       // 0783: the walk's last root sample (sx, sy)
		float sx = 0, sy = 0;
		bool hasTan = false;          // 0783: the walk's tangent yaw (the placement's convention) is set
		float tanYaw = 0;
	};
	std::map<int64_t, Heading> m_h;
	bool m_turnOn = true;
	bool m_curveOn = true;   // 0783
	std::map<int64_t, float> m_turnYaws;
	TurnCounters m_tc;
};

// 0685: the units of b that walkFacings draws turned this frame, merged into b.xforms for ApplyUnitXforms (4981's rigid
// turn about the root): a unit drawn at facing f (the placement's yaw facingYaw(f)) turns to its drawn yaw; an extra's
// own heading composes (its yaw + the drawn yaw - facingYaw(f)). A unit whose drawn facing is not the turn's target (a
// fight's view, a record changed at rest) is left as placed. Returns the units merged.
// 0783: with placedYaw true the runner's placement (MotionPlacement) already lays the figures at the drawn yaw, so only
// an extra's own heading is composed (an entry with heading); no entry is made for the walk's yaw alone.
size_t MergeWalkTurns(const WalkFacings& walkFacings, BuiltFrame& b, bool placedYaw);
size_t MergeWalkTurns(const WalkFacings& walkFacings, BuiltFrame& b);


class AnimRunner;   // below

// Issue 4774: the fight clock's player as AnimFeed drives it: the events its start / advance made since the last take
// (in order), and the clip instances of the figures of the units named.
class AnimFightPlayer : public FightPlayer
{
public:
	virtual void takeEvents(std::vector<c5r::unitanim::TimelineEvent>& out) = 0;
	virtual void figures(const std::set<std::pair<int32_t, int32_t>>& units, std::vector<FigureInstance>& out) = 0;
	// LOUD texts since the last take (a fight at its limit, the core's notes: a missing clip, no hit trigger)
	virtual void takeLoud(std::vector<std::string>& out) { (void)out; }
};

// Issue 4774: one fight's end as AnimFeed keeps it (the trigger stream 4795 and the proxy's hold 4773 take OwnerEnd).
struct FightEndRecord
{
	OwnerEnd end;
	int64_t exeFrame = -1;            // the frame id of the timeline frame it closed in
	uint64_t frameSeq = 0;            // that frame's seq
	size_t released = 0;              // held ops given back by it
};

class AnimFeed
{
public:
	// Builds (ID -> Type) for kind 18's argument; may be empty (then every work event is counted workUnknown).
	void setBuilds(const std::map<int32_t, std::string>& b) { m_builds = b; }
	// Every unit event UnitsCore::onBatch took (seq order, after the batch's table copy). Ignored until start().
	void take(const SrvFeed::Event& e, const SrvFeed::Batch& b);
	// Issue 4783: a batch's late combat results (UnitsCore::onBatch hands Batch::lateResults on). Applied at the next
	// close, before the fight clock's frame: a record taken and not yet added gets the id in place; a fight the clock holds
	// gets it through FightClock::setOwner (its later trigger records and its OwnerEnd carry it). Ignored until start().
	void lateResults(const std::vector<SrvFeed::LateCombatResult>& v);
	// kOverflow (events dropped, the table re-copied): at the next close the held set follows the table (removes of units
	// gone, creates of units not held), counted resyncs.
	void resync() { if(m_started) m_resync = true; }
	// Issue 4755: the position source. Own (false, the default): kind 12 gives `move` and `arrive` in one frame (0375's
	// mapping). Exe (true): kind 12 gives `move` only; arrive() gives the `arrive` (UnitsCore calls it at the unit's arrival
	// record or F9's rule), never in the frame of the unit's move: such an arrive waits for the next frame.
	void setPositionsExe(bool on) { m_posExe = on; }
	bool positionsExe() const { return m_posExe; }
	// Ticket 5066 (rule A, W117; unit-combat-server.md 18.42): the fighter's walks around its fight as stock, mode exe only
	// (render_units_fight_after_walk; 1 default). (b): a kind 12 the hold gives back at a fight's end (the winner's advance,
	// a withdrawal) frees the unit's view kept for its walk back, so its figures walk from where they stand to the slots of
	// the EXE's walk (the core's plot left, then its positions) instead of gliding back to the fight's plot under the kept
	// view and snapping at the return's end. (a): the mover's combat record (kind 25 or 28 whose attacker unit's walk is open,
	// or whose attacker a fight names with a kind 12 held behind it) waits in ticket 5065's hold behind that walk, as the
	// EXE's queue runs RunCombatSim behind the unit's move records; so the fight starts on the plot the walk reached. While it
	// waits, every later op naming one of its units waits behind it in feed order (the attacker's walk ends pass: arrive,
	// teleport, rebase, removal, creation), and its other units keep the view of the frame before (a defender that dies in
	// the same drain stays drawn until its fight). The table holds kinds 25 and 28 while on. Off: the rule before 5066, byte
	// for byte.
	void setFightAfterWalk(bool on);
	bool fightAfterWalk() const { return m_fightAfterWalk; }
	void arrive(int32_t owner, int32_t id);
	// The controller starts: the next close() creates every unit of the table first; events taken so far are dropped.
	void start();
	bool started() const { return m_started; }
	// One drawn frame at the frame clock tMs (after UnitsCore::build). out = the frame's timeline events and step; players
	// = the owners whose PlayerInput the controller must be given (first seen or changed) before applying it.
	void close(const UnitsCore& core, double tMs, int64_t frameId, c5r::unitanim::TimelineFrame& out,
	           std::vector<c5r::units::PlayerInput>& players);
	const AnimCounters& counters() const { return m_c; }
	bool holds(int32_t owner, int32_t id) const { return m_held.count(UnitKeyOf(owner, id)) != 0; }
	size_t held() const { return m_held.size(); }
	size_t walksOpen() const { return m_walks.size(); }   // issue 4772
	// Ticket 5065 (shared with 5066): true while the unit's walk is open in mode exe, from its kind 12 to its arrive (the
	// EXE's arrival or F9's rule), a place (kinds 13 / 30), or the unit's removal or creation. Always false in mode own.
	bool walkOpen(int32_t owner, int32_t id) const { return m_posExe && m_walks.count(UnitKeyOf(owner, id)) != 0; }
	// Ticket 5065 (rule A, W118): the hold behind an open walk. The EXE's queue holds a unit's later records behind its held
	// move record, so stock plays them after the walk (an automated worker's MOVE_TO and BUILD post kind 12 and kind 18 in
	// one DLL call: it runs, then works). Before 5065 the controller took them at once: the worker played its tool or idle
	// clip while the EXE's positions slid it along the path, and the arrive's idle then ended the work. Every op taken
	// passes holdBehindWalks() at the start of close(), before any routing (the fights' hold included): in mode exe an op
	// whose kind is in the table and whose unit's walk is open waits, in feed order, until the walk ends (arrive, place,
	// the unit's removal or creation, a load; F9's rule ends a walk by an arrive); the ops come back first at the next
	// close (in seq order). A unit with ops waiting and no open walk (its kind 12 opened none) and no fight naming it gets
	// them back at the close's end. Mode own: no walk is open, nothing waits.
	// The table: kinds held behind a walk (empty: the rule before 5065, byte for byte). 5065: kinds 17 and 18 under
	// setOrdersAfterWalk (render_units_orders_after_walk, default on); 5066 adds the mover's combat records under its knob.
	void holdAfterWalk(uint16_t kind, bool on);
	bool holdsAfterWalk(uint16_t kind) const { return m_afterWalkKinds.count(kind) != 0; }
	bool holdsAnyAfterWalk() const { return !m_afterWalkKinds.empty(); }
	void setOrdersAfterWalk(bool on);   // kinds 18 (work) and 17 (fortify)
	bool ordersAfterWalk() const;
	size_t afterWalkWaiting() const;    // ops waiting behind open walks now
	// Issue 4774: the fights run on this runner's controller (its FightPlayer, its view of held units); null: none. Without
	// a player the combat records are counted and nothing is held (the scene as before 4774).
	void setRunner(AnimRunner* r);
	// Issue 4774: the same with any player (tests); view: the held units' view the drawing side applies (may be null).
	void setFightPlayer(AnimFightPlayer* p);
	const UnitHold& hold() const { return m_hold; }
	HoldView& view() { return m_view; }
	const FightClock* fights() const { return m_clock.get(); }
	// Issue 4774: the fights' ends of the last close() and of every close so far (the file form: unitfeed's fights.tsv).
	const std::vector<FightEndRecord>& lastFightEnds() const { return m_lastEnds; }
	const std::vector<FightEndRecord>& fightEnds() const { return m_allEnds; }
	int64_t lastFrameId() const { return m_frameId; }   // issue 4722: the frame id of the last close()
	// issue 4722: the clip instances' ends (ClipEnded) of the last close(), for the trigger stream (not in fightEnds())
	const std::vector<OwnerEnd>& lastClipEnds() const { return m_lastClipEnds; }
	void keepFightEnds(bool on) { m_keepEnds = on; }   // m_allEnds is filled only when on (unitfeed)
	// Issue 4774: LOUD texts of the last close (a refused fight, a hold's overflow); the runner logs them through the core.
	const std::vector<std::string>& lastLoud() const { return m_loud; }
	// Issue 4724: units whose fight has ended and whose view is kept while their figures walk back (AnimRunner::returning);
	// the ops that pass for them meanwhile go into the view (a move in mode exe, a removal: the walk back stops there).
	bool keptForReturn(int32_t owner, int32_t id) const { return m_returning.count(std::make_pair(owner, id)) != 0; }
	// Issue 4729: every unit kept for its walk back (the view relists it after the build, so the controller must draw it)
	const std::set<std::pair<int32_t, int32_t>>& returningUnits() const { return m_returning; }
	// Ticket 5066 (a): every unit whose view is kept for a combat waiting behind a walk (relisted and drawn the same way)
	const std::set<std::pair<int32_t, int32_t>>& waitingFightUnits() const { return m_preFrozen; }

private:
	struct Op
	{
		uint16_t kind = 0;
		int32_t owner = -1, id = -1, a2 = 0, a3 = 0, pathLen = 0;
		uint32_t seq = 0;          // issue 4774: the event's seq (0 for an arrive)
		int32_t dest = -1;         // issue 4774: kind 12, the path's last plot
		int32_t combat = -1;       // issue 4774: kinds 25 / 28, the index in m_combats
		bool released = false;     // issue 4774: given back by the hold this frame (not held again)
		bool fightWait = false;    // ticket 5066 (a): waits behind a walk as (or behind) a mover's combat
	};
	// issue 4772: a walk's heading state (mode exe), from its move to its arrive / place / the unit's removal or creation
	struct Walk
	{
		bool haveLast = false;        // a sample taken
		float lastX = 0, lastY = 0;   // the walk's last sample (the root a heading was last measured from)
		bool headed = false;          // a step gave a heading (counted walksNoHeading at the end when false)
	};
	// issue 4774
	struct CombatIn
	{
		SrvFeed::CombatRecord rec;
		std::vector<SrvFeed::CombatMember> members;
		int32_t cityPlot = -1;   // ticket 5062: the attacking city's plot (kind 28's a[3] - 1; -1 not known)
		bool resumed = false;    // ticket 0742: given back after its wait behind a walk (5066 (a)): FightClock::add's resumed
	};
	void fightsFrame(const UnitsCore& core, c5r::unitanim::TimelineFrame& out);
	void endFights(const std::vector<OwnerEnd>& ends, const UnitsCore& core, int64_t frameId, uint64_t frameSeq,
	               std::vector<Op>& released);
	static HeldOp heldOf(const Op& o);
	static Op opOf(const HeldOp& h);
	AnimFightPlayer* m_player = nullptr;
	AnimRunner* m_runner = nullptr;
	std::unique_ptr<FightClock> m_clock;
	UnitHold m_hold;
	HoldView m_view;
	std::vector<CombatIn> m_combats;
	std::vector<SrvFeed::LateCombatResult> m_late;   // issue 4783: applied at the next close
	void applyLate();                                // issue 4783
	std::vector<FightEndRecord> m_lastEnds, m_allEnds;
	std::vector<OwnerEnd> m_lastClipEnds;   // issue 4722
	bool m_keepEnds = false;
	std::vector<std::string> m_loud;
	std::set<std::pair<int32_t, int32_t>> m_returning;   // issue 4724: views kept for the walk back
	int64_t m_frameId = -1;
	uint32_t m_lastTakenSeq = 0;   // issue 4774
	// why: when not formed, the first test that refused (issue 4739: the LOUD line of a create not formed names it)
	bool createOf(const UnitsCore& core, int32_t owner, int32_t id, c5r::unitanim::TimelineEvent& ev, std::string* why = nullptr);
	void push(c5r::unitanim::TimelineFrame& out, c5r::unitanim::EventKind k, int32_t owner, int32_t id);
	void endWalk(int64_t key);                                                        // issue 4772
	void walkHeadings(const UnitsCore& core, c5r::unitanim::TimelineFrame& out);     // issue 4772
	std::vector<Op> m_ops;
	std::map<int64_t, int8_t> m_face;   // issue 4772: the facing the timeline gave each held unit (create, arrive, place, turn)
	std::map<int64_t, Walk> m_walks;    // issue 4772: the open walks (mode exe)
	// ticket 5065: the table of kinds held behind a walk (default: 17 and 18); per walking unit the ops behind its walk
	// (feed order); at the walk's end (endWalk) they move to m_afterWalkBack, which goes first at the next close
	std::set<uint16_t> m_afterWalkKinds{SrvFeed::kUnitFortify, SrvFeed::kUnitWork, SrvFeed::kUnitCombat, SrvFeed::kCityCombat};
	std::map<int64_t, std::vector<Op>> m_afterWalkOps;
	std::vector<Op> m_afterWalkBack;
	void holdBehindWalks();   // ticket 5065: the first step of close()
	// ticket 5066 (a): a waiting combat's record (by seq: Op.combat indexes m_combats, which each close clears); per unit of
	// a waiting combat the walk keys whose queues its later ops wait in (every one: a unit two waiting combats name may wait
	// behind two walks, W125); the units whose view was taken for a waiting combat (released when no waiting combat names
	// them and no fight owns them); kind 12s the fights' hold keeps per unit
	std::map<uint32_t, CombatIn> m_afterWalkCombats;
	std::map<int64_t, std::set<int64_t>> m_fightTaint;
	bool taintedQueue(int64_t unitKey, int64_t& walkKey) const;   // a walk key of the unit's whose queue still waits
	std::set<std::pair<int32_t, int32_t>> m_preFrozen;
	std::vector<std::pair<int32_t, int32_t>> m_freezeNow;
	std::set<int64_t> m_fightHeldMoves;
	bool holdCombatsAfterWalk() const;
	void fightersOf(const SrvFeed::CombatRecord& rec, const std::vector<SrvFeed::CombatMember>& members,
	                std::vector<std::pair<int32_t, int32_t>>& out) const;
	void untaint(int64_t walkKey);              // the queue of walkKey went back: its units' later ops pass again
	void giveBackAfterWalk(std::vector<Op>& back);   // the close's end: back first in m_ops, the combats re-attached
	void preFrozenFrame();                      // ticket 5066 (a): after the fights' ops, views no waiting combat needs
	std::set<int64_t> m_held;
	// issue 4739: per held unit the embarked bit of the input it was created with (or its last embark event); per unit
	// whose create was not formed, the table record's seq it was refused at (tried again only when the record changes)
	std::map<int64_t, bool> m_heldEmbarked;
	std::map<int64_t, uint32_t> m_unformed;
	std::map<int32_t, std::string> m_builds;
	std::map<int32_t, std::string> m_playerKey;   // the last PlayerInput given per player (colour|style|era)
	bool m_started = false, m_startPending = false, m_haveFrame = false, m_resync = false;
	bool m_posExe = false;   // issue 4755
	bool m_fightAfterWalk = true;   // ticket 5066 (setFightAfterWalk)
	static const uint16_t kOpArrive = 0xFF01;   // issue 4755: an Op that is no feed kind (arrive())
	double m_lastMs = 0;
	int64_t m_lastFrameId = 0;
	AnimCounters m_c;
};

// The controller on the scene's environment. Every call on one thread.
class AnimRunner
{
public:
	AnimRunner();
	~AnimRunner();
	// files: the install with the mods (the scene's Env); db: its database (bridge tables, Builds); art: the scene's resolver
	// (outlives the runner). False with why: the string databases, the bridge's tables or Builds cannot be read.
	bool open(const c5r::gamefs::GameFiles& files, c5m::TableSource& db, c5r::units::ArtResolver& art, std::string& why);
	bool isOpen() const { return m_open; }
	const std::map<int32_t, std::string>& builds() const { return m_builds; }
	// One frame: players first, then Controller::apply, then every program into the pose source. A frame-level refusal
	// (a non-finite or negative dt): false, why. Per-unit statuses are noted in `core` (AnimFailed).
	bool apply(const c5r::unitanim::TimelineFrame& f, const std::vector<c5r::units::PlayerInput>& players, UnitsCore& core,
	           std::string& why);
	// Issue 4468: the same with the frame's drawn set (the units the scene lists this frame, every wrap copy): the
	// controller steps every held unit's clock and machine but builds programs (and so the pose source's and the figure
	// lines') for the drawn units only (controller.h apply(frame, drawn); unit-controller.md 18). null = every unit.
	bool apply(const c5r::unitanim::TimelineFrame& f, const std::vector<c5r::units::PlayerInput>& players, UnitsCore& core,
	           const std::set<c5r::unitanim::Controller::UnitKey>* drawn, std::string& why);
	c5r::units::PoseSource* pose();
	// The figure lines of one unit for .c5units (the dump and unitfeed): every figure whose program is one sample, as the
	// driver writes them (unit-controller.md 14.2); a figure that blends has no line (.c5units cannot hold a blend).
	void figuresOf(int32_t player, int32_t id, std::vector<c5r::units::FigureInput>& out) const;
	// Also (issue 4772): a walking unit (its `move` applied, no `arrive` / `place` / `create` / `remove` since) is drawn with
	// the facing its last `turn` gave it, before its first turn the facing its last create / arrive / place gave it (the
	// facing it had before the walk), instead of the record's (end) facing; every other unit keeps the build's facing. The
	// walk is followed from the frames apply() took, so the server and unitfeed (both call fillFigures after apply) agree.
	void fillFigures(c5r::units::UnitScene& sc) const;
	// issue 4772: the walk facings apply() follows (WalkFacings above)
	const WalkFacings& walkFacings() const { return m_walkFacings; }
	// 0685: the walking units turned this frame into b.xforms (MergeWalkTurns on this runner's walk facings); after
	// fillFigures, before ApplyUnitXforms. Returns the units merged.
	// 0783: when this runner's placement source is in use (fightPlacement made it) the figures are laid at the drawn yaw
	// there, and only an extra's own heading is composed here
	size_t mergeWalkTurns(BuiltFrame& b) const { return MergeWalkTurns(m_walkFacings, b, m_motionPlace != nullptr); }
	const c5r::unitanim::Controller& controller() const { return m_ctl; }
	size_t programs() const { return m_ctl.programs().size(); }
	// Issue 4468: clip durations read ahead (the pre-check's thread) into the graph factory (ClipGraphFactory::seed);
	// returns how many it took. clipLazyReads: the clips the factory read itself since open().
	size_t seedClipDurations(const c5r::unitanim::ClipGraphFactory::Durations& d) { return m_graphs ? m_graphs->seed(d) : 0; }
	size_t clipLazyReads() const { return m_graphs ? m_graphs->lazyReads() : 0; }
	size_t blendPrograms() const { return m_blends; }
	// Issue 4774: the fight clock's player on this controller (CombatRun + ControllerFeedback; AnimFeed::setRunner).
	AnimFightPlayer& fightPlayer();
	// Issue 4774: the feed whose fights run here (AnimFeed::setRunner): apply() sets the core's 'held in a fight' field from
	// its hold, steps every unit a fight names and logs its LOUD texts through the core; fillFigures draws a held unit from
	// its view.
	void attachFeed(AnimFeed* feed) { m_feed = feed; }
	// Issue 4774: the combat tables (ArtDefine_UnitMemberCombats / ...CombatWeapons) read at open(); when they could not be
	// read every fight is refused with this text (the units are drawn as before).
	const std::string& combatTablesWhy() const { return m_combatWhy; }
	// 4783 (W33, C1; MC #8906): the strike rule the tables allow (combatStrikeRule), "" when they could not be read
	std::string combatStrikeRule() const { return m_combatOk ? c5r::unitanim::combatStrikeRule(m_combatInfo) : std::string(); }
	// Issue 4722: the trigger stream's tap (null: no stream, the scene as before), not owned.
	void setTriggerTap(srveffects::AnimTriggerTap* t) { m_tap = t; }
	// Ticket 4737 (rule A, W43): CombatParams::charge_any for the fights this runner starts (true, the default again since
	// ticket 4743 W81, MC #11142: every approaching RunCharge attacker charges, exe-audio.md 13.3; false, the default from
	// 4743 W46 to W81: the 4775 rush-only rule). The host's render_test_units_charge_any; the player's from a bundle's
	// render_config (absent: false, recorded before 4737).
	void setChargeAny(bool v) { m_chargeAny = v; }
	bool chargeAny() const { return m_chargeAny; }
	// Ticket 4735 (rule A, W45): CombatParams::turn_first for the fights this runner starts (true, the default: a figure of a
	// fight whose figures stand turns to face the opponent first, playing the run; false: no turn, the rule before 4735).
	// The host's render_test_units_turn_first; the player's from a bundle's render_config (absent: false, before 4735).
	void setTurnFirst(bool v) { m_turnFirst = v; }
	bool turnFirst() const { return m_turnFirst; }
	// Ticket 4744 (rule A, W49): CombatParams::fight_drawn_figures for the fights this runner starts (true, the default: only
	// the figures a unit's placement draws stand in its fight; false: every held figure, the rule before 4744). The host's
	// render_units_fight_drawn_figures; the player's from a bundle's render_config (absent: false, before 4744).
	void setFightDrawnFigures(bool v) { m_fightDrawn = v; }
	bool fightDrawnFigures() const { return m_fightDrawn; }
	// Tickets 4745 + 4747 (rule A, W53): CombatParams::reform_first for the fights this runner starts (true, the default: a
	// unit whose ReformBeforeCombat names its side re-lays its figures facing the opponent before anyone strikes; false: the
	// rule before 4745). The host's render_test_units_reform_first; the player's from a bundle's render_config (absent: false).
	void setReformFirst(bool v) { m_reformFirst = v; }
	bool reformFirst() const { return m_reformFirst; }
	// Ticket 4742 (rule A, W54): CombatParams::walk_home for the fights this runner starts (true, the default: the survivors
	// walk home inside the fight with their run clip, the fight's OwnerEnd at the last arrival; false: the walk after the
	// fight's end, the rule before 4742). The host's render_test_units_walk_home_in_fight; the player's from a bundle's
	// render_config (absent: false).
	void setWalkHome(bool v) { m_walkHome = v; }
	bool walkHome() const { return m_walkHome; }
	// Ticket 4748 (rule A, W59): CombatParams::city_in_place for the fights this runner starts (true, the default: a melee
	// attacker on a city strikes from where its figures stand; false: it approaches the city's centre first, the rule
	// before 4748). The host's render_test_units_city_in_place; the player's from a bundle's render_config (absent: false).
	void setCityInPlace(bool v) { m_cityInPlace = v; }
	bool cityInPlace() const { return m_cityInPlace; }
	// Ticket 4746 (rule A, W57): CombatParams::ready_after_move for the fights this runner starts (true, the default: a
	// waiting figure enters combat ready 1600 only after a movement, stock's state 2; false: every waiting figure requests
	// 1600, the rule before 4746). The host's render_test_units_ready_after_move; the player's from a bundle's render_config
	// (absent: false).
	void setReadyAfterMove(bool v) { m_readyAfterMove = v; }
	bool readyAfterMove() const { return m_readyAfterMove; }
	// Ticket 4750 (rule A, W61): CombatParams::defend_from_stance for the fights this runner starts (true, the default: a
	// non-answering defender's figure takes stock's phase-2 stance, 1450, before bombard defend; false: 1280 at once, the
	// rule before 4750). The host's render_test_units_defend_from_stance; the player's from a bundle's render_config
	// (absent: false).
	void setDefendFromStance(bool v) { m_defendFromStance = v; }
	bool defendFromStance() const { return m_defendFromStance; }
	// Ticket 5050 (rule A, W65): CombatParams::reform_ends_at_arrival and attack_from_stance for the fights this runner
	// starts (true, the default since 5050 W74, MC #10667 / #10726: phase 1 ends at the last reforming figure's arrival /
	// an attacker's first Ready takes stock's phase-2 stance 1450 first; false: the rules before 5050). The host's
	// render_test_units_reform_ends_at_arrival / render_test_units_attack_from_stance; the player's from a bundle's
	// render_config (absent: false, a bundle recorded before 5050).
	void setReformEndsAtArrival(bool v) { m_reformEndsAtArrival = v; }
	bool reformEndsAtArrival() const { return m_reformEndsAtArrival; }
	void setAttackFromStance(bool v) { m_attackFromStance = v; }
	bool attackFromStance() const { return m_attackFromStance; }
	// Ticket 5057 (rule A, W85): CombatParams::rush_ride_through and defender_waits_first_landing for the fights this
	// runner starts (true, the default, MC #11162: a rush rider lands its hit at its arrival and rides on / the type 2
	// defender waits for the attacker's first landing, stock's behaviour; false: today's route S). The host's
	// render_units_rush_ride_through / render_units_defender_waits_first_landing; the player's from a bundle's
	// render_config (absent: false, a bundle recorded before 5057).
	void setRushRideThrough(bool v) { m_rushRideThrough = v; }
	bool rushRideThrough() const { return m_rushRideThrough; }
	void setDefenderWaitsFirstLanding(bool v) { m_defenderWaitsFirstLanding = v; }
	bool defenderWaitsFirstLanding() const { return m_defenderWaitsFirstLanding; }
	// Ticket 5053 (rule A, W112, design W72 18.26 (d)): CombatParams::death_by_variant and end_after_dying for the fights
	// this runner starts (true, the default since the MC's word of 2026-11-25: a dying figure lasts the death clip its
	// machine plays / the standing figures wait for the dying and a defender leaves 1280 with a shuffle; false: the rules
	// before 5053).
	// The host's render_test_units_death_by_variant / render_test_units_end_after_dying; the player's from a bundle's
	// render_config (absent: false, a bundle recorded before 5053).
	void setDeathByVariant(bool v) { m_deathByVariant = v; }
	bool deathByVariant() const { return m_deathByVariant; }
	void setEndAfterDying(bool v) { m_endAfterDying = v; }
	bool endAfterDying() const { return m_endAfterDying; }
	// Ticket 5059 (rule A, W100): CombatParams::victory_needs_word for the fights this runner starts (true, the default,
	// lead-0042 GO #11696: victory only for a member whose EnableActions carry the word Victory, as stock; none in VP, so
	// no victory pose or vox; false: today's route S, the machine's 1800 alone). The host's render_units_victory_word;
	// the player's from a bundle's render_config (absent: false, a bundle recorded before 5059).
	void setVictoryNeedsWord(bool v) { m_victoryNeedsWord = v; }
	bool victoryNeedsWord() const { return m_victoryNeedsWord; }
	// Ticket 5067 S1 (rule A, W122): CombatParams::type2_phase2 for the fights this runner starts (true, the default since
	// the MC's word #12907: stock's 0.665 s phase 2 before a type 2 fight's charge; false: no phase 2 in a charge fight,
	// route S before 5067). The host's render_units_type2_phase2; the player's from a bundle's render_config (absent: false).
	void setType2Phase2(bool v) { m_type2Phase2 = v; }
	bool type2Phase2() const { return m_type2Phase2; }
	// Ticket 0743 (rule A, W137): CombatParams::melee_reach for the fights this runner starts (true, the default: stock's
	// melee reach and nearest target, unit-combat-server.md 18.44; false: every approach ends MoveRadius short, the rule
	// before 0743). The host's render_units_melee_reach; the player's from a bundle's render_config (absent: false).
	void setMeleeReach(bool v) { m_meleeReach = v; }
	bool meleeReach() const { return m_meleeReach; }
	// Ticket 5061 (rule A, W139): CombatParams::defender_step for the fights this runner starts (true, the default: in a
	// type 2 fight the defender's figures step to stock's reach of the attackers from the first landing on,
	// unit-combat-server.md 18.45; false: the defender strikes from where it stands, the rule before 5061). The host's
	// render_units_defender_step; the player's from a bundle's render_config (absent: false).
	void setDefenderStep(bool v) { m_defenderStep = v; }
	bool defenderStep() const { return m_defenderStep; }
	// Ticket 5062 (rule A, W98): a city's own ranged strike names CombatParams::city_effect, the era's projectile VisEffect
	// as stock (Eras.CityBombardEffectTag of the city owner's current era, the DLL's CvCity::GetCityBombardEffectTag; read
	// at open, the era from the core's player record): the effect routes then fly it as a CityVolley and start its
	// ProjectileEnd child, and the EXE plays both VisEffects' SoundEffects (the shot and the miss; research
	// unit-combat-server.md 18.37). True, the default: render_units_trigger_city_shot 1; false: no name, the rule before
	// 5062 byte for byte (the player's from a bundle's render_config, absent: false). Only a city ATTACKER is named: a
	// city's answer in a melee keeps its empty name (route CityDefence, not this ticket's). The era comes from useCore's
	// player records (no core, or the era not known: no name, counted).
	void setCityShot(bool v) { m_cityShot = v; }
	bool cityShot() const { return m_cityShot; }
	void useCore(const UnitsCore* core) { m_eraCore = core; }
	// The tag of an era (EraTypes, the Eras table's ID) as read at open; "" when not known (no Eras table, no row, NULL).
	const std::string& cityBombardTag(int32_t era) const;
	size_t cityBombardTags() const { return m_cityTags.size(); }
	uint64_t cityShotsNamed() const { return m_cityShotsNamed; }      // fights started with a city_effect (5062)
	uint64_t cityShotsUnnamed() const { return m_cityShotsUnnamed; }  // a city attacker left unnamed: era or tag unknown
	const std::string& lastCityShot() const { return m_lastCityShot; }
	// Ticket 5062 (W107): the attacking city's centre. With the switch on, a city attacker's side is placed at its plot's
	// centre (Fight::attackerCityPlot, kind 28's a[3] from the DLL thread) so that the volley has its origin and the
	// routes fly it; no plot known (a recording before 5062, outside the grid) or a centre refused: no centre, the rule
	// before 5062, counted, and one plain line (takeCityLines). Off: never placed (the rule before 5062 byte for byte).
	uint64_t cityCentres() const { return m_cityCentres; }        // city attackers placed at their plot's centre
	uint64_t cityNoCentre() const { return m_cityNoCentre; }      // city attackers left without (no plot, centre refused)
	std::vector<std::string> takeCityLines() { std::vector<std::string> v; v.swap(m_cityLines); return v; }
	// 0685 (diagnostic): with C5_RENDER_UNITS_WALK_LOG=1 (read at open) one line per move / turn / arrive / place / remove
	// of a followed unit and per turning unit per frame; the scene says them with the frame's QPC time. Off: nothing.
	std::vector<std::string> takeWalkLines() { std::vector<std::string> v; v.swap(m_walkLines); return v; }
	const c5m::StringDatabase* strings() const { return m_strings.get(); }   // issue 4728: the stream's bone poser
	// Ticket 0796 (Quick Combat Animations T1, D270 V2; quick_combat.h): every fight this runner starts plays at the rule's
	// rates (the run-up at q.pre_rate, the first strike to its landing plus q.react at 1x, the rest at q.rate): the fight
	// clock, the choreography and the fighters' clip clocks together. Off (q.rate <= 1, the default): every fight at real
	// time, the rule before 0796 byte for byte. The scene's UnitsOptions::quickCombat (render_test_units_quick_combat).
	void setQuickCombat(const c5r::unitanim::QuickCombat& q) { m_quick = q; }
	const c5r::unitanim::QuickCombat& quickCombat() const { return m_quick; }

	// ---- Issue 4724: the fights' figure motion (4776) ----
	// The drawing renderer's placement source: FightPlacement over the module's own placement (makeModulePlacement of the
	// same install), made at the first call; the formation table for the start positions is read from the same install
	// (FormationTable::load, the module's rule). Null when the formations cannot be read (motionWhy() says why): the host
	// then keeps the module's own placement and fights run without positions.
	c5r::units::PlacementSource* fightPlacement(const c5r::gamefs::GameFiles& files, const std::wstring& installRoot);
	const std::string& motionWhy() const { return m_motionWhy; }
	// The frame's height field (the host, before apply): the fights that start in this frame are positioned on it.
	void setGrid(const c5r::heightfield::HeightGrid* g) { m_grid = g; }
	// A unit whose fight ended and whose figures still walk back (placed in the last frame, not yet all in their slots,
	// under kReturnLimit s since the end). AnimFeed keeps its view meanwhile.
	bool returning(int32_t player, int32_t id) const;
	// Ends a unit's walk back now (AnimFeed: its removal or its move in mode exe passed).
	void stopReturn(int32_t player, int32_t id);
	// The unit's motion: its clock (seconds from its fight's start) and whether its fight has ended. False: none.
	bool motionOf(int32_t player, int32_t id, double& t, bool& ended) const;
	const MotionCounters& motionCounters() const { return m_mc; }

private:
	srveffects::AnimTriggerTap* m_tap = nullptr;   // issue 4722
	friend class ControllerFightPlayer;   // issue 4774 (srv_units_anim.cpp)
	friend class MotionPlacement;         // issue 4724 (srv_units_anim.cpp)
	struct Motion
	{
		std::shared_ptr<const c5r::unitanim::FightMotion> motion;
		double t = 0;              // seconds from the fight's start
		bool ended = false;        // the fight ended: the walk back
		bool settled = false;      // a placement found every figure in its slot
		uint64_t tSeq = 0;         // the frame seq whose step t already holds
		uint64_t placedSeq = 0;    // the frame seq of its last placement (0: never)
		bool walked = false;       // ticket 4742: a placement after the fight's end still moved a figure (residualWalks)
	};
	void setMotion(int32_t player, int32_t id, std::shared_ptr<const c5r::unitanim::FightMotion> m, double t, bool ended);
	void dropMotion(int32_t player, int32_t id);
	bool returningKey(int64_t key) const;
	std::map<int64_t, Motion> m_motions;                         // issue 4724: per unit
	std::map<int64_t, c5r::units::PlacementInput> m_placeIn;     // issue 4724: the last drawn placement input of a fighter
	std::unique_ptr<c5r::units::PlacementSource> m_modulePlace;  // issue 4724
	std::unique_ptr<c5r::unitanim::FightPlacement> m_fightPlace; // issue 4724
	std::unique_ptr<c5r::units::PlacementSource> m_motionPlace;  // issue 4724: the source the renderer calls
	c5r::units::FormationTable m_formations;                     // issue 4724
	bool m_motionTried = false;                                  // issue 4724
	std::string m_motionWhy;                                     // issue 4724
	const c5r::heightfield::HeightGrid* m_grid = nullptr;        // issue 4724: this frame's (setGrid)
	uint32_t m_width = 0;                                        // issue 4724: the map's width (plot index -> x, y)
	uint64_t m_seq = 0;                                          // issue 4724: the last applied frame's seq
	std::vector<std::string> m_motionLoud;                       // issue 4724: logged at the next apply
	MotionCounters m_mc;                                         // issue 4724
	AnimFeed* m_feed = nullptr;           // issue 4774
	std::set<int64_t> m_fightFlags;       // issue 4774: the units whose 'held in a fight' field this runner set
	c5r::unitanim::CombatArtTables m_combatTables;   // issue 4774
	bool m_combatOk = false;              // issue 4774
	c5r::unitanim::CombatTablesInfo m_combatInfo;   // 4783 (W33, C1)
	std::string m_combatWhy;              // issue 4774
	c5r::unitanim::ClipGraphFactory::Durations m_fightDurs;   // issue 4774: clip durations the fights asked for
	std::unique_ptr<AnimFightPlayer> m_fightPlayer;           // issue 4774
	bool m_chargeAny = true;                                  // ticket 4737 (setChargeAny); true again since 4743 W81
	bool m_turnFirst = true;                                  // ticket 4735 (setTurnFirst)
	bool m_fightDrawn = true;                                 // ticket 4744 (setFightDrawnFigures)
	bool m_reformFirst = true;                                // tickets 4745 + 4747 (setReformFirst)
	bool m_walkHome = true;                                   // ticket 4742 (setWalkHome)
	bool m_cityInPlace = true;                                // ticket 4748 (setCityInPlace)
	bool m_readyAfterMove = true;                             // ticket 4746 (setReadyAfterMove)
	bool m_defendFromStance = true;                           // ticket 4750 (setDefendFromStance)
	bool m_reformEndsAtArrival = true;                        // ticket 5050 (setReformEndsAtArrival; on since W74)
	bool m_attackFromStance = true;                           // ticket 5050 (setAttackFromStance; on since W74)
	bool m_rushRideThrough = true;                            // ticket 5057 (setRushRideThrough)
	bool m_defenderWaitsFirstLanding = true;                  // ticket 5057 (setDefenderWaitsFirstLanding)
	bool m_deathByVariant = true;                             // ticket 5053 (setDeathByVariant; on, MC 2026-11-25)
	bool m_endAfterDying = true;                              // ticket 5053 (setEndAfterDying; on, MC 2026-11-25)
	bool m_meleeReach = true;                                 // ticket 0743 (setMeleeReach; on)
	bool m_defenderStep = true;                               // ticket 5061 (setDefenderStep; on)
	bool m_victoryNeedsWord = true;                           // ticket 5059 (setVictoryNeedsWord)
	bool m_type2Phase2 = true;                                // ticket 5067 S1 (setType2Phase2; on, MC #12907)
	bool m_cityShot = true;                                   // ticket 5062 (setCityShot)
	const UnitsCore* m_eraCore = nullptr;                     // ticket 5062 (useCore): the city owner's era
	std::vector<std::string> m_cityTags;                      // ticket 5062: Eras.CityBombardEffectTag by ID ("" = NULL)
	uint64_t m_cityShotsNamed = 0, m_cityShotsUnnamed = 0;    // ticket 5062
	std::string m_lastCityShot;                               // ticket 5062
	uint64_t m_cityCentres = 0, m_cityNoCentre = 0;           // ticket 5062 (W107)
	std::vector<std::string> m_cityLines;                     // ticket 5062 (W107): plain lines, the scene says them
	bool m_walkLog = false;                                   // 0685 (diagnostic): C5_RENDER_UNITS_WALK_LOG=1
	std::vector<std::string> m_walkLines;                     // 0685 (diagnostic): the scene says them
	bool m_open = false;
	std::unique_ptr<c5r::units::GameFilesProvider> m_provider;
	std::unique_ptr<c5m::StringDatabase> m_strings;
	std::unique_ptr<c5r::units::ModulePose> m_module;
	std::unique_ptr<c5r::unitanim::ControllerPose> m_pose;
	std::unique_ptr<c5r::unitanim::ResolverArtSource> m_art;
	std::unique_ptr<c5r::unitanim::ClipGraphFactory> m_graphs;
	c5r::unitanim::BridgeTables m_tables;
	std::unique_ptr<c5r::unitanim::ActionBridge> m_bridge;
	c5r::unitanim::Controller m_ctl;
	std::map<int32_t, std::string> m_builds;
	std::set<int64_t> m_failed;   // units whose failure was counted (an episode ends when the unit is Ok or gone)
	size_t m_blends = 0;
	WalkFacings m_walkFacings;   // issue 4772
	std::map<int64_t, float> m_walkRates;   // 0685: a unit's turn rate (turns per second) by UnitKeyOf, read once
	uint64_t m_walkYawPlaced = 0;           // 0783: placements turned to a walk's drawn yaw (MotionPlacement)
	c5r::unitanim::QuickCombat m_quick;     // ticket 0796 (setQuickCombat; off)
public:
	uint64_t walkYawPlaced() const { return m_walkYawPlaced; }   // 0783
};

// The timeline as a file (TEST ONLY render_test_units_anim, and unitfeed's timeline.c5anim): the frames applied so far,
// players by their first record, scene "-" (every unit is a create event), seed 0, mode free.
class AnimRecord
{
public:
	void add(const c5r::unitanim::TimelineFrame& f, const std::vector<c5r::units::PlayerInput>& players);
	bool dirty() const { return m_dirty; }
	size_t frames() const { return m_tl.frames.size(); }
	// writeTimelineFile into path.tmp, then a replacing rename. False with why (the writer's refusal or the file).
	bool write(const std::wstring& path, std::string& why);

private:
	c5r::unitanim::Timeline m_tl;
	std::set<int32_t> m_players;
	bool m_dirty = false;
};

}  // namespace srvunits
