// wbdump.cpp - loads .Civ5Map files through CvWorldBuilderMap::Load and prints everything the load produced, in an
// architecture-independent text form (ticket 0343). Linked three ways by run_ab.py: x86 with the 32-bit closed
// library (the reference), x86 with this repository's reader, x64 with this repository's reader.
//
//   wbdump TYPES.txt [--full] [--noscenario] [--resave DIR [--keep]] MAP.Civ5Map...
//
// --resave DIR (ticket 0326): every loaded map is also saved by CvWorldBuilderMap::Save in five cases (SaveCase) and
// the written file's length and hash printed; plus the writer's refusals once (SaveNegatives). run_ab.py compares
// the files of the 32-bit library with ours byte for byte through these hashes.
//
// TYPES.txt: the game's type lists in the DLL's order (typedesc.py writes it from a gameplay database):
// "[terrain]" ... "[promotions]" section lines, then one type per line, "-" for a missing row (Add(NULL)).
// Each section line is "<tag> <length> <fnv1a-64>" of a canonical byte image (field by field, no padding);
// with --full the image is also printed in hex, 64 bytes a line. A map is loaded into a fresh object.
#define private public
#define protected public
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "fw_prelude.h"
#include <bitset>
#include <FireWorks/FFastVector.h>
#include <FireWorks/FString.h>
#ifndef MAX
#define MAX(a, b) std::max(a, b)
#endif
#define CV_WORLD_BUILDER_MAP_IN_GAME
#include "CvWorldBuilderMap.h"
#include "CvWorldBuilderMapTypeDesc.h"
#undef private
#undef protected
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <string>
#include <vector>

typedef CvWorldBuilderMap WBM;

namespace
{
bool g_bFull = false;

struct Img
{
	std::vector<unsigned char> b;
	void raw(const void* p, size_t n)
	{
		const unsigned char* c = static_cast<const unsigned char*>(p);
		b.insert(b.end(), c, c + n);
	}
	void u8(unsigned v) { b.push_back((unsigned char)v); }
	// A fixed char array as the C string it holds: the bytes after the NUL are left over from earlier use of the
	// memory (strcpy_s of a converted record) and are never read by the DLL.
	void cstr(const char* s, size_t cap)
	{
		size_t n = 0;
		while(n < cap && s[n])
			++n;
		raw(s, n);
		for(size_t i = n; i < cap; ++i)
			u8(0);
	}
	void u16(unsigned v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
	void u32(unsigned v) { u16(v & 0xFFFF); u16((v >> 16) & 0xFFFF); }
	void str(const char* s)
	{
		const size_t n = s ? strlen(s) : 0;
		u32((unsigned)n);
		if(n) raw(s, n);
	}
	template<size_t N> void bits(const std::bitset<N>& k)
	{
		for(size_t i = 0; i < N; i += 8)
		{
			unsigned v = 0;
			for(size_t j = 0; j < 8 && i + j < N; ++j)
				if(k[i + j]) v |= 1u << j;
			u8(v);
		}
	}
};

unsigned long long Fnv(const std::vector<unsigned char>& b)
{
	unsigned long long h = 1469598103934665603ULL;
	for(size_t i = 0; i < b.size(); ++i)
	{
		h ^= b[i];
		h *= 1099511628211ULL;
	}
	return h;
}

void Emit(const char* tag, const Img& im)
{
	printf("%s %u %016llx\n", tag, (unsigned)im.b.size(), Fnv(im.b));
	if(g_bFull)
	{
		for(size_t i = 0; i < im.b.size(); i += 64)
		{
			printf("  %s+%06x ", tag, (unsigned)i);
			for(size_t j = i; j < i + 64 && j < im.b.size(); ++j)
				printf("%02x", im.b[j]);
			printf("\n");
		}
	}
}

void PlayerImg(Img& im, const WBM::Player& p)
{
	im.bits(p.m_kPolicies);
	im.cstr(p.m_szLeaderName, 64);
	im.cstr(p.m_szCivName, 64);
	im.cstr(p.m_szCivType, 64);
	im.cstr(p.m_szTeamColor, 64);
	im.cstr(p.m_szEra, 64);
	im.cstr(p.m_szHandicap, 64);
	im.u32(p.m_uiCulture);
	im.u32(p.m_uiGold);
	im.u32(p.m_uiStartX);
	im.u32(p.m_uiStartY);
	im.u8(p.m_byTeam);
	im.u8(p.m_bPlayable ? 1 : 0);
}

// The allocator's entries: free-list entries (walked from the head) as their next index, the others by `fn`.
template<class T, uint N, class F>
void AllocImg(Img& im, const CvWorldBuilderMapElementAllocator<T, N>& a, F fn)
{
	im.u32(a.m_uiSize);
	im.u32((unsigned)a.m_iFreeList);
	std::vector<bool> free(a.m_uiSize, false);
	unsigned guard = 0;
	for(int i = a.m_iFreeList; i >= 0 && i < (int)a.m_uiSize && !free[i] && guard < a.m_uiSize; ++guard)
	{
		free[i] = true;
		i = a.m_aData[i].iNextFreeEntry;
	}
	for(uint i = 0; i < a.m_uiSize; ++i)
	{
		if(free[i])
		{
			im.u8(0xFE);
			im.u32((unsigned)a.m_aData[i].iNextFreeEntry);
		}
		else
		{
			im.u8(0x01);
			fn(im, *reinterpret_cast<const T*>(&a.m_aData[i]));
		}
	}
}

void UnitImg(Img& im, const WBM::Unit& u)
{
	im.u16(u.m_hStackedUnit.ToWord());
	im.u16(u.m_hCustomName.ToWord());
	im.u32(u.m_uiExperience);
	im.u32(u.m_uiHealth);
	im.u32(u.m_byUnitType);
	im.u8(u.m_byOwner);
	im.u8(u.m_byDirection);
	im.u8(u.m_byFlags);
	im.bits(u.m_kPromotions);
}
void NameImg(Img& im, const WBM::UnitName& n) { im.cstr(n.m_szText, 64); }
void CityImg(Img& im, const WBM::City& c)
{
	im.cstr(c.m_szName, 64);
	im.u8(c.m_byOwner);
	im.u8(c.m_byFlags);
	im.u16(c.m_wPopulation);
	im.u32(c.m_uiHealth);
	im.bits(c.m_kBuildings);
}

void ModTypeImg(Img& im, const ModType& t)
{
	const uint n = t.GetFieldCount();
	im.u32(n);
	for(uint i = 0; i < n + 1; ++i)	// one past the end: the out-of-range answers too
	{
		im.str(t.GetFieldName(i) ? t.GetFieldName(i) : "(null)");
		im.u32((unsigned)t.GetFieldType(i));
		im.u8(t.GetFieldDefaultAsBool(i) ? 1 : 0);
		im.u32((unsigned)t.GetFieldDefaultAsInt(i));
		const float f = t.GetFieldDefaultAsFloat(i);
		unsigned uf;
		memcpy(&uf, &f, 4);
		im.u32(uf);
		im.str(t.GetFieldDefaultAsString(i));
		const ModEnum* e = t.GetFieldEnumType(i);
		im.u32(e ? e->GetMemberCount() : 0xFFFFFFFFu);
		if(e)
			for(uint m = 0; m < e->GetMemberCount() + 1; ++m)
				im.str(e->GetMember(m) ? e->GetMember(m) : "(null)");
	}
}

void EntryImg(Img& im, const ModDataEntry& e, uint n)
{
	if(n == 0)
		return;
	if(e.m_aFields == NULL)
	{
		im.u8(0xEE);
		return;
	}
	for(uint i = 0; i < n; ++i)
	{
		im.u8(e.GetFieldAsBool(i) ? 1 : 0);
		im.u32((unsigned)e.GetFieldAsInt(i));
		im.u8(e.IsFieldDefaultValue(i) ? 1 : 0);
		if(e.m_kModType.GetFieldType(i) == ModType::TYPE_STRING)
			im.str(e.GetFieldAsString(i));
	}
}

bool ReadTypes(const char* path, CvWorldBuilderMapTypeDesc& d)
{
	FILE* f = fopen(path, "rb");
	if(!f)
		return false;
	char line[512];
	CvWorldBuilderMapTypeDesc::TypeMap* cur = NULL;
	CvWorldBuilderMapTypeDesc::TypeMap* cur2 = NULL;
	while(fgets(line, sizeof(line), f))
	{
		size_t n = strlen(line);
		while(n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if(!n)
			continue;
		if(line[0] == '[')
		{
			cur2 = NULL;
			if(!strcmp(line, "[terrain]")) cur = &d.m_kTerrainTypes;
			else if(!strcmp(line, "[features]")) { cur = &d.m_kFeatures; cur2 = &d.m_kNaturalWonders; }
			else if(!strcmp(line, "[resources]")) cur = &d.m_kResources;
			else if(!strcmp(line, "[improvements]")) cur = &d.m_kImprovements;
			else if(!strcmp(line, "[units]")) cur = &d.m_kUnits;
			else if(!strcmp(line, "[techs]")) cur = &d.m_kTechs;
			else if(!strcmp(line, "[policies]")) cur = &d.m_kPolicies;
			else if(!strcmp(line, "[buildings]")) cur = &d.m_kBuildingTypes;
			else if(!strcmp(line, "[promotions]")) cur = &d.m_kUnitPromotions;
			else cur = NULL;
			continue;
		}
		const char* sz = strcmp(line, "-") ? line : NULL;
		if(cur) cur->Add(sz);
		if(cur2) cur2->Add(sz);
	}
	fclose(f);
	return true;
}

// --resave DIR (ticket 0326): the writer. Each case saves the object to DIR\<map index>-<tag>.Civ5Map and prints
// "SAVE <tag> <result> <file length> <fnv1a-64 of the file>" ("- -" when no file is there afterwards). The file is
// removed after hashing unless --keep.
const wchar_t* g_wszResave = NULL;
bool g_bKeep = false;
unsigned g_uiMapIndex = 0;

void FileHash(const wchar_t* wsz, long& len, unsigned long long& h)
{
	len = -1;
	h = 0;
	FILE* f = _wfopen(wsz, L"rb");
	if(!f)
		return;
	Img im;
	unsigned char buf[65536];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		im.raw(buf, n);
	fclose(f);
	len = (long)im.b.size();
	h = Fnv(im.b);
}

void SaveCase(const WBM* m, const CvWorldBuilderMapTypeDesc& kDesc, const char* tag, bool bScenario)
{
	if(!g_wszResave)
		return;
	wchar_t wsz[1024];
	_snwprintf_s(wsz, 1024, _TRUNCATE, L"%s\\%04u-%S.Civ5Map", g_wszResave, g_uiMapIndex, tag);
	DeleteFileW(wsz);
	const bool ok = m->Save(wsz, kDesc, bScenario);
	long len;
	unsigned long long h;
	FileHash(wsz, len, h);
	if(len < 0)
		printf("SAVE %s %d - -\n", tag, ok ? 1 : 0);
	else
		printf("SAVE %s %d %ld %016llx\n", tag, ok ? 1 : 0, len, h);
	if(!g_bKeep)
		DeleteFileW(wsz);
}

// The writer's refusals: a path in a folder that does not exist, an empty path, no path. Each must answer false and
// leave no file.
void SaveNegatives(const CvWorldBuilderMapTypeDesc& kDesc)
{
	if(!g_wszResave)
		return;
	void* mem = calloc(1, sizeof(WBM));
	WBM* m = new(mem) WBM();
	wchar_t wsz[1024];
	_snwprintf_s(wsz, 1024, _TRUNCATE, L"%s\\no-such-folder\\x.Civ5Map", g_wszResave);
	const bool a = m->Save(wsz, kDesc, true);
	const bool aExists = GetFileAttributesW(wsz) != INVALID_FILE_ATTRIBUTES;
	const bool b = m->Save(L"", kDesc, true);
	const bool c = m->Save(NULL, kDesc, false);
	printf("SAVENEG missing-folder %d exists %d; empty-path %d; null-path %d\n", a ? 1 : 0, aExists ? 1 : 0, b ? 1 : 0,
	       c ? 1 : 0);
	m->~WBM();
	free(mem);
}

void Dump(const char* szPath, const CvWorldBuilderMapTypeDesc& kDesc, bool bScenario)
{
	wchar_t wsz[1024];
	MultiByteToWideChar(CP_UTF8, 0, szPath, -1, wsz, 1024);
	printf("MAP %s\n", szPath);
	printf("PREVIEW %u\n", WBM::PreviewPlayableCivCount(wsz));
	// Zero-filled storage, as the DLL's objects (statics of CvWorldBuilderMapLoader.cpp): the fields the
	// constructors leave alone are then zero on both sides, and a re-save of them is defined.
	void* mem = calloc(1, sizeof(WBM));
	WBM* m = new(mem) WBM();
	const bool ok = m->Load(wsz, kDesc, bScenario);
	printf("LOAD %d\n", ok ? 1 : 0);
	printf("DIM %u %u flags %08x\n", m->GetWidth(), m->GetHeight(), m->m_uiFlags);
	printf("WORLD %.64s\n", m->m_szWorldType);
	printf("NAME %s\n", m->m_sMapName.c_str());
	printf("DESC %s\n", m->m_sMapDescription.c_str());
	const WBM::GameDescription& g = m->m_kGameDesc;
	printf("GAME speed=%.64s start=%u max=%u target=%u year=%d players=%u cs=%u teams=%u\n", g.szDefaultSpeed,
	       g.uiStartingTurn, g.uiMaxTurns, g.uiTargetScore, g.iStartYear, g.byPlayerCount, g.byCityStateCount,
	       g.byTeamCount);
	const uint uiPlots = m->GetPlotCount() <= WBM::MaxMapSize ? m->GetPlotCount() : 0;
	{
		Img im;
		im.raw(m->m_szWorldType, 64);
		Emit("worldtype", im);
	}
	{
		Img im;
		for(uint i = 0; i < uiPlots; ++i)
		{
			const WBM::PlotMapData& p = m->m_akMapData[i];
			im.u8(p.GetTerrainType()); im.u8(p.GetResourceType()); im.u8(p.GetFeatureType()); im.u8(p.GetFlags());
			im.u8(p.GetPlotHeight()); im.u8(p.GetContinentType()); im.u8(p.GetNaturalWonderType());
			im.u8(p.GetResourceAmount());
		}
		Emit("plotmap", im);
	}
	{
		Img im;
		for(uint i = 0; i < uiPlots; ++i)
		{
			const WBM::PlotScenarioData& p = m->m_akScenarioData[i];
			im.u16(p.m_hCity.ToWord()); im.u16(p.m_hUnitStack.ToWord()); im.u8(p.m_byCulture);
			im.u8(p.m_byImprovement); im.u8(p.m_byRoute); im.u8(p.m_byRouteOwner);
		}
		Emit("plotscenario", im);
	}
	{
		Img im;
		for(uint i = 0; i < g.byPlayerCount && i < WBM::MaxPlayers; ++i)
			PlayerImg(im, m->m_akPlayers[i]);
		Emit("players", im);
	}
	{
		Img im;
		for(uint i = 0; i < g.byCityStateCount && i < WBM::MaxCityStates; ++i)
			PlayerImg(im, m->m_akCityStates[i]);
		Emit("citystates", im);
	}
	{
		Img im;
		for(uint i = 0; i < g.byTeamCount && i < WBM::MaxTeams; ++i)
		{
			im.cstr(m->m_akTeams[i].m_szName, 32);
			im.bits(m->m_akTeams[i].m_kTechs);
		}
		Emit("teams", im);
	}
	{ Img im; AllocImg(im, m->m_kUnits, UnitImg); Emit("units", im); }
	{ Img im; AllocImg(im, m->m_kUnitNames, NameImg); Emit("unitnames", im); }
	{ Img im; AllocImg(im, m->m_kCities, CityImg); Emit("cities", im); }
	{
		Img im;
		im.raw(m->m_kTeamsInContact.m_Mem, sizeof(m->m_kTeamsInContact.m_Mem));
		im.raw(m->m_kTeamsAtWar.m_Mem, sizeof(m->m_kTeamsAtWar.m_Mem));
		im.raw(m->m_kTeamsAtPermanentWarOrPeace.m_Mem, sizeof(m->m_kTeamsAtPermanentWarOrPeace.m_Mem));
		im.raw(m->m_kTeamsSharingOpenBorders.m_Mem, sizeof(m->m_kTeamsSharingOpenBorders.m_Mem));
		im.raw(m->m_kTeamsSharingDefensivePacts.m_Mem, sizeof(m->m_kTeamsSharingDefensivePacts.m_Mem));
		Emit("relations", im);
	}
	{
		Img im;
		for(uint p = 0; p < WBM::MaxPlayers; ++p)
			for(uint c = 0; c < WBM::MaxCityStates; ++c)
				im.u32((unsigned)m->m_aiCityStateInfluence[p][c]);
		Emit("influence", im);
	}
	{
		Img im;
		im.raw(m->m_kVisibility.m_Mem, sizeof(m->m_kVisibility.m_Mem));
		const bool full = g_bFull;
		g_bFull = false;
		Emit("visibility", im);
		g_bFull = full;
		unsigned n = 0;
		for(size_t i = 0; i < im.b.size(); ++i)
			for(unsigned v = im.b[i]; v; v &= v - 1)
				++n;
		printf("visibility-bits %u\n", n);
		// Version 9: the 32-bit library reads the last players x 256 bytes of the visibility from past the end of its
		// block (heap contents, different on every run); only the part before them comes from the file.
		FILE* f = _wfopen(wsz, L"rb");
		int iVersion = f ? fgetc(f) : -1;
		if(f) fclose(f);
		if(iVersion >= 0 && (iVersion & 0x7F) == 9)
		{
			const uint uiVisBits = (uint)g.byTeamCount * uiPlots;
			const uint uiVisBytes = uiVisBits / 8 + ((uiVisBits % 8) ? 1 : 0);
			const uint uiTail = (uint)g.byPlayerCount * 256;
			Img head;
			if(uiVisBytes > uiTail)
				head.raw(m->m_kVisibility.m_Mem, uiVisBytes - uiTail);
			Emit("visibility-v9head", head);
		}
	}
	{
		Img im;
		im.u32(m->m_kVictoryTypes.GetSerializedSize());
		if(m->m_kVictoryTypes.m_szBuffer) im.raw(m->m_kVictoryTypes.m_szBuffer, m->m_kVictoryTypes.m_uiBufferSize);
		im.u32(m->m_kGameOptions.GetSerializedSize());
		if(m->m_kGameOptions.m_szBuffer) im.raw(m->m_kGameOptions.m_szBuffer, m->m_kGameOptions.m_uiBufferSize);
		const char* probes[] = {"VICTORY_TIME", "VICTORY_SPACE_RACE", "VICTORY_DOMINATION", "VICTORY_CULTURAL",
		                        "VICTORY_DIPLOMATIC", "GAMEOPTION_NO_BARBARIANS", "GAMEOPTION_RAGING_BARBARIANS", ""};
		for(int i = 0; i < 8; ++i)
		{
			im.u8(m->m_kVictoryTypes.Contains(probes[i]) ? 1 : 0);
			im.u8(m->m_kGameOptions.Contains(probes[i]) ? 1 : 0);
		}
		Emit("typegroups", im);
	}
	{
		Img im;
		ModTypeImg(im, m->m_kModData.m_kMapModData);
		ModTypeImg(im, m->m_kModData.m_kPlotModData);
		im.u32(m->m_kModData.m_kData.size());
		for(uint i = 0; i < m->m_kModData.m_kData.size(); ++i)
			im.u32(m->m_kModData.m_kData[i]);
		im.u32(m->m_kModData.m_kStringBuffer.m_uiSize);
		if(m->m_kModData.m_kStringBuffer.m_szBuffer)
			im.raw(m->m_kModData.m_kStringBuffer.m_szBuffer, m->m_kModData.m_kStringBuffer.m_uiSize);
		EntryImg(im, m->m_kModData.GetMapDataEntry(), m->m_kModData.m_kMapModData.GetFieldCount());
		const uint nPlotFields = m->m_kModData.m_kPlotModData.GetFieldCount();
		if(nPlotFields && m->m_kModData.m_kData.size() >= m->m_kModData.m_kMapModData.GetFieldCount() + nPlotFields * uiPlots)
			for(uint i = 0; i < uiPlots; ++i)
				EntryImg(im, m->m_kModData.GetPlotDataEntry(i), nPlotFields);
		Emit("moddata", im);
		printf("moddata-fields map=%u plot=%u data=%u strings=%u\n", m->m_kModData.m_kMapModData.GetFieldCount(),
		       nPlotFields, m->m_kModData.m_kData.size(), m->m_kModData.m_kStringBuffer.m_uiSize);
	}
	// The writer on the loaded object: the re-save (scenario part when it was read) and the map part only.
	if(ok)
	{
		SaveCase(m, kDesc, "scen", true);
		SaveCase(m, kDesc, "map", false);
	}
	// The operations the DLL's loader applies after a load.
	if(ok && g.byPlayerCount > 1)
	{
		m->SwapPlayers(0, (byte)(g.byPlayerCount - 1));
		Img im;
		for(uint i = 0; i < g.byPlayerCount && i < WBM::MaxPlayers; ++i)
			PlayerImg(im, m->m_akPlayers[i]);
		for(uint i = 0; i < uiPlots; ++i)
		{
			const WBM::PlotScenarioData& p = m->m_akScenarioData[i];
			im.u8(p.m_byCulture); im.u8(p.m_byRouteOwner);
		}
		AllocImg(im, m->m_kUnits, UnitImg);
		AllocImg(im, m->m_kCities, CityImg);
		for(uint p = 0; p < WBM::MaxPlayers; ++p)
			for(uint c = 0; c < WBM::MaxCityStates; ++c)
				im.u32((unsigned)m->m_aiCityStateInfluence[p][c]);
		Emit("after-swap", im);
	}
	if(ok)
	{
		m->ClearScenarioData(true);
		Img im;
		for(uint i = 0; i < uiPlots; ++i)
			im.raw(&m->m_akScenarioData[i], 8);
		const WBM::GameDescription& g2 = m->m_kGameDesc;
		im.u32(g2.uiStartingTurn); im.u32(g2.uiMaxTurns); im.u32(g2.uiTargetScore); im.u32((unsigned)g2.iStartYear);
		im.u8(g2.byPlayerCount); im.u8(g2.byCityStateCount); im.u8(g2.byTeamCount); im.u8(g2.szDefaultSpeed[0]);
		im.u32(m->m_kUnits.m_uiSize); im.u32(m->m_kCities.m_uiSize); im.u32(m->m_kUnitNames.m_uiSize);
		im.u32(m->m_kVictoryTypes.GetSerializedSize()); im.u32(m->m_kGameOptions.GetSerializedSize());
		Emit("after-clear", im);
		// After SwapPlayers and ClearScenarioData: the object the DLL keeps after its Preload.
		SaveCase(m, kDesc, "cleared", true);
		// The DLL's own save (CvWorldBuilderMapLoader::Save, the Save Map button): Resize to the map, clear the
		// scenario data, set name, description, flags and world type, save without the scenario part.
		m->Resize(m->GetWidth(), m->GetHeight());
		m->ClearScenarioData();
		m->m_sMapName = "Resaved map";
		m->m_sMapDescription = "";
		const bool bWrap = m->GetFlag(WBM::FLAG_WORLD_WRAP);
		m->m_uiFlags = 0;
		m->SetFlag(WBM::FLAG_WORLD_WRAP, bWrap);
		m->SetWorldType("WORLDSIZE_SMALL");
		SaveCase(m, kDesc, "dllflow", false);
		// A smaller map and a refused world type (63 characters fit, 64 do not: the buffer is cleared).
		m->Resize(m->GetWidth() > 1 ? m->GetWidth() / 2 : 1, m->GetHeight());
		m->Resize(0, 5);	// refused: the size stays
		m->SetWorldType("WORLDSIZE_0123456789012345678901234567890123456789012345678901234");
		SaveCase(m, kDesc, "resized", true);
	}
	m->~WBM();
	free(mem);
}
} // namespace

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		fprintf(stderr, "usage: wbdump TYPES.txt [--full] [--noscenario] MAP...\n");
		return 2;
	}
	CvWorldBuilderMapTypeDesc kDesc;
	if(!ReadTypes(argv[1], kDesc))
	{
		fprintf(stderr, "wbdump: cannot read %s\n", argv[1]);
		return 2;
	}
	printf("TYPES terrain=%u features=%u wonders=%u resources=%u improvements=%u units=%u techs=%u policies=%u "
	       "buildings=%u promotions=%u\n", kDesc.m_kTerrainTypes.Size(), kDesc.m_kFeatures.Size(),
	       kDesc.m_kNaturalWonders.Size(), kDesc.m_kResources.Size(), kDesc.m_kImprovements.Size(),
	       kDesc.m_kUnits.Size(), kDesc.m_kTechs.Size(), kDesc.m_kPolicies.Size(), kDesc.m_kBuildingTypes.Size(),
	       kDesc.m_kUnitPromotions.Size());
	bool bScenario = true;
	static wchar_t wszResave[1024];
	for(int i = 2; i < argc; ++i)
	{
		if(!strcmp(argv[i], "--full")) { g_bFull = true; continue; }
		if(!strcmp(argv[i], "--noscenario")) { bScenario = false; continue; }
		if(!strcmp(argv[i], "--keep")) { g_bKeep = true; continue; }
		if(!strcmp(argv[i], "--resave") && i + 1 < argc)
		{
			MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, wszResave, 1024);
			g_wszResave = wszResave;
			SaveNegatives(kDesc);
			fflush(stdout);
			continue;
		}
		Dump(argv[i], kDesc, bScenario);
		++g_uiMapIndex;
		fflush(stdout);
	}
	printf("END\n");
	return 0;
}
