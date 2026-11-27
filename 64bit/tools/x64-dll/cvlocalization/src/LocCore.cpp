// CvLocalization replacement (c5-64bit-patch issue 1701): the library's global state (logger, ICU data check,
// dictionary, languages), Initialize/Shutdown, SetCurrentLanguage, Lookup, Localization::Language and the 16
// plural rules. Behaviour per member: ../README.md.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "LocInternal.h"

namespace Localization
{
namespace
{
ILocalizationLogger* g_pkLogger = NULL;
bool g_bIcuData = false;                   // the closed library keeps the whole ICU .dat file in memory here
Detail::DictImpl* g_pkDictionary = NULL;
std::vector<Detail::LanguageImpl*> g_languages;  // one per language type ever made current (until shutdown)
size_t g_current = 0;                      // index into g_languages; not reset by ShutdownDictionary (closed)

// ------------------------------------------------------------------------------------------------- plural rules
// Disassembly of the closed library, 0x10006c00..0x10006ff0 (vtables 0x1008b324..0x1008b39c). C remainders
// (negative n keep their sign), as the original. PluralRule k is Mozilla's plural rule k-1 with the deviations
// noted (rules 4, 7, 8, 10, 11, 16 differ from Mozilla's for some n).
template <unsigned int MAX, unsigned int (*FORM)(int)>
class Rule : public IPluralityRule
{
public:
	unsigned int GetMaxPluralForms() const override { return MAX; }
	unsigned int GetPluralForm(int n) const override { return FORM(n); }
};

unsigned int R1(int) { return 1; }
unsigned int R2(int n) { return n == 1 ? 1 : 2; }
unsigned int R3(int n) { return (n == 0 || n == 1) ? 1 : 2; }
unsigned int R4(int n) { return n == 0 ? 1 : ((n % 10 == 1 && n != 11) ? 2 : 3); }
unsigned int R5(int n) { return n == 1 ? 1 : (n == 2 ? 2 : 3); }
unsigned int R6(int n) { return n == 1 ? 1 : ((n == 0 || n % 100 <= 19) ? 2 : 3); }
unsigned int R7(int n) { return n % 10 == 1 ? 1 : (n % 10 == 0 ? 2 : 3); }
unsigned int R8(int n)
{
	const int r = n % 10;
	if (r == 1)
		return n == 11 ? 3 : 1;
	if (r >= 2 && r <= 4 && !(n >= 12 && n <= 14))
		return 2;
	return 3;
}
unsigned int R9(int n) { return n == 1 ? 1 : ((unsigned int)(n - 2) <= 2 ? 2 : 3); }
unsigned int R10(int n)
{
	if (n == 1)
		return 1;
	const int r = n % 10;
	return (r >= 2 && r <= 4 && !(n >= 12 && n <= 14)) ? 2 : 3;
}
unsigned int R11(int n)
{
	const int r = n % 10;
	return r == 1 ? 1 : (r == 2 ? 2 : ((r == 3 || r == 4) ? 3 : 4));
}
unsigned int R12(int n)
{
	if (n == 1)
		return 1;
	if (n == 2)
		return 2;
	if ((unsigned int)(n - 3) <= 3)
		return 3;
	return (unsigned int)(n - 7) <= 3 ? 4 : 5;
}
unsigned int R13(int n)
{
	if (n == 1)
		return 1;
	if (n == 2)
		return 2;
	const int r = n % 100;
	if ((unsigned int)(r - 3) <= 7)
		return 3;
	if ((unsigned int)(r - 11) <= 88)
		return 4;
	return n != 0 ? 5 : 6;
}
unsigned int R14(int n)
{
	if (n == 1)
		return 1;
	const int r = n % 100;
	if (n == 0 || r <= 10)
		return 2;
	return (unsigned int)(r - 11) <= 8 ? 3 : 4;
}
unsigned int R15(int n)
{
	const int r = n % 10;
	return r == 1 ? 1 : (r == 2 ? 2 : 3);
}
unsigned int R16(int n) { return (n % 10 == 1 && n != 11) ? 1 : 2; }
} // namespace

namespace Detail
{
void Log(ILocalizationLogger::LogTypes eType, const char* szMessage)
{
	if (g_pkLogger != NULL)
		g_pkLogger->Log(szMessage, eType);
}

void LogF(ILocalizationLogger::LogTypes eType, size_t bufSize, const char* szFormat, ...)
{
	if (g_pkLogger == NULL)
		return;
	char buf[1024];
	if (bufSize > sizeof(buf))
		bufSize = sizeof(buf);
	va_list ap;
	va_start(ap, szFormat);
	_vsnprintf_s(buf, bufSize, _TRUNCATE, szFormat, ap);
	va_end(ap);
	g_pkLogger->Log(buf, eType);
}

IPluralityRule* CreatePluralityRule(int iRule)
{
	switch (iRule)
	{
	case 1: return new Rule<1, R1>();
	case 2: return new Rule<2, R2>();
	case 3: return new Rule<2, R3>();
	case 4: return new Rule<3, R4>();
	case 5: return new Rule<3, R5>();
	case 6: return new Rule<3, R6>();
	case 7: return new Rule<3, R7>();
	case 8: return new Rule<3, R8>();
	case 9: return new Rule<3, R9>();
	case 10: return new Rule<3, R10>();
	case 11: return new Rule<4, R11>();
	case 12: return new Rule<5, R12>();
	case 13: return new Rule<6, R13>();
	case 14: return new Rule<4, R14>();
	case 15: return new Rule<3, R15>();
	case 16: return new Rule<2, R16>();
	default: return NULL;
	}
}

LanguageImpl::LanguageImpl(const char* szType, IPluralityRule* pkRule)
	: Language(szType, pkRule), m_pkRule(NULL), m_pkTransforms(NULL), m_formatters(0)
{
	memset(m_szType, 0, sizeof(m_szType));
	if (szType != NULL)
		strncpy_s(m_szType, sizeof(m_szType), szType, _TRUNCATE);
	m_pkRule = pkRule;
}

LanguageImpl::~LanguageImpl()
{
	delete m_pkRule;
}

Language* CurrentLanguage()
{
	return g_current < g_languages.size() ? g_languages[g_current] : NULL;
}

IPluralityRule* RuleOf(const Language* pkLanguage)
{
	if (pkLanguage == NULL)
		pkLanguage = CurrentLanguage();
	return pkLanguage != NULL ? pkLanguage->GetPluralityRule() : NULL;
}

DictImpl* Dictionary()
{
	return g_pkDictionary;
}
} // namespace Detail

// ------------------------------------------------------------------------------------------------- Language
Language::Language(const char*, IPluralityRule*)
{
}

Language::Language(const Language&)
{
}

Language::~Language()
{
}

const char* Language::GetType() const
{
	return static_cast<const Detail::LanguageImpl*>(this)->m_szType;
}

unsigned int Language::GetPluralForm(int nElements) const
{
	const IPluralityRule* pkRule = static_cast<const Detail::LanguageImpl*>(this)->m_pkRule;
	return pkRule != NULL ? pkRule->GetPluralForm(nElements) : 0;
}

IPluralityRule* Language::GetPluralityRule() const
{
	return static_cast<const Detail::LanguageImpl*>(this)->m_pkRule;
}

void Language::SetPluralityRule(IPluralityRule* pkPluralityRule)
{
	// The closed library releases the old rule (shared_ptr) and then deletes the new one at once (a bug: the
	// language keeps a dangling pointer). Here the language takes ownership of the new rule.
	Detail::LanguageImpl* self = static_cast<Detail::LanguageImpl*>(this);
	if (self->m_pkRule != pkPluralityRule)
	{
		delete self->m_pkRule;
		self->m_pkRule = pkPluralityRule;
	}
}

bool Language::Compare(const char* szString1, size_t lenString1, const char* szString2, size_t lenString2,
                       int& result) const
{
	// The closed library compares with an ICU collator for the language. Here: Windows' locale-aware comparison for
	// the language type (en_US -> en-US).
	wchar_t wszLocale[LOCALE_NAME_MAX_LENGTH] = {};
	const char* szType = GetType();
	for (int i = 0; szType[i] != 0 && i < LOCALE_NAME_MAX_LENGTH - 1; ++i)
		wszLocale[i] = (wchar_t)(szType[i] == '_' ? '-' : szType[i]);
	std::wstring w1, w2;
	for (int k = 0; k < 2; ++k)
	{
		const char* s = k == 0 ? szString1 : szString2;
		const int n = (int)(k == 0 ? lenString1 : lenString2);
		std::wstring& w = k == 0 ? w1 : w2;
		if (s != NULL && n > 0)
		{
			const int m = MultiByteToWideChar(CP_UTF8, 0, s, n, NULL, 0);
			w.resize(m > 0 ? m : 0);
			if (m > 0)
				MultiByteToWideChar(CP_UTF8, 0, s, n, &w[0], m);
		}
	}
	const int r = CompareStringEx(wszLocale, 0, w1.c_str(), (int)w1.size(), w2.c_str(), (int)w2.size(), NULL, NULL, 0);
	if (r == 0)
		return false;
	result = r - CSTR_EQUAL;
	return true;
}

const ParameterTransforms& Language::GetParameterTransforms() const
{
	// Nothing calls the transforms through this object (their exported signatures differ from the header's).
	return *static_cast<const Detail::LanguageImpl*>(this)->m_pkTransforms;
}

// ------------------------------------------------------------------------------------------------- logging
void SetLogger(ILocalizationLogger* pkLogger)
{
	delete g_pkLogger;
	g_pkLogger = pkLogger;
}

ILocalizationLogger* GetLogger()
{
	return g_pkLogger;
}

void LogMessage(const char* szMessage)
{
	Detail::Log(ILocalizationLogger::TYPE_MESSAGE, szMessage);
}

void LogWarning(const char* szWarning)
{
	Detail::Log(ILocalizationLogger::TYPE_WARNING, szWarning);
}

void LogError(const char* szError)
{
	Detail::Log(ILocalizationLogger::TYPE_ERROR, szError);
}

// ------------------------------------------------------------------------------------------------- lifetime
bool Initialize(const wchar_t* wszIcuDatFile, Database::Connection& db, const char* szLanguagesTableName,
                const char* szLanguageType, bool bDisableFallbackLanguageSupport)
{
	// The closed library reads the whole ICU data file into memory (udata_setCommonData) and fails if it cannot.
	// ICU is not used here; the file is still required, so that Initialize succeeds and fails in the same cases.
	if (g_bIcuData || wszIcuDatFile == NULL)
		return false;
	HANDLE h = CreateFileW(wszIcuDatFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size;
	const bool ok = GetFileSizeEx(h, &size) && size.HighPart == 0 && size.LowPart != 0;
	CloseHandle(h);
	if (!ok)
		return false;
	g_bIcuData = true;
	return InitializeDictionary(db, szLanguagesTableName, szLanguageType, bDisableFallbackLanguageSupport);
}

bool Shutdown()
{
	SetLogger(NULL);
	ShutdownDictionary();
	g_bIcuData = false;
	return true;
}

bool InitializeDictionary(Database::Connection& db, const char* szLanguagesTableName, const char* szLanguageType,
                          bool bDisableFallbackLanguageSupport)
{
	if (g_pkDictionary != NULL)
	{
		Detail::Log(ILocalizationLogger::TYPE_WARNING,
		            "Trying to initialize the string dictionary when it already exists o_0!");
		return false;
	}
	g_pkDictionary = new Detail::DictImpl(db, szLanguagesTableName, bDisableFallbackLanguageSupport);
	if (SetCurrentLanguage(szLanguageType))
		return true;
	if (g_pkDictionary->m_languages.empty())
	{
		Detail::Log(ILocalizationLogger::TYPE_WARNING, "No languages in string dictionary.");
		return false;
	}
	Detail::Log(ILocalizationLogger::TYPE_WARNING, "Falling back to an existing language.");
	const std::string first = g_pkDictionary->m_languages[0].m_type;
	return SetCurrentLanguage(first.c_str());
}

bool ShutdownDictionary()
{
	for (size_t i = 0; i < g_languages.size(); ++i)
		delete g_languages[i];
	g_languages.clear();
	delete g_pkDictionary;
	g_pkDictionary = NULL;
	return true;
}

const StringDictionary& GetStringDictionary()
{
	return *g_pkDictionary;
}

const Language& GetCurrentLanguage()
{
	return *Detail::CurrentLanguage();
}

bool SetCurrentLanguage(const char* szType)
{
	if (szType == NULL || g_pkDictionary == NULL)
		return false;
	size_t idx = g_current;
	bool bFound = false;
	for (size_t i = 0; i < g_languages.size(); ++i)
	{
		if (strcmp(g_languages[i]->m_szType, szType) == 0)
		{
			idx = i;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		const std::vector<Detail::DictLanguage>& langs = g_pkDictionary->m_languages;
		for (size_t i = 0; i < langs.size(); ++i)
		{
			if (langs[i].m_type == szType)
			{
				g_languages.push_back(new Detail::LanguageImpl(szType, Detail::CreatePluralityRule(langs[i].m_iPluralRule)));
				idx = g_languages.size() - 1;
				break;
			}
		}
	}
	// The dictionary preloads the new language's strings while the old language is still current (the closed
	// library sets the index afterwards): their plurality is checked against the old language's rule.
	if (g_pkDictionary->SetCurrentLanguage(szType))
	{
		g_current = idx;
		return true;
	}
	Detail::LogF(ILocalizationLogger::TYPE_ERROR, 0x100,
	             "Could not set language to %s.  No table exists for it in the dictionary.", szType);
	return false;
}

// Before InitializeDictionary the closed library dereferences a NULL dictionary (access violation); here the
// key comes back as a missing key would.
String Lookup(const char* szTextKey)
{
	if (g_pkDictionary == NULL)
		return String(szTextKey);
	return g_pkDictionary->Lookup(szTextKey);
}

// ------------------------------------------------------------------------------------------------- date helpers
namespace
{
String FormatDateTime(SYSTEMTIME& stUniversalTime, const char* szDate, const char* szTime, bool bDate, bool bTime)
{
	SYSTEMTIME local;
	if (!SystemTimeToTzSpecificLocalTime(NULL, &stUniversalTime, &local))
		local = stUniversalTime;
	std::string out;
	char buf[256];
	if (bDate)
	{
		const int n = GetDateFormatA(LOCALE_USER_DEFAULT, szDate != NULL ? 0 : DATE_SHORTDATE, &local, szDate, buf,
		                             sizeof(buf));
		if (n > 0)
			out.append(buf, (size_t)n - 1);
	}
	if (bTime)
	{
		if (!out.empty())
			out.push_back(' ');
		const int n = GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &local, szTime, buf, sizeof(buf));
		if (n > 0)
			out.append(buf, (size_t)n - 1);
	}
	return String(out.c_str(), (unsigned int)out.size());
}
} // namespace

String ConvertUTCDatetoLocalTZDate(SYSTEMTIME& stUniversalTime, const char* szDTFormat)
{
	return FormatDateTime(stUniversalTime, szDTFormat, NULL, true, false);
}

String ConvertUTCTimetoLocalTZTime(SYSTEMTIME& stUniversalTime, const char* szTimeFormat)
{
	return FormatDateTime(stUniversalTime, NULL, szTimeFormat, false, true);
}

String ConvertUTCDateTimetoLocalTZDateTime(SYSTEMTIME& stUniversalTime, const char* szDateFormat,
                                           const char* szTimeFormat)
{
	return FormatDateTime(stUniversalTime, szDateFormat, szTimeFormat, true, true);
}
} // namespace Localization
