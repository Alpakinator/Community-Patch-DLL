// c5r/units/draw.h - L1 of the unit module: the models' draw inputs (vertex streams, indices, textures), the unit
// programs read from the EXE file, the per-frame inputs (palette blocks, the per-instance id stream, cb1 per pass,
// material 87's per-draw cb0) and the three passes on the module's own device (epic 0041, U1 / issue 4352;
// research/05-graphics-composition/unit-renderer.md 5, 6.2 and 10; unit-draws.md 1-6).
//
// Nothing here is shipped or committed from the game (D83): meshes, textures and program bytes are read from the
// player's install at run time; the program table below holds SHA-256 digests only.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c5models/gr2.h"
#include "c5r/status.h"
#include "c5r/units/art.h"
#include "c5r/units/status.h"

namespace c5r { namespace units {

// ---------------------------------------------------------------------------------------------- constants
constexpr uint32_t kPaletteRows = 98;               // rows of one block: colour, fade, 32 bones x 3
constexpr uint32_t kPaletteBones = 32;
constexpr uint32_t kBlocksPerBuffer = 167;          // 16,384 float4 / 98
constexpr uint32_t kPaletteBufferBytes = 262144;
constexpr uint32_t kIdBufferBytes = 8192;           // 2,048 R32_UINT ids: all passes of a frame
constexpr uint32_t kCb1Bytes = 192;                 // 12 float4
constexpr uint32_t kStaticCb0Bytes = 256;           // material 87's cb0 (16 float4; registers 0-4 read)
constexpr uint32_t kSkinnedStride = 28;             // materials 89 / 90
constexpr uint32_t kStaticStride = 20;              // material 87

enum class Material : uint8_t { Static = 87, SkinnedAlpha = 89, Skinned = 90 };
enum class PassKind : uint8_t { ShadowMask = 0, Reflection = 1, Scene = 2 };   // the EXE's order within a frame
const char* passName(PassKind k);

struct Float4 { float x = 0, y = 0, z = 0, w = 0; };

// ---------------------------------------------------------------------------------------------- programs
// One program of the unit set, found in the EXE's .shader section by the SHA-256 of its DXBC container.
struct ProgramRef {
    Material material;
    PassKind pass;            // Reflection and Scene share the pass-0 programs
    bool vertex;              // true VS, false PS
    const char* sha256;       // lower-case hex digest of the DXBC container
    uint32_t va;              // where build 4390913 holds it (informational; lookup is by digest)
};
// The 12 rows (6 VS + 6 PS references: materials 90, 89, 87 x pass 0 / pass 1; unit-playback.md 6 K9).
const std::vector<ProgramRef>& unitProgramTable();

class UnitPrograms {
public:
    // Reads the EXE (read-only), splits its .shader section and finds every program of unitProgramTable() by digest.
    // Errors: the section's (Io, NotFound, ...); a digest not present -> NotFound naming it.
    Status open(const std::wstring& exePath);
    // The same from a list of DXBC containers (tests).
    Status openFromBlobs(const std::vector<std::vector<uint8_t>>& blobs);
    // DXBC bytes of a program, or nullptr.
    const std::vector<uint8_t>* bytes(Material m, PassKind p, bool vertex) const;
    std::string sha256(Material m, PassKind p, bool vertex) const;
    // Creates the shaders and the two input layouts (28-byte + id, 20-byte) on dev.
    Status create(ID3D11Device* dev);
    ID3D11VertexShader* vs(Material m, PassKind p) const;
    ID3D11PixelShader* ps(Material m, PassKind p) const;
    ID3D11InputLayout* layout(Material m, PassKind p) const;   // made against that pass's VS signature
    void release();
    ~UnitPrograms() { release(); }
private:
    static int key(Material m, PassKind p, bool vertex);
    std::map<int, std::vector<uint8_t>> bytes_;
    std::map<int, std::string> sha_;
    std::map<int, ID3D11DeviceChild*> objects_;
    std::map<int, ID3D11InputLayout*> layouts_;
};

// Lower-case hex SHA-256 (Windows CNG).
std::string sha256Hex(const uint8_t* data, size_t size);

// ---------------------------------------------------------------------------------------------- models
struct UnitTexture {
    std::string name;                  // lower-case file name
    std::vector<uint8_t> file;         // the .dds as read
    uint32_t width = 0, height = 0, mips = 0, faces = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<uint8_t> payload;      // every subresource in D3D11 order, concatenated (what a capture stores)
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Resource* resource = nullptr;
};

struct ModelMesh {
    std::string name;                  // the .gr2 mesh name
    uint32_t index = 0;                // in the .gr2's mesh list
    Material material = Material::Skinned;
    uint32_t stride = 0;               // 28 or 20
    uint32_t vertex_count = 0;
    std::vector<uint8_t> vertices;     // vertex_count x stride
    std::vector<uint16_t> indices;
    uint32_t bones = 0;                // bone bindings (palette slots 0 .. bones-1)
    std::vector<uint32_t> tie_vertices;   // empty since 0447 (unit-renderer.md 10.9: no weight ties left to list)
    std::string base_texture, sref_texture;   // t0, t1 (lower-case names)
    const UnitTexture* t0 = nullptr;
    const UnitTexture* t1 = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
};

struct UnitModel {
    std::string gr2;                   // lower-case basename
    float scale = 0;                   // the member's Scale applied to the positions
    bool y_up = false;                 // the file's ArtToolInfo says y up: turned to z up at load (premise P4)
    std::unique_ptr<c5m::FileInfo> file;   // the parsed .gr2 (skeleton and meshes for the pose step)
    std::vector<ModelMesh> meshes;     // the meshes drawn, in .gr2 order
    std::vector<std::string> skipped;  // meshes not drawn and why ("Oars_L_DMG: damage state")
    std::vector<std::string> fallbacks;   // "TextureFallback: <gr2> <mesh>: <file>" (4356, 10.8)
};

// The EXE's texture for a file in no install or mod (4356, unit-renderer.md 10.8 [L, one data point]): 8 x 8
// R8G8B8A8, one mip, texel 0 = (255, 0, 0, 255), every other texel 0. Built in code.
constexpr const char* kFallbackTexture = "(fallback 8x8)";

// Builds a member's model: .gr2 read, meshes repacked by material, textures decoded. Per (gr2, scale) once.
// Codes: ModelNotFound (.gr2 absent), BadModel (reader or repack refused), TooManyBones, UnknownLayout,
// TextureNotFound (a texture the mesh's material names exists but does not parse). A texture in no file at all
// binds the EXE's fallback (kFallbackTexture) and is listed in UnitModel::fallbacks.
class ModelCache {
public:
    explicit ModelCache(c5m::FileProvider& files) : files_(files) {}
    ~ModelCache();
    // The union of the install's string databases (*.gsd); unit .gr2 files keep their names there. Not owned.
    void setStrings(const c5m::StringDatabase* s) { strings_ = s; }
    UnitStatus get(const MemberArt& member, const UnitModel*& out);
    // A texture by name (a .tga / .psd name falls back to the .dds of its stem), decoded once. TextureNotFound.
    // A B8G8R8A8 file's payload is kept in R8G8B8A8 order, as the EXE uploads it (unit-renderer.md 10.3).
    UnitStatus texture(const std::string& name, const UnitTexture*& out);
    // The EXE's fallback texture (kFallbackTexture), made once.
    const UnitTexture* fallbackTexture();
    // Creates the IMMUTABLE vertex and index buffers and the textures of every model loaded so far.
    Status upload(ID3D11Device* dev);
    size_t models() const { return models_.size(); }
    void clear();
private:
    UnitStatus build(const MemberArt& member, UnitModel& m);
    c5m::FileProvider& files_;
    const c5m::StringDatabase* strings_ = nullptr;
    std::map<std::string, std::unique_ptr<UnitModel>> models_;      // key: gr2 + "|" + scale bits
    std::map<std::string, UnitStatus> failed_;
    std::map<std::string, std::unique_ptr<UnitTexture>> textures_;
};

// Every *.gsd the file set resolves, added to `out` in name order. BadModel naming the file that does not load.
UnitStatus loadStringDatabases(const std::vector<std::string>& names, c5m::FileProvider& files,
                               c5m::StringDatabase& out);

// The repacks themselves, usable alone (tests, the oracle).
// 28-byte stream (89 / 90): models' unit_basis + repack_unit_mesh, the EXE's load rule (unit-renderer.md 10.9, 0447);
// equal to the EXE's buffers on every drawn mesh, so there is no tie list (ModelMesh::tie_vertices stays empty).
UnitStatus repackSkinned(const c5m::Mesh& mesh, float scale, bool y_up, ModelMesh& out);
// 20-byte stream (87): POSITION half4 (Position x Scale, w 0), NORMAL half4 (Normal / Scale, w 0), TEXCOORD half2;
// float32 products, halves rounded to nearest with ties away from zero (unit-renderer.md 10.2).
UnitStatus repackStatic(const c5m::Mesh& mesh, float scale, bool y_up, ModelMesh& out);
// Position and Normal of every vertex times M (3 x 3, row-major, column vectors), in place. False when the mesh has
// no Real32 Position[3] or its bytes are inconsistent.
bool applyBasis(c5m::Mesh& mesh, const float M[9]);
// A mesh with Real16 Normal / TextureCoordinates0 (Tangent and Binormal dropped) as Real32 Position, BoneWeights,
// BoneIndices, Normal, TextureCoordinates0, halves widened exactly. False when a needed component is missing.
bool widenReal16(const c5m::Mesh& in, c5m::Mesh& out);
// -0.0 components of Position and Normal set to +0.0 (the cache applies it to Real16-sourced meshes before a repack).
void positiveZeros(c5m::Mesh& mesh);
// Which material a .gr2 mesh is drawn with: no BoneWeights -> 87; else 89 when its material's name or shader set
// says alpha (unit-renderer.md 10.2), else 90. UnknownLayout when neither stream can be built from its components.
UnitStatus meshMaterial(const c5m::FileInfo& f, const c5m::Mesh& mesh, Material& out);

// ---------------------------------------------------------------------------------------------- frame inputs
// cb1 of a pass (unit-draws.md 3): r[0..2] camera rotation rows (not read), r[3] camera position, r[4..7]
// view-projection rows (x r4 + y r5 + z r6 + r7), r[8].x the height threshold (water level), r[9] fog factors and
// the ramp row, r[10] fog colour, r[11] camera position (fog distance).
struct Cb1 { Float4 r[12]; };

struct PassDesc {
    PassKind kind = PassKind::Scene;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;   // none for the mask pass
    D3D11_VIEWPORT viewport = {};
    Cb1 cb1;
};

// The PS's environment slots t2-t5 (irradiance cube, dull and sharp environment cubes, fog ramp). Null = the
// module's own, read from the install at open(): unit_irradiance.dds, unit_environment_dull.dds,
// unit_environment_sharp.dds, fogramp.dds (premise P1 answered, unit-renderer.md 10.3).
struct PassSet {
    std::vector<PassDesc> passes;           // drawn in this order
    ID3D11ShaderResourceView* env[4] = {};  // t2, t3, t4, t5
};
extern const char* const kEnvironmentTextures[4];

// The camera of a pass as cb1 carries it; makeCb1 writes the 12 registers (unit-draws.md 3). The unread lanes
// (r[8].yzw, r[11].w, the w of rows 0-3) are written 0 except r[3].w = 1.
struct PassCamera {
    Float4 rotation[3];        // r0-r2 (not read by the unit programs)
    float eye[3] = {0, 0, 0};  // r3.xyz and r11.xyz
    Float4 viewProj[4];        // r4-r7
    float threshold = 0;       // r8.x: the water level
    Float4 fog;                // r9: x, y factors, w the ramp row
    Float4 fogColour;          // r10
};
Cb1 makeCb1(const PassCamera& c);
PassCamera cameraFromCb1(const Cb1& c);

// ---------------------------------------------------------------------------------------------- checks (DoD B)
// Comparisons of the module's draw inputs with a capture's, one function per check, for the oracle run (4355).
struct BufferCompare {
    bool sizeMatch = false;
    uint32_t elements = 0, equal = 0;      // vertices (or indices)
    std::vector<uint32_t> differ;          // element indices that differ (first 64)
    bool all() const { return sizeMatch && equal == elements; }
};
BufferCompare compareVertices(const ModelMesh& ours, const uint8_t* captured, size_t bytes);
BufferCompare compareIndices(const ModelMesh& ours, const uint8_t* captured, size_t bytes);
// Row 0 rgb within one 8-bit step and a equal; row 1 x, y, z equal (w not read) [unit-draws.md 4-5].
bool headerRowsEqual(const Float4 ours[2], const float captured[8], float* maxDiff);
// cb1: every lane the unit programs read (r3.xyz, r4-r7, r8.x, r9, r10, r11.xyz) bit-equal.
bool cb1ReadLanesEqual(const Cb1& a, const Cb1& b, std::string* why);
// Captured ids of one model's draws (one list per skinned mesh j, the instances in draw order): the figure-major
// block rule holds when {id - j} is the same set of `figures` blocks for every j.
bool idsFigureMajor(const std::vector<std::vector<uint32_t>>& perMesh, uint32_t figures, std::string* why);

// Palette row 0 (team colour) and row 1 (fade) of a block.
void paletteHeader(const float colour[4], float fade_from, float fade_to, float fade, Float4 rows[2]);
// Material 87's cb0 (registers the programs read: 0-4): rows 0-2 the mesh's 3 x 4 world matrix (the rigid mesh's
// palette rows for its bone slot 0 when it binds bones, else [Rz(yaw) | T]), row 3 colour, row 4 fade; rest 0.
void staticCb0(const float colour[4], float fade_from, float fade_to, float fade, const Float4 world[3],
               Float4 out[16]);
// [Rz(yaw) | T] as three rows.
void yawRows(float yaw_rad, const float T[3], Float4 out[3]);

}}  // namespace c5r::units
