// srv_exe_cache_tap.h - the first srvrender::TileCacheSource: the EXE's own tile caches through the bake tap (epic 3200's
// VPShellBakeTap in shell_shared.h), for the renderer inside server.exe (epic 3300, issue 3301).
//
// Origin (COMMON 13, D22): BakePoll, OpenCacheTexture, TypedViewFormat and ReleaseSlots of shell/server/shell_server.cpp
// at this repository's commit b4f7748 (file last changed in ff223fd); that file is unchanged (team 3400 owns it).
// Contract: server/src/render/srv_tile_source.h (the lead's proposal to team 3400).
#pragma once
#include "srv_render_common.h"
#include "srv_tile_source.h"
#include "c5r/terrain/terrain.h"
#include <d3d11_1.h>
#include <functional>
#include <vector>

namespace srvrender {

class ExeCacheTap : public TileCacheSource
{
public:
	// hdr: the version-3 section (its bake-tap block); fixedTilePlots: server.cfg render_tile_plots (0 = follow the tap).
	// The bake tap's tile size reaches the scene through wantedTilePlots() (issue 3306; was a constructor callback).
	ExeCacheTap(VPShellHeader* hdr, uint32_t fixedTilePlots) : m_hdr(hdr), m_fixedTilePlots(fixedTilePlots) {}
	~ExeCacheTap() override { release(); }
	const char* name() const override { return "exe-cache-tap"; }
	void onField(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid, uint32_t tilePlots, uint32_t loadSeq) override;
	void onPlotsChanged(const uint32_t* plotIndices, size_t count) override;
	void beforeDraw(ID3D11DeviceContext* ctx, c5r::terrain::Renderer& r, const ViewInfo& view, double budgetMs) override;
	void idle(ID3D11DeviceContext* ctx, double budgetMs) override;
	void release() override;
	void statsLine(char* out, size_t n) override;
	uint32_t wantedTilePlots() const override { return m_wantTilePlots; }

	// ---- beyond srv_tile_source.h ----
	// Every loop iteration (also when nothing is drawn): a new manifest is applied at once. True when caches changed
	// (the scene draws again with its last camera).
	bool poll(c5r::terrain::Renderer& r);
	void setFieldPresent(bool present) { m_haveField = present; }
	int64_t cacheOpened = 0, cacheMissing = 0, cacheRefused = 0, manifestErrors = 0, manifestsApplied = 0, plotsChanged = 0;

private:
	struct Slot
	{
		ID3D11Texture2D* tex[2] = {};
		ID3D11ShaderResourceView* srv[2] = {};
		uint32_t generation = 0;
		bool opened = false, applied = false;
		uint32_t tileX = 0, tileY = 0;
	};
	bool openCacheTexture(const wchar_t* name, ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv);
	void releaseSlots();
	void unmapManifest();

	VPShellHeader* m_hdr = nullptr;
	uint32_t m_fixedTilePlots = 0;
	uint32_t m_wantTilePlots = 0;   // the bake tap's tile size while the field's differs (wantedTilePlots)
	ID3D11Device* m_dev = nullptr;
	ID3D11Device1* m_dev1 = nullptr;
	bool m_haveField = false;
	uint32_t m_fieldTilePlots = 0;
	HANDLE m_manSec = nullptr;
	const uint8_t* m_manView = nullptr;
	size_t m_manSize = 0;
	wchar_t m_manName[64] = {};
	uint32_t m_manExePid = 0;
	uint32_t m_appliedManifestSeq = 0;
	bool m_manApplied = false;
	bool m_manRefusedLogged = false;
	uint32_t m_loggedState = 0xFFFFFFFFu;
	std::vector<Slot> m_slots;
	const char* m_lastErr = "";
	HRESULT m_lastHr = 0;
};

}  // namespace srvrender
