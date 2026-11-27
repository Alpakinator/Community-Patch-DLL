// srv_units_fights.h - the server's fight clock (epic 0042, issue 4782, route S: D210; research/05-graphics-composition/
// unit-combat-server.md 4 "The fight's clock (4782)"; the rules from combat-choreography.md 3-4, 8, 12 and the Architect's
// route S note, research/07-architecture-decisions/architect-2026-11-20-route-s-combat.md 1.5 and 3.2).
//
// The clock decides when each visualised fight of the DLL starts and ends on the server, and emits OwnerEnd records:
//   - fights start in the DLL's call order (the records' seq order; add() refuses a seq that does not increase);
//   - a fight waits while an earlier fight (playing or still waiting) names one of its units or its city: per unit the
//     DLL's order is kept, other units' fights play at the same time (0321 3.2, 3.4);
//   - a barrier fight plays alone: it waits for every earlier fight, and every later fight waits for it (0321 3.3: the
//     human watches the AI turns, kCombatGameRead set and kCombatActiveTurn clear; or a nuclear attack);
//   - a fight starts at the earliest in the frame after the frame that ended the fight it waited for (the EXE's queue
//     moves the waiting record in the next pump, 0321 4.2; recorded: Run 0.03 s after the previous End, 0477's AI turn);
//   - its end is the player's (the 4775 choreography's own end), never later than the limit: 12 s, 20 s when the plan
//     names an aircraft (0321 4.2, 13.2). No pacing or catch-up rule of the server's own: under route S the EXE waits on
//     the hold of the fight's RunCombatSim record (4773), which the shell drops at the fight's OwnerEnd;
//   - OwnerEnd for each fight (owner = the EXE's event id of its RunCombatSim record, which the DLL keeps as its parent
//     id: CombatRecord::returned with kCombatReturnedKnown), and for each clip instance of a fighting figure that ends or
//     is cut during the fight (note 1.5 item 2). At a fight's own OwnerEnd its figures' open clip instances are covered
//     by the fight's (their owner ends); they get no record of their own then.
// A combat with Quick Combat on, or not visualised, is no fight (0321 4.1, 7): nothing is queued and no OwnerEnd sent.
// A fight the player refuses (since 4777 the choreography plays the city, air, nuclear and area fights; it refuses bad
// input, a unit side without figures and an unknown kind) ends in the frame it starts, with its OwnerEnd, counted
// and LOUD through the caller (D118: named, never a default). Nothing throws. One thread (the render thread).
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "feed/srv_feed.h"

namespace srvunits {

const double kFightLimit = 12.0;      // seconds (0321 4.2)
const double kFightLimitAir = 20.0;   // with an air figure (0321 13.2)

enum class FightEnd : uint8_t
{
	Natural = 0,   // the player reported the choreography's end
	Limit,         // the 12 s / 20 s limit
	Refused,       // the player refused the fight (its text names why)
	Gone,          // unitGone(): a unit of the fight left the scene
	Reset,         // reset(): a load or a reset of the unit table
	ClipEnded,     // a clip instance of a fighting figure ended or was cut (a record with figure and instance)
};
const char* fightEndName(FightEnd e);

struct FightSide
{
	int32_t owner = -1, unit = -1, city = -1;   // unit -1 for a city side
};

struct Fight
{
	uint32_t seq = 0;            // the combat event's seq (the DLL's order)
	uint32_t owner = 0;          // the EXE's event id (CombatRecord::returned); 0 when not known
	bool ownerKnown = false;
	bool barrier = false;
	SrvFeed::CombatRecord rec;   // as recorded
	std::vector<SrvFeed::CombatMember> members;   // as recorded (issue 4726: the interceptor's flagged member included)
	FightSide attacker, defender;
	FightSide interceptor;       // issue 4726: the member flagged SrvFeed::kCombatMemberInterceptor (unit -1: none)
	int32_t attackerCityPlot = -1;   // ticket 5062: the attacking city's plot index (kCityCombat's a[3] - 1); -1 not known
	// set by the clock
	bool started = false;
	uint64_t startFrame = 0;
	double elapsed = 0;          // seconds since its start frame
	double limit = kFightLimit;
	bool refused = false;        // the player refused it; it ends in its start frame
	std::string refusal;
	// ticket 0796 (quick_combat.h): the playback rate of this frame's step (FightPlayer::rate; 1 = real time) and the
	// real seconds since the start frame. elapsed is the fight's own time: it grows by dt x rate (= dt at rate 1)
	double rate = 1.0;
	double wall = 0;
};

// What the clock asks of the choreography (the 4775 core through the controller, or a test double).
struct FightPlan
{
	bool ok = false;
	std::string why;             // when !ok: the refusal (the core's CombatStatus text)
	bool air = false;            // an air figure takes part: the 20 s limit
};
class FightPlayer
{
public:
	virtual ~FightPlayer() {}
	// The fight starts now (its first frame). Not ok: it is refused and ends in this frame.
	virtual FightPlan start(const Fight& f) = 0;
	// The fight advances by dt seconds (its elapsed time is f.elapsed after the step). True when the choreography has
	// ended by itself. Called once per frame from the frame after its start.
	virtual bool advance(const Fight& f, double dt) = 0;
	// The fight ends (any FightEnd but ClipEnded); the player drops its state.
	virtual void end(const Fight& f, FightEnd why) { (void)f; (void)why; }
	// Issue 4783: a started fight's event id became known late (FightClock::setOwner): f.owner and f.ownerKnown are set;
	// what the player keeps of the owner (the trigger stream's fight reference) takes it now.
	virtual void ownerKnown(const Fight& f) { (void)f; }
	// Ticket 0796: the playback rate of the step about to be taken (asked before advance, f.elapsed is the time so far;
	// 1 = real time, the default). The clock then advances f.elapsed, the limit test and the player by dt x rate.
	virtual double rate(const Fight& f) { (void)f; return 1.0; }
};

// One OwnerEnd for the trigger stream (note 1.3): stamped with the frame whose insertion it belongs to.
struct OwnerEnd
{
	uint64_t frame = 0;
	uint32_t owner = 0;          // the fight's event id (0: not known; seq names the fight then)
	uint32_t seq = 0;
	int32_t player = -1, unit = -1, figure = -1;   // a clip instance's figure (-1: the fight itself)
	uint64_t instance = 0;       // the clip instance (0: the fight itself)
	FightEnd why = FightEnd::Natural;
	double elapsed = 0;          // the fight's seconds at the end
	std::string text;            // Refused: the player's text (the caller logs it LOUD)
};

// A fighting figure's current clip instance, as the controller holds it in a frame (FigureProgram::instance).
struct FigureInstance
{
	int32_t player = -1, unit = -1, figure = -1;
	uint64_t instance = 0;
};

struct FightCounters
{
	uint64_t records = 0;        // combat records added
	uint64_t noFight = 0;        // of them, Quick Combat or not visualised: no fight
	uint64_t fights = 0;         // queued
	uint64_t started = 0, barriers = 0;
	uint64_t natural = 0, limit = 0, refused = 0, gone = 0, reset = 0;
	uint64_t clipEnds = 0;       // OwnerEnd records of clip instances
	uint64_t ownerUnknown = 0;   // fights whose event id was not known (kCombatReturnedKnown clear) when added
	uint64_t ownerLate = 0;      // issue 4783: of them, the event id set later by setOwner (queued or playing)
	uint64_t ownerLateNoFight = 0;   // setOwner for a seq the clock does not hold (no fight, or it has ended)
	uint64_t ownerZeroEnds = 0;  // a fight's own OwnerEnd sent with its event id still not known (the shell cannot pass it)
	uint64_t refusedRecords = 0; // add() refusals (seq order, kind)
	uint64_t resumedBelow = 0;   // ticket 0742: resumed records taken below the last seq (they waited behind a walk)
	uint64_t badFrames = 0;      // frame() refusals (dt, frame order)
	uint32_t maxQueued = 0, maxPlaying = 0;
};

class FightClock
{
public:
	explicit FightClock(FightPlayer& player) : player_(player) {}

	// A drained combat record and its damage members, in the DLL's order. False with `why` for a record out of order (seq
	// not above the last one added) or of an unknown kind; nothing is queued then.
	// Ticket 5062: attackerCityPlot = the attacking city's plot index (kCityCombat's a[3] - 1; -1 not known), kept in
	// Fight::attackerCityPlot for a record whose attacker is a city.
	// Ticket 0742: resumed = a record that waited behind its attacker's open walk (5066 (a), AnimFeed's after-walk wait) and
	// comes back now. A later combat of other units may have been added meanwhile, so it may be below the last seq: it is
	// taken then (counted in resumedBelow) unless a fight of its seq is still held or it equals the last seq (a repeat).
	// Before 0742 it was refused with no fight and no OwnerEnd, and the shell's hold waited for its outer bound (22 s),
	// which disarms route S for the rest of the game (unit-combat-server.md 18.43, the owner's beta 4 report).
	bool add(const SrvFeed::CombatRecord& r, const SrvFeed::CombatMember* members, uint32_t nMembers, std::string& why,
	         int32_t attackerCityPlot = -1, bool resumed = false);

	// One server frame (id F, increasing) of dt seconds: (1) fights that may start start (their plan is asked; a refusal
	// ends them at once), (2) every fight that started in an earlier frame advances by dt and ends when the player says
	// so or at its limit, (3) the figures' clip instances are compared with `figures` (the fighting units' figures in
	// this frame; null = not given, no clip records). The OwnerEnd records of the frame are appended to `out`. False with
	// `why` for a dt that is not finite or negative, or a frame id not above the last; nothing changes then.
	bool frame(uint64_t f, double dt, const std::vector<FigureInstance>* figures, std::vector<OwnerEnd>& out,
	           std::string& why);

	// A unit left the scene (its removal reached the server unheld): every fight naming it ends now (OwnerEnd Gone,
	// stamped with the last frame), queued ones included.
	void unitGone(int32_t owner, int32_t unit, std::vector<OwnerEnd>& out);
	// A load or reset: every fight, queued or playing, ends with OwnerEnd Reset (the EXE's holds must be dropped).
	void reset(std::vector<OwnerEnd>& out);
	// Issue 4783 (W33 #8801): the EXE's event id of fight `seq` came after its record was drained (SrvFeed::Batch::
	// lateResults). A fight the clock holds without a known id (queued or playing) takes it: owner and ownerKnown are set,
	// a started fight's player is told (FightPlayer::ownerKnown), and its later OwnerEnds (its own and its clip instances')
	// carry it. Applied: set now; Known: the fight's id was already known (nothing changes); NoFight: no fight of that seq
	// is held (Quick Combat or not visualised, refused, or already ended: an end already sent keeps owner 0).
	enum class OwnerSet : uint8_t { Applied, Known, NoFight };
	OwnerSet setOwner(uint32_t seq, uint32_t owner);

	size_t queued() const;
	size_t playing() const;
	// True while a fight names this unit (playing or queued): the unit is held in a fight (4981's flag, set by 4774).
	bool inFight(int32_t owner, int32_t unit) const;
	// Ticket 0796: the highest playback rate of a started fight naming the unit in the last frame (1 when none).
	double unitRate(int32_t owner, int32_t unit) const;
	const FightCounters& counters() const { return c_; }

private:
	bool shares(const Fight& a, const Fight& b) const;
	void finish(size_t index, FightEnd why, uint64_t frame, std::vector<OwnerEnd>& out);

	FightPlayer& player_;
	std::deque<Fight> fights_;   // in the DLL's order; started ones first among their peers
	uint32_t lastSeq_ = 0;
	bool haveSeq_ = false;
	uint64_t lastFrame_ = 0;
	bool haveFrame_ = false;
	std::map<std::tuple<int32_t, int32_t, int32_t>, std::pair<uint32_t, uint64_t>> clips_;   // figure -> (seq, instance)
	FightCounters c_;
	std::vector<std::pair<std::pair<int32_t, int32_t>, double>> rated_;   // ticket 0796: the last frame's (unit, rate) != 1
};

}  // namespace srvunits
