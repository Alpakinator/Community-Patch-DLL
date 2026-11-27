// srv_guard.h - the window guard of server.exe (issue 2501). See srv_guard.cpp and tools/harness/native/README.md,
// section "The server's guard (issue 2501)".
//
// With CIVBG_GUARD=1 in the environment (the harness sets it on every launch; the proxy's CreateProcessA passes the
// EXE's environment on to the server), main() calls SrvGuardInstall() first, before any DLL is loaded: every
// top-level window the process ever creates is cloaked (DWMWA_CLOAK) at creation and never activated or made
// foreground; a 100 ms watchdog cloaks anything that slipped through (counted separately). The state is published in
// the shared memory Local\CivBgGuard_<pid> with the layout of the EXE's focus guard, so the harness reads the server
// like the EXE. Without CIVBG_GUARD=1 nothing is installed (the owner playing normally sees dialogs).
// Issue 0475: with C5_CHILD_GUARD=<the 32-bit focus_guard.dll> as well (the harness sets it, issue 0473), the guard
// follows the server's 32-bit children (vp64helper.exe of the EXE-crash and DLL-fatal reports): each one starts
// suspended and runs only once that guard, loaded by bg_helper32.exe beside the DLL, reports guard_state 1; otherwise
// it is terminated and CreateProcess fails with ERROR_DLL_INIT_FAILED (fail closed). A part that cannot be installed
// fails the self-test (guard_missing bit 0x40, SRV_GUARD_SELFTEST_EXIT_CODE). See srv_guard.cpp.
#pragma once
#include <stddef.h>

// Exit code of server.exe when CIVBG_GUARD=1 and the guard could not be installed (fail closed, before any DLL).
#define SRV_GUARD_EXIT_CODE 0xC5F00030u
// Issue 2703. Exit code when CIVBG_GUARD=1, every hook went in, but the install-time self-test found a part that does
// not work (today: a WH_CBT activation veto that cannot be installed, the failure of one-off 9009); before any DLL.
// (0xC5F00031 is the EXE's focus_guard.dll self-test failure, issue 9009.)
#define SRV_GUARD_SELFTEST_EXIT_CODE 0xC5F00032u
// Issue 2703. Exit code when the activation veto cannot be installed on a later thread that is about to create a
// top-level window (or that the watchdog found owning one): counted (win_veto_failed), logged, then the server ends.
#define SRV_GUARD_VETO_EXIT_CODE 0xC5F00033u
enum SrvGuardState
{
	SRV_GUARD_NOT_REQUESTED = 0,	// CIVBG_GUARD is not 1: nothing installed
	SRV_GUARD_INSTALLED = 1,
	SRV_GUARD_FAILED = -1,			// requested but not installed: the caller exits with SRV_GUARD_EXIT_CODE
};

// Installs the guard if the environment asks for it. Returns the state; on failure szWhy says why.
int SrvGuardInstall(char* szWhy, size_t cchWhy);
// Issue 2703: the exit code main() must use after SRV_GUARD_FAILED (SRV_GUARD_EXIT_CODE or SRV_GUARD_SELFTEST_EXIT_CODE).
unsigned long SrvGuardExitCode();
// Issue 2703: where a later fail-closed exit (SRV_GUARD_VETO_EXIT_CODE) also writes its line (server.log); NULL = none.
void SrvGuardSetLogSink(void (*pfn)(const char*));
// One line for the server log: guard state and counters, console window, parent pid, cwd, CIVBG_GUARD value.
void SrvGuardDescribe(int iState, char* szOut, size_t cchOut);
// The parent process id (Toolhelp snapshot), 0 if unknown.
unsigned long SrvParentPid();

#ifdef VP_SERVER_TEST_DIALOG
// TEST BUILD ONLY (server/build_server.py --test-dialog; never in the normal build): server.exe --test-dialog ...
// shows NULL-owner message boxes from new threads, as the DLL's CvPreconditionDlg does, then waits for its parent to
// end and exits by itself. See srv_guard.cpp for the options.
bool SrvGuardTestArg(int argc, char** argv, const char* szName);
int SrvGuardTestDialogMain(int argc, char** argv, int iGuardState);
#endif
