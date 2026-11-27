// proxy_ui_bridge.h - the Lua bridge of the proxy (issue 0178; the Architect's hand-over design 3.2,
// research/07-architecture-decisions/architect-2026-11-14-hand-over-designs.md). Hand-written. Implementation:
// proxy_ui_bridge.cpp (Lua side, no proxy runtime dependency, unit-tested in proxy/tests/ui_bridge/); the proxy glue
// (vpproxy.ini switches, the post on the ui channel, the calls from the RegisterScriptLibraries hook and the Update hook)
// is proxy_ui_bridge_rt.cpp. Findings and the premises B1-B4: research/03-dll-engine-boundary/proxy-design.md section 24.
//
// Three abilities, all Lua work on the EXE's MAIN thread (the thread that runs the UI's Lua):
//   1. listen   a C function is added as a listener of a UI event (Events.X.Add); it packs the event's arguments into
//               a fixed-size versioned Record and hands it to the post hook (the proxy posts it on the ui channel; the
//               server puts it into the renderer's queue, never on a DLL code path: rule R1);
//   2. raise    Events.X(...) with arguments built by the proxy. QueueRaise may be called from any thread (the Update
//               hook runs on the game-core thread); the raise itself runs on the main thread, from the bridge's own
//               listener of Events.LocalMachineAppUpdate, which the EXE raises once per frame there;
//   3. replace  a Lua global function (UnitMoving) is replaced by a C closure that counts the call and calls the
//               original with the same arguments, returning its results (H3 later answers from the anchor table).
// Rule R2: only the Lua C API of the game's own Lua DLL; no call into the EXE by address, no code byte.
// Rule R3: no UI Lua file is changed; the bridge listens to and raises the events those files already use.
// Lifetime (premise B4): every closure the bridge gives to Lua carries the install generation as an upvalue and holds
// no pointer to any Lua state; a closure of an older generation does nothing when called. The bridge keeps no lua_State
// pointer across calls: it uses only the state Lua hands to the call it is in.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct lua_State;

namespace UiBridge
{
// ---- the mirror record (wire format of vp_wire.h kRt_UiRecord; server: SrvFeed::kUiMirror) -------------------------
enum { kRecordVersion = 1 };

enum Kind
{
	kKindNone = 0,
	kKindSpawnArrow = 1,           // Events.SpawnArrowEvent(fromX, fromY, toX, toY): a[0..3] as given. The generic
	                               // listener gets no arguments (a[] = 0, research 05 arrows-and-path-line.md 5); ticket 0199's wrapper (ConfigureArrows)
	                               // posts it with the four plot coordinates, and then the generic listener is not added
	kKindRemoveAllArrows = 2,      // Events.RemoveAllArrowsEvent(): no arguments
	kKindMinimapTexture = 3,       // Events.MinimapTextureBroadcastEvent(handle, width, height[, paddingX]): a[0..3]
	kKindDisplayMovement = 4,      // Events.DisplayMovementIndicator(bool): a[0] = 0 / 1 (design 4.3: show / hide)
	kKindUnitMovingCalled = 5,     // not an event: the replaced global UnitMoving was called (a[0] playerID, a[1] unitID,
	                               // a[2] = 1 when the original answered true); posted only with the test switch
	kKindUnitPosition = 6,         // epic 0040 (issue 4751 the layout, 4754 the listeners; unit-feed-contract.md 3.3):
	                               // Events.LocalMachineUnitPositionChanged(playerID, unitID, {x, y, z, w}) read by its own
	                               // listener (the generic one zeroes a table): a[0] unit id, a[1..3] x, y, z as float bits
	                               // (world units), flags = player << kPosPlayerShift; and UnitMoveQueueChanged as the
	                               // arrival: the same record with kPosArrival and the unit's last position
	kKindFrameMark = 7,            // epic 0040: made by the bridge's LocalMachineAppUpdate drain once per EXE frame that made
	                               // records, before them: a[0] the EXE frame number, a[1] the number of records that follow,
	                               // a[2] = a[3] = 0, flags 0. The EXE frame of a kind-6 record is the last mark before it
	kKindPathNode = 8,             // ticket 0300 part 2 (path-models.md 7): one node of an Events.UIPathFinderUpdate path,
	                               // posted by the path install (ConfigurePath): a[0] = x & 0xFFFF | y << 16 (int16 each, as
	                               // the EXE keeps them), a[1] = the node's turn value, a[2] = index (bits 0-7) | count
	                               // (bits 8-15) | the kPath* bits, a[3] = the update's number (1, 2, ... per generation).
	                               // An empty path is one record with index 0, count 0 and the mouse plot in a[0]
	kKindCount
};

// ---- ticket 0300 part 2: the bits of a kKindPathNode record's a[2] (the EXE's tests, path-models.md 7.1) ----
enum
{
	kPathIndexMask = 0xFF,         // the node's index (0-based)
	kPathCountShift = 8,           // bits 8-15: the path's node count (0..kPathMaxNodes)
	kPathCountMask = 0xFFu << 8,
	kPathTested = 1u << 16,        // the tests ran (a head selected unit of the active player; else every test is false)
	kPathEmbark = 1u << 17,        // this node: the embark or the disembark test between the previous node and this one
	kPathAttack = 1u << 18,        // the last node (or the cursor of an empty path): the attack test
	kPathSwap = 1u << 19,          // the cursor of an empty path: CanSwapWithUnitHere (the EXE's Embark marker there)
	kPathTruncated = 1u << 20      // the event had more than kPathMaxNodes nodes (the rest are dropped, as the EXE's record)
};
enum { kPathMaxNodes = 64 };

// Flags of a Record.
enum
{
	kFlagNotNumber0 = 1u << 0,     // bits 0-3: argument i was neither a number nor a boolean (nil, string, table, ...);
	                               // a[i] = 0, except a light userdata, whose address is kept in a[i] (bit 4 + i set)
	kFlagLightUserdata0 = 1u << 4, // bits 4-7: argument i was a light userdata (a[i] = its address, truncated to 32 bits)
	kFlagMoreArgs = 1u << 8,       // the event had more than four arguments (the rest are not carried)
	kFlagFraction = 1u << 9        // a numeric argument had a fractional part (a[i] is truncated toward zero)
};

// Flags of a kKindUnitPosition record (bits 0-9 above are the generic listener's and stay 0 in it).
enum
{
	kPosPlayerShift = 16,          // bits 16-23: the player id (0..255)
	kPosPlayerMask = 0xFFu << 16,
	kPosArrival = 1u << 24         // the unit arrived (UnitMoveQueueChanged, or the rule 4755 settles for quick movement)
};

struct Record                     // 32 bytes, little-endian, the same layout on x86 and x64
{
	uint16_t version;              // kRecordVersion
	uint16_t kind;                 // Kind
	uint32_t flags;
	int32_t a[4];
	uint32_t seq;                  // 1, 2, ... per process, in the order the records were made (main thread)
	uint32_t gen;                  // the install generation that made the record (a new Lua state is a new generation)
};
#if defined(__cplusplus) && (__cplusplus >= 201103L || defined(_MSC_VER))
static_assert(sizeof(Record) == 32, "UiBridge::Record layout");
#endif

// ---- hooks (set once by the glue; the unit test sets its own) -------------------------------------------------------
struct Hooks
{
	void (*post)(const Record& r);          // a record made on the main thread; NULL = records are counted and dropped
	void (*log)(const char* line);          // one line, no newline; NULL = no log
	uint32_t (*now)();                      // milliseconds (tests); NULL = GetTickCount
};
void SetHooks(const Hooks& h);

// ---- switches ------------------------------------------------------------------------------------------------------
// enabled: the bridge installs itself (vpproxy.ini ui_bridge=1). test: also installs the test table VP64UiBridgeTest
// into the state's globals (QueueRaise, Stats; ui_bridge_test=1) and posts kKindUnitMovingCalled records.
void Configure(bool enabled, bool test);
bool Enabled();

// ---- issue 4754 (epic 0040): the unit positions (kKindUnitPosition) and the frame mark (kKindFrameMark) -------------
// on: the bridge installs, into the InGame context, a listener of LocalMachineUnitPositionChanged (playerID, unitID,
// {x, y, z, w}: one kind-6 record) and of UnitMoveQueueChanged (playerID, unitID, bRemainingMoves: an arrival record,
// kind 6 with kPosArrival and the unit's last position, only when bRemainingMoves is false or nil, as the UI files read
// it; the other calls are counted), and the drain. The kind-6 records of a frame are held on the main thread and posted
// by the drain after one kind-7 mark: a[0] = the drain's number in the current generation (the EXE frame number; the
// proxy reaches no Present count), a[1] = the number of kind-6 records that follow. More than kPosBufCap in one frame
// are posted early, after a mark of their own with the same frame number.
// With Configure's enabled = false this is the "positions only" install (the unit option, 4759): no test table, no
// UnitMoving replacement, no other listener, and QueueRaise stays refused. With enabled = true the whole bridge gets the
// two listeners as well; without ConfigurePositions(true) ui_bridge=1 behaves as before. Turning it on after an install
// installs a new generation into the same InGame environment at the next check.
void ConfigurePositions(bool on);
bool PositionsOn();
enum { kPosBufCap = 256 };
struct PositionStats
{
	uint32_t positions;            // kind-6 records made from LocalMachineUnitPositionChanged
	uint32_t arrivals;             // arrival records made from UnitMoveQueueChanged (bRemainingMoves false or nil)
	uint32_t arrivalsNoPosition;   // ... of them with no last position known for the unit (a[1..3] = 0)
	uint32_t queueRemaining;       // UnitMoveQueueChanged calls with bRemainingMoves true (no record)
	uint32_t badArgs;              // calls of either listener whose arguments could not be read (no record)
	uint32_t marks;                // kind-7 records made
	uint32_t earlyFlushes;         // of them, posted before the drain because the frame's buffer was full
	uint32_t unitsKnown;           // units in the last-position table now
	uint32_t tableClears;          // the last-position table was full and cleared
};
PositionStats GetPositionStats();
// ---- ticket 0199 part 2: the ranged-attack arc's end points (kKindSpawnArrow with arguments, kKindRemoveAllArrows) -----
// on: the bridge installs, into the InGame context, a wrapper of that context's Events.SpawnArrowEvent (research/05-
// graphics-composition/arrows-and-path-line.md 6.1 P9 and 13.1): the field is replaced by a table whose metatable's
// __call is a C closure and whose __index is the original. A call SpawnArrowEvent(x0, y0, x1, y1) with four whole
// numbers posts one kind-1 record (a[0..3] = x0, y0, x1, y1, flags 0) at once, then calls the original with the same
// arguments (the EXE's handler and every listener run as before; an error of the original propagates as it would have).
// Other arguments: no record (counted as badArgs), the original is called. A listener of RemoveAllArrowsEvent posts
// kind 2 (with the whole bridge, its generic listener does that; the generic SpawnArrowEvent listener is then left out,
// so every kind-1 record of the session carries end points). A re-install unwraps an older generation's wrapper first,
// so wrappers never nest; an older generation's wrapper still calls the original but posts nothing.
// With Configure's enabled = false this is the "arrows only" install (the server's arc, [Renderer] ServerArc): the
// wrapper, the remove listener and the drain. Turning it on after an install installs a new generation into the same
// InGame environment at the next check.
void ConfigureArrows(bool on);
bool ArrowsOn();
struct ArrowStats
{
	uint32_t spawns;               // kind-1 records posted by the wrapper
	uint32_t removes;              // kind-2 records posted by the arrows-only remove listener
	uint32_t badArgs;              // wrapper calls without four whole numbers (no record)
	uint32_t staleCalls;           // wrapper calls of an older generation (the original called, no record)
	uint32_t wraps;                // wrappers installed (one per generation that found the event)
	uint32_t unwraps;              // older wrappers taken off at a re-install
};
ArrowStats GetArrowStats();

// ---- ticket 0300 part 2: the move-order path (kKindPathNode, kKindDisplayMovement) ---------------------------------------
// on: the bridge installs, into the InGame context, a listener of Events.UIPathFinderUpdate (one argument, the path: an
// array of {x, y, turn} tables, empty when the move mode ends; research/05-graphics-composition/arrows-and-path-line.md
// 6.2) that posts one kind-8 record per node (at most kPathMaxNodes), or one record with count 0 and the mouse plot for an
// empty path; and, unless the whole bridge already has it, the generic listener of DisplayMovementIndicator (kind 4, a[0]
// = the bool). Before posting, the listener reads the head selected unit (Lua UI.GetHeadSelectedUnit(), its GetOwner and
// GetID) and, for an empty path, the mouse plot (UI.GetMouseOverHex()), and hands them with the nodes to the tests hook
// (SetPathTests; the proxy's glue makes the EXE's DLL calls there); the answers go into the records' kPath* bits. Without
// a hook no test runs (kPathTested clear). With Configure's enabled = false this is the "path only" install (the
// server's path, [Renderer] ServerPath). Turning it on after an install installs a new generation at the next check.
void ConfigurePath(bool on);
bool PathOn();
struct PathQuery
{
	int32_t unitOwner, unitId;     // the head selected unit (-1, -1: none, or its owner or id could not be read)
	int count;                     // nodes kept (0 = an empty path)
	int16_t x[kPathMaxNodes], y[kPathMaxNodes];
	int32_t mouseX, mouseY;        // count 0: the mouse plot (-1, -1: unknown)
};
struct PathAnswer
{
	bool tested;                   // the tests ran (the unit is the active player's)
	uint8_t embark[kPathMaxNodes]; // per node (entry 0 unused): embark or disembark from the previous node
	bool lastAttack;               // the last node's attack test (count >= 2)
	bool cursorSwap, cursorAttack; // count 0: the cursor's swap and attack tests
};
// Called on the main thread inside the listener (no Lua call from it; it may call into the DLL). NULL = no tests.
typedef void (*PathTestsFn)(const PathQuery& q, PathAnswer& a);
void SetPathTests(PathTestsFn fn);
struct PathStats
{
	uint32_t updates;              // UIPathFinderUpdate calls that posted records
	uint32_t nodes;                // node records posted
	uint32_t empty;                // empty paths posted
	uint32_t truncated;            // paths longer than kPathMaxNodes (cut)
	uint32_t badArgs;              // calls whose path could not be read (no record)
	uint32_t tested;               // updates whose tests ran
	uint32_t noUnit;               // updates without a readable head selected unit (no test)
	uint32_t readFailures;         // ... of them where a UI function raised an error (logged for the first three)
	uint32_t embarks, attacks, swaps;   // test answers that were true
	uint32_t staleCalls;           // calls of an older generation's listener
	uint64_t costUs;               // the listeners' time on the main thread (inside the EXE's Lua raise), all updates, us
	uint32_t costMaxUs;            // ... the longest one
};
PathStats GetPathStats();

// Ticket 0487: a frame hook, called by the drain once per frame on the main thread after the queued raises (the
// growth of the EXE's UI draw buffers, proxy_uidraw.h). Set before the first OnLuaEntry; NULL = none. With a hook and
// the bridge off (ui_bridge=0, the product), the bridge installs the drain alone ("drain only": no event listener, no
// UnitMoving replacement, no test table), so that the hook runs every frame; Active() is then true.
void SetFrameHook(void (*onFrame)());
// Ticket 0586: a second frame hook, called by the drain after the first with the drain's own Lua state (the InGame
// context's thread as the EXE raises LocalMachineAppUpdate; LUA_ENVIRONINDEX is the InGame context's environment). The
// hook must make every Lua call in protected mode (lua_cpcall / lua_pcall); the drain restores the stack top after it.
// Like SetFrameHook, it makes the bridge install the drain alone when the bridge is off (proxy_livereload.h).
void SetFrameLuaHook(void (*onFrame)(lua_State* L));
bool Active();   // Enabled() or a frame hook is set: OnLuaEntry installs

// ---- entry points --------------------------------------------------------------------------------------------------
// The thread that runs the UI's Lua (the proxy's boot thread, bound to the ui channel). Callbacks on any other thread
// are counted (wrongThread) and do nothing, except the UnitMoving replacement, which always calls the original.
void SetMainThread(uint32_t threadId);
// Called on the main thread with the engine Lua thread the DLL's libraries are being registered into (the proxy's
// RegisterScriptLibraries hook). That thread is SHORT-LIVED (premise B1, proxy-design 24.2: the EXE registers the DLL's
// globals into a registration thread that it frees, and copies those globals into every UI context's environment when
// the context is made). Only the test table (ui_bridge_test=1) is set here, so that every context gets a copy; nothing is
// installed here. Returns true when the test table was set.
bool OnStateRegistered(lua_State* L);

// Called on the main thread by every Lua call of a DLL function (the proxy's FuncThunk), with the calling context's
// thread. At most every kCheckMs it looks for the in-game UI context ("InGame" in _LOADED._G.Threads, the table of
// context threads and their environments) and installs into it, as a new generation, when it is not installed there
// yet (another environment than the installed one) or when the drain has not run for kSilentMs (the listeners were
// lost). The listeners, the drain and the replaced UnitMoving go into the InGame context's own environment and Events
// object (every context has its own Events table; a raise through any of them reaches the listeners of all contexts),
// with the Add calls made on the InGame context's own thread. Runs in protected mode; returns true when it installed.
bool OnLuaEntry(lua_State* L);
enum { kCheckMs = 250, kSilentMs = 10000 };

// A raise of Events.<name>(args...), queued from any thread and run on the main thread at the next frame (the drain).
// Arguments are numbers or booleans. false: the queue is full (64 raises), the name is too long (63 chars), more than
// 8 arguments, or the bridge is off.
struct Arg
{
	enum Type { kNumber = 0, kBoolean = 1 } type;
	double value;                  // a boolean is 0 / 1
};
bool QueueRaise(const char* eventName, int nargs, const Arg* args);

// Called on the game-core thread at the top of every ICvGame1::Update (the proxy's Update hook): a heartbeat check of
// the drain. Logs once when the drain has not run for kStallUpdates Updates while installed (the listeners are gone or
// the main thread is blocked), and once when it runs again. No Lua here.
void OnUpdate();
enum { kStallUpdates = 600 };

// ---- counters ------------------------------------------------------------------------------------------------------
struct Stats
{
	uint32_t gen;                  // current install generation (0 = never installed)
	uint32_t installs, installFailures;
	uint32_t listenersAdded;       // Events.X.Add calls that succeeded, all generations
	uint32_t records[kKindCount];  // records made, by kind
	uint32_t posted;               // records handed to the post hook
	uint32_t drains;               // drain calls (frames) of the current generation's listener
	uint32_t raisesQueued, raisesDone, raisesFailed, raisesDropped;
	uint32_t unitMovingCalls;      // calls of the replaced UnitMoving (all threads)
	uint32_t unitMovingReplaced;   // times the global was replaced (one per generation where it existed)
	uint32_t staleCalls;           // calls of a closure of an older generation (ignored)
	uint32_t wrongThread;          // callbacks on a thread other than the main thread (ignored)
	uint32_t stalls;               // heartbeat stalls logged by OnUpdate
	uint32_t frameHookCalls;       // ticket 0487: calls of the frame hook (all generations)
};
Stats GetStats();
// One line for the log ("ui bridge: gen 2, installs 2, records 14 (arrow 3, ...), ...").
void FormatStats(char* out, size_t cap);

// ---- tests only ----------------------------------------------------------------------------------------------------
void ResetForTest();
}  // namespace UiBridge
