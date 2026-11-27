// c5r/landmarks/inputs.h - the BuildingShader PS inputs the EXE supplies itself, read from the player's install at run
// time (ticket 4882, epic 0561; research/05-graphics-composition/city-renderer.md 11.7 and 11.11, landmark-renderer.md
// 7.4). Nothing of the game is committed (D83): names and rules only.
//
// What the PS reads (the disassembly of PS c704c80b / f0e1f0a6, city-renderer.md 11.11 [C]): the lit colour goes through
// the t6 volume (a 3D colour key, sampled at the gamma-encoded colour) before the fog of war, so a draw with t6 unbound
// is black whatever else is bound; then lerp(t8 cloud base, colour, t7.x), then the t9 volume by t7.x * (1 - t7.y), then
// the distance fog weighted by t5.x. The captures' resources, matched by SHA-256 [C, 4882 2026-11-19]:
//   t5  `fogramp.dds` (128 x 8, 8 mips, R8G8B8A8_UNORM view): the EXE uploads the A8R8G8B8 file as R8G8B8A8 with red and
//       blue swapped (unit-renderer.md 10.3), so .x is the file's blue;
//   t6  the Buildings ColorKey volume (`Buildings_<Style>_ColorKey.tga`, asset-pipeline.md 3.5; equal to the Terrain
//       ones in every install read so far; style Generic here [I which style the EXE binds]);
//   t9  the TerrainFOW ColorKey volume (`TerrainFOW_Generic_ColorKey.tga`, fog-of-war.md 2.7);
//   t2-t4 per draw by the tap's identity (VPModelTextureHash, the BGRA rule): cube maps of the install
//       (landmark-renderer.md 7.6: culture cube lights, `hiconreflmap_*`, `improvements_cubelight`, the resources' and
//       the units' cubes), sampled through sRGB views (the captures' views [C]).
// t7 (the fog-of-war mask) and t8 (the cloud base) are the server's own fog (city-renderer.md 11.7); without it the
// caller binds white() and black() (the terrain's fog-off look: x = 1, no cloud, no tint).
// A missing file keeps the EXE's own fallback: the identity volume for t6 / t9 (ColorKeySystem), no ramp = open fails.
#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "c5r/gamefs/dds.h"
#include "c5r/landmarks/draw.h"
#include "c5r/status.h"

namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace landmarks {

// The names the environment cubes are searched among (lower-case base names): a `.dds` whose name holds one of
// "cube", "refl", "environment", "irradiance" or "light", and that is a cube map.
bool envCubeCandidate(const std::string& lowerName);

class InstallInputs {
public:
    struct Counts {
        uint32_t candidates = 0;   // names that passed envCubeCandidate
        uint32_t cubes = 0;        // of them cube maps with an identity
        uint32_t identities = 0;   // distinct identities (equal files under two names count once)
        uint32_t made = 0;         // cube views made so far
        uint32_t unknown = 0;      // distinct identities asked for that no install cube has
        uint32_t failed = 0;       // cubes whose view could not be made
        bool buildingsFromFile = false, fowFromFile = false;
        double openMs = 0;
    };

    InstallInputs() = default;
    ~InstallInputs() { release(); }
    InstallInputs(const InstallInputs&) = delete;
    InstallInputs& operator=(const InstallInputs&) = delete;

    // CPU part (any thread): the ramp's and the volumes' bytes, the cube index. files is borrowed and must outlive the
    // object. Errors: fogramp.dds missing or not a 2D texture [NotFound / Unsupported]; a ColorKey file that does not
    // parse [buildColorKeyLut's]; a missing ColorKey file is not an error (the identity volume, as the EXE).
    Status open(const gamefs::GameFiles& files);
    // GPU part: the ramp, the two volumes, a 1 x 1 white and a 1 x 1 black texture [Device]; open() first [NotFound].
    Status create(ID3D11Device* dev);
    void release();   // the GPU part (open()'s data is kept: create() may be called again on another device)
    bool opened() const { return opened_; }
    bool created() const { return dev_ != nullptr; }

    ID3D11ShaderResourceView* fogRamp() const { return ramp_; }       // t5
    ID3D11ShaderResourceView* buildingsLut() const { return lut6_; }  // t6
    ID3D11ShaderResourceView* fowLut() const { return lut9_; }        // t9
    ID3D11ShaderResourceView* white() const { return white_; }
    ID3D11ShaderResourceView* black() const { return black_; }
    // The cube of an identity, made at the first ask; null when no install cube has it (counted once in unknown).
    ID3D11ShaderResourceView* env(uint64_t id);
    std::string envName(uint64_t id) const;   // "" when unknown
    // t2-t4 of every item and CPU-built item (4879) of `target` (all targets when ~0u) into in.env, t5 / t6 / t9 into in.frame;
    // in.frame[2] / [3] (t7 / t8) are left as they are.
    void fill(const ModelScene& s, uint32_t target, EnvInput& in);

    // The bytes uploaded (for checks against a capture): the ramp's mips in order, the two volumes (32^3 x 4).
    const std::vector<uint8_t>& rampBytes() const { return rampBytes_; }
    const std::vector<uint8_t>& buildingsBytes() const { return lut6Bytes_; }
    const std::vector<uint8_t>& fowBytes() const { return lut9Bytes_; }
    const Counts& counts() const { return counts_; }

private:
    const gamefs::GameFiles* files_ = nullptr;
    bool opened_ = false;
    std::vector<uint8_t> rampBytes_, lut6Bytes_, lut9Bytes_;
    gamefs::DdsImage rampImg_;                   // its pixels are owned (bytes, converted)
    std::map<uint64_t, std::string> cubes_;      // identity -> the first name holding it
    std::map<uint64_t, ID3D11ShaderResourceView*> made_;
    std::map<uint64_t, bool> unknown_;
    Counts counts_;
    ID3D11Device* dev_ = nullptr;
    std::vector<ID3D11Resource*> resources_;
    ID3D11ShaderResourceView *ramp_ = nullptr, *lut6_ = nullptr, *lut9_ = nullptr, *white_ = nullptr, *black_ = nullptr;
};

// ---------------------------------------------------------------------------------------------- the frame's camera
// Ticket 4882 (city-renderer.md 11.11): under the product's lockstep the tap of Present P is published at P, after the
// composite took the server's frame P, so the server draws a tap one Present old. Its records carry the EXE's camera of
// that Present in VS cb1; drawn as they are, the models would trail the terrain by a frame's pan. applyFrameCamera puts
// the frame's own camera into every VS cb1 the target's records use: rows 0-3 the view-projection (row-major, the VS's
// row-vector product), and when given rows 8-11 the OverlayProj and row 12 FOWAnim (the VS reads 8-11 .xy and 12; VS
// 7ec4e9a5 / c4bb25da / 7a9e17f6, city-renderer.md 5.6). Ticket 0598: when given the eye, row 16 of every PS cb1 the
// target's records use is the main camera's eye (x, y, z, 1) in both passes, the reflection pass included (stock's row
// 16, city-renderer.md 15.2 [C]): the tap's own row 16 while that constant's tapped rows 0-3 are the frame's camera (the
// EXE's exact value; the server's eye differs from it in the last bits, 15.8), else the frame's eye. Rows 17-21 as tapped.
// Ticket 0652 (city-renderer.md 17): on a map that wraps the EXE draws the models it sees through a wrap view in draws of
// their own, whose VS cb1 rows 0-3 are view 0's moved by k x wrapPeriod in world x (k = -1 or +1). Each constant gets the
// wrap copy k of the frame's camera nearest its tapped rows 0-3: rows 0-3 = viewProj moved by k W, rows 8-11 = the
// OverlayProj of copy k (overlayProjWrap, else overlayProj moved by k W), row 16 = the eye moved by -k W. wrapPeriod <= 0
// or not finite: every constant takes copy 0 (as before 0652). Ticket 0659: a constant more than a quarter period from
// every copy (a far camera jump) takes the nearest copy (nearestWrapCopyOf) instead of drawing nothing.
struct FrameCamera {
    const float* viewProj = nullptr;      // 16 floats; null = nothing replaced
    const float* overlayProj = nullptr;   // 16 floats or null
    const float* fowAnim = nullptr;       // 4 floats or null (used only with overlayProj)
    const float* eye = nullptr;           // 3 floats, the main view's eye (PS cb1 row 16), or null = row 16 kept
    uint32_t width = 0, height = 0;       // the target, for the shift in pixels
    float wrapPeriod = 0.0f;              // ticket 0652: world units between two wrap copies (the EXE's 64 W - 1)
    const float* overlayProjWrap[2] = {nullptr, nullptr};   // ticket 0652: copy -1 and +1 (16 floats each) or null
};
// Ticket 0652: the wrap copy k of `now` (rows 0-3 of a row-vector view-projection) nearest the rows `tap`: the difference
// of the translation rows along the largest member of now's row 0, in world units (d), rounded to whole wrap periods;
// -1, 0 or +1 when |d - k W| is at most a quarter period, else kWrapUnfit (a camera step of more than a quarter period
// between the tap and the frame: which EXE view such a record came from cannot be told). Without a period (<= 0 or not
// finite) or a usable row 0: always 0, the rule before 0652. Rows of copy k: rows 0-2 as now, row 3 + k W row 0.
const int kWrapUnfit = 99;
int wrapCopyOf(const Float4* tap, const Float4* now, float wrapPeriod);
// Ticket 0659 (city-renderer.md 17.9): the copy for a constant wrapCopyOf finds unfit (a far camera jump between the tap
// and the frame: a minimap click, a jump by a notification or LookAt): the nearest copy k = round(d / W), -2 to +2, whatever
// |d - k W|. Every copy is a true image of the wrapped world, so a record drawn there is never misplaced; it is on screen
// only where the old and the new view overlap. kWrapUnfit when |k| > 2 (the tapped rows are no camera of this map: a
// fault) and 0 without a period or a usable row 0 (as wrapCopyOf).
int nearestWrapCopyOf(const Float4* tap, const Float4* now, float wrapPeriod);
// The screen shift of the instance origins (materials 3-5; row 3 of each instance's rows, material 3's cb0 rows) between
// the tap's own camera and the frame's: what drawing the tap at its own camera would have misplaced.
struct CameraShift {
    uint32_t consts = 0;   // constants replaced
    uint32_t eyeConsts = 0;   // ticket 0598: PS cb1 constants whose row 16 was given the frame's eye
    uint32_t eyeKept = 0;     // ticket 0598: ... whose row 16 is the tap's own (the tapped camera is the frame's)
    uint32_t wrapConsts = 0;  // ticket 0652: VS cb1 constants given a wrap copy (k = -1 or +1) of the frame's camera
    // ticket 0652, narrowed by 0659: VS cb1 constants whose tapped rows are no wrap copy of the frame's camera at all
    // (nearestWrapCopyOf gives kWrapUnfit): rows 0-3 written all zero, so their records draw nothing this frame (clip w = 0
    // everywhere); a fault, the caller names them LOUD
    uint32_t unfitConsts = 0;
    // ticket 0659: VS cb1 constants more than a quarter period from every copy (a far camera jump between the tap and the
    // frame, no fault): drawn at the nearest copy (nearestWrapCopyOf); their instances, and of those the origins inside
    // the frame (|x|, |y| <= 1 in NDC, w > 0) at that copy and at view 0 (the rule before 0652)
    uint32_t farConsts = 0;
    uint32_t farInstances = 0, farOnScreen = 0, farOnScreenView0 = 0;
    uint32_t n = 0;        // origins projected by both
    double sumPx = 0, maxPx = 0;
};
// Per constant index: the tap's rows 0-3, and [4] its row 16 (ticket 0598; all zero when the constant has no row 16).
typedef std::map<uint32_t, std::array<Float4, 5>> TapCameraRows;
// The tap's rows 0-3 and 16 of every constant an item uses as VS cb1 (call after resolve, before the first applyFrameCamera).
void keepTapCamera(const ModelScene& s, TapCameraRows& out);
// Replaces the rows of the constants the items and CPU-built items of `target` use as VS cb1 (each once), and with an eye
// row 16 of those they use as PS cb1 (each once); shift may be null.
void applyFrameCamera(ModelScene& s, uint32_t target, const FrameCamera& cam, const TapCameraRows& tap, CameraShift* shift);

}}  // namespace c5r::landmarks
