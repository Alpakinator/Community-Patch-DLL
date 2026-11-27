// vp_luaraise.h - one GameEvents raise in one IPC crossing: kRtE_LuaRaise (ticket 4804, epic 0504, speedup P2).
// Shared by the x64 server (srv_remote_lua.cpp: Encode) and the x86 proxy (proxy_lua.cpp: Decode and Serve), and built
// alone by the offline tests (server/tests/luaraise/). C++03 language (the proxy builds against the VC9 headers);
// needs only vpipc/marshal.h, <string> and <vector>. Design: research/08-performance/speedup-p2-gameevents.md.
//
// Today a raise with n arguments makes n + 3 crossings: CreateArgs, n x Push, the Call*, DestroyArgs, each a generated
// call on the EXE's ICvEngineScriptSystem1 / ICvEngineScriptSystemArgs1. With kRtE_LuaRaise the server keeps the
// arguments (LocalArgs) and sends them with the call; the proxy, on the EXE thread that waits in the raise, makes the
// same EXE calls in the same order (Serve below): CreateArgs, the pushes, the Call*, DestroyArgs.
//
// CALL (server -> proxy):
//   u32 kind (Kind), u32 flags (kFlag*), u32 reserve (CreateArgs(reserve) when kFlagReserve), str name (not null),
//   u32 value (the in/out value: bool 0/1, the int, or the float's bits), u32 n (<= kMaxArgs),
//   n x (u32 tag (Tag), then: null nothing | bool/int/float u32 bits | string: str (null kept) and u32 size)
// RETURN: u32 handled (the Call*'s return), u32 value (as in the CALL).
// A string travels as the generated Push(const char*, size_t) did: the C string (strlen, null kept) and the size
// narrowed to 32 bits, passed back unchanged.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "vpipc/marshal.h"

namespace vplr
{
// The Lua bridge's raise protocol version: the proxy sends it with kRt_LuaRegister; the server refuses another.
const uint32_t kVersion = 1;

enum Kind
{
	kHook = 0,         // ICvEngineScriptSystem1::CallHook(name, args, bool&)
	kTestAll = 1,      // CallTestAll(name, args, bool&)
	kTestAny = 2,      // CallTestAny(name, args, bool&)
	kAccumInt = 3,     // CallAccumulator(name, args, int&)
	kAccumFloat = 4,   // CallAccumulator(name, args, float&)
	kKindCount = 5
};
enum Tag
{
	kNull = 0,
	kBool = 1,
	kInt = 2,
	kFloat = 3,
	kString = 4,
	kTagCount = 5
};
enum Flag
{
	kFlagArgs = 1u,     // an args object exists (else the Call* gets NULL)
	kFlagReserve = 2u,  // it was made with CreateArgs(reserve)
	kFlagMask = 3u
};
const uint32_t kMaxArgs = 256;
const uint32_t kMaxBytes = 4u * 1024u * 1024u;  // the whole CALL (vp_wire.h kLuaMaxBytes)

inline uint32_t FloatBits(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}
inline float BitsFloat(uint32_t u)
{
	float f;
	memcpy(&f, &u, 4);
	return f;
}

inline const char* KindName(uint32_t k)
{
	static const char* const s[kKindCount] = {"hook", "testall", "testany", "accum_int", "accum_float"};
	return k < (uint32_t)kKindCount ? s[k] : "?";
}

// ---- the server's side ---------------------------------------------------------------------------------------------
// One pushed argument as the server keeps it.
struct Arg
{
	uint32_t tag;    // Tag
	uint32_t bits;   // bool 0/1, int, float bits; string: the size the DLL passed (narrowed to 32 bits)
	bool null;       // string: the DLL passed a null pointer
	std::string s;   // string: the C string (up to its first NUL, as the generated path sent it)
};

// The size of a str of `len` bytes on the wire (u32 length, bytes, NUL, padding to 4).
inline uint64_t StrBytes(uint64_t len)
{
	return 4u + ((len + 1u + 3u) & ~(uint64_t)3u);
}

// The size of the CALL Encode writes.
inline uint64_t CallBytes(const char* name, const Arg* a, uint32_t n)
{
	uint64_t b = 4u * 3u + StrBytes(name ? strlen(name) : 0) + 4u + 4u;
	for (uint32_t i = 0; i < n; ++i)
	{
		b += 4u;
		if (a[i].tag == kString)
			b += (a[i].null ? 4u : StrBytes(a[i].s.size())) + 4u;
		else if (a[i].tag != kNull)
			b += 4u;
	}
	return b;
}

// Writes the CALL. The caller checks CallBytes <= kMaxBytes and n <= kMaxArgs first (the server refuses loudly).
inline void Encode(vpipc::Writer& w, uint32_t kind, uint32_t flags, uint32_t reserve, const char* name, uint32_t value,
                   const Arg* a, uint32_t n)
{
	w.u32(kind);
	w.u32(flags);
	w.u32(reserve);
	w.str(name ? name : "");
	w.u32(value);
	w.u32(n);
	for (uint32_t i = 0; i < n; ++i)
	{
		w.u32(a[i].tag);
		switch (a[i].tag)
		{
		case kNull: break;
		case kString:
			if (a[i].null)
				w.str(NULL);
			else
				w.str(a[i].s.c_str(), (uint32_t)a[i].s.size());
			w.u32(a[i].bits);
			break;
		default: w.u32(a[i].bits); break;
		}
	}
}

// ---- the proxy's side ----------------------------------------------------------------------------------------------
struct DArg
{
	uint32_t tag;
	uint32_t bits;
	const char* s;  // string: points into the reader's buffer (NULL for a null pointer)
};
struct Call
{
	uint32_t kind, flags, reserve, value;
	const char* name;  // points into the reader's buffer
	std::vector<DArg> args;
};

// Reads a whole CALL. false with `why` set on anything malformed: a short payload, a null name, an unknown kind, flag
// or tag, n above kMaxArgs, bytes left over. The strings point into r's buffer: keep it alive while `c` is used.
inline bool Decode(vpipc::Reader& r, Call& c, const char*& why)
{
	why = NULL;
	c.args.clear();
	c.kind = r.u32();
	c.flags = r.u32();
	c.reserve = r.u32();
	c.name = r.cstr();
	c.value = r.u32();
	const uint32_t n = r.u32();
	if (!r.ok()) { why = "short CALL"; return false; }
	if (!c.name) { why = "null event name"; return false; }
	if (c.kind >= (uint32_t)kKindCount) { why = "unknown call kind"; return false; }
	if (c.flags & ~(uint32_t)kFlagMask) { why = "unknown flag bits"; return false; }
	if (n > kMaxArgs) { why = "more than 256 arguments"; return false; }
	if (n && !(c.flags & kFlagArgs)) { why = "arguments without an args object"; return false; }
	c.args.reserve(n);
	for (uint32_t i = 0; i < n; ++i)
	{
		DArg d;
		d.tag = r.u32();
		d.bits = 0;
		d.s = NULL;
		if (!r.ok()) { why = "short CALL"; return false; }
		switch (d.tag)
		{
		case kNull: break;
		case kBool:
		case kInt:
		case kFloat: d.bits = r.u32(); break;
		case kString:
			d.s = r.cstr();
			d.bits = r.u32();
			break;
		default: why = "unknown argument tag"; return false;
		}
		if (!r.ok()) { why = "short CALL"; return false; }
		c.args.push_back(d);
	}
	if (r.remaining()) { why = "bytes left over"; return false; }
	return true;
}

enum ServeStatus
{
	kServed = 0,
	kNoScriptSystem = 1,  // the engine's script system is not known (no Call* made)
	kCreateFailed = 2     // CreateArgs returned NULL (no Call* made)
};

// The EXE's side of one raise, in today's order on the calling thread: CreateArgs (or CreateArgs(reserve); none
// without kFlagArgs), each Push / PushNULL, the Call*, DestroyArgs. SS and A are ICvEngineScriptSystem1 and
// ICvEngineScriptSystemArgs1 in the proxy, fakes in the tests. `handled` and `value` are the RETURN.
template <class SS, class A>
inline int Serve(SS* ss, const Call& c, bool& handled, uint32_t& value)
{
	handled = false;
	value = c.value;
	if (!ss) return kNoScriptSystem;
	A* a = NULL;
	if (c.flags & kFlagArgs)
	{
		a = (c.flags & kFlagReserve) ? ss->CreateArgs(c.reserve) : ss->CreateArgs();
		if (!a) return kCreateFailed;
		for (size_t i = 0; i < c.args.size(); ++i)
		{
			const DArg& d = c.args[i];
			switch (d.tag)
			{
			case kBool: a->Push(d.bits != 0); break;
			case kInt: a->Push((int)d.bits); break;
			case kFloat: a->Push(BitsFloat(d.bits)); break;
			case kString: a->Push(d.s, (size_t)d.bits); break;
			default: a->PushNULL(); break;
			}
		}
	}
	switch (c.kind)
	{
	case kHook:
	{
		bool b = c.value != 0;
		handled = ss->CallHook(c.name, a, b);
		value = b ? 1u : 0u;
		break;
	}
	case kTestAll:
	{
		bool b = c.value != 0;
		handled = ss->CallTestAll(c.name, a, b);
		value = b ? 1u : 0u;
		break;
	}
	case kTestAny:
	{
		bool b = c.value != 0;
		handled = ss->CallTestAny(c.name, a, b);
		value = b ? 1u : 0u;
		break;
	}
	case kAccumInt:
	{
		int v = (int)c.value;
		handled = ss->CallAccumulator(c.name, a, v);
		value = (uint32_t)v;
		break;
	}
	default:
	{
		float f = BitsFloat(c.value);
		handled = ss->CallAccumulator(c.name, a, f);
		value = FloatBits(f);
		break;
	}
	}
	if (a) ss->DestroyArgs(a);
	return kServed;
}
}  // namespace vplr
