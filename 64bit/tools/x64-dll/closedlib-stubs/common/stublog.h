// stublog.h - shared call logger for the x64 closed-library stubs (issue 1005; used read-only by 1006).
//
// Contract (tracker/issues/1005-stubs-database-localization-lua.md):
//   * Put STUB_CALL("<group>", "<demangled signature>") at the top of every stub.
//   * The first call of each stub is written to the log at once, in call order, with a sequence number,
//     the thread id and the time since the first stub call. Later calls are only counted.
//   * The per-stub call counts are written by StubLog_Flush(), on demand and at process exit (DLL unload).
//   * Log file: the path in environment variable CV64_STUBLOG, else "stubcalls.log" in the current
//     directory. The file is truncated when the first stub of the process is called.
//   * Thread-safe. No dependency on the DLL: Win32 and the C runtime only. This header includes nothing.
//
// Log format (one record per line, tab-separated fields after the tag):
//   #  cv64-stublog 1  pid=<pid>  path=<path>
//   FIRST  seq=<n>  tid=<thread id>  t=<seconds>  <group>  <signature>
//   COUNT  seq=<n>  calls=<n>  <group>  <signature>        (one block per flush, in first-call order)
//   #  flush <k>  t=<seconds>  stubs=<n>  calls=<n>
// The last COUNT block in the file is the current one.

#ifndef CV64_STUBLOG_H
#define CV64_STUBLOG_H
#pragma once

// One per stub, a function-local static: constant-initialised, so it costs no guard and no constructor.
struct StubLogSite
{
	const char* group;
	const char* signature;
	volatile long calls;	// incremented on every call
	long seq;				// 1-based first-call order, 0 until the first call is logged
};

// Records one call of the stub described by `site`. Called by STUB_CALL; not meant to be called directly.
void StubLog_Hit(StubLogSite* site);

// Writes the per-stub call counts (a COUNT block) to the log and flushes the file. Safe to call at any time
// from any thread, including when no stub has been called yet (then it writes the header only).
void StubLog_Flush();

// The log file path in use (resolved on first use). Never NULL.
const char* StubLog_Path();

#define STUB_CALL(group, signature) \
	do { static StubLogSite cv64_stub_site_ = { (group), (signature), 0, 0 }; StubLog_Hit(&cv64_stub_site_); } while (0)

#endif // CV64_STUBLOG_H
