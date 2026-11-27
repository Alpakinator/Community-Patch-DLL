// Saves written and read by the x64 DLL's own serializers (issues 1506, 2001).
//
// The engine's .Civ5Save (research/sources/civ5-headless/saves.md section 2) is: an uncompressed header the EXE writes,
// the pregame (ICvPreGame1::write), u32 2, and a zlib-compressed body: Game::Write (which embeds the cache's
// Civ5SavedGameDatabase.db), Map::Write, Team::Write for teams 0-63, Player::Write for players 0-63,
// Game::WriteSupportingClassData. The server writes the two DLL-written parts uncompressed, as <base>.pregame.bin and
// <base>.body.bin (byte for byte what the DLL wrote into an FMemoryStream, the closed library's replacement the DLL
// itself uses), and since issue 2001 the whole <base>.Civ5Save: a header it builds from the game (below), the
// pregame, u32 2 and the body deflated as the game does (srv_civ5save.cpp, zlib 1.2.3). A .Civ5Save is read natively
// (SrvLoadCiv5Save); the two-file form (SrvLoadRaw) stays for the tools.
//
// Header (saves.md 2.1; research/03-dll-engine-boundary/server-host.md section 9): the turn and game type of the
// pregame; the type names of the active player's civilization and handicap, the pregame's start era, the active
// player's current era, the speed, the world size and the map script; the DLC and mod lists (a loaded game: the
// loaded header's lists; a new game: the DLC rule below, no mods); the active player's leader and civilization name
// keys (empty unless set) and player colour; the DLL's GUID and version string (ICvGameContext1).
// New-game DLC list: every <game>\Assets\DLC\<folder>\*.Civ5Pkg with a <SteamApp> and without <Upgrade>, in folder
// then file name order (case-insensitive), minus the GUIDs in `save_dlc_exclude`; each entry the package GUID, 1 and
// its en_US <Name>. This reproduces the real game's 13-entry list of the huge save from the Steam install when
// DLC_SP_Maps (blocked by Vox Populi) is excluded.
//
// Load (saves.md section 4): resetGame, resetPlayers, clearDLCAllowed, PreGame::read(stream, true),
// setGameType(type, GAME_LOADED), setLoadFileName; CacheGameDatabaseData; Game::Read, Map::Read, teams, players,
// Map::RecalculateLandmasses (issue 0452, as the EXE), ReadSupportingClassData (the body must be used up exactly); then the game-started steps of a new game without the
// settler selection.
#include "srv_common.h"
#include "srv_civ5save.h"
#include "remote/srv_remote_savemods.h"
#include <FireWorks/FMemoryStream.h>
#include <stdio.h>
#include <algorithm>

namespace
{
	bool ReadBody(ICvGameContext1* pContext, const std::string& body, unsigned int uPreUsed, size_t preSize, std::string& why);
	Civ5SaveHeader s_loadedHeader;	// the header of the loaded .Civ5Save (its DLC and mod lists are carried on)
	bool s_bHaveLoadedHeader = false;

	std::string ToBytes(FMemoryStream& s)
	{
		const unsigned int n = s.GetEOF();
		std::string buf(n, '\0');
		s.Rewind();
		if(n)
			s.CopyToMem(&buf[0]);
		return buf;
	}

	template <class T> std::string TypeOf(T* p)
	{
		if(!p)
			return std::string();
		const char* sz = p->GetType();
		const std::string s = sz ? sz : "";
		p->Destroy();
		return s;
	}

	std::string Tag(const std::string& x, const char* szOpen, const char* szClose, size_t from = 0)
	{
		const size_t a = x.find(szOpen, from);
		if(a == std::string::npos)
			return std::string();
		const size_t b = x.find(szClose, a + strlen(szOpen));
		return b == std::string::npos ? std::string() : x.substr(a + strlen(szOpen), b - a - strlen(szOpen));
	}

	int HexNibble(char c)
	{
		if(c >= '0' && c <= '9')
			return c - '0';
		if(c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if(c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	// "{293C1EE3-1176-44f6-AC1F-59663826DE74}" -> the 16 bytes of the GUID in memory order.
	bool ParseGuid(const std::string& s, unsigned char out[16])
	{
		std::string h;
		for(size_t i = 0; i < s.size(); ++i)
			if(HexNibble(s[i]) >= 0)
				h += s[i];
		if(h.size() != 32)
			return false;
		unsigned char b[16];
		for(int i = 0; i < 16; ++i)
			b[i] = (unsigned char)(HexNibble(h[2 * i]) * 16 + HexNibble(h[2 * i + 1]));
		const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};	// Data1-3 little-endian
		for(int i = 0; i < 16; ++i)
			out[i] = b[order[i]];
		return true;
	}

	bool ILess(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; }

	std::vector<std::string> List(const std::string& dir, bool bDirs, const char* szPattern)
	{
		std::vector<std::string> v;
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\" + szPattern).c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			return v;
		do
		{
			const std::string n = fd.cFileName;
			if(n == "." || n == "..")
				continue;
			if(((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == bDirs)
				v.push_back(n);
		} while(FindNextFileA(h, &fd));
		FindClose(h);
		std::sort(v.begin(), v.end(), ILess);
		return v;
	}

	// The new-game DLC list (see the top of the file). szExclude: comma-separated GUID strings.
	std::vector<Civ5SaveHeader::Dlc> NewGameDlc(const char* szGameDir, const char* szExclude)
	{
		std::vector<std::string> excl;
		const std::string ex = szExclude ? szExclude : "";
		for(size_t a = 0; a < ex.size();)
		{
			size_t b = ex.find(',', a);
			if(b == std::string::npos)
				b = ex.size();
			unsigned char g[16];
			if(ParseGuid(ex.substr(a, b - a), g))
				excl.push_back(std::string((const char*)g, 16));
			a = b + 1;
		}
		std::vector<Civ5SaveHeader::Dlc> out;
		const std::string root = std::string(szGameDir) + "\\Assets\\DLC";
		const std::vector<std::string> dirs = List(root, true, "*");
		for(size_t i = 0; i < dirs.size(); ++i)
		{
			const std::vector<std::string> pkgs = List(root + "\\" + dirs[i], false, "*.Civ5Pkg");
			for(size_t k = 0; k < pkgs.size(); ++k)
			{
				std::string x;
				if(!Civ5ReadFile((root + "\\" + dirs[i] + "\\" + pkgs[k]).c_str(), x))
					continue;
				if(x.find("<SteamApp>") == std::string::npos || x.find("<Upgrade>") != std::string::npos)
					continue;
				Civ5SaveHeader::Dlc d;
				if(!ParseGuid(Tag(x, "<GUID>", "</GUID>"), d.guid))
					continue;
				if(std::find(excl.begin(), excl.end(), std::string((const char*)d.guid, 16)) != excl.end())
					continue;
				const size_t n = x.find("<Name>");
				d.one = 1;
				d.name = n == std::string::npos ? std::string() : Tag(x, "<Value language=\"en_US\">", "</Value>", n);
				out.push_back(d);
			}
		}
		return out;
	}

	bool WriteBytes(const std::string& path, const std::string& data, std::string& why)
	{
		return Civ5WriteFile(path.c_str(), data, why);
	}

	// Reads the pregame and the body into the DLL. Returns false with the reason.
	bool LoadParts(ICvGameContext1* pContext, ICvDLLDatabaseUtility1* pDbUtil, const std::string& pre, const std::string& body,
	               const char* szLoadFileName, std::string& why)
	{
		ICvPreGame1* p = pContext->GetPreGame();
		p->resetGame();
		p->resetPlayers();
		p->clearDLCAllowed();
		FMemoryStream sp;
		sp.Init((int)pre.size(), pre.data(), false);
		p->read(sp, true);
		const unsigned int uPreUsed = sp.GetPosition();
		p->setGameType(GAME_SINGLE_PLAYER, GAME_LOADED);
		p->setLoadFileName(CvString(szLoadFileName));
		p->Destroy();

		pDbUtil->CacheGameDatabaseData();
		{
			// Ticket 0681: the mod-mismatch check (srv_remote_savemods.h): the body's game data hash (bytes 4..19) and
			// its gamecore version string (u32 length at 0x14)
			std::string ver;
			if(body.size() >= 0x18)
			{
				const unsigned char* b = reinterpret_cast<const unsigned char*>(body.data());
				const unsigned int n = (unsigned)b[0x14] | (unsigned)b[0x15] << 8 | (unsigned)b[0x16] << 16 | (unsigned)b[0x17] << 24;
				if(n <= 512 && body.size() >= 0x18 + (size_t)n)
					ver.assign(body, 0x18, n);
			}
			SrvSaveMods::BeginLoad(szLoadFileName, body.size() >= 20 ? reinterpret_cast<const unsigned char*>(body.data()) + 4 : NULL, ver,
			                       body.substr(0, 4096));
		}
		try
		{
			const bool ok = ReadBody(pContext, body, uPreUsed, pre.size(), why);
			SrvSaveMods::EndLoad(ok, why.c_str());
			return ok;
		}
		catch(...)
		{
			SrvSaveMods::EndLoad(false, "an exception in the load");
			throw;
		}
	}

	// The body's reads (Game, Map, teams, players, supporting classes) of LoadParts.
	bool ReadBody(ICvGameContext1* pContext, const std::string& body, unsigned int uPreUsed, size_t preSize, std::string& why)
	{
		FMemoryStream sb;
		sb.Init((int)body.size(), body.data(), false);
		ICvGame1* pGame = pContext->GetGame();
		pGame->Read(sb);
		const unsigned int uAfterGame = sb.GetPosition();
		SrvLog("load: pregame read %u of %zu bytes; Game::Read used %u bytes of the body", uPreUsed, preSize, uAfterGame);
		// Ticket 0488: Game::Read ends with the save's embedded database, written through FIFileSystem::Create. When that
		// create was refused or failed the DLL only asserts and leaves the database's bytes in the stream; Map::Read would
		// take its width and height from the SQLite header and allocate 115 GB (server-host.md section 18). Stop here.
		if(*SrvFileCreateFailure())
		{
			pGame->Destroy();
			why = SrvFileCreateFailure();
			return false;
		}
		ICvMap1* pMap = pContext->GetMap();
		pMap->Read(sb);
		pMap->Destroy();
		for(int t = 0; t < MAX_TEAMS; ++t)
		{
			ICvTeam1* pTeam = pContext->GetTeam((TeamTypes)t);
			pTeam->Read(sb);
			pTeam->Destroy();
		}
		for(int pl = 0; pl < MAX_PLAYERS; ++pl)
		{
			ICvPlayer1* pp = pGame->GetPlayer((PlayerTypes)pl);
			pp->Read(sb);
			pp->Destroy();
		}
		// Issue 0452: the EXE recalculates the landmasses once here, after the 64 player reads and before
		// ReadSupportingClassData (its body reader's order; research/03-dll-engine-boundary/server-host.md section 3,
		// "Load"). CvMap::recalculateLandmasses starts with updateWaterFlags on every plot: the plots' adjacent-to-land,
		// lake and fresh-water flags are not saved, so without this call a loaded game plays with them all false.
		pMap = pContext->GetMap();
		pMap->RecalculateLandmasses();
		pMap->Destroy();
		pGame->ReadSupportingClassData(sb);
		pGame->Destroy();
		char sz[320];
		// The pregame has no length prefix in a .Civ5Save: the container reader finds it by the u32 2 marker. That
		// PreGame::read uses up exactly those bytes is the check that the split was right.
		sprintf_s(sz, "pregame %zu bytes (read %u, %s), body %zu bytes (game section %u, read %u in all, %s)", preSize,
		          uPreUsed, uPreUsed == preSize ? "used up exactly" : "NOT used up", body.size(), uAfterGame,
		          sb.GetPosition(), sb.GetPosition() == body.size() ? "used up exactly" : "NOT used up");
		why = sz;
		return sb.GetPosition() == body.size() && uPreUsed == preSize;
	}
}

// The header of a save of the current game (see the top of the file).
Civ5SaveHeader SrvBuildSaveHeader(ICvGameContext1* pContext, const char* szGameDir, const char* szDlcExclude)
{
	Civ5SaveHeader h;
	ICvPreGame1* pPre = pContext->GetPreGame();
	ICvGame1* pGame = pContext->GetGame();
	const PlayerTypes eActive = pGame->GetActivePlayer();
	ICvPlayer1* pPlayer = pGame->GetPlayer(eActive);
	h.turn = (unsigned int)pPre->gameTurn();
	h.gameType = (unsigned char)pPre->gameType();
	h.types[0] = TypeOf(pContext->GetCivilizationInfo(pPre->civilization(eActive)));
	h.types[1] = TypeOf(pContext->GetHandicapInfo(pPre->handicap(eActive)));
	h.types[2] = TypeOf(pContext->GetEraInfo(pPre->era()));
	h.types[3] = TypeOf(pContext->GetEraInfo(pPlayer ? pPlayer->GetCurrentEra() : pPre->era()));
	h.types[4] = TypeOf(pContext->GetGameSpeedInfo(pPre->gameSpeed()));
	h.types[5] = TypeOf(pPre->GetWorldInfo());
	h.types[6] = pPre->mapScriptName().c_str();
	if(s_bHaveLoadedHeader)
	{
		h.dlc = s_loadedHeader.dlc;
		h.mods = s_loadedHeader.mods;
	}
	else
		h.dlc = NewGameDlc(szGameDir, szDlcExclude);
	h.leaderName = pPre->leaderNameKey(eActive).c_str();
	h.civName = pPre->civilizationDescriptionKey(eActive).c_str();
	h.playerColor = TypeOf(pContext->GetPlayerColorInfo(pPlayer ? pPlayer->GetPlayerColor() : pPre->playerColor(eActive)));
	const GUID g = pContext->GetDLLGUID();
	memcpy(h.dllGuid, &g, 16);
	const char* szVer = pContext->GetDLLVersion();
	h.dllVersion = szVer ? szVer : "";
	if(pPlayer)
		pPlayer->Destroy();
	pGame->Destroy();
	pPre->Destroy();
	return h;
}

// Writes <base>.pregame.bin and <base>.body.bin (the DLL-written parts, uncompressed) and, with szGameDir set,
// <base>.Civ5Save (header + pregame + u32 2 + the deflated body).
bool SrvSaveCiv5Save(ICvGameContext1* pContext, const char* szBase, const char* szGameDir, const char* szDlcExclude,
                     std::string& why)
{
	FMemoryStream pre;
	ICvPreGame1* pPre = pContext->GetPreGame();
	pPre->write(pre);
	pPre->Destroy();

	FMemoryStream body;
	ICvGame1* pGame = pContext->GetGame();
	pGame->Write(body);
	ICvMap1* pMap = pContext->GetMap();
	pMap->Write(body);
	pMap->Destroy();
	for(int t = 0; t < MAX_TEAMS; ++t)
	{
		ICvTeam1* pTeam = pContext->GetTeam((TeamTypes)t);
		pTeam->Write(body);
		pTeam->Destroy();
	}
	for(int p = 0; p < MAX_PLAYERS; ++p)
	{
		ICvPlayer1* pl = pGame->GetPlayer((PlayerTypes)p);
		pl->Write(body);
		pl->Destroy();
	}
	pGame->WriteSupportingClassData(body);
	pGame->Destroy();
	const std::string base = szBase;
	const std::string strPre = ToBytes(pre), strBody = ToBytes(body);
	if(!WriteBytes(base + ".pregame.bin", strPre, why) || !WriteBytes(base + ".body.bin", strBody, why))
		return false;
	char sz[256];
	sprintf_s(sz, "pregame %zu bytes, body %zu bytes", strPre.size(), strBody.size());
	std::string res = sz;
	if(szGameDir)
	{
		const std::string hb = Civ5WriteHeader(SrvBuildSaveHeader(pContext, szGameDir, szDlcExclude));
		std::string file;
		if(!Civ5Pack(hb, strPre, strBody, file, why) || !WriteBytes(base + ".Civ5Save", file, why))
			return false;
		sprintf_s(sz, "; %s.Civ5Save %zu bytes (header %zu, zlib %s)", szBase, file.size(), hb.size(), Civ5ZlibVersion());
		res += sz;
	}
	why = res;
	return true;
}

bool SrvSaveRaw(ICvGameContext1* pContext, const char* szBase, std::string& why)
{
	return SrvSaveCiv5Save(pContext, szBase, NULL, NULL, why);
}

// Reads the pregame and the body files into the DLL. Returns false with the reason.
bool SrvLoadRaw(ICvGameContext1* pContext, ICvDLLDatabaseUtility1* pDbUtil, const char* szPregame, const char* szBody, const char* szLoadFileName,
                std::string& why)
{
	std::string pre, body;
	if(!VfsReadFile(szPregame, pre) || !VfsReadFile(szBody, body))
	{
		why = "cannot read the save's pregame or body file";
		return false;
	}
	s_bHaveLoadedHeader = false;
	return LoadParts(pContext, pDbUtil, pre, body, szLoadFileName && *szLoadFileName ? szLoadFileName : szBody, why);
}

// Reads a .Civ5Save natively (container, zlib 1.2.3) into the DLL; its header is kept for later saves.
bool SrvLoadCiv5Save(ICvGameContext1* pContext, ICvDLLDatabaseUtility1* pDbUtil, const char* szSave, const char* szLoadFileName,
                     std::string& why)
{
	std::string file, body;
	Civ5SaveParts parts;
	if(!Civ5ReadFile(szSave, file))
	{
		why = std::string("cannot read ") + szSave;
		return false;
	}
	if(!Civ5Split(file, parts, why) || !Civ5Inflate(file, parts, body, why))
		return false;
	s_loadedHeader = parts.header;
	s_bHaveLoadedHeader = true;
	SrvLog("load: %s: header %zu bytes (turn %u, %s, %zu DLC, %zu mods, DLL version %s), pregame %zu, %zu pieces -> body %zu "
	       "(zlib %s)", szSave, parts.headerBytes.size(), parts.header.turn, parts.header.types[0].c_str(),
	       parts.header.dlc.size(), parts.header.mods.size(), parts.header.dllVersion.c_str(), parts.pregame.size(),
	       parts.pieces.size(), body.size(), Civ5ZlibVersion());
	return LoadParts(pContext, pDbUtil, parts.pregame, body, szLoadFileName && *szLoadFileName ? szLoadFileName : szSave, why);
}
