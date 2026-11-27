// CvLocalization replacement (c5-64bit-patch issue 1701): the {..} parameter parser and the composer.
//
// Grammar and evaluation as the closed library (decompile: parser FUN_1000cb60/1000ccc0 and helpers
// 0x1000c9b0..0x1000e3d0; evaluation FUN_100025c0/100028a0/10002b20/10003250/10003490; choice transforms
// FUN_10001040), including its messages, their buffer sizes and the order in which they are logged:
//
//   form      := text with parameters
//   parameter := '{' ws ['@'] (index [ident] | ident) ws (form-spec | gender-spec/plural-spec) ws
//                ('<<' ws parameter)* ws [':' ident style-args] '}'
//   form-spec := '[' ws value ws ']'      gender-spec := '^' ws value      plural-spec := '*' ws value
//   value     := parameter | 'quoted literal' | number
//
// An index parameter takes the argument (a string argument that is a key of the dictionary is replaced by its
// text); an identifier parameter is a dictionary lookup. Injected parameters (<<) replace the first arguments of
// the nested composition. Style args of a transform run to the matching '}'. See ../README.md for every rule.
#include <string.h>

#include "LocInternal.h"

namespace Localization
{
namespace Detail
{
namespace
{
typedef decltype(ParameterArgument::m_str) StrPtr;

// An argument in the closed library's numbering (1 int, 2 int64, 3 float, 4 double, 5 string) plus the form of a
// string argument to output (closed ParameterArgument +0x10).
struct Arg
{
	int type = 0;
	int i = 0;
	long long i64 = 0;
	float f = 0;
	double d = 0;
	StrPtr s;
	int form = 1;
};

Arg StringArg(const String& s)
{
	Arg a;
	a.type = 5;
	a.s = StrPtr(new String(s));
	return a;
}

Arg FromParameterArgument(const ParameterArgument& p)
{
	Arg a;
	a.type = (int)p.m_type + 1;
	switch (p.m_type)
	{
	case ParameterArgument::ARGTYPE_INT: a.i = p.m_value.i; a.i64 = p.m_value.i; break;
	case ParameterArgument::ARGTYPE_INT64: a.i64 = p.m_value.i64; a.i = (int)p.m_value.i64; break;
	case ParameterArgument::ARGTYPE_FLOAT: a.f = p.m_value.f; break;
	case ParameterArgument::ARGTYPE_DOUBLE: a.d = p.m_value.d; break;
	default: a.s = p.m_str; break;
	}
	return a;
}

ParameterArgument ToParameterArgument(const Arg& a)
{
	switch (a.type)
	{
	case 1: return ParameterArgument(a.i);
	case 2: return ParameterArgument((__int64)a.i64);
	case 3: return ParameterArgument(a.f);
	case 4: return ParameterArgument(a.d);
	default:
	{
		ParameterArgument p(0);
		p.m_type = ParameterArgument::ARGTYPE_STRING;
		p.m_str = a.s;
		return p;
	}
	}
}

NumArg ToNumArg(const Arg& a)
{
	NumArg n;
	n.type = a.type;
	n.i = a.i;
	n.i64 = a.i64;
	n.f = a.f;
	n.d = a.d;
	return n;
}

struct Param;
typedef std::shared_ptr<Param> ParamPtr;

struct Spec
{
	bool hasLit = false;
	Arg lit;
	ParamPtr param;
	bool Present() const { return hasLit || param != nullptr; }
};

struct Param
{
	int index = 0;
	size_t keyStart = 0, keyEnd = 0;
	size_t start = 0, end = 0;
	size_t tStart = 0, tEnd = 0;
	bool hasTransform = false;
	bool at = false;
	Spec form, gender, plural;
	std::vector<Param> injected;
};

// The result of evaluating a parameter (the closed library's SingleFormString result).
struct Result
{
	std::string text;
	size_t plurality = 1;
	int gender = GENDER_MASCULINE;
	std::vector<std::string> traits;
};

struct Ctx
{
	const char* s;
	size_t len;
	char at(size_t i) const { return i <= len ? s[i] : 0; }
};

int g_depth = 0;  // nesting of compositions (the closed library overflows the stack on a cyclic key)
const int kMaxDepth = 64;

bool IsWs(char c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

bool IsIdent(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

void ErrForm(const Ctx& c, const char* fmt)
{
	LogF(ILocalizationLogger::TYPE_ERROR, 0x200, fmt, (int)c.len, c.s);
}

void Eos(const Ctx& c)
{
	ErrForm(c, "End of string met while parsing. %.*s");
}

// Advance one and skip blanks (closed FUN_1000d500).
bool Advance(const Ctx& c, size_t& pos)
{
	size_t p = pos + 1;
	if (p < c.len)
	{
		while (p < c.len - 1 && IsWs(c.s[p]))
			++p;
		pos = p;
		return true;
	}
	Eos(c);
	return false;
}

// Skip blanks at the current position (closed FUN_1000d450).
void SkipWs(const Ctx& c, size_t& pos)
{
	if (pos < c.len)
	{
		while (pos < c.len - 1 && IsWs(c.s[pos]))
			++pos;
	}
}

// Identifier [A-Za-z0-9_]+; pos ends on its last character (closed FUN_1000cfe0).
bool Ident(const Ctx& c, size_t& pos, size_t& start, size_t& end)
{
	if (!IsIdent(c.at(pos)))
		return false;
	start = pos;
	end = pos;
	while (pos + 1 < c.len && IsIdent(c.s[pos + 1]))
		++pos;
	end = pos;
	return true;
}

// Scan over characters of a class; pos ends on the last one (closed FUN_1000d040 / FUN_1000d240).
size_t ScanToken(const Ctx& c, size_t& pos, bool bNumberChars)
{
	const size_t st = pos;
	if (pos < c.len)
	{
		for (;;)
		{
			const char ch = c.s[pos];
			const bool ok = (ch >= '0' && ch <= '9') || (bNumberChars && (ch == '.' || ch == '-'));
			if (!ok)
			{
				--pos;
				break;
			}
			if (c.len - 1 <= pos)
				break;
			++pos;
		}
	}
	return pos + 1 - st;
}

// 1 NULL/empty, 2 not a number, 3 integer, 4 decimal (closed FUN_1000cad0).
int Classify(const char* t, size_t n)
{
	if (t == NULL || n == 0 || strncmp(t, "NULL", 4) == 0)
		return 1;
	bool dot = false, digit = false;
	for (size_t i = 0; i < n; ++i)
	{
		const char ch = t[i];
		if (i == 0 && ch == '-')
			return 2;
		if (ch >= '0' && ch <= '9')
			digit = true;
		else
		{
			if (ch != '.' || dot)
				return 2;
			dot = true;
		}
	}
	if (dot)
		return 4;
	return digit ? 3 : 0;
}

int ParseIndex(const Ctx& c, size_t& pos, int& index)
{
	const size_t st = pos;
	const size_t n = ScanToken(c, pos, false);
	if (Classify(c.s + st, n) != 3)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Expected integer greater than 0.");
		return -1;
	}
	if (n + 1 >= 0x10)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Numbers should be under 16 digits long.");
		return -1;
	}
	index = atoi(std::string(c.s + st, n).c_str());
	return 1;
}

int ParseNumberLiteral(const Ctx& c, size_t& pos, Arg& out)
{
	const size_t st = pos;
	const size_t n = ScanToken(c, pos, true);
	const int k = Classify(c.s + st, n);
	if (k != 3 && k != 4)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Expected number.");
		return -1;
	}
	if (n + 1 > 0xf)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Numbers should be under 16 digits long.");
		return -1;
	}
	const std::string t(c.s + st, n);
	out = Arg();
	if (k == 3)
	{
		out.type = 1;
		out.i = atoi(t.c_str());
		out.i64 = out.i;
	}
	else
	{
		out.type = 3;
		out.f = (float)atof(t.c_str());
	}
	return 1;
}

int ParseQuoted(const Ctx& c, size_t& pos, Arg& out)
{
	if (c.at(pos) != '\'')
		return 0;
	if (c.len < pos + 1)
	{
		Eos(c);
		return -1;
	}
	++pos;
	const size_t st = pos;
	while (c.at(pos) != '\'')
	{
		if (c.len < pos + 1)
		{
			Eos(c);
			return -1;
		}
		++pos;
	}
	out = StringArg(String(c.s + st, (unsigned int)(pos - st)));
	return 1;
}

int ParseParam(const Ctx& c, size_t& pos, Param& p);

int ParseSpec(const Ctx& c, size_t& pos, char marker, const char* errValue, Spec& out)
{
	if (c.at(pos) != marker)
		return 0;
	if (!Advance(c, pos))
		return -1;
	const char ch = c.at(pos);
	if (ch == '{')
	{
		out.param = std::make_shared<Param>();
		if (ParseParam(c, pos, *out.param) == -1)
			return -1;
	}
	else if (ch == '\'')
	{
		if (ParseQuoted(c, pos, out.lit) == -1)
			return -1;
		out.hasLit = true;
	}
	else if (ch >= '0' && ch <= '9')
	{
		if (ParseNumberLiteral(c, pos, out.lit) == -1)
			return -1;
		out.hasLit = true;
	}
	else
	{
		Log(ILocalizationLogger::TYPE_ERROR, errValue);
		return -1;
	}
	if (marker == '[')
	{
		if (!Advance(c, pos))
			return -1;
		if (c.at(pos) != ']')
		{
			ErrForm(c, "Expected ']'. %.*s");
			return -1;
		}
	}
	return 1;
}

const char kErrGender[] = "Expected number, literal, or parameter for gender form specifier.";
const char kErrPlural[] = "Expected number, literal, or parameter for plural form specifier.";
const char kErrForm[] = "Expected number, literal, or parameter for form specifier.";

int ParseGenderPlural(const Ctx& c, size_t& pos, Param& p)
{
	int r = ParseSpec(c, pos, '^', kErrGender, p.gender);
	if (r == -1)
		return -1;
	if (r == 1)
	{
		if (!Advance(c, pos))
			return -1;
		const int r2 = ParseSpec(c, pos, '*', kErrPlural, p.plural);
		if (r2 == -1)
			return -1;
		if (r2 != 1)
			return 1;
	}
	else
	{
		const int r2 = ParseSpec(c, pos, '*', kErrPlural, p.plural);
		if (r2 == -1)
			return -1;
		if (r2 == 0)
			return 0;
		if (!Advance(c, pos))
			return -1;
		const int r3 = ParseSpec(c, pos, '^', kErrGender, p.gender);
		if (r3 == -1)
			return -1;
		if (r3 != 1)
			return 1;
	}
	if (pos + 1 <= c.len)
	{
		++pos;
		return 1;
	}
	Eos(c);
	return -1;
}

int ParseInjection(const Ctx& c, size_t& pos, Param& p)
{
	if (c.at(pos) != '<')
		return 0;
	if (c.len < pos + 1)
	{
		Eos(c);
		return -1;
	}
	++pos;
	if (c.at(pos) != '<')
	{
		ErrForm(c, "Expected '<<' got '<'. %.*s");
		return -1;
	}
	if (!Advance(c, pos))
		return -1;
	const char ch = c.at(pos);
	if (ch == '{')
	{
		Param ip;
		if (ParseParam(c, pos, ip) == 1)
		{
			p.injected.push_back(ip);
			if (pos == c.len)
				return 1;
			if (!Advance(c, pos))
				return -1;
			if (ParseInjection(c, pos, p) == -1)
				return -1;
			return 1;
		}
		ErrForm(c, "Failed to parse injected parameter. %.*s");
	}
	else if (ch == '\'')
		ErrForm(c, "Injected literals are not supported in this release. %.*s");
	else if (ch < '0' || ch > '9')
		ErrForm(c, "Expected <Parameter>, literal, or number. %.*s");
	else
		ErrForm(c, "Injected numbers are not supported in this release. %.*s");
	return -1;
}

int ParseTransform(const Ctx& c, size_t& pos, Param& p)
{
	if (c.at(pos) != ':')
		return 0;
	if (!Advance(c, pos))
		return -1;
	if (!Ident(c, pos, p.tStart, p.tEnd))
	{
		ErrForm(c, "Expected transform. %.*s");
		return -1;
	}
	p.hasTransform = true;
	int depth = 1;
	do
	{
		if (c.len < pos + 1)
		{
			Eos(c);
			return -1;
		}
		++pos;
		if (c.at(pos) == '{')
			++depth;
		if (c.at(pos) == '}')
			--depth;
	} while (depth > 0);
	return 1;
}

int ParseParam(const Ctx& c, size_t& pos, Param& p)
{
	const size_t start = pos;
	if (c.at(pos) != '{')
		return 0;
	if (!Advance(c, pos))
		return -1;
	char ch = c.at(pos);
	if (ch == '@')
	{
		p.at = true;
		if (c.len < pos + 1)
		{
			Eos(c);
			return -1;
		}
		++pos;
		ch = c.at(pos);
	}
	if (ch >= '1' && ch <= '9')
	{
		if (ParseIndex(c, pos, p.index) != 1)
			return -1;
		if (!Advance(c, pos))
			return -1;
		size_t a = 0, b = 0;
		if (Ident(c, pos, a, b))
		{
			if (c.len < pos + 1)
			{
				Eos(c);
				return -1;
			}
			++pos;
		}
	}
	else if (ch == '0')
	{
		ErrForm(c, "Parameters start with index 1 and not 0. %.*s");
		return -1;
	}
	else
	{
		if (!Ident(c, pos, p.keyStart, p.keyEnd))
		{
			ErrForm(c, "Parameter must start with index or identifier. %.*s");
			return -1;
		}
		if (pos + 1 > c.len)
		{
			Eos(c);
			return -1;
		}
		++pos;
	}
	SkipWs(c, pos);
	if (c.at(pos) == '[')
	{
		if (ParseSpec(c, pos, '[', kErrForm, p.form) == -1)
			return -1;
		if (c.len < pos + 1)
		{
			Eos(c);
			return -1;
		}
		++pos;
	}
	else if (ParseGenderPlural(c, pos, p) == -1)
		return -1;
	SkipWs(c, pos);
	if (ParseInjection(c, pos, p) == -1)
		return -1;
	SkipWs(c, pos);
	ch = c.at(pos);
	if (ch != ':' && ch != '}')
	{
		ErrForm(c, "Expected ':' or '}'. %.*s");
		return -1;
	}
	if (ParseTransform(c, pos, p) == -1)
		return -1;
	if (c.at(pos) != '}')
		return -1;
	p.start = start;
	p.end = pos;
	return 1;
}

// The whole form (closed FUN_1000cb60).
bool ParseAll(const Ctx& c, std::vector<Param>* params)
{
	size_t i = 0;
	while (i < c.len)
	{
		if (c.s[i] == '{')
		{
			Param p;
			size_t pos = i;
			const int r = ParseParam(c, pos, p);
			if (r == -1)
				return false;
			if (r == 1 && params != NULL)
				params->push_back(p);
			i = pos;
			if (r == 0)
				++i;
		}
		else
			++i;
	}
	return true;
}

// ------------------------------------------------------------------------------------------------- evaluation
bool Evaluate(const Ctx& c, const Param& p, const std::vector<Arg>& args, Result& r);

String DictLookup(const char* sz, size_t len)
{
	DictImpl* d = Dictionary();
	if (d == NULL)
		return String(sz, (unsigned int)len);
	return d->LookupLanguage(sz, (unsigned int)len, d->GetCurrentLanguageIndex());
}

bool DictTryLookup(const char* sz, size_t len, String& out)
{
	DictImpl* d = Dictionary();
	return d != NULL && d->TryLookupLanguage(sz, (unsigned int)len, d->GetCurrentLanguageIndex(), out);
}

// A String holding a parameter result as its single form (closed FUN_10008a60): the text is not split at '|'.
String StringFromResult(const Result& r)
{
	String s(r.text.c_str(), (unsigned int)r.text.size());
	if (!r.text.empty())
	{
		s.m_Forms.clear();
		SingleFormString form(s.m_BaseString);
		form.m_Plurality = r.plurality;
		form.m_Gender = r.gender;
		form.m_Traits = r.traits;
		s.m_Forms.push_back(form);
	}
	return s;
}

// Composes a copy of a String with extra arguments, form nForm.
std::string ComposeCopy(const String& s, const std::vector<Arg>& args, int nForm, const SingleFormString** ppForm,
                        String& copy)
{
	copy = s;
	for (size_t i = 0; i < args.size(); ++i)
		copy.m_arguments.push_back(ToParameterArgument(args[i]));
	size_t bytes = 0;
	const char* sz = copy.toUTF8(bytes, nForm);
	if (ppForm != NULL)
		*ppForm = (size_t)(nForm - 1) < copy.m_Forms.size() ? &copy.m_Forms[(size_t)(nForm - 1)] : NULL;
	return std::string(sz, bytes);
}

bool IsSimple(const Param& p)
{
	return p.index >= 1 && !p.at && !p.hasTransform && p.injected.empty() && !p.form.Present() &&
	       !p.gender.Present() && !p.plural.Present();
}

// The argument list of a parameter: injected values first, then the outer arguments after them (FUN_10002b20).
bool BuildArgs(const Ctx& c, const Param& p, const std::vector<Arg>& args, std::vector<Arg>& out)
{
	out.clear();
	for (size_t k = 0; k < p.injected.size(); ++k)
	{
		const Param& ip = p.injected[k];
		if (!IsSimple(ip))
		{
			Result r;
			if (!Evaluate(c, ip, args, r))
			{
				Log(ILocalizationLogger::TYPE_ERROR, "Failed to evaluated injected parameter.");
				return false;
			}
			out.push_back(StringArg(StringFromResult(r)));
		}
		else
		{
			// The closed library reads the outer argument without a bounds check (access violation when it is
			// missing); here a missing one is reported as at the top level.
			if ((size_t)(ip.index - 1) >= args.size())
			{
				LogF(ILocalizationLogger::TYPE_ERROR, 0x400, "Missing argument %i. (%.*s)", ip.index, (int)c.len, c.s);
				Log(ILocalizationLogger::TYPE_ERROR, "Failed to evaluated injected parameter.");
				return false;
			}
			out.push_back(args[(size_t)(ip.index - 1)]);
		}
	}
	for (size_t k = p.injected.size(); k < args.size(); ++k)
		out.push_back(args[k]);
	return true;
}

int IntOf(const Arg& a)
{
	switch (a.type)
	{
	case 1: return a.i;
	case 2: return (int)a.i64;
	case 3: return (int)a.f;
	case 4: return (int)a.d;
	default: return 0;
	}
}

// [..] (closed FUN_10002dc0): 1 with the form, 0 none, -1 error.
int ExplicitForm(const Param& p, const std::vector<Arg>& outer, int& form)
{
	if (p.form.hasLit)
	{
		if (p.form.lit.type != 1)
		{
			Log(ILocalizationLogger::TYPE_ERROR, "Form Specifier must evaluate to an integer value.");
			return -1;
		}
		form = p.form.lit.i;
		return 1;
	}
	if (!p.form.param)
		return 0;
	const int idx = p.form.param->index;
	if (idx < 1)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Lookup parameters are not supported as explicit form specifiers.");
		return -1;
	}
	if ((size_t)(idx - 1) >= outer.size())
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Explicit Form Parameter specifies argument index that doesn't exist.");
		return -1;
	}
	if (outer[(size_t)(idx - 1)].type != 1)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Form specifier must evaluate to an integer value.");
		return -1;
	}
	form = outer[(size_t)(idx - 1)].i;
	return 1;
}

// *.. (closed FUN_10002eb0).
int PluralSpec(const Ctx& c, const Param& p, const std::vector<Arg>& outer, Language* lang, size_t& plurality)
{
	if (p.plural.hasLit)
	{
		const Arg& a = p.plural.lit;
		if (a.type == 0 || a.type == 5)
		{
			Log(ILocalizationLogger::TYPE_ERROR, "Form Specifier must evaluate to an integer value.");
			return -1;
		}
		const IPluralityRule* rule = lang != NULL ? lang->GetPluralityRule() : NULL;
		plurality = rule != NULL ? rule->GetPluralForm(IntOf(a)) : 0;
		return 1;
	}
	if (!p.plural.param)
		return 0;
	const Param& sp = *p.plural.param;
	if (sp.index < 1)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Lookup parameters are not supported as plurality form specifiers.");
		return -1;
	}
	if ((size_t)(sp.index - 1) >= outer.size())
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Plurality form parameter specifies argument index that doesn't exist.");
		return -1;
	}
	const Arg& a = outer[(size_t)(sp.index - 1)];
	if (a.type == 0)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Form specifier must not be NULL.");
		return -1;
	}
	if (a.type == 5)
	{
		Result r;
		if (!Evaluate(c, sp, outer, r))
		{
			Log(ILocalizationLogger::TYPE_ERROR, "Failed to evaluate plurality specifier parameter");
			return -1;
		}
		plurality = r.plurality;
		return 1;
	}
	plurality = lang != NULL ? lang->GetPluralForm(IntOf(a)) : 0;
	return 1;
}

// ^.. (closed FUN_100030f0).
int GenderSpec(const Ctx& c, const Param& p, const std::vector<Arg>& outer, int& gender, std::vector<std::string>& traits)
{
	if (p.gender.hasLit)
	{
		if (p.gender.lit.type != 5)
		{
			Log(ILocalizationLogger::TYPE_ERROR, "Form Specifier must evaluate to an string value.");
			return -1;
		}
		size_t bytes = 0;
		const char* sz = p.gender.lit.s->toUTF8(bytes, 1);
		if (ParseGenderSpec(sz, bytes, gender, traits))
			return 1;
		Log(ILocalizationLogger::TYPE_ERROR, "Failed to parse gender and traits.");
		return -1;
	}
	if (!p.gender.param)
		return 0;
	Result r;
	if (!Evaluate(c, *p.gender.param, outer, r))
	{
		Log(ILocalizationLogger::TYPE_ERROR, "Failed to evaluate gender specifier parameter");
		return -1;
	}
	gender = r.gender;
	traits = r.traits;
	return 1;
}

size_t FormByGenderTraits(const String& s, int iGender, const std::vector<std::string>& traits, bool bPlural,
                          size_t plurality)
{
	for (size_t i = 0; i < s.m_Forms.size(); ++i)
	{
		const SingleFormString& f = s.m_Forms[i];
		if (bPlural && f.m_Plurality != plurality)
			continue;
		if (iGender != -1 && f.m_Gender != iGender)
			continue;
		bool all = true;
		for (size_t t = 0; t < traits.size() && all; ++t)
			all = f.HasTrait(traits[t].data(), traits[t].size());
		if (all)
			return i + 1;
	}
	return 0;
}

bool Transform(const Ctx& c, const Param& p, const Arg& a, const std::vector<Arg>& args, Result& r);

// Form selection of a string argument, then the transform (closed FUN_100028a0).
bool SelectAndTransform(const Ctx& c, const Param& p, const Arg& a, const std::vector<Arg>& args,
                        const std::vector<Arg>& outer, Result& r)
{
	int form = 0;
	const int fr = ExplicitForm(p, outer, form);
	if (fr < 0)
		return false;
	if (fr == 0)
	{
		Language* lang = a.s->m_pkLanguage != NULL ? a.s->m_pkLanguage : CurrentLanguage();
		size_t plurality = 0;
		const int pr = PluralSpec(c, p, outer, lang, plurality);
		int gender = 0;
		std::vector<std::string> traits;
		const int gr = GenderSpec(c, p, outer, gender, traits);
		if (gr < 0 || pr < 0)
			return false;
		if (gr >= 1)
		{
			if (pr >= 1)
			{
				form = (int)FormByGenderTraits(*a.s, gender, traits, true, plurality);
				if (form == 0)
				{
					Log(ILocalizationLogger::TYPE_ERROR, "Could not deduce form based on gender and plurality.");
					return false;
				}
			}
			else
			{
				form = (int)FormByGenderTraits(*a.s, gender, traits, false, 0);
				if (form == 0)
				{
					traits.clear();
					form = (int)FormByGenderTraits(*a.s, gender, traits, false, 0);
					if (form == 0)
					{
						Log(ILocalizationLogger::TYPE_ERROR, "Could not deduce form based on gender.");
						return false;
					}
				}
			}
		}
		else if (pr >= 1)
		{
			form = (int)a.s->GetFormByPlurality((int)plurality, 0);
			if (form == 0)
			{
				Log(ILocalizationLogger::TYPE_ERROR, "Could not deduce form based on plurality.");
				return false;
			}
		}
	}
	if (form != 0 && a.s->GetNumForms() != 1)
	{
		Arg b = StringArg(*a.s);
		b.form = form;
		return Transform(c, p, b, args, r);
	}
	return Transform(c, p, a, args, r);
}

// Default output of an argument without a transform (closed FUN_10003490).
bool DefaultFormat(const Arg& a, const std::vector<Arg>& args, Result& r)
{
	r = Result();
	if (a.type >= 1 && a.type <= 4)
	{
		FormatNumberArg(CurrentLanguage(), ToNumArg(a), std::string(), r.text);
		return true;
	}
	if (a.type != 5)
		return false;
	const int nForm = a.form == 0 ? 1 : a.form;
	const SingleFormString* pForm = NULL;
	String copy;
	r.text = ComposeCopy(*a.s, args, nForm, &pForm, copy);
	if (pForm != NULL)
	{
		r.plurality = pForm->m_Plurality;
		r.gender = pForm->m_Gender;
		r.traits = pForm->m_Traits;
	}
	return true;
}

// "key?value; key?value; other?value;" (closed FUN_10001040). The value is composed with the arguments.
template <typename Match>
bool Choice(const std::string& t, const std::vector<Arg>& args, Match match, std::string& out)
{
	const size_t n = t.size();
	if (n == 0)
	{
		Log(ILocalizationLogger::TYPE_ERROR, "conditional transform requires arguments.");
		return false;
	}
	static const char kEos[] = "conditional transform: End of string while searching for ';'.";
	size_t start = 0, i = 0;
	for (;;)
	{
		if (t[i] == '?')
		{
			const char* key = t.data() + start;
			const size_t keyLen = i - start;
			if (match(key, keyLen) || strncmp("other", key, keyLen) == 0)
			{
				const size_t vs = i + 1;
				int depth = 0;
				size_t j = i;
				for (; j < n; ++j)
				{
					const char ch = t[j];
					if (ch == ';')
					{
						if (depth == 0)
							break;
					}
					else if (ch == '{')
						++depth;
					else if (ch == '}')
						--depth;
				}
				if (j == n)
				{
					Log(ILocalizationLogger::TYPE_ERROR, kEos);
					return false;
				}
				String v(t.data() + vs, (unsigned int)(j - vs));
				String copy;
				out += ComposeCopy(v, args, 1, NULL, copy);
				return true;
			}
			int depth = 0;
			for (; i < n; ++i)
			{
				const char ch = t[i];
				if (ch == ';')
				{
					if (depth == 0)
						break;
				}
				else if (ch == '{')
					++depth;
				else if (ch == '}')
					--depth;
			}
			if (i == n)
			{
				Log(ILocalizationLogger::TYPE_ERROR, kEos);
				return false;
			}
			i = i + 1;
			while (i < n && (t[i] == ' ' || t[i] == '\t'))
				++i;
			start = i;
		}
		else
			++i;
		if (n <= i)
			return true;
	}
}

bool StringOnly(const Arg& a, const char* err)
{
	if (a.type == 5)
		return true;
	Log(ILocalizationLogger::TYPE_ERROR, err);
	return false;
}

// One named transform; false when it fails (closed ParameterTransforms members, the style map of FUN_100023b0).
bool RunStyle(const std::string& name, const Arg& a, const std::vector<Arg>& args, const std::string& style,
              std::string& out, bool& bKnown)
{
	bKnown = true;
	Language* lang = CurrentLanguage();
	if (name == "number")
		return FormatNumberArg(lang, ToNumArg(a), style, out);
	if (name == "currency")
		return FormatCurrencyArg(lang, ToNumArg(a), out);
	if (name == "percent")
		return FormatPercentArg(lang, ToNumArg(a), out);
	if (name == "roman")
		return FormatRomanArg(lang, ToNumArg(a), out);
	if (name == "spellout")
		return FormatSpelloutArg(lang, ToNumArg(a), out);
	if (name == "ordinal")
		return FormatOrdinalArg(lang, ToNumArg(a), out);
	if (name == "upper" || name == "lower" || name == "title")
	{
		const int mode = name == "upper" ? 1 : (name == "lower" ? 0 : 2);
		static const char* const kErr[] = {"FormatLowercase: Unsupported argument, must be string.",
		                                   "FormatUppercase: Unsupported argument, must be string.",
		                                   "FormatTitlecase: Unsupported argument, must be string."};
		if (!StringOnly(a, kErr[mode]))
			return false;
		size_t bytes = 0;
		const char* sz = a.s->toUTF8(bytes, 1);
		out = CaseMap(lang, sz, bytes, mode);
		return true;
	}
	if (name == "textkey")
	{
		if (!StringOnly(a, "FormatTextkey: Unsupported argument, must be string."))
			return false;
		size_t bytes = 0;
		const char* sz = a.s->toUTF8(bytes, 1);
		String looked = DictLookup(sz, bytes);
		size_t lb = 0;
		const char* lsz = looked.toUTF8(lb, 1);
		if (lsz != NULL && lb != 0)
			out.assign(lsz, lb);
		else
			out.assign(sz, bytes);
		return true;
	}
	if (name == "gender")
	{
		if (!StringOnly(a, "'gender' transform: Unsupported argument, must be string."))
			return false;
		const String* s = a.s.get();
		return Choice(style, args,
		              [s](const char* key, size_t len) { return s != NULL && s->GetFormByGender(key, len, 0) == 1; },
		              out);
	}
	if (name == "plural")
	{
		int value = 0;
		if (a.type == 5)
			value = a.s->m_Forms.empty() ? 0 : (int)a.s->m_Forms[0].m_Plurality;
		else
		{
			const IPluralityRule* rule = RuleOf(NULL);
			value = rule != NULL ? (int)rule->GetPluralForm(IntOf(a)) : 0;
		}
		char buf[32];
		sprintf_s(buf, "%d", value);
		return Choice(style, args, [&buf](const char* key, size_t len) { return strncmp(buf, key, len) == 0; }, out);
	}
	bKnown = false;
	return false;
}

// The transform of a parameter, or the default output (closed FUN_10003250).
bool Transform(const Ctx& c, const Param& p, const Arg& a, const std::vector<Arg>& args, Result& r)
{
	if (!p.hasTransform)
		return DefaultFormat(a, args, r);
	const std::string name(c.s + p.tStart, p.tEnd - p.tStart + 1);
	std::string style;
	size_t k = p.tEnd + 1;
	if (k < p.end)
	{
		while ((c.s[k] == ' ' || c.s[k] == '\t') && k < p.end)
			++k;
		style.assign(c.s + k, p.end - k);
		const size_t last = style.find_last_not_of(" \n\t");
		style.erase(last == std::string::npos ? 0 : last + 1);
	}
	std::string out;
	bool bKnown = false;
	const bool ok = RunStyle(name, a, args, style, out, bKnown);
	if (!bKnown)
	{
		LogF(ILocalizationLogger::TYPE_ERROR, 0x200, "Cannot find style. (%.*s) (%s)", (int)c.len, c.s, name.c_str());
		return false;
	}
	if (!ok)
	{
		ErrForm(c, "Failed to transform argument. %.*s");
		return false;
	}
	r = Result();
	r.text = out;
	return true;
}

// One parameter (closed FUN_100025c0).
bool Evaluate(const Ctx& c, const Param& p, const std::vector<Arg>& args, Result& r)
{
	std::vector<Arg> a2;
	if (!BuildArgs(c, p, args, a2))
		return false;
	if (p.index < 1)
	{
		const Arg a = StringArg(DictLookup(c.s + p.keyStart, p.keyEnd - p.keyStart + 1));
		return SelectAndTransform(c, p, a, a2, args, r);
	}
	if ((size_t)(p.index - 1) >= a2.size())
	{
		LogF(ILocalizationLogger::TYPE_ERROR, 0x400, "Missing argument %i. (%.*s)", p.index, (int)c.len, c.s);
		return false;
	}
	const Arg& a = a2[(size_t)(p.index - 1)];
	if (a.type == 5)
	{
		size_t blen = 0;
		const char* b = a.s->GetBaseString(blen);
		String looked;
		if (b != NULL && DictTryLookup(b, blen, looked))
			return SelectAndTransform(c, p, StringArg(looked), a2, args, r);
		return SelectAndTransform(c, p, a, a2, args, r);
	}
	if (p.at)
	{
		LogF(ILocalizationLogger::TYPE_ERROR, 0x200,
		     "Expected argument(%i) to be of type 'string' for dictionary lookup. (%.*s)", p.index, (int)c.len, c.s);
		return false;
	}
	return Transform(c, p, a, a2, r);
}

struct DepthGuard
{
	DepthGuard() { ++g_depth; }
	~DepthGuard() { --g_depth; }
};
} // namespace

// "<gender>[:traits]" (closed FUN_1000dff0). The gender word is compared case-insensitively over the length of the
// part before ':', so any prefix (also the empty one) of "masculine", "feminine" or "neuter" matches. The trait
// list reproduces the closed loop: for every ',' the text from the start of the traits up to that ',' is added
// (not the single trait), then the whole traits text; nothing when the traits start with '*'.
bool ParseGenderSpec(const char* sz, size_t len, int& iGender, std::vector<std::string>& traits)
{
	if (sz == NULL)
		len = 0;
	for (size_t i = 0; i < len; ++i)
		if (sz[i] == '|')
			return false;
	size_t head = len;
	for (size_t i = 0; i < len; ++i)
		if (sz[i] == ':')
		{
			head = i;
			break;
		}
	const char* s = sz != NULL ? sz : "";
	if (len != 0 && sz[0] == '*' && head == 1)
		iGender = -1;
	else if (_strnicmp(s, "masculine", head) == 0)
		iGender = GENDER_MASCULINE;
	else if (_strnicmp(s, "feminine", head) == 0)
		iGender = GENDER_FEMININE;
	else if (_strnicmp(s, "neuter", head) == 0)
		iGender = GENDER_NEUTER;
	else
		return false;
	traits.clear();
	const size_t start = head + 1;
	if (start < len && sz[start] != '*')
	{
		for (size_t i = start; i < len; ++i)
			if (sz[i] == ',')
				traits.push_back(std::string(sz + start, i - start));
		traits.push_back(std::string(sz + start, len - start));
	}
	return true;
}

bool ParseForm(const char* sz, size_t len)
{
	if (sz == NULL || len == 0)
		return true;
	const Ctx c = {sz, len};
	return ParseAll(c, NULL);
}

bool ComposeForm(const char* sz, size_t len, const std::vector<ParameterArgument>& arguments, std::string& out)
{
	const Ctx c = {sz, len};
	std::vector<Param> params;
	if (!ParseAll(c, &params))
		return false;
	if (g_depth >= kMaxDepth)
	{
		LogF(ILocalizationLogger::TYPE_ERROR, 0x200, "Composition nested too deeply (cyclic text key?). %.*s",
		     (int)len, sz);
		return false;
	}
	DepthGuard guard;
	std::vector<Arg> args;
	args.reserve(arguments.size());
	for (size_t i = 0; i < arguments.size(); ++i)
		args.push_back(FromParameterArgument(arguments[i]));
	out.clear();
	size_t prev = 0;
	for (size_t k = 0; k < params.size(); ++k)
	{
		const Param& p = params[k];
		if (p.start > prev)
			out.append(sz + prev, p.start - prev);
		Result r;
		if (!Evaluate(c, p, args, r))
		{
			LogF(ILocalizationLogger::TYPE_ERROR, 0x200, "Failed to evaluate parameter %i. %.*s", (int)k + 1, (int)len, sz);
			return false;
		}
		out += r.text;
		prev = p.end + 1;
	}
	if (prev < len)
		out.append(sz + prev, len - prev);
	return true;
}
} // namespace Detail
} // namespace Localization
