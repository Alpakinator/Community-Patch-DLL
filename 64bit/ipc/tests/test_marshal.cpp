// tests/test_marshal.cpp - Writer / Reader (PROTOCOL.md section 5). Task 1201.
// C++03 library API only in tests (VC9 build): no vector::data(), std::to_string, std::chrono.
#include <float.h>
#include <string.h>

#include <string>
#include <vector>

#include "check.h"
#include "internal.h"
#include "vpipc/marshal.h"

using namespace vpipc;

namespace {

enum Color { kRed = 1, kGreen = 2, kBlue = -3 };

uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
uint64_t d2u(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
double u2d(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

double qpc_seconds() {
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart / (double)f.QuadPart;
}

// ---- one value per field; read() returns a canonical byte string of what the Reader produced ----
enum Kind { kI32, kU32, kI64, kU64, kF32, kF64, kBool, kEnum, kStr, kWstr, kBytes, kHandle };

struct Field {
    Kind kind;
    std::string expect;  // canonical form of the expected value
};

std::string raw(const void* p, size_t n) { return std::string(static_cast<const char*>(p), n); }

std::string canon_str(const char* s, uint32_t n) { return s ? "S" + std::string(s, n) : std::string("N"); }
std::string canon_wstr(const wchar_t* s, uint32_t n) { return s ? "W" + raw(s, n * 2u) : std::string("N"); }
std::string canon_bytes(const uint8_t* p, uint32_t n) { return p ? "B" + raw(p, n) : std::string("N"); }

std::string read_field(Reader& r, Kind k) {
    switch (k) {
    case kI32: { int32_t v = r.i32(); return raw(&v, 4); }
    case kU32: { uint32_t v = r.u32(); return raw(&v, 4); }
    case kI64: { int64_t v = r.i64(); return raw(&v, 8); }
    case kU64: { uint64_t v = r.u64(); return raw(&v, 8); }
    case kF32: { uint32_t v = f2u(r.f32()); return raw(&v, 4); }
    case kF64: { uint64_t v = d2u(r.f64()); return raw(&v, 8); }
    case kBool: { uint32_t v = r.b() ? 1u : 0u; return raw(&v, 4); }
    case kEnum: { int32_t v = (int32_t)r.e<Color>(); return raw(&v, 4); }
    case kStr: { uint32_t n = 77; const char* s = r.cstr(&n); if (!s && n) return "BADLEN"; return canon_str(s, n); }
    case kWstr: { uint32_t n = 77; const wchar_t* s = r.wcstr(&n); if (!s && n) return "BADLEN"; return canon_wstr(s, n); }
    case kBytes: { uint32_t n = 77; const uint8_t* p = r.bytes(&n); if (!p && n) return "BADLEN"; return canon_bytes(p, n); }
    case kHandle: { Handle h = r.handle(); return raw(&h.type, 4) + raw(&h.id, 4); }
    }
    return "?";
}

// What a failed read must return.
std::string zero_of(Kind k) {
    static const char z[8] = {0};
    switch (k) {
    case kI64: case kU64: case kF64: case kHandle: return raw(z, 8);
    case kStr: case kWstr: case kBytes: return "N";
    default: return raw(z, 4);
    }
}

struct Payload {
    std::vector<Field> fields;
    void add(Kind k, const std::string& e) { Field f; f.kind = k; f.expect = e; fields.push_back(f); }
};

// Writes every encoding in PROTOCOL.md section 5 (with the awkward values) and records what a read returns.
void build(Writer& w, Payload& pl) {
    const int32_t i32s[] = {0, 1, -1, 0x7FFFFFFF, (int32_t)0x80000000};
    for (size_t i = 0; i < sizeof i32s / sizeof i32s[0]; ++i) { w.i32(i32s[i]); pl.add(kI32, raw(&i32s[i], 4)); }
    const uint32_t u32s[] = {0, 0xFFFFFFFFu, 0xDEADBEEFu};
    for (size_t i = 0; i < 3; ++i) { w.u32(u32s[i]); pl.add(kU32, raw(&u32s[i], 4)); }
    const int64_t i64s[] = {0, -1, (int64_t)0x8000000000000000ull, 0x0123456789ABCDEFll};
    for (size_t i = 0; i < 4; ++i) { w.i64(i64s[i]); pl.add(kI64, raw(&i64s[i], 8)); }
    const uint64_t u64s[] = {0xFFFFFFFFFFFFFFFFull, 0xFEDCBA9876543210ull};
    for (size_t i = 0; i < 2; ++i) { w.u64(u64s[i]); pl.add(kU64, raw(&u64s[i], 8)); }
    // f32: -0.0, denormal, inf, quiet NaNs with payloads (both signs). Signalling NaNs: see test f32_signalling_nan.
    const uint32_t f32s[] = {0x00000000u, 0x80000000u, 0x3F800000u, 0x00000001u, 0x7F800000u, 0xFF800000u,
                             0x7FC00000u, 0xFFC00001u, 0x7FFFFFFFu, 0x7FC12345u, f2u(FLT_MAX)};
    for (size_t i = 0; i < sizeof f32s / sizeof f32s[0]; ++i) { w.f32(u2f(f32s[i])); pl.add(kF32, raw(&f32s[i], 4)); }
    const uint64_t f64s[] = {0, 0x8000000000000000ull, d2u(3.141592653589793), 1ull, 0x7FF0000000000000ull,
                             0x7FF8000000000000ull, 0xFFF8000000000001ull, 0x7FFFFFFFFFFFFFFFull,
                             0x7FF8DEADBEEF1234ull};
    for (size_t i = 0; i < sizeof f64s / sizeof f64s[0]; ++i) { w.f64(u2d(f64s[i])); pl.add(kF64, raw(&f64s[i], 8)); }
    const uint32_t one = 1, zero = 0;
    w.b(true); pl.add(kBool, raw(&one, 4));
    w.b(false); pl.add(kBool, raw(&zero, 4));
    const int32_t blue = kBlue, green = kGreen;
    w.e(kBlue); pl.add(kEnum, raw(&blue, 4));
    w.e(kGreen); pl.add(kEnum, raw(&green, 4));
    // narrow strings: null, empty (three ways), lengths 1..8 (every padding case), embedded NUL, long
    w.str((const char*)0); pl.add(kStr, "N");
    w.str(""); pl.add(kStr, "S");
    w.str((const char*)0, 0); pl.add(kStr, "S");
    w.str("xyz", 0); pl.add(kStr, "S");
    const char* abc = "abcdefgh";
    for (uint32_t n = 1; n <= 8; ++n) { w.str(abc, n); pl.add(kStr, "S" + std::string(abc, n)); }
    w.str("a\0b", 3); pl.add(kStr, "S" + std::string("a\0b", 3));
    std::string longs;
    for (int i = 0; i < 5000; ++i) longs += (char)('A' + i % 26);
    w.str(longs.c_str()); pl.add(kStr, "S" + longs);
    w.str("UTF-8 \xC3\xA9"); pl.add(kStr, "S" + std::string("UTF-8 \xC3\xA9"));
    // wide strings: null, empty, lengths 1..3, surrogate pair, long
    w.wstr((const wchar_t*)0); pl.add(kWstr, "N");
    w.wstr(L""); pl.add(kWstr, "W");
    const wchar_t* wabc = L"xyz";
    for (uint32_t n = 1; n <= 3; ++n) { w.wstr(wabc, n); pl.add(kWstr, "W" + raw(wabc, n * 2)); }
    const wchar_t sur[] = {0xD83D, 0xDE00, 0x00E9, 0};
    w.wstr(sur); pl.add(kWstr, "W" + raw(sur, 6));
    std::wstring longw(3000, L'\x4E2D');
    w.wstr(longw.c_str(), (uint32_t)longw.size()); pl.add(kWstr, "W" + raw(longw.c_str(), longw.size() * 2));
    // byte buffers: empty, 1..5, 1000
    const uint8_t bb[] = {0, 1, 2, 0xFF, 0x80};
    w.bytes(bb, 0); pl.add(kBytes, "B");
    for (uint32_t n = 1; n <= 5; ++n) { w.bytes(bb, n); pl.add(kBytes, "B" + raw(bb, n)); }
    std::vector<uint8_t> big(1000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = (uint8_t)(i * 7);
    w.bytes(&big[0], 1000); pl.add(kBytes, "B" + raw(&big[0], 1000));
    // handles
    Handle h1 = {3, -1}, h2 = {0xFFFFFFFFu, 0x7FFFFFFF};
    w.handle(h1); pl.add(kHandle, raw(&h1.type, 4) + raw(&h1.id, 4));
    w.handle(h2); pl.add(kHandle, raw(&h2.type, 4) + raw(&h2.id, 4));
    // a 64-bit value right after a 4-byte one: 4-aligned, not 8-aligned
    w.i32(5); pl.add(kI32, raw(&i32s[0], 0) + std::string("\x05\0\0\0", 4));
    w.u64(0x1122334455667788ull);
    const uint64_t u = 0x1122334455667788ull;
    pl.add(kU64, raw(&u, 8));
}

}  // namespace

TEST(marshal_round_trip_all_encodings) {
    std::vector<uint8_t> buf(64 * 1024);
    Writer w;
    w.reset(&buf[0], (uint32_t)buf.size(), false);
    Payload pl;
    build(w, pl);
    REQUIRE(!w.overflowed());
    REQUIRE(!w.spilled());
    CHECK(w.data() == &buf[0]);
    CHECK_EQ(w.size() % 4, 0u);
    printf("  payload %u bytes, %u fields\n", w.size(), (unsigned)pl.fields.size());
    Reader r(w.data(), w.size());
    for (size_t i = 0; i < pl.fields.size(); ++i) {
        const std::string got = read_field(r, pl.fields[i].kind);
        if (got != pl.fields[i].expect) printf("  field %u (kind %d) differs\n", (unsigned)i, (int)pl.fields[i].kind);
        CHECK(got == pl.fields[i].expect);
    }
    CHECK(r.ok());
    CHECK_EQ(r.remaining(), 0u);
}

// Every length 0..300 (the inline small-copy paths and the memcpy path) at every source alignment 0..3.
TEST(marshal_all_short_lengths) {
    std::vector<uint8_t> src(400), buf(4096);
    for (size_t i = 0; i < src.size(); ++i) src[i] = (uint8_t)(i * 31 + 7);
    Writer w;
    int bad = 0;
    for (uint32_t align = 0; align < 4; ++align) {
        for (uint32_t n = 0; n <= 300; ++n) {
            memset(&buf[0], 0xEE, buf.size());
            w.reset(&buf[0], (uint32_t)buf.size(), false);
            w.str((const char*)&src[align], n);
            w.bytes(&src[align], n);
            w.wstr((const wchar_t*)&src[align], n / 2);
            w.u32(0xA5A5A5A5u);
            Reader r(w.data(), w.size());
            uint32_t len = 0;
            const char* s = r.cstr(&len);
            bad += !s || len != n || memcmp(s, &src[align], n) != 0;
            const uint8_t* b = r.bytes(&len);
            bad += !b || len != n || memcmp(b, &src[align], n) != 0;
            const wchar_t* ws = r.wcstr(&len);
            bad += !ws || len != n / 2 || memcmp(ws, &src[align], (n / 2) * 2) != 0;
            bad += r.u32() != 0xA5A5A5A5u;
            bad += !r.ok() || r.remaining() != 0;
            bad += buf[w.size()] != 0xEE;
        }
    }
    CHECK_EQ(bad, 0);
}

TEST(marshal_wire_format) {
    uint8_t buf[64];
    memset(buf, 0xEE, sizeof buf);
    Writer w;
    w.reset(buf, sizeof buf, false);
    w.str("ab");           // 02 00 00 00 'a' 'b' 00 00
    w.str((const char*)0); // FF FF FF FF
    w.wstr(L"z");          // 01 00 00 00 'z' 00 00 00
    w.bytes("\x01\x02\x03\x04\x05", 5);  // 05 00 00 00 01 02 03 04 05 00 00 00
    w.f64(-0.0);           // 00 .. 00 80 at offset 28
    w.b(true);
    const uint8_t expect[] = {2, 0, 0, 0, 'a', 'b', 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 1, 0, 0, 0, 'z', 0, 0, 0,
                              5, 0, 0, 0, 1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 1, 0, 0, 0};
    CHECK_EQ(w.size(), (uint32_t)sizeof expect);
    CHECK(memcmp(buf, expect, sizeof expect) == 0);
    CHECK_EQ(buf[sizeof expect], 0xEE);  // nothing written past size()
    uint8_t* p = w.reserve(5);
    REQUIRE(p == buf + sizeof expect);
    CHECK_EQ(w.size(), (uint32_t)sizeof expect + 8u);
    CHECK_EQ(buf[sizeof expect + 5], 0);  // the pad past n is zero
    CHECK_EQ(buf[sizeof expect + 7], 0);
}

// Every truncation of the payload: the reads up to the cut return the right values, the read that crosses
// the cut and every later read return zero results, and ok() ends false.
TEST(marshal_truncation_every_length) {
    std::vector<uint8_t> buf(64 * 1024);
    Writer w;
    w.reset(&buf[0], (uint32_t)buf.size(), false);
    Payload pl;
    build(w, pl);
    REQUIRE(!w.overflowed());
    const uint32_t full = w.size();
    // Copy each truncated payload into its own heap block so a read past the end would touch other memory
    // (and fault under page heap) rather than the rest of the original.
    uint32_t bad = 0;
    for (uint32_t cut = 0; cut < full; ++cut) {
        std::vector<uint8_t> t(buf.begin(), buf.begin() + cut);
        Reader r(cut ? &t[0] : (const uint8_t*)0, cut);
        bool failed = false;
        for (size_t i = 0; i < pl.fields.size(); ++i) {
            const uint8_t* before = r.cursor();
            const std::string got = read_field(r, pl.fields[i].kind);
            if (!failed && !r.ok()) failed = true;
            const std::string& want = failed ? zero_of(pl.fields[i].kind) : pl.fields[i].expect;
            if (got != want) {
                if (++bad < 5) printf("  cut %u field %u: wrong result (failed=%d)\n", cut, (unsigned)i, failed);
            }
            if (!failed && r.cursor() > (cut ? &t[0] : (const uint8_t*)0) + cut) ++bad;
            (void)before;
        }
        if (r.ok() || r.remaining() > cut) ++bad;
    }
    CHECK_EQ(bad, 0u);
    printf("  %u truncations checked\n", full);
}

TEST(marshal_malformed_lengths) {
    uint8_t buf[64];
    Writer w;
    // a string length far past the end
    const uint32_t lens[] = {0xFFFFFFF0u, 0xFFFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 13u};
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; ++i) {
        w.reset(buf, sizeof buf, false);
        w.u32(lens[i]);
        w.u32(0x41414141u);
        w.u32(0);
        uint32_t n = 99;
        Reader a(w.data(), w.size());
        CHECK(a.cstr(&n) == 0 && n == 0 && !a.ok());
        CHECK_EQ(a.i32(), 0);
        Reader b(w.data(), w.size());
        n = 99;
        CHECK(b.wcstr(&n) == 0 && n == 0 && !b.ok());
        Reader c(w.data(), w.size());
        n = 99;
        CHECK(c.bytes(&n) == 0 && n == 0 && !c.ok());
    }
    // bytes has no null encoding: 0xFFFFFFFF is malformed
    w.reset(buf, sizeof buf, false);
    w.u32(kNullLength);
    {
        Reader r(w.data(), w.size());
        uint32_t n = 99;
        CHECK(r.bytes(&n) == 0 && n == 0 && !r.ok());
    }
    // a string without its terminating NUL
    w.reset(buf, sizeof buf, false);
    w.u32(3);
    w.u32(0x44434241u);  // "ABCD": byte 3 is 'D', not NUL
    {
        Reader r(w.data(), w.size());
        CHECK(r.cstr() == 0 && !r.ok());
    }
    w.reset(buf, sizeof buf, false);
    w.u32(1);
    w.u32(0x00410042u);  // units 'B', 'A': the unit after the string is not NUL
    {
        Reader r(w.data(), w.size());
        CHECK(r.wcstr() == 0 && !r.ok());
    }
    // a null Reader
    Reader z(0, 0);
    CHECK_EQ(z.u64(), 0ull);
    CHECK(!z.ok());
    Reader zz(0, 16);
    CHECK(!zz.ok());
    CHECK_EQ(zz.remaining(), 0u);
}

TEST(marshal_writer_spill) {
    uint8_t buf[16];
    memset(buf, 0, sizeof buf);
    Writer w;
    w.reset(buf, sizeof buf, true);
    w.i32(1);
    w.i32(2);
    w.i32(3);
    w.i32(4);  // exactly at the capacity: still in the buffer
    CHECK(!w.spilled());
    CHECK(w.data() == buf);
    CHECK_EQ(w.size(), 16u);
    w.i64(5);  // spills
    CHECK(w.spilled());
    CHECK(!w.overflowed());
    CHECK(w.data() != buf);
    CHECK_EQ(w.size(), 24u);
    for (int i = 6; i < 2000; ++i) w.i32(i);
    w.str("tail");
    CHECK_EQ(w.size(), 24u + 1994u * 4u + 12u);
    Reader r(w.data(), w.size());
    CHECK_EQ(r.i32(), 1);
    CHECK_EQ(r.i32(), 2);
    CHECK_EQ(r.i32(), 3);
    CHECK_EQ(r.i32(), 4);
    CHECK_EQ(r.i64(), 5ll);
    int bad = 0;
    for (int i = 6; i < 2000; ++i) bad += r.i32() != i;
    CHECK_EQ(bad, 0);
    CHECK(strcmp(r.cstr(), "tail") == 0);
    CHECK(r.ok() && r.remaining() == 0);
    // reset goes back to the fixed buffer
    w.reset(buf, sizeof buf, true);
    CHECK(!w.spilled() && w.size() == 0 && w.data() == buf);
    w.u32(9);
    CHECK_EQ(buf[0], 9);
    // a single write larger than the buffer spills too; pure heap writer (no buffer)
    w.reset(buf, sizeof buf, true);
    std::string s(100, 'q');
    w.str(s.c_str());
    CHECK(w.spilled() && w.size() == 4u + 104u);
    w.reset(0, 0, true);
    w.u32(7);
    CHECK(w.spilled() && w.size() == 4 && w.data() != 0);
}

TEST(marshal_writer_overflow) {
    uint8_t buf[16];
    memset(buf, 0xEE, sizeof buf);
    Writer w;
    w.reset(buf, 12, false);
    w.i32(1);
    w.i32(2);
    w.i64(3);  // does not fit: overflow, nothing written
    CHECK(w.overflowed());
    CHECK(!w.spilled());
    CHECK_EQ(w.size(), 8u);
    CHECK(w.data() == buf);
    CHECK_EQ(buf[8], 0xEE);
    w.i32(4);  // would fit, but writes after an overflow are dropped
    CHECK_EQ(w.size(), 8u);
    CHECK_EQ(buf[8], 0xEE);
    CHECK(w.reserve(4) == 0);
    w.reset(buf, 12, false);
    CHECK(!w.overflowed());
    w.str("12345678");  // 4 + 12 = 16 > 12
    CHECK(w.overflowed());
    CHECK_EQ(w.size(), 0u);
    // absurd lengths overflow without touching the source, even when spilling is allowed
    w.reset(buf, sizeof buf, true);
    w.str((const char*)buf, 0xFFFFFFF0u);
    CHECK(w.overflowed());
    w.reset(buf, sizeof buf, true);
    w.bytes(buf, 0xFFFFFFFFu);
    CHECK(w.overflowed());
    w.reset(buf, sizeof buf, true);
    w.wstr((const wchar_t*)buf, 0x80000000u);
    CHECK(w.overflowed());
    w.reset(buf, sizeof buf, true);
    CHECK(w.reserve(0xFFFFFFFFu) == 0);
    CHECK(w.overflowed());
}

// Signalling NaNs: the Writer stores the exact bits. A float/double returned by value goes through x87 ST(0)
// in the x86 ABI unless the call is inlined, and loading a signalling NaN there sets its quiet bit. This is the
// same thing the original 32-bit EXE/DLL boundary does to float returns, so it is reported, not failed.
TEST(marshal_f32_signalling_nan) {
    uint8_t buf[16];
    Writer w;
    w.reset(buf, sizeof buf, false);
    w.f32(u2f(0x7F800001u));
    w.f64(u2d(0x7FF0000000000001ull));
    uint32_t a;
    uint64_t b;
    memcpy(&a, buf, 4);
    memcpy(&b, buf + 4, 8);
    printf("  written: f32 0x%08X, f64 0x%016llX\n", a, (unsigned long long)b);
    Reader r(buf, 12);
    const uint32_t ra = f2u(r.f32());
    const uint64_t rb = d2u(r.f64());
    printf("  read back: f32 0x%08X, f64 0x%016llX (%s)\n", ra, (unsigned long long)rb,
           ra == 0x7F800001u && rb == 0x7FF0000000000001ull ? "exact" : "quieted");
    CHECK(ra == 0x7F800001u || ra == 0x7FC00001u);
    CHECK(rb == 0x7FF0000000000001ull || rb == 0x7FF8000000000001ull);
}

// ---------------------------------------------------------------------------------------- timing

TEST(bench_writer_i32) {
    std::vector<uint8_t> buf(16 * 1024);
    Writer w;
    const uint32_t n = 100000000u, per = 4096;
    const double t0 = qpc_seconds();
    for (uint32_t i = 0; i < n; i += per) {
        w.reset(&buf[0], (uint32_t)buf.size(), false);
        for (uint32_t k = 0; k < per; ++k) w.i32((int32_t)k);
    }
    const double dt = qpc_seconds() - t0;
    CHECK(!w.overflowed());
    printf("  Writer::i32: %.2f ns (%u writes, reset every %u)\n", dt * 1e9 / n, n, per);
}

TEST(bench_writer_str64) {
    std::vector<uint8_t> buf(72 * 1000);
    Writer w;
    const char* s = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const uint32_t n = 20000000u, per = 1000;
    const double t0 = qpc_seconds();
    for (uint32_t i = 0; i < n; i += per) {
        w.reset(&buf[0], (uint32_t)buf.size(), false);
        for (uint32_t k = 0; k < per; ++k) w.str(s, 64);
    }
    const double dt = qpc_seconds() - t0;
    CHECK(!w.overflowed());
    CHECK_EQ(w.size(), 72000u);
    printf("  Writer::str (64 bytes, pointer + length): %.2f ns (%u writes)\n", dt * 1e9 / n, n);
    const double t1 = qpc_seconds();
    for (uint32_t i = 0; i < n; i += per) {
        w.reset(&buf[0], (uint32_t)buf.size(), false);
        for (uint32_t k = 0; k < per; ++k) w.str(s);
    }
    const double dt1 = qpc_seconds() - t1;
    printf("  Writer::str (64 bytes, NUL-terminated, strlen): %.2f ns\n", dt1 * 1e9 / n);
    uint64_t sum = 0;
    const double t2 = qpc_seconds();
    for (uint32_t i = 0; i < n; i += per) {
        Reader r(w.data(), w.size());
        for (uint32_t k = 0; k < per; ++k) {
            uint32_t len;
            const char* p = r.cstr(&len);
            sum += len + (uint8_t)p[0];
        }
    }
    const double dt2 = qpc_seconds() - t2;
    CHECK_EQ(sum, (uint64_t)n * (64u + '0'));
    printf("  Reader::cstr (64 bytes): %.2f ns\n", dt2 * 1e9 / n);
}

VPIPC_TEST_MAIN()
