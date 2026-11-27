// CvLocalization replacement (c5-64bit-patch issue 1701): the StringDictionary over the localization database.
// Same SQL text as the closed library (decompile 0x1000ac20..0x1000b820), run through our Database::Connection and
// Results (CvGameDatabase_<arch>.dll), so the rows are the same on the same SQLite 3.7.17. See ../README.md.
#include <string.h>

#include "LocInternal.h"

namespace Localization
{
namespace Detail
{
namespace
{
const char* const kLookupFallback =
	"select Text, Plurality, Gender from LocalizedText where rowid = (select rowid from (select rowid, Language from "
	"LocalizedText where Tag = ?2) as T1 left join (select Language, Priority from LanguagePriorities where "
	"LanguageSet = ?1 ) as Priorities on T1.Language = Priorities.Language order by Priority DESC LIMIT 1)";
const char* const kLookupNoFallback =
	"SELECT Text, Plurality, Gender from LocalizedText where Language = ? and Tag = ? LIMIT 1";
const char* const kTags = "SELECT DISTINCT Tag from LocalizedText";

struct Lock
{
	explicit Lock(CRITICAL_SECTION& cs) : m_cs(cs) { EnterCriticalSection(&m_cs); }
	~Lock() { LeaveCriticalSection(&m_cs); }
	CRITICAL_SECTION& m_cs;
	Lock& operator=(const Lock&) = delete;
};
} // namespace

DictImpl::DictImpl(Database::Connection& db, const char* szLanguagesTable, bool bDisableFallback)
	: m_db(&db), m_current(0), m_bPreload(true)
{
	InitializeCriticalSection(&m_cs);
	Lock lock(m_cs);
	const int count = db.Count(szLanguagesTable);
	if (count <= 0)
	{
		LogF(ILocalizationLogger::TYPE_ERROR, 0x1f8, "Languages table not found or is empty. %s", db.ErrorMessage());
		return;
	}
	m_languages.reserve((size_t)count);
	{
		Database::Results langs;
		if (!db.SelectAll(langs, szLanguagesTable))
		{
			LogF(ILocalizationLogger::TYPE_ERROR, 0x200, "Failed to enumerate languages table (%s). %s",
			     szLanguagesTable, db.ErrorMessage());
		}
		else
		{
			while (langs.Step())
			{
				DictLanguage l;
				const char* szType = langs.GetText("Type");
				const char* szName = langs.GetText("Name");
				l.m_type = szType != NULL ? szType : "";
				l.m_name = szName != NULL ? szName : "";
				l.m_iPluralRule = langs.GetInt("PluralRule");
				m_languages.push_back(l);
			}
		}
	}
	if (!db.Execute(m_lookup, bDisableFallback ? kLookupNoFallback : kLookupFallback))
		Log(ILocalizationLogger::TYPE_ERROR, "Failed to generate Localized Text Lookup query.");
	db.Execute(m_tags, kTags);
}

DictImpl::~DictImpl()
{
	m_cache.clear();
	m_lookup.Release();
	m_tags.Release();
	DeleteCriticalSection(&m_cs);
}

// Binds ?1 = the language type, ?2 = the key, steps once. Text -> String(text) (NULL -> empty), then the Plurality
// and Gender columns, when not NULL, through String::SetPlurality / SetGender (rules of the current language).
bool DictImpl::QueryLookup(unsigned int idx, const char* szKey, unsigned int lenKey, String& out) const
{
	if (szKey == NULL || idx >= m_languages.size())
		return false;
	m_lookup.Bind(1, m_languages[idx].m_type.c_str(), -1, false);
	m_lookup.Bind(2, szKey, (int)lenKey, false);
	bool bFound = false;
	if (m_lookup.Step())
	{
		out = String(m_lookup.GetText(0));
		const char* szPlurality = m_lookup.GetText(1);
		if (szPlurality != NULL)
			out.SetPlurality(szPlurality, strlen(szPlurality));
		const char* szGender = m_lookup.GetText(2);
		if (szGender != NULL)
			out.SetGender(szGender, strlen(szGender));
		bFound = true;
	}
	m_lookup.Reset();
	return bFound;
}

bool DictImpl::QueryHasKey(unsigned int idx, const char* szKey, unsigned int lenKey) const
{
	if (idx >= m_languages.size())
		return false;
	m_lookup.Bind(1, m_languages[idx].m_type.c_str(), -1, false);
	m_lookup.Bind(2, szKey, (int)lenKey, false);
	const bool bFound = m_lookup.Step();
	m_lookup.Reset();
	return bFound;
}

// Caches every distinct tag of the table for the current language (a missing one as the key itself).
void DictImpl::Preload()
{
	m_cache.clear();
	while (m_tags.Step())
	{
		const char* szTag = m_tags.GetText(0);
		if (szTag == NULL)
			szTag = "";
		const unsigned int len = (unsigned int)strlen(szTag);
		String s;
		if (!QueryLookup(m_current, szTag, len, s))
			s = String(szTag, len);
		m_cache[std::string(szTag, len)] = s;
	}
	m_tags.Reset();
}
} // namespace Detail

// ------------------------------------------------------------------------------------------------- StringDictionary
namespace
{
const Detail::DictImpl* Impl(const StringDictionary* p)
{
	return static_cast<const Detail::DictImpl*>(p);
}

Detail::DictImpl* Impl(StringDictionary* p)
{
	return static_cast<Detail::DictImpl*>(p);
}
} // namespace

String StringDictionary::Lookup(const char* szTextKey) const
{
	if (szTextKey == NULL)
		return String();
	return LookupLanguage(szTextKey, (unsigned int)strlen(szTextKey), Impl(this)->m_current);
}

String StringDictionary::Lookup(const char* szTextKey, unsigned int lenTextKey) const
{
	return LookupLanguage(szTextKey, lenTextKey, Impl(this)->m_current);
}

bool StringDictionary::TryLookup(const char* szTextKey, unsigned int lenTextKey, String& output) const
{
	return TryLookupLanguage(szTextKey, lenTextKey, Impl(this)->m_current, output);
}

String StringDictionary::LookupLanguage(const char* szTextKey, unsigned int uiLanguageIndex) const
{
	if (szTextKey == NULL)
		return String();
	return LookupLanguage(szTextKey, (unsigned int)strlen(szTextKey), uiLanguageIndex);
}

// A missing key gives a String holding the key itself.
String StringDictionary::LookupLanguage(const char* szTextKey, unsigned int lenTextKey, unsigned int uiLanguageIndex) const
{
	if (szTextKey == NULL || lenTextKey == 0)
		return String();
	String s;
	if (!TryLookupLanguage(szTextKey, lenTextKey, uiLanguageIndex, s))
		s = String(szTextKey, lenTextKey);
	return s;
}

// The current language answers from the preloaded cache (only keys the table has); another language, or an empty
// cache, queries the database.
bool StringDictionary::TryLookupLanguage(const char* szTextKey, unsigned int lenTextKey, unsigned int uiLanguageIndex,
                                         String& output) const
{
	if (szTextKey == NULL)
		return false;
	const Detail::DictImpl* d = Impl(this);
	Detail::Lock lock(d->m_cs);
	if (uiLanguageIndex == d->m_current && !d->m_cache.empty())
	{
		auto it = d->m_cache.find(std::string(szTextKey, lenTextKey));
		if (it == d->m_cache.end())
			return false;
		output = it->second;
		return true;
	}
	return d->QueryLookup(uiLanguageIndex, szTextKey, lenTextKey, output);
}

bool StringDictionary::HasKey(const char* szTextKey, unsigned int lenTextKey) const
{
	return HasKey(szTextKey, lenTextKey, Impl(this)->m_current);
}

bool StringDictionary::HasKey(const char* szTextKey, unsigned int lenTextKey, unsigned int uiLanguageIndex) const
{
	if (szTextKey == NULL)
		return false;
	const Detail::DictImpl* d = Impl(this);
	Detail::Lock lock(d->m_cs);
	return d->QueryHasKey(uiLanguageIndex, szTextKey, lenTextKey);
}

unsigned int StringDictionary::GetCurrentLanguageIndex() const
{
	return Impl(this)->m_current;
}

bool StringDictionary::SetCurrentLanguage(unsigned int idx)
{
	Detail::DictImpl* d = Impl(this);
	if (idx >= d->m_languages.size())
		return false;
	d->m_current = idx;
	if (d->m_bPreload)
	{
		Detail::Lock lock(d->m_cs);
		d->Preload();
	}
	return true;
}

bool StringDictionary::SetCurrentLanguage(const char* szLanguageType)
{
	const Detail::DictImpl* d = Impl(this);
	if (szLanguageType == NULL)
		return false;
	for (unsigned int i = 0; i < d->m_languages.size(); ++i)
		if (d->m_languages[i].m_type == szLanguageType)
			return SetCurrentLanguage(i);
	return false;
}

size_t StringDictionary::GetLanguageCount() const
{
	return Impl(this)->m_languages.size();
}

const char* StringDictionary::GetLanguageType(unsigned int idx) const
{
	return Impl(this)->m_languages[idx].m_type.c_str();
}

const char* StringDictionary::GetLanguageDisplayName(unsigned int idx) const
{
	return Impl(this)->m_languages[idx].m_name.c_str();
}

const StringDictionary::Language& StringDictionary::GetLanguage(unsigned int idx) const
{
	return Impl(this)->m_languages[idx];
}

void StringDictionary::NotifyDatabaseUpdated() const
{
	Detail::DictImpl* d = const_cast<Detail::DictImpl*>(Impl(this));
	Detail::Lock lock(d->m_cs);
	d->Preload();
}

const char* StringDictionary::Language::GetType() const
{
	return static_cast<const Detail::DictLanguage*>(this)->m_type.c_str();
}

const char* StringDictionary::Language::GetDisplayName() const
{
	return static_cast<const Detail::DictLanguage*>(this)->m_name.c_str();
}

int StringDictionary::Language::GetPluralityRule() const
{
	return static_cast<const Detail::DictLanguage*>(this)->m_iPluralRule;
}
} // namespace Localization
