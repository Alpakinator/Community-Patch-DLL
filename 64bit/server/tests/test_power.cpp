// test_power.cpp - bug 0118 (tracker 4000), own code, 2026-11-13. Built and run by server/tests/run_power_test.py.
//
//   test_power.exe                      unit test of server/src/srv_power.h: SrvPowerOptOut() applies the opt-out and
//                                       it reads back (process control 0x5 state 0x0, thread control 0x1 state 0x0).
//                                       Exit 0 pass, 1 fail.
//   test_power.exe --rate MODE SECONDS  the pacing loop of shell_server.exe's terrain scene (timeBeginPeriod(1), a
//                                       0.5 ms CREATE_WAITABLE_TIMER_HIGH_RESOLUTION wait, WaitForSingleObject 5 ms)
//                                       for SECONDS; prints the loop rate of every second. MODE: default (nothing
//                                       set), throttle (the process asks to be throttled: both flags controlled
//                                       and on, the state Windows can impose on an invisible process), optout
//                                       (SrvPowerOptOut).
#include "../src/srv_power.h"

#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "winmm.lib")

static int Unit()
{
	int fails = 0;
	unsigned long c = 0, s = 0, e = 0;
	if(SrvPowerQuery(GetCurrentProcess(), &c, &s, &e))
		printf("before: process control 0x%lX state 0x%lX\n", c, s);
	else
		printf("before: query failed (error %lu)\n", e);
	char line[512];
	const bool ok = SrvPowerOptOut(line, sizeof(line));
	printf("%s\n", line);
	if(!ok)
	{
		printf("FAIL: SrvPowerOptOut returned false\n");
		++fails;
	}
	if(!SrvPowerQuery(GetCurrentProcess(), &c, &s, &e))
	{
		printf("FAIL: process read-back failed (error %lu)\n", e);
		++fails;
	}
	else if(c != SRV_POWER_CONTROL || s != SRV_POWER_STATE)
	{
		printf("FAIL: process reads back control 0x%lX state 0x%lX, expected 0x%X 0x%X\n", c, s,
		       (unsigned)SRV_POWER_CONTROL, (unsigned)SRV_POWER_STATE);
		++fails;
	}
	else
		printf("PASS: process control 0x%lX state 0x%lX\n", c, s);
	if(!SrvPowerQueryThread(&c, &s, &e))
	{
		printf("FAIL: thread read-back failed (error %lu)\n", e);
		++fails;
	}
	else if(c != THREAD_POWER_THROTTLING_EXECUTION_SPEED || s != 0)
	{
		printf("FAIL: thread reads back control 0x%lX state 0x%lX, expected 0x1 0x0\n", c, s);
		++fails;
	}
	else
		printf("PASS: thread control 0x%lX state 0x%lX\n", c, s);
	printf("test_power: %s\n", fails ? "FAILED" : "OK");
	return fails ? 1 : 0;
}

static int Rate(const char* modeArg, double seconds)
{
	// MODE[-sleep]: "-sleep" waits with Sleep(1) (shell_server.cpp's fallback when it has no high-resolution timer, and
	// every wait that rests on the system timer tick) instead of the 0.5 ms high-resolution timer
	char mode[32];
	strncpy_s(mode, sizeof(mode), modeArg, _TRUNCATE);
	char* dash = strstr(mode, "-sleep");
	const bool bSleep = dash != NULL;
	if(dash)
		*dash = 0;
	if(!strcmp(mode, "optout"))
	{
		char line[512];
		SrvPowerOptOut(line, sizeof(line));
		printf("%s\n", line);
	}
	else if(!strcmp(mode, "throttle"))
	{
		PROCESS_POWER_THROTTLING_STATE ps;
		ZeroMemory(&ps, sizeof(ps));
		ps.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		ps.ControlMask = SRV_POWER_CONTROL;
		ps.StateMask = SRV_POWER_CONTROL;
		const BOOL b = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof(ps));
		printf("throttle requested: %s\n", b ? "set" : "FAILED");
	}
	else if(strcmp(mode, "default"))
	{
		printf("unknown mode %s\n", mode);
		return 2;
	}
	unsigned long c = 0, s = 0, e = 0;
	SrvPowerQuery(GetCurrentProcess(), &c, &s, &e);
	printf("mode %s: process control 0x%lX state 0x%lX\n", mode, c, s);
	timeBeginPeriod(1);
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if(!timer)
	{
		printf("no high-resolution timer (error %lu)\n", GetLastError());
		return 2;
	}
	LARGE_INTEGER f, t0, now;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t0);
	long long secStart = t0.QuadPart, n = 0;
	int sec = 0;
	for(;;)
	{
		if(bSleep)
			Sleep(1);
		else
		{
			LARGE_INTEGER due;
			due.QuadPart = -5000;   // 0.5 ms, as shell_server.cpp's terrain pacing
			SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
			WaitForSingleObject(timer, 5);
		}
		++n;
		QueryPerformanceCounter(&now);
		if(now.QuadPart - secStart >= f.QuadPart)
		{
			printf("rate %s second %d: %lld Hz\n", mode, ++sec, n);
			fflush(stdout);
			n = 0;
			secStart = now.QuadPart;
			if(sec >= seconds)
				break;
		}
	}
	timeEndPeriod(1);
	CloseHandle(timer);
	return 0;
}

int main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if(argc >= 4 && !strcmp(argv[1], "--rate"))
		return Rate(argv[2], atof(argv[3]));
	return Unit();
}
