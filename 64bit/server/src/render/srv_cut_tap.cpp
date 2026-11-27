// srv_cut_tap.cpp - server.exe's reader of the cut-mask tap (ticket 4553). Contract: srv_cut_tap.h. Written 2026-11-16 for
// this repository (GPL-3.0-or-later), by the pattern of srv_ice_tap.cpp (epic 0169). Layout: shell/composite/cut_tap.h;
// research/05-graphics-composition/trees.md section 14.
#include "srv_cut_tap.h"
#include "srv_render_common.h"

#include <cmath>
#include <cstring>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <vector>

namespace srvrender {

namespace {

const char* kStateNames[] = {"NONE", "?", "READY", "FAILED", "OFF", "NO_GRID"};
const char* kRefusalNames[CREF_COUNT_] = {"none", "block_magic", "block", "stale", "open", "truncated", "magic", "version", "hash",
                                          "disagree", "sizes", "offset", "dir", "grid", "memory"};

std::string Fmt(const char* fmt, ...)
{
	char b[512];
	va_list a;
	va_start(a, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
	va_end(a);
	return b;
}

inline uint32_t U32(const uint8_t* p, size_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

inline uint16_t U16(const uint8_t* p, size_t off)
{
	uint16_t v;
	memcpy(&v, p + off, 2);
	return v;
}

inline uint32_t RoundUp16(uint32_t v) { return (v + 15u) & ~15u; }

enum SnapResult { SNAP_NEVER, SNAP_TORN, SNAP_OK };

SnapResult SnapControl(const VPShellCutTapControl* src, VPShellCutTapControl& out)
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

} // namespace

const char* CutRefusalName(uint32_t r) { return r < CREF_COUNT_ ? kRefusalNames[r] : "?"; }

int32_t CutAckResult(uint32_t r)
{
	switch(r)
	{
	case CREF_NONE: return CUTACK_OK;
	case CREF_BLOCK_MAGIC: case CREF_MAGIC: case CREF_VERSION: return CUTACK_VERSION;
	case CREF_STALE: return CUTACK_STALE;
	case CREF_OPEN: return CUTACK_OPEN;
	case CREF_HASH: return CUTACK_HASH;
	default: return (int32_t)CutErr(r);
	}
}

c5r::Err CutErr(uint32_t r)
{
	switch(r)
	{
	case CREF_NONE: return c5r::Err::Ok;
	case CREF_BLOCK_MAGIC: case CREF_MAGIC: return c5r::Err::BadMagic;
	case CREF_VERSION: return c5r::Err::BadVersion;
	case CREF_OPEN: return c5r::Err::Io;
	case CREF_TRUNCATED: return c5r::Err::Truncated;
	case CREF_MEMORY: return c5r::Err::OutOfMemory;
	default: return c5r::Err::BadLayout;
	}
}

uint32_t CheckCutImage(const uint8_t* p, size_t n, const VPShellCutTapControl* block, c5r::forest::DenseCut& out,
                       VPShellCutImage* hdrOut, std::string& why)
{
	if(n < sizeof(VPShellCutImage))
	{
		why = Fmt("%zu bytes, the header alone is %zu", n, sizeof(VPShellCutImage));
		return CREF_TRUNCATED;
	}
	VPShellCutImage h;
	memcpy(&h, p, sizeof(h));
	if(h.magic != VPSHELL_CUT_MAGIC)
	{
		why = Fmt("image magic 0x%08X, expected 0x%08X", h.magic, VPSHELL_CUT_MAGIC);
		return CREF_MAGIC;
	}
	if(h.version != VPSHELL_CUT_VERSION || h.headerBytes != sizeof(VPShellCutImage))
	{
		why = Fmt("image version %u header %u bytes, expected version %u header %zu", h.version, h.headerBytes, VPSHELL_CUT_VERSION,
		          sizeof(VPShellCutImage));
		return CREF_VERSION;
	}
	if(block)
	{
		if(block->fnv1a64 == 0 || VPCutFnv(VPCUT_FNV_INIT, p, n) != block->fnv1a64)
		{
			why = Fmt("FNV-1a 64 of the %zu bytes is not the block's %016llX", n, (unsigned long long)block->fnv1a64);
			return CREF_HASH;
		}
		if(h.generation != block->generation || h.exePid != block->exePid || h.loadSeq != block->loadSeq ||
		   h.tilesX != block->tilesX || h.tilesY != block->tilesY || h.poolBlocks != block->poolBlocks || h.imageBytes != block->imageBytes)
		{
			why = Fmt("image (generation %u, pid %u, load %u, tiles %u x %u, %u blocks, %llu bytes) and block (%u, %u, %u, %u x %u, %u, %llu) "
			          "disagree", h.generation, h.exePid, h.loadSeq, h.tilesX, h.tilesY, h.poolBlocks, (unsigned long long)h.imageBytes,
			          block->generation, block->exePid, block->loadSeq, block->tilesX, block->tilesY, block->poolBlocks,
			          (unsigned long long)block->imageBytes);
			return CREF_DISAGREE;
		}
	}
	if(h.imageBytes != n)
	{
		why = Fmt("the image says %llu bytes, %zu given", (unsigned long long)h.imageBytes, n);
		return h.imageBytes > n ? CREF_TRUNCATED : CREF_DISAGREE;
	}
	if(h.tilesX == 0 || h.tilesY == 0 || h.tilesX > VPSHELL_CUT_MAX_TILES || h.tilesY > VPSHELL_CUT_MAX_TILES ||
	   h.widthCells != h.tilesX * VPSHELL_CUT_TILE || h.heightCells != h.tilesY * VPSHELL_CUT_TILE)
	{
		why = Fmt("%u x %u tiles (1..%u), %u x %u cells (tiles x 16)", h.tilesX, h.tilesY, VPSHELL_CUT_MAX_TILES, h.widthCells,
		          h.heightCells);
		return CREF_SIZES;
	}
	if((int32_t)h.samplesX <= 0 || (int32_t)h.samplesY <= 0 || RoundUp16(h.samplesX >> 1) != h.widthCells ||
	   RoundUp16(h.samplesY >> 1) != h.heightCells)
	{
		why = Fmt("samples %u x %u do not give %u x %u cells", h.samplesX, h.samplesY, h.widthCells, h.heightCells);
		return CREF_SIZES;
	}
	if(!std::isfinite(h.ox) || !std::isfinite(h.oy))
	{
		why = Fmt("origin (%g, %g) not finite", h.ox, h.oy);
		return CREF_SIZES;
	}
	const uint64_t tiles = (uint64_t)h.tilesX * h.tilesY;
	if(h.dirBytes != tiles * VPSHELL_CUT_DIR_ENTRY || h.poolBytes != (uint64_t)h.poolBlocks * VPSHELL_CUT_BLOCK || h.poolBlocks > tiles ||
	   h.pooledTiles > h.poolBlocks)
	{
		why = Fmt("directory %u bytes (tiles x 8 = %llu), pool %u bytes for %u blocks, %u pooled tiles, %llu tiles", h.dirBytes,
		          (unsigned long long)(tiles * 8), h.poolBytes, h.poolBlocks, h.pooledTiles, (unsigned long long)tiles);
		return CREF_SIZES;
	}
	if(h.dirOffset < sizeof(VPShellCutImage) || (h.dirOffset & 15) || (h.poolOffset & 15) ||
	   (uint64_t)h.dirOffset + h.dirBytes > h.poolOffset || (uint64_t)h.poolOffset + h.poolBytes != h.imageBytes)
	{
		why = Fmt("directory at %u (%u bytes), pool at %u (%u bytes), image %llu bytes: inside the header, misaligned, out of order or "
		          "not ending the image", h.dirOffset, h.dirBytes, h.poolOffset, h.poolBytes, (unsigned long long)h.imageBytes);
		return (uint64_t)h.poolOffset + h.poolBytes > h.imageBytes ? CREF_TRUNCATED : CREF_OFFSET;
	}
	const uint8_t* dir = p + h.dirOffset;
	uint32_t pooled = 0;
	for(uint64_t t = 0; t < tiles; ++t)
	{
		const uint16_t st = U16(dir, (size_t)t * 8);
		if(st == 0)
		{
			const uint32_t k = U32(dir, (size_t)t * 8 + 4);
			if(k >= h.poolBlocks)
			{
				why = Fmt("tile %llu: pool index %u of %u blocks", (unsigned long long)t, k, h.poolBlocks);
				return CREF_DIR;
			}
			pooled++;
		}
		else if(st != 1)
		{
			why = Fmt("tile %llu: state %u (0 or 1)", (unsigned long long)t, st);
			return CREF_DIR;
		}
	}
	if(pooled != h.pooledTiles || pooled != U32(h.gridHeader, VPCUT_G_USED))
	{
		why = Fmt("%u pooled tiles, the image says %u, the grid's in-use count %u", pooled, h.pooledTiles, U32(h.gridHeader, VPCUT_G_USED));
		return CREF_DIR;
	}
	c5r::forest::DenseCut d;
	c5r::Status s;
	try
	{
		s = d.setFromBlocks(h.tilesX, h.tilesY, dir, h.dirBytes, p + h.poolOffset, h.poolBytes, h.ox, h.oy);
	}
	catch(const std::bad_alloc&)
	{
		why = "out of memory for the grid's cells";
		return CREF_MEMORY;
	}
	if(!s.ok())
	{
		why = s.str();
		return CREF_GRID;
	}
	out = std::move(d);
	if(hdrOut)
		*hdrOut = h;
	return CREF_NONE;
}

struct CutTapReader::Impl
{
	uint32_t pid = 0;
	std::wstring controlName;
	HANDLE ctl = nullptr;
	VPShellCutTapControl* block = nullptr;
	std::unique_ptr<c5r::forest::DenseCut> grid;   // null: no grid applied (bitsAt 0)
	VPShellCutImage hdr = {};
	uint64_t gen = 0;                 // the CutSource generation
	uint32_t imageGeneration = 0, appliedLoad = 0;
	uint32_t ackedGeneration = 0;
	bool ackedAny = false;
	uint32_t producerState = 0xFFFFFFFFu, producerFail = 0;
	uint32_t loggedState = 0xFFFFFFFFu, loggedFail = 0xFFFFFFFFu;
	bool producerFailed = false, refusedFirst = false, offLogged = false;
	int32_t lastResult = 0;
	uint64_t refusedBy[CREF_COUNT_] = {};
	std::string why;
	uint64_t louds = 0;
	uint64_t* loudOut = nullptr;   // the public loudLines counter

	bool openControl()
	{
		if(block)
			return true;
		ctl = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, controlName.c_str());
		if(!ctl)
			return false;
		block = (VPShellCutTapControl*)MapViewOfFile(ctl, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_CUTTAP_CONTROL_BYTES);
		if(!block)
		{
			CloseHandle(ctl);
			ctl = nullptr;
			return false;
		}
		RNote("cut: tap control block '%ls' opened", controlName.c_str());
		return true;
	}

	void writeAck(uint32_t generation, int32_t result)
	{
		lastResult = result;
		if(!block)
			return;
		block->ackResult = result;
		block->ackServerPid = GetCurrentProcessId();
		MemoryBarrier();
		block->ackGeneration = generation;   // last
	}

	// LOUD lines of play (trees.md 9.5): the first 20, then every 100th.
	void loud(const std::string& s)
	{
		++louds;
		if(loudOut)
			*loudOut = louds;
		if(louds <= 20 || louds % 100 == 0)
			RNote("LOUD: cut: %s (%llu so far)", s.c_str(), (unsigned long long)louds);
	}
};

CutTapReader::CutTapReader(uint32_t exePid) : m_impl(new Impl)
{
	m_impl->pid = exePid;
	wchar_t n[64];
	_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_CUTTAP_CONTROL_FMT, exePid);
	m_impl->controlName = n;
	m_impl->loudOut = &loudLines;
}

CutTapReader::CutTapReader(uint32_t exePid, const std::wstring& controlName) : m_impl(new Impl)
{
	m_impl->pid = exePid;
	m_impl->controlName = controlName;
	m_impl->loudOut = &loudLines;
}

CutTapReader::~CutTapReader()
{
	if(m_impl->block)
		UnmapViewOfFile(m_impl->block);
	if(m_impl->ctl)
		CloseHandle(m_impl->ctl);
}

bool CutTapReader::poll()
{
	Impl& I = *m_impl;
	polls++;
	if(!I.block && !I.openControl())
		return false;
	VPShellCutTapControl tb;
	const SnapResult sr = SnapControl(I.block, tb);
	if(sr == SNAP_NEVER)
		return false;
	if(sr == SNAP_TORN)
	{
		torn++;
		I.loud(Fmt("the tap's control block stayed torn for 16 reads (%llu torn polls; read again at the next poll)",
		           (unsigned long long)torn));
		return false;
	}
	tb.imageName[63] = 0;
	tb.failText[VPSHELL_CUT_FAILTEXT - 1] = 0;
	if(tb.state != I.loggedState || tb.failReason != I.loggedFail)
	{
		RNote("cut: tap state %s (generation %u, load %u, %u x %u tiles, %u blocks, %llu bytes, fail reason %u%s%s)",
		      tb.state < 6 ? kStateNames[tb.state] : "?", tb.generation, tb.loadSeq, tb.tilesX, tb.tilesY, tb.poolBlocks,
		      (unsigned long long)tb.imageBytes, tb.failReason, tb.failText[0] ? ": " : "", tb.failText);
		I.loggedState = tb.state;
		I.loggedFail = tb.failReason;
	}
	I.producerState = tb.state;
	I.producerFail = tb.failReason;
	I.producerFailed = tb.state == VPCUT_FAILED;
	if(I.producerFailed)
		I.why = std::string("the cut tap failed: ") + tb.failText;
	if(tb.state == VPCUT_OFF || (I.producerFailed && (tb.failReason == VPCUTF_PATTERN || tb.failReason == VPCUTF_EXCEPTION)))
	{
		if(tb.state == VPCUT_OFF && !I.offLogged)
		{
			I.offLogged = true;
			RNote("LOUD: cut: the cut tap is OFF (cut_tap=0, a test switch): the trees are placed with an empty cut mask");
		}
		if(I.grid)
		{
			I.grid.reset();   // the applied grid is not the EXE's any more
			I.imageGeneration = 0;
			I.gen++;
			return true;
		}
		return false;
	}
	if(tb.state != VPCUT_READY)
		return false;
	if(I.ackedAny && tb.generation == I.ackedGeneration)
		return false;
	const int64_t q0 = Qpc();
	uint32_t reason = CREF_NONE;
	std::string why;
	if(tb.magic != VPSHELL_CUT_MAGIC || tb.version != VPSHELL_CUT_VERSION || tb.headerBytes != sizeof(VPShellCutTapControl))
	{
		reason = CREF_BLOCK_MAGIC;
		why = Fmt("control block magic 0x%08X version %u header %u, expected 0x%08X version %u header %zu", tb.magic, tb.version,
		          tb.headerBytes, VPSHELL_CUT_MAGIC, VPSHELL_CUT_VERSION, sizeof(VPShellCutTapControl));
	}
	if(!reason)
	{
		wchar_t expect[64];
		_snwprintf_s(expect, _countof(expect), _TRUNCATE, VPSHELL_CUT_IMAGE_FMT, I.pid, tb.generation);
		if(tb.exePid != I.pid || tb.imageBytes < sizeof(VPShellCutImage) || tb.imageBytes > VPSHELL_CUT_MAX_BYTES ||
		   wcscmp(expect, tb.imageName) != 0)
		{
			reason = CREF_BLOCK;
			why = Fmt("control block: pid %u (expected %u), %llu bytes (limit %llu), image '%ls' (expected '%ls')", tb.exePid, I.pid,
			          (unsigned long long)tb.imageBytes, (unsigned long long)VPSHELL_CUT_MAX_BYTES, tb.imageName, expect);
		}
	}
	if(!reason && I.imageGeneration && tb.generation <= I.imageGeneration)
	{
		reason = CREF_STALE;
		why = Fmt("generation %u, the applied grid is generation %u", tb.generation, I.imageGeneration);
	}
	std::vector<uint8_t> raw;
	if(!reason)
	{
		HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, tb.imageName);
		const uint8_t* view = h ? (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0) : nullptr;
		if(!view)
		{
			reason = CREF_OPEN;
			why = Fmt("cannot open the image section '%ls' (error %lu)", tb.imageName, GetLastError());
		}
		else
		{
			MEMORY_BASIC_INFORMATION mbi = {};
			const size_t size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
			if(tb.imageBytes > size)
			{
				reason = CREF_TRUNCATED;
				why = Fmt("image of %llu bytes, the section's view %zu bytes", (unsigned long long)tb.imageBytes, size);
			}
			else
			{
				try
				{
					raw.assign(view, view + tb.imageBytes);   // copy first: every check runs on bytes no writer can change
				}
				catch(...)
				{
					reason = CREF_MEMORY;
					why = Fmt("cannot allocate %llu bytes for the image copy", (unsigned long long)tb.imageBytes);
				}
			}
			UnmapViewOfFile(view);
		}
		if(h)
			CloseHandle(h);
	}
	c5r::forest::DenseCut d;
	VPShellCutImage hdr = {};
	if(!reason)
		reason = CheckCutImage(raw.data(), raw.size(), &tb, d, &hdr, why);
	I.writeAck(tb.generation, CutAckResult(reason));
	I.ackedAny = true;
	I.ackedGeneration = tb.generation;
	if(reason)
	{
		refused++;
		I.refusedBy[reason]++;
		if(!accepted)
			I.refusedFirst = true;
		I.why = Fmt("generation %u refused (%s, ackResult %d): ", tb.generation, CutRefusalName(reason), CutAckResult(reason)) + why;
		I.loud(I.why + (I.grid ? " (the last good grid stays)" : " (no grid applied yet)"));
		return false;
	}
	accepted++;
	I.grid.reset(new (std::nothrow) c5r::forest::DenseCut(std::move(d)));
	if(!I.grid)
	{
		I.why = "out of memory for the applied grid";
		I.loud(I.why);
		I.imageGeneration = 0;
		I.gen++;
		return true;
	}
	I.hdr = hdr;
	I.imageGeneration = tb.generation;
	I.appliedLoad = tb.loadSeq;
	I.gen++;
	lastApplyMs = QpcToMs(Qpc() - q0);
	I.why.clear();
	RNote("cut: generation %u applied (load %u): %u x %u cells, %u pooled tiles of %u blocks, %u cells with bits 0-2, origin (%.4f, %.4f); "
	      "%llu bytes in %.3f ms",
	      tb.generation, tb.loadSeq, hdr.widthCells, hdr.heightCells, hdr.pooledTiles, hdr.poolBlocks, hdr.treeCells, hdr.ox, hdr.oy,
	      (unsigned long long)tb.imageBytes, lastApplyMs);
	return true;
}

void CutOverlay::set(std::shared_ptr<const c5r::forest::CutSource> src)
{
	m_src = std::move(src);
	++m_gen;
}

void CutOverlay::setRoads(std::shared_ptr<const c5r::forest::CutSource> roads, std::shared_ptr<const c5r::forest::CutSource> clear)
{
	m_roads = std::move(roads);
	m_clear = std::move(clear);
	++m_gen;
}

CutOverlay& CutOverlaySlot()
{
	static CutOverlay s;
	return s;
}

uint8_t CutTapReader::bitsAt(float worldX, float worldY) const
{
	const CutOverlay& o = CutOverlaySlot();   // ticket 4934: the EXE's route cells cleared first, then the overlays ORed in
	const uint8_t exe = m_impl->grid ? m_impl->grid->bitsAt(worldX, worldY) : 0;
	return (uint8_t)((exe & ~o.clearAt(worldX, worldY)) | o.bitsAt(worldX, worldY));
}

uint64_t CutTapReader::generation() const { return m_impl->gen + CutOverlaySlot().generation(); }

CutTapReader::State CutTapReader::state() const
{
	const Impl& I = *m_impl;
	if(installFailure())
		return State::Failed;
	if(!I.block)
		return State::Absent;
	if(I.producerState == VPCUT_OFF)
		return State::Off;
	if(I.grid)
		return State::Active;
	if(I.producerState == VPCUT_NO_GRID)
		return State::NoGrid;
	return State::Waiting;
}

bool CutTapReader::installFailure() const
{
	const Impl& I = *m_impl;
	return I.producerFailed || (I.refusedFirst && !accepted);
}

bool CutTapReader::off() const { return m_impl->producerState == VPCUT_OFF; }
bool CutTapReader::noGrid() const { return m_impl->producerState == VPCUT_NO_GRID; }
const std::string& CutTapReader::lastWhy() const { return m_impl->why; }
bool CutTapReader::hasGrid() const { return m_impl->grid != nullptr; }
uint32_t CutTapReader::imageGeneration() const { return m_impl->imageGeneration; }
uint32_t CutTapReader::loadSeq() const { return m_impl->appliedLoad; }
const VPShellCutImage* CutTapReader::imageHeader() const { return m_impl->grid ? &m_impl->hdr : nullptr; }
uint32_t CutTapReader::producerState() const { return m_impl->producerState; }
int32_t CutTapReader::lastResult() const { return m_impl->lastResult; }
uint64_t CutTapReader::refusedBy(uint32_t reason) const { return reason < CREF_COUNT_ ? m_impl->refusedBy[reason] : 0; }

c5r::Status CutTapReader::loadImage(const uint8_t* image, size_t n, c5r::forest::DenseCut& out, std::string& why)
{
	const uint32_t r = CheckCutImage(image, n, nullptr, out, nullptr, why);
	if(r)
		return c5r::Status::fail(CutErr(r), std::string(CutRefusalName(r)) + ": " + why);
	return c5r::Status::Ok();
}

} // namespace srvrender
