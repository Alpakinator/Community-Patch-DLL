// srv_fow_scene.h - the fog of war of the renderer inside server.exe (epic 0168, task 3954 = child C5).
// Written 2026-11-14 for this repository (GPL-3.0-or-later). Design: research/05-graphics-composition/fog-of-war.md 5 (route
// (a), MC #1131), as built in its section 7.3.
//
// What it does, per drawn frame of the terrain scene (srv_render_scene.cpp):
//   1. decide(): admit() holds the frame (nothing drawn) until the fog can be drawn: the plot table of the field's size
//      and an overlay pair (the mask shares the overlay's window, decision 5 (ii)). The first frame that wants to be
//      drawn is the fog's install: a module that could not start (c5r::fow MaskRenderer / CloudRenderer init),
//      render_overlay=0, an overlay tap that is OFF or absent, or no table / no revealed plot / no pair within
//      FowSceneOptions::installWaitMs of the fog's own time (render-thread gaps above stallGapMs not counted, ticket 0429)
//      is an install failure (design 5.5, decision 4): failed() carries the text, the host
//      takes the fatal route (FowFatalStep -> HostConfig::fatal, 0167's code 5: server.exe writes its report and exits,
//      D125). Until then and after it, no frame is drawn: never a game frame without the fog (D118). Without a hook
//      (tests, test hosts) the failure is LOUD once and the scene draws on without the fog (0167's rule).
//   2. beforeWorld(): the mask of this frame from the plot states and the overlay pair's OverlayProj per view (rebuilt
//      when the fog generation or the pair changed, else kept), FOWAnim from the server's clock and the map's world
//      width. The mask goes to the water's and river's t10 (WaterSceneFow, srv_water_scene.cpp) and, through
//      terrainInputs(), to the terrain (t4, t5, FOWAnim: task 3953 binds them).
//   3. afterWorld(): the cloud planes (FOW CULL and the three FOWBaseLayer planes) into the scene's colour and depth after
//      the terrain and the water, before the image goes to the composite; at FOWLevel 3 then the particles (ticket 0340,
//      c5r::fow::ParticleRenderer, fog-of-war.md 10) on the cloud pass's depth copy, one simulation step per drawn frame.
//      A particle module that cannot start is not an install failure: the fog is drawn as at FOWLevel 2, as stock does
//      when its particle files are missing (fog-of-war.md 10.1), LOUD once.
// In play nothing is fatal (D132 R6): a fog event for a plot outside the grid, a feed sequence gap, an overflow (the
// table is re-read), fog bits above 1, a pair that cannot be used (the last good one is kept), a failed mask build or
// cloud draw: each LOUD (once per reason, or the first three) and counted.
//
// Inputs: the event feed (SrvFeed, drained by the host: onBatch) and its plot table (PlotRecord::fow: bit 0 revealed,
// bit 1 visible, of the active player's team; c5r::fow::stateFromFowBits); the overlay pair (srv_overlay_tap.h
// OverlayFrame, frame N-1 with its texels); the camera block (views, projection, the world box 0xD90FCC / 0xD90FD8, the
// wrap); FOWLevel from the player's GraphicsSettingsDX11.ini (or render_fow_level). No DLL access.
// Every call on the render thread. The modules and the plot table sit behind FowModules / FowPlotTable so that the
// stand-in tests (server/tests/render/fow/) run the scene without the EXE's FOW programs.
#pragma once
#include "srv_render_common.h"
#include "srv_overlay_tap.h"
#include "srv_fow_mask_pub.h"   // ticket 0339: the mask handed to the EXE's model draws
#include "feed/srv_feed.h"
#include "c5r/fow/fow.h"
#include "c5r/heightfield/heightfield.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace srvrender {

// The scene's view of c5r::fow (MakeC5rFowModules wraps MaskRenderer and CloudRenderer).
struct FowModules
{
	virtual ~FowModules() {}
	virtual c5r::Status initMask(ID3D11Device* dev, const std::wstring& exe, uint32_t size) = 0;
	virtual c5r::Status initClouds(ID3D11Device* dev, const std::wstring& exe, const std::wstring& gameDir) = 0;
	virtual c5r::Status buildMask(ID3D11DeviceContext* ctx, const c5r::fow::MaskInput& in, c5r::fow::MaskStats* st) = 0;
	virtual c5r::Status drawClouds(ID3D11DeviceContext* ctx, const c5r::fow::CloudInput& in, c5r::fow::CloudStats* st) = 0;
	virtual ID3D11ShaderResourceView* mask() const = 0;        // MaskRenderer::srv()
	virtual ID3D11ShaderResourceView* cloudBase() const = 0;   // CloudRenderer::cloudBase()
	virtual void release() = 0;                                // the D3D objects (device lost, shutdown)
	// ticket 0340: the FOWLevel-3 particles (c5r::fow::ParticleRenderer). The defaults say "not provided" (Unsupported), so
	// seams written before 0340 (the stand-in tests, the recorder's test) run as at FOWLevel 2.
	virtual c5r::Status initParticles(ID3D11Device*, const std::wstring&, const std::wstring&)
	{
		return c5r::Status::fail(c5r::Err::Unsupported, "this module set has no particles");
	}
	virtual c5r::Status resetParticles(float, float, uint32_t) { return c5r::Status::fail(c5r::Err::Unsupported, "no particles"); }
	virtual c5r::Status drawParticles(ID3D11DeviceContext*, const c5r::fow::ParticleInput&, c5r::fow::ParticleStats*)
	{
		return c5r::Status::fail(c5r::Err::Unsupported, "no particles");
	}
	virtual c5r::fow::ParticleSim* particleSim() { return nullptr; }   // tests, the state (saveState / loadState)
};

// ticket 0340: whether the FOWLevel-3 particles are drawn. 1 (default) | 0. Read from the environment variable
// C5_RENDER_FOW_PARTICLES at FowScene::init (a test and tool switch until the render_config key render_fow_particles is
// wired by the follow-up 0403: ParseHostConfig, the recorder and the player are not 0340's files). The offline player
// (FowScene::setClockState) runs without particles unless the bundle carries their state (ticket 0403, 2026-11-16:
// particles.bin through setParticleState; the recorder writes render_config render_fow_particles; ParseHostConfig still
// reads no such key: the switch stays this environment variable).
int RenderFowParticlesOption();
// the seed of the particles' generator (C5_RENDER_FOW_PARTICLE_SEED, default 1: the C runtime's own first seed)
uint32_t RenderFowParticleSeed();
std::unique_ptr<FowModules> MakeC5rFowModules();

// The plot table (MakeFeedPlotTable: SrvFeed::CopyPlots).
struct FowPlotTable
{
	virtual ~FowPlotTable() {}
	virtual void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& snapshotGen, uint32_t& width, uint32_t& height) = 0;
};
std::unique_ptr<FowPlotTable> MakeFeedPlotTable();

// FOWLevel of the player's GraphicsSettingsDX11.ini ("FOWLevel = N"): Documents from C5_DOCUMENTS (a harness instance)
// else SHGetFolderPathW(CSIDL_PERSONAL), + "\My Games\Sid Meier's Civilization 5\". -1 when the file or the key is
// missing or not 0..3; `from` names the file and what was read.
int ReadFowLevel(std::string& from);
// The mask size for a FOWLevel (fog-of-war.md 2.1, 3.3): 1024 at 2-3, 512 at 0-1.
inline uint32_t FowMaskSize(int level) { return level >= 2 ? 1024u : 512u; }

// render_fow and render_fow_level as ParseHostConfig read them last (srv_render_host.cpp; HostConfig is in
// srv_render_host.h, which 3954 does not hold: these move into it when that header is next changed).
int RenderFowOption();        // 1 (default) | 0
int RenderFowLevelOption();   // -1 (default: the player's GraphicsSettingsDX11.ini) | 0..3

struct FowSceneOptions
{
	bool on = false;                 // render_fow
	bool overlay = true;             // render_overlay (the fog needs the pair: render_fow=1 with 0 is an install failure)
	int level = -1;                  // render_fow_level: -1 = ReadFowLevel(), then 3 when the file says nothing
	std::wstring exe, gameDir;       // the EXE (FOW programs) and the install (cloud textures), read-only
	bool freeze = false;             // the clock held at 0 (render_water_freeze: diagnostics, deterministic pictures)
	double installWaitMs = 10000;    // the first wanted frame waits this long for the table, a revealed plot and a pair
	// ticket 0429: the wait is the fog's own time, not wall time. A gap between two admit() calls longer than this
	// counts as this much; the rest of it is a gap of the render thread (the server's post-field stall, 0407: 8.7-23.4 s
	// in the NVIDIA driver; or no frame wanted) and is not counted, but named in the failure's text. A thread that comes
	// back only every few seconds still reaches installWaitMs after installWaitMs / stallGapMs checks.
	double stallGapMs = 1000;
	// ticket 0674 (pan-coherence.md 3): render_fow_mask_window. true (camera, the default) = when camera N's footprint leaves
	// the overlay pair's window (frame N-1's) on any view, the mask is built in a window made from camera N (the EXE's rule,
	// c5r::fow::windowFromCamera) and the scene moves the overlay into it; false (pair) = the pair's window always (the
	// picture before 0674: the part of the view outside it reads no mask for that frame).
	bool maskWindowCamera = true;
};

// ---- epic 0301, issue 3911 (MC #1450): FOWAnim's clock for the offline scene harness (WaterClockState's pattern) ----
// clockS = T in seconds, accumulated over the frames the fog drew (beforeWorld); clockLastMs = the nowMs of the last of
// them (-1: none yet). The recorder takes it at the span's first frame (fog_clock.bin); the player seeds it there.
struct FowClockState
{
	double clockS = 0;
	double clockLastMs = -1;
};

// ---- ticket 0403: the FOWLevel-3 particles' state for the offline scene harness (the bundle member particles.bin) ----
// spawned: the particles were spawned (FowScene's first particle frame since its init or a new world) before the frame the
// state is taken at; then sim = c5r::fow::ParticleSim::saveState() (byte-exact) and worldW x worldH the world they were
// spawned for. Not spawned: sim empty, the next particle frame spawns them with `seed` for the world it draws (as live).
#define SRV_FOW_PARTICLE_STATE 1   // ticket 0403: this API exists (the tests' main-equality build compiles without it)
struct FowParticleState
{
	bool spawned = false;
	uint32_t seed = 1;
	float worldW = 0, worldH = 0;
	std::vector<uint8_t> sim;
};

// What the terrain draw needs for its fog (task 3953 binds these: t4 = mask, t5 = cloudBase, TerrainFrame 0x090 =
// fowAnim; the TerrainFOW LUT t6 is the terrain module's). on false: the terrain keeps today's constants.
struct FowTerrainInputs
{
	bool on = false;
	ID3D11ShaderResourceView* mask = nullptr;
	ID3D11ShaderResourceView* cloudBase = nullptr;
	float fowAnim[4] = {0.002f, 0.002f, 0.0f, 0.0f};
	// ticket 0674: the window the mask was placed in this frame (windowViews OverlayProj matrices, view order as the
	// camera's). cameraWindow false: the overlay pair's own (the reader places the mask with the pair, as before 0674);
	// true: made from camera N because its footprint left the pair's window: the terrain, water, trees, ice and models
	// must place the mask (and the overlay, moved into it) with these (srv_render_scene.cpp).
	bool cameraWindow = false;
	uint32_t windowViews = 0;
	float windowProj[VPSHELL_OVERLAY_MAX_VIEWS][16] = {};
};

class FowScene
{
public:
	enum State { kOff = 0, kWaiting, kActive, kFailed, kWithout, kStateCount };
	static const char* StateName(int s);

	explicit FowScene(std::unique_ptr<FowModules> m = nullptr, std::unique_ptr<FowPlotTable> t = nullptr);
	~FowScene();
	// tests: replace the seams before init() (the scene keeps them across device rebuilds)
	void setModules(std::unique_ptr<FowModules> m) { m_mod = std::move(m); }
	void setTable(std::unique_ptr<FowPlotTable> t) { m_table = std::move(t); }

	// The modules on this device (their failure is kept, not reported, until the first wanted frame) and a first read of
	// the plot table. Off: nothing.
	void init(ID3D11Device* dev, const FowSceneOptions& o);
	void releaseDevice();
	// The host's drained feed batch: true when a plot's fog state changed (the scene redraws).
	bool onBatch(const SrvFeed::Batch& b);
	// decide(): true = the frame may be drawn now. `cam` the frame's camera, mapW x mapH the field's grid plots
	// (HeightGrid::mapW / mapH: mapH = the DLL's height + 4 margin rows; the table must be mapW x (mapH - 4)), ovl the
	// overlay source's state (nullptr: no source, render_overlay=0), pair the overlay pair of this frame (nullptr: none).
	bool admit(const VPShellCamera& cam, uint32_t mapW, uint32_t mapH, const OverlayTapReader::State* ovl, const OverlayFrame* pair,
	           double nowMs);
	// ticket 0691 (MC #11169): the hold of the newest admit(): the frame id admit() held while it waits for inputs that later
	// EXE frames bring (the table, a revealed plot, the pair) and why (kHold* bits, equal to vp_frame.h's VPHOLD_*: the host
	// reports them to the composite, which then does not wait for that frame); 0 / 0 after an admitted frame, off, without,
	// and a failed fog (which waits for nothing: the composite's hung route stays).
	enum HoldBits : uint32_t { kHoldNoTable = 1u << 0, kHoldTableSize = 1u << 1, kHoldNoRevealed = 1u << 2, kHoldNoPair = 1u << 3, kHoldInstall = 1u << 4 };
	uint32_t holdReasons() const { return m_holdBits; }
	int64_t holdFrameId() const { return m_holdFrame; }
	// Before the world draw: the mask of this frame (nullptr when the fog is not drawn: off, or failed without a hook).
	// grid: the scene's field of this load (the placement: M_h2w, sx, sy, ox, oy, the samples; the world box through
	// c5r::water::worldFromGrid, bit-equal to the EXE's globals); null: the camera block's world box and nominalPlacement.
	ID3D11ShaderResourceView* beforeWorld(ID3D11DeviceContext* ctx, const VPShellCamera& cam, const OverlayFrame* pair, double nowMs,
	                                      const c5r::heightfield::HeightGrid* grid = nullptr);
	// After the terrain and the water, into the scene's targets: the cloud planes.
	void afterWorld(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h);
	// ticket 0753 (J3, rule A5): while recording (afterWorld called with a deferred context) the particle draw's GPU timing
	// is not issued on that context (a deferred context cannot read a query back); recordedTimed() says the recorded draw
	// wanted it, and listTiming brackets the executed list on the immediate context instead (the "gpu=" of the fog's line
	// then covers the whole list: the cloud planes, the particles and the path; the line says gpu_scope=list)
	void setRecording(bool on)
	{
		m_recording = on;
		if(on)
			m_recTimed = false;
	}
	bool recordedTimed() const { return m_recTimed; }
	void listTiming(ID3D11DeviceContext* immediate, bool begin);
	const FowTerrainInputs& terrainInputs() const { return m_terrain; }   // for 3953 (valid after beforeWorld)
	// The install failure (once true, stays): why = the text for the report.
	bool failed(std::string& why) const
	{
		if(m_state == kFailed || m_state == kWithout)
			why = m_failWhy;
		return m_state == kFailed;
	}
	// No fatal hook (tests, test hosts): LOUD once, then the scene draws on without the fog.
	void drawWithout();
	int state() const { return m_state; }
	int level() const { return m_level; }
	uint32_t maskSize() const { return m_maskSize; }
	uint32_t fogGeneration() const { return m_fogGen; }
	const std::vector<c5r::fow::PlotState>& states() const { return m_states; }
	uint32_t tableWidth() const { return m_w; }
	uint32_t tableHeight() const { return m_h; }
	// Ticket 5052 (T): the plot table of the newest read (readTable's copy of SrvFeed::CopyPlots: tableWidth() x
	// tableHeight() records, each plot's terrain and feature) for the units' $(TERRAIN); null with n 0 before a table was
	// used, or while the newest read was refused for its size (m_rec then holds the refused records). key: the read count
	// (rereads), +1 for every copy taken, so every re-read is a new key: a new snapshot, a feed gap, and a batch with changed
	// plots (a forest cut is a SetPlot patch: changedPlots, no new snapshot generation and, without a fog change, no new
	// fogGeneration either); a frame without a read keeps the key (the units copy nothing again). unit-combat-server.md 18.25.3.
	const SrvFeed::PlotRecord* plotTable(size_t& n, uint32_t& key) const
	{
		key = (uint32_t)rereads;
		n = 0;
		if(!m_haveTable || !m_recUsed || m_rec.empty() || m_rec.size() != (size_t)m_w * m_h)
			return nullptr;
		n = m_rec.size();
		return m_rec.data();
	}
	const c5r::fow::MaskInput& lastMaskInput() const { return m_lastIn; }   // tests: the newest build's input
	const c5r::fow::CloudWorld& lastCloudWorld() const { return m_cloudWorld; }
	// The pair the mask is built with (numViews and OverlayProj only; srv null): the scene places the terrain's and the
	// water's mask with it when the overlay's own pair is not bound (the tap lost or stale). False before the first pair.
	bool keptPair(OverlayFrame& out) const
	{
		if(!m_havePair)
			return false;
		out = OverlayFrame();
		out.numViews = m_pairViews;
		out.generation = m_pairGen;
		out.slot = m_pairSlot;
		memcpy(out.overlayProj, m_pairProj, sizeof(out.overlayProj));
		return true;
	}
	// ticket 0339: the EXE's pid (the main section's tap blocks; 0 = not known) for the mask handed to the EXE's model draws
	// (srv_fow_mask_pub.h). The scene sets it every frame; without it nothing is handed over.
	void setExePid(uint32_t pid) { m_exePid = pid; }
	FowMaskPublisher& maskPublisher() { return m_pub; }   // tests: the section name, the counters
	const c5r::fow::MaskStats& lastMaskStats() const { return m_maskStats; }
	const c5r::fow::CloudStats& lastCloudStats() const { return m_cloudStats; }
	// ticket 0340: the particles (FOWLevel 3): on = initialised and drawn; stats of the newest frame; the module's sim
	bool particlesOn() const { return m_partOn; }
	const c5r::fow::ParticleStats& lastParticleStats() const { return m_partStats; }
	c5r::fow::ParticleSim* particleSim() { return m_mod ? m_mod->particleSim() : nullptr; }
	int64_t particleFrames = 0, particleErrors = 0, particleResets = 0;
	const float* lastFowAnim() const { return m_terrain.fowAnim; }
	// epic 0301, issue 3911: the clock's state (FowClockState above). setClockState refuses (false, logged, nothing
	// changed) a value that is not finite or a T below 0.
	FowClockState clockState() const
	{
		FowClockState s;
		s.clockS = m_clockS;
		s.clockLastMs = m_clockLastMs;
		return s;
	}
	bool setClockState(const FowClockState& s);
	// ticket 0403: the particles' state now (the recorder, at the span's first frame before its draw): false when the
	// particles are not on (below FOWLevel 3, switched off, a module that could not start, the offline player) or the
	// module has no simulation; why says which.
	bool particleState(FowParticleState& out, std::string& why) const;
	// ticket 0403: the offline player, after setClockState: the particles on again from a recorded state (spawned: a spawn
	// for the recorded world with the recorded seed, then ParticleSim::loadState; not spawned: the next particle frame
	// spawns with the recorded seed). False, nothing changed but the particles left off, when the module did not start or
	// the state is refused (why: the reason, e.g. loadState's BadLayout for another install's flow field).
	bool setParticleState(const FowParticleState& s, std::string& why);
	void secondLine(char* out, size_t n);
	void totalsLine(char* out, size_t n);

	// totals
	int64_t framesWith = 0, framesHeld = 0, framesWithout = 0, masksBuilt = 0, cloudDraws = 0;
	int64_t buildErrors = 0, cloudErrors = 0, badPairs = 0, keptPairFrames = 0, viewMismatch = 0, nominalFrames = 0;
	int64_t seqGaps = 0, overflows = 0, outsideEvents = 0, badBits = 0, rereads = 0, fowEvents = 0, stateChanges = 0;
	// ticket 0674: frames whose mask window was made from the camera (`camwin=` of the fog 1s line), and frames whose camera
	// footprint could not be made (a degenerate camera: the pair's window kept)
	int64_t cameraWindowFrames = 0, footprintErrors = 0;
	bool maskWindowCamera() const { return m_opt.maskWindowCamera; }   // render_fow_mask_window = camera

private:
	void fail(const std::string& why);
	void readTable(const char* why);
	bool pairUsable(const OverlayFrame& p, std::string& why) const;
	void handOver(ID3D11DeviceContext* ctx, bool builtNow);   // ticket 0339
	void loud(std::string& last, const std::string& now, const char* fmt, ...);
	// a copy of the scene's depth for the cloud pass (its FOW CULL writes 0 that the EXE's models must not see)
	ID3D11DepthStencilView* scratchDepth(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);
	ID3D11Texture2D* m_scratchTex = nullptr;
	ID3D11DepthStencilView* m_scratchDsv = nullptr;
	D3D11_TEXTURE2D_DESC m_scratchDesc = {};

	std::unique_ptr<FowModules> m_mod;
	std::unique_ptr<FowPlotTable> m_table;
	FowSceneOptions m_opt;
	int m_state = kOff;
	std::string m_failWhy;
	c5r::Status m_maskInit, m_cloudInit;
	bool m_modInit = false;
	int m_level = -1;
	std::string m_levelFrom;
	uint32_t m_maskSize = 0;
	// the plot table
	std::vector<SrvFeed::PlotRecord> m_rec;
	std::vector<c5r::fow::PlotState> m_states;
	uint32_t m_w = 0, m_h = 0, m_tableGen = 0;
	bool m_haveTable = false;
	bool m_recUsed = false;          // ticket 5052 (T): m_rec is the newest read and it was used (not refused for its size)
	uint32_t m_fogGen = 0;            // +1 whenever a plot's state changed (or a new table)
	uint32_t m_revealed = 0;          // plots not unexplored
	uint32_t m_lastSeq = 0;
	uint64_t m_lastDropped = 0;
	// the pair the mask was built with (the last good one is kept while the tap is stale or lost)
	bool m_havePair = false;
	uint32_t m_pairViews = 0, m_pairGen = 0, m_pairSlot = 0xFFFFFFFFu;
	float m_pairProj[VPSHELL_OVERLAY_MAX_VIEWS][16] = {};
	uint64_t m_pairPresent = 0;       // ticket 0339: the pair's presentCount (handed over with the mask)
	// ticket 0674: the window the mask is placed in this frame (the pair's, or made from the camera) and the one the newest
	// mask was built in (part of the rebuild key; handed over with the mask)
	bool chooseWindow(const VPShellCamera& cam, uint32_t camViews);
	uint32_t m_winViews = 0, m_builtViews = 0;
	float m_winProj[VPSHELL_OVERLAY_MAX_VIEWS][16] = {};
	float m_builtProj[VPSHELL_OVERLAY_MAX_VIEWS][16] = {};
	int m_camWinSec = 0;
	bool m_camWinNoted = false;
	std::string m_lastFootprintErr;
	// the mask
	bool m_built = false;
	uint32_t m_builtFogGen = 0, m_builtPairGen = 0, m_builtPairSlot = 0xFFFFFFFFu;
	c5r::fow::View m_views[c5r::fow::kMaxViews];
	c5r::fow::MaskInput m_lastIn;
	c5r::fow::CloudWorld m_cloudWorld;
	c5r::fow::MaskStats m_maskStats;
	c5r::fow::CloudStats m_cloudStats;
	bool m_nominalNoted = false;     // the placement fell back to nominalPlacement (noted once)
	bool m_maskOk = false;           // the newest build succeeded (the clouds draw only then)
	// ticket 0691: the hold of the newest admit() (holdReasons / holdFrameId)
	uint32_t m_holdBits = 0;
	int64_t m_holdFrame = 0;
	// install
	double m_firstWantMs = -1;
	// ticket 0429: the fog's own clock: admit() to admit() gaps up to stallGapMs counted, the rest a render-thread gap
	double m_lastAdmitMs = -1;
	double m_ownMs = 0, m_gapMs = 0, m_gapMaxMs = 0;   // totals since init (counted, not counted, the longest gap)
	int64_t m_gaps = 0;
	double m_firstWantOwnMs = 0, m_firstWantGapMs = 0;   // at the first wanted frame (the install's start)
	int64_t m_firstWantGaps = 0;
	double m_holdOwnMs = 0;                               // m_ownMs at the hold episode's start
	void tickClock(double nowMs);
	std::string waitedText(double nowMs) const;   // "... ms wall time, ... in N gaps ..." of the install's wait
	int64_t m_heldStamp = -1;
	bool m_holding = false, m_holdLoud = false;   // an episode of held frames (its first frame noted)
	int64_t m_holdFrom = 0;
	double m_holdSinceMs = 0;
	// the clock (FOWAnim's T: accumulated while the map is drawn)
	double m_clockS = 0, m_clockLastMs = -1;
	float m_worldWidth = 0;
	FowTerrainInputs m_terrain;
	// ticket 0339: the mask handed to the EXE's model draws (the consumer's FAILED state fails the install, D125)
	FowMaskPublisher m_pub{&RNote};
	uint32_t m_exePid = 0;
	// ticket 0340: the particles
	void drawParticles(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, uint32_t w, uint32_t h);
	void particleTiming(ID3D11DeviceContext* ctx, bool begin);
	void dumpParticles();             // C5_FOW_PARTICLE_DUMP (the acceptance's statistics; srv_fow_scene.cpp)
	int m_dumpState = 0;              // 0 not checked, 1 dumping, 2 off
	int m_dumpLeft = 0;
	std::wstring m_dumpPath;
	c5r::Status m_partInit;
	bool m_partOn = false;            // initialised at FOWLevel 3 and not switched off
	bool m_partReset = false;
	bool m_partPlayer = false;        // setClockState was called: the offline player (no particles, 0403)
	float m_partW = 0, m_partH = 0;   // the world the particles were spawned for
	uint32_t m_partSeed = 1;          // ticket 0403: the spawn's seed (RenderFowParticleSeed() at init; the player's recorded one)
	bool m_partModule = false;        // ticket 0403: the particle module started in the newest init (setParticleState needs it)
	c5r::fow::ParticleStats m_partStats;
	std::string m_lastPartErr;
	double m_partCpuMsSec = 0;        // per second: CPU ms of the particle frames (emit, sort, upload, step)
	int m_partSec = 0;
	// GPU time of the particle draw (indication, COMMON 23): a ring of disjoint + 2 timestamp queries, read 3 frames later
	struct PartQuery { ID3D11Query* dis = nullptr; ID3D11Query* t0 = nullptr; ID3D11Query* t1 = nullptr; bool pending = false; };
	PartQuery m_pq[4];
	uint32_t m_pqAt = 0;
	double m_partGpuMsSec = 0;
	int m_partGpuN = 0;
	bool m_recording = false, m_recTimed = false;   // ticket 0753 (setRecording)
	int m_listTimedSec = 0;                          // ticket 0753: timings of the second taken around an executed list
	// per second
	int m_withSec = 0, m_heldSec = 0, m_builtSec = 0;
	// once-per-reason LOUD texts
	std::string m_lastBuildErr, m_lastCloudErr, m_lastBadPair, m_lastOutside, m_lastBits, m_lastGap, m_lastSize;
};

// One step of the fatal route, run by the host after every Scene::poll() (as OverlayFatalStep): when the fog's install
// failed and `done` is false: done = true, then fatal(kFowFatalExitCode, text) (server.exe: SrvRemote::ReportAndExit, which
// does not return). fatal null: LOUD once, the scene draws on without the fog. Never for a fault in play.
const int kFowFatalExitCode = 5;   // the overlay's code (0167): one exit code for "the 64-bit path cannot start its picture"
class Scene;
void FowFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);

// The fog the water scene `w` binds at its next draw() (srv_water_scene.cpp): ocean and river t10 = mask (null: the
// module's own constant input, the picture before 3954); proj = the copy matrices that place it (proj[1] the camera's
// view, [0] / [2] the views at -width / +width), used for WaterFrame.OverlayProj when no overlay is bound (with an overlay
// its own matrices are the same). A free function because srv_water_scene.h is not 3954's (3704's WaterSceneOverlay
// pattern); WaterSceneFowMask returns what was set for `w` (tests).
class WaterScene;
void WaterSceneFow(const WaterScene* w, ID3D11ShaderResourceView* mask, const float (*proj)[16]);
ID3D11ShaderResourceView* WaterSceneFowMask(const WaterScene* w);

}  // namespace srvrender
