// c5r/landmarks/draw.h - the draw layer of the BuildingShader models (ticket 4873, epic 0561; research/05-graphics-
// composition/city-renderer.md 2.2 and 3.1, landmark-renderer.md 7): the EXE's four colour-pass programs (materials 3,
// 4, 5 and 6, pass 0) read from the player's EXE by digest at run time, the input layouts of the three vertex forms,
// the captures' states and samplers, the library's buffers and textures, and the draws of a ModelScene.
//
// Nothing of the game is committed (D83): the program table is shell_model_scene.h's (SHA-256 digests only), the
// programs, meshes and textures are read from the player's install at run time.
//
// What the server supplies (ViewInput / EnvInput): the targets, the PS environment cubes t2-t4 by their identity (the
// tap carries it per draw), and the frame textures t5-t9 (the fog-of-war and lighting inputs). The offline harness
// (render/tools/cityscene) gives the capture's own; the server's own are 4875's (city-renderer.md 2.3, fog).
#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c5r/landmarks/cpumesh.h"
#include "c5r/landmarks/library.h"
#include "c5r/landmarks/tap.h"
#include "c5r/status.h"

namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace landmarks {

enum class DrawPass : uint8_t { Scene = 0, Reflection = 1 };

// ---------------------------------------------------------------------------------------------- programs
// The five pass-0 programs of shell_model_scene.h (kVPModelPrograms[0..4]) found in the EXE's .shader section by their
// SHA-256 digests (lookup by digest only; the VA is informational), and the four DROP pairs.
class BuildingPrograms {
public:
    Status open(const std::wstring& exePath);
    // The same from a list of DXBC containers (tests). Errors: a digest not present [NotFound] naming it.
    Status openFromBlobs(const std::vector<std::vector<uint8_t>>& blobs);
    // Creates the shaders and the four input layouts on dev [Device].
    Status create(ID3D11Device* dev);
    void release();
    ~BuildingPrograms() { release(); }
    const std::vector<uint8_t>* vsBytes(Material m) const;
    const std::vector<uint8_t>* psBytes(Material m) const;
    ID3D11VertexShader* vs(Material m) const;
    ID3D11PixelShader* ps(Material m) const;
    ID3D11InputLayout* layout(Material m) const;
    bool opened() const { return bytes_.size() == 5; }
private:
    std::map<int, std::vector<uint8_t>> bytes_;   // by kVPModelPrograms index
    std::map<int, ID3D11DeviceChild*> objects_;
    std::map<int, ID3D11InputLayout*> layouts_;   // by material
};

// The input elements of a material's vertex forms (landmark-renderer.md 2.1; the captures' layouts [C]): slot 0 the
// file's vertex (32, 20 or 28 B); slot 1 per instance four R32G32B32A32 rows TEXCOORD1-3, COLOR0 (3: none; 6: an
// R32_UINT BLENDINDICES1).
std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements(Material m);

// ---------------------------------------------------------------------------------------------- states
// The pipeline states of a (material, pass) as the 3907/3910 captures show them (landmark-renderer.md 7.3 [C]).
struct DrawStates {
    D3D11_BLEND_DESC blend = {};
    D3D11_DEPTH_STENCIL_DESC depth = {};
    D3D11_RASTERIZER_DESC raster = {};
};
DrawStates captureStates(Material m, DrawPass p);
// Samplers s0-s9 of PS f0e1f0a6 / c704c80b: linear, wrap; s6 and s9 clamp (the captures [C]).
D3D11_SAMPLER_DESC captureSampler(uint32_t slot);
constexpr uint32_t kPsSamplers = 10;

// ---------------------------------------------------------------------------------------------- inputs per draw
struct ViewInput {
    DrawPass pass = DrawPass::Scene;
    uint32_t target = 0;                       // the scene's target index whose items are drawn
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT viewport = {};
    // Optional (the harness): a second target of R32_UINT that receives 1 + the item index of every pixel an item
    // wrote last, drawn after the colour pass with depth equal (the per-object compare).
    ID3D11RenderTargetView* idRtv = nullptr;
};

// The PS's environment and frame textures: t2-t4 by identity (TapDraw::env), t5-t9 per frame. A draw whose env
// identity is not in `env` is drawn with that slot unbound and counted (DrawReport::envMissing).
struct EnvInput {
    std::map<uint64_t, ID3D11ShaderResourceView*> env;
    ID3D11ShaderResourceView* frame[5] = {};   // t5 .. t9
};

struct DrawReport {
    uint32_t items = 0, drawn = 0, skipped = 0;   // skipped: another target, no programs, buffers or textures
    uint32_t envMissing = 0, textureMissing = 0, bufferFailed = 0;
    uint32_t paletteSplits = 0;                   // skinned draws split into several calls (t27 holds 16,384 rows)
    std::vector<std::string> problems;            // the first 32
    // Ticket 4884: the CPU cost of the colour pass (QPC, ms; the id pass not counted): the frame's row and constant
    // buffers on the device (upload) and the item loop's state, binds and draws (items), and the context calls the
    // item loop issued (state setters, binds, Map / Unmap, draws).
    double uploadMs = 0, itemsMs = 0;
    uint32_t contextCalls = 0;
};

// ---------------------------------------------------------------------------------------------- the renderer
class ModelRenderer {
public:
    ModelRenderer() = default;
    ~ModelRenderer() { release(); }
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;
    // programs must be opened; files: the install the library's textures are read from. Creates the programs, the
    // states of captureStates, the samplers and the id program [Device]. programs, lib and files are borrowed: they
    // must outlive the renderer's use of them (the renderer never releases the programs).
    Status init(ID3D11Device* dev, BuildingPrograms& programs, ModelLibrary& lib, const gamefs::GameFiles& files);
    // Draws the scene's items of view.target in their order. Checks: not initialised [Device]; a view without an rtv
    // [BadLayout]; a Map or create failing [Device]. Per item problems are counted in the report, not errors.
    Status draw(ID3D11DeviceContext* ctx, const ModelScene& scene, const ViewInput& view, const EnvInput& env,
                DrawReport* report);
    // Makes a library file drawable: its GPU buffers and every part's t0 / t1 texture (idempotent), so the server
    // publishes only keys it can draw (city-renderer.md 3.2; 4875 #6160). Checks: not initialised [Device]; file out of
    // range [BadLayout]; the file not ok or its buffers failing [their code]; a part's named texture not in the install
    // or not creatable [NotFound]. A failed file stays not prepared (and is not retried for its buffers).
    Status prepare(uint32_t file);
    bool prepared(uint32_t file) const { return prepared_.count(file) != 0; }
    // Ticket 4879: the CPU-built meshes' store the scene's cpuItems are drawn from (borrowed; null: cpuItems are skipped).
    // Their GPU buffers are made at first draw and released when the store no longer holds them.
    void setCpuMeshes(const CpuMeshStore* store) { cpuStore_ = store; }
    // Makes the t0 / t1 of a CPU-built draw drawable (as prepare does for a file's parts): false when a named texture is
    // not in the install or not creatable.
    bool prepareTextures(const std::string& t0, const std::string& t1);
    void release();
    // Texture uploads and GPU buffers made so far (an indication).
    uint32_t textures() const { return static_cast<uint32_t>(textures_.size()); }
    uint64_t gpuBytes() const { return gpuBytes_; }
private:
    struct FileGpu { ID3D11Buffer* vb = nullptr; ID3D11Buffer* skinVb = nullptr; ID3D11Buffer* ib = nullptr; bool failed = false; };
    struct Tex { ID3D11Resource* res = nullptr; ID3D11ShaderResourceView* srv = nullptr; };
    Status fileBuffers(uint32_t file, FileGpu** out);
    ID3D11ShaderResourceView* texture(const std::string& name);
    Status drawItems(ID3D11DeviceContext* ctx, const ModelScene& scene, const ViewInput& view, const EnvInput& env,
                     bool ids, DrawReport& rep);

    ID3D11Device* dev_ = nullptr;
    BuildingPrograms* programs_ = nullptr;
    ModelLibrary* lib_ = nullptr;
    const gamefs::GameFiles* files_ = nullptr;
    std::map<int, ID3D11BlendState*> blend_;
    std::map<int, ID3D11DepthStencilState*> depth_;
    std::map<int, ID3D11RasterizerState*> raster_;
    ID3D11DepthStencilState* idDepth_ = nullptr;
    ID3D11BlendState* idBlend_ = nullptr;
    ID3D11SamplerState* samplers_[kPsSamplers] = {};
    std::map<int, ID3D11PixelShader*> idPs_;   // per material: matches its VS's output signature
    ID3D11Buffer* cb0_ = nullptr;              // material 3's per-draw world rows (256 B, dynamic)
    ID3D11Buffer* idCb_ = nullptr;             // the id program's item index (16 B, dynamic)
    ID3D11Buffer* ids_ = nullptr;              // 0 .. 4095 R32_UINT (the skinned draws' slot 1)
    std::map<uint32_t, FileGpu> gpu_;
    std::map<uint32_t, bool> prepared_;
    std::map<std::string, Tex> textures_;
    uint64_t gpuBytes_ = 0;
    // ticket 4879: the CPU-built meshes
    ID3D11Buffer* cpuBuffer(GeomKind kind, uint64_t hash);
    void pruneCpu();
    // ticket 4884: the pipeline state a pass has bound so far (draw.cpp): a setter is called only when its value changes
    struct Bound;
    Status drawCpuItems(ID3D11DeviceContext* ctx, const ModelScene& scene, const ViewInput& view, const EnvInput& env,
                        bool ids, DrawReport& rep, Bound& b);
    // ticket 4884: the frame's row block and constants written into buffers kept from pass to pass (dynamic, written
    // with WRITE_DISCARD) instead of new immutable buffers per pass; each constant buffer has its constant's exact size
    Status uploadFrame(ID3D11DeviceContext* ctx, const ModelScene& scene);
    ID3D11Buffer* rowsBuf_ = nullptr;
    size_t rowsCap_ = 0;
    ID3D11Buffer* curRows_ = nullptr;          // the pass's row block (null: the scene has no rows)
    std::vector<ID3D11Buffer*> constBufs_;     // by the scene's constant index
    std::vector<uint32_t> constBytes_;
    // ticket 4884: per file the buffers, per file and part the t0 / t1 views (texture() looked up once per part, not
    // per draw: a name's view never changes once made, a failed one stays null as texture() keeps it)
    std::vector<FileGpu*> fileGpu_;
    std::vector<std::vector<std::array<ID3D11ShaderResourceView*, 2>>> partSrv_;
    const std::array<ID3D11ShaderResourceView*, 2>& partTextures(uint32_t file, const ModelFile& mf, uint32_t part);
    const CpuMeshStore* cpuStore_ = nullptr;
    uint64_t cpuGen_ = 0;
    std::map<std::pair<uint8_t, uint64_t>, ID3D11Buffer*> cpuGpu_;
};

}}  // namespace c5r::landmarks
