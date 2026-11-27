// vp_pacing.h - the frame-pacing instruments shared by the composite (x86, inside the EXE) and the renderer inside
// server.exe (x64). Ticket 0688, epic 0686 P1 (decision D241); the measures F1-F6 are defined in
// research/07-architecture-decisions/architect-2026-11-22-frame-pacing-epic.md section 3, the instruments and their lines
// in research/08-performance/frame-pacing-instruments.md.
//
// Header only, no allocation, no lock, no log: the owner of each object calls it from one thread and writes the lines
// itself (the composite: its control thread; the server: through RLog, which queues to the log thread of ticket 5054).
//
//   Hist          a run-long histogram of durations (ms): 0.1 ms buckets below 100 ms, 1 ms below 1 s, 10 ms below
//                 10 s, one overflow bucket; each bucket keeps its count and its sum, so the "lows" are exact means.
//   Summary       F1/F2/F4's statistics of a Hist: p50, p99, p99.9, max, mean, 1 % and 0.1 % low (as fps: 1000 / the
//                 mean of the slowest 1 % / 0.1 %), counts over 2x the median, 50 ms and 100 ms, and coarse bins.
//   WorldTracker  per presented frame: whether it showed a new world picture (a world update), the interval since the
//                 previous one, the world picture's age, and the freezes (a run of presented frames without a world
//                 update while the camera moves; its length = the time between the two world updates around it).
//   PassTimer     the server's per-pass wall time per stage; StageScope switches the stage for a scope (nested stages:
//                 a bake inside the draw), through the calling thread's current timer (CurrentPass).
#pragma once
#include <stdint.h>
#include <string.h>

namespace vppacing {

// ---- Hist -----------------------------------------------------------------------------------------------------------
struct Hist
{
	static const int kA = 1000;   // [0, 100) ms by 0.1 ms
	static const int kB = 900;    // [100, 1000) ms by 1 ms
	static const int kC = 900;    // [1000, 10000) ms by 10 ms
	static const int kN = kA + kB + kC + 1;   // + [10000, inf)

	uint32_t n[kN];
	double sum[kN];
	uint64_t count;
	double total, maxMs;

	void reset() { memset(this, 0, sizeof(*this)); }

	static int bucket(double ms)
	{
		if (!(ms > 0.0)) return 0;   // negative, zero and NaN go to the first bucket
		if (ms < 100.0) { const int b = (int)(ms * 10.0); return b < kA ? b : kA - 1; }
		if (ms < 1000.0) { const int b = kA + (int)(ms - 100.0); return b < kA + kB ? b : kA + kB - 1; }
		if (ms < 10000.0) { const int b = kA + kB + (int)((ms - 1000.0) / 10.0); return b < kN - 1 ? b : kN - 2; }
		return kN - 1;
	}
	static double lower(int b)
	{
		if (b < kA) return b * 0.1;
		if (b < kA + kB) return 100.0 + (b - kA);
		if (b < kN - 1) return 1000.0 + (b - kA - kB) * 10.0;
		return 10000.0;
	}
	static double upper(int b) { return b < kN - 1 ? lower(b + 1) : 1e300; }

	void add(double ms)
	{
		if (ms < 0.0 || ms != ms) ms = 0.0;
		const int b = bucket(ms);
		n[b]++;
		sum[b] += ms;
		count++;
		total += ms;
		if (ms > maxMs) maxMs = ms;
	}

	// The upper edge of the bucket that holds the sample of rank ceil(p * count) (p in 0..1), never above the maximum:
	// an upper bound of the true quantile, at most one bucket width above it (0.1 ms below 100 ms).
	double percentile(double p) const
	{
		if (!count) return 0.0;
		uint64_t rank = (uint64_t)(p * (double)count + 0.999999);
		if (rank < 1) rank = 1;
		if (rank > count) rank = count;
		uint64_t c = 0;
		for (int b = 0; b < kN; ++b)
		{
			c += n[b];
			if (c >= rank) { const double u = upper(b); return u < maxMs ? u : maxMs; }
		}
		return maxMs;
	}

	// The mean of the slowest `frac` of the samples (at least one sample): whole buckets from the top by their exact sums,
	// the last bucket by its own mean.
	double slowMeanMs(double frac) const
	{
		if (!count) return 0.0;
		uint64_t want = (uint64_t)(frac * (double)count);
		if (want < 1) want = 1;
		uint64_t got = 0;
		double s = 0.0;
		for (int b = kN - 1; b >= 0 && got < want; --b)
		{
			if (!n[b]) continue;
			const uint64_t take = (want - got) < (uint64_t)n[b] ? (want - got) : (uint64_t)n[b];
			s += take == n[b] ? sum[b] : sum[b] * ((double)take / (double)n[b]);
			got += take;
		}
		return got ? s / (double)got : 0.0;
	}

	// Samples at or above ms, counted by buckets whose lower edge is at or above ms (exact when ms is a bucket edge: any
	// multiple of 0.1 below 100, of 1 below 1000, of 10 below 10000).
	uint64_t countAtLeast(double ms) const
	{
		uint64_t c = 0;
		for (int b = bucket(ms); b < kN; ++b)
			if (lower(b) >= ms - 1e-9) c += n[b];
		return c;
	}
};

// ---- Summary ----------------------------------------------------------------------------------------------------------
// The coarse bins of the lines (upper edges in ms; the last bin is everything at or above 500 ms): 120 Hz, 60 Hz, 40 Hz,
// 30 Hz, 20 Hz, 10 Hz, 5 Hz and 2 Hz frames.
static const int kBins = 9;
static const double kBinEdges[kBins - 1] = { 8.4, 16.8, 25.0, 33.4, 50.0, 100.0, 200.0, 500.0 };

struct Summary
{
	uint64_t n;
	double mean, p50, p99, p999, maxMs;
	double low1Fps, low01Fps;           // 1000 / the mean of the slowest 1 % and 0.1 % (0 without samples)
	uint64_t over2xMedian, over50, over100;
	uint64_t bins[kBins];
};

inline Summary Summarize(const Hist& h)
{
	Summary s;
	memset(&s, 0, sizeof(s));
	s.n = h.count;
	if (!h.count) return s;
	s.mean = h.total / (double)h.count;
	s.p50 = h.percentile(0.50);
	s.p99 = h.percentile(0.99);
	s.p999 = h.percentile(0.999);
	s.maxMs = h.maxMs;
	const double m1 = h.slowMeanMs(0.01), m01 = h.slowMeanMs(0.001);
	s.low1Fps = m1 > 0.0 ? 1000.0 / m1 : 0.0;
	s.low01Fps = m01 > 0.0 ? 1000.0 / m01 : 0.0;
	// over 2x the median: the median's bucket edge doubled, rounded up to a bucket edge (the count is of whole buckets)
	const double twice = 2.0 * s.p50;
	const double edge = Hist::lower(Hist::bucket(twice)) < twice - 1e-9 ? Hist::upper(Hist::bucket(twice)) : twice;
	s.over2xMedian = h.countAtLeast(edge);
	s.over50 = h.countAtLeast(50.0);
	s.over100 = h.countAtLeast(100.0);
	uint64_t below = 0;
	for (int i = 0; i < kBins - 1; ++i)
	{
		// bucket edges: 8.4, 16.8, 33.4 are multiples of 0.1 (exact)
		const uint64_t atOrAbove = h.countAtLeast(kBinEdges[i]);
		const uint64_t under = h.count - atOrAbove;
		s.bins[i] = under - below;
		below = under;
	}
	s.bins[kBins - 1] = h.count - below;
	return s;
}

// ---- WorldTracker -------------------------------------------------------------------------------------------------------
struct PresentResult
{
	bool worldUpdate;        // this presented frame shows a world picture newer than the frame before it
	double wuIntervalMs;     // with worldUpdate: the time since the previous world update's Present (-1 for the first)
	double ageMs;            // the shown picture's world age (Present - its camera's publish), -1 when unknown
	bool freezeEnded;        // this world update ended a freeze (a run of >= 1 frames without one while the camera moved)
	double freezeMs;         // its length: the time between the two world updates around it
	int freezeFrames;        // the presented frames without a world update in it
	int64_t freezeStartQpc;  // the Present of the world update before it
	int64_t freezeEndQpc;    // this Present
};

struct WorldTracker
{
	int64_t lastShown;       // the newest world picture shown so far (its server frame id; 0 = none yet)
	int64_t lastWuQpc;       // the Present of the last world update
	int runFrames;           // presented frames since the last world update without one
	bool runMoved;           // the camera moved in one of them, or at the world update that ends the run

	void reset() { memset(this, 0, sizeof(*this)); }

	// One presented frame. taken: the server frame id shown (0 = none); camPubQpc: the publish QPC of that picture's
	// camera (0 = unknown); moved: the camera of this Present differs from the previous Present's.
	PresentResult onPresent(int64_t presentQpc, int64_t taken, int64_t camPubQpc, bool moved, int64_t qpcFreq)
	{
		PresentResult r;
		memset(&r, 0, sizeof(r));
		r.wuIntervalMs = -1.0;
		r.ageMs = (taken > 0 && camPubQpc > 0 && presentQpc >= camPubQpc && qpcFreq > 0)
			? (double)(presentQpc - camPubQpc) * 1000.0 / (double)qpcFreq : -1.0;
		r.worldUpdate = taken > 0 && taken > lastShown;
		if (!r.worldUpdate)
		{
			if (lastWuQpc) { runFrames++; runMoved = runMoved || moved; }
			return r;
		}
		if (lastWuQpc && qpcFreq > 0)
		{
			r.wuIntervalMs = (double)(presentQpc - lastWuQpc) * 1000.0 / (double)qpcFreq;
			if (runFrames > 0 && (runMoved || moved))
			{
				r.freezeEnded = true;
				r.freezeMs = r.wuIntervalMs;
				r.freezeFrames = runFrames;
				r.freezeStartQpc = lastWuQpc;
				r.freezeEndQpc = presentQpc;
			}
		}
		lastShown = taken;
		lastWuQpc = presentQpc;
		runFrames = 0;
		runMoved = false;
		return r;
	}
};

// ---- PassTimer (server) ---------------------------------------------------------------------------------------------------
// The stages of one pass of the server's render loop (srv_render_host.cpp Loop and DrawFrame; the scene's own stages
// through StageScope). kWait is the pacing wait at the pass's end: not part of the pass's busy time.
enum Stage
{
	kStOther = 0,   // anything not inside a named stage
	kStPolls,       // the sink's tick, the adapter check, the composite pid, the scene's taps and the fatal steps
	kStFeed,        // the event feed's drain and its per-layer hand-on
	kStPrepare,     // ticket 0713: the feed's drain and the scene's taps in a pass woken by <base>.Tap (before the camera)
	kStDecide,     // the scene's decide (the camera, the fog's admit)
	kStSlot,        // the sink's begin: a free slot (with 3302's busy retry)
	kStPreDraw,     // the tile source's beforeDraw: the pre-draw bakes and copies (scene mark)
	kStDraw,        // the scene's draw: what no layer mark claims (overlay and shadow binding, arc, extras, path, clouds)
	kStFog,         // scene mark: the fog's mask of the frame (beforeWorld)
	kStTerrain,     // scene mark: the terrain draw (with the units' cast into the shadow map before it)
	kStWater,       // scene mark: the water with its reflection pass (ice, units and models in it)
	kStForest,      // scene mark: the Screen Overlay pass and the trees
	kStModels,      // scene mark: the cities and landmarks
	kStUnits,       // scene mark: the units' frame (lists, plans) and their scene pass
	kStHandover,    // the sink's end: the hand-over, the fence and the flush
	kStIdle,        // the tile source's idle slice
	kStRecorder,    // the scene recorder's step (tests)
	kStLog,         // the per-second lines and the memory line
	kStWait,        // the pacing wait
	kStageCount
};
static const char* const kStageNames[kStageCount] = { "other", "polls", "feed", "prepare", "decide", "slot", "predraw", "draw", "fog", "terrain",
	"water", "forest", "models", "units", "handover", "idle", "rec", "log", "wait" };

// Switches the calling thread's current pass (if any) to stage s; returns the stage before (s itself when no pass runs).
// For marks that do not fit a scope (a draw whose result is used after it).
inline int PassMark(int s, int64_t (*qpc)());

struct PassTimer
{
	int64_t t[kStageCount];   // QPC ticks per stage in this pass
	int64_t last;             // the last switch
	int64_t start;            // the pass's start
	int cur;

	void begin(int64_t q) { memset(t, 0, sizeof(t)); last = start = q; cur = kStOther; }
	// From q on, time counts to stage s; returns the stage it counted to before.
	int to(int s, int64_t q)
	{
		const int prev = cur;
		t[cur] += q - last;
		last = q;
		cur = (s >= 0 && s < kStageCount) ? s : kStOther;
		return prev;
	}
	// The pass's busy time (every stage but the wait), after a final switch.
	int64_t busy() const
	{
		int64_t s = 0;
		for (int i = 0; i < kStageCount; ++i)
			if (i != kStWait) s += t[i];
		return s;
	}
	// The busiest stage but the wait.
	int top() const
	{
		int best = kStOther;
		for (int i = 0; i < kStageCount; ++i)
			if (i != kStWait && t[i] > t[best]) best = i;
		return best;
	}
};

// The calling thread's current pass (the render thread sets it; null elsewhere: StageScope does nothing).
inline PassTimer*& CurrentPass()
{
	static thread_local PassTimer* p = nullptr;
	return p;
}

inline int PassMark(int s, int64_t (*qpc)())
{
	PassTimer* p = CurrentPass();
	return p ? p->to(s, qpc()) : s;
}

// Per-second maxima and sums of the stages over the passes of one second.
struct PassSecond
{
	int64_t maxT[kStageCount], sumT[kStageCount];
	int64_t maxBusy, sumBusy;
	int64_t passes, longPasses, longUnlogged;
	double wakeMaxMs, wakeSumMs;
	int64_t wakeN;

	void reset() { memset(this, 0, sizeof(*this)); }
	void add(const PassTimer& p)
	{
		for (int i = 0; i < kStageCount; ++i)
		{
			sumT[i] += p.t[i];
			if (p.t[i] > maxT[i]) maxT[i] = p.t[i];
		}
		const int64_t b = p.busy();
		sumBusy += b;
		if (b > maxBusy) maxBusy = b;
		passes++;
	}
	void addWake(double ms)
	{
		if (ms < 0.0) return;
		wakeN++;
		wakeSumMs += ms;
		if (ms > wakeMaxMs) wakeMaxMs = ms;
	}
};

// Switches the current pass to stage s for the scope, back to the stage before at its end.
struct StageScope
{
	int prev;
	PassTimer* p;
	explicit StageScope(int s, int64_t (*qpc)()) : prev(kStOther), p(CurrentPass()), q(qpc)
	{
		if (p) prev = p->to(s, q());
	}
	~StageScope()
	{
		if (p) p->to(prev, q());
	}
	StageScope(const StageScope&) = delete;
	StageScope& operator=(const StageScope&) = delete;

private:
	int64_t (*q)();
};

}  // namespace vppacing
