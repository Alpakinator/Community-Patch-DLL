// srv_remote_savemods.cpp - ticket 0681: a save loaded with a different set of mods, told to the player (see the header).
// Knowledge base: research/03-dll-engine-boundary/save-mod-mismatch.md.
#include "srv_remote_savemods.h"
#include "srv_civ5save.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

// BEGIN 0681 PURE (server/tests/tools/test_save_mod_mismatch.py compiles this block on its own: standard C++ only, the
// header's Mod type)
namespace SrvSaveMods
{
namespace pure
{
inline std::string Lower(std::string s)
{
	for(size_t i = 0; i < s.size(); ++i)
		if(s[i] >= 'A' && s[i] <= 'Z')
			s[i] = (char)(s[i] - 'A' + 'a');
	return s;
}
// The text between "<key>[" and the next ']' after pos; "" when absent.
inline std::string Bracket(const std::string& t, const char* key)
{
	const size_t k = t.find(key);
	if(k == std::string::npos)
		return std::string();
	const size_t a = t.find('[', k);
	const size_t b = a == std::string::npos ? std::string::npos : t.find(']', a);
	if(b == std::string::npos || b - a > 80)
		return std::string();
	return t.substr(a + 1, b - a - 1);
}
inline std::string ModText(const Mod& m)
{
	char v[32];
	snprintf(v, sizeof(v), " v%u", m.version);
	return (m.name.empty() ? m.id : m.name) + v;
}
}  // namespace pure

bool ParseHashLine(const std::string& text, std::string* saveHash, std::string* gameHash)
{
	if(text.find("Save data hash mismatch") == std::string::npos)
		return false;
	const std::string s = pure::Bracket(text, "Save Hash"), g = pure::Bracket(text, "Game Hash");
	if(saveHash)
		*saveHash = s.empty() ? "(not in the line)" : s;
	if(gameHash)
		*gameHash = g.empty() ? "(not in the line)" : g;
	return true;
}

std::string HashText(const unsigned char* p)
{
	char sz[64];
	unsigned int w[4];
	for(int i = 0; i < 4; ++i)
		w[i] = (unsigned)p[4 * i] | (unsigned)p[4 * i + 1] << 8 | (unsigned)p[4 * i + 2] << 16 | (unsigned)p[4 * i + 3] << 24;
	snprintf(sz, sizeof(sz), "0x%08x-0x%08x-0x%08x-0x%08x", w[0], w[1], w[2], w[3]);
	return sz;
}

std::string DescribeMods(const std::vector<Mod>& header, bool headerKnown, const std::vector<Mod>& active, bool activeKnown)
{
	std::string o;
	if(headerKnown)
	{
		o += "the save's header lists " + std::to_string(header.size()) + (header.size() == 1 ? " mod" : " mods");
		for(size_t i = 0; i < header.size(); ++i)
			o += (i ? ", " : ": ") + pure::ModText(header[i]);
	}
	else
		o += "the save's header was not read";
	if(!activeKnown)
		return o + "; the active mods could not be read";
	o += "; active now: " + std::to_string(active.size());
	for(size_t i = 0; i < active.size(); ++i)
		o += (i ? ", " : ": ") + pure::ModText(active[i]);
	if(headerKnown)
	{
		std::string d;
		for(size_t i = 0; i < header.size(); ++i)
		{
			const std::string id = pure::Lower(header[i].id);
			const Mod* a = NULL;
			for(size_t j = 0; j < active.size() && !a; ++j)
				if(active[j].id == id)
					a = &active[j];
			if(!a)
				d += (d.empty() ? "" : ", ") + std::string("only in the save: ") + pure::ModText(header[i]);
			else if(a->version != header[i].version)
			{
				char v[64];
				snprintf(v, sizeof(v), " (save v%u, active v%u)", header[i].version, a->version);
				d += (d.empty() ? "" : ", ") + std::string("another version: ") + (a->name.empty() ? a->id : a->name) + v;
			}
		}
		for(size_t j = 0; j < active.size(); ++j)
		{
			if(active[j].affectsSaves == 0)
				continue;
			bool in = false;
			for(size_t i = 0; i < header.size() && !in; ++i)
				in = pure::Lower(header[i].id) == active[j].id;
			if(!in)
				d += (d.empty() ? "" : ", ") + std::string("only active: ") + pure::ModText(active[j]);
		}
		o += "; difference in the mods a save lists: " + (d.empty() ? std::string("none") : d);
	}
	std::string never;
	for(size_t j = 0; j < active.size(); ++j)
		if(active[j].affectsSaves == 0)
			never += (never.empty() ? "" : ", ") + pure::ModText(active[j]);
	if(!never.empty())
		o += "; active mods that saves never list (AffectsSavedGames = 0): " + never;
	o += "; a mod with AffectsSavedGames = 0 is not written into a save's header, so a save made with another set of such "
	     "mods looks the same there, while the game data differ";
	return o;
}

std::string PlayerSummary()
{
	return "This save was made with a different set of mods (or mod versions) than the mods active now, so the game "
	       "could not load it; this is not a failure of the 64-bit patch, and the same save fails in Vox Populi without "
	       "the patch too. Load the save with the mods it was made with.";
}
}  // namespace SrvSaveMods
// END 0681 PURE

// The server's log and clock (srv_common.h; declared here so that server/tests/remote/test_savemods.py compiles this file
// without the DLL's headers, as srv_dll_fatal.cpp does)
void SrvLog(const char* szFormat, ...);
double SrvNow();

namespace SrvExeCrash
{
typedef bool (*FatalOverrideFn)(std::string* pToken, std::string* pSummary, std::string* pReason);
extern FatalOverrideFn g_pfnFatalOverride;  // defined in srv_exe_crash.cpp (WriteFatalRecord calls it when set)
}  // namespace SrvExeCrash

namespace SrvSaveMods
{
namespace
{
SRWLOCK s_lock = SRWLOCK_INIT;
bool s_bArmed = false;
std::string s_strDetail;          // the armed detail line
double s_tLoad = -1;              // SrvNow() at BeginLoad
std::string s_strFile;            // the save's file name (no folder)
std::string s_strBodyHash;        // the body's hash, "" when not read
std::string s_strVersion;         // the body's gamecore version string
std::vector<Mod> s_header, s_active;
bool s_bHeader = false, s_bActive = false;
std::string s_strHeaderWhy, s_strActiveWhy;
int s_nSentinel = 0;              // "Deserialization Error" lines since BeginLoad
std::string s_strPrefix;          // the body's first bytes (to find the save in remote mode)
EndFn s_pfnEnd = NULL;            // ticket 0744: the end at the first deserialization error (remote mode); NULL: none
bool s_bEnding = false;           // ticket 0744: that end is under way (its fatal record names it)

std::wstring Wide(const char* s)
{
	if(!s || !*s)
		return std::wstring();
	const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
	std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
	if(n > 0)
		MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
	w.resize(wcslen(w.c_str()));
	return w;
}

std::string Utf8(const std::wstring& w)
{
	if(w.empty())
		return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s((size_t)(n > 0 ? n : 0), '\0');
	if(n > 0)
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

// The header's mods of the save file (read-only, its first 1 MiB at most: the header is under 2 KiB in every save seen).
bool ReadHeaderMods(const std::wstring& file, std::vector<Mod>& out, std::string& why)
{
	out.clear();
	if(file.empty())
		return why = "no save file name (the pregame's loadFileName is empty)", false;
	HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
		return why = "the save file could not be opened (error " + std::to_string(GetLastError()) + ")", false;
	std::string data(1 << 20, '\0');
	DWORD got = 0;
	const BOOL ok = ReadFile(h, &data[0], (DWORD)data.size(), &got, NULL);
	CloseHandle(h);
	if(!ok)
		return why = "the save file could not be read", false;
	data.resize(got);
	Civ5SaveHeader hd;
	size_t end = 0;
	if(!Civ5ParseHeader(data, hd, end, why))
		return false;
	for(size_t i = 0; i < hd.mods.size(); ++i)
	{
		Mod m;
		m.id = pure::Lower(hd.mods[i].id);
		m.version = hd.mods[i].version;
		m.name = hd.mods[i].name;
		m.affectsSaves = 1;
		out.push_back(m);
	}
	return true;
}

// <Documents>\My Games\Sid Meier's Civilization 5\cache\Civ5ModsDatabase.db (C5_DOCUMENTS overrides the Documents folder,
// as srv_residency_source.cpp ModFolders does).
std::wstring ModsDbPath()
{
	std::wstring docs;
	wchar_t env[MAX_PATH * 2];
	const DWORD n = GetEnvironmentVariableW(L"C5_DOCUMENTS", env, MAX_PATH * 2);
	if(n > 0 && n < MAX_PATH * 2)
		docs.assign(env, n);
	else
	{
		typedef HRESULT(WINAPI * PFN)(HWND, int, HANDLE, DWORD, LPWSTR);
		HMODULE sh = LoadLibraryW(L"shell32.dll");
		PFN f = sh ? (PFN)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
		wchar_t w[MAX_PATH];
		if(f && f(NULL, 0x0005 /* CSIDL_PERSONAL */, NULL, 0, w) == S_OK)
			docs = w;
	}
	if(docs.empty())
		return std::wstring();
	return docs + L"\\My Games\\Sid Meier's Civilization 5\\cache\\Civ5ModsDatabase.db";
}

// The activated mods with their names and AffectsSavedGames, through System32's winsqlite3.dll (read-only).
bool ReadActiveMods(std::vector<Mod>& out, std::string& why)
{
	out.clear();
	typedef int(__cdecl * FnOpen)(const char*, void**, int, const char*);
	typedef int(__cdecl * FnClose)(void*);
	typedef int(__cdecl * FnPrepare)(void*, const char*, int, void**, const char**);
	typedef int(__cdecl * FnStep)(void*);
	typedef int(__cdecl * FnFinalize)(void*);
	typedef const unsigned char*(__cdecl * FnText)(void*, int);
	typedef long long(__cdecl * FnInt64)(void*, int);
	HMODULE m = LoadLibraryExW(L"winsqlite3.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if(!m)
		return why = "winsqlite3.dll not loadable from System32", false;
	FnOpen open = (FnOpen)(void*)GetProcAddress(m, "sqlite3_open_v2");
	FnClose close = (FnClose)(void*)GetProcAddress(m, "sqlite3_close");
	FnPrepare prepare = (FnPrepare)(void*)GetProcAddress(m, "sqlite3_prepare_v2");
	FnStep step = (FnStep)(void*)GetProcAddress(m, "sqlite3_step");
	FnFinalize finalize = (FnFinalize)(void*)GetProcAddress(m, "sqlite3_finalize");
	FnText text = (FnText)(void*)GetProcAddress(m, "sqlite3_column_text");
	FnInt64 i64 = (FnInt64)(void*)GetProcAddress(m, "sqlite3_column_int64");
	if(!open || !close || !prepare || !step || !finalize || !text || !i64)
		return why = "winsqlite3.dll lacks a function", false;
	const std::wstring path = ModsDbPath();
	if(path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return why = "cache\\Civ5ModsDatabase.db not found", false;
	void* db = NULL;
	if(open(Utf8(path).c_str(), &db, 0x00000001 /* SQLITE_OPEN_READONLY */, NULL) != 0)
	{
		if(db)
			close(db);
		return why = "cache\\Civ5ModsDatabase.db could not be opened read-only", false;
	}
	void* st = NULL;
	const char* q = "SELECT m.ModID, m.Version, (SELECT p.Value FROM ModProperties p WHERE p.ModID = m.ModID AND p.Version = "
	                "m.Version AND p.Name = 'Name'), (SELECT p.Value FROM ModProperties p WHERE p.ModID = m.ModID AND "
	                "p.Version = m.Version AND p.Name = 'AffectsSavedGames') FROM Mods m WHERE m.Activated = 1 ORDER BY "
	                "m.RowID";
	if(prepare(db, q, -1, &st, NULL) != 0 || !st)
	{
		close(db);
		return why = "the query of Mods and ModProperties failed", false;
	}
	while(step(st) == 100 /* SQLITE_ROW */)
	{
		Mod md;
		const unsigned char* id = text(st, 0);
		const unsigned char* nm = text(st, 2);
		const unsigned char* af = text(st, 3);
		md.id = pure::Lower(id ? (const char*)id : "?");
		md.version = (unsigned int)i64(st, 1);
		md.name = nm ? (const char*)nm : "";
		md.affectsSaves = af ? (atoi((const char*)af) != 0 ? 1 : 0) : -1;
		out.push_back(md);
	}
	finalize(st);
	close(db);
	return true;
}

// <Documents>\My Games\Sid Meier's Civilization 5 (C5_DOCUMENTS overrides the Documents folder).
std::wstring CivUserDir()
{
	const std::wstring db = ModsDbPath();
	const size_t cut = db.rfind(L"\\cache\\");
	return cut == std::wstring::npos ? std::wstring() : db.substr(0, cut);
}

struct SaveFile
{
	std::wstring path;
	FILETIME mtime;
};

void ListSaves(const std::wstring& dir, std::vector<SaveFile>& out, int depth)
{
	if(depth > 4 || out.size() > 2000)
		return;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		const std::wstring n = fd.cFileName;
		if(n == L"." || n == L"..")
			continue;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				ListSaves(dir + L"\\" + n, out, depth + 1);
		}
		else if(n.size() > 9 && _wcsicmp(n.c_str() + n.size() - 9, L".Civ5Save") == 0 && fd.nFileSizeHigh == 0 &&
		        fd.nFileSizeLow < 200u * 1024 * 1024)
		{
			SaveFile s;
			s.path = dir + L"\\" + n;
			s.mtime = fd.ftLastWriteTime;
			out.push_back(s);
		}
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}

// The save whose inflated body begins with `prefix` (remote mode: the DLL's pregame has no file name). Read-only:
// each file is read whole (the container's pieces are found from its end), only its first zlib piece inflated.
bool FindSaveByBody(const std::string& prefix, std::wstring& found, std::string& why)
{
	const std::wstring civ = CivUserDir();
	if(civ.empty())
		return why = "no Documents folder", false;
	std::vector<SaveFile> files;
	ListSaves(civ + L"\\ModdedSaves", files, 0);
	ListSaves(civ + L"\\Saves", files, 0);
	std::sort(files.begin(), files.end(), [](const SaveFile& a, const SaveFile& b) {
		return CompareFileTime(&a.mtime, &b.mtime) > 0;
	});
	const double t0 = SrvNow();
	size_t n = 0;
	for(; n < files.size() && n < 400 && SrvNow() - t0 < 6.0; ++n)
	{
		std::string data, w, body;
		if(!Civ5ReadFile(Utf8(files[n].path).c_str(), data))
			continue;
		Civ5SaveParts parts;
		if(!Civ5Split(data, parts, w) || parts.pieces.empty())
			continue;
		parts.pieces.resize(1);
		if(!Civ5Inflate(data, parts, body, w) && body.size() < prefix.size())
			continue;
		if(body.size() >= prefix.size() && body.compare(0, prefix.size(), prefix) == 0)
		{
			found = files[n].path;
			return true;
		}
	}
	char sz[160];
	snprintf(sz, sizeof(sz), "no save under ModdedSaves or Saves begins with the loaded body (%zu of %zu files read, %.1f s)",
	         n, files.size(), SrvNow() - t0);
	return why = sz, false;
}

std::string FileName(const char* sz)
{
	if(!sz)
		return std::string();
	const char* p = sz;
	for(const char* q = sz; *q; ++q)
		if(*q == '\\' || *q == '/')
			p = q + 1;
	return p;
}
}  // namespace

void BeginLoad(const char* szSaveFile, const unsigned char* pHash16, const std::string& version,
               const std::string& bodyPrefix)
{
	std::vector<Mod> header, active;
	std::string hw, aw;
	const bool bH = ReadHeaderMods(Wide(szSaveFile), header, hw);
	const bool bA = ReadActiveMods(active, aw);
	AcquireSRWLockExclusive(&s_lock);
	s_tLoad = SrvNow();
	s_strFile = FileName(szSaveFile);
	s_strBodyHash = pHash16 ? HashText(pHash16) : std::string();
	s_strVersion = version;
	s_header = header;
	s_active = active;
	s_bHeader = bH;
	s_bActive = bA;
	s_strHeaderWhy = hw;
	s_strActiveWhy = aw;
	s_strPrefix = bodyPrefix.substr(0, 4096);
	s_nSentinel = 0;
	ReleaseSRWLockExclusive(&s_lock);
	std::string line = DescribeMods(header, bH, active, bA);
	if(!bH)
		line += " (the header: " + hw + ")";
	if(!bA)
		line += " (the active mods: " + aw + ")";
	const std::string fname = FileName(szSaveFile);
	SrvLog("save mods: load of '%s' (body version \"%s\", game data hash %s): %s (ticket 0681)",
	       fname.empty() ? "(no file name)" : fname.c_str(), version.c_str(),
	       pHash16 ? HashText(pHash16).c_str() : "(not read)", line.c_str());
}

void OnDllLog(const char* szLogName, const char* szText)
{
	if(!szText || !szLogName || _stricmp(szLogName, "custommods.log") != 0)
		return;
	if(strstr(szText, "Deserialization Error"))
	{
		AcquireSRWLockExclusive(&s_lock);
		const int n = ++s_nSentinel;
		const bool armed = s_bArmed;
		const bool loading = s_tLoad >= 0;
		const EndFn end = n == 1 && loading ? s_pfnEnd : NULL;
		if(end)
			s_bEnding = true;
		char sz[160];
		snprintf(sz, sizeof(sz), "%.1f s after the save's read began", loading ? SrvNow() - s_tLoad : 0.0);
		const std::string where = std::string(sz) + "; save '" + (s_strFile.empty() ? std::string("(no file name)") : s_strFile) +
		                          "', body version \"" + s_strVersion + "\"";
		ReleaseSRWLockExclusive(&s_lock);
		if(n != 1)
			return;
		SrvLog("save mods: the DLL reports a deserialization error (a sentinel of the save did not match)%s (ticket 0681)",
		       armed ? " after the game data mismatch" : "");
		if(!end)
		{
			if(!loading)
				SrvLog("save mods: that deserialization error came before any save's read began: the server does not end "
				       "on it (ticket 0744)");
			return;
		}
		// Ticket 0744 (K31, the MC's ruling (c)): the DLL's read is out of step with the save; reading on only damages
		// memory (K31) or runs off the stream's end (K29). End now, on this thread, inside the DLL's log call.
		const std::string why = "save-mod-mismatch: the game's DLL reports a deserialization error in the save's read (" +
		                        where + (armed ? ", after the game data mismatch" : "") +
		                        "): its read is out of step with the save, so the server ends before the misread can "
		                        "damage its memory (ticket 0744, K31)";
		SrvLog("save mods: LOUD: the server ends at the DLL's first deserialization error of the load (exit %d, code "
		       "save-mod-mismatch; ticket 0744)",
		       kMismatchExitCode);
		end(kMismatchExitCode, why.c_str());
		return;   // only when the hook returned (it must not)
	}
	std::string sh, gh;
	if(!strstr(szText, "Save data hash mismatch") || !ParseHashLine(szText, &sh, &gh))
		return;
	// Remote mode: no file name from the pregame; find the save by its body's first bytes (read-only)
	AcquireSRWLockShared(&s_lock);
	const bool bFind = !s_bHeader && s_strFile.empty() && s_strPrefix.size() >= 64;
	const std::string prefix = s_strPrefix;
	ReleaseSRWLockShared(&s_lock);
	std::string findNote;
	if(bFind)
	{
		const double t0 = SrvNow();
		std::wstring path;
		std::string fw, hw;
		std::vector<Mod> header;
		if(FindSaveByBody(prefix, path, fw))
		{
			const bool bH = ReadHeaderMods(path, header, hw);
			char sz[120];
			snprintf(sz, sizeof(sz), " (found by its body among the saves in %.2f s)", SrvNow() - t0);
			findNote = sz;
			AcquireSRWLockExclusive(&s_lock);
			s_strFile = Utf8(path.substr(path.find_last_of(L"\\/") + 1));
			s_header = header;
			s_bHeader = bH;
			s_strHeaderWhy = hw;
			ReleaseSRWLockExclusive(&s_lock);
		}
		else
			findNote = " (" + fw + ")";
	}
	AcquireSRWLockExclusive(&s_lock);
	char sz[160];
	snprintf(sz, sizeof(sz), "%.1f s after the save's read began", s_tLoad >= 0 ? SrvNow() - s_tLoad : 0.0);
	std::string d = "the game's DLL reports that the save's game data hash [" + sh + "] differs from the running game's [" +
	                gh + "] (" + sz + "; save '" + (s_strFile.empty() ? std::string("(no file name)") : s_strFile) + "'" +
	                findNote + ", body version \"" + s_strVersion + "\"): the save was made with other mods or mod versions than the "
	                "active ones (the mods change the game's rules tables); " +
	                DescribeMods(s_header, s_bHeader, s_active, s_bActive);
	if(!s_bHeader && !s_strHeaderWhy.empty())
		d += " (the header: " + s_strHeaderWhy + ")";
	if(!s_bActive && !s_strActiveWhy.empty())
		d += " (the active mods: " + s_strActiveWhy + ")";
	s_bArmed = true;
	s_strDetail = d;
	ReleaseSRWLockExclusive(&s_lock);
	SrvLog("save mods: LOUD: %s; the DLL loads on and may fail or play a corrupted game (ticket 0681)", d.c_str());
}

void EndLoad(bool bOk, const char* szWhy)
{
	AcquireSRWLockShared(&s_lock);
	const bool armed = s_bArmed;
	const int n = s_nSentinel;
	ReleaseSRWLockShared(&s_lock);
	if(!armed)
		return;
	SrvLog("save mods: the load after the game data mismatch %s%s%s; deserialization errors %d (ticket 0681)",
	       bOk ? "ended" : "FAILED", szWhy && *szWhy ? ": " : "", szWhy ? szWhy : "", n);
}

bool OverrideFatal(std::string* pToken, std::string* pSummary, std::string* pReason)
{
	AcquireSRWLockShared(&s_lock);
	const bool armed = s_bArmed;
	std::string d = s_strDetail;
	const int n = s_nSentinel;
	const bool ending = s_bEnding;
	if(!armed && n > 0)
	{
		// ticket 0744: a deserialization error without the DLL's game data mismatch before it (the hash covers the info
		// tables' counts and Type names only; another difference can still shift the read)
		d = "the game's DLL reports a deserialization error in the save's read (a sentinel of the save did not match) "
		    "without a game data hash mismatch before it (save '" +
		    (s_strFile.empty() ? std::string("(no file name)") : s_strFile) + "', body version \"" + s_strVersion +
		    "\"): the save's data are out of step with the running game's, most likely made with other mods or mod "
		    "versions than the active ones; " +
		    DescribeMods(s_header, s_bHeader, s_active, s_bActive);
		if(!s_bHeader && !s_strHeaderWhy.empty())
			d += " (the header: " + s_strHeaderWhy + ")";
		if(!s_bActive && !s_strActiveWhy.empty())
			d += " (the active mods: " + s_strActiveWhy + ")";
	}
	ReleaseSRWLockShared(&s_lock);
	if((!armed && n == 0) || !pToken || pToken->empty())
		return false;
	// a render module's end (render-*) after a load that read in step is the renderer's own failure, not the load's;
	// after a deserialization error it is the misread's damage (ticket 0744 (a), K31)
	if(pToken->compare(0, 6, "render") == 0 && n == 0)
		return false;
	const std::string was = *pToken;
	*pToken = "save-mod-mismatch";
	if(pSummary)
		*pSummary = PlayerSummary();
	if(pReason)
		*pReason = "save-mod-mismatch: " + d + "; deserialization errors " + std::to_string(n) +
		           (ending && was == "engine-exception" ? std::string("; the server ended at the DLL's first deserialization error (ticket 0744): ")
		                   : "; what ended the server (code " + was + "): ") +
		           *pReason;
	return true;
}

void SetEndHook(EndFn pfn)
{
	AcquireSRWLockExclusive(&s_lock);
	s_pfnEnd = pfn;
	ReleaseSRWLockExclusive(&s_lock);
}

namespace
{
// srv_exe_crash.cpp's hook for WriteFatalRecord, set at start-up (so that a test program linking srv_exe_crash.cpp
// without this file still links; the declaration matches the one there)
struct RegisterFatalOverride
{
	RegisterFatalOverride() { SrvExeCrash::g_pfnFatalOverride = &OverrideFatal; }
} s_registerFatalOverride;
}  // namespace

bool Armed(std::string* pDetail)
{
	AcquireSRWLockShared(&s_lock);
	const bool armed = s_bArmed;
	if(pDetail)
		*pDetail = s_strDetail;
	ReleaseSRWLockShared(&s_lock);
	return armed;
}
}  // namespace SrvSaveMods
