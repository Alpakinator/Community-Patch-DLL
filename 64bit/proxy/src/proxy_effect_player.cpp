// proxy_effect_player.cpp - see proxy_effect_player.h (ticket 4727, epic 0042, route S effects stage A). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No STL in the core and the binding (it would need the proxy's _SECURE_SCL setting before the first STL header,
// ticket 4725; the glue's report thread includes the proxy's own headers first, which set it), no allocation on the frame
// path, no exception out of the hook. The core needs only <windows.h> and vp_trigger.h.
#ifndef VP_EFFECT_PLAYER_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way (and _SECURE_SCL 0, 4725)
#include "proxy_rt_logic.h"     // ProxyLogic::Settings
#include "proxy_report.h"
#include "proxy_session.h"
#include "proxy_exe.h"          // epic 0590: the build table (the binding only)
#include "vp_frame.h"           // the frame being shown (<base>.Frame, read only)
#include <string>
#include <vector>
#endif
#include "proxy_effect_player.h"
#include "vp_trigger.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ProxyEffectPlayer
{
// C++03: the payload's layout is the EXE record's (effects-formats.md 8.1), checked at compile time.
typedef char PayloadSizeCheck[sizeof(Payload) == kPayloadBytes ? 1 : -1];
typedef char PayloadKeyCheck[offsetof(Payload, key) == 0x40 ? 1 : -1];
typedef char PayloadHashCheck[offsetof(Payload, hash) == 0x50 ? 1 : -1];
typedef char PayloadScaleCheck[offsetof(Payload, scale) == 0x54 ? 1 : -1];

namespace
{
// ---- the name hash's table (static, filled once; no allocation) -----------------------------------------------------
uint32_t g_crc[256];
bool g_crcReady = false;
void CrcInit()
{
	for (uint32_t i = 0; i < 256; ++i)
	{
		uint32_t c = i;
		for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		g_crc[i] = c;
	}
	g_crcReady = true;
}
}  // namespace

uint32_t NameHash(const char* name, int len)
{
	if (!g_crcReady) CrcInit();
	uint32_t c = 0xFFFFFFFFu;
	if (!name) return c;
	for (int i = 0; len < 0 ? name[i] != 0 : i < len; ++i)
		c = g_crc[(c ^ (unsigned char)name[i]) & 0xFF] ^ (c >> 8);
	return c;   // no final inversion (the EXE's rule)
}

void MatrixOf(const float pos[3], const float q[4], float m[16])
{
	double x = q[0], y = q[1], z = q[2], w = q[3];
	const double n = x * x + y * y + z * z + w * w;
	if (!(n > 1e-12) || !(n < 1e12))   // zero, tiny, huge or NaN: the identity
	{
		x = y = z = 0;
		w = 1;
	}
	else
	{
		const double r = 1.0 / sqrt(n);
		x *= r;
		y *= r;
		z *= r;
		w *= r;
	}
	// D3DXMatrixRotationQuaternion's layout: row vectors (v' = v * M), rows are the rotated basis vectors.
	m[0] = (float)(1 - 2 * (y * y + z * z));
	m[1] = (float)(2 * (x * y + z * w));
	m[2] = (float)(2 * (x * z - y * w));
	m[3] = 0;
	m[4] = (float)(2 * (x * y - z * w));
	m[5] = (float)(1 - 2 * (x * x + z * z));
	m[6] = (float)(2 * (y * z + x * w));
	m[7] = 0;
	m[8] = (float)(2 * (x * z + y * w));
	m[9] = (float)(2 * (y * z - x * w));
	m[10] = (float)(1 - 2 * (x * x + y * y));
	m[11] = 0;
	m[12] = pos[0];
	m[13] = pos[1];
	m[14] = pos[2];
	m[15] = 1;
}

namespace
{
// ---- sinks and counters ------------------------------------------------------------------------------------------
LogFn g_log = NULL;
ClockFn g_clock = NULL;

struct Counters
{
	long records, starts, moves, stops, ownerEnds, sounds, badRecord;
	long posted[kPostKinds], movesPosted, postFailed;
	long stale, afterEnd, unknownName, unknownHandle, full;
	long orphanStops, watchdogStops, capStops, resetStops, forgotten, newGen, lost;
	long tailStarts, tailPosted;   // 4732: unit-owned starts (owner 0, fightSeq 0: the victory tail) and those posted
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

bool Elapsed(DWORD since, DWORD now, DWORD ms)
{
	return (DWORD)(now - since) >= ms;   // wraps
}

// ---- the tables (main thread only) ---------------------------------------------------------------------------------
struct Fx
{
	bool used;
	PostKind kind;        // kPostOneShot or kPostLasting: the start's type, re-posted for a move
	uint32_t handle;
	uint32_t hash;
	float scale;
	uint32_t key[4];
	uint32_t owner, fightSeq;
	int32_t player, unit, figure;
	uint64_t instance;
	DWORD bornMs, lastMs;
	bool movePending;
	float m[16];          // the newest pose (the start's, then each move's)
};
Fx g_fx[kMaxEffects];
int g_live = 0;

struct Fight
{
	bool used;
	bool ended;
	uint32_t owner, fightSeq;
	DWORD lastMs;         // the newest record of this fight (live), or its end (ended)
};
Fight g_fights[kMaxFights];

const void* g_section = NULL;
Poster g_poster;
VPTrigCursor g_cursor;
uint32_t g_hash[VPTRIG_NAME_CAPACITY];   // name index -> hash
uint8_t g_hashState[VPTRIG_NAME_CAPACITY];   // 0 = not resolved yet, 1 = ok, 2 = not an effect name

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
		f->owner = owner;
		f->fightSeq = fightSeq;
	}
	if (!f->ended) f->lastMs = now;
	return f;
}

Fx* FindFx(uint32_t handle)
{
	if (handle == 0) return NULL;
	for (int i = 0; i < kMaxEffects; ++i)
		if (g_fx[i].used && g_fx[i].handle == handle) return &g_fx[i];
	return NULL;
}

bool Post(PostKind kind, const void* payload)
{
	if (g_poster.post && g_poster.post(g_poster.ctx, kind, payload))
	{
		++g_n.posted[kind];
		return true;
	}
	++g_n.postFailed;
	return false;
}

void PostPose(Fx& e)
{
	Payload p;
	memset(&p, 0, sizeof(p));
	memcpy(p.m, e.m, sizeof(p.m));
	memcpy(p.key, e.key, sizeof(p.key));
	p.hash = e.hash;
	p.scale = e.scale;
	Post(e.kind, &p);
}

void Free(Fx& e)
{
	if (!e.used) return;
	e.used = false;
	--g_live;
}

// 0x2A for a lasting effect (a one-shot is only forgotten: the EXE frees it at the end of its duration).
void StopNow(Fx& e, long* counter)
{
	if (!e.used) return;
	if (e.kind == kPostLasting)
	{
		uint32_t stop[kPayloadBytes / 4];
		memset(stop, 0, sizeof(stop));
		memcpy(stop, e.key, sizeof(e.key));
		Post(kPostStop, stop);
		if (counter) ++*counter;
	}
	else
		++g_n.forgotten;
	Free(e);
}

// R6: every lasting effect stops; everything is forgotten.
void ResetAll(const char* why)
{
	for (int i = 0; i < kMaxEffects; ++i)
		if (g_fx[i].used) StopNow(g_fx[i], &g_n.resetStops);
	for (int i = 0; i < kMaxFights; ++i) g_fights[i].used = false;
	memset(g_hashState, 0, sizeof(g_hashState));
	if (why) Log(false, "effect player: reset (%s): every lasting effect stopped", why);
}

// The name's hash; false when the name is not a published effect name (R5 forbids it; counted by the caller).
bool HashOf(uint32_t name, uint32_t& hash)
{
	if (name == 0 || name >= VPTRIG_NAME_CAPACITY) return false;
	if (g_hashState[name] == 1)
	{
		hash = g_hash[name];
		return true;
	}
	if (g_hashState[name] == 2) return false;
	const VPTrigName* n = VPTrigNameAt(g_section, name);
	if (!n) return false;   // not published yet: no cache
	if (n->kind != VPTRIG_NAME_EFFECT || n->length == 0 || n->length >= VPTRIG_NAME_TEXT || n->text[n->length] != 0)
	{
		g_hashState[name] = 2;
		Log(true, "LOUD: effect player: the name %lu is no effect name: its effects are not played", (unsigned long)name);
		return false;
	}
	g_hash[name] = NameHash(n->text, (int)n->length);
	g_hashState[name] = 1;
	hash = g_hash[name];
	return true;
}

float ScaleOf(float s)
{
	return (s > 0 && s < 1e6f) ? s : 1.0f;   // 0, negative, huge or NaN: as authored
}

// 4732: R7's unit-owned form (owner 0 and fightSeq 0: the server's victory tail) is no fight: no fight entry, so no fight
// watchdog and no after-end drop; its one-shots are forgotten as any one-shot, a lasting one ends by its stop record, its
// clip instance's end or the lasting cap.
bool UnitOwned(const VPTrigRecord& r) { return r.owner == 0 && r.fightSeq == 0; }

void DoStart(const VPTrigRecord& r, int64_t frameShown, DWORD now)
{
	++g_n.starts;
	const bool unitOwned = UnitOwned(r);
	if (unitOwned) ++g_n.tailStarts;
	Fight* f = unitOwned ? NULL : FindFight(r.owner, r.fightSeq);
	if (f && f->ended)
	{
		++g_n.afterEnd;   // no effect of an ended fight starts later
		return;
	}
	const bool lasting = (r.flags & VPTRIG_FLAG_FOLLOWS_OWNER) != 0;
	if (r.frameId < frameShown - (lasting ? kStaleLastingFrames : kStaleFrames))
	{
		++g_n.stale;      // never queued for later
		return;
	}
	uint32_t hash = 0;
	if (!HashOf(r.name, hash))
	{
		++g_n.unknownName;
		return;
	}
	if (!unitOwned) f = TouchFight(r.owner, r.fightSeq, now);
	Fx* e = FindFx(r.handle);   // a handle is unique per generation; a repeated start replaces its entry
	for (int i = 0; i < kMaxEffects && !e; ++i)
		if (!g_fx[i].used) e = &g_fx[i];
	if (!e || (!f && !unitOwned) || r.handle == 0)
	{
		if (r.handle == 0)
			++g_n.badRecord;
		else if (g_n.full++ == 0)
			Log(true, "LOUD: effect player: %s: an effect was not started (counted from now on)",
			    e ? "64 fights at once" : "512 effects at once");
		return;
	}
	if (!e->used) ++g_live;
	e->used = true;
	e->kind = lasting ? kPostLasting : kPostOneShot;
	e->handle = r.handle;
	e->hash = hash;
	e->scale = ScaleOf(r.scale);
	e->key[0] = r.handle;
	e->key[1] = g_cursor.generation;
	e->key[2] = kKeyTagLo;
	e->key[3] = kKeyTagHi;
	e->owner = r.owner;
	e->fightSeq = r.fightSeq;
	e->player = r.player;
	e->unit = r.unit;
	e->figure = r.figure;
	e->instance = r.instance;
	e->bornMs = now;
	e->lastMs = now;
	e->movePending = false;
	MatrixOf(r.pos, r.orient, e->m);
	PostPose(*e);
	if (unitOwned) ++g_n.tailPosted;
}

void DoMove(const VPTrigRecord& r, DWORD now)
{
	++g_n.moves;
	Fx* e = FindFx(r.handle);
	if (!e)
	{
		++g_n.unknownHandle;   // a start dropped (stale, unknown name, full) or an ended owner's
		return;
	}
	if (Fight* f = FindFight(e->owner, e->fightSeq))
		if (!f->ended) f->lastMs = now;
	MatrixOf(r.pos, r.orient, e->m);
	e->lastMs = now;
	e->movePending = true;   // posted once at the end of the step, with the newest pose
}

void DoStop(const VPTrigRecord& r, DWORD now)
{
	++g_n.stops;
	if (Fight* f = FindFight(r.owner, r.fightSeq))
		if (!f->ended) f->lastMs = now;
	Fx* e = FindFx(r.handle);
	if (!e)
	{
		++g_n.unknownHandle;
		return;
	}
	if (e->kind == kPostOneShot)
	{
		// a one-shot the server stops (a trigger with a duration and no bone): its key may still hold the instance
		uint32_t stop[kPayloadBytes / 4];
		memset(stop, 0, sizeof(stop));
		memcpy(stop, e->key, sizeof(e->key));
		Post(kPostStop, stop);
		Free(*e);
		return;
	}
	StopNow(*e, NULL);
}

void EndOwned(const VPTrigRecord& r, bool clip)
{
	for (int i = 0; i < kMaxEffects; ++i)
	{
		Fx& e = g_fx[i];
		if (!e.used || !SameFight(e.owner, e.fightSeq, r.owner, r.fightSeq)) continue;
		if (clip && !(e.player == r.player && e.unit == r.unit && e.figure == r.figure && e.instance == r.instance))
			continue;
		StopNow(e, &g_n.orphanStops);   // the server stops its lasting effects before the end: this is the guard
	}
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
		EndOwned(r, false);
		if (Fight* f = TouchFight(r.owner, r.fightSeq, now))
		{
			f->ended = true;
			f->lastMs = now;
		}
		return;
	}
	if (r.reason == VPTRIG_END_CLIP_ENDED)
	{
		if (Fight* f = FindFight(r.owner, r.fightSeq))
			if (!f->ended) f->lastMs = now;
		EndOwned(r, true);
		return;
	}
	++g_n.badRecord;   // checkTriggerRecord forbids any other OwnerEnd
}

// The moves of this step, the watchdogs, the cap and the forgetting.
void Housekeep(DWORD now)
{
	for (int i = 0; i < kMaxEffects; ++i)
	{
		Fx& e = g_fx[i];
		if (e.used && e.movePending)
		{
			e.movePending = false;
			PostPose(e);
			++g_n.movesPosted;
		}
	}
	for (int i = 0; i < kMaxFights; ++i)
	{
		Fight& f = g_fights[i];
		if (!f.used || f.ended || !Elapsed(f.lastMs, now, kSilentMs)) continue;
		const long before = g_n.watchdogStops;
		for (int k = 0; k < kMaxEffects; ++k)
			if (g_fx[k].used && SameFight(g_fx[k].owner, g_fx[k].fightSeq, f.owner, f.fightSeq))
				StopNow(g_fx[k], &g_n.watchdogStops);
		f.ended = true;
		f.lastMs = now;
		Log(true, "LOUD: effect player: fight %lu (seq %lu) sent nothing for %lu ms: its lasting effects are stopped (%ld) "
		    "by the watchdog", (unsigned long)f.owner, (unsigned long)f.fightSeq, (unsigned long)kSilentMs,
		    g_n.watchdogStops - before);
	}
	for (int i = 0; i < kMaxEffects; ++i)
	{
		Fx& e = g_fx[i];
		if (!e.used) continue;
		if (e.kind == kPostLasting && Elapsed(e.bornMs, now, kLastingCapMs))
		{
			if (g_n.capStops++ == 0)
				Log(true, "LOUD: effect player: a lasting effect (handle %lu) lived %lu ms without a stop: stopped (counted "
				    "from now on)", (unsigned long)e.handle, (unsigned long)kLastingCapMs);
			StopNow(e, NULL);
		}
		else if (e.kind == kPostOneShot && Elapsed(e.lastMs, now, kOneShotForgetMs))
			StopNow(e, NULL);   // forgotten (counted), nothing posted
	}
	// an ended fight is forgotten once the stream is long past it
	for (int i = 0; i < kMaxFights; ++i)
	{
		Fight& f = g_fights[i];
		if (!f.used || !f.ended || !Elapsed(f.lastMs, now, kSilentMs)) continue;
		bool any = false;
		for (int k = 0; k < kMaxEffects && !any; ++k)
			any = g_fx[k].used && SameFight(g_fx[k].owner, g_fx[k].fightSeq, f.owner, f.fightSeq);
		if (!any) f.used = false;
	}
}
}  // namespace

void SetSinks(LogFn log, ClockFn clock)
{
	g_log = log;
	g_clock = clock;
}

bool Attach(const void* section, const Poster& poster)
{
	if (g_section) Detach();
	if (!section || !poster.post)
	{
		Log(true, "LOUD: effect player: not attached (%s)", section ? "no poster" : "no section");
		return false;
	}
	g_section = section;
	g_poster = poster;
	g_cursor.next = 1;
	g_cursor.generation = 0;
	g_cursor.lost = 0;
	memset(g_hashState, 0, sizeof(g_hashState));
	Log(false, "effect player: attached");
	return true;
}

void Detach()
{
	if (!g_section) return;
	ResetAll(NULL);
	g_section = NULL;
	char c[600];
	CountersLine(c, sizeof(c));
	Log(false, "effect player: detached; %s", c);
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
		switch (r.kind)
		{
		case VPTRIG_EFFECT_START: DoStart(r, frameShown, now); break;
		case VPTRIG_EFFECT_MOVE: DoMove(r, now); break;
		case VPTRIG_EFFECT_STOP: DoStop(r, now); break;
		case VPTRIG_OWNER_END: DoOwnerEnd(r, now); break;
		case VPTRIG_SOUND_START:
		case VPTRIG_SOUND_STOP: ++g_n.sounds; break;   // the sound executor's (4723), not this player's
		default: ++g_n.badRecord; break;
		}
	}
	Housekeep(now);
}

void CountersLine(char* out, size_t cap)
{
	if (!out || !cap) return;
	_snprintf(out, cap - 1,
	          "effect player: records %ld (starts %ld, moves %ld, stops %ld, owner ends %ld, sounds ignored %ld, bad %ld), "
	          "posted one-shot %ld, lasting %ld (moves %ld), stop %ld, post failed %ld; stale %ld, after end %ld, unknown "
	          "name %ld, unknown handle %ld, full %ld; stopped by the player: orphan %ld, watchdog %ld, cap %ld, reset %ld; "
	          "forgotten %ld, new generations %ld, lost %ld, live now %d, victory tail (unit-owned) starts %ld (posted %ld)",
	          g_n.records, g_n.starts, g_n.moves, g_n.stops, g_n.ownerEnds, g_n.sounds, g_n.badRecord,
	          g_n.posted[kPostOneShot], g_n.posted[kPostLasting], g_n.movesPosted, g_n.posted[kPostStop], g_n.postFailed,
	          g_n.stale, g_n.afterEnd, g_n.unknownName, g_n.unknownHandle, g_n.full, g_n.orphanStops, g_n.watchdogStops,
	          g_n.capStops, g_n.resetStops, g_n.forgotten, g_n.newGen, g_n.lost, g_live, g_n.tailStarts, g_n.tailPosted);
	out[cap - 1] = 0;
}

namespace Test
{
void Reset()
{
	g_section = NULL;
	memset(&g_poster, 0, sizeof(g_poster));
	memset(&g_n, 0, sizeof(g_n));
	memset(g_fx, 0, sizeof(g_fx));
	memset(g_fights, 0, sizeof(g_fights));
	memset(g_hashState, 0, sizeof(g_hashState));
	g_live = 0;
	g_cursor.next = 1;
	g_cursor.generation = 0;
	g_cursor.lost = 0;
}
long Posted(PostKind k) { return (k >= 0 && k < kPostKinds) ? g_n.posted[k] : -1; }
long MovesPosted() { return g_n.movesPosted; }
long PostFailed() { return g_n.postFailed; }
long Stale() { return g_n.stale; }
long AfterEnd() { return g_n.afterEnd; }
long UnknownName() { return g_n.unknownName; }
long UnknownHandle() { return g_n.unknownHandle; }
long Full() { return g_n.full; }
long OrphanStops() { return g_n.orphanStops; }
long WatchdogStops() { return g_n.watchdogStops; }
long CapStops() { return g_n.capStops; }
long ResetStops() { return g_n.resetStops; }
long Forgotten() { return g_n.forgotten; }
long NewGenerations() { return g_n.newGen; }
long Lost() { return g_n.lost; }
long SoundsIgnored() { return g_n.sounds; }
long TailStarts() { return g_n.tailStarts; }
long TailPosted() { return g_n.tailPosted; }
long BadRecords() { return g_n.badRecord; }
int LiveEffects() { return g_live; }
int Fights()
{
	int n = 0;
	for (int i = 0; i < kMaxFights; ++i) n += g_fights[i].used ? 1 : 0;
	return n;
}
}  // namespace Test
}  // namespace ProxyEffectPlayer

#ifndef VP_EFFECT_PLAYER_OFFLINE
// ---- the binding: the EXE's queue through the build table -----------------------------------------------------------
// Run-time guard (exe-build-table.md section 4; exebuild/patterns.txt lists this file). The three allocators are
// 0x53-byte look-alikes that differ only in their record type (effects-formats.md 15.7): each pattern is the whole body,
// so the type byte and the queue's three operands (qg_channel_index, qg_channels, the InterlockedExchangeAdd import slot
// of the queue guard's rows) are checked, and each has a caller row whose E8 C() must lead to it (lead-0590 #8203 /
// #8368: short look-alikes). Read from build 4390913's golden copy (bytes only, no decompiler output, D83). One string
// literal per row (gen_table.py reads the rows as text).
#define VP_EFFECT_PLAYER_PATTERNS(X) \
	X(effect_alloc_oneshot, "EffectQueue_AllocOneShot28", "A1 R(qg_channel_index) 69 C0 80 01 80 00 56 05 R(qg_channels) 8B B0 00 01 80 00 83 E6 01 69 F6 80 00 40 00 03 F0 68 80 00 00 00 56 FF 15 R(qg_iat_interlockedexchangeadd) 8D 84 30 80 00 00 00 33 C9 5E 3B C1 74 13 C7 00 80 00 00 00 C7 40 04 28 00 00 00 89 48 08 89 48 0C 83 C0 10 C3") \
	X(effect_alloc_lasting, "EffectQueue_AllocLasting29", "A1 R(qg_channel_index) 69 C0 80 01 80 00 56 05 R(qg_channels) 8B B0 00 01 80 00 83 E6 01 69 F6 80 00 40 00 03 F0 68 80 00 00 00 56 FF 15 R(qg_iat_interlockedexchangeadd) 8D 84 30 80 00 00 00 33 C9 5E 3B C1 74 13 C7 00 80 00 00 00 C7 40 04 29 00 00 00 89 48 08 89 48 0C 83 C0 10 C3") \
	X(effect_alloc_stop, "EffectQueue_AllocStop2A", "A1 R(qg_channel_index) 69 C0 80 01 80 00 56 05 R(qg_channels) 8B B0 00 01 80 00 83 E6 01 69 F6 80 00 40 00 03 F0 68 80 00 00 00 56 FF 15 R(qg_iat_interlockedexchangeadd) 8D 84 30 80 00 00 00 33 C9 5E 3B C1 74 13 C7 00 80 00 00 00 C7 40 04 2A 00 00 00 89 48 08 89 48 0C 83 C0 10 C3") \
	X(effect_oneshot_call, "ModelTimedTrigger_OneShotCall", "57 8B 78 04 89 74 24 10 89 4C 24 5C 89 54 24 14 E8 C(effect_alloc_oneshot) 8B 4C 24 5C 8B BF 88 00 00 00 8B D8") \
	X(effect_lasting_call, "ProjectileFlight_LastingCall", "B9 10 00 00 00 8D B4 24 8C 00 00 00 8B FB F3 A5 E8 C(effect_alloc_lasting) 8B F3 B9 10 00 00 00 8B F8 F3 A5") \
	X(effect_stop_call, "PillageSmoke_StopCall", "8B 86 88 00 00 00 0B 86 8C 00 00 00 BB 00 00 00 00 74 28 E8 C(effect_alloc_stop) 8B 8E 88 00 00 00 89 08 8B 96 8C 00 00 00 89 50 04")

const ProxyLogic::Settings& ProxyIniSettings();                  // proxy_update.cpp
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp

namespace ProxyEffectPlayer
{
namespace
{
struct PatRow
{
	int sym;
	const char* name;
	const char* bytes;
};
#define VP_EFFECT_PLAYER_ROW(sym, name, bytes) {VPX_##sym, name, bytes},
const PatRow kPatterns[] = {VP_EFFECT_PLAYER_PATTERNS(VP_EFFECT_PLAYER_ROW)};

// The records' payload layout (effects-formats.md 8.1 [C]: the handlers of types 0x28, 0x29 and 0x2A and their stock
// writers) is verified per build, as 4723's FSound layout. Build 26577625: ticket 0618 (w5-0590, research
// new-exe-build-26577625.md C.6.4, lead-0590 #8510) read the allocators, callers, key map, stop and listeners unchanged
// [C]; its own live start and stop there are still owed (effects-formats.md 15.7.5).
struct LayoutRow
{
	int build;
	const char* evidence;
};
const LayoutRow kLayoutVerified[] = {
	{kVpExeBuild4390913, "effects-formats.md 8.1 (4798: the records, their handlers and writers [C]) and 15.7"},
	{kVpExeBuild26577625, "ticket 0618 (new-exe-build-26577625.md C.6.4: the records' path unchanged [C]) and 15.7"},
};

struct ExeQueue
{
	VpEffectAllocFn alloc[kPostKinds];
	volatile LONG faults;  // an access fault in a call or a write: the player turns itself off
};
ExeQueue g_exe;

bool ExePost(void*, PostKind kind, const void* payload)
{
	if (g_exe.faults || kind < 0 || kind >= kPostKinds || !g_exe.alloc[kind]) return false;
	__try
	{
		void* p = g_exe.alloc[kind]();
		if ((ULONG_PTR)p < 0x10000) return false;   // the allocator's null path returns 0x10
		memcpy(p, payload, kPayloadBytes);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		InterlockedIncrement(&g_exe.faults);
		return false;
	}
}
}  // namespace

bool PosterOfExe(ULONG_PTR base, Poster& out, char* why, size_t whyCap)
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
		_snprintf_s(why, whyCap, _TRUNCATE,
		            "the effect records' layout is verified on Steam builds 4390913 and 26577625 only, not on %s",
		            VpExeBuildLabel(build));
		return false;
	}
	const int syms[3] = {VPX_effect_alloc_oneshot, VPX_effect_alloc_lasting, VPX_effect_alloc_stop};
	for (int i = 0; i < 3; ++i)
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
	for (int i = 0; i < 3; ++i) g_exe.alloc[i] = (VpEffectAllocFn)ProxyExe::Addr(build, syms[i], base);
	g_exe.faults = 0;
	out.ctx = NULL;
	out.post = ExePost;
	_snprintf_s(why, whyCap, _TRUNCATE, "%s (layout: %s)", VpExeBuildLabel(build), verified);
	return true;
}

// ---- the glue ----------------------------------------------------------------------------------------------------------
namespace
{
void (*g_prevHook)() = NULL;
volatile LONG g_want = 0;        // SetServerFights: 1 = on, 0 = off (applied on the main thread at the next frame)
volatile LONG g_refused = 0;     // the queue could not be bound: no attach in this proxy load
Poster g_bound;
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

// The D118 report of the non-fatal stage "effect-player" and the helper's popup, on a thread of its own (4723's form).
DWORD WINAPI ReportThread(LPVOID)
{
	const ProxyLogic::Settings& s = ProxyIniSettings();
	ProxyReport::Input in;
	in.stage = "effect-player";
	in.code = g_reportCheck;
	in.details.push_back(g_reportLine);
	in.details.push_back("non-fatal (ticket 4727, D118): the proxy cannot play the server's fight effects (projectiles, "
	                     "hits) through this EXE's effect system; the game goes on, the fights show no particles");
	in.d33 = false;
	in.nonFatal = true;
	in.summary = std::string("The patch cannot show the fights' effects on this game (effect-player: ") + in.code +
	             "); the game goes on without projectiles and hit effects in the fights.";
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
	for (size_t i = 0; i < lines.size(); ++i) ProxyRtInt::Log("effect player: %s", lines[i].c_str());
	if (dir.empty()) dir = root;
	std::string helperLine;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(s.proxyDir), L"popup", dir,
	                                               s.iniFound ? ProxySession::Widen(s.iniPath) : std::wstring(),
	                                               &helperLine);
	ProxyRtInt::Log("effect player: %s%s; the game goes on (non-fatal)", started ? "" : "the popup FAILED to start: ",
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
		ProxyRtInt::Log("effect player: the report's thread could not start (error %lu): no report",
		                (unsigned long)GetLastError());
}

void* MapSection(const wchar_t* base, const wchar_t* suffix, DWORD bytes, HANDLE& map)
{
	wchar_t name[300];
	_snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE, L"%ls%ls", base, suffix);
	map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, bytes, name);   // created or opened
	if (!map) return NULL;
	// READ | WRITE although the player writes nothing: on x86 VPTrigLoad64 is a lock cmpxchg8b, a write access even when
	// the value is unchanged, so a FILE_MAP_READ view faults at the first 64-bit load (live smoke r3, 2026-11-20 15:40:
	// 0xC0000005 writing the Frame view's latestFrameId; effects-formats.md 15.7.7). The reader block stays the sound
	// executor's: the player never stores into the section.
	void* v = MapViewOfFile(map, kSectionViewAccess, 0, 0, bytes);
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

// Main thread: attach (map <base>.Trig and <base>.Frame (read and write views, see MapSection; the player stores nothing); <base> = VPSHELL_SECTION of the EXE's environment,
// which the proxy itself sets for the composite, proxy_shell.h).
void AttachNow()
{
	wchar_t base[256];
	const DWORD n = GetEnvironmentVariableW(L"VPSHELL_SECTION", base, sizeof(base) / sizeof(base[0]));
	if (n == 0 || n >= sizeof(base) / sizeof(base[0]))
	{
		Log(true, "LOUD: effect player: no VPSHELL_SECTION in the EXE's environment (the server renderer is off?): not "
		    "attached");
		InterlockedExchange(&g_want, 0);
		return;
	}
	g_trigView = MapSection(base, VPTRIG_SECTION_SUFFIX, VPTRIG_SECTION_BYTES, g_trigMap);
	HANDLE fm = NULL;
	void* fv = MapSection(base, VPFRAME_SECTION_SUFFIX, VPFRAME_SECTION_BYTES, fm);
	g_frameMap = fm;
	g_frameView = fv;
	if (!g_trigView || !g_frameView || !Attach(g_trigView, g_bound))
	{
		Log(true, "LOUD: effect player: the trigger or frame section could not be mapped (error %lu): not attached",
		    (unsigned long)GetLastError());
		Unmap();
		InterlockedExchange(&g_want, 0);
	}
}

// The frame being shown (4723's rule, unit-combat-server.md 10.1): the frame the composite published this EXE frame,
// if the server has drawn it; else the server's newest. -1 = none yet.
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
	Poster p;
	if (!PosterOfExe((ULONG_PTR)GetModuleHandleA(NULL), p, why, sizeof(why)))
	{
		InterlockedExchange(&g_refused, 1);
		Log(true, "LOUD: effect player: off for this proxy load: %s; the server's fights will show no effects (D118)", why);
		Report("effect-player-bind", why);
		return false;
	}
	g_bound = p;
	Log(false, "effect player: armed on %s: the server's effect records are played at their frame (attach at the next "
	    "frame)", why);
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
		Log(true, "LOUD: effect player: an access fault in the EXE's queue (%ld): turned off for this proxy load",
		    (long)g_exe.faults);
		InterlockedExchange(&g_refused, 1);
		InterlockedExchange(&g_want, 0);
		Detach();   // its stops fail quietly (faults set): the EXE's effects end with their own durations or stay
		g_exe.faults = 0;
		Unmap();
		Report("effect-player-fault", "an access fault in a call or a write of the EXE's effect queue");
		return;
	}
	if (++g_frames % 1800 == 1)
	{
		char c[700];
		CountersLine(c, sizeof(c));
		Log(false, "%s (frame hook alive, frame %u, thread %lu)", c, g_frames, (unsigned long)GetCurrentThreadId());
	}
}

void Init(void (*existingFrameHook)())
{
	g_prevHook = existingFrameHook;
	SetSinks(GlueLog, NULL);
	// TEST ONLY (ticket 4727, until 4782/4795 call SetServerFights): VP64_TEST_EFFECT_PLAYER=1 in the EXE's process arms
	// the player at the boot as route S would. Never set by the product or the installer.
	char v[8] = "";
	const DWORD n = GetEnvironmentVariableA("VP64_TEST_EFFECT_PLAYER", v, sizeof(v));
	if (n > 0 && n < sizeof(v) && strcmp(v, "1") == 0)
	{
		ProxyRtInt::Log("LOUD: TEST ONLY: VP64_TEST_EFFECT_PLAYER=1: the effect player is armed as route S would arm it "
		                "(ticket 4727; never set by the product)");
		SetServerFights(true);
	}
}
}  // namespace ProxyEffectPlayer
#endif  // VP_EFFECT_PLAYER_OFFLINE
