// srv_extra_visible_outline.h - the extra VisibleAreaOutline ([Extras] VisibleAreaOutline, a visual addition, default 0):
// a thin ribbon on the ground along every hex edge between a plot the active team sees now and a neighbour it has revealed but does not see now.
// The contribution guide's worked example (ticket 4938, epic 0454; research/05-graphics-composition/extras.md section X10).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input: the fog scene's per-plot states (FowScene::states(): visible, seen, unexplored, of the active team, kept from
// the feed's kind 3 kFowChanged and its plot table) and the scene's height field. Display only: it reads nothing else
// and writes nothing but its own draw into the scene's colour target (E1, E7, E11).
//
// Three parts, each testable alone:
//   BuildOutline     the CPU geometry: states + height field -> ribbon triangles (no device)
//   OutlineRenderer  the GPU pass: its own small shaders, blend and depth states, a dynamic vertex buffer
//   VisibleOutline   the extra: its effective state, the rebuild on a new fog generation, the fallbacks (E6) and its
//                    one log line per cause
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "c5r/fow/fow.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace srvextras {

struct OutlineVertex   // 16 bytes: world position and the ribbon's cross coordinate (-1 one side, +1 the other)
{
	float x, y, z, side;
};

struct OutlineParams
{
	float halfWidth = 1.2f;   // world units either side of the edge (a hex is about 64 wide)
	float lift = 0.75f;       // world units above the height field's sample, against the terrain's tessellation
	int segments = 4;         // pieces per hex edge, each with its own ground heights (1..16)
};

struct OutlineStats
{
	uint32_t visible = 0;     // visible plots
	uint32_t edges = 0;       // edges drawn (one per visible plot and seen neighbour)
	uint32_t borderSkips = 0; // neighbours outside a map that does not wrap, or outside its rows (no edge)
};

// The six neighbours of a hex in axial coordinates (q = x - (y >> 1), r = y), in order around it: consecutive entries
// are neighbours of each other, so the corner between direction k and k + 1 is (d[k] + d[k + 1]) / 3 from the centre.
extern const int kAxialDirs[6][2];

// The ribbon of every edge between a visible plot and a neighbour that is revealed but out of sight (seen; not an unexplored one, which the fog's clouds cover), as a
// triangle list (6 vertices per segment). states: w x h entries in plot order (index y * w + x); the grid's mapW must be
// w and its mapH h + 4 (its 4 margin rows). Plot (x, y) sits at grid plot (x, y) with the grid's own placement
// (hexToWorld, sx, sy, ox, oy), as the fog and the arc place it. A map that wraps (grid.wrapX) joins column w - 1 to 0;
// the ribbon is built at the visible plot's own place (the drawer repeats it one world width either side).
// Errors (out and stats left empty): BadLayout for a null table with w * h > 0, w or h 0, a grid whose size, samples or
// placement do not fit, params outside their ranges.
c5r::Status BuildOutline(const c5r::heightfield::HeightGrid& g, const c5r::fow::PlotState* states, uint32_t w, uint32_t h,
                         const OutlineParams& p, std::vector<OutlineVertex>& out, OutlineStats* stats);

// The world width a wrapping map repeats at (samplesX - 1) * sx, as the arc's c5r::arc::worldWidth; 0 when it does not wrap.
float OutlineWrapWidth(const c5r::heightfield::HeightGrid& g);

// The GPU pass. Colour: straight alpha, blended over the scene (SRC_ALPHA, INV_SRC_ALPHA) with the destination's alpha
// kept (the server's picture is composited by its alpha); depth test LESS_EQUAL against the scene's depth, no depth
// write; no culling.
class OutlineRenderer
{
public:
	OutlineRenderer();
	~OutlineRenderer();
	OutlineRenderer(const OutlineRenderer&) = delete;
	OutlineRenderer& operator=(const OutlineRenderer&) = delete;
	// The shaders (compiled from source with D3DCompile) and the states on this device. Device on failure.
	c5r::Status init(ID3D11Device* dev);
	void release();
	bool ready() const { return m_vs != nullptr; }
	// The vertices for the next draws (the buffer grows as needed). OutOfMemory / Device on failure.
	c5r::Status upload(ID3D11DeviceContext* ctx, const std::vector<OutlineVertex>& v);
	// One draw per x offset (0, and +-wrapWidth when it is > 0) with viewProj (row vectors, clip = (x, y, z, 1) M) into
	// colour and depth (depth may be null: no depth test), viewport w x h. The context's state is saved and put back.
	c5r::Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	                 const float viewProj[16], float wrapWidth, const float rgba[4]);
	uint32_t vertexCount() const { return m_count; }

private:
	ID3D11VertexShader* m_vs = nullptr;
	ID3D11PixelShader* m_ps = nullptr;
	ID3D11InputLayout* m_layout = nullptr;
	ID3D11Buffer* m_vb = nullptr;
	ID3D11Buffer* m_cb = nullptr;
	ID3D11BlendState* m_blend = nullptr;
	ID3D11DepthStencilState* m_depth = nullptr;
	ID3D11DepthStencilState* m_noDepth = nullptr;
	ID3D11RasterizerState* m_raster = nullptr;
	uint32_t m_capacity = 0;   // vertices the buffer holds
	uint32_t m_count = 0;      // vertices uploaded
};

// What the extra is given each frame.
struct OutlineFrame
{
	const c5r::heightfield::HeightGrid* grid = nullptr;   // null: no height field yet
	const c5r::fow::PlotState* states = nullptr;          // null: the fog is not running
	uint32_t w = 0, h = 0;                                // the states' table
	uint32_t fogGeneration = 0;                           // rises with every change of a state
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	uint32_t width = 0, height = 0;
	float viewProj[16] = {};
};

typedef void (*OutlineLogFn)(void* ctx, const char* line);

// The extra itself. Every call on the render thread.
class VisibleOutline
{
public:
	// The input that is missing or lost (E6), for its one log line and the counters
	enum Wait { kWaitNone = 0, kWaitFog, kWaitGrid, kWaitTable, kWaitCount };

	explicit VisibleOutline(OutlineLogFn log = nullptr, void* logCtx = nullptr);
	// The effective state from the options (Vp64Opt::Extra(o, kVisibleAreaOutline)) and whether the scene runs the fog at
	// all (render_fow): off with the fog off is off for the session, with one line.
	void configure(bool extraOn, const char* whyOff, bool fogConfigured);
	bool on() const { return m_on && !m_failed; }
	bool failed() const { return m_failed; }
	c5r::Status initDevice(ID3D11Device* dev);   // a failure switches the extra off for the session (one line)
	void releaseDevice();
	// One frame: rebuild on a new fog generation or a new grid, then draw. Nothing when off; an input missing draws
	// nothing (one line when it goes missing, one when it is back); a draw error switches the extra off for the session.
	void frame(ID3D11DeviceContext* ctx, const OutlineFrame& f);
	void totalsLine(char* out, size_t n) const;

	OutlineParams params;
	float rgba[4] = {1.0f, 0.93f, 0.6f, 0.7f};   // a light warm line, 70 % opaque

	// counters
	int64_t frames = 0, drawn = 0, rebuilds = 0, waits[kWaitCount] = {}, drawErrors = 0, buildErrors = 0;
	uint32_t lastEdges = 0;
	double lastBuildMs = 0;   // the last BuildOutline's CPU time (QueryPerformanceCounter)

private:
	void say(const char* fmt, ...);
	void fail(const std::string& why);
	OutlineLogFn m_log;
	void* m_logCtx;
	bool m_on = false, m_failed = false;
	Wait m_wait = kWaitNone;
	OutlineRenderer m_r;
	bool m_haveDevice = false;
	bool m_built = false, m_uploaded = false;
	uint32_t m_gen = 0;
	const c5r::heightfield::HeightGrid* m_grid = nullptr;
	uint64_t m_fieldKey = 0;   // FieldKey of the grid the geometry was built from
	std::vector<OutlineVertex> m_verts;
};

}  // namespace srvextras
