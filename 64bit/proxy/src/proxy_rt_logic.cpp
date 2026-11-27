// proxy_rt_logic.cpp - see proxy_rt_logic.h (issue 2502). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "proxy_rt_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>

namespace ProxyLogic
{
// ================================================================================================ paths and text
std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
	return s.substr(a, b - a);
}

bool IsAbsolutePath(const std::string& p)
{
	return (p.size() >= 2 && p[1] == ':') ||
	       (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/'));
}

std::string DirOf(const std::string& p)
{
	const size_t k = p.find_last_of("\\/");
	return k == std::string::npos ? std::string(".") : p.substr(0, k);
}

static std::string Backslashes(std::string p)
{
	for (size_t i = 0; i < p.size(); ++i)
		if (p[i] == '/') p[i] = '\\';
	return p;
}

std::string ResolvePath(const std::string& base, const std::string& p)
{
	if (p.empty()) return p;
	if (IsAbsolutePath(p)) return Backslashes(p);
	std::string r = Backslashes(p);
	while (r.size() >= 2 && r[0] == '.' && r[1] == '\\') r = r.substr(2);
	return Backslashes(base) + "\\" + r;
}

// ================================================================================================ the installed layout
std::string X64DllName(const std::string& x64Dir, const std::string& manifestFile)
{
	if (!manifestFile.empty()) return manifestFile;
	const DWORD at = GetFileAttributesA((x64Dir + "\\" VP64_X64_DLL_FILE).c_str());
	if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) return VP64_X64_DLL_FILE;
	return VP64_X64_DLL_FILE_OLD;
}

// True when the folder component of p at [i, i+n) equals name (any case) and is a whole component.
static bool IsComponent(const std::string& p, size_t i, const char* name)
{
	const size_t n = strlen(name);
	if (i + n > p.size() || _strnicmp(p.c_str() + i, name, n) != 0) return false;
	return (i == 0 || p[i - 1] == '\\' || p[i - 1] == '/') && (i + n == p.size() || p[i + n] == '\\' || p[i + n] == '/');
}

std::string UserLogsDir(const std::string& path, const std::string& configIni)
{
	// the last MODS folder (the 0148 rule's), and whether an Assets\DLC pair lies on the path
	size_t mods = std::string::npos;
	bool dlc = false;
	for (size_t i = 0; i < path.size(); ++i)
	{
		if (i != 0 && path[i - 1] != '\\' && path[i - 1] != '/') continue;
		if (IsComponent(path, i, "MODS")) mods = i;
		if (IsComponent(path, i, "Assets") && IsComponent(path, i + 7, "DLC")) dlc = true;
	}
	if (dlc)
	{
		// the modpack form: the patch is not under the user folder at all (its Mods folder is the DLC's own)
		const size_t cut = configIni.find_last_of("\\/");
		return cut != std::string::npos && cut > 0 ? Backslashes(configIni.substr(0, cut)) + "\\Logs\\VP64" : std::string();
	}
	if (mods != std::string::npos && mods >= 2) return path.substr(0, mods - 1) + "\\Logs\\VP64";
	return std::string();
}

// ================================================================================================ settings
Settings::Settings()
	: iniFound(false), modeOn(false), keepRuns(10), keepRunsSet(false), locDbSet(false), dllLogs(false), onDeathExit(true), strict(false),
	  connectTimeoutMs(60000), replySpinUs(0), idleSpinUs(0), singleThreadHost(false), counters(true),
	  localAnswers(true), localVerify(false), keepTemp(false), testSlotPoll(0), luaMirror(false), pack(false), failed(false),
	  uiBridge(false), uiBridgeTest(false)
{
}

static bool ParseBool(const std::string& v, bool& out)
{
	if (v == "1" || v == "on" || v == "true" || v == "yes") { out = true; return true; }
	if (v == "0" || v == "off" || v == "false" || v == "no") { out = false; return true; }
	return false;
}

void ParseSettings(const std::string& text, bool found, const std::string& iniPath, const std::string& proxyDir,
                   const std::string& exeDir, Settings& s)
{
	s = Settings();
	s.iniPath = iniPath;
	s.proxyDir = proxyDir;
	s.exeDir = exeDir;
	s.iniFound = found;
	s.modeOn = found;  // decision (2502): no ini = off; the patch always ships one
	std::string x64Dir, serverExe, runRoot, fallback;
	if (found)
	{
		size_t pos = 0;
		int lineNo = 0;
		while (pos <= text.size())
		{
			size_t e = text.find('\n', pos);
			if (e == std::string::npos) e = text.size();
			const std::string line = Trim(text.substr(pos, e - pos));
			pos = e + 1;
			++lineNo;
			if (line.empty() || line[0] == '#' || line[0] == ';') continue;
			char m[300];
			const size_t eq = line.find('=');
			if (eq == std::string::npos)
			{
				_snprintf_s(m, sizeof(m), _TRUNCATE, "ini line %d: no '=' (ignored)\n", lineNo);
				s.problems += m;
				continue;
			}
			const std::string k = Trim(line.substr(0, eq));
			std::string v = Trim(line.substr(eq + 1));
			if (v.size() >= 2 && v[0] == '"' && v[v.size() - 1] == '"') v = v.substr(1, v.size() - 2);
			bool b = false;
			if (k == "mode")
			{
				if (v == "on") s.modeOn = true;
				else if (v == "off") s.modeOn = false;
				else
				{
					_snprintf_s(m, sizeof(m), _TRUNCATE, "ini line %d: mode must be on or off (kept on)\n", lineNo);
					s.problems += m;
				}
			}
			else if (k == "fallback_dll") fallback = v;
			else if (k == "x64_dir") x64Dir = v;
			else if (k == "server_exe") serverExe = v;
			else if (k == "server_config") s.serverConfig = ResolvePath(proxyDir, v);
			else if (k == "run_root") runRoot = v;
			else if (k == "keep_runs")
			{
				const long n = strtol(v.c_str(), NULL, 10);
				s.keepRuns = n < 1 ? 1u : (uint32_t)n;
				s.keepRunsSet = true;   // ticket 0747
			}
			else if (k == "log") s.logPath = ResolvePath(proxyDir, v);
			else if (k == "game_dir") s.gameDir = ResolvePath(proxyDir, v);
			else if (k == "loc_db")
			{
				s.locDbSet = true;   // bug 0123: "none" (or empty) = explicitly no server localization
				s.locDb = v == "none" ? std::string() : ResolvePath(proxyDir, v);
			}
			else if (k == "server_allow") s.serverAllow = ResolvePath(proxyDir, v);
			else if (k == "dll_logs") s.dllLogs = atoi(v.c_str()) != 0;
			else if (k == "server_cfg_extra") s.serverCfgExtra = ResolvePath(proxyDir, v);  // issue 0307, TEST ONLY
			else if (k == "db") s.db = ResolvePath(proxyDir, v);
			else if (k == "server_args") s.serverArgs = v;
			else if (k == "on_server_death")
			{
				if (v == "exit") s.onDeathExit = true;
				else if (v == "continue") s.onDeathExit = false;
				else s.problems += "ini: on_server_death must be exit or continue (kept exit)\n";
			}
			else if (k == "strict") s.strict = atoi(v.c_str()) != 0;
			else if (k == "strict_allow") s.strictAllow = ResolvePath(proxyDir, v);
			else if (k == "connect_timeout_ms") s.connectTimeoutMs = (uint32_t)strtoul(v.c_str(), NULL, 10);
			else if (k == "reply_spin_us") s.replySpinUs = (uint32_t)strtoul(v.c_str(), NULL, 10);
			else if (k == "idle_spin_us") s.idleSpinUs = (uint32_t)strtoul(v.c_str(), NULL, 10);
			else if (k == "host")
			{
				if (v == "exe") s.singleThreadHost = false;
				else if (v == "headless") s.singleThreadHost = true;
				else
				{
					_snprintf_s(m, sizeof(m), _TRUNCATE, "ini line %d: host must be exe or headless (kept exe)\n", lineNo);
					s.problems += m;
				}
			}
			else if (k == "local_answers")
			{
				if (ParseBool(v, b)) s.localAnswers = b;
				else s.problems += "ini: local_answers must be 0 or 1 (kept 1)\n";
			}
			else if (k == "local_verify")
			{
				if (ParseBool(v, b)) s.localVerify = b;
				else s.problems += "ini: local_verify must be 0 or 1 (kept 0)\n";
			}
			else if (k == "keep_temp")
			{
				if (ParseBool(v, b)) s.keepTemp = b;
				else s.problems += "ini: keep_temp must be 0 or 1 (kept 0)\n";
			}
			else if (k == "local_test")
			{
				// Issue 2805: luastale = the UI-Lua mirror answers its entries whatever their epoch (negative test).
				if (v.empty() || v == "stale" || v == "frozen" || v == "luastale") s.localTest = v;
				else s.problems += "ini: local_test must be empty, stale, frozen or luastale (ignored)\n";
			}
			else if (k == "lua_mirror")
			{
				// Issue 2805: the UI-Lua mirror (proxy_local.h); 0 = shadow counters only.
				if (ParseBool(v, b)) s.luaMirror = b;
				else s.problems += "ini: lua_mirror must be 0 or 1 (kept 0)\n";
			}
			else if (k == "test_slot_poll")
			{
				// Issue 2805: a fake slot poll for offline verify runs (proxy_update.cpp); 0..1000 passes per Update.
				const long n = strtol(v.c_str(), NULL, 10);
				if (n >= 0 && n <= 1000) s.testSlotPoll = (uint32_t)n;
				else s.problems += "ini: test_slot_poll must be 0..1000 (kept 0)\n";
			}
			else if (k == "pack")
			{
				// Issue 3504: 1 = x64_dir is a DLL pack (proxy_pack.h): checked at boot; every failure a player can meet
				// boots the 32-bit fallback_dll instead. 0 (default) = development layouts: no check, no fallback.
				if (ParseBool(v, b)) s.pack = b;
				else s.problems += "ini: pack must be 0 or 1 (kept 0)\n";
			}
			else if (k == "ui_bridge")
			{
				// Issue 0178: the Lua bridge (proxy_ui_bridge.h); 0 (default) = not installed.
				if (ParseBool(v, b)) s.uiBridge = b;
				else s.problems += "ini: ui_bridge must be 0 or 1 (kept 0)\n";
			}
			else if (k == "ui_bridge_test")
			{
				// Issue 0178: the bridge's test table VP64UiBridgeTest and UnitMoving records (tests only).
				if (ParseBool(v, b)) s.uiBridgeTest = b;
				else s.problems += "ini: ui_bridge_test must be 0 or 1 (kept 0)\n";
			}
			else if (k == "handin") {}  // issues 3520/3521 (D122): the popup helper's hand-in line; not the proxy's
			else if (k == "leader_scenes") {}  // ticket 0381: read and checked by proxy_leaderscene.cpp (DecideMode)
			else if (k == "failed")
			{
				// Issue 3523 (D119, D125): written by vp64helper failed after a D33-session failure; 1 = the boot behaves
				// as mode=off until vp64helper on clears it (no automatic retry).
				if (ParseBool(v, b)) s.failed = b;
				else s.problems += "ini: failed must be 0 or 1 (kept 0)\n";
			}
			else if (k == "counters")
			{
				if (ParseBool(v, b)) s.counters = b;
				else
				{
					_snprintf_s(m, sizeof(m), _TRUNCATE, "ini line %d: counters must be 0 or 1 (kept 1)\n", lineNo);
					s.problems += m;
				}
			}
			else
			{
				_snprintf_s(m, sizeof(m), _TRUNCATE, "ini line %d: unknown key '%s' (ignored)\n", lineNo, k.c_str());
				s.problems += m;
			}
		}
	}
	s.fallbackDll = ResolvePath(proxyDir, fallback.empty() ? std::string("CvGameCore_Expansion2_x86.dll") : fallback);
	s.x64Dir = ResolvePath(proxyDir, x64Dir.empty() ? std::string("VP64") : x64Dir);
	s.serverExe = serverExe.empty() ? s.x64Dir + "\\server.exe" : ResolvePath(proxyDir, serverExe);
	s.runRoot = runRoot.empty() ? s.x64Dir + "\\run" : ResolvePath(proxyDir, runRoot);
	s.x64Dll = s.x64Dir + "\\" + X64DllName(s.x64Dir, std::string());  // issue 2606
	if (s.gameDir.empty()) s.gameDir = exeDir;
}

// Issue 0117: the player's game language and logging switch, from the game's own config.ini (the file the EXE reads:
// [User Settings] Language = en_US "The currently selected language", [Debug] LoggingEnabled = 0|1). Keys are matched
// exactly (SteamLanguage, AudioLanguage are other keys) in any section; the first occurrence wins.
void ReadGameConfig(const std::string& text, std::string& language, int& logging)
{
	language.clear();
	logging = -1;
	size_t i = 0;
	while (i < text.size())
	{
		size_t e = text.find('\n', i);
		if (e == std::string::npos) e = text.size();
		std::string line = text.substr(i, e - i);
		i = e + 1;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
		if (k.empty() || k[0] == ';' || k[0] == '#' || k[0] == '[') continue;
		if (k == "Language" && language.empty())
		{
			// ll_CC only (en_US, de_DE, zh_Hant_HK is not a Civ 5 code); anything else is ignored (server default en_US)
			bool ok = v.size() == 5 && v[2] == '_' && islower((unsigned char)v[0]) && islower((unsigned char)v[1]) &&
			          isupper((unsigned char)v[3]) && isupper((unsigned char)v[4]);
			if (ok) language = v;
		}
		else if (k == "LoggingEnabled" && logging < 0)
		{
			if (v == "0") logging = 0;
			else if (v == "1") logging = 1;
		}
	}
}

// The game's config.ini: Documents\My Games\Sid Meier's Civilization 5\config.ini through SHGetFolderPathW, the call the
// EXE itself uses (a test instance's shim redirects it, so a test game reads its own copy, never the owner's). Looked up with
// GetProcAddress so the proxy and its unit test do not link shell32. VPPROXY_TEST_CONFIG_INI=<path> (tests only)
// names another file. Returns "" when nothing can be read; `path` gets the file tried.
static std::string ReadPlayerConfigIni(std::string& path)
{
	path.clear();
	char env[MAX_PATH];
	DWORD n = GetEnvironmentVariableA("VPPROXY_TEST_CONFIG_INI", env, MAX_PATH);
	if (n > 0 && n < MAX_PATH) path = env;
	else
	{
		typedef HRESULT(WINAPI * PFN)(HWND, int, HANDLE, DWORD, LPWSTR);
		HMODULE sh = LoadLibraryA("shell32.dll");
		PFN f = sh ? (PFN)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
		wchar_t w[MAX_PATH];
		if (!f || f(NULL, 0x0005 /* CSIDL_PERSONAL */, NULL, 0, w) != S_OK) return std::string();
		char a[MAX_PATH * 2];
		if (!WideCharToMultiByte(CP_ACP, 0, w, -1, a, sizeof(a), NULL, NULL)) return std::string();
		path = std::string(a) + "\\My Games\\Sid Meier's Civilization 5\\config.ini";
	}
	FILE* fp = NULL;
	if (fopen_s(&fp, path.c_str(), "rb") || !fp) return std::string();
	std::string text;
	char buf[4096];
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, got);
	fclose(fp);
	return text;
}

std::string ServerConfigText(const Settings& s, const std::string& runDir)
{
	char n[32];
	std::string t = "# remote-mode server.cfg written by the proxy (vpproxy, issue 2502) for one boot; the D28 layout\n";
	// issue 0117: the game's language and LoggingEnabled reach the server (it ran in en_US with DLL logs off whatever the
	// player had chosen)
	std::string cfgPath, language;
	int logging = -1;
	ReadGameConfig(ReadPlayerConfigIni(cfgPath), language, logging);
	t += "# game language and logging from " + (cfgPath.empty() ? std::string("(no config.ini found)") : cfgPath) + "\n";
	if (!language.empty()) t += "language=" + language + "\n";
	t += "run_dir=" + runDir + "\n";
	// issue 2606: the x64 DLL by its name in this layout (the checked pack's, else _x64 when present, else the old one)
	t += "dll=" + (s.x64Dll.empty() ? s.x64Dir + "\\" + X64DllName(s.x64Dir, std::string()) : s.x64Dll) + "\n";
	t += "lua_dll=" + s.x64Dir + "\\lua51_x64.dll\n";
	t += "db_dll=" + s.x64Dir + "\\CvGameDatabase_x64.dll\n";
	t += "loc_dll=" + s.x64Dir + "\\CvLocalization_x64.dll\n";
	// bug 0123: without loc_db in vpproxy.ini the server got none and ran unlocalized; the default is now the game's own
	// cache database beside its config.ini (Documents\My Games\Sid Meier's Civilization 5\cache, the instance's own folder
	// under a test shim), which the EXE builds for the player's language and active mods before the DLL loads
	std::string locDb = s.locDb;
	if (s.locDbSet)
		t += std::string("# loc_db from vpproxy.ini") + (locDb.empty() ? " (none: no server localization)" : "") + "\n";
	else
	{
		const size_t slash = cfgPath.find_last_of("\\/");
		const std::string cand =
		    slash == std::string::npos ? std::string() : cfgPath.substr(0, slash) + "\\cache\\Localization-Merged.db";
		const DWORD at = cand.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesA(cand.c_str());
		if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY))
		{
			locDb = cand;
			t += "# loc_db: the game's cache (default)\n";
		}
		else
			t += "# loc_db: not found: " + (cand.empty() ? std::string("(no config.ini path)") : cand) +
			     " (no server localization)\n";
	}
	t += "loc_db=" + locDb + "\n";
	t += "game_dir=" + s.gameDir + "\n";
	// dll_logs: vpproxy.ini dll_logs=1 (test switch) or the player's LoggingEnabled = 1, as the stock game logs
	t += std::string("dll_logs=") + ((s.dllLogs || logging == 1) ? "1" : "0") + "\n";
	t += std::string("fail_on_unexpected=") + (s.strict ? "1" : "0") + "\n";
	if (!s.serverAllow.empty()) t += "unexpected_allow=" + s.serverAllow + "\n";
	_snprintf_s(n, sizeof(n), _TRUNCATE, "%u", s.connectTimeoutMs);
	t += std::string("connect_timeout_ms=") + n + "\n";
	return t;
}

// Issue 0307 (TEST ONLY): server_cfg_extra=.
bool ParseServerCfgExtra(const std::string& text, std::vector<std::string>& lines, std::string& why)
{
	lines.clear();
	why.clear();
	size_t pos = 0;
	int lineNo = 0;
	while (pos < text.size())
	{
		size_t e = text.find('\n', pos);
		if (e == std::string::npos) e = text.size();
		const std::string line = Trim(text.substr(pos, e - pos));
		pos = e + 1;
		++lineNo;
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		const size_t eq = line.find('=');
		const std::string k = eq == std::string::npos ? std::string() : Trim(line.substr(0, eq));
		if (k.empty())
		{
			char m[96];
			_snprintf_s(m, sizeof(m), _TRUNCATE, "line %d: %s", lineNo,
			            eq == std::string::npos ? "no '='" : "an empty key");
			why = m;
			lines.clear();
			return false;
		}
		lines.push_back(k + "=" + Trim(line.substr(eq + 1)));
	}
	return true;
}

bool LoadServerCfgExtra(const std::string& path, std::vector<std::string>& lines, std::string& why)
{
	lines.clear();
	FILE* fp = NULL;
	if (fopen_s(&fp, path.c_str(), "rb") || !fp)
	{
		why = "cannot be read (no such file?)";
		return false;
	}
	std::string text;
	char buf[4096];
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, got);
	const bool readErr = ferror(fp) != 0;
	fclose(fp);
	if (readErr)
	{
		why = "read error";
		return false;
	}
	return ParseServerCfgExtra(text, lines, why);
}

std::string ServerConfigFile(const std::string& base, const std::string& optionsFile, const std::string& shellCfg,
                             const std::string& extraPath, const std::vector<std::string>& extra)
{
	std::string t = base;
	if (!optionsFile.empty()) t += "options_file=" + optionsFile + "\n";
	t += shellCfg;
	if (!extraPath.empty())
	{
		t += "# TEST ONLY: server_cfg_extra " + extraPath + " (issue 0307; a later key wins)\n";
		for (size_t i = 0; i < extra.size(); ++i) t += extra[i] + "\n";
	}
	return t;
}

// ================================================================================================ run folders
std::string RunFolderName(const SYSTEMTIME& t, uint32_t pid, uint32_t n)
{
	char b[64];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%04u%02u%02u-%02u%02u%02u-%u-%u", t.wYear, t.wMonth, t.wDay, t.wHour,
	            t.wMinute, t.wSecond, pid, n);
	return b;
}

static bool Digits(const std::string& s, size_t a, size_t n)
{
	if (a + n > s.size()) return false;
	for (size_t i = a; i < a + n; ++i)
		if (s[i] < '0' || s[i] > '9') return false;
	return true;
}

bool IsRunFolderName(const std::string& name)
{
	// 8 digits '-' 6 digits '-' digits '-' digits
	if (name.size() < 19 || !Digits(name, 0, 8) || name[8] != '-' || !Digits(name, 9, 6) || name[15] != '-')
		return false;
	size_t i = 16, d = 0;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9') ++i, ++d;
	if (!d || i >= name.size() || name[i] != '-') return false;
	++i;
	d = 0;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9') ++i, ++d;
	return d > 0 && i == name.size();
}

std::vector<std::string> RunFoldersToPrune(const std::vector<std::string>& existing, uint32_t keep)
{
	std::vector<std::string> runs;
	for (size_t i = 0; i < existing.size(); ++i)
		if (IsRunFolderName(existing[i])) runs.push_back(existing[i]);
	std::sort(runs.begin(), runs.end());  // the name starts with the time: oldest first
	const size_t keepOld = keep > 0 ? keep - 1 : 0;  // one more is about to be created
	std::vector<std::string> out;
	if (runs.size() > keepOld) out.assign(runs.begin(), runs.end() - keepOld);
	return out;
}

uint32_t EffectiveKeepRuns(bool optFromFile, int optValue, bool iniSet, uint32_t iniValue, std::string* why)
{
	char b[160];
	uint32_t n;
	if (optFromFile)
	{
		n = optValue < 1 ? 1u : (uint32_t)optValue;
		_snprintf_s(b, sizeof(b), _TRUNCATE, "[Logs] KeepRuns = %d from the options file%s", optValue,
		            iniSet ? " (it wins over vpproxy.ini keep_runs)" : "");
	}
	else if (iniSet)
	{
		n = iniValue < 1 ? 1u : iniValue;
		_snprintf_s(b, sizeof(b), _TRUNCATE, "vpproxy.ini keep_runs=%u (a test switch; [Logs] KeepRuns is not set in the "
		            "options file)", iniValue);
	}
	else
	{
		n = optValue < 1 ? 1u : (uint32_t)optValue;
		_snprintf_s(b, sizeof(b), _TRUNCATE, "[Logs] KeepRuns = %d (default)", optValue);
	}
	if (n > 100) n = 100;
	if (why) *why = b;
	return n;
}

// "<digits>" at s[i..]: advances i past them; false when there is none
static bool SkipDigits(const std::string& s, size_t& i)
{
	const size_t a = i;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
	return i > a;
}

static bool StartsNoCase(const std::string& s, size_t at, const char* p)
{
	const size_t n = strlen(p);
	return s.size() >= at + n && _strnicmp(s.c_str() + at, p, n) == 0;
}

bool IsRunDatabaseName(const std::string& name)
{
	size_t i = 0;
	if (StartsNoCase(name, 0, "Civ5DebugDatabase."))
	{
		i = 18;
		if (!SkipDigits(name, i)) return false;
	}
	else if (StartsNoCase(name, 0, "units-"))
	{
		i = 6;
		if (!SkipDigits(name, i) || i >= name.size() || name[i] != '.') return false;
		++i;
		if (!SkipDigits(name, i)) return false;
	}
	else
		return false;
	if (!StartsNoCase(name, i, ".db")) return false;
	i += 3;
	if (i == name.size()) return true;
	static const char* const kSqlite[] = {"-journal", "-wal", "-shm"};
	for (size_t k = 0; k < sizeof(kSqlite) / sizeof(kSqlite[0]); ++k)
		if (name.size() == i + strlen(kSqlite[k]) && StartsNoCase(name, i, kSqlite[k])) return true;
	return false;
}

std::vector<std::string> RunDatabasesToDelete(const std::vector<std::string>& names)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < names.size(); ++i)
		if (IsRunDatabaseName(names[i])) out.push_back(names[i]);
	return out;
}

bool DeleteRunDatabases(bool waited, uint32_t exitCode, bool reported, std::string* why)
{
	char b[160];
	b[0] = 0;
	if (!waited)
		_snprintf_s(b, sizeof(b), _TRUNCATE, "the server did not end by itself after kRt_Shutdown");
	else if (exitCode != 0)
		_snprintf_s(b, sizeof(b), _TRUNCATE, "the server exited with code %u, not 0", exitCode);
	else if (reported)
		_snprintf_s(b, sizeof(b), _TRUNCATE, "an error report was written in this load");
	if (why) *why = b;
	return b[0] == 0;
}

// ================================================================================================ allow-list
void AllowList::Parse(const std::string& text)
{
	keys.clear();
	reasons.clear();
	problems.clear();
	size_t pos = 0;
	int lineNo = 0;
	while (pos <= text.size())
	{
		size_t e = text.find('\n', pos);
		if (e == std::string::npos) e = text.size();
		const std::string line = Trim(text.substr(pos, e - pos));
		pos = e + 1;
		++lineNo;
		if (line.empty() || line[0] == '#') continue;
		size_t k = 0;
		while (k < line.size() && line[k] != ' ' && line[k] != '\t') ++k;
		const std::string key = line.substr(0, k);
		const std::string why = Trim(line.substr(k));
		if (why.empty())
		{
			char m[400];
			_snprintf_s(m, sizeof(m), _TRUNCATE, "line %d: '%s' has no reason (not honoured)\n", lineNo, key.c_str());
			problems += m;
			continue;
		}
		keys.push_back(key);
		reasons.push_back(why);
	}
}

const char* AllowList::Match(const std::string& key) const
{
	const std::string bare = key.substr(0, key.find('('));
	for (size_t i = 0; i < keys.size(); ++i)
	{
		const std::string& r = keys[i];
		if (!r.empty() && r[r.size() - 1] == '*')
		{
			const std::string pre = r.substr(0, r.size() - 1);
			if (key.compare(0, pre.size(), pre) == 0 || bare.compare(0, pre.size(), pre) == 0) return r.c_str();
		}
		else if (r == key || r == bare)
			return r.c_str();
	}
	return NULL;
}

// ================================================================================================ method roles
Role RoleOfKey(const char* key)
{
	if (!key) return kRoleNone;
	if (strcmp(key, "ICvGame1::Update()") == 0) return kRoleUpdate;
	if (strcmp(key, "ICvGameContext1::GetGame()") == 0) return kRoleGetGame;
	if (strcmp(key, "ICvGame1::GetActivePlayer()") == 0) return kRoleGetActivePlayer;
	if (strcmp(key, "ICvPlayer1::IsHuman() const") == 0) return kRoleIsHuman;
	if (strcmp(key, "ICvPlayer2::HasTurnTimerExpired()") == 0) return kRoleTurnTimer;
	return kRoleNone;
}

// ================================================================================================ thread binding
uint32_t ChooseChannel(bool isBootThread, Role firstRole, volatile LONG* coreClaimed, const char** reason)
{
	const char* dummy = NULL;
	if (!reason) reason = &dummy;
	if (isBootThread)
	{
		*reason = "the boot thread (it called DllGetGameContext)";
		return kChanUi;
	}
	if (firstRole == kRoleUpdate || firstRole == kRoleGetGame)
	{
		if (InterlockedCompareExchange(coreClaimed, 1, 0) == 0)
		{
			*reason = "the first non-boot thread whose first call is GetGame or Update (the game-core role)";
			return kChanCore;
		}
		*reason = "first call GetGame or Update, but the core channel is already bound to another thread";
		return kChanAux;
	}
	*reason = "a non-boot thread whose first call is not GetGame or Update";
	return kChanAux;
}

uint32_t CheckCall(uint32_t chan, bool isBootThread, bool topLevel, Role role, bool singleThreadHost)
{
	uint32_t v = 0;
	if (role == kRoleUpdate && chan != kChanCore && !singleThreadHost) v |= kViolUpdateOffCore;
	if (chan == kChanUi && !isBootThread && topLevel) v |= kViolUiForeign;
	return v;
}

const char* ChannelName(uint32_t chan)
{
	switch (chan)
	{
	case kChanUi: return "ui";
	case kChanCore: return "core";
	case kChanAux: return "aux";
	default: return "?";
	}
}

// ================================================================================================ frames
FrameTracker::FrameTracker() : armed(false), frames(0), timerCalls(0), lastQpc(0), windowFrames(0), ringNext(0)
{
}

bool FrameTracker::OnUiCall(Role role, int64_t qpc, int64_t qpcFreq)
{
	switch (role)
	{
	case kRoleGetActivePlayer: armed = true; return false;
	case kRoleIsHuman: armed = false; return false;
	case kRoleTurnTimer: break;
	default: return false;
	}
	++timerCalls;
	if (!armed) return false;
	armed = false;
	++frames;
	if (lastQpc && qpcFreq > 0 && qpc >= lastQpc)
	{
		const double us = (double)(qpc - lastQpc) * 1e6 / (double)qpcFreq;
		const uint32_t v = us >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)us;
		if (intervalsUs.size() < kCap) intervalsUs.push_back(v);
		else intervalsUs[ringNext] = v;
		ringNext = (ringNext + 1) % kCap;
		++windowFrames;
	}
	lastQpc = qpc;
	return true;
}

FrameTracker::Stats FrameTracker::TakeWindow()
{
	Stats st;
	memset(&st, 0, sizeof(st));
	st.intervals = windowFrames;
	st.dropped = windowFrames > intervalsUs.size() ? windowFrames - intervalsUs.size() : 0;
	if (!intervalsUs.empty())
	{
		std::vector<uint32_t> v(intervalsUs);
		std::sort(v.begin(), v.end());
		double sum = 0;
		for (size_t i = 0; i < v.size(); ++i) sum += v[i];
		const size_t n = v.size();
		st.meanMs = sum / (double)n / 1000.0;
		st.p50Ms = v[(size_t)(0.50 * (double)(n - 1))] / 1000.0;
		st.p99Ms = v[(size_t)(0.99 * (double)(n - 1))] / 1000.0;
		st.maxMs = v[n - 1] / 1000.0;
	}
	intervalsUs.clear();
	ringNext = 0;
	windowFrames = 0;
	return st;
}

// ================================================================================================ traffic counters
void Traffic::Init(uint32_t kinds)
{
	rows = kinds + 3u;
	cells.assign(rows * 9u, 0);
	last.assign(rows * 9u, 0);
}

void FormatSnapshot(std::string& out, bool header, uint32_t seq, const std::string& label, double timeS,
                    double windowS, uint64_t frames, uint64_t timerCalls, const FrameTracker::Stats& st, Traffic& t,
                    RowNameFn rowName)
{
	if (header)
		out += "snapshot\tlabel\ttime_s\twindow_s\tframes\tfps\tframe_ms_mean\tframe_ms_p50\tframe_ms_p99\tframe_ms_max\t"
		       "frame_intervals\ttimer_calls\tinterface\tdirection\tchannel\tcalls\tcalls_per_frame\n";
	char head[512];
	_snprintf_s(head, sizeof(head), _TRUNCATE, "%u\t%s\t%.3f\t%.3f\t%I64u\t%.2f\t%.3f\t%.3f\t%.3f\t%.3f\t%I64u\t%I64u", seq,
	            label.c_str(), timeS, windowS, frames, windowS > 0 ? (double)frames / windowS : 0.0, st.meanMs, st.p50Ms,
	            st.p99Ms, st.maxMs, st.intervals, timerCalls);
	uint64_t total = 0, local = 0;
	std::vector<uint64_t> d(t.cells.size());
	for (size_t i = 0; i < t.cells.size(); ++i)
	{
		d[i] = t.cells[i] - t.last[i];
		if ((i / 3u) % 3u == 2u)
			local += d[i];  // issue 2700: direction 2 = answered locally (not a crossing)
		else
			total += d[i];
	}
	char row[256];
	_snprintf_s(row, sizeof(row), _TRUNCATE, "\t*\t*\t*\t%I64u\t%.2f\n", total, frames ? (double)total / (double)frames : 0.0);
	out += head;
	out += row;
	if (local)
	{
		_snprintf_s(row, sizeof(row), _TRUNCATE, "\t*\tlocal\t*\t%I64u\t%.2f\n", local,
		            frames ? (double)local / (double)frames : 0.0);
		out += head;
		out += row;
	}
	static const char* const dirs[3] = {"out", "in", "local"};
	for (uint32_t r = 0; r < t.rows; ++r)
		for (uint32_t dir = 0; dir < 3; ++dir)
			for (uint32_t ch = 0; ch < 3; ++ch)
			{
				const uint64_t n = d[(r * 3u + dir) * 3u + ch];
				if (!n) continue;
				const char* name = rowName ? rowName(r) : NULL;
				char nb[32];
				if (!name)
				{
					_snprintf_s(nb, sizeof(nb), _TRUNCATE, "row%u", r);
					name = nb;
				}
				_snprintf_s(row, sizeof(row), _TRUNCATE, "\t%s\t%s\t%s\t%I64u\t%.2f\n", name, dirs[dir], ChannelName(ch), n,
				            frames ? (double)n / (double)frames : 0.0);
				out += head;
				out += row;
			}
	t.last = t.cells;
}
// ================================================================================================ engine handle slots
uint32_t SlotAllocator::Alloc()
{
	if (Waiting() > m_quarantine)
	{
		const uint32_t slot = m_free[m_head++];
		if (m_head >= 4096 && m_head * 2 >= m_free.size())
		{
			m_free.erase(m_free.begin(), m_free.begin() + m_head);
			m_head = 0;
		}
		return slot;
	}
	return m_slots++;
}

void SlotAllocator::Free(uint32_t slot)
{
	if (slot < m_slots) m_free.push_back(slot);
}

// ================================================================================================ RETURN payload ring
const uint8_t* ReturnRing::Keep(const uint8_t* p, uint32_t n)
{
	std::vector<uint8_t>& b = m_bufs[m_next++ % (uint32_t)kSlots];
	// Release a large buffer before reuse (assign would keep its capacity forever), and give a large payload a buffer of
	// exactly its size (assign into a smaller non-empty buffer grows it to 1.5 times the old capacity).
	if (b.capacity() > m_keepMax || (n > m_keepMax && b.capacity() < n)) std::vector<uint8_t>().swap(b);
	b.assign(p, p + n);
	return b.empty() ? NULL : &b[0];
}

size_t ReturnRing::Capacity() const
{
	size_t c = 0;
	for (uint32_t i = 0; i < (uint32_t)kSlots; ++i) c += m_bufs[i].capacity();
	return c;
}
}  // namespace ProxyLogic
