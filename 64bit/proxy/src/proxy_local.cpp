// proxy_local.cpp - local answers of the x86 proxy (issue 2700). Hand-written. Overview: proxy_local.h; design and
// invalidation: research/03-dll-engine-boundary/proxy-design.md section 16.
#include "proxy_local.h"
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#include "proxy_rt_logic.h"
#include "proxy_purity.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
// Issue 3503: the engine -> DLL purity list is data (purity.tsv beside the x64 DLL, loaded by ProxyPurity::Load at the
// boot; proxy_purity.h). What stays compiled is about the proxy's own runtime messages, not the DLL: only the two of
// issue 2700 (kRt_Canon, kRt_LockSync; decision D30) are treated as pure; every other kRt_* bumps the epoch.
const uint32_t kPureRt[] = {vpw::kRt_Canon, vpw::kRt_LockSync};

struct CsLock
{
	CRITICAL_SECTION& cs;
	explicit CsLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
	~CsLock() { LeaveCriticalSection(&cs); }

private:
	CsLock& operator=(const CsLock&);
};

bool g_on = false, g_verify = false, g_testStale = false, g_testFrozen = false, g_ready = false;
CRITICAL_SECTION g_cs;  // canonical table, local wrappers, mirror

// ---- purity -------------------------------------------------------------------------------------------------------
uint8_t* g_pure[kVpKindCount + 1] = {};
uint32_t g_pureN[kVpKindCount + 1] = {};
uint32_t g_updateMethods[8] = {};  // ICvGame1::Update() ids (local_test=stale treats them as pure)
uint32_t g_nUpdate = 0;

// ---- epoch --------------------------------------------------------------------------------------------------------
volatile LONG g_epoch = 0;
volatile LONG g_inflight = 0;

// ---- canonical handles --------------------------------------------------------------------------------------------
volatile LONG g_canonGen = 1;
// Issue 2801 (verification 2700 A5): Init / Uninit / CacheGameDatabaseData / FlushGameDatabaseData in flight on any
// thread (raised in BeforeCall, lowered in AfterCall, both under g_cs). While non-zero no canonical handle is fetched,
// stored or answered: in process the DLL makes a fresh wrapper per call, so a pointer taken mid-rebuild lived for one
// call; a canonical handle would live for the whole generation.
volatile LONG g_genInflight = 0;
std::map<std::pair<uint32_t, int32_t>, vpipc::Handle> g_canon;  // (code, index) -> handle, this generation
std::set<std::pair<uint32_t, int32_t> > g_canonHandles;         // (type, id) of every canonical handle ever made
struct LocalWrapper
{
	bool lockKind;
	DWORD tid;
};
std::map<const void*, LocalWrapper> g_local;  // every local wrapper ever made (they are pooled, never freed)

// Per EXE thread (2026-11-10, after the first live series: 575 local answers per idle frame made the proxy's own
// bookkeeping the cost): a free list of this thread's released local wrappers per handle (reused, so a local
// GetGame/Destroy pair costs no allocation and no table insert or erase), the thread's own local wrappers (released
// without the shared lock), and a cache of canonical handles checked against the generation.
struct ThreadLocal
{
	std::map<std::pair<uint32_t, int32_t>, std::vector<void*> > freeByHandle;
	std::map<const void*, bool> mine;  // local wrapper -> lock kind
	std::map<std::pair<uint32_t, int32_t>, std::pair<vpipc::Handle, LONG> > canon;
};
__declspec(thread) ThreadLocal* t_tl = NULL;
ThreadLocal& TL()
{
	if (!t_tl) t_tl = new ThreadLocal();
	return *t_tl;
}

// ---- lock want (per EXE thread; lock kinds are local only on the ui and core threads) -----------------------------
// Shared memory Local\VPProxyLock_<pid>, 16 LONGs: 0-2 want (ui, core, aux unused), 3 flags, 4-6 the server's DLL ->
// engine calls per channel, 8 (issue 2801) lock-count errors the proxy found (a local lock wrapper released on another
// thread, a count past kMaxLocalLocks): the server's next served call marks its run FAILED.
volatile LONG* g_want = NULL;
HANDLE g_wantMap = NULL;
const int kWantErrors = 8;
// Issue 2801: the server ignores a want word above 1000 (srv_remote_objects.cpp LockSync); a count that reaches it is a
// leak of local lock wrappers and is reported, never silently frozen.
const LONG kMaxLocalLocks = 1000;
__declspec(thread) LONG t_C = 0;  // live local lock wrappers of this thread
// (t_E, the explicit depth recorded at each crossing, removed in issue 2801: nothing read it since e7ea828, so a
// crossing refused by CallsBlocked before OnCrossing could not leave a stale value that mattered.)
__declspec(thread) LONG t_chan = -1;
__declspec(thread) bool t_forcePure = false;

// ---- mirror -------------------------------------------------------------------------------------------------------
struct MKey
{
	uint32_t method;
	uint32_t type;
	int32_t id, arg;
	bool operator<(const MKey& o) const
	{
		if (method != o.method) return method < o.method;
		if (id != o.id) return id < o.id;
		if (arg != o.arg) return arg < o.arg;
		return type < o.type;
	}
};
struct MVal
{
	int32_t value;
	LONG epoch;
};
std::map<MKey, MVal> g_mirror;

// ---- the UI-Lua mirror (issue 2805) -------------------------------------------------------------------------------
int g_luaMode = 0;            // 0 off, 1 shadow (lua_mirror=0), 2 mirror (lua_mirror=1)
bool g_luaStale = false;      // local_test=luastale
// Shadow mode: the bumps of the pure-listed Lua calls (which still bump g_epoch / g_inflight there) are subtracted to
// give the Lua epoch, so that the would-hit count says what the mirror would answer. Zero in mirror mode.
volatile LONG g_adjEpoch = 0, g_adjInflight = 0;
__declspec(thread) bool t_luaShadowNext = false;   // the next CallStart of this thread is the marked Lua call
__declspec(thread) bool t_luaShadowActive = false; // ... and it was counted impure (LuaShadowEnd closes it)
struct LVal
{
	std::string value;
	LONG epoch;
};
std::map<std::string, LVal> g_luaMirror;  // under g_cs
const size_t kLuaMirrorMax = 200000;      // entries; the table is emptied when it reaches this (old epochs are dead)
volatile LONG g_nLuaHits = 0, g_nLuaWould = 0, g_nLuaMisses = 0, g_nLuaStores = 0, g_nLuaTainted = 0;
volatile LONG g_nLuaVerified = 0, g_nLuaMismatch = 0, g_nLuaFlushes = 0;

LONG LuaEpoch() { return g_epoch - g_adjEpoch; }
LONG LuaInflight() { return g_inflight - g_adjInflight; }

// ---- counters -----------------------------------------------------------------------------------------------------
volatile LONG g_nCanonFetch = 0, g_nCanonFail = 0, g_nLocalWrappers = 0, g_nLockSync = 0, g_nInvalidations = 0;
volatile LONG g_nReused = 0, g_nForeignRelease = 0;
volatile LONG g_nGenRefused = 0, g_nLockErrors = 0;  // issue 2801
volatile LONG g_nHits = 0, g_nMisses = 0, g_nStores = 0, g_nVerified = 0, g_nMismatch = 0, g_nEngineTainted = 0;

const uint32_t kCanonByMethodCount = 8;

bool Cross(uint32_t method, vpipc::Reader& r, uint32_t gen, uint32_t code, int32_t index, bool withArgs)
{
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, method);
	if (withArgs)
	{
		w.u32(gen);
		w.u32(code);
		w.i32(index);
	}
	return ProxyRt::End(ch, method, r);
}

// A generation change is in flight: the getter crosses (counted).
bool GenRefused()
{
	InterlockedIncrement(&g_nGenRefused);
	return false;
}

// Fetches (or finds) the canonical handle of (code, index). false: unavailable (the call crosses).
// Issue 2801: never while a generation change (g_genInflight) runs on any thread, for the per-thread cache, the shared
// table and the fetch alike; a fetched handle is stored only if no generation change started or ended meanwhile (the
// generation is bumped at both ends, so `g_canonGen == gen` under g_cs is enough, g_genInflight is checked as well).
// The unlocked fast path can race with a BeforeCall that starts on another thread just after the check: the handle
// returned then was valid when the change began, as an in-process wrapper made just before it would be.
bool Canon(uint32_t code, int32_t index, vpipc::Handle& h)
{
	if (g_genInflight != 0) return GenRefused();
	const std::pair<uint32_t, int32_t> key(code, index);
	ThreadLocal& tl = TL();
	{
		std::map<std::pair<uint32_t, int32_t>, std::pair<vpipc::Handle, LONG> >::const_iterator it = tl.canon.find(key);
		if (it != tl.canon.end() && it->second.second == g_canonGen && g_genInflight == 0)
		{
			h = it->second.first;
			return true;
		}
	}
	LONG gen;
	{
		CsLock l(g_cs);
		if (g_genInflight != 0) return GenRefused();
		std::map<std::pair<uint32_t, int32_t>, vpipc::Handle>::const_iterator it = g_canon.find(key);
		if (it != g_canon.end())
		{
			h = it->second;
			tl.canon[key] = std::make_pair(h, g_canonGen);
			return true;
		}
		gen = g_canonGen;
	}
	vpipc::Reader r;
	if (!Cross(vpw::kRt_Canon, r, (uint32_t)gen, code, index, true))
	{
		if (InterlockedIncrement(&g_nCanonFail) <= 5)
			ProxyRtInt::Log("local: kRt_Canon(%u, %d) failed: the call crosses", code, index);
		return false;
	}
	h = r.handle();
	ProxyRt::DecodeCheck(vpw::kRt_Canon, r);
	if (!r.ok()) return false;
	// Issue 2801: {0, -1} = the server refused (a generation change is being served on another server thread).
	if (h.type == 0 && h.id == -1) return GenRefused();
	InterlockedIncrement(&g_nCanonFetch);
	CsLock l(g_cs);
	if (g_canonGen == gen && g_genInflight == 0)
	{
		g_canon[key] = h;
		if (h.type || h.id) g_canonHandles.insert(std::make_pair(h.type, h.id));
	}
	return true;
}

// Issue 2801: a lock-count disagreement the proxy cannot repair (a lock wrapper released on another thread, a count
// past kMaxLocalLocks): counted, logged, published in want word 8 (the server's next served call fails its run) and
// fatal (exit 7) under strict=1.
void LockError(const char* fmt, ...)
{
	const LONG n = InterlockedIncrement(&g_nLockErrors);
	if (g_want) InterlockedIncrement(const_cast<LONG*>(&g_want[kWantErrors]));
	if (n <= 20)
	{
		char b[512];
		va_list ap;
		va_start(ap, fmt);
		_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
		va_end(ap);
		ProxyRtInt::LogErrPublic("local: LOCK ERROR: %s", b);
	}
	ProxyRtInt::StrictViolation(7, "a local lock wrapper count disagrees with the server's lock (issue 2801)");
}

void SetWant(LONG v)
{
	if (g_want && t_chan >= 0 && t_chan < 2) g_want[t_chan] = v;
}

void LockSync()
{
	InterlockedIncrement(&g_nLockSync);
	vpipc::Reader r;
	Cross(vpw::kRt_LockSync, r, 0, 0, 0, false);
}

// A lock wrapper is created on this thread: the 0 -> 1 transition takes the server lock now (as the DLL's
// constructor would, waiting if another thread holds it); deeper levels are carried by the want word.
void LockEnter()
{
	++t_C;
	if (t_C == kMaxLocalLocks + 1)
		LockError("thread %lu holds %ld local lock wrappers (a leak: the server follows the want word only up to %ld)",
		          GetCurrentThreadId(), t_C, kMaxLocalLocks);
	SetWant(t_C);
	if (t_C == 1) LockSync();  // 0 -> 1: take the lock now (2026-11-10 19:50: not "t_E == 0", which can be stale after
	                           // the server synced the depth at the end of a nested engine call)
}

void LockExit()
{
	if (t_C > 0) --t_C;
	SetWant(t_C);
	if (t_C == 0) LockSync();  // the lock is free exactly when the in-process DLL would free it
}

bool LockThreadOk()
{
	uint32_t chan = 0;
	if (!ProxyRtInt::ThreadChannel(chan) || chan > 1) return false;
	t_chan = (LONG)chan;
	return g_want != NULL;
}

uint32_t CodeOfMethod(uint32_t method)
{
	if (method == kM_ICvGameContext3_GetGame) return vpw::kCanonGame;
	if (method == kM_ICvGameContext3_GetPreGame) return vpw::kCanonPreGame;
	if (method == kM_ICvGameContext3_GetMap) return vpw::kCanonMap;
	if (method == kM_ICvGameContext3_GetGameAsynch) return vpw::kCanonGameAsynch;
	if (method == kM_ICvGameContext3_GetTeam) return vpw::kCanonTeam;
	if (method == kM_ICvGame2_GetPlayer) return vpw::kCanonPlayer;
	if (method == kM_ICvGameContext3_GetTerrainInfo) return vpw::kCanonTerrainInfo;
	if (method == kM_ICvGameContext3_GetFeatureInfo) return vpw::kCanonFeatureInfo;
	return 0;
}
}  // namespace

namespace ProxyLocal
{
void* const kCross = reinterpret_cast<void*>(static_cast<uintptr_t>(1));

void Configure(bool on, bool verify, int test)
{
	const bool testStale = test == 1;
	g_testFrozen = on && test == 2;
	g_on = on;
	g_verify = on && verify;
	g_testStale = on && testStale;
}

void InitTables()
{
	InitializeCriticalSection(&g_cs);
	// Issue 3503/3504: the boot (proxy_rt.cpp) has called a ProxyPurity load before this, in every mode=on boot (pack=1:
	// the pack's file; pack=0: VP_PROXY_PURITY or <x64_dir>\purity.tsv). 3503's interim load here is gone (3504).
	if (!ProxyPurity::LoadAttempted())
		ProxyRtInt::Log("local: purity: no load before the tables (a boot path without the 3504 call); both purity lists "
		                "empty, every call crosses");
	std::set<std::string> matched;
	uint32_t n = 0;
	for (uint32_t k = 1; k <= kVpKindCount; ++k)
	{
		const uint32_t m = vpk_method_count(k);
		if (!m) continue;
		g_pure[k] = static_cast<uint8_t*>(calloc(m, 1));
		if (!g_pure[k]) continue;
		g_pureN[k] = m;
		for (uint32_t i = 0; i < m; ++i)
		{
			const char* key = vpk_method_key((k << 16) | i);
			if (!key) continue;
			if (ProxyPurity::IsPureMethodKey(key))
			{
				g_pure[k][i] = 1;
				matched.insert(key);
				++n;
			}
			if (strcmp(key, "ICvGame1::Update()") == 0 && g_nUpdate < 8) g_updateMethods[g_nUpdate++] = (k << 16) | i;
		}
	}
	// Issue 2805: the UI-Lua mirror's keys come from the same vpproxy.ini (proxy_update.cpp ProxyIniSettings).
	{
		const ProxyLogic::Settings& st = ProxyIniSettings();
		g_luaMode = g_on ? (st.luaMirror ? 2 : 1) : 0;
		g_luaStale = g_on && st.localTest == "luastale";
		ProxyRtInt::Log("local: UI-Lua mirror %s%s", g_luaMode == 2 ? "ON (lua_mirror=1)" : (g_luaMode == 1 ? "shadow only "
		                "(lua_mirror=0: every Lua call crosses; would-hits counted)" : "off (local answers off)"),
		                g_luaStale ? "; NEGATIVE TEST local_test=luastale: Lua entries are answered whatever their epoch" : "");
	}
	g_ready = true;
	// A listed key that names no method of this build's model is never pure (counted: the file is for another model).
	ProxyRtInt::Log("local: local_answers=%d local_verify=%d local_test=%s; %u pure method ids (%u of %u listed keys "
	                "in the model; %u Lua names)%s", g_on ? 1 : 0,
	                g_verify ? 1 : 0, g_testStale ? "stale" : (g_testFrozen ? "frozen" : "\"\""), n,
	                (unsigned)matched.size(), (unsigned)ProxyPurity::MethodCount(), (unsigned)ProxyPurity::LuaCount(),
	                g_testStale ? "; NEGATIVE TEST: ICvGame1::Update does not invalidate the mirror"
	                            : (g_testFrozen ? "; NEGATIVE TEST: no call invalidates the mirror" : ""));
}

bool InitShared()
{
	if (!g_on) return false;
	char name[64];
	_snprintf_s(name, sizeof(name), _TRUNCATE, "%s%lu", vpw::kLockShmPrefix, GetCurrentProcessId());
	g_wantMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, name);
	if (!g_wantMap)
	{
		ProxyRtInt::Log("local: cannot create %s (error %lu): GetGame/GetPlayer keep crossing", name, GetLastError());
		return false;
	}
	g_want = static_cast<volatile LONG*>(MapViewOfFile(g_wantMap, FILE_MAP_ALL_ACCESS, 0, 0, 64));
	if (!g_want)
	{
		ProxyRtInt::Log("local: cannot map %s (error %lu): GetGame/GetPlayer keep crossing", name, GetLastError());
		CloseHandle(g_wantMap);
		g_wantMap = NULL;
		return false;
	}
	for (int k = 0; k < 16; ++k) g_want[k] = 0;
	g_want[3] = 1 | (g_verify ? 2 : 0);  // the server's local answers (GetScriptSystem) follow these bits
	ProxyRtInt::Log("local: lock-want memory %s", name);
	return true;
}

bool On() { return g_on && g_ready; }
bool Verify() { return g_verify; }

bool IsPure(uint32_t method)
{
	if (g_testFrozen) return true;  // the live negative test: nothing invalidates
	const uint32_t t = method >> 16, i = method & 0xFFFFu;
	if (t >= 1 && t <= kVpKindCount)
	{
		if (g_testStale)
			for (uint32_t k = 0; k < g_nUpdate; ++k)
				if (g_updateMethods[k] == method) return true;
		return g_pure[t] && i < g_pureN[t] && g_pure[t][i] != 0;
	}
	for (size_t k = 0; k < sizeof(kPureRt) / sizeof(kPureRt[0]); ++k)
		if (kPureRt[k] == method) return true;
	return false;
}

bool TakeForcePure()
{
	const bool b = t_forcePure;
	t_forcePure = false;
	return b;
}

void CallStart(uint32_t method, bool forcePure, bool& impure)
{
	if (forcePure)
	{
		impure = false;
		return;
	}
	const bool luaShadow = t_luaShadowNext;
	t_luaShadowNext = false;
	impure = g_ready && !IsPure(method);
	if (!impure) return;
	InterlockedIncrement(&g_inflight);
	InterlockedIncrement(&g_epoch);
	if (luaShadow)
	{
		// Issue 2805, shadow mode: after the real bump, so the Lua in-flight count is transiently above zero (refused).
		InterlockedIncrement(&g_adjInflight);
		InterlockedIncrement(&g_adjEpoch);
		t_luaShadowActive = true;
	}
}

void CallEnd(bool impure)
{
	if (!impure) return;
	InterlockedIncrement(&g_epoch);
	InterlockedDecrement(&g_inflight);
}

bool BeforeCall(uint32_t method)
{
	if (method != kM_ICvGameContext3_Init && method != kM_ICvGameContext3_Uninit &&
	    method != kM_ICvDLLDatabaseUtility1_CacheGameDatabaseData &&
	    method != kM_ICvDLLDatabaseUtility1_FlushGameDatabaseData)
		return false;
	if (!g_ready) return false;
	CsLock l(g_cs);
	InterlockedIncrement(&g_genInflight);  // issue 2801: before the generation, so a reader of the new one sees it
	InterlockedIncrement(&g_canonGen);
	g_canon.clear();
	g_mirror.clear();
	g_luaMirror.clear();  // issue 2805
	const LONG n = InterlockedIncrement(&g_nInvalidations);
	if (n <= 20)
		ProxyRtInt::Log("local: canonical handles and the mirror dropped before %s (generation %ld)",
		                vpk_method_key(method), g_canonGen);
	return true;
}

void AfterCall(bool genChange)
{
	if (!genChange) return;
	CsLock l(g_cs);
	InterlockedIncrement(&g_canonGen);  // a handle fetched under the previous generation is never stored or found again
	g_canon.clear();
	g_mirror.clear();
	g_luaMirror.clear();  // issue 2805
	InterlockedDecrement(&g_genInflight);
	const LONG n = InterlockedIncrement(&g_nInvalidations);
	if (n <= 20)
		ProxyRtInt::Log("local: canonical handles and the mirror dropped after the generation change (generation %ld, "
		                "%ld still in flight)", g_canonGen, g_genInflight);
}

void* Answer(uint32_t method, uint32_t code, int32_t index)
{
	if (!On()) return kCross;
	const bool lockKind = code == vpw::kCanonGame || code == vpw::kCanonPlayer;
	// A thread's first call decides its channel by its role (issue 2502: the core thread's first call is GetGame):
	// an undecided thread always crosses, so the binding rule sees the same first call as before.
	uint32_t chan = 0;
	if (!ProxyRtInt::ThreadChannel(chan)) return kCross;
	if (lockKind && !LockThreadOk()) return kCross;
	vpipc::Handle h;
	if (!Canon(code, index, h)) return kCross;
	ProxyRtInt::NoteLocal(method);
	if (h.type == 0 && h.id == 0) return NULL;  // the DLL answers NULL for this index (made no wrapper, took no lock)
	if (lockKind) LockEnter();
	ThreadLocal& tl = TL();
	std::vector<void*>& fl = tl.freeByHandle[std::make_pair(h.type, h.id)];
	if (!fl.empty())
	{
		void* q = fl.back();
		fl.pop_back();
		tl.mine[q] = lockKind;
		InterlockedIncrement(&g_nReused);
		return q;
	}
	void* p = ProxyRt::Wrap(h);
	if (!p)
	{
		if (lockKind) LockExit();
		return NULL;
	}
	LocalWrapper lw = {lockKind, GetCurrentThreadId()};
	{
		CsLock l(g_cs);
		g_local[p] = lw;
	}
	tl.mine[p] = lockKind;
	InterlockedIncrement(&g_nLocalWrappers);
	return p;
}

void AddPermanent(vpipc::Handle h)
{
	if (!g_ready) return;
	CsLock l(g_cs);
	g_canonHandles.insert(std::make_pair(h.type, h.id));
}

bool IsCanonHandle(vpipc::Handle h)
{
	if (!g_ready) return false;
	CsLock l(g_cs);
	return g_canonHandles.count(std::make_pair(h.type, h.id)) != 0;
}

int Release(const void* pInterface, uint32_t destroyMethod)
{
	if (!g_ready) return 0;
	ThreadLocal& tl = TL();
	std::map<const void*, bool>::iterator it = tl.mine.find(pInterface);
	if (it != tl.mine.end())
	{
		const bool lockKind = it->second;
		tl.mine.erase(it);
		ProxyRtInt::NoteLocal(destroyMethod);
		if (lockKind) LockExit();
		// Back to this thread's free list (it stays registered with its handle; the next local answer reuses it).
		const vpipc::Handle h = ProxyRt::HandleOf(pInterface);
		tl.freeByHandle[std::make_pair(h.type, h.id)].push_back(const_cast<void*>(pInterface));
		return 2;
	}
	LocalWrapper lw;
	{
		CsLock l(g_cs);
		std::map<const void*, LocalWrapper>::iterator g = g_local.find(pInterface);
		if (g == g_local.end()) return 0;
		lw = g->second;
	}
	// A local wrapper released on another thread than the one that made it: the lock it stands for belongs to the
	// making thread's count, which this thread cannot change. The wrapper is left alone (not reused).
	// Issue 2801: for a lock wrapper this cannot be repaired: the making thread's server thread keeps the lock until
	// that thread crosses again (only it can cross on its channel), and another thread's first-level acquisition waits
	// for it (a deadlock if it never does). In process the DLL's destructor would release the EXE's lock on a thread
	// that does not own it, so the EXE is not expected to do this; if it does, the run fails loudly (LockError).
	ProxyRtInt::NoteLocal(destroyMethod);
	const LONG n = InterlockedIncrement(&g_nForeignRelease);
	if (lw.lockKind)
		LockError("local lock wrapper %p made on thread %lu released on thread %lu: its lock stays held by the making "
		          "thread's channel", pInterface, lw.tid, GetCurrentThreadId());
	else if (n <= 20)
		ProxyRtInt::LogErrPublic("local: LOUD: local wrapper %p made on thread %lu released on thread %lu", pInterface,
		                         lw.tid, GetCurrentThreadId());
	return 2;
}

bool MirrorGet(uint32_t method, vpipc::Handle h, int32_t arg, int32_t& value)
{
	if (!On()) return false;
	const LONG e1 = g_epoch;
	MemoryBarrier();
	const LONG i = g_inflight;
	MemoryBarrier();
	const LONG e2 = g_epoch;
	if (i != 0 || e1 != e2) return false;
	MKey k = {method, h.type, h.id, arg};
	{
		CsLock l(g_cs);
		std::map<MKey, MVal>::const_iterator it = g_mirror.find(k);
		if (it == g_mirror.end() || it->second.epoch != e1) return false;
		value = it->second.value;
	}
	InterlockedIncrement(&g_nHits);
	ProxyRtInt::NoteLocal(method);
	return true;
}

// The engine-call word of the calling thread's channel (the server counts every DLL -> engine call its thread for
// that channel makes, crossing or answered in the server; D30 condition 2). -1 when unknown.
static LONG EngineCalls()
{
	uint32_t chan = 0;
	if (!g_want || !ProxyRtInt::ThreadChannel(chan) || chan > 2) return -1;
	return g_want[4 + chan];
}

Token MirrorBegin(vpipc::Handle h)
{
	Token t;
	t.epoch = g_epoch;
	MemoryBarrier();
	t.engine = EngineCalls();
	t.ok = On() && g_inflight == 0 && t.engine >= 0 && IsCanonHandle(h);
	if (t.ok) InterlockedIncrement(&g_nMisses);
	return t;
}

void MirrorPut(const Token& t, uint32_t method, vpipc::Handle h, int32_t arg, int32_t value)
{
	if (!t.ok) return;
	MemoryBarrier();
	// D30 condition 3: bracketed. Kept only if the epoch and the in-flight count are what they were before the read,
	// and (condition 2) the server made no call to the engine while it served the read.
	if (g_epoch != t.epoch || g_inflight != 0) return;
	if (EngineCalls() != t.engine)
	{
		InterlockedIncrement(&g_nEngineTainted);
		return;
	}
	MKey k = {method, h.type, h.id, arg};
	MVal v = {value, t.epoch};
	CsLock l(g_cs);
	if (g_epoch != t.epoch) return;
	g_mirror[k] = v;
	InterlockedIncrement(&g_nStores);
}

bool StillValid(const Token& t)
{
	if (!t.ok) return false;
	const LONG e1 = g_epoch;
	MemoryBarrier();
	const LONG i = g_inflight;
	return i == 0 && e1 == t.epoch && EngineCalls() == t.engine;
}

void ForcePureNext() { t_forcePure = true; }

void VerifyResult(uint32_t method, int32_t local, int32_t server)
{
	InterlockedIncrement(&g_nVerified);
	if (local == server) return;
	const LONG n = InterlockedIncrement(&g_nMismatch);
	if (n <= 20)
		ProxyRtInt::LogErrPublic("local: VERIFY MISMATCH %s: mirror %d, server %d%s", vpk_method_key(method), local, server,
		                         n == 20 ? " (further mismatches counted only)" : "");
	ProxyRtInt::StrictViolation(6, "local_verify found a mirror answer that differs from the server's");
}

bool GameHandle(vpipc::Handle& h)
{
	if (!On()) return false;
	return Canon(vpw::kCanonGame, 0, h) && (h.type || h.id);
}

// ---- the UI-Lua mirror (issue 2805) -------------------------------------------------------------------------------
int LuaMode() { return g_ready ? g_luaMode : 0; }
bool LuaStaleTest() { return g_luaStale; }

Token LuaBegin()
{
	Token t;
	t.epoch = LuaEpoch();
	MemoryBarrier();
	t.engine = EngineCalls();
	t.ok = LuaMode() != 0 && LuaInflight() == 0 && t.engine >= 0 && g_genInflight == 0;
	return t;
}

bool LuaGet(const std::string& key, std::string& value)
{
	if (LuaMode() == 0) return false;
	const LONG e1 = LuaEpoch();
	MemoryBarrier();
	const LONG i = LuaInflight();
	MemoryBarrier();
	const LONG e2 = LuaEpoch();
	if (i != 0 || e1 != e2 || g_genInflight != 0) return false;
	{
		CsLock l(g_cs);
		std::map<std::string, LVal>::const_iterator it = g_luaMirror.find(key);
		if (it == g_luaMirror.end() || (it->second.epoch != e1 && !g_luaStale))
		{
			InterlockedIncrement(&g_nLuaMisses);
			return false;
		}
		value = it->second.value;
	}
	if (g_luaMode == 2)
	{
		InterlockedIncrement(&g_nLuaHits);
		ProxyRtInt::NoteLocal(vpw::kRt_LuaCall);
	}
	else
		InterlockedIncrement(&g_nLuaWould);
	return true;
}

void LuaPut(const Token& t, const std::string& key, const std::string& value)
{
	if (!t.ok) return;
	MemoryBarrier();
	if (LuaEpoch() != t.epoch || LuaInflight() != 0) return;
	if (EngineCalls() != t.engine)
	{
		InterlockedIncrement(&g_nLuaTainted);  // D30 condition 2: the server called the engine while it served the read
		return;
	}
	CsLock l(g_cs);
	if (LuaEpoch() != t.epoch || g_genInflight != 0) return;
	if (g_luaMirror.size() >= kLuaMirrorMax)
	{
		g_luaMirror.clear();
		InterlockedIncrement(&g_nLuaFlushes);
	}
	LVal& v = g_luaMirror[key];
	v.value = value;
	v.epoch = t.epoch;
	InterlockedIncrement(&g_nLuaStores);
}

void LuaShadowMarkNext() { t_luaShadowNext = true; }

void LuaShadowEnd()
{
	t_luaShadowNext = false;
	if (!t_luaShadowActive) return;
	t_luaShadowActive = false;
	InterlockedIncrement(&g_adjEpoch);
	InterlockedDecrement(&g_adjInflight);
}

void LuaVerifyResult(const char* function, bool equal)
{
	InterlockedIncrement(&g_nLuaVerified);
	if (equal) return;
	const LONG n = InterlockedIncrement(&g_nLuaMismatch);
	if (n <= 20)
		ProxyRtInt::LogErrPublic("local: VERIFY MISMATCH Lua %s: the UI-Lua mirror's answer differs from the server's%s",
		                         function ? function : "?", n == 20 ? " (further mismatches counted only)" : "");
	ProxyRtInt::StrictViolation(6, "local_verify found a UI-Lua mirror answer that differs from the server's");
}

void Report()
{
	if (!g_ready) return;
	ProxyRtInt::Log("local: canonical handles fetched %ld (failed %ld, invalidations %ld, crossed while a generation "
	                "change was in flight %ld), local wrappers made %ld (reused %ld, released on another thread %ld), "
	                "lock syncs %ld, LOCK ERRORS %ld; mirror hits %ld, misses %ld, stores %ld, not kept (the server called "
	                "the engine) %ld, entries %u; verified %ld, MISMATCHES %ld",
	                g_nCanonFetch, g_nCanonFail, g_nInvalidations, g_nGenRefused, g_nLocalWrappers, g_nReused,
	                g_nForeignRelease, g_nLockSync, g_nLockErrors, g_nHits, g_nMisses,
	                g_nStores, g_nEngineTainted, (unsigned)g_mirror.size(), g_nVerified, g_nMismatch);
	if (g_luaMode)
	{
		ProxyRtInt::Log("local: UI-Lua mirror (%s): hits %ld, would-hits (shadow) %ld, misses %ld, stores %ld, not kept "
		                "(the server called the engine) %ld, entries %u, table emptied %ld; verified %ld, LUA MISMATCHES %ld",
		                g_luaMode == 2 ? "on" : "shadow", g_nLuaHits, g_nLuaWould, g_nLuaMisses, g_nLuaStores, g_nLuaTainted,
		                (unsigned)g_luaMirror.size(), g_nLuaFlushes, g_nLuaVerified, g_nLuaMismatch);
		ProxyLuaMirrorReport();
	}
}

// Issue 0119: the ending thread's ThreadLocal (its maps; before, it was never freed, like the RETURN ring before 9041).
// No behaviour changes: no other thread can reach it, and the pooled wrappers in its free list stay registered in
// g_local as before (wrappers are never freed). A wrapper the thread still held is released, if ever, on another thread,
// which takes the foreign-release path with or without this.
void FreeThread()
{
	delete t_tl;
	t_tl = NULL;
}
}  // namespace ProxyLocal
