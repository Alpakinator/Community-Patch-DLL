// srv_scene_bundle.cpp - the scene bundle of the offline scene harness: manifest, member forms, checker, writer (epic 0301,
// task 3902; contract: research/06-tooling-and-workflows/offline-scene-harness.md sections 1, 2 and 10).
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// The JSON reader and writer here are this file's own (scene.json's fixed keys only need objects, arrays, strings,
// integers and booleans; numbers with a fraction are read for the named cameras' JSON). SHA-256 comes from bcrypt.dll,
// loaded by name as proxy/src/proxy_pack.cpp does (no import library is added to server.exe's link).
#include "srv_scene_bundle.h"
#include "srv_decal_tap.h"   // issue 0364: the decal member's checks (DecalTapReader::loadImage)
#include "shell_model_tap.h"   // ticket 4875: the model_tap member's frame image
#if defined(__has_include)
#if __has_include("c5r/landmarks/tap.h")
#include "c5r/landmarks/tap.h"   // ticket 4875: readTap, the one content check of a tap frame (4873, lead2-0561 #6146)
#define SRV_BUNDLE_HAVE_READTAP 1
#endif
#endif
#include "c5r/fow/fow.h"     // ticket 0403: particles.bin's state form (ParticleSettings, Particle; no code linked)
#include "srv_liveterrain.h"  // epic 0465, ticket 4907: render_live_terrain's checks (CheckLiveTerrainConfig)

#include <windows.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <set>

namespace srvrender {
namespace scenebundle {

namespace {

// ---- SHA-256 through bcrypt.dll ----------------------------------------------------------------------------------------
typedef LONG(WINAPI* OpenAlgFn)(void**, LPCWSTR, LPCWSTR, ULONG);
typedef LONG(WINAPI* CreateHashFn)(void*, void**, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* HashDataFn)(void*, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* FinishHashFn)(void*, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* DestroyHashFn)(void*);

struct Bcrypt
{
	void* alg = nullptr;
	CreateHashFn createHash = nullptr;
	HashDataFn hashData = nullptr;
	FinishHashFn finishHash = nullptr;
	DestroyHashFn destroyHash = nullptr;
	Bcrypt()
	{
		HMODULE bc = LoadLibraryW(L"bcrypt.dll");
		if(!bc)
			return;
		OpenAlgFn openAlg = (OpenAlgFn)(void*)GetProcAddress(bc, "BCryptOpenAlgorithmProvider");
		createHash = (CreateHashFn)(void*)GetProcAddress(bc, "BCryptCreateHash");
		hashData = (HashDataFn)(void*)GetProcAddress(bc, "BCryptHashData");
		finishHash = (FinishHashFn)(void*)GetProcAddress(bc, "BCryptFinishHash");
		destroyHash = (DestroyHashFn)(void*)GetProcAddress(bc, "BCryptDestroyHash");
		if(!openAlg || !createHash || !hashData || !finishHash || !destroyHash || openAlg(&alg, L"SHA256", NULL, 0) != 0)
			alg = nullptr;
	}
};

const Bcrypt& Bc()
{
	static Bcrypt b;   // thread-safe initialisation (C++11); the algorithm handle is shared, each hash has its own
	return b;
}

class Sha256
{
public:
	~Sha256()
	{
		if(m_h)
			Bc().destroyHash(m_h);
	}
	bool begin()
	{
		const Bcrypt& b = Bc();
		return b.alg && b.createHash(b.alg, &m_h, NULL, 0, NULL, 0, 0) == 0;
	}
	bool update(const void* p, size_t n)
	{
		const uint8_t* c = (const uint8_t*)p;
		while(n)
		{
			ULONG k = (ULONG)(std::min)(n, (size_t)1 << 30);
			if(Bc().hashData(m_h, (PUCHAR)c, k, 0) != 0)
				return false;
			c += k;
			n -= k;
		}
		return true;
	}
	std::string finishHex()
	{
		uint8_t d[32];
		if(Bc().finishHash(m_h, d, 32, 0) != 0)
			return std::string();
		static const char hx[] = "0123456789abcdef";
		std::string s(64, '0');
		for(int i = 0; i < 32; ++i)
		{
			s[2 * i] = hx[d[i] >> 4];
			s[2 * i + 1] = hx[d[i] & 15];
		}
		return s;
	}
private:
	void* m_h = nullptr;
};

// ---- refusals ----------------------------------------------------------------------------------------------------------
c5r::Err ErrOf(Refusal r)
{
	switch(r)
	{
	case Refusal::None: return c5r::Err::Ok;
	case Refusal::NoManifest: case Refusal::Io: return c5r::Err::Io;
	case Refusal::Truncated: return c5r::Err::Truncated;
	case Refusal::BadMagic: case Refusal::UnknownFormat: return c5r::Err::BadMagic;
	case Refusal::BadVersion: case Refusal::OlderVersion: case Refusal::NewerVersion: return c5r::Err::BadVersion;
	case Refusal::MissingMember: return c5r::Err::NotFound;
	default: return c5r::Err::BadLayout;
	}
}

c5r::Status Refuse(CheckResult* res, Refusal r, const std::string& why, const std::string& member = std::string())
{
	if(res)
	{
		res->refusal = r;
		res->why = why;
		if(!member.empty())
			res->member = member;
	}
	return c5r::Status::fail(ErrOf(r), std::string(RefusalName(r)) + ": " + why);
}

std::string Fmt(const char* f, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, f);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, f, ap);
	va_end(ap);
	return buf;
}

// ---- little-endian cursor -------------------------------------------------------------------------------------------------
struct Cur
{
	const uint8_t* p;
	size_t n, at;
	Cur(const std::vector<uint8_t>& b, size_t start = 0) : p(b.data()), n(b.size()), at(start) {}
	bool has(size_t k) const { return at <= n && n - at >= k; }
	template <class T> bool get(T* v)
	{
		if(!has(sizeof(T)))
			return false;
		memcpy(v, p + at, sizeof(T));
		at += sizeof(T);
		return true;
	}
	size_t left() const { return at <= n ? n - at : 0; }
};

template <class T> void Put(std::vector<uint8_t>* out, const T& v)
{
	const uint8_t* c = (const uint8_t*)&v;
	out->insert(out->end(), c, c + sizeof(T));
}

void PutBytes(std::vector<uint8_t>* out, const void* p, size_t n)
{
	if(n)
		out->insert(out->end(), (const uint8_t*)p, (const uint8_t*)p + n);
}

void PutHeader(std::vector<uint8_t>* out, const char magic[4], uint32_t version)
{
	FileHeader h;
	memcpy(h.magic, magic, 4);
	h.version = version;
	h.headerBytes = 16;
	h.reserved = 0;
	Put(out, h);
}

c5r::Status CheckHeader(const std::vector<uint8_t>& b, const char magic[4], uint32_t version, CheckResult* res)
{
	if(b.size() < sizeof(FileHeader))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, shorter than the 16-byte header", (unsigned)b.size()));
	FileHeader h;
	memcpy(&h, b.data(), sizeof(h));
	if(memcmp(h.magic, magic, 4) != 0)
		return Refuse(res, Refusal::BadMagic, Fmt("magic is not %.4s", magic));
	if(h.version != version)
		return Refuse(res, Refusal::BadVersion, Fmt("file form version %u, this reader reads %u", h.version, version));
	if(h.headerBytes != 16 || h.reserved != 0)
		return Refuse(res, Refusal::BadVersion, Fmt("header bytes %u (16 expected) or reserved word %u (0 expected)",
		                                            h.headerBytes, h.reserved));
	return c5r::Status::Ok();
}

bool Finite(double d)
{
	return std::isfinite(d);
}

// ---- JSON ---------------------------------------------------------------------------------------------------------------
struct JVal
{
	enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
	bool b = false;
	bool isInt = false;
	int64_t i = 0;
	double d = 0;
	std::string s;
	std::vector<JVal> a;                  // Arr items, Obj values
	std::vector<std::string> keys;        // Obj keys (in file order)
};

class JParser
{
public:
	JParser(const std::string& s) : m_s(s) {}
	bool parse(JVal* out, std::string* why)
	{
		ws();
		if(!value(out, 0))
		{
			*why = m_why;
			return false;
		}
		ws();
		if(m_at != m_s.size())
		{
			*why = Fmt("text after the JSON value at byte %u", (unsigned)m_at);
			return false;
		}
		return true;
	}
private:
	bool err(const char* what)
	{
		if(m_why.empty())
			m_why = Fmt("%s at byte %u", what, (unsigned)m_at);
		return false;
	}
	void ws()
	{
		while(m_at < m_s.size() && (m_s[m_at] == ' ' || m_s[m_at] == '\t' || m_s[m_at] == '\n' || m_s[m_at] == '\r'))
			++m_at;
	}
	bool lit(const char* w)
	{
		size_t k = strlen(w);
		if(m_s.compare(m_at, k, w) != 0)
			return err("bad literal");
		m_at += k;
		return true;
	}
	static void utf8(std::string* o, uint32_t c)
	{
		if(c < 0x80)
			o->push_back((char)c);
		else if(c < 0x800)
		{
			o->push_back((char)(0xC0 | (c >> 6)));
			o->push_back((char)(0x80 | (c & 0x3F)));
		}
		else if(c < 0x10000)
		{
			o->push_back((char)(0xE0 | (c >> 12)));
			o->push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			o->push_back((char)(0x80 | (c & 0x3F)));
		}
		else
		{
			o->push_back((char)(0xF0 | (c >> 18)));
			o->push_back((char)(0x80 | ((c >> 12) & 0x3F)));
			o->push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			o->push_back((char)(0x80 | (c & 0x3F)));
		}
	}
	bool hex4(uint32_t* v)
	{
		if(m_at + 4 > m_s.size())
			return err("short \\u escape");
		uint32_t r = 0;
		for(int k = 0; k < 4; ++k)
		{
			char c = m_s[m_at++];
			r <<= 4;
			if(c >= '0' && c <= '9') r |= (uint32_t)(c - '0');
			else if(c >= 'a' && c <= 'f') r |= (uint32_t)(c - 'a' + 10);
			else if(c >= 'A' && c <= 'F') r |= (uint32_t)(c - 'A' + 10);
			else return err("bad \\u escape");
		}
		*v = r;
		return true;
	}
	bool str(std::string* o)
	{
		if(m_at >= m_s.size() || m_s[m_at] != '"')
			return err("string expected");
		++m_at;
		while(true)
		{
			if(m_at >= m_s.size())
				return err("unterminated string");
			unsigned char c = (unsigned char)m_s[m_at++];
			if(c == '"')
				return true;
			if(c < 0x20)
				return err("control character in a string");
			if(c != '\\')
			{
				o->push_back((char)c);
				continue;
			}
			if(m_at >= m_s.size())
				return err("unterminated escape");
			char e = m_s[m_at++];
			switch(e)
			{
			case '"': o->push_back('"'); break;
			case '\\': o->push_back('\\'); break;
			case '/': o->push_back('/'); break;
			case 'b': o->push_back('\b'); break;
			case 'f': o->push_back('\f'); break;
			case 'n': o->push_back('\n'); break;
			case 'r': o->push_back('\r'); break;
			case 't': o->push_back('\t'); break;
			case 'u':
			{
				uint32_t c1;
				if(!hex4(&c1))
					return false;
				if(c1 >= 0xD800 && c1 < 0xDC00)
				{
					uint32_t c2;
					if(m_at + 2 > m_s.size() || m_s[m_at] != '\\' || m_s[m_at + 1] != 'u')
						return err("lone surrogate");
					m_at += 2;
					if(!hex4(&c2) || c2 < 0xDC00 || c2 >= 0xE000)
						return err("bad surrogate pair");
					c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
				}
				else if(c1 >= 0xDC00 && c1 < 0xE000)
					return err("lone surrogate");
				utf8(o, c1);
				break;
			}
			default: return err("bad escape");
			}
		}
	}
	bool num(JVal* v)
	{
		size_t st = m_at;
		if(m_at < m_s.size() && m_s[m_at] == '-')
			++m_at;
		size_t d0 = m_at;
		while(m_at < m_s.size() && m_s[m_at] >= '0' && m_s[m_at] <= '9')
			++m_at;
		if(m_at == d0)
			return err("bad number");
		if(m_s[d0] == '0' && m_at - d0 > 1)
			return err("leading zero in a number");
		bool isInt = true;
		if(m_at < m_s.size() && m_s[m_at] == '.')
		{
			isInt = false;
			++m_at;
			size_t f0 = m_at;
			while(m_at < m_s.size() && m_s[m_at] >= '0' && m_s[m_at] <= '9')
				++m_at;
			if(m_at == f0)
				return err("bad fraction");
		}
		if(m_at < m_s.size() && (m_s[m_at] == 'e' || m_s[m_at] == 'E'))
		{
			isInt = false;
			++m_at;
			if(m_at < m_s.size() && (m_s[m_at] == '+' || m_s[m_at] == '-'))
				++m_at;
			size_t e0 = m_at;
			while(m_at < m_s.size() && m_s[m_at] >= '0' && m_s[m_at] <= '9')
				++m_at;
			if(m_at == e0)
				return err("bad exponent");
		}
		std::string t = m_s.substr(st, m_at - st);
		v->t = JVal::Num;
		v->d = strtod(t.c_str(), nullptr);
		if(isInt)
		{
			errno = 0;
			long long x = _strtoi64(t.c_str(), nullptr, 10);
			if(errno == ERANGE)
				return err("integer out of range");
			v->i = (int64_t)x;
			v->isInt = true;
		}
		return true;
	}
	bool value(JVal* v, int depth)
	{
		if(depth > 32)
			return err("nested too deep");
		if(m_at >= m_s.size())
			return err("value expected");
		char c = m_s[m_at];
		if(c == '{')
		{
			v->t = JVal::Obj;
			++m_at;
			ws();
			if(m_at < m_s.size() && m_s[m_at] == '}')
			{
				++m_at;
				return true;
			}
			while(true)
			{
				ws();
				std::string k;
				if(!str(&k))
					return false;
				if(std::find(v->keys.begin(), v->keys.end(), k) != v->keys.end())
					return err(("duplicate key \"" + k + "\"").c_str());
				ws();
				if(m_at >= m_s.size() || m_s[m_at] != ':')
					return err("':' expected");
				++m_at;
				ws();
				JVal x;
				if(!value(&x, depth + 1))
					return false;
				v->keys.push_back(k);
				v->a.push_back(std::move(x));
				ws();
				if(m_at < m_s.size() && m_s[m_at] == ',')
				{
					++m_at;
					continue;
				}
				if(m_at < m_s.size() && m_s[m_at] == '}')
				{
					++m_at;
					return true;
				}
				return err("',' or '}' expected");
			}
		}
		if(c == '[')
		{
			v->t = JVal::Arr;
			++m_at;
			ws();
			if(m_at < m_s.size() && m_s[m_at] == ']')
			{
				++m_at;
				return true;
			}
			while(true)
			{
				ws();
				JVal x;
				if(!value(&x, depth + 1))
					return false;
				v->a.push_back(std::move(x));
				ws();
				if(m_at < m_s.size() && m_s[m_at] == ',')
				{
					++m_at;
					continue;
				}
				if(m_at < m_s.size() && m_s[m_at] == ']')
				{
					++m_at;
					return true;
				}
				return err("',' or ']' expected");
			}
		}
		if(c == '"')
		{
			v->t = JVal::Str;
			return str(&v->s);
		}
		if(c == 't')
		{
			v->t = JVal::Bool;
			v->b = true;
			return lit("true");
		}
		if(c == 'f')
		{
			v->t = JVal::Bool;
			return lit("false");
		}
		if(c == 'n')
			return lit("null");
		return num(v);
	}
	const std::string& m_s;
	size_t m_at = 0;
	std::string m_why;
};

void JEsc(std::string* o, const std::string& s)
{
	o->push_back('"');
	for(size_t k = 0; k < s.size(); ++k)
	{
		unsigned char c = (unsigned char)s[k];
		if(c == '"') *o += "\\\"";
		else if(c == '\\') *o += "\\\\";
		else if(c == '\n') *o += "\\n";
		else if(c == '\r') *o += "\\r";
		else if(c == '\t') *o += "\\t";
		else if(c < 0x20) *o += Fmt("\\u%04x", (unsigned)c);
		else o->push_back((char)c);
	}
	o->push_back('"');
}

// A small builder for the writer: values are written in key order (std::map), two-space indent.
struct JOut
{
	std::string s;
	void nl(int ind)
	{
		s.push_back('\n');
		s.append((size_t)ind * 2, ' ');
	}
};

void WriteStrMap(JOut& o, const std::map<std::string, std::string>& m, int ind)
{
	if(m.empty())
	{
		o.s += "{}";
		return;
	}
	o.s += "{";
	bool first = true;
	for(const auto& kv : m)
	{
		if(!first)
			o.s += ",";
		first = false;
		o.nl(ind + 1);
		JEsc(&o.s, kv.first);
		o.s += ": ";
		JEsc(&o.s, kv.second);
	}
	o.nl(ind);
	o.s += "}";
}

// The manifest object reader: each key is taken once; finish() refuses the keys never taken.
class JObj
{
public:
	JObj(const JVal* v, const std::string& path, CheckResult* res) : m_v(v), m_path(path), m_res(res)
	{
		if(v)
			m_used.assign(v->keys.size(), false);
	}
	bool failed() const { return m_failed; }
	const JVal* take(const char* key, JVal::Type t)
	{
		if(m_failed)
			return nullptr;
		for(size_t k = 0; k < m_v->keys.size(); ++k)
			if(m_v->keys[k] == key)
			{
				m_used[k] = true;
				const JVal* x = &m_v->a[k];
				if(x->t != t || (t == JVal::Num && !x->isInt))
				{
					fail(Refusal::BadValue, "\"" + name(key) + "\" has the wrong JSON type (" + TypeName(t) + " expected)");
					return nullptr;
				}
				return x;
			}
		fail(Refusal::MissingKey, "key \"" + name(key) + "\" missing");
		return nullptr;
	}
	bool str(const char* key, std::string* out)
	{
		const JVal* x = take(key, JVal::Str);
		if(!x)
			return false;
		*out = x->s;
		return true;
	}
	bool i64(const char* key, int64_t* out)
	{
		const JVal* x = take(key, JVal::Num);
		if(!x)
			return false;
		*out = x->i;
		return true;
	}
	bool u32(const char* key, uint32_t* out)
	{
		int64_t v;
		if(!i64(key, &v))
			return false;
		if(v < 0 || v > 0xFFFFFFFFll)
		{
			fail(Refusal::BadValue, "\"" + name(key) + "\" out of the range 0..4294967295");
			return false;
		}
		*out = (uint32_t)v;
		return true;
	}
	bool finish()
	{
		if(m_failed)
			return false;
		for(size_t k = 0; k < m_used.size(); ++k)
			if(!m_used[k])
			{
				fail(Refusal::UnknownKey, "unknown key \"" + name(m_v->keys[k].c_str()) + "\"");
				return false;
			}
		return true;
	}
	void fail(Refusal r, const std::string& why)
	{
		if(!m_failed)
			Refuse(m_res, r, why);
		m_failed = true;
	}
	std::string name(const char* key) const { return m_path.empty() ? std::string(key) : m_path + "." + key; }
	static const char* TypeName(JVal::Type t)
	{
		switch(t)
		{
		case JVal::Bool: return "boolean";
		case JVal::Num: return "integer";
		case JVal::Str: return "string";
		case JVal::Arr: return "array";
		case JVal::Obj: return "object";
		default: return "null";
		}
	}
private:
	const JVal* m_v;
	std::string m_path;
	CheckResult* m_res;
	std::vector<bool> m_used;
	bool m_failed = false;
};

bool StrMap(const JVal* v, const std::string& path, std::map<std::string, std::string>* out, CheckResult* res)
{
	for(size_t k = 0; k < v->keys.size(); ++k)
	{
		if(v->a[k].t != JVal::Str)
		{
			Refuse(res, Refusal::BadValue, "\"" + path + "." + v->keys[k] + "\" is not a string");
			return false;
		}
		(*out)[v->keys[k]] = v->a[k].s;
	}
	return true;
}

bool IsHex64(const std::string& s)
{
	if(s.size() != 64)
		return false;
	for(char c : s)
		if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}

bool StartsWith(const std::string& s, const char* p)
{
	return s.compare(0, strlen(p), p) == 0;
}

bool EndsWith(const std::string& s, const char* p)
{
	size_t k = strlen(p);
	return s.size() >= k && s.compare(s.size() - k, k, p) == 0;
}

// A clean relative path: segments of [^/\\:] without ".", "..", or empty ones; not scene.json, not under .partial.
bool NotClean(std::string* why, const std::string& w)
{
	*why = w;
	return false;
}

bool CleanPath(const std::string& p, std::string* why)
{
	if(p.empty())
		return NotClean(why, "empty path");
	if(p.find('\\') != std::string::npos || p.find(':') != std::string::npos)
		return NotClean(why, "backslash or ':' in \"" + p + "\"");
	size_t st = 0;
	while(true)
	{
		size_t e = p.find('/', st);
		std::string seg = p.substr(st, e == std::string::npos ? std::string::npos : e - st);
		if(seg.empty() || seg == "." || seg == "..")
			return NotClean(why, "segment \"" + seg + "\" in \"" + p + "\"");
		for(char c : seg)
			if((unsigned char)c < 0x20)
				return NotClean(why, "control character in \"" + p + "\"");
		if(e == std::string::npos)
			break;
		st = e + 1;
	}
	if(p == "scene.json" || p == "scene.json.tmp" || StartsWith(p, ".partial/") || p == ".partial")
		return NotClean(why, "\"" + p + "\" is a reserved name");
	return true;
}

bool ParseFrameIdName(const std::string& name, const char* suffix, int64_t* id)
{
	if(!StartsWith(name, "dumps/") || !EndsWith(name, suffix))
		return false;
	std::string mid = name.substr(6, name.size() - 6 - strlen(suffix));
	if(mid.empty() || mid.size() > 18)
		return false;
	size_t k = mid[0] == '-' ? 1 : 0;
	if(k == mid.size())
		return false;
	for(; k < mid.size(); ++k)
		if(mid[k] < '0' || mid[k] > '9')
			return false;
	*id = (int64_t)_strtoi64(mid.c_str(), nullptr, 10);
	return Fmt("%lld", (long long)*id) == mid;   // canonical decimal only
}

// The path form of each kind (BadPath otherwise), so that the player finds every member by its name.
bool PathFitsKind(const std::string& p, MemberKind k, std::string* why)
{
	bool ok = false;
	int64_t id;
	switch(k)
	{
	case MemberKind::Heightfield: ok = StartsWith(p, "heightfield/") && EndsWith(p, ".c5hf") && p.find('/', 12) == std::string::npos; break;
	case MemberKind::WaterTap: ok = StartsWith(p, "water/") && EndsWith(p, ".wtap") && p.find('/', 6) == std::string::npos; break;
	case MemberKind::OverlayTexture:
		ok = p.size() == 8 + 16 + 4 && StartsWith(p, "overlay/") && EndsWith(p, ".ovl");
		for(size_t c = 8; ok && c < 24; ++c)
			ok = (p[c] >= '0' && p[c] <= '9') || (p[c] >= 'a' && p[c] <= 'f');
		break;
	case MemberKind::Feed: ok = p == "feed.bin"; break;
	case MemberKind::Frames: ok = p == "frames.bin"; break;
	case MemberKind::CameraBlocks: ok = p == "cameras.bin"; break;
	case MemberKind::WaterClock: ok = p == "water_clock.bin"; break;
	case MemberKind::FogClock: ok = p == "fog_clock.bin"; break;   // format version 2 (issue 3911)
	case MemberKind::Shadow: ok = p == "shadow.bin"; break;          // issue 4308
	case MemberKind::ShadowTexels:
		ok = p.size() == 7 + 16 + 5 && StartsWith(p, "shadow/") && EndsWith(p, ".shtx");
		for(size_t c = 7; ok && c < 23; ++c)
			ok = (p[c] >= '0' && p[c] <= '9') || (p[c] >= 'a' && p[c] <= 'f');
		break;
	case MemberKind::IceMesh:
		ok = p.size() == 4 + 3 + 10 + 4 && StartsWith(p, "ice/gen") && EndsWith(p, ".ice");
		for(size_t c = 7; ok && c < 17; ++c)
			ok = p[c] >= '0' && p[c] <= '9';
		break;
	case MemberKind::DecalTap:   // format version 3 (issue 0364): decals/load<4 digits>.dtap
		ok = p.size() == 11 + 4 + 5 && StartsWith(p, "decals/load") && EndsWith(p, ".dtap");
		for(size_t c = 11; ok && c < 15; ++c)
			ok = p[c] >= '0' && p[c] <= '9';
		break;
	case MemberKind::UnitsDb:   // format version 5 (issue 0440): units/gen<10 digits>.db
		ok = p.size() == 9 + 10 + 3 && StartsWith(p, "units/gen") && EndsWith(p, ".db");
		for(size_t c = 9; ok && c < 19; ++c)
			ok = p[c] >= '0' && p[c] <= '9';
		break;
	case MemberKind::UnitsHistory: ok = p == "units_history.bin"; break;   // format version 5 (issue 0440)
	case MemberKind::FogParticles: ok = p == "particles.bin"; break;       // format version 6 (ticket 0403)
	case MemberKind::ForestState: ok = p == "forest.bin"; break;           // format version 8 (ticket 0495)
	case MemberKind::NearPairs: ok = p == "near.bin"; break;               // format version 8 (ticket 0495)
	case MemberKind::ModelTap: ok = ModelTapIndex(p, nullptr); break;      // format version 9 (ticket 4875)
	case MemberKind::ModelGeom: ok = ModelGeomIndex(p, nullptr); break;    // format version 9, optional (ticket 4888)
	case MemberKind::WaterFrames: ok = p == "water_frames.bin"; break;     // format version 11 (ticket 0622)
	case MemberKind::DumpColour: ok = ParseFrameIdName(p, ".bmp", &id); break;
	case MemberKind::DumpDepth: ok = ParseFrameIdName(p, ".depth.f32", &id); break;
	case MemberKind::NamedCamera: ok = StartsWith(p, "cameras/") && EndsWith(p, ".json") && p.find('/', 8) == std::string::npos; break;
	case MemberKind::NamedCameraBlock: ok = StartsWith(p, "cameras/") && EndsWith(p, ".vpcam") && p.find('/', 8) == std::string::npos; break;
	case MemberKind::StockShot: ok = StartsWith(p, "refs/") && EndsWith(p, ".png") && p.find('/', 5) == std::string::npos; break;
	case MemberKind::StockCapture: ok = StartsWith(p, "refs/") && EndsWith(p, ".rdc") && p.find('/', 5) == std::string::npos; break;
	case MemberKind::StockExport:
	{
		size_t e = p.find(".export/");
		ok = StartsWith(p, "refs/") && e != std::string::npos && e > 5 && p.find('/', 5) == e + 7 && e + 8 < p.size();
		break;
	}
	}
	if(!ok)
		*why = "\"" + p + "\" is not a path of kind " + MemberKindName(k);
	return ok;
}

// ---- files -------------------------------------------------------------------------------------------------------------
std::wstring Join(const std::wstring& dir, const std::string& rel)
{
	std::wstring w = dir;
	if(!w.empty() && w.back() != L'/' && w.back() != L'\\')
		w.push_back(L'/');
	return w + Widen(rel);
}

bool IsDir(const std::wstring& p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsFile(const std::wstring& p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Every file below dir, relative, forward slashes (UTF-8), sorted. False when a folder cannot be listed.
bool ListFiles(const std::wstring& dir, const std::string& rel, std::vector<std::string>* out)
{
	WIN32_FIND_DATAW fd;
	std::wstring pat = (rel.empty() ? dir : Join(dir, rel)) + L"/*";
	HANDLE h = FindFirstFileExW(pat.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
	if(h == INVALID_HANDLE_VALUE)
		return GetLastError() == ERROR_FILE_NOT_FOUND;
	bool ok = true;
	do
	{
		if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
			continue;
		std::string name = (rel.empty() ? std::string() : rel + "/") + Narrow(fd.cFileName);
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			ok = ListFiles(dir, name, out) && ok;
		else
			out->push_back(name);
	} while(FindNextFileW(h, &fd));
	FindClose(h);
	if(rel.empty())
		std::sort(out->begin(), out->end());
	return ok;
}

c5r::Status ReadWhole(const std::wstring& path, std::vector<uint8_t>* out)
{
	out->clear();
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if(h == INVALID_HANDLE_VALUE)
		return c5r::Status::fail(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ?
		                             c5r::Err::NotFound : c5r::Err::Io,
		                         Fmt("cannot open (error %lu)", GetLastError()));
	LARGE_INTEGER sz;
	if(!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || (uint64_t)sz.QuadPart > ((uint64_t)1 << 40))
	{
		CloseHandle(h);
		return c5r::Status::fail(c5r::Err::Io, "cannot size");
	}
	try
	{
		out->resize((size_t)sz.QuadPart);
	}
	catch(...)
	{
		CloseHandle(h);
		return c5r::Status::fail(c5r::Err::OutOfMemory, Fmt("%lld bytes", (long long)sz.QuadPart));
	}
	size_t at = 0;
	while(at < out->size())
	{
		DWORD k = (DWORD)(std::min)(out->size() - at, (size_t)1 << 26), got = 0;
		if(!ReadFile(h, out->data() + at, k, &got, NULL) || got == 0)
		{
			CloseHandle(h);
			return c5r::Status::fail(c5r::Err::Io, Fmt("read failed at byte %llu", (unsigned long long)at));
		}
		at += got;
	}
	CloseHandle(h);
	return c5r::Status::Ok();
}

bool MakeDirs(const std::wstring& root, const std::string& relFile)
{
	size_t st = 0;
	while(true)
	{
		size_t e = relFile.find('/', st);
		if(e == std::string::npos)
			return true;
		std::wstring d = Join(root, relFile.substr(0, e));
		if(!IsDir(d) && !CreateDirectoryW(d.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
		st = e + 1;
	}
}

// ---- the stream heads (frames.bin and cameras.bin carry their record size) ------------------------------------------------
const char* MagicOf(MemberKind k)
{
	switch(k)
	{
	case MemberKind::WaterTap: return kMagicWaterTap;
	case MemberKind::OverlayTexture: return kMagicOverlay;
	case MemberKind::Feed: return kMagicFeed;
	case MemberKind::Frames: return kMagicFrames;
	case MemberKind::CameraBlocks: case MemberKind::NamedCameraBlock: return kMagicCameras;
	case MemberKind::WaterClock: return kMagicWaterClock;
	case MemberKind::FogClock: return kMagicFogClock;   // issue 3911
	case MemberKind::Shadow: return kMagicShadow;       // issue 4308
	case MemberKind::WaterFrames: return kMagicWaterFrames;   // ticket 0622
	default: return nullptr;
	}
}

c5r::Status ParseRecordStream(const std::vector<uint8_t>& b, const char magic[4], uint32_t recBytes, const char* what,
                              size_t* count, CheckResult* res, uint32_t formVersion = 1)
{
	C5R_TRY(CheckHeader(b, magic, formVersion, res));
	Cur c(b, 16);
	uint32_t rb;
	if(!c.get(&rb))
		return Refuse(res, Refusal::Truncated, Fmt("no %s size after the header", what));
	if(rb != recBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("%s size %u, this reader reads %u", what, rb, recBytes));
	if(c.left() % recBytes)
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes after the head are not whole %ss of %u",
		                                           (unsigned)c.left(), what, recBytes));
	*count = c.left() / recBytes;
	return c5r::Status::Ok();
}

}  // namespace

// ==== names =============================================================================================================
const char* const kCounterNames[5] = {"water_refused", "overlay_refused", "overlay_stale", "feed_overflow_events",
                                      "record_errors"};

static const char* const kKindNames[] = {"", "heightfield", "water_tap", "overlay_texture", "feed", "frames",
                                         "camera_blocks", "water_clock", "dump_colour", "dump_depth", "named_camera",
                                         "named_camera_block", "stock_shot", "stock_capture", "stock_export",
                                         "fog_clock",    // fog_clock: format version 2 (issue 3911)
                                         "shadow", "shadow_texels", "ice_mesh",    // issue 4308
                                         "decal_tap",                              // format version 3 (issue 0364)
                                         "units_db", "units_history",              // format version 5 (issue 0440)
                                         "fog_particles",                          // format version 6 (ticket 0403)
                                         "forest_state", "near_pairs",             // format version 8 (ticket 0495)
                                         "model_tap",                              // format version 9 (ticket 4875)
                                         "model_geom",                             // format version 9, optional (ticket 4888)
                                         "water_frames"};                          // format version 11 (ticket 0622)

const char* MemberKindName(MemberKind k)
{
	uint32_t i = (uint32_t)k;
	return i >= 1 && i < sizeof(kKindNames) / sizeof(kKindNames[0]) ? kKindNames[i] : "?";
}

bool MemberKindFromName(const std::string& s, MemberKind* out)
{
	for(uint32_t i = 1; i < sizeof(kKindNames) / sizeof(kKindNames[0]); ++i)
		if(s == kKindNames[i])
		{
			*out = (MemberKind)i;
			return true;
		}
	return false;
}

uint32_t MemberKindVersion(MemberKind k)
{
	if(k == MemberKind::Shadow)
		return 2;   // ticket 4749: the record with the unit-mask flag and the raw size (format version 10)
	return k == MemberKind::Feed ? kFeedFormVersion : 1;   // issue 4457: feed.bin's form 2 (format version 4)
}

uint32_t MemberKindVersionFor(MemberKind k, uint32_t formatVersion)
{
	if(k == MemberKind::Shadow && formatVersion < 10)
		return 1;   // ticket 4749: a version-9 bundle's shadow.bin (344-byte records)
	return MemberKindVersion(k);
}

// ticket 0493: one line per format version (the header's blocks at kFormatVersion), named by the OlderVersion refusal
const char* FormatVersionChange(uint32_t v)
{
	switch(v)
	{
	case 2: return "the fog's inputs: fog_clock.bin, feed.bin's PLOTS of caller 3 (issue 3911)";
	case 3: return "the decals, the shadow and the ice members (issue 0364)";
	case 4: return "feed.bin's file form 2 with the unit feed's records (issue 4457)";
	case 5: return "the unit scene's members units_db and units_history (issue 0440)";
	case 6: return "the fog particles' member particles.bin (ticket 0403)";
	case 7:
		return "the feed's read order: the residency reads the plot table through copyPlots (caller 2) at each new snapshot "
		       "generation (f916762a5, ticket 4555; Scene::fowFeed), which an older feed.bin does not hold (ticket 0493)";
	case 8:
		return "the trees and the near-camera pairs drawn by default: feed.bin's PLOTS of caller 4 (the forest's plot-table reads), "
		       "the members forest.bin and near.bin, render_config render_forest (ticket 0495)";
	case 9:
		return "the cities and landmarks from the compositor's model tap: the member modeltap/frame<NNNNNN>.mtap, render_config "
		       "render_cities, render_landmarks and render_models_source (ticket 4875)";
	case 10:
		return "shadow.bin's file form 2: the shadow slot's unit-mask-dropped flag and ShadowRaw's size per frame, so that the "
		       "player merges the server's unit shadow mask (ticket 4749; version 9 is still read)";
	case 11:
		return "the member water_frames.bin: the water copy drawn per frame, so that a span holding a Waves_Reload replays the old "
		       "waves before it and the new ones after it (ticket 0622; versions 9 and 10 are still read)";
	default: return "";
	}
}

const char* RefusalName(Refusal r)
{
	static const char* const n[] = {"None", "NoManifest", "BadJson", "MissingKey", "UnknownKey", "UnknownFormat",
	                                "OlderVersion", "NewerVersion", "MissingMember", "UnlistedFile", "ShaMismatch",
	                                "BadMagic", "BadVersion", "Truncated", "WrongSize", "EmptyFeed",
	                                "PlotsDiffBeforeFull", "BadMipCount", "Incomplete", "Io", "BadPath", "BadRecord",
	                                "BadValue"};
	uint32_t i = (uint32_t)r;
	return i < sizeof(n) / sizeof(n[0]) ? n[i] : "?";
}

int RefusalExitCode(Refusal r)
{
	return r == Refusal::None ? 0 : 20 + (int)r;
}

std::string CheckResult::line() const
{
	if(refusal == Refusal::None)
		return "scenebundle: OK";
	return std::string("scenebundle: REFUSED ") + RefusalName(refusal) + " " + (member.empty() ? "scene.json" : member) +
	       ": " + why;
}

// ==== helpers ===========================================================================================================
std::string Sha256Hex(const void* data, size_t bytes)
{
	Sha256 h;
	if(!h.begin() || !h.update(data, bytes))
		return std::string();
	return h.finishHex();
}

c5r::Status Sha256FileHex(const std::wstring& path, std::string* hex, uint64_t* bytes)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if(f == INVALID_HANDLE_VALUE)
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot open (error %lu)", GetLastError()));
	Sha256 h;
	if(!h.begin())
	{
		CloseHandle(f);
		return c5r::Status::fail(c5r::Err::Unsupported, "bcrypt.dll's SHA256 is not available");
	}
	std::vector<uint8_t> buf((size_t)1 << 20);
	uint64_t total = 0;
	while(true)
	{
		DWORD got = 0;
		if(!ReadFile(f, buf.data(), (DWORD)buf.size(), &got, NULL))
		{
			CloseHandle(f);
			return c5r::Status::fail(c5r::Err::Io, "read failed");
		}
		if(!got)
			break;
		h.update(buf.data(), got);
		total += got;
	}
	CloseHandle(f);
	*hex = h.finishHex();
	if(bytes)
		*bytes = total;
	return hex->empty() ? c5r::Status::fail(c5r::Err::Unsupported, "hash failed") : c5r::Status::Ok();
}

std::wstring Widen(const std::string& s)
{
	if(s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
	std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
	if(n > 0)
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

std::string Narrow(const std::wstring& w)
{
	if(w.empty())
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s((size_t)(n > 0 ? n : 0), '\0');
	if(n > 0)
		WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

c5r::Status ComputeAssetSet(const std::wstring& install, const std::string& machine, Manifest* m)
{
	static const char kVtFull[] = "45b3b43359279105fd9d0924e71192d413af38ad22c9d73fd00eacc2c73f0a45";
	static const char kVtPackagesOnly[] = "291608a2762b79b14c64ee9da5bc8d403af950ee6b3b9e9f1b2086d7393a7579";
	m->machine = machine;
	m->vt = "unknown";
	m->vtHash.clear();
	m->vtFiles = 0;
	m->assetWhy.clear();
	std::string inst = Narrow(install);
	std::replace(inst.begin(), inst.end(), '\\', '/');
	while(inst.size() > 3 && inst.back() == '/')   // as str(Path(...)): no trailing separator
		inst.pop_back();
	m->install = inst;
	if(install.empty())
		return c5r::Status::Ok();
	if(!IsDir(install))
	{
		m->assetWhy = "install folder not found";
		return c5r::Status::Ok();
	}
	std::wstring vt = install + L"/Assets/DLC/VT";
	if(!IsDir(vt))
	{
		m->vt = "absent";
		return c5r::Status::Ok();
	}
	std::vector<std::string> files;
	if(!ListFiles(vt, std::string(), &files))
	{
		m->assetWhy = "VT folder unreadable: a folder cannot be listed";
		return c5r::Status::Ok();
	}
	std::vector<std::string> lines;
	for(const std::string& f : files)
	{
		std::string hex;
		c5r::Status st = Sha256FileHex(Join(vt, f), &hex, nullptr);
		if(st.code == c5r::Err::Unsupported)
			return st;
		if(!st.ok())
		{
			m->assetWhy = "VT folder unreadable: " + f + ": " + st.msg;
			return c5r::Status::Ok();
		}
		std::wstring w = Widen(f);
		if(!w.empty())
			CharLowerBuffW(&w[0], (DWORD)w.size());
		lines.push_back(Narrow(w) + " " + hex);
	}
	std::sort(lines.begin(), lines.end());
	std::string joined;
	for(size_t k = 0; k < lines.size(); ++k)
	{
		if(k)
			joined.push_back('\n');
		joined += lines[k];
	}
	std::string h = Sha256Hex(joined.data(), joined.size());
	if(h.empty())
		return c5r::Status::fail(c5r::Err::Unsupported, "bcrypt.dll's SHA256 is not available");
	m->vtHash = h;
	m->vtFiles = (uint32_t)files.size();
	m->vt = h == kVtFull ? "present" : h == kVtPackagesOnly ? "packages only" : "present (other hash)";
	return c5r::Status::Ok();
}

// ==== the manifest ======================================================================================================
std::string ManifestToJson(const Manifest& m)
{
	JOut o;
	o.s = "{";
	auto key = [&](const char* k, bool first) {
		if(!first)
			o.s += ",";
		o.nl(1);
		JEsc(&o.s, k);
		o.s += ": ";
	};
	key("adapter", true);
	o.s += "{\"description\": ";
	JEsc(&o.s, m.adapterDescription);
	o.s += ", \"driver\": ";
	JEsc(&o.s, m.adapterDriver);
	o.s += ", \"luid\": ";
	JEsc(&o.s, m.adapterLuid);
	o.s += "}";
	key("asset_set", false);
	o.s += "{\"install\": ";
	JEsc(&o.s, m.install);
	o.s += ", \"machine\": ";
	JEsc(&o.s, m.machine);
	o.s += ", \"vt\": ";
	JEsc(&o.s, m.vt);
	o.s += Fmt(", \"vt_files\": %u, \"vt_hash\": ", m.vtFiles);
	JEsc(&o.s, m.vtHash);
	o.s += ", \"why\": ";
	JEsc(&o.s, m.assetWhy);
	o.s += "}";
	key("bundle_id", false);
	JEsc(&o.s, m.bundleId);
	key("counters", false);
	{
		std::map<std::string, int64_t> c = m.counters;
		for(const char* n : kCounterNames)
			if(!c.count(n))
				c[n] = 0;
		o.s += "{";
		bool first = true;
		for(const auto& kv : c)
		{
			if(!first)
				o.s += ", ";
			first = false;
			JEsc(&o.s, kv.first);
			o.s += Fmt(": %lld", (long long)kv.second);
		}
		o.s += "}";
	}
	key("created", false);
	JEsc(&o.s, m.created);
	key("format", false);
	JEsc(&o.s, m.format);
	key("format_version", false);
	o.s += Fmt("%u", m.formatVersion);
	key("frames", false);
	o.s += Fmt("{\"count\": %u, \"dumped\": [", m.frameCount);
	for(size_t k = 0; k < m.dumpedFrames.size(); ++k)
		o.s += Fmt(k ? ", %lld" : "%lld", (long long)m.dumpedFrames[k]);
	o.s += Fmt("], \"first_id\": %lld}", (long long)m.firstFrameId);
	key("members", false);
	if(m.members.empty())
		o.s += "{}";
	else
	{
		o.s += "{";
		bool first = true;
		for(const auto& kv : m.members)
		{
			if(!first)
				o.s += ",";
			first = false;
			o.nl(2);
			JEsc(&o.s, kv.first);
			o.s += Fmt(": {\"bytes\": %llu, \"kind\": ", (unsigned long long)kv.second.bytes);
			JEsc(&o.s, MemberKindName(kv.second.kind));
			o.s += ", \"sha256\": ";
			JEsc(&o.s, kv.second.sha256);
			o.s += Fmt(", \"version\": %u}", kv.second.version);
		}
		o.nl(1);
		o.s += "}";
	}
	key("pins", false);
	{
		// dotted names back into one level of objects
		std::map<std::string, std::map<std::string, std::string>> groups;
		std::map<std::string, std::string> flat;
		for(const auto& kv : m.pins)
		{
			size_t d = kv.first.find('.');
			if(d == std::string::npos)
				flat[kv.first] = kv.second;
			else
				groups[kv.first.substr(0, d)][kv.first.substr(d + 1)] = kv.second;
		}
		std::map<std::string, std::string> order;   // key -> "" (group) or "f" (flat), for one sorted pass
		for(const auto& g : groups)
			order[g.first] = "";
		for(const auto& f : flat)
			order[f.first] = "f";
		if(order.empty())
			o.s += "{}";
		else
		{
			o.s += "{";
			bool first = true;
			for(const auto& kv : order)
			{
				if(!first)
					o.s += ",";
				first = false;
				o.nl(2);
				JEsc(&o.s, kv.first);
				o.s += ": ";
				if(kv.second == "f")
					JEsc(&o.s, flat[kv.first]);
				else
					WriteStrMap(o, groups[kv.first], 2);
			}
			o.nl(1);
			o.s += "}";
		}
	}
	key("recorder", false);
	o.s += "{\"build_config\": ";
	JEsc(&o.s, m.buildConfig);
	o.s += ", \"keys\": ";
	WriteStrMap(o, m.recorderKeys, 1);
	o.s += ", \"repo_commit\": ";
	JEsc(&o.s, m.repoCommit);
	o.s += ", \"server_exe_sha256\": ";
	JEsc(&o.s, m.serverExeSha256);
	o.s += "}";
	key("render_config", false);
	WriteStrMap(o, m.renderConfig, 1);
	key("save", false);
	o.s += Fmt("{\"load_seq\": %u, \"map_h\": %u, \"map_w\": %u, \"name\": ", m.loadSeq, m.mapH, m.mapW);
	JEsc(&o.s, m.saveName);
	o.s += ", \"sha256\": ";
	JEsc(&o.s, m.saveSha256);
	o.s += Fmt(", \"turn\": %u}", m.turn);
	key("version_notes", false);
	if(m.versionNotes.empty())
		o.s += "[]";
	else
	{
		o.s += "[";
		for(size_t k = 0; k < m.versionNotes.size(); ++k)
		{
			if(k)
				o.s += ",";
			o.nl(2);
			JEsc(&o.s, m.versionNotes[k]);
		}
		o.nl(1);
		o.s += "]";
	}
	o.s += "\n}\n";
	return o.s;
}

c5r::Status ParseManifest(const std::string& json, Manifest* out, CheckResult* res)
{
	CheckResult local;
	if(!res)
		res = &local;
	*res = CheckResult();
	JVal root;
	std::string why;
	JParser p(json);
	if(!p.parse(&root, &why))
		return Refuse(res, Refusal::BadJson, why);
	if(root.t != JVal::Obj)
		return Refuse(res, Refusal::BadJson, "scene.json is not a JSON object");
	Manifest m;
	// the format and the version first: an older bundle is refused as older, not by a key it lacks
	{
		const JVal* f = nullptr;
		const JVal* v = nullptr;
		for(size_t k = 0; k < root.keys.size(); ++k)
		{
			if(root.keys[k] == "format")
				f = &root.a[k];
			if(root.keys[k] == "format_version")
				v = &root.a[k];
		}
		if(!f)
			return Refuse(res, Refusal::MissingKey, "key \"format\" missing");
		if(f->t != JVal::Str || f->s != kFormat)
			return Refuse(res, Refusal::UnknownFormat, "format is not \"" + std::string(kFormat) + "\"" +
			                                               (f->t == JVal::Str ? " (\"" + f->s + "\")" : std::string()));
		if(!v)
			return Refuse(res, Refusal::MissingKey, "key \"format_version\" missing");
		if(v->t != JVal::Num || !v->isInt)
			return Refuse(res, Refusal::BadValue, "format_version is not an integer");
		if(v->i < (int64_t)kOldestReadFormatVersion)   // ticket 4749: kOldestReadFormatVersion..kFormatVersion are read
		{
			// ticket 0493: name what changed since the bundle's version, so that the cause is read at load, not at replay
			std::string since;
			for(int64_t k = v->i + 1 < 2 ? 2 : v->i + 1; k <= (int64_t)kFormatVersion; ++k)
				since += Fmt("%sversion %lld: %s", since.empty() ? "" : "; ", (long long)k, FormatVersionChange((uint32_t)k));
			return Refuse(res, Refusal::OlderVersion,
			              Fmt("format_version %lld is older than %u (refused, not read; changed since: ", (long long)v->i,
			                  kOldestReadFormatVersion) + since + "; record the bundle again)");
		}
		if(v->i > (int64_t)kFormatVersion)
			return Refuse(res, Refusal::NewerVersion, Fmt("format_version %lld is newer than %u (this reader)",
			                                              (long long)v->i, kFormatVersion));
	}
	JObj top(&root, "", res);
	top.str("format", &m.format);
	top.u32("format_version", &m.formatVersion);
	top.str("bundle_id", &m.bundleId);
	top.str("created", &m.created);
	if(const JVal* r = top.take("recorder", JVal::Obj))
	{
		JObj o(r, "recorder", res);
		if(const JVal* k = o.take("keys", JVal::Obj))
			if(!StrMap(k, "recorder.keys", &m.recorderKeys, res))
				return c5r::Status::fail(ErrOf(res->refusal), res->why);
		o.str("server_exe_sha256", &m.serverExeSha256);
		o.str("repo_commit", &m.repoCommit);
		o.str("build_config", &m.buildConfig);
		if(!o.finish())
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
	}
	if(const JVal* pv = top.take("pins", JVal::Obj))
	{
		for(size_t k = 0; k < pv->keys.size(); ++k)
		{
			const JVal& x = pv->a[k];
			if(pv->keys[k].find('.') != std::string::npos)
				return Refuse(res, Refusal::BadValue, "pins key \"" + pv->keys[k] + "\" has a '.'");
			if(x.t == JVal::Str)
				m.pins[pv->keys[k]] = x.s;
			else if(x.t == JVal::Obj)
			{
				std::map<std::string, std::string> sub;
				if(!StrMap(&x, "pins." + pv->keys[k], &sub, res))
					return c5r::Status::fail(ErrOf(res->refusal), res->why);
				for(const auto& kv : sub)
					m.pins[pv->keys[k] + "." + kv.first] = kv.second;
			}
			else
				return Refuse(res, Refusal::BadValue, "\"pins." + pv->keys[k] + "\" is neither a string nor an object");
		}
	}
	if(const JVal* s = top.take("save", JVal::Obj))
	{
		JObj o(s, "save", res);
		o.str("name", &m.saveName);
		o.str("sha256", &m.saveSha256);
		o.u32("map_w", &m.mapW);
		o.u32("map_h", &m.mapH);
		o.u32("turn", &m.turn);
		o.u32("load_seq", &m.loadSeq);
		if(!o.finish())
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
	}
	if(const JVal* a = top.take("asset_set", JVal::Obj))
	{
		JObj o(a, "asset_set", res);
		o.str("vt", &m.vt);
		o.str("vt_hash", &m.vtHash);
		o.u32("vt_files", &m.vtFiles);
		o.str("install", &m.install);
		o.str("machine", &m.machine);
		o.str("why", &m.assetWhy);
		if(!o.finish())
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
		static const char* const kVt[] = {"present", "absent", "packages only", "present (other hash)", "unknown"};
		if(std::find_if(std::begin(kVt), std::end(kVt), [&](const char* x) { return m.vt == x; }) == std::end(kVt))
			return Refuse(res, Refusal::BadValue, "asset_set.vt \"" + m.vt + "\" is not one of asset_set()'s values");
	}
	if(const JVal* a = top.take("adapter", JVal::Obj))
	{
		JObj o(a, "adapter", res);
		o.str("luid", &m.adapterLuid);
		o.str("description", &m.adapterDescription);
		o.str("driver", &m.adapterDriver);
		if(!o.finish())
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
	}
	if(const JVal* rc = top.take("render_config", JVal::Obj))
		if(!StrMap(rc, "render_config", &m.renderConfig, res))
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
	if(const JVal* f = top.take("frames", JVal::Obj))
	{
		JObj o(f, "frames", res);
		o.i64("first_id", &m.firstFrameId);
		o.u32("count", &m.frameCount);
		if(const JVal* d = o.take("dumped", JVal::Arr))
			for(const JVal& x : d->a)
			{
				if(x.t != JVal::Num || !x.isInt)
					return Refuse(res, Refusal::BadValue, "frames.dumped holds a value that is not an integer");
				m.dumpedFrames.push_back(x.i);
			}
		if(!o.finish())
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
	}
	if(const JVal* mv = top.take("members", JVal::Obj))
	{
		for(size_t k = 0; k < mv->keys.size(); ++k)
		{
			const std::string& path = mv->keys[k];
			std::string pw;
			if(!CleanPath(path, &pw))
				return Refuse(res, Refusal::BadPath, pw, path);
			if(mv->a[k].t != JVal::Obj)
				return Refuse(res, Refusal::BadValue, "the member entry is not an object", path);
			JObj o(&mv->a[k], "members[" + path + "]", res);
			Member mem;
			std::string kind;
			int64_t bytes = 0;
			o.str("kind", &kind);
			o.u32("version", &mem.version);
			o.i64("bytes", &bytes);
			o.str("sha256", &mem.sha256);
			if(!o.finish())
			{
				res->member = path;
				return c5r::Status::fail(ErrOf(res->refusal), res->why);
			}
			if(!MemberKindFromName(kind, &mem.kind))
				return Refuse(res, Refusal::BadValue, "unknown member kind \"" + kind + "\"", path);
			if(!PathFitsKind(path, mem.kind, &pw))
				return Refuse(res, Refusal::BadPath, pw, path);
			if(bytes < 0)
				return Refuse(res, Refusal::BadValue, "negative bytes", path);
			mem.bytes = (uint64_t)bytes;
			if(!IsHex64(mem.sha256))
				return Refuse(res, Refusal::BadValue, "sha256 is not 64 lowercase hex digits", path);
			if(mem.version != MemberKindVersionFor(mem.kind, m.formatVersion))   // ticket 4749: shadow form 1 in format 9
				return Refuse(res, Refusal::BadVersion, Fmt("member version %u, this reader reads %u", mem.version,
				                                            MemberKindVersionFor(mem.kind, m.formatVersion)), path);
			m.members[path] = mem;
		}
	}
	if(const JVal* vn = top.take("version_notes", JVal::Arr))
		for(const JVal& x : vn->a)
		{
			if(x.t != JVal::Str)
				return Refuse(res, Refusal::BadValue, "version_notes holds a value that is not a string");
			m.versionNotes.push_back(x.s);
		}
	if(const JVal* cv = top.take("counters", JVal::Obj))
	{
		JObj o(cv, "counters", res);
		for(const char* n : kCounterNames)
		{
			int64_t v = 0;
			if(o.i64(n, &v))
				m.counters[n] = v;
		}
		if(!o.finish())
			return c5r::Status::fail(ErrOf(res->refusal), res->why);
	}
	if(!top.finish())
		return c5r::Status::fail(ErrOf(res->refusal), res->why);
	*out = std::move(m);
	return c5r::Status::Ok();
}

c5r::Status ReadManifest(const std::wstring& bundleDir, Manifest* out, CheckResult* res)
{
	CheckResult local;
	if(!res)
		res = &local;
	*res = CheckResult();
	if(!IsDir(bundleDir))
		return Refuse(res, Refusal::NoManifest, "the bundle folder does not exist");
	std::vector<uint8_t> b;
	c5r::Status st = ReadWhole(Join(bundleDir, "scene.json"), &b);
	if(st.code == c5r::Err::NotFound)
		return Refuse(res, Refusal::NoManifest, "scene.json missing");
	if(!st.ok())
		return Refuse(res, Refusal::Io, "scene.json: " + st.msg);
	return ParseManifest(std::string(b.begin(), b.end()), out, res);
}

// ==== member forms ======================================================================================================
c5r::Status ParseHeightfield(const std::vector<uint8_t>& b, c5r::heightfield::HeightGrid* out, CheckResult* res)
{
	c5r::heightfield::HeightGrid g;
	c5r::Status st = c5r::heightfield::readTap(b.data(), b.size(), g);
	if(!st.ok())
	{
		Refusal r = st.code == c5r::Err::Truncated ? Refusal::Truncated : st.code == c5r::Err::BadMagic ? Refusal::BadMagic :
		            st.code == c5r::Err::BadVersion ? Refusal::BadVersion : Refusal::WrongSize;
		return Refuse(res, r, "readTap: " + st.str());
	}
	size_t want = c5r::heightfield::tapSize(g.mapW, g.mapH, !g.mat.empty());
	if(b.size() != want)
		return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes, the tap announces %llu", (unsigned long long)b.size(),
		                                           (unsigned long long)want));
	if(out)
		*out = std::move(g);
	return c5r::Status::Ok();
}

c5r::Status ParseWaterTap(const std::vector<uint8_t>& b, WaterTapFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicWaterTap, 1, res));
	Cur c(b, 16);
	uint32_t blockBytes;
	if(!c.get(&blockBytes))
		return Refuse(res, Refusal::Truncated, "no block size after the header");
	if(blockBytes != sizeof(VPShellWaterTap))
		return Refuse(res, Refusal::WrongSize, Fmt("water tap block size %u, this reader reads %u (VPShellWaterTap)",
		                                           blockBytes, (unsigned)sizeof(VPShellWaterTap)));
	WaterTapFile f;
	if(!c.get(&f.block) || !c.get(&f.haveCam) || !c.get(&f.gridSeq) || !c.get(&f.samplesX) || !c.get(&f.samplesY))
		return Refuse(res, Refusal::Truncated, "shorter than the block and the grid values");
	f.imageOffset = c.at;
	uint64_t want = f.block.imageBytes;
	if(c.left() < want)
		return Refuse(res, Refusal::Truncated, Fmt("image of %llu bytes announced, %llu present",
		                                           (unsigned long long)want, (unsigned long long)c.left()));
	if(c.left() > want)
		return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes after the image of %llu",
		                                           (unsigned long long)(c.left() - want), (unsigned long long)want));
	if(want < sizeof(VPShellWaterImage))
		return Refuse(res, Refusal::WrongSize, Fmt("image of %llu bytes, shorter than its header",
		                                           (unsigned long long)want));
	VPShellWaterImage img;
	memcpy(&img, b.data() + f.imageOffset, sizeof(img));
	if(img.magic != VPSHELL_WATER_MAGIC)
		return Refuse(res, Refusal::BadMagic, "the image's magic is not WATR");
	if(img.version != VPSHELL_WATER_IMAGE_VERSION)
		return Refuse(res, Refusal::BadVersion, Fmt("image version %u", img.version));
	if(img.headerBytes != sizeof(VPShellWaterImage) || img.recordBytes != sizeof(VPShellWaterRecord))
		return Refuse(res, Refusal::WrongSize, Fmt("image header bytes %u, record bytes %u", img.headerBytes, img.recordBytes));
	if(img.imageBytes != want)
		return Refuse(res, Refusal::WrongSize, Fmt("the image says %llu bytes, the block %llu",
		                                           (unsigned long long)img.imageBytes, (unsigned long long)want));
	if(img.recordCount != f.block.recordCount || img.loadSeq != f.block.loadSeq)
		return Refuse(res, Refusal::BadRecord, "the image's record count or load differs from the block's");
	f.imageBytes = (size_t)want;
	if(out)
		*out = f;
	return c5r::Status::Ok();
}

void EncodeWaterTap(const VPShellWaterTap& block, uint32_t haveCam, uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY,
                    const void* image, size_t imageBytes, std::vector<uint8_t>* out)
{
	out->clear();
	out->reserve(kWaterTapFixedBytes + imageBytes);
	PutHeader(out, kMagicWaterTap, 1);
	Put(out, (uint32_t)sizeof(VPShellWaterTap));
	Put(out, block);
	Put(out, haveCam);
	Put(out, gridSeq);
	Put(out, samplesX);
	Put(out, samplesY);
	PutBytes(out, image, imageBytes);
}

c5r::Status ParseOverlay(const std::vector<uint8_t>& b, OverlayFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicOverlay, 1, res));
	Cur c(b, 16);
	OverlayFile f;
	if(!c.get(&f.width) || !c.get(&f.height) || !c.get(&f.mips) || !c.get(&f.dxgiFormat))
		return Refuse(res, Refusal::Truncated, "shorter than the texture's description");
	if(f.mips != kOverlayMips)
		return Refuse(res, Refusal::BadMipCount, Fmt("%u mips, the overlay has %u", f.mips, kOverlayMips));
	if(f.width != kOverlaySize || f.height != kOverlaySize)
		return Refuse(res, Refusal::WrongSize, Fmt("%u x %u, the overlay is %u x %u", f.width, f.height, kOverlaySize,
		                                           kOverlaySize));
	if(f.dxgiFormat != kOverlayFormat)
		return Refuse(res, Refusal::WrongSize, Fmt("DXGI format %u, the overlay is %u (R8G8B8A8_UNORM)", f.dxgiFormat,
		                                           kOverlayFormat));
	for(uint32_t i = 0; i < kOverlayMips; ++i)
		if(!c.get(&f.rowPitch[i]))
			return Refuse(res, Refusal::Truncated, "shorter than the row pitch table");
	for(uint32_t i = 0; i < kOverlayMips; ++i)
		if(!c.get(&f.offset[i]))
			return Refuse(res, Refusal::Truncated, "shorter than the offset table");
	uint64_t at = kOverlayTableBytes;
	for(uint32_t i = 0; i < kOverlayMips; ++i)
	{
		uint32_t pitch = f.mipWidth(i) * 4;
		if(f.rowPitch[i] != pitch || f.offset[i] != at)
			return Refuse(res, Refusal::WrongSize, Fmt("mip %u: row pitch %u at %u, the tight layout is %u at %llu", i,
			                                           f.rowPitch[i], f.offset[i], pitch, (unsigned long long)at));
		at += (uint64_t)pitch * f.mipHeight(i);
	}
	if(at != kOverlayTableBytes + kOverlayTexelBytes)
		return Refuse(res, Refusal::WrongSize, "texel total differs from 22,369,620");
	if(b.size() < at)
		return Refuse(res, Refusal::Truncated, Fmt("%llu bytes, the 12 mips end at %llu", (unsigned long long)b.size(),
		                                           (unsigned long long)at));
	if(b.size() > at)
		return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes after the last mip", (unsigned long long)(b.size() - at)));
	if(out)
		*out = f;
	return c5r::Status::Ok();
}

c5r::Status EncodeOverlay(const void* texels, size_t bytes, std::vector<uint8_t>* out)
{
	if(bytes != kOverlayTexelBytes)
		return c5r::Status::fail(c5r::Err::BadLayout, Fmt("%llu texel bytes, the 12 mips are %llu",
		                                                  (unsigned long long)bytes, (unsigned long long)kOverlayTexelBytes));
	out->clear();
	out->reserve(kOverlayTableBytes + bytes);
	PutHeader(out, kMagicOverlay, 1);
	Put(out, kOverlaySize);
	Put(out, kOverlaySize);
	Put(out, kOverlayMips);
	Put(out, kOverlayFormat);
	uint32_t pitch[kOverlayMips], off[kOverlayMips], at = kOverlayTableBytes;
	for(uint32_t i = 0; i < kOverlayMips; ++i)
	{
		uint32_t w = (kOverlaySize >> i) ? (kOverlaySize >> i) : 1;
		pitch[i] = w * 4;
		off[i] = at;
		at += pitch[i] * w;
	}
	for(uint32_t i = 0; i < kOverlayMips; ++i)
		Put(out, pitch[i]);
	for(uint32_t i = 0; i < kOverlayMips; ++i)
		Put(out, off[i]);
	PutBytes(out, texels, bytes);
	return c5r::Status::Ok();
}

c5r::Status ParseFeed(const std::vector<uint8_t>& b, std::vector<FeedRecord>* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicFeed, kFeedFormVersion, res));
	std::vector<FeedRecord> recs;
	Cur c(b, 16);
	size_t prevIdx = (size_t)-1;   // the newest PLOTS record
	uint64_t lastIter = 0;
	size_t n = 0;
	// issue 4457: records 3-6 (srvunits::rec); 4, 5 and 6-of-caller-2 fold into the DRAIN right before them
	srvunits::rec::Decoder unitDec(kFormatVersion);
	size_t foldDrain = (size_t)-1;   // the DRAIN that a record 4 / 5 / 6-of-caller-2 may still join
	size_t lastDrain = (size_t)-1;   // the newest DRAIN (records 3 / 6-of-caller-1 join it when its loopIter is theirs)
	bool folded[7] = {};
	while(c.left())
	{
		uint32_t type, bytes;
		size_t recAt = c.at;
		if(!c.get(&type) || !c.get(&bytes))
			return Refuse(res, Refusal::Truncated, Fmt("record %u: partial record head at byte %u", (unsigned)n, (unsigned)recAt));
		if(c.left() < bytes)
			return Refuse(res, Refusal::Truncated, Fmt("record %u: %u payload bytes announced, %u present", (unsigned)n,
			                                           bytes, (unsigned)c.left()));
		std::vector<uint8_t> pay(b.begin() + (ptrdiff_t)c.at, b.begin() + (ptrdiff_t)(c.at + bytes));
		c.at += bytes;
		Cur p(pay);
		FeedRecord r;
		r.type = type;
		if(type == kFeedDrain)
		{
			uint32_t nEv = 0, nCh = 0;
			if(!p.get(&r.loopIter) || !p.get(&r.nextFrameId) || !p.get(&r.batch.snapshotGen) || !p.get(&r.batch.width) ||
			   !p.get(&r.batch.height) || !p.get(&r.batch.dropped) || !p.get(&r.batch.unitTableGen) || !p.get(&r.drainMs) ||
			   !p.get(&nEv))
				return Refuse(res, Refusal::WrongSize, Fmt("record %u (DRAIN): payload of %u bytes too short", (unsigned)n, bytes));
			if(!Finite(r.drainMs))
				return Refuse(res, Refusal::BadRecord, Fmt("record %u (DRAIN): drainMs is not finite", (unsigned)n));
			if(p.left() < (uint64_t)nEv * sizeof(SrvFeed::Event) + 4)
				return Refuse(res, Refusal::WrongSize, Fmt("record %u (DRAIN): %u events announced, payload too short",
				                                           (unsigned)n, nEv));
			r.batch.events.resize(nEv);
			if(nEv)
				memcpy(r.batch.events.data(), pay.data() + p.at, (size_t)nEv * sizeof(SrvFeed::Event));
			p.at += (size_t)nEv * sizeof(SrvFeed::Event);
			p.get(&nCh);
			if(p.left() != (uint64_t)nCh * 4)
				return Refuse(res, Refusal::WrongSize, Fmt("record %u (DRAIN): %u changed plots announced, %u bytes left",
				                                           (unsigned)n, nCh, (unsigned)p.left()));
			r.batch.changedPlots.resize(nCh);
			if(nCh)
				memcpy(r.batch.changedPlots.data(), pay.data() + p.at, (size_t)nCh * 4);
		}
		else if(type == kFeedPlots)
		{
			uint32_t full = 0;
			if(!p.get(&r.loopIter) || !p.get(&r.caller) || !p.get(&r.snapshotGen) || !p.get(&r.width) || !p.get(&r.height) ||
			   !p.get(&full))
				return Refuse(res, Refusal::WrongSize, Fmt("record %u (PLOTS): payload of %u bytes too short", (unsigned)n, bytes));
			// caller 3 (the fog's plot-table reads, FowPlotTable::copy): format version 2 (issue 3911); caller 4 (the forest's):
			// format version 8 (ticket 0495)
			if(r.caller < 1 || r.caller > 4)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u (PLOTS): caller %u (1, 2, 3 or 4)", (unsigned)n, r.caller));
			if(r.width > c5r::heightfield::kMaxPlots || r.height > c5r::heightfield::kMaxPlots || full > 1)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u (PLOTS): %u x %u, full %u", (unsigned)n, r.width,
				                                           r.height, full));
			size_t cells = (size_t)r.width * r.height;
			r.full = full == 1;
			if(r.full)
			{
				if(p.left() != cells * sizeof(SrvFeed::PlotRecord))
					return Refuse(res, Refusal::WrongSize, Fmt("record %u (PLOTS full): %u x %u needs %u bytes, %u present",
					                                           (unsigned)n, r.width, r.height,
					                                           (unsigned)(cells * sizeof(SrvFeed::PlotRecord)), (unsigned)p.left()));
				r.plots.resize(cells);
				if(cells)
					memcpy(r.plots.data(), pay.data() + p.at, cells * sizeof(SrvFeed::PlotRecord));
			}
			else
			{
				if(prevIdx == (size_t)-1)
					return Refuse(res, Refusal::PlotsDiffBeforeFull, Fmt("record %u: a PLOTS diff before any full table", (unsigned)n));
				const std::vector<SrvFeed::PlotRecord>* prev = &recs[prevIdx].plots;
				if(prev->size() != cells)
					return Refuse(res, Refusal::BadRecord, Fmt("record %u (PLOTS diff): %u x %u against a table of %u plots",
					                                           (unsigned)n, r.width, r.height, (unsigned)prev->size()));
				uint32_t nDiff = 0;
				if(!p.get(&nDiff) || p.left() != (uint64_t)nDiff * (4 + sizeof(SrvFeed::PlotRecord)))
					return Refuse(res, Refusal::WrongSize, Fmt("record %u (PLOTS diff): %u entries announced, %u bytes",
					                                           (unsigned)n, nDiff, (unsigned)p.left()));
				r.plots = *prev;
				int64_t lastIdx = -1;
				for(uint32_t k = 0; k < nDiff; ++k)
				{
					uint32_t idx;
					p.get(&idx);
					if(idx >= cells || (int64_t)idx <= lastIdx)
						return Refuse(res, Refusal::BadRecord, Fmt("record %u (PLOTS diff): index %u (table %u, previous %lld)",
						                                           (unsigned)n, idx, (unsigned)cells, (long long)lastIdx));
					lastIdx = idx;
					p.get(&r.plots[idx]);
				}
			}
		}
		else if(type >= kFeedUnits && type <= kFeedPlayers)
		{
			srvunits::rec::Decoded d;
			const srvunits::rec::Result rr = unitDec.decode(type, pay.data(), pay.size(), &d);
			if(!rr.ok())
				return Refuse(res,
				              rr.err == srvunits::rec::Err::kTruncated || rr.err == srvunits::rec::Err::kWrongSize ? Refusal::WrongSize
				                                                                                                    : Refusal::BadRecord,
				              Fmt("record %u (%s): %s", (unsigned)n, srvunits::rec::TypeName(type), rr.line().c_str()));
			if(n && d.loopIter < lastIter)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u: loop iteration %llu after %llu", (unsigned)n,
				                                           (unsigned long long)d.loopIter, (unsigned long long)lastIter));
			const bool fold = type == kFeedPaths || type == kFeedCombats || (type == kFeedPlayers && d.caller == srvunits::rec::kPlayersBatch);
			if(fold)
			{
				if(foldDrain == (size_t)-1 || recs[foldDrain].loopIter != d.loopIter)
					return Refuse(res, Refusal::BadRecord, Fmt("record %u (%s): not right after its DRAIN (loop iteration %llu)", (unsigned)n,
					                                           srvunits::rec::TypeName(type), (unsigned long long)d.loopIter));
				if(folded[type])
					return Refuse(res, Refusal::BadRecord, Fmt("record %u (%s): a second one for the DRAIN of loop iteration %llu",
					                                           (unsigned)n, srvunits::rec::TypeName(type), (unsigned long long)d.loopIter));
				folded[type] = true;
				SrvFeed::Batch& bt = recs[foldDrain].batch;
				if(type == kFeedPaths)
				{
					bt.paths = std::move(d.paths);
					bt.pathPlots = std::move(d.pathPlots);
				}
				else if(type == kFeedCombats)
				{
					bt.combats = std::move(d.combats);
					bt.combatMembers = std::move(d.combatMembers);
				}
				else
					bt.players = std::move(d.players);
				lastIter = d.loopIter;
				++n;
				continue;
			}
			// 3 UNITS, 6 PLAYERS of caller 1: the unit scene's copies, taken while it took the newest DRAIN of this loopIter
			if(lastDrain == (size_t)-1 || recs[lastDrain].loopIter != d.loopIter)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u (%s of caller %u): no DRAIN of loop iteration %llu before it", (unsigned)n,
				                                           srvunits::rec::TypeName(type), d.caller, (unsigned long long)d.loopIter));
			if(type == kFeedUnits)
			{
				UnitCopy uc;
				uc.caller = d.caller;
				uc.tableGen = d.tableGen;
				uc.full = d.full;
				uc.units = std::move(d.units);
				recs[lastDrain].unitCopies.push_back(std::move(uc));
			}
			else
				recs[lastDrain].playerCopies.push_back(std::move(d.players));
			lastIter = d.loopIter;
			foldDrain = (size_t)-1;   // records 4, 5, 6-of-caller-2 come right after their DRAIN, before any copy
			++n;
			continue;
		}
		else
			return Refuse(res, Refusal::BadRecord, Fmt("record %u: unknown type %u", (unsigned)n, type));
		if(type == kFeedDrain || type == kFeedPlots)
			unitDec.noteLoopIter(r.loopIter, type == kFeedDrain ? "DRAIN" : "PLOTS");
		if(n && r.loopIter < lastIter)
			return Refuse(res, Refusal::BadRecord, Fmt("record %u: loop iteration %llu after %llu", (unsigned)n,
			                                           (unsigned long long)r.loopIter, (unsigned long long)lastIter));
		lastIter = r.loopIter;
		if(r.type == kFeedPlots)
			prevIdx = recs.size();
		foldDrain = r.type == kFeedDrain ? recs.size() : (size_t)-1;
		if(r.type == kFeedDrain)
			lastDrain = recs.size();
		memset(folded, 0, sizeof(folded));
		recs.push_back(std::move(r));
		++n;
	}
	if(recs.empty())
		return Refuse(res, Refusal::EmptyFeed, "feed.bin holds no record");
	if(out)
		*out = std::move(recs);
	return c5r::Status::Ok();
}

c5r::Status FeedEncoder::addDrain(uint64_t loopIter, int64_t nextFrameId, const SrvFeed::Batch& b, std::vector<uint8_t>* out,
                                  double drainMs)
{
	out->clear();
	if(!Finite(drainMs))
		drainMs = -1;
	uint32_t payload = (uint32_t)(52 + b.events.size() * sizeof(SrvFeed::Event) + 4 + b.changedPlots.size() * 4);
	Put(out, (uint32_t)kFeedDrain);
	Put(out, payload);
	Put(out, loopIter);
	Put(out, nextFrameId);
	Put(out, b.snapshotGen);
	Put(out, b.width);
	Put(out, b.height);
	Put(out, b.dropped);
	Put(out, b.unitTableGen);   // issue 4457 (format version 4)
	Put(out, drainMs);
	Put(out, (uint32_t)b.events.size());
	PutBytes(out, b.events.data(), b.events.size() * sizeof(SrvFeed::Event));
	Put(out, (uint32_t)b.changedPlots.size());
	PutBytes(out, b.changedPlots.data(), b.changedPlots.size() * 4);
	// issue 4457: the batch's unit side tables, right after their DRAIN
	srvunits::rec::Result r;
	if(r.ok() && !b.paths.empty())
		r = m_units.addPaths(loopIter, b, out);
	if(r.ok() && !b.combats.empty())
		r = m_units.addCombats(loopIter, b, out);
	if(r.ok() && !b.players.empty())
		r = m_units.addPlayers(loopIter, srvunits::rec::kPlayersBatch, b.players, out);
	if(!r.ok())
	{
		out->clear();
		return c5r::Status::fail(c5r::Err::BadLayout, "feed DRAIN of loop iteration " + std::to_string(loopIter) + ": " + r.line());
	}
	return c5r::Status::Ok();
}

c5r::Status FeedEncoder::addUnits(uint64_t loopIter, uint32_t caller, uint32_t tableGen, const std::vector<SrvFeed::UnitRecord>& table,
                                  std::vector<uint8_t>* out)
{
	out->clear();
	const srvunits::rec::Result r = m_units.addUnits(loopIter, caller, tableGen, table, out);
	if(!r.ok())
		return c5r::Status::fail(c5r::Err::BadLayout, "feed UNITS of loop iteration " + std::to_string(loopIter) + ": " + r.line());
	return c5r::Status::Ok();
}

c5r::Status FeedEncoder::addPlayers(uint64_t loopIter, const std::vector<SrvFeed::PlayerRecord>& rows, std::vector<uint8_t>* out)
{
	out->clear();
	const srvunits::rec::Result r = m_units.addPlayers(loopIter, srvunits::rec::kPlayersCopy, rows, out);
	if(!r.ok())
		return c5r::Status::fail(c5r::Err::BadLayout, "feed PLAYERS of loop iteration " + std::to_string(loopIter) + ": " + r.line());
	return c5r::Status::Ok();
}

void FeedEncoder::addPlots(uint64_t loopIter, uint32_t caller, uint32_t snapshotGen, uint32_t width, uint32_t height,
                           const std::vector<SrvFeed::PlotRecord>& table, std::vector<uint8_t>* out, bool forceFull)
{
	out->clear();
	const size_t rb = sizeof(SrvFeed::PlotRecord);
	bool full = forceFull || !m_have || snapshotGen != m_gen || width != m_w || height != m_h || table.size() != m_prev.size();
	std::vector<uint32_t> diff;
	if(!full)
	{
		for(size_t k = 0; k < table.size(); ++k)
			if(memcmp(&table[k], &m_prev[k], rb) != 0)
				diff.push_back((uint32_t)k);
		if(4 + diff.size() * (4 + rb) >= table.size() * rb)
			full = true;
	}
	uint32_t payload = (uint32_t)(28 + (full ? table.size() * rb : 4 + diff.size() * (4 + rb)));
	Put(out, (uint32_t)kFeedPlots);
	Put(out, payload);
	Put(out, loopIter);
	Put(out, caller);
	Put(out, snapshotGen);
	Put(out, width);
	Put(out, height);
	Put(out, (uint32_t)(full ? 1 : 0));
	if(full)
		PutBytes(out, table.data(), table.size() * rb);
	else
	{
		Put(out, (uint32_t)diff.size());
		for(uint32_t k : diff)
		{
			Put(out, k);
			Put(out, table[k]);
		}
	}
	m_prev = table;
	m_gen = snapshotGen;
	m_w = width;
	m_h = height;
	m_have = true;
}

c5r::Status ParseCameras(const std::vector<uint8_t>& b, std::vector<VPShellCamera>* out, CheckResult* res)
{
	size_t count = 0;
	C5R_TRY(ParseRecordStream(b, kMagicCameras, kCameraBlockBytes, "camera block", &count, res));
	if(out)
	{
		out->resize(count);
		if(count)
			memcpy((void*)out->data(), b.data() + 20, count * kCameraBlockBytes);
	}
	return c5r::Status::Ok();
}

c5r::Status ParseFrames(const std::vector<uint8_t>& b, std::vector<FrameRecord>* out, CheckResult* res)
{
	size_t count = 0;
	C5R_TRY(ParseRecordStream(b, kMagicFrames, (uint32_t)sizeof(FrameRecord), "frame record", &count, res));
	std::vector<FrameRecord> v(count);
	if(count)
		memcpy(v.data(), b.data() + 20, count * sizeof(FrameRecord));
	for(size_t k = 0; k < count; ++k)
	{
		const FrameRecord& r = v[k];
		if(k && r.frameId <= v[k - 1].frameId)
			return Refuse(res, Refusal::BadRecord, Fmt("record %u: frame %lld after %lld", (unsigned)k, (long long)r.frameId,
			                                           (long long)v[k - 1].frameId));
		if(!Finite(r.nowMs) || r.overlayNumViews > 4 || (r.flags & ~15u) || r.overlayBound > 1)   // bit 3: issue 3911
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): nowMs, numViews %u, bound %u or flags 0x%x out "
			                                           "of range", (unsigned)k, (long long)r.frameId, r.overlayNumViews,
			                                           r.overlayBound, r.flags));
	}
	if(out)
		*out = std::move(v);
	return c5r::Status::Ok();
}

void EncodeStreamHead(MemberKind kind, std::vector<uint8_t>* out)
{
	out->clear();
	const char* mg = MagicOf(kind);
	if(!mg)
		return;
	PutHeader(out, mg, MemberKindVersion(kind));
	if(kind == MemberKind::Frames)
		Put(out, (uint32_t)sizeof(FrameRecord));
	else if(kind == MemberKind::Shadow)   // issue 4308
		Put(out, (uint32_t)sizeof(ShadowRecord));
	else if(kind == MemberKind::WaterFrames)   // ticket 0622
		Put(out, (uint32_t)sizeof(WaterFrameRecord));
	else if(kind == MemberKind::CameraBlocks || kind == MemberKind::NamedCameraBlock)
		Put(out, kCameraBlockBytes);
}

c5r::Status ParseWaterClock(const std::vector<uint8_t>& b, WaterClockFile* out, CheckResult* res)
{
	static_assert(sizeof(c5r::water::WaveRecord) == 16, "WaveRecord layout");
	C5R_TRY(CheckHeader(b, kMagicWaterClock, 1, res));
	Cur c(b, 16);
	WaterClockFile f;
	uint32_t nWaves = 0, recBytes = 0;
	if(!c.get(&f.clockStartMs) || !c.get(&f.waveLastMs) || !c.get(&f.lastNowMs) || !c.get(&nWaves) || !c.get(&recBytes))
		return Refuse(res, Refusal::Truncated, "shorter than the clock values");
	if(recBytes != sizeof(c5r::water::WaveRecord))
		return Refuse(res, Refusal::WrongSize, Fmt("wave record size %u, this reader reads 16", recBytes));
	if(c.left() < (uint64_t)nWaves * 16)
		return Refuse(res, Refusal::Truncated, Fmt("%u waves announced, %u bytes present", nWaves, (unsigned)c.left()));
	if(c.left() > (uint64_t)nWaves * 16)
		return Refuse(res, Refusal::WrongSize, Fmt("%u bytes after the %u waves", (unsigned)(c.left() - (size_t)nWaves * 16),
		                                           nWaves));
	if(!Finite(f.clockStartMs) || !Finite(f.waveLastMs) || !Finite(f.lastNowMs))
		return Refuse(res, Refusal::BadRecord, "a clock value is not finite");
	f.waves.resize(nWaves);
	for(uint32_t k = 0; k < nWaves; ++k)
	{
		c.get(&f.waves[k]);
		const c5r::water::WaveRecord& w = f.waves[k];
		if(!Finite(w.u) || !Finite(w.v) || !Finite(w.age) || !Finite(w.unused))
			return Refuse(res, Refusal::BadRecord, Fmt("wave %u is not finite", k));
	}
	if(out)
		*out = std::move(f);
	return c5r::Status::Ok();
}

void EncodeWaterClock(const WaterClockFile& f, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicWaterClock, 1);
	Put(out, f.clockStartMs);
	Put(out, f.waveLastMs);
	Put(out, f.lastNowMs);
	Put(out, (uint32_t)f.waves.size());
	Put(out, (uint32_t)sizeof(c5r::water::WaveRecord));
	PutBytes(out, f.waves.data(), f.waves.size() * sizeof(c5r::water::WaveRecord));
}

// ---- epic 0301, issue 3911: fog_clock.bin (format version 2) ---------------------------------------------------------------
c5r::Status ParseFogClock(const std::vector<uint8_t>& b, FogClockFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicFogClock, 1, res));
	Cur c(b, 16);
	FogClockFile f;
	if(!c.get(&f.clockS) || !c.get(&f.clockLastMs))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the fog clock needs %u", (unsigned)b.size(), kFogClockBytes));
	if(c.left())
		return Refuse(res, Refusal::WrongSize, Fmt("%u bytes after the fog clock's two values", (unsigned)c.left()));
	if(!Finite(f.clockS) || !Finite(f.clockLastMs) || f.clockS < 0)
		return Refuse(res, Refusal::BadRecord, Fmt("clockS %g, clockLastMs %g: not finite, or T below 0", f.clockS, f.clockLastMs));
	if(out)
		*out = f;
	return c5r::Status::Ok();
}

void EncodeFogClock(const FogClockFile& f, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicFogClock, 1);
	Put(out, f.clockS);
	Put(out, f.clockLastMs);
}

// ---- format version 6, ticket 0403: particles.bin (srv_scene_bundle.h, the block at FogParticlesFile) ----------------------
static_assert(sizeof(c5r::fow::Particle) == 44, "particle state: 44 bytes a particle (ParticleSim::saveState)");

c5r::Status ParseFogParticles(const std::vector<uint8_t>& b, FogParticlesFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicFogParticles, 1, res));
	Cur c(b, 16);
	FogParticlesFile f;
	uint32_t stateBytes = 0, reserved = 0;
	if(!c.get(&f.flags) || !c.get(&f.seed) || !c.get(&f.worldW) || !c.get(&f.worldH) || !c.get(&stateBytes) || !c.get(&reserved))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the particles' fixed fields need %u", (unsigned)b.size(), kFogParticlesFixedBytes));
	if(reserved != 0 || (f.flags & ~(uint32_t)kFogParticlesSpawned))
		return Refuse(res, Refusal::BadRecord, Fmt("flags 0x%X, reserved %u: only bit 0 and 0", f.flags, reserved));
	const bool spawned = (f.flags & kFogParticlesSpawned) != 0;
	if(!Finite(f.worldW) || !Finite(f.worldH) || (spawned && (f.worldW <= 0 || f.worldH <= 0)))
		return Refuse(res, Refusal::BadRecord, Fmt("world %g x %g: not finite, or not > 0 with the particles spawned", f.worldW, f.worldH));
	if(spawned != (stateBytes > 0))
		return Refuse(res, Refusal::BadRecord, Fmt("spawned %d with %u state bytes: a spawned state needs bytes, an unspawned one none",
		                                           spawned ? 1 : 0, stateBytes));
	if(c.left() != stateBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("stateBytes %u, the file holds %u after the fixed fields", stateBytes, (unsigned)c.left()));
	if(spawned)
	{
		// the state's own form (ParticleSim::saveState, "C5FP" version 1), walked without the install's flow field
		Cur s(b, kFogParticlesFixedBytes);
		char magic[4];
		uint32_t version = 0;
		if(!s.get(&magic) || !s.get(&version))
			return Refuse(res, Refusal::Truncated, "particle state: header");
		if(memcmp(magic, "C5FP", 4) != 0)
			return Refuse(res, Refusal::BadMagic, "particle state: magic not C5FP");
		if(version != 1)
			return Refuse(res, Refusal::BadVersion, Fmt("particle state: version %u, this reader reads 1", version));
		c5r::fow::ParticleSettings set;
		uint32_t fw = 0, fh = 0, frames = 0, rs = 0, lost = 0, steps = 0;
		uint64_t hash = 0;
		float gx = 0, gy = 0;
		if(!s.get(&set) || !s.get(&fw) || !s.get(&fh) || !s.get(&hash) || !s.get(&frames) || !s.get(&f.cellsX) || !s.get(&f.cellsY) ||
		   !s.get(&gx) || !s.get(&gy) || !s.get(&rs) || !s.get(&lost) || !s.get(&steps))
			return Refuse(res, Refusal::Truncated, "particle state: fields");
		if(f.cellsX <= 0 || f.cellsY <= 0 || f.cellsX > 4096 || f.cellsY > 4096 || frames == 0 || !Finite(gx) || !Finite(gy))
			return Refuse(res, Refusal::BadRecord, Fmt("particle state: grid %d x %d, %u atlas frames, origin %g, %g not valid", f.cellsX, f.cellsY,
			                                           frames, gx, gy));
		const uint64_t cells = (uint64_t)f.cellsX * (uint64_t)f.cellsY;
		for(uint64_t k = 0; k < cells; ++k)
		{
			uint32_t n = 0;
			if(!s.get(&n))
				return Refuse(res, Refusal::Truncated, Fmt("particle state: the count of cell %llu", (unsigned long long)k));
			if(n > c5r::fow::kParticleCellCap)
				return Refuse(res, Refusal::BadRecord, Fmt("particle state: cell %llu holds %u particles, at most %u", (unsigned long long)k, n,
				                                           c5r::fow::kParticleCellCap));
			if(!s.has((size_t)n * sizeof(c5r::fow::Particle)))
				return Refuse(res, Refusal::Truncated, Fmt("particle state: the particles of cell %llu", (unsigned long long)k));
			s.at += (size_t)n * sizeof(c5r::fow::Particle);
			f.particles += n;
		}
		if(s.left())
			return Refuse(res, Refusal::WrongSize, Fmt("particle state: %u bytes after the last cell", (unsigned)s.left()));
		f.state.assign(b.begin() + kFogParticlesFixedBytes, b.end());
	}
	if(out)
		*out = std::move(f);
	return c5r::Status::Ok();
}

void EncodeFogParticles(const FogParticlesFile& f, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicFogParticles, 1);
	Put(out, f.flags);
	Put(out, f.seed);
	Put(out, f.worldW);
	Put(out, f.worldH);
	Put(out, (uint32_t)f.state.size());
	Put(out, (uint32_t)0);
	PutBytes(out, f.state.data(), f.state.size());
}

// ---- format version 8, ticket 0495: forest.bin and near.bin (srv_scene_bundle.h, the block at ForestStateFile) ----------
c5r::Status ParseForestState(const std::vector<uint8_t>& b, ForestStateFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicForestState, 1, res));
	Cur c(b, 16);
	ForestStateFile f;
	uint32_t nRec = 0, nBuilt = 0, nMarks = 0, reserved = 0;
	if(!c.get(&f.flags) || !c.get(&f.sessionSeed) || !c.get(&f.layoutSeed) || !c.get(&f.loads) || !c.get(&f.loadSeq) || !c.get(&f.forestId) ||
	   !c.get(&f.jungleId) || !c.get(&f.streamState) || !c.get(&f.tableGen) || !c.get(&f.tableW) || !c.get(&f.tableH) || !c.get(&nRec) ||
	   !c.get(&nBuilt) || !c.get(&nMarks) || !c.get(&f.cutW) || !c.get(&f.cutH) || !c.get(&f.cutOx) || !c.get(&f.cutOy) ||
	   !c.get(&f.cutGeneration) || !c.get(&reserved))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the forest state's fixed fields need %u", (unsigned)b.size(), kForestStateFixedBytes));
	if(reserved != 0 || (f.flags & ~(uint32_t)(kForestHaveCut | kForestIdsResolved)))
		return Refuse(res, Refusal::BadRecord, Fmt("flags 0x%X, reserved %u: only bits 0-1 and 0", f.flags, reserved));
	if(!f.sessionSeed)
		return Refuse(res, Refusal::BadRecord, "sessionSeed 0 (the scene never uses 0)");
	const bool cut = (f.flags & kForestHaveCut) != 0;
	if(cut ? (f.cutW == 0 || f.cutH == 0 || f.cutW > 16384 || f.cutH > 16384) : (f.cutW != 0 || f.cutH != 0))
		return Refuse(res, Refusal::BadRecord, Fmt("cut %u x %u with the cut flag %d (1..16384 a side with it, 0 without)", f.cutW, f.cutH, cut ? 1 : 0));
	if(!Finite(f.cutOx) || !Finite(f.cutOy))
		return Refuse(res, Refusal::BadRecord, Fmt("cut origin %g, %g not finite", f.cutOx, f.cutOy));
	const uint64_t need = (uint64_t)nRec * 16 + (uint64_t)nBuilt * 8 + (uint64_t)nMarks * 8 + (uint64_t)f.cutW * f.cutH;
	if(c.left() < need)
		return Refuse(res, Refusal::Truncated, Fmt("%u records, %u built, %u marks and a %u x %u cut need %llu bytes, %u left", nRec, nBuilt, nMarks,
		                                           f.cutW, f.cutH, (unsigned long long)need, (unsigned)c.left()));
	if(c.left() > need)
		return Refuse(res, Refusal::WrongSize, Fmt("%u bytes after the cut mask", (unsigned)(c.left() - need)));
	std::set<uint64_t> recs;
	int64_t prev = -1;
	for(uint32_t i = 0; i < nRec; ++i)
	{
		ForestStateRecord r;
		c.get(&r.x);
		c.get(&r.y);
		c.get(&r.kind);
		c.get(&r.layout);
		const int64_t key = ((int64_t)r.y << 32) | (uint32_t)r.x;
		if(r.x < 0 || r.y < 0 || r.x > 65535 || r.y > 65535 || r.kind < 1 || r.kind > 4 || r.layout >= 20 || key <= prev)
			return Refuse(res, Refusal::BadRecord, Fmt("record %u: plot %d,%d kind %u layout %u (0..65535, kind 1..4, layout below 20, ascending by y "
			                                           "then x)", i, r.x, r.y, r.kind, r.layout));
		prev = key;
		recs.insert((uint64_t)key);
		f.records.push_back(r);
	}
	std::set<uint64_t> built;
	for(uint32_t i = 0; i < nBuilt; ++i)
	{
		int32_t x = 0, y = 0;
		c.get(&x);
		c.get(&y);
		const uint64_t key = ((uint64_t)(uint32_t)y << 32) | (uint32_t)x;
		if(!recs.count(key) || !built.insert(key).second)
			return Refuse(res, Refusal::BadRecord, Fmt("built plot %u (%d,%d) is no record or is listed twice", i, x, y));
		f.built.push_back(std::make_pair(x, y));
	}
	prev = -1;
	for(uint32_t i = 0; i < nMarks; ++i)
	{
		int32_t x = 0, y = 0;
		c.get(&x);
		c.get(&y);
		const int64_t key = ((int64_t)y << 32) | (uint32_t)x;
		if(x < 0 || y < 0 || x > 65535 || y > 65535 || key <= prev)
			return Refuse(res, Refusal::BadRecord, Fmt("mark %u (%d,%d): tiles 0..65535, ascending by y then x", i, x, y));
		prev = key;
		f.marks.push_back(std::make_pair(x, y));
	}
	if(cut)
		f.cutCells.assign(b.begin() + (std::ptrdiff_t)(b.size() - (size_t)f.cutW * f.cutH), b.end());
	if(out)
		*out = std::move(f);
	return c5r::Status::Ok();
}

void EncodeForestState(const ForestStateFile& f, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicForestState, 1);
	const bool cut = (f.flags & kForestHaveCut) != 0;
	Put(out, f.flags);
	Put(out, f.sessionSeed);
	Put(out, f.layoutSeed);
	Put(out, f.loads);
	Put(out, f.loadSeq);
	Put(out, f.forestId);
	Put(out, f.jungleId);
	Put(out, f.streamState);
	Put(out, f.tableGen);
	Put(out, f.tableW);
	Put(out, f.tableH);
	Put(out, (uint32_t)f.records.size());
	Put(out, (uint32_t)f.built.size());
	Put(out, (uint32_t)f.marks.size());
	Put(out, cut ? f.cutW : 0u);
	Put(out, cut ? f.cutH : 0u);
	Put(out, f.cutOx);
	Put(out, f.cutOy);
	Put(out, f.cutGeneration);
	Put(out, (uint32_t)0);
	for(const ForestStateRecord& r : f.records)
	{
		Put(out, r.x);
		Put(out, r.y);
		Put(out, r.kind);
		Put(out, r.layout);
	}
	for(const auto& p : f.built)
	{
		Put(out, p.first);
		Put(out, p.second);
	}
	for(const auto& p : f.marks)
	{
		Put(out, p.first);
		Put(out, p.second);
	}
	if(cut)
		PutBytes(out, f.cutCells.data(), f.cutCells.size());
}

c5r::Status ParseNearPairs(const std::vector<uint8_t>& b, NearPairsFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicNearPairs, 1, res));
	Cur c(b, 16);
	NearPairsFile f;
	uint32_t flags = 0, stateBytes = 0;
	if(!c.get(&flags) || !c.get(&f.tilesX) || !c.get(&f.tilesY) || !c.get(&stateBytes))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the near pairs' fixed fields need 32", (unsigned)b.size()));
	if((flags & ~(uint32_t)kNearHasImages) || !f.tilesX || !f.tilesY)
		return Refuse(res, Refusal::BadRecord, Fmt("flags 0x%X, tiles %u x %u (flags bit 0 only, tiles not 0)", flags, f.tilesX, f.tilesY));
	const bool images = (flags & kNearHasImages) != 0;
	if(images ? c.left() < stateBytes : c.left() != stateBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("stateBytes %u, the file holds %u after the fixed fields", stateBytes, (unsigned)c.left()));
	Cur s(b, 32);
	char magic[4];
	uint32_t version = 0, tiles = 0;
	if(stateBytes < 12 || !s.get(&magic) || !s.get(&version) || !s.get(&tiles))
		return Refuse(res, Refusal::Truncated, "near state: header");
	if(memcmp(magic, "C5NC", 4) != 0)
		return Refuse(res, Refusal::BadMagic, "near state: magic not C5NC");
	if(version != 1)
		return Refuse(res, Refusal::BadVersion, Fmt("near state: version %u, this reader reads 1", version));
	if((uint64_t)tiles != (uint64_t)f.tilesX * f.tilesY)
		return Refuse(res, Refusal::BadRecord, Fmt("near state: %u tiles, the file says %u x %u", tiles, f.tilesX, f.tilesY));
	f.state.assign(b.begin() + 32, b.begin() + 32 + stateBytes);
	if(images)
	{
		Cur m(b, 32 + (size_t)stateBytes);
		uint32_t n = 0;
		if(!m.get(&n))
			return Refuse(res, Refusal::Truncated, "near images: the count");
		if(n == 0)
			return Refuse(res, Refusal::BadRecord, "near images: flag bit 0 with no image");
		int64_t prev = -1;
		for(uint32_t i = 0; i < n; ++i)
		{
			NearPairImage img;
			uint32_t bytes = 0;
			if(!m.get(&img.tile) || !m.get(&img.size) || !m.get(&img.second) || !m.get(&bytes))
				return Refuse(res, Refusal::Truncated, Fmt("near image %u: its fields", i));
			const int64_t key = (int64_t)img.tile * 2 + img.second;
			if((uint64_t)img.tile >= (uint64_t)f.tilesX * f.tilesY || img.second > 1 || !NearImageBytes(img.size) || bytes != NearImageBytes(img.size) ||
			   key <= prev)
				return Refuse(res, Refusal::BadRecord, Fmt("near image %u: tile %u, size %u, texture %u, %u bytes (a tile of the map, 64/128/256/512, "
				                                           "0 or 1, the size's chain, ascending)", i, img.tile, img.size, img.second, bytes));
			prev = key;
			if(!m.has(bytes))
				return Refuse(res, Refusal::Truncated, Fmt("near image %u: its texels", i));
			img.texels.assign(b.begin() + (std::ptrdiff_t)m.at, b.begin() + (std::ptrdiff_t)(m.at + bytes));
			m.at += bytes;
			f.images.push_back(std::move(img));
		}
		if(m.left())
			return Refuse(res, Refusal::WrongSize, Fmt("near images: %u bytes after the last", (unsigned)m.left()));
	}
	if(out)
		*out = std::move(f);
	return c5r::Status::Ok();
}

uint64_t NearImageBytes(uint32_t size)
{
	if(size != 64 && size != 128 && size != 256 && size != 512)
		return 0;
	uint64_t n = 0;
	for(uint32_t s = size;; s >>= 1)
	{
		n += (uint64_t)s * s * 4;
		if(s == 1)
			break;
	}
	return n;
}

void EncodeNearPairs(const NearPairsFile& f, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicNearPairs, 1);
	Put(out, f.images.empty() ? (uint32_t)0 : (uint32_t)kNearHasImages);
	Put(out, f.tilesX);
	Put(out, f.tilesY);
	Put(out, (uint32_t)f.state.size());
	PutBytes(out, f.state.data(), f.state.size());
	if(f.images.empty())
		return;
	Put(out, (uint32_t)f.images.size());
	for(const NearPairImage& img : f.images)
	{
		Put(out, img.tile);
		Put(out, img.size);
		Put(out, img.second);
		Put(out, (uint32_t)img.texels.size());
		PutBytes(out, img.texels.data(), img.texels.size());
	}
}

c5r::Status ParseDumpColour(const std::vector<uint8_t>& b, uint32_t* w, uint32_t* h, CheckResult* res)
{
	if(b.size() < 54)
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, shorter than the BMP headers", (unsigned)b.size()));
	if(b[0] != 'B' || b[1] != 'M')
		return Refuse(res, Refusal::BadMagic, "not a BMP (no 'BM')");
	uint32_t bfSize, bfOff, biSize;
	int32_t biW, biH;
	uint16_t planes, bits;
	uint32_t comp;
	memcpy(&bfSize, &b[2], 4);
	memcpy(&bfOff, &b[10], 4);
	memcpy(&biSize, &b[14], 4);
	memcpy(&biW, &b[18], 4);
	memcpy(&biH, &b[22], 4);
	memcpy(&planes, &b[26], 2);
	memcpy(&bits, &b[28], 2);
	memcpy(&comp, &b[30], 4);
	if(biSize != 40 || bfOff != 54 || planes != 1 || bits != 32 || comp != 0 || biW <= 0 || biH >= 0 || biW > 16384 ||
	   biH < -16384)
		return Refuse(res, Refusal::BadVersion, Fmt("not V3Sink::dump's form (info %u, offset %u, %d x %d, %u bpp, "
		                                            "compression %u; a 32-bit top-down BI_RGB BMP expected)",
		                                            biSize, bfOff, biW, biH, bits, comp));
	uint64_t want = 54 + (uint64_t)biW * (uint64_t)(-(int64_t)biH) * 4;
	if(b.size() < want)
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, %d x %d needs %llu", (unsigned)b.size(), biW, -biH,
		                                           (unsigned long long)want));
	if(b.size() > want || bfSize != want)
		return Refuse(res, Refusal::WrongSize, Fmt("%u bytes (bfSize %u), %d x %d is %llu", (unsigned)b.size(), bfSize, biW,
		                                           -biH, (unsigned long long)want));
	*w = (uint32_t)biW;
	*h = (uint32_t)(-biH);
	return c5r::Status::Ok();
}

void EncodeDumpColour(uint32_t w, uint32_t h, const void* bgra, std::vector<uint8_t>* out)
{
	out->clear();
	uint32_t img = w * h * 4;
	out->push_back('B');
	out->push_back('M');
	Put(out, (uint32_t)(54 + img));
	Put(out, (uint32_t)0);
	Put(out, (uint32_t)54);
	Put(out, (uint32_t)40);
	Put(out, (int32_t)w);
	Put(out, -(int32_t)h);
	Put(out, (uint16_t)1);
	Put(out, (uint16_t)32);
	Put(out, (uint32_t)0);
	Put(out, img);
	Put(out, (int32_t)0);
	Put(out, (int32_t)0);
	Put(out, (uint32_t)0);
	Put(out, (uint32_t)0);
	PutBytes(out, bgra, img);
}

// ---- epic 0169, issue 4308: shadow.bin, shadow/<sha16>.shtx, ice/gen<...>.ice (srv_scene_bundle.h, the block at ShadowRecord) ----
// ---- format version 5, issue 0440: units/gen<NNNNNNNNNN>.db and units_history.bin (srv_scene_bundle.h, the block at
// UnitsHistoryRecord) ---------------------------------------------------------------------------------------------------------
std::string UnitsDbPath(uint32_t generation)
{
	return Fmt("units/gen%010u.db", generation);
}

c5r::Status ParseUnitsDb(const std::vector<uint8_t>& b, CheckResult* res)
{
	static const char kSqlite[16] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', 0};
	if(b.size() < 100)
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, shorter than an SQLite file's 100-byte header", (unsigned)b.size()));
	if(memcmp(b.data(), kSqlite, 16) != 0)
		return Refuse(res, Refusal::BadMagic, "not an SQLite database (no \"SQLite format 3\" header)");
	return c5r::Status::Ok();
}

c5r::Status ParseUnitsHistory(const std::vector<uint8_t>& b, UnitsHistoryFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicUnitsHistory, 1, res));
	Cur c(b, 16);
	uint32_t rb = 0, reserved = 0;
	UnitsHistoryFile f;
	if(!c.get(&rb) || !c.get(&f.dbGeneration) || !c.get(&f.flags) || !c.get(&reserved))
		return Refuse(res, Refusal::Truncated, "no complete head after the header (recordBytes, dbGeneration, flags, reserved)");
	if(rb != (uint32_t)sizeof(UnitsHistoryRecord))
		return Refuse(res, Refusal::WrongSize, Fmt("history record size %u, this reader reads %u", rb, (unsigned)sizeof(UnitsHistoryRecord)));
	if(reserved != 0 || (f.flags & ~kUnitsHistReady))
		return Refuse(res, Refusal::BadRecord, Fmt("head flags 0x%x or reserved %u out of range", f.flags, reserved));
	if(c.left() % sizeof(UnitsHistoryRecord))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes after the head are not whole history records of %u", (unsigned)c.left(),
		                                           (unsigned)sizeof(UnitsHistoryRecord)));
	const size_t count = c.left() / sizeof(UnitsHistoryRecord);
	if(!count)
		return Refuse(res, Refusal::BadRecord, "no history record (the controller's first frame is one)");
	f.records.resize(count);
	memcpy(f.records.data(), b.data() + c.at, count * sizeof(UnitsHistoryRecord));
	for(size_t k = 0; k < count; ++k)
	{
		const UnitsHistoryRecord& r = f.records[k];
		if(r.reserved != 0 || (r.flags & ~kUnitsHistReady) || !Finite(r.nowMs))
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): flags 0x%x, reserved %u or nowMs %g out of range", (unsigned)k,
			                                           (long long)r.frameId, r.flags, r.reserved, r.nowMs));
		if(k && (r.loopIter < f.records[k - 1].loopIter || r.nowMs < f.records[k - 1].nowMs))
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): loop iteration %llu or nowMs %.6f goes back", (unsigned)k,
			                                           (long long)r.frameId, (unsigned long long)r.loopIter, r.nowMs));
	}
	if(f.records[0].flags & kUnitsHistReady)
		return Refuse(res, Refusal::BadRecord, "the first record is drawn with ready() set: it is not the controller's first frame");
	const bool readyAfter = count > 1 ? (f.records[1].flags & kUnitsHistReady) != 0 : (f.flags & kUnitsHistReady) != 0;
	if(!readyAfter)
		return Refuse(res, Refusal::BadRecord, "ready() is not set after the first record: the history does not start at the controller's first frame");
	if(out)
		*out = std::move(f);
	return c5r::Status::Ok();
}

void EncodeUnitsHistory(const UnitsHistoryFile& f, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicUnitsHistory, 1);
	Put(out, (uint32_t)sizeof(UnitsHistoryRecord));
	Put(out, f.dbGeneration);
	Put(out, f.flags);
	Put(out, (uint32_t)0);
	PutBytes(out, f.records.data(), f.records.size() * sizeof(UnitsHistoryRecord));
}

std::string IceMeshPath(uint32_t generation)
{
	return Fmt("ice/gen%010u.ice", generation);
}

std::string ShadowTexPath(const std::string& sha256Hex)
{
	return "shadow/" + sha256Hex.substr(0, 16) + ".shtx";
}

// ---- ticket 0622 (format version 11): water_frames.bin (srv_scene_bundle.h, the block at WaterFrameRecord) ---------------
std::string Sha256HexOf(const uint8_t sha[32])
{
	static const char hx[] = "0123456789abcdef";
	std::string full(64, '0');
	for(int i = 0; i < 32; ++i)
	{
		full[2 * i] = hx[sha[i] >> 4];
		full[2 * i + 1] = hx[sha[i] & 15];
	}
	return full;
}

c5r::Status ParseWaterFrames(const std::vector<uint8_t>& b, std::vector<WaterFrameRecord>* out, CheckResult* res)
{
	size_t count = 0;
	C5R_TRY(ParseRecordStream(b, kMagicWaterFrames, (uint32_t)sizeof(WaterFrameRecord), "water frame record", &count, res));
	std::vector<WaterFrameRecord> v(count);
	if(count)
		memcpy(v.data(), b.data() + 20, count * sizeof(WaterFrameRecord));
	for(size_t k = 0; k < count; ++k)
	{
		const WaterFrameRecord& r = v[k];
		bool zero = true;
		for(uint8_t c : r.wtapSha256)
			zero = zero && c == 0;
		if(k && r.frameId <= v[k - 1].frameId)
			return Refuse(res, Refusal::BadRecord, Fmt("record %u: frame %lld after %lld", (unsigned)k, (long long)r.frameId,
			                                           (long long)v[k - 1].frameId));
		if((r.flags & ~(uint32_t)kWaterCopyInForce) || r.reserved)
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): flags 0x%x or reserved %u out of range", (unsigned)k,
			                                           (long long)r.frameId, r.flags, r.reserved));
		const bool in = (r.flags & kWaterCopyInForce) != 0;
		if(!in && (r.loadSeq || r.copySeq || !zero))
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): no copy in force, but a load, copy or hash", (unsigned)k,
			                                           (long long)r.frameId));
		if(in && zero)
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): a copy in force without its hash", (unsigned)k,
			                                           (long long)r.frameId));
		if(k)
		{
			const WaterFrameRecord& p = v[k - 1];
			if((p.flags & kWaterCopyInForce) && !in)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): no copy in force after frame %lld drew one (a "
				                                           "reader never drops its copy)", (unsigned)k, (long long)r.frameId,
				                                           (long long)p.frameId));
			if((p.flags & kWaterCopyInForce) && in && p.loadSeq == r.loadSeq && r.copySeq < p.copySeq)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): load %u copy %u after copy %u (a copy never goes back)",
				                                           (unsigned)k, (long long)r.frameId, r.loadSeq, r.copySeq, p.copySeq));
		}
	}
	if(out)
		*out = std::move(v);
	return c5r::Status::Ok();
}

// ---- format version 3, issue 0364: decals/load<NNNN>.dtap (srv_scene_bundle.h, the block at DecalTapFile) ----------------
static_assert(sizeof(VPShellDecalImage) == kDecalHeaderBytes, "VPShellDecalImage: the .dtap header (terrain-decals.md 10.4)");
static_assert(sizeof(VPShellDecalRecord) == kDecalRecordBytes, "VPShellDecalRecord: the .dtap record (terrain-decals.md 10.4)");

std::string DecalTapPath(uint32_t loadSeq)
{
	return loadSeq > 9999 ? std::string() : Fmt("decals/load%04u.dtap", loadSeq);
}

c5r::Status ParseDecalTap(const std::vector<uint8_t>& b, DecalTapFile* out, CheckResult* res)
{
	if(b.size() < kDecalHeaderBytes)
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the decal image header needs %u", (unsigned)b.size(), kDecalHeaderBytes));
	VPShellDecalImage h;
	memcpy(&h, b.data(), sizeof(h));
	if(h.magic != VPSHELL_DECAL_MAGIC)
		return Refuse(res, Refusal::BadMagic, Fmt("magic 0x%08X, a decal image is 0x%08X (C5DT)", h.magic, VPSHELL_DECAL_MAGIC));
	if(h.version != VPSHELL_DECAL_IMAGE_VERSION)
		return Refuse(res, Refusal::BadVersion, Fmt("decal image version %u, this reader reads %u", h.version, VPSHELL_DECAL_IMAGE_VERSION));
	if(h.headerBytes != kDecalHeaderBytes || h.decalBytes != kDecalRecordBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("header %u and record %u bytes, this reader reads %u and %u", h.headerBytes, h.decalBytes,
		                                           kDecalHeaderBytes, kDecalRecordBytes));
	if(h.imageBytes > b.size())
		return Refuse(res, Refusal::Truncated, Fmt("imageBytes %llu, the file holds %llu", (unsigned long long)h.imageBytes,
		                                           (unsigned long long)b.size()));
	if(h.imageBytes < b.size())
		return Refuse(res, Refusal::WrongSize, Fmt("imageBytes %llu, the file holds %llu (a tail after the image)",
		                                           (unsigned long long)h.imageBytes, (unsigned long long)b.size()));
	// everything else by the reader's own file entry (the bytes loadFile reads, through loadImage): no tap, no ack
	DecalTapReader r(0);
	const c5r::Status st = r.loadImage(b.data(), b.size());
	if(!st.ok())
		return Refuse(res, Refusal::BadRecord, "the decal reader refuses the image: " + st.str());
	if(out)
	{
		out->generation = h.generation;
		out->loadSeq = h.loadSeq;
		out->decalCount = h.decalCount;
		out->nameCount = h.nameCount;
		out->vertexCount = h.vertexCount;
		out->imageBytes = h.imageBytes;
	}
	return c5r::Status::Ok();
}

c5r::Status ParseShadow(const std::vector<uint8_t>& b, std::vector<ShadowRecord>* out, CheckResult* res, uint32_t formatVersion)
{
	// ticket 4749: file form 2 (format version 10) holds the whole record, form 1 (format version 9) its first 344 bytes
	static_assert(offsetof(ShadowRecord, rawWidth) == kShadowRecordBytesForm1, "form 1 is the record up to iceLoadSeq");
	const uint32_t form = MemberKindVersionFor(MemberKind::Shadow, formatVersion);
	const uint32_t recBytes = form >= 2 ? (uint32_t)sizeof(ShadowRecord) : kShadowRecordBytesForm1;
	size_t n = 0;
	C5R_TRY(ParseRecordStream(b, kMagicShadow, recBytes, "shadow record", &n, res, form));
	std::vector<ShadowRecord> v(n);
	if(n)
		memset(v.data(), 0, n * sizeof(ShadowRecord));
	for(size_t k = 0; k < n; ++k)
		memcpy(&v[k], b.data() + 20 + k * recBytes, recBytes);
	const uint32_t maxFlags = form >= 2 ? 15u : 7u;
	for(size_t k = 0; k < n; ++k)
	{
		const ShadowRecord& s = v[k];
		const bool bound = (s.flags & kShadowBound) != 0, q0 = (s.flags & kShadowQ0) != 0;
		std::string bad;
		bool zeroSha = true;
		for(uint8_t c : s.texelSha256)
			zeroSha = zeroSha && c == 0;
		if(s.flags > maxFlags)
			bad = Fmt("flags 0x%X above bit %u", s.flags, form >= 2 ? 3u : 2u);
		else if((s.flags & kUnitMaskDropped) && !bound && !q0)
			bad = "the unit mask dropped without a bound triple";
		else if(s.rawWidth > 16384 || s.rawHeight > 16384 || (s.rawWidth == 0) != (s.rawHeight == 0))
			bad = Fmt("raw size %u x %u", s.rawWidth, s.rawHeight);
		else if(bound && q0)
			bad = "both a shadow map and quality 0";
		else if(k && s.frameId <= v[k - 1].frameId)
			bad = "the frame id does not increase";
		else if(s.quality > 3 || s.numViews > 4 || (bound && s.numViews == 0))
			bad = Fmt("quality %u, numViews %u", s.quality, s.numViews);
		else if(!Finite(s.strength) || s.strength < 0 || s.strength > 1)
			bad = Fmt("strength %g", s.strength);
		else if(s.flags & kIceDrawn && !s.iceGeneration)
			bad = "ice drawn with generation 0";
		else if(bound && ((s.dxgiFormat != 56 && s.dxgiFormat != 10) || !s.width || !s.height || zeroSha))
			bad = Fmt("a bound shadow map of format %u, %u x %u, %s texel hash", s.dxgiFormat, s.width, s.height, zeroSha ? "no" : "a");
		else if(q0 && (s.dxgiFormat || s.strength != 0 || !zeroSha))
			bad = "quality 0 with a texture or a strength";
		for(uint32_t vw = 0; bad.empty() && bound && vw < s.numViews; ++vw)
			for(int i = 0; i < 16; ++i)
				if(!Finite(s.matrices[vw][i]))
				{
					bad = Fmt("the matrix of view %u is not finite", vw);
					break;
				}
		if(!bad.empty())
			return Refuse(res, Refusal::BadRecord, Fmt("record %u (frame %lld): %s", (unsigned)k, (long long)s.frameId, bad.c_str()));
	}
	if(out)
		out->swap(v);
	return c5r::Status::Ok();
}

c5r::Status ParseShadowTexels(const std::vector<uint8_t>& b, ShadowTexFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicShadowTex, 1, res));
	Cur c(b, 16);
	ShadowTexFile f;
	if(!c.get(&f.width) || !c.get(&f.height) || !c.get(&f.dxgiFormat) || !c.get(&f.bytesPerTexel))
		return Refuse(res, Refusal::Truncated, "no texel table after the header");
	if(!((f.dxgiFormat == 56 && f.bytesPerTexel == 2) || (f.dxgiFormat == 10 && f.bytesPerTexel == 8)) || !f.width || !f.height ||
	   f.width > 16384 || f.height > 16384)
		return Refuse(res, Refusal::BadRecord, Fmt("format %u with %u bytes a texel, %u x %u", f.dxgiFormat, f.bytesPerTexel, f.width, f.height));
	const uint64_t want = (uint64_t)f.width * f.height * f.bytesPerTexel;
	if(c.left() < want)
		return Refuse(res, Refusal::Truncated, Fmt("%llu texel bytes, %u x %u x %u needs %llu", (unsigned long long)c.left(), f.width, f.height,
		                                           f.bytesPerTexel, (unsigned long long)want));
	if(c.left() > want)
		return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes after the texels", (unsigned long long)(c.left() - want)));
	f.texelOffset = c.at;
	if(out)
		*out = f;
	return c5r::Status::Ok();
}

void EncodeShadowTexels(uint32_t width, uint32_t height, uint32_t dxgiFormat, const void* texels, size_t bytes, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicShadowTex, 1);
	Put(out, width);
	Put(out, height);
	Put(out, dxgiFormat);
	Put(out, (uint32_t)(dxgiFormat == 10 ? 8 : 2));
	PutBytes(out, texels, bytes);
}

c5r::Status ParseIceMesh(const std::vector<uint8_t>& b, c5r::ice::Mesh* out, uint32_t* generation, uint32_t* loadSeq, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicIceMesh, 1, res));
	Cur c(b, 16);
	uint32_t gen, load, vbBytes, ibBytes, chunks, fieldsBytes, fieldsValid;
	if(!c.get(&gen) || !c.get(&load) || !c.get(&vbBytes) || !c.get(&ibBytes) || !c.get(&chunks) || !c.get(&fieldsBytes) || !c.get(&fieldsValid))
		return Refuse(res, Refusal::Truncated, "no mesh table after the header");
	if(fieldsBytes != c5r::ice::kFieldsSize || fieldsValid > 1)
		return Refuse(res, Refusal::BadRecord, Fmt("fieldsBytes %u (0x%X expected), fieldsValid %u", fieldsBytes, c5r::ice::kFieldsSize, fieldsValid));
	if(!chunks || chunks > 4096 || !vbBytes || vbBytes % c5r::ice::kIceStride || !ibBytes || ibBytes % 2)
		return Refuse(res, Refusal::BadRecord, Fmt("%u chunks, VB %u bytes, IB %u bytes", chunks, vbBytes, ibBytes));
	const uint64_t want = (uint64_t)fieldsBytes + (uint64_t)chunks * 28 + vbBytes + ibBytes;
	if(c.left() < want)
		return Refuse(res, Refusal::Truncated, Fmt("%llu bytes after the table, the mesh needs %llu", (unsigned long long)c.left(),
		                                           (unsigned long long)want));
	if(c.left() > want)
		return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes after the mesh", (unsigned long long)(c.left() - want)));
	c5r::ice::Mesh m;
	memcpy(m.fields.raw, b.data() + c.at, fieldsBytes);
	m.fields.valid = fieldsValid == 1;
	c.at += fieldsBytes;
	m.chunks.resize(chunks);
	for(uint32_t i = 0; i < chunks; ++i)
	{
		c5r::ice::Chunk& k = m.chunks[i];
		c.get(&k.box[0]);
		c.get(&k.box[1]);
		c.get(&k.box[2]);
		c.get(&k.box[3]);
		c.get(&k.triangles);
		c.get(&k.startIndex);
		c.get(&k.baseVertex);
		bool fin = true;
		for(float f : k.box)
			fin = fin && Finite(f);
		if(!fin || k.box[0] > k.box[2] || k.box[1] > k.box[3] || (uint64_t)k.startIndex + 3ull * k.triangles > ibBytes / 2)
			return Refuse(res, Refusal::BadRecord, Fmt("chunk %u: box (%g, %g, %g, %g), %u triangles from index %u, IB %u indices", i, k.box[0],
			                                           k.box[1], k.box[2], k.box[3], k.triangles, k.startIndex, ibBytes / 2));
	}
	m.vb.assign(b.data() + c.at, b.data() + c.at + vbBytes);
	c.at += vbBytes;
	m.ib.assign(b.data() + c.at, b.data() + c.at + ibBytes);
	if(out)
		*out = std::move(m);
	if(generation)
		*generation = gen;
	if(loadSeq)
		*loadSeq = load;
	return c5r::Status::Ok();
}

void EncodeIceMesh(const c5r::ice::Mesh& m, uint32_t generation, uint32_t loadSeq, std::vector<uint8_t>* out)
{
	out->clear();
	PutHeader(out, kMagicIceMesh, 1);
	Put(out, generation);
	Put(out, loadSeq);
	Put(out, (uint32_t)m.vb.size());
	Put(out, (uint32_t)m.ib.size());
	Put(out, (uint32_t)m.chunks.size());
	Put(out, (uint32_t)c5r::ice::kFieldsSize);
	Put(out, (uint32_t)(m.fields.valid ? 1 : 0));
	PutBytes(out, m.fields.raw, c5r::ice::kFieldsSize);
	for(const c5r::ice::Chunk& k : m.chunks)
	{
		Put(out, k.box[0]);
		Put(out, k.box[1]);
		Put(out, k.box[2]);
		Put(out, k.box[3]);
		Put(out, k.triangles);
		Put(out, k.startIndex);
		Put(out, k.baseVertex);
	}
	PutBytes(out, m.vb.data(), m.vb.size());
	PutBytes(out, m.ib.data(), m.ib.size());
}

// ==== the checker =======================================================================================================
// ---- format version 9, ticket 4875: modeltap/frame<NNNNNN>.mtap (srv_scene_bundle.h, the block at ModelTapFile) --------
static_assert(sizeof(VPModelTapFrame) == kModelTapHeaderBytes, "VPModelTapFrame: the .mtap header (shell_model_tap.h)");
static_assert(sizeof(VPModelTapDraw) == kModelTapDrawBytes, "VPModelTapDraw: the .mtap draw record (shell_model_tap.h)");

std::string ModelTapPath(uint32_t frameIndex)
{
	return frameIndex > 999999 ? std::string() : Fmt("modeltap/frame%06u.mtap", frameIndex);
}

bool ModelTapIndex(const std::string& p, uint32_t* frameIndex)
{
	if(p.size() != 14 + 6 + 5 || !StartsWith(p, "modeltap/frame") || !EndsWith(p, ".mtap"))
		return false;
	uint32_t v = 0;
	for(size_t c = 14; c < 20; ++c)
	{
		if(p[c] < '0' || p[c] > '9')
			return false;
		v = v * 10 + (uint32_t)(p[c] - '0');
	}
	if(frameIndex)
		*frameIndex = v;
	return true;
}

c5r::Status ParseModelTap(const std::vector<uint8_t>& b, ModelTapFile* out, CheckResult* res)
{
	if(b.size() < kModelTapHeaderBytes)
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the model tap frame header needs %u", (unsigned)b.size(), kModelTapHeaderBytes));
	VPModelTapFrame h;
	memcpy(&h, b.data(), sizeof(h));
	if(h.magic != VPSHELL_MODELTAP_MAGIC)
		return Refuse(res, Refusal::BadMagic, Fmt("magic 0x%08X, a model tap frame is 0x%08X (C5MT)", h.magic, VPSHELL_MODELTAP_MAGIC));
	if(h.version != VPSHELL_MODELTAP_VERSION)
		return Refuse(res, Refusal::BadVersion, Fmt("model tap frame version %u, this reader reads %u", h.version, VPSHELL_MODELTAP_VERSION));
	if(h.headerBytes != kModelTapHeaderBytes || h.drawBytes != kModelTapDrawBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("header %u and draw record %u bytes, this reader reads %u and %u", h.headerBytes,
		                                           h.drawBytes, kModelTapHeaderBytes, kModelTapDrawBytes));
	if(h.imageBytes > b.size())
		return Refuse(res, Refusal::Truncated, Fmt("imageBytes %u, the file holds %llu", h.imageBytes, (unsigned long long)b.size()));
	if(h.imageBytes < b.size())
		return Refuse(res, Refusal::WrongSize, Fmt("imageBytes %u, the file holds %llu (a tail after the image)", h.imageBytes,
		                                           (unsigned long long)b.size()));
	const uint64_t hash = VPModelHash64(b.data() + kModelTapHeaderBytes, b.size() - kModelTapHeaderBytes);
	if(hash != h.fnv1a64)
		return Refuse(res, Refusal::BadRecord, Fmt("image hash 0x%016llX, the header's 0x%016llX", (unsigned long long)hash,
		                                           (unsigned long long)h.fnv1a64));
#ifdef SRV_BUNDLE_HAVE_READTAP
	// everything else by the one content check (4873's readTap, lead2-0561 #6146)
	{
		c5r::landmarks::TapFrame f;
		const c5r::Status st = c5r::landmarks::readTap(b.data(), b.size(), f);
		if(!st.ok())
			return Refuse(res, Refusal::BadRecord, "readTap refuses the frame: " + st.str());
	}
#endif
	if(out)
	{
		out->frameSeq = h.frameSeq;
		out->presentCount = h.presentCount;
		out->drawCount = h.drawCount;
		out->imageBytes = h.imageBytes;
		out->latchedBits = h.latchedBits;
		out->setGeneration = h.setGeneration;
		out->dropped[0] = h.dropped[0];
		out->dropped[1] = h.dropped[1];
	}
	return c5r::Status::Ok();
}

// ---- format version 9, optional member (ticket 4888): modelgeom/frame<NNNNNN>.mgeo (the block at ModelGeomFile) -----------
static_assert(kModelGeomMaxEntries == VPSHELL_MODELGEOM_MAX_ENTRIES, "the section's table");
static_assert(kModelGeomMaxBuffer == VPSHELL_MODELGEOM_MAX_BUFFER, "the section's buffer limit");

std::string ModelGeomPath(uint32_t frameIndex)
{
	return frameIndex > 999999 ? std::string() : Fmt("modelgeom/frame%06u.mgeo", frameIndex);
}

bool ModelGeomIndex(const std::string& p, uint32_t* frameIndex)
{
	if(p.size() != 15 + 6 + 5 || !StartsWith(p, "modelgeom/frame") || !EndsWith(p, ".mgeo"))
		return false;
	uint32_t v = 0;
	for(size_t c = 15; c < 21; ++c)
	{
		if(p[c] < '0' || p[c] > '9')
			return false;
		v = v * 10 + (uint32_t)(p[c] - '0');
	}
	if(frameIndex)
		*frameIndex = v;
	return true;
}

c5r::Status ParseModelGeom(const std::vector<uint8_t>& b, ModelGeomFile* out, CheckResult* res)
{
	C5R_TRY(CheckHeader(b, kMagicModelGeom, 1, res));
	Cur c(b, 16);
	uint32_t count = 0, entryBytes = 0;
	uint64_t dataBytes = 0;
	if(!c.get(&count) || !c.get(&entryBytes) || !c.get(&dataBytes))
		return Refuse(res, Refusal::Truncated, Fmt("%u bytes, the geometry list's fixed fields need 32", (unsigned)b.size()));
	if(entryBytes != kModelGeomEntryBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("entryBytes %u, this reader reads %u", entryBytes, kModelGeomEntryBytes));
	if(count > kModelGeomMaxEntries)
		return Refuse(res, Refusal::WrongSize, Fmt("%u entries, at most %u (the geometry section's table)", count, kModelGeomMaxEntries));
	const uint64_t need = (uint64_t)count * kModelGeomEntryBytes + dataBytes;
	if(dataBytes > (uint64_t)kModelGeomMaxEntries * kModelGeomMaxBuffer || c.left() < need)
		return Refuse(res, Refusal::Truncated, Fmt("%u entries and %llu data bytes, the file holds %u after the fixed fields", count,
		                                           (unsigned long long)dataBytes, (unsigned)c.left()));
	if(c.left() > need)
		return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes after the data (a tail)", (unsigned long long)(c.left() - need)));
	const size_t data0 = 32 + (size_t)count * kModelGeomEntryBytes;
	ModelGeomFile f;
	f.dataBytes = dataBytes;
	uint64_t at = 0;
	uint32_t lastKind = 0;
	uint64_t lastHash = 0;
	for(uint32_t i = 0; i < count; ++i)
	{
		uint64_t hash = 0, off = 0, bytes = 0;
		uint32_t kind = 0, stride = 0;
		c.get(&hash);
		c.get(&kind);
		c.get(&stride);
		c.get(&off);
		c.get(&bytes);
		const std::string e = Fmt("entry %u (kind %u, hash %016llX)", i, kind, (unsigned long long)hash);
		if(kind != VPMGEOM_KIND_INDEX32 && kind != VPMGEOM_KIND_VERTEX)
			return Refuse(res, Refusal::BadRecord, e + ": kind is not 1 (index R32) or 2 (vertex)");
		const bool ib = kind == VPMGEOM_KIND_INDEX32;
		if(ib ? stride != 4 : (stride != 20 && stride != 32))
			return Refuse(res, Refusal::BadRecord, e + Fmt(": stride %u (an index buffer 4, a vertex buffer 20 or 32)", stride));
		if(!bytes || bytes % stride || (ib && bytes < 8) || bytes > kModelGeomMaxBuffer)
			return Refuse(res, Refusal::BadRecord, e + Fmt(": %llu bytes (not 0, a multiple of the stride, an index buffer at least 8, "
			                                               "at most %llu)", (unsigned long long)bytes,
			                                               (unsigned long long)kModelGeomMaxBuffer));
		if(off != at || bytes > dataBytes - at)
			return Refuse(res, Refusal::BadRecord, e + Fmt(": offset %llu and %llu bytes, the data runs back to back from %llu of %llu",
			                                               (unsigned long long)off, (unsigned long long)bytes, (unsigned long long)at,
			                                               (unsigned long long)dataBytes));
		if(i && (kind < lastKind || (kind == lastKind && hash <= lastHash)))
			return Refuse(res, Refusal::BadRecord, e + ": not after the entry before it (ascending kind and hash, no duplicate)");
		const uint8_t* p = b.data() + data0 + (size_t)off;
		const uint64_t h = VPModelGeomHash(kind, p, (size_t)bytes);
		if(h != hash)
			return Refuse(res, Refusal::BadRecord, e + Fmt(": the content hash is %016llX", (unsigned long long)h));
		if(out)
		{
			ModelGeomBuffer g;
			g.kind = kind;
			g.stride = stride;
			g.hash = hash;
			g.bytes.assign(p, p + (size_t)bytes);
			f.buffers.push_back(std::move(g));
		}
		at += bytes;
		lastKind = kind;
		lastHash = hash;
	}
	if(at != dataBytes)
		return Refuse(res, Refusal::WrongSize, Fmt("dataBytes %llu, the entries hold %llu", (unsigned long long)dataBytes,
		                                           (unsigned long long)at));
	if(out)
		*out = std::move(f);
	return c5r::Status::Ok();
}

void EncodeModelGeom(std::vector<ModelGeomBuffer> buffers, std::vector<uint8_t>* out)
{
	std::sort(buffers.begin(), buffers.end(), [](const ModelGeomBuffer& a, const ModelGeomBuffer& b) {
		return a.kind != b.kind ? a.kind < b.kind : a.hash < b.hash;
	});
	out->clear();
	PutHeader(out, kMagicModelGeom, 1);
	uint64_t data = 0;
	for(const ModelGeomBuffer& g : buffers)
		data += g.bytes.size();
	Put(out, (uint32_t)buffers.size());
	Put(out, kModelGeomEntryBytes);
	Put(out, data);
	uint64_t at = 0;
	for(const ModelGeomBuffer& g : buffers)
	{
		Put(out, g.hash);
		Put(out, g.kind);
		Put(out, g.stride);
		Put(out, at);
		Put(out, (uint64_t)g.bytes.size());
		at += g.bytes.size();
	}
	for(const ModelGeomBuffer& g : buffers)
		PutBytes(out, g.bytes.data(), g.bytes.size());
}

namespace {

c5r::Status ParseByKind(const std::string& path, const Member& mem, const std::vector<uint8_t>& b, CheckResult* res,
                        std::vector<FrameRecord>* frames, size_t* cameraBlocks, uint32_t* bmpW, uint32_t* bmpH,
                        std::vector<ShadowRecord>* shadow, UnitsHistoryFile* history, uint32_t formatVersion,
                        std::vector<WaterFrameRecord>* waterFrames)
{
	switch(mem.kind)
	{
	case MemberKind::UnitsDb: return ParseUnitsDb(b, res);                         // format version 5 (issue 0440)
	case MemberKind::UnitsHistory: return ParseUnitsHistory(b, history, res);      // format version 5 (issue 0440)
	case MemberKind::FogParticles: return ParseFogParticles(b, nullptr, res);      // format version 6 (ticket 0403)
	case MemberKind::ForestState: return ParseForestState(b, nullptr, res);        // format version 8 (ticket 0495)
	case MemberKind::NearPairs: return ParseNearPairs(b, nullptr, res);            // format version 8 (ticket 0495)
	case MemberKind::ModelTap: return ParseModelTap(b, nullptr, res);              // format version 9 (ticket 4875)
	case MemberKind::ModelGeom: return ParseModelGeom(b, nullptr, res);            // format version 9, optional (ticket 4888)
	case MemberKind::WaterFrames: return ParseWaterFrames(b, waterFrames, res);    // format version 11 (ticket 0622)
	case MemberKind::Heightfield: return ParseHeightfield(b, nullptr, res);
	case MemberKind::WaterTap: return ParseWaterTap(b, nullptr, res);
	case MemberKind::OverlayTexture:
	{
		C5R_TRY(ParseOverlay(b, nullptr, res));
		if(path.compare(8, 16, mem.sha256, 0, 16) != 0)
			return Refuse(res, Refusal::ShaMismatch, "the file name is not the first 16 hex digits of its SHA-256");
		return c5r::Status::Ok();
	}
	case MemberKind::Feed: return ParseFeed(b, nullptr, res);
	case MemberKind::Frames: return ParseFrames(b, frames, res);
	case MemberKind::CameraBlocks:
	{
		std::vector<VPShellCamera> cams;
		C5R_TRY(ParseCameras(b, &cams, res));
		*cameraBlocks = cams.size();
		return c5r::Status::Ok();
	}
	case MemberKind::NamedCameraBlock:
	{
		std::vector<VPShellCamera> cams;
		C5R_TRY(ParseCameras(b, &cams, res));
		if(cams.size() != 1)
			return Refuse(res, Refusal::WrongSize, Fmt("%u camera blocks, a .vpcam holds one", (unsigned)cams.size()));
		return c5r::Status::Ok();
	}
	case MemberKind::WaterClock: return ParseWaterClock(b, nullptr, res);
	case MemberKind::FogClock: return ParseFogClock(b, nullptr, res);   // issue 3911
	case MemberKind::Shadow: return ParseShadow(b, shadow, res, formatVersion);   // issue 4308; the form by format (4749)
	case MemberKind::ShadowTexels:
	{
		C5R_TRY(ParseShadowTexels(b, nullptr, res));
		if(Sha256Hex(b.data(), b.size()).compare(0, 16, path.substr(7, 16)) != 0)
			return Refuse(res, Refusal::ShaMismatch, "the file name is not the first 16 hex digits of its SHA-256");
		return c5r::Status::Ok();
	}
	case MemberKind::IceMesh:
	{
		uint32_t gen = 0;
		C5R_TRY(ParseIceMesh(b, nullptr, &gen, nullptr, res));
		if(path != IceMeshPath(gen))
			return Refuse(res, Refusal::BadRecord, Fmt("generation %u in a file named %s", gen, path.c_str()));
		return c5r::Status::Ok();
	}
	case MemberKind::DecalTap:   // format version 3 (issue 0364)
	{
		DecalTapFile f;
		C5R_TRY(ParseDecalTap(b, &f, res));
		if(path != DecalTapPath(f.loadSeq))
			return Refuse(res, Refusal::BadRecord, Fmt("load %u in a file named %s", f.loadSeq, path.c_str()));
		return c5r::Status::Ok();
	}
	case MemberKind::DumpColour: return ParseDumpColour(b, bmpW, bmpH, res);
	case MemberKind::DumpDepth:
		if(b.empty() || b.size() % 4)
			return Refuse(res, Refusal::Truncated, Fmt("%u bytes are not whole float32 values", (unsigned)b.size()));
		return c5r::Status::Ok();
	case MemberKind::NamedCamera:
	{
		JVal v;
		std::string why;
		std::string s(b.begin(), b.end());
		JParser p(s);
		if(!p.parse(&v, &why))
			return Refuse(res, Refusal::BadJson, why);
		if(v.t != JVal::Obj)
			return Refuse(res, Refusal::BadJson, "the named camera is not a JSON object");
		return c5r::Status::Ok();
	}
	case MemberKind::StockShot:
	{
		static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
		if(b.size() < 8)
			return Refuse(res, Refusal::Truncated, "shorter than the PNG signature");
		if(memcmp(b.data(), sig, 8) != 0)
			return Refuse(res, Refusal::BadMagic, "not a PNG");
		return c5r::Status::Ok();
	}
	case MemberKind::StockCapture:
		if(b.empty())
			return Refuse(res, Refusal::Truncated, "an empty capture");
		return c5r::Status::Ok();
	case MemberKind::StockExport: return c5r::Status::Ok();
	}
	return Refuse(res, Refusal::BadValue, "unknown kind");
}

}  // namespace

c5r::Status ReadMember(const std::wstring& bundleDir, const Manifest& m, const std::string& path,
                       std::vector<uint8_t>* bytes, CheckResult* res)
{
	CheckResult local;
	if(!res)
		res = &local;
	*res = CheckResult();
	res->member = path;
	auto it = m.members.find(path);
	if(it == m.members.end())
		return Refuse(res, Refusal::MissingMember, "not listed in scene.json");
	c5r::Status st = ReadWhole(Join(bundleDir, path), bytes);
	if(st.code == c5r::Err::NotFound)
		return Refuse(res, Refusal::MissingMember, "listed in scene.json, file missing");
	if(!st.ok())
		return Refuse(res, Refusal::Io, st.msg);
	if(bytes->size() != it->second.bytes)
		return Refuse(res, Refusal::ShaMismatch, Fmt("%llu bytes, scene.json lists %llu", (unsigned long long)bytes->size(),
		                                             (unsigned long long)it->second.bytes));
	std::string h = Sha256Hex(bytes->data(), bytes->size());
	if(h.empty())
		return Refuse(res, Refusal::Io, "SHA-256 is not available (bcrypt.dll)");
	if(h != it->second.sha256)
		return Refuse(res, Refusal::ShaMismatch, "SHA-256 " + h + ", scene.json lists " + it->second.sha256);
	return c5r::Status::Ok();
}

c5r::Status CheckBundle(const std::wstring& bundleDir, CheckResult* res)
{
	CheckResult local;
	if(!res)
		res = &local;
	Manifest m;
	C5R_TRY(ReadManifest(bundleDir, &m, res));
	for(const std::string& n : m.versionNotes)
		if(StartsWith(n, "incomplete"))
			return Refuse(res, Refusal::Incomplete, "version_notes: " + n);
	std::vector<std::string> files;
	if(!ListFiles(bundleDir, std::string(), &files))
		return Refuse(res, Refusal::Io, "a folder of the bundle cannot be listed");
	for(const auto& kv : m.members)
		if(!std::binary_search(files.begin(), files.end(), kv.first))
			return Refuse(res, Refusal::MissingMember, "listed in scene.json, file missing", kv.first);
	for(const std::string& f : files)
		if(f != "scene.json" && !m.members.count(f))
			return Refuse(res, Refusal::UnlistedFile, "a file the manifest does not list", f);
	std::vector<FrameRecord> frames;
	std::vector<ShadowRecord> shadow;   // issue 4308
	bool haveShadow = false;
	UnitsHistoryFile history;           // issue 0440 (format version 5)
	bool haveFrames = false, haveCameras = false;
	size_t cameraBlocks = 0;
	std::map<int64_t, std::pair<uint32_t, uint32_t>> bmpSize;   // frame -> bmp w, h
	std::map<int64_t, uint64_t> depthBytes;
	std::vector<WaterFrameRecord> waterFrames;   // ticket 0622 (format version 11)
	bool haveWaterFrames = false;
	std::map<std::string, std::pair<uint32_t, uint32_t>> waterBySha;   // a water_tap member's sha256 -> its load, copy seq
	for(const auto& kv : m.members)
	{
		std::vector<uint8_t> b;
		C5R_TRY(ReadMember(bundleDir, m, kv.first, &b, res));
		uint32_t bw = 0, bh = 0;
		c5r::Status st = ParseByKind(kv.first, kv.second, b, res, &frames, &cameraBlocks, &bw, &bh, &shadow, &history, m.formatVersion,
		                             &waterFrames);
		if(!st.ok())
		{
			res->member = kv.first;
			return st;
		}
		if(kv.second.kind == MemberKind::WaterFrames)
			haveWaterFrames = true;
		if(kv.second.kind == MemberKind::WaterTap)   // ticket 0622: the block names the copy that water_frames.bin points at
		{
			WaterTapFile wf;
			CheckResult wr;
			if(ParseWaterTap(b, &wf, &wr).ok())
				waterBySha[kv.second.sha256] = std::make_pair(wf.block.loadSeq, wf.block.copySeq);
		}
		int64_t id;
		if(kv.second.kind == MemberKind::Frames)
			haveFrames = true;
		if(kv.second.kind == MemberKind::CameraBlocks)
			haveCameras = true;
		if(kv.second.kind == MemberKind::Shadow)
			haveShadow = true;
		if(kv.second.kind == MemberKind::DumpColour && ParseFrameIdName(kv.first, ".bmp", &id))
			bmpSize[id] = std::make_pair(bw, bh);
		if(kv.second.kind == MemberKind::DumpDepth && ParseFrameIdName(kv.first, ".depth.f32", &id))
			depthBytes[id] = b.size();
	}
	// ---- cross-checks ----
	if(m.frameCount && (!haveFrames || !haveCameras))
		return Refuse(res, Refusal::MissingMember, Fmt("frames.count %u without %s", m.frameCount,
		                                               !haveFrames ? "frames.bin" : "cameras.bin"),
		              !haveFrames ? "frames.bin" : "cameras.bin");
	if(haveFrames && frames.size() != m.frameCount)
		return Refuse(res, Refusal::WrongSize, Fmt("%u frame records, frames.count is %u", (unsigned)frames.size(),
		                                           m.frameCount), "frames.bin");
	if(haveCameras && cameraBlocks != m.frameCount)
		return Refuse(res, Refusal::WrongSize, Fmt("%u camera blocks, frames.count is %u", (unsigned)cameraBlocks,
		                                           m.frameCount), "cameras.bin");
	if(!frames.empty() && frames[0].frameId != m.firstFrameId)
		return Refuse(res, Refusal::BadValue, Fmt("frames.first_id %lld, frames.bin starts at %lld",
		                                          (long long)m.firstFrameId, (long long)frames[0].frameId));
	std::map<int64_t, const FrameRecord*> byId;
	for(const FrameRecord& r : frames)
		byId[r.frameId] = &r;
	for(size_t k = 0; k < m.dumpedFrames.size(); ++k)
	{
		int64_t id = m.dumpedFrames[k];
		if(k && id <= m.dumpedFrames[k - 1])
			return Refuse(res, Refusal::BadValue, "frames.dumped is not strictly increasing");
		auto f = byId.find(id);
		if(f == byId.end())
			return Refuse(res, Refusal::BadValue, Fmt("dumped frame %lld is not in the span", (long long)id));
		std::string bmp = Fmt("dumps/%lld.bmp", (long long)id), dep = Fmt("dumps/%lld.depth.f32", (long long)id);
		if(!bmpSize.count(id))
			return Refuse(res, Refusal::MissingMember, "a dumped frame without its colour dump", bmp);
		if(!depthBytes.count(id))
			return Refuse(res, Refusal::MissingMember, "a dumped frame without its depth dump", dep);
		const FrameRecord& r = *f->second;
		if(bmpSize[id].first != r.width || bmpSize[id].second != r.height)
			return Refuse(res, Refusal::WrongSize, Fmt("%u x %u, the frame's targets are %u x %u", bmpSize[id].first,
			                                           bmpSize[id].second, r.width, r.height), bmp);
		if(depthBytes[id] != (uint64_t)r.width * r.height * 4)
			return Refuse(res, Refusal::WrongSize, Fmt("%llu bytes, the frame's targets are %u x %u float32",
			                                           (unsigned long long)depthBytes[id], r.width, r.height), dep);
	}
	for(const FrameRecord& r : frames)
	{
		bool listed = std::find(m.dumpedFrames.begin(), m.dumpedFrames.end(), r.frameId) != m.dumpedFrames.end();
		if(listed != ((r.flags & kFrameDumped) != 0))
			return Refuse(res, Refusal::BadRecord, Fmt("frame %lld: the dumped flag and frames.dumped disagree",
			                                           (long long)r.frameId), "frames.bin");
		if(r.overlayBound)
		{
			static const char hx[] = "0123456789abcdef";
			std::string full(64, '0');
			for(int i = 0; i < 32; ++i)
			{
				full[2 * i] = hx[r.overlaySha256[i] >> 4];
				full[2 * i + 1] = hx[r.overlaySha256[i] & 15];
			}
			std::string p = "overlay/" + full.substr(0, 16) + ".ovl";
			auto o = m.members.find(p);
			if(o == m.members.end() || o->second.kind != MemberKind::OverlayTexture)
				return Refuse(res, Refusal::MissingMember, Fmt("frame %lld binds an overlay that is not in the bundle",
				                                               (long long)r.frameId), p);
			if(o->second.sha256 != full)
				return Refuse(res, Refusal::ShaMismatch, Fmt("frame %lld names SHA-256 %s", (long long)r.frameId,
				                                             full.c_str()), p);
		}
	}
	// issue 4308: shadow.bin (optional) holds one record per frame with frames.bin's ids; every bound texture and every drawn
	// ice mesh is in the bundle
	if(haveShadow)
	{
		if(shadow.size() != frames.size())
			return Refuse(res, Refusal::WrongSize, Fmt("%u shadow records, %u frame records", (unsigned)shadow.size(), (unsigned)frames.size()),
			              "shadow.bin");
		for(size_t k = 0; k < shadow.size(); ++k)
		{
			const ShadowRecord& s = shadow[k];
			if(s.frameId != frames[k].frameId)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u is frame %lld, frames.bin's is %lld", (unsigned)k, (long long)s.frameId,
				                                           (long long)frames[k].frameId), "shadow.bin");
			if(s.flags & kShadowBound)
			{
				static const char hx[] = "0123456789abcdef";
				std::string full(64, '0');
				for(int i = 0; i < 32; ++i)
				{
					full[2 * i] = hx[s.texelSha256[i] >> 4];
					full[2 * i + 1] = hx[s.texelSha256[i] & 15];
				}
				const std::string p = ShadowTexPath(full);
				auto o = m.members.find(p);
				if(o == m.members.end() || o->second.kind != MemberKind::ShadowTexels)
					return Refuse(res, Refusal::MissingMember, Fmt("frame %lld binds a shadow map that is not in the bundle", (long long)s.frameId), p);
				if(o->second.sha256 != full)
					return Refuse(res, Refusal::ShaMismatch, Fmt("frame %lld names SHA-256 %s", (long long)s.frameId, full.c_str()), p);
			}
			if(s.flags & kIceDrawn)
			{
				const std::string p = IceMeshPath(s.iceGeneration);
				auto o = m.members.find(p);
				if(o == m.members.end() || o->second.kind != MemberKind::IceMesh)
					return Refuse(res, Refusal::MissingMember, Fmt("frame %lld drew an ice mesh that is not in the bundle", (long long)s.frameId), p);
			}
		}
	}
	for(const auto& kv : m.members)
		if((kv.second.kind == MemberKind::ShadowTexels || kv.second.kind == MemberKind::IceMesh) && !haveShadow)
			return Refuse(res, Refusal::MissingMember, "a shadow or ice member without shadow.bin", "shadow.bin");
	// ticket 0622 (format version 11): water_frames.bin, one record per frame with frames.bin's ids, each named copy a water_tap
	// member of the bundle with the record's load and copy seq; required in a format-11 bundle with water copies and frames
	if(haveWaterFrames && m.formatVersion < kWaterFramesFormatVersion)
		return Refuse(res, Refusal::BadValue, Fmt("a water_frames member in a format_version %u bundle (the member is format %u's)",
		                                          m.formatVersion, kWaterFramesFormatVersion), "water_frames.bin");
	if(!haveWaterFrames && m.formatVersion >= kWaterFramesFormatVersion && haveFrames && !waterBySha.empty())
		return Refuse(res, Refusal::MissingMember, Fmt("format_version %u with water_tap members and frames needs water_frames.bin "
		                                               "(the water copy drawn per frame)", m.formatVersion), "water_frames.bin");
	if(haveWaterFrames)
	{
		if(waterFrames.size() != frames.size())
			return Refuse(res, Refusal::WrongSize, Fmt("%u water frame records, %u frame records", (unsigned)waterFrames.size(),
			                                           (unsigned)frames.size()), "water_frames.bin");
		for(size_t k = 0; k < waterFrames.size(); ++k)
		{
			const WaterFrameRecord& w = waterFrames[k];
			if(w.frameId != frames[k].frameId)
				return Refuse(res, Refusal::BadRecord, Fmt("record %u is frame %lld, frames.bin's is %lld", (unsigned)k, (long long)w.frameId,
				                                           (long long)frames[k].frameId), "water_frames.bin");
			if(!(w.flags & kWaterCopyInForce))
				continue;
			const std::string full = Sha256HexOf(w.wtapSha256);
			auto o = waterBySha.find(full);
			if(o == waterBySha.end())
				return Refuse(res, Refusal::MissingMember, Fmt("frame %lld drew a water copy (load %u, copy %u, SHA-256 %s) that is not a "
				                                               "water_tap member", (long long)w.frameId, w.loadSeq, w.copySeq, full.c_str()),
				              "water_frames.bin");
			if(o->second.first != w.loadSeq || o->second.second != w.copySeq)
				return Refuse(res, Refusal::BadRecord, Fmt("frame %lld names load %u copy %u, the member with its SHA-256 holds load %u "
				                                           "copy %u", (long long)w.frameId, w.loadSeq, w.copySeq, o->second.first,
				                                           o->second.second), "water_frames.bin");
		}
	}
	// format version 3, issue 0364: the decal member against render_decals (at most one: the set in force at the first frame)
	{
		auto rd = m.renderConfig.find("render_decals");
		const bool decalsOn = rd != m.renderConfig.end() && rd->second == "1";
		std::string decalMember;
		for(const auto& kv : m.members)
			if(kv.second.kind == MemberKind::DecalTap)
			{
				if(!decalMember.empty())
					return Refuse(res, Refusal::BadValue, "a second decal_tap member (" + decalMember + " is the set in force at the span's first frame)",
					              kv.first);
				if(!decalsOn)
					return Refuse(res, Refusal::BadValue, "a decal_tap member while render_config's render_decals is not 1", kv.first);
				decalMember = kv.first;
			}
		bool noSetNote = false;
		for(const std::string& n : m.versionNotes)
			noSetNote = noSetNote || StartsWith(n, kDecalNoSetNote);
		if(decalsOn && decalMember.empty() && !noSetNote)
			return Refuse(res, Refusal::MissingMember, "render_decals is 1 and the bundle has no decal_tap member and no note \"" +
			                                               std::string(kDecalNoSetNote) + "\"", "decals/load<NNNN>.dtap");
	}
	// format version 9, ticket 4875: the model tap members against render_cities / render_landmarks (the block at
	// ModelTapFile): each NNNNNN below the frame count; on with no member needs the recorder's note; off with one is refused
	{
		auto on = [&](const char* key) {
			auto it = m.renderConfig.find(key);
			return it != m.renderConfig.end() && it->second == "1";
		};
		const bool modelsOn = on("render_cities") || on("render_landmarks");
		bool any = false;
		for(const auto& kv : m.members)
			if(kv.second.kind == MemberKind::ModelTap)
			{
				if(!modelsOn)
					return Refuse(res, Refusal::BadValue, "a model_tap member while render_config's render_cities and render_landmarks "
					                                      "are not 1", kv.first);
				uint32_t idx = 0;
				ModelTapIndex(kv.first, &idx);
				if(idx >= m.frameCount)
					return Refuse(res, Refusal::BadRecord, Fmt("frame index %u, the bundle has %u frames", idx, m.frameCount), kv.first);
				any = true;
			}
		bool note = false;
		for(const std::string& n : m.versionNotes)
			note = note || StartsWith(n, kModelNoFrameNote);
		if(modelsOn && !any && !note)
			return Refuse(res, Refusal::MissingMember, "render_cities or render_landmarks is 1 and the bundle has no model_tap member "
			                                           "and no note \"" + std::string(kModelNoFrameNote) + "\"",
			              "modeltap/frame<NNNNNN>.mtap");
		// ticket 4888: the CPU-built meshes' members (optional; the block at ModelGeomFile): city art, so render_cities "1";
		// each NNNNNN below the frame count
		for(const auto& kv : m.members)
			if(kv.second.kind == MemberKind::ModelGeom)
			{
				if(!on("render_cities"))
					return Refuse(res, Refusal::BadValue, "a model_geom member while render_config's render_cities is not 1 (CPU-built "
					                                      "meshes are city art)", kv.first);
				uint32_t idx = 0;
				ModelGeomIndex(kv.first, &idx);
				if(idx >= m.frameCount)
					return Refuse(res, Refusal::BadRecord, Fmt("frame index %u, the bundle has %u frames", idx, m.frameCount), kv.first);
			}
	}
	// format version 5, issue 0440: the unit scene's database and history against render_units (the block at UnitsHistoryRecord)
	{
		auto ru = m.renderConfig.find("render_units");
		const bool unitsOn = ru != m.renderConfig.end() && ru->second == "1";
		std::string dbMember, histMember;
		for(const auto& kv : m.members)
		{
			if(kv.second.kind != MemberKind::UnitsDb && kv.second.kind != MemberKind::UnitsHistory)
				continue;
			if(!unitsOn)
				return Refuse(res, Refusal::BadValue, std::string("a ") + MemberKindName(kv.second.kind) + " member while render_config's render_units is not 1",
				              kv.first);
			if(kv.second.kind == MemberKind::UnitsDb)
			{
				if(!dbMember.empty())
					return Refuse(res, Refusal::BadValue, "a second units_db member (" + dbMember + " is the database in force at the span)", kv.first);
				dbMember = kv.first;
			}
			else
				histMember = kv.first;
		}
		bool noDbNote = false, noHistNote = false;
		for(const std::string& n : m.versionNotes)
		{
			noDbNote = noDbNote || StartsWith(n, kUnitsNoDbNote);
			noHistNote = noHistNote || StartsWith(n, kUnitsNoHistoryNote);
		}
		if(unitsOn && dbMember.empty() && !noDbNote)
			return Refuse(res, Refusal::MissingMember, "render_units is 1 and the bundle has no units_db member and no note \"" +
			                                               std::string(kUnitsNoDbNote) + "...\"", "units/gen<NNNNNNNNNN>.db");
		if(unitsOn && histMember.empty() && !noHistNote)
			return Refuse(res, Refusal::MissingMember, "render_units is 1 and the bundle has no units_history member and no note \"" +
			                                               std::string(kUnitsNoHistoryNote) + "...\"", "units_history.bin");
		if(!dbMember.empty() && !histMember.empty() && dbMember != UnitsDbPath(history.dbGeneration))
			return Refuse(res, Refusal::BadValue, Fmt("the history's database generation is %u, the units_db member is ", history.dbGeneration) + dbMember,
			              "units_history.bin");
		if(!histMember.empty() && !frames.empty() && history.records.back().loopIter > frames[0].loopIter)
			return Refuse(res, Refusal::BadRecord, Fmt("the last history record's loop iteration %llu is after the span's first (%llu)",
			                                           (unsigned long long)history.records.back().loopIter, (unsigned long long)frames[0].loopIter),
			              "units_history.bin");
	}
	// format version 6, ticket 0403: particles.bin against render_fow_particles (the block at FogParticlesFile)
	{
		auto rp = m.renderConfig.find("render_fow_particles");
		if(rp != m.renderConfig.end() && rp->second != "0" && rp->second != "1")
		{
			if(res)
				res->member.clear();   // scene.json's own value (not the last member read)
			return Refuse(res, Refusal::BadValue, "render_config's render_fow_particles is '" + rp->second + "', not 0 or 1");
		}
		const bool partOn = rp != m.renderConfig.end() && rp->second == "1";
		bool member = false;
		for(const auto& kv : m.members)
			if(kv.second.kind == MemberKind::FogParticles)
			{
				if(!partOn)
					return Refuse(res, Refusal::BadValue, "a fog_particles member while render_config's render_fow_particles is not 1", kv.first);
				member = true;
			}
		bool note = false;
		for(const std::string& n : m.versionNotes)
			note = note || StartsWith(n, kFogParticlesNote);
		if(partOn && !member && !note)
			return Refuse(res, Refusal::MissingMember, "render_fow_particles is 1 and the bundle has no fog_particles member and no note \"" +
			                                               std::string(kFogParticlesNote) + "...\"", "particles.bin");
	}
	// format version 8, ticket 0495: forest.bin against render_forest, near.bin against render_bake_near (the block at
	// ForestStateFile)
	{
		struct Pair
		{
			const char* key;
			MemberKind kind;
			const char* path;
			const char* note;
			bool needsBake;
		};
		const Pair pairs[2] = {{"render_forest", MemberKind::ForestState, "forest.bin", kForestNote, false},
		                       {"render_bake_near", MemberKind::NearPairs, "near.bin", kNearNote, true}};
		auto rt = m.renderConfig.find("render_tiles");
		const bool bake = rt == m.renderConfig.end() || rt->second == "bake";
		for(const Pair& pr : pairs)
		{
			auto rp = m.renderConfig.find(pr.key);
			if(rp != m.renderConfig.end() && rp->second != "0" && rp->second != "1")
			{
				if(res)
					res->member.clear();
				return Refuse(res, Refusal::BadValue, std::string("render_config's ") + pr.key + " is '" + rp->second + "', not 0 or 1");
			}
			const bool on = rp != m.renderConfig.end() && rp->second == "1";
			bool member = false;
			for(const auto& kv : m.members)
				if(kv.second.kind == pr.kind)
				{
					if(!on)
						return Refuse(res, Refusal::BadValue, std::string("a ") + MemberKindName(pr.kind) + " member while render_config's " + pr.key +
						                                          " is not 1", kv.first);
					member = true;
				}
			bool note = false;
			for(const std::string& n : m.versionNotes)
				note = note || StartsWith(n, pr.note);
			if(on && (bake || !pr.needsBake) && !member && !note)
				return Refuse(res, Refusal::MissingMember, std::string(pr.key) + " is 1 and the bundle has no " + MemberKindName(pr.kind) +
				                                               " member and no note \"" + pr.note + "...\"", pr.path);
		}
	}
	// ticket 4852 (epic 0528, format version 8 unchanged): the farm producer's input lives in render_config, not a member:
	// render_farmfields "0" or "1"; with "1", render_farmfields_state (the producer's span text, form "fv1 ...") or a note
	// starting kFarmNote; render_farmfields_state while render_farmfields is not "1" is refused. A bundle without the keys
	// (every bundle before 4852) draws the tap's farm fields as they are.
	{
		auto rf = m.renderConfig.find("render_farmfields");
		if(rf != m.renderConfig.end() && rf->second != "0" && rf->second != "1")
		{
			if(res)
				res->member.clear();
			return Refuse(res, Refusal::BadValue, "render_config's render_farmfields is '" + rf->second + "', not 0 or 1");
		}
		const bool on = rf != m.renderConfig.end() && rf->second == "1";
		auto rs = m.renderConfig.find("render_farmfields_state");
		if(rs != m.renderConfig.end())
		{
			if(res)
				res->member.clear();
			if(!on)
				return Refuse(res, Refusal::BadValue, "render_config's render_farmfields_state while render_farmfields is not 1");
			if(!StartsWith(rs->second, kFarmStateForm))
				return Refuse(res, Refusal::BadValue, std::string("render_config's render_farmfields_state does not start with \"") +
				                                          kFarmStateForm + "\"");
		}
		bool note = false;
		for(const std::string& n : m.versionNotes)
			note = note || StartsWith(n, kFarmNote);
		if(on && rs == m.renderConfig.end() && !note)
		{
			if(res)
				res->member.clear();
			return Refuse(res, Refusal::MissingMember, std::string("render_farmfields is 1 and render_config has no render_farmfields_state "
			                                                       "and the bundle no note \"") +
			                                               kFarmNote + "...\"");
		}
	}
	// ticket 4934 (epic 0454, format version unchanged, as 4852's): the road producer's input, the same rules with
	// render_roadlayout, render_roadlayout_state ("rl1 ...") and kRoadNote; a bundle without the keys draws the tap's roads
	{
		auto rf = m.renderConfig.find("render_roadlayout");
		if(rf != m.renderConfig.end() && rf->second != "0" && rf->second != "1" && rf->second != "2")
		{
			if(res)
				res->member.clear();
			return Refuse(res, Refusal::BadValue, "render_config's render_roadlayout is '" + rf->second + "', not 0, 1 or 2");
		}
		const bool on = rf != m.renderConfig.end() && (rf->second == "1" || rf->second == "2");
		// ticket 4940: the all-connections layout's shape, render_roadlayout_junction "0".."3" and render_roadlayout_jitter
		// "0".."7", only with render_roadlayout "2" (each may be missing: 0)
		const bool all = rf != m.renderConfig.end() && rf->second == "2";
		const struct
		{
			const char* key;
			char hi;
		} shape[] = {{"render_roadlayout_junction", '3'}, {"render_roadlayout_jitter", '7'}};
		for(const auto& s : shape)
		{
			auto sf = m.renderConfig.find(s.key);
			if(sf == m.renderConfig.end())
				continue;
			if(res)
				res->member.clear();
			if(!all)
				return Refuse(res, Refusal::BadValue, std::string("render_config's ") + s.key + " while render_roadlayout is not 2");
			if(sf->second.size() != 1 || sf->second[0] < '0' || sf->second[0] > s.hi)
				return Refuse(res, Refusal::BadValue, std::string("render_config's ") + s.key + " is '" + sf->second + "', not 0.." + s.hi);
		}
		auto rs = m.renderConfig.find("render_roadlayout_state");
		if(rs != m.renderConfig.end())
		{
			if(res)
				res->member.clear();
			if(!on)
				return Refuse(res, Refusal::BadValue, "render_config's render_roadlayout_state while render_roadlayout is not 1 or 2");
			if(!StartsWith(rs->second, kRoadStateForm))
				return Refuse(res, Refusal::BadValue, std::string("render_config's render_roadlayout_state does not start with \"") +
				                                          kRoadStateForm + "\"");
		}
		bool note = false;
		for(const std::string& n : m.versionNotes)
			note = note || StartsWith(n, kRoadNote);
		if(on && rs == m.renderConfig.end() && !note)
		{
			if(res)
				res->member.clear();
			return Refuse(res, Refusal::MissingMember, std::string("render_roadlayout is 1 or 2 and render_config has no render_roadlayout_state "
			                                                       "and the bundle no note \"") +
			                                               kRoadNote + "...\"");
		}
	}
	// epic 0465, ticket 4907 (format version unchanged, as 4852's): render_live_terrain and its journal (srv_liveterrain.h)
	{
		std::string lw;
		bool lmissing = false;
		if(!liveterrain::CheckLiveTerrainConfig(m.renderConfig, m.versionNotes, lw, lmissing))
		{
			if(res)
				res->member.clear();
			return Refuse(res, lmissing ? Refusal::MissingMember : Refusal::BadValue, lw);
		}
	}
	for(const auto& d : bmpSize)
		if(std::find(m.dumpedFrames.begin(), m.dumpedFrames.end(), d.first) == m.dumpedFrames.end())
			return Refuse(res, Refusal::BadRecord, "a colour dump of a frame frames.dumped does not list",
			              Fmt("dumps/%lld.bmp", (long long)d.first));
	for(const auto& d : depthBytes)
		if(std::find(m.dumpedFrames.begin(), m.dumpedFrames.end(), d.first) == m.dumpedFrames.end())
			return Refuse(res, Refusal::BadRecord, "a depth dump of a frame frames.dumped does not list",
			              Fmt("dumps/%lld.depth.f32", (long long)d.first));
	*res = CheckResult();
	return c5r::Status::Ok();
}

// ==== the writer ========================================================================================================
struct Writer::Impl
{
	struct Stream
	{
		FILE* f = nullptr;
		Sha256 h;
		uint64_t bytes = 0;
		MemberKind kind = MemberKind::Feed;
	};
	std::wstring dir, partial;
	bool opened = false, finished = false, incomplete = false;
	std::string why;
	uint64_t failAfter = 0, written = 0;
	std::map<std::string, Member> members;
	std::map<std::string, Stream*> streams;
	~Impl()
	{
		for(auto& s : streams)
		{
			if(s.second->f)
				fclose(s.second->f);
			delete s.second;
		}
	}
	void fail(const std::string& w)
	{
		if(!incomplete)
			why = w;
		incomplete = true;
	}
	// writes n bytes through the seam; false (and incomplete) on a failure
	bool put(FILE* f, const std::string& path, const void* p, size_t n, Sha256* h)
	{
		size_t ok = n;
		bool seam = false;
		if(failAfter && written + n > failAfter)
		{
			ok = (size_t)(failAfter > written ? failAfter - written : 0);
			seam = true;
		}
		if(ok && fwrite(p, 1, ok, f) != ok)
		{
			fail(path + ": write failed");
			return false;
		}
		written += ok;
		if(h)
			h->update(p, ok);
		if(seam)
		{
			fail(Fmt("%s: write failed at byte %llu of the bundle (failAfterBytes test seam)", path.c_str(),
			         (unsigned long long)failAfter));
			return false;
		}
		return true;
	}
	c5r::Status check(const std::string& path, MemberKind kind)
	{
		if(!opened || finished)
			return c5r::Status::fail(c5r::Err::Io, "the writer is not open");
		if(incomplete)
			return c5r::Status::fail(c5r::Err::Io, "the bundle is incomplete: " + why);
		std::string w;
		if(!CleanPath(path, &w) || !PathFitsKind(path, kind, &w))
			return c5r::Status::fail(c5r::Err::BadLayout, w);
		if(members.count(path) || streams.count(path))
			return c5r::Status::fail(c5r::Err::BadLayout, "\"" + path + "\" written twice");
		if(!MakeDirs(partial, path))
		{
			fail(path + ": cannot create its folder");
			return c5r::Status::fail(c5r::Err::Io, why);
		}
		return c5r::Status::Ok();
	}
};

Writer::Writer() : m_impl(new Impl) {}
Writer::~Writer()
{
	delete m_impl;
}

c5r::Status Writer::open(const std::wstring& bundleDir)
{
	Impl& I = *m_impl;
	if(I.opened)
		return c5r::Status::fail(c5r::Err::Io, "already open");
	if(bundleDir.empty())
		return c5r::Status::fail(c5r::Err::Io, "no folder");
	if(IsDir(bundleDir))
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileExW((bundleDir + L"/*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
		bool empty = true;
		if(h != INVALID_HANDLE_VALUE)
		{
			do
				if(wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
					empty = false;
			while(empty && FindNextFileW(h, &fd));
			FindClose(h);
		}
		if(!empty)
			return c5r::Status::fail(c5r::Err::Io, "the bundle folder is not empty");
	}
	else if(IsFile(bundleDir))
		return c5r::Status::fail(c5r::Err::Io, "the bundle path is a file");
	else if(!CreateDirectoryW(bundleDir.c_str(), NULL))
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot create the bundle folder (error %lu)", GetLastError()));
	I.dir = bundleDir;
	I.partial = Join(bundleDir, ".partial");
	if(!CreateDirectoryW(I.partial.c_str(), NULL))
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot create .partial (error %lu)", GetLastError()));
	I.opened = true;
	return c5r::Status::Ok();
}

c5r::Status Writer::addMember(const std::string& path, MemberKind kind, const void* data, size_t bytes)
{
	Impl& I = *m_impl;
	C5R_TRY(I.check(path, kind));
	FILE* f = nullptr;
	if(_wfopen_s(&f, Join(I.partial, path).c_str(), L"wb") || !f)
	{
		I.fail(path + ": cannot create");
		return c5r::Status::fail(c5r::Err::Io, I.why);
	}
	Sha256 h;
	if(!h.begin())
	{
		fclose(f);
		I.fail("SHA-256 is not available (bcrypt.dll)");
		return c5r::Status::fail(c5r::Err::Unsupported, I.why);
	}
	bool ok = I.put(f, path, data, bytes, &h);
	if(fclose(f) != 0 && ok)
	{
		I.fail(path + ": close failed");
		ok = false;
	}
	if(!ok)
		return c5r::Status::fail(c5r::Err::Io, I.why);
	Member m;
	m.kind = kind;
	m.version = MemberKindVersion(kind);
	m.bytes = bytes;
	m.sha256 = h.finishHex();
	I.members[path] = m;
	return c5r::Status::Ok();
}

c5r::Status Writer::appendStart(const std::string& path, MemberKind kind)
{
	Impl& I = *m_impl;
	if(kind != MemberKind::Feed && kind != MemberKind::Frames && kind != MemberKind::CameraBlocks && kind != MemberKind::Shadow &&   // Shadow: 4308
	   kind != MemberKind::WaterFrames)   // ticket 0622
		return c5r::Status::fail(c5r::Err::BadLayout, std::string("not a stream member kind: ") + MemberKindName(kind));
	C5R_TRY(I.check(path, kind));
	Impl::Stream* s = new Impl::Stream;
	s->kind = kind;
	I.streams[path] = s;
	if(_wfopen_s(&s->f, Join(I.partial, path).c_str(), L"wb") || !s->f)
	{
		s->f = nullptr;
		I.fail(path + ": cannot create");
		return c5r::Status::fail(c5r::Err::Io, I.why);
	}
	if(!s->h.begin())
	{
		I.fail("SHA-256 is not available (bcrypt.dll)");
		return c5r::Status::fail(c5r::Err::Unsupported, I.why);
	}
	std::vector<uint8_t> head;
	EncodeStreamHead(kind, &head);
	if(!I.put(s->f, path, head.data(), head.size(), &s->h))
		return c5r::Status::fail(c5r::Err::Io, I.why);
	s->bytes = head.size();
	return c5r::Status::Ok();
}

c5r::Status Writer::append(const std::string& path, const void* data, size_t bytes)
{
	Impl& I = *m_impl;
	auto it = I.streams.find(path);
	if(!I.opened || I.finished || it == I.streams.end() || !it->second->f)
		return c5r::Status::fail(c5r::Err::Io, "\"" + path + "\" is not an open stream member");
	if(I.incomplete)
		return c5r::Status::fail(c5r::Err::Io, "the bundle is incomplete: " + I.why);
	if(!I.put(it->second->f, path, data, bytes, &it->second->h))
		return c5r::Status::fail(c5r::Err::Io, I.why);
	it->second->bytes += bytes;
	return c5r::Status::Ok();
}

void Writer::markIncomplete(const std::string& why)
{
	m_impl->fail(why);
}

bool Writer::incomplete() const
{
	return m_impl->incomplete;
}

void Writer::failAfterBytes(uint64_t n)
{
	m_impl->failAfter = n;
}

c5r::Status Writer::finish(Manifest& m)
{
	Impl& I = *m_impl;
	if(!I.opened || I.finished)
		return c5r::Status::fail(c5r::Err::Io, "the writer is not open");
	I.finished = true;
	for(auto& kv : I.streams)
	{
		Impl::Stream* s = kv.second;
		if(s->f && fclose(s->f) != 0)
			I.fail(kv.first + ": close failed");
		s->f = nullptr;
		if(!I.incomplete)
		{
			Member mem;
			mem.kind = s->kind;
			mem.version = MemberKindVersion(s->kind);
			mem.bytes = s->bytes;
			mem.sha256 = s->h.finishHex();
			I.members[kv.first] = mem;
		}
	}
	if(!I.incomplete)
	{
		// move every top-level entry of .partial into the bundle folder
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileExW((I.partial + L"/*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
		if(h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
					continue;
				std::wstring from = I.partial + L"/" + fd.cFileName, to = I.dir + L"/" + fd.cFileName;
				if(!MoveFileExW(from.c_str(), to.c_str(), 0))
				{
					I.fail(Fmt("cannot move %s into place (error %lu)", Narrow(fd.cFileName).c_str(), GetLastError()));
					break;
				}
			} while(FindNextFileW(h, &fd));
			FindClose(h);
		}
		if(!I.incomplete && !RemoveDirectoryW(I.partial.c_str()))
			I.fail(Fmt("cannot remove .partial (error %lu)", GetLastError()));
	}
	m.format = kFormat;
	m.formatVersion = kFormatVersion;
	if(I.incomplete)
	{
		m.members.clear();
		m.versionNotes.push_back("incomplete: " + I.why);
	}
	else
		m.members = I.members;
	for(const char* n : kCounterNames)
		if(!m.counters.count(n))
			m.counters[n] = 0;
	std::string js = ManifestToJson(m);
	std::wstring tmp = Join(I.dir, "scene.json.tmp"), fin = Join(I.dir, "scene.json");
	FILE* f = nullptr;
	bool ok = !_wfopen_s(&f, tmp.c_str(), L"wb") && f;
	if(ok)
	{
		ok = fwrite(js.data(), 1, js.size(), f) == js.size();
		ok = fclose(f) == 0 && ok;
	}
	ok = ok && MoveFileExW(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING);
	if(!ok)
		return c5r::Status::fail(c5r::Err::Io, "cannot write scene.json" + (I.incomplete ? " (incomplete: " + I.why + ")" :
		                                                                                    std::string()));
	if(I.incomplete)
		return c5r::Status::fail(c5r::Err::Io, "bundle incomplete: " + I.why);
	return c5r::Status::Ok();
}

}  // namespace scenebundle
}  // namespace srvrender
