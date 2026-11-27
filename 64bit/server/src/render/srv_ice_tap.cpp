// srv_ice_tap.cpp - server.exe's reader of the ice tap and its file form (epic 0169, issue 4307). Contract:
// srv_ice_tap.h. Written 2026-11-15 for this repository (GPL-3.0-or-later), by the pattern of srv_decal_tap.cpp (epic
// 0166). Layout: shell/common/shell_ice_tap.h; research/05-graphics-composition/polar-ice.md section 9.
#include "srv_ice_tap.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdarg.h>

namespace srvrender {

namespace {

const char* kStateNames[] = {"NONE", "WRITING", "READY", "FAILED", "OFF", "NO_MESH"};
const char* kRefusalNames[IREF_COUNT_] = {"none", "block_magic", "block", "stale", "open", "truncated", "magic", "version",
                                          "sizes", "hash", "disagree", "count", "offset", "fields", "chunk", "index", "memory"};
constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull;

// The Ice object's offsets the draw reads (polar-ice.md 7; the same list as c5r::ice's setMesh checks): each a float.
const uint32_t kReadFields[] = {
    0xFF0C, 0xFF14, 0xFF1C, 0xFF20, 0xFF24, 0xFF28, 0xFF38, 0xFF3C, 0xFF40, 0xFF44, 0xFF48, 0xFF4C, 0xFFC0, 0xFFC4,
    0xFFD8, 0xFFDC, 0xFFE0, 0xFFE4, 0xFFE8, 0xFFEC, 0xFFF0, 0xFFF4, 0xFFF8, 0xFFFC, 0x10000, 0x10004, 0x10008, 0x1000C,
    0x10010, 0x10014, 0x10018, 0x1001C,
};

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

inline float F32(const uint8_t* p, size_t off)
{
	float v;
	memcpy(&v, p + off, 4);
	return v;
}

enum SnapResult { SNAP_NEVER, SNAP_TORN, SNAP_OK };

// SnapBlock (srv_render_common.h) that tells "never written" from "busy or changing under the seqlock".
SnapResult SnapControl(const VPShellIceTapControl* src, VPShellIceTapControl& out)
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

uint32_t CheckImpl(const uint8_t* p, size_t n, const VPShellIceTapControl* block, c5r::ice::Mesh& out, std::string& why)
{
	if(!p || n < sizeof(VPShellIceImage))
	{
		why = Fmt("image of %llu bytes, shorter than its header (%u)", (unsigned long long)n, (unsigned)sizeof(VPShellIceImage));
		return IREF_TRUNCATED;
	}
	VPShellIceImage h;
	memcpy(&h, p, sizeof(h));
	if(h.magic != VPSHELL_ICE_MAGIC)
	{
		why = Fmt("image magic 0x%08X, expected 0x%08X", h.magic, VPSHELL_ICE_MAGIC);
		return IREF_MAGIC;
	}
	if(h.version != VPSHELL_ICE_VERSION)
	{
		why = Fmt("image version %u, this reader knows %u", h.version, VPSHELL_ICE_VERSION);
		return IREF_VERSION;
	}
	if(h.headerBytes != sizeof(VPShellIceImage) || h.chunkBytes != sizeof(VPShellIceChunk) ||
	   h.fieldsBase != VPSHELL_ICE_FIELDS_BASE || h.fieldsBytes != VPSHELL_ICE_FIELDS_BYTES || h.vbStride != VPSHELL_ICE_STRIDE ||
	   h.ibFormat != VPSHELL_ICE_INDEX_FORMAT)
	{
		why = Fmt("header %u / chunk %u bytes, fields +0x%X x %u, stride %u, index format %u (expected %u / %u, +0x%X x %u, %u, %u)",
		          h.headerBytes, h.chunkBytes, h.fieldsBase, h.fieldsBytes, h.vbStride, h.ibFormat, (unsigned)sizeof(VPShellIceImage),
		          (unsigned)sizeof(VPShellIceChunk), VPSHELL_ICE_FIELDS_BASE, VPSHELL_ICE_FIELDS_BYTES, VPSHELL_ICE_STRIDE,
		          VPSHELL_ICE_INDEX_FORMAT);
		return IREF_SIZES;
	}
	if(h.imageBytes != n)
	{
		why = Fmt("the image says %llu bytes, %s %llu", (unsigned long long)h.imageBytes, block ? "the copy holds" : "the file has",
		          (unsigned long long)n);
		return block ? IREF_DISAGREE : (h.imageBytes > n ? IREF_TRUNCATED : IREF_DISAGREE);
	}
	if(block)
	{
		if(!block->fnv1a64)
		{
			why = "the control block carries no image hash";
			return IREF_HASH;
		}
		const uint64_t f = IceFnv1a64(p, n);
		if(f != block->fnv1a64)
		{
			why = Fmt("image FNV-1a 64 %016llX, the block says %016llX", (unsigned long long)f, (unsigned long long)block->fnv1a64);
			return IREF_HASH;
		}
		if(h.generation != block->generation || h.loadSeq != block->loadSeq || h.exePid != block->exePid ||
		   h.chunkCount != block->chunkCount || h.imageBytes != block->imageBytes)
		{
			why = Fmt("image generation %u load %u pid %u, %u chunks, %llu bytes; the block %u / %u / %u, %u, %llu", h.generation,
			          h.loadSeq, h.exePid, h.chunkCount, (unsigned long long)h.imageBytes, block->generation, block->loadSeq,
			          block->exePid, block->chunkCount, (unsigned long long)block->imageBytes);
			return IREF_DISAGREE;
		}
	}
	if(h.chunkCount == 0 || h.chunkCount > VPSHELL_ICE_MAX_CHUNKS || h.vbBytes == 0 || h.vbBytes % VPSHELL_ICE_STRIDE ||
	   h.ibBytes == 0 || h.ibBytes % 2 || h.vbBytes > VPSHELL_ICE_MAX_BYTES || h.ibBytes > VPSHELL_ICE_MAX_BYTES)
	{
		why = Fmt("%u chunks (1..%u), VB %u bytes (whole 8-byte vertices), IB %u bytes (whole R16 indices)", h.chunkCount,
		          VPSHELL_ICE_MAX_CHUNKS, h.vbBytes, h.ibBytes);
		return IREF_COUNT;
	}
	// the blocks: fields, chunks, VB, IB in this order after the header, 16-byte aligned, inside the image
	const uint64_t ends[4] = {(uint64_t)h.fieldsOffset + h.fieldsBytes, (uint64_t)h.chunkOffset + (uint64_t)h.chunkCount * h.chunkBytes,
	                          (uint64_t)h.vbOffset + h.vbBytes, (uint64_t)h.ibOffset + h.ibBytes};
	const uint32_t offs[4] = {h.fieldsOffset, h.chunkOffset, h.vbOffset, h.ibOffset};
	const char* names[4] = {"fields", "chunks", "VB", "IB"};
	uint64_t prevEnd = h.headerBytes;
	for(int b = 0; b < 4; ++b)
	{
		if(ends[b] > n)
		{
			why = Fmt("the %s block %u + %llu bytes lies past the image's %llu", names[b], offs[b],
			          (unsigned long long)(ends[b] - offs[b]), (unsigned long long)n);
			return IREF_TRUNCATED;
		}
		if(offs[b] % 16 || offs[b] < prevEnd)
		{
			why = Fmt("the %s block at %u is misaligned or overlaps the block before it (ending at %llu)", names[b], offs[b],
			          (unsigned long long)prevEnd);
			return IREF_OFFSET;
		}
		prevEnd = ends[b];
	}
	// the fields
	const uint8_t* fld = p + h.fieldsOffset;
	auto at = [&](uint32_t off) { return off - VPSHELL_ICE_FIELDS_BASE; };
	if(fld[at(0xFEA5)] != 1 || U32(fld, at(0xFF30)) != h.chunkCount || U32(fld, at(0xFEE4)) != h.vbHandle ||
	   U32(fld, at(0xFEE8)) != h.ibHandle)
	{
		why = Fmt("fields: built flag %u, count %u, VB 0x%X, IB 0x%X; the image %u chunks, VB 0x%X, IB 0x%X", fld[at(0xFEA5)],
		          U32(fld, at(0xFF30)), U32(fld, at(0xFEE4)), U32(fld, at(0xFEE8)), h.chunkCount, h.vbHandle, h.ibHandle);
		return IREF_FIELDS;
	}
	for(uint32_t off : kReadFields)
		if(!std::isfinite(F32(fld, at(off))))
		{
			why = Fmt("fields: Ice+0x%X is not finite", off);
			return IREF_FIELDS;
		}
	if(!(F32(fld, at(0xFF1C)) <= F32(fld, at(0xFF24)) && F32(fld, at(0xFF20)) <= F32(fld, at(0xFF28))))
	{
		why = Fmt("fields: the mesh box (%g, %g) .. (%g, %g) has min > max", F32(fld, at(0xFF1C)), F32(fld, at(0xFF20)),
		          F32(fld, at(0xFF24)), F32(fld, at(0xFF28)));
		return IREF_FIELDS;
	}
	// the chunks
	const uint32_t vertices = h.vbBytes / VPSHELL_ICE_STRIDE, indices = h.ibBytes / 2;
	const uint8_t* ib = p + h.ibOffset;
	std::vector<c5r::ice::Chunk> chunks;
	try
	{
		chunks.resize(h.chunkCount);
	}
	catch(...)
	{
		why = "cannot allocate the chunks";
		return IREF_MEMORY;
	}
	for(uint32_t c = 0; c < h.chunkCount; ++c)
	{
		VPShellIceChunk k;
		memcpy(&k, p + h.chunkOffset + (size_t)c * sizeof(k), sizeof(k));
		if(!std::isfinite(k.box[0]) || !std::isfinite(k.box[1]) || !std::isfinite(k.box[2]) || !std::isfinite(k.box[3]) ||
		   !(k.box[0] <= k.box[2] && k.box[1] <= k.box[3]))
		{
			why = Fmt("chunk %u: box (%g, %g) .. (%g, %g) not finite or min > max", c, k.box[0], k.box[1], k.box[2], k.box[3]);
			return IREF_CHUNK;
		}
		if(k.triangles == 0 || (uint64_t)k.startIndex + 3ull * k.triangles > indices)
		{
			why = Fmt("chunk %u: %u triangles from index %u, the IB holds %u indices", c, k.triangles, k.startIndex, indices);
			return IREF_CHUNK;
		}
		if(k.baseVertex < 0 || (uint32_t)k.baseVertex >= vertices)
		{
			why = Fmt("chunk %u: base vertex %d, the VB holds %u vertices", c, k.baseVertex, vertices);
			return IREF_CHUNK;
		}
		uint32_t maxIdx = 0;
		const uint32_t end = k.startIndex + 3 * k.triangles;
		for(uint32_t i = k.startIndex; i < end; ++i)
		{
			uint16_t v;
			memcpy(&v, ib + 2 * (size_t)i, 2);
			maxIdx = std::max<uint32_t>(maxIdx, v);
		}
		if((uint64_t)maxIdx + (uint64_t)k.baseVertex >= vertices)
		{
			why = Fmt("chunk %u: index %u + base vertex %d lies past the VB's %u vertices", c, maxIdx, k.baseVertex, vertices);
			return IREF_INDEX;
		}
		c5r::ice::Chunk& o = chunks[c];
		memcpy(o.box, k.box, sizeof(o.box));
		o.triangles = k.triangles;
		o.startIndex = k.startIndex;
		o.baseVertex = k.baseVertex;
	}
	// the mesh
	try
	{
		c5r::ice::Mesh m;
		m.vb.assign(p + h.vbOffset, p + h.vbOffset + h.vbBytes);
		m.ib.assign(ib, ib + h.ibBytes);
		m.chunks = std::move(chunks);
		static_assert(sizeof(m.fields.raw) == VPSHELL_ICE_FIELDS_BYTES, "fields size");
		memcpy(m.fields.raw, fld, VPSHELL_ICE_FIELDS_BYTES);
		m.fields.valid = true;
		out = std::move(m);
	}
	catch(...)
	{
		why = "cannot allocate the mesh";
		return IREF_MEMORY;
	}
	return IREF_NONE;
}

c5r::Status ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& buf, std::string& why)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(f == INVALID_HANDLE_VALUE)
	{
		why = Fmt("cannot open '%ls' (error %lu)", path.c_str(), GetLastError());
		return c5r::Status::fail(c5r::Err::Io, why);
	}
	LARGE_INTEGER sz = {};
	if(!GetFileSizeEx(f, &sz) || sz.QuadPart < 0 || (uint64_t)sz.QuadPart > VPSHELL_ICE_MAX_BYTES)
	{
		CloseHandle(f);
		why = Fmt("'%ls': %lld bytes (limit %llu)", path.c_str(), (long long)sz.QuadPart, (unsigned long long)VPSHELL_ICE_MAX_BYTES);
		return c5r::Status::fail(c5r::Err::BadLayout, why);
	}
	try
	{
		buf.resize((size_t)sz.QuadPart);
	}
	catch(...)
	{
		CloseHandle(f);
		why = "cannot allocate the file's bytes";
		return c5r::Status::fail(c5r::Err::OutOfMemory, why);
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
		why = Fmt("'%ls': read %llu of %llu bytes", path.c_str(), (unsigned long long)got, (unsigned long long)buf.size());
		return c5r::Status::fail(c5r::Err::Io, why);
	}
	return c5r::Status::Ok();
}

}  // namespace

const char* IceRefusalName(uint32_t r) { return r < IREF_COUNT_ ? kRefusalNames[r] : "?"; }

int32_t IceAckResult(uint32_t r)
{
	switch(r)
	{
	case IREF_NONE: return ICEACK_OK;
	case IREF_HASH: return ICEACK_HASH;
	case IREF_OPEN: return ICEACK_OPEN;
	case IREF_STALE: return ICEACK_STALE;
	case IREF_BLOCK_MAGIC: case IREF_MAGIC: case IREF_VERSION: case IREF_SIZES: return ICEACK_VERSION;
	default: return (int32_t)IceErr(r);
	}
}

c5r::Err IceErr(uint32_t r)
{
	switch(r)
	{
	case IREF_NONE: return c5r::Err::Ok;
	case IREF_OPEN: return c5r::Err::Io;
	case IREF_TRUNCATED: return c5r::Err::Truncated;
	case IREF_MAGIC: case IREF_BLOCK_MAGIC: return c5r::Err::BadMagic;
	case IREF_VERSION: case IREF_SIZES: return c5r::Err::BadVersion;
	case IREF_MEMORY: return c5r::Err::OutOfMemory;
	default: return c5r::Err::BadLayout;
	}
}

uint64_t IceFnv1a64(const uint8_t* p, size_t n) { return VPIceFnv(kFnvBasis, p, n); }

uint32_t CheckIceImage(const uint8_t* image, size_t n, const VPShellIceTapControl* block, c5r::ice::Mesh& out, std::string& why)
{
	try
	{
		return CheckImpl(image, n, block, out, why);
	}
	catch(...)
	{
		why = "an exception while checking the image";
		return IREF_MEMORY;
	}
}

struct IceTapReader::Impl
{
	uint32_t pid = 0;
	VPShellHeader* hdr = nullptr;
	std::wstring controlName;
	HANDLE control = nullptr;
	VPShellIceTapControl* block = nullptr;
	int64_t nextOpenQpc = 0;
	uint32_t loggedState = 0xFFFFFFFFu, loggedFail = 0xFFFFFFFFu;
	uint32_t producerState = 0xFFFFFFFFu, producerFail = 0;
	bool ackedAny = false;
	uint32_t ackedGeneration = 0;
	std::shared_ptr<const c5r::ice::Mesh> mesh;
	std::shared_ptr<const std::vector<uint8_t>> raw;
	uint32_t generation = 0, loadSeq = 0;
	bool producerFailed = false, refusedFirst = false, noMesh = false;
	std::string why;
	int32_t lastResult = 0;
	uint64_t refusedBy[IREF_COUNT_] = {};

	bool openControl()
	{
		const int64_t now = Qpc();
		if(now < nextOpenQpc)
			return false;
		nextOpenQpc = now + QpcFreq() / 4;   // a missing producer is looked for again in 250 ms
		HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, controlName.c_str());
		if(!h)
			return false;
		void* v = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_ICETAP_CONTROL_BYTES);
		if(!v)
		{
			RNote("ice: control section '%ls' opens but does not map %u bytes (error %lu)", controlName.c_str(),
			      VPSHELL_ICETAP_CONTROL_BYTES, GetLastError());
			CloseHandle(h);
			return false;
		}
		control = h;
		block = (VPShellIceTapControl*)v;
		RNote("ice: control section '%ls' opened", controlName.c_str());
		return true;
	}

	void writeAck(uint32_t gen, int32_t result)
	{
		VPShellIceTapControl* t = block;
		t->ackServerPid = GetCurrentProcessId();
		InterlockedExchange((volatile LONG*)&t->ackResult, result);
		MemoryBarrier();
		InterlockedExchange((volatile LONG*)&t->ackGeneration, (LONG)gen);   // written last
		lastResult = result;
	}
};

IceTapReader::IceTapReader(uint32_t exePid, VPShellHeader* hdr) : m_impl(new Impl)
{
	wchar_t name[64];
	_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_ICETAP_CONTROL_FMT, exePid);
	m_impl->pid = exePid;
	m_impl->hdr = hdr;
	m_impl->controlName = name;
}

IceTapReader::IceTapReader(uint32_t exePid, VPShellHeader* hdr, const std::wstring& controlName) : m_impl(new Impl)
{
	m_impl->pid = exePid;
	m_impl->hdr = hdr;
	m_impl->controlName = controlName;
}

IceTapReader::~IceTapReader()
{
	if(m_impl->block)
		UnmapViewOfFile(m_impl->block);
	if(m_impl->control)
		CloseHandle(m_impl->control);
}

bool IceTapReader::poll(uint32_t haveGeneration)
{
	Impl& I = *m_impl;
	polls++;
	auto ready = [&]() { return I.mesh && I.generation > haveGeneration; };
	if(!I.block && !I.openControl())
		return ready();
	VPShellIceTapControl tb;
	const SnapResult sr = SnapControl(I.block, tb);
	if(sr == SNAP_NEVER)
		return ready();
	if(sr == SNAP_TORN)
	{
		torn++;
		return ready();
	}
	tb.imageName[63] = 0;
	tb.failText[VPSHELL_ICE_FAILTEXT - 1] = 0;
	if(tb.state != I.loggedState || tb.failReason != I.loggedFail)
	{
		RNote("ice: tap state %s (generation %u, load %u, %u chunks, %llu bytes, fail reason %u%s%s)",
		      tb.state < 6 ? kStateNames[tb.state] : "?", tb.generation, tb.loadSeq, tb.chunkCount,
		      (unsigned long long)tb.imageBytes, tb.failReason, tb.failText[0] ? ": " : "", tb.failText);
		I.loggedState = tb.state;
		I.loggedFail = tb.failReason;
	}
	I.producerState = tb.state;
	I.producerFail = tb.failReason;
	I.noMesh = tb.state == VPICE_NO_MESH;
	I.producerFailed = tb.state == VPICE_FAILED && tb.failReason != VPICEF_CHANGED;
	if(I.producerFailed)
		I.why = std::string("the ice tap failed: ") + tb.failText;
	if(tb.state == VPICE_NO_MESH || I.producerFailed || tb.state == VPICE_OFF)
	{
		I.mesh.reset();   // the applied mesh is not the EXE's any more (no ice now, or the tap could not copy it)
		I.raw.reset();
	}
	if(tb.state != VPICE_READY)
		return ready();
	if(I.ackedAny && tb.generation == I.ackedGeneration)
		return ready();
	const int64_t q0 = Qpc();
	uint32_t reason = IREF_NONE;
	std::string why;
	if(tb.magic != VPSHELL_ICE_MAGIC || tb.version != VPSHELL_ICE_VERSION)
	{
		reason = IREF_BLOCK_MAGIC;
		why = Fmt("control block magic 0x%08X version %u, expected 0x%08X version %u", tb.magic, tb.version, VPSHELL_ICE_MAGIC,
		          VPSHELL_ICE_VERSION);
	}
	if(!reason)
	{
		wchar_t expect[64];
		_snwprintf_s(expect, _countof(expect), _TRUNCATE, VPSHELL_ICE_IMAGE_FMT, I.pid, tb.generation);
		if(tb.exePid != I.pid || tb.imageBytes < sizeof(VPShellIceImage) || tb.imageBytes > VPSHELL_ICE_MAX_BYTES ||
		   wcscmp(expect, tb.imageName) != 0)
		{
			reason = IREF_BLOCK;
			why = Fmt("control block: pid %u (expected %u), %llu bytes (limit %llu), image '%ls' (expected '%ls')", tb.exePid, I.pid,
			          (unsigned long long)tb.imageBytes, (unsigned long long)VPSHELL_ICE_MAX_BYTES, tb.imageName, expect);
		}
	}
	if(!reason && I.generation && tb.generation <= I.generation)
	{
		reason = IREF_STALE;
		why = Fmt("generation %u, the applied mesh is generation %u", tb.generation, I.generation);
	}
	std::vector<uint8_t> raw;
	if(!reason)
	{
		HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, tb.imageName);
		const uint8_t* view = h ? (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0) : nullptr;
		if(!view)
		{
			reason = IREF_OPEN;
			why = Fmt("cannot open the image section '%ls' (error %lu)", tb.imageName, GetLastError());
		}
		else
		{
			MEMORY_BASIC_INFORMATION mbi = {};
			const size_t size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
			if(tb.imageBytes > size)
			{
				reason = IREF_TRUNCATED;
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
					reason = IREF_MEMORY;
					why = Fmt("cannot allocate %llu bytes for the image copy", (unsigned long long)tb.imageBytes);
				}
			}
			UnmapViewOfFile(view);
		}
		if(h)
			CloseHandle(h);
	}
	c5r::ice::Mesh m;
	if(!reason)
		reason = CheckIceImage(raw.data(), raw.size(), &tb, m, why);
	I.writeAck(tb.generation, IceAckResult(reason));   // the producer logs it
	I.ackedAny = true;
	I.ackedGeneration = tb.generation;
	if(reason)
	{
		refused++;
		I.refusedBy[reason]++;
		if(!accepted)
			I.refusedFirst = true;
		I.why = Fmt("generation %u refused (%s, ackResult %d): ", tb.generation, IceRefusalName(reason), IceAckResult(reason)) + why;
		RNote("ice: %s", I.why.c_str());
		return ready();
	}
	accepted++;
	I.refusedFirst = false;
	I.mesh = std::make_shared<const c5r::ice::Mesh>(std::move(m));
	I.raw = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
	I.generation = tb.generation;
	I.loadSeq = tb.loadSeq;
	lastApplyMs = QpcToMs(Qpc() - q0);
	uint64_t tris = 0;
	for(const c5r::ice::Chunk& c : I.mesh->chunks)
		tris += c.triangles;
	RNote("ice: generation %u (load %u) applied: VB %llu bytes, IB %llu bytes, %llu chunks, %llu triangles; %.2f ms",
	      tb.generation, tb.loadSeq, (unsigned long long)I.mesh->vb.size(), (unsigned long long)I.mesh->ib.size(),
	      (unsigned long long)I.mesh->chunks.size(), (unsigned long long)tris, lastApplyMs);
	return ready();
}

std::shared_ptr<const c5r::ice::Mesh> IceTapReader::mesh() const { return m_impl->mesh; }
uint32_t IceTapReader::generation() const { return m_impl->generation; }
uint32_t IceTapReader::loadSeq() const { return m_impl->loadSeq; }

IceTapReader::State IceTapReader::state() const
{
	const Impl& I = *m_impl;
	if(!I.block)
		return State::Absent;
	if(I.producerState == VPICE_OFF)
		return State::Off;
	if(installFailure())
		return State::Failed;
	if(I.mesh)
		return State::Active;
	return State::Waiting;
}

bool IceTapReader::installFailure() const { return m_impl->producerFailed || m_impl->refusedFirst; }
const std::string& IceTapReader::lastWhy() const { return m_impl->why; }
bool IceTapReader::noMesh() const { return m_impl->noMesh; }
int32_t IceTapReader::lastResult() const { return m_impl->lastResult; }
uint32_t IceTapReader::producerState() const { return m_impl->producerState; }
uint64_t IceTapReader::refusedBy(uint32_t reason) const { return reason < IREF_COUNT_ ? m_impl->refusedBy[reason] : 0; }
std::shared_ptr<const std::vector<uint8_t>> IceTapReader::currentImage() const { return m_impl->raw; }

c5r::Status IceTapReader::loadImage(const uint8_t* image, size_t n, c5r::ice::Mesh& out, std::string& why)
{
	const uint32_t reason = CheckIceImage(image, n, nullptr, out, why);
	if(reason)
	{
		why = std::string(IceRefusalName(reason)) + ": " + why;
		return c5r::Status::fail(IceErr(reason), why);
	}
	why.clear();
	return c5r::Status::Ok();
}

c5r::Status IceTapReader::loadFile(const std::wstring& path, c5r::ice::Mesh& out, std::string& why)
{
	std::vector<uint8_t> buf;
	c5r::Status st = ReadWholeFile(path, buf, why);
	if(!st.ok())
		return st;
	return loadImage(buf.data(), buf.size(), out, why);
}

c5r::Status IceTapReader::writeCurrentImage(const std::wstring& path) const
{
	std::shared_ptr<const std::vector<uint8_t>> raw = m_impl->raw;
	if(!raw)
		return c5r::Status::fail(c5r::Err::NotFound, "no ice image applied yet");
	const std::wstring tmp = path + L".tmp";
	HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot create '%ls' (error %lu)", tmp.c_str(), GetLastError()));
	size_t put = 0;
	bool ok = true;
	while(ok && put < raw->size())
	{
		DWORD w = 0;
		const DWORD want = (DWORD)std::min<size_t>(raw->size() - put, 1u << 24);
		ok = WriteFile(f, raw->data() + put, want, &w, nullptr) && w == want;
		put += w;
	}
	CloseHandle(f);
	if(!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		DeleteFileW(tmp.c_str());
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot write '%ls' (error %lu)", path.c_str(), GetLastError()));
	}
	return c5r::Status::Ok();
}

}  // namespace srvrender
