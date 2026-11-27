// srv_rt.cpp - the SrvRt runtime (srv_rt.h) of the server's remote mode (epic 2300, task 2303): channel per service
// thread, outbound calls with their temporary registrations, the handle table of DLL objects, stream sessions,
// counters per method id (proxy-calls.tsv, proxy-calls-per-turn.tsv), loud stubs.
// Design: research/03-dll-engine-boundary/proxy-design.md 11.2-11.4, 11.6, 11.9. Wire values: proxy/include/vp_wire.h.
#include "srv_remote.h"
#include "srv_remote_feed.h"  // issue 3303: the renderer's feed takes its plot snapshot in LeaveUpdate
#include "srv_budget.h"       // ticket 4652: the turn budget (VP64_TURN_BUDGET=1; off: one bool test per hook)
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include <FireWorks/FMemoryStream.h>
#include <stdio.h>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace
{
	vpipc::Endpoint* s_ep = NULL;
	__declspec(thread) int t_iChan = -1;
	__declspec(thread) int t_iUpdateDepth = 0;

	LARGE_INTEGER s_liFreq;
	double Now()
	{
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return double(li.QuadPart) / double(s_liFreq.QuadPart);
	}

	// ---- counters ----
	struct Counter
	{
		uint64_t calls = 0, bytesIn = 0, bytesOut = 0;
		double sec = 0.0;
		void Add(const Counter& o) { calls += o.calls; bytesIn += o.bytesIn; bytesOut += o.bytesOut; sec += o.sec; }
	};
	CRITICAL_SECTION s_csCount;
	std::map<uint32_t, Counter> s_totals;	// method id -> totals
	std::map<uint32_t, Counter> s_snap;		// totals at the last per-turn flush
	int s_iLastTurn = -1;					// -1: everything up to the end of the first Update
	FILE* s_pPerTurn = NULL;
	long s_lFailedCalls = 0, s_lDecodeProblems = 0, s_lWrongThreadCalls = 0, s_lBadHandles = 0, s_lDispatchFails = 0,
	     s_lUnknownMethods = 0, s_lPosts = 0;

	// Served (engine -> DLL, runtime proxy -> server): "in"; made (DLL -> engine, runtime server -> proxy): "out".
	bool IsOutbound(uint32_t method)
	{
		const uint32_t type = method >> 16;
		if(type == vpw::kRtTypeServerToProxy)
			return true;
		if(type == vpw::kRtTypeProxyToServer)
			return false;
		return vpk_is_engine_iface(type);
	}
	std::string IfaceName(uint32_t method)
	{
		const uint32_t type = method >> 16;
		if(type == vpw::kRtTypeProxyToServer || type == vpw::kRtTypeServerToProxy)
			return "runtime";
		const char* sz = vpk_iface_name(type);
		return sz ? sz : "?";
	}
	std::string MethodKey(uint32_t method)
	{
		switch(method)
		{
		case vpw::kRt_Boot: return "kRt_Boot";
		case vpw::kRt_Shutdown: return "kRt_Shutdown";
		case vpw::kRt_Stats: return "kRt_Stats";
		case vpw::kRt_LuaRegister: return "kRt_LuaRegister";
		case vpw::kRt_LuaDescribeType: return "kRt_LuaDescribeType";
		case vpw::kRt_LuaCall: return "kRt_LuaCall";
		case vpw::kRt_LuaIndex: return "kRt_LuaIndex";
		case vpw::kRt_LuaRelease: return "kRt_LuaRelease";
		case vpw::kRt_StreamOpen: return "kRt_StreamOpen";
		case vpw::kRt_CoreLockYield: return "kRt_CoreLockYield";
		case vpw::kRt_StreamOpenPull: return "kRt_StreamOpenPull";
		case vpw::kRtE_StreamRead: return "kRtE_StreamRead";
		case vpw::kRt_CoreLockResume: return "kRt_CoreLockResume";
		case vpw::kRt_Canon: return "kRt_Canon";
		case vpw::kRt_LockSync: return "kRt_LockSync";
		case vpw::kRtE_DbOps: return "kRtE_DbOps";
		case vpw::kRtE_DbFingerprint: return "kRtE_DbFingerprint";
		case vpw::kRtE_LuaRunChunk: return "kRtE_LuaRunChunk";
		case vpw::kRtE_LuaGetGlobal: return "kRtE_LuaGetGlobal";
		case vpw::kRtE_LuaCall: return "kRtE_LuaCall";
		case vpw::kRtE_LuaRelease: return "kRtE_LuaRelease";
		case vpw::kRtE_LuaRaise: return "kRtE_LuaRaise";  // ticket 4804
		default: break;
		}
		const char* sz = vpk_method_key(method);
		return sz ? sz : "?";
	}
	void Count(uint32_t method, uint32_t bytesIn, uint32_t bytesOut, double sec)
	{
		EnterCriticalSection(&s_csCount);
		Counter& c = s_totals[method];
		++c.calls;
		c.bytesIn += bytesIn;
		c.bytesOut += bytesOut;
		c.sec += sec;
		LeaveCriticalSection(&s_csCount);
	}

	// Per-turn rows since the last flush: (turn, interface, direction, calls, bytes, seconds). Caller holds s_csCount.
	void FlushTurnRowsLocked(int iTurn)
	{
		if(!s_pPerTurn)
			return;
		std::map<std::pair<std::string, int>, Counter> rows;
		for(std::map<uint32_t, Counter>::const_iterator it = s_totals.begin(); it != s_totals.end(); ++it)
		{
			Counter d = it->second;
			std::map<uint32_t, Counter>::const_iterator s = s_snap.find(it->first);
			if(s != s_snap.end())
			{
				d.calls -= s->second.calls;
				d.bytesIn -= s->second.bytesIn;
				d.bytesOut -= s->second.bytesOut;
				d.sec -= s->second.sec;
			}
			if(d.calls)
				rows[std::make_pair(IfaceName(it->first), IsOutbound(it->first) ? 1 : 0)].Add(d);
		}
		for(std::map<std::pair<std::string, int>, Counter>::const_iterator it = rows.begin(); it != rows.end(); ++it)
			fprintf(s_pPerTurn, "%d\t%s\t%s\t%llu\t%llu\t%.6f\n", iTurn, it->first.first.c_str(),
			        it->first.second ? "dll->engine" : "engine->dll", (unsigned long long)it->second.calls,
			        (unsigned long long)(it->second.bytesIn + it->second.bytesOut), it->second.sec);
		fflush(s_pPerTurn);
		s_snap = s_totals;
	}

	// ---- loud stubs ----
	struct UnservedEntry
	{
		uint32_t method;
		long calls;
	};
	CRITICAL_SECTION s_csUnserved;
	std::map<std::string, UnservedEntry> s_unserved;

	// Logs the first few occurrences of a kind of problem, then every 1000th.
	bool ShouldLog(long n) { return n <= 20 || n % 1000 == 0; }

	// ---- handle table of DLL objects ----
	struct Slot
	{
		const void* p = NULL;
		uint32_t kind = 0;
		long refs = 0;		// persistent registrations (Register) not yet destroyed
		long temps = 0;		// open temporary registrations (RegisterTemp in calls still in progress)
		bool used = false;
	};
	SRWLOCK s_lockH = SRWLOCK_INIT;
	std::vector<Slot> s_slots(1);			// slot 0: never used (id 0 = null)
	std::vector<int32_t> s_free;
	std::unordered_map<const void*, int32_t> s_byPtr;
	long s_lKindConflicts = 0, s_lSlotsMax = 0;

	int32_t FindOrCreateLocked(const void* p, uint32_t kind)
	{
		std::unordered_map<const void*, int32_t>::iterator it = s_byPtr.find(p);
		if(it != s_byPtr.end())
		{
			Slot& s = s_slots[it->second];
			if(s.kind != kind && kind)
			{
				// The same pointer may arrive as ICvUnknown (ICvEnumerator::GetCurrent) and later with a more specific
				// kind from a QueryInterface: keep the most specific one.
				if(vpk_in_chain(kind, s.kind))
					s.kind = kind;
				else if(!vpk_in_chain(s.kind, kind))
				{
					const long n = ++s_lKindConflicts;
					if(ShouldLog(n))
						SrvLog("remote: handle %d (%p): kind %s, now registered as %s (unrelated kinds; the newer wins)",
						       it->second, p, vpk_iface_name(s.kind), vpk_iface_name(kind));
					s.kind = kind;
				}
			}
			return it->second;
		}
		int32_t id;
		if(!s_free.empty())
		{
			id = s_free.back();
			s_free.pop_back();
		}
		else
		{
			id = (int32_t)s_slots.size();
			s_slots.push_back(Slot());
		}
		Slot& s = s_slots[id];
		s.p = p;
		s.kind = kind;
		s.refs = 0;
		s.temps = 0;
		s.used = true;
		s_byPtr[p] = id;
		if((long)s_byPtr.size() > s_lSlotsMax)
			s_lSlotsMax = (long)s_byPtr.size();
		return id;
	}
	void ReleaseIfUnusedLocked(int32_t id)
	{
		Slot& s = s_slots[id];
		if(s.used && s.refs <= 0 && s.temps <= 0)
		{
			s_byPtr.erase(s.p);
			s = Slot();
			s_free.push_back(id);
		}
	}
	vpipc::Handle MakeHandle(int32_t id)
	{
		vpipc::Handle h;
		h.type = vpw::MakeType(s_slots[id].kind, false);
		h.id = id;
		return h;
	}

	// ---- outbound calls in progress on this thread (nested calls stack) ----
	struct Frame
	{
		uint32_t method;
		double t0;
		vpipc::Writer* w;
		std::vector<int32_t> temps;
	};
	thread_local std::vector<Frame> t_frames;

	// ---- stream sessions (kRt_StreamOpen) ----
	// Issue 2500 (live): a pull session's stream fetches exactly the bytes the DLL reads from the engine stream
	// (kRtE_StreamRead), keeping everything pulled so SetPosition inside it works; never reads ahead.
	class PullStream : public FMemoryStream
	{
	public:
		explicit PullStream(uint32_t token) : m_token(token) { FMemoryStream::Init(0, NULL, false); }
		unsigned int ReadIt(unsigned int byteCount, void* buffer) override
		{
			Ensure(byteCount);
			return FMemoryStream::ReadIt(byteCount, buffer);
		}
		void Skip(unsigned int delta) override
		{
			Ensure(delta);
			FMemoryStream::Skip(delta);
		}
		bool AtEnd() const override { return false; }
		unsigned int GetSizeLeft() const override { return 0x7FFFFFFFu; }
		uint64_t Pulls() const { return m_pulls; }

	private:
		void Ensure(unsigned int n)
		{
			const unsigned int pos = GetPosition();
			if((uint64_t)pos + n <= m_buf.size())
				return;
			const uint32_t want = (uint32_t)((uint64_t)pos + n - m_buf.size());
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_StreamRead);
			w.u32(m_token);
			w.u32(want);
			vpipc::Reader r;
			if(SrvRt::End(ch, vpw::kRtE_StreamRead, r))
			{
				uint32_t got = 0;
				const uint8_t* p = r.bytes(&got);
				if(p && got)
					m_buf.append(reinterpret_cast<const char*>(p), got);
			}
			++m_pulls;
			FMemoryStream::Init((int)m_buf.size(), m_buf.data(), false);
			FMemoryStream::SetPosition(pos);
		}
		uint32_t m_token;
		std::string m_buf;
		uint64_t m_pulls = 0;
	};

	struct Session
	{
		std::string bytes;
		FMemoryStream stream;
		PullStream* pull = NULL;
		~Session() { delete pull; }
	};
	CRITICAL_SECTION s_csStreams;
	std::map<uint32_t, Session*> s_sessions;
	uint32_t s_uNextSession = 1;
	const size_t kKeepSessions = 4;		// a load reads one session; older ones are freed
	struct StreamUse
	{
		FDataStream* s;
		uint32_t start;
	};
	thread_local std::vector<StreamUse> t_streamIn;
	thread_local std::vector<FMemoryStream*> t_streamOut;
	long s_lStreamProblems = 0;
}

// =====================================================================================================================
// SrvRemote: setup and the call handler

namespace SrvRemote
{
void Init(vpipc::Endpoint* pEp)
{
	s_ep = pEp;
	QueryPerformanceFrequency(&s_liFreq);
	InitializeCriticalSection(&s_csCount);
	InitializeCriticalSection(&s_csUnserved);
	InitializeCriticalSection(&s_csStreams);
	s_pPerTurn = _fsopen("proxy-calls-per-turn.tsv", "w", _SH_DENYWR);
	if(s_pPerTurn)
	{
		fprintf(s_pPerTurn, "turn\tinterface\tdirection\tcalls\tbytes\tseconds\n");
		fflush(s_pPerTurn);
	}
}

void BindThread(uint32_t uChannel)
{
	t_iChan = (int)uChannel;
	s_ep->bind_current_thread(uChannel);
}
int ThreadChannel() { return t_iChan; }
vpipc::Endpoint* Ep() { return s_ep; }
bool InUpdate() { return t_iUpdateDepth > 0; }

// Ticket 0422: the methods this thread is serving (OnCall nests: engine -> DLL -> engine -> DLL), for the dll-fatal report.
namespace
{
	const int kServedMax = 16;
	__declspec(thread) uint32_t t_served[kServedMax];
	__declspec(thread) int t_nServed = 0;
	struct ServedStackScope
	{
		explicit ServedStackScope(uint32_t method)
		{
			if(t_nServed < kServedMax)
				t_served[t_nServed] = method;
			++t_nServed;
		}
		~ServedStackScope() { --t_nServed; }
	};
}  // namespace

std::string ServedCallsText()
{
	std::string s;
	const int n = t_nServed < kServedMax ? t_nServed : kServedMax;
	for(int i = 0; i < n; ++i)
	{
		if(i)
			s += " > ";
		s += IfaceName(t_served[i]) + "::" + MethodKey(t_served[i]);
	}
	if(t_nServed > kServedMax)
		s += " > (more)";
	return s;
}

bool HandleRt(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);	// srv_remote_main.cpp

void OnCall(void*, vpipc::Channel& ch, uint32_t method, vpipc::Reader& args, vpipc::Writer& ret)
{
	// Ticket 4652: the served frame of the turn budget, first and last thing of the call (also on unwinding).
	struct BudgetServed
	{
		bool on;
		~BudgetServed()
		{
			if(on)
				SrvBudget::ServedLeave();
		}
	} budgetServed = {SrvBudget::On()};
	if(budgetServed.on)
		SrvBudget::ServedEnter(method);
	const double t0 = Now();
	const uint32_t uIn = args.remaining();
	// The CALL payload lives in the channel's data area, which the next nested call on this channel overwrites;
	// a handler that calls back (a DLL call inside an engine call, Lua) must still read its arguments, and
	// const char* arguments decoded by pointer must stay valid during the call: work on a private copy
	// (2026-11-10, integration: CreateLuaThread's name was overwritten by the nested RegisterScriptLibraries).
	std::vector<uint8_t> argsCopy(args.cursor(), args.cursor() + args.remaining());
	args.reset(argsCopy.empty() ? NULL : &argsCopy[0], (uint32_t)argsCopy.size());
	// Issue 2700: the explicit game-core lock depth of this thread follows the proxy's want word (local lock wrappers
	// of the calling EXE thread) before anything of the call runs; kRt_LockSync carries nothing else.
	SrvRemote::LockSync(ch.index());
	const ServedStackScope servedStack(method);  // ticket 0422: the dll-fatal report names the calls being served
	SrvRemote::ServedEnter();  // D30 condition 4: DLL code runs only inside a served call
	struct ServedScope
	{
		bool bGen;
		~ServedScope()
		{
			SrvRemote::GenChangeLeave(bGen);
			SrvRemote::ServedLeave();
		}
	} servedScope = {SrvRemote::GenChangeEnter(method)};  // issue 2801: no canonical object while the DLL rebuilds
	bool bKnown;
	if((method >> 16) == vpw::kRtTypeProxyToServer)
		bKnown = HandleRt(method, args, ret, ch) || HandleLuaRt(method, args, ret, ch);
	else
		bKnown = DispatchGenerated(method, args, ret, ch);
	if(!bKnown)
	{
		const long n = InterlockedIncrement(&s_lUnknownMethods);
		if(ShouldLog(n))
			SrvLog("remote: unknown method 0x%08x on channel %u", method, ch.index());
		ch.set_return_status(vpw::kStatusUnknownMethod);
		char sz[64];
		sprintf_s(sz, "unknown method 0x%08x", method);
		ret.str(sz);
	}
	Count(method, uIn, ret.size(), Now() - t0);
}

void OnPost(void*, vpipc::Channel& ch, uint32_t method, vpipc::Reader& args)
{
	// Issue 0178: the proxy's Lua bridge posts UI records (kRt_UiRecord) on the ui channel. A post handler makes no call
	// and touches no DLL object: the record only goes into the renderer's feed (rule R1).
	if(method == vpw::kRt_UiRecord)
	{
		UiRecord(args, ch.index());
		return;
	}
	// Epic 2300 sends no other posts (every call is synchronous, proxy-design 11).
	const long n = InterlockedIncrement(&s_lPosts);
	if(ShouldLog(n))
		SrvLog("remote: unexpected post 0x%08x on channel %u (ignored)", method, ch.index());
}

uint32_t StreamOpenPull(uint32_t token)
{
	Session* s = new Session();
	s->pull = new PullStream(token);
	EnterCriticalSection(&s_csStreams);
	const uint32_t id = s_uNextSession++;
	s_sessions[id] = s;
	while(s_sessions.size() > kKeepSessions)
	{
		delete s_sessions.begin()->second;
		s_sessions.erase(s_sessions.begin());
	}
	LeaveCriticalSection(&s_csStreams);
	SrvLog("remote: stream session %u: pull session over proxy stream token %u (engine stream of unknown size)", id, token);
	return id;
}

uint32_t StreamOpen(const uint8_t* p, uint32_t n)
{
	Session* s = new Session();
	s->bytes.assign(reinterpret_cast<const char*>(p), n);
	s->stream.Init((int)n, s->bytes.data(), false);
	EnterCriticalSection(&s_csStreams);
	const uint32_t id = s_uNextSession++;
	s_sessions[id] = s;
	while(s_sessions.size() > kKeepSessions)
	{
		delete s_sessions.begin()->second;
		s_sessions.erase(s_sessions.begin());
	}
	LeaveCriticalSection(&s_csStreams);
	return id;
}

std::string StatsTable()
{
	std::string out = "method\tkey\tdirection\tcalls\tbytes_in\tbytes_out\tseconds\n";
	EnterCriticalSection(&s_csCount);
	for(std::map<uint32_t, Counter>::const_iterator it = s_totals.begin(); it != s_totals.end(); ++it)
	{
		char sz[256];
		sprintf_s(sz, "0x%08x\t", it->first);
		out += sz;
		out += MethodKey(it->first);
		sprintf_s(sz, "\t%s\t%llu\t%llu\t%llu\t%.6f\n", IsOutbound(it->first) ? "dll->engine" : "engine->dll",
		          (unsigned long long)it->second.calls, (unsigned long long)it->second.bytesIn,
		          (unsigned long long)it->second.bytesOut, it->second.sec);
		out += sz;
	}
	LeaveCriticalSection(&s_csCount);
	return out;
}

void WriteCallTables()
{
	const std::string t = StatsTable();
	FILE* f = NULL;
	if(!fopen_s(&f, "proxy-calls.tsv", "w") && f)
	{
		fwrite(t.data(), 1, t.size(), f);
		fclose(f);
	}
	EnterCriticalSection(&s_csCount);
	FlushTurnRowsLocked(s_iLastTurn);
	if(s_pPerTurn)
		fclose(s_pPerTurn);
	s_pPerTurn = NULL;
	LeaveCriticalSection(&s_csCount);
}

long UnservedCount()
{
	EnterCriticalSection(&s_csUnserved);
	const long n = (long)s_unserved.size();
	LeaveCriticalSection(&s_csUnserved);
	return n;
}

void ReportCounts(FILE* f)
{
	AcquireSRWLockShared(&s_lockH);
	const size_t nLive = s_byPtr.size();
	ReleaseSRWLockShared(&s_lockH);
	fprintf(f, "runtime: failed outbound calls %ld, decode problems %ld, calls from non-service threads %ld, bad handles "
	           "%ld, failed dispatches %ld, unknown methods %ld, posts %ld, stream problems %ld\n",
	        s_lFailedCalls, s_lDecodeProblems, s_lWrongThreadCalls, s_lBadHandles, s_lDispatchFails, s_lUnknownMethods,
	        s_lPosts, s_lStreamProblems);
	fprintf(f, "handle table: %zu live DLL objects at the end, %ld at most, %ld kind conflicts\n", nLive, s_lSlotsMax,
	        s_lKindConflicts);
	EnterCriticalSection(&s_csUnserved);
	fprintf(f, "\nmethods without a remote treatment that were called (generated loud defaults; %zu methods):\n",
	        s_unserved.size());
	for(std::map<std::string, UnservedEntry>::const_iterator it = s_unserved.begin(); it != s_unserved.end(); ++it)
		fprintf(f, "unserved\t%s\t%ld\n", it->first.c_str(), it->second.calls);
	LeaveCriticalSection(&s_csUnserved);
}
}  // namespace SrvRemote

// =====================================================================================================================
// SrvRt (srv_rt.h)

namespace SrvRt
{
vpipc::Channel& Chan()
{
	if(t_iChan < 0)
	{
		const long n = InterlockedIncrement(&s_lWrongThreadCalls);
		if(ShouldLog(n))
			SrvLog("remote: LOUD: a DLL -> engine call from thread %lu, which serves no channel: sent on ui",
			       GetCurrentThreadId());
		return s_ep->channel(vpipc::kChannelUi);
	}
	return s_ep->channel((uint32_t)t_iChan);
}

vpipc::Writer& Begin(vpipc::Channel& ch, uint32_t method)
{
	SrvRemote::NoteEngineCall();  // D30 conditions 2 and 4
	Frame f;
	f.method = method;
	f.t0 = Now();
	f.w = NULL;
	t_frames.push_back(f);
	vpipc::Writer& w = ch.begin_call(method);
	t_frames.back().w = &w;
	return w;
}

// The RETURN payload lives in the channel's data area until this side's next send; decoding it may make a nested
// call (an instance type described on first use, ...). Copy it into one of 64 per-thread buffers used in turn
// (2026-11-10, integration). Issue 0119: the rule of the proxy's ProxyLogic::ReturnRing (issue 9041, proxy-design 18.7):
// a slot whose buffer is above 64 KB is released before it is reused (assign keeps a capacity forever, so every slot that
// once held a large RETURN kept it for the life of the thread), and a payload above 64 KB gets a buffer of exactly its
// size (assign into a smaller non-empty buffer grows it 1.5x). A payload stays valid for the next 63 calls of its
// thread. The slots are freed when their thread ends (thread_local array; before, a heap array that was never freed).
// Unit test: server/tests/run_rt_ring_test.py (it compiles this function out of this file).
static void KeepReturn(vpipc::Reader& r)
{
	enum { kSlots = 64, kKeepMax = 64 * 1024 };
	static thread_local unsigned t_next = 0;
	static thread_local std::vector<uint8_t> t_bufs[kSlots];
	std::vector<uint8_t>& b = t_bufs[t_next++ % (unsigned)kSlots];
	const uint32_t n = r.remaining();
	if (b.capacity() > (size_t)kKeepMax || (n > (uint32_t)kKeepMax && b.capacity() < n)) std::vector<uint8_t>().swap(b);
	b.assign(r.cursor(), r.cursor() + n);
	r.reset(b.empty() ? NULL : &b[0], (uint32_t)b.size());
}

bool End(vpipc::Channel& ch, uint32_t method, vpipc::Reader& r)
{
	uint32_t uOut = 0;
	std::vector<int32_t> temps;
	if(!t_frames.empty())
	{
		uOut = t_frames.back().w ? t_frames.back().w->size() : 0;
		temps.swap(t_frames.back().temps);
	}
	const bool bBudget = SrvBudget::On();  // ticket 4652: the DLL waits for the engine from here
	if(bBudget)
		SrvBudget::OutEnter(method);
	const vpipc::Status st = ch.end_call(r);
	if(bBudget)
		SrvBudget::OutLeave();
	// Issue 2700 (live deadlock, c5h-a 2026-11-10 19:37): the engine's handling of this call may have released local
	// lock wrappers (or taken new ones) on the calling EXE thread; the DLL resumes now, so this thread's explicit lock
	// depth follows the want word here too, not only at the next served call (a stale higher depth kept the lock held
	// through CallTestAll's one-level release and the ui thread waited forever).
	SrvRemote::LockSync(ch.index());
	double t0 = 0.0;
	if(!t_frames.empty())
	{
		t0 = t_frames.back().t0;
		t_frames.pop_back();
	}
	if(!temps.empty())
	{
		AcquireSRWLockExclusive(&s_lockH);
		for(size_t i = 0; i < temps.size(); ++i)
		{
			--s_slots[temps[i]].temps;
			ReleaseIfUnusedLocked(temps[i]);
		}
		ReleaseSRWLockExclusive(&s_lockH);
	}
	Count(method, st == vpipc::Status::Ok ? r.remaining() : 0, uOut, Now() - t0);
	if(st == vpipc::Status::Ok)
	{
		KeepReturn(r);
		return true;
	}
	const long n = InterlockedIncrement(&s_lFailedCalls);
	if(st == vpipc::Status::RemoteError)
	{
		vpipc::Reader m = r;
		const char* sz = m.cstr();
		// Issue 9036 (bug 0102, MC decision 2026-11-12): the engine threw inside this call and the proxy caught it; the
		// EXE did not complete the call, so the session ends here with a result of its own (no game goes on after it).
		if(ch.last_remote_status() == vpw::kStatusEngineException)
			SrvRemote::ReportAndExit(6, (std::string("exe_exception: the engine threw a C++ exception in ") +
			                             MethodKey(method) + " (" + (sz ? sz : "") + ")").c_str());
		if(ShouldLog(n))
			SrvLog("remote: call %s (0x%08x) failed in the engine: status %u: %s", MethodKey(method).c_str(), method,
			       ch.last_remote_status(), sz ? sz : "");
		return false;
	}
	SrvLog("remote: call %s (0x%08x) on channel %u: %s", MethodKey(method).c_str(), method, ch.index(),
	       vpipc::status_name(st));
	if(st == vpipc::Status::PeerDied || st == vpipc::Status::PeerClosed || st == vpipc::Status::Disconnected ||
	   st == vpipc::Status::PeerHung)
		SrvRemote::ReportAndExit(3, (std::string("the engine is gone (") + vpipc::status_name(st) + " in " +
		                             MethodKey(method) + ")").c_str());
	return false;
}

void DecodeCheck(uint32_t method, const vpipc::Reader& r)
{
	if(r.ok() && r.remaining() == 0)
		return;
	const long n = InterlockedIncrement(&s_lDecodeProblems);
	if(ShouldLog(n))
		SrvLog("remote: LOUD: RETURN of %s (0x%08x): %s", MethodKey(method).c_str(), method,
		       !r.ok() ? "read past the end" : "bytes left over");
}

vpipc::Handle Register(const void* p, uint32_t kind)
{
	vpipc::Handle h = {0u, 0};
	if(!p)
		return h;
	AcquireSRWLockExclusive(&s_lockH);
	const int32_t id = FindOrCreateLocked(p, kind);
	++s_slots[id].refs;
	h = MakeHandle(id);
	ReleaseSRWLockExclusive(&s_lockH);
	return h;
}

vpipc::Handle RegisterTemp(const void* p, uint32_t kind)
{
	vpipc::Handle h = {0u, 0};
	if(!p)
		return h;
	AcquireSRWLockExclusive(&s_lockH);
	const int32_t id = FindOrCreateLocked(p, kind);
	h = MakeHandle(id);
	if(!t_frames.empty())
	{
		++s_slots[id].temps;
		t_frames.back().temps.push_back(id);
	}
	else if(s_slots[id].refs == 0)
	{
		// Not inside a call (generated code always is): keep it as a persistent registration rather than lose it.
		++s_slots[id].refs;
		SrvLog("remote: LOUD: RegisterTemp outside an outbound call (%s %p): kept", vpk_iface_name(kind), p);
	}
	ReleaseSRWLockExclusive(&s_lockH);
	return h;
}

void* Resolve(vpipc::Handle h, uint32_t kind)
{
	if(h.type == 0 && h.id == 0)
		return NULL;
	void* p = NULL;
	const char* szWhy = NULL;
	uint32_t have = 0;
	AcquireSRWLockShared(&s_lockH);
	if(vpw::IsEngine(h.type))
		szWhy = "an engine handle where a DLL object is expected";
	else if(h.id <= 0 || h.id >= (int32_t)s_slots.size() || !s_slots[h.id].used)
		szWhy = "unknown or released handle";
	else
	{
		have = s_slots[h.id].kind;
		if(!vpk_in_chain(have, kind))
			szWhy = "kind not in the chain of the object's kind";
		else
			p = const_cast<void*>(s_slots[h.id].p);
	}
	ReleaseSRWLockShared(&s_lockH);
	if(szWhy)
	{
		const long n = InterlockedIncrement(&s_lBadHandles);
		if(ShouldLog(n))
			SrvLog("remote: LOUD: Resolve(type 0x%08x id %d as %s): %s (object kind %s)", h.type, h.id,
			       vpk_iface_name(kind) ? vpk_iface_name(kind) : "?", szWhy, have ? vpk_iface_name(have) : "-");
	}
	return p;
}

void Unregister(vpipc::Handle h)
{
	AcquireSRWLockExclusive(&s_lockH);
	if(!vpw::IsEngine(h.type) && h.id > 0 && h.id < (int32_t)s_slots.size() && s_slots[h.id].used)
	{
		if(s_slots[h.id].refs > 0)
			--s_slots[h.id].refs;
		ReleaseIfUnusedLocked(h.id);
	}
	else
	{
		const long n = InterlockedIncrement(&s_lBadHandles);
		if(ShouldLog(n))
			SrvLog("remote: LOUD: Unregister(type 0x%08x id %d): unknown handle", h.type, h.id);
	}
	ReleaseSRWLockExclusive(&s_lockH);
}

FDataStream& StreamIn(vpipc::Reader& args)
{
	const uint32_t id = args.u32();
	const uint32_t off = args.u32();
	FDataStream* s = NULL;
	EnterCriticalSection(&s_csStreams);
	std::map<uint32_t, Session*>::iterator it = s_sessions.find(id);
	if(it != s_sessions.end() && it->second->pull)
		s = it->second->pull;
	else if(it != s_sessions.end() && off <= it->second->bytes.size())
		s = &it->second->stream;
	LeaveCriticalSection(&s_csStreams);
	if(!s)
	{
		static thread_local FMemoryStream t_empty;
		const long n = InterlockedIncrement(&s_lStreamProblems);
		if(ShouldLog(n))
			SrvLog("remote: LOUD: stream session %u offset %u unknown: the DLL reads an empty stream", id, off);
		t_empty.Init(0, NULL, false);
		s = &t_empty;
	}
	else
		s->SetPosition(off);
	StreamUse u;
	u.s = s;
	u.start = s->GetPosition();
	t_streamIn.push_back(u);
	return *s;
}

void StreamInRet(vpipc::Writer& ret, FDataStream& s)
{
	for(size_t i = t_streamIn.size(); i-- > 0;)
		if(t_streamIn[i].s == &s)
		{
			ret.u32(s.GetPosition() - t_streamIn[i].start);
			t_streamIn.erase(t_streamIn.begin() + i);
			return;
		}
	InterlockedIncrement(&s_lStreamProblems);
	SrvLog("remote: LOUD: StreamInRet for a stream StreamIn did not give out");
	ret.u32(0);
}

FDataStream& StreamOut()
{
	FMemoryStream* s = new FMemoryStream();
	t_streamOut.push_back(s);
	return *s;
}

void StreamOutRet(vpipc::Writer& ret, FDataStream& s)
{
	for(size_t i = t_streamOut.size(); i-- > 0;)
		if(t_streamOut[i] == &s)
		{
			FMemoryStream* m = t_streamOut[i];
			t_streamOut.erase(t_streamOut.begin() + i);
			const unsigned int n = m->GetEOF();
			std::string buf(n, '\0');
			m->Rewind();
			if(n)
				m->CopyToMem(&buf[0]);
			ret.bytes(buf.data(), n);
			delete m;
			return;
		}
	InterlockedIncrement(&s_lStreamProblems);
	SrvLog("remote: LOUD: StreamOutRet for a stream StreamOut did not give out");
	ret.bytes(NULL, 0);
}

bool DispatchFail(vpipc::Channel& ch, vpipc::Writer& ret, uint32_t status, const char* message)
{
	const long n = InterlockedIncrement(&s_lDispatchFails);
	if(ShouldLog(n))
		SrvLog("remote: dispatch failed (status %u): %s", status, message ? message : "");
	ch.set_return_status(status);
	ret.str(message ? message : "");
	return true;
}

void EnterUpdate()
{
	if(++t_iUpdateDepth == 1 && SrvBudget::On())
		SrvBudget::CountUpdate();  // ticket 4652: Update calls per turn (turn-times.tsv)
}

void LeaveUpdate()
{
	if(--t_iUpdateDepth > 0)
		return;
	t_iUpdateDepth = 0;
	// The game turn after each Update (proxy-design 11.9): a local DLL call (the wrapper's lock is server-local).
	ICvGameContext1* pContext = SrvRemote::Context();
	if(!pContext)
		return;
	ICvGame1* pGame = pContext->GetGame();
	if(!pGame)
		return;
	const int iTurn = pGame->GetGameTurn();
	if(SrvBudget::On())
		SrvBudget::AtTurn(iTurn);  // ticket 4652: a changed turn is a boundary of the budget and of turn-times.tsv
	SrvRemoteFeed::AtLeaveUpdate(pContext, pGame, iTurn);  // issue 3303 (returns at once unless render=1 or feed=1)
	pGame->Destroy();
	EnterCriticalSection(&s_csCount);
	if(iTurn != s_iLastTurn)
	{
		FlushTurnRowsLocked(s_iLastTurn);
		s_iLastTurn = iTurn;
	}
	LeaveCriticalSection(&s_csCount);
}

void Unserved(uint32_t method, const char* key)
{
	const std::string k = key ? key : MethodKey(method);
	EnterCriticalSection(&s_csUnserved);
	std::map<std::string, UnservedEntry>::iterator it = s_unserved.find(k);
	const bool bFirst = it == s_unserved.end();
	if(bFirst)
	{
		UnservedEntry e;
		e.method = method;
		e.calls = 1;
		s_unserved[k] = e;
	}
	else
		++it->second.calls;
	LeaveCriticalSection(&s_csUnserved);
	if(!bFirst)
		return;
	const char* szRule = SrvRemote::UnexpectedAllowed(k);
	SrvLog("remote: UNSERVED (first call): %s (0x%08x) on %s thread%s%s", k.c_str(), method,
	       SrvRemote::InUpdate() ? "the Update" : "a service", szRule ? "; allow-listed by " : "", szRule ? szRule : "");
	if(SrvRemote::FailOnUnexpected() && !szRule)
		SrvRemote::ReportAndExit(4, ("fail_on_unexpected: " + k).c_str());
}
}  // namespace SrvRt
