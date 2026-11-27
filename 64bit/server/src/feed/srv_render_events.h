// srv_render_events.h - the server's side of the DLL's render messages (ticket 4932, epic 0454 X2; design:
// docs/extras/flags-and-messages-design.md section 2; contract: research/03-dll-engine-boundary/render-events.md).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The DLL built with VP64_RENDER_EVENTS (fork branch x64-render-events) looks up server.exe's export VP64RenderEvent
// once while it loads (VP64RenderEvents.inl) and calls it after a state change it reports (kinds 1000-1999 of
// srv_feed.h). The export counts every call and pushes the event into the renderer's feed (SrvFeed::Push, which returns
// at once while the feed is off). It never calls the DLL, never blocks beyond the feed's lock and returns nothing, so the
// game plays the same with and without it (rule E1). A DLL without the define never calls it; a host without the export
// (a headless test host) leaves the DLL's pointer NULL.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>

extern "C" __declspec(dllexport) void __cdecl VP64RenderEvent(int iKind, int iPlot, int iA0, int iA1, int iA2, int iA3);

namespace SrvRenderEvents
{
// The version of the kinds this server knows (VP64_RENDER_EVENTS_VERSION of the fork header it was built against).
const unsigned kVersionExpected = 1;
// 1 when the server was compiled against a DLL source with VP64RenderEvents.h (its numbers checked then), 0 otherwise.
bool SourceHasHeader();

struct Counters
{
	uint64_t received;     // calls of the export
	uint64_t badKind;      // kinds outside 1000-1999: counted and dropped
	uint64_t unknownKind;  // kinds inside the range that this server does not know (pushed all the same)
	uint64_t activity;     // kind 1000
	uint64_t carrier;      // kind 1001
	uint64_t setup;        // kind 1000 with a[3] == a[2], or 1001, sent at a unit's setup for display: not separable for
	                       // 1001, so this counts 1000's setup sends only
};
Counters GetCounters();
void ResetCounters();   // tests only
// The game turn the events carry: remote mode's SrvRemoteFeed::AtLeaveUpdate and direct mode's TurnDone set it.
void SetTurn(int iTurn);

// What the server makes of the DLL's export DllGetRenderEventsVersion; pure, for the tests.
struct Decision
{
	bool sends;        // the DLL was built with the define and its kinds are this server's
	std::string line;  // one line for server.log / the report ("render-events: ...")
};
Decision Decide(bool hasExport, unsigned dllVersion);
// After the DLL's load (srv_main.cpp direct mode, srv_remote_main.cpp kRt_Boot): reads the export, returns the line.
std::string Apply(HMODULE hDll);
// Whether the DLL sends render messages (false before Apply). Extras that need them switch themselves off when false.
bool DllSends();
// The counters as one line, for the shutdown report ("" when the DLL does not send and nothing was received).
std::string Summary();

// Direct mode's turn loop (srv_main.cpp): one row per finished turn into render-events-per-turn.tsv in the run folder
// (turn, kind-1000 sends, kind-1001 sends, setup sends, all calls), the counts since the previous row. Opens the file at
// the first call; does nothing while the DLL does not send.
void TurnDone(int iTurn);
}  // namespace SrvRenderEvents
