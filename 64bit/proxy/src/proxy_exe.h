// proxy_exe.h - the proxy's access to the EXE build table (epic 0590, decision D202; ticket 4963). Written 2026-11-19 for
// this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// Every proxy feature that reads, calls, writes or checks something in CivilizationV_DX11.exe finds the running build
// from the image's own header (BuildOfImage: PE TimeDateStamp + SizeOfImage through the registry vp_exe_build.h, read
// behind VirtualQuery) and asks Va / Addr / Rva for a catalogue symbol (exebuild/symbols/proxy.tsv, shell.tsv) of that
// build. A symbol the build does not carry is 0: the feature refuses with one line naming it (NotCarried). Byte patterns
// are X(sym, "name", "bytes") rows checked with vp_exe_patterns.h (CheckPattern).
//
// Test hook (offline unit tests only; the product never calls it): Test::DropSymbol(sym) makes Va return 0 for that
// symbol in every build, so each feature's "not carried" refusal can be tested with complete tables.
//
// Header only, C++03 (the proxy's VC9 toolchain). Needs exebuild/include and the emitted vp_exe_tables.gen.h on the
// include path (proxy/build_proxy.py step 2b; proxy/tools/test_proxy_rt.py build()).
#pragma once
#include <windows.h>
#include <stdio.h>
#include "vp_exe_symbols.h"
#include "vp_exe_patterns.h"

namespace ProxyExe
{
namespace Test
{
// The symbol Va reports as not carried (-1 = none). One value shared by every unit (an inline function's static).
inline int& DroppedSymbol()
{
	static int s_sym = -1;
	return s_sym;
}
inline void DropSymbol(int sym)
{
	DroppedSymbol() = sym;
}
}  // namespace Test

// The symbol's VA (ImageBase 0x400000) in the build, 0 when not carried (or dropped by the test hook).
inline uint32_t Va(int build, int sym)
{
	return sym == Test::DroppedSymbol() ? 0 : VpExeVa(build, sym);
}
inline uint32_t Rva(int build, int sym)
{
	const uint32_t va = Va(build, sym);
	return va ? va - VPEXE_IMAGE_BASE : 0;
}
inline uintptr_t Addr(int build, int sym, uintptr_t base)
{
	return VpExeRebase(Va(build, sym), base);
}

// True if [addr, addr + n) lies in committed, readable memory (no fault on any address).
inline bool Readable(uintptr_t addr, size_t n)
{
	if (addr < 0x10000 || addr + n < addr) return false;
	uintptr_t at = addr;
	while (at < addr + n)
	{
		MEMORY_BASIC_INFORMATION mi;
		if (VirtualQuery((const void*)at, &mi, sizeof(mi)) != sizeof(mi)) return false;
		if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
		                       PAGE_EXECUTE_WRITECOPY;
		if ((mi.Protect & readable) == 0) return false;
		const uintptr_t end = (uintptr_t)mi.BaseAddress + mi.RegionSize;
		if (end <= at) return false;
		at = end;
	}
	return true;
}

// The build of the image at base (kVpExeBuild* or kVpExeBuildUnknown); *stamp and *sizeOfImage (each may be NULL) get the
// header's values, 0 when the image is no readable 32-bit PE.
inline int BuildOfImage(uintptr_t base, uint32_t* stamp, uint32_t* sizeOfImage)
{
	if (stamp) *stamp = 0;
	if (sizeOfImage) *sizeOfImage = 0;
	if (!Readable(base, 0x40)) return kVpExeBuildUnknown;
	DWORD e = 0;
	memcpy(&e, (const void*)(base + 0x3C), 4);
	if (e == 0 || e > 0x10000) return kVpExeBuildUnknown;
	const uint32_t need = e + 4 + 20 + 60;
	if (!Readable(base, need)) return kVpExeBuildUnknown;
	return VpExeBuildOfImage((const void*)base, need, stamp, sizeOfImage);
}

// "off: not an EXE build the patch knows (PE timestamp 0x..., SizeOfImage 0x...)" into buf.
inline const char* UnknownBuild(char* buf, size_t cap, const char* prefix, uint32_t stamp, uint32_t sizeOfImage)
{
	_snprintf_s(buf, cap, _TRUNCATE, "%snot an EXE build the patch knows (PE timestamp 0x%08X, SizeOfImage 0x%08X)", prefix,
	            stamp, sizeOfImage);
	return buf;
}

// "off: symbol <name> is not carried to <build label> (no address in this binary's table)" into buf.
inline const char* NotCarried(char* buf, size_t cap, const char* prefix, int build, int sym)
{
	_snprintf_s(buf, cap, _TRUNCATE, "%ssymbol %s is not carried to %s (no address in this binary's table)", prefix,
	            VpExeSymName(sym), VpExeBuildLabel(build));
	return buf;
}

// Checks a pattern template at a symbol of the build in the image at base (vp_exe_patterns.h, in-process reader behind
// VirtualQuery). The pattern's own address goes through Va (so a dropped symbol is "not carried"). Returns 1 on a match.
inline int CheckPattern(int build, uintptr_t base, int sym, const char* tmpl, VpExePatternResult* r)
{
	return VpExeCheckPatternVa(build, base, Va(build, sym), tmpl, 0, 0, r);
}
}  // namespace ProxyExe
