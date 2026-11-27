// srv_model_geom.h - the server's reader of the geometry section (ticket 4879, epic 0561; research/05-graphics-
// composition/city-renderer.md 13): the CPU-built meshes' buffers (walls, the Great Wall, the road and rail bridge) the
// compositor copied from the EXE into Local\VPShellModelGeom.<pid> (shell_model_tap.h, VPModelGeom*). Written 2026-11-18
// for this repository (GPL-3.0-or-later).
//
// poll(): when the section's generation moved, the header and the entries are copied under the seqlock (seq even and
// unchanged around the copy); each valid entry the layer does not hold is checked (kind, stride, bounds, size) and its
// bytes copied, the seq checked unchanged again (else the poll stops and reads again later: torn), then handed to the
// layer, whose store checks the hash (ModelLayer::addGeometry); a refused entry is not read again until the compositor
// rewrites it (its entry generation). Every buffer the layer holds that is no longer an entry is released (the EXE no
// longer draws it). Nothing of the game is written anywhere (D83): the bytes stay in the server's memory.
#pragma once
#include "srv_models_scene.h"
#include <windows.h>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace srvmodels {

struct GeomCounters
{
	uint64_t polls = 0, reads = 0, added = 0, released = 0, torn = 0;
	uint64_t refusedOversize = 0, refusedLayout = 0, refusedHash = 0, refusedBounds = 0;
	uint64_t copiedBytes = 0;
	bool open = false;
	uint32_t generation = 0, entries = 0;
};

class ModelGeomReader
{
public:
	explicit ModelGeomReader(uint32_t exePid) : m_pid(exePid) {}
	~ModelGeomReader();
	ModelGeomReader(const ModelGeomReader&) = delete;
	ModelGeomReader& operator=(const ModelGeomReader&) = delete;
	// True when the layer's held buffers changed (added or released).
	bool poll(ModelLayer& layer);
	const GeomCounters& counters() const { return m_c; }
	// The first refusal of each kind, for the log (cleared when read).
	std::string takeNote();

private:
	bool open();
	uint32_t m_pid = 0;
	HANDLE m_map = nullptr;
	const uint8_t* m_view = nullptr;
	size_t m_size = 0;
	uint32_t m_lastGen = 0;
	std::set<std::pair<uint64_t, uint32_t>> m_refused;   // (hash ^ kind, entry generation)
	GeomCounters m_c;
	std::string m_note;
};

}  // namespace srvmodels
