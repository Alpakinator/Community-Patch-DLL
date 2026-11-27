// exefield_live.cpp - TEST ONLY: the live check of the EXE field write (ticket 4905, acceptance 3) on a Goldberg test
// instance. Written 2026-11-19 by w6-4905 for this repository (GPL-3.0-or-later). Never shipped, never on the Steam
// install. It runs the product code (srv_liveterrain_exefield.cpp: OpenExeFieldTarget, WriteExeFieldWindow) against the
// real EXE, and reads the whole field before and after to check the write bit for bit.
//
//   exefield_live.exe raise   --pid P --centres FILE --plots x,y [x,y ...] --dz WORLD [--radius S] --out DIR
//   exefield_live.exe restore --pid P --out DIR
//   exefield_live.exe dump    --pid P --file F.c5hf
// raise: dumps the field (DIR/before.c5hf), builds one rectangle per plot (a disc of `radius` samples around the plot
//   centre raised by dz world units with a cosine falloff over the outer third; the disc's inner two thirds take the
//   material of the centre sample), writes it through WriteExeFieldWindow, dumps again (DIR/after.c5hf) and compares:
//   inside the window equal to the window, outside equal to before. The window goes to DIR/window.bin.
// restore: the inverse window (before's samples of the same rectangles) through WriteExeFieldWindow against the current
//   field, then the dump must equal DIR/before.c5hf everywhere.
// CENTRES: the output of render/evidence/0465/4901/centres.lua (first line "W H wrap", then "x y wx wy" per plot).
// Exit 0 ok, 1 a check failed, 2 usage.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "srv_liveterrain_exefield.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

using namespace srvrender::liveterrain;
using c5r::heightfield::HeightGrid;

static bool Rd(HANDLE h, uint64_t at, void* out, size_t n)
{
	SIZE_T got = 0;
	return ReadProcessMemory(h, (LPCVOID)(uintptr_t)at, out, n, &got) && got == n;
}

// The whole field of the EXE (both layers), read from outside (data reads only).
static bool DumpField(const ExeFieldTarget& t, HeightGrid& g, std::string& why)
{
	HANDLE h = (HANDLE)t.process;
	std::vector<uint8_t> obj(c5r::heightfield::kGridObjectSize);
	if(!Rd(h, t.gridAddr, obj.data(), obj.size()))
		return why = "object unreadable", false;
	c5r::heightfield::GridObject o;
	c5r::Status s = c5r::heightfield::parseGridObject(obj.data(), obj.size(), o);
	if(!s.ok())
		return why = s.str(), false;
	std::vector<uint8_t> d16((size_t)o.h16.tilesX * o.h16.tilesY * 8), p16((size_t)o.h16.poolSize * 512);
	std::vector<uint8_t> d8((size_t)o.b8.tilesX * o.b8.tilesY * 8), p8((size_t)o.b8.poolSize * 256);
	if(!Rd(h, o.h16.dirPtr, d16.data(), d16.size()) || !Rd(h, o.h16.poolPtr, p16.data(), p16.size()) ||
	   !Rd(h, o.b8.dirPtr, d8.data(), d8.size()) || !Rd(h, o.b8.poolPtr, p8.data(), p8.size()))
		return why = "a directory or pool is unreadable", false;
	int32_t wrap = 0;
	Rd(h, t.exeBase + (0xD90FBCu - 0x400000u), &wrap, 4);
	s = c5r::heightfield::gridFromMemory(obj.data(), obj.size(), d16.data(), d16.size(), p16.data(), p16.size(), d8.data(),
	                                     d8.size(), p8.data(), p8.size(), wrap == 1, g);
	if(!s.ok())
		return why = s.str(), false;
	return true;
}

static bool SaveTap(const HeightGrid& g, const std::string& path)
{
	std::vector<uint8_t> b;
	if(!c5r::heightfield::writeTap(g, b).ok())
		return false;
	FILE* f = fopen(path.c_str(), "wb");
	if(!f)
		return false;
	const bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
	fclose(f);
	return ok;
}
static bool LoadFile(const std::string& path, std::vector<uint8_t>& b)
{
	FILE* f = fopen(path.c_str(), "rb");
	if(!f)
		return false;
	fseek(f, 0, SEEK_END);
	b.resize((size_t)ftell(f));
	fseek(f, 0, SEEK_SET);
	const bool ok = fread(b.data(), 1, b.size(), f) == b.size();
	fclose(f);
	return ok;
}

struct Rect
{
	uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
	std::vector<uint16_t> heights;
	std::vector<uint8_t> materials;
};
static bool SaveRects(const std::vector<Rect>& v, const std::string& path)
{
	FILE* f = fopen(path.c_str(), "wb");
	if(!f)
		return false;
	uint32_t n = (uint32_t)v.size();
	fwrite(&n, 4, 1, f);
	for(const Rect& r : v)
	{
		fwrite(&r.x0, 4, 4, f);
		fwrite(r.heights.data(), 2, r.heights.size(), f);
		fwrite(r.materials.data(), 1, r.materials.size(), f);
	}
	fclose(f);
	return true;
}
static bool LoadRects(const std::string& path, std::vector<Rect>& v)
{
	std::vector<uint8_t> b;
	if(!LoadFile(path, b) || b.size() < 4)
		return false;
	size_t at = 0;
	uint32_t n;
	memcpy(&n, &b[at], 4);
	at += 4;
	v.resize(n);
	for(Rect& r : v)
	{
		if(at + 16 > b.size())
			return false;
		memcpy(&r.x0, &b[at], 16);
		at += 16;
		const size_t k = (size_t)r.w * r.h;
		if(at + k * 3 > b.size())
			return false;
		r.heights.resize(k);
		r.materials.resize(k);
		memcpy(r.heights.data(), &b[at], k * 2);
		at += k * 2;
		memcpy(r.materials.data(), &b[at], k);
		at += k;
	}
	return true;
}

// Compares `now` with `before` + window: returns the differing samples inside and outside the window.
static void Compare(const HeightGrid& before, const std::vector<Rect>& win, const HeightGrid& now, uint64_t& inside,
                    uint64_t& outside, uint64_t& windowSamples)
{
	HeightGrid e = before;
	std::vector<uint8_t> in(before.h.size(), 0);
	windowSamples = 0;
	for(const Rect& r : win)
		for(uint32_t y = 0; y < r.h; ++y)
			for(uint32_t x = 0; x < r.w; ++x)
			{
				const size_t s = e.index(r.x0 + x, r.y0 + y);
				e.h[s] = r.heights[(size_t)y * r.w + x];
				e.mat[s] = r.materials[(size_t)y * r.w + x];
				windowSamples += !in[s];
				in[s] = 1;
			}
	inside = outside = 0;
	for(size_t s = 0; s < e.h.size(); ++s)
		if(e.h[s] != now.h[s] || e.mat[s] != now.mat[s])
			(in[s] ? inside : outside)++;
}

static const char* Arg(int argc, char** argv, const char* name)
{
	for(int i = 2; i + 1 < argc; ++i)
		if(!strcmp(argv[i], name))
			return argv[i + 1];
	return nullptr;
}

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(argc < 2)
		return printf("usage: see the file header\n"), 2;
	const std::string cmd = argv[1];
	const char* pidS = Arg(argc, argv, "--pid");
	if(!pidS)
		return printf("--pid required\n"), 2;
	ExeFieldTarget t;
	std::string why;
	if(!OpenExeFieldTarget((uint32_t)strtoul(pidS, nullptr, 10), t, why))
		return printf("OpenExeFieldTarget: %s\n", why.c_str()), 1;
	printf("target: exe base 0x%llX, grid 0x%llX\n", (unsigned long long)t.exeBase, (unsigned long long)t.gridAddr);
	int rc = 0;
	HeightGrid cur;
	if(!DumpField(t, cur, why))
	{
		printf("dump: %s\n", why.c_str());
		CloseExeFieldTarget(t);
		return 1;
	}
	printf("field: %ux%u samples (grid plots %ux%u), wrap %d\n", cur.samplesX, cur.samplesY, cur.mapW, cur.mapH, cur.wrapX ? 1 : 0);
	if(cmd == "dump")
	{
		const char* file = Arg(argc, argv, "--file");
		rc = file && SaveTap(cur, file) ? 0 : 1;
	}
	else if(cmd == "raise")
	{
		const char* out = Arg(argc, argv, "--out");
		const char* cen = Arg(argc, argv, "--centres");
		const char* dzS = Arg(argc, argv, "--dz");
		const char* radS = Arg(argc, argv, "--radius");
		if(!out || !cen || !dzS)
			return printf("--out, --centres and --dz required\n"), 2;
		const double dz = atof(dzS), R = radS ? atof(radS) : 30.0, du = dz / cur.sz * 255.0;
		std::vector<uint8_t> cb;
		if(!LoadFile(cen, cb))
			return printf("centres unreadable\n"), 1;
		std::map<std::pair<int, int>, std::pair<double, double>> centre;
		{
			std::string s(cb.begin(), cb.end());
			size_t p = s.find('\n');
			while(p != std::string::npos && p + 1 < s.size())
			{
				const size_t q = s.find('\n', p + 1);
				const std::string ln = s.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
				int x, y;
				double wx, wy;
				if(sscanf(ln.c_str(), "%d %d %lf %lf", &x, &y, &wx, &wy) == 4)
					centre[std::make_pair(x, y)] = std::make_pair((wx + cur.ox) / cur.sx, (wy + cur.oy) / cur.sy);
				p = q;
			}
		}
		std::vector<Rect> win;
		for(int i = 2; i < argc; ++i)
		{
			if(strcmp(argv[i], "--plots"))
				continue;
			for(int k = i + 1; k < argc && argv[k][0] != '-'; ++k)
			{
				int px, py;
				if(sscanf(argv[k], "%d,%d", &px, &py) != 2 || !centre.count(std::make_pair(px, py)))
					return printf("plot %s: no centre\n", argv[k]), 1;
				const double cc = centre[std::make_pair(px, py)].first, cr = centre[std::make_pair(px, py)].second;
				const int x0 = (int)floor(cc - R) - 1, x1 = (int)ceil(cc + R) + 1, y0 = (int)floor(cr - R / 0.866) - 1,
				          y1 = (int)ceil(cr + R / 0.866) + 1;
				if(x0 < 0 || y0 < 0 || x1 >= (int)cur.samplesX || y1 >= (int)cur.samplesY)
					return printf("plot %s: its disc crosses the field's edge or the seam (not in this test)\n", argv[k]), 1;
				Rect r;
				r.x0 = (uint32_t)x0;
				r.y0 = (uint32_t)y0;
				r.w = (uint32_t)(x1 - x0 + 1);
				r.h = (uint32_t)(y1 - y0 + 1);
				const uint8_t cm = cur.mat[cur.index((uint32_t)lround(cc), (uint32_t)lround(cr))];
				for(uint32_t y = 0; y < r.h; ++y)
					for(uint32_t x = 0; x < r.w; ++x)
					{
						const uint32_t c = r.x0 + x, rr = r.y0 + y;
						const size_t s = cur.index(c, rr);
						const double d = hypot(c - cc, (rr - cr) * 0.866);
						double f = 0;
						if(d <= R * 2 / 3)
							f = 1;
						else if(d <= R)
							f = 0.5 * (1 + cos(3.14159265358979 * (d - R * 2 / 3) / (R / 3)));
						const double v = cur.h[s] + du * f;
						r.heights.push_back((uint16_t)(v > 65535 ? 65535 : v < 0 ? 0 : lround(v)));
						r.materials.push_back(d <= R * 2 / 3 ? cm : cur.mat[s]);
					}
				printf("plot (%d,%d): centre sample (%.1f, %.1f), rect %u,%u %ux%u, centre u16 %u -> %u, material -> %u\n", px, py,
				       cc, cr, r.x0, r.y0, r.w, r.h, cur.h[cur.index((uint32_t)lround(cc), (uint32_t)lround(cr))],
				       r.heights[(size_t)((uint32_t)lround(cr) - r.y0) * r.w + ((uint32_t)lround(cc) - r.x0)], cm);
				win.push_back(r);
			}
		}
		const std::string dir = out;
		if(!SaveTap(cur, dir + "/before.c5hf") || !SaveRects(win, dir + "/window.bin"))
			return printf("cannot write into %s\n", out), 1;
		ExeFieldResult r = WriteExeFieldWindow(t, cur, win);
		printf("%s\n", r.line.c_str());
		HeightGrid after;
		if(!DumpField(t, after, why))
			return printf("dump after: %s\n", why.c_str()), 1;
		SaveTap(after, dir + "/after.c5hf");
		uint64_t in = 0, outS = 0, n = 0;
		Compare(cur, win, after, in, outS, n);
		printf("compare after the write: %llu window samples, %llu differ inside, %llu differ outside\n", (unsigned long long)n,
		       (unsigned long long)in, (unsigned long long)outS);
		rc = r.ok() && in == 0 && outS == 0 ? 0 : 1;
	}
	else if(cmd == "restore")
	{
		const char* out = Arg(argc, argv, "--out");
		if(!out)
			return printf("--out required\n"), 2;
		const std::string dir = out;
		std::vector<Rect> win;
		std::vector<uint8_t> bb;
		HeightGrid before;
		if(!LoadRects(dir + "/window.bin", win) || !LoadFile(dir + "/before.c5hf", bb) ||
		   !c5r::heightfield::readTap(bb.data(), bb.size(), before).ok())
			return printf("window.bin or before.c5hf unreadable in %s\n", out), 1;
		for(Rect& r : win)
			for(uint32_t y = 0; y < r.h; ++y)
				for(uint32_t x = 0; x < r.w; ++x)
				{
					const size_t s = before.index(r.x0 + x, r.y0 + y);
					r.heights[(size_t)y * r.w + x] = before.h[s];
					r.materials[(size_t)y * r.w + x] = before.mat[s];
				}
		ExeFieldResult r = WriteExeFieldWindow(t, cur, win);
		printf("%s\n", r.line.c_str());
		HeightGrid now;
		if(!DumpField(t, now, why))
			return printf("dump after restore: %s\n", why.c_str()), 1;
		uint64_t diff = 0;
		for(size_t s = 0; s < now.h.size(); ++s)
			diff += now.h[s] != before.h[s] || now.mat[s] != before.mat[s];
		printf("compare after the restore: %llu samples differ from before.c5hf (whole field)\n", (unsigned long long)diff);
		rc = r.ok() && diff == 0 ? 0 : 1;
	}
	else
		rc = 2;
	CloseExeFieldTarget(t);
	return rc;
}
