// srv_decal_tap.cpp - server.exe's reader of the decal tap and its file form (epic 0166, issue 4103). Contract:
// srv_decal_tap.h. Written 2026-11-15 for this repository (GPL-3.0-or-later), by the pattern of srv_water_tap.cpp
// (epic 3600). Layout: shell/common/shell_decal_tap.h; research/05-graphics-composition/terrain-decals.md section 10.
#include "srv_decal_tap.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdarg.h>

namespace srvrender {

namespace {

using c5r::decal::Decal;
using c5r::decal::DecalRect;
using c5r::decal::DecalSet;
using c5r::decal::DecalVertex;

const char* kStateNames[] = {"NONE", "WRITING", "READY", "FAILED", "OFF"};
const char* kRefusalNames[DREF_COUNT_] = {"none", "block_magic", "block", "stale", "open", "truncated", "magic", "version",
                                          "sizes", "hash", "disagree", "count", "offset", "align", "world", "rect", "record",
                                          "vertex", "texture", "name", "changed", "memory", "area", "walk"};
constexpr float kRectMargin = 1.0f;          // samples a rectangle may reach past the world rectangle (float rounding)
constexpr uint32_t kMaxTriangles = 0xFFFEu / 3;   // the EXE's limit: 3 x triangles <= 0xFFFE per decal (1.2)
constexpr uint32_t kMaxChanged = 2u * VPSHELL_DECAL_MAX_DECALS;
constexpr size_t kMaxQueuedChanges = 256;

std::string Fmt(const char* fmt, ...)
{
	char b[512];
	va_list a;
	va_start(a, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
	va_end(a);
	return b;
}

inline uint64_t FnvAdd(uint64_t h, const void* p, size_t n)
{
	const uint8_t* b = (const uint8_t*)p;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= b[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull;

enum SnapResult { SNAP_NEVER, SNAP_TORN, SNAP_OK };

// SnapBlock (srv_render_common.h) that tells "never written" from "busy or changing under the seqlock".
SnapResult SnapControl(const VPShellDecalTapControl* src, VPShellDecalTapControl& out)
{
	const volatile uint32_t* seq = (const volatile uint32_t*)&src->seq;
	for(int i = 0; i < 16; ++i)
	{
		const uint32_t s1 = *seq;
		if(s1 == 0)
			return SNAP_NEVER;
		if(s1 & 1)
		{
			YieldProcessor();
			continue;
		}
		MemoryBarrier();
		memcpy(&out, (const void*)src, sizeof(out));
		MemoryBarrier();
		if(*seq == s1)
		{
			out.seq = s1;
			return SNAP_OK;
		}
	}
	return SNAP_TORN;
}

inline bool Finite4(const float* r)
{
	return std::isfinite(r[0]) && std::isfinite(r[1]) && std::isfinite(r[2]) && std::isfinite(r[3]);
}

inline bool InsideWorld(const float* r, const float* w)
{
	return r[0] >= w[0] - kRectMargin && r[1] >= w[1] - kRectMargin && r[2] <= w[2] + kRectMargin && r[3] <= w[3] + kRectMargin;
}

struct RectKey
{
	uint32_t b[4];
	bool operator<(const RectKey& o) const { return memcmp(b, o.b, sizeof(b)) < 0; }
	bool operator==(const RectKey& o) const { return memcmp(b, o.b, sizeof(b)) == 0; }
};

inline RectKey KeyOf(const float* r)
{
	RectKey k;
	memcpy(k.b, r, sizeof(k.b));
	return k;
}

// ---- 0365: the delta (terrain-decals.md 19) ----------------------------------------------------------------------------
// A cheap key of a decal for the search of kept decals out of their order: rectangle, priority, pass, vertex count.
inline uint64_t DeltaKey(const Decal& d)
{
	uint32_t w[7];
	memcpy(w, d.rect, 16);
	memcpy(&w[4], &d.priority, 4);
	w[5] = d.pass;
	w[6] = d.vertexCount;
	uint64_t h = 0x9E3779B97F4A7C15ull;
	for(uint32_t v : w)
	{
		h ^= v;
		h *= 0xFF51AFD7ED558CCDull;
		h ^= h >> 33;
	}
	return h;
}

// The hashes and the index of `s` (records, names, vertices and walk fields filled) from `prev`, or false (info->why).
// Every decal whose rectangle is not one of the changed rectangles must equal one of prev's whose rectangle is not one
// either, one to one (so every decal added or removed is named by the changed list and the change queue needs no diff);
// a kept decal takes prev's hash (DecalContentHash covers exactly what Same compares) and its cells.
bool TryDelta(const VPShellDecalImage& img, const DecalSet& prev, DecalSet& s, const std::vector<DecalRect>& ch,
              DecalDeltaInfo& d)
{
	d = DecalDeltaInfo();
	auto no = [&](const std::string& w) {
		d.why = w;
		return false;
	};
	if(s.loadSeq != prev.loadSeq)
		return no(Fmt("a new load (%u after %u)", s.loadSeq, prev.loadSeq));
	if(memcmp(s.worldRect, prev.worldRect, sizeof(s.worldRect)) != 0)
		return no("the world rectangle changed");
	if(s.areaTilesX != prev.areaTilesX || s.areaTilesY != prev.areaTilesY)
		return no("the area block's grid changed");
	if(s.generation <= prev.generation)
		return no(Fmt("generation %u is not newer than the set in force (%u)", s.generation, prev.generation));
	if(img.changedSince == 0)
		return no("the image lists every decal (changedSince 0)");
	if(img.changedSince > prev.generation)
		return no(Fmt("its changes are since generation %u, after the set in force (%u)", img.changedSince, prev.generation));
	const size_t n = s.decals.size(), m = prev.decals.size();
	const double limit = (std::max)(64.0, kDecalDeltaMaxShare * (double)n);
	if((double)ch.size() > limit)
		return no(Fmt("%zu changed rectangles, more than %.0f", ch.size(), limit));
	d.tried = true;
	std::vector<RectKey> keys;
	keys.reserve(ch.size());
	for(const DecalRect& r : ch)
		keys.push_back(KeyOf(r.v));
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	auto touched = [&](const float* r) { return std::binary_search(keys.begin(), keys.end(), KeyOf(r)); };
	// the texture names: the set's index -> prev's (by name; the table is renumbered between generations)
	std::vector<uint32_t> texMap(s.textures.size(), c5r::decal::kNoTexture);
	{
		std::vector<std::pair<std::string, uint32_t>> pn;
		pn.reserve(prev.textures.size());
		for(uint32_t k = 0; k < (uint32_t)prev.textures.size(); ++k)
			pn.push_back(std::make_pair(prev.textures[k], k));
		std::sort(pn.begin(), pn.end());
		for(uint32_t k = 0; k < (uint32_t)s.textures.size(); ++k)
		{
			auto it = std::lower_bound(pn.begin(), pn.end(), std::make_pair(s.textures[k], 0u));
			if(it != pn.end() && it->first == s.textures[k])
				texMap[k] = it->second;
		}
	}
	auto texSame = [&](uint32_t a, uint32_t b) {
		const bool na = a >= s.textures.size(), nb = b >= prev.textures.size();   // kNoTexture (the only index past the table)
		if(na || nb)
			return na && nb;
		return texMap[a] == b || s.textures[a] == prev.textures[b];
	};
	auto same = [&](size_t i, size_t j) {
		const Decal &x = s.decals[i], &y = prev.decals[j];
		if(memcmp(x.rect, y.rect, sizeof(x.rect)) != 0 || memcmp(&x.priority, &y.priority, 4) != 0 || x.pass != y.pass ||
		   x.vertexCount != y.vertexCount)
			return false;
		for(int k = 0; k < 3; ++k)
			if(!texSame(x.texture[k], y.texture[k]))
				return false;
		if((uint64_t)x.firstVertex + x.vertexCount > s.vertices.size() || (uint64_t)y.firstVertex + y.vertexCount > prev.vertices.size())
			return false;
		return x.vertexCount == 0 ||
		       memcmp(s.vertices.data() + x.firstVertex, prev.vertices.data() + y.firstVertex, (size_t)x.vertexCount * sizeof(DecalVertex)) == 0;
	};
	std::vector<uint8_t> tNew(n), tPrev(m);
	for(size_t i = 0; i < n; ++i)
		tNew[i] = touched(s.decals[i].rect) ? 1 : 0;
	for(size_t j = 0; j < m; ++j)
	{
		tPrev[j] = touched(prev.decals[j].rect) ? 1 : 0;
		d.touchedPrev += tPrev[j];
	}
	std::vector<uint32_t> newOf(m, c5r::decal::kDeltaGone), added;
	added.reserve(ch.size() + 16);
	// in order (the image keeps the EXE's batch and list order, so kept decals mostly keep theirs: 4102, 9.1): each kept
	// decal against the next unused kept decal of prev; on a mismatch a search by key (built once, on the first one),
	// and the order resumes after the decal found
	std::vector<uint8_t> usedPrev(m, 0);
	std::vector<std::pair<uint64_t, uint32_t>> cand;
	bool candBuilt = false;
	size_t j = 0;
	for(size_t i = 0; i < n; ++i)
	{
		if(tNew[i])
		{
			added.push_back((uint32_t)i);
			continue;
		}
		while(j < m && (tPrev[j] || usedPrev[j]))
			++j;
		size_t found = m;
		if(j < m && same(i, j))
			found = j;
		else
		{
			if(!candBuilt)
			{
				for(size_t jj = 0; jj < m; ++jj)
					if(!tPrev[jj] && !usedPrev[jj])
						cand.push_back(std::make_pair(DeltaKey(prev.decals[jj]), (uint32_t)jj));
				std::sort(cand.begin(), cand.end());
				candBuilt = true;
			}
			const uint64_t key = DeltaKey(s.decals[i]);
			for(auto it = std::lower_bound(cand.begin(), cand.end(), std::make_pair(key, 0u)); it != cand.end() && it->first == key; ++it)
				if(!usedPrev[it->second] && same(i, it->second))
				{
					found = it->second;
					break;
				}
			if(found == m)
			{
				const float* r = s.decals[i].rect;
				return no(Fmt("refused: decal %zu (rectangle %g, %g .. %g, %g) is in no changed rectangle and equals no decal of the set in force",
				              i, r[0], r[1], r[2], r[3]));
			}
			d.reordered++;
		}
		usedPrev[found] = 1;
		newOf[found] = (uint32_t)i;
		s.decals[i].hash = prev.decals[found].hash;
		j = found + 1;
	}
	for(size_t jj = 0; jj < m; ++jj)
		if(!tPrev[jj] && !usedPrev[jj])
		{
			const float* r = prev.decals[jj].rect;
			return no(Fmt("refused: decal %zu of the set in force (rectangle %g, %g .. %g, %g) is gone but in no changed rectangle",
			              jj, r[0], r[1], r[2], r[3]));
		}
	for(uint32_t a : added)
		s.decals[a].hash = DecalContentHash(s, s.decals[a]);
	d.touchedNew = (uint32_t)added.size();
	c5r::decal::IndexDeltaStats st;
	d.gridFromPrev = c5r::decal::buildIndexFrom(s, prev, newOf, added, &d.gridWhy, &st);
	d.cellsSorted = st.cellsSorted;
	d.used = true;
	d.why.clear();
	return true;
}

uint32_t CheckImpl(const uint8_t* p, size_t n, const VPShellDecalTapControl* block, DecalSet& set,
                   std::vector<DecalRect>& changed, std::string& why, const DecalSet* prev, DecalDeltaInfo* info)
{
	if(!p || n < sizeof(VPShellDecalImage))
	{
		why = Fmt("%llu bytes, less than the %u-byte header", (unsigned long long)n, (unsigned)sizeof(VPShellDecalImage));
		return DREF_TRUNCATED;
	}
	VPShellDecalImage img;
	memcpy(&img, p, sizeof(img));
	if(img.magic != VPSHELL_DECAL_MAGIC)
	{
		why = Fmt("magic 0x%08X, expected 0x%08X", img.magic, VPSHELL_DECAL_MAGIC);
		return DREF_MAGIC;
	}
	if(img.version != VPSHELL_DECAL_IMAGE_VERSION)
	{
		why = Fmt("version %u, expected %u", img.version, VPSHELL_DECAL_IMAGE_VERSION);
		return DREF_VERSION;
	}
	if(img.headerBytes != sizeof(VPShellDecalImage) || img.decalBytes != sizeof(VPShellDecalRecord))
	{
		why = Fmt("header %u / record %u bytes, expected %u / %u", img.headerBytes, img.decalBytes,
		          (unsigned)sizeof(VPShellDecalImage), (unsigned)sizeof(VPShellDecalRecord));
		return DREF_SIZES;
	}
	if(block)
	{
		if(!block->fnv1a64)
		{
			why = "the block carries no image hash";
			return DREF_HASH;
		}
		const uint64_t h = DecalFnv1a64(p, n);
		if(h != block->fnv1a64)
		{
			why = Fmt("image FNV-1a 64 %016llX, the block says %016llX", (unsigned long long)h,
			          (unsigned long long)block->fnv1a64);
			return DREF_HASH;
		}
		if(img.imageBytes != block->imageBytes || img.imageBytes != n || img.generation != block->generation ||
		   img.loadSeq != block->loadSeq || img.decalCount != block->decalCount)
		{
			why = Fmt("image says %llu bytes, generation %u, load %u, %u decals; the block %llu, %u, %u, %u",
			          (unsigned long long)img.imageBytes, img.generation, img.loadSeq, img.decalCount,
			          (unsigned long long)block->imageBytes, block->generation, block->loadSeq, block->decalCount);
			return DREF_DISAGREE;
		}
	}
	else
	{
		if(img.imageBytes > n)
		{
			why = Fmt("the image says %llu bytes, the file holds %llu", (unsigned long long)img.imageBytes, (unsigned long long)n);
			return DREF_TRUNCATED;
		}
		if(img.imageBytes != n)
		{
			why = Fmt("the file holds %llu bytes, its image says %llu", (unsigned long long)n, (unsigned long long)img.imageBytes);
			return DREF_DISAGREE;
		}
	}
	if(img.imageBytes > VPSHELL_DECAL_MAX_BYTES || img.decalCount > VPSHELL_DECAL_MAX_DECALS ||
	   img.nameCount > VPSHELL_DECAL_MAX_NAMES || img.vertexCount > VPSHELL_DECAL_MAX_BYTES / sizeof(DecalVertex) ||
	   img.changedCount > kMaxChanged)
	{
		why = Fmt("%llu bytes, %u decals, %u names, %u vertices, %u changed (limits %llu, %u, %u, %llu, %u)",
		          (unsigned long long)img.imageBytes, img.decalCount, img.nameCount, img.vertexCount, img.changedCount,
		          (unsigned long long)VPSHELL_DECAL_MAX_BYTES, VPSHELL_DECAL_MAX_DECALS, VPSHELL_DECAL_MAX_NAMES,
		          (unsigned long long)(VPSHELL_DECAL_MAX_BYTES / sizeof(DecalVertex)), kMaxChanged);
		return DREF_COUNT;
	}
	// 4111: the area block (the EXE's tile+0x300 per tile); none when its offset and both counts are 0
	const bool hasArea = img.areaOffset || img.areaTilesX || img.areaTilesY;
	if(hasArea)
	{
		const float* w0 = img.worldRect;
		if(!Finite4(w0) || w0[0] > w0[2] || w0[1] > w0[3])
		{
			why = Fmt("world rectangle %g, %g .. %g, %g", w0[0], w0[1], w0[2], w0[3]);
			return DREF_WORLD;
		}
		const double wx = std::floor((double)w0[2] / 64) - std::floor((double)w0[0] / 64) + 1;
		const double wy = std::floor((double)w0[3] / 64) - std::floor((double)w0[1] / 64) + 1;
		if(!img.areaTilesX || !img.areaTilesY || img.areaTilesX > VPSHELL_DECAL_MAX_TILES_XY ||
		   img.areaTilesY > VPSHELL_DECAL_MAX_TILES_XY || (double)img.areaTilesX != wx || (double)img.areaTilesY != wy)
		{
			why = Fmt("area block of %u x %u tiles at %u; the world rectangle %g, %g .. %g, %g has %g x %g tiles (at most %u per axis)",
			          img.areaTilesX, img.areaTilesY, img.areaOffset, w0[0], w0[1], w0[2], w0[3], wx, wy, VPSHELL_DECAL_MAX_TILES_XY);
			return DREF_AREA;
		}
	}
	const uint64_t areaBits = hasArea ? (uint64_t)img.areaTilesX * img.areaTilesY : 0;
	// 0366: the walk block (none when its offset is 0): a header and one entry per record
	const bool hasWalk = img.walkOffset != 0;
	const uint64_t walkBytes = hasWalk ? sizeof(VPShellDecalWalkHeader) + (uint64_t)img.decalCount * sizeof(VPShellDecalWalk) : 0;
	struct Blk
	{
		const char* name;
		uint64_t off, bytes;
	} blk[6] = {{"decals", img.decalOffset, (uint64_t)img.decalCount * sizeof(VPShellDecalRecord)},
	            {"names", img.nameOffset, (uint64_t)img.nameCount * sizeof(VPShellDecalName)},
	            {"vertices", img.vertexOffset, (uint64_t)img.vertexCount * sizeof(DecalVertex)},
	            {"changed", img.changedOffset, (uint64_t)img.changedCount * 16},
	            {"area", img.areaOffset, (areaBits + 7) / 8},
	            {"walk", img.walkOffset, walkBytes}};
	for(const Blk& b : blk)
	{
		if(!b.bytes)
		{
			if(b.off > img.imageBytes)
			{
				why = Fmt("empty %s block at %llu, past the image's %llu bytes", b.name, (unsigned long long)b.off,
				          (unsigned long long)img.imageBytes);
				return DREF_OFFSET;
			}
			continue;
		}
		if(b.off < img.headerBytes || b.off + b.bytes > img.imageBytes)
		{
			why = Fmt("%s block %llu + %llu bytes outside %u .. %llu", b.name, (unsigned long long)b.off,
			          (unsigned long long)b.bytes, img.headerBytes, (unsigned long long)img.imageBytes);
			return DREF_OFFSET;
		}
		if(b.off % 16)
		{
			why = Fmt("%s block at %llu, not 16-byte aligned", b.name, (unsigned long long)b.off);
			return DREF_ALIGN;
		}
	}
	for(int i = 0; i < 6; ++i)
		for(int j = i + 1; j < 6; ++j)
			if(blk[i].bytes && blk[j].bytes && blk[i].off < blk[j].off + blk[j].bytes && blk[j].off < blk[i].off + blk[i].bytes)
			{
				why = Fmt("%s block %llu + %llu overlaps %s block %llu + %llu", blk[i].name, (unsigned long long)blk[i].off,
				          (unsigned long long)blk[i].bytes, blk[j].name, (unsigned long long)blk[j].off,
				          (unsigned long long)blk[j].bytes);
				return DREF_ALIGN;
			}
	const float* w = img.worldRect;
	if(!Finite4(w) || w[0] > w[2] || w[1] > w[3])
	{
		why = Fmt("world rectangle %g, %g .. %g, %g", w[0], w[1], w[2], w[3]);
		return DREF_WORLD;
	}
	// records
	std::vector<VPShellDecalRecord> recs(img.decalCount);
	if(img.decalCount)
		memcpy(recs.data(), p + img.decalOffset, (size_t)blk[0].bytes);
	for(uint32_t i = 0; i < img.decalCount; ++i)
	{
		const VPShellDecalRecord& r = recs[i];
		if(!Finite4(r.rect) || r.rect[0] > r.rect[2] || r.rect[1] > r.rect[3] || !InsideWorld(r.rect, w))
		{
			why = Fmt("decal %u: rectangle %g, %g .. %g, %g (world %g, %g .. %g, %g)", i, r.rect[0], r.rect[1], r.rect[2],
			          r.rect[3], w[0], w[1], w[2], w[3]);
			return DREF_RECT;
		}
		if(!std::isfinite(r.priority) || (r.flags & ~1u) || r.triangles == 0 || r.triangles > kMaxTriangles)
		{
			why = Fmt("decal %u: priority %g, flags 0x%X, %u triangles (1 .. %u)", i, r.priority, r.flags, r.triangles,
			          kMaxTriangles);
			return DREF_RECORD;
		}
		if((uint64_t)r.firstVertex + 3ull * r.triangles > img.vertexCount)
		{
			why = Fmt("decal %u: vertices %u + %u past the block's %u", i, r.firstVertex, 3 * r.triangles, img.vertexCount);
			return DREF_VERTEX;
		}
		for(int k = 0; k < 3; ++k)
			if(r.texture[k] != VPSHELL_DECAL_NO_TEXTURE && r.texture[k] >= img.nameCount)
			{
				why = Fmt("decal %u: texture %d = %u, %u names", i, k, r.texture[k], img.nameCount);
				return DREF_TEXTURE;
			}
	}
	// names
	std::vector<std::string> names(img.nameCount);
	for(uint32_t i = 0; i < img.nameCount; ++i)
	{
		const char* path = (const char*)(p + img.nameOffset + (size_t)i * sizeof(VPShellDecalName) + offsetof(VPShellDecalName, path));
		const void* z = memchr(path, 0, VPSHELL_DECAL_NAME_CHARS);
		if(!z || z == path)
		{
			why = Fmt("name %u: %s", i, z ? "empty" : "not zero-terminated within its 192 bytes");
			return DREF_NAME;
		}
		names[i].assign(path, (const char*)z);
	}
	// changed rectangles
	std::vector<DecalRect> ch(img.changedCount);
	if(img.changedCount)
		memcpy(ch.data(), p + img.changedOffset, (size_t)blk[3].bytes);
	for(uint32_t i = 0; i < img.changedCount; ++i)
	{
		const float* r = ch[i].v;
		if(!Finite4(r) || r[0] > r[2] || r[1] > r[3] || !InsideWorld(r, w))
		{
			why = Fmt("changed rectangle %u: %g, %g .. %g, %g", i, r[0], r[1], r[2], r[3]);
			return DREF_CHANGED;
		}
	}
	// the area block's bits: none set past the last tile
	std::vector<uint8_t> area;
	if(hasArea)
	{
		const uint8_t* a = p + img.areaOffset;
		const uint64_t nbytes = (areaBits + 7) / 8;
		if(areaBits % 8 && (a[nbytes - 1] >> (areaBits % 8)))
		{
			why = Fmt("area block: a bit past the map's %u x %u tiles is set (last byte 0x%02X)", img.areaTilesX, img.areaTilesY,
			          a[nbytes - 1]);
			return DREF_AREA;
		}
		area.resize((size_t)areaBits);
		for(uint64_t k = 0; k < areaBits; ++k)
			area[(size_t)k] = (a[k >> 3] >> (k & 7)) & 1u;
	}
	// 0366: the walk block: a grid of 1 .. 2^20 cells, a finite origin, a finite positive inverse, small + large = the
	// records; per record a cell inside the grid with a finite point, or the large list; the ranks of each cell, and of
	// the large list, exactly 0 .. count - 1
	c5r::decal::DecalWalk walk;
	if(hasWalk)
	{
		VPShellDecalWalkHeader wh;
		memcpy(&wh, p + img.walkOffset, sizeof(wh));
		if(!wh.cols || !wh.rows || (uint64_t)wh.cols * wh.rows > VPSHELL_DECAL_WALK_MAX_CELLS || !std::isfinite(wh.origin[0]) ||
		   !std::isfinite(wh.origin[1]) || !std::isfinite(wh.inverse[0]) || !std::isfinite(wh.inverse[1]) || !(wh.inverse[0] > 0) ||
		   !(wh.inverse[1] > 0) || (uint64_t)wh.smallCount + wh.largeCount != img.decalCount)
		{
			why = Fmt("walk block: %u x %u cells (at most %u), origin %g, %g, inverse %g, %g, %u small + %u large for %u records",
			          wh.cols, wh.rows, VPSHELL_DECAL_WALK_MAX_CELLS, wh.origin[0], wh.origin[1], wh.inverse[0], wh.inverse[1],
			          wh.smallCount, wh.largeCount, img.decalCount);
			return DREF_WALK;
		}
		const uint32_t cells = wh.cols * wh.rows;
		walk.origin[0] = wh.origin[0];
		walk.origin[1] = wh.origin[1];
		walk.inverse[0] = wh.inverse[0];
		walk.inverse[1] = wh.inverse[1];
		walk.cols = wh.cols;
		walk.rows = wh.rows;
		walk.cell.resize(img.decalCount);
		walk.rank.resize(img.decalCount);
		walk.point.resize((size_t)img.decalCount * 2);
		std::vector<uint32_t> perCell(cells, 0);
		uint32_t nSmall = 0, nLarge = 0;
		const uint8_t* we = p + img.walkOffset + sizeof(VPShellDecalWalkHeader);
		for(uint32_t i = 0; i < img.decalCount; ++i)
		{
			VPShellDecalWalk e;
			memcpy(&e, we + (size_t)i * sizeof(e), sizeof(e));
			if(e.cell == VPSHELL_DECAL_WALK_LARGE)
				nLarge++;
			else if(e.cell >= cells || !std::isfinite(e.point[0]) || !std::isfinite(e.point[1]))
			{
				why = Fmt("walk block: record %u in cell %u of %u, point %g, %g", i, e.cell, cells, e.point[0], e.point[1]);
				return DREF_WALK;
			}
			else
			{
				nSmall++;
				perCell[e.cell]++;
			}
			walk.cell[i] = e.cell;
			walk.rank[i] = e.rank;
			walk.point[2 * (size_t)i] = e.point[0];
			walk.point[2 * (size_t)i + 1] = e.point[1];
		}
		if(nSmall != wh.smallCount || nLarge != wh.largeCount)
		{
			why = Fmt("walk block: %u small and %u large records, the header says %u and %u", nSmall, nLarge, wh.smallCount, wh.largeCount);
			return DREF_WALK;
		}
		// ranks: a permutation of 0 .. count - 1 per cell and in the large list
		std::vector<uint32_t> start(cells + 1, 0);
		for(uint32_t c = 0; c < cells; ++c)
			start[c + 1] = start[c] + perCell[c];
		std::vector<uint8_t> seen((size_t)nSmall + nLarge, 0);
		for(uint32_t i = 0; i < img.decalCount; ++i)
		{
			const uint32_t cnt = walk.cell[i] == VPSHELL_DECAL_WALK_LARGE ? nLarge : perCell[walk.cell[i]];
			const size_t slot = walk.cell[i] == VPSHELL_DECAL_WALK_LARGE ? (size_t)nSmall + walk.rank[i] : (size_t)start[walk.cell[i]] + walk.rank[i];
			if(walk.rank[i] >= cnt || seen[slot])
			{
				why = Fmt("walk block: record %u has rank %u in %s of %u (%s)", i, walk.rank[i],
				          walk.cell[i] == VPSHELL_DECAL_WALK_LARGE ? "the large list" : "its cell", cnt,
				          walk.rank[i] >= cnt ? "out of range" : "taken twice");
				return DREF_WALK;
			}
			seen[slot] = 1;
		}
	}
	// the set
	DecalSet s;
	s.walk = std::move(walk);
	if(hasArea)
	{
		s.areaTilesX = img.areaTilesX;
		s.areaTilesY = img.areaTilesY;
		s.areaChanged.swap(area);
	}
	s.generation = img.generation;
	s.loadSeq = img.loadSeq;
	memcpy(s.worldRect, w, sizeof(s.worldRect));
	s.textures.swap(names);
	s.vertices.resize(img.vertexCount);
	if(img.vertexCount)
		memcpy(s.vertices.data(), p + img.vertexOffset, (size_t)blk[2].bytes);
	s.decals.resize(img.decalCount);
	for(uint32_t i = 0; i < img.decalCount; ++i)
	{
		const VPShellDecalRecord& r = recs[i];
		Decal& d = s.decals[i];
		memcpy(d.rect, r.rect, sizeof(d.rect));
		d.priority = r.priority;
		d.pass = r.flags & 1u;
		for(int k = 0; k < 3; ++k)
			d.texture[k] = r.texture[k] == VPSHELL_DECAL_NO_TEXTURE ? c5r::decal::kNoTexture : r.texture[k];
		d.firstVertex = r.firstVertex;
		d.vertexCount = 3 * r.triangles;
		d.listOrder = r.listOrder;
	}
	// 0365: the hashes and the index from the set in force when the image's changed rectangles allow it, else whole
	DecalDeltaInfo tmp;
	DecalDeltaInfo& di = info ? *info : tmp;
	di = DecalDeltaInfo();
	if(prev)
		TryDelta(img, *prev, s, ch, di);
	else
		di.why = "no set in force";
	if(!di.used)
	{
		for(Decal& d : s.decals)
			d.hash = DecalContentHash(s, d);
		c5r::decal::buildIndex(s);
	}
	set = std::move(s);
	changed.swap(ch);
	return DREF_NONE;
}

}  // namespace

const char* DecalRefusalName(uint32_t r) { return r < DREF_COUNT_ ? kRefusalNames[r] : "?"; }

int32_t DecalAckResult(uint32_t r)
{
	switch(r)
	{
	case DREF_NONE: return DTACK_OK;
	case DREF_BLOCK_MAGIC: case DREF_MAGIC: case DREF_VERSION: case DREF_SIZES: return DTACK_VERSION;
	case DREF_STALE: return DTACK_STALE;
	case DREF_OPEN: return DTACK_OPEN;
	case DREF_HASH: return DTACK_HASH;
	case DREF_TRUNCATED: case DREF_OFFSET: return (int32_t)c5r::Err::Truncated;
	case DREF_MEMORY: return (int32_t)c5r::Err::OutOfMemory;
	default: return (int32_t)c5r::Err::BadLayout;
	}
}

c5r::Err DecalErr(uint32_t r)
{
	switch(r)
	{
	case DREF_NONE: return c5r::Err::Ok;
	case DREF_BLOCK_MAGIC: case DREF_MAGIC: return c5r::Err::BadMagic;
	case DREF_VERSION: case DREF_SIZES: return c5r::Err::BadVersion;
	case DREF_OPEN: return c5r::Err::Io;
	case DREF_TRUNCATED: case DREF_OFFSET: return c5r::Err::Truncated;
	case DREF_MEMORY: return c5r::Err::OutOfMemory;
	default: return c5r::Err::BadLayout;
	}
}

uint64_t DecalFnv1a64(const uint8_t* p, size_t n) { return FnvAdd(kFnvBasis, p, n); }

bool DecalAreaAt(const DecalSet* set, int tileX, int tileY)
{
	if(!set)
		return false;
	bool known = false;
	const bool bit = c5r::decal::areaChangedAt(*set, tileX, tileY, known);
	if(known)
		return bit;
	return !c5r::decal::decalsForTile(*set, tileX * c5r::decal::kTileSamples, tileY * c5r::decal::kTileSamples).indices.empty();
}

uint64_t DecalContentHash(const DecalSet& set, const Decal& d)
{
	uint64_t h = kFnvBasis;
	h = FnvAdd(h, d.rect, sizeof(d.rect));
	h = FnvAdd(h, &d.priority, sizeof(d.priority));
	h = FnvAdd(h, &d.pass, sizeof(d.pass));
	for(int k = 0; k < 3; ++k)
	{
		if(d.texture[k] < set.textures.size())
			h = FnvAdd(h, set.textures[d.texture[k]].c_str(), set.textures[d.texture[k]].size() + 1);
		else
			h = FnvAdd(h, "\xFF", 1);   // no texture (or an index a hand-built set left outside)
	}
	if((uint64_t)d.firstVertex + d.vertexCount <= set.vertices.size())
		h = FnvAdd(h, set.vertices.data() + d.firstVertex, (size_t)d.vertexCount * sizeof(DecalVertex));
	return h;
}

uint32_t CheckDecalImage(const uint8_t* image, size_t n, const VPShellDecalTapControl* block, DecalSet& set,
                         std::vector<DecalRect>& changed, std::string& why)
{
	return CheckDecalImage(image, n, block, set, changed, why, nullptr, nullptr);
}

uint32_t CheckDecalImage(const uint8_t* image, size_t n, const VPShellDecalTapControl* block, DecalSet& set,
                         std::vector<DecalRect>& changed, std::string& why, const DecalSet* prev, DecalDeltaInfo* info)
{
	try
	{
		return CheckImpl(image, n, block, set, changed, why, prev, info);
	}
	catch(const std::bad_alloc&)
	{
		why = "allocation failed while building the set";
		return DREF_MEMORY;
	}
	catch(...)
	{
		why = "an exception while building the set";
		return DREF_MEMORY;
	}
}

DecalTapReader::DecalTapReader(uint32_t exePid) : m_pid(exePid)
{
	wchar_t name[64];
	_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_DECALTAP_CONTROL_FMT, exePid);
	m_controlName = name;
}

DecalTapReader::DecalTapReader(uint32_t exePid, const std::wstring& controlName) : m_pid(exePid), m_controlName(controlName) {}

DecalTapReader::~DecalTapReader()
{
	if(m_block)
		UnmapViewOfFile(m_block);
	if(m_control)
		CloseHandle(m_control);
}

bool DecalTapReader::openControl()
{
	const int64_t now = Qpc();
	if(now < m_nextOpenQpc)
		return false;
	m_nextOpenQpc = now + QpcFreq() / 4;   // a missing producer is looked for again in 250 ms
	HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, m_controlName.c_str());
	if(!h)
		return false;
	void* v = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_DECALTAP_CONTROL_BYTES);
	if(!v)
	{
		RNote("decals: control section '%ls' opens but does not map %u bytes (error %lu)", m_controlName.c_str(),
		      VPSHELL_DECALTAP_CONTROL_BYTES, GetLastError());
		CloseHandle(h);
		return false;
	}
	m_control = h;
	m_block = (VPShellDecalTapControl*)v;
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_counters.controlOpens++;
	}
	RNote("decals: control section '%ls' opened", m_controlName.c_str());
	return true;
}

void DecalTapReader::writeAck(uint32_t generation, int32_t result)
{
	VPShellDecalTapControl* t = m_block;
	t->ackServerPid = GetCurrentProcessId();
	InterlockedExchange((volatile LONG*)&t->ackResult, result);
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&t->ackGeneration, (LONG)generation);   // written last
}

void DecalTapReader::refuse(uint32_t reason, const std::string& why, bool fromFile)
{
	{
		std::lock_guard<std::mutex> g(m_mu);
		if(fromFile)
			m_counters.fileRefused++;
		else
			m_counters.refused++;
		if(reason < DREF_COUNT_)
			m_counters.refusedBy[reason]++;
		m_lastResult = DecalAckResult(reason);
		m_lastWhy = why;
	}
	RNote("decals: %s refused (%s, ackResult %d): %s", fromFile ? "file image" : "tap image", DecalRefusalName(reason),
	      DecalAckResult(reason), why.c_str());
}

void DecalTapReader::apply(DecalSet&& set, std::vector<DecalRect>&& changed, std::vector<uint8_t>&& raw, double ms,
                           const DecalDeltaInfo& delta, int64_t q0)
{
	std::shared_ptr<const DecalSet> prev = current();
	DecalChange c;
	c.generation = set.generation;
	c.previousGeneration = prev ? prev->generation : 0;
	memcpy(c.worldRect, set.worldRect, sizeof(c.worldRect));
	// the image's changed rectangles, plus every added or removed decal (by content) whose rectangle they do not hold.
	// 0365: after a delta there is none (every decal added or removed has its rectangle in the list: TryDelta), so the
	// diff of the two sets is skipped; its result would be the list itself.
	const bool skipDiff = delta.used && prev && prev->generation < set.generation;
	std::vector<RectKey> listed;
	if(!skipDiff)
	{
		listed.reserve(changed.size());
		for(const DecalRect& r : changed)
			listed.push_back(KeyOf(r.v));
		std::sort(listed.begin(), listed.end());
	}
	struct HI
	{
		uint64_t h;
		const float* r;
		bool operator<(const HI& o) const { return h < o.h; }
	};
	std::vector<HI> a, b;
	if(prev && !skipDiff)
	{
		a.reserve(prev->decals.size());
		for(const Decal& d : prev->decals)
			a.push_back({d.hash, d.rect});
	}
	if(!skipDiff)
	{
		b.reserve(set.decals.size());
		for(const Decal& d : set.decals)
			b.push_back({d.hash, d.rect});
	}
	std::sort(a.begin(), a.end());
	std::sort(b.begin(), b.end());
	std::vector<const float*> diff;
	size_t i = 0, j = 0;
	while(i < a.size() || j < b.size())
	{
		if(j == b.size() || (i < a.size() && a[i].h < b[j].h))
			diff.push_back(a[i++].r);
		else if(i == a.size() || b[j].h < a[i].h)
			diff.push_back(b[j++].r);
		else
		{
			++i;
			++j;
		}
	}
	c.rects = std::move(changed);
	uint64_t notListed = 0;
	for(const float* r : diff)
	{
		if(std::binary_search(listed.begin(), listed.end(), KeyOf(r)))
			continue;
		DecalRect x;
		memcpy(x.v, r, sizeof(x.v));
		c.rects.push_back(x);
		listed.insert(std::upper_bound(listed.begin(), listed.end(), KeyOf(r)), KeyOf(r));
		notListed++;
	}
	// 4111: the tiles whose area state changed (the EXE's tile+0x300, or the rule when one side has no area block)
	{
		const DecalSet* a0 = prev.get();
		const bool newArea = set.areaTilesX && set.areaTilesY, oldArea = a0 && a0->areaTilesX && a0->areaTilesY;
		const uint32_t tx = newArea ? set.areaTilesX : oldArea ? a0->areaTilesX : 0;
		const uint32_t ty = newArea ? set.areaTilesY : oldArea ? a0->areaTilesY : 0;
		for(uint32_t y = 0; y < ty; ++y)
			for(uint32_t x = 0; x < tx; ++x)
				if(DecalAreaAt(a0, (int)x, (int)y) != DecalAreaAt(&set, (int)x, (int)y))
				{
					c5r::decal::TileOrigin t;
					t.x = (int)x;
					t.y = (int)y;
					c.areaTiles.push_back(t);
				}
	}
	// 0393: the tie rule changed (the walk block appeared or went): every tile whose draw list differs between the two
	// sets is raised too, as a point rectangle at its centre (tilesForRects gives that tile alone). The changed rectangles
	// name only the decals that changed; without this the tiles baked under the old rule keep its order of equal
	// priorities (terrain-decals.md 20).
	const bool tieChange = prev && c5r::decal::hasWalk(*prev) != c5r::decal::hasWalk(set);
	double tieMs = 0;
	c5r::decal::TieRuleStats tieStats;
	if(tieChange)
	{
		const int64_t t0 = Qpc();
		const std::vector<c5r::decal::TileOrigin> tiles = c5r::decal::tieRuleTiles(*prev, set, &tieStats);
		c.rects.reserve(c.rects.size() + tiles.size());
		for(const c5r::decal::TileOrigin& t : tiles)
		{
			DecalRect x;
			x.v[0] = x.v[2] = (float)t.x + (float)(c5r::decal::kTileSamples / 2);
			x.v[1] = x.v[3] = (float)t.y + (float)(c5r::decal::kTileSamples / 2);
			c.rects.push_back(x);
		}
		c.tieTiles = (uint32_t)tiles.size();
		tieMs = QpcToMs(Qpc() - t0);
	}
	uint32_t areaSet = 0;
	for(uint8_t v : set.areaChanged)
		areaSet += v;
	const bool withArea = set.areaTilesX != 0;
	const bool withWalk = c5r::decal::hasWalk(set);   // 0366
	const uint32_t areaX = set.areaTilesX, areaY = set.areaTilesY;
	const size_t nArea = c.areaTiles.size();
	const uint32_t gen = set.generation, count = (uint32_t)set.decals.size();
	const size_t nrects = c.rects.size() - c.tieTiles;   // the log's "changed rectangles" as before 0393
	std::shared_ptr<const DecalSet> next = std::make_shared<const DecalSet>(std::move(set));
	std::shared_ptr<const std::vector<uint8_t>> img = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_set = next;
		m_raw = img;
		if(m_changes.size() >= kMaxQueuedChanges)
		{
			DecalChange all;
			all.generation = c.generation;
			all.previousGeneration = m_changes.front().previousGeneration;
			all.all = true;
			memcpy(all.worldRect, c.worldRect, sizeof(all.worldRect));
			m_changes.clear();
			m_changes.push_back(std::move(all));
			m_counters.changeOverflow++;
		}
		else
			m_changes.push_back(std::move(c));
		m_counters.changesQueued++;
		m_counters.rectsNotListed += notListed;
		m_counters.decals = count;
		m_counters.lastApplyMs = ms;
		m_counters.areaSets += withArea ? 1 : 0;
		m_counters.walkSets += withWalk ? 1 : 0;
		m_counters.areaTilesChanged += nArea;
		if(tieChange)
		{
			m_counters.tieRuleChanges++;
			m_counters.tieRuleTiles += tieStats.differ;
			m_counters.lastTieRuleMs = tieMs;
		}
		m_counters.areaSet = areaSet;
		if(delta.used)
			m_counters.deltaSets++;
		else
			m_counters.wholeSets++;
		if(!delta.used && delta.tried)
			m_counters.deltaRefused++;
		if(delta.used && !delta.gridFromPrev)
			m_counters.deltaGridWhole++;
		m_lastDelta = delta;
		m_lastResult = DTACK_OK;
		m_lastWhy.clear();
	}
	const double total = QpcToMs(Qpc() - q0);
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_counters.lastTotalMs = total;
	}
	// the text up to "%.1f ms" is the line before 0365 (the same measure: copy, checks and build), so render.log parsers
	// of both builds read it alike; after " | ": 0365's whole apply and how the set was built
	char how[400];
	if(delta.used)
		_snprintf_s(how, sizeof(how), _TRUNCATE, "delta: %u of the set in force and %u new decals in the changed rectangles, %u out of order, "
		            "grid %s%s%s, %u cells sorted", delta.touchedPrev, delta.touchedNew, delta.reordered,
		            delta.gridFromPrev ? "from the set in force" : "whole (", delta.gridFromPrev ? "" : delta.gridWhy.c_str(),
		            delta.gridFromPrev ? "" : ")", delta.cellsSorted);
	else
		_snprintf_s(how, sizeof(how), _TRUNCATE, "whole: %s", delta.why.empty() ? "delta off" : delta.why.c_str());
	// 0393: after the text of 0365, so the parsers of both read the line alike
	char tie[200] = "";
	if(tieChange)
		_snprintf_s(tie, sizeof(tie), _TRUNCATE, "; tie rule changed (%s -> %s): %u of %u tiles with decals raised for it, %.1f ms",
		            withWalk ? "the 4111 rule" : "the EXE's walk", withWalk ? "the EXE's walk" : "the 4111 rule", tieStats.differ,
		            tieStats.candidates, tieMs);
	RNote("decals: generation %u applied: %u decals, %llu changed rectangles (%llu not listed by the image), area %s (%u x %u tiles, "
	      "%u with tile+0x300, %zu tiles changed), tie order %s, %.1f ms | total %.1f ms, %s%s", gen, count, (unsigned long long)nrects,
	      (unsigned long long)notListed, withArea ? "from the EXE" : "not carried", areaX, areaY, areaSet, nArea,
	      withWalk ? "the EXE's walk" : "the 4111 rule", ms, total, how, tie);
}

bool DecalTapReader::poll()
{
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_counters.polls++;
	}
	if(!m_block && !openControl())
		return false;
	VPShellDecalTapControl tb;
	const SnapResult sr = SnapControl(m_block, tb);
	if(sr == SNAP_NEVER)
		return false;
	if(sr == SNAP_TORN)
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_counters.torn++;
		return false;
	}
	if(tb.state != m_loggedState)
	{
		RNote("decals: tap state %s (generation %u, load %u, %u decals, %llu bytes, fail reason %u)",
		      tb.state < 5 ? kStateNames[tb.state] : "?", tb.generation, tb.loadSeq, tb.decalCount,
		      (unsigned long long)tb.imageBytes, tb.failReason);
		m_loggedState = tb.state;
	}
	if(tb.state != VPDTAP_READY)
		return false;
	if(m_ackedAny && tb.generation == m_ackedGeneration)
		return false;
	const int64_t q0 = Qpc();
	uint32_t reason = DREF_NONE;
	std::string why;
	tb.imageName[63] = 0;
	if(tb.magic != VPSHELL_DECAL_MAGIC)
	{
		reason = DREF_BLOCK_MAGIC;
		why = Fmt("control block magic 0x%08X, expected 0x%08X", tb.magic, VPSHELL_DECAL_MAGIC);
	}
	if(!reason)
	{
		wchar_t expect[64];
		_snwprintf_s(expect, _countof(expect), _TRUNCATE, VPSHELL_DECAL_IMAGE_FMT, m_pid, tb.generation);
		if(tb.exePid != m_pid || tb.decalCount > VPSHELL_DECAL_MAX_DECALS || tb.imageBytes < sizeof(VPShellDecalImage) ||
		   tb.imageBytes > VPSHELL_DECAL_MAX_BYTES || wcscmp(expect, tb.imageName) != 0)
		{
			reason = DREF_BLOCK;
			why = Fmt("control block: pid %u (expected %u), %u decals, %llu bytes (limits %u, %llu), image '%ls' (expected '%ls')",
			          tb.exePid, m_pid, tb.decalCount, (unsigned long long)tb.imageBytes, VPSHELL_DECAL_MAX_DECALS,
			          (unsigned long long)VPSHELL_DECAL_MAX_BYTES, tb.imageName, expect);
		}
	}
	if(!reason)
	{
		const uint32_t cur = generation();
		if(cur && tb.generation <= cur)
		{
			reason = DREF_STALE;
			why = Fmt("generation %u, the applied set is generation %u", tb.generation, cur);
		}
	}
	std::vector<uint8_t> raw;
	if(!reason)
	{
		HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, tb.imageName);
		const uint8_t* view = h ? (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0) : nullptr;
		if(!view)
		{
			reason = DREF_OPEN;
			why = Fmt("cannot open the image section '%ls' (error %lu)", tb.imageName, GetLastError());
		}
		else
		{
			MEMORY_BASIC_INFORMATION mbi = {};
			const size_t size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
			if(tb.imageBytes > size)
			{
				reason = DREF_TRUNCATED;
				why = Fmt("image of %llu bytes, the section's view %llu bytes", (unsigned long long)tb.imageBytes,
				          (unsigned long long)size);
			}
			else
			{
				try
				{
					raw.assign(view, view + tb.imageBytes);   // copy first: every check runs on bytes no writer can change
				}
				catch(...)
				{
					reason = DREF_MEMORY;
					why = Fmt("cannot allocate %llu bytes for the image copy", (unsigned long long)tb.imageBytes);
				}
			}
			UnmapViewOfFile(view);
		}
		if(h)
			CloseHandle(h);
	}
	DecalSet set;
	std::vector<DecalRect> changed;
	DecalDeltaInfo delta;
	const std::shared_ptr<const DecalSet> prev = m_delta ? current() : nullptr;   // 0365: the base of a delta
	if(!reason)
		reason = CheckDecalImage(raw.data(), raw.size(), &tb, set, changed, why, prev.get(), &delta);
	if(!m_delta)
		delta.why = "delta off";
	writeAck(tb.generation, DecalAckResult(reason));   // the producer may close its section now
	m_ackedAny = true;
	m_ackedGeneration = tb.generation;
	if(reason)
	{
		refuse(reason, Fmt("generation %u: ", tb.generation) + why, false);
		return false;
	}
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_counters.accepted++;
	}
	apply(std::move(set), std::move(changed), std::move(raw), QpcToMs(Qpc() - q0), delta, q0);
	return true;
}

c5r::Status DecalTapReader::loadImage(const uint8_t* image, size_t n)
{
	const int64_t q0 = Qpc();
	std::vector<uint8_t> raw;
	uint32_t reason = DREF_NONE;
	std::string why;
	try
	{
		if(image && n)
			raw.assign(image, image + n);
	}
	catch(...)
	{
		reason = DREF_MEMORY;
		why = Fmt("cannot allocate %llu bytes for the image copy", (unsigned long long)n);
	}
	DecalSet set;
	std::vector<DecalRect> changed;
	DecalDeltaInfo delta;
	const std::shared_ptr<const DecalSet> prev = m_delta ? current() : nullptr;   // 0365: the base of a delta
	if(!reason)
		reason = CheckDecalImage(raw.data(), raw.size(), nullptr, set, changed, why, prev.get(), &delta);
	if(!m_delta)
		delta.why = "delta off";
	if(reason)
	{
		refuse(reason, why, true);
		return c5r::Status::fail(DecalErr(reason), std::string(DecalRefusalName(reason)) + ": " + why);
	}
	{
		std::lock_guard<std::mutex> g(m_mu);
		m_counters.fileLoads++;
	}
	apply(std::move(set), std::move(changed), std::move(raw), QpcToMs(Qpc() - q0), delta, q0);
	return c5r::Status::Ok();
}

c5r::Status DecalTapReader::loadFile(const std::wstring& path)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(f == INVALID_HANDLE_VALUE)
	{
		const std::string why = Fmt("cannot open '%ls' (error %lu)", path.c_str(), GetLastError());
		refuse(DREF_OPEN, why, true);
		return c5r::Status::fail(c5r::Err::Io, why);
	}
	LARGE_INTEGER sz = {};
	if(!GetFileSizeEx(f, &sz) || sz.QuadPart < 0 || (uint64_t)sz.QuadPart > VPSHELL_DECAL_MAX_BYTES)
	{
		CloseHandle(f);
		const std::string why = Fmt("'%ls': %lld bytes (limit %llu)", path.c_str(), (long long)sz.QuadPart,
		                            (unsigned long long)VPSHELL_DECAL_MAX_BYTES);
		refuse(DREF_COUNT, why, true);
		return c5r::Status::fail(c5r::Err::BadLayout, why);
	}
	std::vector<uint8_t> buf;
	try
	{
		buf.resize((size_t)sz.QuadPart);
	}
	catch(...)
	{
		CloseHandle(f);
		refuse(DREF_MEMORY, "cannot allocate the file's bytes", true);
		return c5r::Status::fail(c5r::Err::OutOfMemory, "cannot allocate the file's bytes");
	}
	size_t got = 0;
	while(got < buf.size())
	{
		DWORD r = 0;
		const DWORD want = (DWORD)std::min<size_t>(buf.size() - got, 1u << 24);
		if(!ReadFile(f, buf.data() + got, want, &r, nullptr) || r == 0)
			break;
		got += r;
	}
	CloseHandle(f);
	if(got != buf.size())
	{
		const std::string why = Fmt("'%ls': read %llu of %llu bytes", path.c_str(), (unsigned long long)got,
		                            (unsigned long long)buf.size());
		refuse(DREF_OPEN, why, true);
		return c5r::Status::fail(c5r::Err::Io, why);
	}
	return loadImage(buf.data(), buf.size());
}

c5r::Status DecalTapReader::writeCurrentImage(const std::wstring& path) const
{
	std::shared_ptr<const std::vector<uint8_t>> raw;
	{
		std::lock_guard<std::mutex> g(m_mu);
		raw = m_raw;
	}
	if(!raw)
		return c5r::Status::fail(c5r::Err::NotFound, "no decal set yet");
	const std::wstring tmp = path + L".tmp";
	HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot create '%ls' (error %lu)", tmp.c_str(), GetLastError()));
	size_t done = 0;
	bool ok = true;
	while(done < raw->size())
	{
		DWORD w = 0;
		const DWORD want = (DWORD)std::min<size_t>(raw->size() - done, 1u << 24);
		if(!WriteFile(f, raw->data() + done, want, &w, nullptr) || w == 0)
		{
			ok = false;
			break;
		}
		done += w;
	}
	ok = CloseHandle(f) && ok;
	if(!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		const DWORD le = GetLastError();
		DeleteFileW(tmp.c_str());
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot write '%ls' (error %lu)", path.c_str(), le));
	}
	return c5r::Status::Ok();
}

std::shared_ptr<const DecalSet> DecalTapReader::current() const
{
	std::lock_guard<std::mutex> g(m_mu);
	return m_set;
}

uint32_t DecalTapReader::generation() const
{
	std::lock_guard<std::mutex> g(m_mu);
	return m_set ? m_set->generation : 0;
}

bool DecalTapReader::drainChanges(std::vector<DecalChange>& out)
{
	std::lock_guard<std::mutex> g(m_mu);
	if(m_changes.empty())
		return false;
	for(DecalChange& c : m_changes)
		out.push_back(std::move(c));
	m_changes.clear();
	return true;
}

std::shared_ptr<const DecalSet> DecalTapReader::decalsForTile(int originX, int originY, c5r::decal::TileDecalList& out) const
{
	std::shared_ptr<const DecalSet> s = current();
	out = c5r::decal::TileDecalList();
	if(s)
		out = c5r::decal::decalsForTile(*s, originX, originY);
	return s;
}

DecalTapCounters DecalTapReader::counters() const
{
	std::lock_guard<std::mutex> g(m_mu);
	return m_counters;
}

int32_t DecalTapReader::lastResult() const
{
	std::lock_guard<std::mutex> g(m_mu);
	return m_lastResult;
}

std::string DecalTapReader::lastWhy() const
{
	std::lock_guard<std::mutex> g(m_mu);
	return m_lastWhy;
}

DecalDeltaInfo DecalTapReader::lastDelta() const
{
	std::lock_guard<std::mutex> g(m_mu);
	return m_lastDelta;
}

}  // namespace srvrender
