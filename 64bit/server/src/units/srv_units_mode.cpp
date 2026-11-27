// srv_units_mode.cpp - the server's writer of the mode word's unit bit (epic 0040, issue 4753). The rule is in
// srv_units_mode.h; research/05-graphics-composition/unit-server-scene.md 8.1.
#include "srv_units_mode.h"
#include <cstdarg>
#include <cstdio>

namespace srvunits {

namespace {
const char* const kPhaseNames[] = {"off", "starting", "loading", "live", "lost", "failed"};

// issue 4761: VPFILTER_R_* (shell_shared.h); the compositor's lines name them alike (unit_filter.cpp)
const char* const kFilterReasons[VPFILTER_R_COUNT] = {"no reason given",
                                                      "path 2 refused the EXE's renderer",
                                                      "path 2 not run: the compositor refused the EXE image",
                                                      "an exception in a detour turned the filter off",
                                                      "no DROP program recognised",
                                                      "unit_filter=off",
                                                      "the test table was refused",
                                                      "the table of marked objects is full",
                                                      "no draw reached the filter's hooks"};
const char* FilterReason(uint32_t r) { return r < VPFILTER_R_COUNT ? kFilterReasons[r] : "an unknown reason"; }

std::unique_ptr<ModeWriter> MakeWriter() { return std::unique_ptr<ModeWriter>(new ModeWriter()); }
struct Registrar
{
	Registrar() { RegisterModeWriterFactory(&MakeWriter); }
} s_registrar;
}  // namespace

const char* ModeWriter::phaseName() const { return kPhaseNames[(int)m_phase]; }

void ModeWriter::say(const char* fmt, ...)
{
	if(!m_log)
		return;
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	m_log(m_logCtx, line);
}

void ModeWriter::configure(bool on, uint32_t pid, int64_t qpcFreq, LogFn log, void* logCtx)
{
	m_on = on;
	m_pid = pid;
	m_freq = qpcFreq > 0 ? qpcFreq : 1;
	m_log = log;
	m_logCtx = logCtx;
	m_phase = on ? kStarting : kOff;
	m_bit = 0;
	m_transFrom = 0;
	m_transFromLoad = false;
	m_haveFirstFrame = false;
	m_transFrames = 0;
	m_lostAt = 0;
	m_lostStepMs = 0;
	m_stepMs = 0;   // ticket 0641
	m_lastStepQpc = 0;
	m_notExeSaid = false;
	m_failed = false;
	m_failWhy.clear();
	m_c = ModeCounters();
	m_shadowBit = false;   // ticket 4785: follows m_bit (0); wanted and withdrawn are kept
	m_maskBaseKnown = false;
	if(on)
		say("units mode: the unit bit's writer is on (render_units_ready_hold_ms=%.0f of step time, a step adding at most %.0f ms; "
		    "render_units_positions=%s)",
		    m_holdMs, kUnitsStepGapMs, m_sourceExe ? "exe" : "own");
}

// Ticket 0641: the step clock (the models' rule, cities/srv_models_mode.cpp): a step adds the wall time since the previous
// one, at most kUnitsStepGapMs; a longer gap is a stall of the server's own loop (counted, not LOUD).
void ModeWriter::step(int64_t qpc)
{
	if(m_lastStepQpc)
	{
		double gap = ms(m_lastStepQpc, qpc);
		if(gap < 0)
			gap = 0;
		if(gap > m_c.maxStepGapMs)
			m_c.maxStepGapMs = gap;
		if(gap > kUnitsStepGapMs)
		{
			m_c.stalls++;
			gap = kUnitsStepGapMs;
		}
		m_stepMs += gap;
	}
	if(qpc > m_lastStepQpc)
		m_lastStepQpc = qpc;
}

void ModeWriter::write(uint32_t bit, int64_t qpc)
{
	if(bit == m_bit)
		return;
	m_bit = bit;
	if(bit)
		m_c.sets++;
	else
		m_c.clears++;
	m_c.changes++;
	writeWord(qpc, true);
}

// The producer word (ticket 4785: bits 0 and 4 together; bit 4 = the unit bit AND wanted AND not withdrawn). force: write
// even when the word would not change (the unit bit's own changes, as before 4785)
void ModeWriter::writeWord(int64_t qpc, bool force)
{
	const bool sh = m_bit && m_shadowWanted && !m_shadowWithdrawn;
	if(sh != m_shadowBit)
	{
		m_shadowBit = sh;
		if(sh)
		{
			m_c.shadowSets++;
			m_maskBaseKnown = false;   // the compositor's passedSetUnitMask is read afresh once it latches both bits
		}
		else
			m_c.shadowClears++;
	}
	if(!m_block)
		return;
	// the producer part only, single stores in the contract's order (shell_shared.h: writerPid before word); bits 1-3 are
	// kept (their writers are 0037 / 0300, in this server), bits 5-31 written 0
	const uint32_t keep = m_block->word & (VPMODE_KNOWN & ~(VPMODE_UNITS | VPMODE_UNIT_SHADOWS));
	const uint32_t w = keep | (m_bit ? VPMODE_UNITS : 0u) | (m_shadowBit ? VPMODE_UNIT_SHADOWS : 0u);
	if(!force && w == m_block->word)
		return;
	if(m_block->writerPid != m_pid)
		m_block->writerPid = m_pid;
	m_block->word = w;
	m_block->changedQpc = qpc;
	m_block->changes = m_block->changes + 1;
}

void ModeWriter::setUnitShadow(bool wanted, int64_t qpc)
{
	if(wanted == m_shadowWanted)
		return;
	m_shadowWanted = wanted;
	say("units mode: the server's unit shadows are %s (bit 4 follows the unit bit%s)", wanted ? "ready" : "off",
	    m_shadowWithdrawn ? "; withdrawn for this session" : "");
	if(m_on && !m_failed)
		writeWord(qpc);
}

void ModeWriter::checkMaskDrop(int64_t qpc)
{
	if(!m_block || !m_shadowBit)
		return;
	const uint32_t latched = m_block->latchedWord;
	const uint32_t passed = m_block->passedSetUnitMask;
	m_c.lastPassedSetMask = passed;
	if((latched & (VPMODE_UNITS | VPMODE_UNIT_SHADOWS)) != (VPMODE_UNITS | VPMODE_UNIT_SHADOWS))
		return;
	if(!m_maskBaseKnown)
	{
		m_maskBaseKnown = true;   // the first read with both bits latched: the count before them is not ours
		m_maskPassedAtSet = passed;
		return;
	}
	if(passed == m_maskPassedAtSet)
		return;
	m_shadowWithdrawn = true;
	m_c.shadowDoubled++;
	say("LOUD: units: mode: the compositor passed %u unit shadow-mask draws while it latched VPMODE_UNITS and VPMODE_UNIT_SHADOWS "
	    "(it could not drop them): bit 4 withdrawn for this session, the EXE's units cast their shadows again (stock's shadows; "
	    "the server's would double them)",
	    passed - m_maskPassedAtSet);
	writeWord(qpc);
}

void ModeWriter::onLoadBegin(int64_t qpc)
{
	if(!m_on || m_failed)
		return;
	m_c.loads++;
	if(m_phase == kLost)
	{
		m_c.lossesEndedByLoad++;
		say("LOUD: units: mode: a new load began %.0f ms of step time (%.0f ms wall) into a readiness loss; the loss ends without "
		    "the fatal step (%llu so far)",
		    m_stepMs - m_lostStepMs, ms(m_lostAt, qpc), (unsigned long long)m_c.lossesEndedByLoad);
	}
	write(0, qpc);
	m_phase = kLoading;
	m_transFrom = qpc;
	m_transFromLoad = true;
	m_haveFirstFrame = false;
	m_transFrames = 0;
	say("units mode: load %llu begins: unit bit 0 until the load's first ready frame", (unsigned long long)m_c.loads);
}

void ModeWriter::loseReadiness(int64_t qpc, const char* where)
{
	write(0, qpc);
	m_c.losses++;
	if(m_block)
		m_block->readyLossTotal = m_block->readyLossTotal + 1;
	m_lostAt = qpc;
	m_lostStepMs = m_stepMs;
	m_phase = kLost;
	say("LOUD: units: mode: the unit readiness was lost while the unit bit was set (%s): bit 0, the EXE's units are drawn; the "
	    "server ends unless it is back within %.0f ms of step time (a stall of the server counts at most 250 ms) (%llu so far)",
	    where, m_holdMs, (unsigned long long)m_c.losses);
}

void ModeWriter::checkBound(int64_t qpc)
{
	if(m_phase != kLost)
		return;
	const double lost = m_stepMs - m_lostStepMs;   // ticket 0641: step time
	if(lost <= m_holdMs)
		return;
	m_phase = kFailed;
	m_failed = true;
	m_c.fatal++;
	char why[512];
	snprintf(why, sizeof(why),
	         "units: the unit readiness was lost in play for %.0f ms of frames that ran (%.0f ms wall; a stall of the server counts "
	         "at most %.0f ms), beyond render_units_ready_hold_ms=%.0f: the server cannot draw the units (D118)",
	         lost, ms(m_lostAt, qpc), kUnitsStepGapMs, m_holdMs);
	m_failWhy = why;
	say("LOUD: %s", why);
}

void ModeWriter::checkFilter(int64_t qpc)
{
	if(!m_block)
		return;
	const uint32_t latched = m_block->latchedWord;
	const uint32_t state = m_block->filterState;
	m_c.lastFilterState = state;
	if(m_phase != kLive || !m_bit || !(latched & VPMODE_UNITS) || (state & VPFILTER_STATE_MASK) != VPFILTER_CANNOT_DROP)
		return;
	const uint32_t reason = (state & VPFILTER_REASON_MASK) >> VPFILTER_REASON_SHIFT;
	// the bit is cleared (the filter drops nothing anyway; between the failure and the popup only the EXE's units are meant
	// to be on screen, D118's pass-through), then the D125 step
	write(0, qpc);
	m_phase = kFailed;
	m_failed = true;
	m_c.fatal++;
	m_c.filterFatal++;
	char why[512];
	snprintf(why, sizeof(why),
	         "units: the compositor's unit filter cannot drop the EXE's pass-0 unit draws (%s; filterState 0x%X) while it has "
	         "latched the unit bit: units drawn twice (D118)",
	         FilterReason(reason), state);
	m_failWhy = why;
	say("LOUD: %s", why);
}

void ModeWriter::onPoll(bool ready, int64_t qpc)
{
	if(!m_on || m_failed)
		return;
	step(qpc);   // ticket 0641
	if(m_phase == kLive && !ready)
		loseReadiness(qpc, "between frames");
	checkBound(qpc);
	if(!m_failed)
		checkFilter(qpc);
	if(!m_failed)
		checkMaskDrop(qpc);   // ticket 4785
}

void ModeWriter::onFrame(bool ready, int64_t qpc)
{
	if(!m_on || m_failed)
		return;
	step(qpc);   // ticket 0641
	checkFilter(qpc);   // issue 4761: before the frame's own rule, with the bit as the compositor may have latched it
	if(m_failed)
		return;
	checkMaskDrop(qpc);   // ticket 4785
	const bool exe = m_sourceExe;
	switch(m_phase)
	{
	case kStarting:
	case kLoading:
		if(!m_haveFirstFrame)
		{
			m_haveFirstFrame = true;
			m_firstFrame = qpc;
			if(!m_transFromLoad)
				m_transFrom = qpc;
		}
		if(!ready)
		{
			m_transFrames++;
			return;
		}
		if(!exe)
		{
			m_c.notExeFrames++;
			if(!m_notExeSaid)
			{
				m_notExeSaid = true;
				say("units mode: the unit scene is ready but the position source is own (render_units_positions=own): the unit bit "
				    "stays 0");
			}
			return;
		}
		write(1, qpc);
		m_c.transitions++;
		m_c.lastTransitionMs = ms(m_transFrom, qpc);
		if(m_c.lastTransitionMs > m_c.maxTransitionMs)
			m_c.maxTransitionMs = m_c.lastTransitionMs;
		m_c.lastTransitionFrameMs = ms(m_firstFrame, qpc);
		m_c.lastTransitionFrames = m_transFrames;
		m_phase = kLive;
		say("units mode: start-up transition %llu ended: unit bit 1 after %.0f ms since the %s (%.0f ms and %llu not-ready frames "
		    "since the first drawn frame)",
		    (unsigned long long)m_c.transitions, m_c.lastTransitionMs, m_transFromLoad ? "load's kLoadBegin" : "first drawn frame",
		    m_c.lastTransitionFrameMs, (unsigned long long)m_c.lastTransitionFrames);
		return;
	case kLive:
		if(!ready)
		{
			loseReadiness(qpc, "at a drawn frame");
			return;
		}
		if(!exe)
		{
			// the source left exe while live (setOptions at a new device with another value; tests): not a loss; back to the
			// start-up rule
			write(0, qpc);
			m_phase = kStarting;
			m_transFromLoad = false;
			m_haveFirstFrame = false;
			m_transFrames = 0;
			say("units mode: the position source is no longer exe: unit bit 0");
		}
		return;
	case kLost:
		checkBound(qpc);
		if(m_phase != kLost || !ready)
			return;
		m_c.recovered++;
		m_c.lastLossMs = m_stepMs - m_lostStepMs;   // ticket 0641: step time
		if(m_c.lastLossMs > m_c.maxLossMs)
			m_c.maxLossMs = m_c.lastLossMs;
		m_c.lastLossWallMs = ms(m_lostAt, qpc);
		if(m_c.lastLossWallMs > m_c.maxLossWallMs)
			m_c.maxLossWallMs = m_c.lastLossWallMs;
		if(exe)
		{
			write(1, qpc);
			m_phase = kLive;
		}
		else
		{
			m_phase = kStarting;
			m_transFromLoad = false;
			m_haveFirstFrame = false;
			m_transFrames = 0;
		}
		say("LOUD: units: mode: the unit readiness is back after %.0f ms of step time (%.0f ms wall; bound %.0f ms of step time): "
		    "unit bit %u (%llu of %llu losses recovered)",
		    m_c.lastLossMs, m_c.lastLossWallMs, m_holdMs, m_bit, (unsigned long long)m_c.recovered,
		    (unsigned long long)m_c.losses);
		return;
	default:
		return;
	}
}

void ModeWriter::appendLine(char* out, size_t n) const
{
	size_t used = 0;
	while(used < n && out[used])
		used++;
	if(used + 1 >= n)
		return;
	snprintf(out + used, n - used,
	         " | mode bit=%u phase=%s src=%s hold_ms=%.0f losses=%llu recovered=%llu loss_ended_by_load=%llu last_loss_ms=%.0f "
	         "max_loss_ms=%.0f fatal=%llu loads=%llu transitions=%llu last_transition_ms=%.0f transition_frame_ms=%.0f "
	         "transition_frames=%llu not_exe_frames=%llu changes=%llu filter=0x%X filter_fatal=%llu shadow_bit=%u shadow_wanted=%u "
	         "shadow_sets=%llu shadow_clears=%llu shadow_doubled=%llu passed_set_mask=%u max_loss_wall_ms=%.0f step_ms=%.0f "
	         "stalls=%llu max_gap_ms=%.0f",
	         m_bit, phaseName(), m_sourceExe ? "exe" : "own", m_holdMs, (unsigned long long)m_c.losses,
	         (unsigned long long)m_c.recovered, (unsigned long long)m_c.lossesEndedByLoad, m_c.lastLossMs, m_c.maxLossMs,
	         (unsigned long long)m_c.fatal, (unsigned long long)m_c.loads, (unsigned long long)m_c.transitions, m_c.lastTransitionMs,
	         m_c.lastTransitionFrameMs, (unsigned long long)m_c.lastTransitionFrames, (unsigned long long)m_c.notExeFrames,
	         (unsigned long long)m_c.changes, m_c.lastFilterState, (unsigned long long)m_c.filterFatal, m_shadowBit ? 1u : 0u,
	         m_shadowWanted ? 1u : 0u, (unsigned long long)m_c.shadowSets, (unsigned long long)m_c.shadowClears,
	         (unsigned long long)m_c.shadowDoubled, m_c.lastPassedSetMask, m_c.maxLossWallMs, m_stepMs,
	         (unsigned long long)m_c.stalls, m_c.maxStepGapMs);
}

}  // namespace srvunits
