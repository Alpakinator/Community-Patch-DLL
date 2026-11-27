// effect_player_test.cpp - offline test of the shell's effect player (ticket 4727): proxy/src/proxy_effect_player.cpp's
// core against a stand-in trigger section (shell/common/vp_trigger.h, written here with the server's own helpers) and a
// mock of the EXE's queue (the three record allocators). Nothing of the EXE, no game, no file of the install. Optionally
// replays recorded trigger logs (4721's text form, `R ...` and `N ...` lines, written by 4793's routes) through the
// section and checks the player's invariants on them.
//
//     effect_player_test.exe [--log FILE]... [--expect-fail] [-v]
//
// --expect-fail: one deliberately wrong expectation (the program's own failure path: exit 1, one "FAIL line").
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#define VP_EFFECT_PLAYER_OFFLINE 1
#include "proxy_effect_player.h"
#include "vp_trigger.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ProxyEffectPlayer;

namespace
{
int g_checks = 0, g_failed = 0;
bool g_verbose = false;

void Check(bool ok, const char* what, long a = 0, long b = 0)
{
	++g_checks;
	if (!ok)
	{
		++g_failed;
		printf("FAIL line: %s (%ld, %ld)\n", what, a, b);
	}
	else if (g_verbose)
		printf("ok   %s\n", what);
}
#define EQ(a, b, what) Check((long)(a) == (long)(b), what, (long)(a), (long)(b))
bool Near(float a, float b) { return fabs(a - b) < 1e-5f; }

// ---- the stand-in section ----------------------------------------------------------------------------------------
unsigned char* g_sec = NULL;
uint32_t g_gen = 0;
uint32_t g_handle = 0;

void NewSection()
{
	if (!g_sec) g_sec = (unsigned char*)VirtualAlloc(NULL, VPTRIG_SECTION_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	memset(g_sec, 0, VPTRIG_SECTION_BYTES);
	g_gen = 0;
	g_handle = 0;
}
void ServerInit()
{
	VPTrigInit(g_sec, ++g_gen, 4242);
}

VPTrigRecord Rec(uint8_t kind, int64_t frame, uint32_t owner, uint32_t fseq)
{
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.version = VPTRIG_VERSION;
	r.kind = kind;
	r.frameId = frame;
	r.owner = owner;
	r.fightSeq = fseq;
	r.player = 1;
	r.unit = 8193;
	r.figure = -1;
	r.trigger = -1;
	return r;
}
uint32_t Start(int64_t frame, uint32_t owner, uint32_t name, bool follows, int figure = 2, uint64_t inst = 55,
               uint32_t fseq = 7, float scale = 1.0f)
{
	VPTrigRecord r = Rec(VPTRIG_EFFECT_START, frame, owner, fseq);
	r.flags = follows ? VPTRIG_FLAG_FOLLOWS_OWNER : 0;
	r.figure = figure;
	r.instance = inst;
	r.handle = ++g_handle;
	r.name = name;
	r.pos[0] = 1475.5f;
	r.pos[1] = 292.25f;
	r.pos[2] = 3.0f;
	r.orient[3] = 1.0f;
	r.scale = scale;
	VPTrigWrite(g_sec, r);
	return r.handle;
}
void Move(int64_t frame, uint32_t owner, uint32_t handle, float x, uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_EFFECT_MOVE, frame, owner, fseq);
	r.handle = handle;
	r.figure = 2;
	r.instance = 55;
	r.pos[0] = x;
	r.pos[1] = 10.0f;
	r.pos[2] = 20.0f;
	r.orient[3] = 1.0f;
	VPTrigWrite(g_sec, r);
}
void StopRec(int64_t frame, uint32_t owner, uint32_t handle, uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_EFFECT_STOP, frame, owner, fseq);
	r.handle = handle;
	VPTrigWrite(g_sec, r);
}
void FightEnd(int64_t frame, uint32_t owner, uint8_t reason, uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_OWNER_END, frame, owner, fseq);
	r.reason = reason;
	r.flags = VPTrigPlayedFor(reason) ? VPTRIG_FLAG_PLAYED : 0;
	VPTrigWrite(g_sec, r);
}
void ClipEnd(int64_t frame, uint32_t owner, int figure, uint64_t inst, uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_OWNER_END, frame, owner, fseq);
	r.reason = VPTRIG_END_CLIP_ENDED;
	r.flags = VPTRIG_FLAG_PLAYED;
	r.figure = figure;
	r.instance = inst;
	VPTrigWrite(g_sec, r);
}
void SoundRec(int64_t frame, uint32_t owner)
{
	VPTrigRecord r = Rec(VPTRIG_SOUND_START, frame, owner, 7);
	r.handle = ++g_handle;
	VPTrigWrite(g_sec, r);
}

// ---- the mock queue ----------------------------------------------------------------------------------------------
struct Posted
{
	PostKind kind;
	Payload p;        // for kPostStop the key is at p.m[0..3] (the record's +0x00)
	int frame;
	DWORD ms;
};
const int kMaxPosts = 200000;
Posted* g_posts = NULL;
int g_nPosts = 0;
bool g_postOk = true;
DWORD g_now = 1000;
int g_frame = 0;
int g_loud = 0;

bool MPost(void*, PostKind kind, const void* payload)
{
	if (!g_postOk) return false;
	if (g_nPosts < kMaxPosts)
	{
		Posted& q = g_posts[g_nPosts];
		q.kind = kind;
		memcpy(&q.p, payload, kPayloadBytes);
		q.frame = g_frame;
		q.ms = g_now;
	}
	++g_nPosts;
	return true;
}
DWORD MClock() { return g_now; }
void MLog(const char* line, bool loud)
{
	if (loud) ++g_loud;
	if (g_verbose) printf("  log%s: %s\n", loud ? " LOUD" : "", line);
}
Poster MockPoster()
{
	Poster p;
	p.ctx = NULL;
	p.post = MPost;
	return p;
}
const uint32_t* StopKey(const Posted& q) { return (const uint32_t*)&q.p; }
int PostsOf(PostKind k, int from = 0)
{
	int n = 0;
	for (int i = from; i < g_nPosts && i < kMaxPosts; ++i) n += g_posts[i].kind == k;
	return n;
}

uint32_t g_fxName[8];
void Fresh()
{
	Test::Reset();
	NewSection();
	g_nPosts = 0;
	g_postOk = true;
	g_now = 1000;
	g_frame = 0;
	g_loud = 0;
	SetSinks(MLog, MClock);
	ServerInit();
	g_fxName[0] = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_PARADROP");
	g_fxName[1] = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_ARROW_TRAIL");
	g_fxName[2] = VPTrigAddName(g_sec, VPTRIG_NAME_SOUND3D, "AS3D_SOMETHING");   // not an effect name
	g_fxName[3] = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_ARTILLERY_IMPACT_DIRT");
	Check(Attach(g_sec, MockPoster()), "attach on the stand-in section");
}
// One EXE frame: the clock moves 33 ms, the frame shown is f.
void Frame(int f)
{
	g_frame = f;
	g_now += 33;
	Step(f);
}

// ---- cases ---------------------------------------------------------------------------------------------------------
void CaseHashAndMatrix()
{
	EQ(NameHash("ART_DEF_VEFFECT_PARADROP"), 0x26800651u, "name hash: ART_DEF_VEFFECT_PARADROP = 0x26800651 (8.1)");
	EQ(NameHash(""), 0xFFFFFFFFu, "name hash: the empty name is the start value (no final inversion)");
	EQ(NameHash("ART_DEF_VEFFECT_PARADROPXYZ", 24), 0x26800651u, "name hash: a length is honoured");
	float m[16];
	const float pos[3] = {10.0f, -20.0f, 5.5f};
	const float id[4] = {0, 0, 0, 1};
	MatrixOf(pos, id, m);
	bool ok = true;
	for (int i = 0; i < 12; ++i) ok = ok && Near(m[i], (i % 5 == 0) ? 1.0f : 0.0f);
	Check(ok && Near(m[12], 10.0f) && Near(m[13], -20.0f) && Near(m[14], 5.5f) && Near(m[15], 1.0f),
	      "matrix: identity orientation, translation at +0x30");
	const float s = (float)sqrt(0.5);
	const float rz[4] = {0, 0, s, s};   // 90 degrees about z: x -> y (D3DX row vectors)
	MatrixOf(pos, rz, m);
	Check(Near(m[0], 0) && Near(m[1], 1) && Near(m[2], 0) && Near(m[4], -1) && Near(m[5], 0) && Near(m[10], 1),
	      "matrix: 90 degrees about z maps x to y (row 0 = (0,1,0)), y to -x");
	const float rz2[4] = {0, 0, 2 * s, 2 * s};   // not normalised: normalised here
	float m2[16];
	MatrixOf(pos, rz2, m2);
	Check(Near(m2[1], 1) && Near(m2[4], -1), "matrix: the quaternion is normalised");
	const float zero[4] = {0, 0, 0, 0};
	MatrixOf(pos, zero, m);
	Check(Near(m[0], 1) && Near(m[5], 1) && Near(m[10], 1) && Near(m[1], 0), "matrix: a zero quaternion is the identity");
	const float nanq[4] = {(float)sqrt(-1.0), 0, 0, 1};
	MatrixOf(pos, nanq, m);
	Check(Near(m[0], 1) && Near(m[5], 1) && m[0] == m[0], "matrix: a NaN quaternion is the identity");
	EQ(sizeof(Payload), 0x70, "payload: 0x70 bytes");
}

void CaseFrameRuleAndKinds()
{
	Fresh();
	const uint32_t h1 = Start(10, 7001, g_fxName[3], false, 2, 55, 7, 1.25f);   // a one-shot at frame 10
	const uint32_t h2 = Start(12, 7001, g_fxName[1], true);                      // a lasting one at frame 12
	Frame(9);
	EQ(g_nPosts, 0, "frame rule: nothing at frame 9");
	Frame(10);
	EQ(g_nPosts, 1, "frame rule: the frame-10 start at frame 10");
	Frame(11);
	EQ(g_nPosts, 1, "frame rule: the frame-12 start waits at frame 11");
	Frame(12);
	EQ(g_nPosts, 2, "frame rule: the frame-12 start at frame 12");
	const Posted& a = g_posts[0];
	EQ(a.kind, kPostOneShot, "an EffectStart without FOLLOWS is a 0x28 one-shot");
	EQ(a.p.key[0], h1, "key word 0 = the handle");
	EQ(a.p.key[1], g_gen, "key word 1 = the generation");
	EQ(a.p.key[2], kKeyTagLo, "key word 2 = the tag's low half");
	EQ(a.p.key[3], kKeyTagHi, "key word 3 = the tag's high half (no stock writer's)");
	EQ(a.p.hash, NameHash("ART_DEF_VEFFECT_ARTILLERY_IMPACT_DIRT"), "the hash of the record's name");
	Check(Near(a.p.scale, 1.25f), "the record's scale");
	Check(Near(a.p.m[12], 1475.5f) && Near(a.p.m[13], 292.25f) && Near(a.p.m[14], 3.0f) && Near(a.p.m[0], 1.0f),
	      "the matrix: the record's position, identity orientation");
	bool zero = true;
	for (int i = 0; i < 6; ++i) zero = zero && a.p.zero[i] == 0;
	Check(zero, "the payload's tail is zero");
	EQ(g_posts[1].kind, kPostLasting, "an EffectStart with FOLLOWS is a 0x29 lasting effect");
	EQ(g_posts[1].p.key[0], h2, "the lasting effect's key");
	EQ(Test::LiveEffects(), 2, "both effects known");
	// the reader block is the sound executor's: the player never writes the section
	unsigned char before[128];
	memcpy(before, g_sec + VPTRIG_READER_OFFSET, 128);
	Move(13, 7001, h2, 5.0f);
	Frame(13);
	Check(memcmp(before, g_sec + VPTRIG_READER_OFFSET, 128) == 0, "the reader block is never written by the player");
}

void CaseMoves()
{
	Fresh();
	const uint32_t h = Start(5, 7001, g_fxName[1], true, 2, 55, 7, 0.5f);
	Frame(5);
	EQ(g_nPosts, 1, "moves: the start");
	Move(6, 7001, h, 100.0f);
	Move(6, 7001, h, 200.0f);
	Move(6, 7001, h, 300.0f);
	Frame(6);
	EQ(g_nPosts, 2, "moves: three moves of one effect in one frame are posted once");
	const Posted& m = g_posts[1];
	EQ(m.kind, kPostLasting, "moves: the start's type again (0x29)");
	EQ(m.p.key[0], h, "moves: the same key");
	EQ(m.p.hash, g_posts[0].p.hash, "moves: the same name hash (B2: same key + same name = move)");
	Check(Near(m.p.m[12], 300.0f) && Near(m.p.m[13], 10.0f), "moves: the newest pose");
	Check(Near(m.p.scale, 0.5f), "moves: the start's scale");
	EQ(Test::MovesPosted(), 1, "moves: counted once");
	// a move and a stop in one frame: the stop wins, no move after the stop
	Move(7, 7001, h, 400.0f);
	StopRec(7, 7001, h);
	Frame(7);
	EQ(g_nPosts, 3, "move + stop in one frame: only the stop is posted");
	EQ(g_posts[2].kind, kPostStop, "move + stop: 0x2A");
	EQ(StopKey(g_posts[2])[0], h, "the stop's key at the record's +0x00");
	EQ(StopKey(g_posts[2])[3], kKeyTagHi, "the stop's key: all 16 bytes");
	EQ(Test::LiveEffects(), 0, "the stopped effect is forgotten");
	Move(8, 7001, h, 500.0f);
	StopRec(8, 7001, h);
	Frame(8);
	EQ(g_nPosts, 3, "a move or a stop of a stopped effect posts nothing");
	EQ(Test::UnknownHandle(), 2, "... and is counted as an unknown handle");
	// a one-shot moved: the 0x28 again
	const uint32_t o = Start(9, 7001, g_fxName[3], false);
	Move(10, 7001, o, 7.0f);
	Frame(10);
	EQ(g_posts[g_nPosts - 1].kind, kPostOneShot, "a moved one-shot is re-posted as 0x28");
	EQ(g_posts[g_nPosts - 1].p.key[0], o, "... with its own key");
}

void CaseStale()
{
	Fresh();
	Start(10, 7001, g_fxName[3], false);
	Start(11, 7001, g_fxName[3], false);
	Start(10, 7001, g_fxName[1], true);
	Start(13, 7001, g_fxName[1], true);
	Frame(14);
	EQ(Test::Stale(), 1, "stale: the one-shot 4 frames late is dropped");
	EQ(Test::Posted(kPostOneShot), 1, "stale: the one-shot 3 frames late is posted");
	EQ(Test::Posted(kPostLasting), 2, "stale: lasting starts 4 and 1 frames late are posted");
	Fresh();
	Start(100, 7001, g_fxName[1], true);
	Start(101, 7001, g_fxName[1], true);
	Frame(191);
	EQ(Test::Stale(), 1, "stale: a lasting start 91 frames late is dropped");
	EQ(Test::Posted(kPostLasting), 1, "stale: one 90 frames late is posted");
}

void CaseFightEnd()
{
	Fresh();
	const uint32_t l = Start(1, 7001, g_fxName[1], true);
	const uint32_t o = Start(1, 7001, g_fxName[3], false);
	const uint32_t other = Start(1, 7002, g_fxName[1], true, 2, 55, 8);
	Frame(1);
	EQ(g_nPosts, 3, "fight end: three starts");
	FightEnd(2, 7001, VPTRIG_END_NATURAL);
	Frame(2);
	EQ(PostsOf(kPostStop), 1, "fight end: the fight's lasting effect left running is stopped");
	EQ(StopKey(g_posts[3])[0], l, "fight end: that effect's key");
	EQ(Test::OrphanStops(), 1, "fight end: counted as an orphan (the server stops its own before the end)");
	EQ(Test::Forgotten(), 1, "fight end: the one-shot is forgotten, not stopped (it ends by itself)");
	EQ(Test::LiveEffects(), 1, "fight end: the other fight's effect lives on");
	(void)o;
	Start(3, 7001, g_fxName[3], false);
	Frame(3);
	EQ(Test::AfterEnd(), 1, "fight end: a start of the ended fight is refused");
	EQ(PostsOf(kPostOneShot), 1, "fight end: nothing posted for it");
	FightEnd(4, 7001, VPTRIG_END_NATURAL);
	Frame(4);
	EQ(PostsOf(kPostStop), 1, "a repeated fight end posts nothing");
	// owner 0: the fight is named by its seq
	const uint32_t z = Start(5, 0, g_fxName[1], true, 2, 55, 99);
	Frame(5);
	FightEnd(6, 0, VPTRIG_END_LIMIT, 98);
	Frame(6);
	EQ(PostsOf(kPostStop), 1, "owner 0: another seq's end leaves the effect");
	FightEnd(7, 0, VPTRIG_END_LIMIT, 99);
	Frame(7);
	EQ(PostsOf(kPostStop), 2, "owner 0: its own seq's end stops it");
	EQ(StopKey(g_posts[g_nPosts - 1])[0], z, "owner 0: that key");
	(void)other;
}

void CaseClipEnded()
{
	Fresh();
	const uint32_t a = Start(1, 7001, g_fxName[1], true, 2, 55);
	const uint32_t b = Start(1, 7001, g_fxName[1], true, 2, 56);
	const uint32_t c = Start(1, 7001, g_fxName[1], true, 3, 55);
	Frame(1);
	ClipEnd(2, 7001, 2, 55);
	Frame(2);
	EQ(PostsOf(kPostStop), 1, "clip ended: only that figure's clip instance's lasting effect stops");
	EQ(StopKey(g_posts[g_nPosts - 1])[0], a, "clip ended: its key");
	EQ(Test::LiveEffects(), 2, "clip ended: the fight's other effects live on");
	Start(3, 7001, g_fxName[3], false);
	Frame(3);
	EQ(Test::AfterEnd(), 0, "clip ended: the fight goes on (a start is played)");
	(void)b;
	(void)c;
}

void CaseUnknownNamesAndSounds()
{
	Fresh();
	Start(1, 7001, g_fxName[2], false);   // a sound3d name
	Start(1, 7001, 3000, false);          // not published
	Start(1, 7001, 0, true);              // no name
	SoundRec(1, 7001);
	Frame(1);
	EQ(Test::UnknownName(), 3, "unknown names: a sound name, an unpublished index, none");
	EQ(g_nPosts, 0, "unknown names: nothing posted");
	EQ(Test::SoundsIgnored(), 1, "sound records are the sound executor's");
	const uint32_t later = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_LATE");
	Start(2, 7001, later, false);
	Frame(2);
	EQ(g_nPosts, 1, "a name published later is resolved then (no negative cache of an unpublished index)");
	EQ(g_posts[0].p.hash, NameHash("ART_DEF_VEFFECT_LATE"), "... with its hash");
}

void CaseWatchdogAndCap()
{
	Fresh();
	Start(1, 7001, g_fxName[1], true);
	const uint32_t busy = Start(1, 7002, g_fxName[1], true, 2, 55, 8);
	Frame(1);
	int f = 1;
	// 7002 keeps moving every 30 frames; 7001 sends nothing
	while (g_now < 1000 + kSilentMs + 200)
	{
		++f;
		if (f % 30 == 0) Move(f, 7002, busy, (float)f, 8);
		Frame(f);
	}
	EQ(Test::WatchdogStops(), 1, "watchdog: the silent fight's lasting effect is stopped after 22 s");
	EQ(Test::LiveEffects(), 1, "watchdog: the fight that keeps sending is left alone");
	Check(g_loud >= 1, "watchdog: LOUD");
	while (g_now < 1000 + kLastingCapMs + 200)
	{
		++f;
		if (f % 30 == 0) Move(f, 7002, busy, (float)f, 8);
		Frame(f);
	}
	EQ(Test::CapStops(), 1, "cap: a lasting effect alive 25 s is stopped although its fight sends");
	EQ(Test::LiveEffects(), 0, "cap: nothing left");
	EQ(StopKey(g_posts[g_nPosts - 1])[0], busy, "cap: that effect's key");
	// a one-shot is forgotten 10 s after its last record, without a post
	Fresh();
	Start(1, 7001, g_fxName[3], false);
	Frame(1);
	for (int k = 2; g_now < 1000 + kOneShotForgetMs + 100; ++k) Frame(k);
	EQ(Test::LiveEffects(), 0, "a one-shot is forgotten after 10 s");
	EQ(Test::Forgotten(), 1, "... counted");
	EQ(g_nPosts, 1, "... with no post");
}

// 4732: R7's unit-owned form (owner 0, fightSeq 0: the victory tail) is no fight: no fight entry, no fight watchdog, no
// after-end drop of a later tail effect (the sound executor's twin, W35's 4731 probe).
void CaseVictoryTail()
{
	Fresh();
	Start(1, 0, g_fxName[3], false, 2, 55, 0);   // a tail one-shot
	Frame(1);
	EQ(Test::Posted(kPostOneShot), 1, "tail: the one-shot is posted");
	EQ(Test::Fights(), 0, "tail: no fight entry");
	int f = 1;
	while (g_now < 1000 + kSilentMs + 5000)   // 27 s without a record: past the former watchdog end and 5 s more
		Frame(++f);
	EQ(g_loud, 0, "tail: no watchdog line");
	EQ(Test::WatchdogStops(), 0, "tail: nothing stopped by the watchdog");
	EQ(Test::LiveEffects(), 0, "tail: the one-shot forgotten as any one-shot");
	++f;
	Start(f, 0, g_fxName[3], false, 3, 56, 0);
	Frame(f);
	EQ(Test::Posted(kPostOneShot), 2, "tail: a one-shot 5 s after the former watchdog end is posted");
	EQ(Test::AfterEnd(), 0, "tail: nothing dropped as after end");
	// a lasting tail effect ends by its clip instance's end; a fight end in this form is a bad record
	const int live0 = Test::LiveEffects();   // the one-shot just posted is kept until it is forgotten (10 s)
	++f;
	Start(f, 0, g_fxName[1], true, 4, 57, 0);
	Frame(f);
	EQ(Test::LiveEffects(), live0 + 1, "tail: a lasting tail effect is posted");
	EQ(Test::Posted(kPostLasting), 1, "tail: ... as lasting");
	++f;
	ClipEnd(f, 0, 4, 57, 0);
	FightEnd(f, 0, VPTRIG_END_NATURAL, 0);
	Frame(f);
	EQ(Test::LiveEffects(), live0, "tail: the lasting one stopped by its ClipEnded");
	EQ(Test::BadRecords(), 1, "tail: a fight end in the unit-owned form is a bad record");
	++f;
	Start(f, 0, g_fxName[3], false, 5, 58, 0);
	Frame(f);
	EQ(Test::Posted(kPostOneShot), 3, "tail: still posted after the bad record");
	EQ(Test::TailStarts(), 4, "tail: starts counted");
	EQ(Test::TailPosted(), 4, "tail: posted counted");
	EQ(Test::Fights(), 0, "tail: still no fight entry");
}

void CaseGenerationsAndStop()
{
	Fresh();
	Start(1, 7001, g_fxName[1], true);
	Start(1, 7001, g_fxName[3], false);
	Frame(1);
	ServerInit();   // a new generation (a server restart)
	g_fxName[0] = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_ARROW_TRAIL");   // index 1 now another name
	const uint32_t h = Start(2, 7001, g_fxName[0], true);
	Frame(2);
	EQ(Test::NewGenerations(), 1, "new generation: seen");
	EQ(Test::ResetStops(), 1, "new generation: the lasting effect stopped, the one-shot not");
	EQ(PostsOf(kPostStop), 1, "new generation: one 0x2A");
	EQ(g_posts[g_nPosts - 1].kind, kPostLasting, "new generation: its first start played after the reset");
	EQ(g_posts[g_nPosts - 1].p.hash, NameHash("ART_DEF_VEFFECT_ARROW_TRAIL"), "new generation: the name cache was dropped");
	EQ(g_posts[g_nPosts - 1].p.key[1], 2, "new generation: the key carries generation 2");
	(void)h;
	VPTrigStop(g_sec);   // an orderly server stop: magic 0
	Frame(3);
	EQ(Test::ResetStops(), 2, "server stop: every lasting effect stopped");
	EQ(Test::NewGenerations(), 2, "server stop: counted as a new generation (0)");
	EQ(Test::LiveEffects(), 0, "server stop: nothing left");
	ServerInit();
	g_fxName[0] = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_PARADROP");
	Start(4, 7001, g_fxName[0], true);
	Frame(4);
	EQ(Test::NewGenerations(), 2, "after a stop the next generation is a join, not a third reset");
	EQ(g_posts[g_nPosts - 1].kind, kPostLasting, "after a stop: the new generation's start plays");
	EQ(g_posts[g_nPosts - 1].p.key[1], 3, "after a stop: the key carries generation 3");
}

void CaseLostAndBounded()
{
	Fresh();
	for (unsigned i = 0; i < VPTRIG_CAPACITY + 10; ++i) Start(1, 7001, g_fxName[3], false);
	Frame(1);
	Check(Test::Lost() >= 1, "lost: overwritten records are counted", Test::Lost(), 1);
	EQ(Test::Posted(kPostOneShot), kMaxEffects, "full: 512 effects known at once");
	EQ(Test::Full(), kMaxRecordsPerStep - kMaxEffects, "bounded: 1,024 records in the first frame, the rest of them full");
	const int loud = g_loud;
	Frame(1);
	EQ(Test::Full(), 2 * kMaxRecordsPerStep - kMaxEffects, "bounded: 1,024 more in the next frame");
	Check(loud == 1 && g_loud == 1, "full: LOUD once", g_loud, 1);
}

void CasePostFailureAndDetach()
{
	Fresh();
	g_postOk = false;
	Start(1, 7001, g_fxName[3], false);
	Frame(1);
	EQ(Test::PostFailed(), 1, "a post the queue refuses is counted");
	g_postOk = true;
	Start(2, 7001, g_fxName[1], true);
	Start(2, 7001, g_fxName[1], true);
	Frame(2);
	const int before = g_nPosts;
	Detach();
	EQ(g_nPosts - before, 2, "detach: every lasting effect is stopped");
	Check(!Attached(), "detach: detached");
	Frame(3);
	EQ(g_nPosts - before, 2, "detached: a step does nothing");
	Poster none;
	none.ctx = NULL;
	none.post = NULL;
	Check(!Attach(g_sec, none), "attach refuses a poster without post");
	Check(!Attach(NULL, MockPoster()), "attach refuses no section");
}

// ---- the product's view access (live smoke r3's crash, 2026-11-20: a FILE_MAP_READ view faults in VPTrigLoad64) ------
// No stack objects in the frames that hold __try (MSVC ASan, cycle 9's note).
volatile int64_t g_loadSink = 0;
int LoadFaults(const void* p)
{
	__try
	{
		g_loadSink = VPTrigLoad64((const volatile int64_t*)p);
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 1;
	}
}
int StepFaults(int64_t frame)
{
	__try
	{
		Step(frame);
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 1;
	}
}

void CaseMappedView()
{
	Fresh();
	Detach();
	HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, VPTRIG_SECTION_BYTES, NULL);
	Check(map != NULL, "mapped view: a pagefile section");
	if (!map) return;
	unsigned char* server = (unsigned char*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPTRIG_SECTION_BYTES);
	void* player = MapViewOfFile(map, kSectionViewAccess, 0, 0, VPTRIG_SECTION_BYTES);
	void* readOnly = MapViewOfFile(map, FILE_MAP_READ, 0, 0, VPTRIG_SECTION_BYTES);
	Check(server && player && readOnly, "mapped view: three views");
	if (server && player && readOnly)
	{
		EQ(LoadFaults((const char*)readOnly + 32), 1,
		   "mapped view: a FILE_MAP_READ view faults in VPTrigLoad64 (x86 cmpxchg8b), the r3 crash");
		VPTrigInit(server, 1, 4242);
		const uint32_t n = VPTrigAddName(server, VPTRIG_NAME_EFFECT, "ART_DEF_VEFFECT_PARADROP");
		VPTrigRecord r = Rec(VPTRIG_EFFECT_START, 5, 7001, 7);
		r.handle = 1;
		r.name = n;
		r.scale = 1.0f;
		r.orient[3] = 1.0f;
		VPTrigWrite(server, r);
		unsigned char before[128];
		memcpy(before, server + VPTRIG_READER_OFFSET, 128);
		Check(Attach(player, MockPoster()), "mapped view: attach on the product's view access");
		g_frame = 5;
		EQ(StepFaults(5), 0, "mapped view: a step on a kSectionViewAccess view does not fault");
		EQ(g_nPosts, 1, "mapped view: the start is posted");
		Check(memcmp(before, server + VPTRIG_READER_OFFSET, 128) == 0, "mapped view: the reader block unchanged");
		Detach();
	}
	if (readOnly) UnmapViewOfFile(readOnly);
	if (player) UnmapViewOfFile(player);
	if (server) UnmapViewOfFile(server);
	CloseHandle(map);
}

// ---- recorded trigger logs (4721's text form, written by 4793's routes) ---------------------------------------------
bool Key(const char* line, const char* key, char* out, size_t cap)
{
	char k[32];
	_snprintf(k, sizeof(k), " %s=", key);
	const char* p = strstr(line, k);
	if (!p) return false;
	p += strlen(k);
	size_t n = 0;
	while (p[n] && p[n] != ' ' && p[n] != '\r' && p[n] != '\n' && n + 1 < cap) out[n] = p[n], ++n;
	out[n] = 0;
	return true;
}
int KindOf(const char* s)
{
	const char* k[] = {"None", "SoundStart", "SoundStop", "EffectStart", "EffectMove", "EffectStop", "OwnerEnd"};
	for (int i = 0; i < 7; ++i)
		if (!strcmp(s, k[i])) return i;
	return -1;
}
int ReasonOf(const char* s)
{
	const char* k[] = {"Natural", "Limit", "Refused", "Gone", "Reset", "ClipEnded"};
	for (int i = 0; i < 6; ++i)
		if (!strcmp(s, k[i])) return i;
	return 0;
}

// The posted stream's invariants: a lasting key is never posted (start or move) after its stop, a stop names a key
// that was started, and after the end every lasting key that was started has been stopped.
struct KeyState
{
	uint32_t handle;
	int state;   // 1 = lasting live, 2 = stopped
};
KeyState* g_keys = NULL;
int g_nKeys = 0;
KeyState* FindKey(uint32_t h)
{
	for (int i = 0; i < g_nKeys; ++i)
		if (g_keys[i].handle == h) return &g_keys[i];
	return NULL;
}

void ReplayLog(const char* path)
{
	FILE* f = fopen(path, "rb");
	Check(f != NULL, "--log: the file opens");
	if (!f) return;
	Fresh();
	VPTrigInit(g_sec, ++g_gen, 4242);   // the log's own names: a fresh generation
	Frame(0);
	static char line[2048];
	char v[256];
	int names = 0, recs = 0, starts = 0, lastingStarts = 0, moves = 0, stops = 0, ends = 0;
	int64_t lastFrame = 0;
	while (fgets(line, sizeof(line), f))
	{
		if (line[0] == 'N' && line[1] == ' ')
		{
			unsigned idx = 0;
			char kind[32], text[200];
			if (sscanf(line, "N %u %31s %199[^\r\n]", &idx, kind, text) == 3)
			{
				const uint32_t k = !strcmp(kind, "sound3d") ? 1 : !strcmp(kind, "effect") ? 2 : 3;
				const uint32_t got = VPTrigAddName(g_sec, k, text);
				Check(got == idx, "--log: name index as in the log", (long)got, (long)idx);
				++names;
			}
			continue;
		}
		if (line[0] != 'R' || line[1] != ' ') continue;
		VPTrigRecord r;
		memset(&r, 0, sizeof(r));
		r.version = VPTRIG_VERSION;
		if (Key(line, "F", v, sizeof(v))) r.frameId = _atoi64(v);
		if (Key(line, "kind", v, sizeof(v))) r.kind = (uint8_t)KindOf(v);
		if (Key(line, "reason", v, sizeof(v)) && strcmp(v, "-")) r.reason = (uint8_t)ReasonOf(v);
		if (Key(line, "flags", v, sizeof(v)))
			r.flags = (strstr(v, "loop") ? VPTRIG_FLAG_LOOP : 0) | (strstr(v, "follows") ? VPTRIG_FLAG_FOLLOWS_OWNER : 0) |
			          (strstr(v, "played") ? VPTRIG_FLAG_PLAYED : 0);
		if (Key(line, "owner", v, sizeof(v))) r.owner = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "fseq", v, sizeof(v))) r.fightSeq = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "player", v, sizeof(v))) r.player = atoi(v);
		if (Key(line, "unit", v, sizeof(v))) r.unit = atoi(v);
		if (Key(line, "figure", v, sizeof(v))) r.figure = atoi(v);
		if (Key(line, "handle", v, sizeof(v))) r.handle = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "inst", v, sizeof(v))) r.instance = _strtoui64(v, NULL, 10);
		if (Key(line, "name", v, sizeof(v))) r.name = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "pos", v, sizeof(v))) sscanf(v, "%f,%f,%f", &r.pos[0], &r.pos[1], &r.pos[2]);
		if (Key(line, "scale", v, sizeof(v))) r.scale = (float)atof(v);
		if (Key(line, "orient", v, sizeof(v))) sscanf(v, "%f,%f,%f,%f", &r.orient[0], &r.orient[1], &r.orient[2], &r.orient[3]);
		while (lastFrame < r.frameId - 1) Frame((int)++lastFrame);
		VPTrigWrite(g_sec, r);
		++recs;
		starts += r.kind == VPTRIG_EFFECT_START;
		lastingStarts += r.kind == VPTRIG_EFFECT_START && (r.flags & VPTRIG_FLAG_FOLLOWS_OWNER);
		moves += r.kind == VPTRIG_EFFECT_MOVE;
		stops += r.kind == VPTRIG_EFFECT_STOP;
		ends += r.kind == VPTRIG_OWNER_END && r.figure == -1 && r.instance == 0;
	}
	fclose(f);
	for (int k = 0; k < 120; ++k) Frame((int)++lastFrame);   // 4 s after the last record
	// the posted stream's invariants
	g_nKeys = 0;
	int afterStop = 0, stopUnknown = 0, notStopped = 0;
	for (int i = 0; i < g_nPosts && i < kMaxPosts; ++i)
	{
		const Posted& q = g_posts[i];
		const uint32_t h = q.kind == kPostStop ? StopKey(q)[0] : q.p.key[0];
		KeyState* k = FindKey(h);
		if (q.kind == kPostLasting)
		{
			if (k && k->state == 2) ++afterStop;
			if (!k && g_nKeys < 65536)
			{
				g_keys[g_nKeys].handle = h;
				g_keys[g_nKeys].state = 1;
				++g_nKeys;
			}
		}
		else if (q.kind == kPostStop)
		{
			if (!k) ++stopUnknown;   // a stop of a one-shot key (a trigger with a duration and no bone) is allowed
			else k->state = 2;
		}
	}
	for (int i = 0; i < g_nKeys; ++i) notStopped += g_keys[i].state == 1;
	char c[700];
	CountersLine(c, sizeof(c));
	printf("--log %s: %d names, %d records (effect starts %d, lasting %d, moves %d, stops %d, fight ends %d); posts %d; "
	       "%s\n", path, names, recs, starts, lastingStarts, moves, stops, ends, g_nPosts, c);
	Check(recs > 0, "--log: records read");
	EQ(Test::UnknownName(), 0, "--log: every effect name resolves to a hash");
	EQ(Test::Stale() + Test::AfterEnd(), 0, "--log: nothing stale or after its fight's end (the log's own frames)");
	EQ(afterStop, 0, "--log: no lasting key posted after its stop");
	EQ(notStopped, 0, "--log: every lasting effect started was stopped (by the server or the player)");
	EQ(Test::LiveEffects(), 0, "--log: nothing known 4 s after the last record but forgotten one-shots");
	EQ(Test::Posted(kPostOneShot) + Test::Posted(kPostLasting) - Test::MovesPosted(), starts,
	   "--log: one post per effect start");
	(void)stopUnknown;
}
}  // namespace

int main(int argc, char** argv)
{
	bool expectFail = false;
	const char* logs[64];
	int nLogs = 0;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--expect-fail")) expectFail = true;
		else if (!strcmp(argv[i], "-v")) g_verbose = true;
		else if (!strcmp(argv[i], "--log") && i + 1 < argc && nLogs < 64) logs[nLogs++] = argv[++i];
	}
	g_posts = (Posted*)malloc(sizeof(Posted) * kMaxPosts);
	g_keys = (KeyState*)malloc(sizeof(KeyState) * 65536);
	if (!g_posts || !g_keys) return 2;
	CaseHashAndMatrix();
	CaseFrameRuleAndKinds();
	CaseMoves();
	CaseStale();
	CaseFightEnd();
	CaseClipEnded();
	CaseUnknownNamesAndSounds();
	CaseWatchdogAndCap();
	CaseVictoryTail();
	CaseGenerationsAndStop();
	CaseLostAndBounded();
	CasePostFailureAndDetach();
	CaseMappedView();
	for (int i = 0; i < nLogs; ++i) ReplayLog(logs[i]);
	if (expectFail) EQ(1, 2, "--expect-fail: a deliberately wrong expectation");
	char c[700];
	CountersLine(c, sizeof(c));
	if (g_verbose) printf("%s\n", c);
	printf("effect_player_test: %d/%d checks passed\n", g_checks - g_failed, g_checks);
	free(g_posts);
	free(g_keys);
	return g_failed ? 1 : 0;
}
