// proxy_crash_filter.cpp - the proxy's chained top-level exception filter (ticket 0419). See proxy_crash_filter.h and
// research/03-dll-engine-boundary/error-report-exe-crash.md section 10.
//
// Rules for the code that runs in the filter (a crashed process: the heap may be corrupt, another thread may hold the
// loader lock or the CRT's locks, the stack may be nearly used up after a stack overflow):
//  - no heap, no CRT formatting, no C++ objects: a fixed static buffer and the small appenders below;
//  - no loader lock: the module of an address comes from VirtualQuery (its allocation base) and its file name from
//    K32GetMappedFileNameA (resolved at Install, not in the filter); never GetModuleFileName / GetModuleHandleEx;
//  - small stack: everything large is static, guarded by the once-flag;
//  - no read of the process's memory at all: VirtualQuery and the mapped file's name only. The EXE's preferred ImageBase
//    (for the VA form) is read from the EXE's file header at Install. No code bytes, no stack, no data (D83).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "proxy_crash_filter.h"

namespace ProxyCrashFilter
{
namespace
{
typedef DWORD(WINAPI* MappedNameFn)(HANDLE, LPVOID, LPSTR, DWORD);

LPTOP_LEVEL_EXCEPTION_FILTER g_prev = NULL;
bool g_installed = false;
volatile LONG g_once = 0;              // 0 nothing yet, 1 a thread is recording, 2 recorded
volatile DWORD g_recordingThread = 0;
const DWORD kWaitMs = 5000;            // how long another faulting thread waits for the record
char g_recordPath[MAX_PATH * 2] = "";
HANDLE g_log = INVALID_HANDLE_VALUE;
DWORD g_bootThread = 0;
double (*g_now)() = NULL;
MappedNameFn g_mappedName = NULL;
char g_record[2048];     // the record (static: the filter may run on the last pages of an overflowed stack)
char g_logLine[1024];
char g_nameBuf[MAX_PATH * 2];

// ---------------------------------------------------------------------------------------------- appenders (no CRT)
struct Out
{
	char* p;
	size_t cap;
	size_t n;
	Out(char* buf, size_t c) : p(buf), cap(c), n(0)
	{
		if (cap) p[0] = 0;
	}
	void ch(char c)
	{
		if (n + 1 < cap)
		{
			p[n++] = c;
			p[n] = 0;
		}
	}
	void s(const char* t)
	{
		if (!t) return;
		while (*t) ch(*t++);
	}
	void hex(ULONG_PTR v, int digits)  // "0x" + at least `digits` upper-case digits
	{
		char d[2 * sizeof(ULONG_PTR) + 1];
		int k = 0;
		do
		{
			d[k++] = "0123456789ABCDEF"[v & 15];
			v >>= 4;
		} while (v && k < (int)sizeof(d) - 1);
		while (k < digits && k < (int)sizeof(d) - 1) d[k++] = '0';
		s("0x");
		while (k) ch(d[--k]);
	}
	void dec(unsigned long long v, int width = 0, char pad = ' ')
	{
		char d[24];
		int k = 0;
		do
		{
			d[k++] = (char)('0' + (int)(v % 10));
			v /= 10;
		} while (v && k < (int)sizeof(d));
		while (k < width && k < (int)sizeof(d)) d[k++] = pad;
		while (k) ch(d[--k]);
	}
	void nl() { s("\r\n"); }
};

const char* FileNamePart(const char* path)
{
	const char* f = path;
	for (const char* q = path; *q; ++q)
		if (*q == '\\' || *q == '/') f = q + 1;
	return f;
}

// The EXE's load address and its preferred ImageBase, read from the EXE's FILE at Install (the knowledge base's VA form
// is "VA at ImageBase 0x400000"). Not from the mapped header: the loader rewrites OptionalHeader.ImageBase in memory to
// the address it loaded the image at (found by the e2e case, 2026-11-16: the in-memory header said 0x00A90000 for an
// image loaded there by ASLR).
ULONG_PTR g_exeBase = 0;
ULONG_PTR g_exePreferred = 0;
unsigned char g_hdr[1024];

void ResolveExe()
{
	if (g_exeBase) return;
	g_exeBase = (ULONG_PTR)GetModuleHandleA(NULL);
	char path[MAX_PATH];
	const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (!n || n >= MAX_PATH) return;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD got = 0;
	const BOOL ok = ReadFile(f, g_hdr, sizeof(g_hdr), &got, NULL);
	CloseHandle(f);
	if (!ok || got < sizeof(IMAGE_DOS_HEADER)) return;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)g_hdr;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
	    (DWORD)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > got)
		return;
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(g_hdr + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) return;
	g_exePreferred = (ULONG_PTR)nt->OptionalHeader.ImageBase;
}

void TimeText(Out& o)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	o.dec(t.wYear, 4, '0');
	o.ch('-');
	o.dec(t.wMonth, 2, '0');
	o.ch('-');
	o.dec(t.wDay, 2, '0');
	o.ch(' ');
	o.dec(t.wHour, 2, '0');
	o.ch(':');
	o.dec(t.wMinute, 2, '0');
	o.ch(':');
	o.dec(t.wSecond, 2, '0');
	o.ch('.');
	o.dec(t.wMilliseconds, 3, '0');
}

// "[%10.6f] [%5u] " as vpproxy.log's lines start (proxy_rt.cpp LogV), without the CRT.
void LogHead(Out& o, DWORD tid)
{
	o.ch('[');
	double t = g_now ? g_now() : 0.0;
	if (!(t >= 0.0) || t > 4.0e9) t = 0.0;
	const unsigned long long us = (unsigned long long)(t * 1e6 + 0.5);
	char tmp[32];
	Out x(tmp, sizeof(tmp));
	x.dec(us / 1000000ull);
	for (size_t k = x.n; k < 3; ++k) o.ch(' ');  // %10.6f: 3 + '.' + 6 = 10 for times below 1000 s
	o.s(tmp);
	o.ch('.');
	o.dec(us % 1000000ull, 6, '0');
	o.s("] [");
	char t2[16];
	Out y(t2, sizeof(t2));
	y.dec(tid);
	for (size_t k = y.n; k < 5; ++k) o.ch(' ');
	o.s(t2);
	o.s("] ");
}

void WriteAll(HANDLE h, const char* p, size_t n)
{
	while (n)
	{
		DWORD w = 0;
		if (!WriteFile(h, p, (DWORD)n, &w, NULL) || !w) return;
		p += w;
		n -= w;
	}
}

size_t Len(const char* s)
{
	size_t n = 0;
	while (s[n]) ++n;
	return n;
}

void Record(EXCEPTION_POINTERS* ep)
{
	const DWORD tid = GetCurrentThreadId();
	const EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : NULL;
	const size_t n = FormatRecord(er, tid, g_bootThread, g_record, sizeof(g_record));
	bool written = false;
	DWORD err = 0;
	if (g_recordPath[0])
	{
		HANDLE f = CreateFileA(g_recordPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
		if (f != INVALID_HANDLE_VALUE)
		{
			WriteAll(f, g_record, n);
			FlushFileBuffers(f);
			CloseHandle(f);
			written = true;
		}
		else
			err = GetLastError();
	}
	if (g_log && g_log != INVALID_HANDLE_VALUE)
	{
		Out o(g_logLine, sizeof(g_logLine));
		LogHead(o, tid);
		o.s("LOUD: exe-crash: an unhandled exception in the game's process (ticket 0419): ");
		size_t k = 0;
		while (g_record[k] && g_record[k] != '\r' && g_record[k] != '\n') o.ch(g_record[k++]);
		if (!g_recordPath[0])
			o.s("; no record file (no run folder)");
		else if (written)
		{
			o.s("; record ");
			o.s(g_recordPath);
		}
		else
		{
			o.s("; the record ");
			o.s(g_recordPath);
			o.s(" could not be written (error ");
			o.dec(err);
			o.s(")");
		}
		o.s(g_prev && g_prev != &Filter ? "; handed on to the previous filter"
		                                : "; no previous filter: the system's default handling follows");
		o.ch('\n');
		WriteAll(g_log, g_logLine, o.n);
		FlushFileBuffers(g_log);
	}
}
}  // namespace

const char* CodeName(DWORD code)
{
	switch (code)
	{
	case 0xC0000005u: return "EXCEPTION_ACCESS_VIOLATION";
	case 0xC0000006u: return "EXCEPTION_IN_PAGE_ERROR";
	case 0xC0000017u: return "STATUS_NO_MEMORY";
	case 0xC000001Du: return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case 0xC0000025u: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
	case 0xC0000026u: return "EXCEPTION_INVALID_DISPOSITION";
	case 0xC000008Cu: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case 0xC000008Du: return "EXCEPTION_FLT_DENORMAL_OPERAND";
	case 0xC000008Eu: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case 0xC000008Fu: return "EXCEPTION_FLT_INEXACT_RESULT";
	case 0xC0000090u: return "EXCEPTION_FLT_INVALID_OPERATION";
	case 0xC0000091u: return "EXCEPTION_FLT_OVERFLOW";
	case 0xC0000092u: return "EXCEPTION_FLT_STACK_CHECK";
	case 0xC0000093u: return "EXCEPTION_FLT_UNDERFLOW";
	case 0xC0000094u: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case 0xC0000095u: return "EXCEPTION_INT_OVERFLOW";
	case 0xC0000096u: return "EXCEPTION_PRIV_INSTRUCTION";
	case 0xC00000FDu: return "EXCEPTION_STACK_OVERFLOW";
	case 0xC0000374u: return "STATUS_HEAP_CORRUPTION";
	case 0xC0000409u: return "STATUS_STACK_BUFFER_OVERRUN";
	case 0xC0000417u: return "STATUS_INVALID_CRUNTIME_PARAMETER";
	case 0xE06D7363u: return "an unhandled C++ exception (MSVC 0xE06D7363)";
	case 0x80000002u: return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case 0x80000003u: return "EXCEPTION_BREAKPOINT";
	case 0x80000004u: return "EXCEPTION_SINGLE_STEP";
	default: return "";
	}
}

// K32GetMappedFileNameA (kernel32, Windows 7 and later), else psapi's GetMappedFileNameA. Resolved at Install (outside
// the filter: GetProcAddress and LoadLibrary take the loader lock); DescribeAddress resolves it on its first call
// outside the filter, which only the tests make.
static void ResolveNames()
{
	if (g_mappedName) return;
	HMODULE k32 = GetModuleHandleA("kernel32.dll");
	g_mappedName = k32 ? (MappedNameFn)GetProcAddress(k32, "K32GetMappedFileNameA") : NULL;
	if (!g_mappedName)
	{
		HMODULE ps = LoadLibraryA("psapi.dll");
		g_mappedName = ps ? (MappedNameFn)GetProcAddress(ps, "GetMappedFileNameA") : NULL;
	}
}

void DescribeAddress(const void* addr, char* out, size_t cap, ULONG_PTR* baseOut, ULONG_PTR* imageBaseOut)
{
	if (!g_installed && g_once == 0)  // never inside the filter (Install resolved both before)
	{
		ResolveNames();
		ResolveExe();
	}
	Out o(out, cap);
	ULONG_PTR base = 0, pref = 0;
	MEMORY_BASIC_INFORMATION mbi;
	if (VirtualQuery(addr, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE && mbi.AllocationBase)
	{
		base = (ULONG_PTR)mbi.AllocationBase;
		pref = base == g_exeBase ? g_exePreferred : 0;  // the VA form only for the EXE
		const char* name = "";
		if (g_mappedName)
		{
			const DWORD k = g_mappedName(GetCurrentProcess(), mbi.AllocationBase, g_nameBuf, (DWORD)sizeof(g_nameBuf));
			if (k && k < sizeof(g_nameBuf))
			{
				g_nameBuf[k] = 0;
				name = FileNamePart(g_nameBuf);
			}
		}
		o.s(name[0] ? name : "(an image whose name could not be read)");
		o.ch('+');
		o.hex((ULONG_PTR)addr - base, 1);
	}
	else
	{
		o.s("an address in no module (");
		o.hex((ULONG_PTR)addr, 8);
		o.ch(')');
	}
	if (baseOut) *baseOut = base;
	if (imageBaseOut) *imageBaseOut = pref;
}

size_t FormatRecord(const EXCEPTION_RECORD* er, DWORD threadId, DWORD bootThread, char* out, size_t cap)
{
	Out o(out, cap);
	if (!er)
	{
		o.s("exception: no exception record (thread ");
		o.dec(threadId);
		o.s(")");
		o.nl();
		o.s("written by: the proxy's chained unhandled-exception filter (ticket 0419)");
		o.nl();
		return o.n;
	}
	const DWORD code = er->ExceptionCode;
	const char* name = CodeName(code);
	char where[MAX_PATH + 64];
	ULONG_PTR base = 0, pref = 0;
	DescribeAddress(er->ExceptionAddress, where, sizeof(where), &base, &pref);
	const ULONG_PTR rva = base ? (ULONG_PTR)er->ExceptionAddress - base : 0;
	const bool onBoot = bootThread && threadId == bootThread;
	// line 1: the one the server quotes (error-report-exe-crash.md section 4)
	o.s("exception ");
	o.hex(code, 8);
	if (name[0])
	{
		o.ch(' ');
		o.s(name);
	}
	o.s(" at ");
	o.s(where);
	if (base && pref)
	{
		o.s(" (VA ");
		o.hex(pref + rva, 8);
		o.ch(')');
	}
	o.s(", thread ");
	o.dec(threadId);
	o.s(onBoot ? " (the proxy's boot thread)" : "");
	o.nl();
	o.s("code: ");
	o.hex(code, 8);
	o.ch(' ');
	o.s(name[0] ? name : ((code & 0xC0000000u) == 0xC0000000u ? "NTSTATUS error" : "(no name)"));
	o.nl();
	o.s("where: ");
	o.s(where);
	o.nl();
	if (base)
	{
		o.s("module base: ");
		o.hex(base, 8);
		if (base == g_exeBase)
		{
			o.s(pref ? " (the EXE; its file's ImageBase " : " (the EXE; its file's ImageBase could not be read)");
			if (pref)
			{
				o.hex(pref, 8);
				o.ch(')');
			}
		}
		o.nl();
		o.s("rva: ");
		o.hex(rva, 8);
		o.nl();
		if (pref)
		{
			o.s("va at the EXE's ImageBase: ");
			o.hex(pref + rva, 8);
			o.s(" (the knowledge base's address form)");
			o.nl();
		}
	}
	else
	{
		o.s("address: ");
		o.hex((ULONG_PTR)er->ExceptionAddress, 8);
		o.nl();
	}
	if ((code == 0xC0000005u || code == 0xC0000006u) && er->NumberParameters >= 2)
	{
		const ULONG_PTR kind = er->ExceptionInformation[0];
		o.s("access: ");
		o.s(kind == 0 ? "read of " : kind == 1 ? "write to " : kind == 8 ? "execution at " : "access to ");
		o.hex(er->ExceptionInformation[1], 8);
		if (code == 0xC0000006u && er->NumberParameters >= 3)
		{
			o.s(" (NTSTATUS ");
			o.hex(er->ExceptionInformation[2], 8);
			o.ch(')');
		}
		o.nl();
	}
	o.s("thread: ");
	o.dec(threadId);
	o.s(onBoot ? " (the proxy's boot thread)" : bootThread ? " (not the proxy's boot thread " : "");
	if (!onBoot && bootThread)
	{
		o.dec(bootThread);
		o.ch(')');
	}
	o.nl();
	o.s("process: ");
	o.dec(GetCurrentProcessId());
	o.nl();
	o.s("time: ");
	TimeText(o);
	o.s(" (local)");
	o.nl();
	o.s("flags: ");
	o.hex(er->ExceptionFlags, 1);
	o.s((er->ExceptionFlags & EXCEPTION_NONCONTINUABLE) ? " (noncontinuable)" : "");
	if (er->ExceptionRecord) o.s("; a nested exception record follows (not read)");
	o.nl();
	o.s("written by: the proxy's chained unhandled-exception filter (ticket 0419); no code bytes, no stack and no memory "
	    "of the process (D83)");
	o.nl();
	return o.n;
}

LONG WINAPI Filter(EXCEPTION_POINTERS* ep)
{
	const DWORD tid = GetCurrentThreadId();
	if (InterlockedCompareExchange(&g_once, 1, 0) == 0)
	{
		g_recordingThread = tid;
		Record(ep);
		InterlockedExchange(&g_once, 2);
	}
	else if (g_recordingThread != tid)
	{
		// Another thread is writing the record. The previous filter may end the process at once (the EXE's lets it
		// end), which would cut that record short: wait for it, at most kWaitMs (found by the test case "two",
		// 2026-11-16: the second thread's chain ended the process before the first thread's log line).
		for (DWORD waited = 0; g_once == 1 && waited < kWaitMs; waited += 10) Sleep(10);
	}
	if (g_prev && g_prev != &Filter) return g_prev(ep);
	return EXCEPTION_CONTINUE_SEARCH;
}

bool Recorded()
{
	return g_once != 0;
}

bool Install(const char* recordPath, HANDLE log, DWORD bootThread, double (*now)(), char* line, size_t lineCap)
{
	Out o(line, lineCap);
	if (g_installed)
	{
		o.s("exe-crash filter: already installed; nothing changed");
		return false;
	}
	size_t n = recordPath ? Len(recordPath) : 0;
	if (n >= sizeof(g_recordPath))
	{
		o.s("exe-crash filter: NOT installed: the record path is too long (");
		o.dec(n);
		o.s(" characters)");
		return false;
	}
	for (size_t k = 0; k < n; ++k) g_recordPath[k] = recordPath[k];
	g_recordPath[n] = 0;
	g_log = log;
	g_bootThread = bootThread;
	g_now = now;
	ResolveNames();
	ResolveExe();
	g_prev = SetUnhandledExceptionFilter(&Filter);
	g_installed = true;
	char prev[MAX_PATH + 64];
	if (g_prev)
		DescribeAddress((const void*)g_prev, prev, sizeof(prev), NULL, NULL);
	o.s("exe-crash filter: installed (ticket 0419); record ");
	o.s(g_recordPath[0] ? g_recordPath : "none (no run folder: the log line only)");
	o.s("; it hands on to the previous filter ");
	o.s(g_prev ? prev : "(none: the system's default)");
	if (!g_mappedName) o.s("; LOUD: no K32GetMappedFileNameA: module names will be missing");
	return true;
}

bool Uninstall(char* line, size_t lineCap)
{
	Out o(line, lineCap);
	if (!g_installed) return false;
	g_log = INVALID_HANDLE_VALUE;  // the proxy closes its log after this
	g_installed = false;
	LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(g_prev);
	if (cur == &Filter)
	{
		o.s("exe-crash filter: removed; the previous filter is the process's filter again");
		return true;
	}
	SetUnhandledExceptionFilter(cur);  // not ours on top: leave the later filter where it is
	char later[MAX_PATH + 64];
	DescribeAddress((const void*)cur, later, sizeof(later), NULL, NULL);
	o.s("LOUD: exe-crash filter: a later filter (");
	o.s(cur ? later : "none");
	o.s(") replaced it; left in place; if that filter chains to this one, a crash after the unload calls into the "
	    "unloaded image");
	return false;
}
}  // namespace ProxyCrashFilter
