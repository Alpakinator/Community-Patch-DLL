// decal_tap_test.cpp - the decal tap's reader (server/src/render/srv_decal_tap.cpp) and the per-tile query
// (render/src/decal/decal_set.cpp) on images made here (epic 0166, issue 4103). No game data: a small writer of the
// layout in shell/common/shell_decal_tap.h makes every image, a fake producer serves it through real named sections.
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
//   decal_tap_test.exe                 lists the cases ("CASE <name>" lines), exit 0
//   decal_tap_test.exe <case> [DIR]    runs one case (DIR: scratch for the file form, default %TEMP%); "CASE <name> PASS"
//                                      and exit 0, or "FAIL ..." lines, "CASE <name> FAIL" and exit 1; exit 2 for an
//                                      unknown case
#include "srv_decal_tap.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

using namespace srvrender;
using c5r::decal::DecalRect;
using c5r::decal::DecalSet;
using c5r::decal::TileOrigin;

static int g_fail = 0;
static std::wstring g_dir;

#define CHECK(...)                                                                    \
	do                                                                              \
	{                                                                               \
		if(!(__VA_ARGS__))                                                                  \
		{                                                                           \
			printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__);                           \
			fflush(stdout);                                                         \
			g_fail++;                                                               \
		}                                                                           \
	} while(0)

static bool g_quiet = false;
static void HostLog(const char* line)
{
	if(!g_quiet)
		printf("  log: %s\n", line);
}

// ---- the test's own writer of the layout ------------------------------------------------------------------------------

static uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

struct TDecal
{
	float rect[4] = {10, 10, 20, 20};
	float prio = 0.5f;
	uint32_t flags = 0;
	uint32_t tex[3] = {0, 1, 2};
	uint32_t tri = 2;
	uint32_t listOrder = 0;
	uint32_t seed = 1;
};

struct TImage
{
	uint32_t generation = 1, loadSeq = 7;
	float world[4] = {0, 0, 1023, 1023};
	std::vector<std::string> names = {"roadsandrails_new_d.dds", "roadsandrails_new_h.dds", "blank_texture_spec_decal.dds"};
	std::vector<TDecal> decals;
	std::vector<std::array<float, 4>> changed;
	// 4111: the area block (areaX = 0: none, an image of 4102's layout); bits set at (x, y)
	uint32_t areaX = 0, areaY = 0;
	std::vector<std::pair<uint32_t, uint32_t>> areaBits;
	// 0366: the walk block (walk false: none); per decal its cell (or VPSHELL_DECAL_WALK_LARGE), rank and point
	bool walk = false;
	float wOrigin[2] = {0, 0}, wInv[2] = {0, 0};
	uint32_t wCols = 0, wRows = 0;
	std::vector<std::array<uint32_t, 2>> wCellRank;
	std::vector<std::array<float, 2>> wPoint;
};

// Field offsets of VPShellDecalImage, for the tests that break one
enum
{
	OFF_MAGIC = 0, OFF_VERSION = 4, OFF_HEADERBYTES = 8, OFF_DECALBYTES = 12, OFF_GENERATION = 16, OFF_LOADSEQ = 20,
	OFF_DECALCOUNT = 24, OFF_NAMECOUNT = 28, OFF_VERTEXCOUNT = 32, OFF_CHANGEDCOUNT = 36, OFF_DECALOFFSET = 40,
	OFF_NAMEOFFSET = 44, OFF_VERTEXOFFSET = 48, OFF_CHANGEDOFFSET = 52, OFF_WORLD = 56, OFF_IMAGEBYTES = 72,
	OFF_AREAOFFSET = 88, OFF_AREAX = 92, OFF_AREAY = 96, OFF_WALKOFFSET = 100,
};
static_assert(offsetof(VPShellDecalImage, walkOffset) == OFF_WALKOFFSET, "test layout");
static_assert(offsetof(VPShellDecalImage, areaOffset) == OFF_AREAOFFSET, "test layout");
static_assert(offsetof(VPShellDecalImage, areaTilesY) == OFF_AREAY, "test layout");
static_assert(offsetof(VPShellDecalImage, imageBytes) == OFF_IMAGEBYTES, "test layout");
static_assert(offsetof(VPShellDecalImage, changedOffset) == OFF_CHANGEDOFFSET, "test layout");
static_assert(offsetof(VPShellDecalImage, worldRect) == OFF_WORLD, "test layout");

static size_t Align16(size_t x) { return (x + 15) & ~(size_t)15; }

static std::vector<uint8_t> Build(const TImage& t)
{
	const uint32_t n = (uint32_t)t.decals.size();
	uint32_t verts = 0;
	for(const TDecal& d : t.decals)
		verts += 3 * d.tri;
	size_t off = 112;
	const size_t decalOff = off;
	off = Align16(off + 48ull * n);
	const size_t nameOff = off;
	off = Align16(off + 200ull * t.names.size());
	const size_t vertOff = off;
	off = Align16(off + 8ull * verts);
	const size_t chOff = off;
	off += 16ull * t.changed.size();
	size_t areaOff = 0;
	if(t.areaX)
	{
		off = Align16(off);
		areaOff = off;
		off += ((size_t)t.areaX * t.areaY + 7) / 8;
	}
	size_t walkOff = 0;
	if(t.walk)
	{
		off = Align16(off);
		walkOff = off;
		off += 32 + 16ull * n;
	}
	std::vector<uint8_t> b(off, 0);
	auto put32 = [&](size_t o, uint32_t v) { memcpy(&b[o], &v, 4); };
	put32(OFF_MAGIC, VPSHELL_DECAL_MAGIC);
	put32(OFF_VERSION, VPSHELL_DECAL_IMAGE_VERSION);
	put32(OFF_HEADERBYTES, 112);
	put32(OFF_DECALBYTES, 48);
	put32(OFF_GENERATION, t.generation);
	put32(OFF_LOADSEQ, t.loadSeq);
	put32(OFF_DECALCOUNT, n);
	put32(OFF_NAMECOUNT, (uint32_t)t.names.size());
	put32(OFF_VERTEXCOUNT, verts);
	put32(OFF_CHANGEDCOUNT, (uint32_t)t.changed.size());
	put32(OFF_DECALOFFSET, (uint32_t)decalOff);
	put32(OFF_NAMEOFFSET, (uint32_t)nameOff);
	put32(OFF_VERTEXOFFSET, (uint32_t)vertOff);
	put32(OFF_CHANGEDOFFSET, (uint32_t)chOff);
	memcpy(&b[OFF_WORLD], t.world, 16);
	const uint64_t total = off;
	memcpy(&b[OFF_IMAGEBYTES], &total, 8);
	uint32_t first = 0;
	for(uint32_t i = 0; i < n; ++i)
	{
		const TDecal& d = t.decals[i];
		uint8_t* r = &b[decalOff + 48ull * i];
		memcpy(r + 0, d.rect, 16);
		memcpy(r + 16, &d.prio, 4);
		memcpy(r + 20, &d.flags, 4);
		memcpy(r + 24, d.tex, 12);
		memcpy(r + 36, &first, 4);
		memcpy(r + 40, &d.tri, 4);
		memcpy(r + 44, &d.listOrder, 4);
		for(uint32_t k = 0; k < 3 * d.tri; ++k)
		{
			uint16_t v[4] = {(uint16_t)(d.seed * 131 + k), (uint16_t)(d.seed * 7 + 3 * k), (uint16_t)(k * 17), (uint16_t)d.seed};
			memcpy(&b[vertOff + 8ull * (first + k)], v, 8);
		}
		first += 3 * d.tri;
	}
	for(size_t i = 0; i < t.names.size(); ++i)
	{
		uint8_t* r = &b[nameOff + 200 * i];
		const uint32_t handle = 1000 + (uint32_t)i;
		memcpy(r, &handle, 4);
		memcpy(r + 8, t.names[i].c_str(), std::min<size_t>(t.names[i].size(), 191));
	}
	for(size_t i = 0; i < t.changed.size(); ++i)
		memcpy(&b[chOff + 16 * i], t.changed[i].data(), 16);
	if(t.areaX)
	{
		put32(OFF_AREAOFFSET, (uint32_t)areaOff);
		put32(OFF_AREAX, t.areaX);
		put32(OFF_AREAY, t.areaY);
		for(const auto& xy : t.areaBits)
		{
			if(xy.first >= t.areaX || xy.second >= t.areaY)
				continue;   // a bit of a grid a negative made smaller: not written (past the block)
			const size_t k = (size_t)xy.second * t.areaX + xy.first;
			b[areaOff + k / 8] |= (uint8_t)(1u << (k % 8));
		}
	}
	if(t.walk)
	{
		put32(OFF_WALKOFFSET, (uint32_t)walkOff);
		VPShellDecalWalkHeader h = {};
		memcpy(h.origin, t.wOrigin, 8);
		memcpy(h.inverse, t.wInv, 8);
		h.cols = t.wCols;
		h.rows = t.wRows;
		for(uint32_t i = 0; i < n && i < t.wCellRank.size(); ++i)
			(t.wCellRank[i][0] == VPSHELL_DECAL_WALK_LARGE ? h.largeCount : h.smallCount)++;
		memcpy(&b[walkOff], &h, 32);
		for(uint32_t i = 0; i < n && i < t.wCellRank.size() && i < t.wPoint.size(); ++i)
		{
			VPShellDecalWalk e = {t.wCellRank[i][0], t.wCellRank[i][1], {t.wPoint[i][0], t.wPoint[i][1]}};
			memcpy(&b[walkOff + 32 + 16ull * i], &e, 16);
		}
	}
	return b;
}

static uint32_t Get32(const std::vector<uint8_t>& b, size_t o)
{
	uint32_t v;
	memcpy(&v, &b[o], 4);
	return v;
}
static void Set32(std::vector<uint8_t>& b, size_t o, uint32_t v) { memcpy(&b[o], &v, 4); }
static void SetF(std::vector<uint8_t>& b, size_t o, float v) { memcpy(&b[o], &v, 4); }

static TDecal D(float x0, float y0, float x1, float y1, float prio, uint32_t listOrder, uint32_t seed)
{
	TDecal d;
	d.rect[0] = x0;
	d.rect[1] = y0;
	d.rect[2] = x1;
	d.rect[3] = y1;
	d.prio = prio;
	d.listOrder = listOrder;
	d.seed = seed;
	return d;
}

static TImage Standard(uint32_t generation)
{
	TImage t;
	t.generation = generation;
	t.decals.push_back(D(10, 10, 20, 20, 0.7f, 0, 1));
	t.decals.push_back(D(100, 62, 130, 64, 0.0f, 1, 2));
	t.decals.push_back(D(300, 300, 700, 400, 0.5f, 2, 3));
	t.decals.back().flags = 1;
	t.decals.back().tex[1] = VPSHELL_DECAL_NO_TEXTURE;
	t.decals.push_back(D(5, 5, 60, 60, 0.5f, 3, 4));
	t.decals.back().tri = 7;
	t.changed.push_back({10, 10, 20, 20});
	return t;
}

// ---- the fake producer: real named sections ------------------------------------------------------------------------

struct Producer
{
	uint32_t pid = 0;
	HANDLE ctl = nullptr;
	VPShellDecalTapControl* b = nullptr;
	std::map<uint32_t, HANDLE> images;

	explicit Producer(uint32_t p) : pid(p)
	{
		wchar_t name[64];
		_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_DECALTAP_CONTROL_FMT, pid);
		ctl = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_DECALTAP_CONTROL_BYTES, name);
		b = ctl ? (VPShellDecalTapControl*)MapViewOfFile(ctl, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_DECALTAP_CONTROL_BYTES) : nullptr;
	}
	~Producer()
	{
		for(auto& kv : images)
			CloseHandle(kv.second);
		if(b)
			UnmapViewOfFile(b);
		if(ctl)
			CloseHandle(ctl);
	}
	// The image section of `gen` with `bytes` (sectionBytes: the section's size, 0 = the image's)
	bool section(uint32_t gen, const std::vector<uint8_t>& bytes, size_t sectionBytes = 0)
	{
		wchar_t name[64];
		_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_DECAL_IMAGE_FMT, pid, gen);
		const size_t sz = sectionBytes ? sectionBytes : std::max<size_t>(bytes.size(), 1);
		HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sz, name);
		if(!h)
			return false;
		uint8_t* v = (uint8_t*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if(!v)
		{
			CloseHandle(h);
			return false;
		}
		memcpy(v, bytes.data(), (std::min)(sz, bytes.size()));
		UnmapViewOfFile(v);
		if(images.count(gen))
			CloseHandle(images[gen]);
		images[gen] = h;
		return true;
	}
	// The control block under the seqlock, READY; `tweak` changes it before the seqlock closes (leaveOdd: it never does)
	void publish(uint32_t gen, const std::vector<uint8_t>& img, const std::function<void(VPShellDecalTapControl&)>& tweak = {},
	             bool leaveOdd = false)
	{
		InterlockedIncrement((volatile LONG*)&b->seq);   // odd: writing
		MemoryBarrier();
		b->magic = VPSHELL_DECAL_MAGIC;
		b->state = VPDTAP_READY;
		b->failReason = 0;
		b->exePid = pid;
		b->generation = gen;
		b->loadSeq = img.size() >= 24 ? Get32(img, OFF_LOADSEQ) : 0;
		b->decalCount = img.size() >= 28 ? Get32(img, OFF_DECALCOUNT) : 0;
		b->imageBytes = img.size();
		b->fnv1a64 = Fnv(img.data(), img.size());
		_snwprintf_s(b->imageName, _countof(b->imageName), _TRUNCATE, VPSHELL_DECAL_IMAGE_FMT, pid, gen);
		if(tweak)
			tweak(*b);
		MemoryBarrier();
		if(!leaveOdd)
			InterlockedIncrement((volatile LONG*)&b->seq);
	}
	bool serve(uint32_t gen, const std::vector<uint8_t>& img, const std::function<void(VPShellDecalTapControl&)>& tweak = {})
	{
		if(!section(gen, img))
			return false;
		publish(gen, img, tweak);
		return true;
	}
};

static uint32_t g_pidBase = 0;
static uint32_t FakePid()   // a pid no game has: high bit set, unique per call in this process
{
	if(!g_pidBase)
		g_pidBase = 0x80000000u | ((GetCurrentProcessId() & 0xFFFFu) << 12);
	return g_pidBase++;
}

static bool SameSet(const DecalSet& a, const DecalSet& b)
{
	if(a.generation != b.generation || a.loadSeq != b.loadSeq || memcmp(a.worldRect, b.worldRect, 16) || a.textures != b.textures ||
	   a.decals.size() != b.decals.size() || a.vertices.size() != b.vertices.size())
		return false;
	if(!a.vertices.empty() && memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * 8))
		return false;
	for(size_t i = 0; i < a.decals.size(); ++i)
	{
		const auto &x = a.decals[i], &y = b.decals[i];
		if(memcmp(x.rect, y.rect, 16) || x.priority != y.priority || x.pass != y.pass || memcmp(x.texture, y.texture, 12) ||
		   x.firstVertex != y.firstVertex || x.vertexCount != y.vertexCount || x.listOrder != y.listOrder || x.hash != y.hash)
			return false;
	}
	return a.grid.cols == b.grid.cols && a.grid.rows == b.grid.rows && a.grid.start == b.grid.start && a.grid.items == b.grid.items;
}

static std::wstring TmpPath(const wchar_t* leaf)
{
	return g_dir + L"\\" + leaf;
}

static bool WriteFileBytes(const std::wstring& p, const std::vector<uint8_t>& b)
{
	FILE* f = nullptr;
	if(_wfopen_s(&f, p.c_str(), L"wb") || !f)
		return false;
	const bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
	fclose(f);
	return ok;
}

static std::vector<uint8_t> ReadFileBytes(const std::wstring& p)
{
	std::vector<uint8_t> b;
	FILE* f = nullptr;
	if(_wfopen_s(&f, p.c_str(), L"rb") || !f)
		return b;
	uint8_t buf[65536];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		b.insert(b.end(), buf, buf + n);
	fclose(f);
	return b;
}

// ---- cases -------------------------------------------------------------------------------------------------------

// The same image through a named section and through a file gives equal sets; the writer writes the image as is.
static void CaseSectionAndFileEqual()
{
	Producer p(FakePid());
	CHECK(p.b != nullptr);
	const std::vector<uint8_t> img = Build(Standard(1));
	CHECK(p.serve(1, img));
	DecalTapReader r(p.pid);
	CHECK(r.poll());
	CHECK(p.b->ackGeneration == 1 && p.b->ackResult == 0 && p.b->ackServerPid == GetCurrentProcessId());
	CHECK(!r.poll());   // the same generation is not read twice
	auto s = r.current();
	CHECK(s && s->generation == 1 && s->loadSeq == 7 && s->decals.size() == 4 && s->textures.size() == 3 &&
	      s->textures[2] == "blank_texture_spec_decal.dds");
	if(!s)
		return;
	CHECK(s->decals[2].pass == 1 && s->decals[2].texture[1] == c5r::decal::kNoTexture && s->decals[3].vertexCount == 21 &&
	      s->decals[3].firstVertex == 18 && s->vertices.size() == 6 + 6 + 6 + 21);
	CHECK(s->grid.cols > 0);
	// the recorder's side writes the image as is
	const std::wstring f1 = TmpPath(L"load0001.dtap");
	CHECK(r.writeCurrentImage(f1).ok());
	CHECK(ReadFileBytes(f1) == img);
	// the file form through the same checks
	DecalTapReader rf(FakePid());
	c5r::Status st = rf.loadFile(f1);
	CHECK(st.ok());
	auto sf = rf.current();
	CHECK(sf && SameSet(*s, *sf));
	// and the test's own bytes written straight to a file
	const std::wstring f2 = TmpPath(L"load0002.dtap");
	CHECK(WriteFileBytes(f2, img));
	DecalTapReader rf2(FakePid());
	CHECK(rf2.loadFile(f2).ok());
	CHECK(rf2.current() && SameSet(*s, *rf2.current()));
	CHECK(rf.counters().fileLoads == 1 && r.counters().accepted == 1 && r.counters().refused == 0);
	// before a set: the writer has nothing, the per-tile query is empty
	DecalTapReader none(FakePid());
	CHECK(none.writeCurrentImage(TmpPath(L"none.dtap")).code == c5r::Err::NotFound);
	c5r::decal::TileDecalList l;
	CHECK(!none.decalsForTile(0, 0, l) && l.indices.empty());
	CHECK(r.decalsForTile(0, 0, l) && l.indices == std::vector<uint32_t>({3, 0}));      // 3 (0.5), 0 (0.7)
	CHECK(r.decalsForTile(64, 0, l) && l.indices == std::vector<uint32_t>({1}));        // 1 (x 100 .. 130)
	DeleteFileW(f1.c_str());
	DeleteFileW(f2.c_str());
}

struct Refusal
{
	const char* name;
	uint32_t reason;
	int32_t ack;
	c5r::Err fileErr;   // Ok: the check is the block's, the file form has no such check
	std::function<void(std::vector<uint8_t>&)> image;
	std::function<void(VPShellDecalTapControl&)> block;
};

static std::vector<Refusal> Refusals()
{
	const int32_t BL = (int32_t)c5r::Err::BadLayout, TR = (int32_t)c5r::Err::Truncated;
	auto rec = [](uint32_t i, size_t field) { return (size_t)112 + 48 * i + field; };
	std::vector<Refusal> v = {
		{"block_magic", DREF_BLOCK_MAGIC, -4, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.magic = 0x12345678; }},
		{"block_pid", DREF_BLOCK, BL, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.exePid ^= 1; }},
		{"block_name", DREF_BLOCK, BL, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.imageName[5] = L'X'; }},
		{"block_count", DREF_BLOCK, BL, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.decalCount = VPSHELL_DECAL_MAX_DECALS + 1; }},
		{"block_bytes", DREF_BLOCK, BL, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.imageBytes = VPSHELL_DECAL_MAX_BYTES + 1; }},
		{"bad_magic", DREF_MAGIC, -4, c5r::Err::BadMagic, [](std::vector<uint8_t>& b) { Set32(b, OFF_MAGIC, 0x54443544); }, nullptr},
		{"bad_version", DREF_VERSION, -4, c5r::Err::BadVersion, [](std::vector<uint8_t>& b) { Set32(b, OFF_VERSION, 2); }, nullptr},
		{"bad_header_size", DREF_SIZES, -4, c5r::Err::BadVersion, [](std::vector<uint8_t>& b) { Set32(b, OFF_HEADERBYTES, 116); }, nullptr},
		{"bad_record_size", DREF_SIZES, -4, c5r::Err::BadVersion, [](std::vector<uint8_t>& b) { Set32(b, OFF_DECALBYTES, 52); }, nullptr},
		{"hash_mismatch", DREF_HASH, -1, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.fnv1a64 ^= 0x100; }},
		{"hash_absent", DREF_HASH, -1, c5r::Err::Ok, nullptr, [](VPShellDecalTapControl& c) { c.fnv1a64 = 0; }},
		{"disagree_generation", DREF_DISAGREE, BL, c5r::Err::Ok, [](std::vector<uint8_t>& b) { Set32(b, OFF_GENERATION, 99); }, nullptr},
		{"disagree_bytes", DREF_DISAGREE, BL, c5r::Err::Ok, [](std::vector<uint8_t>& b) {
			 uint64_t x;
			 memcpy(&x, &b[OFF_IMAGEBYTES], 8);
			 x -= 16;
			 memcpy(&b[OFF_IMAGEBYTES], &x, 8);
		 }, nullptr},
		{"count_names", DREF_COUNT, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { Set32(b, OFF_NAMECOUNT, VPSHELL_DECAL_MAX_NAMES + 1); }, nullptr},
		{"count_vertices", DREF_COUNT, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { Set32(b, OFF_VERTEXCOUNT, 0x7FFFFFFF); }, nullptr},
		{"count_changed", DREF_COUNT, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { Set32(b, OFF_CHANGEDCOUNT, 0xFFFFFFFF); }, nullptr},
		{"offset_past_end", DREF_OFFSET, TR, c5r::Err::Truncated, [](std::vector<uint8_t>& b) { Set32(b, OFF_VERTEXOFFSET, Get32(b, OFF_VERTEXOFFSET) + 4096); }, nullptr},
		{"offset_in_header", DREF_OFFSET, TR, c5r::Err::Truncated, [](std::vector<uint8_t>& b) { Set32(b, OFF_DECALOFFSET, 96); }, nullptr},
		{"offset_empty_past_end", DREF_OFFSET, TR, c5r::Err::Truncated, [](std::vector<uint8_t>& b) {
			 Set32(b, OFF_CHANGEDCOUNT, 0);
			 Set32(b, OFF_CHANGEDOFFSET, 0x10000000);
		 }, nullptr},
		{"misaligned", DREF_ALIGN, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { Set32(b, OFF_CHANGEDOFFSET, Get32(b, OFF_CHANGEDOFFSET) - 4); }, nullptr},
		{"overlap", DREF_ALIGN, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { Set32(b, OFF_CHANGEDOFFSET, Get32(b, OFF_VERTEXOFFSET)); }, nullptr},
		{"world_nan", DREF_WORLD, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { SetF(b, OFF_WORLD + 4, std::numeric_limits<float>::quiet_NaN()); }, nullptr},
		{"world_inverted", DREF_WORLD, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { SetF(b, OFF_WORLD + 8, -5.f); }, nullptr},
		{"rect_nan", DREF_RECT, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { SetF(b, rec(2, 8), std::numeric_limits<float>::quiet_NaN()); }, nullptr},
		{"rect_inf", DREF_RECT, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { SetF(b, rec(1, 0), -std::numeric_limits<float>::infinity()); }, nullptr},
		{"rect_inverted", DREF_RECT, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { SetF(b, rec(0, 4), 25.f); }, nullptr},
		{"rect_outside_world", DREF_RECT, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { SetF(b, rec(2, 8), 1025.5f); }, nullptr},
		// 0649: the form AddDecal gives a decal wholly past the east edge (min x past it, max x = the world's max). The tap
		// leaves such a record out of the set (decal_tap.cpp OutsideX); an image that still carries one is refused here.
		{"rect_seam_form", DREF_RECT, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) {
			 SetF(b, rec(2, 0), 1034.25f);
			 SetF(b, rec(2, 8), 1023.f);
		 }, nullptr},
		{"priority_nan", DREF_RECORD, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { SetF(b, rec(0, 16), std::numeric_limits<float>::quiet_NaN()); }, nullptr},
		{"flags_unknown", DREF_RECORD, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { Set32(b, rec(1, 20), 2); }, nullptr},
		{"triangles_zero", DREF_RECORD, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { Set32(b, rec(3, 40), 0); }, nullptr},
		{"triangles_too_many", DREF_RECORD, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { Set32(b, rec(3, 40), 21845); }, nullptr},
		{"vertex_range_outside", DREF_VERTEX, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { Set32(b, rec(3, 36), 19); }, nullptr},
		{"vertex_first_huge", DREF_VERTEX, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { Set32(b, rec(0, 36), 0xFFFFFFF0u); }, nullptr},
		{"texture_out_of_range", DREF_TEXTURE, BL, c5r::Err::BadLayout, [rec](std::vector<uint8_t>& b) { Set32(b, rec(1, 32), 3); }, nullptr},
		{"name_not_terminated", DREF_NAME, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) {
			 const size_t o = Get32(b, OFF_NAMEOFFSET) + 200 + 8;
			 memset(&b[o], 'a', 192);
		 }, nullptr},
		{"name_empty", DREF_NAME, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { b[Get32(b, OFF_NAMEOFFSET) + 8] = 0; }, nullptr},
		{"changed_nan", DREF_CHANGED, BL, c5r::Err::BadLayout, [](std::vector<uint8_t>& b) { SetF(b, Get32(b, OFF_CHANGEDOFFSET) + 12, std::numeric_limits<float>::quiet_NaN()); }, nullptr},
	};
	return v;
}

// Every refusal through the section: acked with its code, counted by reason, the last good set kept (the same object);
// and through the file form (loadImage) where the check is the image's: the Status code and the file counter.
static void CaseRefusals()
{
	const std::vector<Refusal> all = Refusals();
	for(const Refusal& f : all)
	{
		printf("  refusal %s\n", f.name);
		Producer p(FakePid());
		DecalTapReader r(p.pid);
		const std::vector<uint8_t> good = Build(Standard(1));
		CHECK(p.serve(1, good));
		CHECK(r.poll());
		auto before = r.current();
		std::vector<uint8_t> bad = Build(Standard(2));
		if(f.image)
			f.image(bad);
		CHECK(p.serve(2, bad, f.block));
		CHECK(!r.poll());
		const DecalTapCounters c = r.counters();
		if(p.b->ackGeneration != 2 || p.b->ackResult != f.ack || c.refusedBy[f.reason] != 1 || c.refused != 1 ||
		   c.accepted != 1 || r.current() != before || r.lastResult() != f.ack)
		{
			printf("FAIL refusal %s: ackGeneration %u ackResult %d (expected %d), refusedBy[%s] %llu, refused %llu, accepted %llu, "
			       "set kept %d, lastResult %d, why '%s'\n",
			       f.name, p.b->ackGeneration, p.b->ackResult, f.ack, DecalRefusalName(f.reason),
			       (unsigned long long)c.refusedBy[f.reason], (unsigned long long)c.refused, (unsigned long long)c.accepted,
			       r.current() == before, r.lastResult(), r.lastWhy().c_str());
			g_fail++;
		}
		CHECK(DecalAckResult(f.reason) == f.ack);
		// a good generation after the refusal is read
		CHECK(p.serve(3, Build(Standard(3))));
		CHECK(r.poll() && r.generation() == 3);
		if(f.fileErr == c5r::Err::Ok)
			continue;
		DecalTapReader rf(FakePid());
		const c5r::Status st = rf.loadImage(bad.data(), bad.size());
		const DecalTapCounters cf = rf.counters();
		if(st.code != f.fileErr || cf.fileRefused != 1 || cf.refusedBy[f.reason] != 1 || rf.current())
		{
			printf("FAIL refusal %s (file): %s (expected %s), fileRefused %llu, refusedBy %llu\n", f.name, st.str().c_str(),
			       c5r::errName(f.fileErr), (unsigned long long)cf.fileRefused, (unsigned long long)cf.refusedBy[f.reason]);
			g_fail++;
		}
	}
	printf("  %u refusals\n", (unsigned)all.size());
}

// Block-level refusals the table cannot express: stale, open, truncated view; and the file form's own.
static void CaseRefusalsOther()
{
	{   // stale: an older generation after a newer one
		Producer p(FakePid());
		DecalTapReader r(p.pid);
		CHECK(p.serve(5, Build(Standard(5))));
		CHECK(r.poll());
		CHECK(p.serve(4, Build(Standard(4))));
		CHECK(!r.poll());
		CHECK(p.b->ackGeneration == 4 && p.b->ackResult == DTACK_STALE && r.counters().refusedBy[DREF_STALE] == 1 && r.generation() == 5);
	}
	{   // open: the block names a section that does not exist
		Producer p(FakePid());
		DecalTapReader r(p.pid);
		p.publish(1, Build(Standard(1)));
		CHECK(!r.poll());
		CHECK(p.b->ackGeneration == 1 && p.b->ackResult == DTACK_OPEN && r.counters().refusedBy[DREF_OPEN] == 1 && !r.current());
	}
	{   // truncated: the section is smaller than imageBytes
		Producer p(FakePid());
		DecalTapReader r(p.pid);
		std::vector<uint8_t> img = Build(Standard(1));
		CHECK(p.section(1, img, 4096));
		std::vector<uint8_t> big = img;
		big.resize(65536 + 16, 0);   // the block announces more than the 4 KB section
		p.publish(1, big);
		CHECK(!r.poll());
		CHECK(p.b->ackGeneration == 1 && p.b->ackResult == (int32_t)c5r::Err::Truncated &&
		      r.counters().refusedBy[DREF_TRUNCATED] == 1 && !r.current());
	}
	{   // file form: short file, long file, missing file, too big a file, a header-only stub
		const std::vector<uint8_t> img = Build(Standard(1));
		DecalTapReader rf(FakePid());
		std::vector<uint8_t> shortImg(img.begin(), img.end() - 8);
		CHECK(rf.loadImage(shortImg.data(), shortImg.size()).code == c5r::Err::Truncated);
		std::vector<uint8_t> longImg = img;
		longImg.resize(img.size() + 16);
		CHECK(rf.loadImage(longImg.data(), longImg.size()).code == c5r::Err::BadLayout);
		CHECK(rf.loadImage(img.data(), 50).code == c5r::Err::Truncated);
		CHECK(rf.loadImage(nullptr, 0).code == c5r::Err::Truncated);
		CHECK(rf.loadFile(TmpPath(L"does-not-exist.dtap")).code == c5r::Err::Io);
		const DecalTapCounters c = rf.counters();
		CHECK(c.fileRefused == 5 && c.refusedBy[DREF_TRUNCATED] == 3 && c.refusedBy[DREF_DISAGREE] == 1 && c.refusedBy[DREF_OPEN] == 1);
		CHECK(!rf.current());
	}
	{   // the producer's other states are not read and not acked
		Producer p(FakePid());
		DecalTapReader r(p.pid);
		CHECK(p.serve(1, Build(Standard(1)), [](VPShellDecalTapControl& c) { c.state = VPDTAP_FAILED; c.failReason = VPDTAP_FAIL_SEH; }));
		CHECK(!r.poll() && p.b->ackGeneration == 0 && r.counters().refused == 0);
	}
	{   // no producer: nothing happens
		DecalTapReader r(FakePid());
		CHECK(!r.poll() && !r.current() && r.counters().controlOpens == 0);
	}
}

// A writer in the middle of its write (seq odd): the reader counts a torn read, acks nothing and keeps its set; once the
// writer closes the seqlock the generation is read.
static void CaseTorn()
{
	Producer p(FakePid());
	DecalTapReader r(p.pid);
	CHECK(p.serve(1, Build(Standard(1))));
	CHECK(r.poll());
	auto before = r.current();
	const std::vector<uint8_t> img2 = Build(Standard(2));
	CHECK(p.section(2, img2));
	p.publish(2, img2, {}, true);   // left odd
	for(int i = 0; i < 5; ++i)
		CHECK(!r.poll());
	CHECK(r.counters().torn == 5 && p.b->ackGeneration == 1 && r.current() == before);
	InterlockedIncrement((volatile LONG*)&p.b->seq);   // the writer finishes
	CHECK(r.poll() && r.generation() == 2 && p.b->ackGeneration == 2 && p.b->ackResult == 0);

	// a writer thread scribbling garbage under the seqlock while the reader polls: nothing torn is ever trusted
	Producer q(FakePid());
	DecalTapReader rq(q.pid);
	const std::vector<uint8_t> good = Build(Standard(7));
	CHECK(q.section(7, good));
	q.publish(7, good);
	VPShellDecalTapControl valid = *q.b;
	volatile bool stop = false;
	std::thread w([&]() {
		std::mt19937 rng(42);
		while(!stop)
		{
			InterlockedIncrement((volatile LONG*)&q.b->seq);
			MemoryBarrier();
			for(int k = 0; k < 8; ++k)
			{
				q.b->generation = rng();
				q.b->imageBytes = rng();
				q.b->fnv1a64 = rng();
				q.b->imageName[rng() % 40] = (wchar_t)(L'A' + rng() % 26);
				q.b->magic = rng();
			}
			memcpy((uint8_t*)q.b + 4, (const uint8_t*)&valid + 4, offsetof(VPShellDecalTapControl, ackGeneration) - 4);
			MemoryBarrier();
			InterlockedIncrement((volatile LONG*)&q.b->seq);
		}
	});
	const auto t0 = std::chrono::steady_clock::now();
	int polls = 0;
	while(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500))
	{
		rq.poll();
		polls++;
	}
	stop = true;
	w.join();
	const DecalTapCounters c = rq.counters();
	printf("  hammer: %d polls, %llu torn, %llu accepted, %llu refused\n", polls, (unsigned long long)c.torn,
	       (unsigned long long)c.accepted, (unsigned long long)c.refused);
	CHECK(c.refused == 0 && c.accepted == 1 && rq.generation() == 7);
}

static std::vector<std::pair<int, int>> Tiles(const std::vector<TileOrigin>& t)
{
	std::vector<std::pair<int, int>> v;
	for(const TileOrigin& o : t)
		v.push_back({o.x, o.y});
	return v;
}

static std::vector<std::pair<int, int>> ChangeTiles(const DecalChange& c)
{
	return Tiles(c5r::decal::tilesForRects(c.rects.data(), c.rects.size(), c.worldRect));
}

// A generation change raises exactly the tiles its rectangles cover; a skipped generation's rectangles come from the
// content diff; the first set raises the tiles of all its decals.
static void CaseChangesTiles()
{
	Producer p(FakePid());
	DecalTapReader r(p.pid);
	TImage g1;
	g1.generation = 1;
	g1.decals.push_back(D(10, 10, 20, 20, 0.5f, 0, 1));   // tile (0, 0) only
	g1.changed.push_back({10, 10, 20, 20});
	CHECK(p.serve(1, Build(g1)));
	CHECK(r.poll());
	std::vector<DecalChange> ch;
	CHECK(r.drainChanges(ch) && ch.size() == 1);
	CHECK(ch.size() == 1 && ch[0].generation == 1 && ch[0].previousGeneration == 0 && !ch[0].all);
	if(ch.size() == 1)
		CHECK(ChangeTiles(ch[0]) == std::vector<std::pair<int, int>>({{0, 0}}));
	CHECK(!r.drainChanges(ch));
	// generation 2: A removed, B added (x 100..130 -> tiles 64, 128; y 62..64 -> tiles 0, 64 (62 = 64 - 2, inclusive))
	TImage g2;
	g2.generation = 2;
	g2.decals.push_back(D(100, 62, 130, 64, 0.7f, 0, 2));
	g2.changed.push_back({10, 10, 20, 20});
	g2.changed.push_back({100, 62, 130, 64});
	CHECK(p.serve(2, Build(g2)));
	CHECK(r.poll());
	ch.clear();
	CHECK(r.drainChanges(ch) && ch.size() == 1);
	if(ch.size() == 1)
	{
		CHECK(ch[0].previousGeneration == 1 && ch[0].rects.size() == 2);
		const auto t = ChangeTiles(ch[0]);
		CHECK(t == std::vector<std::pair<int, int>>({{0, 0}, {64, 0}, {128, 0}, {64, 64}, {128, 64}}));
		if(t.size() != 5)
			for(auto& x : t)
				printf("  tile %d %d\n", x.first, x.second);
	}
	CHECK(r.counters().rectsNotListed == 0);
	// generations 3 and 4 published before the reader polls: 3 adds C (tile 512, 512), 4 adds E (66.01: tile 64 only in x)
	// and lists only E; the reader sees 4 alone and finds C by the content diff
	TImage g3 = g2;
	g3.generation = 3;
	g3.decals.push_back(D(520, 520, 540, 540, 0.1f, 1, 3));
	g3.changed = {{520, 520, 540, 540}};
	CHECK(p.section(3, Build(g3)));
	p.publish(3, Build(g3));
	TImage g4 = g3;
	g4.generation = 4;
	g4.decals.push_back(D(66.01f, 300, 70, 310, 0.1f, 2, 4));
	g4.changed = {{66.01f, 300, 70, 310}};
	CHECK(p.serve(4, Build(g4)));
	CHECK(r.poll() && r.generation() == 4);
	ch.clear();
	CHECK(r.drainChanges(ch) && ch.size() == 1);
	if(ch.size() == 1)
	{
		CHECK(ch[0].previousGeneration == 2 && ch[0].rects.size() == 2);
		CHECK(ChangeTiles(ch[0]) == std::vector<std::pair<int, int>>({{64, 256}, {512, 512}}));
	}
	CHECK(r.counters().rectsNotListed == 1);
	// the edges: a rectangle ending exactly on a tile's square raises it, 0.01 short does not
	const float world[4] = {0, 0, 1023, 1023};
	DecalRect e1, e2, e3;
	const float a1[4] = {200, 200, 254, 210}, a2[4] = {200, 200, 253.99f, 210}, a3[4] = {-50, -50, 1100, 1100};
	memcpy(e1.v, a1, 16);
	memcpy(e2.v, a2, 16);
	memcpy(e3.v, a3, 16);
	// x 200 .. 254: tile 192 ([190, 258]) and tile 256 ([254, 322], inclusive); not 128 ([126, 194])
	CHECK(Tiles(c5r::decal::tilesForRects(&e1, 1, world)) == std::vector<std::pair<int, int>>({{192, 192}, {256, 192}}));
	CHECK(Tiles(c5r::decal::tilesForRects(&e2, 1, world)) == std::vector<std::pair<int, int>>({{192, 192}}));
	CHECK(c5r::decal::tilesForRects(&e3, 1, world).size() == 256);   // clamped to the world's 16 x 16 tiles
	DecalRect nan;
	nan.v[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(c5r::decal::tilesForRects(&nan, 1, world).empty());
	CHECK(c5r::decal::tilesForRects(nullptr, 0, world).empty());
	// the first set of a reader raises the tiles of all its decals, even when the image lists none
	DecalTapReader r2(FakePid());
	TImage f = g4;
	f.changed.clear();
	std::vector<uint8_t> fb = Build(f);
	CHECK(r2.loadImage(fb.data(), fb.size()).ok());
	ch.clear();
	CHECK(r2.drainChanges(ch) && ch.size() == 1);
	if(ch.size() == 1)
		CHECK(ch[0].previousGeneration == 0 && ch[0].rects.size() == 3 &&
		      ChangeTiles(ch[0]) == std::vector<std::pair<int, int>>({{64, 0}, {128, 0}, {64, 64}, {128, 64}, {64, 256}, {512, 512}}));
	// the queue is bounded: 300 undrained generations collapse into one entry with all = true
	DecalTapReader r3(FakePid());
	for(uint32_t g = 1; g <= 300; ++g)
	{
		TImage t = Standard(g);
		std::vector<uint8_t> b = Build(t);
		CHECK(r3.loadImage(b.data(), b.size()).ok());
	}
	ch.clear();
	CHECK(r3.drainChanges(ch));
	CHECK(ch.size() == 300 - 256 && ch[0].all && r3.counters().changeOverflow == 1);
}

static std::vector<uint32_t> Brute(const DecalSet& s, int ox, int oy)
{
	std::vector<uint32_t> v;
	for(uint32_t i = 0; i < s.decals.size(); ++i)
	{
		const float* r = s.decals[i].rect;
		if(r[2] >= ox - 2 && r[0] <= ox + 66 && r[3] >= oy - 2 && r[1] <= oy + 66)
			v.push_back(i);
	}
	// 4111: equal priorities: small decals newest first, then large ones (a side > 256) in list order
	auto large = [&](uint32_t i) {
		const float* r = s.decals[i].rect;
		return r[2] - r[0] > 256.f || r[3] - r[1] > 256.f;
	};
	std::stable_sort(v.begin(), v.end(), [&](uint32_t a, uint32_t b) {
		if(s.decals[a].priority != s.decals[b].priority)
			return s.decals[a].priority < s.decals[b].priority;
		if(large(a) != large(b))
			return large(b);
		if(s.decals[a].listOrder == s.decals[b].listOrder)
			return false;
		return large(a) ? s.decals[a].listOrder < s.decals[b].listOrder : s.decals[a].listOrder > s.decals[b].listOrder;
	});
	return v;
}

static c5r::decal::Decal Dec(float x0, float y0, float x1, float y1, float prio, uint32_t lo)
{
	c5r::decal::Decal d;
	d.rect[0] = x0;
	d.rect[1] = y0;
	d.rect[2] = x1;
	d.rect[3] = y1;
	d.priority = prio;
	d.listOrder = lo;
	return d;
}

// decalsForTile: order, the square's edges, a decal over many tiles, zero decals; the grid equals the scan and a brute
// force on a random set the size of the huge save's.
static void CaseTileQuery()
{
	using c5r::decal::decalsForTile;
	{   // zero decals, with and without a grid
		DecalSet s;
		CHECK(decalsForTile(s, 0, 0).indices.empty() && decalsForTile(s, 0, 0).set == &s);
		c5r::decal::buildIndex(s);
		CHECK(decalsForTile(s, 0, 0).indices.empty() && decalsForTile(s, 640, 64).indices.empty() &&
		      decalsForTile(s, 13, 7).indices.empty());
	}
	{   // order: ascending priority; equal priorities: small decals newest (highest listOrder) first, then the large ones
		// (a side longer than 256 samples) in listOrder (4111); then index
		DecalSet s;
		s.decals = {Dec(1, 1, 5, 5, 0.7f, 0), Dec(1, 1, 5, 5, 0.0f, 5), Dec(1, 1, 5, 5, 0.5f, 9), Dec(1, 1, 5, 5, 0.5f, 3),
		            Dec(1, 1, 5, 5, 0.5f, 3), Dec(1, 1, 5, 5, 0.1f, 1), Dec(0, 0, 300, 10, 0.5f, 8), Dec(0, 0, 10, 257, 0.5f, 2),
		            Dec(0, 0, 256, 256, 0.5f, 1)};   // 6, 7 large (300 wide, 257 high); 8 exactly 256: small
		for(int g = 0; g < 2; ++g)
		{
			CHECK(decalsForTile(s, 0, 0).indices == std::vector<uint32_t>({1, 5, 2, 3, 4, 8, 7, 6, 0}));
			c5r::decal::buildIndex(s);
		}
	}
	{   // the square's edges, inclusive: [origin - 2, origin + 66]
		DecalSet s;
		s.decals = {Dec(10, 10, 62, 20, 0, 0),      // max x 62 = 64 - 2: in tiles x 0 and 64
		            Dec(10, 10, 61.99f, 20, 0, 1),  // in tile x 0 only
		            Dec(66, 10, 70, 20, 0, 2),      // min x 66 = 0 + 66: in tiles x 0 and 64
		            Dec(66.01f, 10, 70, 20, 0, 3),  // in tile x 64 only
		            Dec(126, 126, 126, 126, 0, 4),  // a point: tiles x 64, 128 and y 64, 128
		            Dec(0, 0, 1000, 100, 0, 5)};    // many tiles: x 0 .. 960, y 0 .. 64
		for(int g = 0; g < 2; ++g)
		{
			CHECK(decalsForTile(s, 0, 0).indices == std::vector<uint32_t>({2, 1, 0, 5}));    // small newest first, then 5 (large)
			CHECK(decalsForTile(s, 64, 0).indices == std::vector<uint32_t>({3, 2, 0, 5}));
			CHECK(decalsForTile(s, 64, 64).indices == std::vector<uint32_t>({4, 5}));
			CHECK(decalsForTile(s, 128, 128).indices == std::vector<uint32_t>({4}));
			CHECK(decalsForTile(s, 192, 128).indices.empty());
			CHECK(decalsForTile(s, 960, 64).indices == std::vector<uint32_t>({5}));
			CHECK(decalsForTile(s, 1024, 64).indices.empty());
			CHECK(decalsForTile(s, 960, 128).indices.empty());
			CHECK(decalsForTile(s, -64, 0).indices == std::vector<uint32_t>({5}));   // [-66, 2] holds x 0 (outside the map: never baked)
			CHECK(decalsForTile(s, -64000, 99999).indices.empty());
			int many = 0;
			for(int y = 0; y < 1024; y += 64)
				for(int x = 0; x < 1024; x += 64)
				{
					const std::vector<uint32_t> ix = decalsForTile(s, x, y).indices;
					many += std::count(ix.begin(), ix.end(), 5u) ? 1 : 0;
				}
			CHECK(many == 16 * 2);
			c5r::decal::buildIndex(s);
			CHECK(s.grid.cols > 0);
		}
	}
	{   // a NaN or infinite rectangle: no grid (scan), the NaN in no tile
		DecalSet s;
		s.decals = {Dec(std::numeric_limits<float>::quiet_NaN(), 0, 5, 5, 0, 0), Dec(1, 1, 5, 5, 0, 1)};
		c5r::decal::buildIndex(s);
		CHECK(s.grid.cols == 0 && decalsForTile(s, 0, 0).indices == std::vector<uint32_t>({1}));
		s.decals.push_back(Dec(-std::numeric_limits<float>::infinity(), 0, std::numeric_limits<float>::infinity(), 5, -1, 2));
		c5r::decal::buildIndex(s);
		CHECK(s.grid.cols == 0 && decalsForTile(s, 6400, 0).indices == std::vector<uint32_t>({2}));
	}
	{   // random: the huge save's size (17,800 decals on 3,584 x 2,560 samples), grid = scan = brute force
		std::mt19937 rng(4103);
		DecalSet s;
		s.worldRect[2] = 3583;
		s.worldRect[3] = 2559;
		const float prios[] = {0.0f, 0.1f, 0.4f, 0.5f, 0.6f, 0.7f};
		for(uint32_t i = 0; i < 17800; ++i)
		{
			float x = (float)(rng() % 3584), y = (float)(rng() % 2560);
			if(rng() % 4 == 0)   // on a square's edge
			{
				x = (float)((rng() % 56) * 64 + ((rng() & 1) ? 66 : -2));
				y = (float)((rng() % 40) * 64 + ((rng() & 1) ? 66 : -2));
			}
			x = (std::min)(x, 3583.f);
			y = (std::min)(y, 2559.f);
			const float w = (rng() % 10 == 0) ? (float)(rng() % 400) : (float)(rng() % 40) + (rng() % 100) / 100.f;
			const float h = (rng() % 10 == 0) ? (float)(rng() % 300) : (float)(rng() % 40);
			s.decals.push_back(Dec(x, y, (std::min)(x + w, 3583.f), (std::min)(y + h, 2559.f), prios[rng() % 6], rng() % 5000));
		}
		DecalSet scan = s;
		const auto t0 = std::chrono::steady_clock::now();
		c5r::decal::buildIndex(s);
		const auto t1 = std::chrono::steady_clock::now();
		CHECK(s.grid.cols == 57 || s.grid.cols == 58);
		size_t total = 0, mismatches = 0;
		double gridUs = 0;
		for(int y = 0; y < 2560; y += 64)
			for(int x = 0; x < 3584; x += 64)
			{
				const auto q0 = std::chrono::steady_clock::now();
				const auto g = decalsForTile(s, x, y).indices;
				gridUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - q0).count();
				total += g.size();
				if(g != decalsForTile(scan, x, y).indices || g != Brute(s, x, y))
					mismatches++;
			}
		for(int k = 0; k < 200; ++k)   // non-aligned origins: the scan
		{
			const int x = (int)(rng() % 3584) - 10, y = (int)(rng() % 2560) - 10;
			if(decalsForTile(s, x, y).indices != Brute(s, x, y))
				mismatches++;
		}
		printf("  random: 17,800 decals, grid %d x %d, %zu items, built in %.2f ms; 2,240 tiles, %zu entries, %.2f us per tile "
		       "(indication); %zu mismatches\n",
		       s.grid.cols, s.grid.rows, s.grid.items.size(), std::chrono::duration<double, std::milli>(t1 - t0).count(), total,
		       gridUs / 2240, mismatches);
		CHECK(mismatches == 0 && total > 17800);
	}
}

// Random byte changes of a good image through the file form: refused or accepted, never a crash; an accepted set's
// references are all inside it.
static void CaseFuzz()
{
	const std::vector<uint8_t> good = Build(Standard(1));
	std::mt19937 rng(7);
	DecalTapReader r(FakePid());
	int accepted = 0, refused = 0, inconsistent = 0;
	g_quiet = true;   // 20,000 refusal lines
	for(int it = 0; it < 20000; ++it)
	{
		std::vector<uint8_t> b = good;
		const int flips = 1 + (int)(rng() % 6);
		for(int k = 0; k < flips; ++k)
		{
			const size_t at = (rng() % 3) ? rng() % 112 + 0 : rng() % b.size();   // mostly the header
			b[(std::min)(at, b.size() - 1)] = (uint8_t)rng();
		}
		if(rng() % 8 == 0)
			b.resize(rng() % (b.size() + 64));
		const c5r::Status st = r.loadImage(b.data(), b.size());
		if(!st.ok())
		{
			refused++;
			continue;
		}
		accepted++;
		auto s = r.current();
		for(const auto& d : s->decals)
		{
			if((uint64_t)d.firstVertex + d.vertexCount > s->vertices.size())
				inconsistent++;
			for(int k = 0; k < 3; ++k)
				if(d.texture[k] != c5r::decal::kNoTexture && d.texture[k] >= s->textures.size())
					inconsistent++;
		}
		std::vector<DecalChange> ch;
		r.drainChanges(ch);
	}
	g_quiet = false;
	printf("  fuzz: %d accepted, %d refused, %d inconsistent\n", accepted, refused, inconsistent);
	CHECK(inconsistent == 0 && refused > 1000 && accepted + refused == 20000);
}

// 4111: the area block (the EXE's tile+0x300 per tile): read and exposed, its changes per generation (bits only, a set
// without the block after one with it), an image without it (4102's layout) keeps the server's rule; negatives: the
// block past the image's end, a bit past the map's tiles, a grid that is not the world's, a zero count, an overlap.
static std::vector<std::pair<int, int>> AreaTiles(const DecalChange& c)
{
	std::vector<std::pair<int, int>> v;
	for(const TileOrigin& t : c.areaTiles)
		v.push_back({t.x, t.y});
	return v;
}

static void CaseArea()
{
	using c5r::decal::areaChangedAt;
	Producer p(FakePid());
	DecalTapReader r(p.pid);
	TImage g1;
	g1.generation = 1;
	g1.decals.push_back(D(10, 10, 20, 20, 0.5f, 0, 1));   // the rule's tile: (0, 0)
	g1.changed.push_back({10, 10, 20, 20});
	g1.areaX = g1.areaY = 16;                             // the world 0 .. 1023: 16 x 16 tiles
	g1.areaBits = {{0, 0}, {3, 2}, {15, 15}};
	CHECK(p.serve(1, Build(g1)));
	CHECK(r.poll() && p.b->ackResult == 0);
	auto s = r.current();
	CHECK(s && s->areaTilesX == 16 && s->areaTilesY == 16 && s->areaChanged.size() == 256);
	if(!s)
		return;
	bool known = false;
	CHECK(areaChangedAt(*s, 3, 2, known) && known);
	CHECK(!areaChangedAt(*s, 2, 3, known) && known);
	CHECK(areaChangedAt(*s, 15, 15, known) && known);
	CHECK(!areaChangedAt(*s, 16, 0, known) && !known);     // outside the block: not known
	CHECK(!areaChangedAt(*s, -1, 0, known) && !known);
	CHECK(DecalAreaAt(s.get(), 15, 15) && !DecalAreaAt(s.get(), 1, 0) && !DecalAreaAt(nullptr, 0, 0));
	CHECK(r.counters().areaSets == 1 && r.counters().areaSet == 3);
	std::vector<DecalChange> ch;
	CHECK(r.drainChanges(ch) && ch.size() == 1);
	if(ch.size() == 1)   // the first set: every tile with the bit (nothing before)
		CHECK(AreaTiles(ch[0]) == std::vector<std::pair<int, int>>({{0, 0}, {3, 2}, {15, 15}}));
	// generation 2: the same decals, only bits changed ((3, 2) cleared, (5, 5) set): no rectangle, two tiles
	TImage g2 = g1;
	g2.generation = 2;
	g2.changed.clear();
	g2.areaBits = {{0, 0}, {5, 5}, {15, 15}};
	CHECK(p.serve(2, Build(g2)));
	CHECK(r.poll());
	ch.clear();
	CHECK(r.drainChanges(ch) && ch.size() == 1);
	if(ch.size() == 1)
		CHECK(ch[0].rects.empty() && AreaTiles(ch[0]) == std::vector<std::pair<int, int>>({{3, 2}, {5, 5}}));
	CHECK(r.counters().areaTilesChanged == 5);
	// generation 3: an image without the block (4102's layout): the rule again (the decal's tile (0, 0)); (5, 5) and
	// (15, 15) lose their bit, (0, 0) keeps its state
	TImage g3 = g2;
	g3.generation = 3;
	g3.areaX = g3.areaY = 0;
	CHECK(p.serve(3, Build(g3)));
	CHECK(r.poll());
	s = r.current();
	CHECK(s && s->areaTilesX == 0 && s->areaChanged.empty());
	if(s)
	{
		CHECK(!areaChangedAt(*s, 0, 0, known) && !known);
		CHECK(DecalAreaAt(s.get(), 0, 0) && !DecalAreaAt(s.get(), 5, 5));
	}
	ch.clear();
	CHECK(r.drainChanges(ch) && ch.size() == 1);
	if(ch.size() == 1)
		CHECK(AreaTiles(ch[0]) == std::vector<std::pair<int, int>>({{5, 5}, {15, 15}}));
	// the file form carries it the same way
	DecalTapReader rf(FakePid());
	std::vector<uint8_t> b1 = Build(g1);
	CHECK(rf.loadImage(b1.data(), b1.size()).ok());
	CHECK(rf.current() && rf.current()->areaTilesX == 16 && rf.current()->areaChanged[2 * 16 + 3] == 1);
	const std::wstring f = TmpPath(L"area0001.dtap");
	CHECK(rf.writeCurrentImage(f).ok() && ReadFileBytes(f) == b1);
	DecalTapReader rf2(FakePid());
	CHECK(rf2.loadFile(f).ok() && rf2.current() && rf2.current()->areaChanged.size() == 256 &&
	      rf2.current()->areaChanged[2 * 16 + 3] == 1 && rf2.current()->areaChanged[3 * 16 + 2] == 0);
	DeleteFileW(f.c_str());

	// negatives, through the file form (Status code, counter) and once through the section (ackResult)
	struct Bad
	{
		const char* name;
		c5r::Err err;
		uint32_t reason;
		std::function<void(TImage&)> image;
		std::function<void(std::vector<uint8_t>&)> bytes;
	};
	const std::vector<Bad> bad = {
		{"area_past_the_end", c5r::Err::Truncated, DREF_OFFSET, nullptr,
		 [](std::vector<uint8_t>& b) { b.resize(b.size() - 8); uint64_t n = b.size(); memcpy(&b[OFF_IMAGEBYTES], &n, 8); }},
		{"area_grid_wider_than_the_world", c5r::Err::BadLayout, DREF_AREA, [](TImage& t) { t.areaX = 17; }, nullptr},
		{"area_grid_shorter_than_the_world", c5r::Err::BadLayout, DREF_AREA, [](TImage& t) { t.areaY = 15; }, nullptr},
		{"area_zero_count", c5r::Err::BadLayout, DREF_AREA, nullptr, [](std::vector<uint8_t>& b) { Set32(b, OFF_AREAY, 0); }},
		{"area_offset_without_counts", c5r::Err::BadLayout, DREF_AREA, [](TImage& t) { t.areaX = 0; },
		 [](std::vector<uint8_t>& b) { Set32(b, OFF_AREAOFFSET, Get32(b, OFF_CHANGEDOFFSET)); }},
		{"area_overlaps_changed", c5r::Err::BadLayout, DREF_ALIGN, nullptr,
		 [](std::vector<uint8_t>& b) { Set32(b, OFF_AREAOFFSET, Get32(b, OFF_CHANGEDOFFSET)); }},
		{"area_misaligned", c5r::Err::BadLayout, DREF_ALIGN, nullptr,
		 [](std::vector<uint8_t>& b) { Set32(b, OFF_AREAOFFSET, Get32(b, OFF_AREAOFFSET) - 8); }},
		{"area_bit_past_the_map", c5r::Err::BadLayout, DREF_AREA, [](TImage& t) {
			 t.world[2] = 5 * 64 - 1;   // 5 x 3 tiles = 15 bits: bit 15 of the second byte is past the map
			 t.world[3] = 3 * 64 - 1;
			 t.areaX = 5;
			 t.areaY = 3;
			 t.areaBits = {{4, 2}};
		 },
		 [](std::vector<uint8_t>& b) { b[Get32(b, OFF_AREAOFFSET) + 1] |= 0x80; }},
	};
	uint64_t refusedArea = 0;
	for(const Bad& x : bad)
	{
		TImage t = g1;
		if(x.image)
			x.image(t);
		std::vector<uint8_t> b = Build(t);
		if(x.bytes)
			x.bytes(b);
		DecalTapReader rb(FakePid());
		const c5r::Status st = rb.loadImage(b.data(), b.size());
		const bool ok = !st.ok() && st.code == x.err && rb.counters().refusedBy[x.reason] == 1 && !rb.current();
		if(!ok)
			printf("  area negative %s: %s (%s), refusedBy %llu\n", x.name, st.str().c_str(), rb.lastWhy().c_str(),
			       (unsigned long long)rb.counters().refusedBy[x.reason]);
		CHECK(ok);
		refusedArea += x.reason == DREF_AREA;
		// the same bytes through the section: acked with the code, the last good set stays
		Producer pb(FakePid());
		DecalTapReader rs(pb.pid);
		CHECK(pb.serve(1, Build(g1)) && rs.poll());
		const auto keep = rs.current();
		VPShellDecalImage* h = (VPShellDecalImage*)b.data();
		h->generation = 2;
		CHECK(pb.serve(2, b) && !rs.poll());
		CHECK(pb.b->ackGeneration == 2 && pb.b->ackResult == DecalAckResult(x.reason) && rs.current() == keep);
	}
	CHECK(refusedArea == 5);
	CHECK(strcmp(DecalRefusalName(DREF_AREA), "area") == 0 && DecalAckResult(DREF_AREA) == (int32_t)c5r::Err::BadLayout);
	// the good 5 x 3 image (bit 14 = tile (4, 2) set, no padding bit) is accepted
	TImage t5 = g1;
	t5.world[2] = 5 * 64 - 1;
	t5.world[3] = 3 * 64 - 1;
	t5.areaX = 5;
	t5.areaY = 3;
	t5.areaBits = {{4, 2}, {0, 0}};
	t5.decals.clear();
	t5.changed.clear();
	std::vector<uint8_t> b5 = Build(t5);
	DecalTapReader r5(FakePid());
	CHECK(r5.loadImage(b5.data(), b5.size()).ok() && r5.current() && r5.current()->areaChanged[14] == 1 &&
	      r5.current()->areaChanged[0] == 1 && r5.counters().areaSet == 2);
}

// The runner's own negative: a failing check makes the case FAIL with exit 1.
// ---- 0366: the walk block and the EXE's own order (terrain-decals.md 18) ---------------------------------------------
// Emu is the EXE's index and large list written from 18.1 / 18.2 as lists (not the reader's arrays): an add puts a small
// decal (both sides at most 256) at the head of the list of the cell of its rectangle's centre, a large one at the large
// list's tail; a remove unlinks. RefSelect walks those lists as 18.3 says. Both in single precision as the EXE.
static float Rf(float v)
{
	volatile float r = v;
	return r;
}
static int32_t Trunc32(float v) { return (v > -2147483648.0f && v < 2147483648.0f) ? (int32_t)v : (std::numeric_limits<int32_t>::min)(); }

struct Emu
{
	float origin[2] = {0, 0}, inv[2] = {0, 0};
	uint32_t cols = 0, rows = 0;
	std::vector<std::vector<int>> cells;   // decal ids, head first
	std::vector<int> large;                // ids, head first
	std::map<int, std::array<float, 4>> rects;
	void init(const float world[4], uint32_t c, uint32_t r)
	{
		cols = c;
		rows = r;
		origin[0] = world[0];
		origin[1] = world[1];
		inv[0] = Rf((float)c / Rf(world[2] - world[0]));
		inv[1] = Rf((float)r / Rf(world[3] - world[1]));
		cells.assign((size_t)c * r, {});
		large.clear();
		rects.clear();
	}
	static bool IsLarge(const std::array<float, 4>& r) { return Rf(r[2] - r[0]) > 256.0f || Rf(r[3] - r[1]) > 256.0f; }
	static float Centre(float a, float b) { return Rf(Rf(a + b) * 0.5f); }
	int32_t Cell1(float p, int a) const
	{
		int32_t c = Trunc32(Rf(Rf(p - origin[a]) * inv[a]));
		const int32_t n = (int32_t)(a ? rows : cols);
		if(c > n - 1)
			c = n - 1;
		return c < 0 ? 0 : c;
	}
	uint32_t CellOf(const std::array<float, 4>& r) const
	{
		return (uint32_t)Cell1(Centre(r[1], r[3]), 1) * cols + (uint32_t)Cell1(Centre(r[0], r[2]), 0);
	}
	void add(int id, const std::array<float, 4>& r)
	{
		rects[id] = r;
		if(IsLarge(r))
			large.push_back(id);
		else
		{
			auto& l = cells[CellOf(r)];
			l.insert(l.begin(), id);
		}
	}
	void remove(int id)
	{
		for(auto& l : cells)
			l.erase(std::remove(l.begin(), l.end(), id), l.end());
		large.erase(std::remove(large.begin(), large.end(), id), large.end());
		rects.erase(id);
	}
	// the walk block of an image whose records are the decals `ids` (image order)
	void publish(TImage& t, const std::vector<int>& ids) const
	{
		t.walk = true;
		memcpy(t.wOrigin, origin, 8);
		memcpy(t.wInv, inv, 8);
		t.wCols = cols;
		t.wRows = rows;
		t.wCellRank.assign(ids.size(), {0, 0});
		t.wPoint.assign(ids.size(), {0, 0});
		for(size_t i = 0; i < ids.size(); ++i)
		{
			const int id = ids[i];
			auto it = std::find(large.begin(), large.end(), id);
			if(it != large.end())
			{
				t.wCellRank[i] = {VPSHELL_DECAL_WALK_LARGE, (uint32_t)(it - large.begin())};
				continue;
			}
			const std::array<float, 4>& r = rects.at(id);
			const uint32_t c = CellOf(r);
			const auto& l = cells[c];
			t.wCellRank[i] = {c, (uint32_t)(std::find(l.begin(), l.end(), id) - l.begin())};
			t.wPoint[i] = {Centre(r[0], r[2]), Centre(r[1], r[3])};
		}
	}
};

// RefSelect: 18.3's walk on the Emu lists for one pass (ids)
static std::vector<int> RefSelect(const Emu& m, int ox, int oy, float worldMaxX, const std::map<int, uint32_t>& passOf, uint32_t pass)
{
	std::vector<int> out;
	const int e = (double)ox >= std::floor((double)worldMaxX / 64) * 64 ? 64 : 65;
	const float tmin[2] = {(float)(ox - 1), (float)(oy - 1)}, tmax[2] = {(float)(ox + e), (float)(oy + e)};
	const float qmin[2] = {Rf(tmin[0] - 256.f), Rf(tmin[1] - 256.f)}, qmax[2] = {Rf(tmax[0] + 256.f), Rf(tmax[1] + 256.f)};
	int32_t c0[2], c1[2];
	for(int a = 0; a < 2; ++a)
	{
		c0[a] = (std::max)(0, Trunc32(Rf(Rf(qmin[a] - m.origin[a]) * m.inv[a])));
		const int32_t hi = Trunc32(Rf(Rf(qmax[a] - m.origin[a]) * m.inv[a]));
		c1[a] = (std::min)((int32_t)(a ? m.rows : m.cols), (std::max)(0, hi == (std::numeric_limits<int32_t>::min)() ? hi : hi + 1));
	}
	auto keep = [&](int id) {
		const auto& r = m.rects.at(id);
		return passOf.at(id) == pass && r[0] <= tmax[0] && tmin[0] <= r[2] && r[1] <= tmax[1] && tmin[1] <= r[3];
	};
	int32_t x = c0[0], y = c0[1];
	for(;;)
	{
		if(x >= 0 && y >= 0 && x < (int32_t)m.cols && y < (int32_t)m.rows)
			for(int id : m.cells[(size_t)y * m.cols + x])
			{
				const auto& r = m.rects.at(id);
				const float px = Emu::Centre(r[0], r[2]), py = Emu::Centre(r[1], r[3]);
				if(px >= qmin[0] && px < qmax[0] && py >= qmin[1] && py < qmax[1] && keep(id))
					out.push_back(id);
			}
		if(++x >= c1[0])
		{
			x = c0[0];
			if(++y >= c1[1])
				break;
		}
	}
	for(int id : m.large)
		if(keep(id))
			out.push_back(id);
	return out;
}

static TDecal DR(const std::array<float, 4>& r, float prio, uint32_t listOrder, uint32_t seed, uint32_t pass = 0)
{
	TDecal d = D(r[0], r[1], r[2], r[3], prio, listOrder, seed);
	d.flags = pass;
	return d;
}

static std::shared_ptr<const DecalSet> LoadSet(DecalTapReader& r, const TImage& t, c5r::Err want = c5r::Err::Ok)
{
	const std::vector<uint8_t> b = Build(t);
	const c5r::Status st = r.loadImage(b.data(), b.size());
	CHECK(st.code == want);
	return r.current();
}

static void CaseWalk()
{
	using c5r::decal::ExeDrawEntry;
	using c5r::decal::decalsForTile;
	using c5r::decal::exeSelection;
	using c5r::decal::exeSortByPriority;
	using c5r::decal::hasWalk;
	g_quiet = true;
	// (1) the sort, a hand-worked case (18.3): a0..a31 at 0.5 then b at 0.1. Median of (0, 16, 32): a16 and b swap, then
	// b and a0; the pivot run grows down to index 1 and up to the end; the left scan passes b: [1, 33). Result: b, a1..a15,
	// a0, a17..a31, a16 (a stable sort would keep a0..a31).
	{
		std::vector<ExeDrawEntry> e;
		for(uint32_t i = 0; i < 32; ++i)
			e.push_back({i, 0.5f});
		e.push_back({100, 0.1f});
		exeSortByPriority(e.data(), e.data() + e.size());
		std::vector<uint32_t> got, want = {100};
		for(const auto& x : e)
			got.push_back(x.decal);
		for(uint32_t i = 1; i < 16; ++i)
			want.push_back(i);
		want.push_back(0);
		for(uint32_t i = 17; i < 32; ++i)
			want.push_back(i);
		want.push_back(16);
		CHECK(got == want);
	}
	// properties: sorted, a permutation; 32 or fewer = a stable sort; all equal = unchanged (any size)
	{
		std::mt19937 rng(366);
		for(int it = 0; it < 4000; ++it)
		{
			const uint32_t n = (uint32_t)(rng() % 90);
			std::vector<ExeDrawEntry> e(n);
			for(uint32_t i = 0; i < n; ++i)
				e[i] = {i, (float)(rng() % 5) * 0.1f};
			std::vector<ExeDrawEntry> st = e;
			std::stable_sort(st.begin(), st.end(), [](const ExeDrawEntry& a, const ExeDrawEntry& b) { return a.priority < b.priority; });
			exeSortByPriority(e.data(), e.data() + e.size());
			bool sorted = true, perm = true, stable = true;
			std::vector<uint8_t> seen(n, 0);
			for(uint32_t i = 0; i < n; ++i)
			{
				if(i && e[i].priority < e[i - 1].priority)
					sorted = false;
				if(e[i].decal >= n || seen[e[i].decal]++)
					perm = false;
				if(e[i].decal != st[i].decal)
					stable = false;
			}
			CHECK(sorted && perm);
			if(n <= 32)
				CHECK(stable);
		}
		std::vector<ExeDrawEntry> eq(200);
		for(uint32_t i = 0; i < 200; ++i)
			eq[i] = {i, 0.6f};
		exeSortByPriority(eq.data(), eq.data() + eq.size());
		bool same = true;
		for(uint32_t i = 0; i < 200; ++i)
			same = same && eq[i].decal == i;
		CHECK(same);
	}
	const float world[4] = {0, 0, 1023, 1023};   // 16 x 16 tiles; the index 4 x 4 cells of 255.75 samples
	std::map<int, uint32_t> passOf;
	// (2) ties across cells: P's centre in cell (1, 0), Q's in cell (0, 1), both on tile (64, 64): rows outer, so P then
	// Q, against the 4111 rule's Q then P (Q has the higher listOrder)
	{
		Emu m;
		m.init(world, 4, 4);
		const std::array<float, 4> rP = {129, 70, 385, 90}, rQ = {70, 129, 90, 385};
		m.add(1, rQ);
		m.add(0, rP);
		TImage t;
		memcpy(t.world, world, 16);
		t.decals = {DR(rP, 0.5f, 0, 1), DR(rQ, 0.5f, 1, 2)};
		m.publish(t, {0, 1});
		CHECK(m.CellOf(rP) == 1 && m.CellOf(rQ) == 4);
		DecalTapReader r(FakePid());
		auto s = LoadSet(r, t);
		CHECK(s && hasWalk(*s));
		if(s)
		{
			CHECK(decalsForTile(*s, 64, 64).indices == std::vector<uint32_t>({0, 1}));
			DecalSet noWalk = *s;   // the same records without the walk: the 4111 rule (newest list order first)
			noWalk.walk = c5r::decal::DecalWalk();
			c5r::decal::buildIndex(noWalk);
			CHECK(!hasWalk(noWalk) && decalsForTile(noWalk, 64, 64).indices == std::vector<uint32_t>({1, 0}));
		}
	}
	// (3) more than 32 decals on a tile: the hand-worked case through the walk (b added first: the end of its cell's list)
	{
		Emu m;
		m.init(world, 4, 4);
		TImage t;
		memcpy(t.world, world, 16);
		std::vector<int> ids;
		const std::array<float, 4> rb = {10, 10, 30, 30};
		m.add(100, rb);
		for(int i = 31; i >= 0; --i)
			m.add(i, {(float)(5 + i), 8, (float)(25 + i), 28});
		for(int i = 0; i < 32; ++i)
		{
			t.decals.push_back(DR({(float)(5 + i), 8, (float)(25 + i), 28}, 0.5f, (uint32_t)i, (uint32_t)i + 1));
			ids.push_back(i);
		}
		t.decals.push_back(DR(rb, 0.1f, 32, 99));
		ids.push_back(100);
		m.publish(t, ids);
		DecalTapReader r(FakePid());
		auto s = LoadSet(r, t);
		CHECK(s && hasWalk(*s));
		if(s)
		{
			std::vector<uint32_t> want = {32};
			for(uint32_t i = 1; i < 16; ++i)
				want.push_back(i);
			want.push_back(0);
			for(uint32_t i = 17; i < 32; ++i)
				want.push_back(i);
			want.push_back(16);
			CHECK(decalsForTile(*s, 0, 0).indices == want);
		}
	}
	// (4) an add after a removal: A, B, C added, B removed, D added into an earlier batch (list order 0): the cell's list
	// is D, C, A (the 4111 rule by list order would give C, A, D); (5) two large decals whose list order is the reverse of
	// their add order come after the small ones in add order; (6) a pass-1 decal is sorted on its own and merged by priority
	{
		Emu m;
		m.init(world, 4, 4);
		const std::array<float, 4> rA = {10, 10, 40, 40}, rB = {12, 12, 42, 42}, rC = {14, 14, 44, 44}, rD = {16, 16, 46, 46};
		const std::array<float, 4> rL1 = {0, 0, 600, 50}, rL2 = {0, 0, 50, 700}, rS = {20, 20, 30, 30};
		m.add(1, rA);
		m.add(2, rB);
		m.add(3, rC);
		m.add(7, rL1);
		m.add(8, rL2);
		m.remove(2);
		m.add(4, rD);
		m.add(9, rS);
		TImage t;
		memcpy(t.world, world, 16);
		t.decals = {DR(rD, 0.5f, 0, 1), DR(rA, 0.5f, 1, 2), DR(rC, 0.5f, 2, 3), DR(rL2, 0.5f, 3, 4), DR(rL1, 0.5f, 4, 5),
		            DR(rS, 0.4f, 5, 6, 1)};
		m.publish(t, {4, 1, 3, 8, 7, 9});
		DecalTapReader r(FakePid());
		auto s = LoadSet(r, t);
		CHECK(s && hasWalk(*s));
		if(s)
		{
			// D, C, A (the cell's list), then L1, L2 (the large list), the pass-1 decal (0.4) first by priority
			CHECK(decalsForTile(*s, 0, 0).indices == std::vector<uint32_t>({5, 0, 2, 1, 4, 3}));
			CHECK(exeSelection(*s, 0, 0, 1).size() == 1 && exeSelection(*s, 0, 0, 0).size() == 5);
		}
	}
	// (7) the EXE's tile rectangle: [o - 1, o + 65] (64 in the last column, both axes), narrower than the 4111 square
	// [o - 2, o + 66]: a decal ending at o - 1.5 is not drawn, one ending at o - 1 is; in the last column a decal from
	// y = 64.5 is not drawn on tile (960, 0), but is on (896, 0)
	{
		Emu m;
		m.init(world, 4, 4);
		const std::array<float, 4> r0 = {40, 100, 62.5f, 110}, r1 = {40, 100, 63, 110}, r2 = {900, 64.5f, 1000, 80};
		m.add(0, r0);
		m.add(1, r1);
		m.add(2, r2);
		TImage t;
		memcpy(t.world, world, 16);
		t.decals = {DR(r0, 0.5f, 0, 1), DR(r1, 0.5f, 1, 2), DR(r2, 0.5f, 2, 3)};
		m.publish(t, {0, 1, 2});
		DecalTapReader r(FakePid());
		auto s = LoadSet(r, t);
		CHECK(s && hasWalk(*s));
		if(s)
		{
			CHECK(decalsForTile(*s, 64, 64).indices == std::vector<uint32_t>({1}));
			CHECK(decalsForTile(*s, 960, 0).indices.empty());
			CHECK(decalsForTile(*s, 896, 0).indices == std::vector<uint32_t>({2}));
			DecalSet noWalk = *s;
			noWalk.walk = c5r::decal::DecalWalk();
			c5r::decal::buildIndex(noWalk);
			CHECK(decalsForTile(noWalk, 64, 64).indices.size() == 2);   // the 4111 square takes both
		}
	}
	// (8) random sets: adds and removes (some large, some pass 1), then on every tile the reader's walk against the lists
	{
		std::mt19937 rng(3660);
		for(int round = 0; round < 6; ++round)
		{
			Emu m;
			const uint32_t cols = 1 + rng() % 9, rows = 1 + rng() % 7;
			m.init(world, cols, rows);
			passOf.clear();
			std::vector<int> live;
			for(int op = 0; op < 400; ++op)
			{
				if(!live.empty() && rng() % 4 == 0)
				{
					const size_t k = rng() % live.size();
					m.remove(live[k]);
					live.erase(live.begin() + (long)k);
					continue;
				}
				const float x0 = (float)(rng() % 1000), y0 = (float)(rng() % 1000);
				const bool big = rng() % 15 == 0;
				const float w = big ? 257.0f + (float)(rng() % 300) : 1.0f + (float)(rng() % 120) + 0.25f;
				const float h = 1.0f + (float)(rng() % 120) + 0.5f;
				const std::array<float, 4> rr = {x0, y0, (std::min)(1023.0f, x0 + w), (std::min)(1023.0f, y0 + h)};
				m.add(op, rr);
				passOf[op] = rng() % 10 == 0 ? 1u : 0u;
				live.push_back(op);
			}
			TImage t;
			memcpy(t.world, world, 16);
			std::vector<int> ids = live;
			std::shuffle(ids.begin(), ids.end(), rng);   // image order is not add order
			for(size_t i = 0; i < ids.size(); ++i)
				t.decals.push_back(DR(m.rects[ids[i]], (float)(rng() % 3) * 0.1f, (uint32_t)i, (uint32_t)i + 1, passOf[ids[i]]));
			m.publish(t, ids);
			DecalTapReader r(FakePid());
			auto s = LoadSet(r, t);
			CHECK(s && hasWalk(*s));
			if(!s)
				continue;
			int bad = 0;
			for(int oy = 0; oy < 1024; oy += 64)
				for(int ox = 0; ox < 1024; ox += 64)
					for(uint32_t pass = 0; pass < 2; ++pass)
					{
						std::vector<int> want = RefSelect(m, ox, oy, world[2], passOf, pass), got;
						for(const ExeDrawEntry& e : exeSelection(*s, ox, oy, pass))
							got.push_back(ids[e.decal]);
						if(got != want)
							bad++;
					}
			CHECK(bad == 0);
			// and decalsForTile is the two sorted selections merged: per pass the order of exeSortByPriority
			for(int oy = 0; oy < 1024; oy += 192)
				for(int ox = 0; ox < 1024; ox += 128)
				{
					std::vector<uint32_t> list = decalsForTile(*s, ox, oy).indices, p[2];
					for(uint32_t k : list)
						p[s->decals[k].pass].push_back(k);
					for(uint32_t pass = 0; pass < 2; ++pass)
					{
						std::vector<ExeDrawEntry> e = exeSelection(*s, ox, oy, pass);
						exeSortByPriority(e.data(), e.data() + e.size());
						std::vector<uint32_t> want;
						for(const auto& x : e)
							want.push_back(x.decal);
						CHECK(p[pass] == want);
					}
					for(size_t i = 1; i < list.size(); ++i)
						CHECK(!(s->decals[list[i]].priority < s->decals[list[i - 1]].priority));
				}
		}
	}
	// (9) refusals of the walk block (BadLayout, DREF_WALK; past the image Truncated): the last good set stays
	{
		Emu m;
		m.init(world, 4, 4);
		m.add(0, {10, 10, 20, 20});
		m.add(1, {12, 12, 22, 22});
		m.add(2, {0, 0, 600, 40});
		TImage good;
		memcpy(good.world, world, 16);
		good.decals = {DR({10, 10, 20, 20}, 0.5f, 0, 1), DR({12, 12, 22, 22}, 0.5f, 1, 2), DR({0, 0, 600, 40}, 0.5f, 2, 3)};
		m.publish(good, {0, 1, 2});
		struct WRef
		{
			const char* name;
			uint32_t reason;
			c5r::Err code;
			std::function<void(TImage&)> mutate;
			std::function<void(std::vector<uint8_t>&)> bytes;
		};
		const std::vector<WRef> refs = {
			{"cols_zero", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wCols = 0; }, nullptr},
			{"cells_too_many", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wCols = 2048; t.wRows = 1024; }, nullptr},
			{"origin_nan", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wOrigin[1] = std::numeric_limits<float>::quiet_NaN(); }, nullptr},
			{"inverse_zero", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wInv[0] = 0; }, nullptr},
			{"inverse_inf", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wInv[1] = std::numeric_limits<float>::infinity(); }, nullptr},
			{"cell_outside", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wCellRank[0][0] = 16; }, nullptr},
			{"point_nan", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wPoint[1][0] = std::numeric_limits<float>::quiet_NaN(); }, nullptr},
			{"rank_outside", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wCellRank[0][1] = 2; }, nullptr},
			{"rank_twice", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wCellRank[1][1] = t.wCellRank[0][1]; }, nullptr},
			{"large_rank_outside", DREF_WALK, c5r::Err::BadLayout, [](TImage& t) { t.wCellRank[2][1] = 1; }, nullptr},
			{"counts_disagree", DREF_WALK, c5r::Err::BadLayout, nullptr, [](std::vector<uint8_t>& b) {
				 const uint32_t o = Get32(b, OFF_WALKOFFSET);
				 Set32(b, o + 24, Get32(b, o + 24) + 1);   // smallCount
				 Set32(b, o + 28, Get32(b, o + 28) - 1);   // largeCount: the sum still 3, the entries say 2 + 1
			 }},
			{"sum_disagree", DREF_WALK, c5r::Err::BadLayout, nullptr, [](std::vector<uint8_t>& b) {
				 const uint32_t o = Get32(b, OFF_WALKOFFSET);
				 Set32(b, o + 28, 5);
			 }},
			{"past_end", DREF_OFFSET, c5r::Err::Truncated, nullptr, [](std::vector<uint8_t>& b) { Set32(b, OFF_WALKOFFSET, Get32(b, OFF_WALKOFFSET) + 64); }},
			{"misaligned", DREF_ALIGN, c5r::Err::BadLayout, nullptr, [](std::vector<uint8_t>& b) {
				 b.resize(b.size() + 16);
				 const uint64_t total = b.size();
				 memcpy(&b[OFF_IMAGEBYTES], &total, 8);
				 Set32(b, OFF_WALKOFFSET, Get32(b, OFF_WALKOFFSET) + 8);
			 }},
			{"overlaps_decals", DREF_ALIGN, c5r::Err::BadLayout, nullptr, [](std::vector<uint8_t>& b) { Set32(b, OFF_WALKOFFSET, Get32(b, OFF_DECALOFFSET)); }},
		};
		DecalTapReader r(FakePid());
		auto s0 = LoadSet(r, good);
		CHECK(s0 && hasWalk(*s0) && s0->walk.large == std::vector<uint32_t>({2}));
		for(const WRef& w : refs)
		{
			TImage t = good;
			if(w.mutate)
				w.mutate(t);
			std::vector<uint8_t> b = Build(t);
			if(w.bytes)
				w.bytes(b);
			const uint64_t before = r.counters().refusedBy[w.reason];
			const c5r::Status st = r.loadImage(b.data(), b.size());
			const bool ok = st.code == w.code && r.counters().refusedBy[w.reason] == before + 1 && r.current() == s0;
			if(!ok)
				printf("FAIL walk refusal %s: code %d, reason counter %llu -> %llu, why %s\n", w.name, (int)st.code,
				       (unsigned long long)before, (unsigned long long)r.counters().refusedBy[w.reason], r.lastWhy().c_str());
			CHECK(ok);
		}
		// through the section: acked 0, counted
		Producer p(FakePid());
		DecalTapReader rs(p.pid);
		good.generation = 1;
		CHECK(p.serve(1, Build(good)));
		CHECK(rs.poll() && p.b->ackResult == 0 && rs.counters().walkSets == 1);
		// and through the section a refused one: ackResult 5, the set kept
		TImage bad = good;
		bad.generation = 2;
		bad.wCellRank[0][1] = 7;
		CHECK(p.serve(2, Build(bad)));
		auto keep = rs.current();
		CHECK(!rs.poll() && p.b->ackResult == (int32_t)c5r::Err::BadLayout && rs.current() == keep && rs.counters().refusedBy[DREF_WALK] == 1);
		// an image without the block after one with it: no walk, the 4111 rule
		TImage plain = good;
		plain.generation = 3;
		plain.walk = false;
		CHECK(p.serve(3, Build(plain)));
		CHECK(rs.poll() && p.b->ackResult == 0);
		auto s3 = rs.current();
		CHECK(s3 && !hasWalk(*s3) && s3->walk.cols == 0 && rs.counters().walkSets == 1);
	}
	// (10) random bytes in the walk block: never a crash; every accepted set gives indices inside it
	{
		Emu m;
		m.init(world, 4, 4);
		TImage t;
		memcpy(t.world, world, 16);
		std::vector<int> ids;
		for(int i = 0; i < 40; ++i)
		{
			const std::array<float, 4> rr = {(float)(i * 20), (float)(i * 11), (float)(i * 20 + (i % 7 ? 30 : 300)), (float)(i * 11 + 20)};
			m.add(i, rr);
			t.decals.push_back(DR(rr, (float)(i % 3) * 0.2f, (uint32_t)i, (uint32_t)i + 1));
			ids.push_back(i);
		}
		m.publish(t, ids);
		const std::vector<uint8_t> base = Build(t);
		const uint32_t wo = Get32(base, OFF_WALKOFFSET);
		std::mt19937 rng(36600);
		int accepted = 0, refused = 0, badIdx = 0;
		for(int it = 0; it < 3000; ++it)
		{
			std::vector<uint8_t> b = base;
			const int flips = 1 + (int)(rng() % 4);
			for(int f = 0; f < flips; ++f)
				b[wo + rng() % (32 + 16 * 40)] = (uint8_t)rng();
			DecalTapReader r(FakePid());
			if(r.loadImage(b.data(), b.size()).code != c5r::Err::Ok)
			{
				refused++;
				continue;
			}
			accepted++;
			auto s = r.current();
			for(int oy = 0; oy < 1024 && s; oy += 256)
				for(int ox = 0; ox < 1024; ox += 128)
					for(uint32_t k : decalsForTile(*s, ox, oy).indices)
						if(k >= s->decals.size())
							badIdx++;
		}
		printf("  walk fuzz: %d accepted, %d refused\n", accepted, refused);
		CHECK(accepted > 0 && refused > 0 && badIdx == 0);
	}
	g_quiet = false;
}

// ---- 0365: the delta (a generation applied from the set in force; terrain-decals.md 19) ----------------------------------
// An editable image: parsed from valid image bytes (the program's or a recorded .dtap), changed, written again.
struct RawDecal
{
	VPShellDecalRecord rec;
	std::vector<uint8_t> verts;   // 8 bytes per vertex
	uint64_t hash = 0;
	VPShellDecalWalk walk = {};
};
struct RawImg
{
	VPShellDecalImage h;
	std::vector<RawDecal> d;
	std::vector<VPShellDecalName> names;
	std::vector<std::array<float, 4>> changed;
	std::vector<uint8_t> area;
	bool hasHash = false, hasWalk = false;
	VPShellDecalWalkHeader wh = {};
};

static bool ParseRaw(const std::vector<uint8_t>& b, RawImg& r)
{
	if(b.size() < sizeof(VPShellDecalImage))
		return false;
	memcpy(&r.h, b.data(), sizeof(r.h));
	const VPShellDecalImage& h = r.h;
	if(h.imageBytes != b.size())
		return false;
	r.d.resize(h.decalCount);
	r.hasHash = h.hashOffset != 0;
	r.hasWalk = h.walkOffset != 0;
	if(r.hasWalk)
		memcpy(&r.wh, &b[h.walkOffset], sizeof(r.wh));
	for(uint32_t i = 0; i < h.decalCount; ++i)
	{
		RawDecal& x = r.d[i];
		memcpy(&x.rec, &b[h.decalOffset + 48ull * i], 48);
		const size_t v0 = h.vertexOffset + 8ull * x.rec.firstVertex, nv = 3ull * x.rec.triangles;
		x.verts.assign(b.begin() + (ptrdiff_t)v0, b.begin() + (ptrdiff_t)(v0 + 8 * nv));
		if(r.hasHash)
			memcpy(&x.hash, &b[h.hashOffset + 8ull * i], 8);
		if(r.hasWalk)
			memcpy(&x.walk, &b[h.walkOffset + 32 + 16ull * i], 16);
	}
	r.names.resize(h.nameCount);
	for(uint32_t i = 0; i < h.nameCount; ++i)
		memcpy(&r.names[i], &b[h.nameOffset + sizeof(VPShellDecalName) * i], sizeof(VPShellDecalName));
	r.changed.resize(h.changedCount);
	for(uint32_t i = 0; i < h.changedCount; ++i)
		memcpy(r.changed[i].data(), &b[h.changedOffset + 16ull * i], 16);
	if(h.areaOffset)
		r.area.assign(b.begin() + h.areaOffset, b.begin() + h.areaOffset + ((size_t)h.areaTilesX * h.areaTilesY + 7) / 8);
	return true;
}

static std::vector<uint8_t> WriteRaw(const RawImg& r)
{
	VPShellDecalImage h = r.h;
	const uint32_t n = (uint32_t)r.d.size();
	uint32_t verts = 0;
	for(const RawDecal& x : r.d)
		verts += (uint32_t)(x.verts.size() / 8);
	size_t off = sizeof(VPShellDecalImage);
	h.decalOffset = (uint32_t)off;
	off = Align16(off + 48ull * n);
	h.nameOffset = (uint32_t)off;
	off = Align16(off + sizeof(VPShellDecalName) * r.names.size());
	h.vertexOffset = (uint32_t)off;
	off = Align16(off + 8ull * verts);
	h.changedOffset = (uint32_t)off;
	off = Align16(off + 16ull * r.changed.size());
	h.hashOffset = 0;
	if(r.hasHash)
	{
		h.hashOffset = (uint32_t)off;
		off = Align16(off + 8ull * n);
	}
	h.areaOffset = 0;
	if(!r.area.empty())
	{
		h.areaOffset = (uint32_t)off;
		off = Align16(off + r.area.size());
	}
	h.walkOffset = 0;
	if(r.hasWalk)
	{
		h.walkOffset = (uint32_t)off;
		off = Align16(off + 32 + 16ull * n);
	}
	h.decalCount = n;
	h.nameCount = (uint32_t)r.names.size();
	h.vertexCount = verts;
	h.changedCount = (uint32_t)r.changed.size();
	h.imageBytes = off;
	std::vector<uint8_t> b(off, 0);
	uint32_t first = 0;
	VPShellDecalWalkHeader wh = r.wh;
	wh.smallCount = wh.largeCount = 0;
	for(uint32_t i = 0; i < n; ++i)
	{
		VPShellDecalRecord rec = r.d[i].rec;
		rec.firstVertex = first;
		rec.triangles = (uint32_t)(r.d[i].verts.size() / 24);
		memcpy(&b[h.decalOffset + 48ull * i], &rec, 48);
		if(!r.d[i].verts.empty())
			memcpy(&b[h.vertexOffset + 8ull * first], r.d[i].verts.data(), r.d[i].verts.size());
		first += (uint32_t)(r.d[i].verts.size() / 8);
		if(r.hasHash)
			memcpy(&b[h.hashOffset + 8ull * i], &r.d[i].hash, 8);
		if(r.hasWalk)
		{
			memcpy(&b[h.walkOffset + 32 + 16ull * i], &r.d[i].walk, 16);
			(r.d[i].walk.cell == VPSHELL_DECAL_WALK_LARGE ? wh.largeCount : wh.smallCount)++;
		}
	}
	for(size_t i = 0; i < r.names.size(); ++i)
		memcpy(&b[h.nameOffset + sizeof(VPShellDecalName) * i], &r.names[i], sizeof(VPShellDecalName));
	for(size_t i = 0; i < r.changed.size(); ++i)
		memcpy(&b[h.changedOffset + 16 * i], r.changed[i].data(), 16);
	if(!r.area.empty())
		memcpy(&b[h.areaOffset], r.area.data(), r.area.size());
	if(r.hasWalk)
		memcpy(&b[h.walkOffset], &wh, 32);
	memcpy(b.data(), &h, sizeof(h));
	return b;
}

// The next generation of `a`: changes since a's generation, none listed yet.
static RawImg NextGen(const RawImg& a)
{
	RawImg b = a;
	b.h.generation = a.h.generation + 1;
	b.h.changedSince = a.h.generation;
	b.changed.clear();
	return b;
}

static std::array<float, 4> RectOf(const RawDecal& x) { return {x.rec.rect[0], x.rec.rect[1], x.rec.rect[2], x.rec.rect[3]}; }

// Removes decal k (its walk rank closed up), listing its rectangle unless `unlisted`
static void RawRemove(RawImg& r, size_t k, bool unlisted = false)
{
	const RawDecal x = r.d[k];
	if(!unlisted)
		r.changed.push_back(RectOf(x));
	r.d.erase(r.d.begin() + (ptrdiff_t)k);
	if(r.hasWalk)
		for(RawDecal& y : r.d)
			if(y.walk.cell == x.walk.cell && y.walk.rank > x.walk.rank)
				y.walk.rank--;
}

// A new decal at record position `at`: decal `from`'s content moved by (dx, dy) and with priority `prio`, the newest of
// its walk cell (rank 0) or the last of the large list; its rectangle listed unless `unlisted`
static void RawAdd(RawImg& r, size_t at, size_t from, float dx, float dy, float prio, bool unlisted = false)
{
	RawDecal x = r.d[from];
	x.rec.rect[0] += dx;
	x.rec.rect[2] += dx;
	x.rec.rect[1] += dy;
	x.rec.rect[3] += dy;
	x.rec.priority = prio;
	x.hash ^= 0x5bd1e995ull;
	if(r.hasWalk)
	{
		if(x.walk.cell == VPSHELL_DECAL_WALK_LARGE)
		{
			uint32_t nl = 0;
			for(const RawDecal& y : r.d)
				nl += y.walk.cell == VPSHELL_DECAL_WALK_LARGE;
			x.walk.rank = nl;
		}
		else
		{
			x.walk.point[0] = (x.rec.rect[0] + x.rec.rect[2]) * 0.5f;
			x.walk.point[1] = (x.rec.rect[1] + x.rec.rect[3]) * 0.5f;
			int cx = (int)((x.walk.point[0] - r.wh.origin[0]) * r.wh.inverse[0]);
			int cy = (int)((x.walk.point[1] - r.wh.origin[1]) * r.wh.inverse[1]);
			cx = (std::max)(0, (std::min)((int)r.wh.cols - 1, cx));
			cy = (std::max)(0, (std::min)((int)r.wh.rows - 1, cy));
			x.walk.cell = (uint32_t)cy * r.wh.cols + (uint32_t)cx;
			x.walk.rank = 0;
			for(RawDecal& y : r.d)
				if(y.walk.cell == x.walk.cell)
					y.walk.rank++;
		}
	}
	if(!unlisted)
		r.changed.push_back(RectOf(x));
	r.d.insert(r.d.begin() + (ptrdiff_t)(std::min)(at, r.d.size()), x);
}

// Every field of two sets, the walk's and the grid's included
static bool SameSetAll(const DecalSet& a, const DecalSet& b, std::string* what = nullptr)
{
	auto bad = [&](const char* w) {
		if(what)
			*what = w;
		return false;
	};
	if(!SameSet(a, b))
		return bad("SameSet (header, names, decals with hashes, vertices, grid lists)");
	if(a.grid.cellX0 != b.grid.cellX0 || a.grid.cellY0 != b.grid.cellY0)
		return bad("grid origin");
	if(a.areaTilesX != b.areaTilesX || a.areaTilesY != b.areaTilesY || a.areaChanged != b.areaChanged)
		return bad("area");
	const auto &w = a.walk, &v = b.walk;
	if(memcmp(w.origin, v.origin, 8) || memcmp(w.inverse, v.inverse, 8) || w.cols != v.cols || w.rows != v.rows || w.cell != v.cell ||
	   w.rank != v.rank || w.point != v.point || w.cellStart != v.cellStart || w.cellItems != v.cellItems || w.large != v.large)
		return bad("walk");
	return true;
}

// decalsForTile of every tile of the world rectangle (and one ring outside it) equal on both sets
static size_t TileListsDiffer(const DecalSet& a, const DecalSet& b, size_t* entries = nullptr)
{
	size_t bad = 0, total = 0;
	const int x0 = (int)std::floor(a.worldRect[0] / 64) - 1, x1 = (int)std::floor(a.worldRect[2] / 64) + 1;
	const int y0 = (int)std::floor(a.worldRect[1] / 64) - 1, y1 = (int)std::floor(a.worldRect[3] / 64) + 1;
	for(int y = y0; y <= y1; ++y)
		for(int x = x0; x <= x1; ++x)
		{
			const auto la = c5r::decal::decalsForTile(a, 64 * x, 64 * y).indices;
			total += la.size();
			if(la != c5r::decal::decalsForTile(b, 64 * x, 64 * y).indices)
				bad++;
		}
	if(entries)
		*entries = total;
	return bad;
}

static std::vector<std::pair<int, int>> RectTiles(const std::vector<DecalRect>& r, const float* world)
{
	return Tiles(c5r::decal::tilesForRects(r.data(), r.size(), world));
}

// One pair: a then b through a reader with the delta, against b built whole by a fresh reader and a then b with the
// delta off. Checks the set field for field, every tile's list, the queued change (the same rectangles and tiles as the
// whole path's), and that the delta was used (wantDelta) or not (its reason contains `whyHas`).
struct PairResult
{
	bool used = false, gridFromPrev = false;
	uint32_t reordered = 0;
	double wholeMs = 0, deltaMs = 0, wholeTotalMs = 0, deltaTotalMs = 0;
	size_t entries = 0;
};
static PairResult CheckPair(const char* label, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, bool wantDelta,
                            const char* whyHas = nullptr, int wantGridFromPrev = 1)
{
	PairResult pr;
	DecalTapReader rd(FakePid(), L"Local\\VPShellDecalTap.none-0365"), whole(FakePid(), L"Local\\VPShellDecalTap.none-0365b"),
	    off(FakePid(), L"Local\\VPShellDecalTap.none-0365c");
	off.setDelta(false);
	const bool okA = rd.loadImage(a.data(), a.size()).ok() && off.loadImage(a.data(), a.size()).ok();
	std::vector<DecalChange> drop;
	rd.drainChanges(drop);
	off.drainChanges(drop);
	const uint64_t notListed0 = rd.counters().rectsNotListed;   // the first set's every decal (it lists only its own changes)
	const bool okB = rd.loadImage(b.data(), b.size()).ok() && whole.loadImage(b.data(), b.size()).ok() &&
	                 off.loadImage(b.data(), b.size()).ok();
	CHECK(okA && okB);
	if(!okA || !okB)
	{
		printf("  %s: an image was refused: %s\n", label, rd.lastWhy().c_str());
		return pr;
	}
	const DecalDeltaInfo di = rd.lastDelta();
	pr.used = di.used;
	pr.gridFromPrev = di.gridFromPrev;
	pr.reordered = di.reordered;
	pr.deltaMs = rd.counters().lastApplyMs;
	pr.deltaTotalMs = rd.counters().lastTotalMs;
	pr.wholeMs = off.counters().lastApplyMs;
	pr.wholeTotalMs = off.counters().lastTotalMs;
	CHECK(di.used == wantDelta);
	if(wantDelta)
	{
		if(wantGridFromPrev >= 0)   // -1: either (an image whose grid edge the scenario cannot avoid)
			CHECK(di.gridFromPrev == (wantGridFromPrev == 1));
		CHECK(rd.counters().deltaSets == 1 && rd.counters().wholeSets == 1);
	}
	else
	{
		CHECK(rd.counters().deltaSets == 0 && rd.counters().wholeSets == 2);
		CHECK(whyHas && di.why.find(whyHas) != std::string::npos);
	}
	CHECK(off.lastDelta().why == "delta off" && off.counters().deltaSets == 0);
	std::string what;
	const auto s1 = rd.current(), s2 = whole.current(), s3 = off.current();
	const bool same = s1 && s2 && s3 && SameSetAll(*s1, *s2, &what) && SameSetAll(*s3, *s2);
	CHECK(same);
	size_t tilesBad = 0;
	if(s1 && s2)
		tilesBad = TileListsDiffer(*s1, *s2, &pr.entries);
	CHECK(tilesBad == 0);
	std::vector<DecalChange> c1, c3;
	rd.drainChanges(c1);
	off.drainChanges(c3);
	CHECK(c1.size() == 1 && c3.size() == 1);
	if(c1.size() == 1 && c3.size() == 1)
	{
		CHECK(c1[0].generation == c3[0].generation && c1[0].previousGeneration == c3[0].previousGeneration && !c1[0].all);
		CHECK(RectTiles(c1[0].rects, c1[0].worldRect) == RectTiles(c3[0].rects, c3[0].worldRect));
		CHECK(Tiles(c1[0].areaTiles) == Tiles(c3[0].areaTiles));
		if(wantDelta)
			CHECK(rd.counters().rectsNotListed == notListed0);
	}
	printf("  %s: %s%s%s (touched %u + %u, %u out of order, %u cells sorted%s%s), set %s%s, %zu tile entries, %zu tiles differ; "
	       "whole %.2f ms (total %.2f), this %.2f ms (total %.2f) (indication)\n",
	       label, di.used ? "delta" : "whole: ", di.used ? "" : di.why.c_str(), di.used && !di.gridFromPrev ? " (grid whole)" : "",
	       di.touchedPrev, di.touchedNew, di.reordered, di.cellsSorted, di.gridWhy.empty() ? "" : ", grid: ", di.gridWhy.c_str(),
	       same ? "equal" : "DIFFERS at ", same ? "" : what.c_str(), pr.entries, tilesBad, pr.wholeMs, pr.wholeTotalMs, pr.deltaMs,
	       pr.deltaTotalMs);
	return pr;
}

// The scenarios of one base image (bytes of a valid generation): each a pair through CheckPair
static void DeltaScenarios(const char* tag, const std::vector<uint8_t>& base, uint32_t seed, bool strictGrid)
{
	const int gw = strictGrid ? 1 : -1;
	RawImg a;
	CHECK(ParseRaw(base, a));
	if(a.d.size() < 8)
	{
		CHECK(a.d.size() >= 8);
		return;
	}
	if(a.h.generation == 0)
		a.h.generation = 1;
	const std::vector<uint8_t> A = WriteRaw(a);
	{   // the writer keeps the set: a parsed and written image reads as the same set
		DecalTapReader r1(FakePid(), L"Local\\VPShellDecalTap.none-0365d"), r2(FakePid(), L"Local\\VPShellDecalTap.none-0365e");
		CHECK(r1.loadImage(base.data(), base.size()).ok() && r2.loadImage(A.data(), A.size()).ok());
		if(r1.current() && r2.current())
		{
			DecalSet x = *r1.current();
			x.generation = r2.current()->generation;
			CHECK(SameSetAll(x, *r2.current()));
		}
	}
	std::mt19937 rng(seed);
	const size_t n = a.d.size();
	auto pick = [&]() { return (size_t)(rng() % n); };
	auto pickSmall = [&]() {   // a decal not in the large list, away from the world's edge
		for(int t = 0; t < 1000; ++t)
		{
			const size_t k = pick();
			const auto& r = a.d[k].rec.rect;
			if(r[2] - r[0] <= 200 && r[3] - r[1] <= 200 && r[0] > a.h.worldRect[0] + 300 && r[2] < a.h.worldRect[2] - 300 &&
			   r[1] > a.h.worldRect[1] + 300 && r[3] < a.h.worldRect[3] - 300)
				return k;
		}
		return pick();
	};
	char label[160];
	{   // one decal removed
		RawImg b = NextGen(a);
		RawRemove(b, pickSmall());
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s remove 1", tag);
		CheckPair(label, A, WriteRaw(b), true, nullptr, gw);
	}
	{   // one decal added in the middle of the records
		RawImg b = NextGen(a);
		RawAdd(b, n / 2, pickSmall(), 37.25f, -21.5f, 0.55f);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s add 1", tag);
		CheckPair(label, A, WriteRaw(b), true, nullptr, gw);
	}
	{   // a road: one removed and two added (the route curves of 9.3)
		RawImg b = NextGen(a);
		const size_t k = pickSmall();
		RawRemove(b, k);
		RawAdd(b, k, pickSmall(), 3.0f, 0.0f, a.d[k].rec.priority);
		RawAdd(b, 0, pickSmall(), -5.0f, 7.0f, 0.5f);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s remove 1 add 2", tag);
		CheckPair(label, A, WriteRaw(b), true, nullptr, gw);
	}
	{   // an added decal with exactly a kept decal's rectangle: that one is in a changed rectangle too
		RawImg b = NextGen(a);
		RawAdd(b, 3, pickSmall(), 0, 0, 0.123f);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s add over a kept rectangle", tag);
		const PairResult p = CheckPair(label, A, WriteRaw(b), true, nullptr, gw);
		(void)p;
	}
	{   // kept decals out of their order (two swapped): the search finds them
		RawImg b = NextGen(a);
		RawRemove(b, pickSmall());
		std::swap(b.d[1], b.d[n / 3]);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s two kept swapped", tag);
		const PairResult p = CheckPair(label, A, WriteRaw(b), true, nullptr, gw);
		CHECK(p.reordered >= 2);
	}
	{   // the texture table renumbered (the names in another order): kept decals match by name
		RawImg b = NextGen(a);
		if(b.names.size() >= 2)
		{
			std::vector<uint32_t> perm(b.names.size());
			for(uint32_t k = 0; k < perm.size(); ++k)
				perm[k] = (uint32_t)perm.size() - 1 - k;
			std::vector<VPShellDecalName> nn(b.names.size());
			for(uint32_t k = 0; k < perm.size(); ++k)
				nn[perm[k]] = b.names[k];
			b.names = nn;
			for(RawDecal& x : b.d)
				for(int t = 0; t < 3; ++t)
					if(x.rec.texture[t] != VPSHELL_DECAL_NO_TEXTURE)
						x.rec.texture[t] = perm[x.rec.texture[t]];
		}
		RawRemove(b, pickSmall());
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s names renumbered", tag);
		CheckPair(label, A, WriteRaw(b), true, nullptr, gw);
	}
	{   // a decal at the grid's edge removed: the span may shrink, the grid is built whole (still a delta)
		size_t edge = 0;
		float best = 1e30f;
		for(size_t k = 0; k < n; ++k)
			if(a.d[k].rec.rect[0] < best)
			{
				best = a.d[k].rec.rect[0];
				edge = k;
			}
		RawImg b = NextGen(a);
		RawRemove(b, edge);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s edge decal removed", tag);
		CheckPair(label, A, WriteRaw(b), true, nullptr, strictGrid ? 0 : -1);
	}
	// ---- refused or not tried: the whole path, the same result
	{   // an added decal the changed list does not name
		RawImg b = NextGen(a);
		RawAdd(b, 5, pickSmall(), 11.0f, 13.0f, 0.5f, true);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s unlisted add (refused)", tag);
		CheckPair(label, A, WriteRaw(b), false, "is in no changed rectangle");
	}
	{   // a removed decal the changed list does not name
		RawImg b = NextGen(a);
		RawRemove(b, pickSmall(), true);
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s unlisted remove (refused)", tag);
		CheckPair(label, A, WriteRaw(b), false, "is gone but in no changed rectangle");
	}
	{   // a kept decal's vertex bytes changed without its rectangle listed (a producer that lies)
		RawImg b = NextGen(a);
		RawRemove(b, pickSmall());
		RawDecal& x = b.d[pickSmall() % b.d.size()];
		if(!x.verts.empty())
			x.verts[0] ^= 0x40;
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s changed vertex unlisted (refused)", tag);
		CheckPair(label, A, WriteRaw(b), false, "is in no changed rectangle");
	}
	{   // a new load
		RawImg b = NextGen(a);
		b.h.loadSeq++;
		RawRemove(b, pickSmall());
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s new load (whole)", tag);
		CheckPair(label, A, WriteRaw(b), false, "a new load");
	}
	{   // another world rectangle (a layout change)
		RawImg b = NextGen(a);
		b.h.worldRect[2] += 64;
		if(!b.area.empty())   // the area grid follows the world's tiles
		{
			const double wx = std::floor((double)b.h.worldRect[2] / 64) - std::floor((double)b.h.worldRect[0] / 64) + 1;
			if((uint32_t)wx != b.h.areaTilesX)
			{
				std::vector<uint8_t> na(((size_t)(uint32_t)wx * b.h.areaTilesY + 7) / 8, 0);
				b.area = na;
				b.h.areaTilesX = (uint32_t)wx;
			}
		}
		RawRemove(b, pickSmall());
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s world changed (whole)", tag);
		CheckPair(label, A, WriteRaw(b), false, a.area.empty() ? "the world rectangle changed" : "changed");
	}
	{   // changedSince 0: every decal listed
		RawImg b = NextGen(a);
		b.h.changedSince = 0;
		RawRemove(b, pickSmall());
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s changedSince 0 (whole)", tag);
		CheckPair(label, A, WriteRaw(b), false, "changedSince 0");
	}
	{   // changes since a generation after the set in force
		RawImg b = NextGen(a);
		b.h.generation++;
		b.h.changedSince = a.h.generation + 1;
		RawRemove(b, pickSmall());
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s changedSince ahead (whole)", tag);
		CheckPair(label, A, WriteRaw(b), false, "after the set in force");
	}
	{   // more changes than the share
		RawImg b = NextGen(a);
		const size_t many = (std::max)((size_t)65, (size_t)(kDecalDeltaMaxShare * n) + 1);
		for(size_t k = 0; k < many; ++k)
			b.changed.push_back({1, 1, 2, 2});
		_snprintf_s(label, sizeof(label), _TRUNCATE, "%s too many changes (whole)", tag);
		CheckPair(label, A, WriteRaw(b), false, "changed rectangles, more than");
	}
}

// A random generation in the shape of the huge save's (17,800 decals on 3,584 x 2,560 samples, ties of priority and
// listOrder, a tenth large, area bits), with or without the walk block
static TImage RandomHuge(uint32_t seed, bool walk, bool area)
{
	std::mt19937 rng(seed);
	TImage t;
	t.generation = 3;
	t.loadSeq = 3;
	t.world[0] = 0;
	t.world[1] = 0;
	t.world[2] = 3583;
	t.world[3] = 2559;
	t.names.clear();
	for(int k = 0; k < 40; ++k)
		t.names.push_back("decal_" + std::to_string(k) + ".dds");
	const float prios[] = {0.0f, 0.1f, 0.4f, 0.5f, 0.6f, 0.7f};
	for(uint32_t i = 0; i < 17800; ++i)
	{
		float x = (float)(rng() % 3500) + 40, y = (float)(rng() % 2480) + 40;
		const bool large = rng() % 10 == 0;
		const float w = large ? (float)(rng() % 400) + 20 : (float)(rng() % 40) + (rng() % 100) / 100.f;
		const float h = large ? (float)(rng() % 300) : (float)(rng() % 40);
		TDecal d = D(x, y, (std::min)(x + w, 3583.f), (std::min)(y + h, 2559.f), prios[rng() % 6], rng() % 5000, rng());
		d.tri = 1 + rng() % 6;
		d.flags = rng() % 50 == 0 ? 1 : 0;
		d.tex[0] = rng() % 40;
		d.tex[1] = rng() % 7 == 0 ? VPSHELL_DECAL_NO_TEXTURE : rng() % 40;
		d.tex[2] = rng() % 40;
		t.decals.push_back(d);
	}
	if(area)
	{
		t.areaX = 56;
		t.areaY = 40;
		for(int k = 0; k < 500; ++k)
			t.areaBits.push_back({rng() % 56, rng() % 40});
	}
	if(walk)
	{
		t.walk = true;
		t.wOrigin[0] = 0;
		t.wOrigin[1] = 0;
		t.wCols = 56;
		t.wRows = 40;
		t.wInv[0] = 56.0f / 3583.0f;
		t.wInv[1] = 40.0f / 2559.0f;
		std::map<uint32_t, uint32_t> perCell;
		uint32_t nl = 0;
		for(const TDecal& d : t.decals)
		{
			const bool large = d.rect[2] - d.rect[0] > 256 || d.rect[3] - d.rect[1] > 256;
			const float px = (d.rect[0] + d.rect[2]) * 0.5f, py = (d.rect[1] + d.rect[3]) * 0.5f;
			if(large)
			{
				t.wCellRank.push_back({VPSHELL_DECAL_WALK_LARGE, nl++});
				t.wPoint.push_back({0, 0});
				continue;
			}
			const int cx = (std::min)(55, (int)(px * t.wInv[0])), cy = (std::min)(39, (int)(py * t.wInv[1]));
			const uint32_t c = (uint32_t)cy * 56 + (uint32_t)cx;
			t.wCellRank.push_back({c, perCell[c]++});
			t.wPoint.push_back({px, py});
		}
	}
	return t;
}

// The delta on the program's images: the standard small image, and random huge-sized ones with and without the walk and
// area blocks; each through every scenario of DeltaScenarios.
static void CaseDelta()
{
	g_quiet = true;   // every load logs a line; the case prints its own
	{   // the small standard image grown to 12 decals (the scenarios want 8 or more)
		TImage t = Standard(1);
		for(uint32_t k = 0; k < 8; ++k)
			t.decals.push_back(D(400.0f + 40 * k, 420.0f + 30 * k, 410.0f + 40 * k, 440.0f + 30 * k, 0.5f, 10 + k, 20 + k));
		DeltaScenarios("small", Build(t), 365, false);
	}
	DeltaScenarios("huge walk+area", Build(RandomHuge(36501, true, true)), 36511, true);
	DeltaScenarios("huge no blocks", Build(RandomHuge(36502, false, false)), 36512, true);
	{   // a chain of 40 generations, each a road (1 removed, 2 added) on the last: delta every time, equal every time
		RawImg a;
		CHECK(ParseRaw(Build(RandomHuge(36503, true, true)), a));
		std::mt19937 rng(36513);
		DecalTapReader rd(FakePid(), L"Local\\VPShellDecalTap.none-0365f");
		std::vector<uint8_t> img = WriteRaw(a);
		CHECK(rd.loadImage(img.data(), img.size()).ok());
		size_t bad = 0;
		for(int g = 0; g < 40; ++g)
		{
			RawImg b = NextGen(a);
			const size_t k = 1 + rng() % (b.d.size() - 2);
			RawRemove(b, k);
			RawAdd(b, k, rng() % b.d.size(), (float)(rng() % 9), (float)(rng() % 9), 0.5f);
			RawAdd(b, rng() % b.d.size(), rng() % b.d.size(), -(float)(rng() % 9), 3.0f, 0.4f);
			img = WriteRaw(b);
			CHECK(rd.loadImage(img.data(), img.size()).ok());
			DecalTapReader whole(FakePid(), L"Local\\VPShellDecalTap.none-0365g");
			CHECK(whole.loadImage(img.data(), img.size()).ok());
			if(!rd.lastDelta().used || !rd.current() || !whole.current() || !SameSetAll(*rd.current(), *whole.current()) ||
			   TileListsDiffer(*rd.current(), *whole.current()) != 0)
				bad++;
			a = b;
		}
		printf("  chain: 40 generations, %llu by the delta, %zu not equal to the whole build\n",
		       (unsigned long long)rd.counters().deltaSets, bad);
		CHECK(bad == 0 && rd.counters().deltaSets == 40);
	}
}

// 0365: the scenarios on recorded images (decal_tap_test.exe --recorded FILE.dtap ...): game data, never committed (D83),
// so not a case of the suite; the evidence names the files and the output.
static int RunRecorded(int argc, wchar_t** argv)
{
	g_quiet = true;
	for(int i = 2; i < argc; ++i)
	{
		const std::vector<uint8_t> b = ReadFileBytes(argv[i]);
		char tag[300];
		_snprintf_s(tag, sizeof(tag), _TRUNCATE, "%ls", argv[i]);
		printf("RECORDED %s: %zu bytes\n", tag, b.size());
		CHECK(!b.empty());
		if(!b.empty())
			DeltaScenarios(tag, b, 36520 + i, false);
	}
	printf("RECORDED %s\n", g_fail ? "FAIL" : "PASS");
	return g_fail ? 1 : 0;
}

static void CaseSelftestFail() { CHECK(1 + 1 == 3); }

struct Case
{
	const char* name;
	void (*fn)();
};

static const Case kCases[] = {
	{"section_and_file_equal", CaseSectionAndFileEqual},
	{"refusals", CaseRefusals},
	{"refusals_other", CaseRefusalsOther},
	{"torn", CaseTorn},
	{"changes_tiles", CaseChangesTiles},
	{"tile_query", CaseTileQuery},
	{"area", CaseArea},
	{"walk", CaseWalk},
	{"fuzz", CaseFuzz},
	{"delta", CaseDelta},
	{"selftest_fail", CaseSelftestFail},
};

int wmain(int argc, wchar_t** argv)
{
	if(argc < 2)
	{
		for(const Case& c : kCases)
			printf("CASE %s\n", c.name);
		return 0;
	}
	if(argc >= 3)
		g_dir = argv[2];
	else
	{
		wchar_t t[MAX_PATH];
		GetTempPathW(MAX_PATH, t);
		g_dir = t;
		if(!g_dir.empty() && g_dir.back() == L'\\')
			g_dir.pop_back();
	}
	LogOpen(nullptr, HostLog);
	if(wcscmp(argv[1], L"--recorded") == 0)
	{
		g_dir.clear();
		const int rc = RunRecorded(argc, argv);
		LogClose();
		return rc;
	}
	for(const Case& c : kCases)
	{
		wchar_t w[64];
		_snwprintf_s(w, _countof(w), _TRUNCATE, L"%hs", c.name);
		if(wcscmp(w, argv[1]) != 0)
			continue;
		c.fn();
		printf("CASE %s %s\n", c.name, g_fail ? "FAIL" : "PASS");
		fflush(stdout);
		LogClose();
		return g_fail ? 1 : 0;
	}
	printf("decal_tap_test: no case matches '%ls'\n", argv[1]);
	return 2;
}
