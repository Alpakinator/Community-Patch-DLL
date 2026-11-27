// test_budget.cpp - host test of the turn budget's accounting (ticket 4652): the switch's default, the call classes,
// server.exe's SrvBudgetCore (rows partition the wall time exactly; priority; save; nesting; boundaries; a threaded
// stress with the real clock) and the proxy's ProxyBudgetCore (buckets from a fake turn block, exe_alone, late and lost
// boundaries). No DLL, no game. Built and run by run_budget_test.py (x64 clang-cl, and with --asan MSVC
// /fsanitize=address for x64 and x86). Exit 0 when every check passes.
#include "srv_budget_core.h"
#include "proxy_budget_core.h"
#include <stdio.h>
#include <stdlib.h>

namespace
{
int g_pass = 0, g_fail = 0;
#define CHECK(cond)                                                                    \
	do                                                                                 \
	{                                                                                  \
		if (cond)                                                                      \
			++g_pass;                                                                  \
		else                                                                           \
		{                                                                              \
			++g_fail;                                                                  \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
		}                                                                              \
	} while (0)
#define CHECK_EQ(a, b)                                                                                    \
	do                                                                                                    \
	{                                                                                                     \
		const long long va_ = (long long)(a), vb_ = (long long)(b);                                      \
		if (va_ == vb_)                                                                                   \
			++g_pass;                                                                                     \
		else                                                                                              \
		{                                                                                                 \
			++g_fail;                                                                                     \
			printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_);      \
		}                                                                                                 \
	} while (0)

volatile int64_t g_t = 0;  // the fake clock
int64_t FakeClock() { return g_t; }
int64_t RealClock()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

using namespace vpbudget;
typedef SrvBudgetCore::Core SCore;
typedef SrvBudgetCore::Bucket SBucket;
const int UI = 0, CORE = 1, AUX = 2;

SCore* NewCore(SrvBudgetCore::Core::ClockFn clock)
{
	SCore* c = static_cast<SCore*>(_aligned_malloc(sizeof(SCore), 64));
	c->Init(clock);
	return c;
}

SBucket Close(SCore* c, int slot, int64_t* pWall)
{
	SBucket b;
	bool synced = false;
	c->Boundary(slot, b, pWall, &synced);
	CHECK(synced);
	return b;
}

// ---------------------------------------------------------------------------------------------------------- switch
void TestSwitch()
{
	CHECK(!SwitchValueOn("", 0));
	CHECK(!SwitchValueOn("0", 1));
	CHECK(SwitchValueOn("1", 1));
	CHECK(!SwitchValueOn("11", 2));
	CHECK(!SwitchValueOn("true", 4));
	CHECK(!SwitchValueOn("1", 0));
	CHECK(!SwitchValueOn(NULL, 1));
	// the real reader: absent (the default), then values
	SetEnvironmentVariableA(kEnvName, NULL);
	CHECK(!SwitchFromEnvironment());
	SetEnvironmentVariableA(kEnvName, "0");
	CHECK(!SwitchFromEnvironment());
	SetEnvironmentVariableA(kEnvName, "");
	CHECK(!SwitchFromEnvironment());
	SetEnvironmentVariableA(kEnvName, " 1");
	CHECK(!SwitchFromEnvironment());
	SetEnvironmentVariableA(kEnvName, "1");
	CHECK(SwitchFromEnvironment());
	SetEnvironmentVariableA(kEnvName, "123456789012345678901234567890");  // longer than the reader's buffer
	CHECK(!SwitchFromEnvironment());
	SetEnvironmentVariableA(kEnvName, NULL);
	CHECK(!SwitchFromEnvironment());
}

// --------------------------------------------------------------------------------------------------------- classes
void TestClasses()
{
	CHECK_EQ(ClassOfMethod("ICvGame1", "ICvGame1::Update()", false), kInUpdate);
	CHECK_EQ(ClassOfMethod("ICvGame2", "ICvGame1::Update()", false), kInUpdate);
	CHECK_EQ(ClassOfMethod("ICvGame2", "ICvGame1::UpdateFOW(bool)", false), kInOther);
	CHECK_EQ(ClassOfMethod("ICvPlot1", "ICvPlot1::GetX()", false), kInPlot);
	CHECK_EQ(ClassOfMethod("ICvScriptSystemUtility1", "ICvScriptSystemUtility1::PushCvUnitInstance(lua_State *, ICvUnit1 *)", false), kInLua);
	CHECK_EQ(ClassOfMethod("ICvPlayer3", "ICvPlayer1::GetID()", false), kInOther);
	CHECK_EQ(ClassOfMethod("ICvEngineUtility4", "ICvEngineUtility1::AutoSave(bool, bool)", true), kOutSave);
	CHECK_EQ(ClassOfMethod("ICvEngineUtility1", "ICvEngineUtility1::SaveGame(SaveGameTypes)", true), kOutSave);
	CHECK_EQ(ClassOfMethod("ICvEngineUtility2", "ICvEngineUtility1::QuickSave()", true), kOutSave);
	CHECK_EQ(ClassOfMethod("ICvEngineUtility4", "ICvEngineUtility1::GetGameCoreLock()", true), kOutEngine);
	CHECK_EQ(ClassOfMethod("ICvEngineScriptSystem1", "ICvEngineScriptSystem1::CallTestAll(...)", true), kOutScript);
	CHECK_EQ(ClassOfMethod("ICvEngineScriptSystemArgs1", "ICvEngineScriptSystemArgs1::Push(int)", true), kOutScript);
	CHECK_EQ(ClassOfMethod("ICvUserInterface2", "ICvUserInterface1::AddMessage(...)", true), kOutUi);
	CHECK_EQ(ClassOfMethod("CvDLLInterfaceIFaceBase", "CvDLLInterfaceIFaceBase::UpdateEndTurn()", true), kOutUi);
	CHECK_EQ(ClassOfMethod("FIFileSystem", "FIFileSystem::Create(...)", true), kOutFile);
	CHECK_EQ(ClassOfMethod("FILogFileMgr", "FILogFileMgr::GetLog(...)", true), kOutFile);
	CHECK_EQ(ClassOfMethod(NULL, NULL, true), kOutOther);
	CHECK_EQ(ClassOfMethod(NULL, NULL, false), kInOther);
	CHECK_EQ(ClassOfRuntime(vpw::kRt_LuaCall), kInLua);
	CHECK_EQ(ClassOfRuntime(vpw::kRt_LuaIndex), kInLua);
	CHECK_EQ(ClassOfRuntime(vpw::kRt_LockSync), kInLock);
	CHECK_EQ(ClassOfRuntime(vpw::kRt_CoreLockYield), kInLock);
	CHECK_EQ(ClassOfRuntime(vpw::kRt_Canon), kInOther);
	CHECK_EQ(ClassOfRuntime(vpw::kRt_Boot), kInOther);
	CHECK_EQ(ClassOfRuntime(vpw::kRtE_LuaCall), kOutScript);
	CHECK_EQ(ClassOfRuntime(vpw::kRtE_DbOps), kOutDb);
	CHECK_EQ(ClassOfRuntime(vpw::kRtE_StreamRead), kOutStream);
	CHECK_EQ(ClassOfRuntime(vpw::kRtH_IniGetKeyValue), kOutOther);
	CHECK_EQ(ClassOfRuntime(0x00190048u), kNone);
	for (int c = 0; c < kClassCount; ++c)
	{
		CHECK(RowName(c)[0] != '?');
		CHECK(ClassName(c)[0] != '?');
	}
	CHECK(RowName(kClassCount)[0] == '?');
}

// ---------------------------------------------------------------------------------------------- server: one thread
void TestServerSequence()
{
	g_t = 1000;
	SCore* c = NewCore(&FakeClock);
	g_t = 1010; c->ServedEnter(CORE, kInUpdate);
	g_t = 1030; c->OutEnter(CORE, kOutScript);
	g_t = 1035; c->ServedEnter(CORE, kInLua);
	g_t = 1040; c->ServedLeave(CORE);
	g_t = 1050; c->OutLeave(CORE);
	g_t = 1070; c->ServedLeave(CORE);
	g_t = 1100;
	int64_t wall = 0;
	const SBucket b = Close(c, CORE, &wall);
	CHECK_EQ(wall, 100);
	CHECK_EQ(b.RowSum(), 100);
	CHECK_EQ(b.rows[0], 40);
	CHECK_EQ(b.rows[kInUpdate], 40);
	CHECK_EQ(b.rows[kOutScript], 15);
	CHECK_EQ(b.rows[kInLua], 5);
	CHECK_EQ(b.calls[kInUpdate], 1);
	CHECK_EQ(b.ticks[kInUpdate], 60);
	CHECK_EQ(b.calls[kOutScript], 1);
	CHECK_EQ(b.ticks[kOutScript], 20);
	CHECK_EQ(b.calls[kInLua], 1);
	CHECK_EQ(b.ticks[kInLua], 5);
	CHECK_EQ(b.msgs[0], 2);           // Update's CALL and RETURN cross while nothing is busy
	CHECK_EQ(b.msgs[kOutScript], 4);  // the script call's CALL and RETURN, the nested Lua call's CALL and RETURN
	CHECK_EQ(b.msgs[kInUpdate], 0);
	CHECK_EQ(b.hidden, 0);
	CHECK_EQ(b.overlap, 0);
	CHECK_EQ(b.anomalies, 0);
	// the next turn is empty: all idle
	g_t = 1150;
	const SBucket b2 = Close(c, CORE, &wall);
	CHECK_EQ(wall, 50);
	CHECK_EQ(b2.rows[0], 50);
	CHECK_EQ(b2.RowSum(), 50);
	_aligned_free(c);
}

// ---------------------------------------------------------------------------------------------- server: priority
void TestServerPriority()
{
	g_t = 0;
	SCore* c = NewCore(&FakeClock);
	g_t = 10; c->ServedEnter(CORE, kInUpdate);
	g_t = 20; c->ServedEnter(UI, kInLua);     // hidden behind the core thread
	g_t = 25; c->ServedEnter(AUX, kInOther);  // hidden behind both
	g_t = 28; c->ServedLeave(AUX);
	g_t = 50; c->ServedLeave(CORE);           // ui's Lua is now the top
	g_t = 60; c->ServedLeave(UI);
	g_t = 70; c->ServedEnter(AUX, kInPlot);   // aux alone
	g_t = 80; c->ServedLeave(AUX);
	g_t = 100;
	int64_t wall = 0;
	const SBucket b = Close(c, UI, &wall);
	CHECK_EQ(wall, 100);
	CHECK_EQ(b.RowSum(), 100);
	CHECK_EQ(b.rows[kInUpdate], 40);
	CHECK_EQ(b.rows[kInLua], 10);
	CHECK_EQ(b.rows[kInPlot], 10);
	CHECK_EQ(b.rows[kInOther], 0);
	CHECK_EQ(b.rows[0], 40);
	CHECK_EQ(b.overlap, 30);              // 20-50
	CHECK_EQ(b.hidden, 4);                // ui's CALL; aux's CALL and RETURN; Update's RETURN (ui's Lua is the top then)
	CHECK_EQ(b.msgs[0], 4);               // Update's CALL, ui's RETURN at 60, aux's plot CALL and RETURN
	CHECK_EQ(b.msgs[kInLua], 0);
	CHECK_EQ(b.calls[kInOther], 1);
	CHECK_EQ(b.ticks[kInOther], 3);
	_aligned_free(c);
}

// --------------------------------------------------------------------------------------------------- server: save
void TestServerSave()
{
	g_t = 0;
	SCore* c = NewCore(&FakeClock);
	g_t = 10; c->ServedEnter(CORE, kInUpdate);
	g_t = 20; c->OutEnter(CORE, kOutSave);    // AutoSave
	g_t = 25; c->ServedEnter(CORE, kInOther); // the engine asks the DLL to write
	g_t = 27; c->OutEnter(CORE, kOutFile);    // ... which calls the engine again
	g_t = 28; c->OutLeave(CORE);
	g_t = 30; c->ServedLeave(CORE);
	g_t = 40; c->OutLeave(CORE);
	g_t = 45; c->OutEnter(CORE, kOutEngine);  // after the save: normal again
	g_t = 47; c->OutLeave(CORE);
	g_t = 50; c->ServedLeave(CORE);
	g_t = 60;
	int64_t wall = 0;
	const SBucket b = Close(c, CORE, &wall);
	CHECK_EQ(b.RowSum(), 60);
	CHECK_EQ(b.rows[kOutSave], 20);
	CHECK_EQ(b.rows[kInUpdate], 18);
	CHECK_EQ(b.rows[kOutEngine], 2);
	CHECK_EQ(b.rows[kInOther], 0);
	CHECK_EQ(b.rows[kOutFile], 0);
	CHECK_EQ(b.rows[0], 20);
	CHECK_EQ(b.calls[kOutSave], 1);
	CHECK_EQ(b.ticks[kOutSave], 20);
	CHECK_EQ(b.calls[kInOther], 1);  // inclusive counters keep the call's own class
	CHECK_EQ(b.calls[kOutFile], 1);
	CHECK_EQ(b.ticks[kOutFile], 1);
	CHECK_EQ(c->SlotOf(CORE).saveDepth, 0);
	_aligned_free(c);
}

// -------------------------------------------------------------------------------- server: a boundary inside a frame
void TestServerBoundary()
{
	g_t = 0;
	SCore* c = NewCore(&FakeClock);
	g_t = 10; c->ServedEnter(CORE, kInUpdate);
	g_t = 15; c->CountUpdate(CORE);
	g_t = 50;
	int64_t wall = 0;
	const SBucket b1 = Close(c, CORE, &wall);  // as LeaveUpdate does: inside the Update frame
	CHECK_EQ(wall, 50);
	CHECK_EQ(b1.rows[0], 10);
	CHECK_EQ(b1.rows[kInUpdate], 40);
	CHECK_EQ(b1.calls[kInUpdate], 0);
	CHECK_EQ(b1.updates, 1);
	g_t = 70; c->ServedLeave(CORE);
	g_t = 100;
	const SBucket b2 = Close(c, CORE, &wall);
	CHECK_EQ(wall, 50);
	CHECK_EQ(b2.rows[kInUpdate], 20);
	CHECK_EQ(b2.rows[0], 30);
	CHECK_EQ(b2.calls[kInUpdate], 1);   // the call ended in the second turn
	CHECK_EQ(b2.ticks[kInUpdate], 60);
	CHECK_EQ(b2.updates, 0);
	// a boundary from a thread that serves no channel (the final flush)
	g_t = 130;
	const SBucket b3 = Close(c, SrvBudgetCore::kChannels, &wall);
	CHECK_EQ(wall, 30);
	CHECK_EQ(b3.rows[0], 30);
	_aligned_free(c);
}

// ------------------------------------------------------------------------ server: deep nesting and unbalanced calls
void TestServerDeep()
{
	g_t = 0;
	SCore* c = NewCore(&FakeClock);
	const int n = SrvBudgetCore::kMaxDepth + 6;
	for (int i = 0; i < n; ++i)
	{
		g_t += 1;
		if (i % 2) c->OutEnter(CORE, kOutScript); else c->ServedEnter(CORE, kInLua);
	}
	for (int i = n - 1; i >= 0; --i)
	{
		g_t += 1;
		if (i % 2) c->OutLeave(CORE); else c->ServedLeave(CORE);
	}
	CHECK_EQ(c->SlotOf(CORE).depth, 0);
	CHECK_EQ(c->SlotOf(CORE).skipped, 0);
	c->ServedLeave(CORE);  // a leave without an enter: ignored
	c->OutLeave(CORE);
	CHECK_EQ(c->SlotOf(CORE).depth, 0);
	g_t += 5;
	int64_t wall = 0;
	const SBucket b = Close(c, CORE, &wall);
	CHECK_EQ(b.RowSum(), wall);
	CHECK_EQ(b.deep, 6);
	CHECK_EQ(b.calls[kInLua] + b.calls[kOutScript], SrvBudgetCore::kMaxDepth);
	_aligned_free(c);
}

// ------------------------------------------------------------------------------- server: threads and the real clock
struct StressArg
{
	SCore* c;
	int ch;
	volatile LONG* stop;
	long frames;
};
DWORD WINAPI StressThread(void* pv)
{
	StressArg* a = static_cast<StressArg*>(pv);
	unsigned r = 12345u + (unsigned)a->ch * 7919u;
	while (!*a->stop)
	{
		r = r * 1103515245u + 12345u;
		const int depth = 1 + (int)((r >> 16) % 4);
		for (int d = 0; d < depth; ++d)
		{
			if (d % 2) a->c->OutEnter(a->ch, kOutScript + (int)((r >> 8) % 7)); else a->c->ServedEnter(a->ch, kInLua + (int)((r >> 4) % 4));
			++a->frames;
		}
		for (int d = depth - 1; d >= 0; --d)
			if (d % 2) a->c->OutLeave(a->ch); else a->c->ServedLeave(a->ch);
		if (((r >> 20) & 15) == 0) SwitchToThread();
	}
	return 0;
}
void TestServerStress()
{
	SCore* c = NewCore(&RealClock);
	volatile LONG stop = 0;
	StressArg args[2] = {{c, UI, &stop, 0}, {c, AUX, &stop, 0}};
	HANDLE h[2];
	for (int i = 0; i < 2; ++i) h[i] = CreateThread(NULL, 0, StressThread, &args[i], 0, NULL);
	int64_t total = 0, walls = 0, bad = 0, anomalies = 0, turns = 0;
	const int64_t t0 = c->Start();
	const DWORD tEnd = GetTickCount() + 1500;
	long coreFrames = 0;
	while (GetTickCount() < tEnd)
	{
		// the core thread: Update frames with a boundary inside some of them, as LeaveUpdate makes them
		c->ServedEnter(CORE, kInUpdate);
		for (int i = 0; i < 200; ++i)
		{
			c->OutEnter(CORE, (i % 50) ? kOutScript : kOutSave);
			c->ServedEnter(CORE, kInPlot);
			c->ServedLeave(CORE);
			c->OutLeave(CORE);
			coreFrames += 2;
		}
		SBucket b;
		int64_t wall = 0;
		bool synced = false;
		c->Boundary(CORE, b, &wall, &synced);
		c->ServedLeave(CORE);
		++turns;
		walls += wall;
		total += b.RowSum();
		if (b.RowSum() != wall || !synced) ++bad;
		anomalies += b.anomalies;
	}
	InterlockedExchange(&stop, 1);
	WaitForMultipleObjects(2, h, TRUE, INFINITE);
	for (int i = 0; i < 2; ++i) CloseHandle(h[i]);
	SBucket b;
	int64_t wall = 0;
	bool synced = false;
	c->Boundary(SrvBudgetCore::kChannels, b, &wall, &synced);
	walls += wall;
	total += b.RowSum();
	if (b.RowSum() != wall || !synced) ++bad;
	CHECK_EQ(bad, 0);
	CHECK_EQ(anomalies, 0);
	CHECK_EQ(total, walls);
	CHECK_EQ(walls, c->LastBoundary() - t0);
	CHECK(turns > 10);
	printf("server stress: %lld turns, frames core %ld ui %ld aux %ld, every turn's rows = its wall in ticks: %s\n",
	       (long long)turns, coreFrames, args[0].frames, args[1].frames, bad ? "NO" : "yes");
	_aligned_free(c);
}

// ---------------------------------------------------------------------------- server: the cost of one frame change
void TestServerCost()
{
	SCore* c = NewCore(&RealClock);
	const int n = 2000000;
	const int64_t a = RealClock();
	for (int i = 0; i < n; ++i)
	{
		c->ServedEnter(UI, kInLua);
		c->ServedLeave(UI);
	}
	const int64_t b = RealClock();
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	const double ns = (double)(b - a) * 1e9 / (double)f.QuadPart / (2.0 * n);
	printf("server cost: %.1f ns per frame change (one thread, uncontended; %d changes)\n", ns, 2 * n);
	CHECK(ns > 0.0);
	_aligned_free(c);
}

// ------------------------------------------------------------------------------------------------------------ proxy
vpbudget::TurnBlock g_block;
void Publish(int64_t tb, int turn)
{
	const int32_t k = g_block.count;
	vpbudget::BoundaryEntry& e = g_block.e[(uint32_t)k % vpbudget::kRing];
	e.seq = 2 * k + 1;
	e.tb = tb;
	e.turn = turn;
	e.seq = 2 * k + 2;
	g_block.count = k + 1;
}
typedef ProxyBudgetCore::Core PCore;
PCore* NewProxy()
{
	memset(&g_block, 0, sizeof(g_block));
	g_block.magic = vpbudget::kMagic;
	g_block.version = vpbudget::kVersion;
	PCore* p = static_cast<PCore*>(_aligned_malloc(sizeof(PCore), 64));
	p->Init(&FakeClock, &g_block);
	p->SetSettle(100);
	return p;
}

void TestProxy()
{
	g_t = 100;
	PCore* p = NewProxy();
	ProxyBudgetCore::Closed out;
	CHECK(!p->TakeClosed(false, g_t, out));   // nothing published
	// load phase: one call 110-150 (in flight), idle 100-110 and 150-200
	g_t = 110; p->BusyBegin(g_t);
	g_t = 150; p->Call(0, kInOther, 110, 150); p->BusyEnd(g_t);
	g_t = 200; p->BusyBegin(g_t);              // the first Update begins (boundary 0 lies inside it)
	Publish(250, 150);                         // boundary 0: turn 150 begins at 250
	g_t = 260; p->Call(1, kOutScript, 255, 260);  // the DLL called the EXE while Update was in flight
	g_t = 300; p->Call(0, kInUpdate, 200, 300); p->BusyEnd(g_t);
	CHECK(!p->TakeClosed(false, g_t, out));   // bucket 0 closed at 250, not settled at 300 (settle 100)
	g_t = 340; p->BusyBegin(g_t);
	Publish(350, 151);                         // boundary 1
	g_t = 360; p->Call(0, kInUpdate, 340, 360); p->BusyEnd(g_t);
	g_t = 400; p->BusyBegin(g_t);
	Publish(450, 152);                         // boundary 2: bucket 1 ready too
	g_t = 460; p->Call(0, kInUpdate, 400, 460); p->BusyEnd(g_t);
	int got = 0;
	ProxyBudgetCore::Closed c0 = {}, c1 = {};
	while (p->TakeClosed(false, g_t, out))
	{
		if (got == 0) c0 = out;
		if (got == 1) c1 = out;
		++got;
	}
	CHECK_EQ(got, 2);
	CHECK_EQ(c0.index, 0);
	CHECK_EQ(c0.turn, -1);
	CHECK_EQ(c0.flag, 1);
	CHECK_EQ(c0.wall, 150);                     // 100 -> 250
	CHECK_EQ(c0.exeAlone, 10 + 50);             // 100-110, 150-200
	CHECK_EQ(c0.calls[0][kInOther], 1);
	CHECK_EQ(c0.ticks[0][kInOther], 40);
	CHECK_EQ(c1.index, 1);
	CHECK_EQ(c1.turn, 150);
	CHECK_EQ(c1.flag, 0);
	CHECK_EQ(c1.wall, 100);                     // 250 -> 350
	CHECK_EQ(c1.exeAlone, 40);                  // 300-340
	CHECK_EQ(c1.calls[0][kInUpdate], 1);        // ended at 300
	CHECK_EQ(c1.ticks[0][kInUpdate], 100);
	CHECK_EQ(c1.calls[1][kOutScript], 1);
	CHECK_EQ(c1.ticks[1][kOutScript], 5);
	// the end: buckets 2 and 3 (partial, up to now)
	g_t = 500;
	got = 0;
	ProxyBudgetCore::Closed c2 = {}, c3 = {};
	while (p->TakeClosed(true, g_t, out))
	{
		if (got == 0) c2 = out;
		if (got == 1) c3 = out;
		++got;
	}
	CHECK_EQ(got, 2);
	CHECK_EQ(c2.turn, 151);
	CHECK_EQ(c2.wall, 100);
	CHECK_EQ(c2.exeAlone, 40);                  // 360-400
	CHECK_EQ(c2.calls[0][kInUpdate], 1);        // 340-360
	CHECK_EQ(c3.turn, 152);
	CHECK_EQ(c3.flag, 2);
	CHECK_EQ(c3.wall, 50);                      // 450 -> now
	CHECK_EQ(c3.calls[0][kInUpdate], 1);        // 400-460
	CHECK(!p->TakeClosed(true, g_t, out));
	CHECK_EQ(p->Late(), 0);
	_aligned_free(p);
}

void TestProxyLostAndLate()
{
	g_t = 0;
	PCore* p = NewProxy();
	for (int k = 0; k < 12; ++k) Publish(100 * (k + 1), 10 + k);  // 12 boundaries, the ring keeps 8
	// an event before the oldest boundary still in the ring: counted late, put in the oldest bucket the ring knows
	CHECK_EQ(p->BucketOf(150), 12 - (int)vpbudget::kRing);
	CHECK_EQ(p->Late(), 1);
	CHECK_EQ(p->BucketOf(1250), 12);
	CHECK_EQ(p->BucketOf(1200), 12);
	CHECK_EQ(p->BucketOf(1199), 11);
	ProxyBudgetCore::Closed out;
	int lost = 0, ok = 0;
	while (p->TakeClosed(false, 1300, out))
	{
		if (out.flag == 3) ++lost;
		else ++ok;
		CHECK(out.flag == 3 ? out.wall == 0 : out.wall == 100 || out.index == 0);
	}
	CHECK_EQ(lost + ok, 12);  // buckets 0..11 (boundary 11 at 1200 is 100 ticks old at 1300)
	CHECK(lost >= 1);
	// an entry being written is skipped: the event goes to the bucket of the boundary before it
	g_block.e[11 % vpbudget::kRing].seq = 2 * 11 + 1;
	CHECK_EQ(p->BucketOf(1250), 11);
	CHECK_EQ(p->Late(), 1);
	_aligned_free(p);
}

void TestProxyStress()
{
	memset(&g_block, 0, sizeof(g_block));
	PCore* p = static_cast<PCore*>(_aligned_malloc(sizeof(PCore), 64));
	p->Init(&RealClock, &g_block);
	// two threads each with calls in flight on and off; the idle time must never be negative and the counts balance
	struct A
	{
		static DWORD WINAPI Run(void* pv)
		{
			PCore* q = static_cast<PCore*>(pv);
			for (int i = 0; i < 200000; ++i)
			{
				const int64_t t = RealClock();
				q->BusyBegin(t);
				q->Call(0, kInLua, t, RealClock());
				q->BusyEnd(RealClock());
			}
			return 0;
		}
	};
	HANDLE h[2];
	for (int i = 0; i < 2; ++i) h[i] = CreateThread(NULL, 0, A::Run, p, 0, NULL);
	WaitForMultipleObjects(2, h, TRUE, INFINITE);
	for (int i = 0; i < 2; ++i) CloseHandle(h[i]);
	ProxyBudgetCore::Closed out;
	const int64_t now = RealClock();
	CHECK(p->TakeClosed(true, now, out));
	CHECK_EQ(out.calls[0][kInLua], 400000);
	CHECK(out.exeAlone >= 0 && out.exeAlone <= out.wall);
	CHECK(out.idleIntervals >= 1);
	_aligned_free(p);
}
// The proxy's cost per call it makes (as ProxyBudget::OutBegin / OutEnd do for a top-level call: two clock reads, the
// busy word twice, the bucket of the end time, two 64-bit adds) and per call it serves (one clock read, the bucket, two
// adds), with a published turn block of 8 boundaries.
void TestProxyCost()
{
	memset(&g_block, 0, sizeof(g_block));
	for (int k = 0; k < 8; ++k) Publish(RealClock() - 1000000 + k, 100 + k);
	PCore* p = static_cast<PCore*>(_aligned_malloc(sizeof(PCore), 64));
	p->Init(&RealClock, &g_block);
	const int n = 1000000;
	const int64_t a = RealClock();
	for (int i = 0; i < n; ++i)
	{
		const int64_t t0 = RealClock();
		p->BusyBegin(t0);
		const int64_t t1 = RealClock();
		p->Call(0, kInLua, t0, t1);
		p->BusyEnd(t1);
	}
	const int64_t b = RealClock();
	for (int i = 0; i < n; ++i) p->Call(1, kOutScript, RealClock(), RealClock());
	const int64_t c = RealClock();
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	const double made = (double)(b - a) * 1e9 / (double)f.QuadPart / n;
	const double served = (double)(c - b) * 1e9 / (double)f.QuadPart / n;
	printf("proxy cost: %.1f ns per call made (top level), %.1f ns per call served (one thread, uncontended)\n", made,
	       served);
	CHECK(made > 0.0 && served > 0.0);
	ProxyBudgetCore::Closed out;
	int64_t calls = 0;
	while (p->TakeClosed(true, RealClock(), out)) calls += out.calls[0][kInLua];
	CHECK_EQ(calls, n);
	_aligned_free(p);
}
}  // namespace

int main()
{
	TestSwitch();
	TestClasses();
	TestServerSequence();
	TestServerPriority();
	TestServerSave();
	TestServerBoundary();
	TestServerDeep();
	TestServerStress();
	TestServerCost();
	TestProxy();
	TestProxyLostAndLate();
	TestProxyStress();
	TestProxyCost();
	printf("test_budget: %d passed, %d failed (%s)\n", g_pass, g_fail, sizeof(void*) == 8 ? "x64" : "x86");
	return g_fail ? 1 : 0;
}
