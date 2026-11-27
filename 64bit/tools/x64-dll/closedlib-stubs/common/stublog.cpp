// stublog.cpp - see stublog.h for the contract and the log format (issue 1005).
//
// All state is plain data with constant initialisers (no constructors run before the first stub call),
// so a stub may be called from any static initialiser of the DLL. The counts are written at unload by
// ExitFlush, registered with atexit from a .CRT$XCB initialiser (below), so it runs after every static
// destructor of the DLL, whose destructors call stubs too (for example gGlobals -> CvGlobals::uninit ->
// Database::Results::Release, issue 1009).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "stublog.h"

namespace
{
	const int kMaxSites = 16384;

	SRWLOCK g_lock = SRWLOCK_INIT;
	HANDLE g_file = NULL;					// NULL = not opened yet, INVALID_HANDLE_VALUE = open failed
	char g_path[MAX_PATH * 2] = { 0 };
	bool g_pathResolved = false;
	LARGE_INTEGER g_t0 = {};
	LARGE_INTEGER g_freq = {};
	StubLogSite* g_sites[kMaxSites] = { 0 };
	long g_nSites = 0;						// sites with a first call logged (also the last seq handed out)
	long g_nDropped = 0;					// first calls beyond kMaxSites (counted, never listed)
	long g_nFlushes = 0;
	bool g_exitFlushed = false;				// the exit flush has run; later hits are "late"

	// "<module>+0x<rva>" for the stub's callers (frames 2..4: StubLog_Hit's caller is the stub itself).
	void DescribeCallers(char* out, size_t cch)
	{
		void* frames[5] = { 0 };
		const USHORT n = RtlCaptureStackBackTrace(1, 5, frames, NULL);
		out[0] = 0;
		for (USHORT i = 0; i < n; ++i)
		{
			HMODULE h = NULL;
			char mod[MAX_PATH] = "?";
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                       static_cast<LPCSTR>(frames[i]), &h) && h)
			{
				char path[MAX_PATH];
				GetModuleFileNameA(h, path, MAX_PATH);
				const char* b = strrchr(path, '\\');
				lstrcpynA(mod, b ? b + 1 : path, MAX_PATH);
			}
			size_t len = strlen(out);
			_snprintf_s(out + len, cch - len, _TRUNCATE, "%s%s+0x%llx", i ? " " : "", mod,
			            h ? (unsigned long long)((char*)frames[i] - (char*)h) : 0ull);
		}
	}

	// Caller holds g_lock (or is single-threaded at first use).
	void ResolvePath()
	{
		if (g_pathResolved)
			return;
		char env[MAX_PATH * 2];
		DWORD n = GetEnvironmentVariableA("CV64_STUBLOG", env, sizeof(env));
		const char* p = (n > 0 && n < sizeof(env)) ? env : "stubcalls.log";
		DWORD m = GetFullPathNameA(p, sizeof(g_path), g_path, NULL);
		if (m == 0 || m >= sizeof(g_path))
		{
			lstrcpynA(g_path, p, sizeof(g_path));
		}
		g_pathResolved = true;
	}

	double Seconds()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (g_freq.QuadPart == 0)
			return 0.0;
		return double(now.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
	}

	void WriteRaw(const char* text, int len)
	{
		if (g_file == NULL || g_file == INVALID_HANDLE_VALUE || len <= 0)
			return;
		DWORD written = 0;
		WriteFile(g_file, text, DWORD(len), &written, NULL);
	}

	void WriteLine(const char* fmt, ...)
	{
		char buf[2048];
		va_list ap;
		va_start(ap, fmt);
		int len = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
		va_end(ap);
		if (len < 0)
			len = int(strlen(buf));	// truncated: write what fits
		WriteRaw(buf, len);
		WriteRaw("\r\n", 2);
	}

	// Caller holds g_lock exclusively. Opens (truncates) the log once per process.
	void EnsureOpen()
	{
		if (g_file != NULL)
			return;
		ResolvePath();
		QueryPerformanceFrequency(&g_freq);
		QueryPerformanceCounter(&g_t0);
		g_file = CreateFileA(g_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (g_file == INVALID_HANDLE_VALUE)
			return;
		WriteLine("#\tcv64-stublog 1\tpid=%lu\tpath=%s", GetCurrentProcessId(), g_path);
	}

	void FlushLocked(bool always)
	{
		if (!always && g_nSites == 0)
			return;
		EnsureOpen();
		long total = 0;
		long n = g_nSites < kMaxSites ? g_nSites : kMaxSites;
		for (long i = 0; i < n; ++i)
		{
			StubLogSite* s = g_sites[i];
			long calls = InterlockedCompareExchange(&s->calls, 0, 0);
			total += calls;
			WriteLine("COUNT\tseq=%ld\tcalls=%ld\t%s\t%s", s->seq, calls, s->group, s->signature);
		}
		++g_nFlushes;
		WriteLine("#\tflush %ld\tt=%.6f\tstubs=%ld\tcalls=%ld\tdropped=%ld", g_nFlushes, Seconds(), n, total, g_nDropped);
		if (g_file != NULL && g_file != INVALID_HANDLE_VALUE)
			FlushFileBuffers(g_file);
	}

	// Writes the counts when the DLL unloads (FreeLibrary or normal process exit). Not reached on
	// TerminateProcess; the FIRST lines are on disk already in that case.
	void __cdecl ExitFlush()
	{
		AcquireSRWLockExclusive(&g_lock);
		FlushLocked(false);
		g_exitFlushed = true;
		ReleaseSRWLockExclusive(&g_lock);
	}

	// Registers ExitFlush before any C++ dynamic initialiser of the DLL runs, so that, atexit being LIFO,
	// it runs after all of their destructors. Issue 1009 (2026-11-10): the issue-1005 version was a global
	// object under `#pragma init_seg(lib)`, but clang-cl 19.1.7 put its initialiser in .CRT$XCU (llvm-objdump
	// of stublog.obj), and since the stub libraries come after the DLL's objects in the link, it was
	// registered last and destroyed first: the stubs called by gGlobals' destructor came after the flush.
	// .CRT$XCB sorts before .CRT$XCU (the linker orders .CRT$XC* by suffix) and after the CRT's own XCA.
	void __cdecl RegisterExitFlush()
	{
		atexit(ExitFlush);
	}
}

#pragma section(".CRT$XCB", read)
extern "C" __declspec(allocate(".CRT$XCB")) void (__cdecl* const cv64_stublog_register_exit_flush)(void);
extern "C" __declspec(allocate(".CRT$XCB")) void (__cdecl* const cv64_stublog_register_exit_flush)(void) =
	RegisterExitFlush;
// Keep the entry even though nothing references it.
#pragma comment(linker, "/include:cv64_stublog_register_exit_flush")

void StubLog_Hit(StubLogSite* site)
{
	if (InterlockedIncrement(&site->calls) != 1)
		return;

	AcquireSRWLockExclusive(&g_lock);
	EnsureOpen();
	long seq = ++g_nSites;
	site->seq = seq;
	if (seq <= kMaxSites)
		g_sites[seq - 1] = site;
	else
		++g_nDropped;
	if (g_exitFlushed)
	{
		char callers[512];
		DescribeCallers(callers, sizeof(callers));
		WriteLine("FIRST\tseq=%ld\ttid=%lu\tt=%.6f\t%s\t%s\tlate: %s", seq, GetCurrentThreadId(), Seconds(),
		          site->group, site->signature, callers);
	}
	else
		WriteLine("FIRST\tseq=%ld\ttid=%lu\tt=%.6f\t%s\t%s", seq, GetCurrentThreadId(), Seconds(), site->group,
		          site->signature);
	ReleaseSRWLockExclusive(&g_lock);
}

void StubLog_Flush()
{
	AcquireSRWLockExclusive(&g_lock);
	FlushLocked(true);
	ReleaseSRWLockExclusive(&g_lock);
}

const char* StubLog_Path()
{
	AcquireSRWLockExclusive(&g_lock);
	ResolvePath();
	ReleaseSRWLockExclusive(&g_lock);
	return g_path;
}
