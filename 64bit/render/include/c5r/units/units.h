// c5r/units/units.h - the renderer's unit module: one frame's unit list (unit_scene.h) to the EXE's own draw inputs
// and the draws of the three passes on the module's own D3D11 device (epic 0041, U1-U3; owner of this header: issue
// 4352). Design: research/05-graphics-composition/unit-renderer.md sections 2, 5-7; results in its section 10.
//
// Stages per unit: L0 art (art.h), L1 models and draw inputs (draw.h), L2 pose (pose.h, 4353), L3 placement
// (placement.h, 4354). prepare() runs them on the CPU and fills a FramePlan; draw() uploads the frame's inputs and
// issues the draws; precheck() runs L0, L1 and the clip binding for every unit type and every art row the game can
// reach, without drawing. The module reads no EXE memory and assumes nothing of the EXE's unit system (D144, D147).
//
// Failure behaviour: every unit gets one UnitStatus (status.h); a unit that is not Ok is not drawn at all and the
// frame's other units are. prepare() and draw() return a frame-level Status only for what stops the whole frame
// (the renderer not opened, a device call failing).
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "c5r/status.h"
#include "c5r/units/art.h"
#include "c5r/units/draw.h"
#include "c5r/units/placement.h"
#include "c5r/units/status.h"
#include "c5r/units/unit_scene.h"

namespace c5m { class TableSource; }
namespace c5r { namespace gamefs { class GameFiles; } namespace heightfield { struct HeightGrid; } }

namespace c5r { namespace units {

// ---------------------------------------------------------------------------------------------- the pose step (L2)
// What the pose step gets per figure and mesh. The default source is the module's (pose.h, 4353); a caller may set
// its own (the oracle binds captured bone rows; tests use fixed ones).
struct PoseRequest {
    const UnitInput* unit = nullptr;
    const MemberArt* member = nullptr;
    const UnitModel* model = nullptr;
    const ModelMesh* mesh = nullptr;
    uint32_t figure = 0;               // index in member order (unit-renderer.md 6.3)
    std::string clip;                  // "" = the member's default clip (6.2)
    float t = 0;
    float scale = 0;                   // the member's Scale
    float yaw = 0;                     // radians, from placement
    float T[3] = {0, 0, 0};            // the figure's model origin, from placement
};
class PoseSource {
public:
    virtual ~PoseSource() = default;
    // The mesh's 32 bones x 3 rows of [Rz(yaw) R_k | s Rz(yaw) t_k + T] (rows of bones the mesh does not bind:
    // identity). Codes: ClipNotFound, CurveUnsupported, BadModel.
    virtual UnitStatus rows(const PoseRequest& req, Float4 out[kPaletteBones * 3]) = 0;
    // For the pre-check: every clip the member names binds to the model (ClipNotFound, CurveUnsupported, BadModel).
    // Default: Ok (the module's source overrides it with pose.h's bindAll).
    virtual UnitStatus checkClips(const MemberArt& member, const UnitModel& model) {
        (void)member;
        (void)model;
        return UnitStatus::Ok();
    }
};

// ---------------------------------------------------------------------------------------------- the placement step (L3)
// Where each living figure stands (placement.h, 4354: FormationTable + placeUnit). Any failure: PlacementFailed.
struct PlacementRequest {
    const UnitInput* unit = nullptr;
    const UnitArt* art = nullptr;
    const heightfield::HeightGrid* grid = nullptr;   // may be null: the source decides (placeUnit needs it)
    float water_level = 0;
};
class PlacementSource {
public:
    virtual ~PlacementSource() = default;
    virtual UnitStatus place(const PlacementRequest& req, UnitPlacement& out) = 0;
};

// The module's own steps (draw_steps.cpp): the pose is pose.h's ModulePose (4353) with bindAll as its clip check;
// the placement is placement.h's placeUnit (4354) over UnitFormations.xml (loose file of installRoot first) with the
// art row's Formation and the members' ZOffset; a missing height field or any failed check is PlacementFailed.
std::unique_ptr<PoseSource> makeModulePose(c5m::FileProvider& files, const c5m::StringDatabase* strings);
std::unique_ptr<PlacementSource> makeModulePlacement(const gamefs::GameFiles& files, const std::wstring& installRoot,
                                                     Status* st);

// ---------------------------------------------------------------------------------------------- the frame plan
struct MeshDraw {
    const UnitModel* model = nullptr;
    const ModelMesh* mesh = nullptr;
    Material material = Material::Skinned;
    uint16_t member = 0;               // index into the unit's UnitArt::members
    // 89 / 90: one instanced draw; blocks[i] = the frame-wide block of instance i (living figures in member order).
    std::vector<uint32_t> blocks;
    // 87: one draw per living figure, cb0 per draw (16 rows each, kStaticCb0Bytes).
    std::vector<Float4> cb0;
    uint32_t instances() const;        // 89 / 90: blocks.size(); 87: cb0.size() / 16
};

struct UnitPlan {
    int32_t player = -1, id = -1;
    UnitStatus status;
    // resolved art (L0), shared with the renderer's row cache (issue 4464: no copy per frame): the art row's, so
    // art->unit_type is "" (the unit's type is the scene's UnitInput). Null when the unit's art row was not reached.
    std::shared_ptr<const UnitArt> art;
    UnitPlacement placement;           // L3
    uint32_t first_block = 0, blocks = 0;
    std::vector<MeshDraw> draws;       // empty unless status Ok
};

struct FramePlan {
    std::vector<UnitPlan> units;       // the scene's order
    std::vector<Float4> palette;       // blocks x 98 rows, block b at 98 b (buffer b / 167, id b % 167)
    uint32_t blocks = 0;
    float water_level = 0;
    void clear();
    size_t drawn() const;              // units with status Ok
};

// The per-instance id stream of a frame: one region per pass in the pass order, each draw's ids (block % 167) at its
// offset; material 87 draws take no ids (the EXE binds the stream but does not advance it, unit-renderer.md 10.4).
struct IdStream {
    std::vector<uint32_t> ids;
    // offsets[p][k] = byte offset of the k-th instanced draw of pass p (frame order: units, then their draws).
    std::vector<std::vector<uint32_t>> offsets;
};
void buildIdStream(const FramePlan& plan, size_t passes, IdStream& out);

// ---------------------------------------------------------------------------------------------- per-type statuses
struct UnitTypeStatus {
    std::string unit_type;             // Units.Type
    std::string art_style;             // the style suffix the row was reached with ("" = none)
    std::string era;                   // the Eras.Type the row was reached with ("" = none)
    std::string art_row;
    UnitStatus status;
};

// ---------------------------------------------------------------------------------------------- the renderer
class UnitRenderer {
public:
    UnitRenderer();
    ~UnitRenderer();
    UnitRenderer(const UnitRenderer&) = delete;
    UnitRenderer& operator=(const UnitRenderer&) = delete;

    // dev may be null (CPU only: prepare and precheck). files: the install with the active mods as extraLooseRoots;
    // db: the game database with the mods applied (read only); exe: CivilizationV_DX11.exe of that install (the unit
    // programs are read from it). Errors: the database's tables (BadLayout), the environment textures (NotFound),
    // UnitFormations.xml (NotFound), the device.
    Status open(ID3D11Device* dev, const gamefs::GameFiles& files, c5m::TableSource& db, const std::wstring& exe,
                const std::wstring& installRoot = std::wstring());   // "" = the EXE's folder (UnitFormations.xml)

    // L0-L3 for every unit of the scene, CPU only. hf: the height field (null: placement fails for every unit).
    Status prepare(const UnitScene& scene, const heightfield::HeightGrid* hf, FramePlan& out);

    // Uploads the plan's palette, ids and models and issues the draws of every pass in `passes` (the module's own
    // state objects as unit-draws.md 1). Restores nothing of the context's state.
    Status draw(ID3D11DeviceContext* ctx, const FramePlan& plan, const PassSet& passes);

    // Every Units.Type x every art style suffix x every era of the database: the art row each reaches, and L0, L1
    // and the clip binding of its members. One entry per distinct (unit type, art row); `out` sorted by unit type.
    Status precheck(std::vector<UnitTypeStatus>& out);

    // The pose and placement sources (null = the module's own steps). Not owned.
    void setPoseSource(PoseSource* p) { pose_ = p; }
    // 0441 (additive): the pose source in use (the one set, else the module's own; null before open()).
    PoseSource* poseSource() const { return pose_ ? pose_ : ownPose_.get(); }
    void setPlacementSource(PlacementSource* p) { place_ = p; }

    ArtResolver& art() { return art_; }
    ModelCache* models() { return models_.get(); }
    UnitPrograms& programs() { return programs_; }
    bool isOpen() const { return open_; }

    // One unit through L0 and L1 (no pose, no placement): the art and every model. Used by prepare and precheck.
    UnitStatus loadUnit(const UnitInput& u, const PlayerInput* owner, UnitArt& art,
                        std::vector<const UnitModel*>& models);

    // Issue 4464: the art cache per art row. L0 and L1 of a unit depend only on its art row (or on its unit type, art
    // style and era, which give the row) and on what open() loaded (the tables, the files, the models, the programs),
    // so each row is loaded once after an open(): the art (ArtResolver's cache: each member's .fxsxml parsed once),
    // the models and the programs' check, with the same status (a failure included) for every later unit and frame.
    // open() drops it. Off (tests and the cost indication's "before" arm): every unit of every frame through the rule
    // again, the members' .fxsxml read and parsed each time, as before 4464. Default on.
    void setArtCache(bool on) { artCache_ = on; }
    bool artCache() const { return artCache_; }
    size_t cachedRows() const { return rows_.size(); }

private:
    struct LoadedRow {
        UnitStatus status;                        // loadUnit's status for this row (warnings included)
        std::shared_ptr<const UnitArt> art;       // never null once built
        std::vector<const UnitModel*> models;     // per member, as far as they loaded
    };
    UnitStatus rowOf(const UnitInput& u, const PlayerInput* owner, std::string& row);
    void buildRow(const std::string& row, bool cachedArt, LoadedRow& out);
    const LoadedRow& loadRow(const std::string& row, LoadedRow& scratch);   // cached, or `scratch` when the cache is off
    bool artCache_ = true;
    std::map<std::string, LoadedRow> rows_;                               // art row -> its L0 and L1
    std::map<std::string, std::pair<UnitStatus, std::string>> rowOf_;    // type \n style \n era -> status, art row
    struct Gpu;
    Status ensureBuffers(uint32_t paletteBuffers, size_t idBytes);
    bool open_ = false;
    ID3D11Device* dev_ = nullptr;
    std::unique_ptr<GameFilesProvider> provider_;
    ArtResolver art_;
    std::unique_ptr<c5m::StringDatabase> strings_;
    std::unique_ptr<ModelCache> models_;
    UnitPrograms programs_;
    PoseSource* pose_ = nullptr;
    PlacementSource* place_ = nullptr;
    std::unique_ptr<PoseSource> ownPose_;
    std::unique_ptr<PlacementSource> ownPlace_;
    std::unique_ptr<Gpu> gpu_;
    const UnitTexture* env_[4] = {};   // t2-t5 from the install (P1, unit-renderer.md 10.3)
    Status programStatus_;              // the programs' read (a failure makes units ProgramNotFound, not open())
};

}}  // namespace c5r::units
