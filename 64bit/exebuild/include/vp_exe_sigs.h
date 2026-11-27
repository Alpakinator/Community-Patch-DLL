// vp_exe_sigs.h - the EXE's image base and the helpers of a pattern table: check, lay, end, fake header (ticket 0603,
// epic 0590). Written 2026-11-24 for this repository (GPL-3.0-or-later).
//
// The one place for what shell/common/exe_camera.h (VPExe*) and shell/composite/shell_exe.h (ShellExe*) each did on its
// own until ticket 0603: the image-base constant, a pattern table's row, the check of a table against the loaded EXE,
// the fake image a test lays out of a table (its PE header and every row's bytes) and the size such an image needs. The
// two headers keep thin names over these; no other file defines an image-base constant.
//
// A pattern table is rows X(symbol, "description", "bytes") (the syntax of research/02-exe-internals/exe-build-table.md
// section 4, checked by vp_exe_patterns.h; tools/exebuild/gen_table.py checks every row on both builds through
// exebuild/patterns.txt), made into VpExeSig rows with VPEXE_SIG_ENTRY:
//
//     #define MY_SIGNATURES(X) X(cam_app_site, "g_Civ5App", "8B 0D R(cam_app)") ...
//     static const VpExeSig kMySigs[] = { MY_SIGNATURES(VPEXE_SIG_ENTRY) };
//     char why[256];
//     const int build = VpExeResolve(exeBase, kNeed, nNeed, why, sizeof(why));   // the build, or -1 with why
//     if (build < 0 || !VpExeCheckSigsOn(build, exeBase, kMySigs, n, why, sizeof(why))) -> refuse, log why
//
// Two message forms, both tested by the suites that read them: VpExeCheckSigs ("pattern '<description>' at <symbol>
// (VA 0x<va>): <reason>[ at byte k]", any reader, returns the failing row) and VpExeCheckSigsOn ("pattern '<description>'
// at <symbol> does not match on <build label> (byte +k)", the in-process reader). No printf in this file: the messages
// are built by hand, so the header compiles in every unit (C++03 proxy units with VC9 headers, x64, clang-cl, MSVC).
//
// Include structure: vp_exe_symbols.h includes this file for VPEXE_IMAGE_BASE alone (with VPEXE_SIGS_BASE_ONLY_
// defined), before anything of its own; every other include gets the helpers too, which need vp_exe_patterns.h. That is
// why the file has two guarded parts and no #pragma once. Include vp_exe_sigs.h (or vp_exe_patterns.h, or
// vp_exe_symbols.h) in any order.

// ---- Part 1: the image base -----------------------------------------------------------------------------------------
#ifndef VPEXE_IMAGE_BASE
// ImageBase of the DX11 EXE in every known build: every VA of the build tables and the knowledge base is at it. The EXE
// is relocated at run time (0x00740000 / 0x00F60000 seen live): always rebase on the module base (VpExeRebase).
#define VPEXE_IMAGE_BASE 0x400000u
#endif

// ---- Part 2: the helpers --------------------------------------------------------------------------------------------
#if !defined(VPEXE_SIGS_BASE_ONLY_) && !defined(VPEXE_SIGS_HELPERS_)
#define VPEXE_SIGS_HELPERS_
#include <stdint.h>
#include <string.h>
#include "vp_exe_patterns.h"

// One row of a pattern table: the catalogue symbol its bytes sit at (VPX_<name>), a description for the log, the
// template.
struct VpExeSig
{
	int sym;
	const char* name;
	const char* text;
};
#define VPEXE_SIG_ENTRY(sym, name, text) { VPX_##sym, name, text },

// ---- Test hook: one symbol "not carried" ----------------------------------------------------------------------------
// The symbol a test made "not carried" (-1 none). One value per module (an inline function's static). The checks and
// VpExeSigVa below honour it; the lay and the end do not (a test image carries every row).
inline int& VpExeSigDropped()
{
	static int s_sym = -1;
	return s_sym;
}

// Test modes only: the symbol of this name (VPX_ prefix allowed) is treated as not carried. Unknown or empty name: none
// is dropped, returns false.
inline bool VpExeSigTestDrop(const char* name)
{
	const int s = (name && name[0]) ? VpExeSymByName(name, -1) : -1;
	VpExeSigDropped() = s;
	return s >= 0;
}

// The symbol's VA (ImageBase VPEXE_IMAGE_BASE) in the build, 0 when not carried (or dropped by the test hook).
inline uint32_t VpExeSigVa(int build, int sym)
{
	return sym == VpExeSigDropped() ? 0u : VpExeVa(build, sym);
}

// ---- Messages without printf ----------------------------------------------------------------------------------------
// Appends to a NUL-terminated message of capacity cap, truncating as _snprintf_s(_TRUNCATE) does.
struct VpExeMsg_
{
	char* buf;
	size_t cap;
	size_t len;
};
inline void VpExeMsgInit_(VpExeMsg_* m, char* buf, size_t cap)
{
	m->buf = buf;
	m->cap = buf ? cap : 0;
	m->len = 0;
	if (m->cap)
		buf[0] = 0;
}
inline void VpExeMsgStr_(VpExeMsg_* m, const char* s)
{
	for (; s && *s && m->len + 1 < m->cap; ++s)
		m->buf[m->len++] = *s;
	if (m->cap)
		m->buf[m->len] = 0;
}
inline void VpExeMsgDec_(VpExeMsg_* m, int v)
{
	char d[12];
	int k = 0;
	unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
	do
	{
		d[k++] = (char)('0' + u % 10);
		u /= 10;
	} while (u && k < 11);
	char s[14];
	int n = 0;
	if (v < 0)
		s[n++] = '-';
	while (k)
		s[n++] = d[--k];
	s[n] = 0;
	VpExeMsgStr_(m, s);
}
// "0x" and eight upper-case hex digits.
inline void VpExeMsgHex8_(VpExeMsg_* m, uint32_t v)
{
	char s[11] = "0x00000000";
	for (int j = 0; j < 8; ++j)
		s[9 - j] = "0123456789ABCDEF"[(v >> (4 * j)) & 15];
	VpExeMsgStr_(m, s);
}

// ---- The build of a loaded image ------------------------------------------------------------------------------------
// The build whose Steam build id is s ("4390913"), or kVpExeBuildUnknown (tests choose a fake image's build by it).
inline int VpExeBuildBySteam(const char* s)
{
	for (int i = 0; s && s[0] && i < kVpExeBuildCount; ++i)
		if (strcmp(VpExeBuildInfoOf(i)->steamBuild, s) == 0)
			return i;
	return kVpExeBuildUnknown;
}

#ifdef VPEXE_HAVE_DEFAULT_READER
// The build of the image loaded in this process at base (kVpExeBuild*, or kVpExeBuildUnknown): its PE header read behind
// VirtualQuery (an unreadable header is unknown, never a fault). *stamp and *sizeOfImage get the header's values (0 when
// unreadable).
inline int VpExeBuildOfLoaded(uintptr_t base, uint32_t* stamp, uint32_t* sizeOfImage)
{
	unsigned char h[0x400];
	uint32_t e = 0;
	if (stamp)
		*stamp = 0;
	if (sizeOfImage)
		*sizeOfImage = 0;
	if (base < 0x10000 || !VpExeReadSelf(0, base, h, 0x40))
		return kVpExeBuildUnknown;
	memcpy(&e, h + 0x3C, 4);
	if (e < 0x40 || e > sizeof(h) - 84 || !VpExeReadSelf(0, base, h, e + 84))
		return kVpExeBuildUnknown;
	return VpExeBuildOfImage(h, e + 84, stamp, sizeOfImage);
}

// The build of the image at base when it is known and carries every symbol of need[0..n) (the test hook honoured); else
// -1 with one line in why: "not an EXE build the patch knows (PE timestamp 0x..., SizeOfImage 0x...)" or "symbol <name>
// is not carried to <build label> (no address in this binary's table)".
inline int VpExeResolve(uintptr_t base, const int* need, int n, char* why, size_t whyN)
{
	uint32_t ts = 0, soi = 0;
	const int build = VpExeBuildOfLoaded(base, &ts, &soi);
	VpExeMsg_ m;
	if (build == kVpExeBuildUnknown)
	{
		VpExeMsgInit_(&m, why, whyN);
		VpExeMsgStr_(&m, "not an EXE build the patch knows (PE timestamp ");
		VpExeMsgHex8_(&m, ts);
		VpExeMsgStr_(&m, ", SizeOfImage ");
		VpExeMsgHex8_(&m, soi);
		VpExeMsgStr_(&m, ")");
		return -1;
	}
	for (int i = 0; i < n; ++i)
		if (!VpExeSigVa(build, need[i]))
		{
			VpExeMsgInit_(&m, why, whyN);
			VpExeMsgStr_(&m, "symbol ");
			VpExeMsgStr_(&m, VpExeSymName(need[i]));
			VpExeMsgStr_(&m, " is not carried to ");
			VpExeMsgStr_(&m, VpExeBuildLabel(build));
			VpExeMsgStr_(&m, " (no address in this binary's table)");
			return -1;
		}
	return build;
}
#endif   // VPEXE_HAVE_DEFAULT_READER

// ---- Check ----------------------------------------------------------------------------------------------------------
// Every row of sigs[0..n) at its symbol's VA in the image of `build` loaded at base, read through `read` (0 = the
// in-process VirtualQuery reader of vp_exe_patterns.h). Returns -1 when all match, else the first failing row with *r
// (a row whose symbol is not carried, or dropped by the test hook: r->reason "pattern address not carried to this build").
inline int VpExeSigsFirstFail(int build, uintptr_t base, const VpExeSig* sigs, int n, VpExeReadFn read, void* ctx,
                              VpExePatternResult* r)
{
	for (int i = 0; i < n; ++i)
		if (!VpExeCheckPatternVa(build, base, VpExeSigVa(build, sigs[i].sym), sigs[i].text, read, ctx, r))
			return i;
	return -1;
}

// The check with the camera's message form. Returns -1 when all match, else the first failing row, with why =
// "pattern '<description>' at <symbol> (VA 0x<va in this build>): <reason>[ at byte k]".
inline int VpExeCheckSigs(int build, uintptr_t base, const VpExeSig* sigs, int n, VpExeReadFn read, void* ctx, char* why,
                          size_t whyLen)
{
	VpExePatternResult r;
	const int i = VpExeSigsFirstFail(build, base, sigs, n, read, ctx, &r);
	if (i < 0 || !why || !whyLen)
		return i;
	VpExeMsg_ m;
	VpExeMsgInit_(&m, why, whyLen);
	VpExeMsgStr_(&m, "pattern '");
	VpExeMsgStr_(&m, sigs[i].name);
	VpExeMsgStr_(&m, "' at ");
	VpExeMsgStr_(&m, VpExeSymName(sigs[i].sym));
	VpExeMsgStr_(&m, " (VA ");
	VpExeMsgHex8_(&m, VpExeSigVa(build, sigs[i].sym));
	VpExeMsgStr_(&m, "): ");
	VpExeMsgStr_(&m, r.reason);
	if (r.offset >= 0)
	{
		VpExeMsgStr_(&m, " at byte ");
		VpExeMsgDec_(&m, r.offset);
	}
	return i;
}

// The check with the taps' message form, through the in-process reader. True when every row matches; else false with
// one line in why naming the first row that fails, its symbol, the build and the reason: "pattern '<description>':
// symbol <name> is not carried to <build label>", "pattern '<description>' at <symbol> does not match on <build label>
// (byte +k)" or "... does not match on <build label> (<reason>)".
inline bool VpExeCheckSigsOn(int build, uintptr_t base, const VpExeSig* sigs, int n, char* why, size_t whyN)
{
	VpExePatternResult r;
	const int i = VpExeSigsFirstFail(build, base, sigs, n, 0, 0, &r);
	if (i < 0)
		return true;
	VpExeMsg_ m;
	VpExeMsgInit_(&m, why, whyN);
	VpExeMsgStr_(&m, "pattern '");
	VpExeMsgStr_(&m, sigs[i].name);
	if (!VpExeSigVa(build, sigs[i].sym))
	{
		VpExeMsgStr_(&m, "': symbol ");
		VpExeMsgStr_(&m, VpExeSymName(sigs[i].sym));
		VpExeMsgStr_(&m, " is not carried to ");
		VpExeMsgStr_(&m, VpExeBuildLabel(build));
		return false;
	}
	VpExeMsgStr_(&m, "' at ");
	VpExeMsgStr_(&m, VpExeSymName(sigs[i].sym));
	VpExeMsgStr_(&m, " does not match on ");
	VpExeMsgStr_(&m, VpExeBuildLabel(build));
	if (r.offset >= 0)
	{
		VpExeMsgStr_(&m, " (byte +");
		VpExeMsgDec_(&m, r.offset);
	}
	else
	{
		VpExeMsgStr_(&m, " (");
		VpExeMsgStr_(&m, r.reason);
	}
	VpExeMsgStr_(&m, ")");
	return false;
}

// ---- Fake images (tests) --------------------------------------------------------------------------------------------
// The VA just past the furthest row of sigs[0..n) in the build (its expanded bytes; ImageBase VPEXE_IMAGE_BASE): a fake
// image laid out of the table needs at least end - VPEXE_IMAGE_BASE bytes. 0 when a row's symbol is not carried or its
// template does not expand.
inline uint32_t VpExeSigsEnd(int build, const VpExeSig* sigs, int n)
{
	uint32_t end = VPEXE_IMAGE_BASE;
	for (int i = 0; i < n; ++i)
	{
		unsigned char exp[VPEXE_PATTERN_MAX], want[VPEXE_PATTERN_MAX];
		VpExePatternResult r;
		const uint32_t va = VpExeVa(build, sigs[i].sym);
		const int len = va ? VpExeExpandPatternAt(build, VPEXE_IMAGE_BASE, va, sigs[i].text, exp, want, VPEXE_PATTERN_MAX, &r)
		                   : -1;
		if (len < 0)
			return 0;
		if (va + (uint32_t)len > end)
			end = va + (uint32_t)len;
	}
	return end;
}

// A fake image's PE header for a build, at b (at least 0x200 writable bytes; they are cleared first): MZ, e_lfanew 0x80,
// PE, i386, one section, the build's TimeDateStamp, the PE32 magic and the build's SizeOfImage, which is what
// vp_exe_build.h reads. An unknown build writes stamp and SizeOfImage 0 (refused by every validation) and returns false.
inline bool VpExeFakeHeader(unsigned char* b, int build)
{
	const VpExeBuildInfo* bi = VpExeBuildInfoOf(build);
	const uint32_t ts = bi ? bi->timeDateStamp : 0, soi = bi ? bi->sizeOfImage : 0, e = 0x80;
	const uint16_t machine = 0x14C, sections = 1, magic = 0x10B, optSize = 0xE0;
	memset(b, 0, 0x200);
	b[0] = 'M';
	b[1] = 'Z';
	memcpy(b + 0x3C, &e, 4);
	memcpy(b + e, "PE\0\0", 4);
	memcpy(b + e + 4, &machine, 2);
	memcpy(b + e + 6, &sections, 2);
	memcpy(b + e + 8, &ts, 4);
	memcpy(b + e + 20, &optSize, 2);
	memcpy(b + e + 24, &magic, 2);
	memcpy(b + e + 24 + 56, &soi, 4);
	return bi != 0;
}

// Lays every row of sigs[0..n) into a fake image of `build`: img is the image's first byte (the byte of VA
// VPEXE_IMAGE_BASE), imgBytes its size; each row's compared bytes go to img[va - VPEXE_IMAGE_BASE], operands rebased on
// base (the address the image stands at; img itself for an image the code under test reads in place). Free bytes (??, a
// call's rel32) are left as they are. broken >= 0: that row's last compared byte is flipped (a refusal test). False when
// a row's symbol is not carried, its template does not expand, or it would lie in the header page's first 0x200 bytes
// or past imgBytes (nothing of that row is written). The header is not written: VpExeFakeHeader.
inline bool VpExeLaySigs(int build, uintptr_t base, unsigned char* img, uint32_t imgBytes, const VpExeSig* sigs, int n,
                         int broken)
{
	for (int i = 0; i < n; ++i)
	{
		unsigned char exp[VPEXE_PATTERN_MAX], want[VPEXE_PATTERN_MAX];
		VpExePatternResult r;
		const uint32_t va = VpExeVa(build, sigs[i].sym);
		const int len = va ? VpExeExpandPatternAt(build, base, va, sigs[i].text, exp, want, VPEXE_PATTERN_MAX, &r) : -1;
		if (len < 0 || va - VPEXE_IMAGE_BASE < 0x200 || va - VPEXE_IMAGE_BASE + (uint32_t)len > imgBytes)
			return false;
		const uint32_t off = va - VPEXE_IMAGE_BASE;
		int last = -1;
		for (int k = 0; k < len; ++k)
			if (want[k])
			{
				img[off + k] = exp[k];
				last = k;
			}
		if (i == broken && last >= 0)
			img[off + last] ^= 0x5A;
	}
	return true;
}

// Tests: the header of a fake image at base names no known build afterwards (its TimeDateStamp changed).
inline void VpExeTestForgetBuild(uintptr_t base)
{
	uint32_t e = 0, ts = 0;
	memcpy(&e, (const void*)(base + 0x3C), 4);
	memcpy(&ts, (const void*)(base + e + 8), 4);
	ts ^= 0x5A5A5A5Au;
	memcpy((void*)(base + e + 8), &ts, 4);
}

#endif   // part 2
