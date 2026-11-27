// CvLocalization replacement (c5-64bit-patch issue 1701): the numeric and case transforms without ICU.
//
// The closed library formats with ICU 4.4 (DecimalFormat for number/percent/currency, RuleBasedNumberFormat for
// spellout/ordinal/roman, UnicodeString case mapping). This file reproduces the ICU 4.4 results for what the game's
// text uses, measured with the 1702 A/B (README.md, "Number formatting"):
//  - a double is reduced to 15 significant digits first (ICU 4.4 DigitList::set: sprintf "%+1.14e"), a float is
//    widened to double; rounding to the pattern's maximum fraction digits is half-even on those decimal digits;
//  - DecimalFormat pattern syntax: prefix/suffix literals (quoted or not), '#', '0', ',', '.', ';' negative
//    subpattern, '%', '-'; "#.##" is read as "#0.##" (ICU's rule for patterns without a '0');
//  - an integer part without digits prints "0" unless a fraction is printed (ICU subformat);
//  - English spellout (%spellout-numbering), digits ordinal with superscript indicators, roman upper.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "LocInternal.h"

namespace Localization
{
namespace Detail
{
namespace
{
// ------------------------------------------------------------------------------------------------- symbols
struct Symbols
{
	const char* decimal;
	const char* group;
	const char* percentPattern;
	const char* currencyPattern;
	const char* currencySymbol;
};

Symbols SymbolsFor(const char* szType)
{
	static const char kNbsp[] = "\xC2\xA0";
	std::string t = szType != NULL ? szType : "";
	const std::string lang = t.substr(0, t.find('_'));
	if (lang == "de" || lang == "es" || lang == "it")
		return {",", ".", "#,##0\xC2\xA0%", "#,##0.00\xC2\xA0\xC2\xA4", "\xE2\x82\xAC"};
	if (lang == "fr")
		return {",", kNbsp, "#,##0\xC2\xA0%", "#,##0.00\xC2\xA0\xC2\xA4", "\xE2\x82\xAC"};
	if (lang == "ru" || lang == "pl")
		return {",", kNbsp, "#,##0\xC2\xA0%", "#,##0.00\xC2\xA0\xC2\xA4", lang == "ru" ? "RUB" : "PLN"};
	if (lang == "en")
		return {".", ",", "#,##0%", "\xC2\xA4#,##0.00;(\xC2\xA4#,##0.00)", "$"};
	return {".", ",", "#,##0%", "\xC2\xA4\xC2\xA0#,##0.00", "\xC2\xA4"};
}

// ------------------------------------------------------------------------------------------------- digit list
// value = 0.d1d2d3... x 10^decAt (ICU DigitList); d has no trailing zeros; empty d = zero.
struct Digits
{
	std::string d;
	int decAt = 0;
	bool neg = false;
};

void Trim(Digits& x)
{
	while (!x.d.empty() && x.d.back() == '0')
		x.d.pop_back();
	size_t lead = 0;
	while (lead < x.d.size() && x.d[lead] == '0')
		++lead;
	if (lead != 0)
	{
		x.d.erase(0, lead);
		x.decAt -= (int)lead;
	}
	if (x.d.empty())
		x.decAt = 0;
}

Digits FromInt64(long long v)
{
	Digits x;
	x.neg = v < 0;
	unsigned long long u = x.neg ? 0ULL - (unsigned long long)v : (unsigned long long)v;
	char buf[32];
	sprintf_s(buf, "%llu", u);
	x.d = buf;
	x.decAt = (int)x.d.size();
	Trim(x);
	return x;
}

// 15 significant digits, as ICU 4.4 DigitList::set(double).
Digits FromDouble(double v)
{
	Digits x;
	x.neg = v < 0.0 || (v == 0.0 && _copysign(1.0, v) < 0.0);
	char buf[64];
	sprintf_s(buf, "%+1.14e", v);
	// "+d.dddddddddddddde+XXX"
	const char* p = buf + 1;
	std::string digits;
	while (*p != 0 && *p != 'e')
	{
		if (*p >= '0' && *p <= '9')
			digits.push_back(*p);
		++p;
	}
	const int exp = *p == 'e' ? atoi(p + 1) : 0;
	x.d = digits;
	x.decAt = exp + 1;
	Trim(x);
	return x;
}

// Half-even rounding to maxFrac fraction digits.
void RoundFraction(Digits& x, int maxFrac)
{
	if (x.d.empty())
		return;
	const int keep = x.decAt + maxFrac;  // digits kept
	if (keep >= (int)x.d.size())
		return;
	if (keep < 0)
	{
		x.d.clear();
		x.decAt = 0;
		return;
	}
	const char next = x.d[(size_t)keep];
	bool up;
	if (next > '5')
		up = true;
	else if (next < '5')
		up = false;
	else if ((size_t)keep + 1 < x.d.size())
		up = true;  // more nonzero digits follow (no trailing zeros are stored)
	else
		up = keep > 0 && ((x.d[(size_t)keep - 1] - '0') & 1) != 0;
	x.d.resize((size_t)keep);
	if (up)
	{
		int i = keep - 1;
		while (i >= 0 && x.d[(size_t)i] == '9')
		{
			x.d[(size_t)i] = '0';
			--i;
		}
		if (i >= 0)
			x.d[(size_t)i]++;
		else
		{
			x.d.insert(x.d.begin(), '1');
			x.decAt++;
		}
	}
	Trim(x);
}

// ------------------------------------------------------------------------------------------------- patterns
struct Pattern
{
	std::string posPrefix, posSuffix, negPrefix, negSuffix;
	bool hasNegative = false;
	int minInt = 1, minFrac = 0, maxFrac = 3, grouping = 3;
	bool groupingUsed = true, decimalAlwaysShown = false;
	int multiplier = 1;
};

// One subpattern; false on an ICU syntax error.
bool ParseSubpattern(const std::string& p, size_t& i, std::string& prefix, std::string& suffix, Pattern& pat,
                     bool bNegative, const Symbols& sym)
{
	int phase = 0;  // 0 prefix, 1 number, 2 suffix
	int digitLeft = 0, zeroDigit = 0, digitRight = 0, groupingCount = -1, decimalPos = -1;
	std::string* affix = &prefix;
	for (; i < p.size(); ++i)
	{
		const char c = p[i];
		const bool numberChar = c == '#' || (c >= '0' && c <= '9') || c == ',' || c == '.';
		if (phase == 0 && numberChar)
			phase = 1;
		if (phase == 1)
		{
			if (c == '#')
			{
				if (zeroDigit > 0)
					++digitRight;
				else
					++digitLeft;
				if (groupingCount >= 0 && decimalPos < 0)
					++groupingCount;
				continue;
			}
			if (c >= '0' && c <= '9')
			{
				if (digitRight > 0)
					return false;
				++zeroDigit;
				if (groupingCount >= 0 && decimalPos < 0)
					++groupingCount;
				continue;
			}
			if (c == ',')
			{
				if (decimalPos >= 0)
					return false;
				groupingCount = 0;
				continue;
			}
			if (c == '.')
			{
				if (decimalPos >= 0)
					return false;
				decimalPos = digitLeft + zeroDigit + digitRight;
				continue;
			}
			phase = 2;
			affix = &suffix;
		}
		if (numberChar && phase == 2)
			return false;
		if (c == ';')
		{
			if (phase == 0 || bNegative)
				return false;
			++i;
			break;
		}
		if (c == '\'')
		{
			if (i + 1 < p.size() && p[i + 1] == '\'')
			{
				affix->push_back('\'');
				++i;
				continue;
			}
			size_t j = i + 1;
			while (j < p.size())
			{
				if (p[j] == '\'')
				{
					if (j + 1 < p.size() && p[j + 1] == '\'')
					{
						affix->push_back('\'');
						j += 2;
						continue;
					}
					break;
				}
				affix->push_back(p[j]);
				++j;
			}
			if (j >= p.size())
				return false;  // a closing quote is missing
			i = j;
			continue;
		}
		if (c == '%')
		{
			pat.multiplier = 100;
			affix->push_back('%');
			continue;
		}
		if ((unsigned char)c == 0xC2 && i + 1 < p.size() && (unsigned char)p[i + 1] == 0xA4)
		{
			affix->append(sym.currencySymbol);
			++i;
			continue;
		}
		if (c == '-')
		{
			affix->push_back('-');
			continue;
		}
		affix->push_back(c);
	}
	if (bNegative)
		return true;
	if (zeroDigit == 0 && digitLeft > 0 && decimalPos >= 0)
	{
		int n = decimalPos;
		if (n == 0)
			++n;
		digitRight = digitLeft - n;
		digitLeft = n - 1;
		zeroDigit = 1;
	}
	if ((decimalPos < 0 && digitRight > 0) ||
	    (decimalPos >= 0 && (decimalPos < digitLeft || decimalPos > digitLeft + zeroDigit)) || groupingCount == 0)
		return false;
	const int total = digitLeft + zeroDigit + digitRight;
	const int effDec = decimalPos >= 0 ? decimalPos : total;
	pat.minInt = effDec - digitLeft;
	pat.maxFrac = decimalPos >= 0 ? total - decimalPos : 0;
	pat.minFrac = decimalPos >= 0 ? digitLeft + zeroDigit - decimalPos : 0;
	pat.groupingUsed = groupingCount > 0;
	pat.grouping = groupingCount > 0 ? groupingCount : 0;
	pat.decimalAlwaysShown = decimalPos == 0 || (decimalPos >= 0 && decimalPos == total);
	return true;
}

bool ParsePattern(const std::string& p, const Symbols& sym, Pattern& pat)
{
	size_t i = 0;
	if (!ParseSubpattern(p, i, pat.posPrefix, pat.posSuffix, pat, false, sym))
		return false;
	if (i < p.size() || (i > 0 && p[i - 1] == ';'))
	{
		pat.hasNegative = true;
		Pattern dummy;
		if (!ParseSubpattern(p, i, pat.negPrefix, pat.negSuffix, dummy, true, sym))
			return false;
	}
	if (!pat.hasNegative)
	{
		pat.negPrefix = "-" + pat.posPrefix;
		pat.negSuffix = pat.posSuffix;
	}
	return true;
}

std::string Subformat(const Digits& x, const Pattern& pat, const Symbols& sym, bool bInteger)
{
	std::string intPart;
	if (x.decAt > 0)
	{
		for (int i = 0; i < x.decAt; ++i)
			intPart.push_back(i < (int)x.d.size() ? x.d[(size_t)i] : '0');
	}
	while ((int)intPart.size() < pat.minInt)
		intPart.insert(intPart.begin(), '0');
	std::string frac;
	if (!bInteger)
	{
		for (int i = x.decAt; i < (int)x.d.size(); ++i)
			frac.push_back(i < 0 ? '0' : x.d[(size_t)i]);
	}
	while ((int)frac.size() < pat.minFrac)
		frac.push_back('0');
	const bool fractionPresent = !frac.empty();
	if (intPart.empty() && !fractionPresent)
		intPart = "0";
	std::string out = x.neg ? pat.negPrefix : pat.posPrefix;
	for (size_t i = 0; i < intPart.size(); ++i)
	{
		out.push_back(intPart[i]);
		const size_t left = intPart.size() - 1 - i;
		if (pat.groupingUsed && pat.grouping > 0 && left > 0 && left % (size_t)pat.grouping == 0)
			out.append(sym.group);
	}
	if (fractionPresent || pat.decimalAlwaysShown)
		out.append(sym.decimal);
	out.append(frac);
	out.append(x.neg ? pat.negSuffix : pat.posSuffix);
	return out;
}

std::string FormatWith(const NumArg& a, const Pattern& pat, const Symbols& sym)
{
	Digits x;
	bool bInteger = false;
	switch (a.type)
	{
	case 1: x = FromInt64(a.i); bInteger = true; break;
	case 2: x = FromInt64(a.i64); bInteger = true; break;
	case 3: x = FromDouble((double)a.f); break;
	default: x = FromDouble(a.d); break;
	}
	if (pat.multiplier != 1 && !x.d.empty())
		x.decAt += 2;  // x100 (percent)
	if (!bInteger)
		RoundFraction(x, pat.maxFrac);
	return Subformat(x, pat, sym, bInteger);
}

// ------------------------------------------------------------------------------------------------- spellout
const char* const kOnes[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                             "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
                             "eighteen", "nineteen"};
const char* const kTens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

std::string Grouped(unsigned long long v)
{
	Pattern pat;
	pat.minFrac = pat.maxFrac = 0;
	return Subformat(FromInt64((long long)v), pat, SymbolsFor("en_US"), true);
}

std::string SpellInt(unsigned long long n)
{
	if (n < 20)
		return kOnes[n];
	if (n < 100)
		return std::string(kTens[n / 10]) + (n % 10 != 0 ? std::string("-") + kOnes[n % 10] : std::string());
	if (n < 1000)
		return SpellInt(n / 100) + " hundred" + (n % 100 != 0 ? " " + SpellInt(n % 100) : std::string());
	static const struct
	{
		unsigned long long v;
		const char* name;
	} kScale[] = {{1000000000000000ULL, " quadrillion"}, {1000000000000ULL, " trillion"}, {1000000000ULL, " billion"},
	              {1000000ULL, " million"}, {1000ULL, " thousand"}};
	if (n >= 1000000000000000000ULL)
		return Grouped(n);
	for (size_t i = 0; i < sizeof(kScale) / sizeof(kScale[0]); ++i)
		if (n >= kScale[i].v)
			return SpellInt(n / kScale[i].v) + kScale[i].name +
			       (n % kScale[i].v != 0 ? " " + SpellInt(n % kScale[i].v) : std::string());
	return std::string();
}

std::string SpellDigits(const Digits& x)
{
	std::string out = x.neg && !x.d.empty() ? "minus " : (x.neg ? "minus " : "");
	unsigned long long intPart = 0;
	bool bBig = false;
	for (int i = 0; i < x.decAt; ++i)
	{
		const int dgt = i < (int)x.d.size() ? x.d[(size_t)i] - '0' : 0;
		if (intPart > 100000000000000000ULL)
			bBig = true;
		intPart = intPart * 10 + (unsigned long long)dgt;
	}
	if (bBig)
	{
		Pattern pat;
		pat.minFrac = pat.maxFrac = 0;
		Digits ip = x;
		ip.neg = false;
		if ((int)ip.d.size() > ip.decAt)
			ip.d.resize((size_t)ip.decAt);
		out += Subformat(ip, pat, SymbolsFor("en_US"), true);
	}
	else
		out += SpellInt(intPart);
	std::string frac;
	for (int i = x.decAt; i < (int)x.d.size(); ++i)
		frac.push_back(i < 0 ? '0' : x.d[(size_t)i]);
	if (!frac.empty())
	{
		out += " point";
		for (size_t i = 0; i < frac.size(); ++i)
		{
			out += " ";
			out += kOnes[frac[i] - '0'];
		}
	}
	return out;
}

Digits ToDigits(const NumArg& a)
{
	switch (a.type)
	{
	case 1: return FromInt64(a.i);
	case 2: return FromInt64(a.i64);
	case 3: return FromDouble((double)a.f);
	default: return FromDouble(a.d);
	}
}

const char* OrdinalSuffix(unsigned long long n)
{
	const unsigned long long r = n % 100;
	if (r >= 10 && r < 20)
		return "\xE1\xB5\x97\xCA\xB0";  // th
	switch (n % 10)
	{
	case 1: return "\xCB\xA2\xE1\xB5\x97";  // st
	case 2: return "\xE2\x81\xBF\xE1\xB5\x88";  // nd
	case 3: return "\xCA\xB3\xE1\xB5\x88";  // rd
	default: return "\xE1\xB5\x97\xCA\xB0";  // th
	}
}

std::string Roman(unsigned long long n)
{
	if (n == 0)
		return "N";
	if (n >= 5000)
		return Grouped(n);
	static const char* const kM[] = {"", "M", "MM", "MMM", "MMMM"};
	static const char* const kC[] = {"", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"};
	static const char* const kX[] = {"", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"};
	static const char* const kI[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
	return std::string(kM[n / 1000]) + kC[(n / 100) % 10] + kX[(n / 10) % 10] + kI[n % 10];
}

// The ICU status each formatter's creation reports, logged once per language (closed FUN_10006410..10006750).
enum Formatter
{
	F_CURRENCY,
	F_PERCENT,
	F_NUMBER,
	F_SPELLOUT,
	F_ORDINAL,
	F_ROMAN,
	F_COUNT
};

void NoteCreated(Language* pkLanguage, Formatter f)
{
	static unsigned int s_noLanguage = 0;
	unsigned int& mask = pkLanguage != NULL ? static_cast<LanguageImpl*>(pkLanguage)->m_formatters : s_noLanguage;
	if (mask & (1u << f))
		return;
	mask |= 1u << f;
	static const char kRoot[] = "A resource bundle lookup returned a result from the root locale (not an error).";
	static const char kFallback[] = "ICU: A resource bundle lookup returned a fallback result (not an error)";
	switch (f)
	{
	case F_SPELLOUT:
	case F_ORDINAL:
		Log(ILocalizationLogger::TYPE_WARNING, kFallback);
		break;
	case F_NUMBER:
	case F_ROMAN:
		Log(ILocalizationLogger::TYPE_WARNING, kRoot);
		break;
	default:  // currency, percent: U_ZERO_ERROR, nothing logged
		break;
	}
}

const char* TypeOf(Language* pkLanguage)
{
	return pkLanguage != NULL ? pkLanguage->GetType() : "";
}
} // namespace

// The number formatter of a language keeps the pattern last applied when a format fails after applying it (a
// string argument with a style: the closed FormatNumber returns before restoring the default). Later formatting
// without a style (a plain {1}, "{1:number}") then uses that pattern until the next successful styled format
// restores the default. Reproduced with LanguageImpl::m_numberPattern ("" = the default).
bool FormatNumberArg(Language* pkLanguage, const NumArg& a, const std::string& style, std::string& out)
{
	NoteCreated(pkLanguage, F_NUMBER);
	const Symbols sym = SymbolsFor(TypeOf(pkLanguage));
	static std::string s_noLanguagePattern;
	std::string& current =
		pkLanguage != NULL ? static_cast<LanguageImpl*>(pkLanguage)->m_numberPattern : s_noLanguagePattern;
	if (!style.empty())
	{
		std::string p;
		bool bApply = true;
		const size_t q1 = style.find('"');
		if (q1 == std::string::npos)
			p = style;
		else
		{
			// substr(first + 1, last - 1): the count is last - 1, not last - first - 1 (closed), so when the first
			// quote is not the first character the pattern runs past the closing quote (a backslash-quote pair
			// around it keeps the trailing pair as a suffix). One quote only: the count is first - 1 or npos.
			const size_t q2 = style.rfind('"');
			p = style.substr(q1 + 1, q2 - 1);
			bApply = !p.empty();
		}
		if (bApply)
		{
			Pattern custom;
			if (!ParsePattern(p, sym, custom))
				return false;
			current = p;
		}
	}
	if (a.type == 5)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "FormatNumber: Unsupported, must be numeric argument");
		return false;
	}
	Pattern pat;
	if (current.empty() || !ParsePattern(current, sym, pat))
	{
		pat = Pattern();
		ParsePattern("#,##0.###", sym, pat);
	}
	out = FormatWith(a, pat, sym);
	if (!style.empty())
		current.clear();
	return true;
}

bool FormatPercentArg(Language* pkLanguage, const NumArg& a, std::string& out)
{
	NoteCreated(pkLanguage, F_PERCENT);
	if (a.type == 5)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "FormatPercent: Unsupported, must be numer argument");
		return false;
	}
	const Symbols sym = SymbolsFor(TypeOf(pkLanguage));
	Pattern pat;
	ParsePattern(sym.percentPattern, sym, pat);
	out = FormatWith(a, pat, sym);
	return true;
}

bool FormatCurrencyArg(Language* pkLanguage, const NumArg& a, std::string& out)
{
	NoteCreated(pkLanguage, F_CURRENCY);
	if (a.type == 5)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "FormatCurrency: Unsupported, must be number argument");
		return false;
	}
	const Symbols sym = SymbolsFor(TypeOf(pkLanguage));
	Pattern pat;
	ParsePattern(sym.currencyPattern, sym, pat);
	out = FormatWith(a, pat, sym);
	return true;
}

bool FormatSpelloutArg(Language* pkLanguage, const NumArg& a, std::string& out)
{
	NoteCreated(pkLanguage, F_SPELLOUT);
	if (a.type == 5)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "FormatSpellout: Unsupported, must be number argument");
		return false;
	}
	out = SpellDigits(ToDigits(a));
	return true;
}

bool FormatOrdinalArg(Language* pkLanguage, const NumArg& a, std::string& out)
{
	NoteCreated(pkLanguage, F_ORDINAL);
	if (a.type == 5)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "FormatOrdinal: Unsupported, must be number argument");
		return false;
	}
	Digits x = ToDigits(a);
	RoundFraction(x, 0);
	unsigned long long n = 0;
	for (int i = 0; i < x.decAt && i < 19; ++i)
		n = n * 10 + (unsigned long long)(i < (int)x.d.size() ? x.d[(size_t)i] - '0' : 0);
	out = std::string(x.neg && n != 0 ? "\xE2\x88\x92" : "") + Grouped(n) + OrdinalSuffix(n);
	return true;
}

bool FormatRomanArg(Language* pkLanguage, const NumArg& a, std::string& out)
{
	NoteCreated(pkLanguage, F_ROMAN);
	if (a.type == 5)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "FormatRoman: Unsupported, must be numer argument");
		return false;
	}
	Digits x = ToDigits(a);
	RoundFraction(x, 0);
	unsigned long long n = 0;
	for (int i = 0; i < x.decAt && i < 19; ++i)
		n = n * 10 + (unsigned long long)(i < (int)x.d.size() ? x.d[(size_t)i] - '0' : 0);
	out = std::string(x.neg && n != 0 ? "\xE2\x88\x92" : "") + Roman(n);
	return true;
}

// Upper (mode 1) / lower (mode 0) case through Windows' linguistic casing for the language; title case (mode 2)
// gives "" as the closed library (its ICU data has no break iterator rules, the titlecasing returns nothing).
std::string CaseMap(Language* pkLanguage, const char* sz, size_t len, int mode)
{
	if (mode == 2 || sz == NULL || len == 0)
		return std::string();
	const int wn = MultiByteToWideChar(CP_UTF8, 0, sz, (int)len, NULL, 0);
	if (wn <= 0)
		return std::string();
	std::wstring w((size_t)wn, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, sz, (int)len, &w[0], wn);
	wchar_t wszLocale[LOCALE_NAME_MAX_LENGTH] = {};
	const char* szType = TypeOf(pkLanguage);
	for (int i = 0; szType[i] != 0 && i < LOCALE_NAME_MAX_LENGTH - 1; ++i)
		wszLocale[i] = (wchar_t)(szType[i] == '_' ? '-' : szType[i]);
	const DWORD flags = (mode == 1 ? LCMAP_UPPERCASE : LCMAP_LOWERCASE) | LCMAP_LINGUISTIC_CASING;
	const wchar_t* loc = IsValidLocaleName(wszLocale) ? wszLocale : LOCALE_NAME_INVARIANT;
	int mn = LCMapStringEx(loc, flags, w.c_str(), wn, NULL, 0, NULL, NULL, 0);
	if (mn <= 0)
		return std::string(sz, len);
	std::wstring m((size_t)mn, L'\0');
	LCMapStringEx(loc, flags, w.c_str(), wn, &m[0], mn, NULL, NULL, 0);
	const int un = WideCharToMultiByte(CP_UTF8, 0, m.c_str(), mn, NULL, 0, NULL, NULL);
	std::string out((size_t)(un > 0 ? un : 0), '\0');
	if (un > 0)
		WideCharToMultiByte(CP_UTF8, 0, m.c_str(), mn, &out[0], un, NULL, NULL);
	return out;
}
} // namespace Detail
} // namespace Localization
