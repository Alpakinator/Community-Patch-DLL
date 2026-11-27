// proxy_budget.cpp - the proxy's side of the turn budget (ticket 4652, epic 0437 W4a): the switch, the class table,
// the shared turn block and budget-proxy.tsv around ProxyBudgetCore (proxy_budget_core.h).
// budget-proxy.tsv: "# qpc_hz <f>" then "turn kind name seconds count ticks", per turn (bucket):
//   turn wall <s> 0 <ticks>             from the server's published boundaries (the same values as budget.tsv's wall)
//   turn load|end|lost 0 0 0            bucket 0 (before the first boundary), the last (partial), boundaries lost
//   row exe_alone <s> <intervals> <ticks>   no EXE thread inside a call to the server
//   row busy <s> 0 <ticks>              wall - exe_alone
//   out|in <class> <s> <calls> <ticks>  out: EXE -> DLL round trips; in: DLL -> EXE handler time in the EXE
#include "proxy_budget.h"
#include "proxy_budget_core.h"
#include "vp_ids.gen.h"
#include <stdio.h>
#include <stdlib.h>

namespace ProxyBudget
{
bool g_bOn = false;
}

namespace
{
ProxyBudgetCore::Core g_core;
LARGE_INTEGER g_freq;
HANDLE g_hSection = NULL;
vpbudget::TurnBlock* g_block = NULL;
FILE* g_file = NULL;
std::string g_path;
int32_t g_seen = 0;           // boundaries seen at the last flush attempt
int32_t g_writtenUpTo = 0;    // buckets written (the next to write)
volatile LONG g_final = 0;
long g_turnsWritten = 0;
uint8_t* g_cls[kVpKindCount + 1] = {};
uint32_t g_clsN[kVpKindCount + 1] = {};
__declspec(thread) int t_inFlight = 0;  // calls this thread has in flight to the server

int64_t QpcNow()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}
double Sec(int64_t t) { return (double)t / (double)g_freq.QuadPart; }

int ClassOf(uint32_t method)
{
	const uint32_t t = method >> 16, i = method & 0xFFFFu;
	if (t >= 1 && t <= kVpKindCount && g_cls[t] && i < g_clsN[t]) return g_cls[t][i];
	const int c = vpbudget::ClassOfRuntime(method);
	return c ? c : (int)vpbudget::kOutOther;
}

void WriteClosed(const ProxyBudgetCore::Closed& c)
{
	if (!g_file) return;
	static const char* const kFlags[4] = {NULL, "load", "end", "lost"};
	fprintf(g_file, "%d\tturn\twall\t%.9f\t0\t%I64d\n", c.turn, Sec(c.wall), c.wall);
	if (c.flag > 0 && c.flag < 4) fprintf(g_file, "%d\tturn\t%s\t0\t0\t0\n", c.turn, kFlags[c.flag]);
	fprintf(g_file, "%d\trow\texe_alone\t%.9f\t%I64d\t%I64d\n", c.turn, Sec(c.exeAlone), c.idleIntervals, c.exeAlone);
	fprintf(g_file, "%d\trow\tbusy\t%.9f\t0\t%I64d\n", c.turn, Sec(c.wall - c.exeAlone), c.wall - c.exeAlone);
	for (int d = 0; d < 2; ++d)
		for (int k = 1; k < ProxyBudgetCore::kClasses; ++k)
			if (c.calls[d][k])
				fprintf(g_file, "%d\t%s\t%s\t%.9f\t%I64d\t%I64d\n", c.turn, d ? "in" : "out", vpbudget::ClassName(k),
				        Sec(c.ticks[d][k]), c.calls[d][k], c.ticks[d][k]);
	fflush(g_file);
	++g_turnsWritten;
}

// Writes what is ready; one writer at a time (the others go on at once).
void MaybeFlush(bool all)
{
	const int32_t n = g_core.Published();
	if (!all && n == g_seen && g_writtenUpTo >= n) return;
	if (!g_core.TryLock()) return;
	g_seen = n;
	ProxyBudgetCore::Closed c;
	const int64_t now = QpcNow();
	while (g_core.TakeClosed(all, now, c))
	{
		WriteClosed(c);
		g_writtenUpTo = (int32_t)c.index + 1;
	}
	g_core.Unlock();
}
}  // namespace

namespace ProxyBudget
{
std::string Init(uint32_t uPid, const std::string& dir)
{
	g_bOn = vpbudget::SwitchFromEnvironment();
	if (!g_bOn) return std::string();
	QueryPerformanceFrequency(&g_freq);
	for (uint32_t k = 1; k <= kVpKindCount; ++k)
	{
		const uint32_t m = vpk_method_count(k);
		if (!m) continue;
		g_cls[k] = static_cast<uint8_t*>(calloc(m, 1));
		if (!g_cls[k]) continue;
		g_clsN[k] = m;
		const bool engine = vpk_is_engine_iface(k);
		for (uint32_t i = 0; i < m; ++i)
			g_cls[k][i] = (uint8_t)vpbudget::ClassOfMethod(vpk_iface_name(k), vpk_method_key((k << 16) | i), engine);
	}
	char name[64];
	_snprintf_s(name, sizeof(name), _TRUNCATE, "%s%u", vpbudget::kSectionPrefix, uPid);
	std::string line;
	g_hSection = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(vpbudget::TurnBlock), name);
	void* p = g_hSection ? MapViewOfFile(g_hSection, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(vpbudget::TurnBlock))
	                     : NULL;
	char buf[512];
	if (p)
	{
		g_block = static_cast<vpbudget::TurnBlock*>(p);
		memset(p, 0, sizeof(vpbudget::TurnBlock));
		g_block->magic = vpbudget::kMagic;
		g_block->version = vpbudget::kVersion;
	}
	g_path = dir + "\\budget-proxy.tsv";
	if (fopen_s(&g_file, g_path.c_str(), "wb") != 0) g_file = NULL;
	if (g_file)
	{
		fprintf(g_file, "# qpc_hz %I64d; ticket 4652 turn budget, the proxy's side\n", (int64_t)g_freq.QuadPart);
		fprintf(g_file, "turn\tkind\tname\tseconds\tcount\tticks\n");
		fflush(g_file);
	}
	g_core.Init(&QpcNow, g_block);
	g_core.SetSettle(g_freq.QuadPart / 20);  // 50 ms after a boundary its turn is written
	_snprintf_s(buf, sizeof(buf), _TRUNCATE,
	            "budget: on (%s=1, ticket 4652): section %s %s, %s %s; qpc %I64d Hz", vpbudget::kEnvName, name,
	            g_block ? "created" : "NOT created (LOUD: no turn boundaries from the server)", g_path.c_str(),
	            g_file ? "opened" : "NOT opened (LOUD)", (int64_t)g_freq.QuadPart);
	return buf;
}

int64_t OutBegin()
{
	const int64_t t = QpcNow();
	if (t_inFlight++ == 0) g_core.BusyBegin(t);
	return t;
}

void OutEnd(uint32_t method, int64_t t0)
{
	const int64_t t = QpcNow();
	g_core.Call(0, ClassOf(method), t0, t);
	if (t_inFlight > 0 && --t_inFlight == 0) g_core.BusyEnd(t);
	MaybeFlush(false);
}

void Served(uint32_t method, int64_t t0)
{
	g_core.Call(1, ClassOf(method), t0, QpcNow());
}

std::string Final()
{
	if (!g_bOn || InterlockedExchange(&g_final, 1)) return std::string();
	// the last turn may be taken by a flush in progress on another thread: wait for it (bounded)
	bool got = false;
	for (int i = 0; i < 2000 && !(got = g_core.TryLock()); ++i) Sleep(1);
	if (got) g_core.Unlock();
	MaybeFlush(true);
	if (g_file) fclose(g_file);
	g_file = NULL;
	char buf[400];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE,
	            "budget: %s closed: %ld turn buckets written; boundaries from the server %d; late events %I64d; the "
	            "server %s the turn block", g_path.c_str(), g_turnsWritten, g_block ? (int)g_block->count : -1,
	            g_core.Late(), (g_block && g_block->serverOn) ? "opened" : "did NOT open");
	return buf;
}
}  // namespace ProxyBudget
