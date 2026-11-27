// CvLocalization replacement (c5-64bit-patch issue 1701): declarations shared by the sources of this library.
// Written from the fork's CvLocalization/include/CvLocalization.h and the behaviour of the closed
// CvLocalizationWin32Final Release.dll (Ghidra decompile of a copy, SHA-256 9b783bc3...c50eac4, and the 1702 A/B).
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "CvGameDatabase.h"
// String and SingleFormString keep their data private in the header and declare no friends; this library is their
// implementation, so it sees the header with every member public. Only data members are private there, and MSVC
// mangling does not encode the access of data members other than statics (String::Empty is public anyway), so the
// exported names do not change. The STL headers are included above so the macro cannot reach them.
#define private public
#include "CvLocalization.h"
#undef private

namespace Localization
{
namespace Detail
{
// ------------------------------------------------------------------------------------------------- logging
// The closed library formats into fixed stack buffers with vsprintf_s (an over-long message ends the process
// through the invalid-parameter handler). Here the same buffer sizes, truncating instead.
void Log(ILocalizationLogger::LogTypes eType, const char* szMessage);
void LogF(ILocalizationLogger::LogTypes eType, size_t bufSize, const char* szFormat, ...);

// ------------------------------------------------------------------------------------------------- plural rules
// PluralRule k (1..16) of the Languages table; NULL for any other value (the closed FUN_10006a30).
IPluralityRule* CreatePluralityRule(int iRule);

// ------------------------------------------------------------------------------------------------- languages
// Localization::Language has no data members in the header, so every Language object this library hands out is a
// LanguageImpl; the exported members cast `this`.
class LanguageImpl : public Language
{
public:
	LanguageImpl(const char* szType, IPluralityRule* pkRule);
	virtual ~LanguageImpl();

	char m_szType[32];                // strncpy_s(.., 0x20, ..) as the closed library
	IPluralityRule* m_pkRule;         // owned
	ParameterTransforms* m_pkTransforms;  // never dereferenced (GetParameterTransforms only returns it)
	unsigned int m_formatters;        // formatters "created" (their ICU status logged once), see LocNumber.cpp
	std::string m_numberPattern;      // pattern left applied in the number formatter ("" = default), LocNumber.cpp
};

// The language a String without an explicit language uses: the current one; NULL before the first successful
// SetCurrentLanguage (the closed library reads past an empty vector there).
Language* CurrentLanguage();
IPluralityRule* RuleOf(const Language* pkLanguage);

// ------------------------------------------------------------------------------------------------- dictionary
struct DictLanguage : public StringDictionary::Language
{
	std::string m_type;
	std::string m_name;
	int m_iPluralRule;
};

class DictImpl : public StringDictionary
{
public:
	DictImpl(Database::Connection& db, const char* szLanguagesTable, bool bDisableFallback);
	~DictImpl();

	void Preload();
	bool QueryLookup(unsigned int idx, const char* szKey, unsigned int lenKey, String& out) const;
	bool QueryHasKey(unsigned int idx, const char* szKey, unsigned int lenKey) const;

	Database::Connection* m_db;
	std::vector<DictLanguage> m_languages;
	unsigned int m_current;
	bool m_bPreload;
	mutable CRITICAL_SECTION m_cs;
	mutable Database::Results m_lookup;  // the Text/Plurality/Gender query
	mutable Database::Results m_tags;    // SELECT DISTINCT Tag from LocalizedText
	std::unordered_map<std::string, String> m_cache;
};

DictImpl* Dictionary();

// ------------------------------------------------------------------------------------------------- composition
// Gender specifier "<gender>[:trait,trait...]" as the closed FUN_1000dff0: false on '|' or an unknown gender word
// (iGender untouched then); '*' alone = -1 (any).
bool ParseGenderSpec(const char* sz, size_t len, int& iGender, std::vector<std::string>& traits);

// Syntax check of one form (the closed parser FUN_1000cb60); logs the parse errors.
bool ParseForm(const char* sz, size_t len);

// Compose one form with the arguments; false (with the closed library's log messages) when a parameter fails.
// The result's plurality, gender and traits are those of the single-form result of a nested composition.
bool ComposeForm(const char* sz, size_t len, const std::vector<ParameterArgument>& args, std::string& out);

// ------------------------------------------------------------------------------------------------- numbers
// Argument types as the closed library numbers them: 1 int, 2 int64, 3 float, 4 double, 5 string.
struct NumArg
{
	int type;
	int i;
	long long i64;
	float f;
	double d;
};
bool FormatNumberArg(Language* pkLanguage, const NumArg& a, const std::string& style, std::string& out);
bool FormatPercentArg(Language* pkLanguage, const NumArg& a, std::string& out);
bool FormatCurrencyArg(Language* pkLanguage, const NumArg& a, std::string& out);
bool FormatSpelloutArg(Language* pkLanguage, const NumArg& a, std::string& out);
bool FormatOrdinalArg(Language* pkLanguage, const NumArg& a, std::string& out);
bool FormatRomanArg(Language* pkLanguage, const NumArg& a, std::string& out);
std::string CaseMap(Language* pkLanguage, const char* sz, size_t len, int mode);
} // namespace Detail
} // namespace Localization
