// srv_frame_jobs.cpp - frame jobs (ticket 0711, J1). Rules and what each part enforces: srv_frame_jobs.h.
// Written 2026-11-23 for this repository (GPL-3.0-or-later).
#include "srv_frame_jobs.h"
#include <stdio.h>
#include <algorithm>
#include <exception>
#include <xmmintrin.h>

namespace srvrender {

namespace {

double NowMs()
{
	static const double kMsPerTick = []() {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return 1000.0 / (double)f.QuadPart;
	}();
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart * kMsPerTick;
}

std::atomic<bool> g_ownerFatal{false};
std::atomic<uint64_t> g_ownerViolations{0};

std::mutex& LoudMu()
{
	static std::mutex mu;
	return mu;
}

std::vector<std::string>& LoudLines()
{
	static std::vector<std::string> lines;
	return lines;
}

// A LOUD line: stderr at once, kept for the host's log (at most 256 kept between two takes). Fatal: the process ends here.
void Loud(const std::string& line, bool fatal)
{
	fprintf(stderr, "%s\n", line.c_str());
	fflush(stderr);
	{
		std::lock_guard<std::mutex> lk(LoudMu());
		if(LoudLines().size() < 256)
			LoudLines().push_back(line);
	}
	if(fatal)
	{
		fprintf(stderr, "LOUD: frame jobs: FATAL (owner checks are fatal in this run); exit code %d\n", kFrameJobsFatalExit);
		fflush(stderr);
		fflush(stdout);
		TerminateProcess(GetCurrentProcess(), (UINT)kFrameJobsFatalExit);
	}
}

uint32_t XorShift(uint32_t& s)
{
	if(s == 0)
		s = 0x9E3779B9u;
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

}  // namespace

bool ParseFrameJobsKey(const std::string& value, unsigned logicalCores, int& workers, std::string& why)
{
	if(value == "auto")
	{
		workers = logicalCores <= 2 ? 0 : kFrameJobsAutoWorkers;
		return true;
	}
	if(value.empty() || value.size() > 2)
	{
		why = "auto, 0 or a number of workers 1.." + std::to_string(kFrameJobsMaxWorkers);
		return false;
	}
	int n = 0;
	for(char c : value)
	{
		if(c < '0' || c > '9')
		{
			why = "auto, 0 or a number of workers 1.." + std::to_string(kFrameJobsMaxWorkers);
			return false;
		}
		n = n * 10 + (c - '0');
	}
	if(n > kFrameJobsMaxWorkers)
	{
		why = "at most " + std::to_string(kFrameJobsMaxWorkers) + " workers";
		return false;
	}
	workers = n;
	return true;
}

unsigned FrameJobsLogicalCores()
{
	const DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	return n ? (unsigned)n : 1u;
}

void FrameJobsSetOwnerChecksFatal(bool fatal)
{
	g_ownerFatal.store(fatal);
}

bool FrameJobsOwnerChecksFatal()
{
	return g_ownerFatal.load();
}

uint64_t FrameJobsOwnerViolations()
{
	return g_ownerViolations.load();
}

std::vector<std::string> FrameJobsTakeLoudLines()
{
	std::lock_guard<std::mutex> lk(LoudMu());
	std::vector<std::string> out;
	out.swap(LoudLines());
	return out;
}

bool FrameJobOwner::mayCall(const char* what) const
{
	if(m_owned.load(std::memory_order_acquire) == 0)
		return true;
	if(m_runner.load(std::memory_order_acquire) == GetCurrentThreadId())
		return true;   // the owning job itself (a worker, or the render thread running it inline)
	g_ownerViolations.fetch_add(1);
	Loud(std::string("LOUD: frame jobs: ") + (what ? what : "?") + " called while a frame job owns the module " + m_module +
	         " (R2; ticket 0711)",
	     g_ownerFatal.load());
	return false;
}

bool FrameJobsOneWriter::claim(const char* where)
{
	const DWORD self = GetCurrentThreadId();
	DWORD expected = 0;
	if(m_thread.compare_exchange_strong(expected, self, std::memory_order_acq_rel) || expected == self)
	{
		m_depth++;
		return true;
	}
	m_violations.fetch_add(1);
	g_ownerViolations.fetch_add(1);
	Loud(std::string("LOUD: frame jobs: ") + (where ? where : "?") + " writes " + m_what + " while thread " + std::to_string(expected) +
	         " writes it (one writer at a time; MC #12834, ticket 0711)",
	     g_ownerFatal.load());
	return false;
}

void FrameJobsOneWriter::release()
{
	if(--m_depth == 0)
		m_thread.store(0, std::memory_order_release);
}

FrameJob::~FrameJob()
{
	if(m_pool)
		m_pool->join(*this);   // R3
}

FrameJobs::~FrameJobs()
{
	stop();
	std::vector<FrameJob*> left;
	{
		std::lock_guard<std::mutex> lk(m_mu);
		left = m_forked;
	}
	for(FrameJob* j : left)
		join(*j);   // R3: a forked job is never left behind (it runs inline here)
}

bool FrameJobs::start(const Options& o, std::string& why)
{
	stop();
	m_opt = o;
	m_quit = false;
	m_forkRng = o.seed ^ 0xA5A5A5A5u;
	const int n = (std::max)(0, (std::min)(o.workers, kFrameJobsMaxWorkers));
	const unsigned mxcsr = _mm_getcsr();   // R7
	int priority = GetThreadPriority(GetCurrentThread());   // the starter's (the render thread: ABOVE_NORMAL)
	if(priority == THREAD_PRIORITY_ERROR_RETURN)
		priority = THREAD_PRIORITY_NORMAL;
	for(int i = 0; i < n; ++i)
	{
		try
		{
			m_threads.emplace_back(&FrameJobs::workerMain, this, i, mxcsr, priority);
		}
		catch(const std::exception& e)
		{
			why = "frame jobs: worker " + std::to_string(i) + " not started (" + e.what() + "); " + std::to_string(m_threads.size()) +
			      " workers, every other job runs inline at its join";
			return false;
		}
	}
	return true;
}

void FrameJobs::stop()
{
	{
		std::lock_guard<std::mutex> lk(m_mu);
		m_quit = true;
	}
	m_cvWork.notify_all();
	for(std::thread& t : m_threads)
		if(t.joinable())
			t.join();
	m_threads.clear();
	std::lock_guard<std::mutex> lk(m_mu);
	m_queue.clear();   // the jobs stay forked (state kQueued): their joins claim them and run them inline
	m_quit = false;
}

void FrameJobs::jitterSleep(uint32_t& state)
{
	const uint32_t ms = XorShift(state) % (uint32_t)(kFrameJobsJitterMaxMs + 1);
	Sleep(ms);
}

void FrameJobs::fork(FrameJob& job, std::function<void(FrameJob&)> fn, std::initializer_list<FrameJobOwner*> owners)
{
	if(job.m_pool)
	{
		{
			std::lock_guard<std::mutex> lk(m_mu);
			m_counts.misuses++;
		}
		Loud(std::string("LOUD: frame jobs: job ") + job.m_name + " forked again before its join; joined first (R3; ticket 0711)", false);
		join(job);
	}
	job.m_fn = std::move(fn);
	job.m_lines.clear();
	job.m_result = FrameJobResult();
	job.m_threw = false;
	job.m_threwWhat.clear();
	job.m_jobMs = 0;
	job.m_ownerCount = 0;
	for(FrameJobOwner* ow : owners)
	{
		if(!ow)
			continue;
		if(job.m_ownerCount == 4)
		{
			Loud(std::string("LOUD: frame jobs: job ") + job.m_name + " names more than 4 owners; the rest are not checked", false);
			break;
		}
		job.m_owners[job.m_ownerCount++] = ow;
		ow->m_runner.store(0, std::memory_order_release);
		ow->m_owned.fetch_add(1, std::memory_order_acq_rel);   // R2: from here until the join
	}
	job.m_mxcsr = _mm_getcsr();   // R7
	job.m_forkThread = GetCurrentThreadId();
	job.m_pool = this;
	{
		std::lock_guard<std::mutex> lk(m_mu);
		job.m_state = FrameJob::kQueued;
		m_forked.push_back(&job);
		if(!m_threads.empty())
			m_queue.push_back(&job);
		m_counts.forks++;
	}
	if(!m_threads.empty())
		m_cvWork.notify_one();
	if(m_opt.jitter)
		jitterSleep(m_forkRng);   // R8: TEST only; changes when, never what
}

void FrameJobs::runFn(FrameJob& job)
{
	const DWORD self = GetCurrentThreadId();
	for(int i = 0; i < job.m_ownerCount; ++i)
		job.m_owners[i]->m_runner.store(self, std::memory_order_release);
	const double t0 = NowMs();
	try
	{
		if(job.m_fn)
			job.m_fn(job);
	}
	catch(const std::exception& e)
	{
		job.m_threw = true;
		job.m_threwWhat = e.what();
	}
	catch(...)
	{
		job.m_threw = true;
		job.m_threwWhat = "an exception that is not a std::exception";
	}
	job.m_jobMs = NowMs() - t0;
	for(int i = 0; i < job.m_ownerCount; ++i)
		job.m_owners[i]->m_runner.store(0, std::memory_order_release);
}

void FrameJobs::workerMain(int index, unsigned mxcsr, int priority)
{
	_mm_setcsr(mxcsr);   // R7: the pool's starter's
	SetThreadPriority(GetCurrentThread(), priority);   // and its priority (ticket 0711, 2026-11-24)
	typedef HRESULT(WINAPI * SetDesc)(HANDLE, PCWSTR);
	if(SetDesc f = (SetDesc)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))
	{
		wchar_t name[32];
		swprintf(name, 32, L"vp-frame-job-%d", index);
		f(GetCurrentThread(), name);
	}
	uint32_t rng = m_opt.seed * 2654435761u + (uint32_t)index * 40503u + 1u;
	std::unique_lock<std::mutex> lk(m_mu);
	for(;;)
	{
		m_cvWork.wait(lk, [this]() { return m_quit || !m_queue.empty(); });
		if(m_quit)
			break;   // queued jobs stay for their joins (R4)
		if(m_opt.jitter)
		{
			lk.unlock();
			jitterSleep(rng);   // R8: lets the joining thread claim first now and then
			lk.lock();
			if(m_quit)
				break;
			if(m_queue.empty())
				continue;
		}
		FrameJob* job = m_queue.front();
		m_queue.pop_front();
		job->m_state = FrameJob::kRunning;   // R4: claimed by this worker, under the lock
		m_counts.workerRuns++;
		lk.unlock();
		const unsigned own = _mm_getcsr();
		_mm_setcsr(job->m_mxcsr);   // R7: the forking thread's
		runFn(*job);
		_mm_setcsr(own);
		lk.lock();
		job->m_state = FrameJob::kDone;   // after this the worker does not touch the job
		m_cvDone.notify_all();
	}
}

const FrameJobResult& FrameJobs::join(FrameJob& job)
{
	if(!job.m_pool)
		return job.m_result;
	if(job.m_pool != this)
	{
		{
			std::lock_guard<std::mutex> lk(m_mu);
			m_counts.misuses++;
		}
		Loud(std::string("LOUD: frame jobs: job ") + job.m_name + " joined through another pool (ticket 0711)", g_ownerFatal.load());
		return job.m_pool->join(job);
	}
	if(GetCurrentThreadId() != job.m_forkThread)
	{
		{
			std::lock_guard<std::mutex> lk(m_mu);
			m_counts.misuses++;
		}
		Loud(std::string("LOUD: frame jobs: job ") + job.m_name + " joined on another thread than its fork (ticket 0711)",
		     g_ownerFatal.load());
	}
	const double t0 = NowMs();
	bool here = false;
	{
		std::unique_lock<std::mutex> lk(m_mu);
		if(job.m_state == FrameJob::kQueued)
		{
			// R4: no worker has claimed it: this thread does
			std::deque<FrameJob*>::iterator it = std::find(m_queue.begin(), m_queue.end(), &job);
			if(it != m_queue.end())
				m_queue.erase(it);
			job.m_state = FrameJob::kRunning;
			m_counts.inlineRuns++;
			here = true;
		}
		else
			m_cvDone.wait(lk, [&job]() { return job.m_state == FrameJob::kDone; });   // R3: no timeout
	}
	const double waited = here ? 0.0 : NowMs() - t0;
	if(here)
		runFn(job);   // the forking thread's MXCSR is this thread's
	FrameJobResult r;
	r.ran = true;
	r.inlineRun = here;
	r.jobMs = job.m_jobMs;
	r.joinWaitMs = waited;
	r.lines.swap(job.m_lines);
	if(job.m_threw)
	{
		r.failed = true;
		r.lines.push_back(std::string("LOUD: frame jobs: job ") + job.m_name + " threw: " + job.m_threwWhat + " (ticket 0711)");
	}
	for(int i = 0; i < job.m_ownerCount; ++i)
	{
		job.m_owners[i]->m_runner.store(0, std::memory_order_release);
		job.m_owners[i]->m_owned.fetch_sub(1, std::memory_order_acq_rel);   // R2: the render thread may call again
		job.m_owners[i] = nullptr;
	}
	job.m_ownerCount = 0;
	{
		std::lock_guard<std::mutex> lk(m_mu);
		job.m_state = FrameJob::kIdle;
		std::vector<FrameJob*>::iterator it = std::find(m_forked.begin(), m_forked.end(), &job);
		if(it != m_forked.end())
			m_forked.erase(it);
		if(job.m_threw)
			m_counts.failures++;
	}
	job.m_fn = nullptr;   // the closure's captures are released on the render thread
	job.m_pool = nullptr;
	job.m_result = std::move(r);
	return job.m_result;
}

FrameJobs::Counts FrameJobs::counts() const
{
	std::lock_guard<std::mutex> lk(m_mu);
	return m_counts;
}

}  // namespace srvrender
