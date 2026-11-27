// CvWorldBuilderMapReader.cpp - the read side of CvWorldBuilderMap (.Civ5Map), ticket 0343.
//
// Written from the knowledge base's description of the format and of the 32-bit library's behaviour
// (research/03-dll-engine-boundary/civ5map-format.md; decision D83: nothing of the closed library is copied). It
// replaces the logging stubs of the read side: Load, PreviewPlayableCivCount, ClearScenarioData, Resize,
// SetWorldType, SetDefaultGameSpeed, SwapPlayers, SwapCityStates, the type-description string table
// (CvWorldBuilderMapTypeDesc::TypeMap, ::String) and the mod data readers (ModType, ModEnum, ModDataEntry,
// CvWorldBuilderMapModData). The writer (Save) is CvWorldBuilderMapWriter.cpp (ticket 0326).
//
// The file holds the 32-bit library's in-memory records byte for byte. Three of them are laid out differently by
// a 64-bit compiler (std::bitset aligns to 8 there): Player (436 bytes in the file, 440 on x64), Unit (84, 88) and
// OldCity (100, 104). They are converted field-preserving when read; every other record has the same layout on
// both architectures and is copied as it is. The file is read through Win32 directly (the server's FIFileSystem
// opens only files inside its run folder); the 32-bit library reads it through the engine's FIFileSystem.
// The same source compiles for x86 (the A/B test against the 32-bit library, tools/x64-dll/civ5map/) and x64.
// FFastVector.h (included by the prelude) uses MIN in templates: define it before.
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "../FireWorks/fw_prelude.h"
#include <bitset>
#include <FireWorks/FFastVector.h>
#include <FireWorks/FString.h>
#include <FireWorks/FIFile.h>
#ifndef MAX
#define MAX(a, b) std::max(a, b)              // as in CvGameCoreDLLPCH.h
#endif
#define CV_WORLD_BUILDER_MAP_IN_GAME          // as in CvWorldBuilderMapLoader.cpp: same class layout
#include "CvWorldBuilderMap.h"
#include "CvWorldBuilderMapTypeDesc.h"
#include <stddef.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>

typedef CvWorldBuilderMap WBM;

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// Record sizes in the file (the 32-bit layouts).
const uint kFilePlayer = 436;
const uint kFileOldPlayer = 304;
const uint kFileUnit = 84;
const uint kFileUnitV2 = 48;
const uint kFileOldUnit = 12;
const uint kFileCity = 136;
const uint kFileCityV2 = 104;
const uint kFileOldCity = 100;
const uint kFileTeam = 64;
const uint kFileUnitName = 64;
const uint kFilePlot = 8;

// Layout checks: these records have the file's layout on every architecture (copied as they are); the other three
// (Player, Unit, OldCity) are at least as large and converted.
#define WB_CHECK(name, cond) typedef char name[(cond) ? 1 : -1]
WB_CHECK(kCheckOldPlayer, sizeof(WBM::OldPlayer) == kFileOldPlayer);
WB_CHECK(kCheckCity, sizeof(WBM::City) == kFileCity);
WB_CHECK(kCheckCityV2, sizeof(WBM::CityV2) == kFileCityV2);
WB_CHECK(kCheckUnitV2, sizeof(WBM::UnitV2) == kFileUnitV2);
WB_CHECK(kCheckOldUnit, sizeof(WBM::OldUnit) == kFileOldUnit);
WB_CHECK(kCheckTeam, sizeof(WBM::Team) == kFileTeam);
WB_CHECK(kCheckUnitName, sizeof(WBM::UnitName) == kFileUnitName);
WB_CHECK(kCheckPlot, sizeof(WBM::PlotMapData) == kFilePlot && sizeof(WBM::PlotScenarioData) == kFilePlot);
WB_CHECK(kCheckPlayer, sizeof(WBM::Player) >= kFilePlayer && offsetof(WBM::Player, m_bPlayable) == 433);
WB_CHECK(kCheckUnit, sizeof(WBM::Unit) >= kFileUnit && offsetof(WBM::Unit, m_byFlags) == 18);
WB_CHECK(kCheckOldCity, sizeof(WBM::OldCity) >= kFileOldCity && offsetof(WBM::OldCity, m_bLocalizedName) == 65);

uint LE32(const byte* p)
{
	return (uint)p[0] | ((uint)p[1] << 8) | ((uint)p[2] << 16) | ((uint)p[3] << 24);
}

// ---------------------------------------------------------------------------------------------------------------
// The file: read whole into memory, then served through FIFile::Read with the same short-read semantics.
class WbMemFile : public FIFile
{
public:
	WbMemFile() : m_uiPos(0), m_bOpen(false) {}
	~WbMemFile() {}
	bool OpenPath(const wchar_t* wsz)
	{
		m_bOpen = false;
		m_kData.clear();
		m_uiPos = 0;
		if(wsz == NULL || *wsz == 0)
			return false;
		HANDLE h = CreateFileW(wsz, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(h == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER li;
		bool ok = GetFileSizeEx(h, &li) != 0 && li.QuadPart >= 0 && li.QuadPart < 0x40000000;
		if(ok)
		{
			m_kData.resize((size_t)li.QuadPart);
			DWORD got = 0;
			ok = m_kData.empty() || (ReadFile(h, &m_kData[0], (DWORD)m_kData.size(), &got, NULL) && got == m_kData.size());
		}
		CloseHandle(h);
		m_bOpen = ok;
		return ok;
	}
	FileErr Open(const char*, OpenMode, dword, uint) { return FILE_BADNAME; }
	FileErr Open(const wchar*, OpenMode, dword, uint) { return FILE_BADNAME; }
	FileErr Close() { m_bOpen = false; return FILE_OK; }
	const wchar_t* GetFileName() { return L""; }
	byte* GetViewCaret() const { return NULL; }
	byte* GetViewEnd() const { return NULL; }
	byte* GetViewPtr() const { return NULL; }
	uint GetLength() const { return (uint)m_kData.size(); }
	void SetLength(uint) {}
	bool IsOpen() const { return m_bOpen; }
	bool IsMapped() const { return false; }
	uint Seek(long l, SeekMode e)
	{
		long base = e == begin ? 0 : e == current ? (long)m_uiPos : (long)m_kData.size();
		long p = base + l;
		if(p < 0) p = 0;
		if(p > (long)m_kData.size()) p = (long)m_kData.size();
		m_uiPos = (uint)p;
		return m_uiPos;
	}
	uint SeekToBegin() { return Seek(0, begin); }
	uint SeekToEnd() { return Seek(0, end); }
	uint GetPosition() { return m_uiPos; }
	void Rewind() { m_uiPos = 0; }
	bool IsEOF() { return m_uiPos >= m_kData.size(); }
	uint Read(void* p, uint n)
	{
		if(!m_bOpen || p == NULL)
			return 0;
		const uint left = (uint)m_kData.size() - m_uiPos;
		const uint k = n < left ? n : left;
		if(k)
			memcpy(p, &m_kData[m_uiPos], k);
		m_uiPos += k;
		return k;
	}
	uint Write(const void*, uint) { return 0; }
	uint Flush() { return 0; }

private:
	std::vector<byte> m_kData;
	uint m_uiPos;
	bool m_bOpen;
};

// Reads n bytes into a buffer of `cap` bytes; bytes beyond `cap` are consumed and dropped (a sane file never has
// them: the counts in the file are below the array sizes). True if all n bytes were in the file.
bool ReadClamped(FIFile& f, void* dst, uint n, uint cap)
{
	const uint k = n < cap ? n : cap;
	if(f.Read(dst, k) < k)
		return false;
	uint rest = n - k;
	byte tmp[512];
	while(rest)
	{
		const uint c = rest < sizeof(tmp) ? rest : (uint)sizeof(tmp);
		if(f.Read(tmp, c) < c)
			return false;
		rest -= c;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The string palette of CvWorldBuilderMapTypeDesc::String: one interned copy per name, compared without case
// (the 32-bit library keeps a set ordered by _stricmp), so String equality is pointer equality.
struct NoCaseLess
{
	bool operator()(const std::string& a, const std::string& b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
};
std::set<std::string, NoCaseLess>& Palette()
{
	static std::set<std::string, NoCaseLess> s;
	return s;
}
CRITICAL_SECTION& PaletteLock()
{
	static CRITICAL_SECTION* p = NULL;
	if(!p)
	{
		CRITICAL_SECTION* c = new CRITICAL_SECTION;
		InitializeCriticalSection(c);
		p = c;
	}
	return *p;
}
struct PaletteLockInit
{
	PaletteLockInit() { PaletteLock(); }
} g_kPaletteLockInit;

const char* Intern(const char* sz)
{
	if(sz == NULL)
		return NULL;
	EnterCriticalSection(&PaletteLock());
	const char* p = Palette().insert(std::string(sz)).first->c_str();
	LeaveCriticalSection(&PaletteLock());
	return p;
}

// ---------------------------------------------------------------------------------------------------------------
// Record conversions (file layout -> this architecture's layout).
void UnitFromFile(const byte* src, void* dst)
{
	// Fields before the promotions are at the same offsets on both architectures; the promotions start at 20 in
	// the file and at offsetof(Unit, m_kPromotions) here.
	byte* d = static_cast<byte*>(dst);
	memset(d, 0, sizeof(WBM::Unit));
	memcpy(d, src, 20);
	memcpy(d + offsetof(WBM::Unit, m_kPromotions), src + 20, kFileUnit - 20);
}

void OldCityFromFile(const byte* src, void* dst)
{
	byte* d = static_cast<byte*>(dst);
	memset(d, 0, sizeof(WBM::OldCity));
	memcpy(d, src, 68);
	memcpy(d + offsetof(WBM::OldCity, m_kBuildings), src + 68, kFileOldCity - 68);
}

// out[lut[i]] = in[i] for i < min(fileCount, N) and lut[i] < N; every other bit of out is 0.
template<size_t N>
void RemapBits(std::bitset<N>& bits, const CvWorldBuilderMapTypeDesc::LookupTable& lut, uint fileCount)
{
	std::bitset<N> out;
	const uint n = fileCount < N ? fileCount : (uint)N;
	for(uint i = 0; i < n && i < lut.size(); ++i)
	{
		const uint j = lut[i];
		if(j < N)
			out[j] = bits[i];
	}
	bits = out;
}

// The allocator's x86 entry stream [i32 free list][entries], converted entry by entry to this architecture's
// layout and handed to the allocator's own Deserialize.
template<class TAlloc>
void DeserializeConverted(TAlloc& kAlloc, const byte* p, uint uiSize, uint uiFileEntry, uint uiEntry,
                          void (*pfnConvert)(const byte*, void*))
{
	if(p == NULL || uiSize == 0)
	{
		kAlloc.Deserialize(NULL, 0);
		return;
	}
	const uint n = (uiSize - 4) / uiFileEntry;
	std::vector<byte> tmp(4 + (size_t)n * uiEntry);
	memcpy(&tmp[0], p, 4);
	for(uint i = 0; i < n; ++i)
		pfnConvert(p + 4 + (size_t)i * uiFileEntry, &tmp[4 + (size_t)i * uiEntry]);
	kAlloc.Deserialize(&tmp[0], (uint)tmp.size());
}

// A legacy entry stream converted to the current record type: entries on the free list (walked from its head while
// the index is above 0, as the 32-bit library does) keep only their next-free index; every other entry is built
// from the legacy record by the conversion constructor.
template<class TOld, class TNew, class TAlloc>
void DeserializeLegacy(TAlloc& kAlloc, const byte* p, uint uiSize, uint uiFileEntry,
                       void (*pfnOld)(const byte*, TOld&))
{
	if(p == NULL || uiSize == 0)
	{
		kAlloc.Deserialize(NULL, 0);
		return;
	}
	const uint n = (uiSize - 4) / uiFileEntry;
	if(n == 0)
	{
		// The 32-bit library allocates nothing for an empty legacy allocator: the target stays empty.
		kAlloc.Deserialize(NULL, 0);
		return;
	}
	int iFreeHead;
	memcpy(&iFreeHead, p, 4);
	std::vector<bool> abFree(n, false);
	for(int iFree = iFreeHead; iFree > 0 && iFree < (int)n;)
	{
		abFree[iFree] = true;
		int iNext;
		memcpy(&iNext, p + 4 + (size_t)iFree * uiFileEntry, 4);
		iFree = iNext;
	}
	std::vector<byte> tmp(4 + (size_t)n * sizeof(TNew));
	memcpy(&tmp[0], &iFreeHead, 4);
	for(uint i = 0; i < n; ++i)
	{
		const byte* src = p + 4 + (size_t)i * uiFileEntry;
		byte* dst = &tmp[4 + (size_t)i * sizeof(TNew)];
		if(abFree[i])
		{
			memcpy(dst, src, 4);
		}
		else
		{
			TOld kOld;
			pfnOld(src, kOld);
			new(dst) TNew(kOld);
		}
	}
	kAlloc.Deserialize(&tmp[0], (uint)tmp.size());
}

void UnitV2FromFile(const byte* src, WBM::UnitV2& u) { memcpy(&u, src, kFileUnitV2); }
void OldUnitFromFile(const byte* src, WBM::OldUnit& u) { memcpy(&u, src, kFileOldUnit); }
void CityV2FromFile(const byte* src, WBM::CityV2& c) { memcpy(&c, src, kFileCityV2); }
void OldCityRecordFromFile(const byte* src, WBM::OldCity& c) { OldCityFromFile(src, &c); }

// strcpy_s(dst, 64, src) of the 32-bit library: an unterminated source leaves dst empty.
void CopyName64(char* dst, const char* src)
{
	size_t n = 0;
	while(n < 64 && src[n])
		++n;
	if(n >= 64)
	{
		dst[0] = 0;
		return;
	}
	memcpy(dst, src, n + 1);
}
} // namespace

// =================================================================================================================
// Construction-time conversions of legacy records.

// The legacy records are only targets of a byte copy from the file.
WBM::OldUnit::OldUnit() {}
WBM::UnitV2::UnitV2() {}
WBM::OldCity::OldCity() {}
WBM::CityV2::CityV2() {}
WBM::OldPlayer::OldPlayer() {}

WBM::Unit::Unit()
{
	m_hStackedUnit = Handle();
	m_hCustomName = UnitName::Handle();
	m_uiExperience = 0;
	m_uiHealth = MaxHealth;
	m_byUnitType = InvalidUnitType;
	m_byOwner = 0xFF;
	m_byDirection = DIRECTION_RANDOM;
	m_byFlags = 0;
}

WBM::Unit::Unit(const UnitV2& k)
{
	// The handle words: the stacked unit is kept, the custom name becomes invalid.
	m_hStackedUnit = Handle();
	m_hCustomName = UnitName::Handle();
	memcpy(&m_hStackedUnit, &k.m_hStackedUnit, sizeof(word));
	m_uiExperience = k.m_uiExperience;
	m_uiHealth = k.m_uiHealth;
	m_byUnitType = k.m_byUnitType;	// byte, zero-extended
	m_byOwner = k.m_byOwner;
	m_byDirection = k.m_byDirection;
	m_byFlags = k.m_byFlags;
	m_kPromotions.reset();
	for(uint i = 0; i < UnitV2::MaxPromotionTypes; ++i)
		m_kPromotions[i] = k.m_kPromotions[i];
}

WBM::Unit::Unit(const OldUnit& k)
{
	m_hStackedUnit = Handle();
	m_hCustomName = UnitName::Handle();
	m_uiExperience = k.m_wExperience;
	m_uiHealth = MaxHealth;
	m_byUnitType = k.m_byUnitType;
	m_byOwner = k.m_byOwner;
	m_byDirection = k.m_byDirection;
	m_byFlags = 0;
	m_kPromotions.reset();
	memcpy(&m_hStackedUnit, &k.m_hStackedUnit, sizeof(word));
	m_uiHealth = (uint)(int)(k.m_fHealth * (float)MaxHealth);
	SetFlag(UNIT_FORTIFIED, k.m_bFortified);
	SetFlag(UNIT_EMBARKED, k.m_bEmbarked);
}

WBM::City::City()
{
	m_byOwner = 0xFF;
	m_byFlags = 0;
	m_wPopulation = 1;
	m_uiHealth = MaxHealth;
	m_kBuildings.reset();
	m_szName[0] = 0;
}

WBM::City::City(const CityV2& k)
{
	m_byOwner = k.m_byOwner;
	m_byFlags = k.m_byFlags;
	m_wPopulation = k.m_wPopulation;
	m_uiHealth = k.m_uiHealth;
	m_kBuildings.reset();
	CopyName64(m_szName, k.m_szName);
	for(uint i = 0; i < CityV2::MaxBuildingTypes; ++i)
		m_kBuildings[i] = k.m_kBuildings[i];
}

WBM::City::City(const OldCity& k)
{
	m_byOwner = k.m_byOwner;
	m_byFlags = 0;
	m_wPopulation = (word)k.m_uiPopulation;
	m_uiHealth = MaxHealth;
	m_kBuildings.reset();
	SetFlag(CITY_LOCALIZED_NAME, k.m_bLocalizedName);
	CopyName64(m_szName, k.m_szName);
}

// The policies are not carried over (the 32-bit library zeroes them); the names are copied as C strings.
WBM::Player::Player(const OldPlayer& k)
	: m_kPolicies()
	, m_uiCulture(k.m_uiCulture)
	, m_uiGold(k.m_uiGold)
	, m_uiStartX(k.m_uiStartX)
	, m_uiStartY(k.m_uiStartY)
	, m_byTeam(k.m_byTeam)
	, m_bPlayable(k.m_bPlayable)
{
	m_szLeaderName[0] = 0;
	m_szCivName[0] = 0;
	CopyName64(m_szCivType, k.m_szCivType);
	CopyName64(m_szTeamColor, k.m_szTeamColor);
	CopyName64(m_szEra, k.m_szEra);
	char szHandicap[sizeof(k.m_szHandicap) + 1];
	memcpy(szHandicap, k.m_szHandicap, sizeof(k.m_szHandicap));
	szHandicap[sizeof(k.m_szHandicap)] = 0;
	CopyName64(m_szHandicap, szHandicap);
}

// =================================================================================================================
// CvWorldBuilderMapTypeDesc

CvWorldBuilderMapTypeDesc::String::String(const char* sz)
	: m_sz(Intern(sz))
{
}

const char* CvWorldBuilderMapTypeDesc::String::operator=(const char* sz)
{
	m_sz = Intern(sz);
	return m_sz;
}

uint CvWorldBuilderMapTypeDesc::TypeMap::Add(const char* szType)
{
	asTypes.push_back(String(szType));
	return (uint)asTypes.size() - 1;
}

uint CvWorldBuilderMapTypeDesc::TypeMap::Add(const String& sType)
{
	asTypes.push_back(sType);
	return (uint)asTypes.size() - 1;
}

const CvWorldBuilderMapTypeDesc::String& CvWorldBuilderMapTypeDesc::TypeMap::Get(uint uiIndex) const
{
	static const String kEmpty;
	if(uiIndex < asTypes.size())
		return asTypes[uiIndex];
	return kEmpty;
}

bool CvWorldBuilderMapTypeDesc::TypeMap::FindType(const char* szType, uint& uiTypeOut) const
{
	const char* sz = Intern(szType);
	for(uint i = 0; i < asTypes.size(); ++i)
	{
		if((const char*)asTypes[i] == sz)
		{
			uiTypeOut = i;
			return true;
		}
	}
	return false;
}

const size_t CvWorldBuilderMapTypeDesc::TypeMap::GetSerializedSize() const
{
	size_t n = 0;
	for(uint i = 0; i < asTypes.size(); ++i)
	{
		const char* sz = asTypes[i];
		n += (sz ? strlen(sz) : 0) + 1;
	}
	return n;
}

// The buffer is a run of NUL-terminated names; the last byte of the buffer is forced to NUL first (in place, as
// the 32-bit library does).
void CvWorldBuilderMapTypeDesc::TypeMap::Deserialize(void* pvBuffer, size_t stSize)
{
	asTypes.clear();
	if(pvBuffer == NULL || stSize == 0)
		return;
	char* p = static_cast<char*>(pvBuffer);
	char* pEnd = p + stSize;
	pEnd[-1] = 0;
	while(p < pEnd)
	{
		Add(p);
		p += strlen(p) + 1;
	}
}

// =================================================================================================================
// CvWorldBuilderMapTypeGroup (the buffer is FMALLOC'd; the constructor and destructor are in the stubs file)

void CvWorldBuilderMapTypeGroup::Clear()
{
	if(m_szBuffer)
	{
		FFREE(m_szBuffer);
		m_szBuffer = NULL;
		m_uiBufferSize = 0;
	}
}

void CvWorldBuilderMapTypeGroup::Deserialize(const void* p, uint uiSize)
{
	Clear();
	if(uiSize == 0)
		return;
	m_szBuffer = static_cast<char*>(FMALLOC(uiSize, c_eMPoolTypeGame, 0));
	m_uiBufferSize = uiSize;
	if(m_szBuffer)
		memcpy(m_szBuffer, p, uiSize);
}

// =================================================================================================================
// Mod data

void ModDataStringBuffer::Clear()
{
	m_uiSize = 0;
	m_uiReservedSize = 0;
	delete[] m_szBuffer;
	m_szBuffer = NULL;
}

const char* ModDataStringBuffer::GetString(uint uiStringID) const
{
	if(uiStringID < m_uiSize)
		return m_szBuffer + uiStringID;
	return "";
}

size_t ModDataStringBuffer::GetSerializedSize() const
{
	return 4 + m_uiSize;
}

void ModDataStringBuffer::Deserialize(const void* pv)
{
	const byte* p = static_cast<const byte*>(pv);
	const uint n = LE32(p);
	m_uiSize = n;
	if(m_szBuffer == NULL)
	{
		m_uiReservedSize = n;
		m_szBuffer = new char[n ? n : 1];
	}
	else if(n > m_uiReservedSize)
	{
		char* pNew = new char[n];
		delete[] m_szBuffer;
		m_szBuffer = pNew;
		m_uiReservedSize = n;
	}
	if(n)
		memcpy(m_szBuffer, p + 4, n);
}

ModEnum::ModEnum()
	: m_pkStringBuffer(NULL)
{
}

ModEnum::ModEnum(ModDataStringBuffer& kStringBuffer)
	: m_pkStringBuffer(&kStringBuffer)
{
}

ModEnum::ModEnum(const ModEnum& kRhs)
	: m_pkStringBuffer(kRhs.m_pkStringBuffer)
{
	for(uint i = 0; i < kRhs.m_kMembers.size(); ++i)
		m_kMembers.push_back(kRhs.m_kMembers[i]);
}

uint ModEnum::GetMemberCount() const
{
	return m_kMembers.size();
}

const char* ModEnum::GetMember(uint uiMember) const
{
	if(uiMember >= m_kMembers.size() || m_pkStringBuffer == NULL)
		return NULL;
	return m_pkStringBuffer->GetString(m_kMembers[uiMember]);
}

size_t ModEnum::GetSerializedSize() const
{
	return 4 + 4 * (size_t)m_kMembers.size();
}

void ModEnum::Deserialize(const void* pv)
{
	const byte* p = static_cast<const byte*>(pv);
	const uint n = LE32(p);
	m_kMembers.resize(n);
	if(n)
		memcpy(&m_kMembers[0], p + 4, 4 * (size_t)n);
}

void ModType::Clear()
{
	m_kFields.clear();
	m_kEnums.clear();
}

uint ModType::GetFieldCount() const
{
	return m_kFields.size();
}

const char* ModType::GetFieldName(uint uiField) const
{
	if(uiField >= m_kFields.size())
		return NULL;
	return m_kStringBuffer.GetString(m_kFields[uiField].dwFieldName);
}

ModType::FieldType ModType::GetFieldType(uint uiField) const
{
	if(uiField >= m_kFields.size())
		return TYPE_INVALID;
	const dword t = m_kFields[uiField].dwFieldType;
	return (FieldType)(t > (dword)TYPE_ENUM ? (dword)TYPE_ENUM : t);
}

bool ModType::GetFieldDefaultAsBool(uint uiField) const
{
	return uiField < m_kFields.size() && m_kFields[uiField].dwDefault != 0;
}

int ModType::GetFieldDefaultAsInt(uint uiField) const
{
	return uiField < m_kFields.size() ? (int)m_kFields[uiField].dwDefault : 0;
}

float ModType::GetFieldDefaultAsFloat(uint uiField) const
{
	if(uiField >= m_kFields.size())
		return 0.0f;
	float f;
	memcpy(&f, &m_kFields[uiField].dwDefault, sizeof(f));
	return f;
}

const char* ModType::GetFieldDefaultAsString(uint uiField) const
{
	if(uiField >= m_kFields.size())
		return "";
	return m_kStringBuffer.GetString(m_kFields[uiField].dwDefault);
}

const ModEnum* ModType::GetFieldEnumType(uint uiField) const
{
	if(uiField >= m_kFields.size())
		return NULL;
	const dword t = m_kFields[uiField].dwFieldType;
	if(t < (dword)TYPE_ENUM || t - (dword)TYPE_ENUM >= m_kEnums.size())
		return NULL;
	return &m_kEnums[t - (dword)TYPE_ENUM];
}

size_t ModType::GetSerializedSize() const
{
	size_t n = 8 + 12 * (size_t)m_kFields.size();
	for(uint i = 0; i < m_kEnums.size(); ++i)
		n += m_kEnums[i].GetSerializedSize();
	return n;
}

// [u32 fields][u32 enums][fields x {u32 type, u32 name id, u32 default}][enums x {u32 n, n x u32 member id}]
void ModType::Deserialize(const void* pv)
{
	const byte* p = static_cast<const byte*>(pv);
	const uint nFields = LE32(p);
	const uint nEnums = LE32(p + 4);
	m_kFields.resize(nFields);
	m_kEnums.clear();
	const ModEnum kProto(m_kStringBuffer);
	for(uint i = 0; i < nEnums; ++i)
		m_kEnums.push_back(kProto);
	p += 8;
	if(nFields)
	{
		memcpy(&m_kFields[0], p, 12 * (size_t)nFields);
		p += 12 * (size_t)nFields;
	}
	for(uint i = 0; i < nEnums; ++i)
	{
		m_kEnums[i].Deserialize(p);
		p += m_kEnums[i].GetSerializedSize();
	}
}

ModDataEntry CvWorldBuilderMapModData::GetMapDataEntry()
{
	return ModDataEntry(m_kMapModData, m_kData.size() ? &m_kData[0] : NULL);
}

ModDataEntry CvWorldBuilderMapModData::GetPlotDataEntry(uint uiPlot)
{
	const uint uiIndex = m_kMapModData.GetFieldCount() + m_kPlotModData.GetFieldCount() * uiPlot;
	return ModDataEntry(m_kPlotModData, uiIndex < m_kData.size() ? &m_kData[uiIndex] : NULL);
}

void CvWorldBuilderMapModData::InitDataEntries(uint uiPlotCount)
{
	m_kData.resize(m_kMapModData.GetFieldCount() + m_kPlotModData.GetFieldCount() * uiPlotCount);
}

void CvWorldBuilderMapModData::ResetTypes()
{
	m_kMapModData.Clear();
	m_kPlotModData.Clear();
	m_kStringBuffer.Clear();
	m_kData.clear();
}

size_t CvWorldBuilderMapModData::GetSerializedSize() const
{
	return m_kMapModData.GetSerializedSize() + m_kPlotModData.GetSerializedSize() + 4 + 4 * (size_t)m_kData.size() +
	       m_kStringBuffer.GetSerializedSize();
}

// [map ModType][plot ModType][u32 n][n x u32 data][u32 string bytes][string bytes]
void CvWorldBuilderMapModData::Deserialize(const void* pv)
{
	const byte* p = static_cast<const byte*>(pv);
	m_kMapModData.Deserialize(p);
	p += m_kMapModData.GetSerializedSize();
	m_kPlotModData.Deserialize(p);
	p += m_kPlotModData.GetSerializedSize();
	const uint n = LE32(p);
	p += 4;
	m_kData.resize(n);
	if(n)
	{
		memcpy(&m_kData[0], p, 4 * (size_t)n);
		p += 4 * (size_t)n;
	}
	m_kStringBuffer.Deserialize(p);
}

bool ModDataEntry::GetFieldAsBool(uint uiField) const
{
	return m_aFields[uiField] != 0;
}

int ModDataEntry::GetFieldAsInt(uint uiField) const
{
	return (int)m_aFields[uiField];
}

float ModDataEntry::GetFieldAsFloat(uint uiField) const
{
	float f;
	memcpy(&f, &m_aFields[uiField], sizeof(f));
	return f;
}

const char* ModDataEntry::GetFieldAsString(uint uiField) const
{
	return m_kModType.m_kStringBuffer.GetString(m_aFields[uiField]);
}

bool ModDataEntry::IsFieldDefaultValue(uint uiField) const
{
	return m_aFields[uiField] == m_kModType.m_kFields[uiField].dwDefault;
}

// =================================================================================================================
// CvWorldBuilderMap

void CvWorldBuilderMap::Resize(uint uiWidth, uint uiHeight)
{
	if(uiWidth * uiHeight <= MaxMapSize && uiWidth != 0 && uiHeight != 0)
	{
		m_kMapDesc.uiWidth = uiWidth;
		m_kMapDesc.uiHeight = uiHeight;
	}
}

void CvWorldBuilderMap::SetWorldType(const char* szWorldType)
{
	if(szWorldType && *szWorldType && strlen(szWorldType) < sizeof(m_szWorldType))
		strcpy_s(m_szWorldType, sizeof(m_szWorldType), szWorldType);
	else
		memset(m_szWorldType, 0, sizeof(m_szWorldType));
}

void CvWorldBuilderMap::SetDefaultGameSpeed(const char* szSpeedType)
{
	if(szSpeedType && *szSpeedType && strlen(szSpeedType) < sizeof(m_kGameDesc.szDefaultSpeed))
		strcpy_s(m_kGameDesc.szDefaultSpeed, sizeof(m_kGameDesc.szDefaultSpeed), szSpeedType);
}

void CvWorldBuilderMap::ClearScenarioData(bool bPreserveImprovements)
{
	const uint uiPlots = GetPlotCount();
	for(uint i = 0; i < uiPlots; ++i)
	{
		PlotScenarioData& k = m_akScenarioData[i];
		const byte byImprovement = k.m_byImprovement;
		memset(&k, 0xFF, sizeof(k));
		if(bPreserveImprovements)
			k.m_byImprovement = byImprovement;
	}
	m_kGameDesc.uiStartingTurn = 0;
	m_kGameDesc.uiMaxTurns = 0;
	m_kGameDesc.uiTargetScore = 0;
	m_kGameDesc.iStartYear = -4000;
	m_kGameDesc.byPlayerCount = 0;
	m_kGameDesc.byCityStateCount = 0;
	m_kGameDesc.byTeamCount = 0;
	m_kGameDesc.szDefaultSpeed[0] = 0;
	m_kUnits.Release();
	m_kUnitNames.Release();
	m_kCities.Release();
	m_kVictoryTypes.Clear();
	m_kGameOptions.Clear();
	m_kVisibility.Reset();
	memset(m_aiCityStateInfluence, 0, sizeof(m_aiCityStateInfluence));
}

void CvWorldBuilderMap::SwapPlayers(byte byPlayer1, byte byPlayer2)
{
	const byte n = m_kGameDesc.byPlayerCount;
	if(byPlayer1 >= n || byPlayer2 >= n || byPlayer1 == byPlayer2)
		return;
	if(byPlayer1 < MaxPlayers && byPlayer2 < MaxPlayers)
	{
		const Player kTmp = m_akPlayers[byPlayer1];
		m_akPlayers[byPlayer1] = m_akPlayers[byPlayer2];
		m_akPlayers[byPlayer2] = kTmp;
	}
	const uint uiPlots = GetPlotCount();
	for(uint i = 0; i < uiPlots; ++i)
	{
		PlotScenarioData& k = GetPlotScenarioData(i);
		if(k.m_byCulture == byPlayer1)
			k.m_byCulture = byPlayer2;
		else if(k.m_byCulture == byPlayer2)
			k.m_byCulture = byPlayer1;
		if(k.m_byRouteOwner == byPlayer1)
			k.m_byRouteOwner = byPlayer2;
		else if(k.m_byRouteOwner == byPlayer2)
			k.m_byRouteOwner = byPlayer1;
		if(City* pkCity = m_kCities[k.m_hCity])
		{
			if(pkCity->m_byOwner == byPlayer1)
				pkCity->m_byOwner = byPlayer2;
			else if(pkCity->m_byOwner == byPlayer2)
				pkCity->m_byOwner = byPlayer1;
		}
		for(Unit* pkUnit = m_kUnits[k.m_hUnitStack]; pkUnit != NULL; pkUnit = m_kUnits[pkUnit->m_hStackedUnit])
		{
			if(pkUnit->m_byOwner == byPlayer1)
				pkUnit->m_byOwner = byPlayer2;
			else if(pkUnit->m_byOwner == byPlayer2)
				pkUnit->m_byOwner = byPlayer1;
		}
	}
	for(byte cs = 0; cs < m_kGameDesc.byCityStateCount && cs < MaxCityStates; ++cs)
	{
		if(byPlayer1 < MaxPlayers && byPlayer2 < MaxPlayers)
		{
			const int iTmp = m_aiCityStateInfluence[byPlayer1][cs];
			m_aiCityStateInfluence[byPlayer1][cs] = m_aiCityStateInfluence[byPlayer2][cs];
			m_aiCityStateInfluence[byPlayer2][cs] = iTmp;
		}
	}
}

void CvWorldBuilderMap::SwapCityStates(byte byCityState1, byte byCityState2)
{
	const byte n = m_kGameDesc.byCityStateCount;
	if(byCityState1 >= n || byCityState2 >= n || byCityState1 == byCityState2 || byCityState1 >= MaxCityStates ||
	   byCityState2 >= MaxCityStates)
		return;
	const Player kTmp = m_akCityStates[byCityState1];
	m_akCityStates[byCityState1] = m_akCityStates[byCityState2];
	m_akCityStates[byCityState2] = kTmp;
	for(uint p = 0; p < MaxPlayers; ++p)
	{
		const int iTmp = m_aiCityStateInfluence[p][byCityState1];
		m_aiCityStateInfluence[p][byCityState1] = m_aiCityStateInfluence[p][byCityState2];
		m_aiCityStateInfluence[p][byCityState2] = iTmp;
	}
}

// -----------------------------------------------------------------------------------------------------------------
// Load

namespace
{
// The type-ID translation of the four map tables (terrain, feature, natural wonder, resource) and, for a scenario,
// of improvements, buildings, techs, policies, units and promotions: from the file's own type lists to the game's.
struct Luts
{
	CvWorldBuilderMapTypeDesc::LookupTable aTables[10];
	bool abDiff[10];
};
enum
{
	TD_TERRAIN, TD_FEATURE, TD_WONDER, TD_RESOURCE, TD_IMPROVEMENT, TD_UNIT, TD_BUILDING, TD_TECH, TD_POLICY,
	TD_PROMOTION
};
const CvWorldBuilderMapTypeDesc::TypeMap& TypeMapOf(const CvWorldBuilderMapTypeDesc& d, int i)
{
	switch(i)
	{
	case TD_TERRAIN: return d.m_kTerrainTypes;
	case TD_FEATURE: return d.m_kFeatures;
	case TD_WONDER: return d.m_kNaturalWonders;
	case TD_RESOURCE: return d.m_kResources;
	case TD_IMPROVEMENT: return d.m_kImprovements;
	case TD_UNIT: return d.m_kUnits;
	case TD_BUILDING: return d.m_kBuildingTypes;
	case TD_TECH: return d.m_kTechs;
	case TD_POLICY: return d.m_kPolicies;
	default: return d.m_kUnitPromotions;
	}
}
bool Build(Luts& l, int i, const CvWorldBuilderMapTypeDesc& kFrom, const CvWorldBuilderMapTypeDesc& kTo)
{
	l.abDiff[i] = CvWorldBuilderMapTypeDesc::BuildLookupTable(TypeMapOf(kFrom, i), TypeMapOf(kTo, i), l.aTables[i]);
	return l.abDiff[i];
}
// lut[v] for v below the file's type count, else `invalid`.
inline uint Map(const Luts& l, const CvWorldBuilderMapTypeDesc& kFrom, int i, uint v, uint invalid)
{
	return v < TypeMapOf(kFrom, i).Size() ? l.aTables[i][v] : invalid;
}
} // namespace

bool CvWorldBuilderMap::Load(const wchar_t* wszFilename, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario)
{
	WbMemFile kFile;
	if(!kFile.OpenPath(wszFilename))
		return false;
	byte abyHeader[9];
	if(kFile.Read(abyHeader, sizeof(abyHeader)) < sizeof(abyHeader))
		return false;
	const byte byVersionFlags = abyHeader[0];
	const byte byVersion = byVersionFlags & 0x7F;
	const uint uiWidth = LE32(abyHeader + 1);
	const uint uiHeight = LE32(abyHeader + 5);
	if(uiWidth * uiHeight > MaxMapSize)
		return false;
	m_kMapDesc.uiWidth = uiWidth;
	m_kMapDesc.uiHeight = uiHeight;
	m_sMapName.Copy(0, "", 0);
	m_sMapDescription.Copy(0, "", 0);
	memset(m_szWorldType, 0, sizeof(m_szWorldType));
	const bool bFileScenario = (byVersionFlags & 0x80) != 0;
	const bool bReadScenario = bFileScenario && bScenario;
	bool bOk;
	switch(byVersion)
	{
	case 0: bOk = Load0(kFile, kTypeDesc, bReadScenario); break;
	case 1: bOk = Load1(kFile, kTypeDesc, bReadScenario); break;
	case 2: bOk = Load2(kFile, kTypeDesc, bReadScenario); break;
	case 3: bOk = Load3(kFile, kTypeDesc, bReadScenario); break;
	case 4: bOk = Load4(kFile, kTypeDesc, bReadScenario); break;
	case 5: bOk = Load5(kFile, kTypeDesc, bReadScenario); break;
	case 6: bOk = Load6(kFile, kTypeDesc, bReadScenario); break;
	case 7: bOk = Load7(kFile, kTypeDesc, bReadScenario); break;
	case 8: bOk = Load8(kFile, kTypeDesc, bReadScenario); break;
	case 9: bOk = Load9(kFile, kTypeDesc, bReadScenario); break;
	case 10: bOk = Load10(kFile, kTypeDesc, bReadScenario); break;
	case 11:
	case 12: bOk = Load11Plus(kFile, kTypeDesc, bReadScenario, byVersion); break;
	default: bOk = false; break;
	}
	if(!bOk)
		return false;
	if(bScenario && !bFileScenario)
		ClearScenarioData(false);
	return true;
}

uint CvWorldBuilderMap::PreviewPlayableCivCount(const wchar_t* wszFilename)
{
	WbMemFile kFile;
	if(!kFile.OpenPath(wszFilename))
		return 0;
	byte aby[10];
	if(kFile.Read(aby, sizeof(aby)) != sizeof(aby))
		return 0;
	if((aby[0] & 0x80) == 0)
		return 0;
	const byte byVersion = aby[0] & 0x7F;
	if(byVersion <= 2)
		return 0;
	if(byVersion != 3 && byVersion != 4)
		return aby[9];
	// Versions 3 and 4 have no count in the header: load the scenario and count the playable players.
	CvWorldBuilderMap* pkMap = new CvWorldBuilderMap();
	CvWorldBuilderMapTypeDesc kDesc;
	uint uiCount = 0;
	pkMap->Load(wszFilename, kDesc, true);
	for(byte i = 0; i < pkMap->GetPlayerCount(); ++i)
		if(pkMap->GetPlayer(i).m_bPlayable)
			++uiCount;
	delete pkMap;
	return uiCount;
}

// The versioned reader: versions 6, 7, 9, 10, 11 and 12, which between them cover every map of the install
// (civ5map-format.md sections 3 and 4). Load6, Load7, Load9 and Load10 call it with their version number.
bool CvWorldBuilderMap::Load11Plus(FIFile& kFile, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario,
                                   byte byFileVersion)
{
	const byte v = byFileVersion;
	const bool bOldRecords = v <= 7;	// OldUnit, OldCity, OldPlayer; no unit names, no promotions
	m_uiFlags = FLAG_WORLD_WRAP | FLAG_TRANSFERABLE;
	byte byPlayableCivs;
	if(kFile.Read(&byPlayableCivs, 1) != 1)
		return false;
	const uint uiPlots = GetPlotCount();

	// Map header: flags and the section lengths (six in version 6: no map name), then the sections in one block.
	const uint uiLens = v == 6 ? 6 : 7;
	byte abyHeader[32];
	const uint uiHeaderBytes = 4 + 4 * uiLens;
	const uint uiGot = kFile.Read(abyHeader, uiHeaderBytes);
	uint auiLen[7] = {0, 0, 0, 0, 0, 0, 0};
	uint uiTotal = 0;
	for(uint i = 0; i < uiLens; ++i)
	{
		auiLen[i] = LE32(abyHeader + 4 + 4 * i);
		uiTotal += auiLen[i];
	}
	if(uiGot < uiHeaderBytes || uiTotal == 0 || uiTotal > 0x100000)
		return false;
	m_uiFlags = LE32(abyHeader);
	if(bOldRecords)
		m_uiFlags |= FLAG_OLD_GOODIE_RULES | FLAG_TRANSFERABLE;	// up to version 7
	std::vector<byte> kBlock(uiTotal);
	if(kFile.Read(&kBlock[0], uiTotal) < uiTotal)
		return false;

	CvWorldBuilderMapTypeDesc kFileDesc;
	byte* p = &kBlock[0];
	if(auiLen[0]) { kFileDesc.m_kTerrainTypes.Deserialize(p, auiLen[0]); p += auiLen[0]; }
	if(auiLen[1]) { kFileDesc.m_kFeatures.Deserialize(p, auiLen[1]); p += auiLen[1]; }
	if(auiLen[2]) { kFileDesc.m_kNaturalWonders.Deserialize(p, auiLen[2]); p += auiLen[2]; }
	if(auiLen[3]) { kFileDesc.m_kResources.Deserialize(p, auiLen[3]); p += auiLen[3]; }
	if(auiLen[4] > 0)
		m_kModData.Deserialize(p);
	else
		m_kModData.ResetTypes();
	p += auiLen[4];
	if(v != 6)
	{
		m_sMapName.Copy((int)strlen(reinterpret_cast<const char*>(p)), reinterpret_cast<const char*>(p), 0);
		p += auiLen[5];
	}
	m_sMapDescription.Copy((int)strlen(reinterpret_cast<const char*>(p)), reinterpret_cast<const char*>(p), 0);
	kBlock.clear();

	// World size type (up to 64 bytes; version 11 on), then the plots.
	if(v >= 11)
	{
		byte abyLen[4] = {0, 0, 0, 0};
		kFile.Read(abyLen, 4);
		const uint uiWorldLen = LE32(abyLen);
		if(uiWorldLen > sizeof(m_szWorldType))
			return false;
		kFile.Read(m_szWorldType, uiWorldLen);
	}
	if(kFile.Read(m_akMapData, uiPlots * kFilePlot) < uiPlots * kFilePlot)
		return false;

	if(bScenario)
	{
		if(bOldRecords)
			ClearScenarioData(false);
		// Scenario header: the game description (84 bytes) and the section lengths (nine up to version 7, then 11).
		const uint uiSections = bOldRecords ? 9 : 11;
		const uint uiScenHeader = 84 + 4 * uiSections;
		byte abyScen[128];
		memset(abyScen, 0, sizeof(abyScen));
		{
			GameDescription kDefault;
			memcpy(abyScen, &kDefault, 84);
		}
		const uint uiScen = kFile.Read(abyScen, uiScenHeader);
		memcpy(&m_kGameDesc, abyScen, 84);
		uint auiS[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		for(uint i = 0; i < uiSections; ++i)
			auiS[i] = LE32(abyScen + 84 + 4 * i);
		const uint uiTeams = m_kGameDesc.byTeamCount;
		const uint uiRelBytes = FRelationshipBitset<MaxTeams, true>::GetSerializedSize(uiTeams);
		const uint uiVisBits = uiTeams * uiPlots;
		const uint uiVisBytes = uiVisBits / 8 + ((uiVisBits % 8) ? 1 : 0);
		// City-state influence: in the file from version 9; counted in the block's size from version 10 (a
		// version 9 reader consumes it from the block without counting it: what follows is zero here).
		const uint uiInfluence = v >= 9 ? (uint)m_kGameDesc.byPlayerCount * 256 : 0;
		uint uiScenTotal = 5 * uiRelBytes + uiVisBytes + (v >= 10 ? uiInfluence : 0);
		for(uint i = 0; i < uiSections; ++i)
			uiScenTotal += auiS[i];
		if(uiScen < uiScenHeader || uiScenTotal > 0x800000)
			return false;
		std::vector<byte> kScen((size_t)uiScenTotal + (v == 9 ? uiInfluence : 0) + 1, 0);
		if(kFile.Read(&kScen[0], uiScenTotal) < uiScenTotal)
			return false;

		byte* q = &kScen[0];
		uint s = 0;
		if(auiS[s]) kFileDesc.m_kImprovements.Deserialize(q, auiS[s]);
		q += auiS[s++];
		if(auiS[s]) kFileDesc.m_kUnits.Deserialize(q, auiS[s]);
		q += auiS[s++];
		kFileDesc.m_kTechs.Deserialize(q, auiS[s]);
		q += auiS[s++];
		kFileDesc.m_kPolicies.Deserialize(q, auiS[s]);
		q += auiS[s++];
		kFileDesc.m_kBuildingTypes.Deserialize(q, auiS[s]);
		q += auiS[s++];
		if(!bOldRecords)
		{
			if(auiS[s]) kFileDesc.m_kUnitPromotions.Deserialize(q, auiS[s]);
			q += auiS[s++];
		}
		m_kUnits.Release();
		if(bOldRecords)
			DeserializeLegacy<OldUnit, Unit>(m_kUnits, q, auiS[s], kFileOldUnit, OldUnitFromFile);
		else if(v <= 11)
			DeserializeLegacy<UnitV2, Unit>(m_kUnits, q, auiS[s], kFileUnitV2, UnitV2FromFile);
		else if(sizeof(Unit) == kFileUnit)
			m_kUnits.Deserialize(auiS[s] ? q : NULL, auiS[s]);
		else
			DeserializeConverted(m_kUnits, auiS[s] ? q : NULL, auiS[s], kFileUnit, (uint)sizeof(Unit), UnitFromFile);
		q += auiS[s++];
		if(!bOldRecords)
		{
			m_kUnitNames.Deserialize(q, auiS[s]);
			q += auiS[s++];
		}
		m_kCities.Release();
		if(bOldRecords)
			DeserializeLegacy<OldCity, City>(m_kCities, q, auiS[s], kFileOldCity, OldCityRecordFromFile);
		else if(v <= 11)
			DeserializeLegacy<CityV2, City>(m_kCities, q, auiS[s], kFileCityV2, CityV2FromFile);
		else
			m_kCities.Deserialize(auiS[s] ? q : NULL, auiS[s]);
		q += auiS[s++];
		m_kVictoryTypes.Deserialize(q, auiS[s]);
		q += auiS[s++];
		m_kGameOptions.Deserialize(q, auiS[s]);
		q += auiS[s++];
		if(uiRelBytes <= 570)
		{
			m_kTeamsInContact.Deserialize(q, uiTeams);
			m_kTeamsAtWar.Deserialize(q + uiRelBytes, uiTeams);
			m_kTeamsAtPermanentWarOrPeace.Deserialize(q + 2 * uiRelBytes, uiTeams);
			m_kTeamsSharingOpenBorders.Deserialize(q + 3 * uiRelBytes, uiTeams);
			m_kTeamsSharingDefensivePacts.Deserialize(q + 4 * uiRelBytes, uiTeams);
		}
		q += 5 * uiRelBytes;
		if(uiInfluence)
		{
			memcpy(m_aiCityStateInfluence, q,
			       uiInfluence < sizeof(m_aiCityStateInfluence) ? uiInfluence : sizeof(m_aiCityStateInfluence));
			q += uiInfluence;
		}
		if(uiVisBits <= MaxMapSize * MaxTeams)
			m_kVisibility.Deserialize(q, uiVisBits);
		kScen.clear();

		if(!ReadClamped(kFile, m_akTeams, uiTeams * kFileTeam, sizeof(m_akTeams)))
			return false;
		const uint uiPlayerRecord = bOldRecords ? kFileOldPlayer : kFilePlayer;
		for(int iTable = 0; iTable < 2; ++iTable)
		{
			const uint n = iTable == 0 ? m_kGameDesc.byPlayerCount : m_kGameDesc.byCityStateCount;
			const uint uiMax = iTable == 0 ? MaxPlayers : MaxCityStates;
			Player* akTable = iTable == 0 ? m_akPlayers : m_akCityStates;
			std::vector<byte> kRecords((size_t)n * uiPlayerRecord + 1);
			if(kFile.Read(&kRecords[0], n * uiPlayerRecord) < n * uiPlayerRecord)
				return false;
			for(uint i = 0; i < n && i < uiMax; ++i)
			{
				const byte* src = &kRecords[(size_t)i * uiPlayerRecord];
				if(bOldRecords)
				{
					OldPlayer kOld;
					memcpy(&kOld, src, kFileOldPlayer);
					new(&akTable[i]) Player(kOld);
					akTable[i].m_uiCulture = 0;
				}
				else
				{
					memcpy(&akTable[i], src, kFilePlayer);
				}
			}
		}
		if(kFile.Read(m_akScenarioData, uiPlots * kFilePlot) < uiPlots * kFilePlot)
			return false;
	}

	// Translate the type IDs from the file's lists to the game's.
	Luts l;
	Build(l, TD_TERRAIN, kFileDesc, kTypeDesc);
	Build(l, TD_FEATURE, kFileDesc, kTypeDesc);
	Build(l, TD_WONDER, kFileDesc, kTypeDesc);
	Build(l, TD_RESOURCE, kFileDesc, kTypeDesc);
	if((l.abDiff[TD_TERRAIN] || l.abDiff[TD_FEATURE] || l.abDiff[TD_WONDER] || l.abDiff[TD_RESOURCE]) && uiPlots)
	{
		for(uint i = 0; i < uiPlots; ++i)
		{
			PlotMapData& k = m_akMapData[i];
			k.SetTerrainType((byte)Map(l, kFileDesc, TD_TERRAIN, k.GetTerrainType(), 0));
			k.SetFeatureType((byte)Map(l, kFileDesc, TD_FEATURE, k.GetFeatureType(), 0xFF));
			k.SetNaturalWonderType((byte)Map(l, kFileDesc, TD_WONDER, k.GetNaturalWonderType(), 0xFF));
			k.SetResourceType((byte)Map(l, kFileDesc, TD_RESOURCE, k.GetResourceType(), 0xFF));
		}
	}
	if(!bScenario)
		return true;

	Build(l, TD_IMPROVEMENT, kFileDesc, kTypeDesc);
	Build(l, TD_BUILDING, kFileDesc, kTypeDesc);
	if((l.abDiff[TD_IMPROVEMENT] || l.abDiff[TD_BUILDING]) && uiPlots)
	{
		for(uint i = 0; i < uiPlots; ++i)
		{
			PlotScenarioData& k = m_akScenarioData[i];
			k.m_byImprovement = (byte)Map(l, kFileDesc, TD_IMPROVEMENT, k.m_byImprovement, 0xFF);
			if(l.abDiff[TD_BUILDING])
				if(City* pkCity = m_kCities[k.m_hCity])
					RemapBits(pkCity->m_kBuildings, l.aTables[TD_BUILDING], kFileDesc.m_kBuildingTypes.Size());
		}
	}
	if(Build(l, TD_TECH, kFileDesc, kTypeDesc))
		for(uint t = 0; t < m_kGameDesc.byTeamCount && t < MaxTeams; ++t)
			RemapBits(m_akTeams[t].m_kTechs, l.aTables[TD_TECH], kFileDesc.m_kTechs.Size());
	if(Build(l, TD_POLICY, kFileDesc, kTypeDesc))
		for(uint i = 0; i < m_kGameDesc.byPlayerCount && i < MaxPlayers; ++i)
			RemapBits(m_akPlayers[i].m_kPolicies, l.aTables[TD_POLICY], kFileDesc.m_kPolicies.Size());
	Build(l, TD_UNIT, kFileDesc, kTypeDesc);
	bool bUnits, bPromotions = false;
	if(bOldRecords)
	{
		// Up to version 7 every unit's type is translated, whether or not the lists differ.
		bUnits = true;
	}
	else
	{
		Build(l, TD_PROMOTION, kFileDesc, kTypeDesc);
		bPromotions = l.abDiff[TD_PROMOTION];
		bUnits = (l.abDiff[TD_UNIT] || bPromotions) && m_kUnits.GetSerializedSize() != 0;
	}
	if(bUnits && uiPlots)
	{
		for(uint i = 0; i < uiPlots; ++i)
		{
			for(Unit* pkUnit = m_kUnits[m_akScenarioData[i].m_hUnitStack]; pkUnit != NULL;
			    pkUnit = m_kUnits[pkUnit->m_hStackedUnit])
			{
				pkUnit->m_byUnitType = Map(l, kFileDesc, TD_UNIT, pkUnit->m_byUnitType, (uint)-1);
				if(bPromotions)
					RemapBits(pkUnit->m_kPromotions, l.aTables[TD_PROMOTION], kFileDesc.m_kUnitPromotions.Size());
			}
		}
	}
	return true;
}

bool CvWorldBuilderMap::Load6(FIFile& kFile, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario)
{
	return Load11Plus(kFile, kTypeDesc, bScenario, 6);
}

bool CvWorldBuilderMap::Load7(FIFile& kFile, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario)
{
	return Load11Plus(kFile, kTypeDesc, bScenario, 7);
}

bool CvWorldBuilderMap::Load9(FIFile& kFile, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario)
{
	return Load11Plus(kFile, kTypeDesc, bScenario, 9);
}

bool CvWorldBuilderMap::Load10(FIFile& kFile, const CvWorldBuilderMapTypeDesc& kTypeDesc, bool bScenario)
{
	return Load11Plus(kFile, kTypeDesc, bScenario, 10);
}

// -----------------------------------------------------------------------------------------------------------------
// Versions 0 to 5 and 8 are refused: no map of the install or of the tested mods uses them, and their readers are
// not described in the knowledge base (civ5map-format.md section 1). The 32-bit library reads them.
bool CvWorldBuilderMap::Load0(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
bool CvWorldBuilderMap::Load1(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
bool CvWorldBuilderMap::Load2(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
bool CvWorldBuilderMap::Load3(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
bool CvWorldBuilderMap::Load4(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
bool CvWorldBuilderMap::Load5(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
bool CvWorldBuilderMap::Load8(FIFile&, const CvWorldBuilderMapTypeDesc&, bool) { return false; }
