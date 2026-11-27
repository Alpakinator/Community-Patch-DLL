// cut_tap_test.cpp - tests of server.exe's cut-tap reader (ticket 4553): srv_cut_tap.cpp against a fake producer in this
// process (a control section of its own name, image sections named as the tap names them with this process's pid).
// Written 2026-11-16 for this repository (GPL-3.0-or-later), by the pattern of server/tests/render/ice/ice_tap_test.cpp.
// Every case prints "PASS name" or "FAIL name: why"; the last line is "cut_tap_test: N passed, M failed, 0 skipped".
// Exit 0 only when every case passed.
#include "srv_cut_tap.h"
#include "srv_render_common.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace srvrender;

namespace {

int g_pass = 0, g_fail = 0;
std::string g_why;

#define CHECK(c)                                                                                 \
	do                                                                                           \
	{                                                                                            \
		if(!(c))                                                                                 \
		{                                                                                        \
			char b_[300];                                                                        \
			_snprintf_s(b_, sizeof(b_), _TRUNCATE, "line %d: %s", __LINE__, #c);                \
			g_why = b_;                                                                          \
			return false;                                                                        \
		}                                                                                        \
	} while(0)

void Run(const char* name, const std::function<bool()>& f)
{
	g_why.clear();
	bool ok = false;
	try
	{
		ok = f();
	}
	catch(...)
	{
		g_why = "exception";
	}
	if(ok)
	{
		g_pass++;
		printf("PASS %s\n", name);
	}
	else
	{
		g_fail++;
		printf("FAIL %s: %s\n", name, g_why.c_str());
	}
	fflush(stdout);
}

const uint32_t TX = 7, TY = 3, BLOCKS = 8;
const float OX = 1.5f, OY = -2.25f;

// The synthetic grid of shell/composite/cut_tap.cpp's test world (the same bytes), as an image.
struct Img
{
	VPShellCutImage h = {};
	std::vector<uint8_t> dir, pool;
	std::vector<uint8_t> bytes() const
	{
		std::vector<uint8_t> b((size_t)h.imageBytes, 0);
		memcpy(b.data(), &h, sizeof(h));
		memcpy(b.data() + h.dirOffset, dir.data(), dir.size());
		if(!pool.empty())
			memcpy(b.data() + h.poolOffset, pool.data(), pool.size());
		return b;
	}
};

Img MakeImg(uint32_t generation, uint32_t pid, uint32_t load = 1)
{
	Img m;
	m.dir.assign(TX * TY * 8, 0xCD);
	for(uint32_t t = 0; t < TX * TY; ++t)
	{
		uint8_t* e = m.dir.data() + t * 8;
		if(t % 3 == 0)
		{
			const uint16_t st = 0;
			const uint32_t k = 6 - t / 3;
			memcpy(e, &st, 2);
			memcpy(e + 4, &k, 4);
		}
		else
		{
			const uint16_t st = 1;
			memcpy(e, &st, 2);
			e[4] = (uint8_t)((t * 37 + 5) & 0xFF);
		}
	}
	m.pool.assign(BLOCKS * 256, 0xEE);
	for(uint32_t k = 0; k < 7; ++k)
		for(uint32_t i = 0; i < 256; ++i)
			m.pool[k * 256 + i] = (uint8_t)((k * 31 + i * 7) & 0xFF);
	VPShellCutImage& h = m.h;
	h.magic = VPSHELL_CUT_MAGIC;
	h.version = VPSHELL_CUT_VERSION;
	h.headerBytes = sizeof(VPShellCutImage);
	h.generation = generation;
	h.loadSeq = load;
	h.exePid = pid;
	h.widthCells = TX * 16;
	h.heightCells = TY * 16;
	h.tilesX = TX;
	h.tilesY = TY;
	h.samplesX = 200;
	h.samplesY = 72;
	h.ox = OX;
	h.oy = OY;
	h.poolBlocks = BLOCKS;
	h.pooledTiles = 7;
	h.dirOffset = 160;
	h.dirBytes = TX * TY * 8;
	h.poolOffset = (160 + h.dirBytes + 15) & ~15u;
	h.poolBytes = BLOCKS * 256;
	h.imageBytes = h.poolOffset + h.poolBytes;
	h.gridHeader[0] = 1;
	const uint32_t used = 7;
	memcpy(h.gridHeader + VPCUT_G_USED, &used, 4);
	return m;
}

// The cell the EXE's lookup reads in the synthetic grid.
uint8_t Cell(const Img& m, uint32_t col, uint32_t row)
{
	const uint32_t t = (row / 16) * TX + col / 16;
	uint16_t st;
	memcpy(&st, m.dir.data() + t * 8, 2);
	if(st)
		return m.dir[t * 8 + 4];
	uint32_t k;
	memcpy(&k, m.dir.data() + t * 8 + 4, 4);
	return m.pool[k * 256 + (row % 16) * 16 + col % 16];
}

// A world point inside cell (col, row) by the rule of c5r/forest/cut.h.
void WorldOf(uint32_t col, uint32_t row, float& x, float& y)
{
	x = (float)(2 * col + 1) - OX;
	y = (float)(2 * row + 1) / 1.1547344f - OY;
}

// A fake producer: the control section and image sections in this process.
struct Producer
{
	std::wstring ctlName;
	HANDLE ctl = nullptr;
	VPShellCutTapControl* blk = nullptr;
	std::vector<HANDLE> images;
	uint32_t pid = GetCurrentProcessId();

	explicit Producer(const wchar_t* tag)
	{
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\VPShellCutTapTest.%u.%ls", pid, tag);
		ctlName = n;
		ctl = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, VPSHELL_CUTTAP_CONTROL_BYTES, n);
		blk = ctl ? (VPShellCutTapControl*)MapViewOfFile(ctl, FILE_MAP_WRITE, 0, 0, VPSHELL_CUTTAP_CONTROL_BYTES) : nullptr;
	}
	~Producer()
	{
		for(HANDLE h : images)
			CloseHandle(h);
		if(blk)
			UnmapViewOfFile(blk);
		if(ctl)
			CloseHandle(ctl);
	}
	// An image section of `bytes` (sectionBytes: its size, 0 = the bytes' size), named as the tap names generation g.
	bool section(uint32_t g, const std::vector<uint8_t>& bytes, size_t sectionBytes = 0)
	{
		wchar_t n[64];
		_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_CUT_IMAGE_FMT, pid, g);
		const size_t sz = sectionBytes ? sectionBytes : bytes.size();
		HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, (DWORD)sz, n);
		if(!h)
			return false;
		void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0);
		if(!v)
			return false;
		memcpy(v, bytes.data(), bytes.size() < sz ? bytes.size() : sz);
		UnmapViewOfFile(v);
		images.push_back(h);
		return true;
	}
	// The control block under its seqlock (as cut_tap.cpp Publish).
	void publish(uint32_t state, uint32_t g, const std::vector<uint8_t>& bytes, const Img* m, uint32_t fail = 0, const char* text = "",
	             std::function<void(VPShellCutTapControl&)> tweak = nullptr)
	{
		VPShellCutTapControl c;
		memcpy(&c, blk, sizeof(c));
		c.magic = VPSHELL_CUT_MAGIC;
		c.version = VPSHELL_CUT_VERSION;
		c.headerBytes = sizeof(VPShellCutTapControl);
		c.state = state;
		c.failReason = fail;
		c.exePid = pid;
		c.generation = g;
		c.loadSeq = m ? m->h.loadSeq : 1;
		c.tilesX = m ? m->h.tilesX : 0;
		c.tilesY = m ? m->h.tilesY : 0;
		c.poolBlocks = m ? m->h.poolBlocks : 0;
		c.imageBytes = bytes.size();
		c.fnv1a64 = bytes.empty() ? 0 : VPCutFnv(VPCUT_FNV_INIT, bytes.data(), bytes.size());
		_snwprintf_s(c.imageName, _countof(c.imageName), _TRUNCATE, g ? VPSHELL_CUT_IMAGE_FMT : L"", pid, g);
		strncpy_s(c.failText, text, _TRUNCATE);
		if(tweak)
			tweak(c);
		InterlockedIncrement((volatile LONG*)&blk->seq);
		MemoryBarrier();
		const uint32_t seq = blk->seq;
		const uint32_t ack = blk->ackGeneration, ackPid = blk->ackServerPid;
		const int32_t ackRes = blk->ackResult;
		memcpy(blk, &c, sizeof(c));
		blk->seq = seq;
		blk->ackGeneration = ack;
		blk->ackServerPid = ackPid;
		blk->ackResult = ackRes;
		MemoryBarrier();
		InterlockedIncrement((volatile LONG*)&blk->seq);
	}
	// Publish a good image of generation g.
	bool good(uint32_t g, const Img& m)
	{
		const std::vector<uint8_t> b = m.bytes();
		if(!section(g, b))
			return false;
		publish(VPCUT_READY, g, b, &m);
		return true;
	}
};

bool GridEquals(const CutTapReader& r, const Img& m)
{
	for(uint32_t row = 0; row < TY * 16; ++row)
		for(uint32_t col = 0; col < TX * 16; ++col)
		{
			float x, y;
			WorldOf(col, row, x, y);
			if(r.bitsAt(x, y) != Cell(m, col, row))
			{
				char b[160];
				_snprintf_s(b, sizeof(b), _TRUNCATE, "cell (%u, %u): reader 0x%02X, expected 0x%02X", col, row, r.bitsAt(x, y),
				            Cell(m, col, row));
				g_why = b;
				return false;
			}
		}
	return true;
}

// One refusal: a good generation 1 first, then generation 2 broken by `breakIt`; checks the reason, the ack, the kept grid.
bool Refusal(const wchar_t* tag, uint32_t reason, int32_t ack, const std::function<void(Producer&, Img&)>& breakIt)
{
	Producer p(tag);
	CHECK(p.blk);
	CutTapReader r(p.pid, p.ctlName);
	Img m1 = MakeImg(1, p.pid);
	CHECK(p.good(1, m1));
	CHECK(r.poll());
	CHECK(r.imageGeneration() == 1);
	const uint64_t gen = r.generation();
	Img m2 = MakeImg(2, p.pid);
	breakIt(p, m2);
	CHECK(!r.poll());
	CHECK(r.refusedBy(reason) == 1);
	CHECK(r.refused == 1 && r.accepted == 1);
	CHECK(p.blk->ackGeneration == 2 && p.blk->ackResult == ack && p.blk->ackServerPid == GetCurrentProcessId());
	CHECK(r.lastResult() == ack);
	CHECK(r.generation() == gen && r.imageGeneration() == 1 && r.hasGrid());   // the last good grid stays
	CHECK(GridEquals(r, m1));
	CHECK(!r.installFailure());
	CHECK(r.loudLines == 1);
	CHECK(r.lastWhy().find(CutRefusalName(reason)) != std::string::npos);
	CHECK(!r.poll());   // not retried: the same generation was acked
	CHECK(r.refused == 1);
	return true;
}

} // namespace

int main()
{
	Run("absent_without_a_control_block", [] {
		CutTapReader r(GetCurrentProcessId(), L"Local\\VPShellCutTapTest.none");
		CHECK(!r.poll());
		CHECK(r.state() == CutTapReader::State::Absent);
		CHECK(!r.installFailure() && !r.hasGrid() && r.generation() == 0);
		CHECK(r.bitsAt(10.f, 10.f) == 0);
		return true;
	});

	Run("accept_every_cell_and_ack", [] {
		Producer p(L"accept");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		CHECK(!r.poll());   // a block never written (seq 0)
		CHECK(r.state() == CutTapReader::State::Waiting);
		Img m = MakeImg(1, p.pid);
		CHECK(p.good(1, m));
		CHECK(r.poll());
		CHECK(r.state() == CutTapReader::State::Active && r.hasGrid());
		CHECK(r.generation() == 1 && r.imageGeneration() == 1 && r.loadSeq() == 1);
		CHECK(p.blk->ackGeneration == 1 && p.blk->ackResult == 0 && p.blk->ackServerPid == GetCurrentProcessId());
		CHECK(GridEquals(r, m));
		// outside the grid: 0 (negative cells, past the width / height)
		CHECK(r.bitsAt(-OX - 1.0f, 5.0f) == 0);
		float x, y;
		WorldOf(TX * 16, 3, x, y);
		CHECK(r.bitsAt(x, y) == 0);
		WorldOf(3, TY * 16, x, y);
		CHECK(r.bitsAt(x, y) == 0);
		const VPShellCutImage* h = r.imageHeader();
		CHECK(h && h->tilesX == TX && h->poolBlocks == BLOCKS && h->ox == OX);
		CHECK(!r.poll());   // nothing new
		CHECK(r.generation() == 1 && r.accepted == 1 && r.refused == 0 && r.loudLines == 0);
		return true;
	});

	Run("new_generation_moves_the_source", [] {
		Producer p(L"newgen");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m1 = MakeImg(1, p.pid);
		CHECK(p.good(1, m1));
		CHECK(r.poll());
		Img m2 = MakeImg(2, p.pid, 4);
		m2.pool[2 * 256 + 0x21] ^= 1;   // tile 12 (block 2), cell (81, 18)
		CHECK(p.good(2, m2));
		CHECK(r.poll());
		CHECK(r.generation() == 2 && r.imageGeneration() == 2 && r.loadSeq() == 4);
		CHECK(GridEquals(r, m2));
		float x, y;
		WorldOf(81, 18, x, y);
		CHECK((r.bitsAt(x, y) ^ Cell(m1, 81, 18)) == 1);
		return true;
	});

	Run("refused_block_magic", [] {
		return Refusal(L"r1", CREF_BLOCK_MAGIC, CUTACK_VERSION, [](Producer& p, Img& m) {
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.magic = 0x12345678; });
		});
	});
	Run("refused_block_pid", [] {
		return Refusal(L"r2", CREF_BLOCK, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.exePid ^= 1; });
		});
	});
	Run("refused_block_name", [] {
		return Refusal(L"r3", CREF_BLOCK, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.imageName[0] = L'X'; });
		});
	});
	Run("refused_stale", [] {
		Producer p(L"stale");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m3 = MakeImg(3, p.pid);
		CHECK(p.good(3, m3));
		CHECK(r.poll());
		Img m2 = MakeImg(2, p.pid);
		const auto b = m2.bytes();
		CHECK(p.section(2, b));
		p.publish(VPCUT_READY, 2, b, &m2);
		CHECK(!r.poll());
		CHECK(r.refusedBy(CREF_STALE) == 1 && p.blk->ackResult == CUTACK_STALE && r.imageGeneration() == 3);
		return true;
	});
	Run("refused_open", [] {
		return Refusal(L"r4", CREF_OPEN, CUTACK_OPEN, [](Producer& p, Img& m) {
			p.publish(VPCUT_READY, 2, m.bytes(), &m);   // no section made
		});
	});
	Run("refused_truncated_view", [] {
		return Refusal(L"r5", CREF_TRUNCATED, (int32_t)c5r::Err::Truncated, [](Producer& p, Img& m) {
			auto b = m.bytes();
			p.section(2, b);
			b.resize(70000);   // the block claims more than the section's 64 KB view holds
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.imageBytes = 70000; });
		});
	});
	Run("refused_image_magic", [] {
		return Refusal(L"r6", CREF_MAGIC, CUTACK_VERSION, [](Producer& p, Img& m) {
			m.h.magic = 0x11111111;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_image_version", [] {
		return Refusal(L"r7", CREF_VERSION, CUTACK_VERSION, [](Producer& p, Img& m) {
			m.h.version = 2;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_hash", [] {
		return Refusal(L"r8", CREF_HASH, CUTACK_HASH, [](Producer& p, Img& m) {
			auto b = m.bytes();
			p.publish(VPCUT_READY, 2, b, &m);   // the block's hash of the good bytes
			b[200] ^= 0x40;                      // the section holds other bytes
			p.section(2, b);
		});
	});
	Run("refused_hash_missing", [] {
		return Refusal(L"r8b", CREF_HASH, CUTACK_HASH, [](Producer& p, Img& m) {
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.fnv1a64 = 0; });
		});
	});
	Run("refused_disagree", [] {
		return Refusal(L"r9", CREF_DISAGREE, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.tilesX = 8; });
		});
	});
	Run("refused_sizes_width", [] {
		return Refusal(L"r10", CREF_SIZES, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.widthCells = 113;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_sizes_samples", [] {
		return Refusal(L"r11", CREF_SIZES, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.samplesX = 240;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_sizes_origin", [] {
		return Refusal(L"r12", CREF_SIZES, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.oy = std::nanf("");
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_sizes_tiles", [] {
		return Refusal(L"r13", CREF_SIZES, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.tilesX = 0;
			m.h.widthCells = 0;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m, 0, "", [](VPShellCutTapControl& c) { c.tilesX = 0; });
		});
	});
	Run("refused_sizes_pool", [] {
		return Refusal(L"r14", CREF_SIZES, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.poolBytes = BLOCKS * 256 - 1;
			m.h.imageBytes -= 1;
			m.pool.resize(BLOCKS * 256 - 1);
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_offset_misaligned", [] {
		return Refusal(L"r15", CREF_OFFSET, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.poolOffset += 8;
			m.h.imageBytes += 8;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_offset_in_header", [] {
		return Refusal(L"r16", CREF_OFFSET, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			m.h.dirOffset = 144;
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_dir_state", [] {
		return Refusal(L"r17", CREF_DIR, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			const uint16_t st = 7;
			memcpy(m.dir.data() + 4 * 8, &st, 2);
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_dir_index", [] {
		return Refusal(L"r18", CREF_DIR, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			const uint32_t k = BLOCKS;
			memcpy(m.dir.data() + 9 * 8 + 4, &k, 4);
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});
	Run("refused_dir_pooled_count", [] {
		return Refusal(L"r19", CREF_DIR, (int32_t)c5r::Err::BadLayout, [](Producer& p, Img& m) {
			const uint32_t used = 6;
			memcpy(m.h.gridHeader + VPCUT_G_USED, &used, 4);
			const auto b = m.bytes();
			p.section(2, b);
			p.publish(VPCUT_READY, 2, b, &m);
		});
	});

	Run("first_image_refused_is_an_install_failure", [] {
		Producer p(L"first");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m = MakeImg(1, p.pid);
		m.h.magic = 0;
		const auto b = m.bytes();
		CHECK(p.section(1, b));
		p.publish(VPCUT_READY, 1, b, &m);
		CHECK(!r.poll());
		CHECK(r.installFailure() && r.state() == CutTapReader::State::Failed);
		CHECK(!r.hasGrid() && r.bitsAt(5.f, 5.f) == 0);
		CHECK(r.lastWhy().find("magic") != std::string::npos);
		Img g = MakeImg(2, p.pid);   // a good one later clears it
		CHECK(p.good(2, g));
		CHECK(r.poll());
		CHECK(!r.installFailure() && r.state() == CutTapReader::State::Active);
		return true;
	});

	Run("producer_failed_pattern_is_an_install_failure", [] {
		Producer p(L"pattern");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m = MakeImg(1, p.pid);
		CHECK(p.good(1, m));
		CHECK(r.poll());
		p.publish(VPCUT_FAILED, 0, {}, nullptr, VPCUTF_PATTERN, "refused: pattern 3 '0x4A0A50' does not match");
		CHECK(r.poll());   // the grid is dropped: it is not the EXE's any more
		CHECK(r.installFailure() && r.state() == CutTapReader::State::Failed);
		CHECK(!r.hasGrid() && r.generation() == 2);
		CHECK(r.lastWhy().find("pattern 3") != std::string::npos);
		return true;
	});

	Run("producer_failed_too_big_without_an_image_is_an_install_failure", [] {
		Producer p(L"toobig");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		p.publish(VPCUT_FAILED, 0, {}, nullptr, VPCUTF_TOO_BIG, "image of 9 bytes, the limit is 1");
		CHECK(!r.poll());
		CHECK(r.installFailure() && r.lastWhy().find("limit") != std::string::npos);
		return true;
	});

	Run("off_drops_the_grid_loud_once", [] {
		Producer p(L"off");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m = MakeImg(1, p.pid);
		CHECK(p.good(1, m));
		CHECK(r.poll());
		p.publish(VPCUT_OFF, 0, {}, nullptr);
		CHECK(r.poll());
		CHECK(r.off() && r.state() == CutTapReader::State::Off && !r.installFailure());
		CHECK(!r.hasGrid() && r.generation() == 2);
		float x, y;
		WorldOf(1, 1, x, y);
		CHECK(r.bitsAt(x, y) == 0);
		CHECK(!r.poll());   // nothing more to drop
		CHECK(r.generation() == 2);
		return true;
	});

	Run("no_grid_keeps_the_last_grid", [] {
		Producer p(L"nogrid");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m = MakeImg(1, p.pid);
		CHECK(p.good(1, m));
		CHECK(r.poll());
		p.publish(VPCUT_NO_GRID, 0, {}, nullptr);
		CHECK(!r.poll());
		CHECK(r.noGrid() && r.hasGrid() && r.generation() == 1 && !r.installFailure());
		CHECK(GridEquals(r, m));
		return true;
	});

	Run("torn_block_counted", [] {
		Producer p(L"torn");
		CHECK(p.blk);
		CutTapReader r(p.pid, p.ctlName);
		Img m = MakeImg(1, p.pid);
		CHECK(p.good(1, m));
		InterlockedIncrement((volatile LONG*)&p.blk->seq);   // a writer that never finishes: seq odd
		CHECK(!r.poll());
		CHECK(r.torn == 1 && !r.hasGrid() && r.loudLines == 1);
		InterlockedIncrement((volatile LONG*)&p.blk->seq);
		CHECK(r.poll());
		CHECK(r.hasGrid());
		return true;
	});

	Run("load_image_file_form", [] {
		Img m = MakeImg(5, 1234);
		auto b = m.bytes();
		c5r::forest::DenseCut d;
		std::string why;
		CHECK(CutTapReader::loadImage(b.data(), b.size(), d, why).ok());
		CHECK(d.width() == TX * 16 && d.height() == TY * 16);
		float x, y;
		WorldOf(40, 20, x, y);
		CHECK(d.bitsAt(x, y) == Cell(m, 40, 20));
		c5r::forest::DenseCut e;
		const c5r::Status s = CutTapReader::loadImage(b.data(), b.size() - 1, e, why);
		CHECK(!s.ok() && s.code == c5r::Err::Truncated && e.width() == 0);
		b[0] ^= 1;
		CHECK(CutTapReader::loadImage(b.data(), b.size(), e, why).code == c5r::Err::BadMagic);
		return true;
	});

	printf("cut_tap_test: %d passed, %d failed, 0 skipped\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
