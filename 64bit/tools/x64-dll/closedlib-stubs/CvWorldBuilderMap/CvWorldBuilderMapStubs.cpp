// CvWorldBuilderMap (CvWorldBuilderMapWin32.obj): construction. No logging stub is left in this group since
// ticket 0326.
// - constructors, destructors and the four error statics are REAL initialisation (the DLL has two
//   static CvWorldBuilderMap objects and a static CvWorldBuilderMapTypeDesc in
//   CvWorldBuilderMapLoader.cpp, built during DLL static initialisation, so these must work and
//   must not log); the default values follow the x86 nested constructors (disassembly 2026-11-10);
// - CvWorldBuilderMapTypeGroup::Contains is REAL (trivial scan of its packed string list);
// - the read side (Load, PreviewPlayableCivCount, ClearScenarioData, Resize, SetWorldType, SwapPlayers,
//   TypeMap::Add and the mod data getters) is REAL since ticket 0343: CvWorldBuilderMapReader.cpp;
// - the writer (Save) is REAL since ticket 0326: CvWorldBuilderMapWriter.cpp.
#include "../FireWorks/fw_prelude.h"
#include <bitset>
#include <FireWorks/FFastVector.h>
#include <FireWorks/FString.h>
#define MAX(a, b) std::max(a, b)              // as in CvGameCoreDLLPCH.h
#define CV_WORLD_BUILDER_MAP_IN_GAME          // as in CvWorldBuilderMapLoader.cpp: same class layout
#include "CvWorldBuilderMap.h"
#include "CvWorldBuilderMapTypeDesc.h"
#include <string.h>

// ---------------------------------------------------------------------------------------------
// Construction (real)

CvWorldBuilderMap::PlotMapData::PlotMapData()
	: m_byTerrainType(0)
	, m_byResourceType(InvalidResource)
	, m_byFeatureType(InvalidFeature)
	, m_byFlags(0)
	, m_byPlotHeight(0)
	, m_byContinentType(0)
	, m_byNaturalWonderType(InvalidNaturalWonder)
	, m_byResourceAmount(0)
{
}

CvWorldBuilderMap::PlotScenarioData::PlotScenarioData()
{
	// x86 writes 0xFF into all 8 bytes: no city, no unit stack, invalid culture/improvement/route.
	memset(this, 0xFF, sizeof(*this));
}

CvWorldBuilderMap::Team::Team()
	: m_kTechs()
{
	m_szName[0] = '\0';
}

CvWorldBuilderMap::Player::Player()
	: m_kPolicies()
	, m_uiCulture(0)
	, m_uiGold(0)
	, m_uiStartX(0)
	, m_uiStartY(0)
	, m_byTeam(InvalidTeam)
	, m_bPlayable(false)
{
	// x86 empties the six name strings and leaves the numbers uninitialised; zeroed here.
	m_szLeaderName[0] = '\0';
	m_szCivName[0] = '\0';
	m_szCivType[0] = '\0';
	m_szTeamColor[0] = '\0';
	m_szEra[0] = '\0';
	m_szHandicap[0] = '\0';
}

CvWorldBuilderMap::MapDescription::MapDescription()
	: uiWidth(0)
	, uiHeight(0)
{
}

CvWorldBuilderMap::GameDescription::GameDescription()
	: uiStartingTurn(0)
	, uiMaxTurns(0)
	, uiTargetScore(0)
	, iStartYear(-4000)
	, byPlayerCount(0)
	, byCityStateCount(0)
	, byTeamCount(0)
{
	szDefaultSpeed[0] = '\0';
}

CvWorldBuilderMap::PlotMapData CvWorldBuilderMap::sm_kErrorPlotMapData;
CvWorldBuilderMap::PlotScenarioData CvWorldBuilderMap::sm_kErrorPlotScenarioData;
CvWorldBuilderMap::Player CvWorldBuilderMap::sm_kErrorPlayer;
CvWorldBuilderMap::Team CvWorldBuilderMap::sm_kErrorTeam;

ModDataStringBuffer::ModDataStringBuffer()
	: m_uiSize(0)
	, m_uiReservedSize(0)
	, m_szBuffer(NULL)
{
}

ModDataStringBuffer::~ModDataStringBuffer()
{
	m_uiSize = 0;
	m_uiReservedSize = 0;
	delete [] m_szBuffer; // new[]'d by Deserialize (CvWorldBuilderMapReader.cpp)
	m_szBuffer = NULL;
}

ModType::ModType(ModDataStringBuffer& kStringBuffer)
	: m_kStringBuffer(kStringBuffer)
{
}

CvWorldBuilderMapModData::CvWorldBuilderMapModData()
	: m_kMapModData(m_kStringBuffer)
	, m_kPlotModData(m_kStringBuffer)
{
}

CvWorldBuilderMapTypeGroup::CvWorldBuilderMapTypeGroup()
	: m_szBuffer(NULL)
	, m_uiBufferSize(0)
{
}

CvWorldBuilderMapTypeGroup::~CvWorldBuilderMapTypeGroup()
{
	if(m_szBuffer)
		FFREE(m_szBuffer); // FMALLOC'd by Deserialize (CvWorldBuilderMapReader.cpp)
	m_szBuffer = NULL;
	m_uiBufferSize = 0;
}

bool CvWorldBuilderMapTypeGroup::Contains(const char* szType) const
{
	// x86: the buffer is a list of entries [signed length byte][NUL-terminated name]; the next
	// entry starts at offset + length + 2.
	if(szType == NULL || *szType == '\0' || m_szBuffer == NULL || m_uiBufferSize == 0)
		return false;
	for(uint uiOffset = 0; uiOffset < m_uiBufferSize; uiOffset += 2 + static_cast<signed char>(m_szBuffer[uiOffset]))
	{
		if(strcmp(m_szBuffer + uiOffset + 1, szType) == 0)
			return true;
	}
	return false;
}

CvWorldBuilderMapTypeDesc::CvWorldBuilderMapTypeDesc()
	: m_kTerrainTypes(32)
	, m_kFeatures(128)
	, m_kNaturalWonders(128)
	, m_kResources(64)
	, m_kImprovements(64)
	, m_kUnits(256)
	, m_kBuildingTypes(256)
	, m_kTechs(256)
	, m_kPolicies(256)
	, m_kUnitPromotions(256)
{
}

CvWorldBuilderMap::CvWorldBuilderMap()
{
	m_uiFlags = 0;
	m_szWorldType[0] = '\0';
	memset(m_aiCityStateInfluence, 0, sizeof(m_aiCityStateInfluence));
}

CvWorldBuilderMap::~CvWorldBuilderMap()
{
}

ModDataEntry::ModDataEntry(ModType& kModType, dword* aFields)
	: m_kModType(kModType)
	, m_aFields(aFields)
{
}
