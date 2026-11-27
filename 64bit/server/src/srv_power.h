// srv_power.h - opt a process out of Windows power throttling (bug 0118, tracker 4000), own code, 2026-11-13.
// Header only (inline functions), so that no build file has to name it: server.exe includes it from
// server/src/srv_main.cpp, shell_server.exe from shell/server/shell_server.cpp, the test from server/tests.
//
// Why: in run d33_r3 of 9039 (research/05-graphics-composition/lockstep-probe.md 3.2) shell_server.exe's loop ran at
// 64 Hz for about 5 minutes: its 0.5 ms high-resolution timer waits and timeBeginPeriod(1) were held to the 15.6 ms
// default tick. Windows 11 may ignore the timer-resolution request of a process the user cannot see and may run such a
// process under EcoQoS (execution-speed throttling). Both servers run on a hidden desktop on the workstation and
// without a visible window on a player's machine.
//
// SrvPowerOptOut() sets, for the whole process, ProcessPowerThrottling with ControlMask = EXECUTION_SPEED |
// IGNORE_TIMER_RESOLUTION and StateMask = 0 ("this process decides these itself: never throttle them"), and for the
// calling thread ThreadPowerThrottling EXECUTION_SPEED with StateMask = 0, then reads the process state back.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdio.h>

// The opt-out the servers apply: both flags controlled, neither throttled.
#define SRV_POWER_CONTROL (PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION)
#define SRV_POWER_STATE 0u

// Reads a process's power-throttling masks (GetProcessInformation(ProcessPowerThrottling)). hProcess needs
// PROCESS_QUERY_LIMITED_INFORMATION. Returns false (and *pdwError = GetLastError()) when the query fails.
inline bool SrvPowerQuery(HANDLE hProcess, unsigned long* pControl, unsigned long* pState, unsigned long* pdwError)
{
	PROCESS_POWER_THROTTLING_STATE st;
	ZeroMemory(&st, sizeof(st));
	st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	if(!GetProcessInformation(hProcess, ProcessPowerThrottling, &st, sizeof(st)))
	{
		if(pdwError)
			*pdwError = GetLastError();
		return false;
	}
	if(pControl)
		*pControl = st.ControlMask;
	if(pState)
		*pState = st.StateMask;
	if(pdwError)
		*pdwError = 0;
	return true;
}

// Reads the calling thread's execution-speed throttling masks (GetThreadInformation(ThreadPowerThrottling)).
inline bool SrvPowerQueryThread(unsigned long* pControl, unsigned long* pState, unsigned long* pdwError)
{
	THREAD_POWER_THROTTLING_STATE st;
	ZeroMemory(&st, sizeof(st));
	st.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	if(!GetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &st, sizeof(st)))
	{
		if(pdwError)
			*pdwError = GetLastError();
		return false;
	}
	if(pControl)
		*pControl = st.ControlMask;
	if(pState)
		*pState = st.StateMask;
	if(pdwError)
		*pdwError = 0;
	return true;
}

// Applies the opt-out to the current process and the calling thread. Returns true when the process part was applied;
// szOut (if not NULL) receives one line for the log: what was set and what reads back, or which call failed and why.
inline bool SrvPowerOptOut(char* szOut, size_t nOut)
{
	PROCESS_POWER_THROTTLING_STATE ps;
	ZeroMemory(&ps, sizeof(ps));
	ps.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	ps.ControlMask = SRV_POWER_CONTROL;
	ps.StateMask = SRV_POWER_STATE;
	const BOOL bProc = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof(ps));
	const DWORD dwProcErr = bProc ? 0 : GetLastError();

	THREAD_POWER_THROTTLING_STATE ts;
	ZeroMemory(&ts, sizeof(ts));
	ts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	ts.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	ts.StateMask = 0;
	const BOOL bThread = SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &ts, sizeof(ts));
	const DWORD dwThreadErr = bThread ? 0 : GetLastError();

	unsigned long c = 0, s = 0, e = 0;
	const bool bQuery = SrvPowerQuery(GetCurrentProcess(), &c, &s, &e);
	if(szOut && nOut)
	{
		char szProc[48], szThread[48], szBack[96];
		if(bProc)
			sprintf_s(szProc, sizeof(szProc), "set");
		else
			sprintf_s(szProc, sizeof(szProc), "FAILED (error %lu)", dwProcErr);
		if(bThread)
			sprintf_s(szThread, sizeof(szThread), "set");
		else
			sprintf_s(szThread, sizeof(szThread), "FAILED (error %lu)", dwThreadErr);
		if(bQuery)
			sprintf_s(szBack, sizeof(szBack), "reads back control 0x%lX state 0x%lX", c, s);
		else
			sprintf_s(szBack, sizeof(szBack), "read-back FAILED (error %lu)", e);
		sprintf_s(szOut, nOut,
		          "power throttling opt-out (bug 0118): process control 0x%X state 0x%X %s, main thread execution "
		          "speed %s; %s",
		          (unsigned)SRV_POWER_CONTROL, (unsigned)SRV_POWER_STATE, szProc, szThread, szBack);
	}
	return bProc != FALSE;
}
