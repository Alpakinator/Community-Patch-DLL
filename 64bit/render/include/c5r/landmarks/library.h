// c5r/landmarks/library.h - the model library: every model the EXE draws with the BuildingShader programs (landmark
// rows, wonder art, walls, the Great Wall, the city sets), its meshes repacked to the vertex the EXE uploads, the
// identity key of each draw, textures by name, lazy residency (ticket 4872, epic 0561; city-renderer.md 2.2).
//
// Written from the knowledge base's description (D83; no EXE code): city-renderer.md 2 and 3, cities-and-landmarks.md
// 1-2, landmarks-and-models.md 1-2, and this ticket's findings in research/05-graphics-composition/landmark-renderer.md
// (the buffer layout per model file and the three vertex forms, read from the 3907/3910 captures' buffers). Nothing of
// the game is committed: the database, the art files and the .gr2 files are read from the player's install at run time.
//
// Layers, each usable on its own:
//   1. meshKey()                 the identity of one EXE draw from its index buffer and range
//   2. layoutFile() / repack*()  one .gr2 -> the EXE's buffer pair for that file (which meshes, their order, the
//                                leading index, the vertex form) and the draw ranges
//   3. ModelLibrary              the catalogue of every model with its keys, built at load; the buffers made resident
//                                on first use, with a memory count
//
// Failure behaviour: every function returns a c5r::Status; nothing throws. A model whose file is missing or refused is
// kept out of the identity set and listed in problems() (the compositor then leaves its draws to the EXE, 3.2).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "c5models/art.h"
#include "c5models/gr2.h"
#include "c5r/status.h"

namespace c5r {
namespace gamefs { class GameFiles; }
namespace cities { class ArtCatalogue; }
namespace farmfields { struct FarmRowChoice; }   // c5r/farmfields/buildings.h (ticket 0653)
namespace landmarks {

// ---------------------------------------------------------------------------------------------- the key
enum class ModelClass : uint8_t { CityBlock, CityPiece /* wonder, display building, wall, ruins */,
                                  Landmark /* improvement, resource, feature, pontoon */, Unknown };
const char* className(ModelClass c);

// One EXE draw's mesh (city-renderer.md 2.2; the rule of 4870's P3b, thread 0561 #6015): the model file's buffer pair
// and the draw's range. indexHash: FNV-1a 64 of the index buffer's bytes from element `hashFrom` to its end (1 for the
// EXE's padded R16 buffers: the pad element varies between loads [C, 4870]); vertexHash: FNV-1a 64 of the whole vertex
// buffer (euro_anc and medit_anc, asia_anc and poly_anc share an index buffer; the first pair shares the vertex buffer
// too and differs only in its textures, so a key can name more than one model: ModelLibrary::candidates()).
struct MeshKey {
    uint64_t indexHash = 0;
    uint64_t vertexHash = 0;
    uint32_t firstIndex = 0, indexCount = 0;
    int32_t baseVertex = 0;
    bool operator==(const MeshKey& o) const {
        return indexHash == o.indexHash && vertexHash == o.vertexHash && firstIndex == o.firstIndex &&
               indexCount == o.indexCount && baseVertex == o.baseVertex;
    }
    bool operator!=(const MeshKey& o) const { return !(*this == o); }
    bool operator<(const MeshKey& o) const;
};
struct MeshKeyHash { size_t operator()(const MeshKey& k) const; };

constexpr uint64_t kFnvOffset = 14695981039346656037ull;   // the standard FNV-1a 64 basis (c5m::fnv1a64 uses another)
constexpr uint64_t kFnvPrime = 1099511628211ull;
uint64_t fnv1a64(const void* data, size_t bytes, uint64_t h = kFnvOffset);

// The key of a draw (firstIndex, indexCount, baseVertex) over an index buffer of `count` elements of `indexBytes`
// (2 or 4) bytes each and the vertex buffer the draw binds (`vertexBytes`: the rigid or the skinned one). Checks:
// indexBytes not 2 or 4, indexCount 0, a range past the index buffer's end, hashFrom past it, no vertex bytes
// [BadLayout].
Status meshKey(const void* indices, size_t count, uint32_t indexBytes, uint32_t hashFrom, const void* vertices,
               size_t vertexBytes, uint32_t firstIndex, uint32_t indexCount, int32_t baseVertex, MeshKey& out);

// ---------------------------------------------------------------------------------------------- the file layout
// The slot-0 vertex the EXE uploads (landmark-renderer.md 2.1, the captures' input layouts [C]):
//   Float32    32 B: POSITION R32G32B32, NORMAL R32G32B32, TEXCOORD0 R32G32              (city blocks)
//   Half20     20 B: POSITION R16G16B16A16_FLOAT (w 0), NORMAL R16G16B16A16_FLOAT (w 0), TEXCOORD0 R16G16_FLOAT
//                    (single models: landmarks, wonders, walls)
//   Skinned28  28 B: POSITION half4, BLENDWEIGHT R8G8B8A8_UNORM, BLENDINDICES R8G8B8A8_UINT, NORMAL half4,
//                    TEXCOORD0 half2 (BuildingShader_Skinned meshes: a second vertex buffer of the same file)
// Half20 and Skinned28 are the units' two streams: the model reader's repack_unit_mesh (unit_mesh.h, the EXE's load
// rule: the file's basis at the model's scale, Position x scale, Normal / scale, Granny's half conversion with ties
// away from zero), called here, not copied. Float32 is the same basis at scale 1 in float32, no halves
// (landmark-renderer.md 2.1).
enum class VertexForm : uint8_t { Float32 = 0, Half20, Skinned28 };
uint32_t vertexStride(VertexForm f);
const char* formName(VertexForm f);

// One draw of a file's buffer: one tri group of one mesh.
struct Part {
    uint32_t fileMesh = 0;          // index of the mesh in the .gr2
    uint32_t group = 0;             // index of the tri group in the mesh
    bool skinned = false;           // drawn from the file's skinned vertex buffer (28 B), else from the rigid one
    std::string mesh;               // the .gr2 mesh name
    std::string material;           // the group's material name
    uint32_t materialIndex = 0;     // its index in the file's Materials
    std::string shaderSet;          // its ExtendedData ShaderSet ("BuildingShader")
    std::vector<std::pair<std::string, std::string>> textures;   // the material's maps: (usage, lower-case file)
    // The textures the EXE binds as PS t0 and t1, lower case: the material's FireGrafix parameter set
    // BuildingTextures.Diffuse / .BuildingSREF when it names a file (VP's resource_tea.gr2 reuses cotton's geometry
    // and maps and names tea_diff.dds there), else its Diffuse / BuildingSREF map; "" when neither names one
    // [C, 4873: the wire keys of the captures' draws, landmark-renderer.md 7.4].
    std::string t0, t1;
    uint32_t vertexCount = 0;       // the mesh's vertices
    MeshKey key;                    // the draw's range in the file's buffer and its key
};

// The rule that decides which meshes of a file go into its buffer [C, landmark-renderer.md 2.2; 4870's P3b]: the
// meshes whose every tri group's material has a shader set starting "BuildingShader" (BuildingShader,
// BuildingShader_Skinned), in the file's mesh order. The others (LandmarkShader ground planes and decals,
// LandmarkShader_Stencil tree cuts) are listed, not packed.
bool packedMesh(const c5m::FileInfo& fi, const c5m::Mesh& m);
// The order the EXE packs a file's meshes in [C, landmark-renderer.md 2.2]: the models in file order, each model's mesh
// bindings in order, a mesh bound twice taken once; meshes no model binds after them in file order [I: none seen].
// (fish.gr2 binds mesh 1 before mesh 0; in every other sampled file this is the file's mesh order.)
std::vector<uint32_t> meshOrder(const c5m::FileInfo& fi);

// The layout of one file as the EXE uploads it [C, landmark-renderer.md 2.2; 4870's P3b #6015, #6025]: one index
// buffer starting with one element the draws never read (kLeadIndices; its value varies between loads), then every
// packed mesh's indices as stored, in file order; two vertex buffers, the rigid meshes' (form: Float32 or Half20) and
// the skinned meshes' (Skinned28), each mesh's base vertex counted within its own buffer.
constexpr uint32_t kLeadIndices = 1;
struct FileLayout {
    VertexForm form = VertexForm::Half20;   // the rigid buffer's form
    float scale = 1;                // the model's scale baked into the unit streams' vertices
    uint32_t indexBytes = 2;        // 2 (R16); 4 (R32) only when a packed mesh has an index above 0xFFFF: a file's
                                    // 32-bit indices that fit 16 bits are drawn R16 by the EXE [C, 4878, city-renderer.md 12.4]
    uint32_t vertexCount = 0;       // the rigid buffer's vertices
    uint32_t skinnedVertexCount = 0;
    uint32_t indexCount = 0;        // kLeadIndices included
    std::vector<Part> parts;
    std::vector<std::string> unpacked;   // the meshes not packed, by name
};
// The rigid form: Float32 when cityBlock, else Half20. A mesh is skinned when a tri group's material has the shader
// set BuildingShader_Skinned. Checks per packed mesh: a skinned mesh in a city set; (Float32) no Real32 Position:3 /
// Normal:3 / TextureCoordinates0 (2-4); no indices or a count not a multiple of 3; an index out of range; a tri group
// empty or past the mesh's indices [Unsupported]; a material index not in the file [BadLayout]; no packed mesh
// [Unsupported]; a scale not finite and > 0 [BadLayout].
Status layoutFile(const c5m::FileInfo& fi, bool cityBlock, float scale, FileLayout& out);

// The buffers of a layout: the rigid vertices (vertexCount x stride of form), the skinned ones (skinnedVertexCount x
// 28), the indices (indexCount x indexBytes, the lead element 0).
struct FileBuffers { std::vector<uint8_t> vertices, skinnedVertices, indices; };
// Checks: the layout's form Skinned28 [BadLayout]; the file's basis (c5m::unit_basis) refused [BadLayout]; a mesh
// repack_unit_mesh refuses (a component missing, more than 65,536 vertices, more than 32 bones) [Unsupported]; a layout
// that does not fit the file [BadLayout]. On failure out is empty.
Status repackFile(const c5m::FileInfo& fi, const FileLayout& lay, FileBuffers& out);

// The FireGrafix parameter sets of a file's materials (ticket 4873, landmark-renderer.md 7.4): per material in the
// file's Materials order, every (ParamBlock, ParamName, file) of every set. The EXE binds these files as t0 / t1, not
// the 3ds Max Maps (the units' finding, unit-renderer.md 10.8; here 7.4). Checks: the file does not load, a parse
// error in a set [BadLayout].
struct MaterialParam { std::string block, name, value; };
Status materialParams(const uint8_t* data, size_t size, const c5m::StringDatabase* strings,
                      std::vector<std::vector<MaterialParam>>& out);

// ---------------------------------------------------------------------------------------------- the library
enum class Source : uint8_t { LandmarkRow, Wonder, Wall, GreatWall, CitySet };
const char* sourceName(Source s);

// One model file (.gr2) at one scale and its buffer (the scale is baked into the unit streams' vertices, so a file used
// at two scales is two buffers).
struct ModelFile {
    std::string gr2;                // lower-case base name
    float scale = 1;
    std::string origin;             // where the game's files had it
    ModelClass cls = ModelClass::Unknown;
    FileLayout layout;
    std::vector<std::string> models;    // the .gr2's model names
    std::vector<std::vector<uint32_t>> modelParts;   // per model: its parts (indices into layout.parts)
    bool ok = false;                // read and laid out; false: see problem
    std::string problem;
};

// One thing the EXE draws by name.
struct Model {
    std::string name;               // landmarks and wonders: the .fxsxml base name; walls and city sets: the .gr2
    Source source = Source::LandmarkRow;
    ModelClass cls = ModelClass::Unknown;
    int32_t file = -1;              // index into files(), -1 when its file could not be named
    float scale = 1;                // the first row's / art entry's scale
    bool animated = false;          // the .fxsxml names an <Animation>
    std::vector<uint32_t> rows;     // LandmarkRow: indices into landmarkRows()
    std::vector<std::string> wonderTypes;   // Wonder: the WonderArtInfo Types that name it
    std::vector<uint32_t> citySets; // CitySet: indices into the catalogue's citySets()
    std::string problem;            // why file is -1 (its .fxsxml missing, unreadable, malformed or meshless)
};

// ArtDefine_Landmarks as the EXE reads it (cities-and-landmarks.md 2.1), kept as data for the layout (4881).
enum class Handler : uint8_t { Snapshot = 0, Farm, Special, Pasture, Spring, Animated, AnimatedRotated, Random };
Handler handlerFromName(const std::string& s);     // anything unknown -> Snapshot (the EXE's 0x7304E0)
struct LandmarkRow {
    c5m::LandmarkRow row;           // ImprovementType, ResourceType, Era, State, Scale, Model, LayoutHandler, Tech
    Handler handler = Handler::Snapshot;
    uint8_t contour = 0;            // TerrainContour as the EXE's one byte [I: 0.75 -> 0]
    int32_t model = -1;             // index into models(); -1 for a NULL model
};

struct LibraryCounts {
    uint32_t landmarkRows = 0, landmarkRowsWithModel = 0, landmarkModels = 0;
    uint32_t wonderTypes = 0, wonderModels = 0, wonderTypesDrawn = 0;   // drawn: a display building's form
    uint32_t wallFiles = 0, greatWallFiles = 0;
    uint32_t citySetEntries = 0, citySetFiles = 0;
    uint32_t files = 0, filesOk = 0, filesFailed = 0;
    uint32_t parts = 0, keys = 0;
    uint32_t keyCollisions = 0;     // a key held by parts of more than one file (identity not unique)
    uint32_t keyCollisionsTextured = 0;   // ... whose parts name different textures
    uint32_t paramsUnread = 0;      // laid-out files whose parameter sets did not parse (their maps name t0 / t1)
    uint64_t catalogueBytes = 0;    // an estimate of what the catalogue keeps (names, parts, keys)
    double loadSeconds = 0;         // the whole load (a workstation indication, never a timing claim)
};

class ModelLibrary {
public:
    ModelLibrary();
    ~ModelLibrary();
    ModelLibrary(const ModelLibrary&) = delete;
    ModelLibrary& operator=(const ModelLibrary&) = delete;

    // Builds the catalogue: the ArtDefine_Landmarks rows (database), the wonder art, wall, Great Wall and city-set
    // files (the art catalogue), every named file read once and laid out (keys computed, the bytes dropped). Checks:
    // the art catalogue not loaded [NotFound]; ArtDefine_Landmarks missing [NotFound]. A model file that is missing or
    // refused is not an error: its ModelFile has ok = false and a problem, and its parts hold no keys.
    Status load(const gamefs::GameFiles& files, c5m::TableSource& db, const cities::ArtCatalogue& art);
    bool loaded() const { return loaded_; }

    const std::vector<ModelFile>& files() const { return files_; }
    const std::vector<Model>& models() const { return models_; }
    const std::vector<LandmarkRow>& landmarkRows() const { return rows_; }
    const LibraryCounts& counts() const { return counts_; }
    const Model* model(const std::string& name) const;     // by name (lower case)
    int32_t fileIndex(const std::string& gr2, float scale) const;   // -1 when not in the library

    // The part a key identifies; null when no laid-out file holds it. When several files hold it (counts'
    // keyCollisions), the first file in files() order; candidates() gives all of them (told apart by Part::t0 / t1).
    struct Hit { uint32_t file = 0, part = 0; };
    const Hit* find(const MeshKey& k) const;
    std::vector<Hit> candidates(const MeshKey& k) const;
    // Every key of a class, sorted (the identity set the server publishes, city-renderer.md 3.2).
    std::vector<MeshKey> keys(ModelClass c) const;

    // ---- the farm buildings (ticket 0653, D230; farm-fields-layout.md 16) ----
    // The farm manager's rows (cities-and-landmarks.md 2.3, 2.5): LayoutHandler FARM, ImprovementType
    // ART_DEF_IMPROVEMENT_FARM, no resource (ResourceType empty, ART_DEF_RESOURCE_NONE, RESOURCE_NONE or NONE), State Any
    // or empty; per era level (farmfields::farmRowLevel of the row's Era) at most 3, in table order. The other FARM rows
    // (the 18 wheat / maize / rice rows) are counted, not chosen (no manager chooses them, 2.3 there). A farm-building row
    // that cannot be drawn is refused and listed with a text naming it (D118: the caller writes one LOUD line per row,
    // never a stand-in): a NULL model, a .fxsxml missing, unreadable or meshless, a model file the library refused, an
    // Era text that is no era, a fourth row of one level. Checks: the library not loaded [NotFound].
    Status farmRows(farmfields::FarmRowChoice& out) const;
    // The laid-out model file of a landmark row, for a drawer that has a row and no tapped draw to copy (the farm
    // buildings' own instances): its index into files(), -1 when the row is out of range, has no model or the model's file
    // is not laid out. The file's buffers then come from resident(), its parts from files()[i].layout.parts.
    int32_t rowFile(int32_t row) const;
    // The model of a farm-building file for a farm's state [C, file read 2026-11-21 by 0653: each of the six files holds
    // three models, "PL_<name>" (pillaged), "HB_<name>" (half built) and "<name>", each with one BuildingShader barn mesh,
    // farm-fields-layout.md 16.1]: state 3 (pillaged) the model named "PL_...", 1 (half built) "HB_...", anything else
    // the one with neither prefix; the index into files()[file].models / modelParts, -1 when the file is out of range,
    // not laid out, or has no such model (compared without case).
    int32_t farmBuildingModel(int32_t file, uint8_t state) const;

    // Residency: a file's buffers, made on first use (read and repacked again). Checks: index out of range
    // [BadLayout]; a file that is not ok [NotFound]; the read or repack failing [its code].
    // The CPU copies' readers [C, ticket 0581, city-renderer.md 12.10]: WireIndex::build (the wire hashes, at the open)
    // and ModelRenderer::fileBuffers (the IMMUTABLE upload); nothing reads a file's copy after its upload, so the
    // renderer releases it there. A pointer from resident() is valid until release(file) or evictAll(); a later
    // resident() of a released file reads and repacks it again (byte-equal: the repack is deterministic).
    using Buffers = FileBuffers;
    Status resident(uint32_t file, const Buffers** out);
    bool isResident(uint32_t file) const;
    uint64_t residentBytes() const { return residentBytes_; }
    uint32_t residentFiles() const { return residentFiles_; }
    // Drops one file's CPU copy (its memory returned, the counts lowered); a file not resident or out of range: nothing.
    void release(uint32_t file);
    void evictAll();

private:
    Status readFile(const std::string& gr2, c5m::FileInfo& fi, std::string* origin,
                    std::vector<uint8_t>* bytes = nullptr) const;
    int32_t addFile(const std::string& gr2, float scale, ModelClass cls);

    bool loaded_ = false;
    const gamefs::GameFiles* gf_ = nullptr;
    std::unique_ptr<c5m::StringDatabase> strings_;
    std::vector<ModelFile> files_;
    std::vector<Model> models_;
    std::vector<LandmarkRow> rows_;
    std::map<std::string, uint32_t> modelIndex_;
    std::map<std::string, uint32_t> fileIndex_;
    std::unordered_map<MeshKey, std::vector<Hit>, MeshKeyHash> keys_;
    std::vector<std::unique_ptr<Buffers>> resident_;
    uint64_t residentBytes_ = 0;
    uint32_t residentFiles_ = 0;
    LibraryCounts counts_;
};

}}  // namespace c5r::landmarks
