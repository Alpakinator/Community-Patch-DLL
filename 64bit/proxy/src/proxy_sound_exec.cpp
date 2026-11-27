// proxy_sound_exec.cpp - see proxy_sound_exec.h (ticket 4723, epic 0042, route S). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No STL in this file (it would need the proxy's _SECURE_SCL setting before the first STL header, ticket 4725), no
// allocation on the frame path, no exception out of the hook. The core needs only <windows.h> and vp_trigger.h.
#ifndef VP_SOUND_EXEC_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way (and _SECURE_SCL 0, 4725)
#include "proxy_rt_logic.h"     // ProxyLogic::Settings
#include "proxy_report.h"
#include "proxy_session.h"
#include "proxy_combat_hold.h"
#include "proxy_exe.h"          // epic 0590: the build table (the binding only)
#include "vp_frame.h"           // the frame being shown (<base>.Frame, read only)
#include <string>
#include <vector>
#endif
#include "proxy_sound_exec.h"
#include "vp_trigger.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ProxySoundExec
{
namespace
{
// ---- sinks and counters ------------------------------------------------------------------------------------------
LogFn g_log = NULL;
OwnerEndFn g_ownerEnd = NULL;
ClockFn g_clock = NULL;
// 4783 (W24 #8726, MC #8731): the server's start of a fight = its first record seen here (any kind but OwnerEnd, owner
// != 0); passed once per fight to the combat hold, whose watchdog then runs from it. A small ring dedupes the calls.
FightStartFn g_fightStart = NULL;
const int kStartedRing = 128;
uint32_t g_startedRing[kStartedRing];
int g_startedAt = 0;
long g_fightStarts = 0;

void NoteFightRecord(uint32_t owner)
{
	if (owner == 0) return;
	for (int i = 0; i < kStartedRing; ++i)
		if (g_startedRing[i] == owner) return;
	g_startedRing[g_startedAt] = owner;
	g_startedAt = (g_startedAt + 1) % kStartedRing;
	++g_fightStarts;
	if (g_fightStart) g_fightStart(owner);
}
void ClearStarted()
{
	for (int i = 0; i < kStartedRing; ++i) g_startedRing[i] = 0;
	g_startedAt = 0;
}

struct Counters
{
	long records, starts, stopRecords, ownerEnds, effects;
	long played, refused, unknownScript, stale, afterEnd, full;
	long stOwnerEnd, stRingOut, stClip, stRecord, stWatchdog, stReset;
	long naturalEnd, holdEnds, holdOwnerZero, newGen, lost, badRecord;
	long tailStarts, tailPlayed;   // 4732: unit-owned starts (owner 0, fightSeq 0: the victory tail) and those played
};
Counters g_n;

void Log(bool loud, const char* fmt, ...)
{
	if (!g_log) return;
	char b[768];   // 4732: the counters line grew (the victory tail's counts)
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(b, sizeof(b) - 1, fmt, ap);
	va_end(ap);
	b[sizeof(b) - 1] = 0;
	g_log(b, loud);
}

DWORD Now()
{
	return g_clock ? g_clock() : GetTickCount();
}

// 4740: signed, so a stamp later than `now` (never made since 4740; was DoStop's fresh clock read inside a step, W38's
// S4) reads "not elapsed" instead of wrapping to ~4.29e9 ms. Still right across GetTickCount's wrap (spans < 24.8 days).
bool Elapsed(DWORD since, DWORD now, DWORD ms)
{
	return (int32_t)(now - since) >= (int32_t)ms;
}

// ---- the tables (main thread only) ---------------------------------------------------------------------------------
struct Sound
{
	bool used;
	bool loop;
	uint32_t handle;
	void* obj;            // the EXE's sound object
	int script;
	uint32_t owner, fightSeq;
	int32_t player, unit, figure;
	uint64_t instance;
	bool stopPending;     // a ring-out is running
	DWORD stopAtMs;
};
Sound g_sounds[kMaxSounds];
int g_live = 0;
int g_sweep = 0;

struct Fight
{
	bool used;
	bool ended;           // the executor's own end of the fight's sounds: the server's OwnerEnd or the watchdog
	bool endPassed;       // 4740: the server's fight end passed to the hold (R8, once); the watchdog does not set it
	uint32_t owner, fightSeq;
	DWORD lastMs;         // the newest record of this fight (live), or its end (ended)
};
Fight g_fights[kMaxFights];

void* g_section = NULL;
uint32_t g_readerPid = 0;
Audio g_audio;
VPTrigCursor g_cursor;
int g_script[VPTRIG_NAME_CAPACITY];   // name index -> script index; -2 = not resolved yet, -1 = unknown

bool SameFight(uint32_t o1, uint32_t s1, uint32_t o2, uint32_t s2)
{
	if (o1 != 0 || o2 != 0) return o1 == o2;   // the EXE's event id names the fight when known
	return s1 == s2;                            // else the combat record's seq (R7)
}

Fight* FindFight(uint32_t owner, uint32_t fightSeq)
{
	for (int i = 0; i < kMaxFights; ++i)
		if (g_fights[i].used && SameFight(g_fights[i].owner, g_fights[i].fightSeq, owner, fightSeq)) return &g_fights[i];
	return NULL;
}

// A fight's entry (live), made when missing: a free slot, else the oldest ended one.
Fight* TouchFight(uint32_t owner, uint32_t fightSeq, DWORD now)
{
	Fight* f = FindFight(owner, fightSeq);
	if (!f)
	{
		for (int i = 0; i < kMaxFights && !f; ++i)
			if (!g_fights[i].used) f = &g_fights[i];
		if (!f)
		{
			DWORD oldest = 0;
			for (int i = 0; i < kMaxFights; ++i)
				if (g_fights[i].ended && (!f || (DWORD)(now - g_fights[i].lastMs) > oldest))
				{
					f = &g_fights[i];
					oldest = (DWORD)(now - g_fights[i].lastMs);
				}
		}
		if (!f) return NULL;   // 64 live fights: the caller counts it
		f->used = true;
		f->ended = false;
		f->endPassed = false;
		f->owner = owner;
		f->fightSeq = fightSeq;
	}
	if (!f->ended) f->lastMs = now;
	return f;
}

void Free(Sound& s)
{
	if (!s.used) return;
	s.used = false;
	s.obj = NULL;
	--g_live;
}

void StopNow(Sound& s, long& counter)
{
	if (!s.used) return;
	if (g_audio.live && g_audio.live(g_audio.ctx, s.obj, s.script))
	{
		if (g_audio.stop) g_audio.stop(g_audio.ctx, s.obj);
		++counter;
	}
	else
		++g_n.naturalEnd;
	Free(s);
}

// An owner's end: loops at once; one-shots end by themselves (the sweep frees them), a one-shot still live kRingOutMs
// later is stopped (4734).
void EndSound(Sound& s, DWORD now, long& loopCounter)
{
	if (!s.used) return;
	if (s.loop)
		StopNow(s, loopCounter);
	else if (!s.stopPending)
	{
		s.stopPending = true;
		s.stopAtMs = now + kRingOutMs;
	}
}

void EndFight(Fight& f, DWORD now, long& loopCounter)
{
	for (int i = 0; i < kMaxSounds; ++i)
		if (g_sounds[i].used && SameFight(g_sounds[i].owner, g_sounds[i].fightSeq, f.owner, f.fightSeq))
			EndSound(g_sounds[i], now, loopCounter);
	f.ended = true;
	f.lastMs = now;
}

void PassOwnerEnd(uint32_t owner, bool played)
{
	if (owner == 0)
	{
		++g_n.holdOwnerZero;
		return;
	}
	++g_n.holdEnds;
	if (g_ownerEnd) g_ownerEnd(owner, played);
}

// R6: everything stops; every live fight is ended with Reset (played = false).
void ResetAll(const char* why)
{
	for (int i = 0; i < kMaxSounds; ++i)
		if (g_sounds[i].used) StopNow(g_sounds[i], g_n.stReset);
	for (int i = 0; i < kMaxFights; ++i)
	{
		// 4740: every fight whose end the hold has not heard (a watchdog-ended one included: the hold still holds it)
		if (g_fights[i].used && !g_fights[i].endPassed) PassOwnerEnd(g_fights[i].owner, false);
		g_fights[i].used = false;
	}
	for (int i = 0; i < (int)VPTRIG_NAME_CAPACITY; ++i) g_script[i] = -2;
	ClearStarted();
	if (why) Log(false, "sound exec: reset (%s): every sound stopped, every fight ended", why);
}

int ScriptOf(uint32_t name)
{
	if (name == 0 || name >= VPTRIG_NAME_CAPACITY) return -1;
	if (g_script[name] != -2) return g_script[name];
	const VPTrigName* n = VPTrigNameAt(g_section, name);
	if (!n) return -1;   // not published yet (R5 forbids it): no cache, counted by the caller
	int idx = -1;
	if (n->kind == VPTRIG_NAME_SOUND3D && n->length > 0 && n->length < VPTRIG_NAME_TEXT && n->text[n->length] == 0 &&
	    g_audio.scriptIndex)
		idx = g_audio.scriptIndex(g_audio.ctx, n->text);
	if (idx < 0)
	{
		idx = -1;
		Log(true, "LOUD: sound exec: the name %lu (%.119s) is no 3D audio script of this game: its sounds are not played",
		    (unsigned long)name, n->kind == VPTRIG_NAME_SOUND3D ? n->text : "(not a sound3d name)");
	}
	g_script[name] = idx;
	return idx;
}

// 4732: R7's unit-owned form (owner 0 and fightSeq 0: the server's victory tail, one-shots of a unit's victory clip for
// a few seconds after its fight's Natural end) is no fight: it gets no fight entry, so no fight watchdog and no
// after-end drop; its one-shots free at their natural end (Housekeep's sweep) or by a ClipEnded / SoundStop record.
bool UnitOwned(const VPTrigRecord& r) { return r.owner == 0 && r.fightSeq == 0; }

void DoStart(const VPTrigRecord& r, int64_t frameShown, DWORD now)
{
	++g_n.starts;
	const bool unitOwned = UnitOwned(r);
	if (unitOwned) ++g_n.tailStarts;
	Fight* f = unitOwned ? NULL : FindFight(r.owner, r.fightSeq);
	if (f && f->ended)
	{
		++g_n.afterEnd;   // no sound of an ended fight starts later
		return;
	}
	if (r.frameId < frameShown - kStaleFrames)
	{
		++g_n.stale;      // never queued for later
		return;
	}
	if (unitOwned && (r.flags & VPTRIG_FLAG_LOOP))
	{
		++g_n.badRecord;  // R7: a loop needs a fight to end it (the server never sends one in this form)
		return;
	}
	if (!unitOwned) f = TouchFight(r.owner, r.fightSeq, now);
	const int script = ScriptOf(r.name);
	if (script < 0)
	{
		++g_n.unknownScript;
		return;
	}
	Sound* slot = NULL;
	for (int i = 0; i < kMaxSounds && !slot; ++i)
		if (!g_sounds[i].used) slot = &g_sounds[i];
	if (!slot || (!f && !unitOwned))
	{
		if (g_n.full++ == 0)
			Log(true, "LOUD: sound exec: %s: a sound was not started (counted from now on)",
			    slot ? "64 fights at once" : "256 live sounds at once");
		return;
	}
	void* obj = g_audio.play ? g_audio.play(g_audio.ctx, script, r.pos) : NULL;
	if (!obj)
	{
		++g_n.refused;    // no voice, a script rule, too far: not retried
		return;
	}
	++g_n.played;
	if (unitOwned) ++g_n.tailPlayed;
	slot->used = true;
	slot->loop = (r.flags & VPTRIG_FLAG_LOOP) != 0;
	slot->handle = r.handle;
	slot->obj = obj;
	slot->script = script;
	slot->owner = r.owner;
	slot->fightSeq = r.fightSeq;
	slot->player = r.player;
	slot->unit = r.unit;
	slot->figure = r.figure;
	slot->instance = r.instance;
	slot->stopPending = false;
	slot->stopAtMs = 0;
	++g_live;
}

// 4740: stamped with the step's `now`, never a fresh clock read: GetTickCount can tick inside the step, and a stamp later
// than the `now` Housekeep compares with made the watchdog end a live fight at once (W38's S4, fight event 122793).
void DoStop(const VPTrigRecord& r, DWORD now)
{
	++g_n.stopRecords;
	if (Fight* f = FindFight(r.owner, r.fightSeq))
		if (!f->ended) f->lastMs = now;
	for (int i = 0; i < kMaxSounds; ++i)
		if (g_sounds[i].used && g_sounds[i].handle == r.handle && r.handle != 0) StopNow(g_sounds[i], g_n.stRecord);
}

void DoOwnerEnd(const VPTrigRecord& r, DWORD now)
{
	++g_n.ownerEnds;
	if (VPTrigIsFightEnd(r))
	{
		if (UnitOwned(r))
		{
			++g_n.badRecord;   // 4732: no fight to end (an ended (0, 0) entry would drop the next victory tails)
			return;
		}
		Fight* f = TouchFight(r.owner, r.fightSeq, now);
		if (f && !f->ended)
			EndFight(*f, now, g_n.stOwnerEnd);
		else if (!f)
		{
			Fight tmp = {true, false, false, r.owner, r.fightSeq, now};
			EndFight(tmp, now, g_n.stOwnerEnd);
		}
		// R8, once per fight. 4740: keyed on what the hold has heard, not on the executor's own end: a fight its watchdog
		// ended (its sounds stopped) still holds the EXE until this end (before 4740 it was swallowed here, the hold's
		// watchdog dropped the fight and route S disarmed; W38's S4)
		if (!f || !f->endPassed)
		{
			if (f) f->endPassed = true;
			PassOwnerEnd(r.owner, (r.flags & VPTRIG_FLAG_PLAYED) != 0);
		}
		return;
	}
	if (r.reason == VPTRIG_END_CLIP_ENDED)
	{
		if (Fight* f = FindFight(r.owner, r.fightSeq))
			if (!f->ended) f->lastMs = now;
		for (int i = 0; i < kMaxSounds; ++i)
		{
			Sound& s = g_sounds[i];
			if (s.used && SameFight(s.owner, s.fightSeq, r.owner, r.fightSeq) && s.player == r.player &&
			    s.unit == r.unit && s.figure == r.figure && s.instance == r.instance)
				EndSound(s, now, g_n.stClip);
		}
		return;
	}
	++g_n.badRecord;   // checkTriggerRecord forbids any other OwnerEnd
}

// Ring-outs, the watchdog, and a few natural ends per frame.
void Housekeep(DWORD now)
{
	for (int i = 0; i < kMaxSounds; ++i)
		if (g_sounds[i].used && g_sounds[i].stopPending && (int32_t)(now - g_sounds[i].stopAtMs) >= 0)
			StopNow(g_sounds[i], g_n.stRingOut);
	for (int i = 0; i < kMaxFights; ++i)
	{
		Fight& f = g_fights[i];
		if (!f.used || f.ended || !Elapsed(f.lastMs, now, kSilentMs)) continue;
		long before = g_n.stWatchdog;
		for (int k = 0; k < kMaxSounds; ++k)
			if (g_sounds[k].used && SameFight(g_sounds[k].owner, g_sounds[k].fightSeq, f.owner, f.fightSeq))
				StopNow(g_sounds[k], g_n.stWatchdog);
		f.ended = true;
		f.lastMs = now;
		Log(true, "LOUD: sound exec: fight %lu (seq %lu) sent nothing for %lu ms: its sounds are stopped (%ld) by the "
		    "watchdog", (unsigned long)f.owner, (unsigned long)f.fightSeq, (unsigned long)kSilentMs,
		    g_n.stWatchdog - before);
	}
	for (int k = 0; k < kSweepPerStep && g_live > 0; ++k)
	{
		g_sweep = (g_sweep + 1) % kMaxSounds;
		Sound& s = g_sounds[g_sweep];
		// also a one-shot of an ended owner (stopPending): it ended by itself before its guard (4734)
		if (s.used && g_audio.live && !g_audio.live(g_audio.ctx, s.obj, s.script))
		{
			++g_n.naturalEnd;
			Free(s);
		}
	}
	// an ended fight is forgotten once its sounds are gone and the stream is long past it
	for (int i = 0; i < kMaxFights; ++i)
	{
		Fight& f = g_fights[i];
		if (!f.used || !f.ended || !Elapsed(f.lastMs, now, kSilentMs)) continue;
		bool any = false;
		for (int k = 0; k < kMaxSounds && !any; ++k)
			any = g_sounds[k].used && SameFight(g_sounds[k].owner, g_sounds[k].fightSeq, f.owner, f.fightSeq);
		if (!any) f.used = false;
	}
}
}  // namespace

void SetSinks(LogFn log, OwnerEndFn ownerEnd, ClockFn clock)
{
	g_log = log;
	g_ownerEnd = ownerEnd;
	g_clock = clock;
}

void SetFightStartSink(FightStartFn fightStart) { g_fightStart = fightStart; }

bool Attach(void* section, uint32_t readerPid, const Audio& audio)
{
	if (g_section) Detach();
	if (!section || !audio.play || !audio.stop || !audio.live || !audio.scriptIndex)
	{
		Log(true, "LOUD: sound exec: not attached (%s)", section ? "the audio is incomplete" : "no section");
		return false;
	}
	g_section = section;
	g_readerPid = readerPid;
	g_audio = audio;
	g_cursor.next = 1;
	g_cursor.generation = 0;
	g_cursor.lost = 0;
	ClearStarted();   // 4783
	for (int i = 0; i < (int)VPTRIG_NAME_CAPACITY; ++i) g_script[i] = -2;
	VPTrigPublishReader(g_section, g_cursor, g_readerPid);
	Log(false, "sound exec: attached (reader pid %lu)", (unsigned long)readerPid);
	return true;
}

void Detach()
{
	if (!g_section) return;
	ResetAll(NULL);
	VPTrigReader* r = VPTRIG_READER(g_section);
	r->readerPid = 0;
	g_section = NULL;
	char c[600];
	CountersLine(c, sizeof(c));
	Log(false, "sound exec: detached; %s", c);
}

bool Attached()
{
	return g_section != NULL;
}

void Step(int64_t frameShown)
{
	if (!g_section) return;
	const DWORD now = Now();
	int done = 0;
	VPTrigRecord r;
	while (frameShown >= 0 && done < kMaxRecordsPerStep)
	{
		const uint32_t prevGen = g_cursor.generation;
		const VPTrigReadResult res = VPTrigReadNext(g_section, g_cursor, frameShown, r);
		if (res == VPTRIG_READ_NONE) break;
		if (res == VPTRIG_READ_NEWGEN)
		{
			if (prevGen == 0) continue;   // the first generation seen after Attach (or after a stop): nothing to end
			++g_n.newGen;
			ResetAll(g_cursor.generation ? "a new generation of the trigger stream" : "the server's stream stopped");
			continue;
		}
		if (res == VPTRIG_READ_LOST)
		{
			++g_n.lost;
			continue;
		}
		++done;
		++g_n.records;
		if (r.kind != VPTRIG_OWNER_END) NoteFightRecord(r.owner);   // 4783: the server's start of the fight
		VPTrigReader* rb = VPTRIG_READER(g_section);
		switch (r.kind)
		{
		case VPTRIG_SOUND_START: DoStart(r, frameShown, now); break;
		case VPTRIG_SOUND_STOP: DoStop(r, now); break;
		case VPTRIG_OWNER_END: DoOwnerEnd(r, now); break;
		case VPTRIG_EFFECT_START:
		case VPTRIG_EFFECT_MOVE:
		case VPTRIG_EFFECT_STOP: ++g_n.effects; break;   // the effect routes' (4793), not this executor's
		default: ++g_n.badRecord; break;
		}
		VPTrigStore64(&rb->executedTotal, VPTrigLoad64(&rb->executedTotal) + 1);
		VPTrigStore64(&rb->droppedStale, g_n.stale);
	}
	Housekeep(now);
	VPTrigPublishReader(g_section, g_cursor, g_readerPid);
}

void CountersLine(char* out, size_t cap)
{
	if (!out || !cap) return;
	_snprintf(out, cap - 1,
	          "sound exec: records %ld (sound starts %ld, stops %ld, owner ends %ld, effects ignored %ld, bad %ld), played "
	          "%ld, refused %ld, unknown script %ld, stale %ld, after end %ld, full %ld, stopped %ld (owner end %ld, ring-out "
	          "%ld, clip %ld, stop record %ld, watchdog %ld, reset %ld), natural end %ld, hold OwnerEnd %ld (owner 0 %ld), "
	          "new generations %ld, lost %ld, live now %d, victory tail (unit-owned) starts %ld (played %ld)",
	          g_n.records, g_n.starts, g_n.stopRecords, g_n.ownerEnds, g_n.effects, g_n.badRecord, g_n.played, g_n.refused,
	          g_n.unknownScript, g_n.stale, g_n.afterEnd, g_n.full,
	          g_n.stOwnerEnd + g_n.stRingOut + g_n.stClip + g_n.stRecord + g_n.stWatchdog + g_n.stReset, g_n.stOwnerEnd,
	          g_n.stRingOut, g_n.stClip, g_n.stRecord, g_n.stWatchdog, g_n.stReset, g_n.naturalEnd, g_n.holdEnds,
	          g_n.holdOwnerZero, g_n.newGen, g_n.lost, g_live, g_n.tailStarts, g_n.tailPlayed);
	out[cap - 1] = 0;
}

namespace Test
{
void Reset()
{
	g_section = NULL;
	g_readerPid = 0;
	memset(&g_audio, 0, sizeof(g_audio));
	memset(&g_n, 0, sizeof(g_n));
	memset(g_sounds, 0, sizeof(g_sounds));
	memset(g_fights, 0, sizeof(g_fights));
	g_live = 0;
	g_sweep = 0;
	g_cursor.next = 1;
	g_cursor.generation = 0;
	g_cursor.lost = 0;
	ClearStarted();   // 4783
	g_fightStarts = 0;
	for (int i = 0; i < (int)VPTRIG_NAME_CAPACITY; ++i) g_script[i] = -2;
}
long Played() { return g_n.played; }
long Refused() { return g_n.refused; }
long UnknownScript() { return g_n.unknownScript; }
long Stale() { return g_n.stale; }
long AfterEnd() { return g_n.afterEnd; }
long Full() { return g_n.full; }
long StoppedOwnerEnd() { return g_n.stOwnerEnd; }
long StoppedRingOut() { return g_n.stRingOut; }
long StoppedClip() { return g_n.stClip; }
long StoppedRecord() { return g_n.stRecord; }
long StoppedWatchdog() { return g_n.stWatchdog; }
long StoppedReset() { return g_n.stReset; }
long NaturalEnd() { return g_n.naturalEnd; }
long HoldEnds() { return g_n.holdEnds; }
long HoldOwnerZero() { return g_n.holdOwnerZero; }
long NewGenerations() { return g_n.newGen; }
long Lost() { return g_n.lost; }
long EffectsIgnored() { return g_n.effects; }
long TailStarts() { return g_n.tailStarts; }
long TailPlayed() { return g_n.tailPlayed; }
long BadRecords() { return g_n.badRecord; }
int LiveSounds() { return g_live; }
int Fights()
{
	int n = 0;
	for (int i = 0; i < kMaxFights; ++i) n += g_fights[i].used ? 1 : 0;
	return n;
}
}  // namespace Test
}  // namespace ProxySoundExec

#ifndef VP_SOUND_EXEC_OFFLINE
// ---- the binding: the EXE's audio through the build table ------------------------------------------------------------
// Run-time guard of the two code entries (exe-build-table.md section 4; exebuild/patterns.txt lists this file). The play's
// first instruction compares the object pointer, so its pattern ties audio_game_obj too. The two caller rows check, on
// every build, that the EXE's own calls lead to the two entries (the C() call-target token of 4960c; lead-0590 #8203:
// both entries are short look-alike risks for a carry). Read from build 4390913's golden copy (bytes only, no
// decompiler output, D83).
#define VP_SOUND_EXEC_PATTERNS(X) \
	X(audio_play3d, "FAudioGame_Play3d", "51 83 3D R(audio_game_obj) 00 55 56 57 8B F1 0F 84 ?? ?? 00 00 80 7E 34 00 0F 84 ?? ?? 00 00 8B 7C 24 1C") \
	X(audio_sound_stop, "FSound_Stop", "56 8B F1 80 7E 28 00 75 31 8A 46 18 A8 01 74 2A A8 10 74 04 A8 20 75 28 A8 02 74 1E") \
	X(audio_play3d_call, "SoundTrigger_Play3dCall", "8B 0D R(audio_game_obj) 57 E8 C(audio_play3d) 8B 55 00 D1 EA") \
	X(audio_stop_call, "UnitLoopRecord_StopCall", "83 F8 40 7C F1 E8 C(audio_sound_stop) 5E C2 0C 00") 	X(audio_sound_fade, "FSound_Fade", "56 8B F1 80 7E 28 00 0F 85 ?? ?? 00 00 8A 4E 18 F6 C1 01") 	X(audio_fade_call, "UnitLoopHeartbeat_FadeCall", "D9 43 48 D9 1C 24 E8 C(audio_sound_fade) 80 26 F8")

const ProxyLogic::Settings& ProxyIniSettings();                  // proxy_update.cpp
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp

namespace ProxySoundExec
{
namespace
{
struct PatRow
{
	int sym;
	const char* name;
	const char* bytes;
};
#define VP_SOUND_EXEC_ROW(sym, name, bytes) {VPX_##sym, name, bytes},
const PatRow kPatterns[] = {VP_SOUND_EXEC_PATTERNS(VP_SOUND_EXEC_ROW)};

// The FSound layout the liveness check reads (exe-audio.md 1 and FSound::Stop's own tests of +0x28 and +0x18 bit 0
// [C]; +0xA4's script data with its index at +4 [I], checked live by 4723's smoke). Verified per build, as 4773's.
const DWORD kSndOffState = 0x18;     // bit 0: the sound holds a voice (Stop does nothing without it)
const DWORD kSndOffStopping = 0x28;  // non-zero: stopping (Stop does nothing)
const DWORD kSndOffScript = 0xA4;    // the script's data; its index at +4
struct LayoutRow
{
	int build;
	const char* evidence;
};
const LayoutRow kLayoutVerified[] = {
	{kVpExeBuild4390913, "exe-audio.md 1, 4.1 and 9 (4797: the play and the stop live, 2026-11-20); FSound::Stop's tests [C]"},
	{kVpExeBuild26577625, "ticket 0618, new-exe-build-26577625.md C.6: the play, Stop, both callers and FSound's 29 methods unchanged 2026-11-20 [C]; play and stop live [L]"},
};

struct ExeAudio
{
	ULONG_PTR objGlobal;   // audio_game_obj, rebased
	VpAudioPlay3dFn play;
	VpAudioStopFn stop;    // not called (5051): kept for its pattern, which guards the layout ExeLive reads
	VpAudioFadeFn fade;    // every end of a sound: the end request stock's heartbeat makes (5051)
	volatile LONG faults;  // an access fault in a call or a read: the executor turns itself off
};
ExeAudio g_exe;

bool ReadDword(ULONG_PTR a, DWORD& v)
{
	__try
	{
		v = *(volatile const DWORD*)a;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		InterlockedIncrement(&g_exe.faults);
		return false;
	}
}
bool ReadByte(ULONG_PTR a, BYTE& v)
{
	__try
	{
		v = *(volatile const BYTE*)a;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		InterlockedIncrement(&g_exe.faults);
		return false;
	}
}

int ExeScriptIndex(void*, const char* name)
{
	ICvEngineUtility1* eu = ProxyRtInt::EngineUtility();
	if (!eu || !name) return -1;
	return eu->GetAudioTagIndex(name, AUDIOTAG_3DSCRIPT);
}

void* CallPlay(void* obj, int script, const float* pos)
{
	__try
	{
		return g_exe.play(obj, 0, script, 0, pos, -1.0f, -1, -1.0f, -1.0f, 0.0f, -1, -1, -1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		InterlockedIncrement(&g_exe.faults);
		return NULL;
	}
}
void* ExePlay(void*, int script, const float pos[3])
{
	if (g_exe.faults) return NULL;
	DWORD obj = 0;
	if (!ReadDword(g_exe.objGlobal, obj) || !obj) return NULL;   // the game's audio is not up: nothing to play
	float p[3] = {pos[0], pos[1], pos[2]};
	return CallPlay((void*)(ULONG_PTR)obj, script, p);
}
bool ExeLive(void*, void* s, int script)
{
	if (!s || g_exe.faults) return false;
	const ULONG_PTR a = (ULONG_PTR)s;
	BYTE state = 0, stopping = 0;
	DWORD data = 0, idx = 0;
	if (!ReadByte(a + kSndOffStopping, stopping) || stopping) return false;
	if (!ReadByte(a + kSndOffState, state) || !(state & 1)) return false;
	if (!ReadDword(a + kSndOffScript, data) || !data) return false;
	if (!ReadDword((ULONG_PTR)data + 4, idx)) return false;
	return (int)idx == script;   // the pool reuses the object: only a sound of the same script is taken as ours
}
void ExeStop(void*, void* s)
{
	if (!s || g_exe.faults) return;
	__try
	{
		// 5051: never FSound::Stop: its paused Miles voice is never reused (unit-combat-server.md 18.24). The fade with
		// 0.0 s requests the end; the manager ends the voice at its next update and gives it back, as for stock.
		g_exe.fade(s, 0, 0.0f, 0.0f, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		InterlockedIncrement(&g_exe.faults);
	}
}
}  // namespace

bool AudioOfExe(ULONG_PTR base, Audio& out, char* why, size_t whyCap)
{
	memset(&out, 0, sizeof(out));
	uint32_t stamp = 0, size = 0;
	const int build = ProxyExe::BuildOfImage(base, &stamp, &size);
	if (build == kVpExeBuildUnknown)
	{
		ProxyExe::UnknownBuild(why, whyCap, "", stamp, size);
		return false;
	}
	const char* verified = NULL;
	for (size_t i = 0; i < sizeof(kLayoutVerified) / sizeof(kLayoutVerified[0]); ++i)
		if (kLayoutVerified[i].build == build) verified = kLayoutVerified[i].evidence;
	if (!verified)
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "the sound object's layout is not verified on %s",
		            VpExeBuildLabel(build));
		return false;
	}
	const int syms[4] = {VPX_audio_play3d, VPX_audio_game_obj, VPX_audio_sound_stop, VPX_audio_sound_fade};
	for (int i = 0; i < 4; ++i)
		if (!ProxyExe::Va(build, syms[i]))
		{
			ProxyExe::NotCarried(why, whyCap, "", build, syms[i]);
			return false;
		}
	for (size_t i = 0; i < sizeof(kPatterns) / sizeof(kPatterns[0]); ++i)
	{
		VpExePatternResult r;
		if (!ProxyExe::CheckPattern(build, base, kPatterns[i].sym, kPatterns[i].bytes, &r))
		{
			_snprintf_s(why, whyCap, _TRUNCATE, "the pattern %s does not match on %s (%s at %d)", kPatterns[i].name,
			            VpExeBuildLabel(build), r.reason, r.offset);
			return false;
		}
	}
	const ULONG_PTR obj = ProxyExe::Addr(build, VPX_audio_game_obj, base);
	if (!ProxyExe::Readable(obj, 4))
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "the audio object's pointer (audio_game_obj) is not readable memory");
		return false;
	}
	g_exe.objGlobal = obj;
	g_exe.play = (VpAudioPlay3dFn)ProxyExe::Addr(build, VPX_audio_play3d, base);
	g_exe.stop = (VpAudioStopFn)ProxyExe::Addr(build, VPX_audio_sound_stop, base);
	g_exe.fade = (VpAudioFadeFn)ProxyExe::Addr(build, VPX_audio_sound_fade, base);
	g_exe.faults = 0;
	out.ctx = NULL;
	out.scriptIndex = ExeScriptIndex;
	out.play = ExePlay;
	out.live = ExeLive;
	out.stop = ExeStop;
	_snprintf_s(why, whyCap, _TRUNCATE, "%s (layout: %s)", VpExeBuildLabel(build), verified);
	return true;
}

// ---- the glue ----------------------------------------------------------------------------------------------------------
namespace
{
void (*g_prevHook)() = NULL;
volatile LONG g_want = 0;        // SetServerFights: 1 = on, 0 = off (applied on the main thread at the next frame)
volatile LONG g_refused = 0;     // the audio could not be bound: no attach in this proxy load
Audio g_bound;
HANDLE g_trigMap = NULL, g_frameMap = NULL;
void* g_trigView = NULL;
const void* g_frameView = NULL;
unsigned g_frames = 0;
char g_reportCheck[64];
char g_reportLine[1400];

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

// The D118 report of the non-fatal stage "sound-exec" and the helper's popup, on a thread of its own (4773's form).
DWORD WINAPI ReportThread(LPVOID)
{
	const ProxyLogic::Settings& s = ProxyIniSettings();
	ProxyReport::Input in;
	in.stage = "sound-exec";
	in.code = g_reportCheck;
	in.details.push_back(g_reportLine);
	in.details.push_back("non-fatal (ticket 4723, D118): the proxy cannot play the server's fight sounds through this "
	                     "EXE's audio; the game goes on, the fights are silent");
	in.d33 = false;
	in.nonFatal = true;
	in.summary = std::string("The patch cannot play the fights' sounds on this game (sound-exec: ") + in.code +
	             "); the game goes on with silent fights.";
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
	std::wstring root, dir;
	std::vector<std::string> lines;
	if (!ProxyReport::Root(&root))
		lines.push_back("report: FAILED: the Documents folder could not be found; no report");
	else
		ProxyReport::Write(in, root, &dir, &lines);
	for (size_t i = 0; i < lines.size(); ++i) ProxyRtInt::Log("sound exec: %s", lines[i].c_str());
	if (dir.empty()) dir = root;
	std::string helperLine;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(s.proxyDir), L"popup", dir,
	                                               s.iniFound ? ProxySession::Widen(s.iniPath) : std::wstring(),
	                                               &helperLine);
	ProxyRtInt::Log("sound exec: %s%s; the game goes on (non-fatal)", started ? "" : "the popup FAILED to start: ",
	                ProxyReport::Scrub(helperLine).c_str());
	ProxyRtInt::LogFlush();
	return 0;
}

void (*g_reportSink)(const char* check, const char* line) = NULL;   // 4783: route S's common report

void Report(const char* check, const char* line)
{
	if (g_reportSink)
	{
		g_reportSink(check, line);
		return;
	}
	strcpy_s(g_reportCheck, sizeof(g_reportCheck), check ? check : "?");
	strcpy_s(g_reportLine, sizeof(g_reportLine), line ? line : "");
	ProxyRtInt::LogFlush();
	HANDLE t = CreateThread(NULL, 0, ReportThread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		ProxyRtInt::Log("sound exec: the report's thread could not start (error %lu): no report",
		                (unsigned long)GetLastError());
}

void* MapSection(const wchar_t* base, const wchar_t* suffix, DWORD bytes, HANDLE& map)
{
	wchar_t name[300];
	_snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE, L"%ls%ls", base, suffix);
	map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, bytes, name);   // created or opened
	if (!map) return NULL;
	void* v = MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes);
	if (!v)
	{
		CloseHandle(map);
		map = NULL;
	}
	return v;
}

void Unmap()
{
	if (g_trigView) UnmapViewOfFile(g_trigView);
	if (g_frameView) UnmapViewOfFile(g_frameView);
	if (g_trigMap) CloseHandle(g_trigMap);
	if (g_frameMap) CloseHandle(g_frameMap);
	g_trigView = NULL;
	g_frameView = NULL;
	g_trigMap = g_frameMap = NULL;
}

// Main thread: attach (map <base>.Trig and <base>.Frame; <base> = VPSHELL_SECTION of the EXE's environment, which the
// proxy itself sets for the composite, proxy_shell.h).
void AttachNow()
{
	wchar_t base[256];
	const DWORD n = GetEnvironmentVariableW(L"VPSHELL_SECTION", base, sizeof(base) / sizeof(base[0]));
	if (n == 0 || n >= sizeof(base) / sizeof(base[0]))
	{
		Log(true, "LOUD: sound exec: no VPSHELL_SECTION in the EXE's environment (the server renderer is off?): not attached");
		InterlockedExchange(&g_want, 0);
		return;
	}
	g_trigView = MapSection(base, VPTRIG_SECTION_SUFFIX, VPTRIG_SECTION_BYTES, g_trigMap);
	HANDLE fm = NULL;
	void* fv = MapSection(base, VPFRAME_SECTION_SUFFIX, VPFRAME_SECTION_BYTES, fm);
	g_frameMap = fm;
	g_frameView = fv;
	if (!g_trigView || !g_frameView || !Attach(g_trigView, GetCurrentProcessId(), g_bound))
	{
		Log(true, "LOUD: sound exec: the trigger or frame section could not be mapped (error %lu): not attached",
		    (unsigned long)GetLastError());
		Unmap();
		InterlockedExchange(&g_want, 0);
	}
}

// The frame being shown: the frame the composite published this EXE frame, if the server has drawn it; else the
// server's newest (R1: every record of a frame is visible before its slot is READY). -1 = none yet.
int64_t FrameShown()
{
	if (!g_frameView) return -1;
	const VPFrameServer* sv = VPFRAME_SERVER(g_frameView);
	if (sv->magic != VPFRAME_MAGIC) return -1;
	const VPFramePublish* pb = VPFRAME_PUBLISH(g_frameView);
	int64_t published = -1;
	for (int k = 0; k < 3 && published < 0; ++k)
	{
		const uint32_t s1 = pb->seq;
		VPTRIG_BARRIER();
		const int64_t f = pb->frameId;
		VPTRIG_BARRIER();
		if ((s1 & 1) == 0 && s1 != 0 && pb->seq == s1) published = f;
	}
	const int64_t latest = VPTrigLoad64(&VPFRAME_RING(g_frameView)->latestFrameId);
	if (published < 0) return latest > 0 ? latest : -1;
	return latest < published ? latest : published;
}
}  // namespace

void SetReportSink(void (*sink)(const char* check, const char* line)) { g_reportSink = sink; }

bool SetServerFights(bool on)
{
	if (!on)
	{
		InterlockedExchange(&g_want, 0);
		return true;
	}
	if (g_refused) return false;
	char why[400];
	Audio a;
	if (!AudioOfExe((ULONG_PTR)GetModuleHandleA(NULL), a, why, sizeof(why)))
	{
		InterlockedExchange(&g_refused, 1);
		Log(true, "LOUD: sound exec: off for this proxy load: %s; the server's fights will be silent (D118)", why);
		Report("sound-exec-bind", why);
		return false;
	}
	g_bound = a;
	Log(false, "sound exec: armed on %s: the server's sound records are played at their frame (attach at the next frame)",
	    why);
	InterlockedExchange(&g_want, 1);
	return true;
}

void FrameHook()
{
	if (g_prevHook) g_prevHook();
	const bool want = g_want != 0;
	if (!want && !Attached()) return;   // off: one test
	if (want && !Attached()) AttachNow();
	if (!want && Attached())
	{
		Detach();
		Unmap();
		return;
	}
	if (!Attached()) return;
	Step(FrameShown());
	if (g_exe.faults)
	{
		Log(true, "LOUD: sound exec: an access fault in the EXE's audio (%ld): turned off for this proxy load",
		    (long)g_exe.faults);
		InterlockedExchange(&g_refused, 1);
		InterlockedExchange(&g_want, 0);
		g_exe.faults = 0;
		Detach();
		Unmap();
		Report("sound-exec-fault", "an access fault in a call or a read of the EXE's audio");
		return;
	}
	if (++g_frames % 1800 == 1)
	{
		char c[600];
		CountersLine(c, sizeof(c));
		Log(false, "%s (frame hook alive, frame %u, thread %lu)", c, g_frames, (unsigned long)GetCurrentThreadId());
	}
}

void Init(void (*existingFrameHook)())
{
	g_prevHook = existingFrameHook;
	SetSinks(GlueLog, ProxyCombatHold::OwnerEnd, NULL);
	SetFightStartSink(ProxyCombatHold::FightStarted);   // 4783: the hold's watchdog from the server's start of each fight
	// TEST ONLY (ticket 4723, until 4782/4795 call SetServerFights): VP64_TEST_SOUND_EXEC=1 in the EXE's process arms the
	// executor at the boot as route S would. Never set by the product or the installer.
	char v[8] = "";
	const DWORD n = GetEnvironmentVariableA("VP64_TEST_SOUND_EXEC", v, sizeof(v));
	if (n > 0 && n < sizeof(v) && strcmp(v, "1") == 0)
	{
		ProxyRtInt::Log("LOUD: TEST ONLY: VP64_TEST_SOUND_EXEC=1: the sound executor is armed as route S would arm it "
		                "(ticket 4723; never set by the product)");
		SetServerFights(true);
	}
}
}  // namespace ProxySoundExec
#endif  // VP_SOUND_EXEC_OFFLINE
