// lk_late.h - how late a lockstep wait of shell_composite.dll was (issue 0150; verification 9055 V4,
// research/05-graphics-composition/renderer-in-server-verification.md 4.1).
//
// Two separate counts per stats interval, written into the composite's "lockstep 1s:" line:
//   late_start  the wait began at or after its deadline: the EXE reached the insertion more than the bound after the
//               UpdateViews publish. The wait loop then returns at once (vp_frame.h: never wait once the deadline has
//               passed), so this says the EXE was late, not the wait.
//   overrun     the wait ended more than `tolUs` (0.5 ms, the contract of vp_frame.h) after max(deadline, wait start):
//               a real overstay of the wait loop. overrun_max_us is the largest such overstay.
// Before 0150 a single `late` counter took wait end - deadline > 0.5 ms, which also counted every late start whose
// insertion came more than 0.5 ms after the deadline (3311 p1: 263 "late", of which 28 real overruns).
// Header-only and free of Windows types so that shell/tests/lk_late_test.cpp can test it alone.
#pragma once
#include <stdint.h>

namespace lklate
{
struct Counts
{
	int lateStart;          // waits that began at or after their deadline
	int overrun;            // waits that ended more than tolUs after max(deadline, start)
	double overrunMaxUs;    // the largest overstay among the overruns, us (0 if none)
};

// One bounded wait: wait0 / wait1 its start and end, deadline the publish's deadline, all in QPC ticks of `freq` per
// second. A wait without a deadline (deadline 0: lockstep off) or a clock without a frequency counts nothing.
inline void Count(Counts& c, int64_t wait0, int64_t wait1, int64_t deadline, int64_t freq, double tolUs = 500.0)
{
	if (deadline == 0 || freq <= 0) return;
	const bool lateStart = wait0 >= deadline;
	if (lateStart) c.lateStart++;
	const int64_t from = lateStart ? wait0 : deadline;
	const double overUs = (double)(wait1 - from) * 1e6 / (double)freq;
	if (overUs > tolUs)
	{
		c.overrun++;
		if (overUs > c.overrunMaxUs) c.overrunMaxUs = overUs;
	}
}
}  // namespace lklate
