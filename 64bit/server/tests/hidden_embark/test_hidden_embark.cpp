// test_hidden_embark.cpp - host test of server/src/srv_hidden_embark.cpp (ticket 0573): the decision from the option and
// the DLL's export, the report on a missing export (remote mode only), the counters' line. No DLL, no game: SrvOptions()
// and SrvExeCrash::ReportServerFailure are stubs here. Built and run by run_hidden_embark_test.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_hidden_embark.h"
#include "srv_options.h"
#include "srv_exe_crash.h"
#include <stdio.h>
#include <string.h>
#include <string>

static Vp64Opt::Options g_opt;
const Vp64Opt::Options& SrvOptions() { return g_opt; }
void SrvOptionsLoad() {}

static int g_reports = 0;
static std::string g_stage, g_failure;
static bool g_reportOn = true;
namespace SrvExeCrash
{
std::string ReportServerFailure(const ServerFailure& f, bool* pOn)
{
	++g_reports;
	g_stage = f.stage;
	g_failure = f.failure;
	*pOn = g_reportOn;
	return g_reportOn ? std::string("X:\\reports\\he") : std::string();
}
std::string ScrubText(const std::string& text) { return text; }
}  // namespace SrvExeCrash

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                       \
	do                                                                 \
	{                                                                  \
		if(c)                                                          \
			++g_pass;                                                  \
		else                                                           \
		{                                                              \
			++g_fail;                                                  \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
		}                                                              \
	} while(0)

static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

static void SetOption(int value, bool fromFile)
{
	g_opt.SetDefaults();
	g_opt.value[Vp64Opt::kHiddenEmbarks] = value;
	g_opt.fromFile[Vp64Opt::kHiddenEmbarks] = fromFile ? 1 : 0;
}

int main()
{
	using namespace SrvHiddenEmbark;
	// the export present at the right version: always called, with the option's value; never a report
	for(int v = 0; v <= 1; ++v)
		for(int remote = 0; remote <= 1; ++remote)
		{
			const Decision d = Decide(v, true, true, 1, remote != 0);
			CHECK(d.call && !d.report);
			CHECK(Has(d.line, v ? "told the DLL on" : "told the DLL off"));
			CHECK(Has(d.line, "(from the file)") && !Has(d.line, "LOUD"));
		}
	CHECK(Has(Decide(0, false, true, 1, true).line, "(default)"));
	// the export missing, option 1: LOUD; the report in remote mode only; never called
	{
		const Decision r = Decide(1, true, false, 0, true);
		CHECK(!r.call && r.report && Has(r.line, "LOUD") && Has(r.line, "no export VP64HiddenEmbarkControl") &&
		      Has(r.line, "writing the D118 report"));
		const Decision d = Decide(1, true, false, 0, false);
		CHECK(!d.call && !d.report && Has(d.line, "LOUD") && !Has(d.line, "D118"));
	}
	// the export at another version, option 1: as missing (the counters' layout may differ)
	{
		const Decision r = Decide(1, true, true, 2, true);
		CHECK(!r.call && r.report && Has(r.line, "has version 2, this server wants 1"));
	}
	// option 0 and no export: nothing to do, no LOUD, no report
	{
		const Decision d = Decide(0, false, false, 0, true);
		CHECK(!d.call && !d.report && !Has(d.line, "LOUD") && Has(d.line, "nothing to do"));
		const Decision w = Decide(0, true, true, 7, true);
		CHECK(!w.call && !w.report && !Has(w.line, "LOUD"));
	}
	// Apply without a DLL (NULL module = no export): option 1 in remote mode writes the report once, with the line
	SetOption(1, true);
	g_reports = 0;
	std::string line = Apply(NULL, true);
	CHECK(g_reports == 1 && g_stage == "hidden-embark" && Has(g_failure, "LOUD") && Has(line, "report: X:\\reports\\he"));
	g_reportOn = false;
	line = Apply(NULL, true);
	CHECK(g_reports == 2 && Has(line, "the report is off"));
	g_reportOn = true;
	line = Apply(NULL, false);   // direct mode: LOUD, no report
	CHECK(g_reports == 2 && Has(line, "LOUD"));
	SetOption(0, false);
	line = Apply(NULL, true);
	CHECK(g_reports == 2 && !Has(line, "LOUD"));
	// no export called: no counters line
	CHECK(Summary().empty());
	// the counters' line: LOUD only with a missed reveal
	Stats st;
	memset(&st, 0, sizeof(st));
	st.iSize = (int)sizeof(st);
	st.iVersion = 1;
	st.iEnabled = 1;
	st.iSource = 1;
	st.iCalls = 389;
	st.iDeferred = 316;
	st.aiFlushed[0] = 16;
	st.aiFlushed[1] = 4;
	st.iDropped = 22;
	st.iPendingMax = 88;
	std::string s = SummaryLine(st);
	CHECK(Has(s, "hidden-embark: on (source: the server), calls 389") && Has(s, "held back 316") &&
	      Has(s, "flushed by move 16, reveal 4, combat 0; missed reveals 0") && Has(s, "max 88") && !Has(s, "LOUD"));
	st.aiFlushed[3] = 2;
	s = SummaryLine(st);
	CHECK(Has(s, "hidden-embark: LOUD: on") && Has(s, "missed reveals 2"));
	st.iEnabled = 0;
	st.iSource = 3;
	CHECK(Has(SummaryLine(st), "off (source: the DLL's default)"));
	// the counters' layout: 176 bytes, as VP64HiddenEmbarkStats in the fork's VP64HiddenEmbark.h (6 ints, 19 long longs)
	CHECK(sizeof(Stats) == 6 * 4 + 19 * 8);
	printf("hidden-embark host test: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
