// c5r/pathmodels/pathmodels.h - the move-order path's models (MovementPath's discs, chevrons and destination ring) placed
// and drawn by the server (ticket 0300, part 1: offline module and tests; increment 3 groundwork, D143).
//
// Written from the knowledge base's description of the EXE's MovementPath system (D83; no EXE code):
// research/02-exe-internals/arrows-and-movement-path.md section 2 (assets, events, the placement rule, the draw), 3801's
// stock capture (research/05-graphics-composition/arrows-and-path-line.md section 4) and this ticket's findings in
// research/05-graphics-composition/path-models.md (the order of the draws, the connector frame, the mesh matrices).
// Nothing of the game is committed: meshes, textures and the two UIShader programs are read from the player's install
// and EXE at run time; the program table holds SHA-256 digests only.
//
// Three layers, each usable on its own:
//   1. placePath()          the instance list: (asset, world matrix) per marker, from the path nodes and their ground
//                           positions (nodePositions() gives those from the EXE's height field)
//   2. PathModels           the 18 assets of assets/UI/Overlays: .fxsxml -> .gr2 meshes repacked to the EXE's 32-byte
//                           vertex (POSITION, NORMAL, TEXCOORD0), material textures, the SelectedAni* clips
//      buildDraws()         instances -> one draw per mesh (index count, cb0 world matrix, textures), in the EXE's order
//   3. PathRenderer         the draws on a D3D11 device with UIShader (material 81) from the EXE and the capture's states
//
// Matrices are row-major with row vectors, as the EXE's constant buffers hold them: a world position is
// [x y z 1] x M, rows 0-2 the axes, row 3 the translation (cb0 rows 0-3 of every path draw).
//
// Failure behaviour: every function returns a c5r::Status; nothing throws, nothing is drawn from a failed step.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "c5models/anim.h"
#include "c5models/gr2.h"
#include "c5r/status.h"

namespace c5r {
namespace gamefs { class GameFiles; }
namespace heightfield { struct HeightGrid; }
namespace pathmodels {

// ---------------------------------------------------------------------------------------------- assets
// The EXE's asset index (arrows-and-movement-path.md 2.1): the order of the 18 names it loads.
enum class Asset : uint8_t {
    CenterOneTurn = 0, CenterTwoTurns, CenterThreeTurns, CenterInaccessible, CenterEmbark, CenterAttack,
    SelectedAniOneTurn, SelectedAniTwoTurns, SelectedAniThreeTurns, SelectedAniInaccessible, SelectedAniEmbark,
    SelectedAniAttack,
    ConnectorOneTurn, ConnectorTwoTurns, ConnectorThreeTurns, ConnectorInaccessible, ConnectorEmbark, ConnectorAttack,
};
constexpr uint32_t kAssetCount = 18;
const char* assetName(Asset a);                 // "CenterOneTurn", ...; "?" out of range

// ---------------------------------------------------------------------------------------------- placement
constexpr uint32_t kMaxNodes = 64;              // the path record's node limit (arrows-and-movement-path.md 2.2)
constexpr uint32_t kMaxInstances = 256;         // the EXE's instance list (2.2)
constexpr uint32_t kConnectors = 6;             // chevrons per step
constexpr uint32_t kLastConnectors = 3;         // chevrons on the step into the destination
constexpr float kTurnEndScale = 1.5f;           // a node where a turn's move ends, and the last node
constexpr float kMidScale = 0.75f;

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Mat4 { float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; };   // row-major, row vectors

// One node as the EXE stores it from the served path (GetX, GetY, GetTurnNumber, each kept as 16 bits).
struct PathNode {
    int16_t x = 0, y = 0;
    int16_t turn = 0;                           // under VP: turns x 100 + 10 x moves + moves left (2.3 "VP note")
};

enum class CursorKind : uint8_t { Inaccessible = 0, Embark, Attack };

struct PathInput {
    std::vector<PathNode> nodes;                // 0 .. kMaxNodes; node 0 is the unit's plot
    std::vector<Vec3> pos;                      // one ground position per node (nodePositions())
    // Per node: the EXE's embark / disembark tests between this node and the previous one held (2.3; the DLL's
    // answers, served in part 2). Empty = none; else one entry per node (entry 0 is not read).
    std::vector<uint8_t> embark;
    bool lastIsAttack = false;                  // the last node's plot holds an attack target (an enemy unit or city)
    // No path (nodes empty): one marker at the cursor's plot.
    bool show = true;                           // DisplayMovementIndicator(bShow): false clears the list
    Vec3 cursor;
    CursorKind cursorKind = CursorKind::Inaccessible;
};

struct Instance {
    Asset asset = Asset::CenterOneTurn;
    Mat4 world;
    uint16_t node = 0;                          // the node it belongs to (0 for the cursor marker)
};

struct PlacementStats {
    uint32_t dropped = 0;                       // instances past kMaxInstances, not placed [I: the EXE stops adding]
    uint32_t zeroSteps = 0;                     // steps between two nodes at one position: no connectors
};

// The placement rule (arrows-and-movement-path.md 2.3; order and connector frame from 3801's capture, path-models.md
// 3): node 0 -> CenterOneTurn, unscaled; node i >= 1 -> [SelectedAni* if last], Center*, then the connectors of the
// step (i-1 -> i), each node's asset chosen by attack (last node only), embark, then the turn value 1 / 2 / other.
// Checks: more than kMaxNodes nodes, pos.size() != nodes.size(), embark neither empty nor one per node, a position
// that is not finite [BadLayout]. show = false gives an empty list (Ok). On failure `out` is cleared.
Status placePath(const PathInput& in, std::vector<Instance>& out, PlacementStats* stats = nullptr);

// The ground position of each node (2.3): the plot centre (units::plotCentre, the EXE's HexToWorld) and z = the height
// field there (units::heightAtWorld); z = floorZ when the height is below it, when the active team has not revealed
// the plot (fogRevealed[i] != 0; the EXE asks GetActiveFogOfWarMode() == 1, FOGOFWARMODE_UNEXPLORED: corrected
// 2026-11-18, ticket 0300 part 2, path-models.md 7.1; the parameter keeps its name), or when missing[i] != 0 (no plot
// object). The EXE's floor is the constant 10.116731 (path-models.md 7.1).
// fogRevealed / missing: empty or one per node. Checks: those of plotCentre (a plot outside the map) [BadLayout]; a
// size mismatch or a floor that is not finite [BadLayout].
Status nodePositions(const heightfield::HeightGrid& g, const std::vector<PathNode>& nodes,
                     const std::vector<uint8_t>& fogRevealed, const std::vector<uint8_t>& missing, float floorZ,
                     std::vector<Vec3>& out);

// ---------------------------------------------------------------------------------------------- models
constexpr uint32_t kVertexStride = 32;          // POSITION R32G32B32, NORMAL R32G32B32, TEXCOORD0 R32G32

struct PathMesh {
    std::string name;                           // the .gr2 mesh name
    std::vector<uint8_t> vertices;              // vertexCount x 32 bytes, the .gr2 order
    uint32_t vertexCount = 0;
    std::vector<uint16_t> indices;              // the .gr2 indices as 16-bit values
    // lower-case names: t0 BaseTextureMap, t1 SREFMap, t2 IrradianceCubeMap. t1 is empty when the material has no
    // SREFMap (SelectedAniAttack's centre); the renderer then binds a black stand-in with alpha 1 [I].
    std::string texture[3];
    int32_t skeleton = -1, bone = -1;           // the bone the mesh is bound to (index into PathAsset::skeletons)
};

struct PathAsset {
    std::string name;
    std::vector<PathMesh> meshes;               // in the .gr2's model order (the EXE draws them in this order)
    std::vector<c5m::Skeleton> skeletons;
    std::vector<c5m::Mesh> sourceMeshes;        // kept for the skin rows (bone bindings)
    bool animated = false;                      // the .fxsxml names an <Animation> (SelectedAni*)
    std::vector<c5m::BoundClip> clips;          // one per skeleton when animated
    float duration = 0;                         // the clip's duration in seconds (looped)
};

// One .gr2 mesh -> the EXE's 32-byte vertices (Position, Normal, the first two TextureCoordinates0 floats, as stored:
// byte-equal to the captured vertex buffers, path-models.md 2.1), 16-bit indices and the three texture names of its
// material. The checks of PathModels::load for one mesh (Unsupported / BadLayout); `skeleton` and `bone` are not set.
Status repackMesh(const c5m::FileInfo& fi, const c5m::Mesh& m, PathMesh& out);

// Reads one file by its lower-case base name (the game's lookup key): Ok, NotFound, Io.
using FileReader = std::function<Status(const std::string& name, std::vector<uint8_t>& out)>;

class PathModels {
public:
    // Reads the 18 assets through the reader: each .fxsxml, its <Mesh>, its <Animation> if any. Checks: a missing file
    // [NotFound]; malformed XML, an .fxsxml without one <Mesh> [BadLayout]; a .gr2 the reader refuses [BadLayout, with
    // the reader's error]; a mesh without Real32 Position:3, Normal:3 and TextureCoordinates0 (at least 2), more than
    // 65,536 vertices, not exactly one material group, a material whose shader set is not UIShader or that lacks its
    // BaseTextureMap or IrradianceCubeMap, an index out of range [Unsupported]; a mesh that no model binds, or whose first bone binding is no
    // bone of that model's skeleton [BadLayout]; a clip that does not bind [Unsupported]. On failure nothing is kept.
    Status load(const FileReader& read);
    Status load(const gamefs::GameFiles& files);
    bool loaded() const { return !assets_.empty(); }
    const PathAsset& asset(Asset a) const { return assets_[static_cast<size_t>(a)]; }

    // The matrix that moves mesh `mesh` of asset `a` at clip time t (seconds, looped): identity for an asset without
    // animation; else the bone's animated world x its inverse bind (World x InverseWorldTransform, the rigid skin row
    // of a one-bone mesh). Row-major, row vectors. BadLayout for an index out of range.
    Status meshMatrix(Asset a, uint32_t mesh, float t, Mat4& out) const;
private:
    std::vector<PathAsset> assets_;
};

// The lower-case texture names every asset needs (deduplicated, sorted).
std::vector<std::string> textureNames(const PathModels& m);

// ---------------------------------------------------------------------------------------------- draws
struct DrawItem {
    Asset asset = Asset::CenterOneTurn;
    uint32_t mesh = 0;
    uint32_t indexCount = 0;
    Mat4 world;                                 // cb0 rows 0-3: mesh matrix x instance matrix
};
// One draw per mesh of each instance, instances in list order, meshes in model order (path-models.md 3). animTime:
// the SelectedAni clock in seconds (the EXE's +0x5E88 [C]). Checks: models not loaded [NotFound]; an asset out of
// range [BadLayout].
Status buildDraws(const PathModels& m, const std::vector<Instance>& instances, float animTime,
                  std::vector<DrawItem>& out);

// Row-vector product a x b (apply a first).
Mat4 mul(const Mat4& a, const Mat4& b);

// ---------------------------------------------------------------------------------------------- renderer
// UIShader (material 81) at SM5, found in the EXE's .shader section by SHA-256 (3801's capture; the VAs are build
// 4390913's, informational).
struct ProgramDigest { const char* sha256; uint32_t va; };
const ProgramDigest& uiShaderVs();
const ProgramDigest& uiShaderPs();

constexpr uint32_t kCb0Bytes = 256;             // the VS reads rows 0-3 (the world matrix)
constexpr uint32_t kCb1Bytes = 128;             // rows 0-3 the camera's world matrix (row 3 = eye), rows 4-7 view-projection

// cb1 from the camera's world matrix (camera to world; row 3 = the eye) and the view-projection, both row-major row
// vectors. out: 32 floats.
void frameConstants(const Mat4& cameraWorld, const Mat4& viewProj, float out[32]);

class PathRenderer {
public:
    PathRenderer();
    ~PathRenderer();
    PathRenderer(const PathRenderer&) = delete;
    PathRenderer& operator=(const PathRenderer&) = delete;

    // The two programs from the EXE (read-only) by digest; NotFound naming the missing digest; the section's errors.
    Status openPrograms(const std::wstring& exePath);
    // The same from DXBC containers (tests). Every container is hashed; NotFound if a digest is not among them.
    Status openProgramsFromBlobs(const std::vector<std::vector<uint8_t>>& blobs);
    // Creates shaders, input layout, states, samplers, the constant buffers, one immutable VB / IB per mesh and every
    // texture (read through `read`, parsed as DDS). Checks: no device or no programs [Device / NotFound]; a texture
    // missing [NotFound] or not a DDS [the parser's code]; t2 not a cube map [Unsupported]; a D3D11 call failing
    // [Device / OutOfMemory].
    Status create(ID3D11Device* dev, const PathModels& models, const FileReader& read);
    bool ready() const;
    // Draws the items into the render target and depth buffer bound on ctx (the back buffer and the scene's depth, as
    // the EXE's last world-pass call), with cb1 = frameConstants(). Sets its own IA, VS, PS, OM blend and depth states
    // and rasterizer state; leaves them set. Checks: not ready [Device]; an item whose asset or mesh is out of range
    // [BadLayout] (nothing is drawn then).
    Status draw(ID3D11DeviceContext* ctx, const std::vector<DrawItem>& items, const float cb1[32]);
    void release();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// A FileReader over the game's file system (GameFiles::read by base name).
FileReader gameFilesReader(const gamefs::GameFiles& files);

}}  // namespace c5r::pathmodels
