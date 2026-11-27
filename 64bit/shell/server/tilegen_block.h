// tilegen_block.h - the tile content-generation block (issue 3407, epic 3400): one u32 per EXE terrain tile, written by a
// producer, read by shell_server.exe --scene terrain to re-bake tiles whose content changed.
//
// Why a block of its own: the stage-1 bake reads the height field and the material bytes (the tap), but roads and
// improvements change neither; the EXE marks their tiles by raising tile+0x1CC (0x4387D0 stamps the tiles of a world
// rectangle with the frame counter, research/05-graphics-composition/terrain-bake-residency.md section 3). Until the DLL
// sends plot changes over the proxy route (epic 3300), a producer copies those counters: shell/tools/tilegen_feed.py
// reads tile+0x1CC of every tile by PID (read-only) and publishes them here; the tests write the block themselves.
//
// Layout (little-endian, a pagefile-backed named section, at least 64 + 4 * tilesX * tilesY bytes):
//   0  u32 magic 'TGEN' (0x4E454754)   4 u32 version (1)   8 u32 seq (odd while the producer writes; even = stable)
//   12 u32 tilesX                      16 u32 tilesY       20 u32 producerPid
//   24 u64 writeQpc (producer's QueryPerformanceCounter at the last write)   32 u32 writes (count)   36..63 reserved (0)
//   64 u32 gen[tilesX * tilesY], the EXE's order: index = x * tilesY + y (column-major, TR+0x1ADC).
// A reader snapshots under the seqlock (seq even and unchanged across the copy). The first snapshot after the server opens
// the block is its baseline (nothing is invalidated); afterwards every tile whose value differs from the previous
// snapshot gets a new content generation in the server (any change counts, not only a rise: a reload resets the EXE's
// counters).
#pragma once
#include <cstdint>

#define VPTILEGEN_MAGIC 0x4E454754u
#define VPTILEGEN_VERSION 1u
#define VPTILEGEN_HEADER_BYTES 64u
#define VPTILEGEN_MAX_TILES (256u * 256u)

#pragma pack(push, 4)
struct VPTileGenHeader
{
	uint32_t magic;
	uint32_t version;
	volatile uint32_t seq;
	uint32_t tilesX, tilesY;
	uint32_t producerPid;
	uint64_t writeQpc;
	uint32_t writes;
	uint32_t reserved[7];
};
#pragma pack(pop)
static_assert(sizeof(VPTileGenHeader) == VPTILEGEN_HEADER_BYTES, "VPTileGenHeader must be 64 bytes");

// The reader (header-only since wave 3, 2026-11-13: the residency moved to render/ as c5r::bake::Residency, and
// shell/build.cmd gains no source of ours). poll() snapshots the block and raises the changed tiles through raiseTile.
// Errors (block missing, bad magic / version, a size that is not the field's) are counted and logged once per kind; the
// block is re-opened once per second while missing.
#ifdef _WINDOWS_
#include <string>
#include <vector>
#include "c5r/bake/residency.h"

class TileGenReader
{
public:
	typedef void (*LogFn)(const char* line);
	~TileGenReader() { close(); }
	void setName(const wchar_t* name) { close(); name_ = name ? name : L""; }
	void close()
	{
		if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
		if (h_) { CloseHandle(h_); h_ = nullptr; }
		size_ = 0; baseline = false; prev_.clear();
	}
	int poll(c5r::bake::Residency& r, double nowMs, LogFn log)
	{
		if (name_.empty() || !r.hasField()) return 0;
		auto fail = [&](const std::string& e) {
			errors++;
			if (e != lastErr_ && log) { std::string l = "residency: tile generation block: " + e; log(l.c_str()); }
			lastErr_ = e;
			return 0;
		};
		if (!view_)
		{
			if (nowMs - lastOpenMs_ < 1000.0) return 0;
			lastOpenMs_ = nowMs;
			HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name_.c_str());
			if (!h) return fail("not found (retried every second)");
			const uint8_t* v = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
			if (!v) { CloseHandle(h); return fail("cannot be mapped"); }
			MEMORY_BASIC_INFORMATION mbi = {};
			size_t sz = VirtualQuery(v, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
			h_ = h; view_ = v; size_ = sz;
			baseline = false; prev_.clear(); lastSeq_ = 0;
			if (log) { char b[200]; sprintf_s(b, "residency: tile generation block '%ls' opened (%zu bytes)", name_.c_str(), sz); log(b); }
		}
		if (size_ < VPTILEGEN_HEADER_BYTES) { close(); return fail("smaller than its header"); }
		const VPTileGenHeader* hd = (const VPTileGenHeader*)view_;
		uint32_t s1 = hd->seq;
		if (s1 == lastSeq_ || (s1 & 1)) return 0;
		MemoryBarrier();
		// a content error is reported once per published seq (the producer must write a new one to be read again)
		if (hd->magic != VPTILEGEN_MAGIC) { lastSeq_ = s1; return fail("bad magic"); }
		if (hd->version != VPTILEGEN_VERSION) { lastSeq_ = s1; return fail("version " + std::to_string(hd->version) + " (want 1)"); }
		uint32_t tx = hd->tilesX, ty = hd->tilesY;
		if (tx != r.tilesX() || ty != r.tilesY())
		{
			lastSeq_ = s1;
			return fail("tiles " + std::to_string(tx) + "x" + std::to_string(ty) + " but the field has " + std::to_string(r.tilesX()) +
				"x" + std::to_string(r.tilesY()));
		}
		size_t n = (size_t)tx * ty;
		if (size_ < VPTILEGEN_HEADER_BYTES + 4 * n) { lastSeq_ = s1; return fail("shorter than its tiles"); }
		std::vector<uint32_t> cur(n);
		memcpy(cur.data(), view_ + VPTILEGEN_HEADER_BYTES, 4 * n);
		MemoryBarrier();
		if (hd->seq != s1) { tornReads++; return 0; }
		lastSeq_ = s1;
		snapshots++;
		lastErr_.clear();
		// the signal's time: the producer's QueryPerformanceCounter at its write (one clock for every process of the machine)
		double sig = nowMs;
		uint64_t wq = hd->writeQpc;
		if (wq)
		{
			LARGE_INTEGER f; QueryPerformanceFrequency(&f);
			double w = (double)wq * 1000.0 / (double)f.QuadPart;
			if (w <= nowMs + 1.0 && w > nowMs - 600000.0) sig = w;
		}
		int raisedNow = 0;
		if (baseline && prev_.size() == n)
		{
			for (size_t k = 0; k < n; ++k)
				if (cur[k] != prev_[k] && r.raiseTile((uint32_t)(k / ty), (uint32_t)(k % ty), sig)) raisedNow++;   // EXE order x * tilesY + y
			raised += raisedNow;
			if (raisedNow && log)
			{
				char b[160]; sprintf_s(b, "residency: tile generation block seq %u: %d tiles changed, re-bake requested", s1, raisedNow); log(b);
			}
		}
		prev_.swap(cur);
		baseline = true;
		return raisedNow;
	}
	int64_t snapshots = 0, raised = 0, errors = 0, tornReads = 0;
	bool baseline = false;

private:
	std::wstring name_;
	HANDLE h_ = nullptr;
	const uint8_t* view_ = nullptr;
	size_t size_ = 0;
	uint32_t lastSeq_ = 0;
	double lastOpenMs_ = -1e18;
	std::vector<uint32_t> prev_;
	std::string lastErr_;
};
#endif
