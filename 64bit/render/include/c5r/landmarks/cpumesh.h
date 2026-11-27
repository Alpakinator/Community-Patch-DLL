// c5r/landmarks/cpumesh.h - the CPU-built meshes (ticket 4879, epic 0561; research/05-graphics-composition/city-renderer.md
// 13): the BuildingShader draws whose buffers the EXE builds itself at run time and no model file holds - city walls (one
// world-space mesh per city on the block programs), the Great Wall (material 3) and the road and rail bridge (material 3,
// three Bridges.gr2 pieces bent per crossing). Each is an R32 index buffer and a vertex buffer, both IMMUTABLE, whose
// content the compositor copies once from the player's EXE at run time and hands to the server in the geometry section
// (shell_model_tap.h, VPModelGeom*). Nothing of the game is committed (D83): the bytes are read from the running game.
//
// The store keeps those buffers by content hash (the wire key's: index buffer from element 1, the whole vertex buffer),
// checked on entry; resolveCpu() turns the tap's R32 draws whose two buffers the store holds into CpuDrawItems, drawn by
// ModelRenderer with the material of their class and t0 / t1 by identity (WireIndex::textureName).
//
// Failure behaviour: every function returns a c5r::Status; nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "c5r/landmarks/tap.h"
#include "c5r/status.h"

namespace c5r { namespace landmarks {

enum class GeomKind : uint8_t { Index32 = 1, Vertex = 2 };   // VPMGEOM_KIND_* of shell_model_tap.h
const char* geomKindName(GeomKind k);

// The content hash a buffer is held under: Index32 VPModelHash64 from byte 4 (element 1: the wire key's rule, 4870 5.5),
// Vertex VPModelHash64 of all bytes. 0 when the bytes cannot carry one (an index buffer under two elements).
uint64_t geometryHash(GeomKind k, const void* data, size_t bytes);

// One held buffer.
struct CpuBuffer {
    GeomKind kind = GeomKind::Vertex;
    uint64_t hash = 0;
    uint32_t stride = 0;             // Vertex: the bytes per vertex the EXE bound (20 or 32); Index32: 4
    std::vector<uint8_t> bytes;
};

struct CpuStoreCounts {
    uint64_t added = 0, removed = 0;
    uint64_t refusedOversize = 0, refusedLayout = 0, refusedHash = 0;
};

class CpuMeshStore {
public:
    static constexpr size_t kMaxBytes = 8u << 20;   // VPSHELL_MODELGEOM_MAX_BUFFER: a bigger buffer is refused
    // Holds a copy of the bytes under (kind, hash). Checks: bytes over kMaxBytes [Unsupported, oversize]; Index32 under
    // two elements or not a multiple of 4, Vertex stride not 20 or 32 or the bytes not a multiple of it [BadLayout]; the
    // bytes' hash not the one given [BadLayout, "content differs from its hash": a write after the copy]. A buffer held
    // already is not copied again (Ok).
    Status add(GeomKind kind, uint64_t hash, uint32_t stride, const uint8_t* data, size_t bytes);
    const CpuBuffer* get(GeomKind kind, uint64_t hash) const;
    bool has(GeomKind kind, uint64_t hash) const { return get(kind, hash) != nullptr; }
    // Releases one buffer (the compositor dropped it from the section: the EXE no longer draws it). False when not held.
    bool remove(GeomKind kind, uint64_t hash);
    void clear();
    std::vector<std::pair<GeomKind, uint64_t>> list() const;
    size_t size() const { return held_.size(); }
    uint64_t bytes() const { return bytes_; }
    uint64_t generation() const { return generation_; }   // changes at every add or remove
    const CpuStoreCounts& counts() const { return counts_; }

private:
    std::map<std::pair<uint8_t, uint64_t>, CpuBuffer> held_;
    uint64_t bytes_ = 0, generation_ = 0;
    CpuStoreCounts counts_;
};

// The material of a CPU-built draw by its class and the vertex stride its buffer needs: Blocks 32 B (walls on the block
// programs), Single 20 B (the Great Wall, bridges). Other materials are never CPU-built [C, 4870 5.7, 4871 P4 / P6].
bool cpuMaterialFits(Material m, uint32_t vertexStride);

// The textures of CPU-built meshes that no library file names, entered in the index so textureName() finds them: the
// road and rail bridge's maps bridges_diff.dds and bridges_sref.dds (Bridges.gr2's material, the EXE's fixed file name;
// landmark-renderer.md 8.1). The walls' and the Great Wall's maps are their library files' (Source Wall / GreatWall).
// Returns how many of them the install has.
uint32_t addCpuTextureNames(WireIndex& index);

// CpuDrawItem (one CPU-built draw the server makes) is declared in tap.h beside DrawItem: ModelScene holds them.

struct CpuResolveReport {
    uint32_t r32 = 0;                // tap draws with an R32 key of a CPU-built class (Blocks, Single)
    uint32_t resolved = 0;           // ... whose buffers are held and textures named
    uint32_t noGeometry = 0;         // ... whose index or vertex buffer is not held (stays with the EXE)
    uint32_t badStride = 0;          // ... held with a vertex stride the material does not take
    uint32_t textureUnnamed = 0;     // ... whose t0 or t1 identity names no file of the install
};

// Every tap draw of f with an R32 key and a CPU-built material whose index and vertex buffers the store holds and whose
// t0 / t1 identities WireIndex::textureName names becomes a CpuDrawItem in out.cpuItems; out.unknown / unknownDraws lose
// those draws (resolve() counted them unknown: no file holds them). Checks: checkFrame [BadLayout]; a key's range past
// its held index buffer [BadLayout] (a malformed tap: refused whole).
Status resolveCpu(const CpuMeshStore& store, const WireIndex& index, const TapFrame& f, ModelScene& out,
                  CpuResolveReport* report);

}}  // namespace c5r::landmarks
