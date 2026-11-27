// srv_options.cpp - the server reads the patch's options file (one-off 9042, decisions D89 and D88). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
// See srv_options.h. The loader itself is options/src/vp64_options.cpp, the same code the proxy uses.
#include "srv_options.h"
#include "srv_common.h"

namespace
{
Vp64Opt::Options s_options;

void Warn(void* ctx, const char* line) { SrvLog("options: %s: %s", (const char*)ctx, line); }

const char* StatusText(Vp64Opt::LoadStatus st)
{
	switch (st)
	{
	case Vp64Opt::kLoaded: return "read";
	case Vp64Opt::kMissing: return "missing (every option takes its default)";
	case Vp64Opt::kUnreadable: return "unreadable";
	case Vp64Opt::kTooLarge: return "too large";
	}
	return "?";
}

void LogValues()
{
	for (int i = 0; i < Vp64Opt::kCount; ++i)
	{
		const Vp64Opt::Def& d = Vp64Opt::Definition((Vp64Opt::Id)i);
		SrvLog("options: [%s] %s = %d (%s)", d.section, d.key, s_options.value[i],
		       s_options.fromFile[i] ? "from the file" : "default");
	}
}

// Ticket 4931: every extra's effective state and why (docs/extras/flags-and-messages-design.md 1.4). First both switches,
// then one line per [Extras] row with a key that is no switch (reserved rows skipped).
void LogExtras()
{
	char line[512];
	Vp64Opt::DescribeSwitches(s_options, line, sizeof(line));
	SrvLog("options: extras: %s", line);
	int n = 0;
	for (int i = 0; i < Vp64Opt::kCount; ++i)
	{
		const Vp64Opt::Def& d = Vp64Opt::Definition((Vp64Opt::Id)i);
		if (!d.key[0] || (d.category != Vp64Opt::kCatBugfix && d.category != Vp64Opt::kCatVisual)) continue;
		Vp64Opt::DescribeExtra(s_options, (Vp64Opt::Id)i, line, sizeof(line));
		SrvLog("options: extras: %s", line);
		++n;
	}
	if (!n) SrvLog("options: extras: this build has no extra yet (only the two switches)");
}

void LogAll()
{
	LogValues();
	LogExtras();
}
}  // namespace

const Vp64Opt::Options& SrvOptions() { return s_options; }

void SrvOptionsLoad()
{
	s_options.SetDefaults();
	const std::string path = SrvCfgStr("options_file");
	if (path.empty())
	{
		SrvLog("options: server.cfg names no options_file: every option takes its default");
		LogAll();
		return;
	}
	wchar_t wpath[1024];
	if (!Vp64Opt::Utf8ToWide(path.c_str(), wpath, sizeof(wpath) / sizeof(wpath[0])))
	{
		SrvLog("options: options_file %s is not valid UTF-8 or longer than 1023 characters: every option takes its "
		       "default", path.c_str());
		LogAll();
		return;
	}
	unsigned warnings = 0;
	const Vp64Opt::LoadStatus st = Vp64Opt::LoadFile(wpath, s_options, Warn, (void*)path.c_str(), &warnings);
	SrvLog("options: %s: %s, %u warning%s", path.c_str(), StatusText(st), warnings, warnings == 1 ? "" : "s");
	LogAll();
}
