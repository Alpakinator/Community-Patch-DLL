// srv_hidden_embark.h - the server's side of hidden embarks (ticket 0573, D193; research/08-performance/hidden-embarks.md).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The DLL built with VP64_HIDDEN_EMBARK (fork branches hidden-embark-x64 / hidden-embark-i0) does not post
// GameplayUnitEmbark for a unit that no watching human can see and sends the unit's true state when it comes into view.
// The DLL learns the switch through its export VP64HiddenEmbarkControl(set, stats); this file calls it once, right after
// DllGetGameContext, with VP64Options.ini [Performance] HiddenEmbarks (SrvOptions()), and reads its counters for the
// report. A DLL without the export and the key at 1: one LOUD line and, in remote mode, the D118 report (the popup is
// vp64helper's, not blocking); the game goes on with every embark shown (MC #6536 answer 2).
#pragma once
#include <windows.h>
#include <string>

namespace SrvHiddenEmbark
{
// The DLL's counters (the layout of VP64HiddenEmbarkStats in the fork's VP64HiddenEmbark.h, version 1). iSize is the
// caller's size in, the size the DLL filled out.
struct Stats
{
	int iSize;
	int iVersion;
	int iEnabled;
	int iSource;      // 1 = the export, 2 = the environment, 3 = the compiled default, 0 = not resolved yet
	int iFacingMode;  // 0 = none, 1 = the facing of the last held-back call
	int iReserved;
	long long iCalls, iPostedWatched, iPostedNoGraphics, iDeferred, iWouldDefer;
	long long aiFlushed[4];    // move, reveal, combat, sweep (sweep = a missed reveal)
	long long aiFlushSame[4];
	long long iDropped, iCleared, iPendingMax, iPendingNow, iOtherThread, iFacingOverrides;
};

typedef int (*ControlFn)(int iSet, Stats* pOut);

// What the server decided from the option and the export; pure (unit-tested without a DLL).
struct Decision
{
	bool call;         // call the export with set = value
	bool report;       // write the D118 report (remote mode only)
	std::string line;  // one line for server.log ("hidden-embark: ..."), LOUD when report or a missing export matters
};
Decision Decide(int optionValue, bool fromFile, bool hasExport, int exportVersion, bool remote);

// Right after DllGetGameContext (srv_main.cpp direct mode, srv_remote_main.cpp kRt_Boot). Returns the log line.
std::string Apply(HMODULE hDll, bool remote);
// The counters as one line (LOUD when a missed reveal happened), for the shutdown report; "" without the export.
std::string Summary();
// The line of a Stats block (exposed for the tests).
std::string SummaryLine(const Stats& st);
}  // namespace SrvHiddenEmbark
