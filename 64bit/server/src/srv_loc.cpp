// Localization in the server (issue 1704): the dictionary is initialised at boot, before the DLL's Init, as the EXE
// does, through the real CvLocalization_x64.dll (tools/x64-dll/cvlocalization/, issue 1701) that the DLL imports, so the
// DLL's Localization::Lookup calls and the Lua Locale table read the same dictionary.
//
// Boot (research/sources/civ5-headless/history/host-plan.md H9; the 32-bit reference host passes the same values):
// Localization::Initialize(<game>\Locale\icudt44l.dat, <run copy of Localization-Merged.db>, "Languages", "en_US",
// false = fallback languages on), with a logger writing <run>\Logs\Localization.log.
//
// Lua: Locale.Lookup / Locale.ConvertTextKey(key, ...) look the key up and push the extra arguments as {1}, {2}, ...
// (numbers: int when integral, else double; booleans: int 0/1; strings with their length; anything else ""), then
// compose form 1, as the 32-bit host does. HasTextKey asks the dictionary (StringDictionary::HasKey). ToUpper/ToLower
// (LCMapStringEx, linguistic casing), Compare (CompareStringEx), Length (UTF-16 units), ToNumber (the library's number
// transform) and GetCurrentLanguage are server implementations over Windows/the library [I]: the EXE's are not
// documented; map scripts and the DLL's own Lua use only Lookup, ConvertTextKey and IsNilOrWhitespace (grep of the
// toolkit game folder and the fork, 2026-11-10).
#include "srv_common.h"
#include "CvLocalization.h"
#include <stdio.h>
#include <string>

namespace
{
	bool s_bLoc = false;
	Database::Connection* s_pLocDb = NULL;

	class SrvLocLogger : public Localization::ILocalizationLogger
	{
	public:
		explicit SrvLocLogger(FILE* f) : m_f(f) {}
		~SrvLocLogger() override
		{
			if(m_f)
				fclose(m_f);
		}
		void Log(const char* szMessage, LogTypes eType) override
		{
			static const char* const kType[] = { "message", "warning", "error" };
			if(m_f)
			{
				fprintf(m_f, "%s: %s\n", (unsigned)eType < 3 ? kType[eType] : "?", szMessage ? szMessage : "");
				fflush(m_f);
			}
		}
	private:
		FILE* m_f;
	};

	// Windows locale name of the current language ("en_US" -> "en-US").
	std::wstring LocaleName()
	{
		std::string s = s_bLoc ? Localization::GetCurrentLanguage().GetType() : "en_US";
		std::wstring w(s.begin(), s.end());
		for(size_t i = 0; i < w.size(); ++i)
			if(w[i] == L'_')
				w[i] = L'-';
		return w;
	}

	std::wstring Widen(const char* s, size_t n)
	{
		if(!n)
			return std::wstring();
		const int k = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
		std::wstring w(k, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s, (int)n, &w[0], k);
		return w;
	}

	void PushNarrow(lua_State* L, const std::wstring& w)
	{
		if(w.empty())
		{
			lua_pushliteral(L, "");
			return;
		}
		const int k = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
		std::string s(k, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], k, NULL, NULL);
		lua_pushlstring(L, s.data(), s.size());
	}

	void PushArgs(lua_State* L, Localization::String& str, int iFirst)
	{
		const int n = lua_gettop(L);
		for(int i = iFirst; i <= n; ++i)
		{
			switch(lua_type(L, i))
			{
			case LUA_TNUMBER:
			{
				const lua_Number d = lua_tonumber(L, i);
				if(d >= -2147483648.0 && d <= 2147483647.0 && (lua_Number)(int)d == d)
					str.PushArgument((int)d);
				else
					str.PushArgument((double)d);
				break;
			}
			case LUA_TBOOLEAN: str.PushArgument(lua_toboolean(L, i) ? 1 : 0); break;
			case LUA_TSTRING:
			{
				size_t len = 0;
				const char* s = lua_tolstring(L, i, &len);
				str.PushArgument(s, len);
				break;
			}
			default: str.PushArgument(""); break;
			}
		}
	}

	void PushComposed(lua_State* L, Localization::String& str, const char* szFallback, size_t lenFallback)
	{
		size_t bytes = 0;
		const char* sz = str.toUTF8(bytes, 1);
		if(sz)
			lua_pushlstring(L, sz, bytes);
		else
			lua_pushlstring(L, szFallback, lenFallback);
	}

	// Locale.Lookup(key, ...) / Locale.ConvertTextKey(key, ...)
	int LuaLookup(lua_State* L)
	{
		size_t len = 0;
		const char* szKey = luaL_checklstring(L, 1, &len);
		Localization::String str = Localization::Lookup(szKey);
		PushArgs(L, str, 2);
		PushComposed(L, str, szKey, len);
		return 1;
	}

	int LuaHasTextKey(lua_State* L)
	{
		size_t len = 0;
		const char* szKey = lua_tolstring(L, 1, &len);
		lua_pushboolean(L, s_bLoc && szKey && len && Localization::GetStringDictionary().HasKey(szKey, (unsigned)len));
		return 1;
	}

	int MapCase(lua_State* L, DWORD dwFlags)
	{
		size_t len = 0;
		const char* s = luaL_checklstring(L, 1, &len);
		const std::wstring w = Widen(s, len);
		if(w.empty())
		{
			lua_pushliteral(L, "");
			return 1;
		}
		const std::wstring loc = LocaleName();
		const DWORD f = dwFlags | LCMAP_LINGUISTIC_CASING;
		const int k = LCMapStringEx(loc.c_str(), f, w.data(), (int)w.size(), NULL, 0, NULL, NULL, 0);
		if(k <= 0)
		{
			lua_pushlstring(L, s, len);
			return 1;
		}
		std::wstring out(k, L'\0');
		LCMapStringEx(loc.c_str(), f, w.data(), (int)w.size(), &out[0], k, NULL, NULL, 0);
		PushNarrow(L, out);
		return 1;
	}
	int LuaToUpper(lua_State* L) { return MapCase(L, LCMAP_UPPERCASE); }
	int LuaToLower(lua_State* L) { return MapCase(L, LCMAP_LOWERCASE); }

	int LuaLength(lua_State* L)
	{
		size_t len = 0;
		const char* s = luaL_checklstring(L, 1, &len);
		lua_pushinteger(L, (lua_Integer)Widen(s, len).size());
		return 1;
	}

	int LuaCompare(lua_State* L)
	{
		size_t la = 0, lb = 0;
		const char* a = luaL_checklstring(L, 1, &la);
		const char* b = luaL_checklstring(L, 2, &lb);
		const std::wstring wa = Widen(a, la), wb = Widen(b, lb), loc = LocaleName();
		const int r = CompareStringEx(loc.c_str(), 0, wa.c_str(), (int)wa.size(), wb.c_str(), (int)wb.size(), NULL, NULL, 0);
		lua_pushinteger(L, r == CSTR_LESS_THAN ? -1 : r == CSTR_GREATER_THAN ? 1 : 0);
		return 1;
	}

	// Locale.ToNumber(n[, pattern]): the library's number transform ({1: number "pattern"}).
	int LuaToNumber(lua_State* L)
	{
		const lua_Number d = luaL_checknumber(L, 1);
		const char* szPattern = lua_isstring(L, 2) ? lua_tostring(L, 2) : NULL;
		std::string text = "{1: number";
		if(szPattern)
			text += std::string(" \"") + szPattern + "\"";
		text += "}";
		Localization::String str(text.c_str());
		str.PushArgument((double)d);
		char sz[64];
		sprintf_s(sz, "%.14g", (double)d);
		PushComposed(L, str, sz, strlen(sz));
		return 1;
	}

	int LuaGetCurrentLanguage(lua_State* L)
	{
		lua_newtable(L);
		lua_pushstring(L, s_bLoc ? Localization::GetCurrentLanguage().GetType() : "en_US");
		lua_setfield(L, -2, "Type");
		return 1;
	}

	const char* kLocaleLua = R"LUA(
local Locale = Locale
function Locale.IsNilOrWhitespace(s) return s == nil or tostring(s):match("^%s*$") ~= nil end
function Locale.IsNilOrEmpty(s) return s == nil or s == "" end
-- Any other member: returns its first argument (not used by the scripts the server runs).
setmetatable(Locale, { __index = function(t, k)
	local f = function(s) return s end
	rawset(t, k, f)
	return f
end })
)LUA";
}

bool LocStartup(const char* szLocDll, const char* szLocDb, const char* szIcuDat, const char* szLanguage, std::string& why)
{
	wchar_t wsz[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, szLocDll, -1, wsz, MAX_PATH);
	if(!LoadLibraryExW(wsz, NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
	{
		why = std::string("cannot load ") + szLocDll;
		return false;
	}
	s_pLocDb = new Database::Connection();
	if(!s_pLocDb->Open(szLocDb, Database::OPEN_READONLY))
	{
		why = std::string("cannot open the localization database ") + szLocDb;
		return false;
	}
	char szLog[MAX_PATH];
	sprintf_s(szLog, "%s\\Logs\\Localization.log", SrvRunDir());
	FILE* f = _fsopen(szLog, "w", _SH_DENYWR);
	Localization::SetLogger(new SrvLocLogger(f));	// the library owns it from here (deleted by SetLogger/Shutdown)
	MultiByteToWideChar(CP_ACP, 0, szIcuDat, -1, wsz, MAX_PATH);
	if(!Localization::Initialize(wsz, *s_pLocDb, "Languages", szLanguage, false))
	{
		why = std::string("Localization::Initialize failed (ICU data ") + szIcuDat + ", language " + szLanguage + ")";
		return false;
	}
	s_bLoc = true;
	char sz[1024];
	sprintf_s(sz, "%s, database %s, ICU data %s, language %s (fallback on), %u languages", szLocDll, szLocDb, szIcuDat,
	          Localization::GetCurrentLanguage().GetType(), (unsigned)Localization::GetStringDictionary().GetLanguageCount());
	why = sz;
	return true;
}

// Issue 2802 (bug 0086): the EXE attaches the localization database to the game database (`ATTACH ? AS Localization`,
// research/03-dll-engine-boundary/server-host.md section 10; the proxy sees it in PRAGMA database_list), and the DLL
// relies on it: CvGame::GetDiploResponse (CvGame.cpp:5961) selects its text keys with
// "select Tag, Bias from Diplomacy_Responses, Language_en_US ...", a view that exists only in Localization-Merged.db.
// Without the attachment the query fails, the response is an empty Localization::String, and every text built from
// it is empty: notification messages (public declarations, CvTeam.cpp:4163), leaderhead speech
// (GameplayDiplomacyAILeaderMessage), Game.GetDiploResponse in the UI's Lua. The random pick in it is stateless
// (randRangeExclusive is const), so attaching changes text only, never the game.
// server.cfg loc_attach: engine (default) attaches the file the engine has attached as Localization (remote mode; the
// 32-bit headless host attaches nothing, so headless proof runs stay as the 32-bit reference); 1 attaches it too and,
// when the engine has none (direct mode, headless host), the server's loc_db; 0 never (the old behaviour).
bool LocAttachGameDb(Database::Connection& db, const char* szEnginePath, std::string& why)
{
	why.clear();
	const std::string mode = SrvCfgStr("loc_attach", "engine");
	const std::string engine = szEnginePath ? szEnginePath : "";
	std::string path, from;
	if(mode == "0")
	{
		why = "loc_attach=0: no Localization database attached (GetDiploResponse finds no text)";
		return true;
	}
	if(mode != "engine" && mode != "1")
	{
		why = "loc_attach must be engine, 1 or 0 (is '" + mode + "')";
		return false;
	}
	if(!engine.empty() && GetFileAttributesA(engine.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		path = engine;
		from = "the engine's";
	}
	else if(!engine.empty())
		why = "the engine's Localization file " + engine + " is not readable here; ";
	if(path.empty() && (mode == "1" || !engine.empty()))
	{
		path = SrvCfgStr("loc_db");
		from = "the server's loc_db";
		if(path.empty())
		{
			why += "no loc_db to attach instead";
			return false;
		}
	}
	if(path.empty())
	{
		why = "the engine has no Localization database attached (loc_attach=engine): none attached, as the engine";
		return true;
	}
	std::string q = "'";
	for(size_t i = 0; i < path.size(); ++i)
	{
		q += path[i];
		if(path[i] == '\'')
			q += '\'';
	}
	q += "'";
	const std::string sql = "ATTACH DATABASE " + q + " AS Localization";
	if(!db.Execute(sql.c_str()))
	{
		why += "ATTACH " + path + " failed: " + (db.ErrorMessage() ? db.ErrorMessage() : "?");
		return false;
	}
	// The view the DLL's query names must be there (a wrong file attaches without error).
	Database::Results r;
	bool bView = false;
	if(db.Execute(r, "SELECT 1 FROM Localization.sqlite_master WHERE name = 'Language_en_US'"))
		bView = r.Step();
	if(!bView)
	{
		why += path + " attached as Localization but has no Language_en_US";
		return false;
	}
	why += "attached " + from + " " + path + " as Localization (loc_attach=" + mode + ")";
	return true;
}

bool LocActive() { return s_bLoc; }

void LocShutdown()
{
	if(s_bLoc)
		Localization::Shutdown();
	s_bLoc = false;
	if(s_pLocDb)
	{
		s_pLocDb->Close();
		delete s_pLocDb;
	}
	s_pLocDb = NULL;
}

void LocInstallLua(lua_State* L)
{
	static const luaL_Reg kFns[] = {
		{ "Lookup", LuaLookup },
		{ "ConvertTextKey", LuaLookup },
		{ "HasTextKey", LuaHasTextKey },
		{ "ToUpper", LuaToUpper },
		{ "ToLower", LuaToLower },
		{ "Length", LuaLength },
		{ "Compare", LuaCompare },
		{ "ToNumber", LuaToNumber },
		{ "GetCurrentLanguage", LuaGetCurrentLanguage },
		{ NULL, NULL },
	};
	lua_newtable(L);
	for(const luaL_Reg* r = kFns; r->name; ++r)
	{
		lua_pushcfunction(L, r->func);
		lua_setfield(L, -2, r->name);
	}
	lua_setglobal(L, "Locale");
	LuaRunString(kLocaleLua, "=Locale");
}
