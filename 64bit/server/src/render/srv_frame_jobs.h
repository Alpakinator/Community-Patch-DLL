// srv_frame_jobs.h - frame jobs: CPU-only work of one drawing pass forked by the render thread once the frame's camera is
// fixed and joined by it before the work's first use in the same pass (ticket 0711, J1; epic 0686, D241, D242, D245).
// Written 2026-11-23 for this repository (GPL-3.0-or-later).
//
// Design: the Architect's advice research/07-architecture-decisions/architect-2026-11-23-frame-jobs.md sections 4 and 5
// (rules R1-R9). What this file enforces and what it leaves to its users (the research file
// research/05-graphics-composition/pan-coherence.md section 5.16 has the same list with the tests that prove it):
//
//   R1 frozen inputs     the user's: a job reads only what its closure captured at the fork (the camera and ONE clock value
//                        by value, the module it owns). The pool reads no clock that decides anything; job_ms and
//                        join_wait_ms are timing counters only.
//   R2 owned module      FrameJobOwner below: the fork marks every owner the job names, the join clears them after the job
//                        has ended. A module's render-thread entry points call mayCall("Class::fn"); while owned it is a
//                        violation: counted, written to stderr as a LOUD line, and FATAL (the process ends with exit code
//                        kFrameJobsFatalExit, no crash box) when FrameJobsSetOwnerChecksFatal(true) is in force (test
//                        builds and every run with a TEST key of the frame jobs).
//   R3 always joined     join() waits for the job however long it takes (no timeout, no older result); a FrameJob that is
//                        destroyed while forked joins itself; a second fork of an un-joined job joins it first (counted as a
//                        misuse, LOUD). A job that never ends is a hung server (0691's rule), not something this file hides.
//   R4 claim or inline   a job is claimed exactly once, under the pool's lock: by a worker, or by the joining render thread,
//                        which then runs it itself (inline). With 0 workers every job runs inline at its join: the serial
//                        cost of today, on the same code path.
//   R5 one sequence      the user's: one job runs its calls in today's order on one thread; the pool never splits a job.
//   R6 no context/IPC/log  the user's for the context and IPC; for the log, FrameJob::log collects lines in the job and the
//                        render thread writes them after the join, in order (FrameJobResult::lines).
//   R7 the same MXCSR    the fork stores the forking thread's MXCSR in the job and the worker runs the job under it (and
//                        restores its own after); every worker also starts with the MXCSR of the thread that started the pool,
//                        and with its scheduling priority (the render thread's ABOVE_NORMAL; on the 4-core laptop the begin
//                        ran 0.3-2 ms slower on a NORMAL worker than inline, 2026-11-24, pan-coherence 5.16.2; cause inferred).
//   R8 the same path     the players use the same FrameJobs with the same keys (render_frame_jobs, render_test_frame_jobs_jitter);
//                        the TEST jitter sleeps 0-5 ms at each fork (render thread, after the job is published) and before
//                        each worker's claim (so both the worker path and the inline path are taken).
//   R9 the frame's clock the host's (SceneFrame::nowMs at the pass's start); a job gets it by value in its closure.
//
// No D3D, no IPC, no log file here: plain threads, one mutex, two condition variables. Every server/src/render/*.cpp is
// compiled by every builder of the render units, so this file depends on the standard library and windows.h only.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace srvrender {

constexpr int kFrameJobsMaxWorkers = 8;      // render_frame_jobs=N: 0..8
constexpr int kFrameJobsAutoWorkers = 2;     // render_frame_jobs=auto on a machine with more than 2 logical cores
constexpr int kFrameJobsFatalExit = 0xF1;    // exit code of a fatal owner violation (R2, test builds)
constexpr int kFrameJobsJitterMaxMs = 5;     // render_test_frame_jobs_jitter: a sleep of 0..5 ms

// render_frame_jobs = auto | 0 | N (ticket 0711). auto: 0 on a machine with 2 logical cores or fewer, else
// kFrameJobsAutoWorkers. False (why) on anything else (empty, negative, above kFrameJobsMaxWorkers, trailing text).
bool ParseFrameJobsKey(const std::string& value, unsigned logicalCores, int& workers, std::string& why);
// The machine's logical cores as auto counts them (GetActiveProcessorCount over all groups).
unsigned FrameJobsLogicalCores();

// R2: process-wide. Fatal: a violation writes its LOUD line to stderr and ends the process with kFrameJobsFatalExit
// (TerminateProcess: no crash box, no WER dialog). Not fatal (the product): counted, the LOUD line kept for the host.
void FrameJobsSetOwnerChecksFatal(bool fatal);
bool FrameJobsOwnerChecksFatal();
uint64_t FrameJobsOwnerViolations();
// The LOUD lines of violations and misuses since the last call (the render thread writes them to its log).
std::vector<std::string> FrameJobsTakeLoudLines();

// R2: one per module a job may own (the units' host). The render thread calls mayCall at each entry point of the module.
class FrameJobOwner
{
public:
	explicit FrameJobOwner(const char* module) : m_module(module) {}
	FrameJobOwner(const FrameJobOwner&) = delete;
	FrameJobOwner& operator=(const FrameJobOwner&) = delete;
	// True when no job owns the module, or when the calling thread is the one running the owning job (a worker, or the
	// render thread running it inline at the join). False: a violation (counted, LOUD, fatal in test builds).
	bool mayCall(const char* what) const;
	bool owned() const { return m_owned.load(std::memory_order_acquire) != 0; }
	const char* module() const { return m_module; }

private:
	friend class FrameJobs;
	const char* m_module;
	std::atomic<int> m_owned{0};
	std::atomic<DWORD> m_runner{0};   // the thread running the owning job (0: not running yet)
};

// What the render thread reads after the join.
struct FrameJobResult
{
	bool ran = false;            // the job's function was called (false: never forked)
	bool inlineRun = false;      // R4: the joining thread claimed and ran it
	bool failed = false;         // the function threw (one LOUD line in lines)
	double jobMs = 0;            // the function's own time, on whichever thread ran it
	double joinWaitMs = 0;       // the joining thread's wait for a worker's run (0 when inline or already done)
	std::vector<std::string> lines;   // R6: FrameJob::log lines in order, then the LOUD line of a failure
};

class FrameJobs;

// One job slot. The user keeps it (a member of the module) and forks it once per pass.
class FrameJob
{
public:
	explicit FrameJob(const char* name) : m_name(name) {}
	FrameJob(const FrameJob&) = delete;
	FrameJob& operator=(const FrameJob&) = delete;
	~FrameJob();   // R3: joins a forked job

	// Inside the job only (R6): a line for the render thread's log, written after the join.
	void log(std::string line) { m_lines.push_back(std::move(line)); }
	bool forked() const { return m_pool != nullptr; }
	const char* name() const { return m_name; }
	// The last join's result (also after a second join, which changes nothing).
	const FrameJobResult& result() const { return m_result; }

private:
	friend class FrameJobs;
	enum State
	{
		kIdle = 0,       // never forked, or joined
		kQueued = 1,     // forked, in the pool's queue, not claimed
		kRunning = 2,    // claimed by a worker
		kDone = 3,       // a worker ran it; the join has not read it
	};
	const char* m_name;
	FrameJobs* m_pool = nullptr;   // set at the fork, cleared at the join
	State m_state = kIdle;          // under the pool's mutex
	std::function<void(FrameJob&)> m_fn;
	FrameJobOwner* m_owners[4] = {nullptr, nullptr, nullptr, nullptr};
	int m_ownerCount = 0;
	unsigned m_mxcsr = 0;           // R7: the forking thread's
	DWORD m_forkThread = 0;
	std::vector<std::string> m_lines;
	FrameJobResult m_result;
	bool m_threw = false;
	std::string m_threwWhat;
	double m_jobMs = 0;
};

// The pool. Made once by the host; fork and join on the render thread.
class FrameJobs
{
public:
	struct Options
	{
		int workers = 0;          // 0: every job inline at its join
		bool jitter = false;      // TEST: render_test_frame_jobs_jitter (R8's determinism proof)
		uint32_t seed = 0x0711;   // the jitter's sleeps (they change timing only, never a result)
	};
	FrameJobs() {}
	FrameJobs(const FrameJobs&) = delete;
	FrameJobs& operator=(const FrameJobs&) = delete;
	~FrameJobs();   // stop, then joins every job still forked (R3)

	// Starts the workers (R7: they take the calling thread's MXCSR). False (why) when a worker could not be made: the pool
	// then has the workers that did start (0 or more) and stays usable; every job not claimed runs inline (R4).
	bool start(const Options& o, std::string& why);
	// Ends the workers after the jobs they run. A job still queued stays forked: its join runs it inline.
	void stop();
	int workers() const { return (int)m_threads.size(); }
	bool jitter() const { return m_opt.jitter; }

	// Forks fn as job (R1: fn's captures are its inputs). The owners are marked owned until the join (R2). With workers the
	// job is queued for them; without, it waits for its join. A job forked again before its join is joined first (LOUD).
	void fork(FrameJob& job, std::function<void(FrameJob&)> fn, std::initializer_list<FrameJobOwner*> owners = {});
	// R3, R4: claims the job if no worker has (and runs it here), else waits for the worker's run. Clears the owners. A job
	// that is not forked returns its last result unchanged. Same thread as the fork (else a misuse, LOUD).
	const FrameJobResult& join(FrameJob& job);

	// Counters since start (render thread).
	struct Counts
	{
		uint64_t forks = 0, inlineRuns = 0, workerRuns = 0, failures = 0, misuses = 0;
	};
	Counts counts() const;

private:
	void workerMain(int index, unsigned mxcsr, int priority);
	static void runFn(FrameJob& job);
	void jitterSleep(uint32_t& state);

	Options m_opt;
	std::vector<std::thread> m_threads;
	mutable std::mutex m_mu;
	std::condition_variable m_cvWork;   // a job queued, or stop
	std::condition_variable m_cvDone;   // a worker finished a job
	std::deque<FrameJob*> m_queue;
	std::vector<FrameJob*> m_forked;    // every job forked and not joined (the destructor joins them)
	bool m_quit = false;
	uint32_t m_forkRng = 0;             // render thread's jitter state
	Counts m_counts;                    // under m_mu
};

// MC #12834 (ticket 0711): a resource with ONE writer thread at a time that a frame job and the render thread write in
// turn (route S's trigger section, "the server's one writer thread only": a job's controller frame writes records, the
// render thread's head polls). claim() takes it for the calling thread (the same thread may claim again, nested); a claim
// while ANOTHER thread holds it is a violation: counted, LOUD, fatal when FrameJobsSetOwnerChecksFatal(true) is in force.
// The pool's fork and join are the hand-overs (their mutex orders the writes), so a correct user never sees one.
class FrameJobsOneWriter
{
public:
	explicit FrameJobsOneWriter(const char* what) : m_what(what) {}
	FrameJobsOneWriter(const FrameJobsOneWriter&) = delete;
	FrameJobsOneWriter& operator=(const FrameJobsOneWriter&) = delete;
	// True: claimed (call release() once); false: a violation (nothing claimed, release() must not be called).
	bool claim(const char* where);
	void release();
	uint64_t violations() const { return m_violations.load(); }
	const char* what() const { return m_what; }
	class Scope
	{
	public:
		Scope(FrameJobsOneWriter& w, const char* where) : m_w(w), m_held(w.claim(where)) {}
		~Scope()
		{
			if(m_held)
				m_w.release();
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		FrameJobsOneWriter& m_w;
		bool m_held;
	};

private:
	const char* m_what;
	std::atomic<DWORD> m_thread{0};
	int m_depth = 0;   // the holder's nesting (only the holder touches it)
	std::atomic<uint64_t> m_violations{0};
};

// R3 on the error paths: joins the job when the scope ends, unless join() was called (the result stays in the job).
class FrameJobJoinGuard
{
public:
	FrameJobJoinGuard(FrameJobs& pool, FrameJob& job) : m_pool(pool), m_job(job) {}
	~FrameJobJoinGuard()
	{
		if(m_job.forked())
			m_pool.join(m_job);
	}
	FrameJobJoinGuard(const FrameJobJoinGuard&) = delete;
	FrameJobJoinGuard& operator=(const FrameJobJoinGuard&) = delete;

private:
	FrameJobs& m_pool;
	FrameJob& m_job;
};

}  // namespace srvrender
