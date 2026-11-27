// proxy_sound_exec.h - route S (D210): the shell's sound executor. On the EXE's main thread, once per frame, it reads
// the server's trigger stream (shell/common/vp_trigger.h, <base>.Trig) up to the frame being shown and executes the sound
// records through the EXE's own 3D play and stop (research/02-exe-internals/exe-audio.md 2, 4, 8); every sound has an
// owner and stops with it (ticket 4723, epic 0042, stage 3 of 4795). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// Design: the Architect's note research/07-architecture-decisions/architect-2026-11-20-route-s-combat.md 1.4-1.6; the
// stream's rules R1-R8 (vp_trigger.h); the frame hook's safe form (research/05-graphics-composition/combat-choreography.md
// 3.5.8); as built: research/05-graphics-composition/unit-combat-server.md section 10.
//
// The rules it keeps:
//   - executes, at frame G, every unread record with frameId <= G in seq order (R2); a SoundStart whose frame lies more
//     than kStaleFrames before G is dropped and counted (droppedStale): a sound is never queued for later, and a play the
//     EXE refuses (no voice, a script rule) is not retried;
//   - every SoundStart names its fight (owner / fightSeq) and its figure and clip instance (R7); the started sound is kept
//     under its handle with them. A fight's OwnerEnd stops the fight's loops at once and lets its one-shots end by
//     themselves (only one still live kRingOutMs later is stopped, 4734);
//     a ClipEnded OwnerEnd does the same for that clip instance's sounds; a SoundStop stops its handle;
//   - no sound of an ended fight starts later (the fight stays in the ended list);
//   - the watchdog: everything of a fight from which no record has come for kSilentMs is stopped (LOUD, counted);
//   - R7's unit-owned form (owner 0 and fightSeq 0: the server's victory tail) is no fight (ticket 4732): its one-shots
//     play without a fight entry, so no watchdog and no after-end applies to them; a loop or a fight OwnerEnd in that
//     form is a bad record (the server sends neither);
//   - a new generation or a stopped server (R6) stops everything and ends every fight with Reset;
//   - a fight OwnerEnd with owner != 0 is passed to the proxy's combat hold as OwnerEnd(owner, played) (R8); owner 0 is
//     counted, never passed on;
//   - bounded work on the frame path: fixed tables, no allocation, no file I/O, no C++ exception out of the hook, no STL.
//
// When it is on: only when route S's server fights are on (SetServerFights(true), the same switch as the combat hold's;
// ticket 4782/4795 calls both), or with the TEST ONLY variable VP64_TEST_SOUND_EXEC=1 of the EXE's process. Off: the frame
// hook returns after one test, nothing is mapped, read or called.
//
// Parts: the core (this header's Attach / Step / Detach on an Audio of plain function pointers) is tested offline on a
// stand-in section and a mock audio (proxy/tests/sound_exec/); the binding (AudioOfExe) is the only part that reads the
// build table (proxy_exe.h: audio_play3d, audio_game_obj, audio_game_vtbl, audio_sound_stop) and calls the EXE; the glue
// (Init, FrameHook, the section mapping, the D118 report) is the only part that needs the proxy runtime.
//
// Toolchain: clang-cl -m32 with VC9 headers (the proxy's), C++03, no STL (and so no _SECURE_SCL question, 4725).
#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

namespace ProxySoundExec
{
const int kMaxSounds = 256;        // live sounds at once (a 12-against-12 fight starts a few per figure and second)
const int kMaxFights = 64;         // fights known at once (live and recently ended), as the hold's table
const DWORD kRingOutMs = 30000;    // a one-shot of an ended owner plays to its natural end; only one still live this
                                   // long after its owner's end is stopped (counted "ring-out"): a guard against a
                                   // looping script sent as a one-shot. Ticket 4734 (lead-0042, 2026-11-21): was 3000
                                   // (note 1.5 rule 3's placeholder), which cut 19 of 748 played one-shots live where
                                   // stock lets every one end by itself (unit-combat-server.md 10.8). Loops are still
                                   // stopped at once at their owner's end.
const DWORD kSilentMs = 22000;     // a fight with no record for this long is ended by the watchdog (the 20 s limit
                                   // with an air figure plus 2 s, the combat hold's watchdog, combat-choreography 3.5.7)
const int64_t kStaleFrames = 3;    // a SoundStart more than this many frames before the frame shown is dropped (0.1 s
                                   // at the 30-fps cap: the server's own catch-up limit, note 1.5 rule 1)
const int kMaxRecordsPerStep = 1024;   // records executed per frame at most; the rest wait for the next frame (their
                                       // frames then count against kStaleFrames)
const int kSweepPerStep = 16;      // sounds checked per frame for a natural end (frees their slots)

// The EXE's audio as the executor uses it. Every function runs on the EXE's main thread. In the product AudioOfExe
// fills it (the 3D play and FSound::Stop through the build table, the script index through the DLL interface's
// ICvEngineUtility1::GetAudioTagIndex); offline the test's mock.
struct Audio
{
	void* ctx;
	int (*scriptIndex)(void* ctx, const char* name);              // AUDIOTAG_3DSCRIPT's index, or -1
	void* (*play)(void* ctx, int script, const float pos[3]);      // the started sound, or NULL (refused, no voice)
	bool (*live)(void* ctx, void* sound, int script);              // the sound object still plays this script
	void (*stop)(void* ctx, void* sound);                          // stop it now
};

// ---- the call types of the EXE's two audio entries (exe-audio.md 2 step 4, 4.1, 8; written from the knowledge base's
// description, D83; no address: the build table's audio_play3d and audio_sound_stop) ------------------------------------
// Both are __thiscall methods, declared __fastcall with an unused EDX (proxy_pacing.cpp's SecsPerTurn form): `this` in
// ECX, every other argument on the stack and popped by the callee, so the stack words declared here must be exactly the
// callee's `ret N` (the 4725 defect class). proxy/tests/sound_exec/sound_exec_abi_test.cpp checks each type's stack
// effect on a stand-in callee with the same `ret N`.
// FAudioGame's 3D play: `this` = the object held at audio_game_obj; 11 stack words, ret 0x2C; the started FSound or 0.
// Arguments as the Sound trigger's handler passed them for a unit's sound, read live (exe-audio.md 9): (script index,
// 0 = no context, &world position x y z (z up), -1.0, -1, -1.0, -1.0, 0.0, -1, -1, -1).
typedef void*(__fastcall* VpAudioPlay3dFn)(void* self, void* edxUnused, int script, void* context, const float* pos,
                                           float a3, int a4, float a5, float a6, float factor, int a8, int a9, int a10);
const unsigned kVpAudioPlay3dStackBytes = 0x2C;
// FSound::Stop: `this` = the FSound; no stack word, ret; a bool in AL.
typedef unsigned char(__fastcall* VpAudioStopFn)(void* self, void* edxUnused);
const unsigned kVpAudioStopStackBytes = 0;
// The FSound fade (ticket 5051): `this` = the FSound; (seconds, a second float, an int), ret 0xC. With seconds 0.0 it only
// requests the end (+0x08 = 4, +0x28 = 1, no Miles call); the manager ends the voice at its next update (AIL_end_sample)
// and reuses it, as for stock's heartbeat. FSound::Stop instead pauses the voice (AIL_stop_sample) and the EXE never
// uses that Miles 3D voice again (64 of them): research unit-combat-server.md 18.24. The executor ends sounds only here.
typedef void(__fastcall* VpAudioFadeFn)(void* self, void* edxUnused, float seconds, float b, int c);
const unsigned kVpAudioFadeStackBytes = 0xC;

typedef void (*LogFn)(const char* line, bool loud);
typedef void (*OwnerEndFn)(uint32_t eventId, bool played);         // R8: ProxyCombatHold::OwnerEnd in the product
typedef DWORD (*ClockFn)();                                         // ms, wraps: GetTickCount in the product

// The sinks (NULL = none; clock NULL = GetTickCount). Before Attach.
void SetSinks(LogFn log, OwnerEndFn ownerEnd, ClockFn clock);
// Ticket 4783 (W24 #8726, MC #8731): the server's start of a fight is its first record here (any kind but OwnerEnd,
// owner != 0), passed once per fight (ProxyCombatHold::FightStarted in the product: the hold's watchdog then runs from
// it). NULL = none.
typedef void (*FightStartFn)(uint32_t eventId);
void SetFightStartSink(FightStartFn fightStart);

// Main thread. section: a mapped <base>.Trig of VPTRIG_SECTION_BYTES (the executor writes only the reader block).
// readerPid: the reader block's readerPid (the EXE's pid). False when the arguments are unusable (counted, logged).
bool Attach(void* section, uint32_t readerPid, const Audio& audio);
// Main thread: stops every live sound, ends every fight (Reset, R8), clears readerPid. Safe when not attached.
void Detach();
bool Attached();
// Main thread, once per frame: frameShown = the frame id G being shown (vp_frame.h), < 0 = none yet (only the
// watchdog and the ring-outs then).
void Step(int64_t frameShown);

// "sound exec: records N (sound starts N, stops N, owner ends N, effects ignored N), played N, refused N, unknown
//  script N, stale N, after end N, full N, stopped N (owner end N, ring-out N, clip N, stop record N, watchdog N, reset
//  N), natural end N, hold OwnerEnd N (owner 0 N), new generations N, lost N, live now N, victory tail (unit-owned)
//  starts N (played N)"
void CountersLine(char* out, size_t cap);

#ifndef VP_SOUND_EXEC_OFFLINE
// ---- the binding (the build table, proxy_exe.h) and the glue (the proxy runtime) ----------------------------------
// The EXE's audio of the image at base, through the build table; false (with the reason) when the build is unknown,
// a symbol is not carried, its layout is not verified on this build, or the game's audio object is not there.
bool AudioOfExe(ULONG_PTR base, Audio& out, char* why, size_t whyCap);
// The product's switch (with the combat hold's: ticket 4782/4795 calls both): on = map <base>.Trig and arm, off = Detach.
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
void Reset();     // forgets everything (attachment, tables, counters; sinks stay)
long Played();
long Refused();
long UnknownScript();
long Stale();
long AfterEnd();
long Full();
long StoppedOwnerEnd();
long StoppedRingOut();
long StoppedClip();
long StoppedRecord();
long StoppedWatchdog();
long StoppedReset();
long NaturalEnd();
long HoldEnds();
long HoldOwnerZero();
long NewGenerations();
long Lost();
long EffectsIgnored();
long TailStarts();   // 4732: unit-owned (owner 0, fightSeq 0) sound starts, and those played
long TailPlayed();
long BadRecords();
int LiveSounds();
int Fights();
}  // namespace Test
}  // namespace ProxySoundExec
