// srv_unit_shadow.cpp - the server's units cast stock's shadow into the tapped shadow map (epic 0042, ticket 4785;
// srv_unit_shadow.h). The resample's behaviour is research/05-graphics-composition/unit-combat-server.md 9.2 R1; its
// programs are the EXE's, found in the install's .shader section by their SHA-256 at run time (D83). Written 2026-11-20
// for this repository (GPL-3.0-or-later).
#include "srv_unit_shadow.h"
#include <cstring>
#include "c5r/shaders/shadersection.h"
#include "c5r/units/draw.h"
#include "srv_render_common.h"

namespace srvrender {

const char* const kUnitShadowResampleVsSha = "c7615a1db4f3731cbeb1ad3067e2427b3037f1e3a3517db71826829abf55302e";
const char* const kUnitShadowResamplePsSha = "be955cbb6b767348a7de81895c3262da5c393546eb5de163169a2ff40303effa";

namespace {
const DXGI_FORMAT kRaw = DXGI_FORMAT_R16G16B16A16_FLOAT;
const DXGI_FORMAT kBlur = DXGI_FORMAT_R16_UNORM;
}  // namespace

bool UnitShadowWanted(bool optOn, bool unitsOn, bool slotFlag, uint32_t quality, bool haveSrv, uint32_t rawW, uint32_t rawH,
                      const char** why)
{
	const char* w = nullptr;
	if(!optOn)
		w = "render_units_shadow=0";
	else if(!unitsOn)
		w = "no unit scene";
	else if(!slotFlag)
		w = "the EXE cast its units (slot flag clear)";
	else if(quality == 0 || !haveSrv)
		w = "quality 0 (no texture)";
	else if(quality > 3)
		w = "quality out of range";
	else if(!rawW || !rawH)
		w = "no ShadowRaw size in the slot";
	if(why)
		*why = w ? w : "";
	return w == nullptr;
}

void UnitShadow::releaseTargets()
{
	SafeRelease(m_rawSrv);
	SafeRelease(m_rawRtv);
	SafeRelease(m_raw);
	SafeRelease(m_outSrv);
	SafeRelease(m_outRtv);
	SafeRelease(m_out);
	m_rawW = m_rawH = 0;
	memset(&m_outDesc, 0, sizeof(m_outDesc));
}

void UnitShadow::release()
{
	releaseTargets();
	SafeRelease(m_vs);
	SafeRelease(m_ps);
	SafeRelease(m_il);
	SafeRelease(m_vb);
	SafeRelease(m_ib);
	SafeRelease(m_cb);
	SafeRelease(m_samp);
	SafeRelease(m_add);
	SafeRelease(m_off);
	SafeRelease(m_rs);
	SafeRelease(m_ds);
	m_dev = nullptr;
}

bool UnitShadow::init(ID3D11Device* dev, const std::wstring& exePath, std::string& why)
{
	release();
	c5r::shaders::ShaderSection sec;
	const c5r::Status s = sec.open(exePath);
	if(!s.ok())
	{
		why = "unit shadow: the EXE's .shader section could not be read: " + s.str();
		return false;
	}
	const c5r::shaders::ShaderBlob *vs = nullptr, *ps = nullptr;
	for(const c5r::shaders::ShaderBlob& b : sec.blobs())
	{
		if(vs && ps)
			break;
		const std::string h = c5r::units::sha256Hex(b.bytes, b.size);
		if(!vs && h == kUnitShadowResampleVsSha)
			vs = &b;
		else if(!ps && h == kUnitShadowResamplePsSha)
			ps = &b;
	}
	return initWith(dev, vs ? vs->bytes : nullptr, vs ? vs->size : 0, ps ? ps->bytes : nullptr, ps ? ps->size : 0, why);
}

bool UnitShadow::initWith(ID3D11Device* dev, const void* vs, size_t vsBytes, const void* ps, size_t psBytes, std::string& why)
{
	release();
	if(!dev)
	{
		why = "unit shadow: no device";
		return false;
	}
	if(!vs || !ps)
	{
		why = std::string("unit shadow: the EXE's ShadowShader resample program") + (!vs && !ps ? "s (VS c7615a1d, PS be955cbb) are" : !vs ? " VS c7615a1d is" : " PS be955cbb is") +
		      " not in the EXE's .shader section";
		return false;
	}
	m_dev = dev;
	if(FAILED(dev->CreateVertexShader(vs, vsBytes, nullptr, &m_vs)) || FAILED(dev->CreatePixelShader(ps, psBytes, nullptr, &m_ps)))
	{
		why = "unit shadow: CreateVertexShader / CreatePixelShader of the ShadowShader programs failed";
		release();
		return false;
	}
	const D3D11_INPUT_ELEMENT_DESC il[] = {
	    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};
	if(FAILED(dev->CreateInputLayout(il, 3, vs, vsBytes, &m_il)))
	{
		why = "unit shadow: CreateInputLayout for the ShadowShader VS failed";
		release();
		return false;
	}
	if(!makeStatic(why))
	{
		release();
		return false;
	}
	return true;
}

bool UnitShadow::makeStatic(std::string& why)
{
	ID3D11Device* dev = m_dev;
	// the quad (9.2): corners (+-1, +-1, 0), the normal = the position (not read), uv (0.5 x + 0.5, 0.5 - 0.5 y)
	const float v[4][8] = {{1, 1, 0, 1, 1, 0, 1, 0}, {-1, 1, 0, -1, 1, 0, 0, 0}, {1, -1, 0, 1, -1, 0, 1, 1}, {-1, -1, 0, -1, -1, 0, 0, 1}};
	const uint32_t idx[6] = {2, 1, 0, 3, 1, 2};
	D3D11_BUFFER_DESC bd = {};
	D3D11_SUBRESOURCE_DATA sd = {};
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.ByteWidth = sizeof(v);
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	sd.pSysMem = v;
	if(FAILED(dev->CreateBuffer(&bd, &sd, &m_vb)))
	{
		why = "unit shadow: CreateBuffer (quad vertices) failed";
		return false;
	}
	bd.ByteWidth = sizeof(idx);
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	sd.pSysMem = idx;
	if(FAILED(dev->CreateBuffer(&bd, &sd, &m_ib)))
	{
		why = "unit shadow: CreateBuffer (quad indices) failed";
		return false;
	}
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = 16;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if(FAILED(dev->CreateBuffer(&bd, nullptr, &m_cb)))
	{
		why = "unit shadow: CreateBuffer (cb1) failed";
		return false;
	}
	D3D11_SAMPLER_DESC smp = {};
	smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	smp.ComparisonFunc = D3D11_COMPARISON_NEVER;
	smp.MaxLOD = D3D11_FLOAT32_MAX;
	if(FAILED(dev->CreateSamplerState(&smp, &m_samp)))
	{
		why = "unit shadow: CreateSamplerState failed";
		return false;
	}
	D3D11_BLEND_DESC bl = {};
	bl.RenderTarget[0].BlendEnable = TRUE;
	bl.RenderTarget[0].SrcBlend = bl.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bl.RenderTarget[0].SrcBlendAlpha = bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if(FAILED(dev->CreateBlendState(&bl, &m_add)))
	{
		why = "unit shadow: CreateBlendState (add) failed";
		return false;
	}
	bl.RenderTarget[0].BlendEnable = FALSE;
	if(FAILED(dev->CreateBlendState(&bl, &m_off)))
	{
		why = "unit shadow: CreateBlendState (off) failed";
		return false;
	}
	D3D11_RASTERIZER_DESC rs = {};
	rs.FillMode = D3D11_FILL_SOLID;
	rs.CullMode = D3D11_CULL_NONE;
	rs.DepthClipEnable = TRUE;
	if(FAILED(dev->CreateRasterizerState(&rs, &m_rs)))
	{
		why = "unit shadow: CreateRasterizerState failed";
		return false;
	}
	D3D11_DEPTH_STENCIL_DESC ds = {};   // depth off, stencil off
	if(FAILED(dev->CreateDepthStencilState(&ds, &m_ds)))
	{
		why = "unit shadow: CreateDepthStencilState failed";
		return false;
	}
	return true;
}

bool UnitShadow::makeTargets(const D3D11_TEXTURE2D_DESC& tap, uint32_t rawW, uint32_t rawH, bool q1, std::string& why)
{
	releaseTargets();
	HRESULT hr = S_OK;
	if(!q1)
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = rawW;
		d.Height = rawH;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = kRaw;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		hr = m_dev->CreateTexture2D(&d, nullptr, &m_raw);
		if(SUCCEEDED(hr))
			hr = m_dev->CreateRenderTargetView(m_raw, nullptr, &m_rawRtv);
		if(SUCCEEDED(hr))
			hr = m_dev->CreateShaderResourceView(m_raw, nullptr, &m_rawSrv);
	}
	D3D11_TEXTURE2D_DESC o = {};
	o.Width = tap.Width;
	o.Height = tap.Height;
	o.MipLevels = 1;
	o.ArraySize = 1;
	o.Format = tap.Format;
	o.SampleDesc.Count = 1;
	o.Usage = D3D11_USAGE_DEFAULT;
	o.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if(SUCCEEDED(hr))
		hr = m_dev->CreateTexture2D(&o, nullptr, &m_out);
	if(SUCCEEDED(hr))
		hr = m_dev->CreateRenderTargetView(m_out, nullptr, &m_outRtv);
	if(SUCCEEDED(hr))
		hr = m_dev->CreateShaderResourceView(m_out, nullptr, &m_outSrv);
	if(FAILED(hr))
	{
		char b[200];
		snprintf(b, sizeof(b), "unit shadow: the targets could not be made (raw %ux%u, map %ux%u format %u, hr 0x%08lX)", rawW, rawH, tap.Width,
		         tap.Height, (unsigned)tap.Format, (unsigned long)hr);
		why = b;
		releaseTargets();
		return false;
	}
	m_rawW = rawW;
	m_rawH = rawH;
	m_outDesc = o;
	targetsMade++;
	return true;
}

void UnitShadow::resample(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* rawSrv, uint32_t rawW, uint32_t rawH,
                          ID3D11RenderTargetView* rtv, uint32_t outW, uint32_t outH, bool add)
{
	const float c[4] = {1.0f / (float)rawW, 1.0f / (float)rawH, -1.0f / (float)rawW, -1.0f / (float)rawH};
	if(memcmp(c, m_cbVals, sizeof(c)) != 0)
	{
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			memcpy(m.pData, c, sizeof(c));
			ctx->Unmap(m_cb, 0);
			memcpy(m_cbVals, c, sizeof(c));
		}
	}
	ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	D3D11_VIEWPORT vp = {0, 0, (float)outW, (float)outH, 0, 1};
	ctx->RSSetViewports(1, &vp);
	ctx->RSSetState(m_rs);
	ctx->OMSetDepthStencilState(m_ds, 0);
	const float bf[4] = {0, 0, 0, 0};
	ctx->OMSetBlendState(add ? m_add : m_off, bf, 0xFFFFFFFFu);
	const UINT stride = 32, off = 0;
	ctx->IASetVertexBuffers(0, 1, &m_vb, &stride, &off);
	ctx->IASetIndexBuffer(m_ib, DXGI_FORMAT_R32_UINT, 0);
	ctx->IASetInputLayout(m_il);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->VSSetShader(m_vs, nullptr, 0);
	ctx->PSSetShader(m_ps, nullptr, 0);
	ctx->VSSetConstantBuffers(1, 1, &m_cb);
	ctx->PSSetConstantBuffers(1, 1, &m_cb);
	ctx->PSSetShaderResources(0, 1, &rawSrv);
	ctx->PSSetSamplers(0, 1, &m_samp);
	ctx->DrawIndexed(6, 0, 0);
	ctx->PSSetShaderResources(0, 1, nullSrv);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);
	ctx->OMSetBlendState(nullptr, bf, 0xFFFFFFFFu);
}

ID3D11ShaderResourceView* UnitShadow::apply(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* tapSrv, uint32_t quality, uint32_t rawW,
                                            uint32_t rawH, const MaskFn& drawMask, std::string& why, bool* fatal)
{
	const int64_t q0 = Qpc();
	if(!ready() || !ctx || !tapSrv || !drawMask || !rawW || !rawH || quality < 1 || quality > 3)
	{
		refused++;
		why = !ready() ? "not initialised" : !tapSrv ? "no tapped texture" : !drawMask ? "no mask pass" : "quality or raw size out of range";
		return nullptr;
	}
	ID3D11Resource* res = nullptr;
	tapSrv->GetResource(&res);
	ID3D11Texture2D* tap = nullptr;
	if(res)
		res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tap);
	SafeRelease(res);
	if(!tap)
	{
		refused++;
		why = "the tapped srv is not a Texture2D";
		return nullptr;
	}
	D3D11_TEXTURE2D_DESC td;
	tap->GetDesc(&td);
	const bool q1 = quality == 1;
	// the description the quality makes (srv_shadow_tap.cpp checked the slot's; a mismatch here is refused, never drawn)
	const bool descOk = td.MipLevels == 1 && td.ArraySize == 1 && (q1 ? (td.Format == kRaw && td.Width == rawW && td.Height == rawH) : td.Format == kBlur);
	if(!descOk)
	{
		SafeRelease(tap);
		refused++;
		char b[160];
		snprintf(b, sizeof(b), "the tapped map %ux%u format %u does not fit quality %u with ShadowRaw %ux%u", td.Width, td.Height,
		         (unsigned)td.Format, quality, rawW, rawH);
		why = b;
		return nullptr;
	}
	const bool haveRaw = m_raw != nullptr;
	if(!m_out || td.Width != m_outDesc.Width || td.Height != m_outDesc.Height || td.Format != m_outDesc.Format ||
	   (!q1 && (!haveRaw || rawW != m_rawW || rawH != m_rawH)) || (q1 && haveRaw))
	{
		if(!makeTargets(td, rawW, rawH, q1, why))
		{
			SafeRelease(tap);
			refused++;
			if(fatal)
				*fatal = true;
			return nullptr;
		}
	}
	ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
	ctx->PSSetShaderResources(0, 1, nullSrv);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);
	ctx->CopyResource(m_out, tap);
	SafeRelease(tap);
	if(q1)
	{
		// quality 1: the mask straight onto the float copy (the module's state: blend off, r = 1 under each unit)
		drawMask(m_outRtv, rawW, rawH);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		appliedQ1++;
	}
	else
	{
		const float zero[4] = {0, 0, 0, 0};   // ShadowRaw's clear value (the captures' empty texels, 9.7)
		ctx->ClearRenderTargetView(m_rawRtv, zero);
		drawMask(m_rawRtv, rawW, rawH);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		resample(ctx, m_rawSrv, rawW, rawH, m_outRtv, m_outDesc.Width, m_outDesc.Height, true);
	}
	applied++;
	lastCpuMs = QpcToMs(Qpc() - q0);
	sumCpuMs += lastCpuMs;
	if(lastCpuMs > maxCpuMs)
		maxCpuMs = lastCpuMs;
	return m_outSrv;
}

}  // namespace srvrender
