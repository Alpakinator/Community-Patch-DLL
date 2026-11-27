// srv_water_tap.cpp - server.exe's reader of the water mesh tap (epic 3600, issue 3603). Contract: srv_water_tap.h.
// Written 2026-11-13 for this repository (GPL-3.0-or-later). Producer: shell/composite/water_tap.cpp. Layout and ack
// protocol: VPShellWaterTap / VPShellWaterImage / VPShellWaterRecord in shell/common/shell_shared.h (the height-field
// tap's protocol, steps 1-4). What the records hold: research/05-graphics-composition/water-system.md section 9.
#include "srv_water_tap.h"
#include "c5r/status.h"
#include <string>

namespace srvrender {

namespace {

const char* kStateNames[] = {"NONE", "WRITING", "READY", "RELEASED", "FAILED", "OFF"};

uint64_t WaterFnv(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

std::string Fmt(const char* fmt, ...)
{
	char b[512];
	va_list a;
	va_start(a, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
	va_end(a);
	return b;
}

// The image's own checks (everything but the block and the camera). WTACK_OK or a refusal code with `why`.
int32_t CheckImage(const uint8_t* view, size_t viewBytes, const VPShellWaterTap& tb, std::vector<WaterTapRecordCopy>& out,
                   VPShellWaterImage& img, std::string& why)
{
	if(tb.imageBytes < sizeof(VPShellWaterImage) || tb.imageBytes > viewBytes || tb.imageBytes > VPSHELL_WATER_MAX_BYTES)
	{
		why = Fmt("image %llu bytes, section view %llu bytes (limit %llu)", (unsigned long long)tb.imageBytes,
		          (unsigned long long)viewBytes, (unsigned long long)VPSHELL_WATER_MAX_BYTES);
		return (int32_t)c5r::Err::Truncated;
	}
	memcpy(&img, view, sizeof(img));
	if(img.magic != VPSHELL_WATER_MAGIC || img.version != VPSHELL_WATER_IMAGE_VERSION)
	{
		why = Fmt("magic 0x%08X version %u, expected 0x%08X version %u", img.magic, img.version, VPSHELL_WATER_MAGIC,
		          VPSHELL_WATER_IMAGE_VERSION);
		return WTACK_VERSION;
	}
	if(img.headerBytes != sizeof(VPShellWaterImage) || img.recordBytes != sizeof(VPShellWaterRecord))
	{
		why = Fmt("header %u / record %u bytes, expected %u / %u", img.headerBytes, img.recordBytes,
		          (unsigned)sizeof(VPShellWaterImage), (unsigned)sizeof(VPShellWaterRecord));
		return WTACK_VERSION;
	}
	if(img.imageBytes != tb.imageBytes || img.recordCount != tb.recordCount || img.loadSeq != tb.loadSeq ||
	   img.exePid != tb.exePid)
	{
		why = Fmt("image says %llu bytes, %u records, load %u, pid %u; the block %llu, %u, %u, %u",
		          (unsigned long long)img.imageBytes, img.recordCount, img.loadSeq, img.exePid,
		          (unsigned long long)tb.imageBytes, tb.recordCount, tb.loadSeq, tb.exePid);
		return (int32_t)c5r::Err::BadLayout;
	}
	if(img.recordCount > VPSHELL_WATER_MAX_RECORDS)
	{
		why = Fmt("%u records, at most %u", img.recordCount, VPSHELL_WATER_MAX_RECORDS);
		return (int32_t)c5r::Err::BadLayout;
	}
	const uint64_t recEnd = (uint64_t)img.headerBytes + (uint64_t)img.recordCount * img.recordBytes;
	if(recEnd > img.imageBytes)
	{
		why = Fmt("%u records end at %llu, past the image's %llu bytes", img.recordCount, (unsigned long long)recEnd,
		          (unsigned long long)img.imageBytes);
		return (int32_t)c5r::Err::Truncated;
	}
	std::vector<WaterTapRecordCopy> recs(img.recordCount);
	for(uint32_t i = 0; i < img.recordCount; ++i)
		memcpy(&recs[i].rec, view + img.headerBytes + (size_t)i * img.recordBytes, sizeof(VPShellWaterRecord));
	for(uint32_t i = 0; i < img.recordCount; ++i)
	{
		const VPShellWaterRecord& r = recs[i].rec;
		if(r.system > VPWSYS_CURRENTS || r.role == VPWROLE_NONE || r.role > VPWROLE_DRAW)
		{
			why = Fmt("record %u: system %u role %u", i, r.system, r.role);
			return (int32_t)c5r::Err::BadLayout;
		}
		if(r.role == VPWROLE_DRAW)
		{
			if(r.dataBytes)
			{
				why = Fmt("record %u: a draw with %llu data bytes", i, (unsigned long long)r.dataBytes);
				return (int32_t)c5r::Err::BadLayout;
			}
			continue;
		}
		if(!r.dataBytes || r.dataOffset < recEnd || r.dataOffset > img.imageBytes || r.dataBytes > img.imageBytes - r.dataOffset)
		{
			why = Fmt("record %u: data %llu + %llu outside the image (records end at %llu, image %llu bytes)", i,
			          (unsigned long long)r.dataOffset, (unsigned long long)r.dataBytes, (unsigned long long)recEnd,
			          (unsigned long long)img.imageBytes);
			return (int32_t)c5r::Err::Truncated;
		}
		if(r.role == VPWROLE_VB && (r.stride < 4 || r.stride > 256 || (uint64_t)r.stride * r.count != r.dataBytes))
		{
			why = Fmt("record %u: VB stride %u x %u vertices != %llu bytes", i, r.stride, r.count, (unsigned long long)r.dataBytes);
			return (int32_t)c5r::Err::BadLayout;
		}
		if(r.role == VPWROLE_IB)
		{
			const uint32_t e = r.dxgiFormat == 57 ? 2 : r.dxgiFormat == 42 ? 4 : 0;
			if(!e || (uint64_t)e * r.count != r.dataBytes)
			{
				why = Fmt("record %u: IB format %u x %u indices != %llu bytes", i, r.dxgiFormat, r.count, (unsigned long long)r.dataBytes);
				return (int32_t)c5r::Err::BadLayout;
			}
		}
		const uint64_t h = WaterFnv(view + r.dataOffset, (size_t)r.dataBytes);
		if(h != r.fnv1a64)
		{
			why = Fmt("record %u: FNV-1a 64 %016llX, the record says %016llX", i, (unsigned long long)h, (unsigned long long)r.fnv1a64);
			return WTACK_HASH;
		}
	}
	// draws: their buffers exist, have the right roles and hold what the draw takes
	for(uint32_t i = 0; i < img.recordCount; ++i)
	{
		const VPShellWaterRecord& r = recs[i].rec;
		if(r.role != VPWROLE_DRAW)
			continue;
		if(r.vbRecord >= img.recordCount || r.ibRecord >= img.recordCount || recs[r.vbRecord].rec.role != VPWROLE_VB ||
		   recs[r.ibRecord].rec.role != VPWROLE_IB)
		{
			why = Fmt("draw record %u: VB record %u, IB record %u are not a VB and an IB", i, r.vbRecord, r.ibRecord);
			return (int32_t)c5r::Err::BadLayout;
		}
		const VPShellWaterRecord& vb = recs[r.vbRecord].rec;
		const VPShellWaterRecord& ib = recs[r.ibRecord].rec;
		if(r.topology != 4 || !r.indexCount || r.indexCount % 3 || (uint64_t)r.startIndex + r.indexCount > ib.count ||
		   r.vertexCount != vb.count || r.vbOffset)
		{
			why = Fmt("draw record %u: topology %u, indices %u + %u of %u, vertices %u of %u", i, r.topology, r.startIndex,
			          r.indexCount, ib.count, r.vertexCount, vb.count);
			return (int32_t)c5r::Err::BadLayout;
		}
		// every index of the draw inside its VB (the producer says so in flag bit 2; a reader trusts no flag)
		const uint8_t* p = view + ib.dataOffset;
		const uint32_t e = ib.dxgiFormat == 42 ? 4 : 2;
		for(uint32_t k = r.startIndex; k < r.startIndex + r.indexCount; ++k)
		{
			uint32_t v = 0;
			memcpy(&v, p + (size_t)k * e, e);
			if((int64_t)v + r.baseVertex < 0 || (int64_t)v + r.baseVertex >= (int64_t)vb.count)
			{
				why = Fmt("draw record %u: index %u = %u (base %d) outside its %u vertices", i, k, v, r.baseVertex, vb.count);
				return (int32_t)c5r::Err::BadLayout;
			}
		}
	}
	const uint64_t hImg = WaterFnv(view, (size_t)img.imageBytes);
	if(tb.fnv1a64 && hImg != tb.fnv1a64)
	{
		why = Fmt("image FNV-1a 64 %016llX, the block says %016llX", (unsigned long long)hImg, (unsigned long long)tb.fnv1a64);
		return WTACK_HASH;
	}
	if(!tb.fnv1a64)
	{
		why = "the block carries no image hash";
		return WTACK_HASH;
	}
	for(auto& r : recs)
		if(r.rec.dataBytes)
			r.data.assign(view + r.rec.dataOffset, view + r.rec.dataOffset + r.rec.dataBytes);
	out.swap(recs);
	return WTACK_OK;
}

// issue 3903: the block's own checks before the section is opened, and the map size after CheckImage, as functions so
// that poll() and loadImage() run the same code (their texts and codes are poll()'s of before, unchanged).
int32_t CheckBlock(const VPShellWaterTap& tb, bool haveCam, uint32_t camGridSeq, std::string& why)
{
	if(haveCam && camGridSeq != tb.loadSeq)
	{
		why = Fmt("stale: the camera block's grid seq is %u, the tap's load seq %u", camGridSeq, tb.loadSeq);
		return WTACK_STALE;
	}
	if(tb.recordCount > VPSHELL_WATER_MAX_RECORDS || tb.imageBytes > VPSHELL_WATER_MAX_BYTES)
	{
		why = Fmt("the block announces %u records and %llu bytes (limits %u, %llu)", tb.recordCount,
		          (unsigned long long)tb.imageBytes, VPSHELL_WATER_MAX_RECORDS, (unsigned long long)VPSHELL_WATER_MAX_BYTES);
		return (int32_t)c5r::Err::BadLayout;
	}
	return WTACK_OK;
}

int32_t CheckMapSize(const VPShellWaterImage& img, bool haveCam, uint32_t samplesX, uint32_t samplesY, std::string& why)
{
	if(haveCam && (img.mapW != samplesX / 64 || img.mapH != samplesY / 64))
	{
		why = Fmt("map size: the image says %ux%u plots, the camera block's grid %ux%u", img.mapW, img.mapH, samplesX / 64,
		          samplesY / 64);
		return (int32_t)c5r::Err::BadLayout;
	}
	return WTACK_OK;
}

}  // namespace

void WaterTapReader::writeAck(uint32_t loadSeq, uint32_t copySeq, int32_t result)
{
	VPShellWaterTap* t = VPSHELL_WATERTAP(m_hdr);
	t->ackServerPid = GetCurrentProcessId();
	InterlockedExchange((volatile LONG*)&t->ackResult, result);
	InterlockedExchange((volatile LONG*)&t->ackCopySeq, (LONG)copySeq);
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&t->ackLoadSeq, (LONG)loadSeq);   // written last
}

bool WaterTapReader::poll()
{
	if(!m_hdr)
		return false;
	VPShellWaterTap tb;
	if(!SnapBlock(VPSHELL_WATERTAP(m_hdr), tb))
		return false;
	if(tb.state != m_loggedState)
	{
		RNote("water: tap block state %s (load seq %u, copy %u, fail reason %u, %u records, %llu image bytes, section '%.64ls')",
		      tb.state < 6 ? kStateNames[tb.state] : "?", tb.loadSeq, tb.copySeq, tb.failReason, tb.recordCount,
		      (unsigned long long)tb.imageBytes, tb.sectionName);
		m_loggedState = tb.state;
	}
	// step 4: the copy of the current load is gone and this reader has none of it: ask once per copy
	if((tb.state == VPWTAP_RELEASED || (tb.state == VPWTAP_FAILED && tb.failReason == VPWTAPF_ACK_TIMEOUT)) &&
	   !(m_haveLoad && m_loadSeq == tb.loadSeq) && !(m_requested && m_reqLoad == tb.loadSeq && m_reqCopy == tb.copySeq))
	{
		VPShellCamera c;
		if(SnapCamera(m_hdr, c) && c.gridValid == 1 && c.gridSeq == tb.loadSeq)
		{
			const uint32_t r = (uint32_t)InterlockedIncrement((volatile LONG*)&VPSHELL_WATERTAP(m_hdr)->requestSeq);
			m_requested = true;
			m_reqLoad = tb.loadSeq;
			m_reqCopy = tb.copySeq;
			requests++;
			RNote("water: tap of load seq %u is %s (copy %u) and this server has none of it: new copy requested (requestSeq %u)",
			      tb.loadSeq, tb.state == VPWTAP_RELEASED ? "RELEASED" : "FAILED ACK_TIMEOUT", tb.copySeq, r);
		}
	}
	if(tb.state != VPWTAP_READY)
		return false;
	if(m_ackedAny && tb.loadSeq == m_ackedLoad && tb.copySeq == m_ackedCopy)
		return false;
	const int64_t q0 = Qpc();
	int32_t result = WTACK_OK;
	std::string why;
	tb.sectionName[63] = 0;
	VPShellCamera cam;
	const bool haveCam = SnapCamera(m_hdr, cam) && cam.gridValid == 1;
	result = CheckBlock(tb, haveCam, haveCam ? cam.gridSeq : 0, why);   // stale, then the block's limits (issue 3903: a function)
	HANDLE h = nullptr;
	const uint8_t* view = nullptr;
	size_t size = 0;
	if(result == WTACK_OK)
	{
		h = OpenFileMappingW(FILE_MAP_READ, FALSE, tb.sectionName);
		view = h ? (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0) : nullptr;
		if(!view)
		{
			const DWORD le = GetLastError();
			if(h)
				CloseHandle(h);
			h = nullptr;
			result = WTACK_OPEN;
			why = Fmt("cannot open section '%ls' (error %lu)", tb.sectionName, le);
		}
		else
		{
			MEMORY_BASIC_INFORMATION mbi = {};
			size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
		}
	}
	std::vector<WaterTapRecordCopy> recs;
	VPShellWaterImage img = {};
	if(result == WTACK_OK)
		result = CheckImage(view, size, tb, recs, img, why);
	// issue 3903: the map-size check (unchanged; it reads only img and the camera) runs before the unmap, so that the sink
	// sees only copies that passed every check, while the image is still mapped
	if(result == WTACK_OK)
		result = CheckMapSize(img, haveCam, haveCam ? cam.samplesX : 0, haveCam ? cam.samplesY : 0, why);
	if(result == WTACK_OK && m_sink)
		m_sink(m_sinkCtx, tb, view, (size_t)tb.imageBytes, haveCam, haveCam ? cam.gridSeq : 0, haveCam ? cam.samplesX : 0,
		       haveCam ? cam.samplesY : 0);
	if(view)
		UnmapViewOfFile(view);
	if(h)
		CloseHandle(h);
	writeAck(tb.loadSeq, tb.copySeq, result);   // the DLL may close its section now
	m_ackedAny = true;
	m_ackedLoad = tb.loadSeq;
	m_ackedCopy = tb.copySeq;
	m_lastResult = result;
	m_lastWhy = why;
	if(result != WTACK_OK)
	{
		refused++;
		RNote("water: tap load seq %u copy %u refused (ackResult %d): %s", tb.loadSeq, tb.copySeq, result, why.c_str());
		return false;
	}
	accepted++;
	m_records.swap(recs);
	m_image = img;
	m_loadSeq = tb.loadSeq;
	m_haveLoad = true;
	uint32_t vb = 0, ib = 0, dr = 0;
	for(const auto& r : m_records)
	{
		vb += r.rec.role == VPWROLE_VB;
		ib += r.rec.role == VPWROLE_IB;
		dr += r.rec.role == VPWROLE_DRAW;
	}
	RNote("water: tap load seq %u copy %u read: %u records (%u VB, %u IB, %u draws), systems 0x%X, %llu bytes, water z %.3f, "
	      "hashes ok, copied and acknowledged in %.1f ms",
	      tb.loadSeq, tb.copySeq, (unsigned)m_records.size(), vb, ib, dr, tb.systems, (unsigned long long)tb.imageBytes,
	      img.waterZ, QpcToMs(Qpc() - q0));
	return true;
}

// issue 3903: the file source (srv_water_tap.h). The order of poll()'s checks after its handshake: the block (stale,
// limits), CheckImage over the recorded image, the map size.
bool WaterTapReader::loadImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam,
                               uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY)
{
	VPShellWaterTap tb = block;
	std::string why;
	int32_t result = CheckBlock(tb, haveCam, gridSeq, why);
	std::vector<WaterTapRecordCopy> recs;
	VPShellWaterImage img = {};
	if(result == WTACK_OK && !image)
	{
		result = (int32_t)c5r::Err::Truncated;
		why = "no image bytes";
	}
	if(result == WTACK_OK)
		result = CheckImage(image, imageBytes, tb, recs, img, why);
	if(result == WTACK_OK)
		result = CheckMapSize(img, haveCam, samplesX, samplesY, why);
	m_lastResult = result;
	m_lastWhy = why;
	if(result != WTACK_OK)
	{
		refused++;
		RNote("water: recorded image of load seq %u copy %u refused (result %d): %s", tb.loadSeq, tb.copySeq, result, why.c_str());
		return false;
	}
	accepted++;
	m_records.swap(recs);
	m_image = img;
	m_loadSeq = tb.loadSeq;
	m_haveLoad = true;
	RNote("water: recorded image of load seq %u copy %u loaded: %u records, %llu bytes, hashes ok", tb.loadSeq, tb.copySeq,
	      (unsigned)m_records.size(), (unsigned long long)tb.imageBytes);
	return true;
}

}  // namespace srvrender
