// farm_guard_proxy_test.cpp - unit test of proxy/src/proxy_farm_guard.cpp with shell/composite/farm_guard.cpp (ticket 0535).
// SPDX-License-Identifier: GPL-3.0-or-later
// A console program, never loaded into the game. Built by proxy/tests/test_farm_guard_proxy.py in the proxy's toolchain
// (clang-cl -m32, VC9 headers) and under MSVC /fsanitize=address, with -DPROXY_FARM_GUARD_NO_SHELL (no proxy_shell.cpp). It
// builds the same fake "B" event manager, listener list, builder and layout graph as shell/tests/farm_guard_test.cpp
// (research/02-exe-internals/farm-builder-guard.md), installs the proxy's guard on it without any composite, and walks the
// list as the EXE's listener walk does, through the node's word (the shared page's hook stub).
//   farm_guard_proxy_test.exe <scratch folder>
// Prints "farm_guard_proxy_test: OK <n> checks" and exits 0, or names every failed check and exits 1 (2: no argument).
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "farm_guard.h"
#include "proxy_farm_guard.h"
#include "../../exebuild/include/vp_exe_build.h"

static int g_checks, g_fails;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static CRITICAL_SECTION g_logLock;
static std::string g_logText;
namespace ProxyRtInt
{
void Log(const char* fmt, ...)
{
	char b[2400];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap); va_end(ap);
	EnterCriticalSection(&g_logLock);
	g_logText += b; g_logText += "\n";
	LeaveCriticalSection(&g_logLock);
}
}  // namespace ProxyRtInt
static bool LogHas(const char* s)
{
	EnterCriticalSection(&g_logLock);
	const bool r = g_logText.find(s) != std::string::npos;
	LeaveCriticalSection(&g_logLock);
	return r;
}
static size_t LogCount(const char* s)
{
	EnterCriticalSection(&g_logLock);
	size_t n = 0;
	for (size_t p = g_logText.find(s); p != std::string::npos; p = g_logText.find(s, p + 1)) ++n;
	LeaveCriticalSection(&g_logLock);
	return n;
}
static void LogClear() { EnterCriticalSection(&g_logLock); g_logText.clear(); LeaveCriticalSection(&g_logLock); }

// ---- the fake EXE objects (as farm_guard_test.cpp) ---------------------------------------------------------------------
#define NIL 0x0FFFFFFFu
struct Node { uint32_t next, prev, pad, obj, fn; };
struct List { uint32_t freeHead, live, data, size, cap, count, head, tail; };
static __declspec(align(16)) unsigned char g_mgr[0x200 + 0x600 * 2];
static unsigned char g_lockInit = 1;
static volatile LONG g_lock;
static List g_list;
static Node g_nodes[8];
static uint32_t g_viewFlag = 1;
static unsigned char g_objStore[16];
static unsigned char g_graph[0xD0];
struct Ep { uint32_t h[4]; uint32_t count; };
static Ep g_eps[64];
static void __fastcall FakeTrampoline(void*, void*, void*) {}
static volatile LONG g_builderCalls;
static void* g_lastObj; static void* g_lastEv;
static int g_doubleFreeOnCall;

static uint32_t* EpHdr() { return (uint32_t*)(g_graph + 0x94); }
static void FakeFree(uint32_t i)
{
	uint32_t* h = EpHdr();
	Ep* e = (Ep*)(uintptr_t)h[2];
	e[i].count |= 0x80000000u;
	if (i < h[0]) { e[i].count = (e[i].count & 0x80000000u) | (h[0] & 0x7FFFFFFFu); h[0] = i; }
	else { e[i].count = (e[i].count & 0x80000000u) | (e[h[0]].count & 0x7FFFFFFFu); e[h[0]].count = (e[h[0]].count & 0x80000000u) | i; }
	h[1]--;
}
static void BuildGraph()
{
	memset(g_graph, 0, sizeof(g_graph)); memset(g_eps, 0, sizeof(g_eps));
	uint32_t* h = EpHdr();
	h[0] = NIL; h[1] = 20; h[2] = (uint32_t)(uintptr_t)g_eps; h[3] = 20; h[4] = 64;
	for (uint32_t i = 0; i < 20; ++i) g_eps[i].count = i % 5;
	FakeFree(3); FakeFree(9); FakeFree(1);
}
static void __fastcall FakeBuilder(void* obj, void* edx, void* ev)
{
	(void)edx;
	const LONG n = InterlockedIncrement(&g_builderCalls); g_lastObj = obj; g_lastEv = ev;
	if (g_doubleFreeOnCall && n == g_doubleFreeOnCall) { FakeFree(7); FakeFree(7); }
}
static uint32_t Register(uint32_t obj, uint32_t fn)
{
	const uint32_t i = g_list.size++;
	g_nodes[i].obj = obj; g_nodes[i].fn = fn; g_nodes[i].next = NIL; g_nodes[i].prev = g_list.tail;
	if (g_list.tail != NIL) g_nodes[g_list.tail].next = i;
	if (g_list.head == NIL) g_list.head = i;
	g_list.tail = i; g_list.count++; g_list.live++;
	return i;
}
// The EXE's walk: through each node's word (the stub, in the guarded node), with the object in ecx and one argument. The
// stack is measured around each call: a callee that does not pop its argument (ret 4) would leave esp 4 bytes off.
static int g_espBad;
static void CallNode(uint32_t fn, uint32_t obj, void* ev)
{
	int delta = 0;
	__asm
	{
		mov esi, esp
		push ev
		mov ecx, obj
		xor edx, edx
		call fn
		mov eax, esp
		sub eax, esi
		mov delta, eax
		mov esp, esi
	}
	if (delta != 0) ++g_espBad;
}
static void Walk(void* ev)
{
	uint32_t idx = g_list.head;
	for (uint32_t k = 0; k < g_list.count; ++k) { Node* n = &g_nodes[idx]; CallNode(n->fn, n->obj, ev); idx = n->next; }
}
static uint32_t* Slot(uint32_t channel) { return (uint32_t*)(g_mgr + 0x1E0 + channel * 0x600); }

static FgWorld g_w;
static void ResetWorld()
{
	memset(g_mgr, 0, sizeof(g_mgr));
	memset(&g_list, 0, sizeof(g_list)); memset(g_nodes, 0, sizeof(g_nodes));
	g_list.data = (uint32_t)(uintptr_t)g_nodes; g_list.cap = 8; g_list.head = g_list.tail = NIL; g_list.freeHead = NIL;
	Slot(0)[0] = (uint32_t)(uintptr_t)FakeTrampoline; Slot(0)[1] = (uint32_t)(uintptr_t)&g_list;
	g_lockInit = 1; g_lock = 0; g_viewFlag = 1;
	memset(&g_w, 0, sizeof(g_w));
	g_w.lockInit = (uintptr_t)&g_lockInit; g_w.lock = (uintptr_t)&g_lock; g_w.mgr = (uintptr_t)g_mgr;
	g_w.slotOff = 0x1E0; g_w.stride = 0x600; g_w.trampoline = (uintptr_t)FakeTrampoline;
	g_w.builder = (uintptr_t)FakeBuilder; g_w.builderObj = (uintptr_t)g_objStore; g_w.viewFlag = (uintptr_t)&g_viewFlag;
	g_w.graph = (uintptr_t)g_graph;
	BuildGraph();
	g_builderCalls = 0; g_doubleFreeOnCall = 0; g_lastObj = g_lastEv = NULL;
}
static uint32_t OBJ() { return (uint32_t)(uintptr_t)g_objStore; }
static uint32_t BUILDER() { return (uint32_t)(uintptr_t)FakeBuilder; }
static FgStats St() { FgStats s; FarmGuard_GetStats(&s); return s; }

// ---- ticket 0655: the EXE's poster, plot test and art id as stand-ins (as farm_guard_test.cpp section 15) -------------------
static unsigned char g_rec[0x80];
static int g_posterCalls;
static void* g_posterQueue;
static uint32_t g_queueIndex;
static const char g_fakeArtName[] = "ART_DEF_IMPROVEMENT_FARM";
static void* __fastcall FakePoster(void* q, void*)
{
	++g_posterCalls; g_posterQueue = q;
	memset(g_rec, 0, sizeof(g_rec));
	return g_rec + 0x10;
}
static uint32_t __fastcall FakePlotOnMap(void* grid, void*, const int32_t* cube)
{
	return grid == (void*)0x5150 && cube[0] + cube[1] + cube[2] == 0 ? 0xABCDEF01u : 0xABCDEF00u;   // al only
}
static int32_t __fastcall FakeArtId(void* db, void*, const char* name)
{
	return db == (void*)0x5250 && name == g_fakeArtName ? 0x27 : -1;
}

// Waits up to ms for the watcher to make cond true (it ticks every 2 ms).
#define WAIT_FOR(cond, ms) do { for (int _i = 0; _i < (ms) && !(cond); _i += 5) Sleep(5); } while (0)

static void WriteIni(const std::wstring& p, const char* body)
{
	FILE* f = NULL;
	if (_wfopen_s(&f, p.c_str(), L"wb") == 0 && f) { fputs(body, f); fclose(f); }
}

int main(int argc, char** argv)   // (not wmain: VC9's libcmt and lld-link pull in both C runtime starts)
{
	if (argc < 2) { printf("usage: farm_guard_proxy_test <scratch folder>\n"); return 2; }
	InitializeCriticalSection(&g_logLock);
	wchar_t wscratch[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wscratch, MAX_PATH);
	const std::wstring scratch = wscratch;
	CreateDirectoryW(scratch.c_str(), NULL);
	const std::wstring ini = scratch + L"\\shell_composite.ini";
	const std::wstring noIni = scratch + L"\\missing.ini";
	SetEnvironmentVariableW(L"VPSHELL_FARM_GUARD", NULL);
	SetEnvironmentVariableW(L"VPSHELL_FARM_BUILDER", NULL);
	SetEnvironmentVariableW(L"VPSHELL_FARM_END_POST", NULL);   // ticket 0655
	SetEnvironmentVariableW(FG_SHARED_ENV, NULL);
	// ---- the owner's EXE check (epic 0590, 2026-11-20): every known build passes, by PE stamp AND SizeOfImage; a stamp of
	// no known build, a known stamp with another SizeOfImage and a non-image do not. Until then only build 4390913 passed,
	// which left the builder unguarded on build 26577625.
	{
		static unsigned char img[0x400];
		for (int b = 0; b < kVpExeBuildCount; ++b)
		{
			const VpExeBuildInfo* bi = VpExeBuildInfoOf(b);
			memset(img, 0, sizeof(img));
			img[0] = 'M'; img[1] = 'Z'; *(uint32_t*)(img + 0x3C) = 0x80; *(uint32_t*)(img + 0x80) = 0x4550u;
			*(uint32_t*)(img + 0x80 + 8) = bi->timeDateStamp; *(uint32_t*)(img + 0x80 + 24 + 56) = bi->sizeOfImage;
			CHECK(ProxyFarmGuard::Test::ValidateImage(img) == 1);
			*(uint32_t*)(img + 0x80 + 24 + 56) = bi->sizeOfImage + 0x1000;   // the stamp alone is not enough
			CHECK(ProxyFarmGuard::Test::ValidateImage(img) == -1);
			*(uint32_t*)(img + 0x80 + 24 + 56) = bi->sizeOfImage; *(uint32_t*)(img + 0x80 + 8) = bi->timeDateStamp + 1;
			CHECK(ProxyFarmGuard::Test::ValidateImage(img) == -1);
		}
		memset(img, 0, sizeof(img));
		CHECK(ProxyFarmGuard::Test::ValidateImage(img) == -1);   // no MZ
		CHECK(ProxyFarmGuard::Test::ValidateImage(NULL) == -1);
	}
	unsigned char pay[0x40] = {};

	// ---- 1. the proxy without a composite installs the guard ---------------------------------------------------------
	uintptr_t page = 0;
	{
		ResetWorld();
		const uint32_t bi = Register(OBJ(), BUILDER());
		ProxyFarmGuard::Test::InstallWorld(&g_w, scratch.c_str(), noIni.c_str());
		CHECK(FarmGuard_Role() == 1);
		char why[200];
		page = FarmGuard_FindShared(why, sizeof(why));
		CHECK(page != 0 && why[0] == 0);                                     // VP64_FARM_GUARD names the page, and it checks
		const FgShared* sh = (const FgShared*)page;
		CHECK(FarmGuard_HookWord() == sh->hookStub && FarmGuard_InertWord() == sh->inertStub && sh->hookStub == page + 0x1000);
		MEMORY_BASIC_INFORMATION m;
		CHECK(VirtualQuery((void*)sh->hookStub, &m, sizeof(m)) == sizeof(m) && m.Protect == PAGE_EXECUTE_READ);   // not RWX
		CHECK(VirtualQuery((void*)page, &m, sizeof(m)) == sizeof(m) && m.Protect == PAGE_READWRITE);
		WAIT_FOR(g_nodes[bi].fn == sh->hookStub, 2000);
		CHECK(g_nodes[bi].fn == sh->hookStub);                               // the watcher swapped the node (no Present needed)
		CHECK(sh->hookTarget != 0 && sh->presentFn != 0 && sh->attaches == 1 && sh->detaches == 0);
		CHECK(ProxyFarmGuard::Test::Ticks() > 0 && St().swaps == 1 && g_lock == 0);
		CHECK(LogHas("farm_guard: owner proxy (ticket 0535): shared page") && LogHas(" created (attach 1 in this process"));
		CHECK(LogHas("farm_guard: installed by the proxy: the builder's listener node 0"));
		CHECK(LogHas("farm_guard: proxy keys: farm_guard=1 (default), farm_builder=1 (default), composite not loaded"));
		Walk(pay);                                                          // through the stub into the proxy's hook
		CHECK(g_builderCalls == 1 && g_lastObj == (void*)g_objStore && g_lastEv == pay && g_espBad == 0);
		CHECK(St().builderCalls == 1 && St().checks == 1 && !St().tripped);
		CHECK(LogHas("farm_guard: before a burst (owner proxy, no composite: the in-game check at the burst's first builder call 1): sound"));
		CHECK(LogHas("farm_guard: graph (before builder call 1, the first of a burst)"));
		Walk(pay);                                                          // the same burst: no second pre-check
		CHECK(g_builderCalls == 2 && LogCount("farm_guard: before a burst") == 1);
		// the burst line from the watcher thread, 2 s after the last call (no graph line off the main thread)
		WAIT_FOR(LogHas("farm_guard: load 1 (grid seq 0, 2 s without a call; no composite: no graph line here): builder calls 2"), 4000);
		CHECK(LogHas("farm_guard: load 1 (grid seq 0, 2 s without a call; no composite: no graph line here): builder calls 2 (skipped 0), checks 2: all sound"));
		CHECK(!LogHas("graph (after load 1"));
		CHECK(!LogHas("LOUD"));
	}

	// ---- 2. the kill switch: VPSHELL_FARM_GUARD=0 puts the builder back, 1 swaps again; farm_builder=0 only with a composite --
	{
		const FgShared* sh = (const FgShared*)page;
		SetEnvironmentVariableW(L"VPSHELL_FARM_GUARD", L"0");
		WAIT_FOR(g_nodes[0].fn == BUILDER(), 3000);
		CHECK(g_nodes[0].fn == BUILDER() && St().restored == 1 && LogHas("farm_guard=0 (env)"));
		const LONG before = g_builderCalls;
		Walk(pay);
		CHECK(g_builderCalls == before + 1 && St().builderCalls == 2);      // the builder's own word: not through the guard
		SetEnvironmentVariableW(L"VPSHELL_FARM_GUARD", NULL);              // the default again: 1
		WAIT_FOR(g_nodes[0].fn == sh->hookStub, 3000);
		CHECK(g_nodes[0].fn == sh->hookStub && St().swaps == 2);
		// farm_builder=0 without the composite: the builder is still called (as before 0535: no composite = no skip)
		SetEnvironmentVariableW(L"VPSHELL_FARM_BUILDER", L"0");
		WAIT_FOR(LogHas("farm_builder=0 (env), composite not loaded"), 3000);
		int fg = -1, fb = -1, skip = -1;
		ProxyFarmGuard::Test::Keys(&fg, &fb, &skip);
		CHECK(fg == 1 && fb == 0 && skip == 0);
		LONG b0 = g_builderCalls;
		Walk(pay);
		CHECK(g_builderCalls == b0 + 1 && St().skipServer == 0);
		ProxyFarmGuard::SetComposite(true);                                 // with the composite (ServerFarmFields = 1): skipped
		ProxyFarmGuard::Test::Keys(&fg, &fb, &skip);
		CHECK(skip == 1);
		WAIT_FOR(LogHas("farm_guard: farm_builder=0: the EXE's farm builder is not called"), 2000);
		b0 = g_builderCalls;
		Walk(pay);
		CHECK(g_builderCalls == b0 && St().skipServer == 1);
		ProxyFarmGuard::SetComposite(false);
		SetEnvironmentVariableW(L"VPSHELL_FARM_BUILDER", NULL);
		WAIT_FOR(LogHas("farm_guard: farm_builder=1: the EXE's farm builder is called again"), 3000);
		CHECK(LogHas("farm_guard: farm_builder=1: the EXE's farm builder is called again"));
	}

	// ---- 3. the unload: the stub returns at once; a reload (the next proxy boot) re-arms it ---------------------------------
	{
		const FgShared* sh = (const FgShared*)page;
		ProxyFarmGuard::Test::Unload();
		CHECK(sh->hookTarget == 0 && sh->presentFn == 0 && sh->detaches == 1);
		CHECK(g_nodes[0].fn == sh->hookStub);                               // the node keeps the stub
		const LONG b0 = g_builderCalls;
		const unsigned long t0 = ProxyFarmGuard::Test::Ticks();
		for (int i = 0; i < 100; ++i) Walk(pay);                            // a type-59 dispatch after the unload
		CHECK(g_builderCalls == b0 && g_espBad == 0);                       // returned at once, its argument popped
		Sleep(50);
		CHECK(ProxyFarmGuard::Test::Ticks() == t0);                         // the watcher is gone
		LogClear();
		ProxyFarmGuard::Test::InstallWorld(&g_w, scratch.c_str(), noIni.c_str());   // a new boot of the proxy in this process
		CHECK(FarmGuard_Role() == 1 && FarmGuard_HookWord() == sh->hookStub);
		CHECK(LogHas("found by VP64_FARM_GUARD (an earlier image of this process made it) (attach 2 in this process"));
		WAIT_FOR(sh->hookTarget != 0, 2000);
		CHECK(sh->hookTarget != 0 && sh->attaches == 2);
		Sleep(30);
		CHECK(St().swaps == 0 && g_nodes[0].fn == sh->hookStub);            // nothing to swap: the node already holds the stub
		Walk(pay);
		CHECK(g_builderCalls == b0 + 1 && g_espBad == 0 && St().builderCalls == 1);   // re-armed
		// a second registration after the reload (the next load): swapped to the same stub
		const uint32_t b2 = Register(OBJ(), BUILDER());
		WAIT_FOR(g_nodes[b2].fn == sh->inertStub || g_nodes[b2].fn == sh->hookStub, 2000);
		CHECK(g_nodes[b2].fn == sh->hookStub && g_nodes[0].fn == sh->inertStub && St().retired == 1);   // the old node retired
		const LONG b1 = g_builderCalls;
		Walk(pay);
		CHECK(g_builderCalls == b1 + 1 && g_espBad == 0);                   // one builder call per event (the inert node returns)
	}

	// ---- 4. a trip outlives the proxy's image: the next boot keeps the builder off --------------------------------------------
	{
		const FgShared* sh = (const FgShared*)page;
		ProxyFarmGuard::Test::Unload();
		ResetWorld();
		const uint32_t bi = Register(OBJ(), BUILDER());
		ProxyFarmGuard::Test::InstallWorld(&g_w, scratch.c_str(), noIni.c_str());
		WAIT_FOR(g_nodes[bi].fn == sh->hookStub && sh->hookTarget != 0, 2000);
		g_doubleFreeOnCall = 1;
		Walk(pay);
		CHECK(g_builderCalls == 1 && St().tripped && sh->tripped == 1 && LogHas("farm_guard: LOUD: the EXE's farm-field layout graph failed"));
		ProxyFarmGuard::Test::Unload();
		ResetWorld();
		BuildGraph();
		const uint32_t bj = Register(OBJ(), BUILDER());
		LogClear();
		ProxyFarmGuard::Test::InstallWorld(&g_w, scratch.c_str(), noIni.c_str());
		CHECK(LogHas("the guard stopped the builder in an earlier boot of this process: it stays off"));
		WAIT_FOR(g_nodes[bj].fn == sh->hookStub && sh->hookTarget != 0, 2000);
		Walk(pay);
		CHECK(g_builderCalls == 0 && St().skipTripped == 1);
		InterlockedExchange((volatile LONG*)&sh->tripped, 0);                // (test only: forget the trip for the next case)
	}

	// ---- 5. farm_guard=0 in the ini from the start: the proxy installs none ---------------------------------------------------
	{
		ProxyFarmGuard::Test::Unload();
		ResetWorld();
		const uint32_t bi = Register(OBJ(), BUILDER());
		WriteIni(ini, "[composite]\r\nfarm_guard=0\r\nfarm_builder=1\r\n");
		LogClear();
		ProxyFarmGuard::Test::InstallWorld(&g_w, scratch.c_str(), ini.c_str());
		Sleep(200);
		CHECK(g_nodes[bi].fn == BUILDER() && St().swaps == 0 && LogHas("farm_guard: proxy keys: farm_guard=0 (ini)"));
		Walk(pay);
		CHECK(g_builderCalls == 1 && St().calls == 0);                      // the EXE's own word: the guard never called
		WriteIni(ini, "[composite]\r\nfarm_guard=1\r\n");                   // re-read within a second
		WAIT_FOR(g_nodes[bi].fn != BUILDER(), 3000);
		CHECK(g_nodes[bi].fn == FarmGuard_HookWord() && LogHas("farm_guard: proxy keys: farm_guard=1 (ini)"));
		ProxyFarmGuard::Test::Unload();
	}
	SetEnvironmentVariableW(L"VPSHELL_FARM_GUARD", NULL);

	// ---- 6. ticket 0655: the farm-end post through the proxy's stub; the switch farm_end_post by environment and by ini ------
	{
		ResetWorld();
		g_w.poster = (uintptr_t)FakePoster; g_w.queueIndex = (uintptr_t)&g_queueIndex; g_w.queueBase = 0x10000000u;
		g_w.plotOnMap = (uintptr_t)FakePlotOnMap; g_w.plotGrid = 0x5150; g_w.artId = (uintptr_t)FakeArtId; g_w.artDb = 0x5250;
		g_w.artName = (uintptr_t)g_fakeArtName;
		const uint32_t bi = Register(OBJ(), BUILDER());
		WriteIni(ini, "[composite]\r\nfarm_guard=1\r\n");
		SetEnvironmentVariableW(L"VPSHELL_FARM_BUILDER", L"0");
		LogClear();
		ProxyFarmGuard::Test::InstallWorld(&g_w, scratch.c_str(), ini.c_str());
		ProxyFarmGuard::SetComposite(true);
		WAIT_FOR(g_nodes[bi].fn == FarmGuard_HookWord() && LogHas("farm_guard: farm_builder=0: the EXE's farm builder is not called"), 3000);
		CHECK(ProxyFarmGuard::Test::FarmEndPost() == 1 && St().postReady == 1);
		CHECK(LogHas("composite loaded: the builder is never called (the server lays out the farm fields); farm_end_post=1 (default): "
		             "the farm-end post makes the farm buildings (ticket 0655)"));
		unsigned char fp[0x40] = {};
		*(uint32_t*)(fp + 0x08) = 0x27u; *(int16_t*)(fp + 0x18) = 11; *(int16_t*)(fp + 0x1A) = 4; fp[0x1C] = 1;
		const int e0 = g_espBad;
		Walk(fp);
		CHECK(g_builderCalls == 0 && St().skipServer == 1 && St().posts == 1 && g_posterCalls == 1 && g_espBad == e0);
		CHECK(*(int16_t*)(g_rec + 0x10) == 11 && *(int16_t*)(g_rec + 0x12) == 4 && g_posterQueue == (void*)0x10000000u);
		fp[0x1C] = 0; Walk(fp); fp[0x1C] = 1;                               // the builder's flag test: no post
		CHECK(St().posts == 1 && St().postFailFlag == 1 && g_posterCalls == 1 && g_espBad == e0);
		// the switch by environment (a beta report bisected on the shipped layout): no post, counted
		SetEnvironmentVariableW(L"VPSHELL_FARM_END_POST", L"0");
		WAIT_FOR(LogHas("farm_guard: farm_end_post=0: no farm-end post"), 3000);
		CHECK(ProxyFarmGuard::Test::FarmEndPost() == 0 && LogHas("farm_end_post=0 (env): no farm-end post, no farm building"));
		Walk(fp);
		CHECK(St().posts == 1 && St().postSwitchedOff == 1 && g_posterCalls == 1);
		SetEnvironmentVariableW(L"VPSHELL_FARM_END_POST", NULL);
		WAIT_FOR(LogHas("farm_guard: farm_end_post=1: the farm-end post is made again"), 3000);
		Walk(fp);
		CHECK(St().posts == 2 && g_posterCalls == 2);
		// the switch by the shipped shell_composite.ini's [composite] key
		WriteIni(ini, "[composite]\r\nfarm_guard=1\r\nfarm_end_post=0\r\n");
		WAIT_FOR(ProxyFarmGuard::Test::FarmEndPost() == 0 && LogCount("farm_guard: farm_end_post=0: no farm-end post") == 2, 3000);
		CHECK(LogHas("farm_end_post=0 (ini)"));
		Walk(fp);
		CHECK(St().posts == 2 && St().postSwitchedOff == 2 && g_posterCalls == 2 && g_espBad == e0);
		ProxyFarmGuard::SetComposite(false);
		SetEnvironmentVariableW(L"VPSHELL_FARM_BUILDER", NULL);
		ProxyFarmGuard::Test::Unload();
		printf("farm_guard_proxy_test: the farm-end post (ticket 0655): done\n");
	}

	if (g_fails)
	{
		printf("farm_guard_proxy_test: FAILED %d of %d checks\n--- log ---\n%s", g_fails, g_checks, g_logText.c_str());
		return 1;
	}
	printf("farm_guard_proxy_test: OK %d checks\n", g_checks);
	return 0;
}
