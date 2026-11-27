// render_host_test.cpp - test host of the renderer inside server.exe (issue 3301): the same render objects server.exe links
// (server/src/render/*, shell/frame/ring_sink.cpp, server/src/feed/srv_feed.cpp, c5render), started from a key=value file
// with the server.cfg keys (render_*), without the DLL, the proxy or vpipc. Test program only; creates no window.
//
//   render_host_test.exe --config FILE [--seconds S] [--feed-events N] [--feed-plots N] [--game-dir DIR] [--run-dir DIR]
//                        [--stop-event NAME]
//
// --stop-event NAME (issue 0333): a named event the test created; the host stops as soon as it is set, --seconds stays
// the upper bound. A test that waits for a count (legacy waits, drawn cameras) ends the run when the count is reached,
// instead of asserting how many happened in a fixed time on a loaded machine.
// --feed-events N: the main thread (standing in for a DLL thread) pushes N events into the feed while the renderer runs,
// so the render thread's Drain count can be checked. --feed-plots N (issue 3306): after 1 s the main thread takes a
// 64 x 40 plot table (BeginSnapshot / EndSnapshot, as SrvRt::LeaveUpdate does) and then patches the improvement of
// plots 0..N-1 (as GameplayPlotStateChange does), so the changed plots reach the scene's tile-cache source
// (TileCacheSource::onPlotsChanged). Prints "result: ..." lines; the renderer's lines go to stdout too.
// Exit: 0 the host ran and stopped (also when the renderer went to FAILED: the process goes on, which is the point),
// 2 bad command line or config (the parser's reason printed), 3 HostStart refused, 4 the process did not survive (never
// returned here).
#include "srv_render_host.h"
#include "feed/srv_feed.h"
#include "feed/srv_feed_server.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "vp64_options.h"

// ticket 0496: HostStop waits 5 s for the render thread and then leaves it running (srv_render_host.cpp, product code);
// the thread's teardown (ReleaseDevice) writes the totals line at its end. Under ASan beside other heavy jobs that took
// more than 5 s in 2 of 104 stops (median 0.15 s, 2026-11-17), the process ended first and a test missed the totals.
static volatile LONG g_stopLeftRunning = 0, g_totalsWritten = 0;

static void PrintLine(const char* line)
{
	printf("%s\n", line);
	fflush(stdout);
	if(strstr(line, "the render thread did not stop within 5 s"))
		InterlockedExchange(&g_stopLeftRunning, 1);
	if(strstr(line, "terrain totals: "))
		InterlockedExchange(&g_totalsWritten, 1);
}

// issue 3308: server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units, here read from --options-file
// with the same loader (options/src/vp64_options.cpp); without the switch every option is at its default.
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }
static void OptWarn(void*, const char* line) { printf("options: %s\n", line); }

// issue 4105: --fatal-hook 1 gives HostConfig::fatal a hook that prints and counts (server.exe's ReportAndExit does not
// return; this one does, so that a test sees whether the route is taken once and the process goes on)
static volatile LONG g_fatalCalls = 0;
static void TestFatal(int code, const char* why)
{
	InterlockedIncrement(&g_fatalCalls);
	printf("result: fatal hook code %d: %s\n", code, why ? why : "(null)");
	fflush(stdout);
}

// bakefake.cpp: the bake-tap stand-in producer (render_host_test.exe --bakefake SECTION TXxTY TILEPLOTS MISSING VERSION
// TYPELESS SECONDS)
int BakeFakeMain(const wchar_t* sectionName, int tilesX, int tilesY, int tilePlots, int missing, int manifestVersion, bool typeless,
                 double seconds);

int main(int argc, char** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(argc == 9 && !strcmp(argv[1], "--bakefake"))
	{
		wchar_t sec[256];
		MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, sec, 256);
		int tx = 0, ty = 0;
		if(sscanf_s(argv[3], "%dx%d", &tx, &ty) != 2 || tx < 1 || ty < 1 || tx * ty > 4096)   // VPSHELL_BAKE_MAX_SLOTS
			return 2;
		return BakeFakeMain(sec, tx, ty, atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]) != 0, atof(argv[8]));
	}
	const char* cfgPath = nullptr;
	double seconds = 5;
	int feedEvents = 0, feedPlots = 0, feedDelayMs = 0, feedRounds = 1;
	int feedTableW = 64, feedTableH = 40, feedArt = -1;   // ticket 4555: --feed-table WxH, --feed-art A (every plot's artType)
	std::string feedField = "improvement";
	ULONGLONG readyAt = 0, roundAt = 0;
	uint64_t roundDrains = 0;   // ticket 0496: the feed's drain count when the last round's last plot was patched
	std::string gameDir = ".", runDir = ".";
	bool fatalHook = false;
	HANDLE stopEvent = nullptr;   // issue 0333
	for(int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if(i + 1 >= argc)
		{
			fprintf(stderr, "%s needs a value\n", a.c_str());
			return 2;
		}
		const char* v = argv[++i];
		if(a == "--config")
			cfgPath = v;
		else if(a == "--seconds")
			seconds = atof(v);
		else if(a == "--feed-events")
			feedEvents = atoi(v);
		else if(a == "--feed-plots")
			feedPlots = atoi(v);
		else if(a == "--feed-delay-ms")   // issue 3308: the plot patches start this long after READY (a field first)
			feedDelayMs = atoi(v);
		else if(a == "--feed-rounds")     // issue 3308: every plot patched R times, 0.5 s apart
			feedRounds = atoi(v) < 0 ? 0 : atoi(v);   // ticket 4555: 0 = the table only, no patches
		else if(a == "--feed-table")     // ticket 4555: the snapshot's grid (default 64x40; the synthetic map's is 6x2)
		{
			if(sscanf_s(v, "%dx%d", &feedTableW, &feedTableH) != 2 || feedTableW < 1 || feedTableH < 1 || feedTableW * feedTableH > 1 << 20)
			{
				fprintf(stderr, "--feed-table %s: want WxH\n", v);
				return 2;
			}
		}
		else if(a == "--feed-art")       // ticket 4555: every plot of the snapshot gets this artType (0..4)
			feedArt = atoi(v);
		else if(a == "--feed-field")      // issue 3308: improvement (layout) | terrain | fow | owner (getters)
			feedField = v;
		else if(a == "--game-dir")
			gameDir = v;
		else if(a == "--run-dir")
			runDir = v;
		else if(a == "--fatal-hook")      // issue 4105
			fatalHook = atoi(v) != 0;
		else if(a == "--stop-event")      // issue 0333
		{
			wchar_t w[256];
			MultiByteToWideChar(CP_UTF8, 0, v, -1, w, 256);
			stopEvent = OpenEventW(SYNCHRONIZE, FALSE, w);
			if(!stopEvent)
			{
				fprintf(stderr, "--stop-event %s: cannot open (error %lu)\n", v, GetLastError());
				return 2;
			}
		}
		else if(a == "--options-file")
		{
			wchar_t w[1024];
			MultiByteToWideChar(CP_UTF8, 0, v, -1, w, 1024);
			unsigned warns = 0;
			const Vp64Opt::LoadStatus ls = Vp64Opt::LoadFile(w, g_options, &OptWarn, nullptr, &warns);
			printf("options: %s: status %d, %u warnings, BakedTileCameraRadius %d\n", v, (int)ls, warns, g_options.BakedTileCameraRadius());
		}
		else
		{
			fprintf(stderr, "unknown argument %s\n", a.c_str());
			return 2;
		}
	}
	if(!cfgPath)
	{
		fprintf(stderr, "--config is required\n");
		return 2;
	}
	std::map<std::string, std::string> kv;
	FILE* f = nullptr;
	if(fopen_s(&f, cfgPath, "r") || !f)
	{
		fprintf(stderr, "cannot open %s\n", cfgPath);
		return 2;
	}
	char line[2048];
	while(fgets(line, sizeof(line), f))
	{
		std::string s = line;
		while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		const size_t eq = s.find('=');
		if(s.empty() || s[0] == '#' || eq == std::string::npos)
			continue;
		kv[s.substr(0, eq)] = s.substr(eq + 1);
	}
	fclose(f);
	srvrender::HostConfig cfg;
	std::string why;
	auto get = [&](const char* k) {
		auto it = kv.find(k);
		return it == kv.end() ? std::string() : it->second;
	};
	if(!srvrender::ParseHostConfig(get, gameDir, runDir, cfg, why))
	{
		printf("result: config refused: %s\n", why.c_str());
		return 2;
	}
	cfg.log = &PrintLine;
	if(fatalHook)
		cfg.fatal = &TestFatal;
	SrvFeed::SetEnabled(feedEvents > 0 || feedPlots > 0);
	if(!srvrender::HostStart(cfg, why))
	{
		printf("result: not started: %s\n", why.c_str());
		return 3;
	}
	const ULONGLONG t0 = GetTickCount64();
	int pushed = 0, patched = 0;
	bool snapshot = false, repeated = false;
	bool stoppedByEvent = false;
	auto stopSet = [&]() {
		if(stopEvent && !stoppedByEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
		{
			stoppedByEvent = true;
			printf("result: --stop-event set after %llu ms\n", (unsigned long long)(GetTickCount64() - t0));
		}
		return stoppedByEvent;
	};
	while((double)(GetTickCount64() - t0) < seconds * 1000.0 && !stopSet())
	{
		if(feedPlots > 0 && srvrender::HostState() == 2 && !readyAt)
			readyAt = GetTickCount64();
		if(feedPlots > 0 && srvrender::HostState() == 2 /* VPFS_READY: the scene and its tile source exist */ &&
		   GetTickCount64() - readyAt >= (ULONGLONG)feedDelayMs)
		{
			if(!snapshot)
			{
				SrvFeed::BeginSnapshot((uint32_t)feedTableW, (uint32_t)feedTableH);
				if(feedArt >= 0)   // ticket 4555: the plots' continent art type (the residency's tile styles)
					for(int i = 0; i < feedTableW * feedTableH; ++i)
					{
						SrvFeed::PlotRecord r = SrvFeed::EmptyRecord();
						r.artType = (uint8_t)feedArt;
						SrvFeed::SetPlot((uint32_t)i, r);
					}
				SrvFeed::EndSnapshot();
				snapshot = true;
				printf("result: snapshot %dx%d art %d taken after %llu ms\n", feedTableW, feedTableH, feedArt,
				       (unsigned long long)(GetTickCount64() - t0));
			}
			else if(patched < feedPlots * feedRounds && feedPlots <= 64 * 40)
			{
				// issue 3308: --feed-rounds R patches every plot R times (round k sets the value 7 + k), 0.5 s apart so that
				// each round reaches the renderer in drains of its own; --feed-field picks the field (and its source bit).
				// Ticket 0496: and not before the render thread has drained once since the round's last patch. Drain takes
				// every changed plot and marks each once (srv_feed.cpp g_changedMark), so two rounds in one drain count 20
				// plots, not 40; under ASan on a loaded workstation a drain came more than 0.5 s apart (2026-11-17)
				const int round = patched / feedPlots, plot = patched % feedPlots;
				if(plot == 0 && round > 0 && (GetTickCount64() - roundAt < 500 || SrvFeed::GetCounters().drains <= roundDrains))
				{
					Sleep(10);
					continue;
				}
				SrvFeed::PlotRecord r = SrvFeed::EmptyRecord();
				uint32_t mask = SrvFeed::kFImprovement | SrvFeed::kFLayout;
				if(feedField == "terrain")
				{
					r.terrain = (int8_t)(1 + round);
					mask = SrvFeed::kFTerrain | SrvFeed::kFGetters;
				}
				else if(feedField == "fow")
				{
					r.fow = (uint8_t)(1 + (round & 1));
					mask = SrvFeed::kFFow | SrvFeed::kFGetters;
				}
				else if(feedField == "owner")
				{
					r.owner = (int8_t)(1 + round);
					mask = SrvFeed::kFOwner | SrvFeed::kFGetters;
				}
				else
					r.improvement = (int16_t)(7 + round);
				if(SrvFeed::UpdatePlot((uint32_t)plot, mask, r))
					++patched;
				else
				{
					printf("result: UpdatePlot(%d) changed nothing\n", plot);
					feedPlots = feedRounds = 0;
				}
				if(plot == feedPlots - 1)
				{
					roundAt = GetTickCount64();
					roundDrains = SrvFeed::GetCounters().drains;
				}
				if(patched % 50)
					continue;
			}
			else if(patched == feedPlots * feedRounds && feedRounds == 1 && !repeated)
			{
				// the same value again: not a change, so not a changed plot for the source (negative case of 3306 item 3)
				repeated = true;
				SrvFeed::PlotRecord r = SrvFeed::EmptyRecord();
				r.improvement = 7;
				const bool changed = SrvFeed::UpdatePlot(0, SrvFeed::kFImprovement | SrvFeed::kFLayout, r);
				printf("result: repeated patch of plot 0 %s\n", changed ? "CHANGED the record" : "changed nothing");
			}
		}
		if(pushed < feedEvents)
		{
			SrvFeed::Event e = {};
			e.kind = SrvFeed::kFowChanged;
			e.plot = -1;
			e.turn = 1;
			SrvFeed::Push(e);
			++pushed;
			if(pushed % 100)
				continue;
		}
		Sleep(10);
	}
	const uint32_t stateBeforeStop = srvrender::HostState();
	srvrender::HostStop("test host: --seconds reached");
	if(g_stopLeftRunning)
	{
		// ticket 0496: the thread HostStop left running goes on with its teardown; wait (bounded) for its totals line
		const ULONGLONG w0 = GetTickCount64();
		while(!g_totalsWritten && GetTickCount64() - w0 < 60000)
			Sleep(10);
		printf("result: HostStop left the render thread running; its totals line %s %llu ms after HostStop returned\n",
		       g_totalsWritten ? "came" : "did NOT come within 60 s", (unsigned long long)(GetTickCount64() - w0));
	}
	const SrvFeed::Counters fc = SrvFeed::GetCounters();
	printf("result: state before stop %u, after stop %u; feed pushed %d (counter %llu, drains %llu); plots patched %d\n",
	       stateBeforeStop, srvrender::HostState(), pushed, (unsigned long long)fc.pushed, (unsigned long long)fc.drains, patched);
	printf("result: %s\n", srvrender::HostSummary().c_str());
	if(fatalHook)
		printf("result: fatal hook calls %ld\n", (long)g_fatalCalls);
	printf("result: process alive at exit\n");
	return 0;
}
