// vp_exe_patterns.h - the run-time guard: check a byte pattern against the loaded EXE through the build table (epic 0590,
// decision D202; ticket 4960). Written 2026-11-19 for this repository (GPL-3.0-or-later).
//
// A pattern is the template of an X(addr, "name", "bytes") row (the exe_camera.h syntax, extended):
//   2 hex digits      a literal byte
//   ??                any byte
//   R(name)           4 bytes holding the symbol's address in the running build, rebased on the module base; a name has
//                     an underscore (cam_app; an optional VPX_ prefix is accepted)
//   R(hexVA)          the same for the symbol whose anchor (VA in build 4390913, no 0x) is hexVA: R(CC8E74)
//   C(name), C(hexVA) right after a literal E8 or E9: its rel32 must lead to that symbol in the running build (ticket
//                     4960c: tells look-alike wrappers that differ only in their callee apart)
// A literal E8 or E9 byte followed by four literal or ?? tokens frees those four (a call's or jump's rel32 moves with
// every link). The rules are the ones tools/exebuild/gen_table.py checks at table generation; a template that passes
// there passes here on the same build.
//
//     VpExePatternResult r;
//     if (!VpExeCheckPatternAnchor(build, exeBase, 0x51DF70u, "55 8B EC ...", 0, 0, &r))
//         log("pattern %s: %s at +%d", name, r.reason, r.offset);           // one line; then refuse the feature
//
// Memory is read through a reader: the caller's (for example ReadProcessMemory in the server), or, with read = 0 on
// Windows, an in-process reader that asks VirtualQuery first, so an unmapped, guard or no-access page gives a mismatch
// ("unreadable"), never a fault. Header only, C++03 (the proxy's toolchain) and x64. Define VPEXE_NO_WINDOWS_H to leave
// out <windows.h> and the default reader (a reader must then be passed).
#pragma once
#include <stdint.h>
#include <string.h>
#include "vp_exe_symbols.h"
#if defined(_WIN32) && !defined(VPEXE_NO_WINDOWS_H)
#include <windows.h>
#define VPEXE_HAVE_DEFAULT_READER 1
#endif

#define VPEXE_PATTERN_MAX 512   // bytes of one pattern

// Reads n bytes at addr (an address in the inspected process) into dst; returns 1 only when all n were read.
typedef int (*VpExeReadFn)(void* ctx, uintptr_t addr, void* dst, uint32_t n);

struct VpExePatternResult
{
	int ok;              // 1 = every compared byte matched
	int offset;          // first mismatching byte (0-based, in the expanded pattern), -1 when not a byte mismatch
	char reason[128];    // one line for the log: "mismatch", "unreadable", "operand R(x) not carried", "syntax ..."
};

#ifdef VPEXE_HAVE_DEFAULT_READER
// In-process reader: every page of [addr, addr+n) must be committed and readable (VirtualQuery), then a plain copy.
inline int VpExeReadSelf(void* ctx, uintptr_t addr, void* dst, uint32_t n)
{
	(void)ctx;
	uintptr_t p = addr, end = addr + n;
	if (end < addr)
		return 0;
	while (p < end)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery((const void*)p, &mbi, sizeof(mbi)) != sizeof(mbi))
			return 0;
		const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & readable))
			return 0;
		uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
		if (regionEnd <= p)
			return 0;
		p = regionEnd;
	}
	memcpy(dst, (const void*)addr, n);
	return 1;
}
#endif

inline int VpExePatHex_(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Sets r (ok 0, offset, reason = what + up to 60 characters of arg) and returns 0. No printf: no CRT warnings.
inline int VpExePatFail_(VpExePatternResult* r, int offset, const char* what, const char* arg, int argLen)
{
	if (r)
	{
		r->ok = 0;
		r->offset = offset;
		int n = 0;
		for (; what && what[n] && n < 64; ++n)
			r->reason[n] = what[n];
		for (int i = 0; arg && i < argLen && i < 60 && arg[i]; ++i)
			r->reason[n++] = arg[i];
		if (arg && argLen > 0 && what && what[0] && what[strlen(what) - 1] == '(')
			r->reason[n++] = ')';
		r->reason[n] = 0;
	}
	return 0;
}

// The symbol an operand text names (name with an underscore, else a hex anchor VA), or -1.
inline int VpExePatOperand_(const char* t, int len)
{
	for (int i = 0; i < len; ++i)
		if (t[i] == '_')
			return VpExeSymByName(t, len);
	if (len < 1 || len > 8)
		return -1;
	uint32_t va = 0;
	for (int i = 0; i < len; ++i)
	{
		int v = VpExePatHex_(t[i]);
		if (v < 0)
			return -1;
		va = va * 16 + (uint32_t)v;
	}
	return VpExeSymByAnchor(va);
}

// Expands a template for the build and base into exp (and want: 1 = compare, 0 = free). atVa = the pattern's VA in the
// build (ImageBase 0x400000), needed only by C(...) tokens (0: a C token is an error). Returns the length, or -1 with
// r->reason set.
inline int VpExeExpandPatternAt(int build, uintptr_t moduleBase, uint32_t atVa, const char* t, unsigned char* exp,
                                unsigned char* want, int cap, VpExePatternResult* r)
{
	int n = 0;
	unsigned char lit[VPEXE_PATTERN_MAX];   // 1 = ?? token, 2 = literal byte, 0 = operand byte (E8/E9 rule)
	if (cap > VPEXE_PATTERN_MAX)
		cap = VPEXE_PATTERN_MAX;
	if (!t)
		return VpExePatFail_(r, -1, "syntax: no template", 0, 0) - 1;
	while (*t)
	{
		if (*t == ' ' || *t == '\t')
		{
			++t;
			continue;
		}
		if (t[0] == 'C' && t[1] == '(')
		{
			// ticket 4960c: C(x) = the rel32 of the E8/E9 literal right before it, which must lead to x in this build
			const char* s = t + 2;
			const char* e = s;
			while (*e && *e != ')')
				++e;
			if (*e != ')')
				return VpExePatFail_(r, -1, "syntax: unclosed C(", 0, 0) - 1;
			if (n < 1 || lit[n - 1] != 2 || (exp[n - 1] != 0xE8 && exp[n - 1] != 0xE9))
				return VpExePatFail_(r, -1, "syntax: C( not after a literal E8 or E9: C(", s, (int)(e - s)) - 1;
			if (!atVa)
				return VpExePatFail_(r, -1, "C( needs the pattern's address: C(", s, (int)(e - s)) - 1;
			int sym = VpExePatOperand_(s, (int)(e - s));
			if (sym < 0)
				return VpExePatFail_(r, -1, "call target not in the catalogue: C(", s, (int)(e - s)) - 1;
			uint32_t target = VpExeVa(build, sym);
			if (!target)
				return VpExePatFail_(r, -1, "call target not carried to this build: C(", s, (int)(e - s)) - 1;
			if (n + 4 > cap)
				return VpExePatFail_(r, -1, "syntax: pattern too long", 0, 0) - 1;
			uint32_t rel = target - (atVa + (uint32_t)n + 4u);   // from the end of the call or jump
			memcpy(exp + n, &rel, 4);
			for (int k = 0; k < 4; ++k)
			{
				want[n + k] = 1;
				lit[n + k] = 0;
			}
			n += 4;
			t = e + 1;
			continue;
		}
		if (t[0] == 'R' && t[1] == '(')
		{
			const char* s = t + 2;
			const char* e = s;
			while (*e && *e != ')')
				++e;
			if (*e != ')')
				return VpExePatFail_(r, -1, "syntax: unclosed R(", 0, 0) - 1;
			int sym = VpExePatOperand_(s, (int)(e - s));
			if (sym < 0)
				return VpExePatFail_(r, -1, "operand not in the catalogue: R(", s, (int)(e - s)) - 1;
			uint32_t va = VpExeVa(build, sym);
			if (!va)
				return VpExePatFail_(r, -1, "operand not carried to this build: R(", s, (int)(e - s)) - 1;
			if (n + 4 > cap)
				return VpExePatFail_(r, -1, "syntax: pattern too long", 0, 0) - 1;
			uint32_t v = (uint32_t)VpExeRebase(va, moduleBase);
			memcpy(exp + n, &v, 4);
			for (int k = 0; k < 4; ++k)
			{
				want[n + k] = 1;
				lit[n + k] = 0;
			}
			n += 4;
			t = e + 1;
			continue;
		}
		if (n >= cap)
			return VpExePatFail_(r, -1, "syntax: pattern too long", 0, 0) - 1;
		if (t[0] == '?' && t[1] == '?')
		{
			exp[n] = 0;
			want[n] = 0;
			lit[n] = 1;
			++n;
			t += 2;
			continue;
		}
		int hi = VpExePatHex_(t[0]), lo = t[1] ? VpExePatHex_(t[1]) : -1;
		if (hi < 0 || lo < 0)
			return VpExePatFail_(r, -1, "syntax: bad token at", t, 2) - 1;
		exp[n] = (unsigned char)(hi * 16 + lo);
		want[n] = 1;
		lit[n] = 2;   // a literal byte (only these start the E8/E9 rule)
		++n;
		t += 2;
	}
	if (n == 0)
		return VpExePatFail_(r, -1, "syntax: empty template", 0, 0) - 1;
	for (int i = 0; i < n; ++i)
	{
		if (lit[i] == 2 && (exp[i] == 0xE8 || exp[i] == 0xE9) && i + 4 < n && lit[i + 1] && lit[i + 2] && lit[i + 3] &&
		    lit[i + 4])
		{
			want[i + 1] = want[i + 2] = want[i + 3] = want[i + 4] = 0;
			i += 4;
		}
	}
	return n;
}

// The same without the pattern's address (templates without C(...) tokens; the form before ticket 4960c).
inline int VpExeExpandPattern(int build, uintptr_t moduleBase, const char* t, unsigned char* exp, unsigned char* want,
                              int cap, VpExePatternResult* r)
{
	return VpExeExpandPatternAt(build, moduleBase, 0, t, exp, want, cap, r);
}

// Checks a template at a VA of the build (ImageBase 0x400000) in the image loaded at moduleBase. read = 0: the in-process
// reader (Windows). Returns 1 on a match; else 0 with r->reason (and r->offset for a byte mismatch).
inline int VpExeCheckPatternVa(int build, uintptr_t moduleBase, uint32_t va, const char* tmpl, VpExeReadFn read,
                               void* ctx, VpExePatternResult* r)
{
	unsigned char exp[VPEXE_PATTERN_MAX], want[VPEXE_PATTERN_MAX], got[VPEXE_PATTERN_MAX];
	if (r)
	{
		r->ok = 0;
		r->offset = -1;
		r->reason[0] = 0;
	}
	if (build < 0 || build >= kVpExeBuildCount)
		return VpExePatFail_(r, -1, "unknown EXE build", 0, 0);
	if (!va)
		return VpExePatFail_(r, -1, "pattern address not carried to this build", 0, 0);
	int n = VpExeExpandPatternAt(build, moduleBase, va, tmpl, exp, want, VPEXE_PATTERN_MAX, r);
	if (n < 0)
		return 0;
#ifdef VPEXE_HAVE_DEFAULT_READER
	if (!read)
		read = VpExeReadSelf;
#endif
	if (!read)
		return VpExePatFail_(r, -1, "no reader", 0, 0);
	if (!read(ctx, VpExeRebase(va, moduleBase), got, (uint32_t)n))
		return VpExePatFail_(r, -1, "unreadable", 0, 0);
	for (int i = 0; i < n; ++i)
		if (want[i] && exp[i] != got[i])
			return VpExePatFail_(r, i, "mismatch", 0, 0);
	if (r)
	{
		r->ok = 1;
		r->reason[0] = 'o';
		r->reason[1] = 'k';
		r->reason[2] = 0;
	}
	return 1;
}

// The same at a catalogue symbol (VPX_name).
inline int VpExeCheckPatternSym(int build, uintptr_t moduleBase, int sym, const char* tmpl, VpExeReadFn read, void* ctx,
                                VpExePatternResult* r)
{
	return VpExeCheckPatternVa(build, moduleBase, VpExeVa(build, sym), tmpl, read, ctx, r);
}

// The same at the symbol whose anchor (VA in build 4390913) is anchorVa: the X(0x51DF70, ...) rows of today's tables.
inline int VpExeCheckPatternAnchor(int build, uintptr_t moduleBase, uint32_t anchorVa, const char* tmpl,
                                   VpExeReadFn read, void* ctx, VpExePatternResult* r)
{
	int sym = VpExeSymByAnchor(anchorVa);
	if (sym < 0)
	{
		if (r)
			r->offset = -1;
		return VpExePatFail_(r, -1, "pattern address is no catalogue anchor", 0, 0);
	}
	return VpExeCheckPatternVa(build, moduleBase, VpExeVa(build, sym), tmpl, read, ctx, r);
}
