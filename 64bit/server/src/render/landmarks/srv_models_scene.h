// srv_models_scene.h - the server's model module: cities and landmarks drawn from the compositor's tap (epic 0561, ticket
// 4875; research/05-graphics-composition/city-renderer.md 2.2-2.4, 3.2-3.5 and 11). Written 2026-11-18 for this repository
// (GPL-3.0-or-later).
//
// Two seams, each a factory registered at static initialisation (the units' pattern, srv_units_mode.h), so that every
// program that links srv_render_scene.cpp but not this folder (the render host tests, the stand-in suites) builds and
// runs as before:
//   ModelsScene  what Scene calls (srv_render_scene.cpp defines RegisterModelsSceneFactory / MakeModelsScene; this
//                folder's srv_models_scene.cpp registers the implementation). Without it Scene refuses render_cities=1
//                or render_landmarks=1 (LOUD) and draws as before.
//   ModelLayer   what the module draws with: the model library, the wire index, the content checks and resolve of a tap
//                frame, the renderer (c5r::landmarks, tickets 4872 / 4873; the adapter is srv_model_layer_c5r.cpp, the
//                stand-in tests give their own). Defined here so the module and its D118 rules are tested without D3D.
//
// The module (all calls on the render thread but ModelLayer::open, which runs on one worker thread):
//   1. configure: render_cities / render_landmarks (options 12 / 13 through the proxy, only with ServerRenderer = 1),
//      render_models_source (exe | own), the bounds of the mode writer (srv_models_mode.h).
//   2. poll: the EXE's pid known -> the tap reader (srv_model_tap.h) with the want bits of the options on (the tap runs
//      before any bit is set: it records, drops nothing); the game database copy known -> the layer opened once on a
//      worker (library load, wire index); the device known -> the layer's device part; then every loop: the reader's
//      poll (the layer's content check and resolve on each copy), the identity set published when the layer's drawable
//      keys changed (BuildIdentitySet, then setGeneration) and the compositor's verdict read back, the mode writer's poll.
//   3. draw: the newest good frame's resolved scene into the scene target (and drawReflection into the water's
//      reflection), with the server's own fog in PS t7 / t8 (FrameTextures); then the mode writer's frame rule.
//   Source own: an empty scene with its reason (the own layout is parked: 4880 / 4881); the bits are never set.
//   Failures (D118, D125): a layer that cannot open or make its device part while an option is on, the mode writer's
//   fatal cases (srv_models_mode.h) -> failed(why) -> the host's ModelsFatalStep: HostConfig::fatal(5, why) once.
//   Ticket 4885: the open's begin, progress and end go to the mode writer (onOpenBegin / onOpenProgress / onOpenEnd);
//   progress = the open's worker thread ran (QueryThreadCycleTime grew since the last poll), so a disk-bound open that
//   reads and parses makes progress and a blocked one does not; the install bound waits for the open's end.
#pragma once
#include "srv_render_common.h"
#include "shell_model_tap.h"
#include "c5r/status.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace srvmodels {

struct KeyInfo;

enum ModelsSource { kModelsSourceExe = 0, kModelsSourceOwn = 1 };

struct ModelsOptions
{
	bool cities = false;              // render_cities
	bool landmarks = false;           // render_landmarks
	int source = kModelsSourceExe;    // render_models_source
	double noFrameMs = 500, holdMs = 30000, installMs = 30000;   // _no_frame_ms, _hold_ms (= kModelsHoldMsDefault: 0641), _install_ms
	// ticket 4885: the layer's open (srv_models_mode.h): no progress of the open's worker (its CPU cycles) this long, and
	// its ceiling from its begin; 0 = none. render_models_open_stall_ms, render_models_open_max_ms
	double openStallMs = 60000, openMaxMs = 600000;
	std::wstring install;             // the game install (art, .gr2, textures), read-only
	std::wstring exe;                 // CivilizationV_DX11.exe: the BuildingShader programs by digest (read-only)
	std::vector<std::wstring> looseRoots;   // the active mods' folders above the install (ModFolders(); may be empty)
	// render_test_models_draw_all=1 (TEST ONLY: offline frames made from a capture, whose records carry no DROPPED flag):
	// every resolved record is drawn. 0 (default): only the records the compositor DROPPED (VPMTAP_DRAW_DROPPED), i.e.
	// exactly what is missing from the EXE's picture, so a bit not latched draws nothing twice.
	bool drawAll = false;
	// R1 (landmark-renderer.md 7.7, lead2-0561): the server's frame for the EXE's Present P draws the tap of P (the frame
	// whose VPModelTapFrame::presentCount is P) when it was accepted; else the reader is polled for it up to
	// render_models_pair_wait_ms (0: no wait); else the newest older tap (counted with its lag in Presents). A tap newer
	// than P only when no older one exists (counted). Ticket 4882 (2026-11-19, city-renderer.md 11.11): under the
	// product's lockstep the tap of P is published at Present P, after the composite took the server's frame P at its
	// insertion, so the exact tap is never there in time and a wait only delays the server's frame (s3: the 4 ms wait made
	// the composite's lockstep give up for the whole session). Default 0; the records are drawn at the frame's own
	// camera instead (FrameTextures::viewProj), so a tap one Present behind costs no misregistration.
	double pairWaitMs = 0.0;
	// Ticket 0709 (pan-coherence.md 5.14): the instances the EXE culled at the drawn tap's camera and that are in view at
	// the frame's camera are drawn from an older drawn tap (CulledKeep below). On by default; the test switch is the
	// environment variable C5_RENDER_MODELS_KEEP=0 (read in configure; no config key yet).
	bool keepCulled = true;
	int64_t keepMaxAgePresents = 120;   // kKeepMaxAgePresentsDefault
	// Ticket 0789 (D269, research/07-architecture-decisions/architect-2026-11-25-retained-world.md 6.1, 6.3): the model
	// store: the keep without the age bound, its own copies, both passes, the removal rule R3, a cut tap bridged and the
	// hand-over frame R5 (CulledKeep's store options below). VP64Options.ini [Renderer] ModelStore (default 1), server.cfg
	// render_models_store (a test override). false: 0709's keep exactly (120 Presents, the scene pass only, no R5).
	bool store = true;
	int storeRefuteTaps = 2;            // kStoreRefuteTapsDefault: R3's evidence, complete taps that lack an instance
	size_t storeMaxEntries = 262144;    // kStoreMaxEntriesDefault: per pass; an eviction is counted and LOUD
	bool any() const { return cities || landmarks; }
};

// The server's frame textures of the BuildingShader PS (t5 .. t9; 4873 EnvInput::frame): t7 the fog-of-war mask, t8 the
// cloud base (fog-of-war.md 2: "the mask in PS slot t7 ... t8 = the cloud base _dif_02; t9 = the TerrainFOW volume").
// Ticket 4882: t5, t6 and t9 (the distance-fog ramp, the Buildings and TerrainFOW ColorKey volumes) and t2-t4 (the
// environment cubes by the tap's identity) are the layer's own, read from the install (c5r/landmarks/inputs.h); a null t7
// / t8 is drawn with a white mask and a black cloud base (the fog-off look), never unbound (unbound t6 or t7 = black).
// The camera rows: the frame's own view-projection (VS cb1 rows 0-3) and, with the server's fog placed, its OverlayProj
// (rows 8-11) and FOWAnim (row 12) replace the tap's in every drawn record, so the models are drawn at the camera and
// the mask placement the terrain is drawn with this frame (city-renderer.md 11.11; the tap may be a Present older).
struct FrameTextures
{
	ID3D11ShaderResourceView* t[5] = {};   // [0] = t5 ... [4] = t9
	bool fogOn = false;                    // the server's fog was drawn this frame (render_fow=1, the mask built)
	bool haveViewProj = false;
	float viewProj[16] = {};               // view x projection, row-major (as D3DXMatrixMultiply; VS cb1 rows 0-3)
	bool haveFog = false;
	float overlayProj[16] = {};            // the OverlayProj the server's mask is placed with (VS cb1 rows 8-11)
	float fowAnim[4] = {};                 // FOWAnim (VS cb1 row 12)
	// ticket 0598: the main view's eye (PS cb1 row 16, city-renderer.md 15.2), the same in the reflection pass as in the
	// scene pass (stock's row 16 is view 0's eye in both); written into every drawn record's PS cb1 row 16
	bool haveEye = false;
	float eye[3] = {};
	// ticket 0652 (city-renderer.md 17): the EXE draws the models it sees through a wrap view in draws of their own (VS cb1
	// rows 0-3 = view 0's moved by k x wrapPeriod); the layer gives each tapped constant the wrap copy k of this frame's
	// camera nearest its tapped rows, with copy k's OverlayProj (overlayProjWrap: [0] k = -1, [1] k = +1, valid with
	// haveFog) and the eye moved by -k W. wrapPeriod <= 0: every record at view 0 (the rule before 0652).
	float wrapPeriod = 0.0f;
	float overlayProjWrap[2][16] = {};
};
// EyeOfViewProj and FrameEye are inline here (2026-11-20, fix of 0598's link break): srv_render_scene.cpp calls FrameEye
// and is also built without this folder's .cpp files (MakeModelsScene() null), so it takes nothing from landmarks/ but
// this header.
// Ticket 0598: the eye of a perspective view-projection (row-major, row-vector product as VS cb1 rows 0-3): the point the
// matrix sends to clip x = y = w = 0, as 0585 solved stock's row 16 (city-renderer.md 15.2). False (eye untouched) for a
// matrix with no finite eye (orthographic, singular) or a non-finite input.
inline bool EyeOfViewProj(const float vp[16], float eye[3])
{
	for(int i = 0; i < 16; ++i)
		if(!std::isfinite(vp[i]))
			return false;
	// rows of the system: clip column j in {0 (x), 1 (y), 3 (w)} of (x, y, z, 1) x vp = 0
	static const int kCol[3] = {0, 1, 3};
	double a[3][3], b[3];
	double scale = 0.0;
	for(int r = 0; r < 3; ++r)
	{
		for(int k = 0; k < 3; ++k)
		{
			a[r][k] = vp[k * 4 + kCol[r]];
			scale = (std::max)(scale, std::fabs(a[r][k]));
		}
		b[r] = -(double)vp[3 * 4 + kCol[r]];
	}
	const auto det3 = [](const double m[3][3]) {
		return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	};
	const double d = det3(a);
	if(!(scale > 0.0) || !(std::fabs(d) > 1e-9 * scale * scale * scale))
		return false;   // orthographic (the w column is (0, 0, 0, 1)) or singular: no finite eye
	double out[3];
	for(int k = 0; k < 3; ++k)
	{
		double m[3][3];
		for(int r = 0; r < 3; ++r)
			for(int c = 0; c < 3; ++c)
				m[r][c] = c == k ? b[r] : a[r][c];
		out[k] = det3(m) / d;
		if(!std::isfinite(out[k]))
			return false;
	}
	for(int k = 0; k < 3; ++k)
		eye[k] = (float)out[k];
	return true;
}
// Ticket 0598: the eye written into PS cb1 row 16: the camera-to-world matrix's row 3 (the FCamera position, VPShellCamera
// camWorld, the units' eye too) when it is finite and not all zero, else EyeOfViewProj(vp). Neither is bit-equal to the
// EXE's row 16 (the solve moved 1-14 pixels by one level in the library's 0:0 replays, the position 1-6), so the layer
// keeps the tap's own row 16 while the tapped camera is the frame's and uses this eye only once the camera has moved
// (applyFrameCamera, city-renderer.md 15.8). False when neither gives an eye (eye untouched).
inline bool FrameEye(const float camWorld[16], const float vp[16], float eye[3])
{
	if(camWorld)
	{
		bool finite = true;
		for(int i = 0; i < 16; ++i)
			finite = finite && std::isfinite(camWorld[i]);
		if(finite && (camWorld[12] != 0.0f || camWorld[13] != 0.0f || camWorld[14] != 0.0f))
		{
			eye[0] = camWorld[12];
			eye[1] = camWorld[13];
			eye[2] = camWorld[14];
			return true;
		}
	}
	return EyeOfViewProj(vp, eye);
}
// Ticket 0788: the tap records the server draws (ModelLayer::accept, unless drawAll): those the compositor DROPPED, and those
// it HELD (passed in the tap's own frame because their key had just become droppable, dropped from the next frame on: the
// frame the server draws this tap in; research/05-graphics-composition/lategame-model-flash.md)
inline bool ServerDrawsRecord(uint8_t flags) { return (flags & (VPMTAP_DRAW_DROPPED | VPMTAP_DRAW_HELD)) != 0; }

const int kFrameFogMask = 2;     // t7
const int kFrameCloudBase = 3;   // t8

// ---- ticket 0709 (pan-coherence.md 5.11, 5.13, 5.14): the instances the EXE culled at the drawn tap's camera ----
// Under exact lockstep the server's frame for Present P draws the tap of P-1 at camera P. The EXE submits a landmark only
// while its origin's world x, y lies inside the ground box of its view widened by 64 world units (landmark-renderer.md
// 7.7.1 [C]), so an instance that enters that box only at camera P is not in tap P-1, and frame P lacks it (the leading
// edge hole of a fast pan; 5.13: all 364 measured holes were such entries, all re-entries). CulledKeep remembers the
// instances of the taps the layer drew in the last maxAgePresents Presents and gives back, for frame P drawn from tap C,
// the instances that
//   (a) tap C does not hold at all (no record, dropped or passed),
//   (b) a drawn tap L held, C - maxAge <= L < C (the newest such sighting is kept),
//   (c) lie outside camera C's ground box by more than culledBy (0: the box itself, not widened: rebuilt from the tapped
//       rows it is off by up to about 50 world units against the EXE's, 5.13; the measured holes lie 11-110 units
//       outside it),
//   (d) lie farther outside camera C's box than camera L's by more than `approach` (the camera moved away from them
//       since they were last seen: one that vanished while the camera stood still or came nearer was removed by the
//       game, never culled, and is not kept),
//   (e) lie inside camera P's box widened by `lead` (64: the EXE's rule, i.e. tap P would hold them),
// and nothing while camera P is camera C (a still camera). A wrongly kept removal can show for one frame at most: in the
// next frame its tap's camera is the one it is in view of, and (c) fails. Distances are world units: the largest of the
// four sides' distances (negative inside), the smallest over the wrap copies k = -1, 0, +1 of the box (x + k W) when the
// wrap period W > 0.
//
// Ticket 0789 (D269; architect-2026-11-25-retained-world.md 5 R1-R5, 6.1, 6.3): with KeepOptions::store the keep is the
// model STORE: "presence is retained, absence needs evidence".
//   - no age bound (maxAgePresents 0): an instance seen once is known until R3 removes it, a load or an older Present
//     clears all, or the memory bound evicts the oldest (counted, LOUD in the layer);
//   - R3: an entry goes when refuteTaps COMPLETE taps (KeepTap::complete: the header's truncated = 0) that tapped its
//     class (KeepTap::tappedBits, the header's wantBits) lack it while its origin lies inside their box (distance <=
//     culledBy); a sighting resets the count; a cut tap removes nothing;
//   - frame P drawn from tap C also gets every entry not held by tap C whose class bit the compositor drops at P
//     (KeepTap::ownedBits) and that lies inside camera P's box + lead and is (a)-(e) above without (b) (a re-entry), or
//     (f) lies inside camera C's box and was lacked by fewer than refuteTaps complete taps (an ABSENT instance: a draw
//     that flipped from dropped to passed for a frame, a draw the tap did not hold; drawn at a still camera too, counted
//     as bridged), or (g) any, when tap C is cut (complete false: bridged).
// The price (5): a real removal is drawn refuteTaps - 1 frames late (1 by default) instead of a hole in many.
const int64_t kKeepMaxAgePresentsDefault = 120;
const int kStoreRefuteTapsDefault = 2;
const size_t kStoreMaxEntriesDefault = 262144;
const int64_t kStoreStepBackPresents = 16;   // a drawn tap at most this much older than the newest is no load (commit)
// The ground box (world x, y) of a camera as the EXE's views snapshot holds it: the four frustum side edges (the NDC
// corners from depth 0 to 1) cut at z = 0 and z = 127.5, their axis-aligned box, not clamped to the world, not widened
// (landmark-renderer.md 7.7.1; server/evidence/0709/entering.py view_box). vp: rows 0-3 of VS cb1 (row-vector product).
// ok false for a singular or non-finite matrix or an edge parallel to the ground.
struct GroundBox
{
	bool ok = false;
	float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};
GroundBox CullGroundBox(const float vp[16]);
// The distance of (x, y) outside b (see above); FLT_MAX when !b.ok.
float GroundBoxOutside(const GroundBox& b, float x, float y, float wrapPeriod);
// The same camera to float rounding (every element within 1e-6 of the larger matrix's largest element).
bool SameViewProj(const float a[16], const float b[16]);

struct KeepOptions
{
	bool on = true;
	int64_t maxAgePresents = kKeepMaxAgePresentsDefault;   // 0 = no age bound (the store)
	float culledBy = 0.0f, approach = 1.0f, lead = 64.0f;   // (c), (d), (e) above, world units
	size_t maxEntries = 65536;                              // a bound on the memory: the oldest sightings go first
	// ticket 0789: the store's rules (R3 removal, (f) absent, (g) cut tap); the layer also reads it for the reflection
	// pass's store and the hand-over records (R5)
	bool store = false;
	int refuteTaps = kStoreRefuteTapsDefault;
};
// Ticket 0789: the store's options from the module's (on: no age bound, the R3 / R5 rules, both passes; off: 0709's keep)
inline KeepOptions StoreKeepOptions(bool on, bool store, int64_t keepMaxAge, int refuteTaps, size_t maxEntries)
{
	KeepOptions k;
	k.on = on;
	k.store = store;
	if(store)
	{
		k.maxAgePresents = 0;
		k.refuteTaps = refuteTaps >= 1 ? refuteTaps : kStoreRefuteTapsDefault;
		k.maxEntries = maxEntries ? maxEntries : kStoreMaxEntriesDefault;
	}
	else
		k.maxAgePresents = keepMaxAge > 0 ? keepMaxAge : kKeepMaxAgePresentsDefault;
	return k;
}
struct KeepStats
{
	uint32_t candidates = 0;   // remembered, within the age, not held by tap C, of a class dropped at P
	uint32_t kept = 0;
	bool sameCamera = false;   // camera P is camera C: no re-entry
	// ticket 0789: the kept by their rule: re-entries (a)-(e), absent inside tap C's box (f), a cut tap (g)
	uint32_t reentry = 0, absent = 0, cut = 0;
};
// Ticket 0789: what the store needs of the drawn tap C besides its camera
struct KeepTap
{
	int64_t present = 0;
	bool complete = true;         // the header's truncated is 0
	uint32_t tappedBits = ~0u;    // the classes the compositor tapped in it (the header's wantBits)
	uint32_t ownedBits = ~0u;     // the classes the compositor drops at P (live: the server's mode bits; else the latch)
};

template <class Payload>
class CulledKeep
{
public:
	struct Seen
	{
		uint64_t id = 0;   // the instance: its part and its origin's bits (the layer's hash)
		float x = 0, y = 0;
		GroundBox box;     // the ground box of the camera its draw was tapped with
		uint32_t bit = ~0u;   // ticket 0789: its class's mode bit (VPMODE_CITIES / VPMODE_LANDMARKS)
		Payload payload;
	};
	struct Entry
	{
		uint64_t id = 0;
		int64_t last = 0;   // the Present of the newest drawn tap that held it
		float x = 0, y = 0;
		GroundBox box;
		uint32_t bit = ~0u;
		int absent = 0;     // ticket 0789: complete taps since `last` that tapped its class and lacked it inside their box
		Payload payload;
	};
	void configure(const KeepOptions& o)
	{
		m_o = o;
		if(!o.on)
			clear();
	}
	const KeepOptions& options() const { return m_o; }
	void clear()
	{
		m_map.clear();
		m_have = false;
		m_newest = 0;
	}
	size_t size() const { return m_map.size(); }
	bool has(uint64_t id) const { return m_map.count(id) != 0; }   // ticket 0789
	uint64_t removed() const { return m_removed; }   // ticket 0789: R3 removals so far
	uint64_t evicted() const { return m_evicted; }   // ticket 0789: entries the memory bound evicted so far
	// Frame P drawn from tap C (Present tapPresent, its main camera tapVp; held: every instance id it holds, any flag) at
	// camera frameVp: the kept entries, by id ascending. (0709's form: a complete tap, every class.)
	void select(int64_t tapPresent, const std::unordered_set<uint64_t>& held, const float tapVp[16], const float frameVp[16],
	            float wrapPeriod, std::vector<const Entry*>& out, KeepStats* st) const
	{
		KeepTap t;
		t.present = tapPresent;
		select(t, held, tapVp, frameVp, wrapPeriod, out, st);
	}
	void select(const KeepTap& t, const std::unordered_set<uint64_t>& held, const float tapVp[16], const float frameVp[16],
	            float wrapPeriod, std::vector<const Entry*>& out, KeepStats* st) const
	{
		out.clear();
		if(st)
			*st = KeepStats();
		if(!m_o.on || m_map.empty() || !tapVp || !frameVp)
			return;
		const bool same = SameViewProj(tapVp, frameVp);
		if(st)
			st->sameCamera = same;
		if(same && !m_o.store)
			return;
		const GroundBox bc = CullGroundBox(tapVp), bf = CullGroundBox(frameVp);
		if(!bf.ok || (!bc.ok && (!m_o.store || t.complete)))
			return;
		for(const auto& kv : m_map)
		{
			const Entry& e = kv.second;
			if(e.last >= t.present || (m_o.maxAgePresents > 0 && e.last < t.present - m_o.maxAgePresents) || held.count(e.id) ||
			   !e.box.ok)
				continue;
			if(m_o.store && !(e.bit & t.ownedBits))
				continue;   // the EXE draws its class at P
			if(st)
				st->candidates++;
			if(!(GroundBoxOutside(bf, e.x, e.y, wrapPeriod) < m_o.lead))
				continue;   // (e): not in the EXE's box at P either
			if(m_o.store && !t.complete)
			{
				out.push_back(&e);   // (g): a cut tap holds nothing to go by
				if(st)
					st->cut++;
				continue;
			}
			const float dc = GroundBoxOutside(bc, e.x, e.y, wrapPeriod);
			if(dc > m_o.culledBy)
			{
				if(same)
					continue;   // a still camera: nothing re-enters
				const float dl = GroundBoxOutside(e.box, e.x, e.y, wrapPeriod);
				if(!(dc > dl + m_o.approach))
					continue;
				out.push_back(&e);
				if(st)
					st->reentry++;
			}
			else if(m_o.store && e.absent < m_o.refuteTaps)
			{
				out.push_back(&e);   // (f): lacked inside the box by fewer than refuteTaps complete taps
				if(st)
					st->absent++;
			}
		}
		std::sort(out.begin(), out.end(), [](const Entry* a, const Entry* b) { return a->id < b->id; });
		if(st)
			st->kept = (uint32_t)out.size();
	}
	// The tap the layer drew (once per tap): its drawn instances become the newest sightings; sightings older than
	// maxAgePresents go; a Present older than the newest committed one (a load, another bundle) forgets everything first.
	// (0709's form: no R3 evidence.)
	void commit(int64_t present, const std::vector<Seen>& seen)
	{
		KeepTap t;
		t.present = present;
		commit(t, nullptr, nullptr, 0.0f, seen);
	}
	// Ticket 0789: with the store's rules, R3 first: the entries the tap does not hold (held: any flag) that lie inside
	// its box (tapVp) count one more absence when the tap is complete and tapped their class, and go at refuteTaps.
	// Call before select() for the same tap.
	void commit(const KeepTap& t, const std::unordered_set<uint64_t>* held, const float tapVp[16], float wrapPeriod,
	            const std::vector<Seen>& seen)
	{
		if(!m_o.on)
			return;
		const int64_t present = t.present;
		if(m_have && present == m_newest)
			return;   // the same tap again
		// ticket 0789: with the store a tap a few Presents older than the newest (the pairing went back: a cut tap
		// skipped, a refused tap) adds nothing and removes nothing; only a larger step back (a load, another bundle)
		// forgets everything, as 0709's keep does at any step back
		if(m_have && present < m_newest && m_o.store && m_newest - present <= kStoreStepBackPresents)
			return;
		if(m_have && present < m_newest)
			m_map.clear();
		m_have = true;
		m_newest = present;
		if(m_o.store && t.complete && held && tapVp)
		{
			const GroundBox bc = CullGroundBox(tapVp);
			if(bc.ok)
				for(auto it = m_map.begin(); it != m_map.end();)
				{
					Entry& e = it->second;
					if((e.bit & t.tappedBits) && !held->count(e.id) && GroundBoxOutside(bc, e.x, e.y, wrapPeriod) <= m_o.culledBy &&
					   ++e.absent >= m_o.refuteTaps)
					{
						it = m_map.erase(it);
						m_removed++;
					}
					else
						++it;
				}
		}
		for(const Seen& s : seen)
		{
			Entry& e = m_map[s.id];
			e.id = s.id;
			e.last = present;
			e.x = s.x;
			e.y = s.y;
			e.box = s.box;
			e.bit = s.bit;
			e.absent = 0;
			e.payload = s.payload;
		}
		if(m_o.maxAgePresents > 0)
			for(auto it = m_map.begin(); it != m_map.end();)
				it = it->second.last < present - m_o.maxAgePresents ? m_map.erase(it) : std::next(it);
		while(m_map.size() > m_o.maxEntries)
		{
			int64_t oldest = present;
			for(const auto& kv : m_map)
				oldest = (std::min)(oldest, kv.second.last);
			if(oldest == present)
				break;
			for(auto it = m_map.begin(); it != m_map.end() && m_map.size() > m_o.maxEntries;)
			{
				if(it->second.last == oldest)
				{
					it = m_map.erase(it);
					m_evicted++;
				}
				else
					++it;
			}
		}
	}

private:
	KeepOptions m_o;
	std::unordered_map<uint64_t, Entry> m_map;
	bool m_have = false;
	int64_t m_newest = 0;
	uint64_t m_removed = 0, m_evicted = 0;
};

struct LayerDrawStats
{
	uint32_t items = 0, drawn = 0, skipped = 0, unknownDraws = 0, envMissing = 0, problems = 0;
	// ticket 4882: the records whose camera rows were replaced, and the screen shift (pixels of the target) of their
	// instances' origins between the tap's own camera and the frame's (materials 3-5): what drawing the tap at its own
	// camera would have cost; about 0 when the tap is the frame's own Present (the check of the rows' convention)
	uint32_t camConsts = 0, shiftN = 0;
	double shiftPxSum = 0, shiftPxMax = 0;
	// ticket 0652: constants drawn through a wrap copy (k = -1 / +1), and those that fit no copy (drawn nothing this frame)
	uint32_t wrapConsts = 0, unfitConsts = 0;
	uint32_t envUnknown = 0;   // t2-t4 identities no install cube holds (total so far)
	// ticket 0709: the scene pass's kept instances (CulledKeep), the candidates looked at, the sightings remembered
	uint32_t keptInst = 0, keepCandidates = 0, keepEntries = 0;
	// ticket 0789 (this pass): the kept by rule (KeepStats), the camera still (P = C), the tap drawn was cut, the R5
	// records of the drawn tap and their instances; the store's totals (removed, evicted, the entries' payload bytes, the
	// constant pool's blocks) of this pass
	uint32_t keptReentry = 0, keptAbsent = 0, keptCut = 0;
	bool sameCamera = false, tapCut = false;
	uint32_t handOverRecs = 0, handOverInst = 0;
	uint64_t storeRemoved = 0, storeEvicted = 0, storeBytes = 0;
	uint32_t storePool = 0;
	// ticket 0789, the residual flash (scene pass, totals): when tap P arrives after frame P was drawn from tap P - 1, its
	// DROPPED instances whose origin was on screen at frame P's camera and that frame P did not draw (missed: the EXE did
	// not draw them either), of those the ones the store held (missedKnown: a rule refused them; 0 is the store's aim), and
	// the frames with any missed
	uint64_t missed = 0, missedKnown = 0, missedFrames = 0, missedChecked = 0;
};

// Ticket 0789 (R5, architect-2026-11-25-retained-world.md 6.3): one identity set the server published, by key: the key's
// bits and class as the compositor reads them (VPModelSetKey). The compositor applies a new set at the Present that ends
// frame C, before it publishes tap C (shell/composite/unit_filter.cpp ModelOnPresent), so tap C's header setGeneration is
// the set the drops of frame P = C + 1 use [C, code read 2026-11-26].
struct OwnedKeys
{
	std::unordered_map<uint64_t, VPModelSetKey> keys;   // VPModelHash64 of the VPModelKey -> the set's entry
	const VPModelSetKey* find(const VPModelKey& k) const
	{
		const auto it = keys.find(VPModelHash64(&k, sizeof(k)));
		return it != keys.end() && !memcmp(&it->second.key, &k, sizeof(k)) ? &it->second : nullptr;
	}
};
// What the compositor drops at the frame's Present, as the server knows it (ModelLayer::setOwnership)
struct ModelOwnership
{
	bool live = false;    // a mode block (server.exe); false (the offline player, tests): the tap's latch, the keys seen dropped
	uint32_t bits = 0;    // live: the server's mode bits (the latch word it writes)
	std::vector<std::pair<uint32_t, std::shared_ptr<const OwnedKeys>>> sets;   // the last published generations, newest last
};
const size_t kOwnedSetHistory = 8;

// Ticket 4888: one CPU-built buffer as the layer holds it (kind VPMGEOM_KIND_INDEX32 / _VERTEX, the content hash it is held
// under (VPModelGeomHash), the vertex stride or 4, the bytes): the scene bundle's model_geom member carries a list of them.
struct GeomBuffer
{
	uint32_t kind = 0;
	uint64_t hash = 0;
	uint32_t stride = 0;
	std::vector<uint8_t> bytes;
};

// ---- the layer seam ----
class ModelLayer
{
public:
	virtual ~ModelLayer() {}
	// Worker thread, once: the library, its art catalogue and the wire index from the install and the database copy.
	virtual c5r::Status open(const ModelsOptions& o, const std::wstring& dbPath) = 0;
	// Render thread: the programs and the renderer on the device; null = the device goes.
	virtual c5r::Status setDevice(ID3D11Device* dev) = 0;
	// Render thread, a budget: make more files drawable (GPU buffers, textures). True when the drawable set changed.
	virtual bool prepare(double budgetMs) = 0;
	// The keys of every drawable part (resident, buffers and textures made).
	virtual void drawableKeys(std::vector<KeyInfo>& out) const = 0;
	// The content checks and resolve of one tap frame image (a copy): Ok replaces the layer's scene.
	virtual c5r::Status accept(const uint8_t* image, size_t n) = 0;
	// Draw the scene's items of the pass (0 scene, 1 reflection) into rtv / dsv.
	virtual c5r::Status draw(ID3D11DeviceContext* ctx, int pass, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
	                         uint32_t w, uint32_t h, const FrameTextures& ft, LayerDrawStats* st) = 0;
	virtual void clearScene() = 0;   // a load begins: the old frame's items go
	// Which records accept() keeps for drawing: false = only VPMTAP_DRAW_DROPPED ones (the product), true = all (tests).
	virtual void setDrawAll(bool all) = 0;
	// Ticket 0709: the keep of the instances the EXE culled at the drawn tap's camera (CulledKeep); a layer without it
	// ignores it.
	virtual void setKeep(const KeepOptions& o) { (void)o; }
	// Ticket 0789: the identity sets and mode bits the hand-over records (R5) and the store's class check go by; called
	// before accept() whenever they change
	virtual void setOwnership(const ModelOwnership& o) { (void)o; }
	virtual std::string describe() const = 0;   // one line for the log (library counts, index, renderer)
	// ---- ticket 4879: the CPU-built meshes (srv_model_geom.h); a layer without them holds nothing ----
	// One buffer of the geometry section (kind VPMGEOM_KIND_*): Ok held; refused with the store's reason (oversize
	// Unsupported, layout or "content differs from its hash" BadLayout).
	virtual c5r::Status addGeometry(uint32_t kind, uint64_t hash, uint32_t stride, const uint8_t* p, size_t n)
	{
		(void)kind; (void)hash; (void)stride; (void)p; (void)n;
		return c5r::Status::fail(c5r::Err::Unsupported, "this layer holds no CPU-built geometry");
	}
	virtual bool holdsGeometry(uint32_t kind, uint64_t hash) const { (void)kind; (void)hash; return false; }
	virtual void releaseGeometry(uint32_t kind, uint64_t hash) { (void)kind; (void)hash; }
	virtual void heldGeometry(std::vector<std::pair<uint32_t, uint64_t>>& out) const { out.clear(); }
	// Ticket 4888: one held buffer's stride and bytes (the scene bundle's model_geom member); false when not held.
	virtual bool geometryBytes(uint32_t kind, uint64_t hash, uint32_t* stride, std::vector<uint8_t>* bytes) const
	{
		(void)kind; (void)hash; (void)stride; (void)bytes;
		return false;
	}
	// True once after the layer's CPU-built keys changed (a tap resolved a new one, or a buffer was released): the
	// identity set is built again.
	virtual bool takeCpuKeysChanged() { return false; }
	virtual std::string geometryLine() const { return std::string(); }   // " cpu ..." for the models line
};
typedef std::unique_ptr<ModelLayer> (*ModelLayerFactory)();
void RegisterModelLayerFactory(ModelLayerFactory f);   // srv_models_scene.cpp
std::unique_ptr<ModelLayer> MakeModelLayer();          // null without an adapter (no c5r::landmarks in the build)

// ---- the module seam (Scene's) ----
struct ModelsCounters
{
	int64_t polls = 0, framesAccepted = 0, framesRefused = 0, fileLoads = 0;
	int64_t setsPublished = 0, setVerdicts = 0, setRefused = 0;
	uint32_t setKeys = 0, setCities = 0, setLandmarks = 0, drawableKeys = 0;
	int64_t drawnFrames = 0, drawErrors = 0, reflDrawn = 0;
	int64_t unknownDraws = 0, envMissing = 0;
	// R1 pairing (beginFrame): taps of the frame's own Present, found after a wait, one older, one newer, none
	int64_t pairExact = 0, pairWaited = 0, pairOlder = 0, pairNewer = 0, pairNone = 0;
	int64_t maxLagPresents = 0;
	double maxPairWaitMs = 0;
	// ticket 4882: scene passes drawn at the frame's camera, and the instances' screen shift that avoided, split by the
	// pairing (the tap of the frame's own Present, or an older one)
	int64_t camFrames = 0, shiftExactN = 0, shiftOlderN = 0;
	double shiftExactMaxPx = 0, shiftOlderSumPx = 0, shiftOlderMaxPx = 0;
	uint32_t envUnknown = 0;
	// ticket 0709: scene passes that drew kept instances, the instances, the most in one pass, the sightings remembered
	int64_t keepFrames = 0, keepInst = 0;
	uint32_t keepMax = 0, keepEntries = 0;
	bool keepOn = false;
	// ticket 0789: the store (both passes unless named): kept by rule; frames whose store bridged what tap C should have
	// held (absent or cut), of those at a still camera (R3: LOUD), the reflection's kept instances; R3 removals and
	// evictions; R5 frames, records and instances; the entries and payload bytes (scene + reflection), the pool's blocks
	bool storeOn = false;
	int64_t storeReentry = 0, storeAbsent = 0, storeCut = 0, bridgedFrames = 0, stillBridged = 0, reflKept = 0;
	uint64_t storeRemoved = 0, storeEvicted = 0, storeBytes = 0;
	uint32_t reflEntries = 0, storePool = 0;
	int64_t handOverFrames = 0, handOverRecs = 0, handOverInst = 0;
	// ticket 0789: the drawn and the accepted taps' headers (Architect section 9's counters for 0788): accepted taps that
	// were cut (truncated > 0), pairings that skipped a cut tap for an older complete one, setGeneration and latchedBits
	// changes between accepted taps, passed records the taps held NOT_IN_SET / BIT_OFF (all of them, any key)
	int64_t tapsCut = 0, pairSkippedCut = 0, genChanges = 0, latchChanges = 0, passedNotInSet = 0, passedBitOff = 0;
	// ticket 0789: the residual flash (LayerDrawStats::missed ...): checked frames, missed instances, of them held by the
	// store, frames with any missed
	uint64_t missedChecked = 0, missed = 0, missedKnown = 0, missedFrames = 0;
	bool layerOpen = false, deviceOk = false, setAcked = false;
};

class ModelsScene
{
public:
	virtual ~ModelsScene() {}
	virtual void configure(const ModelsOptions& o) = 0;
	virtual void setLayer(std::unique_ptr<ModelLayer> layer) = 0;   // tests: a layer of their own (before the first poll)
	virtual void attachMode(VPShellMode* block) = 0;
	virtual void setExePid(uint32_t pid) = 0;
	virtual void setDatabase(const std::wstring& path, uint32_t gen) = 0;
	virtual void setDevice(ID3D11Device* dev) = 0;
	// True when a tap frame was accepted whose bytes differ from the previous one (the scene draws again).
	virtual bool poll(int64_t qpc) = 0;
	virtual void idle(double budgetMs) = 0;
	virtual void onLoadBegin(int64_t qpc) = 0;
	// Before the frame's draws (the reflection is drawn before the scene pass): the tap of the frame's Present
	// (SceneFrame::cam.frameStamp; 0 = no pairing, the newest: the offline player, tests)
	virtual void beginFrame(int64_t presentCount) = 0;
	virtual void drawScene(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	                       uint32_t h, const FrameTextures& ft) = 0;
	virtual void drawReflection(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	                            uint32_t h, const FrameTextures& ft) = 0;
	virtual void frameDone(int64_t qpc) = 0;   // after the frame's draws: the mode writer's frame rule
	virtual bool failed(std::string& why) const = 0;
	virtual bool on() const = 0;
	virtual uint32_t modeBits() const = 0;
	virtual void line(char* out, size_t n) const = 0;
	// Ticket 0789: the model store's line ("models store: ..."), printed after line(); "" without a store
	virtual void storeLine(char* out, size_t n) const
	{
		if(n)
			out[0] = 0;
	}
	virtual ModelsCounters counters() const = 0;
	// the bundle member (modeltap/frame<NNNNNN>.mtap): the current image, its hash (0 = none), a file image replayed
	virtual uint64_t imageHash() const = 0;
	virtual c5r::Status writeCurrentImage(const std::wstring& path) const = 0;
	virtual c5r::Status loadImage(const uint8_t* image, size_t n) = 0;
	// ticket 4883: the CPU-built meshes (ticket 4879's geometry section) the layer holds now. Ticket 4888: the scene recorder
	// writes them as the member model_geom (modelgeom/frame<NNNNNN>.mgeo) at the span's first frame and at every frame where
	// the held set changed; it notes the gap (sb::kModelGeomNote) only when such a member cannot be written.
	virtual size_t heldGeometryCount() const { return 0; }
	// Ticket 4888: the held buffers with their bytes, by (kind, hash) ascending; heldSetHash() a hash of the (kind, hash)
	// list only (0 when none is held), so the recorder sees a change without copying bytes. loadGeometry (the offline
	// player, before the frame's tap is handed over): the layer then holds exactly `in` (buffers not in it are released, the
	// others added through ModelLayer::addGeometry's checks); Ok, or the first refusal (nothing more is added after it).
	virtual void geometrySnapshot(std::vector<GeomBuffer>& out) const { out.clear(); }
	virtual uint64_t heldSetHash() const { return 0; }
	virtual c5r::Status loadGeometry(const std::vector<GeomBuffer>& in)
	{
		(void)in;
		return c5r::Status::fail(c5r::Err::Unsupported, "this model module holds no CPU-built geometry");
	}
};
typedef std::unique_ptr<ModelsScene> (*ModelsSceneFactory)();
void RegisterModelsSceneFactory(ModelsSceneFactory f);   // srv_render_scene.cpp (always linked)
std::unique_ptr<ModelsScene> MakeModelsScene();          // null without this folder

const int kModelsFatalExitCode = 5;   // the route of D125, as the units'

}  // namespace srvmodels
