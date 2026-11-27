// srv_water_scene.h - the water of the renderer inside server.exe (epic 3600, issue 3607).
// Written 2026-11-14 for this repository (GPL-3.0-or-later).
//
// What it does. It owns the water tap's reader (srv_water_tap.h, issue 3603) and the water module (c5r::water,
// render/include/c5r/water/water.h, issues 3604-3606) and decides, per frame, whether the module draws the water or the
// terrain module's flat placeholder plane stays (c5r::terrain::Options::water):
//   - mode kFull (render_water=1, the default): the module draws when it is initialised, has the height field, and the
//     tap delivered an image of the load the camera shows. Else the placeholder, with the reason logged once per load
//     (never LOUD: a missing tap or a module that is not there yet is a fallback, not a fault).
//   - kPlaceholder (render_water=placeholder): the flat plane only (the pre-3600 picture), the tap still read and acked.
//   - kOff (render_water=0): no water at all.
// Draw order and depth (water-system.md 6.3 and 13, 3601's capture [L]): after the terrain, into the terrain's colour
// and depth targets, without clearing depth; the module sets each pass's depth state (ocean tests, river writes).
// The water clock (decision 4 (ii), MC #447): the server's own. Under D33 the EXE's water time stands still (it advances
// only while the EXE's water draw runs, 3601 [L]), so the server advances the two clock fields of the tapped Water object
// itself at the EXE's measured rates: Water+0xDC by 4 x Water+0xD8 per second (0.721/s at the default speed 0.18) and
// Water+0xE4 by Water+0xE0 per second (0.03/s), from the tapped values. Phase equality with stock is by chance.
// `freeze` (render_water_freeze=1, diagnostics) holds both at the tapped values, as P12's data write freezes stock.
// The reflection input (decision 3 (ii)): stock's "Reflection Texture Raw" holds only models and the polar ice on
// (0,0,0,0) (3601 [L]); the server has neither, so the scene binds t4 = a 1024 x 1024 RGBA8 target cleared to
// (0,0,0,0), stock's empty reflection at stock's size (the ocean PS reads t4 only in an additive term and the alpha
// factor, both 0 there: water-system.md 13).
// The reflected ice (issue 0382, water-system.md 13.6, polar-ice.md 13): t4 is now a render target (1024 x 1024 RGBA8
// with a D24S8 depth) that the scene clears to (0,0,0,0) / depth 1 every drawn frame, as stock's reflection pass
// 0x5A0F80 does, and into which the reflection hook (WaterSceneReflection, set by the terrain scene when it has an ice
// mesh) draws the polar ice with the EXE's reflection camera (c5r::water::reflectionCamera of view 0); WaterFrame 0x160
// carries that camera's view-projection, so the ocean and river PSs read the ice where stock does. Without a hook (no
// ice, render_ice=0, or render_ice_reflection=0) the target stays (0,0,0,0): the picture before 0382, bit for bit.
// Stock's reflection also holds the models; the server has none until increment 3.
//
// The coast waves (issue 3609, decision 2 (i)): the tap's VPWSYS_WAVE records (the Wave_System object, its chunk array,
// the wave array, one VB / IB / draw per chunk; water-system.md 9.4, 12.5) go to c5r::water::WaveRenderer (3606), drawn
// after the ocean and the rivers with the ocean's views, additive, into the same targets (water-system.md 12.1, 10.7).
// The wave update (c5r::water::updateWaves, the EXE's 0x6AB850 with its seed restarted every call) runs once per drawn
// frame with dt from the server's clock (decision 4 (ii)); `freeze` holds the waves at the tapped ages. A wave part that
// does not parse leaves the ocean drawn and logs its reason once per image (not LOUD).
//
// Every call is on the render thread. The module sits behind WaterModule so that the stand-in tests
// (server/tests/render/water/water_scene_test.cpp) run the scene's decisions without a GPU.
#pragma once
#include "srv_render_common.h"
#include "srv_water_tap.h"
#include "c5r/water/water.h"
#include "c5r/water/waves.h"
#include "c5r/heightfield/heightfield.h"
#include <memory>
#include <string>

namespace c5r { namespace gamefs { class GameFiles; } }

namespace srvrender {

// The scene's view of c5r::water::Renderer (MakeC5rWaterModule wraps the real one).
struct WaterModule
{
	virtual ~WaterModule() {}
	virtual c5r::Status init(ID3D11Device* dev, const std::wstring& exe, const std::wstring& gameDir) = 0;
	// Issue 0198: an index of gameDir for the next init only (null after it); the default ignores it.
	virtual void shareFiles(const c5r::gamefs::GameFiles*) {}
	virtual c5r::Status setHeightField(const c5r::heightfield::HeightGrid& g) = 0;
	// Epic 0465, ticket 4909: the same field edited inside rects (c5r::water::Renderer::updateHeightRegion). The default:
	// Unsupported (WaterScene::onFieldRegion then sets the whole field).
	virtual c5r::Status updateHeightRegion(const c5r::heightfield::HeightGrid&, const std::vector<c5r::terrain::SampleRect>&,
	                                       const std::vector<c5r::bake::HeightPage>*, c5r::terrain::RegionUpdateStats*)
	{
		return c5r::Status::fail(c5r::Err::Unsupported, "no region update");
	}
	virtual c5r::Status setMesh(c5r::water::System s, const c5r::water::Mesh& m) = 0;
	virtual c5r::Status setTexture(c5r::water::Slot slot, ID3D11ShaderResourceView* srv) = 0;
	virtual c5r::Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
	                         const c5r::water::FrameInputs& in, const c5r::water::Options& o, c5r::water::FrameStats* st) = 0;
	virtual void release() = 0;
	// The coast waves (issue 3609). The defaults: no wave renderer (the stand-ins that do not test waves).
	virtual bool wavesAvailable() const { return false; }
	virtual std::string wavesWhy() const { return "no wave renderer"; }
	virtual c5r::Status setWaveChunks(const std::vector<c5r::water::WaveChunk>&)
	{
		return c5r::Status::fail(c5r::Err::NotFound, "no wave renderer");
	}
	// After draw(): every chunk with each view of that draw (the ocean's ViewProj per view), into rtv / dsv.
	virtual c5r::Status drawWaves(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, const c5r::water::WaveObject&,
	                              float /*planeZ*/, const std::vector<c5r::water::WaveRecord>&, c5r::water::WaveStats*)
	{
		return c5r::Status::fail(c5r::Err::NotFound, "no wave renderer");
	}
};
std::unique_ptr<WaterModule> MakeC5rWaterModule();

// The tap's VPWSYS_WAVE records -> the wave object, the chunks (k-th wave draw record = chunk k of the chunk array) and
// the wave array as tapped. NotFound: no Wave_System object (an image without waves). Ok with no chunks: the object
// says 0 chunks. BadLayout: the object fails parseWaveObject; the chunk array or the wave array missing or of another
// size than the object's counts; the number of draws not the chunk count; a draw whose VB is not 12-byte vertices or
// whose IB is not R16 or holds fewer indices than the draw; a chunk's waves past the array.
c5r::Status ParseWaveRecords(const std::vector<WaterTapRecordCopy>& recs, c5r::water::WaveObject& wo,
                             std::vector<c5r::water::WaveChunk>& chunks, std::vector<c5r::water::WaveRecord>& waves);

struct WaterSceneOptions
{
	enum Mode { kOff = 0, kFull = 1, kPlaceholder = 2 };
	int mode = kFull;
	bool freeze = false;             // diagnostics: the water clock held at the tapped values
	std::wstring exe, gameDir;       // the EXE (shaders) and the install (art), read-only
	// issue 0198: an index of gameDir the caller has opened, read by the waves during init only (WaterScene::init does
	// not keep it); null: the module opens its own.
	const c5r::gamefs::GameFiles* files = nullptr;
};

// Epic 0301, issue 3903 (research/06-tooling-and-workflows/offline-scene-harness.md 6, 2.7): the water clock's state,
// what a frame's picture depends on besides the image and that frame's nowMs. The coast waves integrate every drawn
// frame's dt, so a frame is redrawn exactly from this state at the start of a span plus every frame's nowMs in turn.
// (Declared in srv_scene_sources.h by the lead's contract header; here because the water tests build without server/src.)
struct WaterClockState
{
	double clockStartMs = -1;                            // m_clockStartMs (-1: no frame drawn with the image yet)
	double waveLastMs = -1;                              // m_waveLastMs (-1: no wave update yet)
	double lastNowMs = -1;                               // m_lastNowMs (the last decide()'s nowMs)
	std::vector<c5r::water::WaveRecord> waves;           // the coast waves (16 B each); empty without waves
};

// Why the placeholder (or nothing) is drawn instead of the module's water.
enum WaterWhy
{
	WW_MODULE = 0,      // the module draws
	WW_OFF,             // render_water=0
	WW_PLACEHOLDER,     // render_water=placeholder
	WW_NO_MODULE,       // the module failed to initialise (or is the stub)
	WW_NO_FIELD,        // no height field yet
	WW_NO_TAP,          // no image accepted yet (tap off, not READY yet, or every copy refused)
	WW_OTHER_LOAD,      // the accepted image belongs to another load than the camera's
	WW_NO_FIELDS,       // the accepted image had no usable Water object (its fields feed WaterFrame and the clock)
	WW_COUNT
};
const char* WaterWhyName(int w);

// Issue 0382: what draws into the reflection target. Called by WaterScene::draw before the ocean, once per drawn frame,
// with the target (already cleared), its depth, the reflection camera (view 0xDA1990, proj 0xDA1A90, 1024 x 1024; camWorld
// zero) and the scene camera's z (the ice's fog row 29.y takes it, polar-ice.md 7). *draws: the draw calls made.
typedef c5r::Status (*WaterReflectionFn)(void* user, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                                         ID3D11DepthStencilView* dsv, const c5r::terrain::Camera& refl, float sceneCamZ,
                                         uint32_t* draws);
class WaterScene;
// The hook of the next draws of `w` (null: none; the target then stays (0,0,0,0)). Set by the terrain scene each frame.
void WaterSceneReflection(const WaterScene* w, WaterReflectionFn fn, void* user);
// render_ice_reflection (server.cfg, srv_render_host.cpp): false = no hook is called (the picture before 0382).
// Process-wide, set once by the host before the scene starts; default true.
// Epic 0375 (unit-server-scene.md 7, decision 7 (c); merged with 0382 on 2026-11-16): the models' hook of the next draws
// of `w`, called in the same pass before the ice hook (stock draws the model systems first) with the same target, camera
// and z, and not gated by render_ice_reflection. Set by the terrain scene each frame while render_units_passes=1. A
// failure is LOUD once per new text, counted in reflErrors, the target cleared and the ice not drawn that frame. A frame
// whose hooks all succeeded counts in reflFrames, also with the models' hook alone (issue 4464); w_refl_draws = the
// models' draws (the units give 0) + the ice's.
void WaterSceneReflectionModels(const WaterScene* w, WaterReflectionFn fn, void* user);
void WaterSceneSetReflectIce(bool on);
bool WaterSceneReflectIce();

class WaterScene
{
public:
	explicit WaterScene(std::unique_ptr<WaterModule> m = nullptr);
	~WaterScene();
	void attach(VPShellHeader* hdr);
	// The module on this device. A failure is logged once (not LOUD) and leaves the placeholder.
	void init(ID3D11Device* dev, const WaterSceneOptions& o);
	void releaseDevice();
	// Every loop iteration: the tap block (reads and acks a READY copy).
	void poll();
	// The height field of a load (after the terrain accepted it). The grid is the caller's kept copy and must outlive the
	// scene's use of it (srv_render_scene.cpp keeps it for device rebuilds); the module builds its pages from it now.
	void onField(const c5r::heightfield::HeightGrid& g, uint32_t loadSeq);
	// Epic 0465, ticket 4909: a live terrain edit of the same field (g, the kept copy onField was given) inside rects:
	// only the pages the rects reach (built: from the live-terrain worker, may be null). A module without the region
	// update, or one that refuses it, takes the whole field (onField's call) and the line says so. ms: the time spent.
	void onFieldRegion(const c5r::heightfield::HeightGrid& g, const std::vector<c5r::terrain::SampleRect>& rects,
	                   const std::vector<c5r::bake::HeightPage>* built, c5r::terrain::RegionUpdateStats* stats);
	// Per frame, before the terrain draw: the decision for this camera. True = the module draws (placeholder off).
	bool decide(const VPShellCamera& cam, double nowMs);
	int why() const { return m_why; }
	bool placeholder() const { return m_opt.mode != WaterSceneOptions::kOff && m_why != WW_MODULE; }
	// After the terrain draw, when decide() said so: the module's water into the terrain's targets.
	c5r::Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
	                 const c5r::terrain::Camera& cam, c5r::water::FrameStats* st);
	// The water clock's two fields at nowMs (tests): Water+0xDC and Water+0xE4.
	void clockAt(double nowMs, float& dc, float& e4) const;
	const WaterTapReader* reader() const { return m_reader.get(); }   // null before attach()
	// ---- issue 3903 (epic 0301): the file source and the clock passed in ----
	WaterTapReader* reader() { return m_reader.get(); }               // the recorder sets its sink here
	// The recorded image through WaterTapReader::loadImage (the same checks as poll()), then used as poll() uses an
	// accepted copy. A scene without a reader (attach(nullptr), the player) gets one without a section. False: refused
	// (reader()->lastResult() / lastWhy()); the scene keeps what it had.
	bool loadImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam, uint32_t gridSeq,
	               uint32_t samplesX, uint32_t samplesY);
	WaterClockState clockState() const;
	// Seeds the clock and the waves (the player, from water_clock.bin, after the image is in use: useImage() resets
	// both). False, nothing changed: a time not finite, or a wave array whose size is not the image's (with waves).
	bool setClockState(const WaterClockState& s);
	void secondLine(char* out, size_t n);   // also logs reflLine's text as a line of its own (issue 0382), then resets the second
	void reflLine(char* out, size_t n) const;   // issue 0382: "water refl 1s: w_refl_ice= w_refl_frames= ..." of this second
	void totalsLine(char* out, size_t n);

	int64_t imagesUsed = 0, reflectionErrors = 0, meshErrors = 0, drawsTotal = 0, drawErrors = 0, badCamera = 0, placeholderFrames = 0;
	int64_t waveErrors = 0, waveDrawsTotal = 0;   // issue 3609
	// issue 0382: frames whose reflection hook drew, its failures, frames without a usable reflection camera
	int64_t reflFrames = 0, reflErrors = 0, reflBadCamera = 0;
	const c5r::water::ReflectionCamera& lastReflection() const { return m_refl; }   // tests: the newest frame's camera
	static const uint32_t kReflectionTargetSize = 1024;   // t4's size (epic 0375's units draw their reflection at it)
	ID3D11ShaderResourceView* reflectionSrv() const { return m_reflSrv; }   // tests (epic 0375)
	bool wavesReady() const { return m_wavesOk; }
	const std::vector<c5r::water::WaveRecord>& waves() const { return m_waves; }   // tests: the server-side wave array

private:
	void useImage();
	void drawReflection(ID3D11DeviceContext* ctx, const c5r::terrain::Camera& cam, c5r::water::FrameInputs& in);   // issue 0382
	std::unique_ptr<WaterModule> m_mod;
	std::unique_ptr<WaterTapReader> m_reader;
	WaterSceneOptions m_opt;
	bool m_modOk = false, m_modTried = false;
	std::string m_modWhy;
	bool m_haveField = false;
	uint32_t m_fieldLoad = 0;
	const c5r::heightfield::HeightGrid* m_grid = nullptr;   // the scene's kept field (not copied: 126 MiB on huge)
	bool m_haveImage = false;
	uint32_t m_imageLoad = 0;
	uint32_t m_systems = 0;               // bit (1 << System) with a mesh the module accepted
	c5r::water::WaterFields m_fields;
	float m_dc0 = 0, m_e4 = 0, m_dcRate = 0, m_e4Rate = 0;
	double m_clockStartMs = -1, m_lastNowMs = 0;
	float m_waterZ = -1.0f;
	int m_oceanPass = -1;                 // Water+0xE8 of the tapped object (0, 2 or 3), else -1 (the module's)
	ID3D11Texture2D* m_reflTex = nullptr;   // t4: "Reflection Texture Raw" (issue 0382: a render target)
	ID3D11ShaderResourceView* m_reflSrv = nullptr;
	ID3D11RenderTargetView* m_reflRtv = nullptr;   // issue 0382 (null: the immutable empty texture of 3607)
	ID3D11Texture2D* m_reflDepth = nullptr;
	ID3D11DepthStencilView* m_reflDsv = nullptr;
	c5r::water::ReflectionCamera m_refl;
	int m_reflSec = 0;                    // per second: frames the hook drew
	uint32_t m_reflDraws = 0;             // the newest hook's draw calls
	std::string m_lastReflError;
	int m_why = WW_NO_TAP;
	int m_loggedWhy = -1;
	uint32_t m_loggedLoad = 0xFFFFFFFFu;
	int64_t m_seenAccepted = 0;
	// per second
	int m_drawsSec = 0, m_placeholderSec = 0;
	double m_cpuSumSec = 0;
	std::string m_lastDrawError;
	c5r::water::FrameStats m_lastStats;   // the newest successful draw's (the per-second line)
	// the coast waves (issue 3609)
	bool m_wavesOk = false;               // the image's waves parsed and the module took the chunks
	c5r::water::WaveObject m_waveObj;
	std::vector<c5r::water::WaveRecord> m_waves;
	uint32_t m_waveChunks = 0;
	double m_waveLastMs = -1;
	std::string m_lastWaveError;
};

}  // namespace srvrender
