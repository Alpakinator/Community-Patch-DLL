// proxy_report_mods.cpp - the activated mods for the error report (issue 3521; proxy_report.h ActivatedMods).
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// Read with the engine's own database library (the proxy imports it since issue 2504) from a second, read-only
// connection to <folder of config.ini>\cache\Civ5ModsDatabase.db: Mods(RowID, ModID, Version, ..., Activated) and the
// mod's name from ModProperties(ModID, Version, Name = 'Name', Value) [L: schema read from an instance's copy,
// 2026-11-14]. The EXE keeps its own connection to that file (g_Civ5App+0xAF8); a reader takes only a shared lock.
#include "proxy_report.h"
#include "proxy_session.h"
#include "CvGameDatabase.h"
#include <stdio.h>

namespace ProxyReport
{
bool ActivatedMods(const std::wstring& configIni, std::string* text, std::string* why)
{
	const size_t cut = configIni.find_last_of(L"\\/");
	if (cut == std::wstring::npos)
	{
		*why = "no config.ini folder";
		return false;
	}
	const std::wstring dbw = configIni.substr(0, cut) + L"\\cache\\Civ5ModsDatabase.db";
	if (GetFileAttributesW(dbw.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		*why = "cache\\Civ5ModsDatabase.db beside config.ini does not exist";
		return false;
	}
	Database::Connection db;
	if (!db.Open(ProxySession::Utf8(dbw).c_str(), Database::OPEN_READONLY))
	{
		*why = "cache\\Civ5ModsDatabase.db could not be opened read-only";
		return false;
	}
	Database::Results r;
	if (!db.Execute(r, "SELECT m.ModID, m.Version, (SELECT p.Value FROM ModProperties p WHERE p.ModID = m.ModID AND "
	                   "p.Version = m.Version AND p.Name = 'Name') FROM Mods m WHERE m.Activated = 1 ORDER BY m.RowID"))
	{
		*why = "the query of Mods and ModProperties failed";
		db.Close();
		return false;
	}
	unsigned n = 0;
	std::string out;
	while (r.Step())
	{
		const char* id = r.GetText(0);
		const char* ver = r.GetText(1);
		const char* name = r.GetText(2);
		char line[512];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "%s  version %s  %s\n", id ? id : "?", ver ? ver : "?",
		            name ? name : "(no name)");
		out += line;
		++n;
	}
	r.Release();
	db.Close();
	if (!n) out = "(no mod is activated)\n";
	*text = out;
	return true;
}
}  // namespace ProxyReport
