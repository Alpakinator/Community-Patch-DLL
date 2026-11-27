// standin_util.h - shared bits of the ring stand-ins (issue 3302): log file, argument helpers, the frame-id picture
// encoding both sides check, the "v3" arm's mini-section, a GPU load shader. Test programs only; never in the product.
#pragma once
#include "../ring_common.h"
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <share.h>

namespace standin {

using vpframe::ring::Qpc;
using vpframe::ring::QpcFreq;

struct Log
{
	FILE* f = nullptr;
	void open(const wchar_t* path)
	{
		if (path && *path) f = _wfsopen(path, L"w", _SH_DENYWR);   // readable while we write (tests poll it)
		if (!f) f = stdout;
		setvbuf(f, nullptr, _IONBF, 0);
	}
	void line(const char* fmt, ...)
	{
		char b[1024];
		va_list a; va_start(a, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a); va_end(a);
		SYSTEMTIME t; GetLocalTime(&t);
		fprintf(f, "%02u:%02u:%02u.%03u %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, b);
	}
	static void Cb(void* user, const char* l) { ((Log*)user)->line("%s", l); }
};

inline double Ms(int64_t ticks) { return (double)ticks * 1000.0 / (double)QpcFreq(); }

// The picture of frame F: colour B = F & 255, G = (F >> 8) & 255, R = (F >> 16) & 255, A = 255 (exact in UNORM);
// depth (F & 0xFFFFF) / 2^20 (exact in float). Both sides compute it; the consumer reads one texel back to check.
inline void FrameColour(int64_t f, float c[4])
{
	c[0] = (float)((f >> 16) & 255) / 255.0f;   // R
	c[1] = (float)((f >> 8) & 255) / 255.0f;    // G
	c[2] = (float)(f & 255) / 255.0f;           // B
	c[3] = 1.0f;
}
inline uint32_t FrameBgra(int64_t f) { return 0xFF000000u | (uint32_t)(((f >> 16) & 255) << 16) | (uint32_t)(((f >> 8) & 255) << 8) | (uint32_t)(f & 255); }
inline float FrameDepth(int64_t f) { return (float)(f & 0xFFFFF) / 1048576.0f; }

// The "v3" arm (keyed mutex + copy, the version-3 hand-over of shell_server.exe / shell_composite.dll, reduced to what
// the cost series needs): one shared texture with a keyed mutex, named in this block at <base>.V3.
struct V3Block
{
	volatile uint32_t seq;
	uint32_t width, height, generation;
	volatile int64_t frameId;      // written by the producer while it holds the mutex, right before ReleaseSync(1)
	volatile int64_t readyQpc;
	volatile int64_t published;
	wchar_t texName[128];
};

static const char* kShader =
	"cbuffer C : register(b0) { float4 col; uint iters; uint3 pad; };\n"
	"Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
	"struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
	"V VS(uint id : SV_VertexID) { V o; o.uv = float2((id << 1) & 2, id & 2); o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
	"float4 PSLoad(V i) : SV_Target { float a = i.pos.x * 0.001 + i.pos.y * 0.0007; [loop] for (uint k = 0; k < iters; ++k) a = frac(a * 1.0001 + 0.1); return col + (a > 2.0 ? 1 : 0); }\n"
	"float4 PSBlit(V i) : SV_Target { return tex.Sample(smp, i.uv); }\n";

struct Gpu
{
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* psLoad = nullptr;
	ID3D11PixelShader* psBlit = nullptr;
	ID3D11Buffer* cb = nullptr;
	ID3D11SamplerState* smp = nullptr;
	ID3D11BlendState* blend = nullptr;   // straight alpha over (the composite's blend)

	bool init(ID3D11Device* dev)
	{
		ID3DBlob* b = nullptr; ID3DBlob* e = nullptr;
		struct { const char* entry; const char* target; } st[3] = { { "VS", "vs_4_0" }, { "PSLoad", "ps_4_0" }, { "PSBlit", "ps_4_0" } };
		for (int i = 0; i < 3; ++i)
		{
			if (FAILED(D3DCompile(kShader, strlen(kShader), "standin", nullptr, nullptr, st[i].entry, st[i].target, 0, 0, &b, &e)))
			{
				if (e) { fprintf(stderr, "%s\n", (const char*)e->GetBufferPointer()); e->Release(); }
				return false;
			}
			HRESULT hr = i == 0 ? dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs)
				: dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, i == 1 ? &psLoad : &psBlit);
			b->Release();
			if (FAILED(hr)) return false;
		}
		D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) return false;
		D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(dev->CreateSamplerState(&sd, &smp))) return false;
		D3D11_BLEND_DESC bl = {};
		bl.RenderTarget[0].BlendEnable = TRUE;
		bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		return SUCCEEDED(dev->CreateBlendState(&bl, &blend));
	}
	// Full-screen passes of the load shader into rtv (the producer's draw: output = col exactly; the consumer's "EXE work").
	void load(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h, const float col[4], uint32_t iters, int passes)
	{
		if (passes <= 0) return;
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			float* p = (float*)m.pData; memcpy(p, col, 16); ((uint32_t*)p)[4] = iters; ctx->Unmap(cb, 0);
		}
		D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		ctx->RSSetViewports(1, &vp);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetInputLayout(nullptr);
		ctx->VSSetShader(vs, nullptr, 0);
		ctx->PSSetShader(psLoad, nullptr, 0);
		ctx->PSSetConstantBuffers(0, 1, &cb);
		for (int i = 0; i < passes; ++i) ctx->Draw(3, 0);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}
	// The composite's draw: the server picture over rtv with straight alpha (no copy: srv may be the slot itself).
	void blit(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h, ID3D11ShaderResourceView* srv)
	{
		D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		ctx->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
		ctx->RSSetViewports(1, &vp);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetInputLayout(nullptr);
		ctx->VSSetShader(vs, nullptr, 0);
		ctx->PSSetShader(psBlit, nullptr, 0);
		ctx->PSSetShaderResources(0, 1, &srv);
		ctx->PSSetSamplers(0, 1, &smp);
		ctx->Draw(3, 0);
		ID3D11ShaderResourceView* none = nullptr;
		ctx->PSSetShaderResources(0, 1, &none);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}
};

inline bool CreateDevice(ID3D11Device*& dev, ID3D11DeviceContext*& ctx, uint64_t& luid)
{
	D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 2,
		D3D11_SDK_VERSION, &dev, nullptr, &ctx);
	if (FAILED(hr)) return false;
	IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr; DXGI_ADAPTER_DESC d = {};
	if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && SUCCEEDED(xd->GetAdapter(&ad))) ad->GetDesc(&d);
	if (ad) ad->Release();
	if (xd) xd->Release();
	luid = ((uint64_t)(uint32_t)d.AdapterLuid.HighPart << 32) | d.AdapterLuid.LowPart;
	return true;
}

inline bool ArgIs(const wchar_t* a, const wchar_t* name) { return wcscmp(a, name) == 0; }

}  // namespace standin
