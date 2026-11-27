// farm_guard_test.cpp - unit test of composite/farm_guard.cpp (ticket 0527). Written 2026-11-18 for this repository
// (GPL-3.0-or-later). A console program, never loaded into the game: it builds a fake "B" event manager, a fake listener
// list with the EXE's node form (research/02-exe-internals/farm-builder-guard.md), a fake builder and a fake layout graph
// whose free list behaves as the EXE's free 0x5855A0 is described there, and walks the list as the EXE's listener walk
// does. Prints "farm_guard_test: OK <n> checks" and exits 0, or names every failed check and exits 1.
//   farm_guard_test.exe <scratch folder>
#include <windows.h>
#include <math.h>
#include <share.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "farm_guard.h"
#include "shell_exe.h"   // ticket 4965: fake images of both builds, the build labels and symbol names
#include "vp_farm_points.h"   // ticket 0657: the server's points block
#include <vector>

static int g_checks, g_fails;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static std::string g_logText;
static void Log(const char* fmt, ...)
{
	char b[2048];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, ap); va_end(ap);
	g_logText += b; g_logText += "\n";
}
static bool LogHas(const char* s) { return g_logText.find(s) != std::string::npos; }

// ---- the fake EXE objects ------------------------------------------------------------------------------------------------
#define NIL 0x0FFFFFFFu
struct Node { uint32_t next, prev, pad, obj, fn; };
static_assert(sizeof(Node) == 20, "the EXE's listener node is 20 bytes");
struct List { uint32_t freeHead, live, data, size, cap, count, head, tail; };
static_assert(sizeof(List) == 0x20, "list header");

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
static int g_otherCalls;
static void __fastcall FakeOther(void*, void*, void*) { ++g_otherCalls; }

static int g_builderCalls;
static void* g_lastObj; static void* g_lastEv;
static int g_doubleFreeOnCall;   // the builder frees one endpoint twice on this call (1-based; 0 = never)

// The free of a free-list array: bit 31 set; linked before the head when below it, else after the head; live - 1.
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
// A sound graph: n entries, the ones in `freed` freed (in that order), the others live with count i % 5.
static void BuildGraph(uint32_t n, const uint32_t* freed, uint32_t nf)
{
	memset(g_graph, 0, sizeof(g_graph)); memset(g_eps, 0, sizeof(g_eps));
	uint32_t* h = EpHdr();
	h[0] = NIL; h[1] = n; h[2] = (uint32_t)(uintptr_t)g_eps; h[3] = n; h[4] = 64;
	for (uint32_t i = 0; i < n; ++i) g_eps[i].count = i % 5;
	for (uint32_t k = 0; k < nf; ++k) FakeFree(freed[k]);
}
static void __fastcall FakeBuilder(void* obj, void* edx, void* ev)
{
	(void)edx;
	++g_builderCalls; g_lastObj = obj; g_lastEv = ev;
	if (g_doubleFreeOnCall && g_builderCalls == g_doubleFreeOnCall) { FakeFree(7); FakeFree(7); }
}

static FgWorld g_w;
static void ResetList()
{
	memset(&g_list, 0, sizeof(g_list)); memset(g_nodes, 0, sizeof(g_nodes));
	g_list.data = (uint32_t)(uintptr_t)g_nodes; g_list.cap = 8; g_list.head = g_list.tail = NIL; g_list.freeHead = NIL;
}
// The EXE's register (0x8478A0 + 0x861760): a new node at the next index, linked at the tail.
static uint32_t Register(uint32_t obj, uint32_t fn)
{
	const uint32_t i = g_list.size++;
	g_nodes[i].obj = obj; g_nodes[i].fn = fn; g_nodes[i].next = NIL; g_nodes[i].prev = g_list.tail;
	if (g_list.tail != NIL) g_nodes[g_list.tail].next = i;
	if (g_list.head == NIL) g_list.head = i;
	g_list.tail = i; g_list.count++; g_list.live++;
	return i;
}
// The EXE's unregister (0x846BE0): the first node whose object and function both match is unlinked; false = none.
static bool Unregister(uint32_t obj, uint32_t fn)
{
	uint32_t i = g_list.head;
	for (uint32_t k = 0; k < g_list.count; ++k)
	{
		if (g_nodes[i].obj == obj && g_nodes[i].fn == fn)
		{
			const uint32_t p = g_nodes[i].prev, n = g_nodes[i].next;
			if (p != NIL) g_nodes[p].next = n; else g_list.head = n;
			if (n != NIL) g_nodes[n].prev = p; else g_list.tail = p;
			g_list.count--; g_list.live--;
			return true;
		}
		i = g_nodes[i].next;
	}
	return false;
}
// The EXE's listener walk (0x8668E0): count nodes from the head, each called (object in ecx, one argument).
static void Walk(void* ev)
{
	uint32_t idx = g_list.head;
	for (uint32_t k = 0; k < g_list.count; ++k)
	{
		Node* n = &g_nodes[idx];
		((FgListenerFn)(uintptr_t)n->fn)((void*)(uintptr_t)n->obj, nullptr, ev);
		idx = n->next;
	}
}

static uint32_t* Slot(uint32_t channel) { return (uint32_t*)(g_mgr + 0x1E0 + channel * 0x600); }
static void ResetWorld(const wchar_t* recordDir)
{
	memset(g_mgr, 0, sizeof(g_mgr));
	ResetList();
	Slot(0)[0] = (uint32_t)(uintptr_t)FakeTrampoline; Slot(0)[1] = (uint32_t)(uintptr_t)&g_list;
	g_lockInit = 1; g_lock = 0; g_viewFlag = 1;
	g_w.lockInit = (uintptr_t)&g_lockInit; g_w.lock = (uintptr_t)&g_lock; g_w.mgr = (uintptr_t)g_mgr;
	g_w.slotOff = 0x1E0; g_w.stride = 0x600; g_w.trampoline = (uintptr_t)FakeTrampoline;
	g_w.builder = (uintptr_t)FakeBuilder; g_w.builderObj = (uintptr_t)g_objStore; g_w.viewFlag = (uintptr_t)&g_viewFlag;
	g_w.graph = (uintptr_t)g_graph;
	g_w.poster = g_w.queueIndex = g_w.queueBase = g_w.plotOnMap = g_w.plotGrid = g_w.artId = g_w.artDb = g_w.artName = 0;   // 0655: no post
	const uint32_t fr[] = { 3, 9, 1 };
	BuildGraph(20, fr, 3);
	g_builderCalls = 0; g_doubleFreeOnCall = 0; g_lastObj = g_lastEv = nullptr;
	g_logText.clear();
	FarmGuard_TestUse(&g_w, recordDir);
}
static FarmGuardEnv Env(int enabled)
{
	FarmGuardEnv e = {};
	e.exeValidated = 1; e.gridValid = 1; e.gridSeq = 1; e.enabled = enabled; e.log = Log;
	return e;
}
static FgStats St() { FgStats s; FarmGuard_GetStats(&s); return s; }
static uint32_t OBJ() { return (uint32_t)(uintptr_t)g_objStore; }
static uint32_t BUILDER() { return (uint32_t)(uintptr_t)FakeBuilder; }
static uint32_t HOOK() { return (uint32_t)(uintptr_t)FarmGuard_Hook(); }
static uint32_t INERT() { return (uint32_t)(uintptr_t)FarmGuard_Inert(); }

// ---- ticket 4854: a fake layout graph with a grid, nodes and endpoints (farm-field-double-free.md 2.2) -------------------
#include <vector>
struct FakeGraph
{
	unsigned char hdr[0xD0];
	std::vector<unsigned char> cells, nodes, eps;
	uint32_t vtx[3];
};
static FakeGraph g_fg;
static uint32_t& U32(std::vector<unsigned char>& v, size_t off) { return *(uint32_t*)&v[off]; }
static uint16_t& U16(std::vector<unsigned char>& v, size_t off) { return *(uint16_t*)&v[off]; }
static uint32_t& H32(size_t off) { return *(uint32_t*)&g_fg.hdr[off]; }
static uint32_t& Cnt(uint32_t c) { return U32(g_fg.cells, c * 24u + 4); }
static uint32_t& First(uint32_t c) { return U32(g_fg.cells, c * 24u + 8); }
static uint32_t& Last(uint32_t c) { return U32(g_fg.cells, c * 24u + 12); }
static uint32_t& NPts(uint32_t n) { return U32(g_fg.nodes, n * 56u + 0x10); }
static uint16_t& NEp(uint32_t n, int end16) { return U16(g_fg.nodes, n * 56u + (end16 ? 0x16 : 0x14)); }
static uint32_t& NNext(uint32_t n) { return U32(g_fg.nodes, n * 56u + 0x28); }
static uint32_t& NPrev(uint32_t n) { return U32(g_fg.nodes, n * 56u + 0x2C); }
static uint16_t& NId(uint32_t n) { return U16(g_fg.nodes, n * 56u + 0x34); }
static unsigned char& NKind(uint32_t n) { return g_fg.nodes[n * 56u + 0x36]; }
static uint32_t& EpH(uint32_t e, int s) { return U32(g_fg.eps, e * 20u + 4u * s); }
static uint32_t& EpC(uint32_t e) { return U32(g_fg.eps, e * 20u + 0x10); }
// nx x ny cells (empty), n nodes (2 points, unfiled: id 0xFFFF, kind 1, ends 0xFFFF), m live endpoints with count 0
static void FgInit(uint32_t nx, uint32_t ny, uint32_t n, uint32_t m)
{
	memset(g_fg.hdr, 0, sizeof(g_fg.hdr));
	g_fg.cells.assign((size_t)nx * ny * 24u + 24u, 0); g_fg.nodes.assign(n * 56u + 56u, 0); g_fg.eps.assign(m * 20u + 20u, 0);
	H32(0x84) = nx; H32(0x88) = ny; H32(0x8C) = (uint32_t)(uintptr_t)g_fg.cells.data();
	H32(0x94) = NIL; H32(0x98) = m; H32(0x9C) = (uint32_t)(uintptr_t)g_fg.eps.data(); H32(0xA0) = m; H32(0xA4) = m;
	H32(0xBC) = NIL; H32(0xC0) = n; H32(0xC4) = (uint32_t)(uintptr_t)g_fg.nodes.data(); H32(0xC8) = n; H32(0xCC) = n;
	for (uint32_t c = 0; c < nx * ny; ++c) { First(c) = NIL; Last(c) = NIL; }
	for (uint32_t i = 0; i < n; ++i) { NPts(i) = 2; NEp(i, 0) = NEp(i, 1) = 0xFFFF; NNext(i) = NIL; NPrev(i) = NIL; NId(i) = 0xFFFF; NKind(i) = 1; }
	g_fg.vtx[0] = 0x1000; g_fg.vtx[1] = 1234; g_fg.vtx[2] = 4096;
}
// the list insert at the tail of cell c, the id set to the cell's low 16 bits
static void FgFile(uint32_t c, uint32_t i)
{
	NPrev(i) = Last(c); NNext(i) = NIL;
	if (Last(c) != NIL) NNext(Last(c)) = i; else First(c) = i;
	Last(c) = i; Cnt(c)++;
	NId(i) = (uint16_t)(c & 0xFFFF);
}
// the endpoint e takes node i's end (0 = +0x14, 1 = +0x16): a handle in the next slot, the node names e back
static void FgEnd(uint32_t e, uint32_t i, int end16)
{
	const uint32_t s = EpC(e)++;
	EpH(e, (int)s) = i | (end16 ? 0x10000000u : 0u);
	NEp(i, end16) = (uint16_t)e;
}
static uintptr_t FG() { return (uintptr_t)g_fg.hdr; }
static uintptr_t VTX() { return (uintptr_t)g_fg.vtx; }
// The small sound case (hand-built, ticket 4854 test A): 4 x 3 cells, 6 nodes, 3 endpoints.
//   cell 2: nodes 0, 1; cell 5: node 2; cell 11: node 3; node 4 on no list (id 0xFFFF, kind 1); node 5 freed (bit 31).
//   endpoint 0: node 0 +0x14, node 2 +0x14; endpoint 1: node 0 +0x16, node 1 +0x14; endpoint 2 freed (the free list's head).
static void FgSmall()
{
	FgInit(4, 3, 6, 3);
	FgFile(2, 0); FgFile(2, 1); FgFile(5, 2); FgFile(11, 3);
	NPrev(5) |= 0x80000000u; H32(0xC0) = 5;
	FgEnd(0, 0, 0); FgEnd(0, 2, 0); FgEnd(1, 0, 1); FgEnd(1, 1, 0);
	EpC(2) = 0x80000000u | NIL; H32(0x94) = 2; H32(0x98) = 2;
}

// ---- ticket 4855: the grid's extent and origin, the plot geometry (a hex lattice 64 apart, rows 64 x 0.866 apart) -------
static float g_h2w[16];
static float g_aabb[2];
static void FgGeo(float ex, float ey, float ox, float oy)
{
	*(float*)&g_fg.hdr[0x6C] = ex; *(float*)&g_fg.hdr[0x70] = ey; *(float*)&g_fg.hdr[0x74] = ox; *(float*)&g_fg.hdr[0x78] = oy;
}
// x = 64 hx + 32 hy + tx + minX; y = (64 hy + ty) x 0.866 + minY (row-vector form: m[0], m[4], m[12] for x; m[5], m[13] for y)
static void FgMat(float tx, float ty, float minX, float minY)
{
	memset(g_h2w, 0, sizeof(g_h2w));
	g_h2w[0] = 64.0f; g_h2w[4] = 32.0f; g_h2w[12] = tx; g_h2w[5] = 64.0f; g_h2w[13] = ty; g_h2w[15] = 1.0f;
	g_aabb[0] = minX; g_aabb[1] = minY;
}
// the event payload of the plot at grid (gx, gy): hex x = gx - (gy - (gy & 1)) / 2 at +0x18, y at +0x1A, the farm's art id
static unsigned char g_pay[0x40];
static void* Pay(int gx, int gy)
{
	memset(g_pay, 0, sizeof(g_pay));
	*(uint32_t*)(g_pay + 0x08) = 0x27u; *(int16_t*)(g_pay + 0x18) = (int16_t)(gx - (gy - (gy & 1)) / 2);
	*(int16_t*)(g_pay + 0x1A) = (int16_t)gy; g_pay[0x1C] = 1;
	return g_pay;
}

static std::wstring ReadAllW(const std::wstring& p, std::string* out)
{
	out->clear();
	FILE* f = _wfsopen(p.c_str(), L"rb", _SH_DENYNO);   // shared: the guard may still hold the file open for writing
	if (!f) return L"";
	char b[4096]; size_t n;
	while ((n = fread(b, 1, sizeof(b), f)) > 0) out->append(b, n);
	fclose(f);
	return p;
}

// ---- ticket 0655: the EXE's poster, plot test and art id as stand-ins (cities-and-landmarks.md 2.6.1) -----------------------
static unsigned char g_rec[0x80];             // the record the fake poster hands out (its payload at +0x10, as the EXE's)
static int g_posterCalls, g_plotCalls, g_artCalls, g_posterNull;
static void* g_posterQueue;
static int32_t g_plotCube[3];
static const char* g_artNameSeen;
static uint32_t g_queueIndex;
static const char g_fakeArtName[] = "ART_DEF_IMPROVEMENT_FARM";
static void* __fastcall FakePoster(void* q, void*)
{
	++g_posterCalls; g_posterQueue = q;
	if (g_posterNull) return nullptr;
	memset(g_rec, 0, sizeof(g_rec));
	*(uint32_t*)g_rec = 0x80; *(uint32_t*)(g_rec + 4) = 5;
	return g_rec + 0x10;
}
// The plot test answers in al only: the rest of eax is garbage (the EXE's code sets al alone).
static uint32_t __fastcall FakePlotOnMap(void* grid, void*, const int32_t* cube)
{
	++g_plotCalls; memcpy(g_plotCube, cube, sizeof(g_plotCube));
	const bool on = grid == (void*)0x5150 && cube[0] + cube[1] + cube[2] == 0 && cube[1] >= 0 && cube[1] < 50 && cube[0] > -40 && cube[0] < 80;
	return on ? 0xABCDEF01u : 0xABCDEF00u;
}
static int32_t g_artIdAnswer = 0x27;
static int32_t __fastcall FakeArtId(void* db, void*, const char* name)
{
	++g_artCalls; g_artNameSeen = name;
	return db == (void*)0x5250 && name == g_fakeArtName ? g_artIdAnswer : -1;
}
static void PostWorld(FgWorld* w)
{
	w->poster = (uintptr_t)FakePoster; w->queueIndex = (uintptr_t)&g_queueIndex; w->queueBase = 0x10000000u;
	w->plotOnMap = (uintptr_t)FakePlotOnMap; w->plotGrid = 0x5150; w->artId = (uintptr_t)FakeArtId; w->artDb = 0x5250;
	w->artName = (uintptr_t)g_fakeArtName;
	g_posterCalls = g_plotCalls = g_artCalls = g_posterNull = 0; g_posterQueue = nullptr; g_artIdAnswer = 0x27; g_queueIndex = 0;
}

// ---- ticket 0657 (section 17): a fake farm manager's placer and listeners -------------------------------------------
static int P_placerCalls, P_placerBadTable, L5_endCalls, L5_endDeferred, L5_eraCalls;
// the placer by the rule (cities-and-landmarks.md 2.6.2 (1)): float, the loop from the last point, the iterator's layout
struct P { static void Place(unsigned char* m, unsigned char* r)
{
	int16_t hx, hy; memcpy(&hx, r + 0x10, 2); memcpy(&hy, r + 0x12, 2);
	const float cx = (float)hx * 64.0f + (float)hy * 32.0f - 4096.0f, cy = (float)hy * 55.424f - 2327.81f;
	float px = cx, py = cy, best = 3.402823466e+38f;
	const unsigned n = r[5];
	if (n && *(uint32_t*)(m + 0x6F4) == 0x0BADF00Du) { P_placerBadTable++; return; }
	for (unsigned i = 0; i < n; ++i)
	{
		const unsigned char* c = (const unsigned char*)(uintptr_t)*(uint32_t*)(m + 0x6F4) + *(uint16_t*)(r + 0x20 + 2 * i) * 0x2Cu;
		const unsigned char* store = (const unsigned char*)(uintptr_t)*(const uint32_t*)(c + 8);
		const unsigned char* segs = (const unsigned char*)(uintptr_t)*(const uint32_t*)(store + 8);
		float xs[64], ys[64]; unsigned k = 0;
		for (uint32_t sg = *(const uint32_t*)(c + 0x10) & 0xFFFFFFu; sg != NIL && k < 60; sg = *(const uint32_t*)(segs + sg * 0xFC + 4))
		{
			const uint32_t cnt = *(const uint32_t*)(segs + sg * 0xFC + 8) & 0x7FFFFFFFu;
			for (uint32_t j = 0; j < cnt && k < 60; ++j, ++k) { xs[k] = ((const float*)(segs + sg * 0xFC + 0xC))[2 * j]; ys[k] = ((const float*)(segs + sg * 0xFC + 0xC))[2 * j + 1]; }
		}
		const uint32_t last = *(const uint32_t*)(c + 0x14);
		const uint32_t lc = *(const uint32_t*)(segs + last * 0xFC + 8) & 0x7FFFFFFFu;
		float xp = ((const float*)(segs + last * 0xFC + 0xC))[2 * (lc - 1)], yp = ((const float*)(segs + last * 0xFC + 0xC))[2 * (lc - 1) + 1];
		float F = 0, ax = 0, ay = 0, cr = 0;
		for (unsigned j = 0; j < k; ++j)
		{
			F += (ys[j] - yp) * xp;
			const float q = ys[j] * xp - xs[j] * yp;
			cr += q; ax += (xs[j] + xp) * q; ay += (yp + ys[j]) * q;
			xp = xs[j]; yp = ys[j];
		}
		F *= 0.5f;
		const float inv = 1.0f / (cr * 3.0f);
		const float gx = ax * inv, gy = ay * inv;
		const float dd = (gx - cx) * (gx - cx) + (gy - cy) * (gy - cy);
		if (fabsf(F) >= 100.0f && dd < best) { best = dd; px = gx; py = gy; }
	}
	memcpy(r + 0x14, &px, 4); memcpy(r + 0x18, &py, 4);
	*(uint32_t*)(r + 0x0C) |= 0x100;
	P_placerCalls++;
}
};
// the farm-end listener (the deferral, the plot's records), the era listener (every record with an instance)
struct L5 { static void __fastcall End(void* m, void*, void* ev)
{
	L5_endCalls++;
	unsigned char* mm = (unsigned char*)m;
	if (mm[0x207C0] & 1) { L5_endDeferred++; return; }
	int16_t x = ((int16_t*)ev)[0], y = ((int16_t*)ev)[1];
	unsigned char* rs = (unsigned char*)(uintptr_t)*(uint32_t*)(mm + 0x207A4);
	for (uint32_t i = 0; i < *(uint32_t*)(mm + 0x207A8); ++i)
	{
		unsigned char* r = rs + i * 0x90;
		if ((*(uint32_t*)(r + 0x0C) & 0x400) || *(int16_t*)(r + 0x10) != x || *(int16_t*)(r + 0x12) != y) continue;
		P::Place(mm, r);
	}
}
static void __fastcall Era(void* m, void*, void*)
{
	L5_eraCalls++;
	unsigned char* mm = (unsigned char*)m;
	if (mm[0x207C0] & 1) return;
	unsigned char* rs = (unsigned char*)(uintptr_t)*(uint32_t*)(mm + 0x207A4);
	for (uint32_t i = 0; i < *(uint32_t*)(mm + 0x207A8); ++i)
	{
		unsigned char* r = rs + i * 0x90;
		if (!(*(uint32_t*)(r + 0x0C) & 0x400) && (*(uint32_t*)(r + 0x0C) & 0x100)) P::Place(mm, r);
	}
}
};

// ---- ticket 0784: a fake Particle object and its type-0x28 listener (farm-builder-guard.md 15.1): with the busy bit the
// listener copies the payload into its own deferred list (replayed later by itself, not through the node); else it applies
// the record (here: notes its name hash).
struct T28
{
	struct Obj { int busy; int calls, deferred, applied; void* lastEv; uint32_t hashes[16]; unsigned char saved[8][0x70]; };
	static Obj g_main, g_scene;
	static void Reset() { memset(&g_main, 0, sizeof(g_main)); memset(&g_scene, 0, sizeof(g_scene)); }
	static bool Copy(const void* ev, unsigned char* out)
	{
		__try { memcpy(out, ev, 0x70); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void Apply(Obj* o, const unsigned char* p) { if (o->applied < 16) o->hashes[o->applied] = *(const uint32_t*)(p + 0x50); o->applied++; }
	static void __fastcall Listener(void* obj, void*, void* ev)
	{
		Obj* o = (Obj*)obj;
		o->calls++; o->lastEv = ev;
		unsigned char p[0x70];
		if (!Copy(ev, p)) return;
		if (o->busy) { if (o->deferred < 8) memcpy(o->saved[o->deferred], p, 0x70); o->deferred++; return; }
		Apply(o, p);
	}
	static void Replay(Obj* o) { for (int i = 0; i < o->deferred && i < 8; ++i) Apply(o, o->saved[i]); o->deferred = 0; }
};
T28::Obj T28::g_main, T28::g_scene;

#ifdef FARM_GUARD_PEER_DLL
// ---- ticket 0535: this file built as farm_guard_peer.dll (shell\build.cmd): a second module with its own copy of
// farm_guard.cpp, the composite beside the proxy's guard in one process. farm_guard_test.exe loads it in section 14.
extern "C" __declspec(dllexport) void PeerUse(const FgWorld* w, const wchar_t* dir) { FarmGuard_TestUse(w, dir); }
extern "C" __declspec(dllexport) void PeerPresent(void (*log)(const char*, ...), int enabled, uint32_t gridValid, uint32_t gridSeq)
{
	FarmGuardEnv e = {};
	e.exeValidated = 1; e.gridValid = gridValid; e.gridSeq = gridSeq; e.enabled = enabled; e.log = log;
	FarmGuard_OnPresent(e);
}
extern "C" __declspec(dllexport) void PeerStats(FgStats* s) { FarmGuard_GetStats(s); }
extern "C" __declspec(dllexport) int PeerRole() { return FarmGuard_Role(); }
extern "C" __declspec(dllexport) uintptr_t PeerHookWord() { return FarmGuard_HookWord(); }
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
#else
int wmain(int argc, wchar_t** argv)
{
	if (argc < 2) { printf("usage: farm_guard_test <scratch folder>\n"); return 2; }
	const std::wstring scratch = argv[1];
	CreateDirectoryW(scratch.c_str(), nullptr);
	const DWORD pid = GetCurrentProcessId();

	// ---- 1. the byte patterns on fake images of every known build (ticket 4965, epic 0590: the build table) ------------
	for (int b = 0; b < kVpExeBuildCount; ++b)
	{
		const uint32_t bytes = FgImageBytes(b);
		CHECK(bytes >= 0x1000);
		unsigned char* img = (unsigned char*)VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		CHECK(img != nullptr);
		const uint32_t base = (uint32_t)(uintptr_t)img;
		char why[256] = "";
		int build = -2;
		CHECK(FgPatternCount() == 13);
		CHECK(!FgCheckPatterns(base, why, sizeof(why), &build));     // an image of zeros: no PE header, no known build
		CHECK(build == kVpExeBuildUnknown && strstr(why, "not an EXE build the patch knows") != nullptr);
		CHECK(FgWritePatterns(b, base, bytes, -1));
		CHECK(FgCheckPatterns(base, why, sizeof(why), &build) && build == b);
		for (int k = 0; k < FgPatternCount(); ++k)
		{
			CHECK(FgWritePatterns(b, base, bytes, k));
			why[0] = 0;
			const bool ok = FgCheckPatterns(base, why, sizeof(why));
			char want[96]; _snprintf_s(want, _TRUNCATE, " at %s does not match on ", VpExeSymName(FgPatternSymbol(k)));
			CHECK(!ok && strstr(why, want) != nullptr && strstr(why, VpExeBuildLabel(b)) != nullptr);
		}
		CHECK(FgWritePatterns(b, base, bytes, -1));
		// a header no build has: refused before any pattern is read
		ShellExeTestForgetBuild(base);
		CHECK(!FgCheckPatterns(base, why, sizeof(why), &build) && build == kVpExeBuildUnknown);
		CHECK(strstr(why, "not an EXE build the patch knows (PE timestamp 0x") != nullptr);
		CHECK(FgWritePatterns(b, base, bytes, -1));
		// a symbol the table does not carry: refused naming it (a data symbol, then a pattern site)
		CHECK(ShellExeTestDrop("fg_lock"));
		CHECK(!FgCheckPatterns(base, why, sizeof(why), &build) && strstr(why, "symbol fg_lock is not carried to ") != nullptr);
		CHECK(ShellExeTestDrop("VPX_fg_builder_ret"));
		CHECK(!FgCheckPatterns(base, why, sizeof(why), &build) && strstr(why, "symbol fg_builder_ret is not carried to ") != nullptr);
		FgWorld w0 = {};
		CHECK(ShellExeTestDrop("fg_vtxvec") && !FgWorldFromBase(b, base, &w0) && w0.builder == 0);
		ShellExeTestDrop("");
		CHECK(FgCheckPatterns(base, why, sizeof(why), &build) && build == b);
		CHECK(!FgWritePatterns(b, base, 0x1000, -1));                 // a too small image is refused, nothing written past it
		// FgWorldFromBase: every address rebased from the build's table, the graph at the object's +0x158
		FgWorld w;
		CHECK(FgWorldFromBase(b, 0x00570000u, &w));
		const uint32_t d = 0x570000u - 0x400000u;
		CHECK(w.builder == ShellExeVa(b, VPX_fg_builder) + d && w.builderObj == ShellExeVa(b, VPX_fg_builder_obj) + d);
		CHECK(w.graph == w.builderObj + 0x158u && w.mgr == ShellExeVa(b, VPX_pac_qb_index) + d && w.lock == ShellExeVa(b, VPX_fg_lock) + d);
		CHECK(w.slotOff == 0x1E0u && w.stride == 0x600u && w.viewFlag == ShellExeVa(b, VPX_fg_viewflag) + d);
		CHECK(w.h2w == ShellExeVa(b, VPX_cam_h2w) + d && w.aabbMin == ShellExeVa(b, VPX_cam_aabb) + d);
		if (b == kVpExeBuild4390913)   // the anchor build: the values farm-builder-guard.md names
			CHECK(w.builder == 0x748FC0u + d && w.builderObj == 0xBA20D0u + d && w.graph == 0xBA2228u + d && w.mgr == 0x2A10200u + d &&
			      w.lock == 0xCBFC70u + d && w.viewFlag == 0xB9DDA0u + d);
		printf("farm_guard_test: patterns, refusals and addresses on %s: done\n", VpExeBuildLabel(b));
		VirtualFree(img, 0, MEM_RELEASE);
		// ticket 0655: the farm-end post's own patterns (with their call targets) and addresses
		const uint32_t pbytes = FgPostImageBytes(b);
		CHECK(pbytes >= 0x1000 && FgPostPatternCount() == 5);
		unsigned char* pimg = (unsigned char*)VirtualAlloc(nullptr, pbytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		CHECK(pimg != nullptr);
		if (pimg)
		{
			const uint32_t pbase = (uint32_t)(uintptr_t)pimg;
			CHECK(!FgPostCheckPatterns(pbase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr);   // zeros
			CHECK(FgPostWritePatterns(b, pbase, pbytes, -1));
			why[0] = 0;
			CHECK(FgPostCheckPatterns(pbase, b, why, sizeof(why)) && why[0] == 0);
			for (int k = 0; k < FgPostPatternCount(); ++k)
			{
				CHECK(FgPostWritePatterns(b, pbase, pbytes, k));
				CHECK(!FgPostCheckPatterns(pbase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr && strstr(why, VpExeBuildLabel(b)) != nullptr);
			}
			CHECK(FgPostWritePatterns(b, pbase, pbytes, -1));
			// a call target moved: the builder's call of the poster leads elsewhere (its rel32 changed by one)
			const uint32_t site = ShellExeVa(b, VPX_fg_builder_post) - 0x400000u + pbase;
			*(uint32_t*)(uintptr_t)(site + 17) += 1;   // 8B 0D <4> 69 C9 80 01 80 00 81 C1 <4> E8 <rel32 at +17>
			CHECK(!FgPostCheckPatterns(pbase, b, why, sizeof(why)) && strstr(why, "at fg_builder_post does not match") != nullptr);
			*(uint32_t*)(uintptr_t)(site + 17) -= 1;
			CHECK(FgPostCheckPatterns(pbase, b, why, sizeof(why)));
			CHECK(ShellExeTestDrop("fg_post_farm_end"));
			CHECK(!FgPostCheckPatterns(pbase, b, why, sizeof(why)) && strstr(why, "symbol fg_post_farm_end is not carried to ") != nullptr);
			FgWorld pw = {};
			CHECK(!FgPostWorldFromBase(b, pbase, &pw) && pw.poster == 0);
			ShellExeTestDrop("");
			CHECK(FgPostWorldFromBase(b, 0x00570000u, &pw));
			const uint32_t d = 0x570000u - 0x400000u;
			CHECK(pw.poster == ShellExeVa(b, VPX_fg_post_farm_end) + d && pw.queueIndex == ShellExeVa(b, VPX_qg_channel_index) + d);
			CHECK(pw.queueBase == ShellExeVa(b, VPX_qg_channels) + d && pw.plotOnMap == ShellExeVa(b, VPX_fg_plot_on_map) + d);
			CHECK(pw.plotGrid == ShellExeVa(b, VPX_fg_plot_grid) + d && pw.artId == ShellExeVa(b, VPX_fg_art_id) + d);
			CHECK(pw.artDb == ShellExeVa(b, VPX_fg_art_db) + d && pw.artName == ShellExeVa(b, VPX_fg_farm_art_name) + d);
			if (b == kVpExeBuild4390913)   // the anchor build: the values cities-and-landmarks.md 2.6.1 names
				CHECK(pw.poster == 0x48E810u + d && pw.queueIndex == 0x1A0E400u + d && pw.queueBase == 0x1A0F080u + d &&
				      pw.plotOnMap == 0x42B440u + d && pw.plotGrid == 0xD91130u + d);
			printf("farm_guard_test: the farm-end post's patterns on %s: done\n", VpExeBuildLabel(b));
			// ticket 0655 (P1): the area probe's three patterns and its world
			const uint32_t abytes = FgAreaImageBytes(b);
			unsigned char* aimg = abytes ? (unsigned char*)VirtualAlloc(nullptr, abytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
			CHECK(aimg != nullptr && FgAreaPatternCount() == 3);
			if (aimg)
			{
				const uint32_t abase = (uint32_t)(uintptr_t)aimg;
				CHECK(!FgAreaCheckPatterns(abase, b, why, sizeof(why)));
				CHECK(FgAreaWritePatterns(b, abase, abytes, -1) && FgAreaCheckPatterns(abase, b, why, sizeof(why)));
				for (int k = 0; k < FgAreaPatternCount(); ++k)
				{
					CHECK(FgAreaWritePatterns(b, abase, abytes, k));
					CHECK(!FgAreaCheckPatterns(abase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr);
				}
				FgWorld aw;
				CHECK(FgAreaWorldFromBase(b, 0x00570000u, &aw));
				CHECK(aw.builder == ShellExeVa(b, VPX_fg_area_listener) + d && aw.lock == ShellExeVa(b, VPX_fg_a_lock) + d &&
				      aw.mgr == ShellExeVa(b, VPX_qg_channel_index) + d && aw.slotOff == 0x110u && aw.stride == 0x600u &&
				      aw.builderObj == ShellExeVa(b, VPX_fg_builder_obj) + d);
				if (b == kVpExeBuild4390913) CHECK(aw.builder == 0x79D040u + d && aw.lock == 0xCBA9F0u + d && aw.mgr == 0x1A0E400u + d);
				VirtualFree(aimg, 0, MEM_RELEASE);
			}
			// ticket 0657: the edge placement's 13 patterns (with their call targets) and its two lists
			const uint32_t ebytes = FgEdgeImageBytes(b);
			unsigned char* eimg = ebytes ? (unsigned char*)VirtualAlloc(nullptr, ebytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
			CHECK(eimg != nullptr && FgEdgePatternCount() == 13);
			if (eimg)
			{
				const uint32_t ebase = (uint32_t)(uintptr_t)eimg;
				CHECK(!FgEdgeCheckPatterns(ebase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr);   // zeros
				CHECK(FgEdgeWritePatterns(b, ebase, ebytes, -1));
				why[0] = 0;
				CHECK(FgEdgeCheckPatterns(ebase, b, why, sizeof(why)) && why[0] == 0);
				for (int k = 0; k < FgEdgePatternCount(); ++k)
				{
					CHECK(FgEdgeWritePatterns(b, ebase, ebytes, k));
					CHECK(!FgEdgeCheckPatterns(ebase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr && strstr(why, VpExeBuildLabel(b)) != nullptr);
				}
				CHECK(FgEdgeWritePatterns(b, ebase, ebytes, -1));
				// the group's call of the A5 register leads elsewhere (a look-alike register): refused
				const uint32_t gsite = ShellExeVa(b, VPX_fg_farm_group) - 0x400000u + ebase;
				*(uint32_t*)(uintptr_t)(gsite + 62) += 0x40;   // ... 89 74 24 18 E8 <rel32 at +62>
				CHECK(!FgEdgeCheckPatterns(ebase, b, why, sizeof(why)) && strstr(why, "at fg_farm_group does not match") != nullptr);
				*(uint32_t*)(uintptr_t)(gsite + 62) -= 0x40;
				CHECK(FgEdgeCheckPatterns(ebase, b, why, sizeof(why)));
				CHECK(ShellExeTestDrop("fg_curve_prev"));
				CHECK(!FgEdgeCheckPatterns(ebase, b, why, sizeof(why)) && strstr(why, "symbol fg_curve_prev is not carried to ") != nullptr);
				FgEdgeWorld ew0 = {};
				CHECK(!FgEdgeWorldFromBase(b, ebase, &ew0) && ew0.end.builder == 0);
				ShellExeTestDrop("");
				FgEdgeWorld ew;
				CHECK(FgEdgeWorldFromBase(b, 0x00570000u, &ew));
				CHECK(ew.end.builder == ShellExeVa(b, VPX_fg_farm_end_listener) + d && ew.end.lock == ShellExeVa(b, VPX_fg_a_lock) + d &&
				      ew.end.mgr == ShellExeVa(b, VPX_qg_channel_index) + d && ew.end.slotOff == 0x30u && ew.end.builderObj == 0);
				CHECK(ew.era.builder == ShellExeVa(b, VPX_fg_farm_era_listener) + d && ew.era.lock == ShellExeVa(b, VPX_fg_lock) + d &&
				      ew.era.mgr == ShellExeVa(b, VPX_pac_qb_index) + d && ew.era.slotOff == 0x188u && ew.era.builderObj == 0);
				if (b == kVpExeBuild4390913)   // the anchor build: the values cities-and-landmarks.md 2.6.2 / 2.6.3 name
					CHECK(ew.end.builder == 0x603000u + d && ew.era.builder == 0x5307A0u + d && ew.end.lock == 0xCBA9F0u + d && ew.era.lock == 0xCBFC70u + d &&
					      ShellExeVa(b, VPX_fg_a5_lists) == 0x1A0E434u && ShellExeVa(b, VPX_fg_b48_lists) == 0x2A1038Cu);
				else   // every build: the lists sit where the registers' list operand says (the channel index + 0x34 / + 0x18C)
					CHECK(ShellExeVa(b, VPX_fg_a5_lists) == ShellExeVa(b, VPX_qg_channel_index) + 0x34u &&
					      ShellExeVa(b, VPX_fg_b48_lists) == ShellExeVa(b, VPX_pac_qb_index) + 0x18Cu);
				printf("farm_guard_test: the edge placement's patterns on %s: done\n", VpExeBuildLabel(b));
				VirtualFree(eimg, 0, MEM_RELEASE);
			}
			// ticket 0784: the tread filter's five patterns (with their call targets) and its world
			const uint32_t tbytes = FgTreadImageBytes(b);
			unsigned char* timg = tbytes ? (unsigned char*)VirtualAlloc(nullptr, tbytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
			CHECK(timg != nullptr && FgTreadPatternCount() == 5);
			if (timg)
			{
				const uint32_t tbase = (uint32_t)(uintptr_t)timg;
				CHECK(!FgTreadCheckPatterns(tbase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr);   // zeros
				CHECK(FgTreadWritePatterns(b, tbase, tbytes, -1));
				why[0] = 0;
				CHECK(FgTreadCheckPatterns(tbase, b, why, sizeof(why)) && why[0] == 0);
				for (int k = 0; k < FgTreadPatternCount(); ++k)
				{
					CHECK(FgTreadWritePatterns(b, tbase, tbytes, k));
					CHECK(!FgTreadCheckPatterns(tbase, b, why, sizeof(why)) && strstr(why, " does not match on ") != nullptr && strstr(why, VpExeBuildLabel(b)) != nullptr);
				}
				CHECK(FgTreadWritePatterns(b, tbase, tbytes, -1));
				// the group's call of the 0x28 register leads elsewhere (a look-alike register of another type): refused
				const uint32_t gsite = ShellExeVa(b, VPX_tf_l28_reg) - 0x400000u + tbase;
				*(uint32_t*)(uintptr_t)(gsite + 13) += 0x60;   // C7 44 24 ?? <4> 89 74 24 ?? E8 <rel32 at +13>
				CHECK(!FgTreadCheckPatterns(tbase, b, why, sizeof(why)) && strstr(why, "at tf_l28_reg does not match") != nullptr);
				*(uint32_t*)(uintptr_t)(gsite + 13) -= 0x60;
				CHECK(FgTreadCheckPatterns(tbase, b, why, sizeof(why)));
				CHECK(ShellExeTestDrop("tf_particle_obj"));
				CHECK(!FgTreadCheckPatterns(tbase, b, why, sizeof(why)) && strstr(why, "symbol tf_particle_obj is not carried to ") != nullptr);
				FgWorld tw0 = {};
				CHECK(!FgTreadWorldFromBase(b, tbase, &tw0) && tw0.builder == 0);
				ShellExeTestDrop("");
				FgWorld tw;
				CHECK(FgTreadWorldFromBase(b, 0x00570000u, &tw));
				CHECK(tw.builder == ShellExeVa(b, VPX_tf_l28_listener) + d && tw.lock == ShellExeVa(b, VPX_fg_a_lock) + d &&
				      tw.lockInit == ShellExeVa(b, VPX_fg_a_lockinit) + d && tw.mgr == ShellExeVa(b, VPX_qg_channel_index) + d &&
				      tw.slotOff == 0x148u && tw.stride == 0x600u && tw.builderObj == ShellExeVa(b, VPX_tf_particle_obj) + d &&
				      tw.trampoline == ShellExeVa(b, VPX_fg_type_handler) + d);
				CHECK(ShellExeVa(b, VPX_tf_a28_lists) == ShellExeVa(b, VPX_qg_channel_index) + 0x14Cu);
				if (b == kVpExeBuild4390913)   // the anchor build: the values farm-builder-guard.md 15.1 names
					CHECK(tw.builder == 0x77DC60u + d && tw.builderObj == 0xC8EB50u + d && tw.lock == 0xCBA9F0u + d);
				printf("farm_guard_test: the tread filter's patterns on %s: done\n", VpExeBuildLabel(b));
				VirtualFree(timg, 0, MEM_RELEASE);
			}
			VirtualFree(pimg, 0, MEM_RELEASE);
		}
	}

	// ---- 2. the free-list check ----------------------------------------------------------------------------------------
	{
		ResetWorld(scratch.c_str());
		FgCheck c;
		FgCheckGraph(g_w.graph, &c);
		CHECK(c.sound == 1 && c.freedByBit == 3 && c.chain == 3 && c.live == 17 && c.size == 20 && c.why[0] == 0);
		memset(g_graph, 0, sizeof(g_graph));                         // empty: sound
		FgCheckGraph(g_w.graph, &c); CHECK(c.sound == 1 && c.size == 0);
		const uint32_t fr[] = { 3, 9, 1 };
		BuildGraph(20, fr, 3); FakeFree(9);                         // 9 freed again: it was neither the head nor after it
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && c.why[0]);
		BuildGraph(20, fr, 3); FakeFree(1);                         // the head freed again: it names itself (0515's cycle)
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "cycle") != nullptr);
		BuildGraph(20, fr, 3); g_eps[4].count = 7;                  // a live count above 4
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && c.over4 == 1 && c.firstOver4 == 4 && strstr(c.why, "more than 4") != nullptr);
		BuildGraph(20, fr, 3); EpHdr()[1] = 16;                     // live + freed != size
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "!= size") != nullptr);
		BuildGraph(20, fr, 3); g_eps[3].count = 0x80000000u | 15;   // the chain runs into live entry 15
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "live entry 15") != nullptr && c.badIndex == 15);
		BuildGraph(20, fr, 3); g_eps[3].count = 0x80000000u | 40;   // past the array
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "past the array") != nullptr && c.badIndex == 40);
		BuildGraph(20, fr, 3); g_eps[12].count |= 0x80000000u; EpHdr()[1] = 16;   // a freed entry not on the chain
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "free list holds 3") != nullptr);
		BuildGraph(20, fr, 3); EpHdr()[4] = 5000000;                // implausible header
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "not plausible") != nullptr);
		BuildGraph(20, fr, 3); EpHdr()[2] = 0;
		FgCheckGraph(g_w.graph, &c); CHECK(!c.sound && strstr(c.why, "not plausible") != nullptr);
		// a fault: the data pointer names reserved, uncommitted memory
		void* res = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
		BuildGraph(20, fr, 3); EpHdr()[2] = (uint32_t)(uintptr_t)res;
		CHECK(!FgCheckGraphSafe(g_w.graph, &c) && !c.sound && strstr(c.why, "faulted") != nullptr);
		VirtualFree(res, 0, MEM_RELEASE);
	}

	// ---- 3. the node search, its negatives ---------------------------------------------------------------------------
	{
		ResetWorld(scratch.c_str());
		FgScan s;
		FgScanList(g_w, &s); CHECK(s.ok && s.n == 0);                // an empty list
		Register(0x1234, 0x5678); const uint32_t bi = Register(OBJ(), BUILDER()); Register(OBJ(), 0x9999);
		FgScanList(g_w, &s); CHECK(s.ok && s.n == 1 && s.idx[0] == bi && s.fn[0] == BUILDER() && s.fnWord[0] == (uintptr_t)&g_nodes[bi].fn);
		Slot(0)[1] = 0; FgScanList(g_w, &s); CHECK(s.ok && s.n == 0 && s.list == 0);   // no list on type 59 yet
		Slot(0)[1] = (uint32_t)(uintptr_t)&g_list;
		*(uint32_t*)g_mgr = 99; FgScanList(g_w, &s); CHECK(!s.ok && strstr(s.why, "channel") != nullptr);
		*(uint32_t*)g_mgr = 0;
		Slot(0)[0] = 0x1111; FgScanList(g_w, &s); CHECK(!s.ok && strstr(s.why, "handler") != nullptr);
		Slot(0)[0] = (uint32_t)(uintptr_t)FakeTrampoline;
		g_list.count = 9; FgScanList(g_w, &s); CHECK(!s.ok && strstr(s.why, "not plausible") != nullptr);
		g_list.count = 3;
		g_nodes[0].next = 7; FgScanList(g_w, &s); CHECK(!s.ok && strstr(s.why, "past the list") != nullptr);
		// through the Present: a bad list is LOUD, counted, and no word is written
		g_logText.clear();
		FarmGuard_OnPresent(Env(1));
		CHECK(St().scanBad == 1 && St().swaps == 0 && g_nodes[bi].fn == BUILDER() && LogHas("LOUD: the listener list of type 59 is not as expected"));
		CHECK(g_lock == 0);                                          // the EXE's lock released
		g_nodes[0].next = bi;
		// the channel index moves the slot: channel 1
		ResetWorld(scratch.c_str());
		*(uint32_t*)g_mgr = 1; Slot(1)[0] = (uint32_t)(uintptr_t)FakeTrampoline; Slot(1)[1] = (uint32_t)(uintptr_t)&g_list;
		Slot(0)[1] = 0;
		Register(OBJ(), BUILDER());
		FgScanList(g_w, &s); CHECK(s.ok && s.n == 1 && s.channel == 1);
	}

	// ---- 4. install, pass-through, the lock -----------------------------------------------------------------------------
	{
		ResetWorld(scratch.c_str());
		const uint32_t other = Register(0x1234, (uint32_t)(uintptr_t)FakeOther);
		const uint32_t bi = Register(OBJ(), BUILDER());
		g_lockInit = 0;                                              // never initialised: nothing done, nothing counted
		FarmGuard_OnPresent(Env(1));
		CHECK(g_nodes[bi].fn == BUILDER() && St().swaps == 0 && St().lockBusy == 0);
		g_lockInit = 1; g_lock = 1;                                  // held by the EXE: this Present does nothing
		FarmGuard_OnPresent(Env(1));
		CHECK(g_nodes[bi].fn == BUILDER() && St().lockBusy == 1 && g_lock == 1);
		g_lock = 0;
		FarmGuard_OnPresent(Env(1));
		CHECK(g_nodes[bi].fn == HOOK() && St().swaps == 1 && g_lock == 0 && g_nodes[other].fn == (uint32_t)(uintptr_t)FakeOther && LogHas("farm_guard: installed"));
		FarmGuard_OnPresent(Env(1));                                 // again: nothing more
		CHECK(St().swaps == 1);
		void* ev = (void*)0x4242;
		Walk(ev);
		CHECK(g_builderCalls == 1 && g_lastObj == (void*)g_objStore && g_lastEv == ev && g_otherCalls == 1);
		CHECK(St().calls == 1 && St().builderCalls == 1 && St().checks == 1 && St().checksBad == 0 && !St().tripped);
		Walk((void*)0x4343);
		CHECK(g_builderCalls == 2 && g_lastEv == (void*)0x4343 && St().checks == 2);
		// MC #5073: the event line of a real payload (dword +0x08, plot x +0x18, y +0x1A, byte +0x1C)
		unsigned char pay[0x40] = {};
		*(uint32_t*)(pay + 0x08) = 0x0000ABCDu; *(int16_t*)(pay + 0x18) = 12; *(int16_t*)(pay + 0x1A) = 34; pay[0x1C] = 1;
		Walk(pay);
		CHECK(g_builderCalls == 3 && g_lastEv == pay);
		// the burst line after 2 s without a call
		Sleep(2100);
		FarmGuard_OnPresent(Env(1));
		CHECK(LogHas("farm_guard: load 1 (grid seq 1, 2 s without a call): builder calls 3 (skipped 0), checks 3: all sound"));
		CHECK(LogHas("; event lines 3"));
		CHECK(!LogHas("LOUD"));
		std::string ev3;
		ReadAllW(scratch + L"\\farm-builder-calls.log", &ev3);
		CHECK(ev3.find("# ticket 0527") == 0);
		CHECK(ev3.find("1 seq=1 built payload not readable\r\n") != std::string::npos);
		CHECK(ev3.find("3 seq=1 built x=12 y=34 d8=0x0000ABCD b1c=1\r\n") != std::string::npos);
		CHECK(LogHas("farm_guard: the builder's events are logged one line per call in"));
	}

	// ---- 5. re-registration: the unregister cannot find the swapped node -------------------------------------------------
	{
		ResetWorld(scratch.c_str());
		const uint32_t a = Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		CHECK(g_nodes[a].fn == HOOK());
		CHECK(!Unregister(OBJ(), BUILDER()));                         // the GameView shutdown: the node stays
		g_viewFlag = 0;                                              // ... and the city-field flag goes to 0
		Walk((void*)1);
		CHECK(g_builderCalls == 0 && St().skipNoView == 1);
		const uint32_t b = Register(OBJ(), BUILDER());               // the next GameView startup
		g_viewFlag = 1;
		Walk((void*)2);                                              // before a Present: the new node calls the builder, the stale one not
		CHECK(g_builderCalls == 1 && St().skipStale == 1 && St().builderCalls == 0);
		FarmGuard_OnPresent(Env(1));
		CHECK(g_nodes[a].fn == INERT() && g_nodes[b].fn == HOOK() && St().retired == 1 && St().swaps == 2 && LogHas("retired"));
		Walk((void*)3);
		CHECK(g_builderCalls == 2 && St().builderCalls == 1 && g_lastEv == (void*)3);
		CHECK(!Unregister(OBJ(), BUILDER()) && g_list.count == 2);   // still listed, both: one inert, one guarded
	}

	// ---- 6. the kill switch --------------------------------------------------------------------------------------------
	{
		ResetWorld(scratch.c_str());
		const uint32_t a = Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		CHECK(g_nodes[a].fn == HOOK());
		FarmGuard_OnPresent(Env(0));
		CHECK(g_nodes[a].fn == BUILDER() && St().restored == 1 && LogHas("farm_guard=0"));
		Walk((void*)5);
		CHECK(g_builderCalls == 1 && St().calls == 0);               // straight to the builder
		CHECK(Unregister(OBJ(), BUILDER()));                          // and the EXE's unregister finds it again
		Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		CHECK(St().swaps == 2);
	}

	// ---- 7. the trip, the record in the run folder of this process ------------------------------------------------------
	{
		wchar_t d[MAX_PATH];
		const std::wstring root = scratch + L"\\out";
		CreateDirectoryW(root.c_str(), nullptr);
		CreateDirectoryW((root + L"\\VP64").c_str(), nullptr);
		CreateDirectoryW((root + L"\\VP64\\run").c_str(), nullptr);
		_snwprintf_s(d, _TRUNCATE, L"%s\\VP64\\run\\20261118-000000-%lu-1", root.c_str(), pid); CreateDirectoryW(d, nullptr);
		_snwprintf_s(d, _TRUNCATE, L"%s\\VP64\\run\\20991231-235959-%lu-1", root.c_str(), pid + 1); CreateDirectoryW(d, nullptr);   // another process
		_snwprintf_s(d, _TRUNCATE, L"%s\\VP64\\run\\20991231-235959-%lu9-1", root.c_str(), pid); CreateDirectoryW(d, nullptr);    // a longer pid
		_snwprintf_s(d, _TRUNCATE, L"%s\\VP64\\run\\20261118-010203-%lu-2", root.c_str(), pid); CreateDirectoryW(d, nullptr);   // the newest of ours
		const std::wstring want = d;
		ResetWorld(root.c_str());
		const uint32_t a = Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		g_doubleFreeOnCall = 2;
		Walk((void*)1);
		CHECK(!St().tripped && g_builderCalls == 1);
		Walk((void*)2);                                              // the builder frees endpoint 7 twice in this call
		CHECK(St().tripped && St().checksBad == 1 && g_builderCalls == 2);
		CHECK(LogHas("farm_guard: LOUD: the EXE's farm-field layout graph failed the free-list check after builder call 2"));
		CHECK(LogHas("is not called again in this process"));
		std::string rec;
		ReadAllW(want + L"\\farm-guard.log", &rec);
		CHECK(rec.find("LOUD") != std::string::npos && rec.find("not called again") != std::string::npos && rec.find("\r\n") != std::string::npos);
		std::string other;
		_snwprintf_s(d, _TRUNCATE, L"%s\\VP64\\run\\20991231-235959-%lu-1\\farm-guard.log", root.c_str(), pid + 1);
		CHECK(ReadAllW(d, &other).empty());
		_snwprintf_s(d, _TRUNCATE, L"%s\\VP64\\run\\20991231-235959-%lu9-1\\farm-guard.log", root.c_str(), pid);
		CHECK(ReadAllW(d, &other).empty());
		CHECK(!LogHas("run folder of this process was not found"));
		Walk((void*)3);                                              // no further call reaches the builder
		CHECK(g_builderCalls == 2 && St().skipTripped == 1);
		std::string evr;
		ReadAllW(want + L"\\farm-builder-calls.log", &evr);          // flushed at the trip; the skipped call after it too
		CHECK(evr.find("2 seq=1 built payload not readable") != std::string::npos);
		FarmGuard_OnPresent(Env(0));                                 // farm_guard=0 after a trip: the guard stays
		CHECK(g_nodes[a].fn == HOOK() && St().restored == 0 && LogHas("ignored: the guard tripped"));
		Walk((void*)4);
		CHECK(g_builderCalls == 2);
		Sleep(2100);
		FarmGuard_OnPresent(Env(1));
		CHECK(LogHas("BAD (the guard tripped"));
	}

	// ---- 8. no run folder of this process: the record beside the log, LOUD ----------------------------------------------
	{
		const std::wstring root = scratch + L"\\norun";
		CreateDirectoryW(root.c_str(), nullptr);
		ResetWorld(root.c_str());
		Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		g_doubleFreeOnCall = 1;
		Walk((void*)1);
		CHECK(St().tripped);
		CHECK(LogHas("LOUD: the proxy's run folder of this process was not found"));
		std::string rec;
		ReadAllW(root + L"\\farm-guard.log", &rec);
		CHECK(rec.find("not called again") != std::string::npos);
	}

	// ---- 9. a list that cannot be read in the hook: the builder is called as the EXE would --------------------------------
	{
		ResetWorld(scratch.c_str());
		Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		void* res = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
		const uint32_t keep = Slot(0)[1];
		Slot(0)[1] = (uint32_t)(uintptr_t)res;                       // the list pointer names unmapped memory
		Node n0 = g_nodes[0];
		((FgListenerFn)(uintptr_t)n0.fn)((void*)(uintptr_t)n0.obj, nullptr, (void*)9);
		CHECK(g_builderCalls == 1 && St().skipStale == 0);
		FarmGuard_OnPresent(Env(1));                                 // the Present: a fault in the scan, LOUD, no write
		CHECK(St().scanBad == 1 && g_lock == 0);
		Slot(0)[1] = keep;
		VirtualFree(res, 0, MEM_RELEASE);
	}

	// ---- 10. the in-game line of every load: the check at a Present, also with no builder call; a bad graph trips ------
	{
		ResetWorld(scratch.c_str());
		FarmGuard_TestTimes(50, 120);
		Register(OBJ(), BUILDER());
		FarmGuardEnv e = Env(1); e.gridSeq = 5;
		FarmGuard_OnPresent(e);                                      // the swap; the map of seq 5 came up now
		CHECK(!LogHas("in game (grid seq 5)"));
		Sleep(200);
		FarmGuard_OnPresent(e);
		CHECK(LogHas("farm_guard: in game (grid seq 5): builder calls through the guard 0 since the last in-game line (0 in this process, skipped 0); the check at this Present: sound"));
		FarmGuard_OnPresent(e);                                      // once per load
		CHECK(g_logText.find("in game (grid seq 5)") == g_logText.rfind("in game (grid seq 5)"));
		Walk((void*)1); Walk((void*)2);
		const uint32_t fr[] = { 3, 9, 1 };
		BuildGraph(20, fr, 3); FakeFree(1);                          // damage outside a builder call (the head freed again)
		e.gridSeq = 6;
		FarmGuard_OnPresent(e);
		Sleep(200);
		FarmGuard_OnPresent(e);
		CHECK(LogHas("in game (grid seq 6): builder calls through the guard 2 since the last in-game line (2 in this process"));
		CHECK(LogHas("the check at this Present: BAD: the free list has a cycle"));
		CHECK(LogHas("farm_guard: LOUD: the EXE's farm-field layout graph failed the free-list check at the in-game check of grid seq 6"));
		CHECK(St().tripped && St().checksBad == 1);
		const int before = g_builderCalls;
		Walk((void*)3);
		CHECK(g_builderCalls == before && St().skipTripped == 1);
		e.gridSeq = 7;                                               // a later load after the trip: logged, no second trip
		FarmGuard_OnPresent(e); Sleep(200); FarmGuard_OnPresent(e);
		CHECK(LogHas("in game (grid seq 7)") && LogHas("(the guard tripped earlier: no builder call)"));
		CHECK(St().checksBad == 1);
		e.gridValid = 0; e.gridSeq = 8;                              // no map: no line
		FarmGuard_OnPresent(e); Sleep(200); FarmGuard_OnPresent(e);
		CHECK(!LogHas("in game (grid seq 8)"));
	}

	// ---- 11. ticket 4853: farm_builder=0 (the server lays out the farm fields): the hook never calls the builder --------
	{
		ResetWorld(scratch.c_str());
		FarmGuard_TestTimes(50, 120);
		const uint32_t other = Register(0x1234, (uint32_t)(uintptr_t)FakeOther);
		const uint32_t a = Register(OBJ(), BUILDER());
		FarmGuardEnv e = Env(1); e.gridSeq = 3; e.skipBuilder = 1;
		FarmGuard_OnPresent(e);                                      // the swap, with the skip in force before any call
		CHECK(g_nodes[a].fn == HOOK() && St().swaps == 1 && LogHas("farm_guard: farm_builder=0: the EXE's farm builder is not called"));
		unsigned char pay[0x40] = {};
		*(uint32_t*)(pay + 0x08) = 0x27u; *(int16_t*)(pay + 0x18) = 7; *(int16_t*)(pay + 0x1A) = 9; pay[0x1C] = 1;
		const int othersBefore = g_otherCalls;
		Walk(pay); Walk((void*)0x4545); Walk(pay);
		CHECK(g_builderCalls == 0 && St().builderCalls == 0 && St().calls == 3 && St().skipServer == 3 && St().checks == 0);
		CHECK(g_otherCalls == othersBefore + 3);                     // the other listeners of type 59 are called as before
		Sleep(200);
		FarmGuard_OnPresent(e);                                      // the burst line and the in-game line
		CHECK(LogHas("builder calls 3 (skipped 3), checks 0: none run"));
		CHECK(LogHas("; not called for farm_builder=0 3"));
		CHECK(LogHas("in game (grid seq 3): builder calls through the guard 0 since the last in-game line (0 in this process, skipped 3); the check at this Present: sound"));
		CHECK(LogHas("not called for farm_builder=0: 3 in this process"));
		std::string evs;
		ReadAllW(scratch + L"\\farm-builder-calls.log", &evs);
		// ticket 0655: this world carries no poster: the post is off (counted refused), the event line says so
		CHECK(evs.find("1 seq=3 skipped-server-post-off-for-build x=7 y=9 d8=0x00000027 b1c=1\r\n") != std::string::npos);
		CHECK(evs.find("2 seq=3 skipped-server-post-off-for-build payload not readable\r\n") != std::string::npos);
		CHECK(St().posts == 0 && St().postRefused == 3 && St().postReady == 0);
		// farm_guard=0 while farm_builder=0: the node keeps the hook, the builder stays uncalled
		FarmGuardEnv off = e; off.enabled = 0;
		FarmGuard_OnPresent(off);
		CHECK(g_nodes[a].fn == HOOK() && St().restored == 0 && LogHas("farm_guard=0 ignored while farm_builder=0"));
		Walk(pay);
		CHECK(g_builderCalls == 0 && St().skipServer == 4);
		// a new registration while the skip holds: the new node gets the hook too, the old one goes inert
		CHECK(!Unregister(OBJ(), BUILDER()));
		const uint32_t b = Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(e);
		CHECK(g_nodes[a].fn == INERT() && g_nodes[b].fn == HOOK() && St().swaps == 2);
		Walk(pay);
		CHECK(g_builderCalls == 0 && St().skipServer == 5);
		// farm_builder=1 again (the ini changed): calls reach the builder and are checked, as 0527
		FarmGuardEnv on = e; on.skipBuilder = 0;
		FarmGuard_OnPresent(on);
		CHECK(LogHas("farm_guard: farm_builder=1: the EXE's farm builder is called again"));
		Walk(pay);
		CHECK(g_builderCalls == 1 && St().builderCalls == 1 && St().checks == 1 && St().skipServer == 5);
		// farm_builder=0 with farm_guard=0 from the start: the builder's own node is swapped all the same
		ResetWorld(scratch.c_str());
		const uint32_t c = Register(OBJ(), BUILDER());
		FarmGuardEnv off2 = Env(0); off2.skipBuilder = 1;
		FarmGuard_OnPresent(off2);
		CHECK(g_nodes[c].fn == HOOK() && St().swaps == 1);
		Walk(pay);
		CHECK(g_builderCalls == 0 && St().skipServer == 1);
		// an older composite's env (skipBuilder 0, the field's default): exactly 0527's behaviour
		ResetWorld(scratch.c_str());
		const uint32_t d = Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(Env(1));
		Walk(pay);
		CHECK(g_nodes[d].fn == HOOK() && g_builderCalls == 1 && St().skipServer == 0 && !LogHas("farm_builder"));
		(void)other;
	}

	// ---- 12. ticket 4854 (M3): the graph line's counts on hand-built graphs ---------------------------------------------
	{
		FgGraphStat g;
		char t[2000];
		// A. the small sound graph
		FgSmall();
		CHECK(FgGraphScanSafe(FG(), VTX(), &g) && g.ok);
		CHECK(g.nx == 4 && g.ny == 3 && g.cells == 12 && g.nodeLive == 5 && g.nodeSize == 6 && g.nodeCap == 6 && g.nodeFreedBit == 1);
		CHECK(g.epLive == 2 && g.epSize == 3 && g.epCap == 3 && g.epFreed == 1);
		CHECK(g.listsWalked == 12 && g.listed == 4 && g.listsBad == 0 && g.firstBadCell == 0xFFFFFFFFu);
		CHECK(g.listedNoPoints == 0 && g.listedFreed == 0 && g.idMismatch == 0 && g.heldAbove == 0 && g.onTwoLists == 0);
		CHECK(g.unlisted == 1 && g.unlistedNamed == 0);                              // node 4 (node 5 is freed)
		CHECK(g.idNoneByKind[0] == 0 && g.idNoneByKind[1] == 1 && g.idNoneByKind[2] == 0 && g.idNoneByKind[3] == 0);
		CHECK(g.c65535 == 0 && g.c65535Count == 0 && g.c65535Shared == 0);
		CHECK(g.hNeverFilled == 0 && g.hNeverFilledKind0 == 0 && g.hPast == 0 && g.hNotBack == 0 && g.epCountOver4 == 0);
		CHECK(g.vtxOk == 1 && g.vtxSize == 1234 && g.vtxCap == 4096);
		FgGraphLineText(g, "test A", t, sizeof(t));
		CHECK(strstr(t, "farm_guard: graph (test A): grid 4 x 3 = 12 cells (within 65,535); nodes live 5, size 6, capacity 6, freed bit 1 (live + freed = size); endpoints live 2, size 3, capacity 3, freed 1; ids past 65,535: nodes no, endpoints no; cell lists walked 12, nodes listed 4, lists not sound 0;") == t);
		CHECK(strstr(t, "cell id 0xFFFF on live nodes by kind 0/1/2/other 0/1/0/0; cell 65,535: none (the grid fits 16-bit ids); handles: never-filled slot 0 (kind 0: 0), past the node array 0, not named back 0; endpoints counting above 4 0; vertex vector size 1234, capacity 4096; scan ") != nullptr);
		CHECK(!strstr(t, "LOUD") && !strstr(t, "NOT COMPLETE"));
		// the guard's own F2 check finds the fake's endpoint free list sound (used through the Present below)
		FgCheck fc; FgCheckGraph(FG(), &fc); CHECK(fc.sound == 1);

		// B. damage that leaves the lists sound: a listed node with no points, a listed node with the freed bit, a wrong
		//    +0x34, a never-filled slot of kind 0 and one of kind 3, a handle past the node array, a handle not named back
		FgSmall();
		NPts(1) = 0;                                   // listed in cell 2, no points
		NPrev(2) |= 0x80000000u;                       // listed in cell 5 with the freed bit (its link stays none)
		NId(3) = 7;                                    // in cell 11, +0x34 says 7
		EpH(1, 1) = 0x1FFFFFFFu;                       // endpoint 1's slot 1: never filled, kind bits 0 (0x77C6DA's case)
		EpH(0, 1) = 0x7FFFFFFFu;                       // endpoint 0's slot 1: never filled, kind bits 3
		EpH(1, 0) = 50u;                               // endpoint 1's slot 0: node 50 of 6
		NEp(0, 0) = 2;                                 // node 0's +0x14 names endpoint 2, not 0 (endpoint 0's slot 0 points at it)
		CHECK(FgGraphScanSafe(FG(), VTX(), &g) && g.ok);
		CHECK(g.listsBad == 0 && g.listed == 4 && g.listedNoPoints == 1 && g.listedFreed == 1 && g.idMismatch == 1 && g.nodeFreedBit == 2);
		CHECK(g.hNeverFilled == 2 && g.hNeverFilledKind0 == 1 && g.hPast == 1 && g.hNotBack == 1);
		CHECK(g.unlisted == 1 && g.idNoneByKind[1] == 1);
		FgGraphLineText(g, "test B", t, sizeof(t));
		CHECK(strstr(t, "farm_guard: LOUD: graph (test B)") == t);
		CHECK(strstr(t, "listed with no points 1, with the freed bit 1; +0x34 not the holding cell's low 16 bits 1; held by a cell above 65,535 0; on two lists 0;") != nullptr);
		CHECK(strstr(t, "handles: never-filled slot 2 (kind 0: 1), past the node array 1, not named back 1;") != nullptr);
		CHECK(strstr(t, "(live + freed != size)") != nullptr);   // header live 5, freed bit now 2, size 6

		// C. broken lists, one fault each (cell 2 holds nodes 0 -> 1)
		struct { const char* name; int what; const char* why; uint32_t cell; } lc[] = {
			{ "ring", 0, "a ring: node 0x0 met again after 2", 2 },
			{ "count high", 1, "count 3, walked 2", 2 },
			{ "count low", 2, "more nodes than its count 1", 2 },
			{ "past", 3, "node 0x28 past the node array after 2", 2 },
			{ "back link", 4, "node 0x1 links back to 0x5, not 0x0", 2 },
			{ "last word", 5, "last word 0x0, the walk ended at 0x1", 2 },
			{ "two lists", 6, "more nodes than its count 1", 3 },
		};
		for (const auto& k : lc)
		{
			FgSmall();
			switch (k.what)
			{
			case 0: NNext(1) = 0; break;
			case 1: Cnt(2) = 3; break;
			case 2: Cnt(2) = 1; break;
			case 3: NNext(1) = 40; break;
			case 4: NPrev(1) = 5; break;
			case 5: Last(2) = 0; break;
			case 6: First(3) = 0; Last(3) = 0; Cnt(3) = 1; break;   // cell 3 names node 0 too (which runs on to node 1)
			}
			CHECK(FgGraphScanSafe(FG(), VTX(), &g) && g.ok);
			const bool one = g.listsBad == 1 && g.firstBadCell == k.cell && strcmp(g.firstBadWhy, k.why) == 0;
			CHECK(one);
			if (!one) printf("  case %s: lists bad %u, first cell %u: '%s'\n", k.name, g.listsBad, g.firstBadCell, g.firstBadWhy);
			if (k.what == 6) CHECK(g.onTwoLists == 1 && g.idMismatch == 1 && g.listed == 5);   // node 0 in cell 3 with id 2
			FgGraphLineText(g, k.name, t, sizeof(t));
			char want[128]; _snprintf_s(want, _TRUNCATE, "lists not sound 1 (first: cell %u: %s);", k.cell, k.why);
			CHECK(strstr(t, "farm_guard: LOUD: graph (") == t && strstr(t, want) != nullptr);
		}

		// D. an oversize grid: 300 x 219 = 65,700 cells; cell 65,535 = column 299, row 54 (299 x 219 + 54)
		FgInit(300, 219, 6, 0);
		FgFile(0xFFFF, 0); NKind(0) = 2;              // node 0 in cell 65,535: its id is 0xFFFF ("none")
		FgFile(65537, 1);                             // a cell above 65,535 (id 1)
		FgFile(65600, 3); NId(3) = 65;                // above 65,535, and +0x34 not its low 16 bits (64)
		FgFile(1, 2);
		CHECK(FgGraphScanSafe(FG(), VTX(), &g) && g.ok);
		CHECK(g.cells == 65700 && g.listsWalked == 65700 && g.listed == 4 && g.listsBad == 0);
		CHECK(g.heldAbove == 2 && g.idMismatch == 1 && g.onTwoLists == 0);
		CHECK(g.c65535 == 1 && g.c65535Count == 1 && g.c65535Walked == 1 && g.c65535Shared == 0);
		CHECK(g.idNoneByKind[0] == 0 && g.idNoneByKind[1] == 2 && g.idNoneByKind[2] == 1 && g.idNoneByKind[3] == 0);   // nodes 4, 5 unfiled kind 1; node 0 kind 2
		CHECK(g.unlisted == 2 && g.unlistedNamed == 0);
		FgGraphLineText(g, "test D", t, sizeof(t));
		CHECK(strstr(t, "grid 300 x 219 = 65700 cells (ABOVE 65,535)") != nullptr && !strstr(t, "LOUD"));
		CHECK(strstr(t, "cell 65,535: count 1, walked 1, sound, also on another list 0;") != nullptr);
		CHECK(strstr(t, "held by a cell above 65,535 2;") != nullptr);
		// D2. 0538's ring: node 0, still listed in cell 65,535 with the id 0xFFFF, is inserted at its front again
		NNext(0) = First(0xFFFF); NPrev(0) = NIL; First(0xFFFF) = 0; Cnt(0xFFFF)++;
		CHECK(FgGraphScanSafe(FG(), VTX(), &g) && g.ok);
		CHECK(g.c65535 == -1 && g.c65535Count == 2 && g.c65535Walked == 1 && strcmp(g.c65535Why, "a ring: node 0x0 met again after 1") == 0);
		CHECK(g.listsBad == 1 && g.firstBadCell == 0xFFFF);
		FgGraphLineText(g, "test D2", t, sizeof(t));
		CHECK(strstr(t, "farm_guard: LOUD: graph (test D2)") == t && strstr(t, "cell 65,535: count 2, walked 1, NOT SOUND: a ring: node 0x0 met again after 1, also on another list 0;") != nullptr);
		// D3. 0538's second form: node 0 (in cell 65,535) inserted at the front of cell 65,534 as well
		NNext(0) = NIL; Cnt(0xFFFF) = 1;
		First(0xFFFE) = 0; Last(0xFFFE) = 0; Cnt(0xFFFE) = 1;
		CHECK(FgGraphScanSafe(FG(), VTX(), &g) && g.ok);
		CHECK(g.listsBad == 0 && g.onTwoLists == 1 && g.c65535 == 1 && g.c65535Shared == 1 && g.idMismatch == 2 && g.listed == 5);
		FgGraphLineText(g, "test D3", t, sizeof(t));
		CHECK(strstr(t, "cell 65,535: count 1, walked 1, sound, also on another list 1;") != nullptr);

		// E. an empty graph (nothing laid out, no grid yet), an implausible one, a fault, endpoint and node ids past 65,535
		FgGraphStat e;
		memset(g_fg.hdr, 0, sizeof(g_fg.hdr));
		CHECK(FgGraphScanSafe(FG(), 0, &e) && e.ok && e.cells == 0 && e.listed == 0 && e.vtxOk == 0);
		FgGraphLineText(e, "test E", t, sizeof(t));
		CHECK(strstr(t, "grid 0 x 0 = 0 cells (within 65,535)") != nullptr && strstr(t, "vertex vector not read;") != nullptr && !strstr(t, "LOUD"));
		FgSmall(); H32(0xCC) = 5000000;                // node capacity
		CHECK(FgGraphScanSafe(FG(), VTX(), &e) && !e.ok && strstr(e.why, "node array not plausible") != nullptr);
		FgGraphLineText(e, "test E2", t, sizeof(t));
		CHECK(strstr(t, "; NOT COMPLETE: node array not plausible") != nullptr);
		FgSmall(); H32(0x88) = 2000000;                // 4 x 2,000,000 cells
		CHECK(FgGraphScanSafe(FG(), VTX(), &e) && !e.ok && strstr(e.why, "grid not plausible") != nullptr);
		FgSmall();
		void* res = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
		H32(0x8C) = (uint32_t)(uintptr_t)res;          // the cell array names reserved memory: the walk faults
		CHECK(!FgGraphScanSafe(FG(), VTX(), &e) && !e.ok && strstr(e.why, "faulted") != nullptr && e.nodeFreedBit == 1 && e.vtxOk == 1 && e.vtxSize == 1234);
		CHECK(!FgGraphScanSafe((uintptr_t)res, 0, &e) && strstr(e.why, "header could not be read") != nullptr);
		VirtualFree(res, 0, MEM_RELEASE);
		FgInit(1, 1, 65536, 65537);                   // 65,536 nodes and 65,537 endpoints: ids past 65,535
		CHECK(FgGraphScanSafe(FG(), VTX(), &e) && e.ok && e.unlisted == 65536 && e.idNoneByKind[1] == 65536);
		FgGraphLineText(e, "test E5", t, sizeof(t));
		CHECK(strstr(t, "ids past 65,535: nodes YES, endpoints YES;") != nullptr);
		// endpoint 70,000's handle on a node whose 16-bit id cannot name it; a count above 4
		FgInit(1, 1, 2, 70001);
		EpC(70000) = 1; EpH(70000, 0) = 1; NEp(1, 0) = (uint16_t)70000;
		EpC(3) = 6; EpH(3, 0) = 0; EpH(3, 1) = 0; EpH(3, 2) = 0; EpH(3, 3) = 0;
		CHECK(FgGraphScanSafe(FG(), VTX(), &e) && e.ok && e.hNotBack == 5 && e.epCountOver4 == 1);   // 1 + endpoint 3's four (node 0 names 0xFFFF)

		// F. through the Present: the graph line after the in-game line and after a burst, with farm_builder 1 and 0
		for (int skip = 0; skip < 2; ++skip)
		{
			ResetWorld(scratch.c_str());
			FgSmall();
			g_w.graph = FG(); g_w.vtxVec = VTX();
			FarmGuard_TestUse(&g_w, scratch.c_str());
			FarmGuard_TestTimes(50, 120);
			Register(OBJ(), BUILDER());
			FarmGuardEnv ev = Env(1); ev.gridSeq = 9; ev.skipBuilder = skip;
			FarmGuard_OnPresent(ev);
			Walk((void*)1); Walk((void*)2);
			Sleep(200);
			FarmGuard_OnPresent(ev);
			// this Present: the load's burst line and its graph line, then the in-game line and its graph line
			CHECK(LogHas("farm_guard: graph (after load 1, grid seq 9): grid 4 x 3 = 12 cells (within 65,535); nodes live 5, size 6"));
			CHECK(LogHas("farm_guard: graph (in game, grid seq 9): grid 4 x 3 = 12 cells (within 65,535); nodes live 5, size 6"));
			CHECK(g_logText.find("graph (in game, grid seq 9)") > g_logText.find("farm_guard: in game (grid seq 9)"));
			CHECK(St().graphLines == 2 && St().graphFaults == 0 && !LogHas("LOUD"));
			CHECK(skip ? St().skipServer == 2 && g_builderCalls == 0 : St().builderCalls == 2 && St().checks == 2 && !St().tripped);
			FarmGuard_OnPresent(ev);                   // the in-game line once per load: no further graph line
			CHECK(St().graphLines == 2);
			Walk((void*)3);                            // a burst in play: its burst line, then a graph line
			Sleep(100);
			FarmGuard_OnPresent(ev);
			CHECK(LogHas("farm_guard: graph (after load 2, grid seq 9): grid 4 x 3 = 12 cells") && St().graphLines == 3);
			// the counts move with the graph: a listed node loses its points between two lines; nothing trips
			NPts(3) = 0;
			Walk((void*)4); Sleep(100); FarmGuard_OnPresent(ev);
			CHECK(LogHas("farm_guard: LOUD: graph (after load 3, grid seq 9)") && LogHas("listed with no points 1,") && !St().tripped);
			CHECK(St().graphLines == 4);
		}
	}

	// ---- 13. ticket 4855: rule 2 (hand-computed cases: farm-fields-layout.md 15 and the 4855 issue log) --------------------
	{
		// the grid and plot frame of 9074's map: 180 x 112 plots, 360 x 224 cells over 11,519 x 6,428.3 from (-5759.5,
		// -3214.2); plots: x = 64 hx + 32 hy + 32 - 5760, y = (64 hy + 128) x 0.866 - 3214.2
		auto map9074 = []() { FgInit(360, 224, 4, 0); FgGeo(11519.0f, 6428.3f, -5759.5f, -3214.2f); FgMat(32.0f, 128.0f, -5760.0f, -3214.2f); };
		// a map whose cell 65,535 is in the last column, beside the x seam: 164 x 100 plots, 328 x 200 cells
		auto mapSeam = []() { FgInit(328, 200, 4, 0); FgGeo(10495.0f, 5764.096f, -5247.5f, -2882.048f); FgMat(32.0f, 128.0f, -5248.0f, -2882.048f); };
		// a map whose cell 65,535 is the last cell, in the margin above the last row: 128 x 128 plots, 256 x 256 cells
		auto mapEdge = []() { FgInit(256, 256, 4, 0); FgGeo(8191.0f, 7315.968f, -4095.5f, -3657.984f); FgMat(32.0f, 128.0f, -4096.0f, -3657.984f); };
		const uintptr_t H2W = (uintptr_t)g_h2w, AABB = (uintptr_t)g_aabb;
		FgRule2Geo r;

		// A. the geometry: 9074's map, no coastline near cell 65,535: column 292, row 127, plot (146, 64) = hex (114, 64);
		//    9 plots can reach the cell (hex 113-115 in rows 63 and 64, 112-114 in row 65), radius 2
		map9074(); NKind(0) = 0; float* b0 = (float*)&g_fg.nodes[0x18]; b0[0] = 0; b0[1] = 0; b0[2] = 10; b0[3] = 10;   // a kind-0 node far away
		FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(r.ok == 1 && r.cells == 80640 && r.col == 292 && r.row == 127 && r.mapW == 180);
		CHECK(r.px == 146 && r.py == 64 && r.phx == 114 && r.phy == 64 && r.reach == 9 && r.reachFar == 0);
		CHECK(fabsf(r.cx - 3599.69f) < 0.05f && fabsf(r.cy - 444.77f) < 0.05f && fabsf(r.pdist - 16.34f) < 0.05f);
		CHECK(r.kind0 == 1 && r.kind0Touch == 0 && r.radius == 2);
		// A2. a coastline piece whose box meets the cell (x 3583.7-3615.7, y 430.4-459.1): radius 4
		NKind(1) = 0; float* b1 = (float*)&g_fg.nodes[56 + 0x18]; b1[0] = 3590; b1[1] = 440; b1[2] = 3600; b1[3] = 450;
		FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(r.ok == 1 && r.kind0 == 2 && r.kind0Touch == 1 && r.radius == 4);
		NPrev(1) |= 0x80000000u;                                    // freed: not counted
		FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(r.ok == 1 && r.kind0 == 1 && r.kind0Touch == 0 && r.radius == 2);
		// B. the seam map: column 327, row 135, plot (163, 68) = hex (129, 68), 6 plots reach the cell; a coastline piece at
		//    the map's left edge (x -5250..-5240) meets the cell's image one world width over: radius 4
		mapSeam();
		FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(r.ok == 1 && r.cells == 65600 && r.col == 327 && r.row == 135 && r.mapW == 164);
		CHECK(r.px == 163 && r.py == 68 && r.phx == 129 && r.phy == 68 && r.reach == 6 && r.reachFar == 0 && r.radius == 2);
		NKind(0) = 0; b0 = (float*)&g_fg.nodes[0x18]; b0[0] = -5250; b0[1] = 1015; b0[2] = -5240; b0[3] = 1030;
		FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(r.ok == 1 && r.kind0Touch == 1 && r.radius == 4);
		// C. the edge map: the last cell (column 255, row 255), in the margin above row 127: plot (127, 130), off the map
		mapEdge();
		FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(r.ok == 1 && r.cells == 65536 && r.col == 255 && r.row == 255 && r.px == 127 && r.py == 130 && r.reachFar == 0);
		// D. refusals: no cell 65,535 (255 x 257 = 65,535 cells), not a hex lattice, a world width that does not fit, plots
		//    past distance 2 reaching the cell (cells ten times higher)
		FgInit(255, 257, 2, 0); FgRule2Geometry(FG(), H2W, AABB, &r); CHECK(!r.ok && strstr(r.why, "no cell 65,535") != nullptr);
		map9074(); g_h2w[4] = 0.0f; FgRule2Geometry(FG(), H2W, AABB, &r); CHECK(!r.ok && strstr(r.why, "not the hex lattice") != nullptr);
		map9074(); FgGeo(15000.0f, 6428.3f, -5759.5f, -3214.2f); FgRule2Geometry(FG(), H2W, AABB, &r); CHECK(!r.ok && strstr(r.why, "map width") != nullptr);
		map9074(); FgGeo(11519.0f, 64283.0f, -5759.5f, -32141.5f); FgRule2Geometry(FG(), H2W, AABB, &r);
		CHECK(!r.ok && strstr(r.why, "past hex distance 2 can reach") != nullptr && r.reachFar > 0);

		// E. the hex distance with the x wrap (hand-computed; hex x = grid x - (y - (y & 1)) / 2)
		CHECK(FgHexDistWrap(116, 64, 114, 64, 180) == 2 && FgHexDistWrap(117, 64, 114, 64, 180) == 3);
		CHECK(FgHexDistWrap(116, 63, 114, 64, 180) == 2 && FgHexDistWrap(117, 63, 114, 64, 180) == 3 && FgHexDistWrap(112, 63, 114, 64, 180) == 3);
		CHECK(FgHexDistWrap(-34, 68, 129, 68, 164) == 1 && FgHexDistWrap(-33, 68, 129, 68, 164) == 2 && FgHexDistWrap(-32, 68, 129, 68, 164) == 3);
		CHECK(FgHexDistWrap(-33, 66, 129, 68, 164) == 2 && FgHexDistWrap(-32, 66, 129, 68, 164) == 3 && FgHexDistWrap(-35, 70, 129, 68, 164) == 0 + 2);
		CHECK(FgHexDistWrap(-34, 68, 129, 68, 0) == 163);           // no wrap

		// F. through the hook, every plot of the map once (farm_builder=1): exactly the expected calls skipped
		struct Plot { int gx, gy; bool skip; };
		auto run = [&](void (*mk)(), int W, int Hh, const Plot* hand, int nHand, int wantSkip, int radius, int kindTouch) {
			ResetWorld(scratch.c_str());
			mk();
			if (kindTouch == 1) { NKind(0) = 0; float* b = (float*)&g_fg.nodes[0x18]; b[0] = 3590; b[1] = 440; b[2] = 3600; b[3] = 450; }
			if (kindTouch == 2) { NKind(0) = 0; float* b = (float*)&g_fg.nodes[0x18]; b[0] = -5250; b[1] = 1015; b[2] = -5240; b[3] = 1030; }
			g_w.graph = FG(); g_w.h2w = H2W; g_w.aabbMin = AABB; g_w.vtxVec = 0;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			FarmGuard_TestTimes(50, 120);
			Register(OBJ(), BUILDER());
			FarmGuard_OnPresent(Env(1));
			for (int k = 0; k < nHand; ++k)
			{
				const int before = g_builderCalls;
				Walk(Pay(hand[k].gx, hand[k].gy));
				const bool skipped = g_builderCalls == before;
				CHECK(skipped == hand[k].skip);
				if (skipped != hand[k].skip) printf("  plot (%d, %d): skipped %d, expected %d\n", hand[k].gx, hand[k].gy, (int)skipped, (int)hand[k].skip);
			}
			ResetWorld(scratch.c_str());   // again from nothing for the whole-map count
			mk();
			if (kindTouch == 1) { NKind(0) = 0; float* b = (float*)&g_fg.nodes[0x18]; b[0] = 3590; b[1] = 440; b[2] = 3600; b[3] = 450; }
			if (kindTouch == 2) { NKind(0) = 0; float* b = (float*)&g_fg.nodes[0x18]; b[0] = -5250; b[1] = 1015; b[2] = -5240; b[3] = 1030; }
			g_w.graph = FG(); g_w.h2w = H2W; g_w.aabbMin = AABB; g_w.vtxVec = 0;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			FarmGuard_TestTimes(50, 120);
			Register(OBJ(), BUILDER());
			FarmGuard_OnPresent(Env(1));
			for (int gy = 0; gy < Hh; ++gy)
				for (int gx = 0; gx < W; ++gx) Walk(Pay(gx, gy));
			const FgStats s = St();
			CHECK((int)s.skipRule2Near == wantSkip && (int)s.builderCalls == W * Hh - wantSkip && g_builderCalls == W * Hh - wantSkip);
			CHECK(s.skipRule2Stop == 0 && s.skipRule2Geo == 0 && s.rule2Grids == 1 && !s.rule2Stopped);
			char want[96]; _snprintf_s(want, _TRUNCATE, "radius %d: the builder is not called for the plots within hex distance %d", radius, radius);
			CHECK(LogHas(want));
			if (!((int)s.skipRule2Near == wantSkip)) printf("  whole map %d x %d: skipped %u, expected %d\n", W, Hh, s.skipRule2Near, wantSkip);
		};
		const Plot h9074[] = {
			{ 146, 64, true }, { 148, 64, true }, { 144, 64, true }, { 149, 64, false }, { 143, 64, false },
			{ 147, 63, true }, { 144, 63, true }, { 148, 63, false }, { 143, 63, false },
			{ 145, 62, true }, { 146, 62, true }, { 147, 62, true }, { 144, 62, false }, { 148, 62, false },
			{ 145, 66, true }, { 146, 66, true }, { 147, 66, true }, { 148, 66, false }, { 144, 66, false },
			{ 144, 65, true }, { 147, 65, true }, { 148, 65, false }, { 146, 67, false }, { 146, 61, false }, { 0, 0, false } };
		run(map9074, 180, 112, h9074, 25, 19, 2, 0);
		// radius 4 (a coastline piece meets the cell): 61 plots; (150, 64) and (146, 68) at distance 4 skipped, (151, 64) not
		const Plot h9074r4[] = { { 150, 64, true }, { 151, 64, false }, { 146, 68, true }, { 146, 69, false }, { 149, 63, true }, { 148, 60, true } };
		run(map9074, 180, 112, h9074r4, 6, 61, 4, 1);
		// the seam: the plots across the seam (grid x 0 and 1) within distance 2 are skipped
		const Plot hSeam[] = {
			{ 163, 68, true }, { 161, 68, true }, { 0, 68, true }, { 1, 68, true }, { 2, 68, false }, { 160, 68, false },
			{ 0, 67, true }, { 1, 67, false }, { 161, 67, true }, { 160, 67, false },
			{ 0, 69, true }, { 1, 69, false }, { 161, 69, true },
			{ 0, 66, true }, { 1, 66, false }, { 162, 66, true }, { 161, 66, false },
			{ 0, 70, true }, { 162, 70, true }, { 161, 70, false }, { 163, 71, false } };
		run(mapSeam, 164, 100, hSeam, 21, 19, 2, 0);
		run(mapSeam, 164, 100, hSeam, 0, 61, 4, 2);
		// the edge: the cell lies above the last row; its plot (127, 130) is off the map and no on-map plot is within 2
		const Plot hEdge[] = { { 127, 127, false }, { 0, 127, false }, { 126, 127, false } };
		run(mapEdge, 128, 128, hEdge, 3, 0, 2, 0);

		// G. a grid at or below 65,535 cells is never skipped: 255 x 257 = 65,535 cells, and Huge's 256 x 160 with no plot
		//    geometry at all (a read of address 0 would fault: nothing is read)
		for (int k = 0; k < 2; ++k)
		{
			ResetWorld(scratch.c_str());
			if (k == 0) FgInit(255, 257, 2, 0); else FgInit(256, 160, 2, 0);
			Cnt(k == 0 ? 65534 : 40959) = 3;                         // the last cell holds nodes: nothing to do with rule 2
			g_w.graph = FG(); g_w.h2w = 0; g_w.aabbMin = 0;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			Register(OBJ(), BUILDER());
			FarmGuard_OnPresent(Env(1));
			for (int gx = 0; gx < 60; ++gx) Walk(Pay(gx, 30));
			CHECK(g_builderCalls == 60 && St().skipRule2Near == 0 && St().skipRule2Stop == 0 && St().skipRule2Geo == 0 && St().rule2Grids == 0);
			CHECK(!LogHas("rule 2"));
		}

		// H. (a): cell 65,535 holds a node before a builder call: that call and every later one skipped, LOUD, the record
		{
			ResetWorld(scratch.c_str());
			map9074();
			g_w.graph = FG(); g_w.h2w = H2W; g_w.aabbMin = AABB;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			FarmGuard_TestTimes(50, 120);
			Register(OBJ(), BUILDER());
			FarmGuard_OnPresent(Env(1));
			Walk(Pay(10, 10)); Walk(Pay(20, 20));                      // far plots: built
			CHECK(g_builderCalls == 2);
			FgFile(0xFFFF, 2);                                       // the bake or a failed rule put a node into the cell
			Walk(Pay(10, 11));
			CHECK(g_builderCalls == 2 && St().rule2Stopped == 1 && St().skipRule2Stop == 1);
			CHECK(LogHas("farm_guard: LOUD: rule 2 (a): cell 65,535 of the farm-field layout grid (360 x 224 = 80640 cells, above 65,535) holds 1 nodes before builder call 3"));
			std::string rec; ReadAllW(scratch + L"\\farm-guard.log", &rec);
			CHECK(rec.find("rule 2 (a): cell 65,535") != std::string::npos && rec.find("not called again") != std::string::npos);
			Cnt(0xFFFF) = 0; First(0xFFFF) = Last(0xFFFF) = NIL;     // emptied again: the rule stays stopped for the process
			Walk(Pay(30, 30)); Walk(Pay(40, 40));
			CHECK(g_builderCalls == 2 && St().skipRule2Stop == 3);
			Sleep(100); FarmGuard_OnPresent(Env(1));
			CHECK(LogHas("rule 2 skipped 3 (rule 2 (a) fired: no further builder call in this process)"));
			std::string evs; ReadAllW(scratch + L"\\farm-builder-calls.log", &evs);
			CHECK(evs.find("skipped-rule2-stopped x=") != std::string::npos);
			CHECK(LogHas("; rule 2: plot (146, 64), kind-0 nodes 0, touching cell 65,535 0, radius 2; stopped by (a)"));
		}
		// I. farm_builder=0 on an oversize grid with cell 65,535 not empty: the server's arm, rule 2 does not act; the graph line
		//    reports the geometry
		{
			ResetWorld(scratch.c_str());
			map9074(); FgFile(0xFFFF, 1);
			g_w.graph = FG(); g_w.h2w = H2W; g_w.aabbMin = AABB;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			FarmGuard_TestTimes(50, 120);
			Register(OBJ(), BUILDER());
			FarmGuardEnv e = Env(1); e.skipBuilder = 1;
			FarmGuard_OnPresent(e);
			Walk(Pay(146, 64)); Walk(Pay(10, 10));
			CHECK(g_builderCalls == 0 && St().skipServer == 2 && St().skipRule2Near == 0 && St().skipRule2Stop == 0 && !St().rule2Stopped);
			Sleep(200); FarmGuard_OnPresent(e);
			CHECK(!LogHas("LOUD: rule 2") && LogHas("; rule 2: plot (146, 64), kind-0 nodes 0, touching cell 65,535 0, radius 2; not acting (farm_builder=0)"));
		}
		// J. no geometry (the matrix is not a hex lattice, or unreadable): every call on that grid skipped, LOUD once
		for (int k = 0; k < 2; ++k)
		{
			ResetWorld(scratch.c_str());
			map9074(); g_h2w[0] = 50.0f;
			void* res = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
			g_w.graph = FG(); g_w.h2w = k == 0 ? H2W : (uintptr_t)res; g_w.aabbMin = AABB;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			Register(OBJ(), BUILDER());
			FarmGuard_OnPresent(Env(1));
			Walk(Pay(10, 10)); Walk(Pay(146, 64)); Walk(Pay(170, 100));
			CHECK(g_builderCalls == 0 && St().skipRule2Geo == 3 && St().rule2Grids == 1);
			CHECK(LogHas(k == 0 ? "farm_guard: LOUD: rule 2: the grid has 80640 cells (above 65,535) and the plot holding cell 65,535 is not known (the plot geometry is not the hex lattice"
			                    : "is not known (a read of the grid or the plot geometry faulted)"));
			CHECK(g_logText.find("LOUD: rule 2: the grid") == g_logText.rfind("LOUD: rule 2: the grid"));   // once per grid
			VirtualFree(res, 0, MEM_RELEASE);
		}
		// K. a new registration (the next load) chooses the radius again: a coastline piece added between two loads
		{
			ResetWorld(scratch.c_str());
			map9074();
			g_w.graph = FG(); g_w.h2w = H2W; g_w.aabbMin = AABB;
			FarmGuard_TestUse(&g_w, scratch.c_str());
			const uint32_t a = Register(OBJ(), BUILDER());
			FarmGuard_OnPresent(Env(1));
			Walk(Pay(150, 64));                                      // distance 4: built with radius 2
			CHECK(g_builderCalls == 1 && St().rule2Grids == 1);
			NKind(3) = 0; float* b3 = (float*)&g_fg.nodes[3 * 56 + 0x18]; b3[0] = 3590; b3[1] = 440; b3[2] = 3600; b3[3] = 450;
			Walk(Pay(150, 64));                                      // the same load: the radius stays 2
			CHECK(g_builderCalls == 2);
			CHECK(!Unregister(OBJ(), BUILDER())); g_nodes[a].fn = INERT();
			Register(OBJ(), BUILDER());                              // the next load's registration, swapped at the Present
			FarmGuard_OnPresent(Env(1));
			Walk(Pay(150, 64));                                      // radius 4 now: skipped
			CHECK(g_builderCalls == 2 && St().rule2Grids == 2 && St().skipRule2Near == 1 && LogHas("radius 4: the builder is not called"));
		}
	}

	// ---- 14. ticket 0535: one owner per process (the proxy), the composite beside it installs nothing ------------------------
	{
		typedef void (*PeerUseFn)(const FgWorld*, const wchar_t*);
		typedef void (*PeerPresentFn)(void (*)(const char*, ...), int, uint32_t, uint32_t);
		typedef void (*PeerStatsFn)(FgStats*);
		typedef int (*PeerRoleFn)();
		typedef uintptr_t (*PeerHookWordFn)();
		wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
		std::wstring peerPath = self; peerPath = peerPath.substr(0, peerPath.find_last_of(L'\\') + 1) + L"farm_guard_peer.dll";
		HMODULE peer = LoadLibraryW(peerPath.c_str());
		CHECK(peer != nullptr);
		PeerUseFn peerUse = peer ? (PeerUseFn)GetProcAddress(peer, "PeerUse") : nullptr;
		PeerPresentFn peerPresent = peer ? (PeerPresentFn)GetProcAddress(peer, "PeerPresent") : nullptr;
		PeerStatsFn peerStats = peer ? (PeerStatsFn)GetProcAddress(peer, "PeerStats") : nullptr;
		PeerRoleFn peerRole = peer ? (PeerRoleFn)GetProcAddress(peer, "PeerRole") : nullptr;
		PeerHookWordFn peerHook = peer ? (PeerHookWordFn)GetProcAddress(peer, "PeerHookWord") : nullptr;
		CHECK(peerUse && peerPresent && peerStats && peerRole && peerHook);
		if (peerUse && peerPresent && peerStats && peerRole && peerHook)
		{
			auto PSt = [&]() { FgStats s; peerStats(&s); return s; };
			SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
			char why[400] = "";
			// A. the owner (this module, as the proxy) and the composite (the peer): exactly one installs
			ResetWorld(scratch.c_str());
			FarmGuard_TestTimes(50, 120);
			const uint32_t bi = Register(OBJ(), BUILDER());
			CHECK(FarmGuard_OwnerAttach("proxy", why, sizeof(why)) && strstr(why, " created (attach 1") != nullptr);
			char fwhy[160] = "";
			const uintptr_t page = FarmGuard_FindShared(fwhy, sizeof(fwhy));
			const FgShared* sh = (const FgShared*)page;
			CHECK(page && FarmGuard_Role() == 1 && FarmGuard_HookWord() == sh->hookStub && FarmGuard_InertWord() == sh->inertStub);
			peerUse(&g_w, scratch.c_str());
			for (int k = 0; k < 3; ++k) { peerPresent(Log, 1, 1, 1); FarmGuard_OnTick(Env(1)); peerPresent(Log, 1, 1, 1); }
			CHECK(peerRole() == 2 && PSt().swaps == 0 && PSt().ready == 1);    // the composite found the owner's page
			CHECK(St().swaps == 1 && g_nodes[bi].fn == sh->hookStub);          // one swap, by the owner, to its stub
			CHECK(sh->forwarded >= 5 && sh->hookTarget != 0 && sh->presentFn != 0);
			CHECK(LogHas("farm_guard: the proxy owns the guard in this process (ticket 0535; shared page"));
			CHECK(LogHas("farm_guard: installed by the proxy: the builder's listener node"));
			Walk((void*)0x77);
			CHECK(g_builderCalls == 1 && g_lastEv == (void*)0x77 && g_lastObj == (void*)g_objStore && St().builderCalls == 1 && PSt().calls == 0);
			CHECK(!LogHas("farm_guard: before a burst"));                      // a composite hands over its Presents: no pre-check
			Sleep(200);
			for (int k = 0; k < 3; ++k) { peerPresent(Log, 1, 1, 1); FarmGuard_OnTick(Env(1)); }
			CHECK(LogHas("farm_guard: load 1 (grid seq 1, 2 s without a call): builder calls 1"));   // the owner's burst line, at the Present
			CHECK(LogHas("farm_guard: graph (after load 1, grid seq 1)") && LogHas("farm_guard: in game (grid seq 1)"));
			CHECK(LogHas("farm_guard: graph (in game, grid seq 1)"));
			// B. the kill switch: the owner puts the builder back; the composite installs nothing either
			FarmGuard_OnTick(Env(0));
			for (int k = 0; k < 3; ++k) { peerPresent(Log, 0, 1, 1); FarmGuard_OnTick(Env(0)); }
			CHECK(g_nodes[bi].fn == BUILDER() && St().restored == 1 && PSt().swaps == 0 && PSt().restored == 0);
			Walk((void*)0x78);
			CHECK(g_builderCalls == 2 && St().builderCalls == 1);
			FarmGuard_OnTick(Env(1));
			CHECK(g_nodes[bi].fn == sh->hookStub && St().swaps == 2);
			// C. the unload (the stub returns at once) and the reload (re-armed), as at a second boot of the proxy
			FarmGuard_OwnerDetach();
			CHECK(sh->hookTarget == 0 && sh->presentFn == 0 && sh->detaches == 1);
			Walk((void*)0x79); Walk((void*)0x7A);
			CHECK(g_builderCalls == 2);                                       // a type-59 dispatch after the unload: nothing called
			peerPresent(Log, 1, 1, 1);                                        // the composite's Present: nothing to hand to
			CHECK(PSt().swaps == 0 && g_nodes[bi].fn == sh->hookStub);
			FarmGuard_TestUse(&g_w, scratch.c_str());                         // the next boot's image (fresh state)
			CHECK(FarmGuard_OwnerAttach("proxy", why, sizeof(why)) && strstr(why, "found by VP64_FARM_GUARD") != nullptr && strstr(why, "attach 2") != nullptr);
			FarmGuard_OnTick(Env(1));
			CHECK(sh->hookTarget != 0 && St().swaps == 0);                    // the node already holds the stub
			Walk((void*)0x7B);
			CHECK(g_builderCalls == 3 && g_lastEv == (void*)0x7B);           // re-armed
			FarmGuard_OwnerDetach();
			// D. no owner in the process (a test host, 0527's form): the composite installs its own
			SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
			FarmGuard_TestForgetRole();
			ResetWorld(scratch.c_str());
			const uint32_t bj = Register(OBJ(), BUILDER());
			peerUse(&g_w, scratch.c_str());
			peerPresent(Log, 1, 1, 1);
			CHECK(peerRole() == 0 && PSt().swaps == 1 && g_nodes[bj].fn == peerHook() && peerHook() != sh->hookStub);
			Walk((void*)0x7C);
			CHECK(g_builderCalls == 1 && PSt().builderCalls == 1);
			// E. a page name that is not valid: LOUD, and the composite installs its own
			SetEnvironmentVariableW(FG_SHARED_ENV, L"00001000");
			ResetWorld(scratch.c_str());
			const uint32_t bk = Register(OBJ(), BUILDER());
			peerUse(&g_w, scratch.c_str());
			peerPresent(Log, 1, 1, 1);
			CHECK(peerRole() == 0 && g_nodes[bk].fn == peerHook() && LogHas("farm_guard: LOUD: 0x00001000 is not a committed read-write page"));
			SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
			// F. the stub page's protection: the stubs execute-read, the data read-write
			MEMORY_BASIC_INFORMATION m;
			CHECK(VirtualQuery((void*)sh->hookStub, &m, sizeof(m)) == sizeof(m) && m.Protect == PAGE_EXECUTE_READ);
			CHECK(VirtualQuery((void*)page, &m, sizeof(m)) == sizeof(m) && m.Protect == PAGE_READWRITE);
		}
		FarmGuard_TestForgetRole();
	}

	// ---- 15. ticket 0655: the farm-end post on the skipped-server path (the builder's last step, nothing else of it) -------
	{
		SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
		FarmGuard_TestForgetRole();
		PostWorld(&g_w);
		ResetWorld(scratch.c_str());                                 // (zeroes the post's fields: set them again)
		PostWorld(&g_w);
		FarmGuard_TestUse(&g_w, scratch.c_str());
		FarmGuard_TestTimes(50, 120);
		CHECK(St().postReady == 1);
		const uint32_t a = Register(OBJ(), BUILDER());
		FarmGuardEnv e = Env(1); e.gridSeq = 4; e.skipBuilder = 1;
		FarmGuard_OnPresent(e);
		CHECK(g_nodes[a].fn == HOOK());
		unsigned char graphBefore[sizeof(g_graph)]; memcpy(graphBefore, g_graph, sizeof(g_graph));
		Ep epsBefore[64]; memcpy(epsBefore, g_eps, sizeof(g_eps));
		unsigned char pay[0x40] = {};
		*(uint32_t*)(pay + 0x08) = 0x27u; *(int16_t*)(pay + 0x18) = 7; *(int16_t*)(pay + 0x1A) = 9; pay[0x1C] = 1;
		// A. an event that passes the three tests: one post, the builder's two stores, the queue of channel 0
		Walk(pay);
		CHECK(g_builderCalls == 0 && St().skipServer == 1 && St().posts == 1 && g_posterCalls == 1);
		CHECK(g_posterQueue == (void*)0x10000000u && *(int16_t*)(g_rec + 0x10) == 7 && *(int16_t*)(g_rec + 0x12) == 9);
		CHECK(*(uint32_t*)(g_rec + 4) == 5 && *(uint32_t*)(g_rec + 0x14) == 0);   // nothing written past the two words
		CHECK(g_plotCalls == 1 && g_plotCube[0] == 7 && g_plotCube[1] == 9 && g_plotCube[2] == -16);
		CHECK(g_artCalls == 1 && g_artNameSeen == g_fakeArtName);
		// channel 1 (the index word): the queue object one channel stride on
		g_queueIndex = 1;
		Walk(pay);
		CHECK(St().posts == 2 && g_posterQueue == (void*)(0x10000000u + 0x800180u));
		g_queueIndex = 0;
		// B. each test failed alone: no post, counted per test, the later tests not run
		pay[0x1C] = 0; Walk(pay); pay[0x1C] = 1;
		CHECK(St().posts == 2 && St().postFailFlag == 1 && g_posterCalls == 2 && g_plotCalls == 2 && g_artCalls == 2);
		*(int16_t*)(pay + 0x1A) = 60; Walk(pay); *(int16_t*)(pay + 0x1A) = 9;   // off the map
		CHECK(St().posts == 2 && St().postFailMap == 1 && g_posterCalls == 2 && g_plotCalls == 3 && g_artCalls == 2);
		*(uint32_t*)(pay + 0x08) = 0x28u; Walk(pay);                            // another improvement's art
		CHECK(St().posts == 2 && St().postFailArt == 1 && g_posterCalls == 2 && g_artCalls == 3);
		g_artIdAnswer = -1; *(uint32_t*)(pay + 0x08) = 0xFFFFFFFFu; Walk(pay); // no farm art in the database: never a post
		CHECK(St().posts == 2 && St().postFailArt == 2 && g_posterCalls == 2);
		g_artIdAnswer = 0x27; *(uint32_t*)(pay + 0x08) = 0x27u;
		// C. refused: an unreadable payload, a queue index out of range, a poster that returns nothing (LOUD once)
		Walk((void*)0x4545);
		CHECK(St().postRefused == 1 && g_posterCalls == 2 && g_plotCalls == 5);
		g_queueIndex = 2; Walk(pay); g_queueIndex = 0;
		CHECK(St().postRefused == 2 && g_posterCalls == 2);
		g_posterNull = 1; Walk(pay); Walk(pay); g_posterNull = 0;
		CHECK(St().postRefused == 4 && g_posterCalls == 4 && St().posts == 2);
		CHECK(LogHas("farm_guard: LOUD: the EXE's farm-end poster returned no record (queue channel 0)"));
		// D. nothing else of the builder ran: the graph and its endpoints are as before, the builder never called
		CHECK(memcmp(graphBefore, g_graph, sizeof(g_graph)) == 0 && memcmp(epsBefore, g_eps, sizeof(g_eps)) == 0 && g_builderCalls == 0);
		// E. the burst line and the in-game line carry the counts; the event lines name each outcome
		Sleep(200);
		FarmGuard_OnPresent(e);
		CHECK(LogHas("farm-end posts 2 (failed the builder's tests 4, refused 4, switched off 0)"));
		CHECK(LogHas("farm-end posts 2 in this process (failed the flag / plot / art test 1 / 1 / 2, refused 4, switched off 0; post on)"));
		std::string evs;
		ReadAllW(scratch + L"\\farm-builder-calls.log", &evs);
		CHECK(evs.find("seq=4 skipped-server-posted x=7 y=9 d8=0x00000027 b1c=1\r\n") != std::string::npos);
		CHECK(evs.find("skipped-server-post-test-flag x=7 y=9") != std::string::npos);
		CHECK(evs.find("skipped-server-post-test-map x=7 y=60") != std::string::npos);
		CHECK(evs.find("skipped-server-post-test-art x=7 y=9 d8=0x00000028") != std::string::npos);
		CHECK(evs.find("skipped-server-post-refused-payload payload not readable") != std::string::npos);
		CHECK(evs.find("skipped-server-post-refused-queue x=7") != std::string::npos);
		CHECK(evs.find("skipped-server-post-refused-null x=7") != std::string::npos);
		// F. the test switch (farm_end_post=0): no post, counted, logged; on again: posts again
		FarmGuardEnv off = e; off.farmEndPostOff = 1;
		FarmGuard_OnPresent(off);
		CHECK(LogHas("farm_guard: farm_end_post=0: no farm-end post on the skipped-server path"));
		Walk(pay);
		CHECK(St().posts == 2 && St().postSwitchedOff == 1 && g_posterCalls == 4 && g_plotCalls == 8);
		FarmGuard_OnPresent(e);
		CHECK(LogHas("farm_guard: farm_end_post=1: the farm-end post is made again"));
		Walk(pay);
		CHECK(St().posts == 3 && g_posterCalls == 5);
		// G. the other skip paths post nothing: no view (the GameView group shut down), a stale node, the trip; and the
		// builder path (farm_builder=1: the builder makes its own post)
		const int pc = g_posterCalls;
		g_viewFlag = 0; Walk(pay); g_viewFlag = 1;
		CHECK(St().skipNoView == 1 && g_posterCalls == pc);
		const uint32_t s2 = Register(OBJ(), BUILDER());              // a registration not swapped yet: the hook's call is stale
		Walk(pay);
		CHECK(St().skipStale >= 1 && g_posterCalls == pc && g_builderCalls == 1);   // the builder's own node called it once
		FarmGuard_OnPresent(e);
		CHECK(g_nodes[s2].fn == HOOK() && g_nodes[a].fn == INERT());
		FarmGuardEnv on = e; on.skipBuilder = 0;
		FarmGuard_OnPresent(on);
		Walk(pay);
		CHECK(g_builderCalls == 2 && g_posterCalls == pc && St().posts == 3);  // the new node's hook calls the builder (the old one retired)
		FarmGuard_OnPresent(e);
		FakeFree(7); FakeFree(7);                                    // a double free: the in-game check of the next load trips
		e.gridSeq = 5;
		FarmGuard_OnPresent(e); Sleep(200); FarmGuard_OnPresent(e);
		CHECK(St().tripped == 1);
		Walk(pay);
		CHECK(St().skipTripped >= 1 && g_posterCalls == pc && St().posts == 3);
		// H. a world without the post (the build's sites did not match): refused, counted, the event line says so
		ResetWorld(scratch.c_str());
		FarmGuard_TestTimes(50, 120);
		const uint32_t c = Register(OBJ(), BUILDER());
		FarmGuard_OnPresent(e);
		Walk(pay);
		CHECK(g_nodes[c].fn == HOOK() && St().postReady == 0 && St().postRefused == 1 && St().posts == 0 && g_builderCalls == 0);
		Sleep(200); FarmGuard_OnPresent(e);
		CHECK(LogHas("farm-end posts 0 (failed the builder's tests 0, refused 1, switched off 0) (the post is off for this build)"));
		printf("farm_guard_test: the farm-end post (ticket 0655): done\n");
	}

	// ---- 16. ticket 0655 (P1): the diagnostic area probe on a fake A manager's type-33 list -------------------------------
	{
		static __declspec(align(16)) unsigned char amgr[0x200 + 0x600 * 2];
		static unsigned char aLockInit = 1;
		static volatile LONG aLock;
		static List alist;
		static Node anodes[8];
		memset(amgr, 0, sizeof(amgr)); memset(&alist, 0, sizeof(alist)); memset(anodes, 0, sizeof(anodes));
		alist.data = (uint32_t)(uintptr_t)anodes; alist.cap = 8; alist.head = alist.tail = NIL; alist.freeHead = NIL;
		*(uint32_t*)(amgr + 0x110) = (uint32_t)(uintptr_t)FakeTrampoline; *(uint32_t*)(amgr + 0x114) = (uint32_t)(uintptr_t)&alist;
		auto ARegister = [&](uint32_t obj, uint32_t fn) {
			const uint32_t i = alist.size++;
			anodes[i].obj = obj; anodes[i].fn = fn; anodes[i].next = NIL; anodes[i].prev = alist.tail;
			if (alist.tail != NIL) anodes[alist.tail].next = i;
			if (alist.head == NIL) alist.head = i;
			alist.tail = i; alist.count++; alist.live++;
			return i;
		};
		auto AWalk = [&](void* ev) {
			uint32_t idx = alist.head;
			for (uint32_t k = 0; k < alist.count; ++k) { Node* n = &anodes[idx]; ((FgListenerFn)(uintptr_t)n->fn)((void*)(uintptr_t)n->obj, nullptr, ev); idx = n->next; }
		};
		ResetWorld(scratch.c_str());
		FarmGuard_TestTimes(50, 120);
		FgWorld aw = {};
		aw.lockInit = (uintptr_t)&aLockInit; aw.lock = (uintptr_t)&aLock; aw.mgr = (uintptr_t)amgr; aw.slotOff = 0x110; aw.stride = 0x600;
		aw.trampoline = (uintptr_t)FakeTrampoline; aw.builder = (uintptr_t)FakeOther; aw.builderObj = (uintptr_t)g_objStore;
		FarmGuard_TestUseArea(&aw);
		const uint32_t other = ARegister(0x4444, (uint32_t)(uintptr_t)FakeTrampoline);
		const uint32_t an = ARegister(OBJ(), (uint32_t)(uintptr_t)FakeOther);
		FarmGuardEnv e = Env(1); e.skipBuilder = 1;
		FarmGuard_OnPresent(e);                                     // the key off: nothing touched
		CHECK(anodes[an].fn == (uint32_t)(uintptr_t)FakeOther && St().areaSwaps == 0 && St().areaReady == 1);
		e.areaProbe = 1;
		FarmGuard_OnPresent(e);
		CHECK(anodes[an].fn == (uint32_t)(uintptr_t)FarmGuard_AreaProbe() && St().areaSwaps == 1 && aLock == 0);
		CHECK(anodes[other].fn == (uint32_t)(uintptr_t)FakeTrampoline);
		CHECK(LogHas("farm_guard: area probe (farm_area_probe=1, a DIAGNOSTIC of ticket 0655): node 1 of type 33"));
		const int oc = g_otherCalls;
		for (int k = 0; k < 25; ++k) AWalk((void*)0x99);
		CHECK(g_otherCalls == oc + 25 && St().areaCalls == 25);    // the listener called once per event, through the probe
		Sleep(200);
		FarmGuard_OnPresent(e);
		CHECK(LogHas("farm_guard: area probe: 25 calls of the builder object's area listener (calls 1..25 of this process)"));
		CHECK(LogHas("; the first 20 mean ") && LogHas(", the last 20 mean "));
		std::string al;
		ReadAllW(scratch + L"\\farm-area-calls.log", &al);
		CHECK(al.find("\r\n25 ") != std::string::npos);
		// a new registration (the next load): the old probe node retires, the new node gets the probe; one call per event
		const uint32_t an2 = ARegister(OBJ(), (uint32_t)(uintptr_t)FakeOther);
		FarmGuard_OnPresent(e);
		CHECK(anodes[an].fn == (uint32_t)(uintptr_t)FarmGuard_AreaInert() && anodes[an2].fn == (uint32_t)(uintptr_t)FarmGuard_AreaProbe());
		CHECK(St().areaRetired == 1 && St().areaSwaps == 2);
		AWalk((void*)0x99);
		CHECK(g_otherCalls == oc + 26 && St().areaCalls == 26);
		// the key off again: the listener's own address back
		e.areaProbe = 0;
		FarmGuard_OnPresent(e);
		CHECK(anodes[an2].fn == (uint32_t)(uintptr_t)FakeOther && St().areaRestored == 1);
		AWalk((void*)0x99);
		CHECK(g_otherCalls == oc + 27 && St().areaCalls == 26);
		// a held lock: nothing written this time
		e.areaProbe = 1; aLock = 1;
		FarmGuard_OnPresent(e);
		CHECK(anodes[an2].fn == (uint32_t)(uintptr_t)FakeOther);
		aLock = 0;
		FarmGuard_OnPresent(e);
		CHECK(anodes[an2].fn == (uint32_t)(uintptr_t)FarmGuard_AreaProbe());
		e.areaProbe = 0; FarmGuard_OnPresent(e);
		FarmGuard_TestUseArea(nullptr);
		printf("farm_guard_test: the area probe (ticket 0655 P1): done\n");
	}

	// ---- 17. ticket 0657: the edge placement (a fake farm manager, its two listeners and a placer by the rule of 2.6.2) -------
	{
		// two fake managers' lists: the A manager's type 5 (slot 0x30) and the B manager's type 48 (slot 0x188)
		static __declspec(align(16)) unsigned char emgr[0x200 + 0x600 * 2], rmgr[0x200 + 0x600 * 2];
		static unsigned char eLockInit = 1, rLockInit = 1;
		static volatile LONG eLock, rLock;
		static List elist, rlist;
		static Node enodes[8], rnodes[8];
		memset(emgr, 0, sizeof(emgr)); memset(rmgr, 0, sizeof(rmgr));
		auto InitList = [](List& l, Node* n) { memset(&l, 0, sizeof(l)); memset(n, 0, sizeof(Node) * 8); l.data = (uint32_t)(uintptr_t)n; l.cap = 8; l.head = l.tail = NIL; l.freeHead = NIL; };
		InitList(elist, enodes); InitList(rlist, rnodes);
		*(uint32_t*)(emgr + 0x30) = (uint32_t)(uintptr_t)FakeTrampoline; *(uint32_t*)(emgr + 0x34) = (uint32_t)(uintptr_t)&elist;
		*(uint32_t*)(rmgr + 0x188) = (uint32_t)(uintptr_t)FakeTrampoline; *(uint32_t*)(rmgr + 0x18C) = (uint32_t)(uintptr_t)&rlist;
		auto LReg = [](List& l, Node* nodes, uint32_t obj, uint32_t fn) {
			const uint32_t i = l.size++;
			nodes[i].obj = obj; nodes[i].fn = fn; nodes[i].next = NIL; nodes[i].prev = l.tail;
			if (l.tail != NIL) nodes[l.tail].next = i;
			if (l.head == NIL) l.head = i;
			l.tail = i; l.count++; l.live++;
			return i;
		};
		auto LWalk = [](List& l, Node* nodes, void* ev) {
			uint32_t idx = l.head;
			for (uint32_t k = 0; k < l.count; ++k) { Node* n = &nodes[idx]; ((FgListenerFn)(uintptr_t)n->fn)((void*)(uintptr_t)n->obj, nullptr, ev); idx = n->next; }
		};
		// the fake farm manager and its records (0x90 bytes: +5 count, +0xC flags, +0x10 hex, +0x14 position, +0x20 ids)
		static unsigned char* fm = (unsigned char*)calloc(0x20800, 1);
		static unsigned char recs[4][0x90];
		memset(recs, 0, sizeof(recs));
		const int16_t hexes[4][2] = { { 7, 9 }, { 8, 9 }, { 3, 2 }, { 7, 9 } };
		for (int i = 0; i < 4; ++i) { memcpy(recs[i] + 0x10, hexes[i], 4); *(uint16_t*)(recs[i] + 0x20) = 0xBEEF; }
		*(uint32_t*)(recs[3] + 0x0C) = 0x400;   // record 3: freed (same plot as record 0): never touched
		*(uint32_t*)(fm + 0x207A4) = (uint32_t)(uintptr_t)recs; *(uint32_t*)(fm + 0x207A8) = 4;
		const uint32_t kTable = 0x0BADF00Du;   // the manager's own table word: never read while a record has no curve
		*(uint32_t*)(fm + 0x6F4) = kTable;
		P_placerCalls = P_placerBadTable = 0; L5_endCalls = L5_endDeferred = L5_eraCalls = 0;

		SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
		FarmGuard_TestForgetRole();
		ResetWorld(scratch.c_str());
		PostWorld(&g_w);
		FarmGuard_TestUse(&g_w, scratch.c_str());
		FarmGuard_TestTimes(50, 120);
		FgEdgeWorld ew = {};
		ew.end.lockInit = (uintptr_t)&eLockInit; ew.end.lock = (uintptr_t)&eLock; ew.end.mgr = (uintptr_t)emgr; ew.end.slotOff = 0x30;
		ew.end.stride = 0x600; ew.end.trampoline = (uintptr_t)FakeTrampoline; ew.end.builder = (uintptr_t)L5::End;
		ew.era.lockInit = (uintptr_t)&rLockInit; ew.era.lock = (uintptr_t)&rLock; ew.era.mgr = (uintptr_t)rmgr; ew.era.slotOff = 0x188;
		ew.era.stride = 0x600; ew.era.trampoline = (uintptr_t)FakeTrampoline; ew.era.builder = (uintptr_t)L5::Era;
		FarmGuard_TestUseEdge(&ew);
		const uint32_t FM = (uint32_t)(uintptr_t)fm, ENDW = (uint32_t)(uintptr_t)FarmGuard_EdgeEnd(), ERAW = (uint32_t)(uintptr_t)FarmGuard_EdgeEra();
		const uint32_t eOther = LReg(elist, enodes, 0x4444, (uint32_t)(uintptr_t)FakeOther);
		const uint32_t eN = LReg(elist, enodes, FM, (uint32_t)(uintptr_t)L5::End);
		const uint32_t rN = LReg(rlist, rnodes, FM, (uint32_t)(uintptr_t)L5::Era);
		// the points block: DONE for grid seq 4; (7, 9) a point, (8, 9) the centre, (3, 2) a point
		static std::vector<unsigned char> blk(VPFARM_SECTION_BYTES, 0);
		VPFarmHeader* h = (VPFarmHeader*)&blk[0];
		VPFarmEntry* en = (VPFarmEntry*)(&blk[0] + VPFARM_HEADER_BYTES);
		auto Publish = [&](uint32_t load, uint32_t state, int n) {
			h->seq++;   // odd
			h->magic = VPFARM_MAGIC; h->version = VPFARM_VERSION; h->loadSeq = load; h->state = state; h->count = (uint32_t)n;
			memset(en, 0, sizeof(VPFarmEntry) * 3);
			en[0].hexX = 3; en[0].hexY = 2; en[0].kind = VPFARM_KIND_POINT; en[0].wx = -3800.25f; en[0].wy = -2200.5f;
			en[1].hexX = 7; en[1].hexY = 9; en[1].kind = VPFARM_KIND_POINT; en[1].wx = -3650.75f; en[1].wy = -1810.125f;
			en[2].hexX = 8; en[2].hexY = 9; en[2].kind = VPFARM_KIND_CENTRE;
			h->seq++;   // even
		};
		Publish(4, VPFARM_DONE, 3);
		FarmGuard_TestEdgePoints(&blk[0]);
		FarmGuard_TestGrid(1, 4);
		unsigned char pay[4];
		auto Ev = [&](int16_t x, int16_t y) { memcpy(pay, &x, 2); memcpy(pay + 2, &y, 2); return (void*)pay; };
		auto Pos = [&](int i, float* x, float* y) { memcpy(x, recs[i] + 0x14, 4); memcpy(y, recs[i] + 0x18, 4); };
		auto Clean = [&]() {   // nothing of a transient curve left anywhere
			bool ok = *(uint32_t*)(fm + 0x6F4) == kTable;
			for (int i = 0; i < 4; ++i) ok = ok && recs[i][5] == 0 && *(uint16_t*)(recs[i] + 0x20) == 0xBEEF;
			return ok;
		};
		// A. farm_builder=1 (the EXE's builder runs, its records have real curves): the nodes are not touched
		FarmGuardEnv e = Env(1); e.gridSeq = 4;
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN].fn == (uint32_t)(uintptr_t)L5::End && rnodes[rN].fn == (uint32_t)(uintptr_t)L5::Era && St().edgeSwaps == 0);
		// B. farm_builder=0: both swapped (the other listener of type 5 untouched); farm_edge=0 puts them back
		e.skipBuilder = 1;
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN].fn == ENDW && rnodes[rN].fn == ERAW && enodes[eOther].fn == (uint32_t)(uintptr_t)FakeOther && St().edgeSwaps == 2);
		CHECK(eLock == 0 && rLock == 0 && LogHas("farm_guard: edge (ticket 0657): on:") && LogHas("farm-end listener node 1"));
		e.farmEdgeOff = 1;
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN].fn == (uint32_t)(uintptr_t)L5::End && rnodes[rN].fn == (uint32_t)(uintptr_t)L5::Era && St().edgeRestored == 2);
		CHECK(LogHas("farm_guard: farm_edge=0: no edge pass-through"));
		e.farmEdgeOff = 0;
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN].fn == ENDW && rnodes[rN].fn == ERAW);
		// C. a farm end with a point: the EXE's listener places record 0 at the point (the transient square's centroid), and
		// nothing of the curve is left; the freed record of the same plot is not touched
		const int oc = g_otherCalls;
		LWalk(elist, enodes, Ev(7, 9));
		float x = 0, y = 0; Pos(0, &x, &y);
		CHECK(L5_endCalls == 1 && P_placerCalls == 1 && P_placerBadTable == 0 && g_otherCalls == oc + 1);
		CHECK(fabsf(x - -3650.75f) < 0.02f && fabsf(y - -1810.125f) < 0.02f);
		CHECK(Clean() && *(uint32_t*)(recs[3] + 0x14) == 0);
		FgStats s = St();
		CHECK(s.endCalls == 1 && s.endPlaced == 1 && s.endChecked == 1 && s.endOff1 == 0 && s.endErrMax < 0.02 && s.edgeFaults == 0);
		printf("farm_guard_test: edge: placed %.4f, %.4f for the point -3650.75, -1810.125 (error %.5f)\n", x, y, s.endErrMax);
		// D. the plot's entry is CENTRE: the EXE's listener without a curve (the plot centre)
		LWalk(elist, enodes, Ev(8, 9));
		Pos(1, &x, &y);
		CHECK(L5_endCalls == 2 && fabsf(x - (8 * 64.0f + 9 * 32.0f - 4096.0f)) < 1e-3f && St().endCentre == 1 && Clean());
		// E. the section is not DONE: held (posted again with the same plot; the EXE's listener not called), then placed
		Publish(4, VPFARM_PRODUCING, 0);
		memset(recs[2] + 0x14, 0, 8);
		const int pc = g_posterCalls;
		LWalk(elist, enodes, Ev(3, 2));
		CHECK(L5_endCalls == 2 && g_posterCalls == pc + 1 && *(int16_t*)(g_rec + 0x10) == 3 && *(int16_t*)(g_rec + 0x12) == 2);
		CHECK(St().endHeld == 1 && Clean());
		Publish(4, VPFARM_DONE, 3);
		LWalk(elist, enodes, Ev(3, 2));
		Pos(2, &x, &y);
		CHECK(L5_endCalls == 3 && fabsf(x - -3800.25f) < 0.02f && fabsf(y - -2200.5f) < 0.02f && St().endPlaced == 2 && Clean());
		// F. another load's points (the camera's grid seq 5, the section still load 4) and no valid grid: held
		FarmGuard_TestGrid(1, 5);
		LWalk(elist, enodes, Ev(7, 9));
		FarmGuard_TestGrid(0, 4);
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(L5_endCalls == 3 && St().endHeld == 3 && g_posterCalls == pc + 3);
		FarmGuard_TestGrid(1, 4);
		// G. a plot missing from a DONE list: held for the short limit, then the plot centre (counted as run out)
		FarmGuard_TestEdgeHold(20000, 0);
		LWalk(elist, enodes, Ev(20, 20));
		CHECK(L5_endCalls == 4 && St().endExpired == 1 && St().endCentre == 2);
		FarmGuard_TestEdgeHold(20000, 5000);
		LWalk(elist, enodes, Ev(21, 20));
		CHECK(L5_endCalls == 4 && St().endHeld == 4);
		// H. the manager defers: held (its own deferral would place at the centre later), counted apart
		fm[0x207C0] = 1;
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(L5_endCalls == 4 && L5_endDeferred == 0 && St().endHeldDefer == 1);
		FarmGuard_TestEdgeHold(0, 5000);   // a deferral longer than the limit: the EXE's listener (it defers itself)
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(L5_endCalls == 5 && L5_endDeferred == 1);
		fm[0x207C0] = 0;
		FarmGuard_TestEdgeHold(20000, 5000);
		// I. the re-post fails (the poster gives nothing): the plot centre at once, counted
		g_posterNull = 1;
		Publish(4, VPFARM_PRODUCING, 0);
		LWalk(elist, enodes, Ev(30, 30));
		CHECK(L5_endCalls == 6 && St().endRepostFail == 1);
		g_posterNull = 0;
		Publish(4, VPFARM_DONE, 3);
		// J. the view shut down: dropped (nothing called, as F1's skip); no section: the plot centre
		g_viewFlag = 0;
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(L5_endCalls == 6 && St().endDropped == 1);
		g_viewFlag = 1;
		FarmGuard_TestEdgePoints(nullptr);
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(L5_endCalls == 7 && St().endNoSection == 1 && Clean());
		FarmGuard_TestEdgePoints(&blk[0]);
		// K. the era: every record with an instance and a point gets its curve for the call; the listener re-places them there
		*(uint32_t*)(recs[0] + 0x0C) |= 0x100; *(uint32_t*)(recs[2] + 0x0C) |= 0x100;
		memset(recs[0] + 0x14, 0, 8); memset(recs[2] + 0x14, 0, 8);
		LWalk(rlist, rnodes, (void*)0x77);
		Pos(0, &x, &y);
		CHECK(L5_eraCalls == 1 && fabsf(x - -3650.75f) < 0.02f && fabsf(y - -1810.125f) < 0.02f);
		Pos(2, &x, &y);
		CHECK(fabsf(x - -3800.25f) < 0.02f && fabsf(y - -2200.5f) < 0.02f && St().eraCurves == 2 && Clean() && P_placerBadTable == 0);
		fm[0x207C0] = 1;
		LWalk(rlist, rnodes, (void*)0x77);
		CHECK(L5_eraCalls == 2 && St().eraDeferred == 1 && Clean());
		fm[0x207C0] = 0;
		// L. a new registration of the manager (the next load) beside the swapped nodes: the old ones retire, the new ones are
		// swapped; a call through the old node before the retirement does nothing (stale)
		const uint32_t eN2 = LReg(elist, enodes, FM, (uint32_t)(uintptr_t)L5::End);
		const int ec = L5_endCalls;
		LWalk(elist, enodes, Ev(7, 9));                               // old node: stale (nothing); new node: the EXE's own
		CHECK(L5_endCalls == ec + 1 && St().endStale == 1);
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN].fn == INERT() && enodes[eN2].fn == ENDW && St().edgeRetired == 1);
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(L5_endCalls == ec + 2 && St().endPlaced >= 3 && Clean());
		// M. a held lock: no node changes this time
		e.farmEdgeOff = 1; eLock = 1;
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN2].fn == ENDW && rnodes[rN].fn == (uint32_t)(uintptr_t)L5::Era);
		eLock = 0;
		FarmGuard_OnPresent(e);
		CHECK(enodes[eN2].fn == (uint32_t)(uintptr_t)L5::End);
		// N. edge off: the EXE's listeners are called directly, nothing of the patch runs
		const uint32_t calls0 = St().endCalls;
		LWalk(elist, enodes, Ev(7, 9));
		CHECK(St().endCalls == calls0 && Clean());
		// O. the pass-throughs while the edge is not wanted (a node that outlived the key): straight to the EXE's listener
		FarmGuard_EdgeEnd()(fm, nullptr, Ev(7, 9));
		CHECK(St().endCalls == calls0 + 1 && Clean());
		FarmGuard_TestUseEdge(nullptr);
		FarmGuard_TestEdgePoints(nullptr);
		printf("farm_guard_test: the edge placement (ticket 0657): done\n");
	}

	// ---- 18. ticket 0784: the tread filter on a fake A manager's type-0x28 list (the main Particle object's listener) --------
	{
		static __declspec(align(16)) unsigned char tmgr[0x200 + 0x600 * 2];
		static unsigned char tLockInit = 1;
		static volatile LONG tLock;
		static List tlist;
		static Node tnodes[8];
		memset(tmgr, 0, sizeof(tmgr)); memset(&tlist, 0, sizeof(tlist)); memset(tnodes, 0, sizeof(tnodes));
		tlist.data = (uint32_t)(uintptr_t)tnodes; tlist.cap = 8; tlist.head = tlist.tail = NIL; tlist.freeHead = NIL;
		*(uint32_t*)(tmgr + 0x148) = (uint32_t)(uintptr_t)FakeTrampoline; *(uint32_t*)(tmgr + 0x14C) = (uint32_t)(uintptr_t)&tlist;
		auto TRegister = [&](uint32_t obj, uint32_t fn) {
			const uint32_t i = tlist.size++;
			tnodes[i].obj = obj; tnodes[i].fn = fn; tnodes[i].next = NIL; tnodes[i].prev = tlist.tail;
			if (tlist.tail != NIL) tnodes[tlist.tail].next = i;
			if (tlist.head == NIL) tlist.head = i;
			tlist.tail = i; tlist.count++; tlist.live++;
			return i;
		};
		auto TWalk = [&](void* ev) {
			uint32_t idx = tlist.head;
			for (uint32_t k = 0; k < tlist.count; ++k) { Node* n = &tnodes[idx]; ((FgListenerFn)(uintptr_t)n->fn)((void*)(uintptr_t)n->obj, nullptr, ev); idx = n->next; }
		};
		// the hashes: the EXE's rule (effects-formats.md 8.1) on a documented name, then the two tread names
		CHECK(FgEffectNameHash("ART_DEF_VEFFECT_PARADROP") == 0x26800651u);
		const uint32_t h0 = FgEffectNameHash(FG_TREAD_NAME0), h1 = FgEffectNameHash(FG_TREAD_NAME1);
		CHECK(h0 == 0xE6690C27u && h1 == 0xD5A8C88Du);
		ResetWorld(scratch.c_str());
		FgWorld tw = {};
		tw.lockInit = (uintptr_t)&tLockInit; tw.lock = (uintptr_t)&tLock; tw.mgr = (uintptr_t)tmgr; tw.slotOff = FG_TREAD_SLOT_OFF;
		tw.stride = 0x600; tw.trampoline = (uintptr_t)FakeTrampoline; tw.builder = (uintptr_t)T28::Listener;
		tw.builderObj = (uintptr_t)&T28::g_main;
		FarmGuard_TestUseTread(&tw);
		T28::Reset();
		const uint32_t scene = TRegister((uint32_t)(uintptr_t)&T28::g_scene, (uint32_t)(uintptr_t)T28::Listener);   // another Particle object
		const uint32_t tn = TRegister((uint32_t)(uintptr_t)&T28::g_main, (uint32_t)(uintptr_t)T28::Listener);
		const uint32_t LST = (uint32_t)(uintptr_t)T28::Listener, PASS = (uint32_t)(uintptr_t)FarmGuard_TreadPass();
		unsigned char evT0[0x70], evT1[0x70], evOther[0x70], evTagged[0x70];
		auto Pay = [](unsigned char* p, uint32_t hash, uint32_t tag) { memset(p, 0, 0x70); *(uint32_t*)(p + 0x50) = hash; *(uint32_t*)(p + 0x4C) = tag; *(float*)(p + 0x54) = 1.0f; };
		Pay(evT0, h0, 0xFFFFFFFFu); Pay(evT1, h1, 0); Pay(evOther, 0x26800651u, 0); Pay(evTagged, h0, FG_VP64_KEY_TAG);
		// A. the key off: nothing touched, every record reaches both objects
		FarmGuardEnv e = Env(1);
		FarmGuard_OnPresent(e);
		CHECK(tnodes[tn].fn == LST && tnodes[scene].fn == LST && St().treadSwaps == 0 && St().treadReady == 1);
		TWalk(evT0);
		CHECK(T28::g_main.calls == 1 && T28::g_scene.calls == 1);
		// B. the key on: only the main object's node is swapped, under the lock
		e.treadFilter = 1;
		FarmGuard_OnPresent(e);
		CHECK(tnodes[tn].fn == PASS && tnodes[scene].fn == LST && St().treadSwaps == 1 && tLock == 0);
		CHECK(LogHas("farm_guard: treads (ticket 0784): on: the EXE's two landship tread effects are dropped"));
		CHECK(LogHas("farm_guard: treads (ticket 0784): the main Particle object's type-0x28 listener node 1 (list of 2, channel 0"));
		// C. the two tread names without the VP64 tag dropped; any other name, and a tread of the shell's player, passed with
		// the same object and argument
		T28::Reset();
		TWalk(evT0); TWalk(evT1); TWalk(evOther); TWalk(evTagged);
		CHECK(T28::g_main.calls == 2 && T28::g_main.lastEv == evTagged && T28::g_main.hashes[0] == 0x26800651u && T28::g_main.hashes[1] == h0);
		CHECK(T28::g_scene.calls == 4);                                // the scene's own node is the EXE's: it sees all four
		FgStats s = St();
		CHECK(s.treadCalls == 4 && s.treadDropped0 == 1 && s.treadDropped1 == 1 && s.treadTagged == 1 && s.treadFaults == 0);
		// D. the busy object: a passed record is deferred by the listener itself and replayed by it (not through the node); a
		// dropped one never reaches the deferred list
		T28::Reset();
		T28::g_main.busy = 1;
		TWalk(evT0); TWalk(evOther); TWalk(evT1);
		CHECK(T28::g_main.deferred == 1 && T28::g_main.applied == 0);
		T28::g_main.busy = 0;
		T28::Replay(&T28::g_main);
		CHECK(T28::g_main.applied == 1 && T28::g_main.hashes[0] == 0x26800651u && St().treadDropped0 == 2 && St().treadDropped1 == 2);
		// E. the in-game line carries the counts; the "treads" line once the drops moved
		Sleep(150);
		FarmGuard_TestTimes(50, 100);
		e.gridSeq = 2;
		FarmGuard_OnPresent(e); Sleep(150); FarmGuard_OnPresent(e);
		CHECK(LogHas("treads (0784) on: type-0x28 records 7, dropped 4 (2 + 2), passed 3 (effect player's treads 1, unreadable 0)"));
		CHECK(LogHas("farm_guard: treads (ticket 0784): 7 type-0x28 records through the filter in this process: dropped 4 (ART_DEF_VEFFECT_TANK_TREADS 2, ART_DEF_VEFFECT_TANK_TREADS_02 2)"));
		// F. an unreadable payload: passed (the listener decides), counted
		T28::Reset();
		FarmGuard_TreadPass()((void*)&T28::g_main, nullptr, (void*)0x10);
		CHECK(St().treadFaults == 1 && T28::g_main.calls == 1);
		// G. a new registration of the main object (its node of an earlier start-up beside it): the old one retires, the new
		// one gets the filter
		const uint32_t tn2 = TRegister((uint32_t)(uintptr_t)&T28::g_main, LST);
		FarmGuard_OnPresent(e);
		CHECK(tnodes[tn].fn == INERT() && tnodes[tn2].fn == PASS && St().treadRetired == 1 && St().treadSwaps == 2);
		T28::Reset();
		TWalk(evOther); TWalk(evT0);
		CHECK(T28::g_main.calls == 1 && T28::g_scene.calls == 2);
		// H. a held lock: nothing written; then the key off: the listener's own address back, every record passed
		e.treadFilter = 0; tLock = 1;
		FarmGuard_OnPresent(e);
		CHECK(tnodes[tn2].fn == PASS);
		T28::Reset();
		TWalk(evT0);                                                    // switched off before its node was restored: passed
		CHECK(T28::g_main.calls == 1 && St().treadOff == 1);
		tLock = 0;
		FarmGuard_OnPresent(e);
		CHECK(tnodes[tn2].fn == LST && St().treadRestored == 1 && LogHas("farm_guard: treads (ticket 0784) off: node 2 holds the EXE's listener again"));
		CHECK(LogHas("farm_guard: treads (ticket 0784): off: the EXE's tread effect is drawn by the game"));
		T28::Reset();
		TWalk(evT0);
		CHECK(T28::g_main.calls == 1 && T28::g_scene.calls == 1);
		// I. the owner (the proxy): the node word is the shared page's tread stub; at the unload the stub reaches the EXE's
		// listener and the node gets it back when the lock is free
		SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
		FarmGuard_TestForgetRole();
		ResetWorld(scratch.c_str());
		FarmGuard_TestUseTread(&tw);
		char why[400] = "";
		CHECK(FarmGuard_OwnerAttach("proxy", why, sizeof(why)));
		char fwhy[160] = "";
		const FgShared* sh = (const FgShared*)FarmGuard_FindShared(fwhy, sizeof(fwhy));
		CHECK(sh && sh->version == 3u && sh->treadStub == sh->self + 0x1080u);
		e = Env(1); e.treadFilter = 1;
		FarmGuard_OnTick(e);
		CHECK(tnodes[tn2].fn == sh->treadStub && sh->treadTarget == (LONG)PASS && sh->treadOrig == (LONG)LST);
		T28::Reset();
		TWalk(evT0); TWalk(evOther);
		CHECK(T28::g_main.calls == 1 && T28::g_main.hashes[0] == 0x26800651u);   // through the stub: dropped and passed
		tLock = 1;                                                      // the unload finds the lock held: the node keeps the stub
		FarmGuard_OwnerDetach();
		CHECK(sh->treadTarget == 0 && tnodes[tn2].fn == sh->treadStub);
		T28::Reset();
		TWalk(evT0);
		CHECK(T28::g_main.calls == 1 && T28::g_main.hashes[0] == h0);    // no owner: the stub jumps to the EXE's listener
		tLock = 0;
		FarmGuard_TestUse(&g_w, scratch.c_str());                       // the next boot
		FarmGuard_TestUseTread(&tw);
		CHECK(FarmGuard_OwnerAttach("proxy", why, sizeof(why)) && strstr(why, "found by VP64_FARM_GUARD") != nullptr);
		FarmGuard_OnTick(e);
		CHECK(sh->treadTarget == (LONG)PASS && tnodes[tn2].fn == sh->treadStub && St().treadSwaps == 0);   // re-armed
		FarmGuard_OwnerDetach();                                        // the lock free: the listener's own word back
		CHECK(tnodes[tn2].fn == LST);
		SetEnvironmentVariableW(FG_SHARED_ENV, nullptr);
		FarmGuard_TestForgetRole();
		FarmGuard_TestUseTread(nullptr);
		printf("farm_guard_test: the tread filter (ticket 0784): done\n");
	}

	if (g_fails) { printf("farm_guard_test: FAILED %d of %d checks\n--- log ---\n%s", g_fails, g_checks, g_logText.c_str()); return 1; }
	printf("farm_guard_test: OK %d checks\n", g_checks);
	return 0;
}
#endif
