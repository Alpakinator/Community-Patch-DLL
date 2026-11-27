// srv_models_mode.cpp - the server's writer of the mode word's model bits (epic 0561, ticket 4875). The rule is in
// srv_models_mode.h; research/05-graphics-composition/city-renderer.md 11.
#include "srv_models_mode.h"
#include <cstdarg>
#include <cstdio>

namespace srvmodels {

namespace {
// VPFILTER_R_* and VPMFILTER_R_* (shell_shared.h); the compositor's lines name them alike
const char* const kFilterReasons[VPMFILTER_R_COUNT] = {"no reason given",
                                                       "path 2 refused the EXE's renderer",
                                                       "path 2 not run: the compositor refused the EXE image",
                                                       "an exception in a detour turned the filter off",
                                                       "no DROP program recognised",
                                                       "model_filter=off",
                                                       "the test table was refused",
                                                       "the table of marked objects is full",
                                                       "no draw reached the filter's hooks",
                                                       "no identity set of this server is applied",
                                                       "the model tap's sections could not be made or written"};
const char* FilterReason(uint32_t r) { return r < VPMFILTER_R_COUNT ? kFilterReasons[r] : "an unknown reason"; }
const char* const kPhaseNames[] = {"off", "starting", "live", "lost", "failed"};
}  // namespace

const char* ModelModeWriter::PhaseName(Phase p) { return (unsigned)p < 5 ? kPhaseNames[p] : "?"; }

void ModelModeWriter::say(const char* fmt, ...)
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

void ModelModeWriter::configure(bool cities, bool landmarks, uint32_t pid, int64_t qpcFreq, LogFn log, void* logCtx)
{
	m_on[0] = cities;
	m_on[1] = landmarks;
	m_pid = pid;
	m_freq = qpcFreq > 0 ? qpcFreq : 1;
	m_log = log;
	m_logCtx = logCtx;
	for(int i = 0; i < kBits; ++i)
	{
		m_phase[i] = m_on[i] ? kStarting : kOff;
		m_lostAt[i] = 0;
		m_lostStepMs[i] = 0;
		m_c[i] = ModelBitCounters();
	}
	// ticket 0641: the step clock starts again
	m_stepMs = 0;
	m_lastStepQpc = 0;
	m_stalls = 0;
	m_maxStepGapMs = 0;
	m_seenFrameQpc = 0;
	m_freshStepMs = 0;
	m_bits = 0;
	m_haveFirstFrame = false;
	m_firstFrame = 0;
	m_failed = false;
	m_failWhy.clear();
	m_changes = 0;
	// ticket 4885: m_open is the layer's (it opens once per process), so configure keeps it
	if(cities || landmarks)
		say("models mode: the model bits' writer is on (cities %d, landmarks %d; no_frame_ms=%.0f hold_ms=%.0f of step time, a "
		    "step adding at most %.0f ms; install_ms=%.0f from the later of the load's first drawn frame and the model layer's "
		    "open; open_stall_ms=%.0f open_max_ms=%.0f)",
		    cities ? 1 : 0, landmarks ? 1 : 0, m_noFrameMs, m_holdMs, kModelsStepGapMs, m_installMs, m_openStallMs, m_openMaxMs);
}

// Ticket 0641: the step clock. A step adds the wall time since the previous one, at most kModelsStepGapMs; a longer gap is a
// stall of the server's own loop (the compositor sees the server stale meanwhile and passes the EXE's draws itself), said
// for the first 20 and then every 100th (not LOUD: a loss has its own LOUD line). A NEW accepted-frame stamp (greater than every one seen) moves the freshness
// mark to the step clock less the stamp's wall age (0 in the product: ModelsScene stamps at the poll that passes it).
void ModelModeWriter::step(int64_t lastFrameQpc, int64_t qpc)
{
	if(m_lastStepQpc)
	{
		double gap = ms(m_lastStepQpc, qpc);
		if(gap < 0)
			gap = 0;
		if(gap > m_maxStepGapMs)
			m_maxStepGapMs = gap;
		if(gap > kModelsStepGapMs)
		{
			m_stalls++;
			if(m_stalls <= 20 || m_stalls % 100 == 0)
				say("models mode: the server's render loop did not run for %.0f ms (a stall of the server itself, %llu so "
				    "far): it counts %.0f ms toward the model bits' bounds",
				    gap, (unsigned long long)m_stalls, kModelsStepGapMs);
			gap = kModelsStepGapMs;
		}
		m_stepMs += gap;
	}
	if(qpc > m_lastStepQpc)
		m_lastStepQpc = qpc;
	if(lastFrameQpc && lastFrameQpc > m_seenFrameQpc)
	{
		m_seenFrameQpc = lastFrameQpc;
		const double age = ms(lastFrameQpc, qpc);
		m_freshStepMs = m_stepMs - (age > 0 ? age : 0);
	}
}

void ModelModeWriter::setBounds(double noFrameMs, double holdMs, double installMs)
{
	m_noFrameMs = noFrameMs > 0 ? noFrameMs : kModelsNoFrameMsDefault;
	m_holdMs = holdMs > 0 ? holdMs : kModelsHoldMsDefault;
	m_installMs = installMs >= 0 ? installMs : kModelsInstallMsDefault;
}

void ModelModeWriter::setOpenBounds(double stallMs, double maxMs)
{
	m_openStallMs = stallMs >= 0 ? stallMs : kModelsOpenStallMsDefault;
	m_openMaxMs = maxMs >= 0 ? maxMs : kModelsOpenMaxMsDefault;
}

// ---- ticket 4885: the model layer's open ----
void ModelModeWriter::onOpenBegin(int64_t qpc)
{
	if(m_open.state != ModelOpenCounters::kNotBegun)
		return;
	m_open.state = ModelOpenCounters::kRunning;
	m_open.beginQpc = qpc;
	m_open.lastProgressQpc = qpc;
	if(anyOn())
		say("models mode: the model layer's open begins: the install bound waits for its end (open_stall_ms=%.0f "
		    "open_max_ms=%.0f)",
		    m_openStallMs, m_openMaxMs);
}

void ModelModeWriter::onOpenProgress(int64_t qpc)
{
	if(m_open.state != ModelOpenCounters::kRunning)
		return;
	const double gap = ms(m_open.lastProgressQpc, qpc);
	if(gap > m_open.maxGapMs)
		m_open.maxGapMs = gap;
	m_open.lastProgressQpc = qpc;
	m_open.progress++;
}

void ModelModeWriter::onOpenEnd(bool ok, const char* why, int64_t qpc)
{
	if(m_open.state != ModelOpenCounters::kRunning && m_open.state != ModelOpenCounters::kNotBegun)
		return;
	if(m_open.state == ModelOpenCounters::kNotBegun)
		m_open.beginQpc = m_open.lastProgressQpc = qpc;
	const double gap = ms(m_open.lastProgressQpc, qpc);
	if(gap > m_open.maxGapMs)
		m_open.maxGapMs = gap;
	m_open.endQpc = qpc;
	m_open.openMs = ms(m_open.beginQpc, qpc);
	m_open.state = ok ? ModelOpenCounters::kOpen : ModelOpenCounters::kFailed;
	if(!anyOn())
		return;
	if(ok)
	{
		say("models mode: the model layer is open after %.0f ms (longest time without progress %.0f ms): the install bound "
		    "(%.0f ms) counts from here",
		    m_open.openMs, m_open.maxGapMs, m_installMs);
		return;
	}
	if(m_failed)
		return;
	char line[768];
	snprintf(line, sizeof(line),
	         "models: the model layer's open (library, art catalogue, wire index, programs) failed after %.0f ms: %s: the "
	         "server cannot draw the cities or landmarks (D118)",
	         m_open.openMs, why && *why ? why : "no reason given");
	m_open.failFatal++;
	fail(firstOn(), line);
}

bool ModelModeWriter::checkOpen(int64_t qpc)
{
	if(m_open.state != ModelOpenCounters::kRunning)
		return false;
	const double since = ms(m_open.beginQpc, qpc);
	const double gap = ms(m_open.lastProgressQpc, qpc);
	char line[768];
	if(m_openStallMs > 0 && gap > m_openStallMs)
	{
		snprintf(line, sizeof(line),
		         "models: the model layer's open (library, art catalogue, wire index, programs) made no progress for %.0f ms, "
		         "beyond render_models_open_stall_ms=%.0f (%.0f ms since it began, %llu progress samples): the open hangs; the "
		         "server cannot draw the cities or landmarks (D118)",
		         gap, m_openStallMs, since, (unsigned long long)m_open.progress);
		m_open.stallFatal++;
	}
	else if(m_openMaxMs > 0 && since > m_openMaxMs)
	{
		snprintf(line, sizeof(line),
		         "models: the model layer's open (library, art catalogue, wire index, programs) has not ended %.0f ms after it "
		         "began, beyond render_models_open_max_ms=%.0f (longest time without progress %.0f ms): the server cannot draw "
		         "the cities or landmarks (D118)",
		         since, m_openMaxMs, m_open.maxGapMs);
		m_open.maxFatal++;
	}
	else
		return false;
	fail(firstOn(), line);
	return true;
}

// Ticket 4879 (step 0, 4875's live smoke on c5h-c, 2026-11-18 [L]): the compositor treats the server as live only when the
// mode block's writerPid equals the server's pid (unit_filter.cpp's latch), and only a live server's wantBits and identity
// set are read. With ServerUnits = 0 nothing else writes writerPid, and write() stores it only at a change of the bits,
// which needs the compositor to have applied the set first: the bits never became ready and the install bound took the
// D125 step 30 s after the first drawn frame. So the writer claims the block as soon as it has one and an option is on
// (writerPid only; the word is written at the first change as before).
void ModelModeWriter::attach(VPShellMode* block)
{
	m_block = block;
	if(m_block && anyOn() && m_pid && m_block->writerPid != m_pid)
		m_block->writerPid = m_pid;
}

void ModelModeWriter::write(uint32_t bits, int64_t qpc)
{
	bits &= VPMODE_CITIES | VPMODE_LANDMARKS;
	if(bits == m_bits)
		return;
	for(int i = 0; i < kBits; ++i)
	{
		const uint32_t b = BitOf(i);
		if((bits & b) && !(m_bits & b))
			m_c[i].sets++;
		if(!(bits & b) && (m_bits & b))
			m_c[i].clears++;
	}
	m_bits = bits;
	m_changes++;
	if(!m_block)
		return;
	// the units writer's order (srv_units_mode.cpp): writerPid when not ours, word, changedQpc, changes; bit 0 (units) and
	// bit 3 (paths) as they are, bits 4-31 written 0
	if(m_block->writerPid != m_pid)
		m_block->writerPid = m_pid;
	const uint32_t keep = m_block->word & (VPMODE_KNOWN & ~(VPMODE_CITIES | VPMODE_LANDMARKS));
	m_block->word = keep | bits;
	m_block->changedQpc = qpc;
	m_block->changes = m_block->changes + 1;
}

void ModelModeWriter::fail(int i, const char* why)
{
	write(m_bits & ~BitOf(i), 0);
	m_phase[i] = kFailed;
	m_c[i].fatal++;
	if(!m_failed)
	{
		m_failed = true;
		m_failWhy = why;
	}
	say("LOUD: %s", why);
}

void ModelModeWriter::onLoadBegin(int64_t qpc)
{
	if(!anyOn() || m_failed)
		return;
	write(0, qpc);
	for(int i = 0; i < kBits; ++i)
	{
		if(!m_on[i])
			continue;
		if(m_phase[i] == kLost)
		{
			m_c[i].lossesEndedByLoad++;
			say("LOUD: models: %s: a new load began %.0f ms of step time (%.0f ms wall) into a loss; the loss ends without the "
			    "fatal step",
			    BitName(i), lostMs(i), ms(m_lostAt[i], qpc));
		}
		m_phase[i] = kStarting;
	}
	m_haveFirstFrame = false;
	say("models mode: a load begins: the model bits are 0 until the load's first ready frame");
}

void ModelModeWriter::lose(int i, bool byReady, int64_t qpc, const char* where)
{
	write(m_bits & ~BitOf(i), qpc);
	m_c[i].losses++;
	if(byReady)
		m_c[i].lossesReady++;
	else
		m_c[i].lossesStale++;
	m_lostAt[i] = qpc;
	m_lostStepMs[i] = m_stepMs;
	m_phase[i] = kLost;
	say("LOUD: models: %s: %s while the bit was set (%s): bit 0, the EXE draws them; the server ends unless it is back within "
	    "%.0f ms of step time (a stall of the server counts at most 250 ms) (%llu losses so far)",
	    BitName(i), byReady ? "the readiness was lost" : "no accepted tap frame", where, m_holdMs,
	    (unsigned long long)m_c[i].losses);
}

void ModelModeWriter::checkFilter(int64_t qpc)
{
	if(!m_block)
		return;
	const uint32_t latched = m_block->latchedWord;
	const uint32_t word2 = VPMODE_FILTERSTATE2(m_block);   // reserved[0] (shell_shared.h accessors, lead2-0561)
	for(int i = 0; i < kBits; ++i)
	{
		const uint32_t half = VPFILTER2_HALF(word2, i == 0 ? VPFILTER2_CITIES_SHIFT : VPFILTER2_LANDMARKS_SHIFT);
		m_c[i].lastFilterHalf = half;
		const uint32_t b = BitOf(i);
		if(m_phase[i] != kLive || !(m_bits & b) || !(latched & b) || (half & VPFILTER_STATE_MASK) != VPFILTER_CANNOT_DROP)
			continue;
		const uint32_t reason = (half & VPFILTER_REASON_MASK) >> VPFILTER_REASON_SHIFT;
		m_c[i].filterFatal++;
		char why[512];
		snprintf(why, sizeof(why),
		         "models: the compositor's model filter cannot drop the EXE's %s draws (%s; filterState2 0x%X) while it has "
		         "latched the %s bit: drawn twice (D118)",
		         BitName(i), FilterReason(reason), word2, BitName(i));
		(void)qpc;
		fail(i, why);
	}
}

void ModelModeWriter::checkBounds(int64_t qpc)
{
	if(checkOpen(qpc))
		return;
	// ticket 4885: the install bound does not run while the layer opens, and counts from the later of the load's first
	// drawn frame and the open's end (before the open begins: from the load's first drawn frame, the wait for the game
	// database copy)
	const bool opening = m_open.state == ModelOpenCounters::kRunning;
	int64_t installFrom = m_firstFrame;
	if(m_open.state == ModelOpenCounters::kOpen && m_open.endQpc > installFrom)
		installFrom = m_open.endQpc;
	for(int i = 0; i < kBits; ++i)
	{
		if(m_phase[i] == kLost && lostMs(i) > m_holdMs)
		{
			char why[512];
			snprintf(why, sizeof(why),
			         "models: %s: the readiness or the tap frames were lost in play for %.0f ms of frames that ran (%.0f ms wall; "
			         "a stall of the server counts at most %.0f ms), beyond render_models_hold_ms=%.0f: the server cannot draw them "
			         "(D118)",
			         BitName(i), lostMs(i), ms(m_lostAt[i], qpc), kModelsStepGapMs, m_holdMs);
			fail(i, why);
		}
		else if(m_phase[i] == kStarting && m_installMs > 0 && m_haveFirstFrame && !opening && ms(installFrom, qpc) > m_installMs)
		{
			char why[640];
			if(m_open.state == ModelOpenCounters::kOpen)
				snprintf(why, sizeof(why),
				         "models: %s: the option is on but the module was not ready within render_models_install_ms=%.0f of the "
				         "later of the load's first drawn frame and the model layer's open (open after %.0f ms; then the identity "
				         "set, the compositor's verdict, tap frames, renderer): the server cannot draw them (D118)",
				         BitName(i), m_installMs, m_open.openMs);
			else
				snprintf(why, sizeof(why),
				         "models: %s: the option is on but the model layer's open has not begun within "
				         "render_models_install_ms=%.0f of the load's first drawn frame (no game database copy, or no model "
				         "layer): the server cannot draw them (D118)",
				         BitName(i), m_installMs);
			m_c[i].installFatal++;
			fail(i, why);
		}
		if(m_failed)
			return;
	}
}

void ModelModeWriter::onPoll(const bool ready[kBits], int64_t lastFrameQpc, int64_t qpc)
{
	if(!anyOn() || m_failed)
		return;
	step(lastFrameQpc, qpc);   // ticket 0641
	const bool fr = fresh();
	for(int i = 0; i < kBits; ++i)
		if(m_phase[i] == kLive && (!ready[i] || !fr))
			lose(i, !ready[i], qpc, "between frames");
	checkBounds(qpc);
	if(!m_failed)
		checkFilter(qpc);
}

void ModelModeWriter::onFrame(const bool ready[kBits], int64_t lastFrameQpc, int64_t qpc)
{
	if(!anyOn() || m_failed)
		return;
	step(lastFrameQpc, qpc);   // ticket 0641
	checkFilter(qpc);
	if(m_failed)
		return;
	if(!m_haveFirstFrame)
	{
		m_haveFirstFrame = true;
		m_firstFrame = qpc;
	}
	const bool fr = fresh();
	uint32_t bits = m_bits;
	for(int i = 0; i < kBits; ++i)
	{
		const uint32_t b = BitOf(i);
		const bool ok = ready[i] && fr;
		switch(m_phase[i])
		{
		case kStarting:
			if(ok)
			{
				bits |= b;
				m_phase[i] = kLive;
				m_c[i].lastTransitionMs = ms(m_firstFrame, qpc);
				if(m_open.state == ModelOpenCounters::kOpen && m_open.endQpc > m_firstFrame)
					say("models mode: %s bit 1 after %.0f ms since the load's first drawn frame (%.0f ms after the model layer's "
					    "open)",
					    BitName(i), m_c[i].lastTransitionMs, ms(m_open.endQpc, qpc));
				else
					say("models mode: %s bit 1 after %.0f ms since the load's first drawn frame", BitName(i),
					    m_c[i].lastTransitionMs);
			}
			break;
		case kLive:
			if(!ok)
			{
				write(bits, qpc);
				lose(i, !ready[i], qpc, "at a drawn frame");
				bits = m_bits;
			}
			break;
		case kLost:
			if(lostMs(i) > m_holdMs)
				break;   // checkBounds ends it
			if(ok)
			{
				m_c[i].recovered++;
				m_c[i].lastLossMs = lostMs(i);
				if(m_c[i].lastLossMs > m_c[i].maxLossMs)
					m_c[i].maxLossMs = m_c[i].lastLossMs;
				m_c[i].lastLossWallMs = ms(m_lostAt[i], qpc);
				if(m_c[i].lastLossWallMs > m_c[i].maxLossWallMs)
					m_c[i].maxLossWallMs = m_c[i].lastLossWallMs;
				bits |= b;
				m_phase[i] = kLive;
				say("LOUD: models: %s: back after %.0f ms of step time (%.0f ms wall; bound %.0f ms of step time): bit 1 (%llu of "
				    "%llu losses recovered)",
				    BitName(i), m_c[i].lastLossMs, m_c[i].lastLossWallMs, m_holdMs, (unsigned long long)m_c[i].recovered,
				    (unsigned long long)m_c[i].losses);
			}
			break;
		default:
			break;
		}
	}
	write(bits, qpc);
	checkBounds(qpc);
}

void ModelModeWriter::appendLine(char* out, size_t n) const
{
	size_t used = 0;
	while(used < n && out[used])
		used++;
	for(int i = 0; i < kBits && used + 1 < n; ++i)
	{
		const ModelBitCounters& c = m_c[i];
		snprintf(out + used, n - used,
		         " | %s on=%d bit=%u phase=%s sets=%llu clears=%llu losses=%llu (ready %llu, stale %llu) recovered=%llu "
		         "by_load=%llu fatal=%llu filter_fatal=%llu install_fatal=%llu filter=0x%X max_loss_ms=%.0f max_loss_wall_ms=%.0f "
		         "transition_ms=%.0f",
		         BitName(i), m_on[i] ? 1 : 0, (m_bits & BitOf(i)) ? 1u : 0u, PhaseName(m_phase[i]), (unsigned long long)c.sets,
		         (unsigned long long)c.clears, (unsigned long long)c.losses, (unsigned long long)c.lossesReady,
		         (unsigned long long)c.lossesStale, (unsigned long long)c.recovered, (unsigned long long)c.lossesEndedByLoad,
		         (unsigned long long)c.fatal, (unsigned long long)c.filterFatal, (unsigned long long)c.installFatal,
		         c.lastFilterHalf, c.maxLossMs, c.maxLossWallMs, c.lastTransitionMs);
		while(used < n && out[used])
			used++;
	}
	// ticket 0641: the step clock
	if(anyOn() && used + 1 < n)
	{
		snprintf(out + used, n - used, " | step ms=%.0f stalls=%llu max_gap_ms=%.0f", m_stepMs, (unsigned long long)m_stalls,
		         m_maxStepGapMs);
		while(used < n && out[used])
			used++;
	}
	// ticket 4885: the model layer's open
	static const char* const kOpenStates[] = {"not_begun", "running", "open", "failed"};
	if(anyOn() && used + 1 < n)
		snprintf(out + used, n - used,
		         " | open state=%s open_ms=%.0f max_gap_ms=%.0f progress=%llu stall_fatal=%llu max_fatal=%llu fail_fatal=%llu",
		         kOpenStates[(unsigned)m_open.state < 4 ? m_open.state : 0], m_open.openMs, m_open.maxGapMs,
		         (unsigned long long)m_open.progress, (unsigned long long)m_open.stallFatal, (unsigned long long)m_open.maxFatal,
		         (unsigned long long)m_open.failFatal);
}

}  // namespace srvmodels
