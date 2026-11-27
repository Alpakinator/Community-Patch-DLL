// test_exefield.cpp - offline tests of the EXE field write (server/src/render/srv_liveterrain_exefield.*, ticket 4905,
// epic 0465). Written 2026-11-19 by w6-4905 for this repository (GPL-3.0-or-later).
//
// A mock GridHeightmap (object, both directories and pools, the layout of live-terrain.md P4) lives in this process's
// own memory, inside one 4 GB window (its 32-bit pointers plus ExeFieldTarget::ptrHigh), chosen to be the window of
// this program's image so that an image page can be offered as a target. The writer reaches it through
// ReadProcessMemory / WriteProcessMemory / VirtualQueryEx on GetCurrentProcess(), the same calls as on the EXE.
// Every case checks the mock field bit for bit afterwards (the window written, nothing else changed; or nothing
// changed at all on a refusal).
//
//   test_exefield.exe [case]     exit 0 all passed, 1 a check failed, 2 no case matches
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "srv_liveterrain_exefield.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace srvrender::liveterrain;
using c5r::heightfield::HeightGrid;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...)                                                                    \
	do                                                                                   \
	{                                                                                    \
		++g_checks;                                                                      \
		if(!(c))                                                                         \
		{                                                                                \
			++g_fail;                                                                    \
			printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c);                        \
			printf(__VA_ARGS__);                                                         \
			printf("\n");                                                                \
		}                                                                                \
	} while(0)

static uint32_t g_rng = 0x4905u;
static uint32_t Rand()
{
	g_rng = g_rng * 1664525u + 1013904223u;
	return g_rng >> 8;
}

// ---------------------------------------------------------------- the mock grid
static const size_t kPage = 4096;
static size_t Up(size_t n) { return (n + kPage - 1) & ~(kPage - 1); }

struct Mock
{
	uint8_t* base = nullptr;
	size_t size = 0;
	uint64_t high = 0;
	uint8_t *obj = nullptr, *dir16 = nullptr, *pool16 = nullptr, *dir8 = nullptr, *pool8 = nullptr;
	uint32_t tilesX = 0, tilesY = 0, nTiles = 0;
	HeightGrid field;   // what the mock holds at the start (the server's "before")
	~Mock()
	{
		if(base)
			VirtualFree(base, 0, MEM_RELEASE);
	}
	uint32_t lo(const void* p) const { return (uint32_t)((uint64_t)(uintptr_t)p & 0xFFFFFFFFull); }
	ExeFieldTarget target() const;
	uint32_t idx(int layer, uint32_t tile) const
	{
		uint32_t v;
		memcpy(&v, (layer ? dir8 : dir16) + (size_t)tile * 8 + 4, 4);
		return v;
	}
	uint16_t h(uint32_t x, uint32_t y) const
	{
		const uint32_t t = (y / 16) * tilesX + x / 16;
		uint16_t v;
		memcpy(&v, pool16 + ((size_t)idx(0, t) * 256 + (y % 16) * 16 + (x % 16)) * 2, 2);
		return v;
	}
	uint8_t m(uint32_t x, uint32_t y) const
	{
		const uint32_t t = (y / 16) * tilesX + x / 16;
		return pool8[(size_t)idx(1, t) * 256 + (y % 16) * 16 + (x % 16)];
	}
	uint8_t* sampleAddr16(uint32_t x, uint32_t y) const
	{
		const uint32_t t = (y / 16) * tilesX + x / 16;
		return pool16 + ((size_t)idx(0, t) * 256 + (y % 16) * 16 + (x % 16)) * 2;
	}
	void put32(size_t off, uint32_t v) { memcpy(obj + off, &v, 4); }
};

// This program's own image: base and SizeOfImage (the "EXE image" no write may touch).
static void OwnImage(uint64_t& base, uint64_t& size)
{
	const uint8_t* b = (const uint8_t*)GetModuleHandleW(nullptr);
	uint32_t lfanew, soi;
	memcpy(&lfanew, b + 0x3C, 4);
	memcpy(&soi, b + lfanew + 24 + 56, 4);
	base = (uint64_t)(uintptr_t)b;
	size = soi;
}
ExeFieldTarget Mock::target() const
{
	ExeFieldTarget t;
	t.process = GetCurrentProcess();
	t.gridAddr = (uint64_t)(uintptr_t)obj;
	t.ptrHigh = high;
	OwnImage(t.exeBase, t.exeImageSize);
	return t;
}

// Reserves `size` bytes inside the 4 GB window of this program's image (so ptrHigh is the same for the image).
static uint8_t* AllocInImageWindow(size_t size, uint64_t& high)
{
	const uint64_t img = (uint64_t)(uintptr_t)GetModuleHandleW(nullptr);
	high = img & ~0xFFFFFFFFull;
	for(uint64_t off = 0x01000000ull; off + size < 0x100000000ull; off += 0x01000000ull)
	{
		void* p = VirtualAlloc((void*)(uintptr_t)(high + off), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if(p)
			return (uint8_t*)p;
	}
	return nullptr;
}

// A field of mapW x mapH plots with a deterministic pattern; scrambled: the pool slots in reverse tile order.
static bool MakeMock(Mock& mk, uint32_t mapW, uint32_t mapH, bool scrambled)
{
	HeightGrid& g = mk.field;
	g.mapW = mapW;
	g.mapH = mapH;
	g.samplesX = mapW * 64;
	g.samplesY = mapH * 64;
	g.ox = g.samplesX * 0.5f;
	g.oy = g.samplesY * 0.433f;
	const size_t N = (size_t)g.samplesX * g.samplesY;
	g.h.resize(N);
	g.mat.resize(N);
	for(uint32_t y = 0; y < g.samplesY; ++y)
		for(uint32_t x = 0; x < g.samplesX; ++x)
		{
			g.h[g.index(x, y)] = (uint16_t)(x * 131u + y * 977u + (x * y) % 61u);
			g.mat[g.index(x, y)] = (uint8_t)((x / 7 + y / 5) % 41);
		}
	mk.tilesX = g.samplesX / 16;
	mk.tilesY = g.samplesY / 16;
	mk.nTiles = mk.tilesX * mk.tilesY;
	const size_t dirB = (size_t)mk.nTiles * 8, p16B = (size_t)mk.nTiles * 512, p8B = (size_t)mk.nTiles * 256;
	mk.size = kPage + Up(dirB) * 2 + Up(p16B) + Up(p8B);
	mk.base = AllocInImageWindow(mk.size, mk.high);
	if(!mk.base)
		return false;
	mk.obj = mk.base;
	mk.dir16 = mk.obj + kPage;
	mk.pool16 = mk.dir16 + Up(dirB);
	mk.dir8 = mk.pool16 + Up(p16B);
	mk.pool8 = mk.dir8 + Up(dirB);
	c5r::heightfield::MemoryImage img;
	if(!c5r::heightfield::buildMemoryImage(g, false, img).ok() || img.obj.size() < c5r::heightfield::kGridObjectSize ||
	   img.pool16.size() != p16B || img.pool8.size() != p8B)
		return false;
	memcpy(mk.obj, img.obj.data(), img.obj.size());
	for(uint32_t t = 0; t < mk.nTiles; ++t)
	{
		const uint32_t slot = scrambled ? mk.nTiles - 1 - t : t;
		uint8_t e[8] = {};
		memcpy(e + 4, &slot, 4);
		memcpy(mk.dir16 + (size_t)t * 8, e, 8);
		memcpy(mk.dir8 + (size_t)t * 8, e, 8);
		memcpy(mk.pool16 + (size_t)slot * 512, &img.pool16[(size_t)t * 512], 512);
		memcpy(mk.pool8 + (size_t)slot * 256, &img.pool8[(size_t)t * 256], 256);
	}
	mk.put32(0x0C + 0x10, mk.lo(mk.dir16));
	mk.put32(0x0C + 0x1C, mk.lo(mk.pool16));
	mk.put32(0x0C + 0x20, mk.nTiles);
	mk.put32(0x0C + 0x24, mk.nTiles);
	mk.put32(0x4C + 0x10, mk.lo(mk.dir8));
	mk.put32(0x4C + 0x1C, mk.lo(mk.pool8));
	mk.put32(0x4C + 0x20, mk.nTiles);
	mk.put32(0x4C + 0x24, mk.nTiles);
	return true;
}

struct OwnRect
{
	uint32_t x0, y0, w, h;
	std::vector<uint16_t> heights;
	std::vector<uint8_t> materials;
};
static OwnRect RandRect(const HeightGrid& g, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
	OwnRect r{x0, y0, w, h, {}, {}};
	r.heights.resize((size_t)w * h);
	r.materials.resize((size_t)w * h);
	for(uint32_t y = 0; y < h; ++y)
		for(uint32_t x = 0; x < w; ++x)
		{
			const size_t s = g.index(x0 + x, y0 + y);
			// about one sample in eight keeps its value: unchanged samples inside a window are normal
			const bool keep = (Rand() & 7) == 0;
			r.heights[(size_t)y * w + x] = keep ? g.h[s] : (uint16_t)(Rand() & 0xFFFF);
			r.materials[(size_t)y * w + x] = keep ? g.mat[s] : (uint8_t)(Rand() % 41);
		}
	return r;
}
static HeightGrid Applied(const HeightGrid& g, const std::vector<OwnRect>& win)
{
	HeightGrid e = g;
	for(const OwnRect& r : win)
		for(uint32_t y = 0; y < r.h; ++y)
			for(uint32_t x = 0; x < r.w; ++x)
			{
				e.h[e.index(r.x0 + x, r.y0 + y)] = r.heights[(size_t)y * r.w + x];
				e.mat[e.index(r.x0 + x, r.y0 + y)] = r.materials[(size_t)y * r.w + x];
			}
	return e;
}
// Samples of the mock that differ from `e` (0 = bit for bit equal).
static uint64_t Diff(const Mock& mk, const HeightGrid& e)
{
	uint64_t n = 0;
	for(uint32_t y = 0; y < e.samplesY; ++y)
		for(uint32_t x = 0; x < e.samplesX; ++x)
			if(mk.h(x, y) != e.h[e.index(x, y)] || mk.m(x, y) != e.mat[e.index(x, y)])
				++n;
	return n;
}
static bool Has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
static void Protect(Mock& m, uint32_t x, uint32_t y, DWORD prot);

// ---------------------------------------------------------------- cases
static void CaseWriteReadback()
{
	for(int scrambled = 0; scrambled < 2; ++scrambled)
	{
		Mock mk;
		CHECK(MakeMock(mk, 4, 6, scrambled != 0), "mock");
		std::vector<OwnRect> win = {RandRect(mk.field, 37, 50, 70, 45), RandRect(mk.field, 130, 200, 64, 64)};
		const HeightGrid exp = Applied(mk.field, win);
		ExeFieldResult r = WriteExeFieldWindow(mk.target(), mk.field, win);
		printf("  %s\n", r.line.c_str());
		CHECK(r.status == ExeFieldStatus::Written, "%s", r.line.c_str());
		CHECK(Diff(mk, exp) == 0, "the mock differs from before + window in %llu samples", (unsigned long long)Diff(mk, exp));
		uint64_t hc = 0, mc = 0;
		for(size_t s = 0; s < exp.h.size(); ++s)
		{
			hc += exp.h[s] != mk.field.h[s];
			mc += exp.mat[s] != mk.field.mat[s];
		}
		CHECK(r.heightsChanged == hc && r.materialsChanged == mc, "counted %llu/%llu, expected %llu/%llu",
		      (unsigned long long)r.heightsChanged, (unsigned long long)r.materialsChanged, (unsigned long long)hc,
		      (unsigned long long)mc);
		CHECK(r.samples == 70u * 45u + 64u * 64u, "samples %llu", (unsigned long long)r.samples);
		CHECK(!Has(r.line, "LOUD"), "%s", r.line.c_str());
		// the same window again: nothing differs any more, nothing is written (the before is now exp)
		ExeFieldResult r2 = WriteExeFieldWindow(mk.target(), exp, win);
		CHECK(r2.status == ExeFieldStatus::Written && r2.writes == 0, "%s", r2.line.c_str());
		// the old before against the new EXE field: refused, nothing changes
		ExeFieldResult r3 = WriteExeFieldWindow(mk.target(), mk.field, win);
		CHECK(r3.status == ExeFieldStatus::Refused && Has(r3.line, "differs from the server's"), "%s", r3.line.c_str());
		CHECK(Diff(mk, exp) == 0, "changed by a refused call");
	}
}

static void CaseWrapSplit()
{
	Mock mk;
	CHECK(MakeMock(mk, 4, 6, true), "mock");
	const uint32_t SX = mk.field.samplesX;
	// a window across the x seam comes as two rectangles: [SX-26, SX) and [0, 20)
	std::vector<OwnRect> win = {RandRect(mk.field, SX - 26, 64, 26, 40), RandRect(mk.field, 0, 64, 20, 40)};
	const HeightGrid exp = Applied(mk.field, win);
	ExeFieldResult r = WriteExeFieldWindow(mk.target(), mk.field, win);
	printf("  %s\n", r.line.c_str());
	CHECK(r.status == ExeFieldStatus::Written, "%s", r.line.c_str());
	CHECK(Diff(mk, exp) == 0, "differs in %llu samples", (unsigned long long)Diff(mk, exp));
	CHECK(mk.h(SX - 1, 64) == exp.h[exp.index(SX - 1, 64)] && mk.h(0, 64) == exp.h[exp.index(0, 64)], "seam samples");
	CHECK(mk.h(SX - 27, 64) == mk.field.h[mk.field.index(SX - 27, 64)] && mk.h(20, 64) == mk.field.h[mk.field.index(20, 64)],
	      "outside the window");
}

static void CaseNothingToDo()
{
	Mock mk;
	CHECK(MakeMock(mk, 4, 6, false), "mock");
	std::vector<OwnRect> none;
	ExeFieldResult r = WriteExeFieldWindow(mk.target(), mk.field, none);
	CHECK(r.status == ExeFieldStatus::Written && r.writes == 0 && r.samples == 0, "%s", r.line.c_str());
	OwnRect same{16, 16, 32, 32, {}, {}};
	for(uint32_t y = 0; y < 32; ++y)
		for(uint32_t x = 0; x < 32; ++x)
		{
			same.heights.push_back(mk.field.h[mk.field.index(16 + x, 16 + y)]);
			same.materials.push_back(mk.field.mat[mk.field.index(16 + x, 16 + y)]);
		}
	std::vector<OwnRect> w1 = {same};
	ExeFieldResult r2 = WriteExeFieldWindow(mk.target(), mk.field, w1);
	CHECK(r2.status == ExeFieldStatus::Written && r2.writes == 0, "%s", r2.line.c_str());
	CHECK(Diff(mk, mk.field) == 0, "changed");
}

// One refusal: `prep` breaks the mock or the inputs; the call must refuse with `expect` in the line and change nothing.
static void Refusal(const char* name, const char* expect,
                    const std::function<void(Mock&, HeightGrid&, std::vector<OwnRect>&, ExeFieldTarget&)>& prep,
                    const std::function<void(Mock&)>& undo = nullptr)
{
	Mock mk;
	CHECK(MakeMock(mk, 4, 6, true), "mock");
	HeightGrid before = mk.field;
	std::vector<OwnRect> win = {RandRect(mk.field, 40, 40, 48, 48)};
	ExeFieldTarget t = mk.target();
	prep(mk, before, win, t);
	// the mock as it is after prep, outside the deliberate break
	std::vector<uint8_t> snap16(mk.pool16, mk.pool16 + (size_t)mk.nTiles * 512), snap8(mk.pool8, mk.pool8 + (size_t)mk.nTiles * 256);
	ExeFieldResult r = WriteExeFieldWindow(t, before, win);
	printf("  [%s] %s\n", name, r.line.c_str());
	CHECK(r.status == ExeFieldStatus::Refused, "[%s] status %s: %s", name, ExeFieldStatusName(r.status), r.line.c_str());
	CHECK(r.writes == 0, "[%s] %u writes", name, r.writes);
	CHECK(Has(r.line, "LOUD: exefield: Refused") && Has(r.line, expect), "[%s] line lacks '%s': %s", name, expect, r.line.c_str());
	if(undo)
		undo(mk);
	CHECK(memcmp(snap16.data(), mk.pool16, snap16.size()) == 0 && memcmp(snap8.data(), mk.pool8, snap8.size()) == 0,
	      "[%s] the pools changed", name);
}

static void CaseRefusals()
{
	typedef std::vector<OwnRect> W;
	Refusal("no target", "no target", [](Mock&, HeightGrid&, W&, ExeFieldTarget& t) { t.gridAddr = 0; });
	Refusal("not ready", "ready byte", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) { m.obj[0x09] = 0; });
	Refusal("object unreadable", "unreadable", [](Mock&, HeightGrid&, W&, ExeFieldTarget& t) { t.gridAddr = 0x10; });
	Refusal("other size", "the EXE's field is", [](Mock&, HeightGrid& b, W& w, ExeFieldTarget&) {
		b.samplesY -= 64;
		b.h.resize((size_t)b.samplesX * b.samplesY);
		b.mat.resize(b.h.size());
		w[0].y0 = 0;
	});
	Refusal("server field without materials", "materials", [](Mock&, HeightGrid& b, W&, ExeFieldTarget&) { b.mat.clear(); });
	Refusal("rect outside", "outside the field", [](Mock& m, HeightGrid&, W& w, ExeFieldTarget&) { w[0].x0 = m.field.samplesX - 10; });
	Refusal("negative x as from a bad cast", "outside the field", [](Mock&, HeightGrid&, W& w, ExeFieldTarget&) { w[0].x0 = (uint32_t)-5; });
	Refusal("arrays short", "heights and", [](Mock&, HeightGrid&, W& w, ExeFieldTarget&) { w[0].materials.pop_back(); });
	Refusal("u16 constant tile", "not pooled", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		const uint16_t st = 1;
		memcpy(m.dir16 + (size_t)(3 * m.tilesX + 3) * 8, &st, 2);
	});
	Refusal("u8 constant tile", "not pooled", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		const uint16_t st = 1;
		memcpy(m.dir8 + (size_t)(4 * m.tilesX + 4) * 8, &st, 2);
	});
	Refusal("pool index past the pool", "past the pool", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		const uint32_t bad = m.nTiles + 5;
		memcpy(m.dir16 + (size_t)(3 * m.tilesX + 4) * 8 + 4, &bad, 4);
	});
	Refusal("pool size above capacity", "capacity", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) { m.put32(0x0C + 0x20, m.nTiles + 1); });
	Refusal("null pool", "pool 0x00000000", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) { m.put32(0x4C + 0x1C, 0); });
	Refusal("pool past 4 GB", "32-bit address space", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) { m.put32(0x0C + 0x1C, 0xFFFFF000u); });
	Refusal("EXE field differs (height)", "differs from the server's", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		uint8_t* p = m.sampleAddr16(50, 51);
		p[0] ^= 1;
	});
	Refusal("EXE field differs (material, outside the rect but in a touched tile)", "differs from the server's",
	        [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		        const uint32_t t = (32 / 16) * m.tilesX + 32 / 16;   // tile (2,2) holds samples 32..47; the rect starts at 40
		        m.pool8[(size_t)m.idx(1, t) * 256 + 0] ^= 3;
	        });
	Refusal("read-only pool page", "protect 0x2", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		DWORD old;
		VirtualProtect(m.sampleAddr16(60, 60), 1, PAGE_READONLY, &old);
	}, [](Mock& m) {
		DWORD old;
		VirtualProtect(m.sampleAddr16(60, 60), 1, PAGE_READWRITE, &old);
	});
	Refusal("executable pool page", "protect 0x40", [](Mock& m, HeightGrid&, W&, ExeFieldTarget&) {
		DWORD old;
		VirtualProtect(m.sampleAddr16(60, 60), 1, PAGE_EXECUTE_READWRITE, &old);
	}, [](Mock& m) {
		DWORD old;
		VirtualProtect(m.sampleAddr16(60, 60), 1, PAGE_READWRITE, &old);
	});
	Refusal("wrong grid pointer (cam_gridptr)", "[cam_gridptr]",[](Mock& m, HeightGrid&, W&, ExeFieldTarget& t) {
		t.gridPtrAddr = (uint64_t)(uintptr_t)(m.obj + 0x100);   // a dword of the object page that holds 0
	});
	// MC #6744 (a): guard pages (a modifier bit on PAGE_READWRITE) are refused before any pool read, so the guard is
	// never touched
	// (its own block: the snapshot of Refusal would touch the guard page itself)
	{
		Mock m;
		CHECK(MakeMock(m, 4, 6, true), "mock");
		std::vector<OwnRect> win = {RandRect(m.field, 40, 40, 48, 48)};
		std::vector<uint8_t> snap16(m.pool16, m.pool16 + (size_t)m.nTiles * 512), snap8(m.pool8, m.pool8 + (size_t)m.nTiles * 256);
		uint8_t* gp = m.sampleAddr16(60, 60);   // computed before the guard (it reads the directory only)
		DWORD old;
		VirtualProtect(gp, 1, PAGE_READWRITE | PAGE_GUARD, &old);
		ExeFieldResult r = WriteExeFieldWindow(m.target(), m.field, win);
		printf("  [guard page] %s\n", r.line.c_str());
		MEMORY_BASIC_INFORMATION mb = {};
		VirtualQuery(gp, &mb, sizeof(mb));
		const DWORD after = mb.Protect;
		VirtualProtect(gp, 1, PAGE_READWRITE, &old);
		CHECK(r.status == ExeFieldStatus::Refused && r.writes == 0 && Has(r.line, "LOUD: exefield: Refused") &&
		      Has(r.line, "protect 0x104"), "[guard page] %s", r.line.c_str());
		CHECK(after == (PAGE_READWRITE | PAGE_GUARD), "[guard page] the guard was consumed (protect 0x%lX): a read touched it", after);
		CHECK(memcmp(snap16.data(), m.pool16, snap16.size()) == 0 && memcmp(snap8.data(), m.pool8, snap8.size()) == 0,
		      "[guard page] the pools changed");
	}
	// MC #6744 (c): the image range unknown
	Refusal("EXE image size unknown", "image range is not known", [](Mock&, HeightGrid&, W&, ExeFieldTarget& t) { t.exeImageSize = 0; });
}

// A mapped view as the u8 pool: read-write (MEM_MAPPED) and write-copy (MC #6744 (a): PAGE_WRITECOPY) are refused.
static void CaseMappedView()
{
	const struct { DWORD access; const char* expect; const char* name; } kinds[] = {
		{FILE_MAP_ALL_ACCESS, "type 0x40000 protect 0x4", "read-write view"},
		{FILE_MAP_COPY, "protect 0x8", "write-copy view"},
	};
	for(const auto& kind : kinds)
	{
		Mock mk;
		CHECK(MakeMock(mk, 4, 6, false), "mock");
		const size_t p8B = (size_t)mk.nTiles * 256;
		HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)Up(p8B), nullptr);
		CHECK(sec != nullptr, "CreateFileMapping");
		uint8_t* rw = sec ? (uint8_t*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, Up(p8B)) : nullptr;
		if(rw)
		{
			memcpy(rw, mk.pool8, p8B);   // the section holds the u8 pool; the view under test maps it as `kind`
			UnmapViewOfFile(rw);
		}
		uint8_t* view = nullptr;
		for(uint64_t off = 0x01000000ull; sec && !view && off + p8B < 0x100000000ull; off += 0x01000000ull)
			view = (uint8_t*)MapViewOfFileEx(sec, kind.access, 0, 0, Up(p8B), (void*)(uintptr_t)(mk.high + off));
		CHECK(view != nullptr, "[%s] MapViewOfFileEx in the image's window", kind.name);
		if(!view)
			return;
		mk.put32(0x4C + 0x1C, mk.lo(view));
		std::vector<OwnRect> win = {RandRect(mk.field, 40, 40, 20, 20)};
		std::vector<uint8_t> snap(view, view + p8B), snap16(mk.pool16, mk.pool16 + (size_t)mk.nTiles * 512);
		ExeFieldResult r = WriteExeFieldWindow(mk.target(), mk.field, win);
		printf("  [%s] %s\n", kind.name, r.line.c_str());
		CHECK(r.status == ExeFieldStatus::Refused && r.writes == 0 && Has(r.line, kind.expect) && Has(r.line, "LOUD"), "[%s] %s",
		      kind.name, r.line.c_str());
		CHECK(memcmp(snap.data(), view, p8B) == 0 && memcmp(snap16.data(), mk.pool16, snap16.size()) == 0, "[%s] changed",
		      kind.name);
		UnmapViewOfFile(view);
		CloseHandle(sec);
	}
}

// MC #6744 (c): the containment rule itself.
static void CaseRunInside()
{
	std::string why;
	const uint64_t P0 = 0x10000000, P1 = 0x10100000, I0 = 0x00400000, I1 = 0x01400000;
	CHECK(ExeFieldRunInside(P0, 512, P0, P1, I0, I1, why), "a run at the pool's start: %s", why.c_str());
	CHECK(ExeFieldRunInside(P1 - 512, 512, P0, P1, I0, I1, why), "a run at the pool's end: %s", why.c_str());
	CHECK(!ExeFieldRunInside(P0 - 256, 512, P0, P1, I0, I1, why) && Has(why, "outside its pool"), "below the pool: %s", why.c_str());
	CHECK(!ExeFieldRunInside(P1 - 256, 512, P0, P1, I0, I1, why) && Has(why, "outside its pool"), "past the pool: %s", why.c_str());
	CHECK(!ExeFieldRunInside(P0, 0, P0, P1, I0, I1, why), "an empty run");
	CHECK(!ExeFieldRunInside(~0ull - 100, 512, 0, ~0ull, I0, I1, why), "a wrapping run");
	// a pool that lies in the image (a corrupt pointer) is refused by the image rule
	CHECK(!ExeFieldRunInside(I0 + 0x1000, 512, I0, I1, I0, I1, why) && Has(why, "inside the EXE's image"), "in the image: %s", why.c_str());
	CHECK(!ExeFieldRunInside(I1 - 256, 512, I0, I1 + 0x1000, I0, I1, why) && Has(why, "inside the EXE's image"), "over the image's end: %s",
	      why.c_str());
	CHECK(!ExeFieldRunInside(P0, 512, P0, P1, I0, I0, why) && Has(why, "not known"), "image unknown: %s", why.c_str());
}

// MC #6744 (d): every call queries again: a region flipped to read-only between two calls on one target is refused.
static void CaseRequery()
{
	Mock mk;
	CHECK(MakeMock(mk, 4, 6, true), "mock");
	const ExeFieldTarget t = mk.target();
	std::vector<OwnRect> w1 = {RandRect(mk.field, 40, 40, 48, 48)};
	ExeFieldResult r1 = WriteExeFieldWindow(t, mk.field, w1);
	CHECK(r1.status == ExeFieldStatus::Written && r1.regionChecks > 0, "first call: %s", r1.line.c_str());
	const HeightGrid now = Applied(mk.field, w1);
	Protect(mk, 60, 60, PAGE_READONLY);
	std::vector<OwnRect> w2 = {RandRect(now, 40, 40, 48, 48)};
	std::vector<uint8_t> snap16(mk.pool16, mk.pool16 + (size_t)mk.nTiles * 512);
	ExeFieldResult r2 = WriteExeFieldWindow(t, now, w2);
	printf("  %s\n", r2.line.c_str());
	CHECK(r2.status == ExeFieldStatus::Refused && r2.writes == 0 && Has(r2.line, "protect 0x2") && r2.regionChecks > 0,
	      "second call: %s", r2.line.c_str());
	Protect(mk, 60, 60, PAGE_READWRITE);
	CHECK(memcmp(snap16.data(), mk.pool16, snap16.size()) == 0 && Diff(mk, now) == 0, "the second call changed the field");
}

// A page of this program's image as the u16 pool: refused by type (MEM_IMAGE), the image unchanged.
static void CaseImagePage()
{
	Mock mk;
	CHECK(MakeMock(mk, 4, 6, false), "mock");
	const uint64_t img = (uint64_t)(uintptr_t)GetModuleHandleW(nullptr);
	const uint8_t* page = (const uint8_t*)(uintptr_t)(img + 0x2000);
	CHECK((img & ~0xFFFFFFFFull) == mk.high, "image and mock in one 4 GB window");
	// tile (0,0) of the u16 layer -> slot 0 of a one-tile pool on the image page; the server's before holds its bytes
	mk.put32(0x0C + 0x1C, mk.lo(page));
	mk.put32(0x0C + 0x20, 1);
	mk.put32(0x0C + 0x24, 1);
	const uint32_t zero = 0;
	memcpy(mk.dir16 + 4, &zero, 4);
	HeightGrid before = mk.field;
	for(uint32_t y = 0; y < 16; ++y)
		for(uint32_t x = 0; x < 16; ++x)
			memcpy(&before.h[before.index(x, y)], page + (y * 16 + x) * 2, 2);
	std::vector<uint8_t> snap(page, page + 512);
	std::vector<OwnRect> win = {RandRect(before, 2, 2, 8, 8)};
	// the image range rule (MC #6744 (c)) refuses it first
	ExeFieldResult r = WriteExeFieldWindow(mk.target(), before, win);
	printf("  %s\n", r.line.c_str());
	CHECK(r.status == ExeFieldStatus::Refused && r.writes == 0 && Has(r.line, "inside the EXE's image"), "%s", r.line.c_str());
	CHECK(memcmp(snap.data(), page, 512) == 0, "the image page changed");
	// with a wrong image range (another module's, say), VirtualQueryEx's type still refuses it
	ExeFieldTarget t2 = mk.target();
	t2.exeBase = 0x10000;
	t2.exeImageSize = 0x1000;
	ExeFieldResult r2 = WriteExeFieldWindow(t2, before, win);
	printf("  %s\n", r2.line.c_str());
	CHECK(r2.status == ExeFieldStatus::Refused && r2.writes == 0 && Has(r2.line, "type 0x1000000"), "%s", r2.line.c_str());
	CHECK(memcmp(snap.data(), page, 512) == 0, "the image page changed");
}

// The hooks: change the target between the checks and the writes.
struct HookCtx
{
	Mock* mk;
	std::function<void(Mock&, uint32_t)> fn;
};
static void HookThunk(void* c, uint32_t k)
{
	HookCtx* h = (HookCtx*)c;
	h->fn(*h->mk, k);
}
// Two rectangles in tile rows 0 and 10 of an unscrambled mock: separate pool runs, so separate writes (u16 then u8).
static std::vector<OwnRect> TwoRuns(const HeightGrid& g)
{
	return {RandRect(g, 0, 0, 32, 16), RandRect(g, 0, 160, 32, 16)};
}
static void Protect(Mock& m, uint32_t x, uint32_t y, DWORD prot)
{
	DWORD old;
	VirtualProtect(m.sampleAddr16(x, y), 1, prot, &old);
}

static void CaseRollback()
{
	// write 1 fails (its page went read-only after the region check; NtWriteVirtualMemory does not make it writable):
	// write 0 restored, the read-only page neither written nor made writable
	{
		Mock mk;
		CHECK(MakeMock(mk, 4, 6, false), "mock");
		std::vector<OwnRect> win = TwoRuns(mk.field);
		HookCtx hc{&mk, [](Mock& m, uint32_t k) {
			           if(k == 1)
				           Protect(m, 0, 160, PAGE_READONLY);
		           }};
		ExeFieldTarget t = mk.target();
		t.testBeforeWrite = HookThunk;
		t.testCtx = &hc;
		ExeFieldResult r = WriteExeFieldWindow(t, mk.field, win);
		printf("  %s\n", r.line.c_str());
		CHECK(r.status == ExeFieldStatus::RolledBack && Has(r.line, "restored and read back equal"), "%s", r.line.c_str());
		CHECK(r.writes == 3, "writes %u (1 write, 1 failed write, 1 restore)", r.writes);
		CHECK(Has(r.line, "NtWriteVirtualMemory"), "%s", r.line.c_str());
		MEMORY_BASIC_INFORMATION mb = {};
		VirtualQuery(mk.sampleAddr16(0, 160), &mb, sizeof(mb));
		CHECK(mb.Protect == PAGE_READONLY, "the read-only page's protect is 0x%lX after the write", mb.Protect);
		Protect(mk, 0, 160, PAGE_READWRITE);
		CHECK(Diff(mk, mk.field) == 0, "not restored: %llu samples differ", (unsigned long long)Diff(mk, mk.field));
	}
	// the restore cannot be made either: RollbackFailed, LOUD
	{
		Mock mk;
		CHECK(MakeMock(mk, 4, 6, false), "mock");
		std::vector<OwnRect> win = TwoRuns(mk.field);
		HookCtx hc{&mk, [](Mock& m, uint32_t k) {
			           if(k == 1)
			           {
				           Protect(m, 0, 0, PAGE_READONLY);
				           Protect(m, 0, 160, PAGE_READONLY);
			           }
		           }};
		ExeFieldTarget t = mk.target();
		t.testBeforeWrite = HookThunk;
		t.testCtx = &hc;
		ExeFieldResult r = WriteExeFieldWindow(t, mk.field, win);
		printf("  %s\n", r.line.c_str());
		CHECK(r.status == ExeFieldStatus::RollbackFailed && Has(r.line, "LOUD") && Has(r.line, "NOT all restored"), "%s",
		      r.line.c_str());
		Protect(mk, 0, 0, PAGE_READWRITE);
		Protect(mk, 0, 160, PAGE_READWRITE);
	}
	// the object changes before the first write: refused, nothing written
	{
		Mock mk;
		CHECK(MakeMock(mk, 4, 6, false), "mock");
		std::vector<OwnRect> win = TwoRuns(mk.field);
		HookCtx hc{&mk, [](Mock& m, uint32_t k) {
			           if(k == 0)
				           m.put32(0x0C + 0x20, m.nTiles - 1);
		           }};
		ExeFieldTarget t = mk.target();
		t.testBeforeWrite = HookThunk;
		t.testCtx = &hc;
		ExeFieldResult r = WriteExeFieldWindow(t, mk.field, win);
		printf("  %s\n", r.line.c_str());
		CHECK(r.status == ExeFieldStatus::Refused && Has(r.line, "changed during the write") && r.writes == 0, "%s", r.line.c_str());
		CHECK(Diff(mk, mk.field) == 0, "changed");
	}
	// the object changes after the first write (a load began): no restore into a field being rebuilt, RollbackFailed
	{
		Mock mk;
		CHECK(MakeMock(mk, 4, 6, false), "mock");
		std::vector<OwnRect> win = TwoRuns(mk.field);
		HookCtx hc{&mk, [](Mock& m, uint32_t k) {
			           if(k == 1)
				           m.obj[0x09] = 0;
		           }};
		ExeFieldTarget t = mk.target();
		t.testBeforeWrite = HookThunk;
		t.testCtx = &hc;
		ExeFieldResult r = WriteExeFieldWindow(t, mk.field, win);
		printf("  %s\n", r.line.c_str());
		CHECK(r.status == ExeFieldStatus::RollbackFailed && Has(r.line, "changed during the write") && Has(r.line, "not restored"),
		      "%s", r.line.c_str());
		CHECK(r.writes == 1, "writes %u", r.writes);
	}
}

static void CaseOpenTarget()
{
	ExeFieldTarget t;
	std::string why;
	CHECK(!OpenExeFieldTarget(0, t, why) && Has(why, "pid"), "%s", why.c_str());
	// this test program is not the EXE: refused (64-bit, no 32-bit main module, or another stamp)
	CHECK(!OpenExeFieldTarget(GetCurrentProcessId(), t, why) && !t.process, "opened: %s", why.c_str());
	printf("  own process: %s\n", why.c_str());
	CloseExeFieldTarget(t);
}

// Cost on a huge-sized mock (128 x 84 grid plots): a window of 8 x 8 plots and one of 16 x 16 plots, median of 7.
static void CaseCost()
{
	Mock mk;
	CHECK(MakeMock(mk, 128, 84, false), "huge mock");
	if(!mk.base)
		return;
	// what one VirtualQueryEx costs at the start and near the end of the mock's one big region (the pools)
	{
		LARGE_INTEGER f, a, b;
		QueryPerformanceFrequency(&f);
		const uint8_t* at[2] = {mk.pool16, mk.base + mk.size - 4096};
		for(int i = 0; i < 2; ++i)
		{
			MEMORY_BASIC_INFORMATION m;
			QueryPerformanceCounter(&a);
			for(int k = 0; k < 20; ++k)
				VirtualQueryEx(GetCurrentProcess(), at[i], &m, sizeof(m));
			QueryPerformanceCounter(&b);
			printf("  VirtualQueryEx at %s of a %zu MB region: %.3f ms per call (region size reported %zu MB)\n",
			       i ? "the last page" : "the u16 pool", mk.size >> 20,
			       (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 20, (size_t)m.RegionSize >> 20);
		}
	}
	const uint32_t sides[2] = {8, 16};
	HeightGrid cur = mk.field;   // the server's field as the writes go on
	for(uint32_t side : sides)
	{
		std::vector<double> ms;
		for(int k = 0; k < 7; ++k)
		{
			std::vector<OwnRect> win = {RandRect(cur, 64 * 40 + 13, 64 * 30 + 7, side * 64, side * 64)};
			ExeFieldResult r = WriteExeFieldWindow(mk.target(), cur, win);
			CHECK(r.status == ExeFieldStatus::Written, "%s", r.line.c_str());
			if(r.status != ExeFieldStatus::Written)
				break;
			cur = Applied(cur, win);
			ms.push_back(r.ms);
			if(k == 0)
				printf("  %ux%u plots: %s\n", side, side, r.line.c_str());
		}
		std::sort(ms.begin(), ms.end());
		if(!ms.empty())
			printf("  COST %ux%u plots (%u samples): median %.2f ms, min %.2f, max %.2f over %zu calls\n", side, side,
			       side * side * 4096, ms[ms.size() / 2], ms.front(), ms.back(), ms.size());
		CHECK(Diff(mk, cur) == 0, "huge mock differs");
	}
}

struct Case
{
	const char* name;
	void (*fn)();
};
// Ticket 4966: the EXE build from (stamp, SizeOfImage) and its addresses from the build table, on a made-up module base.
static void CaseBuildAddresses()
{
	const uint64_t base = 0x10000000ull;
	ExeFieldAddrs a;
	std::string why;
	// an unknown EXE (and the stamp of build 4390913 with another SizeOfImage) is refused, naming what it read
	CHECK(!ExeFieldAddressesFor(0x12345678u, 0x06519000u, base, a, why) && Has(why, "0x12345678") &&
	      Has(why, "no CivilizationV_DX11.exe build"), "%s", why.c_str());
	CHECK(!ExeFieldAddressesFor(0x546CD0A8u, 0x01000000u, base, a, why) && a.build == -1, "%s", why.c_str());
	// build 4390913: the build-4390913 VAs
	CHECK(ExeFieldAddressesFor(0x546CD0A8u, 0x06519000u, base, a, why), "%s", why.c_str());
	CHECK(a.build == 0 && a.grid == base + (0xD91270u - 0x400000u) && a.gridPtr == base + (0xD90FC4u - 0x400000u) &&
	      a.constants == base + (0xD90FBCu - 0x400000u), "build %d", a.build);
	// build 26577625: its own addresses from the table, the block's layout as in build 4390913
	CHECK(ExeFieldAddressesFor(0x6AF3837Du, 0x063A2000u, base, a, why), "%s", why.c_str());
	CHECK(a.build == 1 && a.constants != base + (0xD90FBCu - 0x400000u), "build %d", a.build);
	CHECK(a.gridPtr - a.constants == 0x8 && a.grid - a.constants == 0x2B4, "layout %llX %llX",
	      (unsigned long long)(a.gridPtr - a.constants), (unsigned long long)(a.grid - a.constants));
}

static const Case kCases[] = {
	{"write_readback", CaseWriteReadback}, {"wrap_split", CaseWrapSplit},   {"nothing_to_do", CaseNothingToDo},
	{"refusals", CaseRefusals},            {"mapped_view", CaseMappedView}, {"image_page", CaseImagePage},
	{"rollback", CaseRollback},            {"open_target", CaseOpenTarget}, {"run_inside", CaseRunInside},
	{"requery", CaseRequery},              {"cost", CaseCost},              {"build_addresses", CaseBuildAddresses},
};

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	int ran = 0, failedCases = 0;
	for(const Case& c : kCases)
	{
		if(argc > 1 && strcmp(argv[1], c.name) != 0)
			continue;
		++ran;
		const int f0 = g_fail;
		printf("-- %s\n", c.name);
		c.fn();
		const bool ok = g_fail == f0;
		failedCases += !ok;
		printf("CASE %s %s\n", c.name, ok ? "PASS" : "FAIL");
	}
	if(!ran)
	{
		printf("no case matches '%s'\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	printf("%d checks, %d failed, %d case(s) failed\n", g_checks, g_fail, failedCases);
	return g_fail ? 1 : 0;
}
