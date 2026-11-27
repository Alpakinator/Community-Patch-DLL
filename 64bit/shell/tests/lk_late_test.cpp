// lk_late_test.cpp - unit test of shell/composite/lk_late.h (issue 0150): the lockstep wait's late starts and overruns
// are counted apart. Built by shell\build.cmd into bin\lk_late_test.exe (x86, as the composite; test program only), run
// by shell/tests/test_lk_late.py. Exit 0 and "lk_late_test: OK <n> checks" when every check holds, else 1 and the
// failed checks.
#include <stdio.h>
#include "lk_late.h"

static int g_checks, g_failed;

static void Check(bool ok, const char* what, int line)
{
	g_checks++;
	if (!ok) { g_failed++; printf("FAIL line %d: %s\n", line, what); }
}
#define CHECK(x) Check((x), #x, __LINE__)

static const int64_t F = 10000000;                  // QPC ticks per second (10 MHz, the usual value on Windows 10/11)
static int64_t Us(double us) { return (int64_t)(us * F / 1e6); }

int main()
{
	const int64_t D = 1000000000;                   // a deadline

	// 1. A wait that starts after the deadline (the EXE reached the insertion 3.7 ms after UpdateViews, 0.3 ms past the
	//    3.4-ms bound) and returns at once: a late start, no overrun. The pre-0150 counter (end - deadline > 0.5 ms)
	//    would have counted it as late: that is 9055's V4.
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D + Us(300), D + Us(302), D, F);
		CHECK(c.lateStart == 1);
		CHECK(c.overrun == 0);
		CHECK(c.overrunMaxUs == 0.0);
		lklate::Count(c, D + Us(2000), D + Us(2010), D, F);   // 2 ms past the deadline at the start: still no overrun
		CHECK(c.lateStart == 2 && c.overrun == 0);
	}
	// 2. A real overrun: the wait began before the deadline and ended 0.9 ms after it.
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D - Us(3000), D + Us(900), D, F);
		CHECK(c.lateStart == 0);
		CHECK(c.overrun == 1);
		CHECK(c.overrunMaxUs > 899.9 && c.overrunMaxUs < 900.1);
	}
	// 3. Both: a late start whose wait then took 0.7 ms (measured from its own start, not from the deadline).
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D + Us(1000), D + Us(1700), D, F);
		CHECK(c.lateStart == 1 && c.overrun == 1);
		CHECK(c.overrunMaxUs > 699.9 && c.overrunMaxUs < 700.1);
	}
	// 4. Within the contract: ended 0.4 ms after the deadline, or exactly 0.5 ms after it (the limit is "more than").
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D - Us(3400), D + Us(400), D, F);
		lklate::Count(c, D - Us(100), D + Us(500), D, F);
		lklate::Count(c, D - Us(3400), D - Us(1000), D, F);   // the frame came 1 ms before the deadline
		CHECK(c.lateStart == 0 && c.overrun == 0 && c.overrunMaxUs == 0.0);
	}
	// 5. A wait that starts exactly at the deadline is a late start (the loop sees now >= deadline and returns).
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D, D + Us(5), D, F);
		CHECK(c.lateStart == 1 && c.overrun == 0);
	}
	// 6. No deadline (lockstep off) or no clock frequency: nothing counted.
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D + Us(5000), D + Us(9000), 0, F);
		lklate::Count(c, D + Us(5000), D + Us(9000), D, 0);
		CHECK(c.lateStart == 0 && c.overrun == 0 && c.overrunMaxUs == 0.0);
	}
	// 7. The maximum is the largest overstay, kept over several waits; a custom tolerance.
	{
		lklate::Counts c = lklate::Counts();
		lklate::Count(c, D - Us(100), D + Us(950), D, F);
		lklate::Count(c, D - Us(100), D + Us(600), D, F);
		lklate::Count(c, D + Us(4000), D + Us(5100), D, F);   // late start + 1.1 ms overstay
		CHECK(c.overrun == 3 && c.lateStart == 1);
		CHECK(c.overrunMaxUs > 1099.9 && c.overrunMaxUs < 1100.1);
		lklate::Counts t = lklate::Counts();
		lklate::Count(t, D - Us(100), D + Us(200), D, F, 100.0);
		CHECK(t.overrun == 1);
	}
	// 8. 3311 p1's shape in miniature: 9 waits that reached the insertion after the deadline and returned at once, one
	//    real overrun. The old counter said 10 "late"; the new one says 9 late starts and 1 overrun.
	{
		lklate::Counts c = lklate::Counts();
		int oldLate = 0;
		for (int i = 0; i < 9; ++i)
		{
			int64_t w0 = D + Us(600 + 100 * i), w1 = w0 + Us(3);
			lklate::Count(c, w0, w1, D, F);
			if ((double)(w1 - D) * 1e6 / F > 500.0) oldLate++;
		}
		lklate::Count(c, D - Us(2000), D + Us(800), D, F);
		oldLate++;
		CHECK(oldLate == 10);
		CHECK(c.lateStart == 9 && c.overrun == 1);
	}

	if (g_failed) { printf("lk_late_test: %d of %d checks FAILED\n", g_failed, g_checks); return 1; }
	printf("lk_late_test: OK %d checks\n", g_checks);
	return 0;
}
