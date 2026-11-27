// srv_tile_source.h - where the terrain scene inside server.exe gets its baked tile caches (epic 3300 / epic 3400 seam).
//
// PROPOSAL by the team-3300 lead (2026-11-13), sent to team 3400 through the MC; not yet agreed. Until it is, only the
// first implementation below exists. Design: research/05-graphics-composition/renderer-in-server-design.md section 6.
//
// The scene (issue 3301, server/src/render/) owns the c5r::terrain::Renderer, the height field and the camera; a
// TileCacheSource owns which tiles have caches and hands them to the renderer with Renderer::setTileCache. Two
// implementations:
//   * the EXE-cache tap (epic 3200's bake tap, VPShellBakeTap in shell_shared.h): the caches the EXE baked, opened as
//     shared textures (3301 ports shell_server.cpp's BakePoll; the test reference of D81 point 3);
//   * the server's own baker and residency (epic 3400: c5r::bake::TileBaker, all land tiles resident by default, the
//     BakedTileCameraRadius option of VP64Options.ini, re-bake on content change). Team 3400 writes it in render/ as a
//     library, so the same code serves shell_server.exe and server.exe without a second copy.
// Every call is made on the render thread, with the render thread's device and immediate context. No DLL access.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace c5r {
namespace terrain { class Renderer; }
namespace heightfield { struct HeightGrid; }
}  // namespace c5r

namespace srvrender {

// What the camera sees this frame, for priorities (tiles in view first) and the radius option.
struct ViewInfo
{
	int64_t frameId = 0;
	float camWorld[16] = {};          // VPShellCamera::camWorld (rows right, up, back, position)
	float view[16] = {}, proj[16] = {};
	uint32_t width = 0, height = 0;   // target size
};

class TileCacheSource
{
public:
	virtual ~TileCacheSource() {}
	virtual const char* name() const = 0;
	// A new height field (a load) or the same one after a device rebuild; the source drops what belongs to the old one.
	virtual void onField(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid, uint32_t tilePlots, uint32_t loadSeq) = 0;
	// Plots whose content changed (from the event feed: routes, improvements, features, resources): their tiles are
	// re-baked by a source that bakes; the tap ignores it (the EXE re-bakes and the tap follows).
	virtual void onPlotsChanged(const uint32_t* plotIndices, size_t count) = 0;
	// Before each draw: hand new or changed caches to the renderer (setTileCache). Must stay inside budgetMs of CPU.
	virtual void beforeDraw(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double budgetMs) = 0;
	// After the frame was published, while the render thread would otherwise wait: background work (bake-ahead), at
	// most budgetMs. The host passes 0 when a newer camera is already waiting.
	virtual void idle(ID3D11DeviceContext* ctx, double budgetMs) = 0;
	// Device lost: release every D3D object; onField() follows with the new device.
	virtual void release() = 0;
	// One line for the host's per-second log (tiles resident, baked, pending, video memory).
	virtual void statsLine(char* out, size_t n) = 0;
	// The tile size (plots per tile edge) the source needs the field's tiles in, 0 = any (issue 3306: replaces the
	// EXE-cache tap's constructor callback). The scene asks after every poll; when the answer differs from the field's tile
	// size it rebuilds the tiles from its kept field and calls onField() with the new size. A fixed size of the
	// configuration (server.cfg render_tile_plots) wins: a source that cannot work with it refuses its caches and says so.
	virtual uint32_t wantedTilePlots() const { return 0; }
};

}  // namespace srvrender
