// proxy_route_s.cpp - see proxy_route_s.h (ticket 4783, epic 0042, route S; MC ruling #8543). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No STL in the core (the proxy's _SECURE_SCL question, ticket 4725), no allocation on the frame path, no exception out
// of the hook. The glue's report thread includes the proxy's own headers first (4773's form).
#ifndef VP_ROUTE_S_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way (and _SECURE_SCL 0, 4725)
#include "proxy_rt_logic.h"     // ProxyLogic::Settings
#include "proxy_report.h"
#include "proxy_session.h"
#include "proxy_exe.h"          // epic 0590: the build table (cam_app; the glue only)
#include "proxy_combat_hold.h"
#include "proxy_sound_exec.h"
#include "proxy_effect_player.h"
#include "vp64_options.h"
#include <string>
#include <vector>
#endif
#include "proxy_route_s.h"
#include "vp_trigger.h"         // R9: the question and the answer in <base>.Trig (the core: its enum values only)

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ProxyRouteS
{
namespace
{
enum Answer
{
	kNotAsked = 0,
	kYes = 1,
	kNo = 2,
	kNoAnswer = 3
};

LogFn g_log = NULL;
ReportFn g_report = NULL;
DWORD TickClock() { return GetTickCount(); }
ClockFn g_clock = TickClock;
Parts g_parts;
bool g_haveParts = false;
uint32_t g_build = 0;
char g_label[160] = "an unknown build";
bool g_supported = false;

volatile LONG g_state = kIdle;
uint32_t g_gen = 0;
DWORD g_armStart = 0;
// The answer window in step time (4730): the wall clock at the last arming step, the window's time so far (each gap
// counted at most kStepGapMs), the longest gap; each part's answer time after the question (wall and step time).
DWORD g_lastStep = 0;
DWORD g_stepMs = 0;
DWORD g_maxGap = 0;
DWORD g_ansWall[kPartCount];
DWORD g_ansStep[kPartCount];
bool g_noAnswerByServer = false;   // the NO ANSWERs were set by the server's decision, not by the window
int g_ans[kPartCount];
char g_why[kPartCount][300];
bool g_combatOff = false;   // combat(true) succeeded while armed (set back at the fall-back)
volatile LONG g_reported = 0;
uint32_t g_wdIds[kMaxIds];
DWORD g_wdMs[kMaxIds];
bool g_wdStarted[kMaxIds];
int g_wdN = 0;
volatile LONG g_wdPending = 0;
long g_asks = 0, g_gens = 0, g_watchdogs = 0, g_faults = 0, g_notTaken = 0, g_reports = 0, g_disarms = 0;

const char* const kPartName[kPartCount] = {"combat hold", "sound executor", "effect player"};

void Log(bool loud, const char* fmt, ...)
{
	char line[1600];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	if (g_log) g_log(line, loud);
}

// The D118 report, once per EXE process, and only on a supported build (a refusal there is a fault).
void ReportOnce(const char* check, const char* line)
{
	if (!g_supported) return;
	if (InterlockedCompareExchange(&g_reported, 1, 0) != 0) return;
	++g_reports;
	if (g_report) g_report(check, line);
}

// Part p answered now (its first answer: armed and answering, or refused).
void NoteAnswer(int p)
{
	g_ansWall[p] = g_clock() - g_armStart;
	g_ansStep[p] = g_stepMs;
}

const char* AnswerText(int p, char* buf, size_t cap)
{
	switch (g_ans[p])
	{
	case kYes:
		_snprintf_s(buf, cap, _TRUNCATE, "%s armed (answered at %lu ms, %lu ms of step time)", kPartName[p],
		            (unsigned long)g_ansWall[p], (unsigned long)g_ansStep[p]);
		break;
	case kNo:
		_snprintf_s(buf, cap, _TRUNCATE, "%s REFUSED%s%s%s at %lu ms", kPartName[p], g_why[p][0] ? " (" : "", g_why[p],
		            g_why[p][0] ? ")" : "", (unsigned long)g_ansWall[p]);
		break;
	case kNoAnswer:
	{
		char when[64];
		if (g_noAnswerByServer)
			_snprintf_s(when, sizeof(when), _TRUNCATE, "before the server's decision");
		else
			_snprintf_s(when, sizeof(when), _TRUNCATE, "in the %lu ms window", (unsigned long)kAnswerMs);
		_snprintf_s(buf, cap, _TRUNCATE, "%s NO ANSWER %s (%lu ms of step time, %lu ms after the question, longest gap "
		                                 "between steps %lu ms)", kPartName[p], when, (unsigned long)g_stepMs,
		            (unsigned long)(g_clock() - g_armStart), (unsigned long)g_maxGap);
		break;
	}
	default: _snprintf_s(buf, cap, _TRUNCATE, "%s not asked", kPartName[p]); break;
	}
	return buf;
}

// The three parts' answers in one piece of the line.
void PartsText(char* out, size_t cap)
{
	char a[3][400];
	_snprintf_s(out, cap, _TRUNCATE, "%s, %s, %s", AnswerText(kHold, a[0], sizeof(a[0])),
	            AnswerText(kSound, a[1], sizeof(a[1])), AnswerText(kEffect, a[2], sizeof(a[2])));
}

// The bit back as config.ini has it (only when this module cleared it); the text says what the fights do now.
void CombatBack(char* text, size_t cap)
{
	text[0] = 0;
	if (!g_haveParts || !g_parts.combat)
	{
		_snprintf_s(text, cap, _TRUNCATE, "no Combat bit binding");
		return;
	}
	if (!g_parts.combat(g_parts.ctx, false, text, cap))
		Log(true, "LOUD: route S: the Combat bit could not be set back (%s): fights may run as with Combat off", text);
	g_combatOff = false;
}

void DisarmParts()
{
	if (!g_haveParts) return;
	g_parts.disarm(g_parts.ctx, kHold);      // no new hold
	g_parts.releaseHolds(g_parts.ctx);       // every hold dropped at the hold's next frame
	g_parts.disarm(g_parts.ctx, kSound);
	g_parts.disarm(g_parts.ctx, kEffect);
}

// A refusal or a missed answer before ARMED: none armed for this EXE process.
void Fallback(const char* check, const char* cause)
{
	DisarmParts();
	char combat[120];
	CombatBack(combat, sizeof(combat));
	InterlockedExchange(&g_state, kNotArmed);
	char parts[1300];
	PartsText(parts, sizeof(parts));
	char line[1800];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "LOUD: route S: NOT ARMED on %s: %s; none of the three is armed for this EXE process and fights play the "
	            "EXE's own way (Combat bit: %s): %s%s",
	            g_label, cause, combat, parts, g_supported ? "" : " (not a supported build: no report)");
	if (g_log) g_log(line, true);
	ReportOnce(check, line);
}

// After ARMED: the hold's watchdog, a part that stopped answering, the server's NOT TAKEN.
void DisarmAsOne(const char* check, const char* cause)
{
	DisarmParts();
	char combat[120];
	CombatBack(combat, sizeof(combat));
	InterlockedExchange(&g_state, kDisarmed);
	++g_disarms;
	char line[1800];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "LOUD: route S: DISARMED as one on %s for the rest of this EXE process: %s; every held fight released "
	            "now, the server sends no further record, fights play the EXE's own way (Combat bit: %s)%s",
	            g_label, cause, combat, g_supported ? "" : " (not a supported build: no report)");
	if (g_log) g_log(line, true);
	ReportOnce(check, line);
}

void StartArming()
{
	++g_asks;
	for (int p = 0; p < kPartCount; ++p)
	{
		g_ans[p] = kNotAsked;
		g_ansWall[p] = g_ansStep[p] = 0;
	}
	if (!g_haveParts)
	{
		Fallback("route-s-parts", "the parts are not bound");
		return;
	}
	InterlockedExchange(&g_state, kArming);
	g_armStart = g_clock();
	g_lastStep = g_armStart;
	g_stepMs = 0;
	g_maxGap = 0;
	g_noAnswerByServer = false;
	// The two readers first: they attach at their next frame; the hold only once both are attached. Their arm's answer
	// counts only when it is a refusal: an armed reader answers when it is attached (StepArming).
	g_ans[kSound] = g_parts.arm(g_parts.ctx, kSound) ? kNotAsked : kNo;
	g_ans[kEffect] = g_parts.arm(g_parts.ctx, kEffect) ? kNotAsked : kNo;
	NoteAnswer(kSound);
	NoteAnswer(kEffect);
	if (g_ans[kSound] == kNo || g_ans[kEffect] == kNo)
	{
		// the other reader's answer as before: armed (it is disarmed again by the fallback)
		if (g_ans[kSound] != kNo) g_ans[kSound] = kYes;
		if (g_ans[kEffect] != kNo) g_ans[kEffect] = kYes;
		Fallback(g_ans[kSound] == kNo ? "route-s-sound-exec" : "route-s-effect-player", "a part refused");
	}
}

// The window's clock (4730): the time since the last arming step, at most kStepGapMs of it.
void TickWindow()
{
	const DWORD now = g_clock();
	const DWORD gap = now - g_lastStep;
	g_lastStep = now;
	if (gap > g_maxGap) g_maxGap = gap;
	g_stepMs += gap < kStepGapMs ? gap : kStepGapMs;
}

// The readers' answers: each one's answer time is the first look that sees it attached.
void SeeReaders(bool* sound, bool* effect)
{
	*sound = g_parts.answering(g_parts.ctx, kSound);
	*effect = g_parts.answering(g_parts.ctx, kEffect);
	if (*sound && g_ans[kSound] != kYes)
	{
		g_ans[kSound] = kYes;
		NoteAnswer(kSound);
	}
	if (*effect && g_ans[kEffect] != kYes)
	{
		g_ans[kEffect] = kYes;
		NoteAnswer(kEffect);
	}
}

void StepArming()
{
	TickWindow();
	bool sound = false, effect = false;
	SeeReaders(&sound, &effect);
	if (sound && effect)
	{
		g_ans[kHold] = g_parts.arm(g_parts.ctx, kHold) && g_parts.answering(g_parts.ctx, kHold) ? kYes : kNo;
		NoteAnswer(kHold);
		if (g_ans[kHold] != kYes)
		{
			Fallback("route-s-combat-hold", "a part refused");
			return;
		}
		char combat[120] = "";
		if (!g_parts.combat(g_parts.ctx, true, combat, sizeof(combat)))
		{
			char cause[200];
			_snprintf_s(cause, sizeof(cause), _TRUNCATE, "the EXE's Combat bit cannot be cleared (%s)", combat);
			Fallback("route-s-combat-bit", cause);
			return;
		}
		g_combatOff = true;
		InterlockedExchange(&g_state, kArmed);
		char parts[1300];
		PartsText(parts, sizeof(parts));
		Log(false, "route S: ARMED on %s (decided once for this EXE process, %lu ms after the question, %lu ms of step time "
		           "of the %lu ms window, longest gap between steps %lu ms): %s; Combat bit %s",
		    g_label, (unsigned long)(g_clock() - g_armStart), (unsigned long)g_stepMs, (unsigned long)kAnswerMs,
		    (unsigned long)g_maxGap, parts, combat);
		return;
	}
	// 4730: step time, not wall time: a load stall between two steps counts at most kStepGapMs
	if (g_stepMs < kAnswerMs) return;
	if (!sound) g_ans[kSound] = kNoAnswer;
	if (!effect) g_ans[kEffect] = kNoAnswer;
	Fallback(!sound ? "route-s-sound-exec-no-answer" : "route-s-effect-player-no-answer", "a part did not answer in time");
}

void StepArmed()
{
	if (g_wdPending)
	{
		InterlockedExchange(&g_wdPending, 0);
		char ids[400] = "";
		size_t at = 0;
		for (int i = 0; i < g_wdN && at < sizeof(ids); ++i)
		{
			const int n = _snprintf_s(ids + at, sizeof(ids) - at, _TRUNCATE, "%s%lu after %lu ms (%s)", i ? ", " : "",
			                          (unsigned long)g_wdIds[i], (unsigned long)g_wdMs[i],
			                          g_wdStarted[i] ? "started by the server, its 22 s bound" : "never started, the outer bound");
			if (n < 0) break;
			at += (size_t)n;
		}
		char cause[600];
		_snprintf_s(cause, sizeof(cause), _TRUNCATE,
		            "the hold's watchdog dropped fight event id %s without the server's OwnerEnd (the last safety net)", ids);
		g_wdN = 0;
		DisarmAsOne("route-s-watchdog", cause);
		return;
	}
	for (int p = 0; p < kPartCount; ++p)
	{
		if (g_parts.answering(g_parts.ctx, p)) continue;
		++g_faults;
		char cause[500];
		_snprintf_s(cause, sizeof(cause), _TRUNCATE, "the %s stopped answering (%s)", kPartName[p],
		            g_why[p][0] ? g_why[p] : "no longer attached or active");
		DisarmAsOne(p == kHold ? "route-s-combat-hold-fault" : p == kSound ? "route-s-sound-exec-fault" :
		                                                                      "route-s-effect-player-fault",
		            cause);
		return;
	}
}

// ---- the Combat bit (guarded leaf functions without stack objects: SEH and MSVC ASan, rt_tests' 4855 note) -------------
const DWORD kCombatBit = 0x800;        // [DisableSystems] Combat (disable-systems-and-masks.md 2, row 11)
const DWORD kKnownBits = 0x00FFFFFF;   // the 24 [DisableSystems] keys (proxy_shell.cpp KNOWN_BITS)
int g_iniRead = -2;                    // SetIniMask: 1 config.ini read, 0 no config.ini (all set), -1 unreadable, -2 never
uint32_t g_iniMask = 0;
bool g_bitClearedByUs = false;         // the bit was set as found and this module cleared it

bool ReadDword(ULONG_PTR at, DWORD* out)
{
	__try
	{
		*out = *reinterpret_cast<const volatile DWORD*>(at);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}
bool AndOr(ULONG_PTR at, bool clear, DWORD bit)
{
	__try
	{
		// a compare-exchange loop (the VC9 headers have no InterlockedAnd / InterlockedOr for x86): only `bit` changes
		volatile LONG* p = reinterpret_cast<volatile LONG*>(at);
		LONG old = *p;
		for (;;)
		{
			const LONG want = clear ? (LONG)((DWORD)old & ~bit) : (LONG)((DWORD)old | bit);
			const LONG seen = InterlockedCompareExchange(p, want, old);
			if (seen == old) break;
			old = seen;
		}
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// MC #8582 condition 2: read before write. The value found must equal config.ini's mask (read once at the proxy's
// start) in all 24 known bits, Combat's included; anything else is a value the proxy does not recognise: no write, no
// arming.
bool LegalValue(DWORD v, char* text, size_t cap)
{
	if (g_iniRead < 0)
	{
		_snprintf_s(text, cap, _TRUNCATE, "config.ini's mask is not known (%s): the value cannot be checked, not written",
		            g_iniRead == -1 ? "config.ini could not be read" : "not read");
		return false;
	}
	const uint32_t ini = g_iniRead == 1 ? g_iniMask : 0xFFFFFFFFu;
	if (((v ^ ini) & kKnownBits) != 0)
	{
		_snprintf_s(text, cap, _TRUNCATE, "the app's DisableSystems mask differs from config.ini's in bits 0x%08lX: not "
		                                  "recognised, not written", (unsigned long)((v ^ ini) & kKnownBits));
		return false;
	}
	return true;
}
}  // namespace

void SetIniMask(int read, uint32_t mask)
{
	g_iniRead = read;
	g_iniMask = mask;
}

bool CombatClearedByUs() { return g_bitClearedByUs; }

bool CombatBit(ULONG_PTR field, bool off, char* text, size_t cap)
{
	if (!off && !g_bitClearedByUs)
	{
		_snprintf_s(text, cap, _TRUNCATE, g_iniRead == 1 && !(g_iniMask & kCombatBit) ?
		                                      "off as config.ini has it: nothing written, fights as with Combat off" :
		                                      "as config.ini has it: nothing written, stock fights");
		return true;
	}
	DWORD v = 0;
	if (!field || !ReadDword(field, &v))
	{
		_snprintf_s(text, cap, _TRUNCATE, "the app's DisableSystems mask cannot be read");
		return false;
	}
	if (off)
	{
		if (!LegalValue(v, text, cap)) return false;
		if (!(v & kCombatBit))
		{
			_snprintf_s(text, cap, _TRUNCATE, "already off (config.ini has Combat off): nothing written, fights as with "
			                                  "Combat off while armed or not");
			return true;
		}
		if (!AndOr(field, true, kCombatBit))
		{
			_snprintf_s(text, cap, _TRUNCATE, "the app's DisableSystems mask cannot be written");
			return false;
		}
		g_bitClearedByUs = true;
		_snprintf_s(text, cap, _TRUNCATE, "cleared at run time (config.ini has it on; set back as found at the disarm and "
		                                  "the unload)");
		return true;
	}
	// The restore: the value found at the arming had the bit set (only then was it cleared); the other bits are untouched.
	if (!AndOr(field, false, kCombatBit))
	{
		_snprintf_s(text, cap, _TRUNCATE, "the app's DisableSystems mask cannot be written");
		return false;
	}
	g_bitClearedByUs = false;
	_snprintf_s(text, cap, _TRUNCATE, "set back on as found at the arming: stock fights");
	return true;
}

bool CombatKeepOff(ULONG_PTR field)
{
	DWORD v = 0;
	if (!g_bitClearedByUs || !field || !ReadDword(field, &v) || !(v & kCombatBit)) return false;
	return AndOr(field, true, kCombatBit);
}

void SetSinks(LogFn log, ReportFn report, ClockFn clock)
{
	g_log = log;
	g_report = report;
	g_clock = clock ? clock : TickClock;
}

void Configure(const Parts& parts, uint32_t steamBuild, const char* buildLabel, bool supported)
{
	g_parts = parts;
	g_haveParts = parts.arm && parts.answering && parts.disarm && parts.releaseHolds && parts.combat;
	g_build = steamBuild;
	_snprintf_s(g_label, sizeof(g_label), _TRUNCATE, "%s", buildLabel ? buildLabel : "an unknown build");
	g_supported = supported;
}

void Ask(uint32_t gen)
{
	if (gen == 0) return;
	if (gen != g_gen)
	{
		g_gen = gen;
		++g_gens;
		if (g_state != kIdle)
			Log(false, "route S: generation %lu asks; the answer stays as decided for this EXE process (state %d)",
			    (unsigned long)gen, (int)g_state);
	}
	if (g_state == kIdle) StartArming();
}

void Step()
{
	if (g_state == kArming)
		StepArming();
	else if (g_state == kArmed)
		StepArmed();
	else if (g_wdPending)
		InterlockedExchange(&g_wdPending, 0);   // a hold armed alone (a TEST switch): the hold's own line says it
}

void ServerDecision(uint32_t taken)
{
	if (taken != VPTRIG_TAKEN_NO_ANSWER && taken != VPTRIG_TAKEN_REFUSED) return;
	if (g_state != kArming && g_state != kArmed) return;
	++g_notTaken;
	const char* cause = taken == VPTRIG_TAKEN_NO_ANSWER ? "the server did not take route S (no answer at its first fight record)"
	                                : "the server did not take route S (it read a refusal)";
	if (g_state == kArming)
	{
		bool sound = false, effect = false;
		SeeReaders(&sound, &effect);   // a reader attached at this frame answered (its time named)
		g_noAnswerByServer = true;
		for (int p = 0; p < kPartCount; ++p)
			if (g_ans[p] == kNotAsked) g_ans[p] = kNoAnswer;
		Fallback("route-s-not-taken", cause);
	}
	else
		DisarmAsOne("route-s-not-taken", cause);
}

void HoldWatchdog(uint32_t eventId, DWORD afterMs, bool started)
{
	++g_watchdogs;
	if (g_wdN < kMaxIds)
	{
		g_wdIds[g_wdN] = eventId;
		g_wdMs[g_wdN] = afterMs;
		g_wdStarted[g_wdN] = started;
		++g_wdN;
	}
	InterlockedExchange(&g_wdPending, 1);
}

void PartReport(int part, const char* check, const char* line)
{
	if (part < 0 || part >= kPartCount) return;
	_snprintf_s(g_why[part], sizeof(g_why[part]), _TRUNCATE, "%s: %s", check ? check : "?", line ? line : "");
	// A part armed alone (its TEST switch, no question from the server): its report as it was, once per process.
	if (g_state == kIdle) ReportOnce(check ? check : "?", line ? line : "");
}

State CurrentState() { return static_cast<State>(g_state); }

uint32_t AnswerState()
{
	switch (g_state)
	{
	case kArmed: return VPTRIG_ROUTES_ARMED;
	case kNotArmed: return VPTRIG_ROUTES_NOT_ARMED;
	case kDisarmed: return VPTRIG_ROUTES_DISARMED;
	default: return VPTRIG_ROUTES_NONE;
	}
}

uint32_t AnswerParts()
{
	uint32_t bits = 0;
	for (int p = 0; p < kPartCount; ++p)
	{
		if (g_ans[p] == kYes) bits |= 1u << p;
		if (g_ans[p] != kNotAsked) bits |= 1u << (p + VPTRIG_PART_ANSWERED_SHIFT);
	}
	return bits;
}

uint32_t SteamBuild() { return g_build; }

void CountersLine(char* out, size_t cap)
{
	static const char* const kNames[] = {"idle", "arming", "armed", "not armed", "disarmed"};
	const LONG st = g_state;
	_snprintf_s(out, cap, _TRUNCATE,
	            "route S: state %s, asks %ld, generations %ld, watchdogs %ld, part faults %ld, server not taken %ld, "
	            "disarms %ld, reports %ld, Combat bit %s by route S",
	            st >= 0 && st <= 4 ? kNames[st] : "?", g_asks, g_gens, g_watchdogs, g_faults, g_notTaken, g_disarms,
	            g_reports, g_combatOff ? "cleared" : "not cleared");
}

namespace Test
{
void Reset()
{
	g_state = kIdle;
	g_gen = 0;
	g_armStart = 0;
	g_lastStep = g_stepMs = g_maxGap = 0;
	g_noAnswerByServer = false;
	for (int p = 0; p < kPartCount; ++p)
	{
		g_ans[p] = kNotAsked;
		g_ansWall[p] = g_ansStep[p] = 0;
		g_why[p][0] = 0;
	}
	g_combatOff = false;
	g_bitClearedByUs = false;
	g_reported = 0;
	g_wdN = 0;
	g_wdPending = 0;
	g_asks = g_gens = g_watchdogs = g_faults = g_notTaken = g_reports = g_disarms = 0;
}
long Reports() { return g_reports; }
long Watchdogs() { return g_watchdogs; }
long Disarms() { return g_disarms; }
const char* PartWhy(int part) { return part >= 0 && part < kPartCount ? g_why[part] : ""; }
}  // namespace Test

#ifndef VP_ROUTE_S_OFFLINE
}  // namespace ProxyRouteS

const ProxyLogic::Settings& ProxyIniSettings();                  // proxy_update.cpp
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp

// The run-time guard of the Combat bit's field (exe-build-table.md section 4; lead-0042 #8576 / MC #8582 condition 1): the
// combat simulation's own start test reads [g_Civ5App] + 0x940 and tests 0x800 (combat-choreography.md 4.1 [C]); the
// pattern ties cam_app, the offset and the bit to that code on every build. The row is lead-0590's (thread 0590, asked
// 2026-11-20 #8600): until the build table carries rts_combat_gate for a build, the symbol is looked up by name, is not
// found, and the Combat part refuses there (route S is then not armed: arm as one). Bytes read from build 4390913's
// golden copy (bytes only, no decompiler output, D83); the shape occurs once in each build's image.
#define VP_ROUTE_S_PATTERNS(X) \
	X(rts_combat_gate, "CombatSim_StartGate", "8B 0D R(cam_app) F7 81 40 09 00 00 00 08 00 00 0F 84")

namespace ProxyRouteS
{
namespace
{
const DWORD kAppOffCfg = 0x940;     // the app's DisableSystems mask (proxy_session.cpp APP_OFF_CFG)
struct PatRow
{
	const char* sym;
	const char* name;
	const char* bytes;
};
#define VP_ROUTE_S_ROW(sym, name, bytes) {#sym, name, bytes},
const PatRow kPatterns[] = {VP_ROUTE_S_PATTERNS(VP_ROUTE_S_ROW)};
void (*g_chained)() = NULL;
int g_buildId = kVpExeBuildUnknown;
int g_testRefuse = -1;               // TEST ONLY: VP64_TEST_ROUTE_S_REFUSE
bool g_bitRelogged = false;
HANDLE g_server = NULL;              // the trigger section's server process (SYNCHRONIZE), watched while armed
uint32_t g_serverPid = 0;
HANDLE g_trigMap = NULL;
void* g_trig = NULL;
unsigned g_frames = 0;
uint32_t g_pubGen = 0, g_pubState = 0, g_pubParts = 0;
char g_reportCheck[64];
char g_reportLine[1800];

void GlueLog(const char* line, bool loud)
{
	if (loud)
		ProxyRtInt::LogErrPublic("%s", line);
	else
		ProxyRtInt::Log("%s", line);
}

HMODULE SelfModule()
{
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCSTR>(&SelfModule), &self);
	return self;
}

// The D118 report of the non-fatal stage "route-s" and the helper's popup, on a thread of its own (4773's form: a
// report from the frame hook would run inside a Lua listener, ticket 0487).
DWORD WINAPI ReportThread(LPVOID)
{
	const ProxyLogic::Settings& s = ProxyIniSettings();
	ProxyReport::Input in;
	in.stage = "route-s";
	in.code = g_reportCheck;
	in.details.push_back(g_reportLine);
	in.details.push_back("non-fatal (ticket 4783, D118, MC #8543): route S's three parts (the combat hold, the sound "
	                     "executor, the effect player) arm as one; one of them refused, did not answer, or stopped, so none "
	                     "is armed for this game session and the fights play the EXE's own way");
	for (int p = 0; p < kPartCount; ++p)
		if (g_why[p][0]) in.details.push_back(std::string(kPartName[p]) + ": " + g_why[p]);
	in.d33 = false;
	in.nonFatal = true;
	in.summary = std::string("The patch cannot play combat through the server on this game (route-s: ") + in.code +
	             "); the game goes on and shows fights the game's own way.";
	{
		char id[1024];
		ProxyBuildIdText(SelfModule(), id, sizeof(id));
		in.patch = id;
	}
	ProxyReport::Hashed h;
	h.label = "proxy";
	{
		char p[MAX_PATH * 2] = {};
		GetModuleFileNameA(SelfModule(), p, sizeof(p));
		h.path = p;
	}
	in.hashes.push_back(h);
	if (!s.logPath.empty()) in.tails.push_back(s.logPath);
	if (s.iniFound) in.copies.push_back(s.iniPath);
	{
		wchar_t opt[1024];
		if (Vp64Opt::DefaultPath(opt, sizeof(opt) / sizeof(opt[0])) != Vp64Opt::kPathNone) in.copiesW.push_back(opt);
	}
	{
		char c[600];
		CountersLine(c, sizeof(c));
		in.session.push_back(c);
		ProxyCombatHold::CountersLine(c, sizeof(c));
		in.session.push_back(c);
		ProxySoundExec::CountersLine(c, sizeof(c));
		in.session.push_back(c);
		ProxyEffectPlayer::CountersLine(c, sizeof(c));
		in.session.push_back(c);
	}
	std::wstring root, dir;
	std::vector<std::string> lines;
	if (!ProxyReport::Root(&root))
		lines.push_back("report: FAILED: the Documents folder could not be found; no report");
	else
		ProxyReport::Write(in, root, &dir, &lines);
	for (size_t i = 0; i < lines.size(); ++i) ProxyRtInt::Log("route S: %s", lines[i].c_str());
	if (dir.empty()) dir = root;
	std::string helperLine;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(s.proxyDir), L"popup", dir,
	                                               s.iniFound ? ProxySession::Widen(s.iniPath) : std::wstring(),
	                                               &helperLine);
	ProxyRtInt::Log("route S: %s%s; the game goes on (non-fatal)", started ? "" : "the popup FAILED to start: ",
	                ProxyReport::Scrub(helperLine).c_str());
	ProxyRtInt::LogFlush();
	return 0;
}

void GlueReport(const char* check, const char* line)
{
	strcpy_s(g_reportCheck, sizeof(g_reportCheck), check ? check : "?");
	strcpy_s(g_reportLine, sizeof(g_reportLine), line ? line : "");
	ProxyRtInt::LogFlush();
	HANDLE t = CreateThread(NULL, 0, ReportThread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		ProxyRtInt::Log("route S: the report's thread could not start (error %lu): no report", (unsigned long)GetLastError());
}

// The address of the app's DisableSystems mask, 0 when the build table, the guard row, its pattern or the app is not
// there. No address is logged (D202: names only).
ULONG_PTR CfgAddr(bool checkRow, char* text, size_t cap)
{
	if (g_buildId == kVpExeBuildUnknown)
	{
		_snprintf_s(text, cap, _TRUNCATE, "not a known build");
		return 0;
	}
	const ULONG_PTR base = (ULONG_PTR)GetModuleHandleA(NULL);
	for (size_t i = 0; checkRow && i < sizeof(kPatterns) / sizeof(kPatterns[0]); ++i)
	{
		const int sym = VpExeSymByName(kPatterns[i].sym, -1);
		if (sym < 0 || !ProxyExe::Va(g_buildId, sym))
		{
			_snprintf_s(text, cap, _TRUNCATE, "the build table carries no %s to %s (lead-0590's row): not written",
			            kPatterns[i].sym, VpExeBuildLabel(g_buildId));
			return 0;
		}
		VpExePatternResult r;
		if (!ProxyExe::CheckPattern(g_buildId, base, sym, kPatterns[i].bytes, &r))
		{
			_snprintf_s(text, cap, _TRUNCATE, "the pattern %s does not match on %s (%s at %d): not written",
			            kPatterns[i].name, VpExeBuildLabel(g_buildId), r.reason, r.offset);
			return 0;
		}
	}
	const ULONG_PTR appAt = ProxyExe::Addr(g_buildId, VPX_cam_app, base);
	DWORD app = 0;
	if (!appAt || !ReadDword(appAt, &app) || app < 0x10000)
	{
		_snprintf_s(text, cap, _TRUNCATE, "cam_app is not carried or the app object is not there");
		return 0;
	}
	return (ULONG_PTR)app + kAppOffCfg;
}

bool PartsCombat(void*, bool off, char* text, size_t cap)
{
	// The restore needs no row check (the field was found and checked at the arming); without a clear, nothing to do.
	if (!off && !CombatClearedByUs()) return CombatBit(0, false, text, cap);
	const ULONG_PTR cfg = CfgAddr(off, text, cap);
	if (!cfg) return false;
	return CombatBit(cfg, off, text, cap);
}

// While armed: the bit stays cleared (a write by the EXE would bring its simulation back beside the server's fights).
void KeepCombatOff()
{
	if (!CombatClearedByUs() || (g_frames % 30) != 0) return;
	char text[200];
	const ULONG_PTR cfg = CfgAddr(false, text, sizeof(text));
	if (cfg && CombatKeepOff(cfg) && !g_bitRelogged)
	{
		g_bitRelogged = true;
		ProxyRtInt::LogErrPublic("LOUD: route S: the EXE's Combat bit was set again while armed: cleared again (logged once)");
	}
}

// ---- the parts ---------------------------------------------------------------------------------------------------------
bool TestRefused(int part)
{
	if (g_testRefuse != part) return false;
	PartReport(part, "route-s-test-refuse", "TEST ONLY: refused by VP64_TEST_ROUTE_S_REFUSE (never set by the product)");
	return true;
}
bool PartsArm(void*, int part)
{
	if (TestRefused(part)) return false;
	switch (part)
	{
	case kHold: return ProxyCombatHold::SetServerFights(true);
	case kSound: return ProxySoundExec::SetServerFights(true);
	case kEffect: return ProxyEffectPlayer::SetServerFights(true);
	default: return false;
	}
}
bool PartsAnswering(void*, int part)
{
	switch (part)
	{
	case kHold: return ProxyCombatHold::CurrentState() == ProxyCombatHold::kActive;
	case kSound: return ProxySoundExec::Attached();
	case kEffect: return ProxyEffectPlayer::Attached();
	default: return false;
	}
}
void PartsDisarm(void*, int part)
{
	switch (part)
	{
	case kHold: ProxyCombatHold::SetServerFights(false); break;
	case kSound: ProxySoundExec::SetServerFights(false); break;
	case kEffect: ProxyEffectPlayer::SetServerFights(false); break;
	default: break;
	}
}
void PartsRelease(void*) { ProxyCombatHold::ReleaseAll(); }

void HoldReport(const char* check, const char* line) { PartReport(kHold, check, line); }
void SoundReport(const char* check, const char* line) { PartReport(kSound, check, line); }
void EffectReport(const char* check, const char* line) { PartReport(kEffect, check, line); }

// <base>.Trig, opened (never created: the server makes it when its trigger stream is on) every 60 frames until it is there.
void OpenTrig()
{
	wchar_t base[256];
	const DWORD n = GetEnvironmentVariableW(L"VPSHELL_SECTION", base, sizeof(base) / sizeof(base[0]));
	if (n == 0 || n >= sizeof(base) / sizeof(base[0])) return;
	wchar_t name[300];
	_snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE, L"%ls%ls", base, VPTRIG_SECTION_SUFFIX);
	HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
	if (!h) return;
	void* v = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPTRIG_SECTION_BYTES);
	if (!v)
	{
		CloseHandle(h);
		return;
	}
	g_trigMap = h;
	g_trig = v;
	ProxyRtInt::Log("route S: the server's trigger section is open; route S is decided at its question (R9)");
}

// MC #8582 condition 3: a server that died while route S is armed (no orderly stop: the section's magic stays) would
// leave every later fight to the hold's watchdog once; its process handle is watched every 30 frames instead.
bool ServerDied(uint32_t pid)
{
	if (pid == 0) return false;
	if (pid != g_serverPid)
	{
		if (g_server) CloseHandle(g_server);
		g_server = OpenProcess(SYNCHRONIZE, FALSE, pid);
		g_serverPid = pid;
	}
	if (!g_server || (g_frames % 30) != 0) return false;
	return WaitForSingleObject(g_server, 0) == WAIT_OBJECT_0;
}

void Publish(uint32_t gen)
{
	const uint32_t state = AnswerState();
	if (state == 0 || !g_trig) return;
	const uint32_t parts = AnswerParts();
	if (gen == g_pubGen && state == g_pubState && parts == g_pubParts) return;
	VPTrigPublishRouteS(g_trig, gen, state, parts, SteamBuild());
	g_pubGen = gen;
	g_pubState = state;
	g_pubParts = parts;
}
}  // namespace

void Init(void (*existingFrameHook)())
{
	g_chained = existingFrameHook;
	SetSinks(GlueLog, GlueReport, NULL);
	uint32_t stamp = 0, size = 0;
	g_buildId = ProxyExe::BuildOfImage((uintptr_t)GetModuleHandleA(NULL), &stamp, &size);
	char label[160];
	uint32_t steam = 0;
	if (g_buildId != kVpExeBuildUnknown)
	{
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s", VpExeBuildLabel(g_buildId));
		const VpExeBuildInfo* b = VpExeBuildInfoOf(g_buildId);
		steam = b ? (uint32_t)strtoul(b->steamBuild, NULL, 10) : 0;
	}
	else
		ProxyExe::UnknownBuild(label, sizeof(label), "", stamp, size);
	Parts p = {NULL, PartsArm, PartsAnswering, PartsDisarm, PartsRelease, PartsCombat};
	Configure(p, steam, label, g_buildId != kVpExeBuildUnknown && VpExeBuildProductReady(g_buildId));
	{
		// Read once here (not on the frame path): the legal value of the Combat bit's field (MC #8582 condition 2).
		std::string why;
		uint32_t mask = 0;
		const int read = ProxySession::ReadConfigIniMask(&mask, &why);
		SetIniMask(read, mask);
		if (read < 0)
			ProxyRtInt::Log("route S: config.ini could not be read (%s): route S will not arm (the Combat bit is not "
			                "written without its legal value)", why.c_str());
	}
	ProxyCombatHold::SetReportSink(HoldReport);
	ProxyCombatHold::SetWatchdogSink(HoldWatchdog);
	ProxySoundExec::SetReportSink(SoundReport);
	ProxyEffectPlayer::SetReportSink(EffectReport);
#ifdef VP64_ROUTE_S_TEST_REFUSE_BUILD
	// TEST ONLY, compiled out of every product build (MC #8639): no builder of the repository defines
	// VP64_ROUTE_S_TEST_REFUSE_BUILD (proxy/build_proxy.py, server/build_server.py), so a release proxy never reads the
	// variable. A test proxy built with /DVP64_ROUTE_S_TEST_REFUSE_BUILD reads VP64_TEST_ROUTE_S_REFUSE=hold|sound|effect
	// and makes that ONE part's arm answer refused. The live forced refusal of the product is ServerCombat = 0 (MC #8627).
	char v[16] = "";
	const DWORD n = GetEnvironmentVariableA("VP64_TEST_ROUTE_S_REFUSE", v, sizeof(v));
	if (n > 0 && n < sizeof(v))
	{
		g_testRefuse = _stricmp(v, "hold") == 0 ? kHold : _stricmp(v, "sound") == 0 ? kSound : _stricmp(v, "effect") == 0 ? kEffect : -1;
		ProxyRtInt::Log("LOUD: TEST ONLY: VP64_TEST_ROUTE_S_REFUSE=%s: %s (ticket 4783; a test proxy only)", v,
		                g_testRefuse >= 0 ? "that part's arm answers refused" : "not hold, sound or effect: ignored");
	}
#endif
}

void FrameHook()
{
	if (g_chained) g_chained();
	++g_frames;
	if (!g_trig && (g_frames % 60) == 1) OpenTrig();
	if (g_trig)
	{
		const VPTrigServer* s = VPTRIG_SERVER(g_trig);
		if (s->magic == VPTRIG_MAGIC)
		{
			const uint32_t ask = s->routeSAsk;
			VPTRIG_BARRIER();
			const uint32_t gen = s->generation;
			if (ask != 0 && ask == gen)
			{
				Ask(gen);
				ServerDecision(s->routeSTaken);
				if ((g_state == kArmed || g_state == kArming) && ServerDied(s->serverPid))
				{
					if (g_state == kArming)
						Fallback("route-s-server-died", "the server process ended before the arming finished");
					else
						DisarmAsOne("route-s-server-died", "the server process ended (no orderly stop)");
				}
				Step();
				Publish(gen);
				if (g_state == kArmed) KeepCombatOff();
				return;
			}
		}
	}
	Step();
	if (g_state == kArmed) KeepCombatOff();
}

bool OnUnload(char* line, size_t cap)
{
	line[0] = 0;
	if (g_server)
	{
		CloseHandle(g_server);
		g_server = NULL;
	}
	if (!CombatClearedByUs()) return false;
	char text[200];
	const bool ok = PartsCombat(NULL, false, text, sizeof(text));
	_snprintf_s(line, cap, _TRUNCATE, "route S: at the unload the EXE's Combat bit is %s%s", ok ? "" : "NOT restored: ",
	            text);
	return true;
}
#endif  // VP_ROUTE_S_OFFLINE
}  // namespace ProxyRouteS
