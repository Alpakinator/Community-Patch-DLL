// CvWorldBuilderMapWriter.cpp - the write side of CvWorldBuilderMap (.Civ5Map), ticket 0326.
//
// Written from the knowledge base's description of the 32-bit library's writer
// (research/03-dll-engine-boundary/civ5map-format.md section 8, with the record and mod data layouts of sections 3
// and 5; decision D83: nothing of the closed library is copied). It replaces the last logging stub of the group:
// CvWorldBuilderMap::Save, and adds the serializers it uses (CvWorldBuilderMapTypeDesc::TypeMap,
// CvWorldBuilderMapTypeGroup, the mod data classes).
//
// Save always writes version 12. The file holds the 32-bit library's in-memory records byte for byte: the two
// records a 64-bit compiler lays out differently and that the writer emits (Player 440 -> 436 bytes, Unit 88 -> 84)
// are converted back to the file layout; every other record is written as it is. The file is written through Win32
// directly (the server's FIFileSystem opens only files inside its run folder; the 32-bit library writes through the
// engine's FIFileSystem). The same source compiles for x86 (the re-save A/B against the 32-bit library,
// tools/x64-dll/civ5map/) and x64.
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "../FireWorks/fw_prelude.h"
#include <bitset>
#include <FireWorks/FFastVector.h>
#include <FireWorks/FString.h>
#ifndef MAX
#define MAX(a, b) std::max(a, b)              // as in CvGameCoreDLLPCH.h
#endif
#define CV_WORLD_BUILDER_MAP_IN_GAME          // as in CvWorldBuilderMapLoader.cpp: same class layout
#include "CvWorldBuilderMap.h"
#include "CvWorldBuilderMapTypeDesc.h"
#include <stddef.h>
#include <string.h>
#include <vector>

typedef CvWorldBuilderMap WBM;

namespace
{
// Record sizes in the file (the 32-bit layouts; civ5map-format.md section 3).
const uint kOutPlayer = 436;
const uint kOutUnit = 84;
const uint kOutUnitName = 64;
const uint kOutCity = 136;
const uint kOutTeam = 64;
const uint kOutPlot = 8;
const uint kOutWorldType = 64;

#define WBW_CHECK(name, cond) typedef char name[(cond) ? 1 : -1]
WBW_CHECK(kWCheckCity, sizeof(WBM::City) == kOutCity);
WBW_CHECK(kWCheckTeam, sizeof(WBM::Team) == kOutTeam);
WBW_CHECK(kWCheckUnitName, sizeof(WBM::UnitName) == kOutUnitName);
WBW_CHECK(kWCheckPlot, sizeof(WBM::PlotMapData) == kOutPlot && sizeof(WBM::PlotScenarioData) == kOutPlot);
WBW_CHECK(kWCheckPlayer, sizeof(WBM::Player) >= kOutPlayer && offsetof(WBM::Player, m_bPlayable) == 433);
WBW_CHECK(kWCheckUnit, sizeof(WBM::Unit) >= kOutUnit && offsetof(WBM::Unit, m_byFlags) == 18);

void PutLE32(byte* p, uint v)
{
	p[0] = (byte)v;
	p[1] = (byte)(v >> 8);
	p[2] = (byte)(v >> 16);
	p[3] = (byte)(v >> 24);
}

// The output file: Win32, created or truncated; Write answers the bytes written (as FIFile::Write).
class WbOutFile
{
public:
	WbOutFile() : m_h(INVALID_HANDLE_VALUE) {}
	~WbOutFile() { Close(); }
	bool Open(const wchar_t* wsz)
	{
		if(wsz == NULL || *wsz == 0)
			return false;
		m_h = CreateFileW(wsz, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		return m_h != INVALID_HANDLE_VALUE;
	}
	uint Write(const void* p, uint n)
	{
		const byte* c = static_cast<const byte*>(p);
		uint done = 0;
		while(done < n)
		{
			DWORD put = 0;
			if(!WriteFile(m_h, c + done, n - done, &put, NULL) || put == 0)
				break;
			done += put;
		}
		return done;
	}
	void Close()
	{
		if(m_h != INVALID_HANDLE_VALUE)
			CloseHandle(m_h);
		m_h = INVALID_HANDLE_VALUE;
	}

private:
	HANDLE m_h;
};

// An FString's text and the length the writer gives it (length + 1, the terminating NUL included).
uint StringBytes(const FString& s)
{
	return (uint)s.GetLength() + 1;
}

void PutString(byte* p, const FString& s)
{
	const uint n = StringBytes(s);
	const char* sz = s.c_str();
	if(sz)
		memcpy(p, sz, n);
	p[n - 1] = 0;
}

// The allocator as the file holds it: [i32 free-list head][entries in the 32-bit layout]; 0 bytes when empty. The
// allocator's own Serialize gives this architecture's entries; each one (used or free) is cut to the file layout.
template<class TAlloc, class T>
std::vector<byte> AllocatorBytes(const TAlloc& kAlloc, uint uiOutEntry)
{
	std::vector<byte> out;
	const uint uiSize = kAlloc.GetSerializedSize();
	if(uiSize == 0)
		return out;
	std::vector<byte> mem(uiSize);
	kAlloc.Serialize(&mem[0]);
	const uint n = (uiSize - 4) / (uint)sizeof(T);
	out.resize(4 + (size_t)n * uiOutEntry);
	memcpy(&out[0], &mem[0], 4);
	for(uint i = 0; i < n; ++i)
	{
		const byte* src = &mem[4 + (size_t)i * sizeof(T)];
		byte* dst = &out[4 + (size_t)i * uiOutEntry];
		if(sizeof(T) == uiOutEntry)
			memcpy(dst, src, uiOutEntry);
		else
		{
			// Unit: the fields before the promotions sit at the same offsets on both architectures; the promotions
			// are at 20 in the file and at offsetof(Unit, m_kPromotions) here.
			memcpy(dst, src, 20);
			memcpy(dst + 20, src + offsetof(WBM::Unit, m_kPromotions), uiOutEntry - 20);
		}
	}
	return out;
}

uint AllocatorFileSize(uint uiEntries, uint uiOutEntry)
{
	return uiEntries == 0 ? 0 : 4 + uiEntries * uiOutEntry;
}

uint RelationBytes(uint uiTeams)
{
	const uint uiBits = ((uiTeams - 1) * uiTeams) / 2;
	return uiBits / 8 + ((uiBits % 8) ? 1 : 0);
}
} // namespace

// =================================================================================================================
// Serializers (the inverse of the reader's Deserialize functions)

// Each name followed by one NUL; a missing name is an empty one.
void CvWorldBuilderMapTypeDesc::TypeMap::Serialize(void* pvBuffer) const
{
	char* p = static_cast<char*>(pvBuffer);
	for(uint i = 0; i < asTypes.size(); ++i)
	{
		const char* sz = asTypes[i];
		const size_t n = sz ? strlen(sz) : 0;
		if(n)
			memcpy(p, sz, n);
		p[n] = 0;
		p += n + 1;
	}
}

void CvWorldBuilderMapTypeGroup::Serialize(void* p) const
{
	if(m_uiBufferSize)
		memcpy(p, m_szBuffer, m_uiBufferSize);
}

// [u32 size][size bytes]; the bytes only when the buffer exists.
void ModDataStringBuffer::Serialize(void* pv) const
{
	byte* p = static_cast<byte*>(pv);
	PutLE32(p, m_uiSize);
	if(m_szBuffer)
		memcpy(p + 4, m_szBuffer, m_uiSize);
}

// [u32 n][n x u32 member string id]
void ModEnum::Serialize(void* pv) const
{
	byte* p = static_cast<byte*>(pv);
	const uint n = m_kMembers.size();
	PutLE32(p, n);
	if(n)
		memcpy(p + 4, &m_kMembers[0], 4 * (size_t)n);
}

// [u32 fields][u32 enums][fields x {u32 type, u32 name id, u32 default}][enums x ModEnum]
void ModType::Serialize(void* pv) const
{
	byte* p = static_cast<byte*>(pv);
	const uint nFields = m_kFields.size();
	const uint nEnums = m_kEnums.size();
	PutLE32(p, nFields);
	PutLE32(p + 4, nEnums);
	p += 8;
	if(nFields)
	{
		memcpy(p, &m_kFields[0], 12 * (size_t)nFields);
		p += 12 * (size_t)nFields;
	}
	for(uint i = 0; i < nEnums; ++i)
	{
		m_kEnums[i].Serialize(p);
		p += m_kEnums[i].GetSerializedSize();
	}
}

// [map ModType][plot ModType][u32 n][n x u32 values][string buffer]
void CvWorldBuilderMapModData::Serialize(void* pv) const
{
	byte* p = static_cast<byte*>(pv);
	m_kMapModData.Serialize(p);
	p += m_kMapModData.GetSerializedSize();
	m_kPlotModData.Serialize(p);
	p += m_kPlotModData.GetSerializedSize();
	const uint n = m_kData.size();
	PutLE32(p, n);
	p += 4;
	if(n)
	{
		memcpy(p, &m_kData[0], 4 * (size_t)n);
		p += 4 * (size_t)n;
	}
	m_kStringBuffer.Serialize(p);
}

// =================================================================================================================
// CvWorldBuilderMap::Save (civ5map-format.md section 8)

bool CvWorldBuilderMap::Save(const wchar_t* wszFilename, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario) const
{
	static_assert(sizeof(m_szWorldType) == kOutWorldType, "world type buffer of 64 bytes");
	WbOutFile kFile;
	if(!kFile.Open(wszFilename))
		return false;
	bool bOk = false;
	do
	{
		const uint uiPlots = m_kMapDesc.uiWidth * m_kMapDesc.uiHeight;

		// 1. Version, size, playable civilisations.
		byte abyHead[10];
		abyHead[0] = bScenario ? 0x8C : 0x0C;
		PutLE32(abyHead + 1, m_kMapDesc.uiWidth);
		PutLE32(abyHead + 5, m_kMapDesc.uiHeight);
		byte byPlayable = 0;
		if(bScenario)
			for(uint i = 0; i < m_kGameDesc.byPlayerCount; ++i)
				if(m_akPlayers[i].m_bPlayable)
					++byPlayable;
		abyHead[9] = byPlayable;
		if(kFile.Write(abyHead, sizeof(abyHead)) < sizeof(abyHead))
			break;

		// 2. Map header: flags and the seven section lengths.
		const bool bModData = m_kModData.m_kMapModData.GetFieldCount() != 0 || m_kModData.m_kPlotModData.GetFieldCount() != 0;
		uint auiLen[7];
		auiLen[0] = (uint)kTypeDesc.m_kTerrainTypes.GetSerializedSize();
		auiLen[1] = (uint)kTypeDesc.m_kFeatures.GetSerializedSize();
		auiLen[2] = (uint)kTypeDesc.m_kNaturalWonders.GetSerializedSize();
		auiLen[3] = (uint)kTypeDesc.m_kResources.GetSerializedSize();
		auiLen[4] = bModData ? (uint)m_kModData.GetSerializedSize() : 0;
		auiLen[5] = StringBytes(m_sMapName);
		auiLen[6] = StringBytes(m_sMapDescription);
		byte abyMapHeader[32];
		PutLE32(abyMapHeader, m_uiFlags);
		uint uiMapBlock = 0;
		for(int i = 0; i < 7; ++i)
		{
			PutLE32(abyMapHeader + 4 + 4 * i, auiLen[i]);
			uiMapBlock += auiLen[i];
		}
		if(kFile.Write(abyMapHeader, sizeof(abyMapHeader)) < sizeof(abyMapHeader))
			break;

		// 3. The map sections in one block.
		{
			std::vector<byte> kBlock(uiMapBlock);
			byte* p = &kBlock[0];
			kTypeDesc.m_kTerrainTypes.Serialize(p);
			p += auiLen[0];
			kTypeDesc.m_kFeatures.Serialize(p);
			p += auiLen[1];
			kTypeDesc.m_kNaturalWonders.Serialize(p);
			p += auiLen[2];
			kTypeDesc.m_kResources.Serialize(p);
			p += auiLen[3];
			if(auiLen[4])
			{
				m_kModData.Serialize(p);
				p += auiLen[4];
			}
			PutString(p, m_sMapName);
			p += auiLen[5];
			PutString(p, m_sMapDescription);
			if(kFile.Write(&kBlock[0], uiMapBlock) < uiMapBlock)
				break;
		}

		// 4. World type (unchecked writes), then the plot records.
		byte abyWorldLen[4];
		PutLE32(abyWorldLen, kOutWorldType);
		kFile.Write(abyWorldLen, 4);
		kFile.Write(m_szWorldType, kOutWorldType);
		if(kFile.Write(m_akMapData, uiPlots * kOutPlot) < uiPlots * kOutPlot)
			break;

		if(bScenario)
		{
			// 5. Scenario header: the game description and the eleven section lengths.
			const std::vector<byte> kUnits = AllocatorBytes<Unit::Map, Unit>(m_kUnits, kOutUnit);
			const std::vector<byte> kNames = AllocatorBytes<UnitName::Map, UnitName>(m_kUnitNames, kOutUnitName);
			const std::vector<byte> kCities = AllocatorBytes<City::Map, City>(m_kCities, kOutCity);
			uint auiS[11];
			auiS[0] = (uint)kTypeDesc.m_kImprovements.GetSerializedSize();
			auiS[1] = (uint)kTypeDesc.m_kUnits.GetSerializedSize();
			auiS[2] = (uint)kTypeDesc.m_kTechs.GetSerializedSize();
			auiS[3] = (uint)kTypeDesc.m_kPolicies.GetSerializedSize();
			auiS[4] = (uint)kTypeDesc.m_kBuildingTypes.GetSerializedSize();
			auiS[5] = (uint)kTypeDesc.m_kUnitPromotions.GetSerializedSize();
			auiS[6] = (uint)kUnits.size();
			auiS[7] = (uint)kNames.size();
			auiS[8] = (uint)kCities.size();
			auiS[9] = m_kVictoryTypes.GetSerializedSize();
			auiS[10] = m_kGameOptions.GetSerializedSize();
			if(auiS[6] == 0)
			{
				auiS[1] = 0;	// no units: no unit-type list
				auiS[5] = 0;	// and no promotion list
			}
			byte abyScen[128];
			memcpy(abyScen, &m_kGameDesc, 84);
			for(int i = 0; i < 11; ++i)
				PutLE32(abyScen + 84 + 4 * i, auiS[i]);
			if(kFile.Write(abyScen, sizeof(abyScen)) < sizeof(abyScen))
				break;

			// 6. The scenario block.
			const uint uiTeams = m_kGameDesc.byTeamCount;
			const uint uiRel = RelationBytes(uiTeams);
			const uint uiVisBits = uiTeams * uiPlots;
			const uint uiVis = uiVisBits / 8 + ((uiVisBits % 8) ? 1 : 0);
			const uint uiInfluence = (uint)m_kGameDesc.byPlayerCount * 256;
			uint uiScenBlock = 5 * uiRel + uiInfluence + uiVis;
			for(int i = 0; i < 11; ++i)
				uiScenBlock += auiS[i];
			std::vector<byte> kBlock((size_t)uiScenBlock + 1);
			byte* q = &kBlock[0];
			kTypeDesc.m_kImprovements.Serialize(q);
			q += auiS[0];
			if(auiS[1])
			{
				kTypeDesc.m_kUnits.Serialize(q);
				q += auiS[1];
			}
			kTypeDesc.m_kTechs.Serialize(q);
			q += auiS[2];
			kTypeDesc.m_kPolicies.Serialize(q);
			q += auiS[3];
			kTypeDesc.m_kBuildingTypes.Serialize(q);
			q += auiS[4];
			if(auiS[5])
			{
				kTypeDesc.m_kUnitPromotions.Serialize(q);
				q += auiS[5];
			}
			if(auiS[6]) memcpy(q, &kUnits[0], auiS[6]);
			q += auiS[6];
			if(auiS[7]) memcpy(q, &kNames[0], auiS[7]);
			q += auiS[7];
			if(auiS[8]) memcpy(q, &kCities[0], auiS[8]);
			q += auiS[8];
			m_kVictoryTypes.Serialize(q);
			q += auiS[9];
			m_kGameOptions.Serialize(q);
			q += auiS[10];
			m_kTeamsInContact.Serialize(q, uiTeams);
			q += uiRel;
			m_kTeamsAtWar.Serialize(q, uiTeams);
			q += uiRel;
			m_kTeamsAtPermanentWarOrPeace.Serialize(q, uiTeams);
			q += uiRel;
			m_kTeamsSharingOpenBorders.Serialize(q, uiTeams);
			q += uiRel;
			m_kTeamsSharingDefensivePacts.Serialize(q, uiTeams);
			q += uiRel;
			memcpy(q, m_aiCityStateInfluence, uiInfluence);
			q += uiInfluence;
			m_kVisibility.Serialize(q, uiVisBits);
			if(kFile.Write(&kBlock[0], uiScenBlock) < uiScenBlock)
				break;

			// 7. Teams, players, city states, scenario plot records.
			const uint uiTeamBytes = uiTeams * kOutTeam;
			if(kFile.Write(m_akTeams, uiTeamBytes) < uiTeamBytes)
				break;
			bool bPlayersOk = true;
			for(int iTable = 0; iTable < 2 && bPlayersOk; ++iTable)
			{
				const Player* akTable = iTable == 0 ? m_akPlayers : m_akCityStates;
				const uint n = iTable == 0 ? m_kGameDesc.byPlayerCount : m_kGameDesc.byCityStateCount;
				if(sizeof(Player) == kOutPlayer)
					bPlayersOk = kFile.Write(akTable, n * kOutPlayer) >= n * kOutPlayer;
				else
				{
					// One write per table, as the 32-bit library; the records cut to their first 436 bytes.
					std::vector<byte> kRecords((size_t)n * kOutPlayer + 1);
					for(uint i = 0; i < n; ++i)
						memcpy(&kRecords[(size_t)i * kOutPlayer], &akTable[i], kOutPlayer);
					bPlayersOk = kFile.Write(&kRecords[0], n * kOutPlayer) >= n * kOutPlayer;
				}
			}
			if(!bPlayersOk)
				break;
			if(kFile.Write(m_akScenarioData, uiPlots * kOutPlot) < uiPlots * kOutPlot)
				break;
		}
		bOk = true;
	} while(false);

	kFile.Close();
	if(!bOk)
		DeleteFileW(wszFilename);
	return bOk;
}
