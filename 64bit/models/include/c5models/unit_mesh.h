// unit_mesh.h - a unit mesh of a .gr2 converted into the EXE's unit vertex streams (issue 4152, epic 0327, premise K1;
// the exact load rule since ticket 0447). Own code from the knowledge base's description of the layout and of the
// rule (research/05-graphics-composition/unit-renderer.md 10.9, unit-playback.md section 3): no EXE code, no game data.
//
// The EXE's rule (unit-renderer.md 10.9, ticket 0379; byte-equal to the capture on all 515 drawn meshes of the
// catalogue):
//   1. Per model file and member Scale, Granny's basis conversion: desired units per metre fl(UnitsPerMeter x Scale),
//      desired axes right (1, 0, 0), up (0, 0, 1), back (0, -1, 0), origin 0. L = the 3 x 3 product of the desired and
//      the file's axes, every element times k = fl(desired / UnitsPerMeter); I = the inverse of L by cofactors, each
//      times fl(1 / det); A = 0 - the file's origin. (UnitBasis, unit_basis.)
//   2. Every vertex, float32 in this order: Position' = A + ((L0 x + L1 y) + L2 z) (rows 3-5, 6-8 alike);
//      Normal' x = (ny I3 + nz I6) + nx I0, y = (ny I4 + nz I7) + I1 nx, z = (nz I8 + I2 nx) + I5 ny.
//   3. Float32 to half by Granny's conversion (half_from_float_granny: ties away from zero; below the normal range the
//      mantissa is shifted first, then rounded the same way; a NaN keeps its sign and its top ten mantissa bits).
//      Real16 texture coordinates are copied as they are; Real32 ones take the same conversion.
//   4. A mesh with two or more bone bindings takes the 28-byte stream, every other mesh the 20-byte one. In the
//      28-byte stream every vertex's weights are renormalised in float (unit_weights).
//
// 28-byte stream (slot 0, materials 89 / 90), per vertex of the mesh's PrimaryVertexData in .gr2 order:
//    0  POSITION     R16G16B16A16_FLOAT  Position', w = 0
//    8  BLENDWEIGHT  R8G8B8A8_UNORM      the .gr2 BoneWeights after step 4
//   12  BLENDINDICES R8G8B8A8_UINT       the .gr2 BoneIndices as they are (palette slots = the mesh's BoneBindings)
//   16  NORMAL       R16G16B16A16_FLOAT  Normal', not renormalised, w = 0
//   24  TEXCOORD     R16G16_FLOAT        TextureCoordinates0 u, v
// 20-byte stream (material 87): POSITION half4 @0, NORMAL half4 @8, TEXCOORD half2 @16.
// Index buffer: the mesh's indices as 16-bit values, in .gr2 order, base vertex 0.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "c5models/gr2.h"

namespace c5m {

constexpr uint32_t kUnitVertexStride = 28;     // the skinned stream
constexpr uint32_t kUnitStaticStride = 20;     // the stream of a mesh with fewer than two bone bindings
constexpr uint32_t kUnitPaletteBones = 32;     // bone slots of one palette block (SkinBoneCount)
// The EXE's units per metre for a file without ArtToolInfo (unit-renderer.md 10.9 step 1; what Granny then does with
// the file is not seen [I]: the module treats it as a file in the stock basis at this unit).
constexpr float kUnitDefaultUnitsPerMeter = 39.37008f;

struct UnitMesh {
    std::vector<uint8_t> vertices;    // vertex_count x stride bytes
    std::vector<uint16_t> indices;
    uint32_t vertex_count = 0;
    uint32_t stride = kUnitVertexStride;   // 28 (skinned) or 20 (static)
    uint32_t bones = 0;               // BoneBindings of the mesh (palette slots used: 0 .. bones - 1)
    uint32_t renormalised = 0;        // skinned vertices whose weight bytes step 4 changed
};

// Step 1: the conversion of one model file at one member Scale (row-major 3 x 3, column vectors).
struct UnitBasis {
    float k = 1;           // fl(fl(UnitsPerMeter x Scale) / UnitsPerMeter): Scale, or one or two ulps off it
    float L[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // L's inverse by cofactors; normals take its transpose
    float A[3] = {0, 0, 0};
};

// From an ArtToolInfo given by parts. False (err set) when scale or units_per_meter is not finite and > 0, an axis is
// not finite, or the axes give a singular L.
bool unit_basis(float units_per_meter, const float right[3], const float up[3], const float back[3],
                const float origin[3], float scale, UnitBasis& out, std::string* err);
// From the file's ArtToolInfo; a file without one (units per metre 0 and no axes) as the stock basis at
// kUnitDefaultUnitsPerMeter [I].
bool unit_basis(const FileInfo& f, float scale, UnitBasis& out, std::string* err);
// The two ArtToolInfos of the unit files: 3ds Max exports (39.370079 units per metre, right +x, up +z, back -y: L is k
// times the identity) and VP's FBX exports (1.0 units per metre, right +x, up +y, back +z: (x, y, z) -> (x, -z, y)).
bool unit_basis_stock(float scale, UnitBasis& out, std::string* err);
bool unit_basis_fbx(float scale, UnitBasis& out, std::string* err);

// The nearest IEEE half to x (ties to even; overflow to infinity; NaN kept as a quiet NaN), as its bit pattern.
uint16_t half_from_double(double x);
// The nearest half to x with ties away from zero (4152's rule; equal to half_from_float_granny in the normal range).
uint16_t half_from_float_away(float x);
// Step 3: Granny's float32 to half (unit-renderer.md 10.9).
uint16_t half_from_float_granny(float x);
double double_from_half(uint16_t h);

// Step 4 on one vertex's four UNORM8 weights (in place): s = (w2 + w1) + w0 in float; when s > 0, k = fl(255 / s),
// w0' = floor(fl(k w0) + 0.5), w1' = floor(fl(k w1) + 0.5), w2' = trunc(fl(255 - fl(w1' + w0'))) (low byte), w3' = 0;
// when s = 0 the weights stay as they are.
void unit_weights(uint8_t w[4]);

// True when the mesh takes the 28-byte stream (two or more bone bindings).
inline bool unit_mesh_skinned(const Mesh& m) { return m.bone_bindings.size() >= 2; }

// Converts mesh into out by the rule above, into the 28-byte stream when skinned, else the 20-byte one. Needs
// Position[3] (Real32 or Real16), Normal[3] and TextureCoordinates0[2+] (Real32 or Real16), and for the 28-byte stream
// NormalUInt8 BoneWeights[4] and UInt8 BoneIndices[4]. Fails (false, err set; out cleared) when a component is missing
// or of another type or outside the vertex, the vertex bytes are not count x stride, a vertex index needs more than
// 16 bits or is not below the vertex count, or (28-byte stream) the mesh binds more than kUnitPaletteBones bones
// (split into several draws: premise K8, not done here) or a weighted bone index (the first always) is not below the
// mesh's bone count.
bool repack_unit_mesh(const Mesh& mesh, const UnitBasis& basis, bool skinned, UnitMesh& out, std::string* err);
// The 28-byte stream of a mesh in a 3ds Max file (unit_basis_stock) at scale = ArtDefine_UnitMemberInfos.Scale of the
// member (a float in the database); also fails when scale is not finite and > 0.
bool repack_unit_mesh(const Mesh& mesh, float scale, UnitMesh& out, std::string* err);

// FNV-1a 64 over bytes (a fingerprint for comparisons; not a cryptographic hash).
uint64_t fnv1a64(const uint8_t* data, size_t size);

}  // namespace c5m
