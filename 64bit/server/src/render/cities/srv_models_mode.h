// srv_models_mode.h - the server's writer of the mode word's model bits VPMODE_CITIES (bit 1) and VPMODE_LANDMARKS
// (bit 2) (epic 0561, ticket 4875; research/05-graphics-composition/city-renderer.md 3.3, 3.4 and 11). Written 2026-11-18
// for this repository (GPL-3.0-or-later), by the pattern of server/src/units/srv_units_mode.h (epic 0040, issue 4753).
//
// The block is VPShellMode (shell/common/shell_shared.h). The units writer (srv_units_mode.cpp) owns the producer part
// (writerPid, changes, readyLossTotal, changedQpc) and keeps bits 1-3 as they are; this writer changes only bits 1 and 2
// of word, on the render thread (the units writer's thread: Scene::poll and Scene::draw), with the units writer's store
// order at a change: writerPid (only when it is not already this process's pid: a server without render_units still
// needs it, the compositor uses the word only from its latched server), word (bit 0 and bit 3 as they are, bits 4-31 0),
// changedQpc, changes + 1. It never writes readyLossTotal, the consumer part or the reserved words; it reads latchedWord
// and filterState2 (ticket 4874). Lead2-0561 #6137 and MC #6134: srv_units_mode is not changed.
//
// The rule per bit (each bit on its own; all calls on the render thread):
//   - on = its option (render_cities / render_landmarks = 1, written by the proxy only with ServerRenderer = 1). Off: the
//     bit is never set and nothing is written for it.
//   - ready = the module's readiness for that bit (ModelsScene: library loaded, the identity set published and applied by
//     the compositor with VPMSET_OK, the renderer made, keys of the bit in the set; to set a bit, the generation applied
//     must be the one published last); fresh = lastFrameQpc within noFrameMs of now, where ModelsScene passes the last
//     time the tap was RUNNING with every published frame read and accepted (the compositor publishes only while
//     something is tapped, so a view without models stays fresh). The bit is set at a drawn frame (onFrame) where ready
//     and fresh hold.
//   - Start-up: from configure (or a load's kLoadBegin) until the first set, the EXE draws the models; no bound, except
//     the install bound installMs (0 = none): the bit not set within it is an install failure (failed(), the D125 step),
//     because an option the player turned on must not silently stay off. Ticket 4885 (2026-11-19; was "within installMs
//     of the load's first drawn frame", which a disk-bound open of 18-32 s turned into the fatal step though nothing had
//     failed, 4878's live run e1): the install bound counts from the later of the load's first drawn frame and the end
//     of the model layer's open (onOpenEnd), and does not run while the open runs (onOpenBegin .. onOpenEnd). Before the
//     open begins (no game database copy yet) it counts from the load's first drawn frame as before. The open itself is
//     bounded by its own failure (onOpenEnd(false): the D125 step naming the open), by no progress (onOpenProgress)
//     within openStallMs (render_models_open_stall_ms, 0 = none), and by a ceiling openMaxMs from its begin
//     (render_models_open_max_ms, 0 = none) for an open that keeps making "progress" forever.
//   - Live: readiness lost (not ready) or no accepted frame within noFrameMs -> the bit cleared at once (the EXE draws
//     that class again), phase lost. Back (ready and fresh) within holdMs of the loss -> set again, LOUD and counted. Not
//     back within holdMs -> failed(): HostConfig::fatal(5, ...) (D118, D125).
//   - Ticket 0641 (2026-11-21): noFrameMs and holdMs count STEP TIME, never wall time across a stall of the server itself.
//     Every onPoll and onFrame is a step; a step adds the wall time since the previous step, at most kModelsStepGapMs
//     (250 ms, ticket 4730's rule for route S's arming window): while the render loop runs (16-250 ms per step) step time
//     equals wall time, and a stall of any length (0637's run: 4.8 s of the render thread in one terrain bake, then
//     2 drawn frames in 2.4 s) costs at most 250 ms. Freshness is the step time since the last NEW accepted-frame stamp
//     (lastFrameQpc greater than any seen; at its first sight its wall age counts in full). The hold's default is 30 s of
//     step time (at least 120 steps, so it still ends when every step is a stall): while a bit is lost the compositor
//     passes the EXE's own draws of that class (the stock picture), so waiting costs the player nothing visible.
//   - The compositor's filter cannot drop (D118 per bit, filterState2): while this writer holds the bit and latchedWord
//     has it, the bit's half-word state VPFILTER_CANNOT_DROP is fatal at once (the class would be drawn twice); the bit is
//     cleared first. 0 (not reported) and 1 (can drop) never act.
//   - A load (kLoadBegin) clears both bits and ends a loss without the fatal step (counted).
#pragma once
#include "shell_shared.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace srvmodels {

const double kModelsNoFrameMsDefault = 500.0;     // render_models_no_frame_ms: no accepted tap frame this long = bit cleared
// render_models_hold_ms: a loss this long (step time) = the fatal step. Ticket 0641: 30 s of frames that ran (was 2000 ms of
// wall time, O5's 2 s, which a 2.4 s stall of the server's own render thread turned into the end of a game)
const double kModelsHoldMsDefault = 30000.0;
const double kModelsStepGapMs = 250.0;            // ticket 0641: the most wall time one step adds (4730's kStepGapMs)
const double kModelsInstallMsDefault = 30000.0;   // render_models_install_ms: the bit never set this long = install failure
// ticket 4885: the model layer's open (library, art catalogue, wire index, programs, inputs: 14-32 s measured live on
// slow disks, city-renderer.md 11.13). No progress this long = the open hangs; the ceiling catches an open that never ends
const double kModelsOpenStallMsDefault = 60000.0;    // render_models_open_stall_ms
const double kModelsOpenMaxMsDefault = 600000.0;     // render_models_open_max_ms

// ticket 4885: the model layer's open as the writer saw it (one per process: the layer opens once)
struct ModelOpenCounters
{
	enum State { kNotBegun = 0, kRunning, kOpen, kFailed };
	State state = kNotBegun;
	int64_t beginQpc = 0, lastProgressQpc = 0, endQpc = 0;
	uint64_t progress = 0;      // onOpenProgress calls
	double openMs = -1;         // begin -> end
	double maxGapMs = 0;        // the longest time without progress while it ran
	uint64_t stallFatal = 0, maxFatal = 0, failFatal = 0;
};

struct ModelBitCounters
{
	uint64_t sets = 0, clears = 0;
	uint64_t losses = 0, lossesReady = 0, lossesStale = 0;   // all, by readiness, by no frame within noFrameMs
	uint64_t recovered = 0, lossesEndedByLoad = 0;
	uint64_t fatal = 0, filterFatal = 0, installFatal = 0;
	uint32_t lastFilterHalf = 0;   // the bit's half-word of filterState2 as last read
	double lastLossMs = -1, maxLossMs = -1;           // recovered losses, step time (ticket 0641)
	double lastLossWallMs = -1, maxLossWallMs = -1;   // the same losses in wall time (ticket 0641)
	double lastTransitionMs = -1;  // the load's first drawn frame -> the bit set
};

class ModelModeWriter
{
public:
	typedef void (*LogFn)(void* ctx, const char* line);
	enum Phase { kOff = 0, kStarting, kLive, kLost, kFailed };
	static const int kBits = 2;   // index 0 = VPMODE_CITIES, 1 = VPMODE_LANDMARKS
	static uint32_t BitOf(int i) { return i == 0 ? VPMODE_CITIES : VPMODE_LANDMARKS; }
	static const char* BitName(int i) { return i == 0 ? "cities" : "landmarks"; }
	static const char* PhaseName(Phase p);

	// on[i]: render_cities / render_landmarks; pid: this server (writerPid); freq the QPC frequency. Resets the state and
	// the counters.
	void configure(bool cities, bool landmarks, uint32_t pid, int64_t qpcFreq, LogFn log, void* logCtx);
	void setBounds(double noFrameMs, double holdMs, double installMs);
	// ticket 4885: the open's bounds (< 0 = the default, 0 = none)
	void setOpenBounds(double stallMs, double maxMs);
	// Takes the mode block and, when a bit's option is on, claims it: writerPid = pid (ticket 4879's live smoke of 4875,
	// 2026-11-18: the compositor reads wantBits and the identity set only from the live server, whose liveness needs
	// writerPid; without the units writer nothing else writes it).
	void attach(VPShellMode* block);

	void onLoadBegin(int64_t qpc);
	// ticket 4885, the model layer's open (render thread; the module samples the worker): begun; progress seen since the
	// last call (the worker ran); ended (ok, or failed with its reason: the D125 step naming the open). The open's state
	// is the layer's, not the load's: configure and onLoadBegin keep it.
	void onOpenBegin(int64_t qpc);
	void onOpenProgress(int64_t qpc);
	void onOpenEnd(bool ok, const char* why, int64_t qpc);
	// ready / fresh per bit (index 0 cities, 1 landmarks), after each drawn frame (Scene::draw)
	void onFrame(const bool ready[kBits], int64_t lastFrameQpc, int64_t qpc);
	// every loop iteration (Scene::poll): clears on a loss, checks the bounds and the filter
	void onPoll(const bool ready[kBits], int64_t lastFrameQpc, int64_t qpc);

	bool failed(std::string& why) const
	{
		if(m_failed)
			why = m_failWhy;
		return m_failed;
	}
	bool on(int i) const { return m_on[i]; }
	bool anyOn() const { return m_on[0] || m_on[1]; }
	uint32_t bits() const { return m_bits; }    // the bits this writer holds now
	Phase phase(int i) const { return m_phase[i]; }
	const ModelBitCounters& counters(int i) const { return m_c[i]; }
	const ModelOpenCounters& openCounters() const { return m_open; }
	uint64_t changes() const { return m_changes; }
	// ticket 0641: the step clock (ms of frames that ran), the steps whose gap was capped (stalls of the server's own loop)
	// and the longest gap seen
	double stepMs() const { return m_stepMs; }
	uint64_t stalls() const { return m_stalls; }
	double maxStepGapMs() const { return m_maxStepGapMs; }
	// " | models cities=.. landmarks=.." appended to out (a C string)
	void appendLine(char* out, size_t n) const;

private:
	void write(uint32_t bits, int64_t qpc);
	void say(const char* fmt, ...);
	double ms(int64_t from, int64_t to) const { return (double)(to - from) * 1000.0 / (double)m_freq; }
	// ticket 0641: one step of the step clock (every onPoll and onFrame), then the freshness in step time
	void step(int64_t lastFrameQpc, int64_t qpc);
	bool fresh() const { return m_seenFrameQpc != 0 && m_stepMs - m_freshStepMs <= m_noFrameMs; }
	double lostMs(int i) const { return m_stepMs - m_lostStepMs[i]; }   // the loss's step time
	void lose(int i, bool byReady, int64_t qpc, const char* where);
	void fail(int i, const char* why);
	void checkFilter(int64_t qpc);
	void checkBounds(int64_t qpc);
	bool checkOpen(int64_t qpc);         // ticket 4885: the open's bounds; true when it failed
	int firstOn() const { return m_on[0] ? 0 : 1; }

	VPShellMode* m_block = nullptr;
	bool m_on[kBits] = {false, false};
	uint32_t m_pid = 0;
	int64_t m_freq = 1;
	LogFn m_log = nullptr;
	void* m_logCtx = nullptr;
	double m_noFrameMs = kModelsNoFrameMsDefault, m_holdMs = kModelsHoldMsDefault, m_installMs = kModelsInstallMsDefault;
	double m_openStallMs = kModelsOpenStallMsDefault, m_openMaxMs = kModelsOpenMaxMsDefault;
	ModelOpenCounters m_open;

	Phase m_phase[kBits] = {kOff, kOff};
	uint32_t m_bits = 0;
	int64_t m_lostAt[kBits] = {0, 0};          // wall (the log's second number)
	double m_lostStepMs[kBits] = {0, 0};       // ticket 0641: the step clock at the loss
	// ticket 0641: the step clock
	double m_stepMs = 0;
	int64_t m_lastStepQpc = 0;
	uint64_t m_stalls = 0;
	double m_maxStepGapMs = 0;
	int64_t m_seenFrameQpc = 0;    // the newest accepted-frame stamp seen
	double m_freshStepMs = 0;      // the step clock at that stamp
	bool m_haveFirstFrame = false;
	int64_t m_firstFrame = 0;
	bool m_failed = false;
	std::string m_failWhy;
	uint64_t m_changes = 0;
	ModelBitCounters m_c[kBits];
};

}  // namespace srvmodels
