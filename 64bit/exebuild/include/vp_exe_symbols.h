// vp_exe_symbols.h - the EXE addresses of the running build, by symbol name (epic 0590, decision D202; ticket 4960).
// Written 2026-11-19 for this repository (GPL-3.0-or-later).
//
// Every product module that reads, calls or checks something in CivilizationV_DX11.exe names it by a symbol of the
// committed catalogue (exebuild/symbols/<area>.tsv) and asks here for its VA in the running build:
//
//     int build = VpExeBuildOfImage(image, bytes, &stamp, &soi);       // vp_exe_build.h
//     if (!VpExeBuildSupported(build)) -> off, one log line naming the build (unknown or incomplete table)
//     uintptr_t p = VpExeAddr(build, VPX_cam_app, exeBase);            // 0 when this build does not carry the symbol
//
// The values come from vp_exe_tables.gen.h, which tools/exebuild/emit_header.py writes into the build's output folder
// from the local per-build tables (setting exe_tables; never committed, D202). A build without the setting or a table
// fails at the emitter, naming the setting. VAs are at ImageBase 0x400000; the EXE is relocated at run time, so every
// address is rebased on the module base (VpExeRebase / VpExeAddr).
//
// Header only, C++03 (the proxy's msvcr90 toolchain) and x64 (the server, render/). No Windows header. The pattern
// tables' helpers (check, lay, end, fake header) are in vp_exe_sigs.h.
#pragma once
#include <stdint.h>
#include <string.h>
#include "vp_exe_build.h"
#include "vp_exe_tables.gen.h"

// VPEXE_IMAGE_BASE (0x400000) comes from vp_exe_sigs.h, the one header that defines it (ticket 0603); its second part,
// the pattern tables' helpers, is left out here and comes with an include of vp_exe_sigs.h itself.
#define VPEXE_SIGS_BASE_ONLY_
#include "vp_exe_sigs.h"
#undef VPEXE_SIGS_BASE_ONLY_

// The symbol's VA in the build (ImageBase 0x400000), 0 when the build is unknown or does not carry the symbol.
inline uint32_t VpExeVa(int build, int sym)
{
	if (build < 0 || build >= kVpExeBuildCount || sym < 0 || sym >= kVpExeSymCount)
		return 0;
	return kVpExeSymVa[build][sym];
}

// The symbol's RVA in the build, 0 when not carried.
inline uint32_t VpExeRva(int build, int sym)
{
	uint32_t va = VpExeVa(build, sym);
	return va ? va - VPEXE_IMAGE_BASE : 0;
}

// A VA (ImageBase 0x400000) moved to the loaded module base; 0 stays 0.
inline uintptr_t VpExeRebase(uint32_t va, uintptr_t moduleBase)
{
	return va ? moduleBase + (uintptr_t)(va - VPEXE_IMAGE_BASE) : 0;
}

// The symbol's address in the loaded EXE, 0 when not carried.
inline uintptr_t VpExeAddr(int build, int sym, uintptr_t moduleBase)
{
	return VpExeRebase(VpExeVa(build, sym), moduleBase);
}

// True when the build is known and its table is present and complete (every catalogue symbol and array cell carried).
inline bool VpExeBuildSupported(int build)
{
	return build >= 0 && build < kVpExeBuildCount && kVpExeBuildComplete[build] != 0;
}

// The symbol's name for logs ("cam_app"), "?" when out of range.
inline const char* VpExeSymName(int sym)
{
	return (sym >= 0 && sym < kVpExeSymCount) ? kVpExeSymNames[sym] : "?";
}

// The build's label for logs ("Steam build 26577625 (...)"), or "unknown build".
inline const char* VpExeBuildLabel(int build)
{
	const VpExeBuildInfo* b = VpExeBuildInfoOf(build);
	return b ? b->label : "unknown build";
}

// The symbol with this name (an optional "VPX_" prefix is accepted), or -1. len < 0: name is NUL-terminated.
inline int VpExeSymByName(const char* name, int len)
{
	if (!name)
		return -1;
	if (len < 0)
		len = (int)strlen(name);
	if (len > 4 && memcmp(name, "VPX_", 4) == 0)
	{
		name += 4;
		len -= 4;
	}
	for (int i = 0; i < kVpExeSymCount; ++i)
		if ((int)strlen(kVpExeSymNames[i]) == len && memcmp(kVpExeSymNames[i], name, (size_t)len) == 0)
			return i;
	return -1;
}

// The symbol whose anchor (its VA in build 4390913) is va, or -1: lets a table written with old-build VAs
// (X(0x51DF70, ...), R(CC8E74)) resolve through the running build's table.
inline int VpExeSymByAnchor(uint32_t va)
{
	for (int i = 0; i < kVpExeSymCount; ++i)
		if (kVpExeSymAnchor[i] == va)
			return i;
	return -1;
}

// An array symbol's cells in the build (rows x cols, row-major; a cell 0 = not carried), or NULL.
inline const uint32_t* VpExeArray(int build, int arr, int* rows, int* cols)
{
	if (build < 0 || build >= kVpExeBuildCount || arr < 0 || arr >= kVpExeArrCount)
		return 0;
	if (rows)
		*rows = kVpExeArrays[arr].rows;
	if (cols)
		*cols = kVpExeArrays[arr].cols;
	return kVpExeArrays[arr].cells[build];
}
