// srv_unit_shadow.h - the server's units cast stock's shadow into the tapped shadow map (epic 0042, ticket 4785; route (b)
// with (a)'s drop of ticket 4784: research/05-graphics-composition/unit-combat-server.md sections 9 and 13). Written
// 2026-11-20 for this repository (GPL-3.0-or-later).
//
// What it does, per drawn frame whose tapped slot says the compositor dropped the EXE's unit mask draws
// (VPSHD_SLOT_UNIT_MASK_DROPPED, shell/common/shell_shadow_tap.h): the caller's unit mask pass (the unit module's ShadowMask
// pass with the slot's ShadowMatrix0, SceneUnits::drawMask) draws into a ShadowRaw-sized target, and the result is merged
// into a copy of the tapped map, which the terrain (t3) and the water (t13) then sample instead of the tap's own texture:
//   quality 3 / 2 (the tapped map is ShadowBlur, R16_UNORM 2048 / 1024): the raw target (rawW x rawH = the EXE's back buffer,
//     R16G16B16A16_FLOAT) is cleared to 0, the mask drawn into it, then resampled with the EXE's own ShadowShader programs
//     (found by SHA-256 in the install EXE's .shader section at run time: nothing of the EXE is in this repository, D83) and
//     added ONE / ONE onto the copy (R16_UNORM saturates): min(1, tap + R(units)), equal to stock's min(1, R(blocks +
//     landmarks + units)) except on the edge ring where a unit's mask meets a city block's (section 9.2).
//   quality 1 (the tapped map is ShadowRaw itself, R16G16B16A16_FLOAT, unresampled): the mask is drawn straight onto the
//     copy with the module's own state (blend off): r = 1 under every unit, as stock's (units write r = 1; a block writes 1; a
//     landmark's blend keeps 1), and the terrain reads saturate(t3.r) [C, 9.2].
//   quality 0: nothing (no texture, strength 0).
// The resample: one indexed quad (six indices) over the target, cb1 = (1/W, 1/H, -1/W, -1/H) of the raw size in VS and PS
// slot 1, sampler MIN_MAG_MIP_LINEAR / WRAP in slot 0, the raw target in PS t0 (9.2 R1: the mean of eight bilinear alpha
// samples). The quad's layout: POSITION R32G32B32 at 0, NORMAL R32G32B32 at 12, TEXCOORD R32G32 at 24, stride 32; corners
// (+-1, +-1, 0), uv (0.5 x + 0.5, 0.5 - 0.5 y), indices (2, 1, 0, 3, 1, 2).
// Failures: init (the programs not in the EXE, a device call failed) and a target that cannot be made are the D118 path:
// the scene reports them through Scene::shadowFailed (the host's ShadowFatalStep, D125's route); a frame the module cannot
// serve for a passing reason (no mask callback, a slot without texture) returns null and the caller binds the tap's map.
#pragma once
#include "srv_render_common.h"   // windows.h with NOMINMAX first
#include <d3d11.h>
#include <stdint.h>
#include <functional>
#include <string>

namespace srvrender {

// SHA-256 of the EXE's ShadowShader pass-0 programs (material 70) as the 3907 exports hold them (digests only, D83)
extern const char* const kUnitShadowResampleVsSha;
extern const char* const kUnitShadowResamplePsSha;

// The per-frame decision (pure; the scene's rule, tested on its own). Returns true when the frame's tapped map is to be
// replaced by the merged one: the switch on (render_units_shadow=1), the unit scene present, the slot carries the drop flag
// and a texture (quality 1-3) and a raw size. Otherwise the tap's own map is bound (today's picture), and *why (when given)
// names the reason in a few words.
bool UnitShadowWanted(bool optOn, bool unitsOn, bool slotFlag, uint32_t quality, bool haveSrv, uint32_t rawW, uint32_t rawH,
                      const char** why);

class UnitShadow
{
public:
	typedef std::function<void(ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h)> MaskFn;
	~UnitShadow() { release(); }
	// The programs from the install EXE (read-only), the quad, the states. false + why: the D118 path (the caller reports it).
	bool init(ID3D11Device* dev, const std::wstring& exePath, std::string& why);
	// Tests: the same with the programs' bytecode handed in (null: not found).
	bool initWith(ID3D11Device* dev, const void* vs, size_t vsBytes, const void* ps, size_t psBytes, std::string& why);
	bool ready() const { return m_vs != nullptr; }
	// The merged map's srv for this frame, or null (why says it): tapSrv the tapped slot's texture, quality its quality,
	// rawW x rawH the EXE's ShadowRaw size of that frame, drawMask the unit mask pass. *fatal is set when the failure is the
	// D118 path (a target could not be made), else left as it is.
	ID3D11ShaderResourceView* apply(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* tapSrv, uint32_t quality, uint32_t rawW,
	                                uint32_t rawH, const MaskFn& drawMask, std::string& why, bool* fatal);
	// Ticket 0753 (J3): the merged map's srv the last apply() made (null before the first); apply() returns it again while the
	// tap's description stays, so record ahead predicts the terrain's t3 with it and checks the prediction after apply()
	ID3D11ShaderResourceView* mergedSrv() const { return m_outSrv; }
	// The resample alone: rawSrv (rawW x rawH) into rtv (outW x outH), blend ONE / ONE when add, else off. Tests use it
	// against captured pairs; apply() calls it.
	void resample(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* rawSrv, uint32_t rawW, uint32_t rawH, ID3D11RenderTargetView* rtv,
	              uint32_t outW, uint32_t outH, bool add);
	void release();

	// counters (render thread)
	uint64_t applied = 0;        // frames merged
	uint64_t appliedQ1 = 0;      // ... of them at quality 1 (drawn onto the float copy)
	uint64_t refused = 0;        // frames apply() returned null
	uint64_t targetsMade = 0;    // target sets made (a size or quality change makes them again)
	double lastCpuMs = 0, maxCpuMs = 0, sumCpuMs = 0;
	uint32_t rawW() const { return m_rawW; }
	uint32_t rawH() const { return m_rawH; }

private:
	bool makeStatic(std::string& why);
	bool makeTargets(const D3D11_TEXTURE2D_DESC& tap, uint32_t rawW, uint32_t rawH, bool q1, std::string& why);
	void releaseTargets();

	ID3D11Device* m_dev = nullptr;
	ID3D11VertexShader* m_vs = nullptr;
	ID3D11PixelShader* m_ps = nullptr;
	ID3D11InputLayout* m_il = nullptr;
	ID3D11Buffer *m_vb = nullptr, *m_ib = nullptr, *m_cb = nullptr;
	ID3D11SamplerState* m_samp = nullptr;
	ID3D11BlendState *m_add = nullptr, *m_off = nullptr;
	ID3D11RasterizerState* m_rs = nullptr;
	ID3D11DepthStencilState* m_ds = nullptr;
	// the raw target (quality 2 / 3)
	ID3D11Texture2D* m_raw = nullptr;
	ID3D11RenderTargetView* m_rawRtv = nullptr;
	ID3D11ShaderResourceView* m_rawSrv = nullptr;
	uint32_t m_rawW = 0, m_rawH = 0;
	// the merged map (the tap's description)
	ID3D11Texture2D* m_out = nullptr;
	ID3D11RenderTargetView* m_outRtv = nullptr;
	ID3D11ShaderResourceView* m_outSrv = nullptr;
	D3D11_TEXTURE2D_DESC m_outDesc = {};
	float m_cbVals[4] = {0, 0, 0, 0};
};

}  // namespace srvrender
