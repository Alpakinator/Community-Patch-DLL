// srv_units_mode.h - the server's writer of the mode word's unit bit (epic 0040, issue 4753).
//
// research/05-graphics-composition/unit-server-scene.md 8.1 ("0040: the mode word"), unit-draw-filter.md 2.2 (the block's
// layout and owners), research/07-architecture-decisions/epic-0040-breakdown.md 1.3, 1.5, O5, O6 and 11.
//
// The block is VPShellMode at VPSHELL_MODE_OFFSET of the main section (shell/common/shell_shared.h, issue 4751). This writer
// owns its producer part only: word (bit 0 VPMODE_UNITS, bit 4 VPMODE_UNIT_SHADOWS since ticket 4785 (setUnitShadow); bits
// 1-3 are kept as they are, bits 5-31 are written 0),
// writerPid, changes, readyLossTotal, changedQpc. It never writes the consumer part (latchedWord .. passedSetTotal,
// filterState) or the reserved words; it reads latchedWord and filterState (issue 4761). Stores are single 32-bit stores (changedQpc one aligned 64-bit store), in this order at a change of the
// word: writerPid (when it is not already this process's pid), word, changedQpc, changes.
//
// The rule (all calls on the render thread):
//   - The bit is 1 only while the unit scene is on (configure(true, ...): render_units=1), Scene::unitsReady() holds and the
//     position source is exe (setOptions(..., sourceExe): server.cfg render_units_positions=exe, parsed by the host since
//     4753, default own; issue 4755 builds the behaviour behind it, so until then the product keeps own and never sets the
//     bit). In every other state it is 0, and with render_units=0 nothing is written at all.
//   - It is set only at a drawn frame (onFrame) whose readiness holds; it is cleared at once by onFrame or onPoll (every
//     loop iteration) when the readiness is lost, and by onLoadBegin (the feed's kLoadBegin) at a new load.
//   - Start-up transition (breakdown O6, allowed): from a load's kLoadBegin (or the first drawn frame, when no load was seen)
//     to the first ready frame of that load the bit is 0 and the EXE draws its units. Its length is logged and counted when
//     the bit is set; no bound applies to it.
//   - A readiness lost while the bit was 1 (breakdown 1.5, O5): the bit is cleared at once and readyLossTotal counted. Back
//     within holdMs (server.cfg render_units_ready_hold_ms; the compositor's hold has no time limit, unit-draw-filter.md
//     2.4): counted and LOUD, the bit set again at that ready frame. Not back within holdMs: failed() is true from then on
//     with the text for HostConfig::fatal(5, ...); the host's UnitsFatalStep takes it once (D125's route, D118). A load
//     that begins during a loss ends the loss (counted, LOUD, not fatal).
//     Ticket 0641 (2026-11-21): holdMs counts STEP TIME, the models' rule (cities/srv_models_mode.h): every onPoll and
//     onFrame is a step that adds the wall time since the previous step, at most kUnitsStepGapMs (250 ms, ticket 4730's
//     rule), so a stall of the server's own render thread (0637's run: 4.8 s in one terrain bake) costs at most 250 ms; the
//     default is 30000 ms of step time (was 2000 ms of wall time, O5's 2 s). While the bit is 0 the compositor passes the
//     EXE's own unit draws (stock's figures), so a long wait shows the player the stock units.
//   - The compositor's filter cannot drop (issue 4761, D118): the writer reads the consumer word filterState (it never writes
//     it) in every onPoll and onFrame. While it holds the bit (phase live) and the compositor has latched it (latchedWord
//     has VPMODE_UNITS), a filterState of VPFILTER_CANNOT_DROP is fatal at once: the EXE's units and the server's would
//     both be drawn for the rest of the game, and a filter that cannot drop does not recover, so the hold bound does not
//     apply (well within render_units_ready_hold_ms). failed() is true with "units: the compositor's unit filter cannot
//     drop ... (<reason>) ...: units drawn twice (D118)". 0 (not reported: no compositor, an older one) and 1 (can drop)
//     never act, nor does any state while the bit is 0 or not latched.
#pragma once
#include "shell_shared.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace srvunits {

// render_units_ready_hold_ms: ms of step time (ticket 0641: 30 s of frames that ran; was 2000 ms of wall time, breakdown 11,
// O5's 2 s); kUnitsStepGapMs: the most wall time one step adds (the models' kModelsStepGapMs, 4730's kStepGapMs)
const double kUnitsReadyHoldMsDefault = 30000.0;
const double kUnitsStepGapMs = 250.0;

struct ModeCounters
{
	uint64_t changes = 0;            // changes of the word written by this writer
	uint64_t sets = 0, clears = 0;   // of the unit bit
	uint64_t losses = 0;             // readiness lost while the bit was 1 (= what this writer added to readyLossTotal)
	uint64_t recovered = 0;          // losses back within the bound (LOUD, not fatal)
	uint64_t lossesEndedByLoad = 0;  // losses that a new load ended (LOUD, not fatal)
	uint64_t fatal = 0;              // losses beyond the bound (0 or 1: the server ends)
	uint64_t filterFatal = 0;        // issue 4761: the compositor's filter could not drop with the bit latched (0 or 1)
	uint32_t lastFilterState = 0;    // issue 4761: filterState as last read (0 = not reported, or no block)
	double lastLossMs = -1, maxLossMs = -1;   // length of the recovered losses (step time, ticket 0641)
	double lastLossWallMs = -1, maxLossWallMs = -1;   // the same in wall time (ticket 0641)
	uint64_t stalls = 0;             // ticket 0641: steps whose gap was capped at kUnitsStepGapMs
	double maxStepGapMs = 0;         // ticket 0641: the longest gap between two steps
	uint64_t loads = 0;              // kLoadBegin seen
	uint64_t transitions = 0;        // start-up transitions ended by a set bit (breakdown O6)
	double lastTransitionMs = -1, maxTransitionMs = -1;   // kLoadBegin (or the first frame) -> the first ready frame
	double lastTransitionFrameMs = -1;   // the load's first drawn frame -> the first ready frame (O6's measure)
	uint64_t lastTransitionFrames = 0;   // drawn frames of the load before the first ready one
	uint64_t notExeFrames = 0;       // ready frames that left the bit 0 because the position source is not exe (own)
	// ticket 4785: bit 4 VPMODE_UNIT_SHADOWS
	uint64_t shadowSets = 0, shadowClears = 0;   // of bit 4 (written only together with the unit bit)
	uint64_t shadowDoubled = 0;      // bit 4 withdrawn because the compositor passed mask draws under both bits (0 or 1)
	uint32_t lastPassedSetMask = 0;  // passedSetUnitMask as last read
};

// The out-of-line members are virtual so that srv_render_scene.cpp, which every render program links (the render host
// tests, the offline player and the scene tests do not link server/src/units/), calls them without a link-time reference
// to this file: the scene makes its writer through MakeModeWriter (the factory pattern of MakeSceneUnits), and a program
// without server/src/units/ gets none (it cannot have a unit scene either: Scene::configureUnits refuses render_units=1).
class ModeWriter
{
public:
	typedef void (*LogFn)(void* ctx, const char* line);
	virtual ~ModeWriter() {}

	// on = render_units=1 (off: the writer never writes and every call is a no-op); pid the server's pid (writerPid); qpcFreq
	// the QPC frequency of the clock the calls pass (> 0); log (may be null) gets every note and LOUD line. Resets the state
	// and the counters (the options are kept).
	virtual void configure(bool on, uint32_t pid, int64_t qpcFreq, LogFn log, void* logCtx);
	// holdMs: render_units_ready_hold_ms (<= 0: the default); sourceExe: render_units_positions=exe. Keeps the state (the
	// scene calls it at every init, i.e. at every device). Read at the next call.
	void setOptions(double holdMs, bool sourceExe)
	{
		m_holdMs = holdMs > 0 ? holdMs : kUnitsReadyHoldMsDefault;
		m_sourceExe = sourceExe;
	}
	bool sourceExe() const { return m_sourceExe; }
	// Ticket 4785: whether the server casts the units' shadows (render_units_shadow=1 and its mask path ready: the scene's
	// UnitShadow initialised, the shadow tap read). Bit 4 VPMODE_UNIT_SHADOWS is written as (unit bit) AND wanted AND not
	// withdrawn; a change of wanted while the unit bit is set rewrites the word at once (the compositor latches it at its next
	// Present and flags the shadow-tap slots it copies under it). Withdrawn for the rest of the session (LOUD, counted, not
	// fatal: the EXE's units cast again, stock's shadows) when the compositor's passedSetUnitMask grows while its latch has
	// both bits: it could not drop the EXE's mask draws, and the shadows would be cast twice. Kept over configure().
	virtual void setUnitShadow(bool wanted, int64_t qpc);   // virtual: srv_render_scene.cpp calls it (the class comment below)
	bool unitShadowBit() const { return m_shadowBit; }
	// The section's mode block (VPSHELL_MODE(header)); null: detached, the state is kept but nothing is written.
	void attach(VPShellMode* block) { m_block = block; }

	virtual void onLoadBegin(int64_t qpc);             // the feed's kLoadBegin (Scene::unitFeed)
	virtual void onFrame(bool ready, int64_t qpc);     // after each drawn frame (Scene::draw, after the units' scene pass)
	virtual void onPoll(bool ready, int64_t qpc);      // every loop iteration (Scene::poll): clears on a loss, checks the bound

	// True once a loss outlasted the bound, or the compositor's filter could not drop with the bit latched (4761); why = the
	// text for HostConfig::fatal(5, why) ("units: ...").
	bool failed(std::string& why) const
	{
		if(m_failed)
			why = m_failWhy;
		return m_failed;
	}
	bool on() const { return m_on; }
	uint32_t bit() const { return m_bit; }
	virtual const char* phaseName() const;
	double holdMs() const { return m_holdMs; }
	const ModeCounters& counters() const { return m_c; }
	// " | mode bit=.. phase=.. ..." appended to out (a C string): the scene's "units 1s" line and "units mode totals" line
	virtual void appendLine(char* out, size_t n) const;

	enum Phase { kOff = 0, kStarting, kLoading, kLive, kLost, kFailed };
	Phase phase() const { return m_phase; }

private:
	void write(uint32_t bit, int64_t qpc);
	void say(const char* fmt, ...);
	double ms(int64_t from, int64_t to) const { return (double)(to - from) * 1000.0 / (double)m_freq; }
	void loseReadiness(int64_t qpc, const char* where);
	void step(int64_t qpc);   // ticket 0641: the step clock (every onPoll and onFrame)
	void checkBound(int64_t qpc);
	void checkFilter(int64_t qpc);   // issue 4761
	void checkMaskDrop(int64_t qpc);   // ticket 4785
	void writeWord(int64_t qpc, bool force = false);   // ticket 4785: the producer word from m_bit and bit 4's rule

	VPShellMode* m_block = nullptr;
	bool m_on = false;
	double m_holdMs = kUnitsReadyHoldMsDefault;
	bool m_sourceExe = false;
	uint32_t m_pid = 0;
	int64_t m_freq = 1;
	LogFn m_log = nullptr;
	void* m_logCtx = nullptr;

	Phase m_phase = kOff;
	uint32_t m_bit = 0;
	int64_t m_transFrom = 0;       // the transition's start (kLoadBegin, or the first drawn frame)
	bool m_transFromLoad = false;
	bool m_haveFirstFrame = false;
	int64_t m_firstFrame = 0;      // the load's first drawn frame (m_haveFirstFrame)
	uint64_t m_transFrames = 0;
	int64_t m_lostAt = 0;
	double m_lostStepMs = 0;      // ticket 0641: the step clock at the loss
	double m_stepMs = 0;          // ticket 0641: the step clock
	int64_t m_lastStepQpc = 0;
	bool m_notExeSaid = false;
	bool m_failed = false;
	std::string m_failWhy;
	bool m_shadowWanted = false;     // ticket 4785
	bool m_shadowWithdrawn = false;
	bool m_shadowBit = false;        // bit 4 as written
	uint32_t m_maskPassedAtSet = 0;  // passedSetUnitMask when bit 4 was last latched with both bits
	bool m_maskBaseKnown = false;
	ModeCounters m_c;
};

// The factory: srv_units_mode.cpp registers it at static initialisation; RegisterModeWriterFactory and MakeModeWriter are
// defined in srv_render_scene.cpp (always linked). Null without server/src/units/.
typedef std::unique_ptr<ModeWriter> (*ModeWriterFactory)();
void RegisterModeWriterFactory(ModeWriterFactory f);
std::unique_ptr<ModeWriter> MakeModeWriter();

}  // namespace srvunits
