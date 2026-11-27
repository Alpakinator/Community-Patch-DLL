// frame_jobs_test.cpp - tests of the frame-jobs pool (ticket 0711, J1; server/src/render/srv_frame_jobs.{h,cpp}).
// Written 2026-11-23 for this repository (GPL-3.0-or-later).
//
//   frame_jobs_test.exe              every in-process test; last line "frame_jobs_test: OK" and exit 0, else
//                                    "frame_jobs_test: FAILED: <n> checks" and exit 1
//   frame_jobs_test.exe --child X    one negative case for test_frame_jobs.py (its exit code and stderr are the result):
//       owner-fatal       R2 with fatal checks: the render thread calls a module a job owns -> exit 0xF1, a LOUD line
//       owner-counted     R2 without fatal checks: the call is refused, counted, LOUD on stderr, exit 0
//       join-thread       a join on another thread than the fork, fatal checks -> exit 0xF1
//       writer-fatal      MC #12834: a second thread claims a FrameJobsOneWriter another holds, fatal checks -> exit 0xF1
//
// What each test proves is named at its head with the rule (R1-R9 of the Architect's frame-jobs advice, section 4).
#include "srv_frame_jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <xmmintrin.h>

using namespace srvrender;

static int g_fails = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                                                   \
	do                                                                                                                \
	{                                                                                                                 \
		++g_checks;                                                                                                   \
		if(!(cond))                                                                                                   \
		{                                                                                                             \
			++g_fails;                                                                                                \
			fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
		}                                                                                                             \
	} while(0)

// ---- A stand-in for the units' begin: a stateful module the job owns (memo with eviction, float arithmetic) ----
// Each frame mutates the memo, reads a camera and a clock handed by value (R1), and produces an output hash. The same
// call sequence on any thread must give the same hashes (R5), under the forking thread's MXCSR (R7).
struct Module
{
	FrameJobOwner owner{"test-module"};
	std::map<uint32_t, float> memo;   // grows and evicts like the pose memo
	uint64_t outHash = 0;
	std::vector<float> rows;

	void begin(int frame, float cam, double clockMs)
	{
		rows.assign(512, 0.0f);
		for(int i = 0; i < 512; ++i)
		{
			const uint32_t key = (uint32_t)((frame * 7 + i * 13) % 977);
			std::map<uint32_t, float>::iterator it = memo.find(key);
			float v;
			if(it != memo.end())
				v = it->second;
			else
			{
				// a denormal on purpose: under FTZ/DAZ it is flushed, without it it is kept (R7's check)
				v = 1.0e-39f * (float)(key + 1) * (0.5f + cam);
				memo[key] = v;
			}
			const float x = v * 1.0e3f + (float)clockMs * 1.0e-9f + cam;
			rows[i] = (i & 1) ? v * 0.75f : x;   // odd rows keep the denormal itself (0 under FTZ/DAZ)
		}
		while(memo.size() > 600)   // eviction order depends on the call sequence
			memo.erase(memo.begin());
		uint64_t h = 1469598103934665603ull;
		for(float f : rows)
		{
			uint32_t u;
			memcpy(&u, &f, 4);
			h = (h ^ u) * 1099511628211ull;
		}
		h = (h ^ memo.size()) * 1099511628211ull;
		outHash = h;
	}
};

static void SpinUs(int us)
{
	LARGE_INTEGER f, t0, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t0);
	do
		QueryPerformanceCounter(&t);
	while((t.QuadPart - t0.QuadPart) * 1000000 < (LONGLONG)us * f.QuadPart);
}

static std::vector<uint64_t> RunSeries(int workers, bool jitter, int frames, unsigned mxcsr, FrameJobs::Counts* counts)
{
	const unsigned old = _mm_getcsr();
	_mm_setcsr(mxcsr);
	std::vector<uint64_t> out;
	Module m;
	if(workers < 0)
	{
		for(int f = 0; f < frames; ++f)
		{
			m.begin(f, 0.25f * (float)(f % 9), 1000.0 + 16.6 * f);
			out.push_back(m.outHash);
		}
		_mm_setcsr(old);
		return out;
	}
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = workers;
	o.jitter = jitter;
	std::string why;
	CHECK(pool.start(o, why));
	FrameJob job("test-begin");
	for(int f = 0; f < frames; ++f)
	{
		const float cam = 0.25f * (float)(f % 9);
		const double clock = 1000.0 + 16.6 * f;   // R9: one clock value per frame, by value
		Module* mp = &m;
		pool.fork(job, [mp, f, cam, clock](FrameJob&) { mp->begin(f, cam, clock); }, {&m.owner});
		CHECK(m.owner.owned());
		if(workers > 0 && !jitter)
			SpinUs(300);   // the render thread's own work in the window (slot, predraw, the head): the worker claims
		const FrameJobResult& r = pool.join(job);
		CHECK(r.ran);
		CHECK(!m.owner.owned());
		out.push_back(m.outHash);
	}
	if(counts)
		*counts = pool.counts();
	pool.stop();
	_mm_setcsr(old);
	return out;
}

// R4, R5, R7, R8: serial, jobs 0, 1, 2 and 2 + jitter give the same per-frame hashes under FTZ|DAZ set by the render thread
static void TestDeterminism()
{
	const unsigned ftz = (_mm_getcsr() | _MM_FLUSH_ZERO_ON | 0x0040u /* DAZ */);
	const int frames = 400;
	const std::vector<uint64_t> serial = RunSeries(-1, false, frames, ftz, nullptr);
	FrameJobs::Counts c0, c1, c2, cj;
	const std::vector<uint64_t> j0 = RunSeries(0, false, frames, ftz, &c0);
	const std::vector<uint64_t> j1 = RunSeries(1, false, frames, ftz, &c1);
	const std::vector<uint64_t> j2 = RunSeries(2, false, frames, ftz, &c2);
	const std::vector<uint64_t> jj = RunSeries(2, true, 150, ftz, &cj);
	CHECK(j0 == serial);
	CHECK(j1 == serial);
	CHECK(j2 == serial);
	CHECK(jj.size() == 150 && std::equal(jj.begin(), jj.end(), serial.begin()));
	CHECK(c0.forks == (uint64_t)frames && c0.inlineRuns == (uint64_t)frames && c0.workerRuns == 0);
	CHECK(c1.forks == (uint64_t)frames && c1.inlineRuns + c1.workerRuns == (uint64_t)frames);
	CHECK(cj.inlineRuns + cj.workerRuns == 150);
	printf("determinism: jobs 1: %llu worker, %llu inline; jobs 2: %llu worker, %llu inline; jobs 2 + jitter: %llu worker, %llu inline\n",
	       (unsigned long long)c1.workerRuns, (unsigned long long)c1.inlineRuns, (unsigned long long)c2.workerRuns,
	       (unsigned long long)c2.inlineRuns, (unsigned long long)cj.workerRuns, (unsigned long long)cj.inlineRuns);
	// with a window of 300 us (the pass's is 1.7-2.5 ms) the workers take most jobs (the worker path is what jobs 1 proves)
	CHECK(c1.workerRuns > (uint64_t)frames / 2);
	CHECK(c2.workerRuns > (uint64_t)frames / 2);
	// the jitter takes both paths (R8's proof exercises the claim at the join as well as the worker's)
	CHECK(cj.workerRuns > 0);
	CHECK(cj.inlineRuns > 0);
	// sensitivity: the same series without FTZ/DAZ differs, so the equality above did depend on the MXCSR reaching the job
	const unsigned plain = (_mm_getcsr() & ~(unsigned)(_MM_FLUSH_ZERO_ON | 0x0040u));
	const std::vector<uint64_t> noftz = RunSeries(-1, false, 50, plain, nullptr);
	CHECK(!std::equal(noftz.begin(), noftz.end(), serial.begin()));
}

// R7: the job on a worker sees the forking thread's MXCSR (set after the pool started), and the worker gets its own back
static void TestMxcsr()
{
	const unsigned old = _mm_getcsr();
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 1;
	std::string why;
	CHECK(pool.start(o, why));
	const unsigned want = (old & ~(unsigned)_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | _MM_FLUSH_ZERO_ON | 0x0040u;
	_mm_setcsr(want);
	int workerSeen = 0;
	for(int i = 0; i < 50; ++i)
	{
		FrameJob job("mxcsr");
		unsigned seen = 0;
		DWORD tid = 0;
		pool.fork(job, [&seen, &tid](FrameJob&) {
			seen = _mm_getcsr();
			tid = GetCurrentThreadId();
		});
		Sleep(i % 3);
		const FrameJobResult& r = pool.join(job);
		CHECK((seen & 0xFFC0u) == (want & 0xFFC0u));   // the control bits (the flags below 0x40 are sticky status)
		if(!r.inlineRun)
		{
			CHECK(tid != GetCurrentThreadId());
			workerSeen++;
		}
	}
	_mm_setcsr(old);
	// the worker restores its own MXCSR after a job: a job forked under the default runs under the default
	FrameJob job("mxcsr-back");
	unsigned seen = 0;
	pool.fork(job, [&seen](FrameJob&) { seen = _mm_getcsr(); });
	Sleep(2);
	pool.join(job);
	CHECK((seen & 0xFFC0u) == (old & 0xFFC0u));
	printf("mxcsr: %d of 50 jobs ran on the worker\n", workerSeen);
}

// R6: lines logged in the job come back at the join, in order; nothing is lost when the worker runs it
static void TestLines()
{
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 2;
	std::string why;
	CHECK(pool.start(o, why));
	for(int k = 0; k < 20; ++k)
	{
		FrameJob job("lines");
		pool.fork(job, [k](FrameJob& j) {
			for(int i = 0; i < 5; ++i)
				j.log("line " + std::to_string(k) + "." + std::to_string(i));
		});
		const FrameJobResult& r = pool.join(job);
		CHECK(r.lines.size() == 5);
		for(int i = 0; i < 5 && i < (int)r.lines.size(); ++i)
			CHECK(r.lines[i] == "line " + std::to_string(k) + "." + std::to_string(i));
		CHECK(!r.failed);
	}
}

// R3: a throwing job is joined, reported LOUD in its lines, counted; the pool goes on
static void TestThrow()
{
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 1;
	std::string why;
	CHECK(pool.start(o, why));
	FrameJobOwner ow("throwing-module");
	FrameJob job("thrower");
	pool.fork(job, [](FrameJob& j) {
		j.log("before");
		throw std::runtime_error("boom");
	}, {&ow});
	const FrameJobResult& r = pool.join(job);
	CHECK(r.ran && r.failed);
	CHECK(r.lines.size() == 2 && r.lines[0] == "before" && r.lines[1].find("LOUD") == 0 && r.lines[1].find("boom") != std::string::npos);
	CHECK(!ow.owned());
	CHECK(pool.counts().failures == 1);
	int ran = 0;
	pool.fork(job, [&ran](FrameJob&) { ran++; });
	CHECK(!pool.join(job).failed);
	CHECK(ran == 1);
}

// R3: join is idempotent; a job never forked returns ran = false; a job destroyed while forked is joined (runs once); a
// fork of an un-joined job joins it first (misuse counted); a pool destroyed with a forked job joins it
static void TestAlwaysJoined()
{
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 1;
	std::string why;
	CHECK(pool.start(o, why));
	{
		FrameJob never("never");
		CHECK(!pool.join(never).ran);
	}
	int runs = 0;
	{
		FrameJob scoped("scoped");
		pool.fork(scoped, [&runs](FrameJob&) {
			Sleep(3);
			runs++;
		});
	}   // ~FrameJob joins
	CHECK(runs == 1);
	{
		FrameJob twice("twice");
		pool.fork(twice, [&runs](FrameJob&) { runs++; });
		pool.join(twice);
		const FrameJobResult& r2 = pool.join(twice);
		CHECK(r2.ran);
		CHECK(runs == 2);
	}
	{
		FrameJob again("again");
		pool.fork(again, [&runs](FrameJob&) { runs++; });
		pool.fork(again, [&runs](FrameJob&) { runs += 10; });   // joins the first one, LOUD
		pool.join(again);
		CHECK(runs == 13);
		CHECK(pool.counts().misuses == 1);
	}
	{
		// the scope guard on an error path
		FrameJob guarded("guarded");
		bool done = false;
		{
			FrameJobJoinGuard g(pool, guarded);
			pool.fork(guarded, [&done](FrameJob&) { done = true; });
		}
		CHECK(done && !guarded.forked());
	}
	{
		// a pool destroyed with a job still forked and queued: the job runs inline in the destructor
		int late = 0;
		FrameJob j("late");
		{
			FrameJobs p0;
			FrameJobs::Options o0;
			o0.workers = 0;
			CHECK(p0.start(o0, why));
			p0.fork(j, [&late](FrameJob&) { late++; });
		}
		CHECK(late == 1 && !j.forked());
	}
	{
		// stop with a job queued: its join runs it inline
		FrameJobs p1;
		FrameJobs::Options o1;
		o1.workers = 1;
		CHECK(p1.start(o1, why));
		FrameJob blocker("blocker"), queued("queued");
		std::atomic<int> gate{0};
		p1.fork(blocker, [&gate](FrameJob&) {
			while(gate.load() == 0)
				Sleep(1);
		});
		Sleep(5);   // the worker claims the blocker
		p1.fork(queued, [](FrameJob&) {});
		gate.store(1);
		p1.stop();
		CHECK(p1.join(blocker).ran);
		const FrameJobResult& rq = p1.join(queued);
		CHECK(rq.ran);
	}
	FrameJobsTakeLoudLines();
}

// R2 and R4: while queued (not claimed) the render thread may not call the module; during an inline run at the join the
// render thread is the runner and may; during a worker's run the render thread may not; after the join it may.
static void TestOwnerChecks()
{
	FrameJobsSetOwnerChecksFatal(false);
	const uint64_t v0 = FrameJobsOwnerViolations();
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 0;
	std::string why;
	CHECK(pool.start(o, why));
	FrameJobOwner ow("units");
	FrameJob job("owner");
	bool innerOk = false;
	FrameJobOwner* owp = &ow;
	pool.fork(job, [owp, &innerOk](FrameJob&) { innerOk = owp->mayCall("inside the job"); }, {&ow});
	CHECK(!ow.mayCall("UnitsHost::drawPass (test, queued)"));   // a violation, counted
	pool.join(job);
	CHECK(innerOk);   // the inline runner may call its own module
	CHECK(ow.mayCall("UnitsHost::drawPass (test, after the join)"));
	CHECK(FrameJobsOwnerViolations() == v0 + 1);
	// a worker runs it: the render thread is refused while it runs
	FrameJobs p1;
	FrameJobs::Options o1;
	o1.workers = 1;
	CHECK(p1.start(o1, why));
	std::atomic<int> started{0}, release{0};
	p1.fork(job, [&started, &release](FrameJob&) {
		started.store(1);
		while(release.load() == 0)
			Sleep(1);
	}, {&ow});
	for(int i = 0; i < 2000 && started.load() == 0; ++i)
		Sleep(1);
	CHECK(started.load() == 1);
	CHECK(!ow.mayCall("UnitsHost::drawPass (test, worker running)"));
	release.store(1);
	p1.join(job);
	CHECK(ow.mayCall("after"));
	CHECK(FrameJobsOwnerViolations() == v0 + 2);
	const std::vector<std::string> loud = FrameJobsTakeLoudLines();
	CHECK(loud.size() == 2 && loud[0].find("LOUD: frame jobs: UnitsHost::drawPass") == 0);
}

// R1/R3 timing counters: a worker's run shows join_wait, an inline run shows job_ms and no wait
static void TestCounters()
{
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 1;
	std::string why;
	CHECK(pool.start(o, why));
	FrameJob job("timed");
	int workerCases = 0;
	for(int i = 0; i < 10; ++i)
	{
		pool.fork(job, [](FrameJob&) { Sleep(40); });
		Sleep(3);   // up to one timer tick (15.6 ms): the worker's run still has 20 ms or more to go
		const FrameJobResult& r = pool.join(job);
		CHECK(r.jobMs >= 30.0);
		if(r.inlineRun)
			CHECK(r.joinWaitMs == 0.0);
		else
		{
			workerCases++;
			CHECK(r.joinWaitMs >= 5.0);
		}
	}
	CHECK(workerCases > 0);
	FrameJobs p0;
	FrameJobs::Options o0;
	CHECK(p0.start(o0, why));
	p0.fork(job, [](FrameJob&) { Sleep(5); });
	const FrameJobResult& r0 = p0.join(job);
	CHECK(r0.inlineRun && r0.joinWaitMs == 0.0 && r0.jobMs >= 3.0);
}

// render_frame_jobs = auto | 0 | N, and its refusals (the negative cases)
static void TestKey()
{
	int w = -1;
	std::string why;
	CHECK(ParseFrameJobsKey("auto", 1, w, why) && w == 0);
	CHECK(ParseFrameJobsKey("auto", 2, w, why) && w == 0);
	CHECK(ParseFrameJobsKey("auto", 3, w, why) && w == kFrameJobsAutoWorkers);
	CHECK(ParseFrameJobsKey("auto", 32, w, why) && w == kFrameJobsAutoWorkers);
	CHECK(ParseFrameJobsKey("0", 8, w, why) && w == 0);
	CHECK(ParseFrameJobsKey("1", 8, w, why) && w == 1);
	CHECK(ParseFrameJobsKey("8", 8, w, why) && w == 8);
	const char* bad[] = {"", "-1", "9", "x", "2x", " 1", "1 ", "AUTO", "100", "1.5"};
	for(const char* b : bad)
	{
		w = 77;
		why.clear();
		CHECK(!ParseFrameJobsKey(b, 8, w, why));
		CHECK(w == 77 && !why.empty());
	}
	CHECK(FrameJobsLogicalCores() >= 1);
}

// many short jobs under ASan: the job objects are freed right after their joins (a worker touching a job after kDone, or
// a join returning before the worker let go, shows here as a use after free)
static void TestChurn()
{
	FrameJobs pool;
	FrameJobs::Options o;
	o.workers = 2;
	o.jitter = false;
	std::string why;
	CHECK(pool.start(o, why));
	uint64_t sum = 0;
	for(int i = 0; i < 3000; ++i)
	{
		FrameJob* j = new FrameJob("churn");
		std::vector<int>* data = new std::vector<int>(64, i);
		pool.fork(*j, [data](FrameJob& self) {
			int s = 0;
			for(int v : *data)
				s += v;
			self.log(std::to_string(s));
		});
		const FrameJobResult& r = pool.join(*j);
		sum += (uint64_t)atoi(r.lines.empty() ? "0" : r.lines[0].c_str());
		delete j;
		delete data;
	}
	uint64_t want = 0;
	for(int i = 0; i < 3000; ++i)
		want += (uint64_t)(64 * i);
	CHECK(sum == want);
}

// MC #12834: route S's trigger section has one writer at a time. The render thread writes (the head), forks a job that
// writes (the controller's frame) and joins: the fork and join hand the writer over, never a violation, with workers 0, 1
// and 2 and the jitter. Two threads writing at once: the second claim is a violation (counted, LOUD), and the first
// holder keeps it; a nested claim by the holder is not one.
static void TestOneWriter()
{
	FrameJobsSetOwnerChecksFatal(false);
	FrameJobsTakeLoudLines();
	FrameJobsOneWriter w("the test section");
	const int arms[4][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}};
	for(const auto& arm : arms)
	{
		FrameJobs pool;
		FrameJobs::Options o;
		o.workers = arm[0];
		o.jitter = arm[1] != 0;
		std::string why;
		CHECK(pool.start(o, why));
		FrameJob job("writer");
		uint64_t written = 0;
		for(int f = 0; f < (arm[1] ? 40 : 300); ++f)
		{
			{
				FrameJobsOneWriter::Scope s(w, "head");
				written++;
			}
			FrameJobsOneWriter* wp = &w;
			uint64_t* np = &written;
			pool.fork(job, [wp, np](FrameJob&) {
				FrameJobsOneWriter::Scope s(*wp, "body");
				FrameJobsOneWriter::Scope nested(*wp, "body, nested");
				SpinUs(50);
				(*np)++;
			});
			SpinUs(100);
			pool.join(job);
		}
		CHECK(w.violations() == 0);
		CHECK(written == (uint64_t)(arm[1] ? 80 : 600));
	}
	std::atomic<int> held{0}, release{0};
	std::thread a([&]() {
		FrameJobsOneWriter::Scope s(w, "thread a");
		held.store(1);
		while(release.load() == 0)
			Sleep(1);
	});
	for(int i = 0; i < 2000 && held.load() == 0; ++i)
		Sleep(1);
	CHECK(held.load() == 1);
	CHECK(!w.claim("thread b, at once"));   // the second writer
	CHECK(w.violations() == 1);
	release.store(1);
	a.join();
	CHECK(w.claim("after"));
	w.release();
	CHECK(w.violations() == 1);
	const std::vector<std::string> loud = FrameJobsTakeLoudLines();
	CHECK(loud.size() == 1 && loud[0].find("LOUD: frame jobs: thread b, at once writes the test section while thread") == 0);
}

// R7's companion (2026-11-24): a worker runs at the priority of the thread that started the pool (the render thread's
// ABOVE_NORMAL), and the job runs on the worker (not inline)
static void TestPriority()
{
	const int old = GetThreadPriority(GetCurrentThread());
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	{
		FrameJobs pool;
		FrameJobs::Options o;
		o.workers = 1;
		std::string why;
		CHECK(pool.start(o, why));
		FrameJob job("priority");
		int seen = -99;
		DWORD tid = 0;
		std::atomic<int> started{0};
		pool.fork(job, [&seen, &tid, &started](FrameJob&) {
			seen = GetThreadPriority(GetCurrentThread());
			tid = GetCurrentThreadId();
			started.store(1);
		});
		for(int i = 0; i < 2000 && started.load() == 0; ++i)
			Sleep(1);
		pool.join(job);
		CHECK(tid != 0 && tid != GetCurrentThreadId());
		CHECK(seen == THREAD_PRIORITY_ABOVE_NORMAL);
	}
	SetThreadPriority(GetCurrentThread(), old);
}

static int Child(const char* which)
{
	if(!strcmp(which, "writer-fatal"))
	{
		FrameJobsSetOwnerChecksFatal(true);
		FrameJobsOneWriter w("the child section");
		std::atomic<int> held{0};
		std::thread a([&]() {
			w.claim("child thread a");   // held until the process ends
			held.store(1);
			Sleep(INFINITE);
		});
		a.detach();
		while(held.load() == 0)
			Sleep(1);
		w.claim("child thread b");   // fatal: the process ends here
		printf("child writer-fatal: not fatal\n");
		return 2;
	}
	if(!strcmp(which, "owner-fatal") || !strcmp(which, "owner-counted"))
	{
		const bool fatal = !strcmp(which, "owner-fatal");
		FrameJobsSetOwnerChecksFatal(fatal);
		FrameJobs pool;
		FrameJobs::Options o;
		o.workers = 0;
		std::string why;
		pool.start(o, why);
		FrameJobOwner ow("units");
		FrameJob job("child");
		pool.fork(job, [](FrameJob&) {}, {&ow});
		const bool ok = ow.mayCall("UnitsHost::drawPass (child)");   // fatal: the process ends here
		pool.join(job);
		printf("child %s: mayCall %s, violations %llu\n", which, ok ? "allowed" : "refused", (unsigned long long)FrameJobsOwnerViolations());
		return ok ? 2 : 0;
	}
	if(!strcmp(which, "join-thread"))
	{
		FrameJobsSetOwnerChecksFatal(true);
		FrameJobs pool;
		FrameJobs::Options o;
		o.workers = 0;
		std::string why;
		pool.start(o, why);
		FrameJob job("child");
		pool.fork(job, [](FrameJob&) {});
		std::thread t([&pool, &job]() { pool.join(job); });   // fatal: the process ends here
		t.join();
		printf("child join-thread: not fatal\n");
		return 2;
	}
	fprintf(stderr, "frame_jobs_test: unknown child %s\n", which);
	return 64;
}

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(argc == 3 && !strcmp(argv[1], "--child"))
		return Child(argv[2]);
	if(argc != 1)
	{
		fprintf(stderr, "usage: frame_jobs_test.exe [--child owner-fatal|owner-counted|join-thread|writer-fatal]\n");
		return 64;
	}
	TestKey();
	TestLines();
	TestThrow();
	TestAlwaysJoined();
	TestOwnerChecks();
	TestOneWriter();
	TestPriority();
	TestMxcsr();
	TestCounters();
	TestChurn();
	TestDeterminism();
	if(g_fails)
	{
		printf("frame_jobs_test: FAILED: %d of %d checks\n", g_fails, g_checks);
		return 1;
	}
	printf("frame_jobs_test: OK (%d checks)\n", g_checks);
	return 0;
}
