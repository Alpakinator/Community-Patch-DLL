// srv_exe_crash.h - D118 for an EXE crash (ticket 0418; decisions D118, D119, D122, D124): when the EXE dies with an
// exception exit code (0xC0000005 and kin), the server writes the D119 error report (stage exe-crash) and starts the
// popup helper, because the proxy that writes the report for every other failure died with the EXE.
// SPDX-License-Identifier: GPL-3.0-or-later
// Knowledge base: research/03-dll-engine-boundary/error-report-exe-crash.md.
//
// The EXE is the server's vpipc peer (the proxy runs inside it). At the attach the server opens the peer process
// (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION) and keeps the handle, so the exit code stays readable after the
// process has ended and a reused pid cannot mislead it. When the engine is gone (a service thread's serve() or a call the
// DLL made returned PeerDied and kin), SrvRemote::ReportAndExit(3, ...) calls OnPeerGone first, inside the one exiting
// thread (no other thread can end the process meanwhile): it waits up to kPeerEndWaitMs for the EXE to end,
// reads its exit code and, for a crash code (IsCrashCode) on the shipped layout (vpproxy.ini pack=1 beside the server's
// folder, or server.cfg exe_crash_report=1), writes the report folder and starts vp64helper.exe popup. A normal exit
// (0, the proxy's own exit code 3 after a server death, a harness stop, STATUS_CONTROL_C_EXIT) writes nothing new.
// Ticket 0519: an exit code that is not a crash code, other than 0, is still an EXE crash when the proxy's crash record
// <run>\exe-crash.txt (ticket 0419) names this EXE's pid on its "process:" line and was written after the EXE started:
// Windows Error Reporting's "has stopped working" box ends a crashed EXE with 255 after "Close program". The report then
// takes the code and its name from the record and says the exit code and the proof in [failure]. Exit code 0 (the game's
// own clean end) is never taken as a crash. The optional trigger on vpipc's "peer hung" (report while WER's box is up)
// was not taken: research/03-dll-engine-boundary/server-host.md, the EXE-crash section, says why.
//
// server.cfg keys (all optional; the proxy writes none of them, so the shipped server uses the defaults; tests set
// them directly or through vpproxy.ini server_cfg_extra=, issue 0307):
//   exe_crash_report = auto | 0 | 1   auto (default): on when <proxy dir>\vpproxy.ini says pack=1
//   proxy_dir = <dir>                 the folder of the proxy, vpproxy.ini and vp64helper.exe (default: the parent of
//                                     server.exe's folder, the D28 layout <proxy dir>\VP64\server.exe)
//   report_root = <dir>               the reports folder (default <player folder>\VP64\reports, as the proxy's)
//   exe_crash_helper_args = <text>    TEST ONLY: appended to the helper's command line (LOUD in server.log)
//   test_exe_kill = attach|boot|load:<ms>[:<hex code>]   TEST ONLY (LOUD): ends the EXE with TerminateProcess(code,
//                                     default C0000005) <ms> after the event, to check this path live without an EXE
//                                     patch. load = the start of a save's read (ICvGame1::Read, NoteLoad).
#pragma once
#include <stdint.h>
#include <string>

namespace SrvExeCrash
{
enum
{
	kPeerEndWaitMs = 5000  // how long OnPeerGone waits for the EXE to end after the channel failed
};

// True for an exit code that means the process died of an exception: NTSTATUS severity error (0xC0000000..0xFFFFFFFF)
// except STATUS_CONTROL_C_EXIT (0xC000013A, a console close or Ctrl+C), and the warning-severity exception codes
// EXCEPTION_DATATYPE_MISALIGNMENT, EXCEPTION_BREAKPOINT and EXCEPTION_SINGLE_STEP (0x80000002..4). *pName: the code's
// name ("EXCEPTION_ACCESS_VIOLATION", ...; "NTSTATUS error" for an unnamed one); "" when not a crash code.
bool IsCrashCode(uint32_t code, const char** pName);

void OnAttach(uint32_t uPeerPid);  // after attach_server: opens and keeps the peer's process handle; arms test_exe_kill
void NoteBoot();                   // kRt_Boot
void NoteLoad();                   // a save's read begins (srv_remote_savevariant.cpp, ICvGame1::Read)

// Called by SrvRemote::ReportAndExit(3) (srv_remote_main.cpp). Safe from several threads at once: the first caller does
// the work; the others wait (at most 60 s) until it is done and get the same result. Returns one
// clause for the FAILED reason ("the EXE ended with exception code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)"), "" when
// the EXE ended normally, is still running or could not be checked.
std::string OnPeerGone();

// Ticket 0422: the same report folder and helper start for a failure the server sees in its own process while the EXE
// still runs (stage dll-fatal: the DLL stopped on a failed check, srv_dll_fatal.h). Same switch (exe_crash_report: auto =
// vpproxy.ini pack=1), same root, same scrub; report.txt's [failure] is stage, code, then `failure` (lines ending in
// '\n'), then the EXE, its start, the times since the last load, kRt_Boot and the attach, and server.log's last FAILED or
// LOUD line. popup-summary.txt holds `summary`; report.txt's title names `title`, the paragraph under it is `intro`.
struct ServerFailure
{
	std::string stage;     // "dll-fatal"
	std::string title;     // "the DLL stopped on a failed check, ticket 0422"
	uint32_t code;         // the exception code
	const char* codeName;  // its name
	std::string summary, intro, failure;
	ServerFailure() : code(0), codeName("") {}
};
// Returns the report folder (UTF-8; "" when none was made); *pOn = false when the report is off (logged LOUD, no helper).
std::string ReportServerFailure(const ServerFailure& f, bool* pOn);
// The report's privacy scrub (the profile folder, the user and computer names, SteamID64s), for other server texts.
std::string ScrubText(const std::string& text);
// Ticket 0641: the shipped layout, by the report switch's rule (exe_crash_report: auto = <proxy dir>\vpproxy.ini says
// pack=1; 0 / 1 force it). SrvRemote::ReportAndExit then ends a deliberate end at once after server.log's result line
// (no server-report.txt, no call tables: six files of 2-8 s each on a slow disk). *pWhy: one clause.
bool ShippedLayout(std::string* pWhy);
// Ticket 5054: server.exe's log writer queues the log lines; a report syncs it (at most 2 s) before it reads server.log and
// render.log, and the dll-fatal handler before the DLL's filter ends the process. Null (the default, the unit tests): no
// sync. LogSync() calls it.
void SetLogSync(bool (*fn)(unsigned long ms));   // unsigned long = DWORD
bool LogSync(unsigned long ms);

// Ticket 0641 (D118, D119): the server's own reason when it ends on purpose. SrvRemote::ReportAndExit(code, why) writes
// <run>\server-fatal.txt for every exit code but 0 (a clean end), 3 (the engine is gone: the EXE ended first; OnPeerGone's
// report) and the save-variant codes 0x2056SSRR (the proxy's own words, ticket 0354), before server-report.txt and before
// the process ends. The proxy (pack=1) reads it when it writes its D119 report after the server's death
// (proxy_rt.cpp ReportFailure): the summary becomes the popup's summary line, code and reason go into [failure]. One
// report, the proxy's: the server starts no helper on this path (the proxy's report knows the session: boot or game, D33
// or stock). The technical reason itself is already the popup's first line since ticket 0511 (the proxy reads server.log's
// "result: FAILED: <why>" line, proxy_report.cpp ServerReason); this record adds what stopped in a player's words.
// The file's lines (UTF-8, CRLF, scrubbed, each one line):
//   server-fatal: the 64-bit server ended on purpose (ticket 0641)
//   exit: <decimal exit code>
//   pid: <the server's process id> ticket 0744: the proxy takes a record of this pid also when the server then died by an
//                                  exception code before its exit (K31: a render end, then an access violation)
//   code: <token>                  render-models, render-units, render-fog, ..., server-error, server-exception, database,
//                                  engine-exception, save-mod-mismatch (srv_remote_savemods.h), exit-<n>
//   summary: <the player's words>  one sentence on what stopped and that the game ended
//   reason: <why as the server logged it>
struct FatalRecord
{
	std::string token, summary;
};
// Pure: the token and the player's words for an exit code and its reason (exposed for the tests). Empty token: no record
// for this code (0, 3, a save-variant code).
FatalRecord FatalWords(int code, const std::string& why);
// Writes <SrvRunDir()>\server-fatal.txt (scrubbed). True when written; false and one log line when not (no run folder, a
// code without a record, a write error).
bool WriteFatalRecord(int code, const std::string& why);
}  // namespace SrvExeCrash
