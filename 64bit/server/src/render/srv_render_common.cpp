// srv_render_common.cpp - see srv_render_common.h (issue 3301; the log writer: ticket 5054).
#include "srv_render_common.h"
#include <share.h>
#include <stdarg.h>
#include <io.h>
#include <vector>

namespace srvrender {

namespace {
SRWLOCK s_logLock = SRWLOCK_INIT;   // render.log's FILE*: opening, closing, and every write of it
FILE* s_log = nullptr;
HANDLE s_hRender = INVALID_HANDLE_VALUE;   // ticket 5054: render.log's handle for the crash path
HostLogFn volatile s_fn = nullptr;

// issue 0417: a log line whose write takes this long (the lock, render.log, the host's server.log and stdout) is followed
// by a line naming the parts (the first 32 of the process): the bakes' log lines are written on the render thread
LONG s_slowWrites = 0;
constexpr double kSlowWriteMs = 50.0;

double NowMs()
{
	static LARGE_INTEGER f = {};
	if(!f.QuadPart)
		QueryPerformanceFrequency(&f);
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

// ---- ticket 5054: the log writer ------------------------------------------------------------------------------------
struct QLine
{
	unsigned targets;
	std::string text;   // with its '\n'
};
enum : LONG
{
	kModeSync = 0,
	kModeWriter = 1,
	kModeCrash = 2,
	kModeStopped = 3,
};
volatile LONG s_mode = kModeSync;
SRWLOCK s_qLock = SRWLOCK_INIT;   // the queue and its counters; never held across a write
CONDITION_VARIABLE s_qWritten = CONDITION_VARIABLE_INIT;
std::vector<QLine>* s_q = nullptr;   // never freed (static destructors run after the writer may still hold it)
size_t s_qBytes = 0;
unsigned long long s_seqQueued = 0, s_seqDone = 0;   // lines taken into the queue / written or given up by the writer
bool s_stop = false;
// the episode of drops that the writer has not said yet
unsigned long long s_epDropR = 0, s_epDropS = 0, s_epDrop = 0;
double s_epFirstMs = 0;
LogWriterCounters s_c;
HANDLE s_wake = nullptr, s_thread = nullptr;
HANDLE volatile s_testHold = nullptr;
HANDLE s_testParked = nullptr;
// server.log (server.exe)
FILE* s_server = nullptr;
HANDLE s_hServer = INVALID_HANDLE_VALUE;
double (*s_nowS)() = nullptr;
// the crash path's writes
SRWLOCK s_crashLock = SRWLOCK_INIT;
constexpr int kSlowWriteLinesMax = 64;

HANDLE OsHandle(FILE* f)
{
	if(!f)
		return INVALID_HANDLE_VALUE;
	const int fd = _fileno(f);
	if(fd < 0)
		return INVALID_HANDLE_VALUE;
	return (HANDLE)_get_osfhandle(fd);
}

void StampRender(char* out, size_t n)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	_snprintf_s(out, n, _TRUNCATE, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

// one line of the writer's own (or of the crash path) to render.log and server.log, each with its own stamp
void OwnLine(const char* body, std::string& toRender, std::string& toServer)
{
	char st[32];
	StampRender(st, sizeof(st));
	toRender = std::string(st) + body + "\n";
	char ss[48];
	_snprintf_s(ss, sizeof(ss), _TRUNCATE, "[%9.3f] ", s_nowS ? s_nowS() : 0.0);
	toServer = std::string(ss) + body + "\n";
}

// WriteFile at the end of the file (the crash path; no CRT lock)
void AppendHandle(HANDLE h, const char* p, size_t n, bool file)
{
	if(h == INVALID_HANDLE_VALUE || !h || !n)
		return;
	DWORD w = 0;
	if(file)
	{
		OVERLAPPED ov = {};
		ov.Offset = 0xFFFFFFFFu;   // with OffsetHigh: write at the end of the file
		ov.OffsetHigh = 0xFFFFFFFFu;
		WriteFile(h, p, (DWORD)n, &w, &ov);
	}
	else
		WriteFile(h, p, (DWORD)n, &w, nullptr);
}

void CrashWrite(unsigned targets, const char* p, size_t n)
{
	AcquireSRWLockExclusive(&s_crashLock);
	if(targets & kLogToRender)
		AppendHandle(s_hRender, p, n, true);
	if(targets & kLogToServer)
		AppendHandle(s_hServer, p, n, true);
	if(targets & kLogToStdout)
		AppendHandle(GetStdHandle(STD_OUTPUT_HANDLE), p, n, false);
	s_c.crashLines++;
	ReleaseSRWLockExclusive(&s_crashLock);
}

// the writer's write of one line to its files (render.log under s_logLock: LogOpen/LogClose)
void WriteLine(const QLine& l, bool& render, bool& server, bool& out)
{
	if(l.targets & kLogToRender)
	{
		if(s_log)
		{
			fwrite(l.text.data(), 1, l.text.size(), s_log);
			render = true;
		}
	}
	if((l.targets & kLogToServer) && s_server)
	{
		fwrite(l.text.data(), 1, l.text.size(), s_server);
		server = true;
	}
	if(l.targets & kLogToStdout)
	{
		fwrite(l.text.data(), 1, l.text.size(), stdout);
		out = true;
	}
}

DWORD WINAPI WriterThread(void*)
{
	std::vector<QLine> batch;
	double lastWriteMs = 0;
	for(;;)
	{
		WaitForSingleObject(s_wake, 100);
		if(HANDLE hold = s_testHold)
		{
			SetEvent(s_testParked);
			WaitForSingleObject(hold, INFINITE);
		}
		bool stop = false;
		unsigned long long dR = 0, dS = 0, d = 0;
		double firstMs = 0;
		size_t peakLines = 0;
		AcquireSRWLockExclusive(&s_qLock);
		if(s_mode != kModeWriter && s_mode != kModeStopped)
		{
			ReleaseSRWLockExclusive(&s_qLock);
			return 0;   // the crash path took over
		}
		batch.clear();
		batch.swap(*s_q);
		s_qBytes = 0;
		dR = s_epDropR;
		dS = s_epDropS;
		d = s_epDrop;
		firstMs = s_epFirstMs;
		s_epDropR = s_epDropS = s_epDrop = 0;
		peakLines = s_c.peakLines;
		stop = s_stop;
		ReleaseSRWLockExclusive(&s_qLock);

		if(!batch.empty() || d)
		{
			const double t0 = NowMs();
			bool r = false, s = false, o = false;
			AcquireSRWLockExclusive(&s_logLock);
			for(const QLine& l : batch)
				WriteLine(l, r, s, o);
			if(d)
			{
				char b[600];
				_snprintf_s(b, sizeof(b), _TRUNCATE,
				            "LOUD: log writer: %llu lines dropped (render.log %llu, server.log %llu) over %.0f ms while the writer was "
				            "held up (its last write took %.1f ms); queue peak %zu lines; no thread waited for the log, the render "
				            "thread included (ticket 5054)",
				            d, dR, dS, NowMs() - firstMs, lastWriteMs, peakLines);
				std::string tr, ts;
				OwnLine(b, tr, ts);
				WriteLine(QLine{kLogToRender, tr}, r, s, o);
				WriteLine(QLine{kLogToServer, ts}, r, s, o);
			}
			if(r && s_log)
				fflush(s_log);
			ReleaseSRWLockExclusive(&s_logLock);
			if(s)
				fflush(s_server);
			if(o)
				fflush(stdout);
			const double t1 = NowMs();
			lastWriteMs = t1 - t0;
			unsigned long long slowN = 0;
			AcquireSRWLockExclusive(&s_qLock);
			s_seqDone += batch.size();
			s_c.written += batch.size();
			if(lastWriteMs > s_c.slowestWriteMs)
				s_c.slowestWriteMs = lastWriteMs;
			if(lastWriteMs >= kSlowWriteMs)
				slowN = ++s_c.slowWrites;
			ReleaseSRWLockExclusive(&s_qLock);
			WakeAllConditionVariable(&s_qWritten);
			if(slowN && slowN <= (unsigned long long)kSlowWriteLinesMax + 1)
			{
				char b[400];
				if(slowN <= (unsigned long long)kSlowWriteLinesMax)
					_snprintf_s(b, sizeof(b), _TRUNCATE,
					            "log writer: a write of %zu lines took %.1f ms (the disk's wait: no producer waited, the render thread "
					            "included; slow write %llu; ticket 5054)",
					            batch.size(), lastWriteMs, slowN);
				else
					_snprintf_s(b, sizeof(b), _TRUNCATE, "log writer: more slow writes are not logged (counted: the exit line)");
				std::string tr, ts;
				OwnLine(b, tr, ts);
				bool r2 = false, s2 = false, o2 = false;
				AcquireSRWLockExclusive(&s_logLock);
				WriteLine(QLine{kLogToRender, tr}, r2, s2, o2);
				if(r2 && s_log)
					fflush(s_log);
				ReleaseSRWLockExclusive(&s_logLock);
				WriteLine(QLine{kLogToServer, ts}, r2, s2, o2);
				if(s2)
					fflush(s_server);
			}
		}
		else
			WakeAllConditionVariable(&s_qWritten);
		if(stop)
		{
			AcquireSRWLockExclusive(&s_qLock);
			const bool empty = s_q->empty() && !s_epDrop;
			ReleaseSRWLockExclusive(&s_qLock);
			if(empty)
				return 0;
			SetEvent(s_wake);
		}
	}
}

void EmitBody(bool note, const char* body, bool timed)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	const double t0 = NowMs();
	if(s_mode != kModeSync && s_mode != kModeStopped)
	{
		// ticket 5054: the writer (or the crash path) writes the line; this thread does not wait on a disk
		char line[1600];
		const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond,
		                          st.wMilliseconds, body);
		if(n < 0)
		{
			line[sizeof(line) - 2] = '\n';   // a cut line keeps its end
			line[sizeof(line) - 1] = 0;
		}
		if(LogQueue(kLogToRender, line, strlen(line)))
		{
			HostLogFn fn = s_fn;
			if(note && fn)
			{
				char hl[1600];
				_snprintf_s(hl, sizeof(hl), _TRUNCATE, "render: %s", body);
				fn(hl);
			}
			return;
		}
	}
	AcquireSRWLockExclusive(&s_logLock);
	const double t1 = NowMs();
	if(s_log)
	{
		fprintf(s_log, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
		fflush(s_log);
	}
	HostLogFn fn = s_fn;
	ReleaseSRWLockExclusive(&s_logLock);
	const double t2 = NowMs();
	if(note && fn)
	{
		char line[1600];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "render: %s", body);
		fn(line);
	}
	const double t3 = NowMs();
	if(timed && t3 - t0 >= kSlowWriteMs && InterlockedIncrement(&s_slowWrites) <= 32)
	{
		char b[400];
		_snprintf_s(b, sizeof(b), _TRUNCATE,
		            "render log: the line before took %.1f ms to write (waiting for the log lock %.1f ms, render.log %.1f ms, "
		            "server.log and stdout %.1f ms; slow write %ld)",
		            t3 - t0, t1 - t0, t2 - t1, t3 - t2, (long)s_slowWrites);
		EmitBody(note, b, false);
	}
}

void Emit(bool note, const char* fmt, va_list ap)
{
	char body[1536];
	_vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
	EmitBody(note, body, true);
}
}  // namespace

int64_t Qpc()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

int64_t QpcFreq()
{
	static int64_t s_f = 0;
	if(!s_f)
	{
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		s_f = f.QuadPart;
	}
	return s_f;
}

double QpcToMs(int64_t d) { return (double)d * 1000.0 / (double)QpcFreq(); }

void LogOpen(const wchar_t* path, HostLogFn fn)
{
	AcquireSRWLockExclusive(&s_logLock);
	if(!s_log && path && *path)
	{
		s_log = _wfsopen(path, L"a", _SH_DENYNO);   // readable while the server runs (tests)
		s_hRender = OsHandle(s_log);
	}
	s_fn = fn;
	ReleaseSRWLockExclusive(&s_logLock);
}

void LogClose()
{
	// ticket 5054: what is queued for render.log is written first (at most 5 s); a writer stuck on the disk keeps the file
	LogSync(5000);
	bool got = false;
	for(int i = 0; i < 500 && !(got = TryAcquireSRWLockExclusive(&s_logLock) != 0); ++i)
		Sleep(10);
	if(!got)
		return;
	if(s_log)
		fclose(s_log);
	s_log = nullptr;
	s_hRender = INVALID_HANDLE_VALUE;
	ReleaseSRWLockExclusive(&s_logLock);
}

void RLog(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(false, fmt, ap);
	va_end(ap);
}

void RNote(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(true, fmt, ap);
	va_end(ap);
}

std::string Narrow(const std::wstring& w)
{
	char b[1024] = {};
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, b, (int)sizeof(b) - 1, nullptr, nullptr);
	return b;
}

// ---- ticket 5054: the log writer ------------------------------------------------------------------------------------

void LogSetServerSink(FILE* serverLog, double (*nowSeconds)())
{
	s_server = serverLog;
	s_hServer = OsHandle(serverLog);
	s_nowS = nowSeconds;
}

bool LogWriterStart()
{
	if(s_mode != kModeSync)
		return s_mode == kModeWriter;
	if(!s_q)
	{
		s_q = new std::vector<QLine>();
		s_q->reserve(256);
	}
	s_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	s_testParked = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if(!s_wake || !s_testParked)
		return false;
	InterlockedExchange(&s_mode, kModeWriter);
	s_thread = CreateThread(nullptr, 0, &WriterThread, nullptr, 0, nullptr);
	if(!s_thread)
	{
		InterlockedExchange(&s_mode, kModeSync);
		return false;
	}
	return true;
}

bool LogQueue(unsigned targets, const char* text, size_t n)
{
	const LONG mode = s_mode;
	if(mode == kModeCrash)
	{
		CrashWrite(targets, text, n);
		return true;
	}
	if(mode != kModeWriter)
		return false;
	QLine l{targets, std::string(text, n)};   // the copy outside the lock
	bool wake = false;
	AcquireSRWLockExclusive(&s_qLock);
	if(s_mode == kModeCrash)
	{
		ReleaseSRWLockExclusive(&s_qLock);
		CrashWrite(targets, text, n);
		return true;
	}
	if(s_mode != kModeWriter)
	{
		ReleaseSRWLockExclusive(&s_qLock);
		return false;
	}
	if(s_q->size() >= kLogQueueLines || s_qBytes + n > kLogQueueBytes)
	{
		if(!s_epDrop)
		{
			s_epFirstMs = NowMs();
			s_c.episodes++;
		}
		s_epDrop++;
		s_c.dropped++;
		if(targets & kLogToRender)
		{
			s_epDropR++;
			s_c.droppedRender++;
		}
		if(targets & kLogToServer)
		{
			s_epDropS++;
			s_c.droppedServer++;
		}
	}
	else
	{
		wake = s_q->empty();
		s_q->push_back(std::move(l));
		s_qBytes += n;
		s_seqQueued++;
		s_c.queued++;
		if(s_q->size() > s_c.peakLines)
			s_c.peakLines = s_q->size();
		if(s_qBytes > s_c.peakBytes)
			s_c.peakBytes = s_qBytes;
	}
	ReleaseSRWLockExclusive(&s_qLock);
	if(wake)
		SetEvent(s_wake);
	return true;
}

bool LogQueueServer(const char* text, size_t n, bool toStdout)
{
	return LogQueue(kLogToServer | (toStdout ? kLogToStdout : 0u), text, n);
}

bool LogSync(DWORD ms)
{
	if(s_mode != kModeWriter || GetCurrentThreadId() == GetThreadId(s_thread))
		return true;
	const ULONGLONG end = GetTickCount64() + ms;
	AcquireSRWLockExclusive(&s_qLock);
	const unsigned long long target = s_seqQueued;
	bool ok = true;
	SetEvent(s_wake);
	while(s_seqDone < target && s_mode == kModeWriter)
	{
		const ULONGLONG now = GetTickCount64();
		if(now >= end)
		{
			ok = false;
			break;
		}
		SleepConditionVariableSRW(&s_qWritten, &s_qLock, (DWORD)(end - now), 0);
	}
	ReleaseSRWLockExclusive(&s_qLock);
	return ok;
}

bool LogShutdown(DWORD ms)
{
	if(s_mode != kModeWriter)
		return s_mode != kModeCrash;
	const LogWriterCounters c = LogCounters();
	char b[400];
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "log writer: stopping at exit: %llu lines written, %llu queued, %llu dropped in %llu episodes, queue peak %zu lines "
	            "%zu bytes, %llu slow writes, the slowest %.1f ms (ticket 5054)",
	            c.written, c.queued - c.written, c.dropped, c.episodes, c.peakLines, c.peakBytes, c.slowWrites,
	            c.slowestWriteMs);
	std::string tr, ts;
	OwnLine(b, tr, ts);
	LogQueue(kLogToRender, tr.data(), tr.size());
	if(s_server)
		LogQueue(kLogToServer, ts.data(), ts.size());
	AcquireSRWLockExclusive(&s_qLock);
	s_stop = true;
	ReleaseSRWLockExclusive(&s_qLock);
	SetEvent(s_wake);
	if(WaitForSingleObject(s_thread, ms) == WAIT_OBJECT_0)
	{
		AcquireSRWLockExclusive(&s_qLock);
		InterlockedExchange(&s_mode, kModeStopped);
		ReleaseSRWLockExclusive(&s_qLock);
		return true;
	}
	char w[160];
	_snprintf_s(w, sizeof(w), _TRUNCATE, "the log writer did not finish within %lu ms at exit", (unsigned long)ms);
	LogFlushForCrash(w);
	return false;
}

void LogFlushForCrash(const char* why)
{
	std::vector<QLine> taken;
	bool got = false;
	unsigned long long d = 0, dR = 0, dS = 0;
	if(InterlockedCompareExchange(&s_mode, kModeCrash, kModeWriter) != kModeWriter)
		return;   // no writer (the lines were written synchronously) or already done
	// the lock is held only for pushes and pops; a bounded try all the same (this thread may have faulted inside a push)
	for(int i = 0; i < 200 && !(got = TryAcquireSRWLockExclusive(&s_qLock) != 0); ++i)
		Sleep(1);
	if(got)
	{
		taken.swap(*s_q);
		s_qBytes = 0;
		d = s_epDrop;
		dR = s_epDropR;
		dS = s_epDropS;
		s_epDrop = s_epDropR = s_epDropS = 0;
		ReleaseSRWLockExclusive(&s_qLock);
	}
	WakeAllConditionVariable(&s_qWritten);
	for(const QLine& l : taken)
		CrashWrite(l.targets, l.text.data(), l.text.size());
	char b[700];
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "LOUD: log writer: the crash path (%s): %zu queued lines written by thread %lu%s; %llu lines had been dropped "
	            "(render.log %llu, server.log %llu); lines from now on are written by their own thread; the writer's batch in "
	            "flight, if any, can be missing or written twice (ticket 5054)",
	            why ? why : "-", taken.size(), GetCurrentThreadId(),
	            got ? "" : " (the queue's lock could not be taken: its lines are lost)", d, dR, dS);
	std::string tr, ts;
	OwnLine(b, tr, ts);
	CrashWrite(kLogToRender, tr.data(), tr.size());
	CrashWrite(kLogToServer, ts.data(), ts.size());
}

LogWriterCounters LogCounters()
{
	AcquireSRWLockExclusive(&s_qLock);
	LogWriterCounters c = s_c;
	c.mode = (int)s_mode;
	ReleaseSRWLockExclusive(&s_qLock);
	return c;
}

bool LogTestHoldWriter(HANDLE ev)
{
	if(s_mode != kModeWriter)
		return false;
	if(!ev)
	{
		s_testHold = nullptr;
		return true;
	}
	ResetEvent(s_testParked);
	s_testHold = ev;
	SetEvent(s_wake);
	return WaitForSingleObject(s_testParked, 5000) == WAIT_OBJECT_0;
}

}  // namespace srvrender
