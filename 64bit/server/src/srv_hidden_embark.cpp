// srv_hidden_embark.cpp - the server's side of hidden embarks (ticket 0573, D193): see srv_hidden_embark.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_hidden_embark.h"
#include "srv_options.h"
#include "remote/srv_exe_crash.h"	// the D118 report for a DLL without the export while the option is on
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace SrvHiddenEmbark
{
namespace
{
const int kVersion = 1;  // the DLL's VP64_HIDDEN_EMBARK_VERSION this file is written for
ControlFn s_pfn = NULL;

std::string Fmt(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
	va_end(ap);
	return buf;
}

const char* SourceName(int s)
{
	switch(s)
	{
	case 1: return "the server";
	case 2: return "the environment";
	case 3: return "the DLL's default";
	}
	return "not resolved yet";
}
}  // namespace

Decision Decide(int optionValue, bool fromFile, bool hasExport, int exportVersion, bool remote)
{
	Decision d;
	d.call = false;
	d.report = false;
	const char* src = fromFile ? "from the file" : "default";
	if(hasExport && exportVersion == kVersion)
	{
		d.call = true;
		d.line = Fmt("hidden-embark: [Performance] HiddenEmbarks = %d (%s); the DLL's feature version %d; told the DLL %s "
		             "(ticket 0573)", optionValue, src, exportVersion, optionValue ? "on" : "off");
		return d;
	}
	const std::string what = hasExport ? Fmt("its export VP64HiddenEmbarkControl has version %d, this server wants %d",
	                                          exportVersion, kVersion)
	                                    : std::string("it has no export VP64HiddenEmbarkControl (built without VP64_HIDDEN_EMBARK)");
	if(optionValue == 1)
	{
		d.report = remote;
		d.line = Fmt("hidden-embark: LOUD: [Performance] HiddenEmbarks = 1 (%s) but the DLL cannot do it: %s; every embark "
		             "is shown as in the stock game%s (ticket 0573)", src, what.c_str(),
		             remote ? "; writing the D118 report" : "");
	}
	else
		d.line = Fmt("hidden-embark: [Performance] HiddenEmbarks = 0 (%s); the DLL: %s; nothing to do (ticket 0573)", src,
		             what.c_str());
	return d;
}

std::string Apply(HMODULE hDll, bool remote)
{
	const int value = SrvOptions().HiddenEmbarks();
	const bool fromFile = SrvOptions().fromFile[Vp64Opt::kHiddenEmbarks] != 0;
	ControlFn pfn = hDll ? reinterpret_cast<ControlFn>(GetProcAddress(hDll, "VP64HiddenEmbarkControl")) : NULL;
	int version = 0;
	if(pfn)
		version = pfn(-1, NULL);   // a query: the feature version only
	const Decision d = Decide(value, fromFile, pfn != NULL, version, remote);
	if(d.call)
	{
		s_pfn = pfn;
		pfn(value ? 1 : 0, NULL);
	}
	if(d.report)
	{
		SrvExeCrash::ServerFailure f;
		f.stage = "hidden-embark";
		f.title = "the option HiddenEmbarks is on but the game's DLL does not have the feature, ticket 0573";
		f.code = 0;
		f.codeName = "none (not a crash: the game goes on)";
		f.summary = "The option HiddenEmbarks is on, but the patch's game DLL does not have that feature. The game goes on and "
		            "shows every embark as before. The 64-bit server wrote this report.";
		f.intro = "VP64Options.ini sets [Performance] HiddenEmbarks = 1, but the game DLL the 64-bit server loaded does not\n"
		          "have the feature (a DLL from another build of the patch?). Nothing else is wrong: the game goes on and\n"
		          "shows every embark and disembark as the normal game does. Please send this folder in.\n";
		f.failure = d.line + "\n";
		bool on = false;
		const std::string folder = SrvExeCrash::ReportServerFailure(f, &on);
		return d.line + (on ? "; report: " + (folder.empty() ? std::string("(the folder could not be made)") : folder)
		                    : std::string("; the report is off"));
	}
	return d.line;
}

std::string SummaryLine(const Stats& st)
{
	const long long missed = st.aiFlushed[3];
	return Fmt("hidden-embark: %s%s (source: %s), calls %lld: posted watched %lld, posted without graphics %lld, held back "
	           "%lld, would hold back %lld; flushed by move %lld, reveal %lld, combat %lld; missed reveals %lld; dropped "
	           "%lld, cleared %lld; pending now %lld, max %lld; facing overrides %lld; other-thread entries %lld (ticket 0573)",
	           missed > 0 ? "LOUD: " : "", st.iEnabled ? "on" : "off", SourceName(st.iSource), st.iCalls,
	           st.iPostedWatched, st.iPostedNoGraphics, st.iDeferred, st.iWouldDefer, st.aiFlushed[0], st.aiFlushed[1],
	           st.aiFlushed[2], missed, st.iDropped, st.iCleared, st.iPendingNow, st.iPendingMax, st.iFacingOverrides,
	           st.iOtherThread);
}

std::string Summary()
{
	if(!s_pfn)
		return std::string();
	Stats st;
	memset(&st, 0, sizeof(st));
	st.iSize = (int)sizeof(st);
	s_pfn(-1, &st);
	if(st.iSize == 0)
		return "hidden-embark: the counters were not readable (the DLL's table was busy: a crash inside a hook?) (ticket 0573)";
	if(st.iSize != (int)sizeof(st))
		return Fmt("hidden-embark: LOUD: the DLL filled %d bytes of its counters, this server reads %d (ticket 0573)", st.iSize,
		           (int)sizeof(st));
	return SummaryLine(st);
}
}  // namespace SrvHiddenEmbark
