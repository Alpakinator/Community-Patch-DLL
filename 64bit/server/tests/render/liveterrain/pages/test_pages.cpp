// test_pages.cpp - tests of ticket 0587's BuildPageWithWindows (server/src/render/srv_liveterrain_pages.*): an EXE height
// page of a field with live windows written over it, built without writing the field, must equal, level by level, the page
// bake::buildHeightPage builds from the field with the windows written (ApplyWindow's order). Written 2026-11-19 for this
// repository (GPL-3.0-or-later). Findings: research/05-graphics-composition/live-terrain.md 8.14.8.
//
// Synthetic fields (deterministic pseudo-random heights, no inputs): every page of each field, 11 levels and 1 level, for
// window sets inside a page, on the 128-sample page overlap, on the x and y seams (first and last columns and rows), one
// sample, two overlapping windows of two changes (the later one wins), and a window of two rectangles; fields wider than a
// page, narrower than a page in x or y (a page then reads the same sample more than once), wrapped and not. The field is
// checked unchanged after every build. Refusals: a rectangle outside the field, heights not w x h, a null window, a page past
// the count, an unsupported mip count, a field whose samples are not plots * 64; each leaves `out` empty.
//
//   test_pages.exe [case ...]      cases: equal refusals cost (default: all)
// Exit 0 all PASS, 1 a case failed, 2 bad arguments.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "srv_liveterrain_pages.h"

using namespace srvrender::liveterrain;
namespace c5b = c5r::bake;
namespace c5h = c5r::heightfield;

namespace {

int g_fail = 0;
#define CHECK(c)                                                                \
	do                                                                          \
	{                                                                           \
		if(!(c))                                                                \
		{                                                                       \
			std::printf("  CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); \
			g_fail++;                                                           \
		}                                                                       \
	} while(0)

uint32_t g_rng = 12345u;
uint32_t Rand()
{
	g_rng = g_rng * 1664525u + 1013904223u;
	return g_rng >> 8;
}

c5h::HeightGrid Field(uint32_t plotsX, uint32_t plotsY, bool wrap)
{
	c5h::HeightGrid g;
	g.mapW = plotsX;
	g.mapH = plotsY;
	g.samplesX = plotsX * c5h::kSamplesPerPlot;
	g.samplesY = plotsY * c5h::kSamplesPerPlot;
	g.wrapX = wrap;
	g.h.resize((size_t)g.samplesX * g.samplesY);
	for(size_t i = 0; i < g.h.size(); ++i)   // smooth-ish ground plus noise: every byte of the page's >> 8 moves
		g.h[i] = (uint16_t)(((i % g.samplesX) * 37 + (i / g.samplesX) * 53 + (Rand() & 0x3FF)) & 0xFFFF);
	g.mat.assign(g.h.size(), 0);
	return g;
}

Rect Win(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
	Rect r;
	r.x0 = x0;
	r.y0 = y0;
	r.w = w;
	r.h = h;
	r.heights.resize((size_t)w * h);
	r.materials.resize((size_t)w * h);
	for(size_t i = 0; i < r.heights.size(); ++i)
	{
		r.heights[i] = (uint16_t)(Rand() & 0xFFFF);
		r.materials[i] = (uint8_t)(Rand() & 0xFF);
	}
	return r;
}

// ApplyWindow's writes (srv_liveterrain.cpp), heights only: the reference field
void Write(c5h::HeightGrid& g, const std::vector<Rect>& w)
{
	for(const Rect& r : w)
		for(uint32_t y = 0; y < r.h; ++y)
			for(uint32_t x = 0; x < r.w; ++x)
				g.h[(size_t)(r.y0 + y) * g.samplesX + r.x0 + x] = r.heights[(size_t)y * r.w + x];
}

// every page of g, with `changes` written over it, against buildHeightPage of the written field; g checked unchanged
bool AllPagesEqual(const char* name, const c5h::HeightGrid& g, const std::vector<std::vector<Rect>>& changes, uint32_t mips)
{
	c5h::HeightGrid ref = g;
	std::vector<const std::vector<Rect>*> ws;
	for(const std::vector<Rect>& c : changes)
	{
		Write(ref, c);
		ws.push_back(&c);
	}
	const std::vector<uint16_t> before = g.h;
	uint32_t px = 0, py = 0;
	if(!c5b::heightPageCount(g, px, py).ok())
	{
		std::printf("  %s: heightPageCount failed\n", name);
		return false;
	}
	uint32_t same = 0, differ = 0;
	for(uint32_t y = 0; y < py; ++y)
		for(uint32_t x = 0; x < px; ++x)
		{
			c5b::HeightPageIndex p;
			p.px = x;
			p.py = y;
			c5b::HeightPage a, b;
			const c5r::Status sa = BuildPageWithWindows(g, p, mips, ws, a);
			const c5r::Status sb = c5b::buildHeightPage(ref, p, mips, b);
			if(sa.ok() && sb.ok() && a.levels == b.levels && a.index.px == x && a.index.py == y && a.levels.size() == mips)
				same++;
			else
			{
				differ++;
				std::printf("  %s: page (%u, %u) differs (%s / %s)\n", name, x, y, sa.str().c_str(), sb.str().c_str());
			}
		}
	const bool untouched = g.h == before;
	// the windows really move the pages (else the case proves nothing): the written field's pages differ from the field's
	bool moved = false;
	for(uint32_t y = 0; y < py && !moved; ++y)
		for(uint32_t x = 0; x < px && !moved; ++x)
		{
			c5b::HeightPageIndex p;
			p.px = x;
			p.py = y;
			c5b::HeightPage a, b;
			if(c5b::buildHeightPage(g, p, mips, a).ok() && c5b::buildHeightPage(ref, p, mips, b).ok() && a.levels != b.levels)
				moved = true;
		}
	std::printf("  %s (%ux%u plots, %s, %u level(s)): %u of %u page(s) equal, %u differ; field unchanged %s; the windows move a page %s\n",
	            name, g.mapW, g.mapH, g.wrapX ? "wrapped" : "not wrapped", mips, same, px * py, differ, untouched ? "yes" : "NO",
	            moved ? "yes" : "NO");
	return differ == 0 && same == px * py && untouched && moved;
}

void CaseEqual()
{
	struct F
	{
		uint32_t w, h;
		bool wrap;
	};
	const F fields[] = {{32, 20, true}, {32, 20, false}, {56, 40, true}, {10, 6, true}, {20, 12, false}, {40, 9, true}};
	for(const F& f : fields)
	{
		const c5h::HeightGrid g = Field(f.w, f.h, f.wrap);
		const uint32_t X = g.samplesX, Y = g.samplesY;
		for(uint32_t mips : {11u, 1u})
		{
			CHECK(AllPagesEqual("inside", g, {{Win(X / 3, Y / 3, 300 < X / 3 ? 300 : X / 3, 200 < Y / 3 ? 200 : Y / 3)}}, mips));
			if(X > 1000 && Y > 1000)
				CHECK(AllPagesEqual("page overlap", g, {{Win(830, 830, 140, 140)}}, mips));
			CHECK(AllPagesEqual("x seam, last columns", g, {{Win(X - 90, Y / 2, 90, 64)}}, mips));
			CHECK(AllPagesEqual("x seam, first columns", g, {{Win(0, Y / 2, 70, 64)}}, mips));
			CHECK(AllPagesEqual("row 0 and the last rows", g, {{Win(X / 2, 0, 64, 70)}, {Win(X / 4, Y - 66, 64, 66)}}, mips));
			CHECK(AllPagesEqual("one sample", g, {{Win(X / 2 + 1, Y / 2 + 3, 1, 1)}}, mips));
			{   // two changes over the same samples: the later one wins
				const Rect a = Win(X / 2 - 100, Y / 2 - 50, 200, 100);
				const Rect b = Win(X / 2 - 50, Y / 2 - 20, 200, 100 < Y / 2 ? 100 : Y / 2 - 1);
				CHECK(AllPagesEqual("two overlapping changes", g, {{a}, {b}}, mips));
			}
			CHECK(AllPagesEqual("a window of two rectangles", g, {{Win(10, 10, 50, 40), Win(X - 64, Y - 64, 64, 64)}}, mips));
			CHECK(AllPagesEqual("the whole field", g, {{Win(0, 0, X, Y)}}, mips));
		}
	}
}

void CaseRefusals()
{
	const c5h::HeightGrid g = Field(32, 20, true);
	c5b::HeightPageIndex p;
	c5b::HeightPage out;
	auto refused = [&](const char* name, const std::vector<std::vector<Rect>>& changes, c5b::HeightPageIndex at, uint32_t mips,
	                   c5r::Err want, const c5h::HeightGrid& field) {
		std::vector<const std::vector<Rect>*> ws;
		for(const std::vector<Rect>& c : changes)
			ws.push_back(&c);
		out.levels.assign(3, std::vector<uint8_t>(4, 7));
		const c5r::Status st = BuildPageWithWindows(field, at, mips, ws, out);
		const bool ok = !st.ok() && st.code == want && out.levels.empty();
		std::printf("  %s: %s (%s)\n", name, ok ? "refused, out empty" : "NOT AS EXPECTED", st.str().c_str());
		CHECK(ok);
	};
	refused("a rectangle past the right edge", {{Win(g.samplesX - 10, 0, 11, 4)}}, p, 11, c5r::Err::BadLayout, g);
	refused("a rectangle past the last row", {{Win(0, g.samplesY - 3, 4, 4)}}, p, 11, c5r::Err::BadLayout, g);
	refused("an empty rectangle", {{Win(5, 5, 0, 4)}}, p, 11, c5r::Err::BadLayout, g);
	{
		Rect r = Win(5, 5, 8, 8);
		r.heights.pop_back();
		refused("heights not w x h", {{r}}, p, 11, c5r::Err::BadLayout, g);
	}
	{
		std::vector<const std::vector<Rect>*> ws(1, nullptr);
		out.levels.assign(2, std::vector<uint8_t>(1, 1));
		const c5r::Status st = BuildPageWithWindows(g, p, 11, ws, out);
		std::printf("  a null window: %s (%s)\n", !st.ok() && out.levels.empty() ? "refused, out empty" : "NOT AS EXPECTED", st.str().c_str());
		CHECK(!st.ok() && st.code == c5r::Err::BadLayout && out.levels.empty());
	}
	c5b::HeightPageIndex past;
	past.px = 99;
	refused("a page past the count", {{Win(5, 5, 8, 8)}}, past, 11, c5r::Err::NotFound, g);
	refused("mip count 5", {{Win(5, 5, 8, 8)}}, p, 5, c5r::Err::Unsupported, g);
	c5h::HeightGrid bad = g;
	bad.samplesX -= 1;
	refused("samples not plots * 64", {{Win(5, 5, 8, 8)}}, p, 11, c5r::Err::BadLayout, bad);
	c5h::HeightGrid shortH = g;
	shortH.h.pop_back();
	refused("a field shorter than its size", {{Win(5, 5, 8, 8)}}, p, 11, c5r::Err::BadLayout, shortH);
}

void CaseCost()
{   // an indication only (one machine, single runs; COMMON 23): a Tiny field's page with a window against a plain build
	const c5h::HeightGrid g = Field(56, 40, true);
	const std::vector<Rect> w = {Win(2496, 704, 320, 384)};
	std::vector<const std::vector<Rect>*> ws(1, &w);
	c5b::HeightPageIndex p;
	p.px = 2;
	p.py = 0;
	double a = 1e9, b = 1e9;
	for(int rep = 0; rep < 5; ++rep)
	{
		c5b::HeightPage o;
		auto t0 = std::chrono::steady_clock::now();
		CHECK(BuildPageWithWindows(g, p, 11, ws, o).ok());
		auto t1 = std::chrono::steady_clock::now();
		CHECK(c5b::buildHeightPage(g, p, 11, o).ok());
		auto t2 = std::chrono::steady_clock::now();
		a = std::min(a, std::chrono::duration<double, std::milli>(t1 - t0).count());
		b = std::min(b, std::chrono::duration<double, std::milli>(t2 - t1).count());
	}
	std::printf("  one page, 11 levels: with the window %.2f ms, buildHeightPage %.2f ms (best of 5; an indication)\n", a, b);
}

}  // namespace

int main(int argc, char** argv)
{
	struct C
	{
		const char* name;
		void (*fn)();
	};
	const C cases[] = {{"equal", CaseEqual}, {"refusals", CaseRefusals}, {"cost", CaseCost}};
	std::vector<std::string> want;
	for(int i = 1; i < argc; ++i)
		want.push_back(argv[i]);
	for(const std::string& w : want)
	{
		bool known = false;
		for(const C& c : cases)
			known = known || w == c.name;
		if(!known)
		{
			std::printf("test_pages: no case matches '%s' (cases: equal refusals cost)\n", w.c_str());
			return 2;
		}
	}
	int failedCases = 0;
	for(const C& c : cases)
	{
		bool run = want.empty();
		for(const std::string& w : want)
			run = run || w == c.name;
		if(!run)
			continue;
		const int before = g_fail;
		std::printf("CASE %s\n", c.name);
		c.fn();
		const bool pass = g_fail == before;
		failedCases += pass ? 0 : 1;
		std::printf("CASE %s %s\n", c.name, pass ? "PASS" : "FAIL");
	}
	std::printf("test_pages: %d check(s) failed, %d case(s) failed\n", g_fail, failedCases);
	return g_fail ? 1 : 0;
}
