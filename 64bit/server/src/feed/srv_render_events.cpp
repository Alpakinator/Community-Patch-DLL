// srv_render_events.cpp - the server's side of the DLL's render messages (ticket 4932, epic 0454 X2). See
// srv_render_events.h. Compiled with the server's arguments, whose include path holds the DLL fork's
// CvGameCoreDLL_Expansion2 (server/build_server.py); the render side never includes this file's header chain.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_render_events.h"
#include "srv_feed.h"
#include <share.h>
#include <stdio.h>

// The kinds' numbers against the DLL source's header, when the server is built against a source that has it: a server
// built against a header with other numbers does not compile (as remote/srv_dll_interface.h does for the interface
// version). Without the header (a pin before x64-render-events) the export exists and no DLL calls it.
#if defined(__has_include)
#if __has_include("VP64RenderEvents.h")
#include "VP64RenderEvents.h"
#define SRV_RENDER_EVENTS_SOURCE 1
static_assert(VP64_RENDER_EVENTS_VERSION == 1, "VP64RenderEvents.h: VP64_RENDER_EVENTS_VERSION is not "
                                               "SrvRenderEvents::kVersionExpected (1): raise both together");
static_assert(VP64_RENDER_EVENTS_FIRST == SrvFeed::kExtrasFirst && VP64_RENDER_EVENTS_LAST == SrvFeed::kExtrasLast,
              "VP64RenderEvents.h: the kinds' range differs from srv_feed.h (kExtrasFirst, kExtrasLast)");
static_assert(VP64_RENDER_EVENT_UNIT_ACTIVITY == SrvFeed::kUnitActivityChanged,
              "VP64RenderEvents.h: VP64_RENDER_EVENT_UNIT_ACTIVITY differs from srv_feed.h kUnitActivityChanged");
static_assert(VP64_RENDER_EVENT_UNIT_CARRIER == SrvFeed::kUnitCarrierChanged,
              "VP64RenderEvents.h: VP64_RENDER_EVENT_UNIT_CARRIER differs from srv_feed.h kUnitCarrierChanged");
#endif
#endif
#if !defined(SRV_RENDER_EVENTS_SOURCE)
#define SRV_RENDER_EVENTS_SOURCE 0
#endif
static_assert(SrvRenderEvents::kVersionExpected == 1, "kVersionExpected and the static_assert above go together");

namespace
{
volatile LONG64 s_received = 0, s_badKind = 0, s_unknownKind = 0, s_activity = 0, s_carrier = 0, s_setup = 0;
volatile LONG s_turn = -1;      // the game turn of the events (SetTurn / TurnDone); -1 unknown
volatile LONG s_sends = 0;      // Apply's decision
// TurnDone (direct mode's turn loop thread only)
FILE* s_pPerTurn = NULL;
bool s_bPerTurnTried = false;
LONG64 s_lastActivity = 0, s_lastCarrier = 0, s_lastSetup = 0, s_lastReceived = 0;

const char* const kExport = "DllGetRenderEventsVersion";
}  // namespace

extern "C" __declspec(dllexport) void __cdecl VP64RenderEvent(int iKind, int iPlot, int iA0, int iA1, int iA2, int iA3)
{
	InterlockedIncrement64(&s_received);
	if(iKind < SrvFeed::kExtrasFirst || iKind > SrvFeed::kExtrasLast)
	{
		InterlockedIncrement64(&s_badKind);
		return;
	}
	if(iKind == SrvFeed::kUnitActivityChanged)
	{
		InterlockedIncrement64(&s_activity);
		if(iA2 == iA3)
			InterlockedIncrement64(&s_setup);
	}
	else if(iKind == SrvFeed::kUnitCarrierChanged)
		InterlockedIncrement64(&s_carrier);
	else
		InterlockedIncrement64(&s_unknownKind);
	SrvFeed::Event e = {};
	e.kind = (uint16_t)iKind;
	e.plot = iPlot;
	e.turn = (int32_t)s_turn;
	e.a[0] = iA0;
	e.a[1] = iA1;
	e.a[2] = iA2;
	e.a[3] = iA3;
	SrvFeed::Push(e);   // returns at once while the feed is off; never calls the DLL
}

namespace SrvRenderEvents
{
bool SourceHasHeader() { return SRV_RENDER_EVENTS_SOURCE != 0; }

Counters GetCounters()
{
	Counters c;
	c.received = (uint64_t)s_received;
	c.badKind = (uint64_t)s_badKind;
	c.unknownKind = (uint64_t)s_unknownKind;
	c.activity = (uint64_t)s_activity;
	c.carrier = (uint64_t)s_carrier;
	c.setup = (uint64_t)s_setup;
	return c;
}

void ResetCounters()
{
	InterlockedExchange64(&s_received, 0);
	InterlockedExchange64(&s_badKind, 0);
	InterlockedExchange64(&s_unknownKind, 0);
	InterlockedExchange64(&s_activity, 0);
	InterlockedExchange64(&s_carrier, 0);
	InterlockedExchange64(&s_setup, 0);
	s_lastActivity = s_lastCarrier = s_lastSetup = s_lastReceived = 0;
}

void SetTurn(int iTurn) { InterlockedExchange(&s_turn, (LONG)iTurn); }

Decision Decide(bool hasExport, unsigned dllVersion)
{
	Decision d;
	char buf[400];
	if(!hasExport)
	{
		d.sends = false;
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
		            "render-events: the DLL has no %s export (built without VP64_RENDER_EVENTS): it sends no render "
		            "messages; extras that need them stay off (ticket 4932)", kExport);
	}
	else if(dllVersion != kVersionExpected)
	{
		d.sends = false;
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
		            "render-events: LOUD: the DLL's render-message kinds are version %u, this server knows version %u: "
		            "the DLL and the server come from different builds; extras that need the messages stay off "
		            "(ticket 4932)", dllVersion, kVersionExpected);
	}
	else
	{
		d.sends = true;
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
		            "render-events: the DLL sends render messages, version %u (as this server expects; the server was "
		            "built %s the DLL source's VP64RenderEvents.h) (ticket 4932)", dllVersion,
		            SRV_RENDER_EVENTS_SOURCE ? "against" : "WITHOUT");
	}
	d.line = buf;
	return d;
}

std::string Apply(HMODULE hDll)
{
	typedef unsigned int (*VersionFn)();
	VersionFn fn = hDll ? reinterpret_cast<VersionFn>(reinterpret_cast<void*>(GetProcAddress(hDll, kExport))) : NULL;
	const Decision d = Decide(fn != NULL, fn ? fn() : 0u);
	InterlockedExchange(&s_sends, d.sends ? 1 : 0);
	return d.line;
}

bool DllSends() { return s_sends != 0; }

std::string Summary()
{
	const Counters c = GetCounters();
	if(!s_sends && c.received == 0)
		return std::string();
	char buf[400];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE,
	            "render-events: %s%llu calls: activity %llu (at setup %llu), carrier %llu, unknown kinds %llu, outside "
	            "1000-1999 %llu (ticket 4932)", c.badKind ? "LOUD: " : "", (unsigned long long)c.received,
	            (unsigned long long)c.activity, (unsigned long long)c.setup, (unsigned long long)c.carrier,
	            (unsigned long long)c.unknownKind, (unsigned long long)c.badKind);
	return buf;
}

void TurnDone(int iTurn)
{
	SetTurn(iTurn + 1);
	if(!s_sends)
		return;
	if(!s_pPerTurn && !s_bPerTurnTried)
	{
		s_bPerTurnTried = true;
		s_pPerTurn = _fsopen("render-events-per-turn.tsv", "w", _SH_DENYWR);
		if(s_pPerTurn)
			fprintf(s_pPerTurn, "turn\tactivity\tcarrier\tactivity_at_setup\tcalls\n");
	}
	const Counters c = GetCounters();
	if(s_pPerTurn)
	{
		fprintf(s_pPerTurn, "%d\t%lld\t%lld\t%lld\t%lld\n", iTurn, (long long)c.activity - s_lastActivity,
		        (long long)c.carrier - s_lastCarrier, (long long)c.setup - s_lastSetup,
		        (long long)c.received - s_lastReceived);
		fflush(s_pPerTurn);
	}
	s_lastActivity = (LONG64)c.activity;
	s_lastCarrier = (LONG64)c.carrier;
	s_lastSetup = (LONG64)c.setup;
	s_lastReceived = (LONG64)c.received;
}
}  // namespace SrvRenderEvents
