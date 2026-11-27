// c5r/landmarks/tap.h - the tap frame (the EXE's BuildingShader colour draws as the compositor taps them), the
// resolved scene, their file forms .mtap and .c5models, and the library's lookup by the wire key (ticket 4873, epic
// 0561; research/05-graphics-composition/city-renderer.md 2.2 and 2.4, landmark-renderer.md 7).
//
// The .mtap form is the compositor's frame image byte for byte (4874, thread 0561 #6098 / #6099): its layout is
// defined once, in shell/common/shell_model_tap.h (VPModelTapFrame and what follows it); this header holds the parsed
// form (TapFrame) and the reader and writer that go through that layout. Written from the knowledge base (D83).
//
// Failure behaviour: every function returns a c5r::Status; nothing throws. The readers check every count, offset and
// size against the bytes they were given before reading (Truncated, BadMagic, BadLayout, Unsupported).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "c5r/landmarks/library.h"
#include "c5r/status.h"

namespace c5r { namespace landmarks {

// The four BuildingShader materials the server draws (pass 0 programs, city-renderer.md 3.1).
enum class Material : uint8_t {
    Single = 3,      // BuildingShader_CODE: one draw per mesh, world rows in VS cb0 rows 0-3 (wonders, display buildings)
    Instanced = 4,   // BuildingShader_CODE_Instanced: vertex slot 1, four float4 rows per instance (landmarks)
    Blocks = 5,      // BuildingShader_CODE_Instanced_Opaque: slot 1 as 4, 32-byte vertices (city blocks)
    Skinned = 6,     // BuildingShader_Skinned_CODE: slot 1 an R32 id, VS t27 96 rows per id (animated parts)
};
const char* materialName(Material m);
// The record's class (VPMODEL_CLASS_* of shell_model_scene.h) to the material and back; false for an unknown class.
bool materialFromClass(uint32_t cls, Material& out);
uint8_t classOf(Material m);
// Float4 rows of instance data per instance: 4 (materials 3, 4, 5) or 96 (6: 32 bones x 3 rows).
uint32_t rowsPerInstance(Material m);
// The material a library part is drawn with: CityBlock files 5, skinned parts 6, CityPiece files 3, else 4
// (city-renderer.md 3.1: wonders and display buildings are material 3, landmarks material 4) [C, 4870 5.2].
Material partMaterial(ModelClass fileClass, bool skinned);

// ---------------------------------------------------------------------------------------------- the wire key
// VPModelKey of shell_model_tap.h: the index buffer's hash from index 1, the whole slot-0 vertex buffer's hash, the t0 /
// t1 texture identity, the draw's range. Hashes are VPModelHash64; 0 = none.
struct WireKey {
    uint64_t ib = 0, vb = 0, t0 = 0, t1 = 0;
    uint32_t startIndex = 0, indexCount = 0;
    int32_t baseVertex = 0;
    uint32_t flags = 0;              // kWireIndex32: R32 indices
    bool operator==(const WireKey& o) const {
        return ib == o.ib && vb == o.vb && t0 == o.t0 && t1 == o.t1 && startIndex == o.startIndex &&
               indexCount == o.indexCount && baseVertex == o.baseVertex && flags == o.flags;
    }
    bool operator!=(const WireKey& o) const { return !(*this == o); }
    bool operator<(const WireKey& o) const;
    bool sameGeometry(const WireKey& o) const {   // all but the textures
        return ib == o.ib && vb == o.vb && startIndex == o.startIndex && indexCount == o.indexCount &&
               baseVertex == o.baseVertex && flags == o.flags;
    }
};
constexpr uint32_t kWireIndex32 = 1;

// VPModelHash64 over bytes (shell_model_tap.h's inline C, called, not copied).
uint64_t wireHash(const void* data, size_t bytes);
// The wire key's index hash: the index buffer from element 1 to its end. Checks: indexBytes not 2 or 4, fewer than
// two elements, a size not a multiple of indexBytes [BadLayout].
Status wireIndexHash(const void* indices, size_t bytes, uint32_t indexBytes, uint64_t& out);

// The texture identity (VPModelTextureHash): the level VPModelTextureMip picks (the first whose width and height are
// <= 64, else the last), its rows tight, array slice 0 (a cube's +X face). `mips` are slice 0's levels in order (data,
// bytes, row pitch; a row pitch of 0 means tight). 0 when the format has no row size or the level is missing or short.
struct TextureLevel { const uint8_t* data = nullptr; size_t bytes = 0; uint32_t rowPitch = 0; };
uint64_t textureIdentity(uint32_t dxgiFormat, uint32_t width, uint32_t height, uint32_t mipLevels,
                         const std::vector<TextureLevel>& mips);

// ---------------------------------------------------------------------------------------------- the tap frame
constexpr uint32_t kNoConst = 0xFFFFFFFFu;

// One render target a tapped draw went to (VPModelTapTarget): the scene target, the water reflection.
struct TapTarget {
    uint32_t width = 0, height = 0, samples = 1, format = 0;   // the RTV's texture
    uint32_t depthWidth = 0, depthHeight = 0;                  // the DSV's texture
    uint32_t draws = 0;                                        // tapped draws into it
};

// One tapped EXE draw (VPModelTapDraw).
struct TapDraw {
    WireKey key;
    Material material = Material::Instanced;   // from the record's class
    uint8_t bits = 0;                // the identity set's bits for the key (0 = not in the set)
    uint8_t flags = 0;               // VPMTAP_DRAW_* (DROPPED, DEFERRED, NOT_IN_SET, BIT_OFF, UNHASHED)
    uint32_t target = 0;             // into TapFrame::targets
    uint32_t instanceCount = 0, startInstance = 0;
    uint32_t instRow = 0, instRows = 0;   // into TapFrame::rows: rowsPerInstance(material) per instance
    uint32_t vsCb1 = kNoConst, psCb1 = kNoConst;   // into TapFrame::consts
    uint32_t order = 0, skinId0 = 0;
    uint64_t env[3] = {0, 0, 0};     // PS t2, t3, t4 identity (the environment cubes)
};

struct Float4 { float x = 0, y = 0, z = 0, w = 0; };

// One constant buffer's content (VPModelTapConst): `rows` float4 rows of TapFrame::rows from firstRow.
struct TapConst { uint32_t firstRow = 0, rows = 0, stage = 0, uses = 0; };

struct TapFrame {
    uint64_t frameSeq = 0, presentCount = 0;
    uint32_t setGeneration = 0, latchedBits = 0, wantBits = 0;
    uint32_t dropped[2] = {0, 0};
    uint32_t passedTapped = 0, truncated = 0;
    std::vector<TapTarget> targets;
    std::vector<TapDraw> draws;
    std::vector<Float4> rows;
    std::vector<TapConst> consts;
};

// Consistency of a parsed frame [BadLayout]: counts within the header's bounds (draws, consts, targets); every draw's
// target, rows and constant references in range; instance count 1 .. VPSHELL_MODELTAP_MAX_INSTANCES; instRows =
// rowsPerInstance x instances; every constant inside the rows and at most VPSHELL_MODELTAP_CB_MAX_BYTES.
Status checkFrame(const TapFrame& f);

// .mtap (the frame image): read, with magic [BadMagic], version [Unsupported], header and record sizes, counts and
// offsets [BadLayout], sizes [Truncated] and the hash [BadLayout] checked before use, then checkFrame; and write.
Status readTap(const uint8_t* data, size_t size, TapFrame& out);
Status writeTap(const TapFrame& f, std::vector<uint8_t>& out);
Status readFileBytes(const std::string& path, std::vector<uint8_t>& out);
Status writeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes);

// ---------------------------------------------------------------------------------------------- the scene
// One draw the server makes: a part of a library file, with its tapped instance data.
struct DrawItem {
    uint32_t file = 0, part = 0;     // into ModelLibrary::files() and that file's layout.parts
    Material material = Material::Instanced;
    uint32_t target = 0;
    uint32_t instanceCount = 0, instRow = 0;
    uint32_t vsCb1 = kNoConst, psCb1 = kNoConst;
    uint64_t env[3] = {0, 0, 0};
    uint32_t tapDraw = 0;            // the TapFrame draw it came from (the harness's per-object compare)
};

// One CPU-built draw (ticket 4879, cpumesh.h): no file holds its buffers; the server draws the buffers the compositor
// copied from the EXE (CpuMeshStore), by the tap draw's own key, with t0 / t1 by the file names their identities name.
struct CpuDrawItem {
    WireKey key;
    Material material = Material::Single;
    uint32_t target = 0;
    uint32_t instanceCount = 0, instRow = 0;
    uint32_t vsCb1 = kNoConst, psCb1 = kNoConst;
    uint64_t env[3] = {0, 0, 0};
    uint32_t tapDraw = 0;
    uint32_t vertexStride = 0;       // the held vertex buffer's stride
    std::string t0, t1;              // "" = not bound (identity 0)
};

struct ModelScene {
    uint64_t frameSeq = 0;
    std::vector<TapTarget> targets;
    std::vector<DrawItem> items;
    std::vector<CpuDrawItem> cpuItems;   // ticket 4879: drawn after items (not written to .c5models)
    std::vector<Float4> rows;
    std::vector<TapConst> consts;
    uint32_t unknownDraws = 0;       // no part holds the key (the identity set keeps such draws with the EXE)
    uint32_t ambiguousDraws = 0;     // several parts hold the key: the first is drawn (equal geometry and textures)
    std::vector<uint32_t> unknown;   // their TapFrame draw indices
};

// The library's wire keys: per part, from the file's resident buffers (index hash from element 1, the part's vertex
// buffer) and the install's textures (t0 / t1 by name, their identity as the compositor hashes the EXE's upload).
class WireIndex {
public:
    // files: the install the library was loaded from (the textures are read through it).
    WireIndex(ModelLibrary& lib, const gamefs::GameFiles& files) : lib_(lib), files_(&files) {}
    // Builds the index of every ok file: each file made resident, hashed and evicted again unless it was resident
    // before; every part's t0 / t1 read once by name. Checks: library not loaded [NotFound]. A file whose residency
    // fails is skipped and counted in failedFiles(); a texture the install lacks counts in missingTextures() and gives
    // the identity 0.
    Status build();
    // Every (file, part) whose wire key equals k, in file order.
    std::vector<ModelLibrary::Hit> find(const WireKey& k) const;
    // The same without the texture identity (the harness's report of texture misses).
    std::vector<ModelLibrary::Hit> findGeometry(const WireKey& k) const;
    // Ticket 4884: find(k) in place, without a copy (resolve runs every frame): `count` entries from the returned one.
    typedef std::pair<WireKey, ModelLibrary::Hit> Entry;
    const Entry* findRange(const WireKey& k, size_t& count) const;
    bool hasGeometry(const WireKey& k) const;
    const WireKey* key(uint32_t file, uint32_t part) const;
    uint32_t failedFiles() const { return failed_; }
    uint32_t missingTextures() const { return missingTextures_; }
    // A texture's identity by name (lower case, .tga / .psd -> .dds of the stem), 0 when it is not in the install or
    // has no identity. B8G8R8A8 files are hashed in R8G8B8A8 order (the EXE uploads them so, unit-renderer.md 10.3).
    uint64_t textureId(const std::string& name);
    // The file a texture identity was computed from by textureId (lower case; the first name when two files share the
    // identity), "" when none (ticket 4879: the CPU-built meshes' t0 / t1).
    std::string textureName(uint64_t id) const;
    double buildSeconds() const { return seconds_; }   // a workstation indication, never a timing claim
private:
    ModelLibrary& lib_;
    const gamefs::GameFiles* files_ = nullptr;
    std::vector<std::vector<WireKey>> keys_;      // per file, per part (empty: not indexed)
    std::vector<std::pair<WireKey, ModelLibrary::Hit>> sorted_, geometry_;
    std::map<std::string, uint64_t> textures_;
    std::map<uint64_t, std::string> names_;      // identity -> the first name (textureName)
    uint32_t failed_ = 0, missingTextures_ = 0;
    double seconds_ = 0;
};

struct ResolveReport {
    uint32_t draws = 0, resolved = 0, unknown = 0, ambiguous = 0;
    uint32_t textureMiss = 0;        // the geometry is held, the texture identity is not
    uint32_t materialMiss = 0;       // held, but by a part of another material
};
// Source `exe`: every tap draw to the part its wire key names. Checks: checkFrame [BadLayout].
Status resolve(const WireIndex& index, const ModelLibrary& lib, const TapFrame& f, ModelScene& out,
               ResolveReport* report);

// .c5models: a resolved scene, each item's file named by (gr2, scale) so it reads back against any load of the
// library. Magic "C5MD", version 1; header, string table, items, rows, consts, targets. Every read checked
// (Truncated, BadMagic, Unsupported, BadLayout; a file the library does not hold [NotFound]).
Status writeScene(const ModelScene& s, const ModelLibrary& lib, std::vector<uint8_t>& out);
Status readScene(const uint8_t* data, size_t size, const ModelLibrary& lib, ModelScene& out);

}}  // namespace c5r::landmarks
