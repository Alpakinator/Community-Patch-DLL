// proxy_livereload.cpp - the EXE's ice and coast waves rebuilt after a live terrain change (epic 0465, ticket 0586).
// Written 2026-11-19 for this repository (GPL-3.0-or-later). Hand-written. Header and design: proxy_livereload.h.
// No dependency on the proxy runtime (the log goes through Hooks), so the same file is built into the proxy and into the
// unit test (proxy/tests/livereload/, stock Lua 5.1.4, also under MSVC ASan).
//
// Rules kept in this file: every Lua call runs under lua_cpcall (the body is a plain function with no C++ object in its
// frame); every read of EXE memory is inside the image and checked with VirtualQuery first; the only write is bit 0x80
// of the ice grid's bytes, after every check passed.
#include "proxy_livereload.h"
#include "vp_livereload.h"
#include <windows.h>
#include "proxy_exe.h"   // ticket 0619 (epic 0590, D202): the build and its addresses from the build table
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace
{
using namespace ProxyLiveReload;

Hooks g_hooks = { NULL };
Stats g_stats;
uint8_t* g_testBase = NULL;
size_t g_testSize = 0;

HANDLE g_map = NULL;
vplr::Section* g_sec = NULL;
uint32_t g_framesSinceTry = 0;
bool g_triedOnce = false;
bool g_loggedNoSection = false;

int g_exeState = 0;   // 0 not checked, 1 good, < 0 the refusal (kRefusedExe / kRefusedPattern)
char g_exeWhy[200];
uint8_t* g_ice = NULL;   // the Ice object of the running build (ticket 0619: from the build table, set by CheckExe)

// Ice_Reload's site (symbol ice_reload): push <grid>; mov ecx, <Ice>; call. The same template as ice_tap.cpp's row.
const char kIceReloadPattern[] = "68 R(cam_grid) B9 R(ice_ice) E8";

void Logf(const char* fmt, ...)
{
	if (!g_hooks.log) return;
	char b[512];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	g_hooks.log(b);
}

void Image(uint8_t*& base, size_t& size)
{
	if (g_testBase)
	{
		base = g_testBase;
		size = g_testSize;
		return;
	}
	base = (uint8_t*)GetModuleHandleW(NULL);
	size = 0;
	if (!base) return;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
	const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return;
	size = nt->OptionalHeader.SizeOfImage;
}

// True when [p, p + n) is committed, readable memory inside the image; `rw`: PAGE_READWRITE (no guard, no write-copy).
bool RangeOk(const uint8_t* base, size_t size, const uint8_t* p, size_t n, bool rw)
{
	if (!base || p < base || n > size || (size_t)(p - base) > size - n) return false;
	const uint8_t* q = p;
	const uint8_t* end = p + n;
	while (q < end)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(q, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
		const DWORD prot = mbi.Protect;
		if (prot & (PAGE_GUARD | PAGE_NOACCESS)) return false;
		if (rw && (prot & 0xFF) != PAGE_READWRITE) return false;
		if (!rw && !(prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
		                     PAGE_EXECUTE_WRITECOPY)))
			return false;
		const uint8_t* next = (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
		if (next <= q) return false;
		q = next;
	}
	return true;
}


uint32_t Rd32(const uint8_t* p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

// The EXE checks, once per process (read only).
int CheckExe()
{
	if (g_exeState != 0) return g_exeState;
	uint8_t* base = NULL;
	size_t size = 0;
	Image(base, size);
	g_exeWhy[0] = 0;
	if (!base || size < 0x1000 || !RangeOk(base, size, base, 0x400, false))
	{
		_snprintf_s(g_exeWhy, sizeof(g_exeWhy), _TRUNCATE, "the EXE's image could not be read");
		return g_exeState = vplr::kRefusedExe;
	}
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
	const uint32_t lfanew = (uint32_t)dos->e_lfanew;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || lfanew > 0x300 ||
	    Rd32(base + lfanew) != IMAGE_NT_SIGNATURE)
	{
		_snprintf_s(g_exeWhy, sizeof(g_exeWhy), _TRUNCATE, "the EXE has no PE header");
		return g_exeState = vplr::kRefusedExe;
	}
	// ticket 0619 (epic 0590, D202): the build from the registry (PE stamp and SizeOfImage), the addresses from its table
	uint32_t stamp = 0, soi = 0;
	const int build = ProxyExe::BuildOfImage((uintptr_t)base, &stamp, &soi);
	if (build == kVpExeBuildUnknown)
	{
		ProxyExe::UnknownBuild(g_exeWhy, sizeof(g_exeWhy), "", stamp, soi);
		strncat_s(g_exeWhy, sizeof(g_exeWhy), " (nothing is read, written or called, D200)", _TRUNCATE);
		return g_exeState = vplr::kRefusedExe;
	}
	const int syms[3] = {VPX_ice_reload, VPX_ice_ice, VPX_cam_grid};
	for (int i = 0; i < 3; ++i)
		if (!ProxyExe::Va(build, syms[i]))
		{
			ProxyExe::NotCarried(g_exeWhy, sizeof(g_exeWhy), "", build, syms[i]);
			return g_exeState = vplr::kRefusedExe;
		}
	// Ice_Reload passes the grid and the Ice object: push <grid>; mov ecx, <Ice>; call (the row ice_tap.cpp checks too)
	VpExePatternResult r;
	memset(&r, 0, sizeof(r));
	if (!RangeOk(base, size, (const uint8_t*)ProxyExe::Addr(build, VPX_ice_reload, (uintptr_t)base), 11, false))
	{
		_snprintf_s(g_exeWhy, sizeof(g_exeWhy), _TRUNCATE, "Ice_Reload's code (symbol ice_reload, %s) cannot be read",
		            VpExeBuildLabel(build));
		return g_exeState = vplr::kRefusedPattern;
	}
	if (!ProxyExe::CheckPattern(build, (uintptr_t)base, VPX_ice_reload, kIceReloadPattern, &r))
	{
		_snprintf_s(g_exeWhy, sizeof(g_exeWhy), _TRUNCATE, "Ice_Reload's code (symbol ice_reload, %s) is not \"push <cam_grid>; "
		            "mov ecx, <ice_ice>; call\": %s at +%d", VpExeBuildLabel(build), r.reason, r.offset);
		return g_exeState = vplr::kRefusedPattern;
	}
	g_ice = (uint8_t*)ProxyExe::Addr(build, VPX_ice_ice, (uintptr_t)base);
	return g_exeState = 1;
}

// ---- the protected call of a global by name ----
struct CallCtx
{
	const char* name;
	bool probe;          // only look the global up (no call)
	int result;          // kOk, kNoFunction
	const char* where;   // "InGame" or "globals"
};

int CallBody(lua_State* L)
{
	CallCtx* c = (CallCtx*)lua_touserdata(L, 1);
	lua_getfield(L, LUA_ENVIRONINDEX, c->name);   // the InGame context's environment (the drain's)
	c->where = "InGame";
	if (!lua_isfunction(L, -1))
	{
		lua_pop(L, 1);
		lua_getfield(L, LUA_GLOBALSINDEX, c->name);
		c->where = "globals";
	}
	if (!lua_isfunction(L, -1))
	{
		c->result = vplr::kNoFunction;
		return 0;
	}
	if (!c->probe) lua_call(L, 0, 0);
	c->result = vplr::kOk;
	return 0;
}

int64_t Qpc()
{
	LARGE_INTEGER q;
	QueryPerformanceCounter(&q);
	return q.QuadPart;
}

uint32_t Micros(int64_t d)
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	const double us = (double)d * 1e6 / (double)f.QuadPart;
	return us > 4e9 ? 0xFFFFFFFFu : (uint32_t)us;
}

int CallGlobal(lua_State* L, const char* name, uint32_t& micros, char* why, size_t whyCap, bool probe = false)
{
	CallCtx c;
	c.name = name;
	c.probe = probe;
	c.result = vplr::kLuaError;
	c.where = "";
	const int top = lua_gettop(L);
	const int64_t t0 = Qpc();
	const int rc = lua_cpcall(L, CallBody, &c);
	micros = Micros(Qpc() - t0);
	if (rc != 0)
	{
		const char* m = lua_isstring(L, -1) ? lua_tostring(L, -1) : "(no message)";
		_snprintf_s(why, whyCap, _TRUNCATE, "%s() raised an error: %s", name, m);
		lua_settop(L, top);
		return vplr::kLuaError;
	}
	lua_settop(L, top);
	if (c.result == vplr::kNoFunction)
		_snprintf_s(why, whyCap, _TRUNCATE, "%s is not a function in the InGame context nor in the thread's globals", name);
	return c.result;
}

// Step 2's grid part. Returns kOk (bits written) or an error with `why`.
int WriteIce(const vplr::Section& r, uint32_t& set, uint32_t& cleared, char* why, size_t whyCap)
{
	set = cleared = 0;
	uint8_t* base = NULL;
	size_t size = 0;
	Image(base, size);
	uint8_t* ice = g_ice;   // set by CheckExe for the running build (ticket 0619)
	if (!ice || !RangeOk(base, size, ice + vplr::kIceOffW, 8, false))
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "the ice object's size fields cannot be read");
		return vplr::kNotWritable;
	}
	const uint32_t gw = Rd32(ice + vplr::kIceOffW), gh = Rd32(ice + vplr::kIceOffH);
	if (gw != r.mapW || gh != r.mapH)
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "the ice grid is %u x %u, the request's map %u x %u", gw, gh, r.mapW, r.mapH);
		return vplr::kGridMismatch;
	}
	uint8_t* grid = ice + vplr::kIceOffGrid;
	const size_t gridBytes = (size_t)vplr::kIceStride * (r.mapH - 1) + r.mapW;
	if (!RangeOk(base, size, grid, gridBytes, true))
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "the ice grid (%u bytes at image offset 0x%X) is not committed PAGE_READWRITE memory "
		            "inside the EXE's image", (unsigned)gridBytes, (unsigned)(grid - base));
		return vplr::kNotWritable;
	}
	uint32_t flips = 0;
	for (uint32_t y = 0; y < r.mapH; ++y)
		for (uint32_t x = 0; x < r.mapW; ++x)
		{
			const bool want = vplr::BitGet(r.ice, y * r.mapW + x);
			const bool have = (grid[y * vplr::kIceStride + x] & vplr::kIceBit) != 0;
			flips += want != have ? 1u : 0u;
		}
	if (flips != r.expectFlips)
	{
		_snprintf_s(why, whyCap, _TRUNCATE, "the grid differs from the request in %u plots, the server expected %u: nothing written "
		            "(the grid is not the one the server's table describes)", flips, r.expectFlips);
		return vplr::kGridMismatch;
	}
	for (uint32_t y = 0; y < r.mapH; ++y)
		for (uint32_t x = 0; x < r.mapW; ++x)
		{
			uint8_t& b = grid[y * vplr::kIceStride + x];
			const bool want = vplr::BitGet(r.ice, y * r.mapW + x);
			const bool have = (b & vplr::kIceBit) != 0;
			if (want && !have)
			{
				b = (uint8_t)(b | vplr::kIceBit);
				++set;
			}
			else if (!want && have)
			{
				b = (uint8_t)(b & ~vplr::kIceBit);
				++cleared;
			}
		}
	return vplr::kOk;
}

const char* ResultName(int r)
{
	switch (r)
	{
	case vplr::kOk: return "ok";
	case vplr::kNotAsked: return "not asked";
	case vplr::kRefusedExe: return "REFUSED (EXE)";
	case vplr::kRefusedPattern: return "REFUSED (code pattern)";
	case vplr::kGridMismatch: return "GRID MISMATCH";
	case vplr::kNotWritable: return "NOT WRITABLE";
	case vplr::kNoFunction: return "NO FUNCTION";
	case vplr::kLuaError: return "LUA ERROR";
	case vplr::kBadRequest: return "BAD REQUEST";
	}
	return "?";
}

void Serve(lua_State* L, vplr::Section& s, uint32_t req)
{
	++g_stats.requests;
	char why[160];
	why[0] = 0;
	int iceR = vplr::kNotAsked, wavesR = vplr::kNotAsked;
	uint32_t set = 0, cleared = 0, iceUs = 0, wavesUs = 0;
	const uint32_t flags = s.reqFlags;
	if (s.mapW < 1 || s.mapW > vplr::kMaxW || s.mapH < 1 || s.mapH > vplr::kMaxH || (flags & ~(vplr::kReqIce | vplr::kReqWaves)) ||
	    !flags)
	{
		_snprintf_s(why, sizeof(why), _TRUNCATE, "map %u x %u, flags 0x%X out of range", s.mapW, s.mapH, flags);
		iceR = wavesR = vplr::kBadRequest;
	}
	else
	{
		const int exe = CheckExe();
		if (exe < 0)
		{
			strcpy_s(why, sizeof(why), g_exeWhy);
			if (flags & vplr::kReqIce) iceR = exe;
			if (flags & vplr::kReqWaves) wavesR = exe;
		}
		else
		{
			if (flags & vplr::kReqIce)
			{
				// the function first: a missing Ice_Reload leaves the grid as it is (the server then counts it unchanged)
				uint32_t probeUs = 0;
				iceR = CallGlobal(L, "Ice_Reload", probeUs, why, sizeof(why), true);
				if (iceR == vplr::kOk) iceR = WriteIce(s, set, cleared, why, sizeof(why));
				if (iceR == vplr::kOk) iceR = CallGlobal(L, "Ice_Reload", iceUs, why, sizeof(why));
			}
			if (flags & vplr::kReqWaves)
			{
				char w2[160];
				w2[0] = 0;
				wavesR = CallGlobal(L, "Waves_Reload", wavesUs, w2, sizeof(w2));
				if (!why[0] && w2[0]) strcpy_s(why, sizeof(why), w2);
			}
		}
	}
	s.iceResult = iceR;
	s.wavesResult = wavesR;
	s.bitsSet = set;
	s.bitsCleared = cleared;
	s.iceMicros = iceUs;
	s.wavesMicros = wavesUs;
	s.proxyPid = GetCurrentProcessId();
	memcpy(s.why, why, sizeof(s.why) < sizeof(why) ? sizeof(s.why) : sizeof(why));
	s.why[sizeof(s.why) - 1] = 0;
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&s.ackSeq, (LONG)req);
	g_stats.bitsSet += set;
	g_stats.bitsCleared += cleared;
	g_stats.lastIce = iceR;
	g_stats.lastWaves = wavesR;
	if (iceR == vplr::kOk) ++g_stats.iceOk;
	if (wavesR == vplr::kOk) ++g_stats.wavesOk;
	if (iceUs > g_stats.iceMicrosMax) g_stats.iceMicrosMax = iceUs;
	if (wavesUs > g_stats.wavesMicrosMax) g_stats.wavesMicrosMax = wavesUs;
	const bool bad = (iceR != vplr::kOk && iceR != vplr::kNotAsked) || (wavesR != vplr::kOk && wavesR != vplr::kNotAsked);
	if (bad) ++g_stats.failures;
	Logf("%slivereload: request %u (server load seq %u, map %u x %u, %u ice plots): ice %s (%u set, %u cleared, Ice_Reload %.1f ms), "
	     "waves %s (Waves_Reload %.1f ms)%s%s",
	     bad ? "LOUD: " : "", req, s.loadSeq, s.mapW, s.mapH, s.iceCount, ResultName(iceR), set, cleared, iceUs / 1000.0,
	     ResultName(wavesR), wavesUs / 1000.0, why[0] ? ": " : "", why);
}

bool OpenSection()
{
	wchar_t name[64];
	_snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE, VPLR_SECTION_FMT, (unsigned)GetCurrentProcessId());
	HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
	if (!h) return false;
	vplr::Section* s = (vplr::Section*)MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(vplr::Section));
	if (!s)
	{
		CloseHandle(h);
		return false;
	}
	MemoryBarrier();
	if (s->magic != vplr::kMagic || s->version != vplr::kVersion || s->bytes != sizeof(vplr::Section))
	{
		Logf("LOUD: livereload: the section of server pid %u has magic 0x%08X, version %u, %u bytes (want 0x%08X, %u, %u): not used",
		     s->serverPid, s->magic, s->version, s->bytes, vplr::kMagic, vplr::kVersion, (unsigned)sizeof(vplr::Section));
		UnmapViewOfFile(s);
		CloseHandle(h);
		return false;
	}
	g_map = h;
	g_sec = s;
	++g_stats.opens;
	Logf("livereload: section of server pid %u opened (the EXE's ice and coast waves follow live terrain, ticket 0586)", s->serverPid);
	return true;
}
}  // namespace

namespace ProxyLiveReload
{
void SetHooks(const Hooks& h) { g_hooks = h; }

void OnFrame(lua_State* L)
{
	++g_stats.frames;
	if (!g_sec)
	{
		if (g_triedOnce && ++g_framesSinceTry < kOpenEveryFrames) return;
		g_triedOnce = true;
		g_framesSinceTry = 0;
		if (!OpenSection()) return;
	}
	vplr::Section& s = *g_sec;
	const uint32_t req = s.reqSeq;
	if (req == s.ackSeq) return;
	MemoryBarrier();
	Serve(L, s, req);
}

Stats GetStats() { return g_stats; }

void StatusLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE, "livereload: section %s, %u requests (ice ok %u, waves ok %u, failures %u), bits set %u cleared %u, "
	            "Ice_Reload max %.1f ms, Waves_Reload max %.1f ms",
	            g_sec ? "open" : "not open", g_stats.requests, g_stats.iceOk, g_stats.wavesOk, g_stats.failures, g_stats.bitsSet,
	            g_stats.bitsCleared, g_stats.iceMicrosMax / 1000.0, g_stats.wavesMicrosMax / 1000.0);
}

void SetImageForTest(uint8_t* base, size_t size)
{
	g_testBase = base;
	g_testSize = size;
	g_exeState = 0;
	g_ice = NULL;
}

void ResetForTest()
{
	if (g_sec) UnmapViewOfFile(g_sec);
	if (g_map) CloseHandle(g_map);
	g_sec = NULL;
	g_map = NULL;
	g_framesSinceTry = 0;
	g_triedOnce = false;
	g_loggedNoSection = false;
	g_exeState = 0;
	g_ice = NULL;
	memset(&g_stats, 0, sizeof(g_stats));
}
}  // namespace ProxyLiveReload
