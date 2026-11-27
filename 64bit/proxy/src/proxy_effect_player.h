// proxy_effect_player.h - route S, effects stage A (D210, D211): the shell's effect player. On the EXE's main thread,
// once per frame, it reads the server's trigger stream (shell/common/vp_trigger.h, <base>.Trig) up to the frame being
// shown and plays the effect records on the EXE's own effect system: it posts the records the EXE's Particle system
// listens to on its 4 MB event queue (research/05-graphics-composition/effects-formats.md 8.1: type 0x28 an owned one-shot,
// 0x29 an owned lasting effect, the same key and name again = a move, 0x2A a stop by key). Every effect has an owner and
// stops with it (ticket 4727, epic 0042; the server side is 4793, effects-formats.md 15). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Design: effects-formats.md 15.3 (the proposal) and 15.7 (as built); the pattern is 4723's sound executor
// (proxy_sound_exec.h, unit-combat-server.md 10); the frame hook's safe form: combat-choreography.md 3.5.8.
//
// The rules it keeps:
//   - executes, at frame G, every unread record with frameId <= G in seq order (R2); an EffectStart without FOLLOWS (a
//     one-shot) more than kStaleFrames before G is dropped, one with FOLLOWS (a lasting effect) more than
//     kStaleLastingFrames before G; an effect is never queued for later;
//   - EffectStart: FOLLOWS -> record 0x29 (lasting, owned), else 0x28 (one-shot, owned); the owner key is the server's
//     handle and generation with a tag no stock writer uses; the name hash is the EXE's CRC over the resolved name;
//     the matrix is the record's pos and orient (D3DX row-major), the scale the record's;
//   - EffectMove: the start's type again with the same key and name (B2); several moves of one effect in one frame are
//     posted once, with the newest pose; EffectStop: 0x2A with the key;
//   - owners (R7): a fight's OwnerEnd stops the fight's lasting effects that the server has not stopped (an orphan,
//     counted) and forgets its one-shots (they end by themselves in the EXE); a ClipEnded does the same for that clip
//     instance; no effect of an ended fight starts later;
//   - the watchdog: a fight with no record for kSilentMs has its lasting effects stopped (LOUD); a lasting effect older
//     than kLastingCapMs is stopped (LOUD): no orphan outlives the 20 s fight limit by more than a few seconds;
//   - R7's unit-owned form (owner 0 and fightSeq 0: the server's victory tail) is no fight (ticket 4732): its effects are
//     posted without a fight entry, so no fight watchdog and no after-end applies to them; a fight OwnerEnd in that form
//     is a bad record (the server sends none);
//   - a new generation or a stopped server (R6) stops every lasting effect and forgets everything;
//   - the reader block of the section is the sound executor's (4723): the player keeps its own cursor and never writes
//     the section (read only);
//   - bounded work on the frame path: fixed tables, no allocation, no file I/O, no C++ exception out of the hook, no STL.
//
// When it is on: only when route S's server fights are on (SetServerFights(true), the same switch as the combat hold's
// and the sound executor's; ticket 4782/4795 calls them), or with the TEST ONLY variable VP64_TEST_EFFECT_PLAYER=1 of the
// EXE's process. Off: the frame hook returns after one test, nothing is mapped, read or posted.
//
// Parts: the core (Attach / Step / Detach on a Poster of plain function pointers) is tested offline on a stand-in
// section and a mock poster (proxy/tests/effect_player/); the binding (PosterOfExe) is the only part that reads the
// build table (proxy_exe.h: effect_alloc_oneshot, effect_alloc_lasting, effect_alloc_stop and their caller rows) and
// calls the EXE; the glue (Init, FrameHook, the section mapping, the D118 report) is the only part that needs the proxy
// runtime.
//
// Toolchain: clang-cl -m32 with VC9 headers (the proxy's), C++03, no STL in the core and the binding (4725).
#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

namespace ProxyEffectPlayer
{
const int kMaxEffects = 512;         // effects known at once (lasting, and one-shots until kOneShotForgetMs)
const int kMaxFights = 64;           // fights known at once (live and recently ended), as the hold's table
const DWORD kSilentMs = 22000;       // a fight with no record for this long is ended by the watchdog (4723's value)
const DWORD kLastingCapMs = 25000;   // a lasting effect older than this is stopped (the 20 s limit plus 5 s)
const DWORD kOneShotForgetMs = 10000;  // a one-shot is forgotten this long after its last record (no stop is sent)
const int64_t kStaleFrames = 3;      // a one-shot start more than this many frames before the frame shown is dropped
const int64_t kStaleLastingFrames = 90;  // a lasting start more than this (3 s at the 30-fps cap) is dropped
const int kMaxRecordsPerStep = 1024;  // records executed per frame at most; the rest wait for the next frame

// The three records of the EXE's Particle system (effects-formats.md 8.1). Each is 0x80 bytes with the queue's header;
// the poster writes the 0x70-byte payload after the header.
enum PostKind
{
	kPostOneShot = 0,   // type 0x28: start or move an owned one-shot (freed by the EXE at the end of its duration)
	kPostLasting = 1,   // type 0x29: start or move an owned lasting effect (kept until stopped)
	kPostStop = 2,      // type 0x2A: stop the owner key's effect
	kPostKinds = 3
};

// The payload of 0x28 / 0x29 (and of 0x2A: only the key, at +0x00). Plain data, written whole into the EXE's record.
struct Payload
{
	float m[16];        // +0x00 the world matrix, D3DX row-major (rows = basis vectors, translation at +0x30/+0x34/+0x38)
	uint32_t key[4];    // +0x40 the owner key (16 bytes, two 64-bit words); for 0x2A this sits at +0x00 instead
	uint32_t hash;      // +0x50 the name hash (NameHash)
	float scale;        // +0x54 the scale (the definition's scale times this)
	uint32_t zero[6];   // +0x58 .. +0x70 zero
};
const unsigned kPayloadBytes = 0x70;

// The owner key's tag (the second 64-bit word): 0x56503634 'VP64' over the ticket number. Every stock writer read leaves
// that word 0 or a sign-extended index (effects-formats.md 8.1), so no stock key equals one of the player's.
const uint32_t kKeyTagLo = 0x00004727u;
const uint32_t kKeyTagHi = 0x56503634u;

// The EXE's effect name hash (effects-formats.md 8.1): CRC-32, reflected polynomial 0xEDB88320, start 0xFFFFFFFF, no
// final inversion, over the name's bytes (zlib.crc32(name) ^ 0xFFFFFFFF). len < 0: up to the NUL.
uint32_t NameHash(const char* name, int len = -1);
// The D3DX row-major matrix of a world position and an orientation quaternion (x, y, z, w; normalised here; a zero or
// non-finite one is the identity).
void MatrixOf(const float pos[3], const float q[4], float m[16]);

// The EXE's queue as the player uses it. Every function runs on the EXE's main thread. In the product PosterOfExe fills
// it (the three record allocators through the build table); offline the test's mock.
struct Poster
{
	void* ctx;
	bool (*post)(void* ctx, PostKind kind, const void* payload);   // kPayloadBytes; false = not posted (counted)
};

// ---- the call type of the EXE's three record allocators (effects-formats.md 8.1 and 15.7; written from the knowledge
// base's description, D83; no address: the build table's effect_alloc_*) -----------------------------------------------
// Each takes no argument and returns in EAX the record's payload (record + 0x10) after writing the header
// [0x80][type][0][0]; a plain `ret` (cdecl, nothing to pop). proxy/tests/effect_player/effect_player_abi_test.cpp checks the
// stack effect on a stand-in callee with the same `ret` (the 4725 defect class).
typedef void*(__cdecl* VpEffectAllocFn)();
const unsigned kVpEffectAllocStackBytes = 0;

// The access of the player's views of <base>.Trig and <base>.Frame. READ | WRITE although the player stores nothing: on
// x86 VPTrigLoad64 is a lock cmpxchg8b, a write access even when the value is unchanged, so a FILE_MAP_READ view faults
// at the first 64-bit load (live smoke r3, 2026-11-20; effects-formats.md 15.7.7). effect_player_test maps its stand-in
// section with this value and steps the player on it (the regression test).
const DWORD kSectionViewAccess = FILE_MAP_READ | FILE_MAP_WRITE;

typedef void (*LogFn)(const char* line, bool loud);
typedef DWORD (*ClockFn)();   // ms, wraps: GetTickCount in the product

// The sinks (NULL = none; clock NULL = GetTickCount). Before Attach.
void SetSinks(LogFn log, ClockFn clock);

// Main thread. section: a mapped <base>.Trig of VPTRIG_SECTION_BYTES (never stored into, but the view must be writable on x86: VPTrigLoad64 is a cmpxchg8b; the reader block is the sound
// executor's). False when the arguments are unusable (logged).
bool Attach(const void* section, const Poster& poster);
// Main thread: stops every lasting effect, forgets everything. Safe when not attached.
void Detach();
bool Attached();
// Main thread, once per frame: frameShown = the frame id G being shown (vp_frame.h), < 0 = none yet (only the
// watchdogs then).
void Step(int64_t frameShown);

// "effect player: records N (starts N, moves N, stops N, owner ends N, sounds ignored N, bad N), posted N (one-shot N,
//  lasting N, move N, stop N), refused N, ..."
void CountersLine(char* out, size_t cap);

#ifndef VP_EFFECT_PLAYER_OFFLINE
// ---- the binding (the build table, proxy_exe.h) and the glue (the proxy runtime) ----------------------------------
// The EXE's queue of the image at base, through the build table; false (with the reason) when the build is unknown, a
// symbol is not carried, a pattern does not match, or the records' layout is not verified on this build.
bool PosterOfExe(ULONG_PTR base, Poster& out, char* why, size_t whyCap);
// The product's switch (with the combat hold's and the sound executor's: ticket 4782/4795 calls them): on = map
// <base>.Trig and arm, off = Detach.
bool SetServerFights(bool on);
// Ticket 4783: the D118 report goes to this sink instead (route S's common state, proxy_route_s.h: one report).
void SetReportSink(void (*sink)(const char* check, const char* line));
// proxy_ui_bridge_rt.cpp Init: the sinks, the TEST ONLY switch, the chain. Once per proxy load.
void Init(void (*existingFrameHook)());
// The frame hook the bridge calls: the chained hook first, then the step at the frame shown.
void FrameHook();
#endif

namespace Test
{
void Reset();   // forgets everything (attachment, tables, counters; sinks stay)
long Posted(PostKind k);
long MovesPosted();
long PostFailed();
long Stale();
long AfterEnd();
long UnknownName();
long UnknownHandle();
long Full();
long OrphanStops();        // lasting effects the shell stopped at their owner's end (the server had not)
long WatchdogStops();
long CapStops();
long ResetStops();
long Forgotten();          // one-shots forgotten without a stop
long NewGenerations();
long Lost();
long SoundsIgnored();
long TailStarts();   // 4732: unit-owned (owner 0, fightSeq 0) effect starts, and those posted
long TailPosted();
long BadRecords();
int LiveEffects();
int Fights();
}  // namespace Test
}  // namespace ProxyEffectPlayer
