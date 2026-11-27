// sound_exec_abi_test.cpp - ticket 4723: the stack effect of the sound executor's EXE call types
// (proxy/src/proxy_sound_exec.h, "the call types") on stand-in callees written in assembly with the EXE functions' own returns
// (research/02-exe-internals/exe-audio.md 8: the 3D play `ret 0x2C`, FSound::Stop `ret`; 5051: the fade `ret 0xC`). ESP is read before and after
// each call; a callee that pops one word more (`ret 0x30`) is the positive control that the probe sees a mismatch
// (ticket 4725's defect class). x86 only (clang-cl -m32, the proxy's toolchain).
//
//     sound_exec_abi_test.exe [--expect-fail] [-v]
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#define VP_SOUND_EXEC_OFFLINE 1
#include "proxy_sound_exec.h"

#include <stdio.h>
#include <string.h>

using namespace ProxySoundExec;

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

// Stand-ins. The play returns its first stack argument (the script) plus `this`'s low byte, so the arguments' order is
// checked too; the stop returns 1 in AL.
__declspec(naked) void* __fastcall FakePlay(void*, void*, int, void*, const float*, float, int, float, float, float, int,
                                            int, int)
{
	__asm {
		mov eax, [esp + 4]
		add eax, ecx
		ret 0x2C
	}
}
__declspec(naked) void* __fastcall FakePlayWrong(void*, void*, int, void*, const float*, float, int, float, float, float,
                                                 int, int, int)
{
	__asm {
		mov eax, [esp + 4]
		ret 0x30
	}
}
__declspec(naked) unsigned char __fastcall FakeStop(void*, void*)
{
	__asm {
		mov al, 1
		ret
	}
}

// The fade (5051): ret 0xC; stores its first float's bits plus `this`'s low byte, so the order is checked too.
unsigned long g_fadeSeen = 0;
__declspec(naked) void __fastcall FakeFade(void*, void*, float, float, int)
{
	__asm {
		mov eax, [esp + 4]
		add eax, ecx
		mov g_fadeSeen, eax
		ret 0xC
	}
}

unsigned long g_before = 0, g_after = 0;
float g_pos[3] = {1.5f, -2.25f, 0.1f};

__declspec(noinline) long CallPlay(VpAudioPlay3dFn fn, long* deltaOut)
{
	void* r;
	__asm mov g_before, esp
	r = fn((void*)0x10, 0, 77, 0, g_pos, -1.0f, -1, -1.0f, -1.0f, 0.0f, -1, -1, -1);
	__asm mov g_after, esp
	__asm mov esp, g_before   // heals the positive control (a no-op when the callee popped what it should)
	*deltaOut = (long)(g_after - g_before);
	return (long)(size_t)r;
}
__declspec(noinline) long CallStop(VpAudioStopFn fn, long* deltaOut)
{
	unsigned char r;
	__asm mov g_before, esp
	r = fn((void*)0x20, 0);
	__asm mov g_after, esp
	__asm mov esp, g_before   // heals the positive control (a no-op when the callee popped what it should)
	*deltaOut = (long)(g_after - g_before);
	return r;
}
__declspec(noinline) void CallFade(VpAudioFadeFn fn, long* deltaOut)
{
	__asm mov g_before, esp
	fn((void*)0x30, 0, 0.0f, 0.0f, 0);
	__asm mov g_after, esp
	__asm mov esp, g_before   // heals a wrong callee (a no-op when the callee popped what it should)
	*deltaOut = (long)(g_after - g_before);
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
	long r = CallPlay((VpAudioPlay3dFn)&FakePlay, &d);
	Check(d == 0, "VpAudioPlay3dFn on a ret 0x2C callee: ESP unchanged", d, 0);
	Check(r == 77 + 0x10, "VpAudioPlay3dFn: this in ECX, the script as the first stack word", r, 77 + 0x10);
	Check(kVpAudioPlay3dStackBytes == 0x2C, "the play's stack bytes are 0x2C (11 words)", (long)kVpAudioPlay3dStackBytes,
	      0x2C);
	r = CallStop((VpAudioStopFn)&FakeStop, &d);
	Check(d == 0, "VpAudioStopFn on a plain ret callee: ESP unchanged", d, 0);
	Check(r == 1, "VpAudioStopFn: the bool in AL", r, 1);
	CallFade((VpAudioFadeFn)&FakeFade, &d);
	Check(d == 0, "VpAudioFadeFn on a ret 0xC callee: ESP unchanged", d, 0);
	Check(g_fadeSeen == 0x30, "VpAudioFadeFn: this in ECX, 0.0 s as the first stack word", (long)g_fadeSeen, 0x30);
	Check(kVpAudioFadeStackBytes == 0xC, "the fade's stack bytes are 0xC (3 words)", (long)kVpAudioFadeStackBytes, 0xC);
	r = CallPlay((VpAudioPlay3dFn)&FakePlayWrong, &d);
	Check(d == 4, "positive control: a callee that pops one word more is seen (ESP +4)", d, 4);
	if (expectFail) Check(false, "--expect-fail: a deliberately wrong expectation");
	printf("sound_exec_abi_test: %d/%d checks passed\n", g_checks - g_failed, g_checks);
	return g_failed ? 1 : 0;
}
