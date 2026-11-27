// vp_trigger.h - the trigger stream between the server (x64) and the shell (x86, inside the EXE): one fixed-size record
// per sound or effect trigger and per owner end, stamped with the frame id F of vp_frame.h, written by the server and
// executed by the shell on the EXE's main thread when frame F is inserted (route S, decision D210; ticket 4721, stage 1
// of 4795). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// Design: research/07-architecture-decisions/architect-2026-11-20-route-s-combat.md 1.3-1.6 (what a record carries, the
// frame rule, the catch-up rule, the ownership table); the contract as built and why it is a shared section beside the
// frame section rather than a wire record: research/05-graphics-composition/unit-combat-server.md section 7.
//
// This header holds no EXE address, no symbol of the EXE build table and no game name (D83, D202): names reach the
// stream at run time, read by the server from the player's install, through the name table below. It includes nothing
// but the C/C++ standard headers and <intrin.h> (MSVC, clang-cl), so the shell (x86), the proxy (x86), the server and
// render's c5render (x64) all include it as it is. vp_frame.h, shell_shared.h and exe_camera.h are not changed by it.
//
// Layout: fixed-size fields with explicit padding, identical on x86 and x64 (static_asserts at the end; the test
// render/tests/unitanim/test_trigger_record.cpp checks the offsets again at run time and compiles for x86 too). Every
// field has exactly one writer process, named per block. A fresh section is zero-filled; nobody memsets a section
// another process may have mapped.
//
// ---- Named objects (the session's Local\ namespace; <base> = the version-3 section name, as vp_frame.h) -------------
//   <base>.Trig   this section, VPTRIG_SECTION_BYTES; created-or-opened by BOTH sides (CreateFileMappingW opens an
//                 existing one), so the start order does not matter. The server writes magic last at its init and
//                 clears it at an orderly stop. No event: the shell reads the ring at each insertion (vp_frame.h step 3),
//                 which already waits for the frame whose records it executes.
//
// ---- The rules -------------------------------------------------------------------------------------------------------
//   R1 Order. The server writes records in seq order (1, 2, ...) with frameId never decreasing, and writes every record
//      of frame F BEFORE it marks F's slot READY (vp_frame.h step 2). So at the insertion of frame G every record with
//      frameId <= G is visible to the shell.
//   R2 Execute. At the insertion of frame G (the frame actually taken, exact or a miss), the shell executes, in seq
//      order, every unread record with frameId <= G and stops at the first with frameId > G (it stays for a later
//      insertion). Sound records of a frame that was never shown are executed with the next frame shown; the shell's
//      own staleness rule (note 1.5 item 5: never queue sound for later) is the executor's (ticket 4723).
//   R3 Never blocks. The server never waits for the shell. With a reader attached (reader.readerPid != 0) and fewer
//      than VPTRIG_RESERVE free records, the server drops every record but OwnerEnd (counted droppedFull); OwnerEnd is
//      always written, and if even that overruns, the shell finds records overwritten (counted lostTotal) and its
//      watchdogs (note 1.5 item 3; the proxy hold's watchdog) end what the lost records would have ended.
//   R4 Per-slot seqlock. A record slot's `seq` is 0 while the server writes it and the record's own seq after; the
//      shell copies a slot only when seq equals the seq it expects before and after the copy (VPTrigReadNext below).
//      Seq numbers are never reused within a generation, so the check has no ABA case.
//   R5 Names first. A name (an audio script, an effect, a trigger file) is written into the name table once, before
//      the first record that names it, and published by nameCount. Index 0 means "no name". Names live as long as the
//      generation.
//   R6 Generation. A server that (re)initialises the section takes generation + 1 and starts seq and names from zero.
//      A shell that sees a new generation drops its name cache and treats every owner of the old one as ended with
//      Reset (stops its sounds, drops its effects).
//   R7 Owners. Every SoundStart and EffectStart names its owner: the fight (owner = CombatRecord::returned, the EXE's
//      event id of the RunCombatSim record, the DLL's parent id; 0 = not known, then fightSeq names the fight) and
//      player, unit, figure and clip instance. OwnerEnd with figure -1 and instance 0 ends the fight; OwnerEnd with a
//      figure and an instance (reason ClipEnded) ends that clip instance. The shell stops the loops of an ended owner
//      at once and lets one-shots ring out (note 1.5 item 2).
//   R8 The proxy's hold. A fight OwnerEnd with owner != 0 is passed on as proxy_combat_hold's
//      OwnerEnd(uint32 eventId = owner, bool played = (flags & VPTRIG_FLAG_PLAYED) != 0) (4773, #7862): played is
//      true for Natural and Limit, false for Refused, Gone and Reset (VPTrigPlayedFor). Owner 0 is counted, never
//      passed on.
//   R9 Route S arms as one (ticket 4783, MC ruling #8543 on thread 0042). After each init the server asks
//      (routeSAsk = its generation). The shell's common state (proxy_route_s.h) arms the combat hold, the sound executor
//      and the effect player together, once per EXE process, and answers in the reader block (routeSParts, routeSBuild,
//      routeSState, then routeSGen last). The server writes no record of a generation whose answer is not ARMED; with no
//      answer at its first fight record it decides NOT TAKEN (routeSTaken), and the shell then disarms as one. An answer
//      may later turn DISARMED (the hold's watchdog, a part's fault): the server then writes no further record.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define VPTRIG_MAGIC 0x47545056u          /* 'VPTG' */
#define VPTRIG_VERSION 1u                 /* the section's and every record's version */
#define VPTRIG_SECTION_SUFFIX L".Trig"
#define VPTRIG_RECORD_BYTES 128u
#define VPTRIG_NAME_BYTES 128u
#define VPTRIG_NAME_TEXT 120u             /* a name's bytes including its NUL: at most 119 characters */
#define VPTRIG_CAPACITY 4096u             /* records in the ring (a power of two) */
#define VPTRIG_NAME_CAPACITY 4096u        /* name slots; index 0 is never used */
#define VPTRIG_RESERVE 256u               /* R3: free records kept for OwnerEnd while a reader is attached */
#define VPTRIG_SERVER_OFFSET 0u
#define VPTRIG_READER_OFFSET 128u
#define VPTRIG_NAMES_OFFSET 256u
#define VPTRIG_RECORDS_OFFSET (VPTRIG_NAMES_OFFSET + VPTRIG_NAME_CAPACITY * VPTRIG_NAME_BYTES)   /* 524544 */
#define VPTRIG_SECTION_BYTES (VPTRIG_RECORDS_OFFSET + VPTRIG_CAPACITY * VPTRIG_RECORD_BYTES)    /* 1048832 */

// The record kinds (note 1.3).
enum VPTrigKind
{
	VPTRIG_NONE = 0,
	VPTRIG_SOUND_START = 1,   // start the audio script `name` at pos; handle names it for a later stop
	VPTRIG_SOUND_STOP = 2,    // stop the sound `handle` (an explicit stop; loops also stop with their owner's end)
	VPTRIG_EFFECT_START = 3,  // start the effect `name` at pos with orient, scale, vel; handle names it
	VPTRIG_EFFECT_MOVE = 4,   // move the effect `handle` to pos / orient / vel (a projectile, an effect that follows)
	VPTRIG_EFFECT_STOP = 5,   // stop the effect `handle`
	VPTRIG_OWNER_END = 6,     // the owner ended (R7, R8): reason, played
	VPTRIG_KIND_COUNT = 7
};

// OwnerEnd's reason: the fight clock's FightEnd values (server/src/units/srv_units_fights.h, unit-combat-server.md
// section 4.1), number for number, so the emitter (4722) casts without a table.
enum VPTrigReason
{
	VPTRIG_END_NATURAL = 0,     // the choreography ended by itself
	VPTRIG_END_LIMIT = 1,       // the 12 s / 20 s limit
	VPTRIG_END_REFUSED = 2,     // the server refused the fight (D118: the server's log names why)
	VPTRIG_END_GONE = 3,        // a unit of the fight left the scene
	VPTRIG_END_RESET = 4,       // a load or a reset of the unit table
	VPTRIG_END_CLIP_ENDED = 5,  // a clip instance of a fighting figure ended or was cut (figure and instance set)
	VPTRIG_END_COUNT = 6
};

// flags
#define VPTRIG_FLAG_LOOP 0x1u           // the script or effect loops (it needs an owner: R7; stopped by its owner's end)
#define VPTRIG_FLAG_FOLLOWS_OWNER 0x2u  // the effect or sound follows its figure (EffectMove / later starts carry the pose)
#define VPTRIG_FLAG_PLAYED 0x4u         // OwnerEnd only: R8's played (set exactly when VPTrigPlayedFor(reason))
#define VPTRIG_FLAGS_KNOWN 0x7u

// R9: the shell's answer (reader block routeSState) and the server's decision (server block routeSTaken).
enum VPTrigRouteSState
{
	VPTRIG_ROUTES_NONE = 0,       // no answer yet
	VPTRIG_ROUTES_ARMED = 1,      // the hold, the sound executor and the effect player are armed and attached
	VPTRIG_ROUTES_NOT_ARMED = 2,  // a part refused or did not answer in time: none is armed (stock fights)
	VPTRIG_ROUTES_DISARMED = 3    // was armed; disarmed as one (the hold's watchdog, a part's fault, the server's NOT TAKEN)
};
enum VPTrigRouteSTaken
{
	VPTRIG_TAKEN_UNDECIDED = 0,
	VPTRIG_TAKEN_YES = 1,         // the answer for routeSAsk was ARMED: the server sends its records
	VPTRIG_TAKEN_NO_ANSWER = 2,   // no answer at the server's first fight record: no record for this generation
	VPTRIG_TAKEN_REFUSED = 3      // the answer was NOT_ARMED or DISARMED: no record
};
// routeSParts: bit p = part p answered armed, bit p + 8 = part p answered at all (p: 0 hold, 1 sound executor, 2 effect
// player).
#define VPTRIG_PART_HOLD 0x1u
#define VPTRIG_PART_SOUND 0x2u
#define VPTRIG_PART_EFFECT 0x4u
#define VPTRIG_PARTS_ALL 0x7u
#define VPTRIG_PART_ANSWERED_SHIFT 8u

// The name table's kinds.
enum VPTrigNameKind
{
	VPTRIG_NAME_NONE = 0,
	VPTRIG_NAME_SOUND3D = 1,      // a 3D audio script id (Audio_3DSounds.ScriptID), $(WEAPON) / $(TERRAIN) resolved
	VPTRIG_NAME_EFFECT = 2,       // an effect name (a VisEffect / particle effect row)
	VPTRIG_NAME_TRIGGERFILE = 3,  // the trigger file a record's trigger comes from (for the log)
	VPTRIG_NAME_KIND_COUNT = 4
};

#pragma pack(push, 8)

// ---- one record (writer: server), VPTRIG_CAPACITY of them at VPTRIG_RECORDS_OFFSET ------------------------------------
struct VPTrigRecord
{
	uint64_t seq;          //   0 the record's number in this generation (from 1); the slot's seqlock (R4)
	int64_t frameId;       //   8 F: the frame whose insertion executes it (vp_frame.h's frame id)
	uint16_t version;      //  16 VPTRIG_VERSION
	uint8_t kind;          //  18 VPTrigKind
	uint8_t reason;        //  19 OwnerEnd: VPTrigReason; 0 otherwise
	uint32_t flags;        //  20 VPTRIG_FLAG_*
	uint32_t owner;        //  24 the fight's event id (CombatRecord::returned); 0 = not known
	uint32_t fightSeq;     //  28 the combat record's seq (names the fight when owner is 0); 0 = no fight
	int32_t player;        //  32 the figure's unit: owner player (-1 = none)
	int32_t unit;          //  36 ... and unit id (-1 = none)
	int32_t figure;        //  40 the figure (member order; -1 = the unit or the fight itself)
	uint32_t handle;       //  44 the server's handle of a sound or effect (from 1, unique per generation); 0 = none
	uint64_t instance;     //  48 the clip instance that fired it (FigureProgram::instance; 0 = the fight itself)
	uint32_t name;         //  56 name table index of the script or effect (0 = none)
	uint32_t file;         //  60 name table index of the trigger file (VPTRIG_NAME_TRIGGERFILE; 0 = none)
	float pos[3];          //  64 world position x, y, z (z up; HexToWorld's order, the 3D play's &pos)
	float scale;           //  76 effects: scale (1 = as authored); 0 for sound and OwnerEnd
	float orient[4];       //  80 effects: orientation quaternion x, y, z, w; 0 for sound and OwnerEnd
	float vel[3];          //  96 effects: velocity in world units per second; 0 for sound and OwnerEnd
	int32_t ec;            // 108 the trigger's identity: the ec (action code) of its clip (0 = none)
	int32_t trigger;       // 112 ... and its trigger id in the file (-1 = none)
	float clipTime;        // 116 the trigger's time in its clip (s)
	float late;            // 120 how late it is against frame F's time (s, >= 0); one-shots > 0.1 s are dropped by
	                       //     the server (note 1.5 item 1), so the log shows what passed
	uint32_t reserved0;    // 124 0
};

// ---- one name (writer: server), VPTRIG_NAME_CAPACITY of them at VPTRIG_NAMES_OFFSET; index 0 unused -------------------
struct VPTrigName
{
	uint32_t kind;                 // VPTrigNameKind
	uint32_t length;               // characters before the NUL (1..VPTRIG_NAME_TEXT - 1)
	char text[VPTRIG_NAME_TEXT];   // NUL-terminated, the rest zero
};

// ---- server block (writer: server) ------------------------------------------------------------------------------------
struct VPTrigServer
{
	uint32_t magic;                 //   0 VPTRIG_MAGIC, written last at init; 0 after an orderly stop
	uint32_t version;               //   4 VPTRIG_VERSION
	uint32_t recordBytes;           //   8 VPTRIG_RECORD_BYTES
	uint32_t nameBytes;             //  12 VPTRIG_NAME_BYTES
	uint32_t capacity;              //  16 VPTRIG_CAPACITY
	uint32_t nameCapacity;          //  20 VPTRIG_NAME_CAPACITY
	uint32_t generation;            //  24 R6: +1 at each init of the stream (from 1)
	uint32_t serverPid;             //  28
	volatile int64_t writeSeq;      //  32 the newest record's seq (0 = none yet); written after the record (R4)
	volatile uint32_t nameCount;    //  40 names published: indexes 1..nameCount are valid (R5)
	uint32_t pad0;                  //  44
	volatile int64_t droppedFull;   //  48 R3: records dropped because the reader was behind
	volatile int64_t droppedLate;   //  56 the emitter's catch-up drops (note 1.5 item 1; written by 4722)
	volatile int64_t namesRefused;  //  64 names too long, empty, with control characters, or past the table
	volatile uint32_t routeSAsk;    //  72 R9: the generation for which the server asks route S (0 = no question)
	volatile uint32_t routeSTaken;  //  76 R9: the server's decision for routeSAsk: VPTrigRouteSTaken
	uint32_t reserved[12];          //  80
};

// ---- reader block (writer: shell) -------------------------------------------------------------------------------------
struct VPTrigReader
{
	volatile int64_t readSeq;       //   0 the newest record the shell consumed (R3's free count: capacity - (writeSeq - readSeq))
	volatile int64_t lostTotal;     //   8 records overwritten before the shell read them (R3)
	volatile int64_t executedTotal; //  16 records executed
	volatile int64_t droppedStale;  //  24 records the executor dropped by its own rule (R2; 4723)
	volatile uint32_t readerPid;    //  32 0 = no reader attached (the server then never drops for R3; nor while
	                                //     generationSeen is not the server's generation)
	volatile uint32_t generationSeen; // 36 the generation the reader works on
	volatile uint32_t routeSGen;    //  40 R9: the generation the answer is for (written LAST, after the three below)
	volatile uint32_t routeSState;  //  44 R9: VPTrigRouteSState (may change later for the same generation: disarmed)
	volatile uint32_t routeSParts;  //  48 R9: VPTRIG_PART_* bits: each part's answer (armed) and that it answered
	volatile uint32_t routeSBuild;  //  52 R9: the EXE's Steam build number (4390913, 26577625; 0 = not a known build)
	uint32_t reserved[18];          //  56
};

#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(VPTrigRecord) == VPTRIG_RECORD_BYTES, "record size");
static_assert(alignof(VPTrigRecord) == 8, "record alignment");
static_assert(offsetof(VPTrigRecord, seq) == 0, "layout");
static_assert(offsetof(VPTrigRecord, frameId) == 8, "layout");
static_assert(offsetof(VPTrigRecord, version) == 16, "layout");
static_assert(offsetof(VPTrigRecord, kind) == 18, "layout");
static_assert(offsetof(VPTrigRecord, reason) == 19, "layout");
static_assert(offsetof(VPTrigRecord, flags) == 20, "layout");
static_assert(offsetof(VPTrigRecord, owner) == 24, "layout");
static_assert(offsetof(VPTrigRecord, fightSeq) == 28, "layout");
static_assert(offsetof(VPTrigRecord, player) == 32, "layout");
static_assert(offsetof(VPTrigRecord, unit) == 36, "layout");
static_assert(offsetof(VPTrigRecord, figure) == 40, "layout");
static_assert(offsetof(VPTrigRecord, handle) == 44, "layout");
static_assert(offsetof(VPTrigRecord, instance) == 48, "layout");
static_assert(offsetof(VPTrigRecord, name) == 56, "layout");
static_assert(offsetof(VPTrigRecord, file) == 60, "layout");
static_assert(offsetof(VPTrigRecord, pos) == 64, "layout");
static_assert(offsetof(VPTrigRecord, scale) == 76, "layout");
static_assert(offsetof(VPTrigRecord, orient) == 80, "layout");
static_assert(offsetof(VPTrigRecord, vel) == 96, "layout");
static_assert(offsetof(VPTrigRecord, ec) == 108, "layout");
static_assert(offsetof(VPTrigRecord, trigger) == 112, "layout");
static_assert(offsetof(VPTrigRecord, clipTime) == 116, "layout");
static_assert(offsetof(VPTrigRecord, late) == 120, "layout");
static_assert(offsetof(VPTrigRecord, reserved0) == 124, "layout");
static_assert(sizeof(VPTrigName) == VPTRIG_NAME_BYTES, "name size");
static_assert(offsetof(VPTrigName, text) == 8, "layout");
static_assert(sizeof(VPTrigServer) == 128, "server block size");
static_assert(alignof(VPTrigServer) == 8, "server block alignment");
static_assert(offsetof(VPTrigServer, generation) == 24, "layout");
static_assert(offsetof(VPTrigServer, writeSeq) == 32, "layout");
static_assert(offsetof(VPTrigServer, nameCount) == 40, "layout");
static_assert(offsetof(VPTrigServer, droppedFull) == 48, "layout");
static_assert(offsetof(VPTrigServer, namesRefused) == 64, "layout");
static_assert(sizeof(VPTrigReader) == 128, "reader block size");
static_assert(offsetof(VPTrigReader, readerPid) == 32, "layout");
static_assert(offsetof(VPTrigReader, generationSeen) == 36, "layout");
static_assert(offsetof(VPTrigServer, routeSAsk) == 72, "layout");
static_assert(offsetof(VPTrigServer, routeSTaken) == 76, "layout");
static_assert(offsetof(VPTrigReader, routeSGen) == 40, "layout");
static_assert(offsetof(VPTrigReader, routeSState) == 44, "layout");
static_assert(offsetof(VPTrigReader, routeSParts) == 48, "layout");
static_assert(offsetof(VPTrigReader, routeSBuild) == 52, "layout");
static_assert(VPTRIG_SERVER_OFFSET + sizeof(VPTrigServer) <= VPTRIG_READER_OFFSET, "server block past the reader's");
static_assert(VPTRIG_READER_OFFSET + sizeof(VPTrigReader) <= VPTRIG_NAMES_OFFSET, "reader block past the names");
static_assert((VPTRIG_CAPACITY & (VPTRIG_CAPACITY - 1)) == 0, "capacity is a power of two");
static_assert(VPTRIG_RESERVE < VPTRIG_CAPACITY, "reserve");
static_assert(VPTRIG_RECORDS_OFFSET % 8 == 0, "records aligned");
static_assert(VPTRIG_SECTION_BYTES == 1048832u, "section size");
#endif

#define VPTRIG_SERVER(p) ((VPTrigServer*)((char*)(p) + VPTRIG_SERVER_OFFSET))
#define VPTRIG_READER(p) ((VPTrigReader*)((char*)(p) + VPTRIG_READER_OFFSET))
#define VPTRIG_NAME(p, i) ((VPTrigName*)((char*)(p) + VPTRIG_NAMES_OFFSET + (size_t)(i) * VPTRIG_NAME_BYTES))
#define VPTRIG_SLOT(p, seq) \
	((VPTrigRecord*)((char*)(p) + VPTRIG_RECORDS_OFFSET + (size_t)((uint64_t)(seq) & (VPTRIG_CAPACITY - 1u)) * VPTRIG_RECORD_BYTES))

#ifdef __cplusplus
// No <atomic>: the proxy (x86) builds against the VC9 headers (proxy/build_proxy.py), which have none. Both targets are
// x86 / x64 (stores are not reordered with stores, loads not with loads), so a compiler barrier between the volatile
// accesses gives the release / acquire order the rules need; nothing here needs store -> load order.
#include <intrin.h>
#if defined(__clang__) || defined(__GNUC__)
#define VPTRIG_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define VPTRIG_BARRIER() _ReadWriteBarrier()
#endif

// R8: played for an OwnerEnd reason.
static inline bool VPTrigPlayedFor(uint8_t reason)
{
	return reason == VPTRIG_END_NATURAL || reason == VPTRIG_END_LIMIT || reason == VPTRIG_END_CLIP_ENDED;
}
// A fight's own OwnerEnd (R7), as opposed to a clip instance's.
static inline bool VPTrigIsFightEnd(const VPTrigRecord& r)
{
	return r.kind == VPTRIG_OWNER_END && r.figure == -1 && r.instance == 0;
}

// 64-bit loads and stores of the shared counters: atomic on x86 too (a plain 64-bit access is two on x86).
static inline int64_t VPTrigLoad64(const volatile int64_t* p)
{
#if defined(_M_IX86) || defined(__i386__)
	return _InterlockedCompareExchange64(const_cast<volatile long long*>(reinterpret_cast<const volatile long long*>(p)), 0, 0);
#else
	return *p;
#endif
}
static inline void VPTrigStore64(volatile int64_t* p, int64_t v)
{
#if defined(_M_IX86) || defined(__i386__)
	volatile long long* q = reinterpret_cast<volatile long long*>(p);
	long long old = *q;
	for (;;)
	{
		long long seen = _InterlockedCompareExchange64(q, v, old);
		if (seen == old) break;
		old = seen;
	}
#else
	*p = v;
#endif
}

// ---- server side -------------------------------------------------------------------------------------------------------
// Init (R6): the server, once per stream, on a mapped section of VPTRIG_SECTION_BYTES. The records and names of an
// older generation stay in place but are void (seq and nameCount restart; every slot's seq is cleared first).
static inline void VPTrigInit(void* section, uint32_t generation, uint32_t serverPid)
{
	VPTrigServer* s = VPTRIG_SERVER(section);
	s->magic = 0;
	VPTRIG_BARRIER();
	for (uint32_t i = 0; i < VPTRIG_CAPACITY; ++i)
		VPTrigStore64(reinterpret_cast<volatile int64_t*>(&VPTRIG_SLOT(section, i)->seq), 0);
	s->version = VPTRIG_VERSION;
	s->recordBytes = VPTRIG_RECORD_BYTES;
	s->nameBytes = VPTRIG_NAME_BYTES;
	s->capacity = VPTRIG_CAPACITY;
	s->nameCapacity = VPTRIG_NAME_CAPACITY;
	s->generation = generation;
	s->serverPid = serverPid;
	VPTrigStore64(&s->writeSeq, 0);
	s->nameCount = 0;
	VPTrigStore64(&s->droppedFull, 0);
	VPTrigStore64(&s->droppedLate, 0);
	VPTrigStore64(&s->namesRefused, 0);
	s->routeSAsk = 0;      // R9: no question until VPTrigAskRouteS
	s->routeSTaken = VPTRIG_TAKEN_UNDECIDED;
	VPTRIG_BARRIER();
	s->magic = VPTRIG_MAGIC;
}
// Orderly stop: magic cleared (the shell then ends every owner with Reset, R6).
static inline void VPTrigStop(void* section)
{
	VPTRIG_SERVER(section)->magic = 0;
}
// R9, server: ask route S for the section's generation (after VPTrigInit).
static inline void VPTrigAskRouteS(void* section)
{
	VPTrigServer* s = VPTRIG_SERVER(section);
	s->routeSTaken = VPTRIG_TAKEN_UNDECIDED;
	VPTRIG_BARRIER();
	s->routeSAsk = s->generation;
}
// R9, server: the shell's answer for this generation (VPTRIG_ROUTES_NONE when none yet); *parts and *build may be NULL.
static inline uint32_t VPTrigRouteSAnswer(const void* section, uint32_t* parts, uint32_t* build)
{
	const VPTrigServer* s = VPTRIG_SERVER(section);
	const VPTrigReader* r = VPTRIG_READER(section);
	const uint32_t gen = r->routeSGen;
	VPTRIG_BARRIER();
	if (gen == 0 || gen != s->generation) return VPTRIG_ROUTES_NONE;
	const uint32_t state = r->routeSState;
	if (parts) *parts = r->routeSParts;
	if (build) *build = r->routeSBuild;
	return state;
}
// R9, shell: publish the answer for generation gen (parts and build first, the generation last).
static inline void VPTrigPublishRouteS(void* section, uint32_t gen, uint32_t state, uint32_t parts, uint32_t build)
{
	VPTrigReader* r = VPTRIG_READER(section);
	r->routeSParts = parts;
	r->routeSBuild = build;
	r->routeSState = state;
	VPTRIG_BARRIER();
	r->routeSGen = gen;
}
// R5: publish a name; its index (1..), or 0 when refused (empty, too long, a control character, the table full).
static inline uint32_t VPTrigAddName(void* section, uint32_t kind, const char* text)
{
	VPTrigServer* s = VPTRIG_SERVER(section);
	size_t n = text ? strlen(text) : 0;
	bool ok = kind > VPTRIG_NAME_NONE && kind < VPTRIG_NAME_KIND_COUNT && n > 0 && n < VPTRIG_NAME_TEXT &&
	          s->nameCount + 1 < VPTRIG_NAME_CAPACITY;
	for (size_t i = 0; ok && i < n; ++i)
		if (static_cast<unsigned char>(text[i]) < 0x20 || text[i] == 0x7f) ok = false;
	if (!ok)
	{
		VPTrigStore64(&s->namesRefused, VPTrigLoad64(&s->namesRefused) + 1);
		return 0;
	}
	uint32_t index = s->nameCount + 1;
	VPTrigName* e = VPTRIG_NAME(section, index);
	memset(e, 0, sizeof(*e));
	e->kind = kind;
	e->length = static_cast<uint32_t>(n);
	memcpy(e->text, text, n);
	VPTRIG_BARRIER();
	s->nameCount = index;
	return index;
}
// R1, R3, R4: write one record (its seq is assigned here; the caller fills the rest, version included). False when R3
// dropped it (counted). The server's one writer thread only.
static inline bool VPTrigWrite(void* section, const VPTrigRecord& rec)
{
	VPTrigServer* s = VPTRIG_SERVER(section);
	const VPTrigReader* r = VPTRIG_READER(section);
	int64_t seq = VPTrigLoad64(&s->writeSeq) + 1;
	if (r->readerPid != 0 && r->generationSeen == s->generation && rec.kind != VPTRIG_OWNER_END)
	{
		int64_t unread = (seq - 1) - VPTrigLoad64(&r->readSeq);
		if (unread < 0) unread = 0;
		if (unread > static_cast<int64_t>(VPTRIG_CAPACITY - VPTRIG_RESERVE))
		{
			VPTrigStore64(&s->droppedFull, VPTrigLoad64(&s->droppedFull) + 1);
			return false;
		}
	}
	VPTrigRecord* slot = VPTRIG_SLOT(section, seq);
	volatile int64_t* slotSeq = reinterpret_cast<volatile int64_t*>(&slot->seq);
	VPTrigStore64(slotSeq, 0);   // R4: being written
	VPTRIG_BARRIER();
	VPTrigRecord copy = rec;
	copy.seq = 0;
	memcpy(slot, &copy, sizeof(copy));
	VPTRIG_BARRIER();
	VPTrigStore64(slotSeq, seq);
	VPTRIG_BARRIER();
	VPTrigStore64(&s->writeSeq, seq);
	return true;
}

// ---- shell side --------------------------------------------------------------------------------------------------------
// The reader's own position (kept by the shell, not in the section; the section's reader block mirrors it).
struct VPTrigCursor
{
	int64_t next;         // the next seq to read (from 1)
	uint32_t generation;  // the generation `next` belongs to (0 = none yet)
	int64_t lost;         // records found overwritten (R3)
};
enum VPTrigReadResult
{
	VPTRIG_READ_NONE = 0,     // nothing to execute now (no record, or the next one is for a later frame: R2)
	VPTRIG_READ_OK = 1,       // `out` holds the next record; the cursor moved past it
	VPTRIG_READ_NEWGEN = 2,   // R6: a new generation (or the server stopped: magic 0); the cursor restarted; end every
	                          // owner with Reset and drop the name cache, then read again
	VPTRIG_READ_LOST = 3,     // R3: records were overwritten unread; the cursor jumped past them (lost counts them)
};
// R2, R4, R6: the next record with frameId <= maxFrame. Call in a loop at the insertion of frame maxFrame until NONE.
static inline VPTrigReadResult VPTrigReadNext(const void* section, VPTrigCursor& c, int64_t maxFrame, VPTrigRecord& out)
{
	const VPTrigServer* s = VPTRIG_SERVER(section);
	uint32_t magic = s->magic;
	VPTRIG_BARRIER();
	uint32_t gen = magic == VPTRIG_MAGIC ? s->generation : 0;
	if (gen != c.generation)
	{
		c.generation = gen;
		c.next = 1;
		return VPTRIG_READ_NEWGEN;
	}
	if (gen == 0) return VPTRIG_READ_NONE;
	if (c.next < 1) c.next = 1;
	int64_t written = VPTrigLoad64(&s->writeSeq);
	VPTRIG_BARRIER();
	if (c.next > written) return VPTRIG_READ_NONE;
	if (written - c.next >= static_cast<int64_t>(VPTRIG_CAPACITY))
	{
		int64_t first = written - static_cast<int64_t>(VPTRIG_CAPACITY) + 1;
		c.lost += first - c.next;
		c.next = first;
		return VPTRIG_READ_LOST;
	}
	const VPTrigRecord* slot = VPTRIG_SLOT(section, c.next);
	const volatile int64_t* slotSeq = reinterpret_cast<const volatile int64_t*>(&slot->seq);
	int64_t before = VPTrigLoad64(slotSeq);
	VPTRIG_BARRIER();
	if (before != c.next)
	{
		// written > next, so the slot was overwritten by a newer record (the reader fell behind during the call)
		if (before > c.next || before == 0)
		{
			int64_t now = VPTrigLoad64(&s->writeSeq);
			int64_t first = now - static_cast<int64_t>(VPTRIG_CAPACITY) + 1;
			if (first <= c.next) first = c.next + 1;
			c.lost += first - c.next;
			c.next = first;
			return VPTRIG_READ_LOST;
		}
		return VPTRIG_READ_NONE;   // cannot happen with R1 (an older seq in a slot whose record was published)
	}
	VPTrigRecord copy;
	memcpy(&copy, slot, sizeof(copy));
	VPTRIG_BARRIER();
	int64_t after = VPTrigLoad64(slotSeq);
	if (after != c.next)
	{
		int64_t now = VPTrigLoad64(&s->writeSeq);
		int64_t first = now - static_cast<int64_t>(VPTRIG_CAPACITY) + 1;
		if (first <= c.next) first = c.next + 1;
		c.lost += first - c.next;
		c.next = first;
		return VPTRIG_READ_LOST;
	}
	copy.seq = static_cast<uint64_t>(c.next);
	if (copy.frameId > maxFrame) return VPTRIG_READ_NONE;   // R2: stays for a later insertion
	out = copy;
	++c.next;
	return VPTRIG_READ_OK;
}
// The reader block's mirror of the cursor (R3's free count and the counters); the shell calls it after each insertion.
static inline void VPTrigPublishReader(void* section, const VPTrigCursor& c, uint32_t readerPid)
{
	VPTrigReader* r = VPTRIG_READER(section);
	r->generationSeen = c.generation;
	VPTrigStore64(&r->readSeq, c.next > 0 ? c.next - 1 : 0);
	VPTrigStore64(&r->lostTotal, c.lost);
	r->readerPid = readerPid;
}
// A name by index (R5): NULL when the index is not published yet or out of range.
static inline const VPTrigName* VPTrigNameAt(const void* section, uint32_t index)
{
	const VPTrigServer* s = VPTRIG_SERVER(section);
	uint32_t count = s->nameCount;
	VPTRIG_BARRIER();
	if (index == 0 || index > count || index >= VPTRIG_NAME_CAPACITY) return NULL;
	return VPTRIG_NAME(section, index);
}
#endif
