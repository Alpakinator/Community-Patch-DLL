// farm_guard.h - the farm builder guard inside shell_composite.dll (ticket 0527, pieces F1 and F2 of the Architect's draft
// research/07-architecture-decisions/architect-2026-11-18-farm-field-layout-ticket.md; decision D186). Written 2026-11-18
// for this repository (GPL-3.0-or-later). What the EXE's objects are and why each byte pattern is checked:
// research/02-exe-internals/farm-builder-guard.md (behaviour only, D83).
//
// F1, the switch point. The EXE's farm-field builder (fg_builder, thiscall on the static object fg_builder_obj, one stack
// argument, ret 4) is a listener of event type 59 on the "B" event manager (pac_qb_index). Its listener node (20 bytes:
// +0x00 next index, +0x0C object, +0x10 function) is found at a Present (the EXE's main thread) under the EXE's own
// registration lock, and its function word is replaced by FgHook (a data write, compare-and-swap against the builder's
// address; D4: no code byte changes). FgHook calls the builder with the same object and argument. The swap is re-applied
// at every Present that finds a node holding the builder again (a new GameView startup, mod activation with
// ReloadLandmarkSystem = 1). The EXE's unregister matches object and function, so it cannot remove a swapped node: such a
// node stays in the list after the GameView shutdown. FgHook therefore calls the builder only while the city-field flag
// fg_viewflag says the GameView group is started and no node of the list still holds the builder itself; at the next
// Present a stale swapped node gets FgInert (returns at once) in its function word, and the new node gets FgHook.
//
// F2, the guard. After every builder call FgHook checks the layout graph's endpoint free list (object fg_builder_obj +
// 0x158, header +0x94: free head, live count, data, size, capacity; 20-byte entries, bit 31 of +0x10 = freed): the walk
// from the head meets only freed entries, each once, and ends at 0x0FFFFFFF; it meets every freed entry; live + freed =
// size; no live entry counts more than 4 handles (0515, crashes-huge-load.md 8). The first bad result: one LOUD line, the
// record farm-guard.log in the proxy's run folder (the D119 report takes the tail of every *.log there), and no further
// call reaches the builder in this process. Every burst of builder calls (a load, or farms changed in play) ends with one
// "farm_guard: load ..." line: calls, checks, their result and cost. Every load (a map shown) also gets one "farm_guard:
// in game ..." line 10 s after its map came up, with the check run at that Present; a bad result there trips the guard
// too (the graph is also written outside the builder's calls: a Tiny map without farms holds 238 nodes, 0527 t01).
//
// Addresses (epic 0590, D202; ticket 4965): the EXE build table (shell_exe.h, exebuild/symbols/shell.tsv); the build is
// read from the image's header, an unknown build or a symbol its table does not carry refuses the guard like a failed
// pattern. The names above are the catalogue's.
//
// Keys: farm_guard = 1 (default) | 0 in [composite] of shell_composite.ini (Config::farmGuard of shell_composite.cpp,
// re-read when the ini changes): 0 puts the builder's own address back into a swapped node (not after a bad result).
// farm_builder = 1 (default) | 0 (ticket 4853, epic 0528 D; the proxy sets VPSHELL_FARM_BUILDER=0 with VP64Options.ini
// [Renderer] ServerFarmFields = 1, when the server lays out its own farm fields): with 0 FgHook returns before the builder
// call (counted skipServer, event line "skipped-server") and the hook stays in the node whatever farm_guard says, so the
// EXE's farm layout never runs and its graph keeps only what other writers put there.
// Ticket 0655 (D230): on that skipped-server path FgHook does the builder's last step and nothing else of it: under the
// builder's own three tests (the payload's flag byte +0x1C non-zero, the plot on the map by the EXE's own plot test, the
// payload's art id +8 the farm's by the EXE's own art id call) it calls the EXE's poster of the farm-end record on the queue
// object the builder uses and writes the payload's hex x and y into the record, so the EXE's farm manager makes the farm's
// building (research/05-graphics-composition/cities-and-landmarks.md 2.6.1). A call of EXE functions, no byte of the EXE
// changed. Its own patterns (FgPostCheckPatterns) must match, else the post stays off with one LOUD line and the guard runs
// as before. Test switch: farm_end_post = 1 (default) | 0 in [composite] (VPSHELL_FARM_END_POST first; read by the proxy,
// the owner; FarmGuardEnv::farmEndPostOff). Counters: FgStats posts, postFail*, postRefused, postSwitchedOff; the event
// line word "skipped-server-posted".
// Ticket 4854 (M3): every in-game line and every burst line is followed by one "farm_guard: graph (...)" line, a read-only
// scan of the graph's cell lists, nodes and endpoints (FgGraphScan below), with farm_guard and farm_builder at any value.
// Environment: VPSHELL_RUN_DIR (the proxy's run folder) names where farm-guard.log goes.
//
// Ticket 0535: one owner per process. The same source is compiled into the proxy (proxy/src/proxy_farm_guard.cpp), which
// owns the guard on every 64-bit start, also without the composite (ServerRenderer = 0). The owner creates a shared page
// (FarmGuard_OwnerAttach): its first 4 KB hold FgShared, its second 4 KB two stubs of the owner's own code, never freed.
// The node's function word holds the hook stub (mov eax, [hookTarget]; jmp eax, or ret 4 while hookTarget is 0), so a
// node that outlives an unloaded proxy image (the EXE unloads the game DLL at a mod change and may load it again, issue
// 3308) returns at once instead of calling into unmapped memory, and the next proxy boot finds the page by the environment
// variable VP64_FARM_GUARD and points the stub at its own hook. The composite's copy (FarmGuard_OnPresent) finds the same
// page and installs nothing: it hands each Present to the owner (FgShared::presentFn), so the in-game line and the graph
// line still run at the Present. Without a composite the owner's watcher thread (FarmGuard_OnTick) swaps the node and
// writes the burst lines, and the in-game check moves to the first builder call of every burst (before the builder).
#pragma once
#include <windows.h>
#include <stdint.h>

struct FarmGuardEnv
{
	uint32_t exeBase;            // module base of the EXE (addresses are rebased on it)
	int32_t exeValidated;        // the composite's EXE validation: 1 valid, -1 refused, 0 not yet
	uint32_t gridValid;          // the camera block: a map is loaded
	uint32_t gridSeq;            // the camera block: the load
	int enabled;                 // farm_guard in the ini
	const wchar_t* outDir;       // the composite's log folder (farm-guard.log's fallback)
	void (*log)(const char* fmt, ...);
	int skipBuilder;             // ticket 4853: farm_builder=0 (the server draws the farm fields): the hook never calls the
	                             // builder (counted skipServer) and stays installed even with farm_guard=0; 0 = as 0527
	int farmEndPostOff;          // ticket 0655: farm_end_post=0 (the test switch): no farm-end post on the skipped-server path;
	                             // 0 (the default, also for a zeroed or older initialiser) = the post is made
	int areaProbe;               // ticket 0655 (P1): farm_area_probe=1, a DIAGNOSTIC: the builder object's area (A33) listener
	                             // node calls a timing pass-through (FarmGuard_AreaProbe); 0 (default) = nothing is touched
	int farmEdgeOff;             // ticket 0657: farm_edge=0 (the test switch): no pass-through on the farm manager's two placing
	                             // listeners, every building at its plot centre (as 0655); 0 (default, also for a zeroed or
	                             // older initialiser) = the edge placement is on while skipBuilder is
	int treadFilter;             // ticket 0784 (D268): tread_filter=1 ([Renderer] HideTreadEffect = 1 with ServerUnits = 1,
	                             // set by the proxy): the main Particle object's type-0x28 node calls the tread filter, which
	                             // drops the two landship tread effects; 0 (default, also for a zeroed or older initialiser) =
	                             // the node holds the EXE's own listener
};

// The EXE's main thread, every Present of the target swap chain.
void FarmGuard_OnPresent(const FarmGuardEnv& env);

// ---- the parts below are for the tests (shell/tests/farm_guard_test.cpp) and the composite's log ---------------------

// Every address the guard uses, absolute (the real EXE: FgWorldFromBase; the tests: their own fake objects). The names in
// the comments are the build table's symbols (exebuild/symbols/shell.tsv).
struct FgWorld
{
	uintptr_t lockInit;          // byte whose bit 0 says the registration lock word was initialised (fg_lockinit)
	uintptr_t lock;              // the registration lock word (fg_lock; 0 free, 1 held)
	uintptr_t mgr;               // the "B" event manager (pac_qb_index; its first word is the channel index)
	uint32_t slotOff;            // the handler table entry of type 59 from mgr (0x1E0: the handler, +4: the listener list)
	uint32_t stride;             // bytes per channel (0x600)
	uintptr_t trampoline;        // the handler every type has (fg_type_handler -> the listener walk)
	uintptr_t builder;           // the farm builder (fg_builder)
	uintptr_t builderObj;        // its object (fg_builder_obj)
	uintptr_t viewFlag;          // the city-field flag word (fg_viewflag): 1 between GameView startup and shutdown
	uintptr_t graph;             // the layout graph (fg_builder_obj + 0x158)
	uintptr_t vtxVec;            // ticket 4854: the farm-track strip's static vertex vector (fg_vtxvec: begin, +4 size, +8 capacity)
	uintptr_t h2w;               // ticket 4855: the hex -> world matrix (cam_h2w, 16 floats, D3DX row-vector form)
	uintptr_t aabbMin;           // ticket 4855: the world box's minimum x, y (cam_aabb, +4)
	// ticket 0655: the farm-end post (all 0 = no post: a world without them, or a build whose post sites did not match)
	uintptr_t poster;            // the poster of the farm-end record (fg_post_farm_end; thiscall on the queue object, no argument)
	uintptr_t queueIndex;        // the 4 MB queue's channel index word (qg_channel_index)
	uintptr_t queueBase;         // its channel 0 (qg_channels); channel c at + c * 0x800180
	uintptr_t plotOnMap;         // the builder's plot test (fg_plot_on_map; thiscall on plotGrid, one argument)
	uintptr_t plotGrid;          // (fg_plot_grid)
	uintptr_t artId;             // the builder's art id of a name (fg_art_id; thiscall on artDb, one argument)
	uintptr_t artDb;             // (fg_art_db)
	uintptr_t artName;           // the farm's art name the builder passes (fg_farm_art_name)
};
// The addresses of the build (kVpExeBuild*) in the image at base. False (w untouched) when a symbol is not carried.
bool FgWorldFromBase(int build, uint32_t base, FgWorld* w);

// The image's build (from its header), every symbol FgWorldFromBase needs carried, every byte pattern matching; checked
// before the first read or write of the real EXE. False with the reason in why; *build (may be NULL) gets the build.
bool FgCheckPatterns(uint32_t base, char* why, size_t whyCap, int* build = 0);
int FgPatternCount();
int FgPatternSymbol(int k);   // the catalogue symbol (VPX_*) pattern k sits at, -1 out of range
// Tests: the bytes a fake image of the build needs (0: a pattern not carried), and the image itself: a header naming the
// build and every pattern's bytes at that build's addresses (pattern `broken` gets one byte changed; -1 = none).
uint32_t FgImageBytes(int build);
bool FgWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken);

// Ticket 0655: the farm-end post's own patterns (the builder's three tests and its last step, the poster, the plot test's
// head, the art name), checked after the guard's: a mismatch leaves the guard as it is and only the post off (one LOUD
// line). FgPostWorldFromBase fills the post's fields of w; false (w untouched) when a symbol is not carried.
bool FgPostCheckPatterns(uint32_t base, int build, char* why, size_t whyCap);
bool FgPostWorldFromBase(int build, uint32_t base, FgWorld* w);
int FgPostPatternCount();
uint32_t FgPostImageBytes(int build);
bool FgPostWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken);
// Ticket 0655 (P1, the Architect's pass condition): the diagnostic area probe. The A manager's type-33 list holds the
// builder object's area listener (fg_area_listener, thiscall, one argument, ret 4); with farm_area_probe=1 the owner swaps
// that node's function word (a data write under the A manager's own registration lock, as F1 does for type 59) to
// FarmGuard_AreaProbe, which calls the listener and times each call (QPC), and logs per burst the count, the mean of the
// first and last 20 calls, the maximum; every value goes to farm-area-calls.log. With the key at 0 the node gets the
// listener's own address back; a node the EXE registered again retires the old swapped one to FarmGuard_AreaInert.
bool FgAreaCheckPatterns(uint32_t base, int build, char* why, size_t whyCap);
bool FgAreaWorldFromBase(int build, uint32_t base, FgWorld* w);   // lock, list and object of type 33; builder = the listener
int FgAreaPatternCount();
uint32_t FgAreaImageBytes(int build);
bool FgAreaWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken);
// Ticket 0657: the farm buildings on the server's fields (research/05-graphics-composition/cities-and-landmarks.md 2.6.2,
// 2.6.3). With farm_builder=0 the EXE's farm manager has no field curves and its placer keeps each building at the plot
// centre; stock puts it at the centroid of one of the plot's fields. The server publishes that point per farm plot in
// <VPSHELL_SECTION>.Farm (shell/common/vp_farm_points.h). The owner swaps the function words of the farm manager's two
// placing listeners (a data write under each manager's own registration lock, as F1): the farm end (A manager, type 5,
// fg_farm_end_listener) and the era (B manager, type 48, fg_farm_era_listener). Their pass-throughs (FarmGuard_EdgeEnd,
// FarmGuard_EdgeEra) give each record whose plot has a server point ONE curve of the patch's own for the length of the EXE's
// call: a square of side 512 centred on the point (its centroid is the point, |F| far above 100), in a curve table of the
// patch's own whose address replaces the manager's table word (+0x6F4) for that call only; record +5 (count) and +0x20 (the
// first id) and the table word are put back right after. Nothing outside the call sees a curve, so the paths that stamp the
// mask bits from curves (2.6.2 (4)) never meet one: 0 field records and 0 margin bits stay. A farm end whose plot has no
// point of this load yet (the section not DONE for the camera's grid seq, or the plot missing) or that arrives while the
// manager defers (+0x207C0 bit 0) is held: the pass-through posts the same farm end again (the EXE's own poster, as 0655's
// post) and returns; it is pumped the next time the 4 MB queue is (the dispatch reads the other buffer). After 20 s (5 s for
// a plot missing from a DONE list) the EXE's listener runs as before 0657 (the plot centre), counted. Owner words are stubs
// in the shared page that jump to the EXE's own listener while no owner is attached (a node that outlives a proxy image
// behaves as stock). A node of an earlier registration beside a new one is retired to the inert word, as F1's.
// Test switch: farm_edge = 1 (default) | 0 in [composite] (VPSHELL_FARM_EDGE first; read by the proxy).
struct FgEdgeWorld
{
	FgWorld end;                 // lock, mgr, slotOff 0x30 of the A manager; builder = fg_farm_end_listener; builderObj 0 (any)
	FgWorld era;                 // the B manager's, slotOff 0x188; builder = fg_farm_era_listener
};
#define FG_EDGE_END_SLOT_OFF 0x30u    // mgr + 8 + 5 * 8: type 5's handler; +4 its listener list (A manager)
#define FG_EDGE_ERA_SLOT_OFF 0x188u   // mgr + 8 + 48 * 8: type 48's (B manager)
#define FG_EDGE_HALF 256.0f           // half the side of the transient square (world units)
bool FgEdgeCheckPatterns(uint32_t base, int build, char* why, size_t whyCap);
bool FgEdgeWorldFromBase(int build, uint32_t base, FgEdgeWorld* w);
int FgEdgePatternCount();
uint32_t FgEdgeImageBytes(int build);
bool FgEdgeWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken);
// The farm manager's layout the pass-throughs read and write (cities-and-landmarks.md 2.6.1, 2.6.2): offsets from the
// manager (the listeners' object) and in a 0x90-byte record.
#define FG_FM_DEFER 0x207C0u          // bit 0: the manager defers its listeners
#define FG_FM_RECS 0x207A4u           // the record array; + 4 its count
#define FG_FM_CURVES 0x6F4u           // the curve table word the placer reads
#define FG_REC_BYTES 0x90u
#define FG_REC_NCURVES 0x05u          // byte
#define FG_REC_FLAGS 0x0Cu            // 0x100 instance made, 0x400 freed
#define FG_REC_HEX 0x10u              // int16 hex x, + 2 hex y
#define FG_REC_POS 0x14u              // the placed x, y, z (the placer writes them)
#define FG_REC_IDS 0x20u              // uint16 curve ids
#define FG_CURVE_BYTES 0x2Cu          // +8 the point store (its +8 = the segment array), +0x10 first, +0x14 last segment
#define FG_SEG_BYTES 0xFCu            // +0 previous, +4 next, +8 point count, +0xC x, y pairs
// Test: the edge's world (NULL = none), the points block in place of <base>.Farm (NULL = none), the camera's grid.
void FarmGuard_TestUseEdge(const FgEdgeWorld* ew);
void FarmGuard_TestEdgePoints(const void* block);
void FarmGuard_TestGrid(uint32_t gridValid, uint32_t gridSeq);
void FarmGuard_TestEdgeHold(double loadMs, double missingMs);   // the hold limits (FarmGuard_TestUse sets 20000, 5000)
// Ticket 0784 (D268; research/02-exe-internals/farm-builder-guard.md 15; unit-server-scene.md 19.14): the tread filter.
// With ServerUnits = 1 the EXE still animates its own hidden unit, and the landship's Run and charge clips start two
// timed-trigger effects on its own figure's FX_Treads bone (ART_DEF_VEFFECT_TANK_TREADS, _02: a scrolling tread shell),
// re-posted as a type-0x28 record (an owned one-shot start or move) every frame the trigger is active; the shell lies
// where the EXE's figure is, not on the server's hull, so it parts from the hull in a turn. The owner swaps the function
// word of the main Particle object's type-0x28 listener node on the 4 MB A manager (tf_l28_listener; thiscall, one
// argument = the record's payload: +0x40 the 16-byte owner key, +0x50 the name hash, ret 4) to a pass-through, under the A
// manager's registration lock (a data write, D228 base product). The pass-through drops a record whose name hash is one
// of the two tread names and whose key's last dword is not the shell effect player's tag 'VP64' (effects-formats.md
// 15.7.3), and calls the EXE's listener with the same object and argument for every other record (before its own busy
// test, so a deferred record is one that passed). Only the node of the main Particle object (tf_particle_obj, a static of
// the EXE that lives to the process's end) is swapped: other Particle objects (scenes) are left alone. The node word is a
// stub of the shared page (FgShared::treadStub) that jumps to the EXE's listener while no owner is attached. Five byte
// patterns must match (FgTreadCheckPatterns), else the filter stays OFF with one LOUD line. Key: tread_filter (default 0)
// in [composite], VPSHELL_TREAD_FILTER first; the proxy sets it from [Renderer] HideTreadEffect with ServerUnits = 1.
#define FG_TREAD_SLOT_OFF 0x148u     // mgr + 8 + 0x28 * 8: type 0x28's handler; +4 its listener list (A manager)
#define FG_TREAD_LISTS_OFF 0x14Cu    // tf_a28_lists - qg_channel_index (checked by FgTreadWorldFromBase)
#define FG_VP64_KEY_TAG 0x56503634u  // 'VP64': the shell effect player's owner key, dword 3 (payload +0x4C)
#define FG_EV_KEY_TAG 0x4Cu          // payload offsets of a type-0x28 record (effects-formats.md 8.1)
#define FG_EV_HASH 0x50u
bool FgTreadCheckPatterns(uint32_t base, int build, char* why, size_t whyCap);
bool FgTreadWorldFromBase(int build, uint32_t base, FgWorld* w);   // A manager, slot 0x148; builder = the listener, obj = the main Particle object
int FgTreadPatternCount();
uint32_t FgTreadImageBytes(int build);
bool FgTreadWritePatterns(int build, uint32_t base, uint32_t imageBytes, int broken);
// The EXE's effect name hash (effects-formats.md 8.1: CRC-32, reflected 0xEDB88320, start 0xFFFFFFFF, no final inversion).
uint32_t FgEffectNameHash(const char* name);
#define FG_TREAD_NAME0 "ART_DEF_VEFFECT_TANK_TREADS"
#define FG_TREAD_NAME1 "ART_DEF_VEFFECT_TANK_TREADS_02"
#define FG_AREA_SLOT_OFF 0x110u     // mgr + 8 + 33 * 8: type 33's handler; +4 its listener list
#define FG_QUEUE_STRIDE 0x800180u   // one channel of the 4 MB queue (two buffers, the flip counter, the high-water mark)
#define FG_QUEUE_CHANNELS 2u        // channels the queue has (message-queue-and-fixed-buffers.md 1)

// F2's check of the endpoint free list. Reads memory directly: the caller guards it (SEH).
struct FgCheck
{
	int sound;                   // 1 = every rule holds (an empty graph is sound)
	uint32_t head, live, size, cap, freedByBit, chain, over4;
	uint32_t firstOver4, firstOver4Word;   // the first live entry counting more than 4, and its count word
	uint32_t badIndex;           // where the walk failed (0xFFFFFFFF = it did not)
	char why[160];               // the first rule that failed ("" when sound)
};
void FgCheckGraph(uintptr_t graph, FgCheck* r);
// The same under SEH: false when the read faulted (r->why says so).
bool FgCheckGraphSafe(uintptr_t graph, FgCheck* r);

// Ticket 4854 (M3 of the Architect's routes file, research/07-architecture-decisions/architect-2026-11-18-farm-graph-routes.md
// 2 (b) and 6.1 item 3): the graph line, a read-only measurement of the layout graph's cell lists, nodes and endpoints. It
// never writes and never trips the guard. Layout read from research/02-exe-internals/farm-field-double-free.md 2.2 and
// crashes-huge-load.md 2 (offsets from the graph): +0x6C / +0x70 extent, +0x74 / +0x78 origin, +0x84 / +0x88 cells in x
// and y, +0x8C the cell array (24 bytes: +0x04 count, +0x08 first node, +0x0C last node); cell index = column x rows + row,
// cut to 16 bits; +0x94 endpoints and +0xBC nodes (free-list arrays: head, live, data, size, capacity). Node (56 bytes):
// +0x10 point count, +0x14 / +0x16 endpoint ids, +0x28 next and +0x2C previous link in its cell (bit 31 of +0x2C = the
// freed bit), +0x34 cell id (16-bit, 0xFFFF none), +0x36 kind. Endpoint (20 bytes): four handles (node index bits 0-27,
// bit 28 the end at +0x16, bits 29-30 a kind), count word +0x10 (bit 31 freed).
struct FgGraphStat
{
	int ok;                      // 0 = a header was not plausible, or a read faulted (why); the counts below are partial
	uint32_t nx, ny, cells;      // the grid (cells = nx * ny; above 65,535 = cell 65,535 reads as "none")
	uint32_t nodeLive, nodeSize, nodeCap, nodeFreedBit;   // node array; nodeFreedBit = entries with bit 31 of +0x2C
	uint32_t epLive, epSize, epCap, epFreed;              // endpoint array; epFreed = entries with bit 31 of +0x10
	uint32_t listsWalked, listed;                         // cells walked; node entries met on their lists
	uint32_t listsBad, firstBadCell;                      // lists whose walk does not end as the cell's words say
	char firstBadWhy[64];
	uint32_t listedNoPoints, listedFreed;                 // listed nodes with +0x10 == 0; with the freed bit
	uint32_t idMismatch;         // listed nodes whose +0x34 differs from the low 16 bits of the holding cell's index
	uint32_t heldAbove;          // listed nodes held by a cell whose index is above 65,535
	uint32_t onTwoLists;         // nodes met on more than one cell's list
	uint32_t unlisted, unlistedNamed;                     // live nodes on no list; of them, with a cell id other than 0xFFFF
	uint32_t idNoneByKind[4];    // live nodes (no freed bit) with +0x34 == 0xFFFF, by kind 0, 1, 2, other
	int c65535;                  // 0 = no such cell (grid <= 65,535 cells), 1 = its list is sound, -1 = not sound
	uint32_t c65535Count, c65535Walked, c65535Shared;     // its count word, the nodes walked, members also on another list
	char c65535Why[64];
	uint32_t hNeverFilled, hNeverFilledKind0;             // handles below a live endpoint's count naming index 0x0FFFFFFF
	                                                      // (a never-filled slot, the case of farm-field-double-free.md); of them, kind bits 0
	uint32_t hPast;              // handles below the count naming a node index at or past the node array (not 0x0FFFFFFF)
	uint32_t hNotBack;           // handles naming a node whose endpoint id at that end is not this endpoint
	uint32_t epCountOver4;       // live endpoints counting more than 4 (their handles are read up to 4)
	int vtxOk;                   // the vertex vector's words read
	uint32_t vtxSize, vtxCap;    // its size word (fg_vtxvec + 4) and capacity (+ 8)
	double ms;                   // the scan's time
	char why[128];
};
// Direct reads (the caller guards them). nodeMark and nodeList: scratch of at least the node array's size (zeroed here).
void FgGraphScan(uintptr_t graph, uintptr_t vtxVec, uint8_t* nodeMark, uint32_t* nodeList, uint32_t scratchCap, FgGraphStat* r);
// The same with its own scratch and under SEH: false when a read faulted (r->why says so).
bool FgGraphScanSafe(uintptr_t graph, uintptr_t vtxVec, FgGraphStat* r);
// The log line of a scan ("farm_guard: graph (<where>): ..."), LOUD when a list is not sound, a listed node has no
// points or the freed bit, or a kind-0 handle names a never-filled slot.
void FgGraphLineText(const FgGraphStat& g, const char* where, char* out, size_t cap);

// Ticket 4855 (the Architect's rule 2, routes file 6.1 item 2; premise: research/05-graphics-composition/farm-fields-layout.md
// 15): with the builder in use (farm_builder=1) on a grid above 65,535 cells, before each builder call the hook reads the
// count word of cell 65,535: (a) not 0: no further builder call in this process, one LOUD line and the D119 record; (b) else
// the calls of the plots within hex distance R of the plot holding cell 65,535 (x wrapped) are skipped, R = 2, or R = 4 when a
// kind-0 node (a coastline piece of the bake) has a box meeting the cell (MC #5493 from ticket 0542: the weld files a node
// by its middle point, and a farm call up to distance 4 can split such a piece and file its halves there); R is chosen at
// the first builder call of each load (after the bake), again after every new registration of the builder. That plot comes from
// the grid (+0x6C..+0x78 extent and origin, +0x84 / +0x88 cells) and the EXE's own plot geometry (the plot rectangle of
// farm-fields-layout.md 1.3: hex -> world matrix, y x 0.866, plus the world box's minimum); a geometry that is not a hex
// lattice 64 apart, or any plot beyond distance 2 whose rectangle grown by 0.66 meets the cell, makes the rule skip every
// builder call on that grid instead (0538's proposal 1), LOUD.
struct FgRule2Geo
{
	int ok;                      // 1 = the rule's plot is known and the self-check passed
	uint32_t nx, ny, cells, col, row, mapW;
	float cx, cy, cw, ch;        // cell 65,535's centre and size (layout units)
	int32_t px, py, phx, phy;    // the plot holding it: grid x (wrapped into 0..mapW-1), y; hex x, y (unwrapped)
	float pdist;                 // the distance from that plot's centre to the cell's centre
	uint32_t reach, reachFar;    // plots within hex distance 4 whose rectangle (grown by 0.66) meets the cell; of them past 2
	uint32_t kind0, kind0Touch;  // MC #5493 (0542, farm-field-double-free.md 3.4): live kind-0 nodes (the bake's coastline
	                             // pieces); of them, those whose box meets cell 65,535 (or its image one world width over)
	int radius;                  // 2, or 4 when a kind-0 node touches the cell (a farm call up to distance 4 can split it)
	char why[160];
};
// Direct reads (the caller guards them).
void FgRule2Geometry(uintptr_t graph, uintptr_t h2w, uintptr_t aabbMin, FgRule2Geo* g);
// Hex (cube) distance between two plots given as the event's hex x, y; x wrapped by mapW columns (0 = no wrap).
int FgHexDistWrap(int hx1, int hy1, int hx2, int hy2, int mapW);

// The listener list of type 59, as found (under the caller's lock or not).
enum { FG_MAX_NODES = 16 };
struct FgScan
{
	int ok;                      // 0 = the list is not as expected (why)
	uint32_t channel, count, size;
	uintptr_t list;              // 0 = no list yet (ok)
	uint32_t n;                  // nodes of builderObj with the builder, FgHook or FgInert in their function word
	uint32_t idx[FG_MAX_NODES];
	uintptr_t fnWord[FG_MAX_NODES];   // address of the node's function word
	uintptr_t fn[FG_MAX_NODES];
	char why[128];
};
void FgScanList(const FgWorld& w, FgScan* s);   // direct reads: the caller guards it (SEH)

typedef void(__fastcall* FgListenerFn)(void* obj, void* edx, void* ev);
FgListenerFn FarmGuard_Hook();
FgListenerFn FarmGuard_Inert();

struct FgStats
{
	uint32_t calls, builderCalls, skipTripped, skipNoView, skipStale, checks, checksBad, checkFaults;
	uint32_t skipServer;         // ticket 4853: calls not passed to the builder because farm_builder=0 (skipBuilder)
	uint32_t swaps, retired, restored, lockBusy, scanBad, anomalies;
	uint32_t graphLines, graphFaults;   // ticket 4854: graph lines written; scans whose read faulted
	uint32_t skipRule2Near, skipRule2Stop, skipRule2Geo, rule2Grids;   // ticket 4855: calls skipped by rule 2 (b), after (a),
	                                                                   // for a grid whose geometry failed; grids set up
	int rule2Stopped;            // ticket 4855: rule 2 (a) fired (cell 65,535 held a node before a builder call)
	// ticket 0655: the farm-end post on the skipped-server path (the builder's last step, nothing else of it)
	uint32_t posts;              // farm-end records posted
	uint32_t postFailFlag, postFailMap, postFailArt;   // events that failed the builder's test: the flag byte, the plot on the
	                                                   // map, the farm's art id (no post, as the builder would not post)
	uint32_t postRefused;        // events not posted for another reason: the post is off for the build (its sites did not
	                             // match), the payload or the queue index could not be read, the poster returned nothing
	uint32_t postSwitchedOff;    // events not posted because farm_end_post=0
	int postReady;               // 1 = the post's sites matched (or a test world carries a poster)
	int tripped, dead, ready;
	double checkMsTotal, checkUsMax;
	// ticket 0655 (P1): the diagnostic area probe
	int areaReady;               // 1 = its sites matched (or a test world was given)
	uint32_t areaSwaps, areaRestored, areaRetired, areaLines;
	uint32_t areaCalls;          // calls timed in this process
	double areaUsMax;
	// ticket 0657: the edge placement
	int edgeReady;               // 1 = its sites matched (or a test world was given)
	int edgeSection;             // 1 = <base>.Farm is mapped (or a test block given)
	uint32_t edgeSwaps, edgeRestored, edgeRetired;   // node words changed (both lists)
	uint32_t endCalls, endPlaced, endCentre, endHeld, endHeldDefer, endExpired, endRepostFail, endNoSection, endDropped, endStale;
	                             // farm ends through the pass-through: placed at a server point (the transient curve), let
	                             // through to the plot centre (no point, kind CENTRE, edge off for this event), held (re-posted)
	                             // for the points / for the deferral, holds expired (then the centre), re-posts that failed,
	                             // no section, dropped with the view shut down, calls through a stale node
	uint32_t endChecked, endOff1;   // placed records whose position was read back after the call; of them more than 1 unit off
	double endErrMax;            // the largest distance between a placed record and its point (world units)
	uint32_t eraCalls, eraCurves, eraDeferred, eraDropped, eraStale;   // era events; transient curves given; deferred; dropped; stale
	uint32_t edgeFaults;         // a read or write of the manager or its records faulted (that event ran as before 0657)
	// ticket 0784: the tread filter
	int treadReady;              // 1 = its sites matched (or a test world was given)
	uint32_t treadSwaps, treadRestored, treadRetired;   // node words changed
	uint32_t treadCalls;         // records through the pass-through (main thread)
	uint32_t treadDropped0, treadDropped1;   // dropped: ART_DEF_VEFFECT_TANK_TREADS, _02 (no VP64 tag)
	uint32_t treadTagged;        // a tread record with the VP64 tag (the shell's effect player): passed
	uint32_t treadOff;           // records passed because the filter was switched off before its node was restored
	uint32_t treadFaults;        // the payload could not be read: passed
	uint32_t treadLines;         // "treads" lines written
};
void FarmGuard_GetStats(FgStats* s);
FgListenerFn FarmGuard_AreaProbe();
FgListenerFn FarmGuard_AreaInert();
FgListenerFn FarmGuard_EdgeEnd();   // ticket 0657: the two pass-throughs
FgListenerFn FarmGuard_EdgeEra();
FgListenerFn FarmGuard_TreadPass();   // ticket 0784: the tread filter's pass-through
// Test: the tread filter's world (lock, list of type 0x28, the listener as `builder`, its object); NULL = none.
void FarmGuard_TestUseTread(const FgWorld* tw);
// Test: the area probe's world (lock, list of type 33, the listener as `builder`, its object); NULL = no probe.
void FarmGuard_TestUseArea(const FgWorld* aw);
// Test: use this world (no pattern check) and start again from nothing; recordDir = where farm-guard.log goes.
void FarmGuard_TestUse(const FgWorld* w, const wchar_t* recordDir);
// Test: the burst line's quiet time and the in-game line's delay (ms; FarmGuard_TestUse sets 2000 and 10000 again).
void FarmGuard_TestTimes(double quietMs, double inGameMs);

// ---- ticket 0535: one owner per process ---------------------------------------------------------------------------------
#define FG_SHARED_ENV L"VP64_FARM_GUARD"     // the shared page's address, 8 hex digits, in the EXE process's environment
#define FG_SHARED_MAGIC 0x56504647u          // "GFPV"
#define FG_SHARED_VERSION 3u                 // 2: ticket 0657's edge stubs; 3: ticket 0784's tread stub
struct FgShared                              // the first page (read and write); the second page holds the stubs (execute, read)
{
	uint32_t magic, version, pid, self;      // self = the page's own address
	volatile LONG hookTarget;                // the attached owner's hook; 0 = the hook stub returns at once (ret 4)
	volatile LONG presentFn;                 // the attached owner's FgPresentFn for a composite's Presents; 0 = none
	volatile LONG attaches, detaches;        // owner images attached and detached in this process
	volatile LONG forwarded;                 // Presents a composite handed to the owner
	volatile LONG tripped, rule2Stopped;     // the guard tripped / rule 2 (a) fired in this process (kept across boots)
	uint32_t hookStub, inertStub;            // the two stubs' addresses (self + 0x1000, self + 0x1020)
	char owner[16];                          // "proxy"
	// version 2 (ticket 0657): the edge pass-throughs' stubs: mov eax, [edgeTarget[i]]; test; jnz; mov eax, [edgeOrig[i]]; jmp eax
	volatile LONG edgeTarget[2];             // [0] the farm end, [1] the era: the attached owner's pass-through; 0 = the EXE's own
	volatile LONG edgeOrig[2];               // the EXE's listeners (rebased), written by the first owner that checked the EXE
	uint32_t edgeStub[2];                    // self + 0x1040, self + 0x1060
	// version 3 (ticket 0784): the tread filter's stub, the edge stubs' form
	volatile LONG treadTarget;               // the attached owner's pass-through; 0 = the EXE's own listener
	volatile LONG treadOrig;                 // the Particle object's type-0x28 listener (rebased)
	uint32_t treadStub;                      // self + 0x1080
};
typedef void(__cdecl* FgPresentFn)(uint32_t gridValid, uint32_t gridSeq);

// The owner (the proxy's boot thread): finds the page named by VP64_FARM_GUARD (an earlier proxy image of this process) or
// creates it and names it there, and makes this module the owner: its node words are the page's stubs, its lines name
// `owner`. The stubs reach this module's hook only once FarmGuard_OnTick has checked the EXE. False (why) when no page.
bool FarmGuard_OwnerAttach(const char* owner, char* why, size_t whyCap);
// The owner's unload (DLL_PROCESS_DETACH, under the loader lock; no logging): hookTarget and presentFn back to 0, the event
// log closed. A node holding the hook stub returns at once until the next owner attaches.
void FarmGuard_OwnerDetach();
// The owner's periodic tick (the proxy's watcher thread): the EXE check once, the kill switch, the swap; the burst lines (no
// graph read: this is not the main thread) while no composite hands over its Presents.
void FarmGuard_OnTick(const FarmGuardEnv& env);
// The page found from the environment and checked (0 = none or not valid; why says which).
uintptr_t FarmGuard_FindShared(char* why, size_t whyCap);
// The node words in use (the stubs in the owner, else FgHook / FgInert) and the role: 1 owner, 2 follower (a composite
// that found an owner's page), 0 alone (the composite without a proxy guard: 0527's form).
uintptr_t FarmGuard_HookWord();
uintptr_t FarmGuard_InertWord();
int FarmGuard_Role();
// Test: forget the role (the owner's page stays: it is never freed), as a fresh image.
void FarmGuard_TestForgetRole();
