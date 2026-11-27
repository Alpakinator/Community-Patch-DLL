// proxy_report.h - the local error report of decision D119 (issue 3521; the Architect's section 3, the owner's D120: a file
// the tester sends in; no upload, no network code; D122: handed in as an issue on the patch's GitHub repository; D124:
// written at every failure of the 64-bit path, after which the session goes on as the stock game). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One folder per failure, written before the helper's popup (vp64helper.exe popup --report <dir>):
//   Documents\My Games\Sid Meier's Civilization 5\VP64\reports\<yyyymmdd-hhmmss>[-n]\
//     popup-summary.txt    one line naming the failed stage and code (issue 3520's helper shows it in its popup)
//     report.txt           the failure (stage, code, details; issue 0186: server.log's last FAILED or LOUD line; ticket
//                          0511: "server's reason: " the server's own verdict, which the popup shows first), the patch version and the pack's release, the SHA-256 of
//                          the 32-bit DLL, the x64 DLL, the proxy and server.exe (found and expected), the session's
//                          config and view masks, the EXE's build stamp, the activated mods with versions, Windows
//                          version, GPU and driver, memory, display mode, and the list of the attachments
//     <log>.tail.txt       the last kTailBytes of each log (vpproxy.log, the server's logs)
//     <file>               small text files copied whole (manifest.json, vpproxy.ini, VP64Options.ini)
// The folder's root is the folder of the game's config.ini (ProxySession::ConfigIniPath, which honours
// VPPROXY_TEST_CONFIG_INI; config.ini itself is neither read nor written here) + \VP64\reports.
// Left out (D83 and privacy): the Steam id, the computer name and the user name (every text is scrubbed: the profile
// folder becomes %USERPROFILE%, the user and computer names <user> and <computer>, a SteamID64 <steamid>), saves, any
// dump of the EXE process, and (MC #577, issue 3523; until then 3522 copied it) the server's own minidump: reports are
// public GitHub issues (D122) and a dump holds the game state and strings that may name the user. report.txt names each
// <run>\*.dmp with its size and time and says so; the dump stays in the run folder. (The proxy itself cannot dump
// server.exe: MiniDumpWriteDump of a 64-bit process from a 32-bit one fails with 0x8007012B, [L] 2026-11-14,
// research/03-dll-engine-boundary/proxy-design.md section 22.3; server.exe writes its own, issue 3522.)
// No engine, no vpipc: unit-tested by proxy/tools/rt_tests/session_unit.cpp. The activated mods come from
// proxy_report_mods.cpp (the engine's database library), which the unit test does not link.
#pragma once
// The STL layout must match the other proxy TUs (see proxy_rt_logic.h).
#if defined(_MSC_VER) && !defined(_SECURE_SCL)
#define _SECURE_SCL 0
#endif
#if defined(_MSC_VER) && !defined(_HAS_ITERATOR_DEBUGGING)
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace ProxyReport
{
enum
{
	kTailBytes = 64 * 1024,   // of each log
	kCopyMax = 256 * 1024,    // a text file larger than this is cut to its last kCopyMax bytes
	kFailLineMax = 400,       // issue 0186: the quoted server.log line is clipped to this many bytes (+ " [...]")
	kReasonMax = 600          // ticket 0511: the server's reason ("server's reason: " in report.txt), clipped the same way
};

struct Hashed
{
	std::string label, path;   // path in the ANSI code page (the proxy's paths)
	std::string expected;      // lower-case hex from the pack's manifest, "" when it names none
	std::string expectedNote;  // e.g. "one of 2 x86_dlls"; the expected list for the 32-bit DLL
};

struct Input
{
	std::string stage;                // "pack", "server-start", "handshake", "server-death"
	std::string code;                 // the stage's code (a status name, "server-missing", ...)
	std::vector<std::string> details; // one line each
	std::string patch;                // the proxy's build line (vpproxy.log's first line)
	std::string packRelease;          // the manifest's release ("" when no manifest was read)
	std::vector<Hashed> hashes;
	std::vector<std::string> tails;   // log files (ANSI paths)
	std::vector<std::string> copies;  // small text files copied whole (ANSI paths; manifest.json, vpproxy.ini)
	std::vector<std::wstring> copiesW;  // the same for wide paths (the options file)
	std::string runDir;               // the boot's run folder: *.dmp in it are named in report.txt, never copied ("" none)
	std::vector<std::string> session; // the session lines (masks, rule bits, the kind)
	std::string mods;                 // the activated mods, one per line ("" = not read; say why in modsWhy)
	std::string modsWhy;
	bool d33;                         // issue 3523: a D33 session (the game was ended), else a stock one
	bool nonFatal;                    // ticket 0487: a non-fatal stage (uidraw): the game went on with the patch
	bool bootEnded;                   // ticket 0721: the boot had no game context to return, so the game was ended
	Input() : d33(false), nonFatal(false), bootEnded(false) {}
	std::string summary;              // one line for the popup (popup-summary.txt: the helper shows its first line in
	                                  // place of its default summary; it names the stage and the code)
};

// <folder of config.ini>\VP64\reports. False when no config.ini path can be formed.
bool Root(std::wstring* out);

// Writes one report folder under root (created with its parents). *dir = the folder written, or the one that could not
// be made. *lines = one log line per step that failed or was left out, the last one "report: ..." with the result.
// False when report.txt could not be written (the caller still ends the game and starts the helper).
bool Write(const Input& in, const std::wstring& root, std::wstring* dir, std::vector<std::string>* lines);

// The privacy scrub every text of the report goes through (see the top of this file).
std::string Scrub(const std::string& text);
// The [exe], [system] sections' text (no names: Windows version, memory, GPU and driver, display mode).
std::string ExeText();
std::string SystemText();
// The last n bytes of a file, from the first line start in them ("" with *why when it cannot be read).
std::string Tail(const std::string& path, size_t n, std::string* why);
// Issue 0186: the last line of a log's text that holds the word FAILED or LOUD (upper case, not inside a longer word of
// letters and digits: VPOVL_FAILED counts, FAILEDX and CLOUD do not), as one line: its line end dropped, control
// characters made spaces, trailing blanks dropped, clipped to maxLen bytes (never inside a UTF-8 sequence) with " [...]"
// after a cut. "" when no line holds either word. Write quotes it from server.log's tail under [failure]; the caller
// scrubs the text first (clipping a scrubbed text cannot leave half a user name behind).
std::string LastFailureLine(const std::string& text, size_t maxLen);
// Ticket 0511: the server's own reason for its end, from server.log's text: the last verdict line the server writes at
// every exit (server/src/remote/srv_remote_main.cpp WriteReport: "result: FAILED: <why>; remote mode, section ...", at
// the line's start or after its "[ time] " stamp). Returns <why> without the "; remote mode, section ..." tail, control
// characters made spaces, outer blanks dropped, clipped like LastFailureLine. "" when the last verdict is not
// "result: FAILED: <why>" (passed, no reason) or no verdict line is in the text (a crash, a cut tail). Write puts it
// under [failure] as "server's reason: <why>", which vp64helper's popup shows first (the stage and code then follow as
// its details). The caller scrubs the text first.
std::string ServerReason(const std::string& text, size_t maxLen);

// Ticket 0744 (K31): whether the server's own record <run>\server-fatal.txt (server/src/remote/srv_exe_crash.h
// FatalRecord) is the reason of the server that ended. Pure (server/tests/remote/test_savemods.py tests it):
//   serverEnded  the server's exit code was read (false: still running or no handle; the record is taken, as before)
//   serverExit   that exit code; serverPid the server's process id (0 when unknown)
//   recExit      the record's "exit:" value; recPid its "pid:" value, recHasPid whether it has that line (servers
//                before ticket 0744 write none)
// Returns kFatalUseNo (not this server's reason: *why says so), kFatalUseExit (the exit codes match: ticket 0641's rule)
// or kFatalUsePid (the exit codes differ, the record names this server's pid and the server ended by an exception code:
// it wrote its reason, then faulted before its own exit, as in K31: the render end, then 0xC0000005).
// An exception code is the NTSTATUS error class (0xC0000000 and up but 0xC000013A, a console close) or
// 0x80000002..0x80000004, the rule of the server's SrvExeCrash::IsCrashCode.
enum FatalUse
{
	kFatalUseNo = 0,
	kFatalUseExit = 1,
	kFatalUsePid = 2
};
inline bool IsExceptionExit(unsigned long code)
{
	return (code != 0xC000013Aul && (code & 0xC0000000ul) == 0xC0000000ul) || (code >= 0x80000002ul && code <= 0x80000004ul);
}
inline FatalUse UseFatalRecord(bool serverEnded, unsigned long serverExit, unsigned long serverPid, unsigned long recExit,
                               bool recHasPid, unsigned long recPid, std::string* why)
{
	if (!serverEnded || serverExit == recExit) return kFatalUseExit;
	if (recHasPid && serverPid != 0 && recPid == serverPid && IsExceptionExit(serverExit)) return kFatalUsePid;
	if (why)
		*why = !recHasPid ? "the record names no pid"
		       : serverPid == 0 ? "the server's pid is unknown"
		       : recPid != serverPid ? "the record is of another server process"
		                             : "the server's exit code is not an exception code";
	return kFatalUseNo;
}

// proxy_report_mods.cpp (the product; not in the unit test): the activated mods of the game's mods database
// (<folder of config.ini>\cache\Civ5ModsDatabase.db, Mods.Activated = 1, in RowID order: ModID, Version, Name).
// False with *why when the database cannot be read.
bool ActivatedMods(const std::wstring& configIni, std::string* text, std::string* why);
}  // namespace ProxyReport
