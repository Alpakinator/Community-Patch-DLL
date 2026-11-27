// srv_render_common.h - helpers of the renderer inside server.exe (epic 3300, issue 3301): log, clock, seqlock reads.
//
// Every file in server/src/render/ is compiled WITHOUT the DLL fork's include path (server/build_server.py, render
// units): the render thread cannot even name a DLL type (design research/05-graphics-composition/
// renderer-in-server-design.md section 1; bug 0103). Nothing here includes srv_common.h or a remote header.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "shell_shared.h"

namespace srvrender {

template <class T> inline void SafeRelease(T*& p)
{
	if(p)
	{
		p->Release();
		p = nullptr;
	}
}

int64_t Qpc();
int64_t QpcFreq();
double QpcToMs(int64_t d);

// The render log. RLog: render.log only (per-second lines, details). RNote: render.log and the host's callback (server.log
// in server.exe). Both are safe from any thread; lines are prefixed with the local time in render.log.
typedef void (*HostLogFn)(const char* line);
void LogOpen(const wchar_t* path, HostLogFn fn);
void LogClose();
void RLog(const char* fmt, ...);
void RNote(const char* fmt, ...);

// Ticket 5054: the log writer. Without it (test programs) every line is written on the calling thread, as before. With it
// (server.exe: LogWriterStart) a producer formats its line and queues it under a lock held only for the push, and one
// writer thread does every write of render.log, server.log and stdout: a disk that stalls makes the writer wait, never
// the render thread. The queue holds at most kLogQueueLines lines or kLogQueueBytes bytes; a line past that is dropped and
// counted, and the writer writes ONE LOUD line per episode to both logs when it gets through. The writer also says each
// write of 50 ms or more (the first 64). Research: research/05-graphics-composition/unit-combat-server.md 18.27.
enum : unsigned
{
	kLogToRender = 1,   // render.log (LogOpen)
	kLogToServer = 2,   // server.log (LogSetServerSink)
	kLogToStdout = 4,   // the process's stdout
};
constexpr size_t kLogQueueLines = 4096;
constexpr size_t kLogQueueBytes = 4u << 20;
// server.exe: server.log for the writer, and the clock of its "[%9.3f]" stamps (the writer's own lines). Before the start.
void LogSetServerSink(FILE* serverLog, double (*nowSeconds)());
// Starts the writer thread (once). False when it could not be created (the logs stay synchronous).
bool LogWriterStart();
// A whole line (with its '\n') for the targets. True: queued (or dropped and counted, or written at once on the crash
// path); false: no writer runs (never started, or stopped at exit): the caller writes the line itself.
bool LogQueue(unsigned targets, const char* text, size_t n);
// LogQueue for server.log (and stdout): server.exe's SrvLog, whose source cannot include this header (shell/common is
// not on its include path), declares the log writer's functions itself: srv_main.cpp, srv_remote_main.cpp.
bool LogQueueServer(const char* text, size_t n, bool toStdout);
// Waits (at most ms) until every line queued before the call is written. True when written (or no writer runs).
bool LogSync(DWORD ms);
// At exit: the writer writes everything queued and stops (at most ms); later lines are written synchronously. A writer
// that does not finish in time is left and the queue is written by LogFlushForCrash. Returns true when the writer finished.
bool LogShutdown(DWORD ms);
// On the crash path (any thread, the faulting one included): takes the queue (the lock is never held across a write; a
// bounded try), writes its lines with WriteFile on the files' handles (not the CRT's FILE*, whose lock the writer may hold
// while it waits on the disk), and from then on every line is written that way on its caller's thread. The writer's
// batch in flight, if any, can be missing or written twice (the line it writes says so). why: one clause for that line.
void LogFlushForCrash(const char* why);
// Counters for tests and the exit line.
struct LogWriterCounters
{
	unsigned long long queued = 0, written = 0, dropped = 0, droppedRender = 0, droppedServer = 0, episodes = 0;
	unsigned long long slowWrites = 0, crashLines = 0;
	size_t peakLines = 0, peakBytes = 0;
	double slowestWriteMs = 0;
	int mode = 0;   // 0 synchronous (no writer), 1 the writer runs, 2 the crash path, 3 stopped at exit
};
LogWriterCounters LogCounters();
// TEST ONLY (server/tests/render/logwriter): the writer waits on ev (until it is set) before it takes its next batch, as a
// writer blocked on the disk; returns once the writer waits there (at most 5 s; false if it did not). null: no hold.
bool LogTestHoldWriter(HANDLE ev);

// A consistent copy of a seqlocked block whose first member is `volatile uint32_t seq` (0 = never written).
template <class T> inline bool SnapBlock(const T* src, T& out)
{
	const volatile uint32_t* seq = (const volatile uint32_t*)src;
	for(int i = 0; i < 16; ++i)
	{
		const uint32_t s1 = *seq;
		if(s1 == 0)
			return false;
		if(s1 & 1)
		{
			YieldProcessor();
			continue;
		}
		MemoryBarrier();
		memcpy(&out, (const void*)src, sizeof(T));
		MemoryBarrier();
		if(*seq == s1)
		{
			*(uint32_t*)&out = s1;
			return true;
		}
	}
	return false;
}

inline bool SnapCamera(const VPShellHeader* hdr, VPShellCamera& out)
{
	return SnapBlock(VPSHELL_CAMERA(hdr), out);
}

std::string Narrow(const std::wstring& w);

}  // namespace srvrender
