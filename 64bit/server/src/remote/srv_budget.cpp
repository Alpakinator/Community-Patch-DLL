// srv_budget.cpp - the turn budget of server.exe's remote mode (ticket 4652, epic 0437 W4a): the switch, the class of
// every method id, the files and the shared turn block around SrvBudgetCore (srv_budget_core.h).
// Files in the run folder (the server's working directory), only with VP64_TURN_BUDGET=1:
//   budget.tsv       "# qpc_hz <f>" then "turn kind name seconds count ticks", per turn (the interval between two
//                    boundaries; a boundary is the end of the Update in which the game turn changed):
//                      turn wall <s> <updates> <ticks>        the turn's wall time; count = Update calls in it
//                      turn load|first|end 0 0 0              the load phase (turn -1), the first and the last (partial)
//                      row <row name> <s> <messages> <ticks>  vp_budget.h RowName; the rows sum to wall in ticks
//                      in|out <class> <s> <calls> <ticks>     inclusive: served handler time / round trip of calls made
//                      info hidden_msgs 0 <n> 0 | info overlap <s> 0 <ticks> | info anomalies, deep, unsynced 0 <n> 0
//                      info flush_prev <s> 0 <ticks>          writing the previous turn's lines (inside this turn)
//   turn-times.tsv   "turn seconds updates" as direct mode (srv_main.cpp); the first turn after the load carries a 4th
//                    field "(first)" (it starts at the end of the first Update), the last partial one "(end)".
#include "srv_remote.h"
#include "srv_budget.h"
#include "srv_budget_core.h"
#include "vp_ids.gen.h"
#include <stdio.h>
#include <stdlib.h>

namespace SrvBudget
{
bool g_bOn = false;
}

namespace
{
SrvBudgetCore::Core s_core;
LARGE_INTEGER s_freq;
FILE* s_pBudget = NULL;
FILE* s_pTurns = NULL;
CRITICAL_SECTION s_csFiles;
int s_iTurn = -1;              // the game turn of the bucket now open (-1: the load, before the first Update ended)
bool s_bFirstWritten = false;  // the first real turn was written (it carries "first")
volatile LONG s_lFinal = 0;
int64_t s_flushPrev = 0;       // ticks spent writing the previous turn's lines
__declspec(thread) bool t_bInClose = false;  // a fault while this thread closes a turn must not wait for itself
long s_nTurnsWritten = 0;
vpbudget::TurnBlock* s_pBlock = NULL;
HANDLE s_hBlock = NULL;
uint8_t* s_cls[kVpKindCount + 1] = {};
uint32_t s_clsN[kVpKindCount + 1] = {};

int64_t QpcNow()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

double Sec(int64_t t) { return (double)t / (double)s_freq.QuadPart; }

int ClassOf(uint32_t method)
{
	const uint32_t t = method >> 16, i = method & 0xFFFFu;
	if (t >= 1 && t <= kVpKindCount && s_cls[t] && i < s_clsN[t]) return s_cls[t][i];
	const int c = vpbudget::ClassOfRuntime(method);
	return c ? c : (int)vpbudget::kInOther;
}

int SlotOfThisThread()
{
	const int ch = SrvRemote::ThreadChannel();
	return (ch >= 0 && ch < SrvBudgetCore::kChannels) ? ch : SrvBudgetCore::kChannels;
}

void Publish(int64_t tb, int iTurn)
{
	if (!s_pBlock) return;
	const int32_t k = s_pBlock->count;
	vpbudget::BoundaryEntry& e = s_pBlock->e[(uint32_t)k % vpbudget::kRing];
	e.seq = 2 * k + 1;
	MemoryBarrier();
	e.tb = tb;
	e.turn = iTurn;
	MemoryBarrier();
	e.seq = 2 * k + 2;
	InterlockedExchange((volatile LONG*)&s_pBlock->count, k + 1);
}

// The closed bucket's lines; caller holds s_csFiles.
void WriteTurn(int iTurn, const char* szFlag, const SrvBudgetCore::Bucket& b, int64_t wall, bool bSynced)
{
	if (!s_pBudget) return;
	fprintf(s_pBudget, "%d\tturn\twall\t%.9f\t%lld\t%lld\n", iTurn, Sec(wall), (long long)b.updates, (long long)wall);
	if (szFlag) fprintf(s_pBudget, "%d\tturn\t%s\t0\t0\t0\n", iTurn, szFlag);
	for (int r = 0; r < SrvBudgetCore::kRows; ++r)
		fprintf(s_pBudget, "%d\trow\t%s\t%.9f\t%lld\t%lld\n", iTurn, vpbudget::RowName(r), Sec(b.rows[r]),
		        (long long)b.msgs[r], (long long)b.rows[r]);
	for (int c = 1; c < SrvBudgetCore::kRows; ++c)
		if (b.calls[c])
			fprintf(s_pBudget, "%d\t%s\t%s\t%.9f\t%lld\t%lld\n", iTurn, vpbudget::IsIn(c) ? "in" : "out",
			        vpbudget::ClassName(c), Sec(b.ticks[c]), (long long)b.calls[c], (long long)b.ticks[c]);
	fprintf(s_pBudget, "%d\tinfo\thidden_msgs\t0\t%lld\t0\n", iTurn, (long long)b.hidden);
	fprintf(s_pBudget, "%d\tinfo\toverlap\t%.9f\t0\t%lld\n", iTurn, Sec(b.overlap), (long long)b.overlap);
	fprintf(s_pBudget, "%d\tinfo\tflush_prev\t%.9f\t0\t%lld\n", iTurn, Sec(s_flushPrev), (long long)s_flushPrev);
	if (b.anomalies) fprintf(s_pBudget, "%d\tinfo\tanomalies\t0\t%lld\t0\n", iTurn, (long long)b.anomalies);
	if (b.deep) fprintf(s_pBudget, "%d\tinfo\tdeep\t0\t%lld\t0\n", iTurn, (long long)b.deep);
	if (!bSynced) fprintf(s_pBudget, "%d\tinfo\tunsynced\t0\t1\t0\n", iTurn);
	fflush(s_pBudget);
	if (s_pTurns && iTurn >= 0)
	{
		const char* szNote = !s_bFirstWritten ? "\t(first)" : (szFlag && strcmp(szFlag, "end") == 0 ? "\t(end)" : "");
		fprintf(s_pTurns, "%d\t%.3f\t%lld%s\n", iTurn, Sec(wall), (long long)b.updates, szNote);
		fflush(s_pTurns);
	}
	if (iTurn >= 0) s_bFirstWritten = true;
	++s_nTurnsWritten;
}

// A boundary on this thread's slot: closes the open bucket (labelled s_iTurn) and opens the next (iNewTurn).
void Close(int iNewTurn, const char* szFlag, bool bPublish)
{
	t_bInClose = true;
	SrvBudgetCore::Bucket b;
	int64_t wall = 0;
	bool bSynced = true;
	const int64_t tb = s_core.Boundary(SlotOfThisThread(), b, &wall, &bSynced);
	if (bPublish) Publish(tb, iNewTurn);
	EnterCriticalSection(&s_csFiles);
	const int iTurn = s_iTurn;
	const char* flag = szFlag ? szFlag : (iTurn < 0 ? "load" : (!s_bFirstWritten ? "first" : NULL));
	WriteTurn(iTurn, flag, b, wall, bSynced);
	s_iTurn = iNewTurn;
	s_flushPrev = QpcNow() - tb;
	LeaveCriticalSection(&s_csFiles);
	t_bInClose = false;
}
}  // namespace

namespace SrvBudget
{
void Init()
{
	g_bOn = vpbudget::SwitchFromEnvironment();
	if (!g_bOn) return;  // off: nothing is opened, written or reported
	QueryPerformanceFrequency(&s_freq);
	InitializeCriticalSection(&s_csFiles);
	for (uint32_t k = 1; k <= kVpKindCount; ++k)
	{
		const uint32_t m = vpk_method_count(k);
		if (!m) continue;
		s_cls[k] = static_cast<uint8_t*>(calloc(m, 1));
		if (!s_cls[k]) continue;
		s_clsN[k] = m;
		const bool engine = vpk_is_engine_iface(k);
		for (uint32_t i = 0; i < m; ++i)
			s_cls[k][i] = (uint8_t)vpbudget::ClassOfMethod(vpk_iface_name(k), vpk_method_key((k << 16) | i), engine);
	}
	s_pBudget = _fsopen("budget.tsv", "w", _SH_DENYWR);
	s_pTurns = _fsopen("turn-times.tsv", "w", _SH_DENYWR);
	if (s_pBudget)
	{
		fprintf(s_pBudget, "# qpc_hz %lld; ticket 4652 turn budget; rows sum to wall in ticks\n", (long long)s_freq.QuadPart);
		fprintf(s_pBudget, "turn\tkind\tname\tseconds\tcount\tticks\n");
		fflush(s_pBudget);
	}
	if (s_pTurns)
	{
		fprintf(s_pTurns, "turn\tseconds\tupdates\n");
		fflush(s_pTurns);
	}
	s_core.Init(&QpcNow);
	SrvRemote::Report("budget: on (%s=1, ticket 4652): budget.tsv%s, turn-times.tsv%s in the run folder; qpc %lld Hz",
	                  vpbudget::kEnvName, s_pBudget ? "" : " (LOUD: cannot open)", s_pTurns ? "" : " (LOUD: cannot open)",
	                  (long long)s_freq.QuadPart);
}

void OpenBlock(uint32_t uProxyPid)
{
	if (!g_bOn || s_pBlock) return;
	char szName[64];
	sprintf_s(szName, "%s%u", vpbudget::kSectionPrefix, uProxyPid);
	s_hBlock = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, szName);
	void* p = s_hBlock ? MapViewOfFile(s_hBlock, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(vpbudget::TurnBlock)) : NULL;
	vpbudget::TurnBlock* b = static_cast<vpbudget::TurnBlock*>(p);
	if (!b || b->magic != vpbudget::kMagic || b->version != vpbudget::kVersion)
	{
		SrvRemote::Report("budget: LOUD: no turn block %s from the proxy (error %lu%s): budget-proxy.tsv will not have the "
		                  "server's turn boundaries", szName, GetLastError(), b ? ", wrong magic or version" : "");
		if (p) UnmapViewOfFile(p);
		if (s_hBlock) CloseHandle(s_hBlock);
		s_hBlock = NULL;
		return;
	}
	b->serverOn = 1;
	s_pBlock = b;
	SrvRemote::Report("budget: turn block %s opened (the proxy's budget is on)", szName);
}

void ServedEnter(uint32_t method)
{
	const int ch = SrvRemote::ThreadChannel();
	if (ch >= 0 && ch < SrvBudgetCore::kChannels) s_core.ServedEnter(ch, ClassOf(method));
}

void ServedLeave()
{
	const int ch = SrvRemote::ThreadChannel();
	if (ch >= 0 && ch < SrvBudgetCore::kChannels) s_core.ServedLeave(ch);
}

void OutEnter(uint32_t method)
{
	const int ch = SrvRemote::ThreadChannel();
	if (ch >= 0 && ch < SrvBudgetCore::kChannels) s_core.OutEnter(ch, ClassOf(method));
}

void OutLeave()
{
	const int ch = SrvRemote::ThreadChannel();
	if (ch >= 0 && ch < SrvBudgetCore::kChannels) s_core.OutLeave(ch);
}

void CountUpdate()
{
	const int ch = SrvRemote::ThreadChannel();
	if (ch >= 0 && ch < SrvBudgetCore::kChannels) s_core.CountUpdate(ch);
}

void AtTurn(int iTurn)
{
	if (iTurn == s_iTurn || s_lFinal) return;
	Close(iTurn, NULL, true);
}

void Final(const char* szWhy)
{
	if (!g_bOn || t_bInClose || InterlockedExchange(&s_lFinal, 1)) return;
	Close(s_iTurn, "end", false);
	EnterCriticalSection(&s_csFiles);
	if (s_pBudget) fclose(s_pBudget);
	if (s_pTurns) fclose(s_pTurns);
	s_pBudget = s_pTurns = NULL;
	LeaveCriticalSection(&s_csFiles);
	SrvRemote::Report("budget: closed at %s: %ld turn lines written (the load and the last, partial turn included); "
	                  "boundaries published to the proxy: %d", szWhy ? szWhy : "?", s_nTurnsWritten,
	                  s_pBlock ? (int)s_pBlock->count : -1);
}
}  // namespace SrvBudget
