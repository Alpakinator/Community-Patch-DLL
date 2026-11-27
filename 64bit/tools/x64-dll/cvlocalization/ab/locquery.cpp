// A/B query program for the CvLocalization library (issue 1702).
//
// Compiled once per architecture and linked against either the closed CvLocalizationWin32.lib (x86, VC9 STL inside
// the DLL, with the closed CvGameDatabaseWin32.lib) or our CvLocalization_<arch>.lib (with our CvGameDatabase).
// Layout-agnostic: Localization::String and Database::Connection are built with placement new into large aligned
// buffers, so only the library's exported constructors touch their members; only exported members are called;
// destruction is the explicit (virtual) destructor; sizeof of those classes is never used. Functions that return a
// String by value are called through a function pointer that passes the hidden return pointer explicitly, so the
// caller never needs sizeof(String) either. ParameterTransforms members are never called (the header is out of date).
//
// Usage: locquery <icu .dat> <localization database> <corpus file> <output file>
// The database is opened read-write (the library only reads); pass a fresh copy. The corpus is written by
// gen_corpus.py (one command per line, tab separated, escaped). Output is deterministic text, no pointers.
//
// Optional groups (defined on the command line by run_ab.py when the library under test exports them; the closed
// library exports all): LOC_HAVE_DICT (StringDictionary members), LOC_HAVE_EXTRA (String form/trait/base-string
// members, Language::GetPluralityRule/GetType).
#define CvGameDatabaseAPI __declspec(dllimport)
#define CvLocalizationAPI __declspec(dllimport)
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <string>
#include <vector>
#include "CvGameDatabase.h"
#include "CvLocalization.h"

using namespace Localization;

static FILE* g_out;
static int g_logCount;
static int g_invalidParams;

// Output is built in memory and written at command boundaries (FlushOut). A log line that arrives while a line is
// half printed is held back and placed right after that line, so every block is "head line, then its messages".
static std::string g_buf;
static std::vector<std::string> g_pending;

static bool AtLineStart() { return g_buf.empty() || g_buf[g_buf.size() - 1] == '\n'; }

static void Side(const std::string& line)
{
	if (AtLineStart())
		g_buf += line;
	else
		g_pending.push_back(line);
}

static void P(const char* fmt, ...)
{
	char sbuf[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(sbuf, sizeof(sbuf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n < (int)sizeof(sbuf))
		g_buf.append(sbuf, n);
	else
	{
		std::vector<char> big(n + 1);
		va_start(ap, fmt);
		vsnprintf(&big[0], big.size(), fmt, ap);
		va_end(ap);
		g_buf.append(&big[0], n);
	}
	if (!g_pending.empty() && AtLineStart())
	{
		for (size_t i = 0; i < g_pending.size(); ++i)
			g_buf += g_pending[i];
		g_pending.clear();
	}
}

static void FlushOut()
{
	if (!AtLineStart())
		g_buf += " <unterminated>\n";
	for (size_t i = 0; i < g_pending.size(); ++i)
		g_buf += g_pending[i];
	g_pending.clear();
	fwrite(g_buf.data(), 1, g_buf.size(), g_out);
	fflush(g_out);
	g_buf.clear();
}

// Invalid-parameter reports of the CRTs (MSVCR90 for the closed library, the UCRT for ours): an output line, and
// the CRT function returns its error instead of terminating the process.
static void __cdecl InvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
	++g_invalidParams;
	Side("  INVALID_PARAMETER\n");
}
typedef _invalid_parameter_handler(__cdecl* SetIphFn)(_invalid_parameter_handler);
static void InstallInvalidParameterHandlers()
{
	_set_invalid_parameter_handler(InvalidParameter);
	// MSVCR90 is a side-by-side assembly: GetModuleHandle outside its activation context does not find it, so walk
	// the loaded modules.
	HMODULE m = NULL;
	HMODULE mods[512];
	DWORD need = 0;
	if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
	{
		for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; ++i)
		{
			char name[MAX_PATH];
			if (K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], name, MAX_PATH) && _stricmp(name, "msvcr90.dll") == 0)
				m = mods[i];
		}
	}
	SetIphFn f = m ? (SetIphFn)(void*)GetProcAddress(m, "_set_invalid_parameter_handler") : NULL;
	if (f)
		f(InvalidParameter);
	// Not part of the compared output: which CRTs got the handler differs between the arms.
	fprintf(stderr, "invalid-parameter handler: ucrt yes, msvcr90 %s\n", f ? "yes" : "not loaded");
}

// Quote: control bytes and quote/backslash escaped, bytes >= 0x80 kept (UTF-8 stays readable).
static std::string Q(const char* s, size_t n = (size_t)-1)
{
	if (!s)
		return "<NULL>";
	std::string o = "\"";
	for (size_t i = 0; n == (size_t)-1 ? s[i] != 0 : i < n; ++i)
	{
		const unsigned char c = (unsigned char)s[i];
		if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
		else if (c == '\n') o += "\\n";
		else if (c == '\r') o += "\\r";
		else if (c == '\t') o += "\\t";
		else if (c < 0x20 || c == 0x7F) { char t[8]; sprintf(t, "\\x%02x", c); o += t; }
		else o += (char)c;
	}
	return o + "\"";
}

// Every library message goes into the output, at the point it is logged.
class TestLogger : public ILocalizationLogger
{
public:
	explicit TestLogger(int id) : m_id(id) {}
	virtual ~TestLogger()
	{
		char t[48];
		sprintf(t, "  LOGGER %d deleted\n", m_id);
		Side(t);
	}
	virtual void Log(const char* szMessage, LogTypes eType)
	{
		++g_logCount;
		char t[16];
		sprintf(t, "  LOG%d ", (int)eType);
		Side(t + Q(szMessage) + "\n");
	}
	int m_id;
};

// ---- layout-agnostic String holder ---------------------------------------------------------------------------
struct alignas(64) Buf
{
	unsigned char b[1024];
};

#if defined(_M_IX86)
#define LOC_THISCALL __thiscall
#else
#define LOC_THISCALL
#endif

// String Localization::Lookup(const char*): hidden return pointer first, returned in eax/rax.
typedef void* (__cdecl* FreeLookupFn)(void* ret, const char* key);
// Code-pointer copy: a function pointer, or a non-virtual member pointer of a single-inheritance class, is one code
// pointer in the MSVC ABI.
template <typename F, typename M>
static F MemberAddress(M pm)
{
	static_assert(sizeof(M) == sizeof(F), "member pointer is not a plain code pointer");
	F f;
	memcpy(&f, &pm, sizeof(f));
	return f;
}
static FreeLookupFn g_lookup = MemberAddress<FreeLookupFn>(&Localization::Lookup);

struct LookupTag {};
struct Str
{
	Buf buf;
	String* s;
	Str() { Fill(); s = ::new (static_cast<void*>(&buf)) String(); }
	explicit Str(const char* raw) { Fill(); s = ::new (static_cast<void*>(&buf)) String(raw); }
	Str(LookupTag, const char* key) { Fill(); s = static_cast<String*>(g_lookup(&buf, key)); }
	Str(const Str& o) { Fill(); s = ::new (static_cast<void*>(&buf)) String(*o.s); }
	~Str() { s->~String(); }
	void Fill() { memset(&buf, 0xCD, sizeof(buf)); }
	String* operator->() { return s; }
	String& operator*() { return *s; }
private:
	Str& operator=(const Str&);
};

#ifdef LOC_HAVE_EXTRA
static String* NewRawLen(Buf& b, const char* raw, unsigned len)
{
	memset(&b, 0xCD, sizeof(b));
	return ::new (static_cast<void*>(&b)) String(raw, len);
}
#endif

#ifdef LOC_HAVE_DICT
// Member functions of StringDictionary that return a String by value: this, hidden return pointer, arguments.
typedef void* (LOC_THISCALL* DictLookup1Fn)(const void* self, void* ret, const char* key);
typedef void* (LOC_THISCALL* DictLookup2Fn)(const void* self, void* ret, const char* key, unsigned len);
typedef void* (LOC_THISCALL* DictLookupLang2Fn)(const void* self, void* ret, const char* key, unsigned idx);
typedef void* (LOC_THISCALL* DictLookupLang3Fn)(const void* self, void* ret, const char* key, unsigned len,
                                                unsigned idx);
#endif

// ---- corpus parsing -------------------------------------------------------------------------------------------
static std::string Unescape(const std::string& in)
{
	std::string o;
	for (size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] != '\\' || i + 1 >= in.size())
		{
			o += in[i];
			continue;
		}
		const char c = in[++i];
		if (c == 't') o += '\t';
		else if (c == 'n') o += '\n';
		else if (c == 'r') o += '\r';
		else if (c == '\\') o += '\\';
		else if (c == 'x' && i + 2 < in.size())
		{
			char h[3] = {in[i + 1], in[i + 2], 0};
			o += (char)strtoul(h, NULL, 16);
			i += 2;
		}
		else { o += '\\'; o += c; }
	}
	return o;
}

static std::vector<std::string> SplitTabs(const std::string& line)
{
	std::vector<std::string> f;
	size_t a = 0;
	for (;;)
	{
		size_t b = line.find('\t', a);
		f.push_back(Unescape(line.substr(a, b == std::string::npos ? std::string::npos : b - a)));
		if (b == std::string::npos)
			break;
		a = b + 1;
	}
	return f;
}

// ---- SEH wrapper: a crash in the library is an output line, not the end of the run ----------------------------
typedef void (*Job)(void* ctx);
static int g_crashes;
static bool Guard(Job job, void* ctx, const char* what)
{
	__try
	{
		job(ctx);
		FlushOut();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		++g_crashes;
		if (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
			_resetstkoflw();
		if (!AtLineStart())
			g_buf += " <crashed>\n";
		P("X CRASH 0x%08lx in %s\n", (unsigned long)GetExceptionCode(), what);
		FlushOut();
		return false;
	}
}

// ---- output of one String --------------------------------------------------------------------------------------
static std::string FormText(String& s, int form)
{
	size_t bytes = 12345;
	const char* t = s.toUTF8(bytes, form);
	char h[48];
	sprintf(h, "b=%d ", t ? (int)bytes : -1);
	return h + Q(t, t ? bytes : (size_t)-1);
}

// KEY: everything about a bare lookup, all forms plus one past the end.
struct KeyCtx { const char* key; };
static void DoKey(void* p)
{
	KeyCtx* c = static_cast<KeyCtx*>(p);
	Str s(LookupTag(), c->key);
	const int n = (int)s->GetNumForms();
	P("K %s n=%d e=%d", Q(c->key).c_str(), n, s->IsEmpty() ? 1 : 0);
	for (int f = 1; f <= n + 1; ++f)
		P(" | f%d pl=%d g=%d %s", f, (int)s->GetPlurality(f), (int)s->GetGender(f), FormText(*s, f).c_str());
	const char* t0 = s->toUTF8();
	P(" | u=%s\n", Q(t0).c_str());
}

// FMT: compose with arguments.
struct FmtCtx { const std::vector<std::string>* f; };
static void PushArgs(String& s, const std::vector<std::string>& f, size_t first, std::string& res,
                     std::vector<Str*>& keep)
{
	for (size_t i = first; i < f.size(); ++i)
	{
		const std::string& a = f[i];
		const char k = a.empty() ? '?' : a[0];
		const std::string v = a.empty() ? std::string() : a.substr(1);
		bool r = false;
		switch (k)
		{
		case 'i': r = s.PushArgument((int)strtol(v.c_str(), NULL, 10)); break;
		case 'I': r = s.PushArgument((__int64)_strtoi64(v.c_str(), NULL, 10)); break;
		case 'f': r = s.PushArgument((float)strtod(v.c_str(), NULL)); break;
		case 'd': r = s.PushArgument(strtod(v.c_str(), NULL)); break;
		case 's': r = s.PushArgument(v.c_str()); break;
		case 'n': r = s.PushArgument(v.c_str(), v.size()); break;
		case '0': r = s.PushArgument((const char*)NULL); break;
		case 'k':
		{
			Str* ks = new Str(LookupTag(), v.c_str());
			keep.push_back(ks);
			r = s.PushArgument(**ks);
			break;
		}
		case 'r':
		{
			Str* ks = new Str(v.c_str());
			keep.push_back(ks);
			r = s.PushArgument(**ks);
			break;
		}
		default: P("  BADARG %s\n", Q(a.c_str()).c_str()); break;
		}
		res += r ? '1' : '0';
	}
}

static void DoFmt(void* p)
{
	const std::vector<std::string>& f = *static_cast<FmtCtx*>(p)->f;
	// FMT id mode text args...
	const std::string& mode = f[2];
	Str* s = mode == "L" ? new Str(LookupTag(), f[3].c_str()) : new Str(f[3].c_str());
	std::string pushed;
	std::vector<Str*> keep;
	PushArgs(**s, f, 4, pushed, keep);
	const int n = (int)(*s)->GetNumForms();
	P("F %s p=%s n=%d %s", f[1].c_str(), pushed.c_str(), n, FormText(**s, 1).c_str());
	for (int k = 2; k <= n; ++k)
		P(" | f%d %s", k, FormText(**s, k).c_str());
	P("\n");
	delete s;
	for (size_t i = 0; i < keep.size(); ++i)
		delete keep[i];
}

static void DoLang(void* p)
{
	const char* t = static_cast<const char*>(p);
	bool ok = SetCurrentLanguage(t);
	const Language& L = GetCurrentLanguage();
	P("LANG %s set=%d current=%s\n", Q(t).c_str(), ok ? 1 : 0, Q(L.GetType()).c_str());
}

// ---- plural-rule probe -----------------------------------------------------------------------------------------
static void DoPluralProbe(void* p)
{
	const char* t = static_cast<const char*>(p);
	const Language& L = GetCurrentLanguage();
	P("PLURAL %s", Q(t).c_str());
#ifdef LOC_HAVE_EXTRA
	IPluralityRule* r = L.GetPluralityRule();
	P(" rule=%s max=%d", r ? "set" : "NULL", r ? (int)r->GetMaxPluralForms() : -1);
#endif
	// Run-length: n0..n1=form
	int start = -200;
	unsigned prev = L.GetPluralForm(-200);
	for (int n = -199; n <= 1201; ++n)
	{
		unsigned cur = n <= 1200 ? L.GetPluralForm(n) : 0xFFFFFFFFu;
		if (cur != prev)
		{
			P(" %d..%d=%u", start, n - 1, prev);
			start = n;
			prev = cur;
		}
	}
	const int big[] = {10000, 100000, 1000000, 2147483647, -2147483647 - 1, 1000001, 1000002, 1000005};
	P(" |");
	for (int i = 0; i < 8; ++i)
		P(" %d=%u", big[i], L.GetPluralForm(big[i]));
	P("\n");
}

// ---- hard-coded edge cases -------------------------------------------------------------------------------------
static void EdgeNullKey(void*)
{
	Str s(LookupTag(), (const char*)NULL);
	P(" lookup(NULL): n=%d e=%d %s\n", (int)s->GetNumForms(), s->IsEmpty() ? 1 : 0, FormText(*s, 1).c_str());
}
static void EdgeEmptyKey(void*)
{
	Str s(LookupTag(), "");
	P(" lookup(\"\"): n=%d e=%d %s u=%s\n", (int)s->GetNumForms(), s->IsEmpty() ? 1 : 0, FormText(*s, 1).c_str(),
	  Q(s->toUTF8()).c_str());
}
static void EdgeDefault(void*)
{
	Str s;
	P(" default: n=%d e=%d %s u=%s\n", (int)s->GetNumForms(), s->IsEmpty() ? 1 : 0, FormText(*s, 1).c_str(),
	  Q(s->toUTF8()).c_str());
	P(" default pl1=%d g1=%d\n", (int)s->GetPlurality(1), (int)s->GetGender(1));
}
static void EdgeRawNull(void*)
{
	Str s((const char*)NULL);
	P(" String(NULL): n=%d e=%d %s\n", (int)s->GetNumForms(), s->IsEmpty() ? 1 : 0, FormText(*s, 1).c_str());
}
static void EdgeEmpty(void*)
{
	P(" String::Empty e=%d n=%d\n", String::Empty.IsEmpty() ? 1 : 0, (int)String::Empty.GetNumForms());
}
static const char* g_formKey;
static void EdgeForms(void*)
{
	Str s(LookupTag(), g_formKey);
	const int forms[] = {0, -1, 1, 2, 3, 100, 0x7fffffff};
	for (int i = 0; i < 7; ++i)
	{
		Str c(s);
		P(" %s form %d: %s\n", g_formKey, forms[i], FormText(*c, forms[i]).c_str());
	}
	const size_t sf[] = {0, 1, 2, 3, 100, (size_t)-1};
	for (int i = 0; i < 6; ++i)
		P(" %s GetPlurality(%d)=%d GetGender(%d)=%d\n", g_formKey, (int)sf[i], (int)s->GetPlurality(sf[i]),
		  (int)sf[i], (int)s->GetGender(sf[i]));
}
static void EdgePushAfter(void*)
{
	Str s(LookupTag(), "TXT_KEY_AB_EDGE_TWO_ARGS");
	P(" push-after: first=%s", FormText(*s, 1).c_str());
	s->PushArgument(7);
	P(" second(+7)=%s", FormText(*s, 1).c_str());
	s->PushArgument("late");
	P(" third(+late)=%s u=%s\n", FormText(*s, 1).c_str(), Q(s->toUTF8()).c_str());
}
static void EdgeCopies(void*)
{
	Str a(LookupTag(), "TXT_KEY_AB_EDGE_TWO_ARGS");
	a->PushArgument(3);
	a->PushArgument("TXT_KEY_AB_EDGE_FEM");
	Str b(a);  // copy before compose
	P(" copy-before: a=%s b=%s\n", FormText(*a, 1).c_str(), FormText(*b, 1).c_str());
	Str c(a);  // copy after compose
	P(" copy-after: c=%s\n", FormText(*c, 1).c_str());
	Str d;
	*d = *a;  // assignment
	P(" assign: d=%s\n", FormText(*d, 1).c_str());
	d->PushArgument(99);  // the copy is independent?
	P(" assign+push: d=%s a=%s\n", FormText(*d, 1).c_str(), FormText(*a, 1).c_str());
	*d = "raw {1_X} and {2_Y}";  // operator=(const char*): arguments kept or cleared?
	P(" assign-raw: d=%s\n", FormText(*d, 1).c_str());
	*d = *d;  // self-assignment
	P(" self-assign: d=%s\n", FormText(*d, 1).c_str());
	*d = (const char*)"";
	P(" assign-empty: e=%d %s\n", d->IsEmpty() ? 1 : 0, FormText(*d, 1).c_str());
	Str e("raw {1_X}");
	e->PushArgument(*a);  // a composed String as argument
	P(" string-arg: %s\n", FormText(*e, 1).c_str());
	Str g(LookupTag(), "TXT_KEY_AB_EDGE_FEM");
	Str h("{1_Thing} / {@1_Thing}");
	h->PushArgument(*g);
	P(" string-arg-key: %s\n", FormText(*h, 1).c_str());
}
static void EdgeLong(void*)
{
	std::string big;
	for (int i = 0; i < 4000; ++i)
		big += "Long text block {1_Num} with {2_Name: textkey} and [ICON_GOLD] gold; ";
	Str s(big.c_str());
	s->PushArgument(12);
	s->PushArgument("TXT_KEY_AB_EDGE_FEM");
	size_t bytes = 0;
	const char* t = s->toUTF8(bytes, 1);
	unsigned h = 2166136261u;
	for (size_t i = 0; t && i < bytes; ++i)
		h = (h ^ (unsigned char)t[i]) * 16777619u;
	P(" long: in=%d out=%d fnv=%08x head=%s\n", (int)big.size(), t ? (int)bytes : -1, h,
	  Q(t, t ? (bytes < 120 ? bytes : 120) : (size_t)-1).c_str());
	std::string many;
	for (int i = 1; i <= 40; ++i)
	{
		char t2[16];
		sprintf(t2, "{%d_A%d}", i, i);
		many += t2;
	}
	Str m(many.c_str());
	for (int i = 1; i <= 40; ++i)
		m->PushArgument(i * 3);
	P(" forty-args: %s\n", FormText(*m, 1).c_str());
	std::string deep;
	for (int i = 0; i < 300; ++i)
		deep += "{";
	deep += "1_X";
	for (int i = 0; i < 300; ++i)
		deep += "}";
	Str d(deep.c_str());
	d->PushArgument(5);
	P(" deep-braces: %s\n", FormText(*d, 1).c_str());
}
#ifdef LOC_HAVE_EXTRA
static void EdgeExtra(void*)
{
	Buf b;
	String* s = NewRawLen(b, "abc {1_X} def", 9);
	s->PushArgument(5);
	size_t bl = 0;
	const char* base = s->GetBaseString(bl);
	P(" String(raw,9): base=%s %s\n", Q(base, base ? bl : (size_t)-1).c_str(), FormText(*s, 1).c_str());
	s->~String();
	Str k(LookupTag(), g_formKey);
	base = k->GetBaseString(bl);
	P(" %s base=%s\n", g_formKey, Q(base, base ? bl : (size_t)-1).c_str());
	P(" %s byPl(1)=%d byPl(2)=%d byPl(3)=%d byPl(2,1)=%d byG(feminine)=%d\n", g_formKey,
	  (int)k->GetFormByPlurality(1), (int)k->GetFormByPlurality(2), (int)k->GetFormByPlurality(3),
	  (int)k->GetFormByPlurality(2, 1), (int)k->GetFormByGender("feminine"));
	P(" lang ptr null=%d\n", k->GetLanguage() == NULL ? 1 : 0);
	Str t("plain");
	P(" SetGender(feminine)=%d g=%d SetPlurality(2)=%d pl=%d\n", t->SetGender("feminine") ? 1 : 0,
	  (int)t->GetGender(1), t->SetPlurality((size_t)2) ? 1 : 0, (int)t->GetPlurality(1));
}
#endif

#ifdef LOC_HAVE_DICT
static std::vector<std::string>* g_dictKeys;
static void EdgeDict(void*)
{
	const StringDictionary& d = GetStringDictionary();
	const unsigned n = (unsigned)d.GetLanguageCount();
	P(" dict: languages=%u current=%u\n", n, d.GetCurrentLanguageIndex());
	for (unsigned i = 0; i < n; ++i)
	{
		const StringDictionary::Language& L = d.GetLanguage(i);
		P("  lang[%u] type=%s name=%s rule=%d | %s %s\n", i, Q(L.GetType()).c_str(), Q(L.GetDisplayName()).c_str(),
		  L.GetPluralityRule(), Q(d.GetLanguageType(i)).c_str(), Q(d.GetLanguageDisplayName(i)).c_str());
	}
	DictLookup1Fn lk1 = MemberAddress<DictLookup1Fn>(
	    static_cast<String (StringDictionary::*)(const char*) const>(&StringDictionary::Lookup));
	DictLookup2Fn lk2 = MemberAddress<DictLookup2Fn>(
	    static_cast<String (StringDictionary::*)(const char*, unsigned) const>(&StringDictionary::Lookup));
	DictLookupLang2Fn ll2 = MemberAddress<DictLookupLang2Fn>(
	    static_cast<String (StringDictionary::*)(const char*, unsigned) const>(&StringDictionary::LookupLanguage));
	DictLookupLang3Fn ll3 = MemberAddress<DictLookupLang3Fn>(
	    static_cast<String (StringDictionary::*)(const char*, unsigned, unsigned) const>(
	        &StringDictionary::LookupLanguage));
	for (size_t k = 0; k < g_dictKeys->size(); ++k)
	{
		const char* key = (*g_dictKeys)[k].c_str();
		const unsigned len = (unsigned)strlen(key);
		P("  key %s has=%d,%d", Q(key).c_str(), d.HasKey(key, len) ? 1 : 0, d.HasKey(key, len > 4 ? len - 4 : 0) ? 1 : 0);
		for (unsigned i = 0; i < n && i < 3; ++i)
			P(" has[%u]=%d", i, d.HasKey(key, len, i) ? 1 : 0);
		{
			Buf b;
			memset(&b, 0xCD, sizeof(b));
			String* s = static_cast<String*>(lk1(&d, &b, key));
			P(" lk=%s", FormText(*s, 1).c_str());
			s->~String();
		}
		{
			Buf b;
			memset(&b, 0xCD, sizeof(b));
			String* s = static_cast<String*>(lk2(&d, &b, key, len > 4 ? len - 4 : len));
			P(" lk(len-4)=%s", FormText(*s, 1).c_str());
			s->~String();
		}
		for (unsigned i = 0; i < n && i < 3; ++i)
		{
			Buf b;
			memset(&b, 0xCD, sizeof(b));
			String* s = static_cast<String*>(ll2(&d, &b, key, i));
			P(" ll[%u]=%s", i, FormText(*s, 1).c_str());
			s->~String();
			memset(&b, 0xCD, sizeof(b));
			s = static_cast<String*>(ll3(&d, &b, key, len, i));
			P(" ll3[%u]=%s", i, FormText(*s, 1).c_str());
			s->~String();
		}
		{
			Str out;
			bool ok = d.TryLookup(key, len, *out);
			P(" try=%d %s", ok ? 1 : 0, FormText(*out, 1).c_str());
			Str out2;
			ok = d.TryLookupLanguage(key, len, 0, *out2);
			P(" tryl0=%d %s\n", ok ? 1 : 0, FormText(*out2, 1).c_str());
		}
	}
}
#endif

struct ReinitCtx
{
	const wchar_t* icu;
	Database::Connection* db;
	const char* lang;
	bool disable;
	int mode;  // 0 Initialize, 1 Shutdown + Initialize, 2 ShutdownDictionary + InitializeDictionary
};
static void DoReinit(void* p)
{
	ReinitCtx* c = static_cast<ReinitCtx*>(p);
	if (c->mode == 1)
	{
		bool s1 = Shutdown();
		P("Shutdown=%d\n", s1 ? 1 : 0);
		FlushOut();
	}
	if (c->mode == 2)
	{
		bool s1 = ShutdownDictionary();
		P("ShutdownDictionary=%d\n", s1 ? 1 : 0);
		FlushOut();
		bool i2 = InitializeDictionary(*c->db, "Languages", c->lang, c->disable);
		P("InitializeDictionary=%d\n", i2 ? 1 : 0);
	}
	else
	{
		bool i2 = Initialize(c->icu, *c->db, "Languages", c->lang, c->disable);
		P("Initialize=%d\n", i2 ? 1 : 0);
	}
	FlushOut();
	P("current=%s\n", Q(GetCurrentLanguage().GetType()).c_str());
}

// ---- main ------------------------------------------------------------------------------------------------------
struct alignas(64) ConnBuf
{
	unsigned char b[4096];
};

int main(int argc, char** argv)
{
	if (argc != 5)
	{
		fprintf(stderr, "usage: locquery <icu dat> <localization db> <corpus> <output>\n");
		return 2;
	}
	g_out = fopen(argv[4], "wb");
	if (!g_out)
		return 3;
	FILE* corpus = fopen(argv[3], "rb");
	if (!corpus)
		return 3;
	std::vector<std::string> lines;
	{
		std::string cur;
		int ch;
		while ((ch = fgetc(corpus)) != EOF)
		{
			if (ch == '\n') { lines.push_back(cur); cur.clear(); }
			else if (ch != '\r') cur += (char)ch;
		}
		if (!cur.empty())
			lines.push_back(cur);
		fclose(corpus);
	}
	P("# locquery 1\n");
	wchar_t icu[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, argv[1], -1, icu, MAX_PATH);

	ConnBuf cb;
	memset(&cb, 0xCD, sizeof(cb));
	Database::Connection* db = ::new (static_cast<void*>(&cb)) Database::Connection();
	bool ok = db->Open(argv[2], Database::OPEN_READWRITE | Database::OPEN_FULLMUTEX);
	P("db open=%d\n", ok ? 1 : 0);
	if (!ok)
	{
		FlushOut();
		return 4;
	}
	InstallInvalidParameterHandlers();

	int loggerId = 0;
	SetLogger(new TestLogger(++loggerId));
	LogMessage("ab: message");
	LogWarning("ab: warning");
	LogError("ab: error");

	// Before Initialize: a lookup with no dictionary.
	P("== before-init\n");
	{
		KeyCtx c = {"TXT_KEY_UNIT_SETTLER"};
		Guard(DoKey, &c, "key before init");
	}


	std::vector<std::string> dictKeys;
	std::string formKey = "TXT_KEY_AB_EDGE_TWO_ARGS";
	for (size_t li = 0; li < lines.size(); ++li)
	{
		const std::string& line = lines[li];
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> f = SplitTabs(line);
		const std::string& cmd = f[0];
		if (cmd == "SECTION")
		{
			P("== %s\n", f[1].c_str());
		}
		else if (cmd == "LANG")
		{
			Guard(DoLang, (void*)f[1].c_str(), "SetCurrentLanguage");
		}
		else if (cmd == "KEY")
		{
			KeyCtx c = {f[1].c_str()};
			Guard(DoKey, &c, ("KEY " + f[1]).c_str());
		}
		else if (cmd == "FMT" && f.size() >= 4)
		{
			FmtCtx c = {&f};
			Guard(DoFmt, &c, ("FMT " + f[1]).c_str());
		}
		else if (cmd == "INIT" || cmd == "DICTINIT")
		{
			// INIT lang disable: Initialize; DICTINIT lang disable: ShutdownDictionary + InitializeDictionary.
			P("== %s %s disable=%s\n", cmd.c_str(), f[1].c_str(), f[2].c_str());
			ReinitCtx c = {icu, db, f[1].c_str(), f[2] == "1", cmd == "INIT" ? 0 : 2};
			Guard(DoReinit, &c, cmd.c_str());
		}
		else if (cmd == "PLURAL")
		{
			Guard(DoPluralProbe, (void*)f[1].c_str(), "PLURAL");
		}
		else if (cmd == "FORMKEY")
		{
			formKey = f[1];
		}
		else if (cmd == "DICTKEY")
		{
			dictKeys.push_back(f[1]);
		}
		else if (cmd == "EDGE")
		{
			g_formKey = formKey.c_str();
			P("== edge\n");
			Guard(EdgeNullKey, NULL, "lookup NULL");
			Guard(EdgeEmptyKey, NULL, "lookup empty");
			Guard(EdgeDefault, NULL, "default String");
			Guard(EdgeRawNull, NULL, "String(NULL)");
			Guard(EdgeEmpty, NULL, "String::Empty");
			Guard(EdgeForms, NULL, "forms");
			Guard(EdgePushAfter, NULL, "push after toUTF8");
			Guard(EdgeCopies, NULL, "copies");
			Guard(EdgeLong, NULL, "long");
#ifdef LOC_HAVE_EXTRA
			Guard(EdgeExtra, NULL, "extra");
#else
			P(" extra: skipped (LOC_HAVE_EXTRA not defined)\n");
#endif
#ifdef LOC_HAVE_DICT
			g_dictKeys = &dictKeys;
			Guard(EdgeDict, NULL, "dict");
#else
			P(" dict: skipped (LOC_HAVE_DICT not defined)\n");
#endif
			P(" set-language edge cases\n");
			Guard(DoLang, (void*)"no_SUCH", "SetCurrentLanguage nope");
			Guard(DoLang, (void*)"", "SetCurrentLanguage empty");
			Guard(DoLang, (void*)"en_US", "SetCurrentLanguage en");
			P(" logger swap\n");
			SetLogger(new TestLogger(++loggerId));
			LogMessage("ab: after swap");
		}
		else if (cmd == "REINIT")
		{
			// Shutdown and Initialize again: f[1] = language, f[2] = 1 to disable fallback.
			P("== reinit %s disable=%s\n", f[1].c_str(), f[2].c_str());
			ReinitCtx c = {icu, db, f[1].c_str(), f[2] == "1", 1};
			Guard(DoReinit, &c, "REINIT");
		}
		else
			P("BADCMD %s\n", Q(line.c_str()).c_str());
		FlushOut();
	}
	P("== shutdown\n");
	P("Shutdown=%d\n", Shutdown() ? 1 : 0);
	P("log messages=%d crashes=%d invalid-parameter=%d\n", g_logCount, g_crashes, g_invalidParams);
	SetLogger(NULL);
	P("logger cleared\n");
	db->Close();
	db->~Connection();
	P("# end\n");
	FlushOut();
	fclose(g_out);
	return 0;
}
