// proxy_guard.cpp - the proxy's check of the server's window guard (issue 2703). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Why: one-off 9009 found the EXE's WH_CBT activation veto failing (error 126) for an afternoon while the guard only
// logged it. The server's guard (server/src/srv_guard.cpp) now self-tests and publishes guard_state like the EXE's
// (tools/harness/native/README.md, the shared memory layout); bg_helper32 and launch.py refuse an EXE whose guard is
// incomplete, and this is the same check for the server, made by the only party that starts it: the proxy's boot.
// Toolchain: clang-cl -m32, VC9 headers (proxy_rt_internal.h).
#include "proxy_guard.h"
#include "proxy_rt_internal.h"
#include <stdio.h>
#include <string.h>

namespace
{
// Offsets of the GuardShm layout (focus_guard.cpp / srv_guard.cpp static_asserts; native/README.md).
const DWORD kOffMagic = 0, kOffEnabled = 8, kOffWinHooks = 1240, kOffVetoOk = 1268, kOffVetoFailed = 1272,
            kOffGuardState = 1276, kOffGuardMissing = 1280, kOffGuardLog = 1284;  // guard_log: char[260], 9010
const DWORD kMagic = 0x31474243;  // 'CBG1'

LONG At(const BYTE* p, DWORD off)
{
	LONG v;
	memcpy(&v, p + off, sizeof(v));
	return v;
}

const char* ExitCodeMeaning(DWORD code)
{
	switch (code)
	{
	case 0xC5F00030u: return "the server's window guard could not be installed (SRV_GUARD_EXIT_CODE)";
	case 0xC5F00032u: return "the server's guard self-test failed (SRV_GUARD_SELFTEST_EXIT_CODE, issue 2703)";
	case 0xC5F00033u: return "the server's activation veto failed on a thread (SRV_GUARD_VETO_EXIT_CODE, issue 2703)";
	default: return "not a guard exit code";
	}
}
}  // namespace

bool ProxyGuard::CheckServerGuard(HANDLE hServer, unsigned long serverPid)
{
	char env[8] = "";
	const DWORD n = GetEnvironmentVariableA("CIVBG_GUARD", env, sizeof(env));
	if (!(n == 1 && env[0] == '1'))
	{
		ProxyRtInt::Log("boot: guard: CIVBG_GUARD is not 1: the server's window guard is not required, not checked");
		return true;
	}
	if (hServer && WaitForSingleObject(hServer, 0) == WAIT_OBJECT_0)
	{
		DWORD code = 0;
		GetExitCodeProcess(hServer, &code);
		ProxyRtInt::Log("boot FAILED: guard: CIVBG_GUARD=1 and the server (pid %lu) has exited with code 0x%08X: %s",
		                serverPid, (unsigned)code, ExitCodeMeaning(code));
		return false;
	}
	char name[64];
	_snprintf_s(name, sizeof(name), _TRUNCATE, "Local\\CivBgGuard_%lu", serverPid);
	HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
	const BYTE* p = map ? (const BYTE*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 4096) : NULL;
	if (!p)
	{
		ProxyRtInt::Log("boot FAILED: guard: CIVBG_GUARD=1 but the server's guard has no shared memory %s (error %u)", name,
		                (unsigned)GetLastError());
		if (map) CloseHandle(map);
		return false;
	}
	const DWORD magic = (DWORD)At(p, kOffMagic);
	const LONG enabled = At(p, kOffEnabled), hooks = At(p, kOffWinHooks), vetoOk = At(p, kOffVetoOk),
	           vetoFailed = At(p, kOffVetoFailed), state = At(p, kOffGuardState), missing = At(p, kOffGuardMissing);
	char logPath[260];
	memcpy(logPath, p + kOffGuardLog, sizeof(logPath));
	logPath[sizeof(logPath) - 1] = 0;
	UnmapViewOfFile(p);
	CloseHandle(map);
	const bool ok = magic == kMagic && enabled && state == 1 && vetoFailed == 0;
	ProxyRtInt::Log("boot%s: guard: server pid %lu: %s magic 0x%08X, enabled %ld, guard_state %ld, guard_missing 0x%lx "
	                "(bit1 win32u hook, bit2 WH_CBT veto, bit3 dwmapi, bit5 watchdog), win_hooks 0x%lx, veto ok %ld "
	                "failed %ld, guard log %s%s",
	                ok ? "" : " FAILED", serverPid, name, (unsigned)magic, (long)enabled, (long)state,
	                (unsigned long)missing, (unsigned long)hooks, (long)vetoOk, (long)vetoFailed,
	                logPath[0] ? logPath : "(none)",
	                ok ? "" : ": the server's window guard is incomplete; refusing to continue (issue 2703)");
	if (!ok && hServer && WaitForSingleObject(hServer, 0) != WAIT_OBJECT_0)
	{
		// A server whose windows could be activated must not run on: end it now and wait until it is gone (the boot's
		// close would otherwise give it 2 s and then terminate it without waiting).
		TerminateProcess(hServer, 1);
		const DWORD w = WaitForSingleObject(hServer, 5000);
		ProxyRtInt::Log("boot: guard: server pid %lu terminated (%s)", serverPid, w == WAIT_OBJECT_0 ? "gone" : "still there after 5 s");
	}
	return ok;
}
