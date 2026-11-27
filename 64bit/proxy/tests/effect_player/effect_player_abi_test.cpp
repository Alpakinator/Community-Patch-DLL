// effect_player_abi_test.cpp - ticket 4727: the stack effect of the effect player's EXE call type
// (proxy/src/proxy_effect_player.h, VpEffectAllocFn) on a stand-in callee written in assembly with the EXE allocators' own
// return (effects-formats.md 15.7: no argument, a plain `ret`, EAX = the payload). ESP is read before and after the call;
// a callee that pops one word (`ret 4`) is the positive control that the probe sees a mismatch (ticket 4725's defect
// class). x86 only (clang-cl -m32, the proxy's toolchain).
//
//     effect_player_abi_test.exe [--expect-fail] [-v]
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#define VP_EFFECT_PLAYER_OFFLINE 1
#include "proxy_effect_player.h"

#include <stdio.h>
#include <string.h>

using namespace ProxyEffectPlayer;

namespace
{
int g_checks = 0, g_failed = 0;
bool g_verbose = false;
void Check(bool ok, const char* what, long a = 0, long b = 0)
{
	++g_checks;
	if (!ok)
	{
		++g_failed;
		printf("FAIL line: %s (%ld, %ld)\n", what, a, b);
	}
	else if (g_verbose)
		printf("ok   %s (%ld, %ld)\n", what, a, b);
}

unsigned char g_record[0x80];

// The stand-ins: the allocator's shape (ESI saved and restored, ECX cleared, EAX = record + 0x10, plain ret).
__declspec(naked) void* __cdecl FakeAlloc()
{
	__asm {
		push esi
		lea eax, g_record
		xor ecx, ecx
		pop esi
		add eax, 0x10
		ret
	}
}
__declspec(naked) void* __cdecl FakeAllocWrong()
{
	__asm {
		lea eax, g_record
		add eax, 0x10
		ret 4
	}
}

unsigned long g_before = 0, g_after = 0;

__declspec(noinline) long CallAlloc(VpEffectAllocFn fn, long* deltaOut)
{
	void* r;
	__asm mov g_before, esp
	r = fn();
	__asm mov g_after, esp
	__asm mov esp, g_before   // heals the positive control (a no-op when the callee popped what it should)
	*deltaOut = (long)(g_after - g_before);
	return (long)((unsigned char*)r - g_record);
}
}  // namespace

int main(int argc, char** argv)
{
	bool expectFail = false;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--expect-fail")) expectFail = true;
		if (!strcmp(argv[i], "-v")) g_verbose = true;
	}
	long d = 99;
	long r = CallAlloc((VpEffectAllocFn)&FakeAlloc, &d);
	Check(d == 0, "VpEffectAllocFn on a plain ret callee: ESP unchanged", d, 0);
	Check(r == 0x10, "VpEffectAllocFn: the payload pointer in EAX", r, 0x10);
	Check(kVpEffectAllocStackBytes == 0, "the allocators' stack bytes are 0", (long)kVpEffectAllocStackBytes, 0);
	r = CallAlloc((VpEffectAllocFn)&FakeAllocWrong, &d);
	Check(d == 4, "positive control: a callee that pops one word is seen (ESP +4)", d, 4);
	if (expectFail) Check(false, "--expect-fail: a deliberately wrong expectation");
	printf("effect_player_abi_test: %d/%d checks passed\n", g_checks - g_failed, g_checks);
	return g_failed ? 1 : 0;
}
