// CvLocalization replacement (c5-64bit-patch issue 1701): SharedString, SingleFormString, ParameterArgument,
// StringParameter and Localization::String. The member layout is the fork header's (the DLL builds Strings on its
// own stack); behaviour as the closed library (decompile 0x10007000..0x1000a1f0, 1702 A/B). See ../README.md.
#include <stdlib.h>
#include <string.h>

#include "LocInternal.h"

namespace Localization
{
namespace
{
struct FreeDeleter
{
	void operator()(char* p) const { free(p); }
};

size_t NumForms(const std::vector<SingleFormString>& forms)
{
	return forms.size();
}
} // namespace

// ------------------------------------------------------------------------------------------------- SharedString
SharedString::SharedString() : m_Buffer(), m_BufferLength(0), m_offset(0), m_length(0)
{
}

SharedString::SharedString(const char* szString, size_t length) : m_Buffer(), m_BufferLength(0), m_offset(0), m_length(0)
{
	// malloc(length + 1), zeroed, the text copied: always NUL-terminated.
	char* p = static_cast<char*>(malloc(length + 1));
	if (p != NULL)
	{
		memset(p, 0, length + 1);
		if (szString != NULL && length != 0)
			memcpy(p, szString, length);
	}
	m_Buffer = std::tr1::shared_ptr<char>(p, FreeDeleter());
	m_BufferLength = length + 1;
	m_length = length;
}

SharedString::SharedString(const SharedString& copy)
	: m_Buffer(copy.m_Buffer), m_BufferLength(copy.m_BufferLength), m_offset(copy.m_offset), m_length(copy.m_length)
{
}

SharedString& SharedString::operator=(const SharedString& other)
{
	m_Buffer = other.m_Buffer;
	m_BufferLength = other.m_BufferLength;
	m_offset = other.m_offset;
	m_length = other.m_length;
	return *this;
}

const char* SharedString::GetBuffer(size_t& length) const
{
	length = m_BufferLength;
	return m_Buffer.get();
}

const char* SharedString::GetString(size_t& length) const
{
	length = m_length;
	return m_Buffer.get() + m_offset;
}

size_t SharedString::GetBufferLength() const
{
	return m_BufferLength;
}

size_t SharedString::GetOffset() const
{
	return m_offset;
}

size_t SharedString::GetLength() const
{
	return m_length;
}

bool SharedString::SetOffsetAndLength(size_t offset, size_t length)
{
	if (offset + length > m_BufferLength)
		return false;
	m_offset = offset;
	m_length = length;
	return true;
}

// ------------------------------------------------------------------------------------------------- StringParameter
StringParameter::StringParameter()
	: ArgumentIndex(0), uiTextKeyStart(0), lenTextKey(0), uiParameterStart(0), uiParameterEnd(0),
	  uiTransformTagStart(0), uiTransformTagEnd(0)
{
}

StringParameter::StringParameter(const StringParameter& other)
	: ArgumentIndex(other.ArgumentIndex), uiTextKeyStart(other.uiTextKeyStart), lenTextKey(other.lenTextKey),
	  uiParameterStart(other.uiParameterStart), uiParameterEnd(other.uiParameterEnd),
	  uiTransformTagStart(other.uiTransformTagStart), uiTransformTagEnd(other.uiTransformTagEnd),
	  m_injected_parameters(other.m_injected_parameters)
{
}

// ------------------------------------------------------------------------------------------------- ParameterArgument
ParameterArgument::ParameterArgument(const ParameterArgument& other)
	: m_value(other.m_value), m_str(other.m_str), m_type(other.m_type)
{
}

ParameterArgument::ParameterArgument(const String& strText) : m_str(new String(strText)), m_type(ARGTYPE_STRING)
{
	m_value.i64 = 0;
}

ParameterArgument::ParameterArgument(const int val) : m_str(), m_type(ARGTYPE_INT)
{
	m_value.i64 = 0;
	m_value.i = val;
}

ParameterArgument::ParameterArgument(const __int64 val) : m_str(), m_type(ARGTYPE_INT64)
{
	m_value.i64 = val;
}

ParameterArgument::ParameterArgument(const float val) : m_str(), m_type(ARGTYPE_FLOAT)
{
	m_value.i64 = 0;
	m_value.f = val;
}

ParameterArgument::ParameterArgument(const double val) : m_str(), m_type(ARGTYPE_DOUBLE)
{
	m_value.d = val;
}

// ------------------------------------------------------------------------------------------------- SingleFormString
// Defaults as the closed constructor: plurality 1, gender 1 (masculine), not parsed, not composed.
SingleFormString::SingleFormString()
	: m_BaseString(), m_ComposedString(), m_Plurality(1), m_Gender(GENDER_MASCULINE), m_Composed(false), m_Parsed(false)
{
}

SingleFormString::SingleFormString(const SharedString& baseString, size_t, Gender)
	: m_BaseString(baseString), m_ComposedString(), m_Plurality(1), m_Gender(GENDER_MASCULINE), m_Composed(false),
	  m_Parsed(false)
{
}

SingleFormString::~SingleFormString()
{
}

SingleFormString& SingleFormString::operator=(const SingleFormString& other)
{
	m_BaseString = other.m_BaseString;
	m_ComposedString = other.m_ComposedString;
	m_Plurality = other.m_Plurality;
	m_Gender = other.m_Gender;
	m_Traits = other.m_Traits;
	m_Parameters = other.m_Parameters;
	m_Composed = other.m_Composed;
	m_Parsed = other.m_Parsed;
	return *this;
}

const SharedString& SingleFormString::GetBaseString() const
{
	return m_BaseString;
}

const SharedString& SingleFormString::GetComposedString() const
{
	return m_ComposedString;
}

void SingleFormString::SetPlurality()
{
}

size_t SingleFormString::GetPlurality() const
{
	return m_Plurality;
}

void SingleFormString::SetGender(Gender gender)
{
	m_Gender = gender;
}

Gender SingleFormString::GetGender() const
{
	return static_cast<Gender>(m_Gender);
}

bool SingleFormString::HasTrait(const char* szTrait, size_t lenTrait) const
{
	for (size_t i = 0; i < m_Traits.size(); ++i)
		if (m_Traits[i].size() == lenTrait && strncmp(m_Traits[i].data(), szTrait, lenTrait) == 0)
			return true;
	return false;
}

// The parse result is kept in m_Parsed (a failed parse is retried on every composition, as the closed library).
bool SingleFormString::Parse()
{
	if (!m_Parsed)
	{
		size_t len = 0;
		const char* sz = m_BaseString.GetString(len);
		// The closed library parses up to the first NUL inside the form.
		if (sz != NULL)
		{
			for (size_t i = 0; i < len; ++i)
				if (sz[i] == 0)
				{
					len = i + 1;
					break;
				}
		}
		m_Parsed = Detail::ParseForm(sz, len);
	}
	return m_Parsed;
}

bool SingleFormString::Compose(const std::vector<ParameterArgument>& arguments)
{
	if (!Parse())
		return false;
	m_ComposedString = SharedString();
	size_t len = 0;
	const char* sz = m_BaseString.GetString(len);
	std::string out;
	bool bHasParameters = false;
	for (size_t i = 0; i < len; ++i)
		if (sz[i] == '{')
		{
			bHasParameters = true;
			break;
		}
	if (!bHasParameters)
	{
		// No parameter: the composed string is the base form itself (same buffer, not NUL-terminated at the end of
		// a form that is followed by '|').
		m_ComposedString = m_BaseString;
		m_Composed = true;
		return true;
	}
	if (!Detail::ComposeForm(sz, len, arguments, out))
		return false;
	m_ComposedString = SharedString(out.data(), out.size());
	m_Composed = true;
	return true;
}

// ------------------------------------------------------------------------------------------------- String
String String::Empty;

namespace
{
// Splits the base string at '|' into forms (the closed FUN_10009a70). Every form shares the base buffer.
void SplitForms(const SharedString& base, std::vector<SingleFormString>& forms)
{
	forms.clear();
	size_t len = 0;
	const char* sz = base.GetString(len);
	if (sz == NULL || len == 0)
		return;
	const size_t off = base.GetOffset();
	size_t start = 0;
	for (size_t i = 0; i <= len; ++i)
	{
		if (i == len || sz[i] == '|')
		{
			SharedString part(base);
			part.SetOffsetAndLength(off + start, i - start);
			forms.push_back(SingleFormString(part));
			start = i + 1;
		}
	}
}
} // namespace

String::String() : m_BaseString(), m_ComposedNullTerminated(), m_arguments(), m_Forms(), m_pkLanguage(NULL)
{
}

String::String(const char* szString) : m_BaseString(), m_ComposedNullTerminated(), m_pkLanguage(NULL)
{
	const size_t len = szString != NULL ? strlen(szString) : 0;
	if (len != 0)
	{
		m_BaseString = SharedString(szString, len);
		SplitForms(m_BaseString, m_Forms);
	}
	else
		m_BaseString = SharedString("", 0);
}

String::String(const char* szString, unsigned int uiLen) : m_BaseString(), m_ComposedNullTerminated(), m_pkLanguage(NULL)
{
	if (szString != NULL && uiLen != 0)
	{
		m_BaseString = SharedString(szString, uiLen);
		SplitForms(m_BaseString, m_Forms);
	}
	else
		m_BaseString = SharedString("", 0);
}

String::String(const String& other)
	: m_BaseString(other.m_BaseString), m_ComposedNullTerminated(other.m_ComposedNullTerminated),
	  m_arguments(other.m_arguments), m_Forms(other.m_Forms), m_pkLanguage(other.m_pkLanguage)
{
}

String::~String()
{
}

bool String::IsEmpty() const
{
	return m_BaseString.GetLength() == 0;
}

String& String::operator=(const String& other)
{
	if (this != &other)
	{
		m_BaseString = other.m_BaseString;
		m_ComposedNullTerminated = other.m_ComposedNullTerminated;
		m_arguments = other.m_arguments;
		m_Forms = other.m_Forms;
		m_pkLanguage = other.m_pkLanguage;
	}
	return *this;
}

String& String::operator=(const char* szBaseString)
{
	// Keeps the arguments and the cached composition (closed); NULL is treated as "" (the closed library crashes).
	const size_t len = szBaseString != NULL ? strlen(szBaseString) : 0;
	if (len == 0)
		m_BaseString = SharedString();
	else
		m_BaseString = SharedString(szBaseString, len);
	SplitForms(m_BaseString, m_Forms);
	return *this;
}

const char* String::toUTF8()
{
	// Form 1 is composed into a NUL-terminated copy once; an empty result is recomposed on the next call.
	if (m_ComposedNullTerminated.GetLength() == 0)
	{
		size_t bytes = 0;
		const char* sz = toUTF8(bytes, 1);
		m_ComposedNullTerminated = SharedString(sz, bytes);
	}
	size_t len = 0;
	return m_ComposedNullTerminated.GetString(len);
}

const char* String::toUTF8(size_t& bytes, int nForm)
{
	if ((size_t)(nForm - 1) < NumForms(m_Forms))
	{
		SingleFormString& form = m_Forms[(size_t)(nForm - 1)];
		if (form.Compose(m_arguments))
		{
			const SharedString& composed = form.GetComposedString();
			return composed.GetString(bytes);
		}
	}
	bytes = 0;
	return "";
}

const char* String::GetBaseString(size_t& bytes) const
{
	return m_BaseString.GetString(bytes);
}

Language* String::GetLanguage() const
{
	return m_pkLanguage;
}

bool String::SetLanguage(Language* pkLanguage)
{
	m_pkLanguage = pkLanguage;
	return true;
}

bool String::PushArgument(const char* szText)
{
	m_arguments.push_back(ParameterArgument(String(szText)));
	return true;
}

bool String::PushArgument(const char* szText, size_t length)
{
	m_arguments.push_back(ParameterArgument(String(szText, (unsigned int)length)));
	return true;
}

bool String::PushArgument(const int val)
{
	m_arguments.push_back(ParameterArgument(val));
	return true;
}

bool String::PushArgument(const __int64 val)
{
	m_arguments.push_back(ParameterArgument(val));
	return true;
}

bool String::PushArgument(const float val)
{
	m_arguments.push_back(ParameterArgument(val));
	return true;
}

bool String::PushArgument(const double val)
{
	m_arguments.push_back(ParameterArgument(val));
	return true;
}

bool String::PushArgument(const String& strText)
{
	m_arguments.push_back(ParameterArgument(strText));
	return true;
}

size_t String::GetNumForms() const
{
	return m_Forms.size();
}

size_t String::GetFormByPlurality(int iPlurality, int nOffset) const
{
	for (size_t i = 0; i < m_Forms.size(); ++i)
	{
		if ((int)m_Forms[i].GetPlurality() == iPlurality)
		{
			if (nOffset == 0)
				return i + 1;
			--nOffset;
		}
	}
	return 0;
}

namespace
{
bool HasAllTraits(const SingleFormString& form, const std::vector<std::string>& traits)
{
	for (size_t t = 0; t < traits.size(); ++t)
		if (!form.HasTrait(traits[t].data(), traits[t].size()))
			return false;
	return true;
}
} // namespace

// nOffset is ignored by the closed library's GetFormByGender and GetFormByPluralityAndGender: the first match.
size_t String::GetFormByGender(const char* szGender, size_t lenGender, int) const
{
	int iGender = -1;
	std::vector<std::string> traits;
	if (!Detail::ParseGenderSpec(szGender, lenGender, iGender, traits))
		return 0;
	for (size_t i = 0; i < m_Forms.size(); ++i)
		if ((iGender == -1 || m_Forms[i].GetGender() == iGender) && HasAllTraits(m_Forms[i], traits))
			return i + 1;
	return 0;
}

size_t String::GetFormByPluralityAndGender(int iPlurality, const char* szGender, size_t lenGender, int) const
{
	int iGender = -1;
	std::vector<std::string> traits;
	if (!Detail::ParseGenderSpec(szGender, lenGender, iGender, traits))
		return 0;
	for (size_t i = 0; i < m_Forms.size(); ++i)
		if ((int)m_Forms[i].GetPlurality() == iPlurality && (iGender == -1 || m_Forms[i].GetGender() == iGender) &&
		    HasAllTraits(m_Forms[i], traits))
			return i + 1;
	return 0;
}

bool String::SetGender(const char* szGender, size_t lenGender)
{
	size_t parts = 1;
	for (size_t i = 0; i < lenGender; ++i)
		if (szGender[i] == '|')
			++parts;
	if (parts != 1)
	{
		if (parts != m_Forms.size())
			return false;
		size_t start = 0, nForm = 1;
		for (size_t i = 0; i < lenGender; ++i)
		{
			if (szGender[i] == '|')
			{
				if (!SetGender(nForm, szGender + start, i - start))
					return false;
				++nForm;
				start = i + 1;
			}
		}
		return SetGender(nForm, szGender + start, lenGender - start);
	}
	for (size_t nForm = 1; nForm <= m_Forms.size(); ++nForm)
		if (!SetGender(nForm, szGender, lenGender))
			return false;
	return true;
}

bool String::SetGender(size_t nForm, const char* szGender, size_t lenGender)
{
	if (nForm - 1 >= m_Forms.size())
		return false;
	int iGender = 0;  // an unknown gender word leaves 0 (closed)
	std::vector<std::string> traits;
	Detail::ParseGenderSpec(szGender, lenGender, iGender, traits);
	SingleFormString& form = m_Forms[nForm - 1];
	if (iGender != -1)
	{
		form.SetGender(static_cast<Gender>(iGender));
		form.m_Composed = false;
		form.m_Parsed = false;
	}
	for (size_t t = 0; t < traits.size(); ++t)
		if (!form.HasTrait(traits[t].data(), traits[t].size()))
			form.m_Traits.push_back(traits[t]);
	return true;
}

bool String::SetGender(Gender gender)
{
	for (size_t i = 0; i < m_Forms.size(); ++i)
	{
		m_Forms[i].m_Gender = gender;
		m_Forms[i].m_Composed = false;
		m_Forms[i].m_Parsed = false;
	}
	return true;
}

// Sets the gender when valid but always returns false (closed).
bool String::SetGender(size_t nForm, Gender gender)
{
	if (nForm - 1 < m_Forms.size() && (int)gender != -1)
	{
		m_Forms[nForm - 1].m_Gender = gender;
		m_Forms[nForm - 1].m_Composed = false;
		m_Forms[nForm - 1].m_Parsed = false;
	}
	return false;
}

Gender String::GetGender(size_t nForm) const
{
	if (nForm - 1 < m_Forms.size())
		return m_Forms[nForm - 1].GetGender();
	return GENDER_NEUTER;
}

namespace
{
void SetFormPlurality(SingleFormString& form, size_t p)
{
	form.m_Plurality = p;
	form.m_Composed = false;
	form.m_Parsed = false;
}

// atoi of at most 7 characters (strncpy_s into char[8]); -1 when longer.
int ParsePlurality(const char* sz, size_t len)
{
	char buf[8] = {};
	if (len > 7)
		return -1;
	if (len != 0)
		memcpy(buf, sz, len);
	return atoi(buf);
}
} // namespace

bool String::SetPlurality(const char* szPlurality, size_t lenPlurality)
{
	const IPluralityRule* pkRule = Detail::RuleOf(m_pkLanguage);
	if (pkRule == NULL)
		return false;
	const unsigned int uMax = pkRule->GetMaxPluralForms();
	size_t parts = 1;
	for (size_t i = 0; i < lenPlurality; ++i)
		if (szPlurality[i] == '|')
			++parts;
	if (parts == 1 && lenPlurality < 8)
	{
		const unsigned int v = (unsigned int)ParsePlurality(szPlurality, lenPlurality);
		if (v == 0)
		{
			Detail::Log(ILocalizationLogger::TYPE_ERROR, "Plurality Starts at 1.");
			return false;
		}
		if (v <= uMax)
		{
			for (size_t i = 0; i < m_Forms.size(); ++i)
				SetFormPlurality(m_Forms[i], v);
			return true;
		}
	}
	if (parts != m_Forms.size())
		return false;
	size_t start = 0, nForm = 0;
	for (size_t i = 0; i <= lenPlurality; ++i)
	{
		if (i == lenPlurality || szPlurality[i] == '|')
		{
			if (i - start > 7)
				return false;
			const unsigned int v = (unsigned int)ParsePlurality(szPlurality + start, i - start);
			if (v == 0)
			{
				Detail::Log(ILocalizationLogger::TYPE_ERROR, "Plurality Starts at 1.");
				return false;
			}
			if (v > uMax)
				return false;
			SetFormPlurality(m_Forms[nForm], v);
			++nForm;
			start = i + 1;
		}
	}
	return true;
}

// Note the strict "<" against the maximum (the string overload uses "<="), as the closed library.
bool String::SetPlurality(size_t iPlurality)
{
	const IPluralityRule* pkRule = Detail::RuleOf(m_pkLanguage);
	if (pkRule == NULL)
		return false;
	if (iPlurality == 0)
	{
		Detail::Log(ILocalizationLogger::TYPE_ERROR, "Plurality Starts at 1.");
		return false;
	}
	if (iPlurality >= pkRule->GetMaxPluralForms())
		return false;
	for (size_t i = 0; i < m_Forms.size(); ++i)
		SetFormPlurality(m_Forms[i], iPlurality);
	return true;
}

bool String::SetPlurality(size_t nForm, size_t iPlurality)
{
	if (nForm - 1 >= m_Forms.size())
		return false;
	const IPluralityRule* pkRule = Detail::RuleOf(m_pkLanguage);
	if (pkRule == NULL)
		return false;
	if (iPlurality == 0)
	{
		Detail::Log(ILocalizationLogger::TYPE_ERROR, "Plurality Starts at 1.");
		return false;
	}
	if (iPlurality >= pkRule->GetMaxPluralForms())
		return false;
	SetFormPlurality(m_Forms[nForm - 1], iPlurality);
	return true;
}

size_t String::GetPlurality(size_t nForm) const
{
	if (nForm - 1 < m_Forms.size())
		return m_Forms[nForm - 1].GetPlurality();
	return 0;
}

bool String::Compose(size_t nForm)
{
	if (nForm - 1 < m_Forms.size())
		return m_Forms[nForm - 1].Compose(m_arguments);
	return false;
}

bool String::Parse(size_t nForm)
{
	if (nForm - 1 < m_Forms.size())
		return m_Forms[nForm - 1].Parse();
	return false;
}
} // namespace Localization
