// srv_dll_fatal.cpp - D118 for the server's DLL stopping on a failed check (ticket 0422): see srv_dll_fatal.h.
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_dll_fatal.h"
#include "srv_exe_crash.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

void SrvLog(const char* szFormat, ...);
const char* SrvRunDir();

namespace SrvDllFatal
{
namespace
{
volatile uintptr_t s_base = 0, s_end = 0;
wchar_t s_wszModule[MAX_PATH] = L"";
PVOID s_pHandler = NULL;
volatile LONG s_lFired = 0, s_lDone = 0;
TextFn s_pfnServed = NULL, s_pfnLua = NULL;
FailFn s_pfnFail = NULL;

std::string Fmt(const char* fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

std::string U8(const wchar_t* w)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	std::string s(n > 1 ? n - 1 : 0, '\0');
	if(n > 1)
		WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
	return s;
}

// Reads memory of this process without faulting (ReadProcessMemory fails on an unreadable page).
bool SafeRead(uintptr_t at, void* out, size_t n)
{
	SIZE_T got = 0;
	return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)at, out, n, &got) && got == n;
}

// A NUL-terminated printable string of at most `cap` characters at `at`; false when unreadable, not terminated within
// cap, or holding a control character other than tab.
bool ReadText(uintptr_t at, size_t cap, std::string* out)
{
	out->clear();
	if(at < 0x10000)
		return false;
	char buf[64];
	while(out->size() < cap)
	{
		// read up to the end of the page only, so a string just before an unreadable page still reads
		const size_t toPage = 4096 - (size_t)((at + out->size()) & 4095);
		const size_t want = toPage < sizeof(buf) ? toPage : sizeof(buf);
		if(!SafeRead(at + out->size(), buf, want))
			return false;
		for(size_t i = 0; i < want; ++i)
		{
			const unsigned char ch = (unsigned char)buf[i];
			if(ch == 0)
				return true;
			if((ch < 0x20 && ch != '\t') || ch == 0x7F)
				return false;
			*out += (char)ch;
			if(out->size() >= cap)
				return false;
		}
	}
	return false;
}

uint64_t Reg(const CONTEXT* c, int i)
{
	switch(i)
	{
	case 0: return c->Rax;
	case 1: return c->Rcx;
	case 2: return c->Rdx;
	case 3: return c->Rbx;
	case 4: return c->Rsp;
	case 5: return c->Rbp;
	case 6: return c->Rsi;
	case 7: return c->Rdi;
	case 8: return c->R8;
	case 9: return c->R9;
	case 10: return c->R10;
	case 11: return c->R11;
	case 12: return c->R12;
	case 13: return c->R13;
	case 14: return c->R14;
	default: return c->R15;
	}
}
const char* RegName(int i)
{
	static const char* const names[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
	                                      "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
	return i >= 0 && i < 16 ? names[i] : "?";
}

bool SourceFile(const std::string& f)
{
	static const char* const exts[] = {".cpp", ".h", ".hpp", ".inl", ".c", ".cc", ".cxx"};
	for(size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i)
	{
		const size_t n = strlen(exts[i]);
		if(f.size() > n && _stricmp(f.c_str() + f.size() - n, exts[i]) == 0)
			return true;
	}
	return false;
}

// The arguments' source registers of the call that ends just before `trap`: walks back over register-to-register moves
// (REX 89/8B with ModRM mod 11, or the two-byte form without REX) and keeps, for rcx, rdx, r8 and r9, the last move into
// it before the call. src[k] = register index, -1 when no move into it was found.
bool DecodeArgMoves(uintptr_t trap, uintptr_t base, int src[4], std::string* why)
{
	for(int k = 0; k < 4; ++k)
		src[k] = -1;
	const int kWin = 48;
	if(trap < base + kWin)
	{
		*why = "the trap is too near the start of the image";
		return false;
	}
	unsigned char w[kWin];
	if(!SafeRead(trap - kWin, w, kWin))
	{
		*why = "the bytes before the trap cannot be read";
		return false;
	}
	int p = kWin - 5;  // the call rel32 just before the ud2
	if(w[p] != 0xE8)
	{
		*why = "no call (E8 rel32) just before the ud2";
		return false;
	}
	for(int step = 0; step < 8 && p >= 2; ++step)
	{
		unsigned char rex = 0, op, m;
		int len;
		if(p >= 3 && (w[p - 3] & 0xF0) == 0x40 && (w[p - 2] == 0x89 || w[p - 2] == 0x8B) && (w[p - 1] & 0xC0) == 0xC0)
		{
			rex = w[p - 3];
			op = w[p - 2];
			m = w[p - 1];
			len = 3;
		}
		else if((w[p - 2] == 0x89 || w[p - 2] == 0x8B) && (w[p - 1] & 0xC0) == 0xC0)
		{
			op = w[p - 2];
			m = w[p - 1];
			len = 2;
		}
		else
			break;
		const int reg = ((m >> 3) & 7) | ((rex & 4) ? 8 : 0);
		const int rm = (m & 7) | ((rex & 1) ? 8 : 0);
		const int dst = op == 0x89 ? rm : reg;
		const int from = op == 0x89 ? reg : rm;
		const int slot = dst == 1 ? 0 : dst == 2 ? 1 : dst == 8 ? 2 : dst == 9 ? 3 : -1;
		if(slot >= 0 && src[slot] < 0)
			src[slot] = from;
		p -= len;
	}
	if(src[0] < 0 || src[1] < 0 || src[2] < 0)
	{
		*why = "the moves into rcx, rdx and r8 before the call were not all found";
		return false;
	}
	return true;
}

std::string ShortFile(const std::string& f)
{
	const size_t cut = f.find_last_of("\\/");
	return cut == std::string::npos ? f : f.substr(cut + 1);
}

// The report, the marker for the proxy, server-report.txt. On the faulting thread, before the DLL's own filter.
void OnStop(EXCEPTION_POINTERS* p)
{
	const uintptr_t trap = (uintptr_t)p->ExceptionRecord->ExceptionAddress;
	const uintptr_t base = s_base, end = s_end;
	const Check c = ReadCheck(p->ContextRecord, trap, base, end);
	const std::string module = U8(s_wszModule);
	const std::string where = Fmt("%s+0x%llX (module RVA; the DLL's crashes.log \"Location (in file)\" is this minus 0xC00)",
	                              module.c_str(), (unsigned long long)(trap - base));
	const std::string lua = s_pfnLua ? s_pfnLua() : std::string();
	const std::string served = s_pfnServed ? s_pfnServed() : std::string();
	const std::string checkText =
	    c.decoded ? Fmt("%s (%s:%u)", c.expr.c_str(), ShortFile(c.file).c_str(), c.line) : std::string("(text not read)");
	SrvLog("%s", SrvExeCrash::ScrubText(Fmt("remote: LOUD: dll-fatal: the DLL stopped on a failed check (exception "
	                                        "0xC000001D at a ud2, %s): %s; Lua call: %s; thread %lu (ticket 0422)",
	                                        where.c_str(), checkText.c_str(), lua.empty() ? "none" : lua.c_str(),
	                                        GetCurrentThreadId())).c_str());

	SrvExeCrash::ServerFailure f;
	f.stage = "dll-fatal";
	f.title = "the DLL stopped on a failed check, ticket 0422";
	f.code = 0xC000001Du;
	f.codeName = "EXCEPTION_ILLEGAL_INSTRUCTION (a ud2: the DLL's deliberate stop after a failed check)";
	f.summary = c.decoded ? Fmt("The game stopped on an internal check in the game's DLL (dll-fatal: %s at %s:%u); the game "
	                            "had to end. The 64-bit server wrote this report.",
	                            c.expr.c_str(), ShortFile(c.file).c_str(), c.line)
	                      : std::string("The game stopped on an internal check in the game's DLL (dll-fatal: the check's "
	                                    "text could not be read); the game had to end. The 64-bit server wrote this report.");
	f.intro = "The game's DLL (the Vox Populi game rules, which the 64-bit patch runs in its 64-bit server) stopped on a\n"
	          "failed internal check, so the game had to end. The normal (32-bit) game stops on the same check with the\n"
	          "DLL's own error box. Please send this folder in (the popup says where).\n";
	std::string fl;
	if(c.decoded)
	{
		fl += "check: " + c.expr + "\n";
		fl += "file: " + c.file + "\n";
		fl += Fmt("line: %u\n", c.line);
		fl += "message: " + (c.message.empty() ? std::string("(none)") : c.message) + "\n";
	}
	else
		fl += "check: the text could not be read (" + c.how + ")\n";
	fl += "how the check was read: " + c.how + "\n";
	fl += "where: " + where + "\n";
	fl += "the Lua call that led there: " + (lua.empty() ? std::string("none on this thread") : lua) + "\n";
	fl += "the engine calls being served on this thread (outermost first): " +
	      (served.empty() ? std::string("none") : served) + "\n";
	fl += Fmt("thread: %lu\n", GetCurrentThreadId());
	f.failure = fl;
	bool on = false;
	const std::string folder = SrvExeCrash::ReportServerFailure(f, &on);

	// The proxy (pack=1) writes its own server-death report and starts a second popup when it sees the server gone;
	// this file tells it the server already did (proxy_rt.cpp CallFailed). Only when the report was on.
	const char* run = SrvRunDir();
	if(on && run && run[0])
	{
		const std::string path = std::string(run) + "\\dll-fatal.txt";
		FILE* fp = NULL;
		if(!fopen_s(&fp, path.c_str(), "wb") && fp)
		{
			fprintf(fp, "dll-fatal: the server wrote the D119 report for this stop (ticket 0422)\r\n");
			fprintf(fp, "report: %s\r\n", folder.empty() ? "(none: the folder could not be made)" : folder.c_str());
			fprintf(fp, "check: %s\r\n", checkText.c_str());
			fclose(fp);
			SrvLog("dll-fatal: %s written (the proxy writes no second report)", path.c_str());
		}
		else
			SrvLog("dll-fatal: LOUD: %s could not be written (the proxy may write a second report)", path.c_str());
	}
	if(s_pfnFail)
		s_pfnFail(Fmt("the DLL stopped on a failed check: %s at %s", checkText.c_str(), where.c_str()).c_str());
	SrvLog("dll-fatal: handing the exception on to the DLL's own filter (crashes.log, minidump; the process ends with "
	       "0xC000001D)");
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* p)
{
	if(!p || !p->ExceptionRecord || p->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION)
		return EXCEPTION_CONTINUE_SEARCH;
	const uintptr_t at = (uintptr_t)p->ExceptionRecord->ExceptionAddress;
	if(at < s_base || at + 2 > s_end)
		return EXCEPTION_CONTINUE_SEARCH;
	unsigned char b[2];
	if(!SafeRead(at, b, 2) || b[0] != 0x0F || b[1] != 0x0B)
		return EXCEPTION_CONTINUE_SEARCH;
	if(InterlockedExchange(&s_lFired, 1))
	{
		// Once: a second stop on another thread writes nothing, but it waits (at most 30 s) until the first report is
		// done, because the DLL's filter it goes on to would end the process in the middle of that report.
		for(int i = 0; i < 3000 && s_lDone == 0; ++i)
			Sleep(10);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	try
	{
		OnStop(p);
	}
	catch(...)
	{
		SrvLog("dll-fatal: LOUD: an exception while writing the report (ticket 0422)");
	}
	SrvExeCrash::LogSync(2000);   // ticket 5054: the DLL's filter ends the process; the queued log lines are written first
	InterlockedExchange(&s_lDone, 1);
	return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

Check ReadCheck(const CONTEXT* ctx, uintptr_t trap, uintptr_t base, uintptr_t end)
{
	Check c;
	int src[4];
	std::string why;
	if(!DecodeArgMoves(trap, base, src, &why))
	{
		c.how = why;
		return c;
	}
	const uintptr_t pe = (uintptr_t)Reg(ctx, src[0]), pf = (uintptr_t)Reg(ctx, src[1]);
	const uint64_t line = Reg(ctx, src[2]) & 0xFFFFFFFFull;
	std::string regs = Fmt("rcx<-%s, rdx<-%s, r8<-%s", RegName(src[0]), RegName(src[1]), RegName(src[2]));
	if(src[3] >= 0)
		regs += Fmt(", r9<-%s", RegName(src[3]));
	if(pe < base || pe >= end || pf < base || pf >= end)
	{
		c.how = "the expression or file register does not point into the DLL's image (" + regs + ")";
		return c;
	}
	if(!ReadText(pe, 1024, &c.expr) || c.expr.empty())
	{
		c.how = "the expression is not a readable text (" + regs + ")";
		return c;
	}
	if(!ReadText(pf, 520, &c.file) || !SourceFile(c.file))
	{
		c.how = "the file is not a readable source file name (" + regs + ")";
		c.expr.clear();
		c.file.clear();
		return c;
	}
	if(line < 1 || line > 1000000)
	{
		c.how = Fmt("the line %llu is out of range (%s)", (unsigned long long)line, regs.c_str());
		c.expr.clear();
		c.file.clear();
		return c;
	}
	c.line = (uint32_t)line;
	if(src[3] >= 0 && !ReadText((uintptr_t)Reg(ctx, src[3]), 1024, &c.message))
		c.message.clear();
	c.decoded = true;
	c.how = "from the register moves before the call that precedes the ud2 (" + regs + ")";
	return c;
}

std::string Install(HMODULE hModule)
{
	MEMORY_BASIC_INFORMATION mbi;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)hModule;
	if(!hModule || !VirtualQuery(hModule, &mbi, sizeof(mbi)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
		return "dll-fatal: LOUD: no module: the vectored handler is not installed (ticket 0422)";
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)hModule + dos->e_lfanew);
	const uintptr_t base = (uintptr_t)hModule;
	GetModuleFileNameW(hModule, s_wszModule, MAX_PATH);
	const wchar_t* slash = wcsrchr(s_wszModule, L'\\');
	if(slash)
		memmove(s_wszModule, slash + 1, (wcslen(slash + 1) + 1) * sizeof(wchar_t));
	s_end = 0;
	s_base = base;
	s_end = base + nt->OptionalHeader.SizeOfImage;
	if(!s_pHandler)
		s_pHandler = AddVectoredExceptionHandler(1, Handler);
	if(!s_pHandler)
		return Fmt("dll-fatal: LOUD: AddVectoredExceptionHandler failed (error %lu): a failed check of the DLL is not "
		           "reported (ticket 0422)", GetLastError());
	return Fmt("dll-fatal: vectored handler installed for %s (image 0x%llX, %lu bytes): a ud2 there (a failed check) "
	           "writes the D119 report and starts the popup (ticket 0422)",
	           U8(s_wszModule).c_str(), (unsigned long long)base, (unsigned long)nt->OptionalHeader.SizeOfImage);
}

void SetContextText(TextFn pfnServedCalls, TextFn pfnLuaCall)
{
	s_pfnServed = pfnServedCalls;
	s_pfnLua = pfnLuaCall;
}

void SetFailHook(FailFn pfn) { s_pfnFail = pfn; }
}  // namespace SrvDllFatal
