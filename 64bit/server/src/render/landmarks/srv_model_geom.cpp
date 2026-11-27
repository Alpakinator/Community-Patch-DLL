// srv_model_geom.cpp - the geometry section's reader (ticket 4879; srv_model_geom.h, city-renderer.md 13). Written
// 2026-11-18 for this repository (GPL-3.0-or-later).
#include "srv_model_geom.h"
#include "shell_model_tap.h"
#include <cstring>

namespace srvmodels {

namespace {
uint64_t Key(uint64_t hash, uint32_t kind) { return hash ^ ((uint64_t)kind << 62); }
}  // namespace

ModelGeomReader::~ModelGeomReader()
{
	if(m_view)
		UnmapViewOfFile(m_view);
	if(m_map)
		CloseHandle(m_map);
}

bool ModelGeomReader::open()
{
	if(m_view)
		return true;
	wchar_t n[64];
	_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_MODELGEOM_FMT, m_pid);
	m_map = OpenFileMappingW(FILE_MAP_READ, FALSE, n);
	if(!m_map)
		return false;   // the compositor makes it when it first sees a CPU-built mesh
	m_view = (const uint8_t*)MapViewOfFile(m_map, FILE_MAP_READ, 0, 0, 0);
	if(!m_view)
	{
		CloseHandle(m_map);
		m_map = nullptr;
		return false;
	}
	MEMORY_BASIC_INFORMATION mbi = {};
	VirtualQuery(m_view, &mbi, sizeof(mbi));
	m_size = mbi.RegionSize;
	m_c.open = true;
	return true;
}

std::string ModelGeomReader::takeNote()
{
	std::string s;
	s.swap(m_note);
	return s;
}

bool ModelGeomReader::poll(ModelLayer& layer)
{
	m_c.polls++;
	if(!open())
		return false;
	if(m_size < VPSHELL_MODELGEOM_DATA_OFFSET)
		return false;
	const VPModelGeomHeader* h = (const VPModelGeomHeader*)m_view;
	const uint32_t seq0 = h->seq;
	MemoryBarrier();
	if((seq0 & 1) || h->generation == m_lastGen)
		return false;
	VPModelGeomHeader hd;
	memcpy(&hd, h, sizeof(hd));
	if(hd.magic != VPSHELL_MODELGEOM_MAGIC || hd.version != VPSHELL_MODELGEOM_VERSION || hd.headerBytes != sizeof(VPModelGeomHeader) ||
	   hd.entryBytes != sizeof(VPModelGeomEntry) || hd.maxEntries != VPSHELL_MODELGEOM_MAX_ENTRIES ||
	   hd.dataOffset != VPSHELL_MODELGEOM_DATA_OFFSET || (uint64_t)hd.dataOffset + hd.dataBytes > m_size)
	{
		if(m_note.empty())
			m_note = "the geometry section's header does not hold (magic, version, sizes): nothing read";
		m_c.refusedBounds++;
		m_lastGen = hd.generation;
		return false;
	}
	std::vector<VPModelGeomEntry> es(VPSHELL_MODELGEOM_MAX_ENTRIES);
	memcpy(es.data(), m_view + sizeof(VPModelGeomHeader), es.size() * sizeof(VPModelGeomEntry));
	MemoryBarrier();
	if(h->seq != seq0)
	{
		m_c.torn++;
		return false;
	}
	m_c.reads++;
	bool changed = false;
	std::set<uint64_t> present;
	const uint8_t* data = m_view + hd.dataOffset;
	std::vector<uint8_t> buf;
	for(const VPModelGeomEntry& e : es)
	{
		if(e.kind != VPMGEOM_KIND_INDEX32 && e.kind != VPMGEOM_KIND_VERTEX)
			continue;
		present.insert(Key(e.hash, e.kind));
		if(layer.holdsGeometry(e.kind, e.hash) || m_refused.count({Key(e.hash, e.kind), e.generation}))
			continue;
		if((uint64_t)e.offset + e.bytes > hd.dataBytes)
		{
			m_c.refusedBounds++;
			m_refused.insert({Key(e.hash, e.kind), e.generation});
			if(m_note.empty())
				m_note = "an entry past the section's data (bounds): refused";
			continue;
		}
		buf.assign(data + e.offset, data + e.offset + e.bytes);
		MemoryBarrier();
		if(h->seq != seq0)
		{
			m_c.torn++;
			return changed;   // the compositor wrote meanwhile: read again at the next poll (m_lastGen not moved)
		}
		const c5r::Status st = layer.addGeometry(e.kind, e.hash, e.stride, buf.data(), buf.size());
		if(st.ok())
		{
			m_c.added++;
			m_c.copiedBytes += e.bytes;
			changed = true;
			continue;
		}
		m_refused.insert({Key(e.hash, e.kind), e.generation});
		if(st.code == c5r::Err::Unsupported)
			m_c.refusedOversize++;
		else if(st.str().find("hash") != std::string::npos)
			m_c.refusedHash++;
		else
			m_c.refusedLayout++;
		if(m_note.empty())
			m_note = "a geometry entry refused: " + st.str();
	}
	// released: held by the layer, no longer an entry
	std::vector<std::pair<uint32_t, uint64_t>> held;
	layer.heldGeometry(held);
	for(const auto& k : held)
		if(!present.count(Key(k.second, k.first)))
		{
			layer.releaseGeometry(k.first, k.second);
			m_c.released++;
			changed = true;
		}
	m_lastGen = hd.generation;
	m_c.generation = hd.generation;
	m_c.entries = (uint32_t)present.size();
	return changed;
}

}  // namespace srvmodels
