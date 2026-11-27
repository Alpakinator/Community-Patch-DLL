// srv_liveterrain_exefield.cpp - live terrain: the server writes a changed window into the EXE's height field (epic
// 0465, ticket 4905). Written 2026-11-19 by w6-4905 for this repository (GPL-3.0-or-later). Design and refusal list:
// srv_liveterrain_exefield.h and research/05-graphics-composition/live-terrain.md 8.13. Data writes only (D4): the
// EXE's heap pools, checked page by page with VirtualQueryEx; nothing of the EXE's image is ever written.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include "srv_liveterrain_exefield.h"
// Ticket 4966: the EXE build table. The product (server/build_server.py, /DVP_REQUIRE_EXE_TABLES) and the exe-field tests
// compile with the generated vp_exe_tables.gen.h on the include path. The other test builders that compile every file
// of server/src/render (about a dozen) do not have it, and they never open a live EXE: they get a build of this file that
// refuses every EXE ("built without the EXE build table").
#if __has_include("vp_exe_tables.gen.h")
#include "../../../exebuild/include/vp_exe_symbols.h"
#define SRV_EXEFIELD_HAVE_TABLES 1
#elif defined(VP_REQUIRE_EXE_TABLES)
#error "vp_exe_tables.gen.h is not on the include path: run tools/exebuild/emit_header.py (setting exe_tables)"
#else
#include "../../../exebuild/include/vp_exe_build.h"
#define SRV_EXEFIELD_HAVE_TABLES 0
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>


namespace srvrender {
namespace liveterrain {

namespace {

const uint32_t kTileEdge = 16, kTileSamples = 256;
const uint64_t kMaxWindowSamples = 64ull << 20;   // far above 8.1's bound (256 plots = 1 M samples)

std::string Fmt(const char* fmt, ...)
{
	char b[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	return b;
}

bool Rd(HANDLE h, uint64_t at, void* out, size_t n)
{
	SIZE_T got = 0;
	return ReadProcessMemory(h, (LPCVOID)(uintptr_t)at, out, n, &got) && got == n;
}

// The writes go through NtWriteVirtualMemory: unlike kernel32's WriteProcessMemory it never changes a page's protection
// to make a read-only or executable page writable for the write, so a page that is not writable makes the write fail.
typedef LONG(NTAPI* NtWriteVirtualMemoryFn)(HANDLE, PVOID, const void*, SIZE_T, SIZE_T*);
NtWriteVirtualMemoryFn NtWrite()
{
	static NtWriteVirtualMemoryFn fn = (NtWriteVirtualMemoryFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtWriteVirtualMemory");
	return fn;
}
// One data write; false (why) when it did not write all n bytes. `got` receives the bytes written.
bool Wr(HANDLE h, uint64_t at, const void* src, size_t n, size_t& got, std::string& why)
{
	SIZE_T g = 0;
	const LONG st = NtWrite()(h, (PVOID)(uintptr_t)at, src, n, &g);
	got = g;
	if(st < 0 || g != n)
	{
		why = Fmt("NtWriteVirtualMemory(0x%llX, %zu) wrote %zu bytes, status 0x%08lX", (unsigned long long)at, n, (size_t)g,
		          (unsigned long)st);
		return false;
	}
	return true;
}

// Ranges already checked in this call (a VirtualQueryEx result covers its whole region). A query costs time linear in
// the region's size beyond the queried address (0.84 ms at the start of a 127 MB region, live-terrain.md 8.13), so each
// region is queried once per call, not once per write.
struct Checked
{
	std::vector<std::pair<uint64_t, uint64_t>> ok;   // [lo, hi)
	uint64_t covers(uint64_t p) const
	{
		for(const auto& r : ok)
			if(p >= r.first && p < r.second)
				return r.second;
		return 0;
	}
};

// Every region of [at, at + n) committed, MEM_PRIVATE, protect exactly PAGE_READWRITE (no guard, no image, no view).
bool RegionOk(HANDLE h, uint64_t at, size_t n, uint32_t& checks, std::string& why, Checked* cache = nullptr)
{
	const uint64_t end = at + n;
	uint64_t p = at;
	while(p < end)
	{
		if(cache)
			if(const uint64_t hi = cache->covers(p))
			{
				p = hi;
				continue;
			}
		MEMORY_BASIC_INFORMATION m = {};
		++checks;
		if(!VirtualQueryEx(h, (LPCVOID)(uintptr_t)p, &m, sizeof(m)))
		{
			why = Fmt("VirtualQueryEx(0x%llX) failed, error %lu", (unsigned long long)p, GetLastError());
			return false;
		}
		if(m.State != MEM_COMMIT || m.Type != MEM_PRIVATE || m.Protect != PAGE_READWRITE)
		{
			why = Fmt("0x%llX is not committed private read-write data (state 0x%lX type 0x%lX protect 0x%lX)",
			          (unsigned long long)p, m.State, m.Type, m.Protect);
			return false;
		}
		const uint64_t rend = (uint64_t)(uintptr_t)m.BaseAddress + m.RegionSize;
		if(rend <= p)
		{
			why = Fmt("VirtualQueryEx(0x%llX) gave an empty region", (unsigned long long)p);
			return false;
		}
		if(cache)
			cache->ok.push_back(std::make_pair((uint64_t)(uintptr_t)m.BaseAddress, rend));
		p = rend;
	}
	return true;
}

// The key fields of the object that must not change between the checks and the end of the write.
struct Key
{
	uint32_t sx = 0, sy = 0;
	uint32_t dir[2] = {0, 0}, pool[2] = {0, 0}, poolSize[2] = {0, 0}, tilesX[2] = {0, 0}, tilesY[2] = {0, 0};
	uint8_t ready = 0;
	bool operator==(const Key& o) const { return memcmp(this, &o, sizeof(Key)) == 0; }
};
Key KeyOf(const c5r::heightfield::GridObject& g, uint8_t ready)
{
	Key k;
	memset(&k, 0, sizeof(k));
	k.sx = g.samplesX;
	k.sy = g.samplesY;
	const c5r::heightfield::GridObject::Layer* L[2] = {&g.h16, &g.b8};
	for(int i = 0; i < 2; ++i)
	{
		k.dir[i] = L[i]->dirPtr;
		k.pool[i] = L[i]->poolPtr;
		k.poolSize[i] = L[i]->poolSize;
		k.tilesX[i] = L[i]->tilesX;
		k.tilesY[i] = L[i]->tilesY;
	}
	k.ready = ready;
	return k;
}

// Reads and checks the object. False (why): unreadable, or a check of parseGridObject or of the pools.
bool ReadObject(const ExeFieldTarget& t, c5r::heightfield::GridObject& g, Key& key, std::string& why)
{
	uint8_t obj[c5r::heightfield::kGridObjectSize];
	if(!Rd(t.process, t.gridAddr, obj, sizeof(obj)))
	{
		why = Fmt("the GridHeightmap object at 0x%llX is unreadable (error %lu)", (unsigned long long)t.gridAddr, GetLastError());
		return false;
	}
	c5r::Status s = c5r::heightfield::parseGridObject(obj, sizeof(obj), g);
	if(!s.ok())
	{
		why = "the GridHeightmap object: " + s.str();
		return false;
	}
	const c5r::heightfield::GridObject::Layer* L[2] = {&g.h16, &g.b8};
	for(int i = 0; i < 2; ++i)
	{
		const uint32_t tileBytes = i == 0 ? 512u : 256u;
		const char* name = i == 0 ? "u16 layer" : "u8 layer";
		if(!L[i]->dirPtr || !L[i]->poolPtr || !L[i]->poolSize || L[i]->poolSize > L[i]->poolCapacity)
		{
			why = Fmt("%s: directory 0x%08X, pool 0x%08X, pool size %u tiles of capacity %u", name, L[i]->dirPtr, L[i]->poolPtr,
			          L[i]->poolSize, L[i]->poolCapacity);
			return false;
		}
		if((uint64_t)L[i]->dirPtr + (uint64_t)L[i]->tilesX * L[i]->tilesY * 8 > 0x100000000ull ||
		   (uint64_t)L[i]->poolPtr + (uint64_t)L[i]->poolSize * tileBytes > 0x100000000ull)
		{
			why = Fmt("%s: directory or pool past the 32-bit address space", name);
			return false;
		}
	}
	key = KeyOf(g, obj[0x09]);
	if(t.gridPtrAddr)
	{
		uint32_t p = 0;
		if(!Rd(t.process, t.gridPtrAddr, &p, 4) || (uint64_t)p != t.gridAddr)
		{
			why = Fmt("[cam_gridptr] (%s) holds 0x%08X, not the grid 0x%llX",
			          VpExeBuildInfoOf(t.exeBuild) ? VpExeBuildInfoOf(t.exeBuild)->label : "unknown build", p,
			          (unsigned long long)t.gridAddr);
			return false;
		}
	}
	return true;
}

// One touched tile: its pool addresses and the old / new contents of both layers.
struct TileWork
{
	uint32_t tile = 0, idx[2] = {0, 0};
	uint64_t addr[2] = {0, 0};
	uint16_t old16[kTileSamples], new16[kTileSamples];
	uint8_t old8[kTileSamples], new8[kTileSamples];
	bool changed[2] = {false, false};
};

// A run of tiles of one layer whose pool slots follow each other (one read, one write).
struct Run
{
	int layer = 0;
	uint64_t addr = 0;
	std::vector<size_t> tiles;     // indices into the tile list, in pool order
};

void MakeRuns(std::vector<TileWork>& tw, int layer, bool changedOnly, std::vector<Run>& out)
{
	std::vector<size_t> order;
	for(size_t i = 0; i < tw.size(); ++i)
		if(!changedOnly || tw[i].changed[layer])
			order.push_back(i);
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return tw[a].idx[layer] < tw[b].idx[layer]; });
	for(size_t k = 0; k < order.size(); ++k)
	{
		const TileWork& w = tw[order[k]];
		if(!out.empty() && out.back().layer == layer && !out.back().tiles.empty())
		{
			const TileWork& last = tw[out.back().tiles.back()];
			if(w.idx[layer] == last.idx[layer] + 1)
			{
				out.back().tiles.push_back(order[k]);
				continue;
			}
		}
		Run r;
		r.layer = layer;
		r.addr = w.addr[layer];
		r.tiles.push_back(order[k]);
		out.push_back(r);
	}
}

size_t RunBytes(const Run& r) { return r.tiles.size() * (r.layer == 0 ? 512u : 256u); }

void Gather(const std::vector<TileWork>& tw, const Run& r, bool newData, std::vector<uint8_t>& buf)
{
	const size_t tb = r.layer == 0 ? 512 : 256;
	buf.resize(r.tiles.size() * tb);
	for(size_t k = 0; k < r.tiles.size(); ++k)
	{
		const TileWork& w = tw[r.tiles[k]];
		const void* src = r.layer == 0 ? (const void*)(newData ? w.new16 : w.old16) : (const void*)(newData ? w.new8 : w.old8);
		memcpy(&buf[k * tb], src, tb);
	}
}

double NowMs()
{
	static LARGE_INTEGER f = {};
	if(!f.QuadPart)
		QueryPerformanceFrequency(&f);
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

}  // namespace

const char* ExeFieldStatusName(ExeFieldStatus s)
{
	switch(s)
	{
	case ExeFieldStatus::Written: return "Written";
	case ExeFieldStatus::Refused: return "Refused";
	case ExeFieldStatus::RolledBack: return "RolledBack";
	case ExeFieldStatus::RollbackFailed: return "RollbackFailed";
	}
	return "?";
}

bool ExeFieldRunInside(uint64_t at, uint64_t n, uint64_t poolLo, uint64_t poolHi, uint64_t imageLo, uint64_t imageHi,
                       std::string& why)
{
	const uint64_t end = at + n;
	if(!n || end < at)
	{
		why = Fmt("an empty or wrapping run at 0x%llX (%llu bytes)", (unsigned long long)at, (unsigned long long)n);
		return false;
	}
	if(at < poolLo || end > poolHi)
	{
		why = Fmt("0x%llX..0x%llX lies outside its pool 0x%llX..0x%llX", (unsigned long long)at, (unsigned long long)end,
		          (unsigned long long)poolLo, (unsigned long long)poolHi);
		return false;
	}
	if(imageHi <= imageLo)
	{
		why = "the EXE's image range is not known (exeImageSize 0)";
		return false;
	}
	if(at < imageHi && end > imageLo)
	{
		why = Fmt("0x%llX..0x%llX lies inside the EXE's image 0x%llX..0x%llX", (unsigned long long)at, (unsigned long long)end,
		          (unsigned long long)imageLo, (unsigned long long)imageHi);
		return false;
	}
	return true;
}

bool OpenExeFieldTarget(uint32_t pid, ExeFieldTarget& out, std::string& why)
{
	out = ExeFieldTarget();
	if(!pid)
	{
		why = "the EXE's pid is not known";
		return false;
	}
	HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
	if(!h)
	{
		why = Fmt("OpenProcess(pid %u, VM read/write/operation, query) failed, error %lu", pid, GetLastError());
		return false;
	}
	HMODULE mods[1] = {};
	DWORD need = 0;
	if(!K32EnumProcessModulesEx(h, mods, sizeof(mods), &need, LIST_MODULES_32BIT) || !mods[0])
	{
		why = Fmt("EnumProcessModulesEx(pid %u, 32-bit) failed, error %lu", pid, GetLastError());
		CloseHandle(h);
		return false;
	}
	const uint64_t base = (uint64_t)(uintptr_t)mods[0];
	uint32_t lfanew = 0, sig = 0, stamp = 0, sizeOfImage = 0;
	// PE: signature, file header (TimeDateStamp at +8), optional header at +24 (SizeOfImage at its +56)
	if(!Rd(h, base + 0x3C, &lfanew, 4) || lfanew > 0x1000 || !Rd(h, base + lfanew, &sig, 4) || sig != 0x00004550u ||
	   !Rd(h, base + lfanew + 8, &stamp, 4) || !Rd(h, base + lfanew + 24 + 56, &sizeOfImage, 4) || !sizeOfImage)
	{
		why = Fmt("pid %u: the main module's PE header at 0x%llX is unreadable", pid, (unsigned long long)base);
		CloseHandle(h);
		return false;
	}
	ExeFieldAddrs ad;
	std::string bwhy;
	if(!ExeFieldAddressesFor(stamp, sizeOfImage, base, ad, bwhy))
	{
		why = Fmt("pid %u: %s: refused", pid, bwhy.c_str());
		CloseHandle(h);
		return false;
	}
	uint32_t p = 0;
	if(!Rd(h, ad.gridPtr, &p, 4) || (uint64_t)p != ad.grid)
	{
		why = Fmt("pid %u: [cam_gridptr 0x%llX] holds 0x%08X, not the rebased grid 0x%llX (%s)", pid,
		          (unsigned long long)ad.gridPtr, p, (unsigned long long)ad.grid,
		          VpExeBuildInfoOf(ad.build) ? VpExeBuildInfoOf(ad.build)->label : "unknown build");
		CloseHandle(h);
		return false;
	}
	out.process = h;
	out.exeBase = base;
	out.exeImageSize = sizeOfImage;
	out.gridAddr = ad.grid;
	out.gridPtrAddr = ad.gridPtr;
	out.constantsAddr = ad.constants;
	out.exeBuild = ad.build;
	out.ownsHandle = true;
	return true;
}

bool ExeFieldAddressesFor(uint32_t stamp, uint32_t sizeOfImage, uint64_t base, ExeFieldAddrs& out, std::string& why)
{
	out = ExeFieldAddrs();
	const int b = VpExeBuildFromHeader(stamp, sizeOfImage);
	if(b == kVpExeBuildUnknown)
	{
		why = Fmt("the main module (TimeDateStamp 0x%08X, SizeOfImage 0x%08X) is no CivilizationV_DX11.exe build the "
		          "patch knows (exebuild/include/vp_exe_build.h)", stamp, sizeOfImage);
		return false;
	}
#if !SRV_EXEFIELD_HAVE_TABLES
	(void)base;
	why = Fmt("%s: this binary was built without the EXE build table (a test build of server/src/render)",
	          VpExeBuildInfoOf(b)->label);
	return false;
#else
	// every symbol used, carried to this build
	static const int kNeed[] = {VPX_cam_wrapx, VPX_cam_gridptr, VPX_cam_h2w, VPX_lt_corners, VPX_lt_plots_w,
	                            VPX_lt_plots_h, VPX_lt_g, VPX_lt_ginv, VPX_lt_grid_samples_w, VPX_lt_grid_samples_h,
	                            VPX_cam_grid};
	for(size_t i = 0; i < sizeof(kNeed) / sizeof(kNeed[0]); ++i)
		if(!VpExeVa(b, kNeed[i]))
		{
			why = Fmt("%s: the EXE build table does not carry %s (tools/exebuild/gen_table.py, setting exe_tables)",
			          VpExeBuildLabel(b), VpExeSymName(kNeed[i]));
			return false;
		}
	// the block is read whole and parsed at build-4390913 offsets: each field must sit at its offset in this build too
	const uint32_t blk = VpExeVa(b, VPX_cam_wrapx), blk0 = kVpExeSymAnchor[VPX_cam_wrapx];
	for(size_t i = 1; i < sizeof(kNeed) / sizeof(kNeed[0]); ++i)
	{
		const uint32_t off = VpExeVa(b, kNeed[i]) - blk, off0 = kVpExeSymAnchor[kNeed[i]] - blk0;
		if(off != off0)
		{
			why = Fmt("%s: %s is at offset 0x%X of the grid constants block, not 0x%X as in build 4390913: the block's "
			          "layout changed", VpExeBuildLabel(b), VpExeSymName(kNeed[i]), off, off0);
			return false;
		}
	}
	out.build = b;
	out.grid = VpExeAddr(b, VPX_cam_grid, (uintptr_t)base);
	out.gridPtr = VpExeAddr(b, VPX_cam_gridptr, (uintptr_t)base);
	out.constants = VpExeAddr(b, VPX_cam_wrapx, (uintptr_t)base);
	return true;
#endif
}

void CloseExeFieldTarget(ExeFieldTarget& t)
{
	if(t.ownsHandle && t.process)
		CloseHandle((HANDLE)t.process);
	t = ExeFieldTarget();
}

ExeFieldResult WriteExeFieldWindow(const ExeFieldTarget& t, const c5r::heightfield::HeightGrid& before,
                                   const ExeFieldRect* rects, size_t nRects)
{
	ExeFieldResult res;
	const double t0 = NowMs();
	std::string why;
	HANDLE h = (HANDLE)t.process;
	auto refuse = [&](const std::string& w) -> ExeFieldResult {
		res.status = ExeFieldStatus::Refused;
		res.ms = NowMs() - t0;
		res.line = "LOUD: exefield: Refused: " + w + "; nothing written, the change waits for the reload";
		return res;
	};
	if(!h || !t.gridAddr)
		return refuse("no target (the EXE's process or grid is not known)");
	// --- 1. the server's field and the window
	const uint32_t SX = before.samplesX, SY = before.samplesY;
	const size_t N = (size_t)SX * SY;
	if(!SX || !SY || SX % kTileEdge || SY % kTileEdge || before.h.size() != N || before.mat.size() != N)
		return refuse(Fmt("the server's field is %ux%u with %zu heights and %zu materials", SX, SY, before.h.size(), before.mat.size()));
	if(nRects && !rects)
		return refuse("no rectangles");
	for(size_t i = 0; i < nRects; ++i)
	{
		const ExeFieldRect& r = rects[i];
		const uint64_t n = (uint64_t)r.w * r.h;
		if((uint64_t)r.x0 + r.w > SX || (uint64_t)r.y0 + r.h > SY)
			return refuse(Fmt("rectangle %zu (%u,%u %ux%u) lies outside the field %ux%u", i, r.x0, r.y0, r.w, r.h, SX, SY));
		if(r.nHeights != n || r.nMaterials != n || (n && (!r.heights || !r.materials)))
			return refuse(Fmt("rectangle %zu (%ux%u) has %zu heights and %zu materials", i, r.w, r.h, r.nHeights, r.nMaterials));
		res.samples += n;
	}
	if(res.samples > kMaxWindowSamples)
		return refuse(Fmt("the window has %llu samples (limit %llu)", (unsigned long long)res.samples, (unsigned long long)kMaxWindowSamples));
	// --- 2. the object
	c5r::heightfield::GridObject g;
	Key key0;
	if(!ReadObject(t, g, key0, why))
		return refuse(why);
	if(g.samplesX != SX || g.samplesY != SY)
		return refuse(Fmt("the EXE's field is %ux%u, the server's %ux%u (another load?)", g.samplesX, g.samplesY, SX, SY));
	if(res.samples == 0)
	{
		res.status = ExeFieldStatus::Written;
		res.ms = NowMs() - t0;
		res.line = "exefield: Written: empty window, nothing to write";
		return res;
	}
	const uint32_t TX = g.h16.tilesX;
	// --- the touched tiles
	std::vector<int32_t> at((size_t)TX * g.h16.tilesY, -1);   // tile -> index into tw
	std::vector<TileWork> tw;
	uint32_t tyMin = 0xFFFFFFFFu, tyMax = 0;
	size_t upTo = 0;
	for(size_t i = 0; i < nRects; ++i)
		if(rects[i].w && rects[i].h)
			upTo += (size_t)((rects[i].x0 + rects[i].w - 1) / kTileEdge - rects[i].x0 / kTileEdge + 1) *
			        ((rects[i].y0 + rects[i].h - 1) / kTileEdge - rects[i].y0 / kTileEdge + 1);
	tw.reserve(upTo);
	for(size_t i = 0; i < nRects; ++i)
	{
		const ExeFieldRect& r = rects[i];
		if(!r.w || !r.h)
			continue;
		for(uint32_t ty = r.y0 / kTileEdge; ty <= (r.y0 + r.h - 1) / kTileEdge; ++ty)
			for(uint32_t tx = r.x0 / kTileEdge; tx <= (r.x0 + r.w - 1) / kTileEdge; ++tx)
			{
				const uint32_t tile = ty * TX + tx;
				if(at[tile] >= 0)
					continue;
				at[tile] = (int32_t)tw.size();
				tw.emplace_back();
				tw.back().tile = tile;
				tyMin = std::min(tyMin, ty);
				tyMax = std::max(tyMax, ty);
			}
	}
	res.tiles = (uint32_t)tw.size();
	// directory entries of both layers (one read of the touched tile rows each)
	const c5r::heightfield::GridObject::Layer* L[2] = {&g.h16, &g.b8};
	for(int l = 0; l < 2; ++l)
	{
		const size_t e0 = (size_t)tyMin * TX, ne = (size_t)(tyMax - tyMin + 1) * TX;
		std::vector<uint8_t> dir(ne * 8);
		if(!Rd(h, t.ptrHigh + L[l]->dirPtr + e0 * 8, dir.data(), dir.size()))
			return refuse(Fmt("%s directory 0x%08X unreadable (error %lu)", l ? "u8" : "u16", L[l]->dirPtr, GetLastError()));
		for(TileWork& w : tw)
		{
			const uint8_t* e = &dir[(w.tile - e0) * 8];
			uint16_t state;
			uint32_t idx;
			memcpy(&state, e, 2);
			memcpy(&idx, e + 4, 4);
			if(state != 0)
				return refuse(Fmt("%s tile %u,%u is not pooled (state %u): a constant tile is never written", l ? "u8" : "u16",
				                  w.tile % TX, w.tile / TX, state));
			if(idx >= L[l]->poolSize)
				return refuse(Fmt("%s tile %u,%u: pool index %u past the pool of %u tiles", l ? "u8" : "u16", w.tile % TX, w.tile / TX,
				                  idx, L[l]->poolSize));
			w.idx[l] = idx;
			w.addr[l] = t.ptrHigh + L[l]->poolPtr + (uint64_t)idx * (l == 0 ? 512u : 256u);
		}
	}
	// --- 3. every touched run (reads; the writes are a subset): inside its pool, outside the EXE's image, and every
	// region it covers committed private read-write; each region queried once per call (Checked), before any pool read
	std::vector<Run> rr[2];
	MakeRuns(tw, 0, false, rr[0]);
	MakeRuns(tw, 1, false, rr[1]);
	Checked chk;
	for(int l = 0; l < 2; ++l)
	{
		const uint64_t lo = t.ptrHigh + L[l]->poolPtr, hi = lo + (uint64_t)L[l]->poolSize * (l == 0 ? 512u : 256u);
		for(const Run& r : rr[l])
		{
			if(!ExeFieldRunInside(r.addr, RunBytes(r), lo, hi, t.exeBase, t.exeBase + t.exeImageSize, why))
				return refuse(Fmt("%s run: ", l ? "u8" : "u16") + why);
			if(!RegionOk(h, r.addr, RunBytes(r), res.regionChecks, why, &chk))
				return refuse(why);
		}
	}
	// --- 4. read back every touched tile and compare with the server's field
	std::vector<uint8_t> buf;
	for(int l = 0; l < 2; ++l)
	{
		const std::vector<Run>& runs = rr[l];
		for(const Run& r : runs)
		{
			buf.resize(RunBytes(r));
			if(!Rd(h, r.addr, buf.data(), buf.size()))
				return refuse(Fmt("%s pool read at 0x%llX (%zu bytes) failed, error %lu", l ? "u8" : "u16", (unsigned long long)r.addr,
				                  buf.size(), GetLastError()));
			const size_t tb = l == 0 ? 512 : 256;
			for(size_t k = 0; k < r.tiles.size(); ++k)
			{
				TileWork& w = tw[r.tiles[k]];
				if(l == 0)
					memcpy(w.old16, &buf[k * tb], tb);
				else
					memcpy(w.old8, &buf[k * tb], tb);
			}
		}
	}
	uint64_t diff = 0;
	uint32_t dx = 0, dy = 0;
	for(TileWork& w : tw)
	{
		const uint32_t bx = (w.tile % TX) * kTileEdge, by = (w.tile / TX) * kTileEdge;
		for(uint32_t j = 0; j < kTileEdge; ++j)
			for(uint32_t i = 0; i < kTileEdge; ++i)
			{
				const size_t s = before.index(bx + i, by + j);
				const uint32_t k = j * kTileEdge + i;
				if(w.old16[k] != before.h[s] || w.old8[k] != before.mat[s])
				{
					if(!diff)
					{
						dx = bx + i;
						dy = by + j;
					}
					++diff;
				}
			}
		memcpy(w.new16, w.old16, sizeof(w.new16));
		memcpy(w.new8, w.old8, sizeof(w.new8));
	}
	if(diff)
		return refuse(Fmt("the EXE's field differs from the server's in %llu samples of the %u touched tiles (first at %u,%u: EXE "
		                  "%u/%u, server %u/%u)", (unsigned long long)diff, res.tiles, dx, dy,
		                  tw[at[(dy / kTileEdge) * TX + dx / kTileEdge]].old16[(dy % kTileEdge) * kTileEdge + dx % kTileEdge],
		                  tw[at[(dy / kTileEdge) * TX + dx / kTileEdge]].old8[(dy % kTileEdge) * kTileEdge + dx % kTileEdge],
		                  before.h[before.index(dx, dy)], before.mat[before.index(dx, dy)]));
	// --- the new contents
	for(size_t i = 0; i < nRects; ++i)
	{
		const ExeFieldRect& r = rects[i];
		for(uint32_t y = 0; y < r.h; ++y)
			for(uint32_t x = 0; x < r.w; ++x)
			{
				const uint32_t c = r.x0 + x, rr = r.y0 + y;
				TileWork& w = tw[at[(rr / kTileEdge) * TX + c / kTileEdge]];
				const uint32_t k = (rr % kTileEdge) * kTileEdge + (c % kTileEdge);
				w.new16[k] = r.heights[(size_t)y * r.w + x];
				w.new8[k] = r.materials[(size_t)y * r.w + x];
			}
	}
	for(TileWork& w : tw)
	{
		for(uint32_t k = 0; k < kTileSamples; ++k)
		{
			if(w.new16[k] != w.old16[k])
				++res.heightsChanged;
			if(w.new8[k] != w.old8[k])
				++res.materialsChanged;
		}
		w.changed[0] = memcmp(w.new16, w.old16, sizeof(w.new16)) != 0;
		w.changed[1] = memcmp(w.new8, w.old8, sizeof(w.new8)) != 0;
	}
	std::vector<Run> wr;
	MakeRuns(tw, 0, true, wr);
	MakeRuns(tw, 1, true, wr);
	if(wr.empty())
	{
		res.status = ExeFieldStatus::Written;
		res.ms = NowMs() - t0;
		res.line = Fmt("exefield: Written: %llu samples in %u tiles, no sample differs from the EXE's field, nothing written",
		               (unsigned long long)res.samples, res.tiles);
		return res;
	}
	res.msPrepare = NowMs() - t0;
	// the write runs once more against the same rules (their regions are in this call's Checked: no new query)
	if(!NtWrite())
		return refuse("ntdll's NtWriteVirtualMemory not found");
	for(const Run& r : wr)
	{
		const uint64_t lo = t.ptrHigh + L[r.layer]->poolPtr, hi = lo + (uint64_t)L[r.layer]->poolSize * (r.layer == 0 ? 512u : 256u);
		if(!ExeFieldRunInside(r.addr, RunBytes(r), lo, hi, t.exeBase, t.exeBase + t.exeImageSize, why) ||
		   !RegionOk(h, r.addr, RunBytes(r), res.regionChecks, why, &chk))
			return refuse(why);
	}
	// --- 5. the writes; after a failure the runs written so far are restored
	size_t done = 0;
	bool objectChanged = false;
	std::string fail;
	for(; done < wr.size(); ++done)
	{
		const Run& r = wr[done];
		if(t.testBeforeWrite)
			t.testBeforeWrite(t.testCtx, (uint32_t)done);
		c5r::heightfield::GridObject g2;
		Key key2;
		if(!ReadObject(t, g2, key2, why) || !(key2 == key0))
		{
			objectChanged = true;
			fail = "the GridHeightmap object changed during the write (a load began?)" + (why.empty() ? std::string() : ": " + why);
			break;
		}
		Gather(tw, r, true, buf);
		size_t got = 0;
		++res.writes;
		if(!Wr(h, r.addr, buf.data(), buf.size(), got, why))
		{
			fail = "write " + std::to_string(done) + ": " + why;
			if(got)
				++done;   // partly written: restore it too
			break;
		}
	}
	res.msWrite = NowMs() - t0 - res.msPrepare;
	// read back what was written, and the object once more
	if(fail.empty())
	{
		std::vector<uint8_t> rb;
		for(const Run& r : wr)
		{
			Gather(tw, r, true, buf);
			rb.resize(buf.size());
			if(!Rd(h, r.addr, rb.data(), rb.size()) || memcmp(rb.data(), buf.data(), buf.size()) != 0)
			{
				fail = Fmt("the read-back of %s run 0x%llX differs from what was written", r.layer ? "u8" : "u16",
				           (unsigned long long)r.addr);
				break;
			}
		}
		c5r::heightfield::GridObject g2;
		Key key2;
		if(fail.empty() && (!ReadObject(t, g2, key2, why) || !(key2 == key0)))
		{
			objectChanged = true;
			fail = "the GridHeightmap object changed during the write (a load began?)";
		}
	}
	res.ms = NowMs() - t0;
	if(fail.empty())
	{
		res.status = ExeFieldStatus::Written;
		res.line = Fmt("exefield: Written: %zu rect(s), %llu samples, %llu heights and %llu materials changed, %u tiles, %u writes, "
		               "%u region checks, %.2f ms (prepare %.2f, write %.2f)", nRects, (unsigned long long)res.samples, (unsigned long long)res.heightsChanged,
		               (unsigned long long)res.materialsChanged, res.tiles, res.writes, res.regionChecks, res.ms, res.msPrepare, res.msWrite);
		return res;
	}
	if(done == 0 && !objectChanged)
	{
		res.status = ExeFieldStatus::Refused;
		res.line = "LOUD: exefield: Refused: " + fail + "; nothing written, the change waits for the reload";
		return res;
	}
	if(objectChanged)
	{
		// The pools may belong to another load now: writing the old bytes back could hit memory that is no longer the field.
		res.status = done ? ExeFieldStatus::RollbackFailed : ExeFieldStatus::Refused;
		res.line = Fmt("LOUD: exefield: %s: %s; %zu of %zu writes made, not restored (the field is being rebuilt); the change "
		               "waits for the reload", ExeFieldStatusName(res.status), fail.c_str(), done, wr.size());
		return res;
	}
	std::string rbFail;
	for(size_t k = done; k-- > 0;)
	{
		const Run& r = wr[k];
		Gather(tw, r, false, buf);
		size_t got = 0;
		std::vector<uint8_t> rb(buf.size());
		if(!RegionOk(h, r.addr, buf.size(), res.regionChecks, why))
		{
			rbFail = "restore " + std::to_string(k) + ": " + why;
			continue;
		}
		++res.writes;
		if(!Wr(h, r.addr, buf.data(), buf.size(), got, why))
			rbFail = "restore " + std::to_string(k) + ": " + why;
		else if(!Rd(h, r.addr, rb.data(), rb.size()) || memcmp(rb.data(), buf.data(), buf.size()) != 0)
			rbFail = Fmt("restore %zu at 0x%llX: the read-back differs", k, (unsigned long long)r.addr);
	}
	res.ms = NowMs() - t0;
	res.status = rbFail.empty() ? ExeFieldStatus::RolledBack : ExeFieldStatus::RollbackFailed;
	res.line = Fmt("LOUD: exefield: %s: %s; %zu write(s) %s%s; the change waits for the reload", ExeFieldStatusName(res.status),
	               fail.c_str(), done, rbFail.empty() ? "restored and read back equal" : "NOT all restored: ", rbFail.c_str());
	return res;
}

}  // namespace liveterrain
}  // namespace srvrender
