// exe_tables_test.cpp - unit test of exebuild/include/vp_exe_build.h, vp_exe_symbols.h and vp_exe_patterns.h against a
// fake image (ticket 4960, epic 0590). Written 2026-11-19 for this repository (GPL-3.0-or-later).
//
// Built by tools/exebuild/tests/test_exebuild.py twice: with the proxy's x86 toolchain (clang-cl -m32 in the VS 2008
// environment, C++03 rules: no auto, no lambdas) and with an x64 MSVC cl. The generated header comes from a FAKE catalogue
// and FAKE tables written by the test (symbols tst_*; both builds' values made up), never the real ones.
// Prints one line per failed check and "exe_tables_test: N/N checks passed"; exit 0 only when all pass.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vp_exe_patterns.h"

static int g_checks = 0, g_failed = 0;

static void Check(bool ok, const char* what)
{
	++g_checks;
	if (!ok)
	{
		++g_failed;
		printf("FAIL %s\n", what);
	}
}

// The fake image: a buffer that stands for the EXE loaded at base = the buffer's address. RVA = VA - 0x400000.
static unsigned char g_image[0x4000];

static void Put(uint32_t va, const unsigned char* b, int n)
{
	memcpy(g_image + (va - VPEXE_IMAGE_BASE), b, (size_t)n);
}

static void Put32(uint32_t va, uint32_t v)
{
	memcpy(g_image + (va - VPEXE_IMAGE_BASE), &v, 4);
}

struct FakeReader
{
	uintptr_t base;
	uint32_t size;
	int calls;
};

static int ReadFake(void* ctx, uintptr_t addr, void* dst, uint32_t n)
{
	FakeReader* f = (FakeReader*)ctx;
	++f->calls;
	if (addr < f->base || addr + n > f->base + f->size)
		return 0;
	memcpy(dst, (const void*)addr, n);
	return 1;
}

static int ReadNever(void*, uintptr_t, void*, uint32_t)
{
	return 0;
}

int main()
{
	// --- build identity (vp_exe_build.h)
	Check(VpExeBuildFromHeader(0x546CD0A8u, 0x06519000u) == kVpExeBuild4390913, "header of build 4390913");
	Check(VpExeBuildFromHeader(0x6AF3837Du, 0x063A2000u) == kVpExeBuild26577625, "header of build 26577625");
	Check(VpExeBuildFromHeader(0x12345678u, 0x06519000u) == kVpExeBuildUnknown, "unknown stamp -> kVpExeBuildUnknown");
	Check(VpExeBuildFromHeader(0x546CD0A8u, 0x06000000u) == kVpExeBuildUnknown, "stamp alone is not enough");
	Check(VpExeVa(kVpExeBuildUnknown, VPX_tst_fn) == 0, "unknown build carries nothing");
	Check(!VpExeBuildSupported(kVpExeBuildUnknown), "unknown build not supported");
	Check(strcmp(VpExeBuildLabel(kVpExeBuildUnknown), "unknown build") == 0, "label of an unknown build");

	// --- the tables (fake values written by the test)
	Check(kVpExeSymCount == 4, "four fake symbols");
	Check(VpExeVa(kVpExeBuild4390913, VPX_tst_fn) == 0x401000u, "anchor build value = anchor");
	Check(VpExeVa(kVpExeBuild26577625, VPX_tst_fn) == 0x401800u, "carried value of tst_fn");
	Check(VpExeVa(kVpExeBuild26577625, VPX_tst_data) == 0x402400u, "carried value of tst_data");
	Check(VpExeVa(kVpExeBuild26577625, VPX_tst_missing) == 0, "a symbol not carried is 0");
	Check(VpExeRva(kVpExeBuild26577625, VPX_tst_data) == 0x2400u, "RVA");
	Check(VpExeAddr(kVpExeBuild26577625, VPX_tst_data, 0x10000000u) == 0x10002400u, "rebase");
	Check(VpExeAddr(kVpExeBuild26577625, VPX_tst_missing, 0x10000000u) == 0, "rebase of 0 stays 0");
	Check(VpExeBuildSupported(kVpExeBuild4390913), "anchor build complete");
	Check(!VpExeBuildSupported(kVpExeBuild26577625), "fake new build marked incomplete");
	Check(VpExeSymByName("tst_data", -1) == VPX_tst_data, "by name");
	Check(VpExeSymByName("VPX_tst_data", -1) == VPX_tst_data, "by name with VPX_ prefix");
	Check(VpExeSymByName("tst_nope", -1) == -1, "unknown name");
	Check(VpExeSymByAnchor(0x402000u) == VPX_tst_data, "by anchor");
	Check(VpExeSymByAnchor(0x7777777u) == -1, "unknown anchor");
	Check(strcmp(VpExeSymName(VPX_tst_fn), "tst_fn") == 0 && strcmp(VpExeSymName(99), "?") == 0, "names for logs");
	int rows = 0, cols = 0;
	const uint32_t* a1 = VpExeArray(kVpExeBuild26577625, VPXA_tst_sites, &rows, &cols);
	const uint32_t* a0 = VpExeArray(kVpExeBuild4390913, VPXA_tst_sites, 0, 0);
	Check(a1 && a0 && rows == 2 && cols == 3, "array shape");
	Check(a0 && a0[0] == 0x401000u && a0[1] == 7u && a0[2] == 0x402000u, "array row 0 in the anchor build");
	Check(a1 && a1[0] == 0x401800u && a1[1] == 7u && a1[2] == 0x402400u, "array row 0 carried, keep column copied");
	Check(a1 && a1[3] == 0 && a1[4] == 9u && a1[5] == 0x402400u, "array cell not carried is 0");
	Check(VpExeArray(kVpExeBuildUnknown, VPXA_tst_sites, 0, 0) == 0, "array of an unknown build");

	// --- patterns on a fake image of build 26577625: tst_fn at 0x401800 loads [tst_data] and calls something
	uintptr_t base = (uintptr_t)g_image;
	const unsigned char code[] = { 0x8B, 0x0D, 0, 0, 0, 0, 0xE8, 0x11, 0x22, 0x33, 0x44, 0xC3 };
	Put(0x401800u, code, (int)sizeof(code));
	Put32(0x401802u, (uint32_t)VpExeAddr(kVpExeBuild26577625, VPX_tst_data, base));   // the loader's relocation
	FakeReader fr = { base, (uint32_t)sizeof(g_image), 0 };
	VpExePatternResult r;
	const int B = kVpExeBuild26577625;
	Check(VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0D R(tst_data) E8 00 00 00 00 C3", ReadFake, &fr, &r) && r.ok,
	      "pattern by names, E8 rel32 free");
	Check(VpExeCheckPatternAnchor(B, base, 0x401000u, "8B 0D R(402000) E8 ?? ?? ?? ?? C3", ReadFake, &fr, &r),
	      "pattern by anchor VAs (today's X(0xVA) / R(hex) form)");
	Check(VpExeCheckPatternAnchor(B, base, 0x401000u, "8B0D R(VPX_tst_data)", ReadFake, &fr, &r), "packed hex, VPX_");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0E R(tst_data)", ReadFake, &fr, &r) && r.offset == 1 &&
	          strcmp(r.reason, "mismatch") == 0,
	      "mismatch at offset 1");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0D R(tst_fn)", ReadFake, &fr, &r) && r.offset >= 2 &&
	          r.offset <= 5,
	      "wrong operand is a mismatch inside the operand");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0D R(tst_nope)", ReadFake, &fr, &r) &&
	          strstr(r.reason, "not in the catalogue") && strstr(r.reason, "tst_nope"),
	      "unknown operand named in the reason");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0D R(tst_missing)", ReadFake, &fr, &r) &&
	          strstr(r.reason, "not carried"),
	      "operand not carried");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_missing, "8B", ReadFake, &fr, &r) && strstr(r.reason, "not carried"),
	      "pattern address not carried");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0D R(tst_data", ReadFake, &fr, &r) &&
	          strstr(r.reason, "syntax"),
	      "syntax: unclosed R(");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0G", ReadFake, &fr, &r) && strstr(r.reason, "syntax"),
	      "syntax: bad hex");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "", ReadFake, &fr, &r) && strstr(r.reason, "syntax"),
	      "syntax: empty");
	Check(!VpExeCheckPatternSym(B, base, VPX_tst_fn, "8B 0D", ReadNever, 0, &r) && strcmp(r.reason, "unreadable") == 0,
	      "unreadable through the reader");
	Check(!VpExeCheckPatternSym(kVpExeBuildUnknown, base, VPX_tst_fn, "8B", ReadFake, &fr, &r) &&
	          strstr(r.reason, "unknown EXE build"),
	      "unknown build refused");
	Check(!VpExeCheckPatternAnchor(B, base, 0x405555u, "8B", ReadFake, &fr, &r) && strstr(r.reason, "anchor"),
	      "pattern address not an anchor");
	// E8 followed by an operand: not freed
	const unsigned char call2[] = { 0xE8, 0, 0, 0, 0 };
	Put(0x401900u, call2, 5);
	// (the operand's low byte may be 0 when the buffer is 256-aligned, as under ASan: the mismatch is then further in)
	Check(!VpExeCheckPatternVa(B, base, 0x401900u, "E8 R(tst_data)", ReadFake, &fr, &r) && r.offset >= 1 &&
	          r.offset <= 4,
	      "E8 before an operand is not freed");
	// ticket 4960c: C(name) checks a call's target. At 0x401A00 (no symbol; checked by VA) a call to tst_fn in this build.
	{
		const uint32_t at = 0x401A00u, target = VpExeVa(B, VPX_tst_fn);
		const uint32_t rel = target - (at + 1 + 4);
		unsigned char call3[6] = {0xE8, 0, 0, 0, 0, 0xC3};
		memcpy(call3 + 1, &rel, 4);
		Put(at, call3, 6);
		Check(VpExeCheckPatternVa(B, base, at, "E8 C(tst_fn) C3", ReadFake, &fr, &r) && r.ok, "C(name): the right callee");
		Check(VpExeCheckPatternVa(B, base, at, "E8 C(401000) C3", ReadFake, &fr, &r), "C(hex anchor): the right callee");
		Check(!VpExeCheckPatternVa(B, base, at, "E8 C(tst_vtbl) C3", ReadFake, &fr, &r) && r.offset >= 1 && r.offset <= 4,
		      "C(name): another callee is a mismatch in the rel32");
		Check(!VpExeCheckPatternVa(B, base, at, "8B C(tst_fn)", ReadFake, &fr, &r) && strstr(r.reason, "not after"),
		      "C( not after E8/E9 is a syntax error");
		Check(!VpExeCheckPatternVa(B, base, at, "E8 C(tst_missing)", ReadFake, &fr, &r) && strstr(r.reason, "not carried"),
		      "C( of a symbol not carried");
		unsigned char ex[16], wa[16];
		Check(VpExeExpandPattern(B, base, "E8 C(tst_fn)", ex, wa, 16, &r) < 0 && strstr(r.reason, "needs the pattern"),
		      "C( without the pattern's address is refused");
	}
	// the same image read as build 4390913: tst_fn's anchor 0x401000 holds nothing of this
	Check(!VpExeCheckPatternSym(kVpExeBuild4390913, base, VPX_tst_fn, "8B 0D R(tst_data)", ReadFake, &fr, &r),
	      "the other build's table does not match this image");
#ifdef VPEXE_HAVE_DEFAULT_READER
	// the in-process reader never faults: a no-access page and an unmapped range are "unreadable"
	unsigned char* page = (unsigned char*)VirtualAlloc(0, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	Check(page != 0, "VirtualAlloc");
	if (page)
	{
		memcpy(page + 0x1000 - 2, "\x8B\x0D", 2);
		DWORD old = 0;
		VirtualProtect(page + 0x1000, 0x1000, PAGE_NOACCESS, &old);
		uintptr_t fakeBase = (uintptr_t)page + 0x1000 - 2 - (0x401800u - VPEXE_IMAGE_BASE);
		Check(!VpExeCheckPatternSym(B, fakeBase, VPX_tst_fn, "8B 0D R(tst_data)", 0, 0, &r) &&
		          strcmp(r.reason, "unreadable") == 0,
		      "default reader: pattern running into a no-access page");
		Check(VpExeCheckPatternSym(B, fakeBase, VPX_tst_fn, "8B 0D", 0, 0, &r), "default reader: readable part");
		VirtualFree(page, 0, MEM_RELEASE);
		Check(!VpExeCheckPatternSym(B, fakeBase, VPX_tst_fn, "8B 0D", 0, 0, &r), "default reader: freed memory");
	}
#endif
	printf("exe_tables_test: %d/%d checks passed (%s)\n", g_checks - g_failed, g_checks,
	       sizeof(void*) == 8 ? "x64" : "x86");
	return g_failed ? 1 : 0;
}
