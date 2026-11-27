// srv_remote.h - internal declarations of the x64 server's remote mode (epic 2300, task 2303). Not seen by the
// generated code (that sees srv_rt.h only). Design: research/03-dll-engine-boundary/proxy-design.md section 11.
//
// Files:
//   srv_remote_main.cpp     RemoteMain: settings, attach, service threads, kRt_Boot / Shutdown / Stats / StreamOpen,
//                           the report, the SetGameDatabase hook
//   srv_rt.cpp              the SrvRt API (srv_rt.h): channels, calls, handle table, streams, counters, loud stubs
//   srv_remote_objects.cpp  the engine objects: EngineWrap / EngineForget / EngineHandleOf and the hand-written
//                           subclasses of RemoteGen_ICvEngineUtility4 (local lock, thread role) and
//                           RemoteGen_ICvUserInterface2
//   srv_remote_human.cpp    the human-game paths (issue 2503): ServerHooks of the node walker, static tables, combat
//                           members, SetDLCAvailable, loadFromIni (remote ini parser); the local enumerator
//   srv_remote_lua.cpp      Lua: scratch states, value codec, kRt_Lua*, the RemoteGen_ICvEngineScriptSystem1 and
//                           RemoteGen_ICvEngineScriptSystemArgs1 subclasses, the Push/Get*Instance and
//                           PushReplayFromStream hooks
#pragma once
// Issue 4754: SRV_REMOTE_UI_RECORD_ONLY (defined only by server/tests/feed/test_ui_record.cpp) leaves out everything
// but the ui record's block at the end, which needs no DLL header.
#ifndef SRV_REMOTE_UI_RECORD_ONLY
#include "srv_rt.h"
#include <string>
#include <vector>

namespace SrvRemote
{
// ---- srv_remote_main.cpp ----
void Report(const char* szFormat, ...);          // a line of server-report.txt (and server.log)
void Fail(const char* szWhy);                    // marks the run failed (result line), reported
// Writes server-report.txt, proxy-calls.tsv and the per-turn rows, then ends the process with `code`. Once per
// process; a second caller (another thread) waits until the first has terminated the process.
__declspec(noreturn) void ReportAndExit(int code, const char* szWhy);
bool WriteReport(std::string* pstrPath);         // server-report.txt and the call tables; false if already written
bool FailOnUnexpected();                         // fail_on_unexpected=1 in the config
// The allow-list rule (unexpected_allow=<file>, issue 9008) that covers this unserved key, or NULL.
const char* UnexpectedAllowed(const std::string& key);
ICvGameContext1* Context();                      // the DLL's game context after kRt_Boot (NULL before)

// ---- srv_rt.cpp ----
void Init(vpipc::Endpoint* pEp);                 // before the service threads start
void BindThread(uint32_t uChannel);              // a service thread: this thread serves (and calls on) that channel
int ThreadChannel();                             // -1: not a service thread
vpipc::Endpoint* Ep();
// The call handler of every channel: runtime methods (kRt_*), then DispatchGenerated; counts and times each call.
void OnCall(void* ctx, vpipc::Channel& ch, uint32_t method, vpipc::Reader& args, vpipc::Writer& ret);
void OnPost(void* ctx, vpipc::Channel& ch, uint32_t method, vpipc::Reader& args);
// Issue 0178: a kRt_UiRecord post of the proxy's Lua bridge: checked, counted, pushed into the renderer's feed
// (SrvFeed::kUiMirror). Never touches the DLL (rule R1). UiRecordSummary: one line for the report. Issue 4754: both
// use SrvRemote::UiRec at the end of this file (decode, the event's layout, the counters).
void UiRecord(vpipc::Reader& args, uint32_t uChannel);
std::string UiRecordSummary();
bool InUpdate();                                // this thread is inside the dispatch of ICvGame1::Update
uint32_t StreamOpen(const uint8_t* p, uint32_t n);
uint32_t StreamOpenPull(uint32_t token);   // issue 2500: a pull session (kRt_StreamOpenPull)   // kRt_StreamOpen: a session over a copy of the bytes
// Ticket 0422: the engine calls this thread is serving, outermost first ("ICvGame1::Update > runtime::kRt_LuaCall"),
// for the dll-fatal report; "" when none. Kept as a small per-thread stack in OnCall (two stores per call).
std::string ServedCallsText();
std::string StatsTable();                        // kRt_Stats: the counter table (TSV)
void WriteCallTables();                          // proxy-calls.tsv (totals) and the last rows of proxy-calls-per-turn.tsv
void ReportCounts(FILE* f);                      // unserved / unimplemented list and failure counters for the report
long UnservedCount();

// ---- srv_remote_objects.cpp ----
void LockInit();
int CoreLockYield();                             // kRt_CoreLockYield: releases every level this thread holds
void CoreLockResume(int iDepth);                 // kRt_CoreLockResume: takes the lock iDepth times again
void ObjectsReport(FILE* f);
// issue 2700 (local answers, proxy-design section 16)
bool LockOpen(uint32_t uProxyPid);               // maps Local\VPProxyLock_<pid> (at kRt_Boot)
void LockSync(uint32_t uChannel);                // the prologue of every served call on ui/core
bool Canon(uint32_t uGen, uint32_t uCode, int32_t iIndex, vpipc::Handle& h, std::string& strErr);  // kRt_Canon
void CanonReport(FILE* f);
void CanonInit();                                // from LockInit
bool LocalOn();                                  // the proxy's local answers are on (want word 3, bit 0)
void ServedEnter();                              // OnCall entry / exit on this thread (D30 condition 4)
void ServedLeave();
void NoteEngineCall();                           // every DLL -> engine call: want word 4+channel; outside-call check
                                                 // (issue 2801: also every engine method the server answers itself)
// Issue 2801: OnCall brackets ICvGameContext1::Init / Uninit and ICvDLLDatabaseUtility1::CacheGameDatabaseData /
// FlushGameDatabaseData with these; while one is served on any thread, Canon refuses (the proxy's getter crosses).
bool GenChangeEnter(uint32_t method);            // true: method is one of the four (pass it to GenChangeLeave)
void GenChangeLeave(bool bEntered);
long OutsideCount();
void MaybeOutsideTest();
bool LocalVerify();                              // ... with local_verify=1 (bit 1)

// ---- srv_remote_lua.cpp ----
bool HandleLuaRt(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);
long LuaScratchErrors();
void LuaReport(FILE* f);                         // scratch Lua errors (first 50), codec counters
// Ticket 0422: the innermost kRt_LuaCall this thread is serving, as the dll-fatal report names it
// ("Plot.IsResourceConnectedByImprovement(a Plot object, -1) on the ui channel"); "" when none.
std::string LuaCallText();
}  // namespace SrvRemote

// Factories of the hand-written remote subclasses (srv_remote_lua.cpp), used by EngineWrap.
ICvEngineScriptSystem1* VpNewRemoteScriptSystem(vpipc::Handle h);
ICvEngineScriptSystemArgs1* VpNewRemoteArgs(vpipc::Handle h);
void VpDeleteRemoteArgs(ICvEngineScriptSystemArgs1* p);

// ---- srv_remote_human.cpp (issue 2503) ----
// A server-side ICvEnumerator over the given objects (each carrying one reference, which the enumerator now owns; the
// vector is emptied). One reference to the enumerator for the caller; GetCurrent returns a new reference to the item.
ICvEnumerator* VpNewLocalEnumerator(std::vector<ICvUnknown*>& items);
#endif  // SRV_REMOTE_UI_RECORD_ONLY

// ---- issues 0178 and 4754: the ui record of the proxy's Lua bridge: decoded, counted, mapped into the feed event ------
// Header-only and free of the DLL's headers, so server/tests/feed/test_ui_record.cpp builds it alone (with srv_feed.cpp).
// srv_remote_main.cpp's UiRecord and UiRecordSummary are thin wrappers around it.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "feed/srv_feed.h"

namespace SrvRemote
{
namespace UiRec
{
// The wire record: UiBridge::Record of proxy/src/proxy_ui_bridge.h (32 bytes, little-endian): u16 version, u16 kind,
// u32 flags, i32 a[4], u32 seq, u32 gen. The proxy's header is not included on this side; the values are repeated here.
enum : uint32_t { kBytes = 32 };
enum : uint16_t { kVersion = 1 };
enum : uint16_t { kKindUnitPosition = 6, kKindFrameMark = 7, kKinds = 8 };   // UiBridge::Kind (1-5: 0178's)
enum : uint32_t { kPosPlayerShift = 16, kPosPlayerMask = 0xFFu << 16, kPosArrival = 1u << 24 };   // kind 6's flags

struct Fields
{
	uint16_t version, kind;
	uint32_t flags;
	int32_t a[4];
	uint32_t seq, gen;
};

// False when the bytes are not one record of this version; r.version then holds the version read (0 when none was).
inline bool Decode(const uint8_t* p, uint32_t n, Fields& r)
{
	memset(&r, 0, sizeof(r));
	if(!p || n < 2)
		return false;
	memcpy(&r.version, p, 2);
	if(n != kBytes || r.version != kVersion)
		return false;
	memcpy(&r.kind, p + 2, 2);
	memcpy(&r.flags, p + 4, 4);
	memcpy(r.a, p + 8, 16);
	memcpy(&r.seq, p + 24, 4);
	memcpy(&r.gen, p + 28, 4);
	return true;
}

// The feed event (srv_feed.h kUiMirror): flags = the record's kind, a[0..3] = its four values, plot = turn = -1; for
// kind 6 (kKindUnitPosition) plot = the player (the record's flags bits 16-23) and turn = 1 for an arrival
// (kPosArrival), 0 for a position. The record's own flags are not kept otherwise (kinds 1-5 carry argument flags only).
inline SrvFeed::Event ToEvent(const Fields& r)
{
	SrvFeed::Event e = {};
	e.kind = SrvFeed::kUiMirror;
	e.flags = r.kind;
	e.plot = -1;
	e.turn = -1;
	if(r.kind == kKindUnitPosition)
	{
		e.plot = (int32_t)((r.flags & kPosPlayerMask) >> kPosPlayerShift);
		e.turn = (r.flags & kPosArrival) ? 1 : 0;
	}
	for(int i = 0; i < 4; ++i)
		e.a[i] = r.a[i];
	return e;
}

// The counters of the report line. Written by the ui service thread (the post handler), read by the report.
struct Tally
{
	volatile LONG kinds[kKinds + 1];   // by kind; [kKinds] = a kind above the known ones
	volatile LONG total, bad, seqGaps, lastSeq, lastGen;
	volatile LONG arrivals;            // kind-6 records with kPosArrival (kinds[6] counts them too)
	volatile LONG announced;           // the sum of the frame marks' a[1] (the kind-6 records they announce)

	// Counts a decoded record; returns how many of its kind have come (1 = the first).
	long Add(const Fields& r)
	{
		InterlockedIncrement(&total);
		const long n = InterlockedIncrement(&kinds[r.kind < kKinds ? r.kind : kKinds]);
		if(r.kind == kKindUnitPosition && (r.flags & kPosArrival))
			InterlockedIncrement(&arrivals);
		if(r.kind == kKindFrameMark && r.a[1] > 0)
			InterlockedExchangeAdd(&announced, (LONG)r.a[1]);
		const LONG prev = InterlockedExchange(&lastSeq, (LONG)r.seq);
		if(prev != 0 && (uint32_t)prev + 1 != r.seq)
			InterlockedIncrement(&seqGaps);
		InterlockedExchange(&lastGen, (LONG)r.gen);
		return n;
	}
	long Bad() { return InterlockedIncrement(&bad); }

	// "ui records (issue 0178): N received (arrow .., unit-position K (positions P, arrivals A), frame-mark M (announcing
	// K'), other ..), bad .., sequence gaps .., last seq .. gen .."
	std::string Summary() const
	{
		char sz[640];
		sprintf_s(sz, "ui records (issue 0178): %ld received (arrow %ld, remove-arrows %ld, minimap %ld, movement %ld, "
		              "unit-moving %ld, unit-position %ld (positions %ld, arrivals %ld), frame-mark %ld (announcing %ld), "
		              "other %ld), bad %ld, sequence gaps %ld, last seq %ld gen %ld",
		          total, kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[kKindUnitPosition],
		          kinds[kKindUnitPosition] - arrivals, arrivals, kinds[kKindFrameMark], announced, kinds[0] + kinds[kKinds],
		          bad, seqGaps, lastSeq, lastGen);
		return sz;
	}
};
}  // namespace UiRec
}  // namespace SrvRemote
