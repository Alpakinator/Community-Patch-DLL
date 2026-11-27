// shell_exe.h - the composite taps' access to the EXE build table (epic 0590, decision D202; ticket 4965). Written
// 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// A tap that reads or checks something in CivilizationV_DX11.exe names it by a catalogue symbol (exebuild/symbols/
// shell.tsv; C++ VPX_<name>) and asks the build table for its address in the running build:
//
//     char why[256];
//     const int build = ShellExeResolve(exeBase, kNeed, n, why, sizeof(why));   // the build, or -1 with why
//     if (build < 0 || !ShellExeCheck(build, exeBase, kPats, nPats, why, sizeof(why))) -> FAILED PATTERN, why
//     uintptr_t tds = ShellExeAddr(build, VPX_bake_ts, exeBase);
//
// The build comes from the image's own PE header (TimeDateStamp + SizeOfImage, vp_exe_build.h), read behind VirtualQuery,
// so an unknown build is refused before anything else is read. Byte patterns are rows X(VPX_name, "name", "bytes") of a
// tap's table (the syntax of exe-build-table.md section 4; tools/exebuild/gen_table.py checks every row on every build
// it generates, through exebuild/patterns.txt) and are checked here by vp_exe_patterns.h. The VAs themselves are in the
// emitted vp_exe_tables.gen.h, made from the local tables (setting exe_tables; never committed, D202).
//
// Tests: ShellExeLay writes a fake image of any known build (a PE header naming it, every pattern's bytes at that build's
// addresses), so each tap's real code path runs on fake images of both builds. ShellExeTestDrop(name) makes one symbol
// "not carried" for the not-carried refusal's test; the taps call it only in their test modes.
//
// Since ticket 0603 every helper here is a thin name over exebuild/include/vp_exe_sigs.h, which the camera's
// shell/common/exe_camera.h shares (the row type, the test hook, the check, the fake image, the image base).
//
// Header only, C++03 (farm_guard.cpp is compiled into the proxy too: clang-cl -m32, VC9 headers).
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vp_exe_sigs.h"

// One row of a tap's pattern table: { VPX_<name> (where the bytes are), "name" (for the log), "template" }.
typedef VpExeSig ShellExePat;

// Test modes only: the symbol of this name (VPX_ prefix allowed) is treated as not carried. Unknown name: no effect,
// returns false.
inline bool ShellExeTestDrop(const char* name)
{
	return VpExeSigTestDrop(name);
}

// The symbol's VA (ImageBase 0x400000) in the build, 0 when not carried.
inline uint32_t ShellExeVa(int build, int sym)
{
	return VpExeSigVa(build, sym);
}

// The symbol's address in the image at base, 0 when not carried.
inline uintptr_t ShellExeAddr(int build, int sym, uintptr_t base)
{
	return VpExeRebase(VpExeSigVa(build, sym), base);
}

// The build of the image at base (kVpExeBuild*, or kVpExeBuildUnknown) from its PE header, read behind VirtualQuery.
inline int ShellExeBuildOf(uintptr_t base, uint32_t* stamp, uint32_t* sizeOfImage)
{
	return VpExeBuildOfLoaded(base, stamp, sizeOfImage);
}

// The build of the image at base when it is known and carries every symbol of need[0..n); else -1 with one line in why
// ("not an EXE build the patch knows (PE timestamp 0x..., SizeOfImage 0x...)" or "symbol <name> is not carried to
// <build>").
inline int ShellExeResolve(uintptr_t base, const int* need, int n, char* why, size_t whyN)
{
	return VpExeResolve(base, need, n, why, whyN);
}

// Every pattern of p[0..n) at its symbol in the image at base (reads behind VirtualQuery). False with one line in why
// naming the first pattern that fails, its symbol, the build and the reason.
inline bool ShellExeCheck(int build, uintptr_t base, const ShellExePat* p, int n, char* why, size_t whyN)
{
	return VpExeCheckSigsOn(build, base, p, n, why, whyN);
}

// The build whose Steam build id is s ("4390913"), or kVpExeBuildUnknown (tests choose a fake image's build by it).
inline int ShellExeBuildBySteam(const char* s)
{
	return VpExeBuildBySteam(s);
}

// Tests: the header of a fake image made by ShellExeLay names no known build afterwards (its TimeDateStamp changed).
inline void ShellExeTestForgetBuild(uintptr_t base)
{
	VpExeTestForgetBuild(base);
}

// Bytes from the image base to the end of the furthest pattern of p[0..n) in the build, rounded up to a page and at
// least one page (the size a fake image needs); 0 when a pattern's symbol is not carried or its template does not expand.
inline uint32_t ShellExeSpan(int build, const ShellExePat* p, int n)
{
	const uint32_t end = VpExeSigsEnd(build, p, n);
	if (!end)
		return 0;
	const uint32_t bytes = end - VPEXE_IMAGE_BASE < 0x1000u ? 0x1000u : end - VPEXE_IMAGE_BASE;
	return (bytes + 0xFFFu) & ~0xFFFu;
}

// Tests: makes the writable region [base, base + bytes) a fake image of the build: a PE header with its TimeDateStamp and
// SizeOfImage, then the bytes of every pattern of p[0..n) at the build's address of its symbol (operands rebased on base;
// free bytes untouched). broken >= 0: the last compared byte of that pattern is flipped. False when the build is unknown,
// a pattern does not expand or lies outside the region (nothing is written past it).
inline bool ShellExeLay(int build, uintptr_t base, uint32_t bytes, const ShellExePat* p, int n, int broken)
{
	if (!VpExeBuildInfoOf(build) || bytes < 0x1000)
		return false;
	VpExeFakeHeader((unsigned char*)base, build);
	return VpExeLaySigs(build, base, (unsigned char*)base, bytes, p, n, broken);
}
