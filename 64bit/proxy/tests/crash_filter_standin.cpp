// crash_filter_standin.cpp - the stand-in that faults, for proxy/tests/test_crash_filter.py (ticket 0419).
// Built with proxy/src/proxy_crash_filter.cpp in the proxy's toolchain (clang-cl -m32, VC9 headers) and, for the unit
// mode, under MSVC /fsanitize=address. With -DCRASH_FILTER_DLL the same file is the small DLL whose exported function
// faults (a fault in a module other than the EXE).
//
//   crash_filter_standin.exe unit
//   crash_filter_standin.exe <fault mode> <work dir>
// Fault modes: the stand-in installs a "previous" filter of its own first (it writes <work>\prev.txt with the code it
// saw and returns EXCEPTION_EXECUTE_HANDLER, so the process ends with the exception's code, as the EXE's own filter
// lets it), then ProxyCrashFilter::Install(<work>\exe-crash.txt, <work>\vpproxy.log), prints the expected place
// ("expect-where: ..."), and faults:
//   av-read av-write av-exec ud2 dll-ud2 div0 cpp stack two nolog badpath uninstalled noprev fastfail
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifdef CRASH_FILTER_DLL
extern "C" __declspec(dllexport) __declspec(naked) void FaultUd2()
{
	__asm { ud2 }
}
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
	return TRUE;
}
#else
#include "proxy_crash_filter.h"

extern "C" __declspec(noinline) __declspec(naked) void StandinUd2()
{
	__asm { ud2 }
}

static char g_work[MAX_PATH] = "";
static int g_pass = 0, g_fail = 0;

static void Check(const char* name, bool ok, const char* detail = "")
{
	if (ok)
		++g_pass;
	else
		++g_fail;
	printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, (!ok && detail[0]) ? "  [" : "", (!ok && detail[0]) ? detail : "");
	if (!ok && detail[0]) printf("]\n");
}

static LONG WINAPI TestPrev(EXCEPTION_POINTERS* ep)
{
	char p[MAX_PATH + 16], t[64];
	wsprintfA(p, "%s\\prev.txt", g_work);
	wsprintfA(t, "prev saw 0x%08X\r\n", ep && ep->ExceptionRecord ? (unsigned)ep->ExceptionRecord->ExceptionCode : 0u);
	HANDLE f = CreateFileA(p, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
	if (f != INVALID_HANDLE_VALUE)
	{
		DWORD w;
		WriteFile(f, t, (DWORD)strlen(t), &w, NULL);
		CloseHandle(f);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI LaterFilter(EXCEPTION_POINTERS*)
{
	return EXCEPTION_CONTINUE_SEARCH;
}

// The preferred ImageBase in this EXE's file (the loader rewrites the mapped header's field under ASLR).
static ULONG_PTR FileImageBase()
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	FILE* f = fopen(path, "rb");
	if (!f) return 0;
	static unsigned char b[1024];
	const size_t n = fread(b, 1, sizeof(b), f);
	fclose(f);
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)b;
	if (n < sizeof(*dos) || (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > n) return 0;
	return (ULONG_PTR)((const IMAGE_NT_HEADERS*)(b + dos->e_lfanew))->OptionalHeader.ImageBase;
}

static double FakeNow()
{
	return 12.5;
}

static bool Has(const char* hay, const char* needle)
{
	return strstr(hay, needle) != NULL;
}

// ------------------------------------------------------------------------------------------------ unit (no fault)
static int Unit()
{
	char buf[2048], d[512];
	// the code table
	Check("CodeName 0xC0000005", !strcmp(ProxyCrashFilter::CodeName(0xC0000005u), "EXCEPTION_ACCESS_VIOLATION"));
	Check("CodeName 0xC00000FD", !strcmp(ProxyCrashFilter::CodeName(0xC00000FDu), "EXCEPTION_STACK_OVERFLOW"));
	Check("CodeName unknown is empty", ProxyCrashFilter::CodeName(0xC0001234u)[0] == 0);

	// an address in this EXE: module + RVA
	ULONG_PTR base = 0, pref = 0;
	ProxyCrashFilter::DescribeAddress((const void*)&StandinUd2, d, sizeof(d), &base, &pref);
	char want[128];
	wsprintfA(want, "crash_filter_standin.exe+0x%X", (unsigned)((ULONG_PTR)&StandinUd2 - (ULONG_PTR)GetModuleHandleA(NULL)));
	Check("DescribeAddress: an address in the EXE is <exe>+0x<rva>", !_stricmp(d, want), d);
	Check("DescribeAddress: base is the EXE's", base == (ULONG_PTR)GetModuleHandleA(NULL));
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleA(NULL);
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)dos + dos->e_lfanew);
	Check("DescribeAddress: the ImageBase is the EXE file's (not the mapped header's)", pref && pref == FileImageBase());
	(void)nt;
	// kernel32
	ProxyCrashFilter::DescribeAddress((const void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep"), d, sizeof(d),
	                                  &base, NULL);
	Check("DescribeAddress: an address in kernel32", !_strnicmp(d, "kernel32.dll+0x", 15), d);
	// no module: the heap, and NULL
	static char heapLike[64];
	void* h = HeapAlloc(GetProcessHeap(), 0, 64);
	ProxyCrashFilter::DescribeAddress(h, d, sizeof(d), &base, &pref);
	Check("DescribeAddress: a heap address is in no module", Has(d, "an address in no module (0x") && base == 0 && pref == 0, d);
	HeapFree(GetProcessHeap(), 0, h);
	ProxyCrashFilter::DescribeAddress(NULL, d, sizeof(d), &base, NULL);
	Check("DescribeAddress: NULL", !strcmp(d, "an address in no module (0x00000000)"), d);
	(void)heapLike;
	// a short buffer is cut, never overrun
	char tiny[8];
	memset(tiny, 'x', sizeof(tiny));
	ProxyCrashFilter::DescribeAddress((const void*)&StandinUd2, tiny, 6, NULL, NULL);
	Check("DescribeAddress: cut at the buffer", strlen(tiny) == 5 && tiny[6] == 'x' && tiny[7] == 'x', tiny);

	// a synthetic access violation in this EXE
	EXCEPTION_RECORD er;
	memset(&er, 0, sizeof(er));
	er.ExceptionCode = 0xC0000005u;
	er.ExceptionAddress = (PVOID)((ULONG_PTR)&StandinUd2);
	er.NumberParameters = 2;
	er.ExceptionInformation[0] = 1;
	er.ExceptionInformation[1] = 0x20;
	size_t n = ProxyCrashFilter::FormatRecord(&er, 1234, 1234, buf, sizeof(buf));
	char first[600];
	wsprintfA(first, "exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at %s (VA 0x%08X), thread 1234 (the proxy's boot "
	          "thread)\r\n", want, (unsigned)(FileImageBase() + ((ULONG_PTR)&StandinUd2 - (ULONG_PTR)dos)));
	Check("FormatRecord: first line (code, name, module+RVA, VA at the ImageBase, thread)", !strncmp(buf, first, strlen(first)),
	      buf);
	Check("FormatRecord: length returned", n == strlen(buf));
	Check("FormatRecord: access line", Has(buf, "\r\naccess: write to 0x00000020\r\n"), buf);
	Check("FormatRecord: rva line", Has(buf, "\r\nrva: 0x"), buf);
	Check("FormatRecord: thread line on the boot thread", Has(buf, "\r\nthread: 1234 (the proxy's boot thread)\r\n"), buf);
	Check("FormatRecord: the D83 line", Has(buf, "no code bytes, no stack and no memory of the process (D83)"), buf);
	Check("FormatRecord: flags continuable", Has(buf, "\r\nflags: 0x0\r\n"), buf);
	// every line is one of the known facts (nothing else can slip in)
	{
		const char* keys[] = {"exception ", "code: ", "where: ", "module base: ", "rva: ", "va at the EXE's ImageBase: ",
		                      "access: ", "thread: ", "process: ", "time: ", "flags: ", "written by: "};
		bool ok = true;
		for (const char* p = buf; *p;)
		{
			bool known = false;
			for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k)
				if (!strncmp(p, keys[k], strlen(keys[k]))) known = true;
			if (!known) ok = false;
			const char* e = strstr(p, "\r\n");
			if (!e) break;
			p = e + 2;
		}
		Check("FormatRecord: every line is a known fact", ok, buf);
	}
	// another thread, an unknown code in no module, noncontinuable, a nested record
	EXCEPTION_RECORD inner;
	memset(&inner, 0, sizeof(inner));
	er.ExceptionCode = 0xC0001234u;
	er.ExceptionAddress = (PVOID)0x30;
	er.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
	er.ExceptionRecord = &inner;
	ProxyCrashFilter::FormatRecord(&er, 77, 1234, buf, sizeof(buf));
	Check("FormatRecord: unknown code, no module",
	      !strncmp(buf, "exception 0xC0001234 at an address in no module (0x00000030), thread 77\r\n", 72), buf);
	Check("FormatRecord: NTSTATUS error name", Has(buf, "\r\ncode: 0xC0001234 NTSTATUS error\r\n"), buf);
	Check("FormatRecord: no access line for another code", !Has(buf, "access:"), buf);
	Check("FormatRecord: thread not the boot thread", Has(buf, "\r\nthread: 77 (not the proxy's boot thread 1234)\r\n"), buf);
	Check("FormatRecord: noncontinuable and nested", Has(buf, "flags: 0x1 (noncontinuable); a nested exception record"), buf);
	// in-page error with its NTSTATUS
	memset(&er, 0, sizeof(er));
	er.ExceptionCode = 0xC0000006u;
	er.ExceptionAddress = (PVOID)0x30;
	er.NumberParameters = 3;
	er.ExceptionInformation[0] = 0;
	er.ExceptionInformation[1] = 0x1000;
	er.ExceptionInformation[2] = 0xC000009Cu;
	ProxyCrashFilter::FormatRecord(&er, 77, 0, buf, sizeof(buf));
	Check("FormatRecord: in-page error with its NTSTATUS", Has(buf, "access: read of 0x00001000 (NTSTATUS 0xC000009C)"), buf);
	Check("FormatRecord: no boot thread known", Has(buf, "\r\nthread: 77\r\n"), buf);
	ProxyCrashFilter::FormatRecord(NULL, 5, 0, buf, sizeof(buf));
	Check("FormatRecord: no record", !strncmp(buf, "exception: no exception record (thread 5)", 41), buf);
	// a short buffer: cut, terminated, nothing past it
	char cutbuf[40];
	memset(cutbuf, 'x', sizeof(cutbuf));
	er.ExceptionCode = 0xC0000005u;
	n = ProxyCrashFilter::FormatRecord(&er, 77, 0, cutbuf, 32);
	Check("FormatRecord: cut at cap - 1", n == 31 && cutbuf[31] == 0 && cutbuf[32] == 'x', cutbuf);

	// Install / Uninstall
	SetUnhandledExceptionFilter(&TestPrev);
	char line[700];
	char path[MAX_PATH];
	wsprintfA(path, "C:\\nowhere-0419\\exe-crash.txt");
	Check("Install: first call", ProxyCrashFilter::Install(path, INVALID_HANDLE_VALUE, GetCurrentThreadId(), &FakeNow, line,
	                                                       sizeof(line)), line);
	Check("Install: the line names the record and the previous filter",
	      Has(line, "exe-crash filter: installed (ticket 0419); record C:\\nowhere-0419\\exe-crash.txt; it hands on to the "
	                "previous filter crash_filter_standin.exe+0x"), line);
	LPTOP_LEVEL_EXCEPTION_FILTER top = SetUnhandledExceptionFilter(&TestPrev);
	SetUnhandledExceptionFilter(top);
	Check("Install: the filter is the process's filter", top == &ProxyCrashFilter::Filter);
	Check("Install: a second call changes nothing", !ProxyCrashFilter::Install("x", NULL, 0, NULL, line, sizeof(line)) &&
	                                                    Has(line, "already installed"), line);
	top = SetUnhandledExceptionFilter(&TestPrev);
	SetUnhandledExceptionFilter(top);
	Check("Install: still ours after the second call (no self-chain)", top == &ProxyCrashFilter::Filter);
	Check("Uninstall: puts the previous filter back", ProxyCrashFilter::Uninstall(line, sizeof(line)) &&
	                                                      Has(line, "removed"), line);
	top = SetUnhandledExceptionFilter(&TestPrev);
	Check("Uninstall: the previous filter is the process's filter", top == &TestPrev);
	Check("Uninstall: a second call does nothing and says nothing", !ProxyCrashFilter::Uninstall(line, sizeof(line)) &&
	                                                                    line[0] == 0, line);
	// installed again (a reload of the proxy has fresh statics; here the same image), then a later filter on top
	Check("Install: again after Uninstall", ProxyCrashFilter::Install("", INVALID_HANDLE_VALUE, 0, NULL, line, sizeof(line)) &&
	                                            Has(line, "record none (no run folder: the log line only)"), line);
	SetUnhandledExceptionFilter(&LaterFilter);
	Check("Uninstall: a later filter on top is left in place (LOUD)",
	      !ProxyCrashFilter::Uninstall(line, sizeof(line)) && Has(line, "LOUD: exe-crash filter: a later filter") &&
	          Has(line, "replaced it; left in place"), line);
	top = SetUnhandledExceptionFilter(NULL);
	Check("Uninstall: the later filter is still the process's filter", top == &LaterFilter);
	// too long a record path
	char longp[MAX_PATH * 3];
	memset(longp, 'a', sizeof(longp) - 1);
	longp[sizeof(longp) - 1] = 0;
	Check("Install: a record path that is too long is refused",
	      !ProxyCrashFilter::Install(longp, NULL, 0, NULL, line, sizeof(line)) && Has(line, "NOT installed"), line);
	Check("Recorded: nothing was recorded in the unit", !ProxyCrashFilter::Recorded());
	printf("crash_filter_unit: %d/%d checks passed\n", g_pass, g_pass + g_fail);
	return g_fail ? 1 : 0;
}

// ------------------------------------------------------------------------------------------------ faults
static int Recurse(int n)
{
	volatile char pad[512];
	pad[0] = (char)n;
	if (n == -1) return 0;  // never: keeps -Winfinite-recursion quiet
	return Recurse(n + 1) + pad[0];
}

static HANDLE g_go = NULL;
static DWORD WINAPI FaultThread(LPVOID)
{
	WaitForSingleObject(g_go, INFINITE);
	*(volatile int*)0x40 = 1;
	return 0;
}

typedef void (*VoidFn)();

int main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc >= 2 && !strcmp(argv[1], "unit")) return Unit();
	if (argc < 3)
	{
		printf("usage: crash_filter_standin.exe unit | <mode> <work dir>\n");
		return 2;
	}
	const char* mode = argv[1];
	lstrcpynA(g_work, argv[2], MAX_PATH);
	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);  // no WER box (noprev, fastfail)
	char rec[MAX_PATH + 32], logp[MAX_PATH + 32];
	wsprintfA(rec, !strcmp(mode, "badpath") ? "%s\\no-such-folder\\exe-crash.txt" : "%s\\exe-crash.txt", g_work);
	wsprintfA(logp, "%s\\vpproxy.log", g_work);
	HANDLE log = INVALID_HANDLE_VALUE;
	if (strcmp(mode, "nolog"))
		log = CreateFileA(logp, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		                  NULL);
	SetUnhandledExceptionFilter(strcmp(mode, "noprev") ? &TestPrev : NULL);
	char line[700];
	if (!ProxyCrashFilter::Install(rec, log, GetCurrentThreadId(), &FakeNow, line, sizeof(line)))
	{
		printf("standin: FAIL: Install: %s\n", line);
		return 2;
	}
	printf("install: %s\n", line);
	if (!strcmp(mode, "uninstalled"))
	{
		ProxyCrashFilter::Uninstall(line, sizeof(line));
		printf("uninstall: %s\n", line);
	}
	const ULONG_PTR exe = (ULONG_PTR)GetModuleHandleA(NULL);
	if (!strcmp(mode, "av-read") || !strcmp(mode, "uninstalled") || !strcmp(mode, "nolog") || !strcmp(mode, "badpath") ||
	    !strcmp(mode, "noprev"))
	{
		printf("expect-access: read of 0x00000010\n");
		return *(volatile int*)0x10;
	}
	if (!strcmp(mode, "av-write"))
	{
		printf("expect-access: write to 0x00000020\n");
		*(volatile int*)0x20 = 1;
		return 0;
	}
	if (!strcmp(mode, "av-exec"))
	{
		VoidFn f = (VoidFn)(ULONG_PTR)0x30;
		f();
		return 0;
	}
	if (!strcmp(mode, "ud2"))
	{
		printf("expect-where: crash_filter_standin.exe+0x%X\n", (unsigned)((ULONG_PTR)&StandinUd2 - exe));
		printf("expect-va: 0x%08X\n", (unsigned)(FileImageBase() + ((ULONG_PTR)&StandinUd2 - exe)));
		StandinUd2();
		return 0;
	}
	if (!strcmp(mode, "dll-ud2"))
	{
		char dll[MAX_PATH];
		GetModuleFileNameA(NULL, dll, MAX_PATH);
		char* slash = strrchr(dll, '\\');
		lstrcpyA(slash ? slash + 1 : dll, "crash_filter_dll.dll");
		HMODULE m = LoadLibraryA(dll);
		VoidFn f = m ? (VoidFn)GetProcAddress(m, "FaultUd2") : NULL;
		if (!f)
		{
			printf("standin: FAIL: crash_filter_dll.dll or FaultUd2 not found\n");
			return 2;
		}
		printf("expect-where: crash_filter_dll.dll+0x%X\n", (unsigned)((ULONG_PTR)f - (ULONG_PTR)m));
		printf("expect-no-va: a DLL's place is module + RVA only\n");
		f();
		return 0;
	}
	if (!strcmp(mode, "div0"))
	{
		volatile int z = 0;
		return 10 / z;
	}
	if (!strcmp(mode, "cpp")) throw 42;
	if (!strcmp(mode, "stack")) return Recurse(0);
	if (!strcmp(mode, "two"))
	{
		g_go = CreateEventA(NULL, TRUE, FALSE, NULL);
		HANDLE t[2];
		for (int i = 0; i < 2; ++i) t[i] = CreateThread(NULL, 0, &FaultThread, NULL, 0, NULL);
		Sleep(100);
		SetEvent(g_go);
		WaitForMultipleObjects(2, t, TRUE, 10000);
		printf("standin: FAIL: the threads' faults did not end the process\n");
		return 2;
	}
	if (!strcmp(mode, "fastfail"))
	{
		__asm { mov ecx, 7 }  // __fastfail(FAST_FAIL_FATAL_APP_EXIT): int 0x29 (VC9's headers have no __fastfail)
		__asm { int 0x29 }
		return 0;
	}
	printf("standin: FAIL: unknown mode %s\n", mode);
	return 2;
}
#endif
