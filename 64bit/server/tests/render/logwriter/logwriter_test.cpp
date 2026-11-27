// logwriter_test.cpp - ticket 5054: the log writer of server/src/render/srv_render_common.cpp, offline.
//
//   logwriter_test.exe DIR            runs every case below in DIR (made empty first by the builder); exit 0 when all pass
//   logwriter_test.exe crash DIR      child: a forced access violation with the writer held (case crash)
//   logwriter_test.exe stuckexit DIR  child: LogShutdown with the writer held (case stuck-exit)
//
// Cases (research/05-graphics-composition/unit-combat-server.md 18.27.1):
//   blocked   the writer held (as on a stalled disk) while a "render thread" draws 200 frames of 50 lines each: every
//             frame's time stays bounded, 4096 lines are kept in order, the rest dropped and counted, ONE drop line
//   bytes     the 4 MiB bound: 3000 lines of 1.4 KB with the writer held: the kept bytes fit, a second episode's line
//   notes     RNote reaches render.log and (through the host's function, as server.exe's SrvLog) server.log
//   exit      lines queued then LogShutdown: all written, the exit line, later lines written synchronously
//   crash     child: writer held, 300 notes queued, an access violation; the filter (as LogException) flushes on the
//             crash path and reads server.log back as the D119 report does: every note and the exception line are there
//   stuck-exit child: writer held at exit: LogShutdown gives up after 500 ms and the crash path writes the queue
#include "srv_render_common.h"
#include <share.h>
#include <stdarg.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace srvrender;

namespace {

LARGE_INTEGER g_freq, g_start;
#if defined(__SANITIZE_ADDRESS__)
const DWORD kCrashCode = 0xE0005054u;   // the ASan build's forced crash (see ChildCrash)
#else
const DWORD kCrashCode = 0xC0000005u;   // an access violation
#endif
FILE* g_server = nullptr;
int g_fail = 0;
std::wstring g_dir;

double NowS()
{
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)(c.QuadPart - g_start.QuadPart) / (double)g_freq.QuadPart;
}

double NowMsAbs()
{
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)g_freq.QuadPart;
}

// as server.exe's SrvLog: "[%9.3f] line" to server.log, queued when the writer runs
void HostLog(const char* line)
{
	char b[4200];
	const int n = _snprintf_s(b, sizeof(b), _TRUNCATE, "[%9.3f] %s\n", NowS(), line);
	if(n > 0 && LogQueueServer(b, (size_t)n, false))
		return;
	if(g_server)
	{
		fputs(b, g_server);
		fflush(g_server);
	}
}

void Check(bool ok, const char* what, ...)
{
	char b[1024];
	va_list ap;
	va_start(ap, what);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, what, ap);
	va_end(ap);
	printf("%s: %s\n", ok ? "PASS" : "FAIL", b);
	fflush(stdout);
	if(!ok)
		g_fail++;
}

std::string ReadAll(const std::wstring& path)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
	                       OPEN_EXISTING, 0, nullptr);
	if(h == INVALID_HANDLE_VALUE)
		return std::string();
	std::string s;
	char b[65536];
	DWORD n = 0;
	while(ReadFile(h, b, sizeof(b), &n, nullptr) && n)
		s.append(b, n);
	CloseHandle(h);
	return s;
}

std::vector<std::string> Lines(const std::string& s)
{
	std::vector<std::string> v;
	size_t p = 0;
	while(p < s.size())
	{
		size_t e = s.find('\n', p);
		if(e == std::string::npos)
			e = s.size();
		v.push_back(s.substr(p, e - p));
		p = e + 1;
	}
	return v;
}

// lines whose text (after the stamp) starts with tag, in order
std::vector<std::string> Tagged(const std::vector<std::string>& lines, const std::string& tag)
{
	std::vector<std::string> v;
	for(const std::string& l : lines)
		if(l.find(tag) != std::string::npos)
			v.push_back(l);
	return v;
}

size_t Count(const std::vector<std::string>& lines, const std::string& needle)
{
	size_t n = 0;
	for(const std::string& l : lines)
		if(l.find(needle) != std::string::npos)
			n++;
	return n;
}

void Open(const std::wstring& dir)
{
	g_dir = dir;
	QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&g_start);
	g_server = _wfsopen((dir + L"\\server.log").c_str(), L"w", _SH_DENYWR);   // as srv_main.cpp's server.log
	LogOpen((dir + L"\\render.log").c_str(), &HostLog);
	LogSetServerSink(g_server, &NowS);
}

// ---- in-process cases ----------------------------------------------------------------------------------------------

void CaseBlocked()
{
	const LogWriterCounters c0 = LogCounters();
	HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	Check(LogTestHoldWriter(hold), "blocked: the writer waits on the test's hold");
	const int frames = 200, perFrame = 50;
	double frameMax = 0, lineMax = 0;
	std::vector<double> frameMs;
	for(int f = 0; f < frames; ++f)
	{
		const double f0 = NowMsAbs();
		for(int i = 0; i < perFrame; ++i)
		{
			const double l0 = NowMsAbs();
			RLog("t1 line %05d of frame %d", f * perFrame + i, f);
			lineMax = std::max(lineMax, NowMsAbs() - l0);
		}
		const double ms = NowMsAbs() - f0;
		frameMs.push_back(ms);
		frameMax = std::max(frameMax, ms);
		Sleep(5);   // a frame's other work: the hold lasts about 1-2 s
	}
	std::sort(frameMs.begin(), frameMs.end());
	const LogWriterCounters c1 = LogCounters();
	const unsigned long long dropped = c1.dropped - c0.dropped;
	Check(dropped == (unsigned long long)(frames * perFrame) - kLogQueueLines, "blocked: %llu lines dropped and counted (expected %zu)",
	      dropped, (size_t)(frames * perFrame) - kLogQueueLines);
	// the render thread did not wait: a frame of 50 lines while the writer was held (a preempted thread can still lose a
	// time slice; the median says the cost)
	Check(frameMax < 50.0 && frameMs[frameMs.size() / 2] < 1.0,
	      "blocked: frame time bounded with the writer held: median %.3f ms, max %.3f ms (50 lines each), one line max %.3f ms",
	      frameMs[frameMs.size() / 2], frameMax, lineMax);
	SetEvent(hold);
	LogTestHoldWriter(nullptr);
	Check(LogSync(5000), "blocked: the queue is written after the hold ends");
	const std::vector<std::string> lines = Lines(ReadAll(g_dir + L"\\render.log"));
	const std::vector<std::string> t1 = Tagged(lines, "t1 line ");
	bool order = t1.size() == kLogQueueLines;
	for(size_t i = 0; order && i < t1.size(); ++i)
	{
		char want[32];
		_snprintf_s(want, sizeof(want), _TRUNCATE, "t1 line %05zu ", i);
		order = t1[i].find(want) != std::string::npos;
	}
	Check(order, "blocked: render.log holds the first %zu lines in order (%zu found)", kLogQueueLines, t1.size());
	char drop[96];
	_snprintf_s(drop, sizeof(drop), _TRUNCATE, "LOUD: log writer: %llu lines dropped (render.log %llu, server.log 0)", dropped, dropped);
	Check(Count(lines, "lines dropped") == 1 && Count(lines, drop) == 1, "blocked: exactly one drop line in render.log: '%s'", drop);
	Check(Count(Lines(ReadAll(g_dir + L"\\server.log")), drop) == 1, "blocked: the drop line is in server.log too");
	CloseHandle(hold);
}

void CaseBytes()
{
	const LogWriterCounters c0 = LogCounters();
	HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	Check(LogTestHoldWriter(hold), "bytes: the writer waits on the test's hold");
	std::string pad(1400, 'x');
	for(int i = 0; i < 3000; ++i)
		RLog("t2 line %05d %s", i, pad.c_str());
	const LogWriterCounters c1 = LogCounters();
	SetEvent(hold);
	LogTestHoldWriter(nullptr);
	Check(LogSync(5000), "bytes: the queue is written after the hold ends");
	const unsigned long long kept = c1.queued - c0.queued, dropped = c1.dropped - c0.dropped;
	Check(kept + dropped == 3000 && dropped > 0 && kept < 3000 && c1.peakBytes <= kLogQueueBytes,
	      "bytes: %llu kept, %llu dropped by the 4 MiB bound (peak %zu bytes)", kept, dropped, c1.peakBytes);
	const std::vector<std::string> lines = Lines(ReadAll(g_dir + L"\\render.log"));
	Check(Tagged(lines, "t2 line ").size() == kept, "bytes: render.log holds the %llu kept lines", kept);
	Check(Count(lines, "lines dropped") == 2 && c1.episodes - c0.episodes == 1, "bytes: a second episode, a second drop line");
	CloseHandle(hold);
}

void CaseNotes()
{
	for(int i = 0; i < 100; ++i)
		RNote("t3 note %03d", i);
	Check(LogSync(5000), "notes: synced");
	const std::vector<std::string> r = Tagged(Lines(ReadAll(g_dir + L"\\render.log")), "t3 note ");
	const std::vector<std::string> s = Tagged(Lines(ReadAll(g_dir + L"\\server.log")), "] render: t3 note ");
	Check(r.size() == 100 && s.size() == 100 && r.back().find("t3 note 099") != std::string::npos &&
	          s.back().find("t3 note 099") != std::string::npos,
	      "notes: 100 notes in render.log (%zu) and server.log (%zu), in order", r.size(), s.size());
}

void CaseExit()
{
	for(int i = 0; i < 2000; ++i)
		RNote("t4 exit %04d", i);
	const double t0 = NowMsAbs();
	const bool ok = LogShutdown(5000);
	const double ms = NowMsAbs() - t0;
	const LogWriterCounters c = LogCounters();
	Check(ok && c.mode == 3, "exit: LogShutdown finished in %.1f ms, mode %d (3 = stopped)", ms, c.mode);
	std::vector<std::string> r = Lines(ReadAll(g_dir + L"\\render.log"));
	std::vector<std::string> s = Lines(ReadAll(g_dir + L"\\server.log"));
	Check(Tagged(r, "t4 exit ").size() == 2000 && Tagged(s, "t4 exit ").size() == 2000,
	      "exit: every queued line written before the stop (render.log %zu, server.log %zu of 2000)", Tagged(r, "t4 exit ").size(),
	      Tagged(s, "t4 exit ").size());
	Check(Count(r, "log writer: stopping at exit") == 1 && Count(s, "log writer: stopping at exit") == 1, "exit: the exit line in both logs");
	Check(!LogQueue(kLogToRender, "x\n", 2), "exit: no writer after the stop (LogQueue false)");
	RNote("t4 after the stop");
	r = Lines(ReadAll(g_dir + L"\\render.log"));
	s = Lines(ReadAll(g_dir + L"\\server.log"));
	Check(Count(r, "t4 after the stop") == 1 && Count(s, "t4 after the stop") == 1, "exit: a line after the stop is written at once");
}

// ---- children --------------------------------------------------------------------------------------------------------

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* p)
{
	// as srv_main.cpp's LogException: the crash path first, then the exception's line
	LogFlushForCrash("test crash");
	char b[200];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "EXCEPTION 0x%08lX in the test's crash child, thread %lu",
	            p->ExceptionRecord->ExceptionCode, GetCurrentThreadId());
	HostLog(b);
	// as the D119 report: server.log read back from disk; its last lines go to report.txt
	const std::vector<std::string> s = Lines(ReadAll(g_dir + L"\\server.log"));
	FILE* f = _wfopen((g_dir + L"\\report.txt").c_str(), L"wb");
	if(f)
	{
		const size_t from = s.size() > 3 ? s.size() - 3 : 0;
		for(size_t i = from; i < s.size(); ++i)
			fprintf(f, "%s\n", s[i].c_str());
		fclose(f);
	}
	return EXCEPTION_EXECUTE_HANDLER;   // the process ends with the exception code
}

int ChildCrash()
{
	if(!LogWriterStart())
		return 10;
	HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if(!LogTestHoldWriter(hold))
		return 11;
	for(int i = 0; i < 300; ++i)
		RNote("crash note %03d", i);
	SetUnhandledExceptionFilter(&CrashFilter);
#if defined(__SANITIZE_ADDRESS__)
	// ASan's own handler takes an access violation (its SEGV report, exit 1): the ASan build raises an exception of its own
	RaiseException(kCrashCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
	*(volatile int*)(uintptr_t)8 = 1;   // the forced crash (as render_test_av)
#endif
	return 12;
}

int ChildStuckExit()
{
	if(!LogWriterStart())
		return 10;
	HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if(!LogTestHoldWriter(hold))
		return 11;
	for(int i = 0; i < 50; ++i)
		RNote("stuck note %02d", i);
	const bool ok = LogShutdown(500);
	RNote("stuck after the shutdown");
	TerminateProcess(GetCurrentProcess(), ok ? 21 : 20);   // as server.exe: no static destructors
	return 22;
}

DWORD RunChild(const wchar_t* mode, const std::wstring& dir)
{
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring cmd = std::wstring(L"\"") + exe + L"\" " + mode + L" \"" + dir + L"\"";
	STARTUPINFOW si = {sizeof(si)};
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> c(cmd.begin(), cmd.end());
	c.push_back(0);
	if(!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		return 0xFFFFFFFFu;
	DWORD code = 0xFFFFFFFEu;
	if(WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0)
		GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return code;
}

void CaseCrash(const std::wstring& dir)
{
	const std::wstring d = dir + L"\\crash";
	CreateDirectoryW(d.c_str(), nullptr);
	const DWORD code = RunChild(L"crash", d);
	Check(code == kCrashCode, "crash: the child ended with the forced crash's code (0x%08lX, 0x%08lX expected)", code, kCrashCode);
	const std::vector<std::string> s = Lines(ReadAll(d + L"\\server.log"));
	const std::vector<std::string> r = Lines(ReadAll(d + L"\\render.log"));
	const std::vector<std::string> sn = Tagged(s, "] render: crash note ");
	const std::vector<std::string> rn = Tagged(r, " crash note ");
	Check(sn.size() == 300 && rn.size() == 300 && sn.back().find("crash note 299") != std::string::npos,
	      "crash: the 300 queued notes written by the crash path (server.log %zu, render.log %zu)", sn.size(), rn.size());
	char exc[32];
	_snprintf_s(exc, sizeof(exc), _TRUNCATE, "EXCEPTION 0x%08lX", kCrashCode);
	Check(Count(s, "LOUD: log writer: the crash path (test crash): ") == 1 && Count(s, exc) == 1,
	      "crash: server.log has the crash path's line and the exception line");
	const size_t iNote = sn.empty() ? 0 : (size_t)(std::find(s.begin(), s.end(), sn.back()) - s.begin());
	const std::string excs = exc;
	const size_t iExc = (size_t)(std::find_if(s.begin(), s.end(), [&excs](const std::string& l) {
		                             return l.find(excs) != std::string::npos;
	                             }) - s.begin());
	Check(!sn.empty() && iNote < iExc, "crash: the queued notes come before the exception line");
	const std::string rep = ReadAll(d + L"\\report.txt");
	Check(rep.find("crash note 299") != std::string::npos && rep.find(excs) != std::string::npos,
	      "crash: the report's read-back of server.log ends with the last note and the exception line");
}

void CaseStuckExit(const std::wstring& dir)
{
	const std::wstring d = dir + L"\\stuck";
	CreateDirectoryW(d.c_str(), nullptr);
	const DWORD code = RunChild(L"stuckexit", d);
	Check(code == 20, "stuck-exit: LogShutdown(500) gave up on the held writer (child exit %lu, 20 expected)", code);
	const std::vector<std::string> s = Lines(ReadAll(d + L"\\server.log"));
	Check(Tagged(s, "] render: stuck note ").size() == 50 && Count(s, "the log writer did not finish within 500 ms at exit") == 1 &&
	          Count(s, "stuck after the shutdown") == 1,
	      "stuck-exit: the 50 queued notes, the crash path's line and a later line are in server.log (%zu notes)",
	      Tagged(s, "] render: stuck note ").size());
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
	if(argc == 3 && !wcscmp(argv[1], L"crash"))
	{
		Open(argv[2]);
		return ChildCrash();
	}
	if(argc == 3 && !wcscmp(argv[1], L"stuckexit"))
	{
		Open(argv[2]);
		return ChildStuckExit();
	}
	if(argc != 2)
	{
		fprintf(stderr, "usage: logwriter_test.exe DIR | crash DIR | stuckexit DIR\n");
		return 2;
	}
	const std::wstring dir = argv[1];
	Open(dir);
	Check(LogWriterStart() && LogCounters().mode == 1, "start: the writer runs (mode 1)");
	CaseBlocked();
	CaseBytes();
	CaseNotes();
	CaseExit();
	CaseCrash(dir);
	CaseStuckExit(dir);
	printf("logwriter_test: %s (%d failed)\n", g_fail ? "FAILED" : "OK", g_fail);
	return g_fail ? 1 : 0;
}
