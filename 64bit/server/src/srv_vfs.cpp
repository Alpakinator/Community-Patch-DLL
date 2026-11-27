// Read-only virtual file system over the game directory (issue 1502).
//
// Contract (research/sources/civ5-headless/engine-contract.md 10.3, running.md 3.7): one file per name; a DLC package
// with a higher <Priority> (its .Civ5Pkg) wins over a lower one and over the base game's Assets; inside one package
// the file found last in the directory walk wins (VP's copy over CP's, the package's UI folder over its Mods). The
// G&K package (DLC\Expansion) is not part of a BNW game and the Tablet package not of the DX11 game: both are left out.
// Checked against the 32-bit host's include resolution of the same game directory (server/evidence/1505/*/
// include-resolution.log). Only the game directory is indexed; nothing is ever written.
//
// Paths (bug 0100, 2026-11-13): a path relative to the game directory (the map script "Assets\Maps\Communitu_79a.lua",
// as LoadFile gets it from the pregame) is looked up by its file name first, like an include, and the literal path is
// only the fallback for a file the index does not hold; an absolute path is taken as it is. engine-contract.md 10.3 and
// its new-game trace: LoadFile resolves the relative path through the VFS with include precedence; civ5-headless
// HHScriptSystem::ResolveFile does the same (step 2 before step 3). Before 0100 the literal path won, so the server ran
// the base game's old Communitu_79a.lua where c5h and the game run VP's. Unit test: server/tests/run_vfs_test.py.
#include "srv_common.h"
#include <stdio.h>

namespace
{
	struct Entry
	{
		std::string full;		// absolute path
		std::string vfs;		// path relative to the game directory
		int rank;				// package priority; base game -1
		long order;				// walk order (later wins on equal rank)
	};
	std::string s_strGameDir;
	std::map<std::string, Entry> s_byName;		// lower-case file name -> winner
	std::map<std::string, Entry> s_byNameNoExt;	// lower-case name without extension (.lua only) -> winner
	std::vector<std::string> s_luaList;
	long s_lOrder = 0;
	size_t s_nFiles = 0;

	std::string Lower(std::string s)
	{
		for(size_t i = 0; i < s.size(); ++i)
			s[i] = (char)tolower((unsigned char)s[i]);
		return s;
	}

	void Consider(std::map<std::string, Entry>& m, const std::string& key, const Entry& e)
	{
		std::map<std::string, Entry>::iterator it = m.find(key);
		if(it == m.end() || e.rank > it->second.rank || (e.rank == it->second.rank && e.order > it->second.order))
			m[key] = e;
	}

	void Walk(const std::string& dir, const std::string& rel, int rank)
	{
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			return;
		std::vector<std::pair<std::string, bool> > items;
		do
		{
			if(!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
				continue;
			items.push_back(std::make_pair(std::string(fd.cFileName), (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));
		} while(FindNextFileA(h, &fd));
		FindClose(h);
		// Deterministic order: case-insensitive by name.
		std::sort(items.begin(), items.end(), [](const std::pair<std::string, bool>& a, const std::pair<std::string, bool>& b) {
			return _stricmp(a.first.c_str(), b.first.c_str()) < 0;
		});
		for(size_t i = 0; i < items.size(); ++i)
		{
			const std::string full = dir + "\\" + items[i].first;
			const std::string r = rel.empty() ? items[i].first : rel + "\\" + items[i].first;
			if(items[i].second)
			{
				Walk(full, r, rank);
				continue;
			}
			Entry e;
			e.full = full;
			e.vfs = r;
			e.rank = rank;
			e.order = ++s_lOrder;
			++s_nFiles;
			const std::string name = Lower(items[i].first);
			Consider(s_byName, name, e);
			if(name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0)
				Consider(s_byNameNoExt, name.substr(0, name.size() - 4), e);
		}
	}

	// Highest <Priority> of the package's .Civ5Pkg files; 0 if none states one.
	int PackagePriority(const std::string& dir)
	{
		int best = 0;
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\*.Civ5Pkg").c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			return best;
		do
		{
			std::string data;
			if(!VfsReadFile(dir + "\\" + fd.cFileName, data))
				continue;
			const size_t p = data.find("<Priority>");
			if(p != std::string::npos)
				best = std::max(best, atoi(data.c_str() + p + 10));
		} while(FindNextFileA(h, &fd));
		FindClose(h);
		return best;
	}
}

bool VfsReadFile(const std::string& strFull, std::string& strData)
{
	FILE* f = NULL;
	if(fopen_s(&f, strFull.c_str(), "rb") || !f)
		return false;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	strData.resize(n > 0 ? (size_t)n : 0);
	const size_t got = n > 0 ? fread(&strData[0], 1, (size_t)n, f) : 0;
	fclose(f);
	return got == strData.size();
}

bool VfsInit(const char* szGameDir, std::string& strWhy)
{
	s_strGameDir = szGameDir;
	const std::string assets = s_strGameDir + "\\Assets";
	if(GetFileAttributesA(assets.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		strWhy = "no Assets folder in " + s_strGameDir;
		return false;
	}
	// Base game: Assets\* except Assets\DLC.
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((assets + "\\*").c_str(), &fd);
	std::vector<std::string> top;
	if(h != INVALID_HANDLE_VALUE)
	{
		do
			top.push_back(fd.cFileName);
		while(FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::sort(top.begin(), top.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
	for(size_t i = 0; i < top.size(); ++i)
	{
		if(top[i] == "." || top[i] == ".." || !_stricmp(top[i].c_str(), "DLC"))
			continue;
		const std::string full = assets + "\\" + top[i];
		if(GetFileAttributesA(full.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
			Walk(full, "Assets\\" + top[i], -1);
	}
	// DLC packages.
	std::vector<std::string> dlc;
	h = FindFirstFileA((assets + "\\DLC\\*").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
			if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
				dlc.push_back(fd.cFileName);
		while(FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::sort(dlc.begin(), dlc.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
	std::string used;
	for(size_t i = 0; i < dlc.size(); ++i)
	{
		if(!_stricmp(dlc[i].c_str(), "Expansion") || !_stricmp(dlc[i].c_str(), "Tablet"))
			continue;
		const std::string full = assets + "\\DLC\\" + dlc[i];
		const int prio = PackagePriority(full);
		Walk(full, "Assets\\DLC\\" + dlc[i], prio);
		char sz[64];
		sprintf_s(sz, " %d", prio);
		used += dlc[i] + sz + ",";
	}
	for(std::map<std::string, Entry>::const_iterator it = s_byNameNoExt.begin(); it != s_byNameNoExt.end(); ++it)
		s_luaList.push_back(it->second.vfs);
	char sz[128];
	sprintf_s(sz, "%zu files, %zu names, %zu Lua names; packages (priority): ", s_nFiles, s_byName.size(), s_byNameNoExt.size());
	strWhy = sz + used;
	return true;
}

bool VfsResolve(const char* szName, std::string& strFull, std::string& strVfsPath)
{
	if(!szName || !*szName)
		return false;
	std::string n = szName;
	for(size_t i = 0; i < n.size(); ++i)
		if(n[i] == '/')
			n[i] = '\\';
	const bool bAbsolute = (n.size() > 2 && n[1] == ':') || (n.size() > 1 && n[0] == '\\' && n[1] == '\\');
	// 1. An absolute path that exists is taken as it is.
	if(bAbsolute && GetFileAttributesA(n.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		strFull = n;
		strVfsPath = n;
		return true;
	}
	// 2. The file name through the index (the package with the highest priority wins), also for a relative path.
	const std::string base = n.find('\\') == std::string::npos ? n : n.substr(n.rfind('\\') + 1);
	const std::string key = Lower(base);
	const Entry* pHit = NULL;
	std::map<std::string, Entry>::const_iterator it = s_byName.find(key);
	if(it != s_byName.end())
		pHit = &it->second;
	else if((it = s_byNameNoExt.find(key)) != s_byNameNoExt.end())
		pHit = &it->second;
	if(pHit)
	{
		strFull = pHit->full;
		strVfsPath = pHit->vfs;
		return true;
	}
	// 3. A relative path to a file the index does not hold (outside Assets, or in a package left out).
	if(!bAbsolute && n.find('\\') != std::string::npos)
	{
		const std::string cand = s_strGameDir + "\\" + n;
		if(GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			strFull = cand;
			strVfsPath = n;
			return true;
		}
	}
	return false;
}

const std::vector<std::string>& VfsLuaFileList() { return s_luaList; }
size_t VfsIndexSize() { return s_nFiles; }
