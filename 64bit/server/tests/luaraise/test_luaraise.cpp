// test_luaraise.cpp - offline tests of proxy/include/vp_luaraise.h (ticket 4804, speedup P2): kRtE_LuaRaise's codec and
// the proxy's EXE-side sequence, driven with a fake script system. No game, no DLL. Built x64 and x86 and under MSVC
// /fsanitize=address by server/tests/luaraise/run_luaraise_test.py. Exit 0 when every check passes, 1 otherwise.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vp_luaraise.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                                                                        \
	do                                                                                                                  \
	{                                                                                                                   \
		if (c)                                                                                                          \
			++g_pass;                                                                                                   \
		else                                                                                                            \
		{                                                                                                               \
			++g_fail;                                                                                                   \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                         \
		}                                                                                                               \
	} while (0)

// ---- a fake EXE script system: records every call in order ----------------------------------------------------------
std::vector<std::string> g_events;
void Ev(const std::string& s) { g_events.push_back(s); }
std::string Fmt(const char* f, ...)
{
	char b[512];
	va_list ap;
	va_start(ap, f);
	vsnprintf(b, sizeof(b), f, ap);
	va_end(ap);
	return b;
}

struct FakeArgs
{
	int id;
	unsigned int n;
	unsigned int Push(bool b) { Ev(Fmt("args%d.Push(bool %d)", id, b ? 1 : 0)); return n++; }
	unsigned int Push(const char* s, size_t len)
	{
		Ev(Fmt("args%d.Push(str %s, %u)", id, s ? s : "(null)", (unsigned)len));
		return n++;
	}
	unsigned int Push(int i) { Ev(Fmt("args%d.Push(int %d)", id, i)); return n++; }
	unsigned int Push(float f)
	{
		uint32_t u;
		memcpy(&u, &f, 4);
		Ev(Fmt("args%d.Push(float %08x)", id, u));
		return n++;
	}
	unsigned int PushNULL() { Ev(Fmt("args%d.PushNULL()", id)); return n++; }
};

struct FakeSS
{
	int next;
	bool createFails;
	std::vector<std::string> listeners;  // names with a listener: handled = true
	FakeArgs* live;
	FakeSS() : next(1), createFails(false), live(NULL) {}
	bool Has(const char* name) const
	{
		for (size_t i = 0; i < listeners.size(); ++i)
			if (listeners[i] == name) return true;
		return false;
	}
	FakeArgs* Make(const std::string& how)
	{
		Ev(how);
		if (createFails) return NULL;
		FakeArgs* a = new FakeArgs();
		a->id = next++;
		a->n = 0;
		live = a;
		return a;
	}
	FakeArgs* CreateArgs() { return Make("CreateArgs()"); }
	FakeArgs* CreateArgs(unsigned int r) { return Make(Fmt("CreateArgs(%u)", r)); }
	void DestroyArgs(FakeArgs* a)
	{
		Ev(Fmt("DestroyArgs(args%d)", a ? a->id : 0));
		if (a == live) live = NULL;
		delete a;
	}
	std::string A(FakeArgs* a) { return a ? Fmt("args%d", a->id) : std::string("NULL"); }
	// engine-contract 10.1: CallHook's value always false; the tests' values: testall/any flip, accumulators add 7 / 0.5
	bool CallHook(const char* nm, FakeArgs* a, bool& v)
	{
		Ev(Fmt("CallHook(%s, %s, %d)", nm, A(a).c_str(), v ? 1 : 0));
		v = false;
		return Has(nm);
	}
	bool CallTestAll(const char* nm, FakeArgs* a, bool& v)
	{
		Ev(Fmt("CallTestAll(%s, %s, %d)", nm, A(a).c_str(), v ? 1 : 0));
		if (Has(nm)) v = !v;
		return Has(nm);
	}
	bool CallTestAny(const char* nm, FakeArgs* a, bool& v)
	{
		Ev(Fmt("CallTestAny(%s, %s, %d)", nm, A(a).c_str(), v ? 1 : 0));
		if (Has(nm)) v = !v;
		return Has(nm);
	}
	bool CallAccumulator(const char* nm, FakeArgs* a, int& v)
	{
		Ev(Fmt("CallAccumulator(%s, %s, int %d)", nm, A(a).c_str(), v));
		if (Has(nm)) v += 7;
		return Has(nm);
	}
	bool CallAccumulator(const char* nm, FakeArgs* a, float& v)
	{
		Ev(Fmt("CallAccumulator(%s, %s, float %g)", nm, A(a).c_str(), (double)v));
		if (Has(nm)) v += 0.5f;
		return Has(nm);
	}
};

vplr::Arg MakeArg(uint32_t tag, uint32_t bits)
{
	vplr::Arg a;
	a.tag = tag;
	a.bits = bits;
	a.null = false;
	return a;
}
vplr::Arg Str(const char* s, uint32_t size)
{
	vplr::Arg a;
	a.tag = vplr::kString;
	a.bits = size;
	a.null = s == NULL;
	if (s) a.s = s;
	return a;
}

// Encodes into a heap buffer of exactly the encoded size (so ASan sees any read past it).
std::vector<uint8_t> EncodeCall(uint32_t kind, uint32_t flags, uint32_t reserve, const char* name, uint32_t value,
                                const std::vector<vplr::Arg>& v)
{
	vpipc::Writer w;
	std::vector<uint8_t> scratch(64);
	w.reset(&scratch[0], (uint32_t)scratch.size(), true);
	vplr::Encode(w, kind, flags, reserve, name, value, v.empty() ? NULL : &v[0], (uint32_t)v.size());
	return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

struct Served
{
	bool decoded;
	const char* why;
	int status;
	bool handled;
	uint32_t value;
};

// Decodes from an exact-size heap copy and serves.
Served DecodeServe(FakeSS* ss, const std::vector<uint8_t>& bytes)
{
	Served s = {false, NULL, -1, false, 0};
	uint8_t* p = static_cast<uint8_t*>(malloc(bytes.empty() ? 1 : bytes.size()));
	if (!bytes.empty()) memcpy(p, &bytes[0], bytes.size());
	vpipc::Reader r(p, (uint32_t)bytes.size());
	vplr::Call c;
	s.decoded = vplr::Decode(r, c, s.why);
	if (s.decoded) s.status = vplr::Serve<FakeSS, FakeArgs>(ss, c, s.handled, s.value);
	free(p);
	return s;
}

// ---- tests ----------------------------------------------------------------------------------------------------------
// Every kind, mixed arguments: the fake EXE sees CreateArgs, the pushes in order with the same values, the call with the
// name and initial value, DestroyArgs; the reply carries handled and the value.
void TestOrderAndValues()
{
	std::vector<vplr::Arg> v;
	v.push_back(MakeArg(vplr::kInt, (uint32_t)-5));
	v.push_back(MakeArg(vplr::kBool, 1));
	v.push_back(MakeArg(vplr::kFloat, vplr::FloatBits(-0.0f)));
	v.push_back(MakeArg(vplr::kNull, 0));
	v.push_back(Str("CITY_NAME", 9));
	v.push_back(Str(NULL, 0));
	v.push_back(Str("abc", 2));  // a size below strlen travels unchanged, as the generated path sent it
	v.push_back(MakeArg(vplr::kInt, 0x7FFFFFFFu));
	struct K
	{
		uint32_t kind;
		uint32_t in;
		const char* call;
		bool handled;
		uint32_t out;
	} ks[] = {
	    {vplr::kHook, 1, "CallHook(PlayerDoTurn, args1, 1)", true, 0},
	    {vplr::kTestAll, 1, "CallTestAll(PlayerDoTurn, args1, 1)", true, 0},
	    {vplr::kTestAny, 0, "CallTestAny(PlayerDoTurn, args1, 0)", true, 1},
	    {vplr::kAccumInt, 11, "CallAccumulator(PlayerDoTurn, args1, int 11)", true, 18},
	    {vplr::kAccumFloat, vplr::FloatBits(1.25f), "CallAccumulator(PlayerDoTurn, args1, float 1.25)", true,
	     vplr::FloatBits(1.75f)},
	};
	for (size_t k = 0; k < sizeof(ks) / sizeof(ks[0]); ++k)
	{
		FakeSS ss;
		ss.listeners.push_back("PlayerDoTurn");
		g_events.clear();
		const Served s = DecodeServe(&ss, EncodeCall(ks[k].kind, vplr::kFlagArgs, 0, "PlayerDoTurn", ks[k].in, v));
		CHECK(s.decoded && s.status == vplr::kServed);
		CHECK(s.handled == ks[k].handled && s.value == ks[k].out);
		const char* want[] = {"CreateArgs()", "args1.Push(int -5)", "args1.Push(bool 1)", "args1.Push(float 80000000)",
		                      "args1.PushNULL()", "args1.Push(str CITY_NAME, 9)", "args1.Push(str (null), 0)",
		                      "args1.Push(str abc, 2)", "args1.Push(int 2147483647)", ks[k].call, "DestroyArgs(args1)"};
		const size_t nw = sizeof(want) / sizeof(want[0]);
		CHECK(g_events.size() == nw);
		for (size_t i = 0; i < nw && i < g_events.size(); ++i)
		{
			if (g_events[i] != want[i]) printf("  kind %u event %u: '%s', want '%s'\n", ks[k].kind, (unsigned)i,
			                                   g_events[i].c_str(), want[i]);
			CHECK(g_events[i] == want[i]);
		}
		CHECK(ss.live == NULL);  // destroyed
	}
}

// CreateArgs(reserve), no args object (NULL), an empty args object.
void TestReserveAndNoArgs()
{
	FakeSS ss;
	g_events.clear();
	std::vector<vplr::Arg> v;
	v.push_back(MakeArg(vplr::kInt, 3));
	Served s = DecodeServe(&ss, EncodeCall(vplr::kHook, vplr::kFlagArgs | vplr::kFlagReserve, 12, "UnitSetXY", 0, v));
	CHECK(s.decoded && s.status == vplr::kServed && !s.handled && s.value == 0);
	CHECK(g_events.size() == 4 && g_events[0] == "CreateArgs(12)" && g_events[1] == "args1.Push(int 3)" &&
	      g_events[2] == "CallHook(UnitSetXY, args1, 0)" && g_events[3] == "DestroyArgs(args1)");
	g_events.clear();
	s = DecodeServe(&ss, EncodeCall(vplr::kAccumInt, 0, 0, "GetSomething", 4, std::vector<vplr::Arg>()));
	CHECK(s.decoded && s.status == vplr::kServed && !s.handled && s.value == 4);
	CHECK(g_events.size() == 1 && g_events[0] == "CallAccumulator(GetSomething, NULL, int 4)");
	g_events.clear();
	s = DecodeServe(&ss, EncodeCall(vplr::kTestAll, vplr::kFlagArgs, 0, "X", 1, std::vector<vplr::Arg>()));
	CHECK(s.decoded && s.status == vplr::kServed && s.value == 1);
	CHECK(g_events.size() == 3 && g_events[0] == "CreateArgs()" && g_events[1] == "CallTestAll(X, args2, 1)" &&
	      g_events[2] == "DestroyArgs(args2)");
}

// An event name nobody listens to: handled false, value unchanged for every kind (engine-contract 10.1; CallHook's value
// is always false).
void TestNoListener()
{
	FakeSS ss;
	ss.listeners.push_back("Other");
	const uint32_t kinds[] = {vplr::kTestAll, vplr::kTestAny, vplr::kAccumInt, vplr::kAccumFloat};
	const uint32_t ins[] = {1, 0, 99, vplr::FloatBits(2.5f)};
	for (int i = 0; i < 4; ++i)
	{
		const Served s = DecodeServe(&ss, EncodeCall(kinds[i], vplr::kFlagArgs, 0, "NoSuchEvent", ins[i],
		                                             std::vector<vplr::Arg>(1, MakeArg(vplr::kInt, 1))));
		CHECK(s.decoded && s.status == vplr::kServed && !s.handled && s.value == ins[i]);
	}
	const Served h = DecodeServe(&ss, EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, "NoSuchEvent", 1, std::vector<vplr::Arg>()));
	CHECK(h.decoded && !h.handled && h.value == 0);
}

// The EXE side cannot run the event: no script system, CreateArgs returns NULL. No Call* is made.
void TestServeFailures()
{
	std::vector<vplr::Arg> v(1, MakeArg(vplr::kInt, 1));
	{
		const std::vector<uint8_t> b = EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, "E", 0, v);
		uint8_t* p = static_cast<uint8_t*>(malloc(b.size()));
		memcpy(p, &b[0], b.size());
		vpipc::Reader r(p, (uint32_t)b.size());
		vplr::Call c;
		const char* why = NULL;
		CHECK(vplr::Decode(r, c, why));
		bool h = true;
		uint32_t val = 5;
		const int st = vplr::Serve<FakeSS, FakeArgs>(NULL, c, h, val);
		CHECK(st == vplr::kNoScriptSystem && !h);
		free(p);
	}
	FakeSS ss;
	ss.createFails = true;
	g_events.clear();
	const Served s = DecodeServe(&ss, EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, "E", 0, v));
	CHECK(s.decoded && s.status == vplr::kCreateFailed && !s.handled);
	CHECK(g_events.size() == 1 && g_events[0] == "CreateArgs()");  // nothing pushed, no call, nothing destroyed
}

// Malformed CALLs are refused before any EXE call, with a reason; every prefix of a valid CALL is refused.
void TestMalformed()
{
	std::vector<vplr::Arg> v;
	v.push_back(MakeArg(vplr::kInt, 1));
	v.push_back(Str("name", 4));
	v.push_back(MakeArg(vplr::kFloat, 0));
	const std::vector<uint8_t> good = EncodeCall(vplr::kTestAll, vplr::kFlagArgs, 0, "PlayerCanTrain", 1, v);
	FakeSS ss;
	for (size_t len = 0; len < good.size(); ++len)
	{
		g_events.clear();
		const Served s = DecodeServe(&ss, std::vector<uint8_t>(good.begin(), good.begin() + len));
		CHECK(!s.decoded && s.why != NULL && g_events.empty());
	}
	{
		std::vector<uint8_t> b = good;
		b.push_back(0);
		b.push_back(0);
		b.push_back(0);
		b.push_back(0);
		g_events.clear();
		const Served s = DecodeServe(&ss, b);
		CHECK(!s.decoded && s.why && strcmp(s.why, "bytes left over") == 0 && g_events.empty());
	}
	// field patches: kind 5, flags bit 2, tag 5, n 257, args without kFlagArgs, null name
	{
		std::vector<uint8_t> b = good;
		const uint32_t k = 5;
		memcpy(&b[0], &k, 4);
		const Served s = DecodeServe(&ss, b);
		CHECK(!s.decoded && s.why && strcmp(s.why, "unknown call kind") == 0);
	}
	{
		std::vector<uint8_t> b = good;
		const uint32_t f = 4 | vplr::kFlagArgs;
		memcpy(&b[4], &f, 4);
		const Served s = DecodeServe(&ss, b);
		CHECK(!s.decoded && s.why && strcmp(s.why, "unknown flag bits") == 0);
	}
	{
		const std::vector<uint8_t> b = EncodeCall(vplr::kHook, 0, 0, "E", 0, v);  // args but no args object
		const Served s = DecodeServe(&ss, b);
		CHECK(!s.decoded && s.why && strcmp(s.why, "arguments without an args object") == 0);
	}
	{
		std::vector<vplr::Arg> bad(1, MakeArg(5, 0));
		const Served s = DecodeServe(&ss, EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, "E", 0, bad));
		CHECK(!s.decoded && s.why && strcmp(s.why, "unknown argument tag") == 0);
	}
	{
		std::vector<vplr::Arg> many(vplr::kMaxArgs + 1, MakeArg(vplr::kNull, 0));
		const Served s = DecodeServe(&ss, EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, "E", 0, many));
		CHECK(!s.decoded && s.why && strcmp(s.why, "more than 256 arguments") == 0);
		std::vector<vplr::Arg> max(vplr::kMaxArgs, MakeArg(vplr::kNull, 0));
		g_events.clear();
		const Served m = DecodeServe(&ss, EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, "E", 0, max));
		CHECK(m.decoded && m.status == vplr::kServed && g_events.size() == vplr::kMaxArgs + 3);
	}
	{
		vpipc::Writer w;
		std::vector<uint8_t> scratch(64);
		w.reset(&scratch[0], 64, true);
		w.u32(0);
		w.u32(0);
		w.u32(0);
		w.str(NULL);  // null name
		w.u32(0);
		w.u32(0);
		const Served s = DecodeServe(&ss, std::vector<uint8_t>(w.data(), w.data() + w.size()));
		CHECK(!s.decoded && s.why && strcmp(s.why, "null event name") == 0);
	}
	CHECK(ss.live == NULL);
}

// CallBytes equals what Encode writes (the server refuses a CALL above kMaxBytes with it); an oversize CALL is above.
void TestSizes()
{
	const char* names[] = {"", "E", "Ab", "Abc", "PlayerCanConstruct", "A name of exactly thirty-two by"};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
		for (uint32_t n = 0; n < 9; ++n)
		{
			std::vector<vplr::Arg> v;
			for (uint32_t k = 0; k < n; ++k)
			{
				switch (k % 5)
				{
				case 0: v.push_back(MakeArg(vplr::kNull, 0)); break;
				case 1: v.push_back(MakeArg(vplr::kInt, k)); break;
				case 2: v.push_back(Str(std::string(k, 'x').c_str(), k)); break;
				case 3: v.push_back(Str(NULL, 0)); break;
				default: v.push_back(MakeArg(vplr::kFloat, vplr::FloatBits(0.5f))); break;
				}
			}
			const std::vector<uint8_t> b = EncodeCall(vplr::kHook, vplr::kFlagArgs, 0, names[i], 0, v);
			CHECK(vplr::CallBytes(names[i], v.empty() ? NULL : &v[0], n) == b.size());
		}
	std::vector<vplr::Arg> big(1, Str(std::string(vplr::kMaxBytes, 'y').c_str(), vplr::kMaxBytes));
	CHECK(vplr::CallBytes("E", &big[0], 1) > vplr::kMaxBytes);
}

// Random bytes never crash the decoder (ASan watches the reads) and a random CALL is served only when it decodes.
void TestFuzz()
{
	uint32_t seed = 4804;
	FakeSS ss;
	ss.listeners.push_back("E");
	int decoded = 0;
	// half random words, half valid CALLs with one to three bytes changed or cut short
	std::vector<vplr::Arg> v;
	v.push_back(MakeArg(vplr::kInt, 7));
	v.push_back(Str("E", 1));
	v.push_back(MakeArg(vplr::kBool, 1));
	v.push_back(Str(NULL, 0));
	const std::vector<uint8_t> good = EncodeCall(vplr::kTestAny, vplr::kFlagArgs | vplr::kFlagReserve, 4, "E", 0, v);
	for (int it = 0; it < 20000; ++it)
	{
		seed = seed * 1664525u + 1013904223u;
		std::vector<uint8_t> b;
		if (it & 1)
		{
			b = good;
			const int edits = 1 + (int)((seed >> 4) % 3);
			for (int e = 0; e < edits; ++e)
			{
				seed = seed * 1664525u + 1013904223u;
				b[(seed >> 8) % b.size()] = (uint8_t)(seed >> 24);
			}
			if ((seed >> 3) % 4 == 0) b.resize((seed >> 12) % b.size());
		}
		else
		{
			const size_t len = (seed >> 8) % 96;
			b.resize(len);
			for (size_t i = 0; i < len; ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				// mostly small words so that lengths and tags are often plausible
				b[i] = (uint8_t)((seed >> 24) % ((i % 4) ? 2 : 9));
			}
		}
		g_events.clear();
		const Served s = DecodeServe(&ss, b);
		if (s.decoded)
		{
			++decoded;
			CHECK(s.status == vplr::kServed);
		}
		else
			CHECK(g_events.empty());
	}
	CHECK(ss.live == NULL);
	printf("fuzz: 20000 buffers, %d decoded and served\n", decoded);
}
}  // namespace

int main()
{
	printf("vp_luaraise tests (%d-bit)\n", (int)(sizeof(void*) * 8));
	TestOrderAndValues();
	TestReserveAndNoArgs();
	TestNoListener();
	TestServeFailures();
	TestMalformed();
	TestSizes();
	TestFuzz();
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
