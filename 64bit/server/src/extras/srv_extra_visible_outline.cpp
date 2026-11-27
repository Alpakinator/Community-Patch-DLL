// srv_extra_visible_outline.cpp - the extra VisibleAreaOutline (ticket 4938). See srv_extra_visible_outline.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_extra_visible_outline.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace srvextras {

using c5r::Err;
using c5r::Status;
using c5r::fow::PlotState;
using c5r::heightfield::HeightGrid;

const int kAxialDirs[6][2] = {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};

namespace {

bool Finite(float v) { return isfinite(v) != 0; }

template <class T>
void SafeRelease(T*& p)
{
	if(p)
	{
		p->Release();
		p = nullptr;
	}
}

// The world position of an axial point (hx, hy; fractional at the hex corners) with the grid's placement, as
// c5r::arc::plotToWorldXY places a plot's centre, and the ground's height there.
void AxialToWorld(const HeightGrid& g, float hx, float hy, float lift, float out[3])
{
	const float* m = g.hexToWorld;
	const float sxs = hx * m[0] + hy * m[4] + m[12];
	const float sys = hx * m[1] + hy * m[5] + m[13];
	out[0] = sxs * g.sx - g.ox;
	out[1] = sys * g.sy - g.oy;
	out[2] = c5r::heightfield::heightAtSample(g, (int)floorf(sxs), (int)floorf(sys)) + lift;
}

}  // namespace

float OutlineWrapWidth(const HeightGrid& g)
{
	return (g.wrapX && g.samplesX > 0) ? (float)(g.samplesX - 1) * g.sx : 0.0f;
}

Status BuildOutline(const HeightGrid& g, const PlotState* states, uint32_t w, uint32_t h, const OutlineParams& p,
                    std::vector<OutlineVertex>& out, OutlineStats* stats)
{
	out.clear();
	if(stats)
		*stats = OutlineStats();
	if(w == 0 || h == 0)
		return Status::fail(Err::BadLayout, "the plot table is empty");
	if(!states)
		return Status::fail(Err::BadLayout, "no plot states");
	if(g.mapW != w || g.mapH != h + 4)
	{
		char b[160];
		snprintf(b, sizeof(b), "the plot table is %ux%u, the height field's grid %ux%u (expected %ux%u)", w, h, g.mapW, g.mapH, w,
		         h + 4);
		return Status::fail(Err::BadLayout, b);
	}
	if(g.samplesX == 0 || g.samplesY == 0 || g.h.size() != (size_t)g.samplesX * g.samplesY)
		return Status::fail(Err::BadLayout, "the height field's samples are empty or inconsistent");
	for(int i = 0; i < 16; ++i)
		if(!Finite(g.hexToWorld[i]))
			return Status::fail(Err::BadLayout, "the height field's placement is not finite");
	if(!Finite(g.sx) || !Finite(g.sy) || !Finite(g.ox) || !Finite(g.oy) || !Finite(g.sz))
		return Status::fail(Err::BadLayout, "the height field's placement is not finite");
	if(!(p.halfWidth > 0.0f && p.halfWidth <= 16.0f) || !(p.lift >= -16.0f && p.lift <= 16.0f) || p.segments < 1 || p.segments > 16)
		return Status::fail(Err::BadLayout, "the outline's parameters are outside their ranges");

	OutlineStats st;
	const int W = (int)w, H = (int)h;
	const int n = p.segments;
	std::vector<float> pts((size_t)(n + 1) * 3);
	for(int y = 0; y < H; ++y)
		for(int x = 0; x < W; ++x)
		{
			if(states[(size_t)y * w + x] != PlotState::Visible)
				continue;
			st.visible++;
			const int q = x - (y >> 1), r = y;
			for(int k = 0; k < 6; ++k)
			{
				const int nr = r + kAxialDirs[k][1];
				if(nr < 0 || nr >= H)
				{
					st.borderSkips++;
					continue;
				}
				int nx = q + kAxialDirs[k][0] + (nr >> 1);
				if(g.wrapX)
					nx = ((nx % W) + W) % W;
				else if(nx < 0 || nx >= W)
				{
					st.borderSkips++;
					continue;
				}
				if(states[(size_t)nr * w + nx] != PlotState::Seen)   // only toward revealed plots out of sight (an unexplored one lies under the clouds)
					continue;
				st.edges++;
				// the edge's ends: the corners shared with the directions before and after k (centroids of three centres)
				const int* dp = kAxialDirs[(k + 5) % 6];
				const int* dk = kAxialDirs[k];
				const int* dn = kAxialDirs[(k + 1) % 6];
				const float ax = q + (dp[0] + dk[0]) / 3.0f, ay = r + (dp[1] + dk[1]) / 3.0f;
				const float bx = q + (dk[0] + dn[0]) / 3.0f, by = r + (dk[1] + dn[1]) / 3.0f;
				for(int s = 0; s <= n; ++s)
				{
					const float t = (float)s / (float)n;
					AxialToWorld(g, ax + (bx - ax) * t, ay + (by - ay) * t, p.lift, &pts[(size_t)s * 3]);
				}
				// the ribbon's cross direction in the ground plane, from the whole edge
				float dx = pts[(size_t)n * 3 + 0] - pts[0], dy = pts[(size_t)n * 3 + 1] - pts[1];
				const float len = sqrtf(dx * dx + dy * dy);
				if(!(len > 0.0f) || !Finite(len))
					return Status::fail(Err::BadLayout, "a hex edge of zero length: the height field's placement is degenerate");
				dx /= len;
				dy /= len;
				const float nxw = -dy * p.halfWidth, nyw = dx * p.halfWidth;
				for(int s = 0; s < n; ++s)
				{
					const float* a = &pts[(size_t)s * 3];
					const float* b = &pts[(size_t)(s + 1) * 3];
					const OutlineVertex l0 = {a[0] - nxw, a[1] - nyw, a[2], -1.0f}, r0 = {a[0] + nxw, a[1] + nyw, a[2], 1.0f};
					const OutlineVertex l1 = {b[0] - nxw, b[1] - nyw, b[2], -1.0f}, r1 = {b[0] + nxw, b[1] + nyw, b[2], 1.0f};
					out.push_back(l0);
					out.push_back(r0);
					out.push_back(l1);
					out.push_back(r0);
					out.push_back(r1);
					out.push_back(l1);
				}
			}
		}
	if(stats)
		*stats = st;
	return Status::Ok();
}

// ---- the GPU pass ----------------------------------------------------------------------------------------------------

namespace {

const char kShader[] =
	"cbuffer C : register(b0) { row_major float4x4 VP; float4 Offset; float4 Colour; };\n"
	"struct VI { float3 pos : POSITION; float side : TEXCOORD0; };\n"
	"struct VO { float4 pos : SV_Position; float side : TEXCOORD0; };\n"
	"VO vs_main(VI i) { VO o; o.pos = mul(float4(i.pos + Offset.xyz, 1.0), VP); o.side = i.side; return o; }\n"
	"float4 ps_main(VO i) : SV_Target {\n"
	"  float a = Colour.a * (1.0 - smoothstep(0.55, 1.0, abs(i.side)));\n"
	"  return float4(Colour.rgb, a); }\n";

struct Constants
{
	float vp[16];
	float offset[4];
	float colour[4];
};
static_assert(sizeof(Constants) == 96, "Constants layout");

Status Compile(const char* entry, const char* target, ID3DBlob** out)
{
	ID3DBlob* err = nullptr;
	const HRESULT hr = D3DCompile(kShader, sizeof(kShader) - 1, "visible_outline", nullptr, nullptr, entry, target,
	                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
	if(FAILED(hr))
	{
		std::string m = std::string("D3DCompile ") + entry;
		if(err)
			m += std::string(": ") + (const char*)err->GetBufferPointer();
		SafeRelease(err);
		return Status::fail(Err::Device, m);
	}
	SafeRelease(err);
	return Status::Ok();
}

Status Hr(HRESULT hr, const char* what)
{
	if(SUCCEEDED(hr))
		return Status::Ok();
	char b[128];
	snprintf(b, sizeof(b), "%s failed (0x%08lX)", what, (unsigned long)hr);
	return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, b);
}

}  // namespace

OutlineRenderer::OutlineRenderer() {}
OutlineRenderer::~OutlineRenderer() { release(); }

void OutlineRenderer::release()
{
	SafeRelease(m_vs);
	SafeRelease(m_ps);
	SafeRelease(m_layout);
	SafeRelease(m_vb);
	SafeRelease(m_cb);
	SafeRelease(m_blend);
	SafeRelease(m_depth);
	SafeRelease(m_noDepth);
	SafeRelease(m_raster);
	m_capacity = m_count = 0;
}

Status OutlineRenderer::init(ID3D11Device* dev)
{
	release();
	if(!dev)
		return Status::fail(Err::Device, "no device");
	ID3DBlob* vsb = nullptr;
	ID3DBlob* psb = nullptr;
	Status st = Compile("vs_main", "vs_5_0", &vsb);
	if(st.ok())
		st = Compile("ps_main", "ps_5_0", &psb);
	if(st.ok())
		st = Hr(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs), "CreateVertexShader");
	if(st.ok())
		st = Hr(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps), "CreatePixelShader");
	if(st.ok())
	{
		const D3D11_INPUT_ELEMENT_DESC el[2] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		st = Hr(dev->CreateInputLayout(el, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_layout), "CreateInputLayout");
	}
	SafeRelease(vsb);
	SafeRelease(psb);
	if(st.ok())
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(Constants);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		st = Hr(dev->CreateBuffer(&bd, nullptr, &m_cb), "CreateBuffer (constants)");
	}
	if(st.ok())
	{
		D3D11_BLEND_DESC b = {};
		b.RenderTarget[0].BlendEnable = TRUE;
		b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;   // the destination's alpha is kept: the server's picture is
		b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;   // composited by it
		b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		st = Hr(dev->CreateBlendState(&b, &m_blend), "CreateBlendState");
	}
	if(st.ok())
	{
		D3D11_DEPTH_STENCIL_DESC d = {};
		d.DepthEnable = TRUE;
		d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		st = Hr(dev->CreateDepthStencilState(&d, &m_depth), "CreateDepthStencilState");
		if(st.ok())
		{
			d.DepthEnable = FALSE;
			st = Hr(dev->CreateDepthStencilState(&d, &m_noDepth), "CreateDepthStencilState (none)");
		}
	}
	if(st.ok())
	{
		D3D11_RASTERIZER_DESC r = {};
		r.FillMode = D3D11_FILL_SOLID;
		r.CullMode = D3D11_CULL_NONE;
		r.DepthClipEnable = TRUE;
		st = Hr(dev->CreateRasterizerState(&r, &m_raster), "CreateRasterizerState");
	}
	if(!st.ok())
		release();
	return st;
}

Status OutlineRenderer::upload(ID3D11DeviceContext* ctx, const std::vector<OutlineVertex>& v)
{
	if(!ready() || !ctx)
		return Status::fail(Err::Device, "the outline's pass is not initialised");
	m_count = 0;
	if(v.empty())
		return Status::Ok();
	if(v.size() > 0x7FFFFFF)
		return Status::fail(Err::OutOfMemory, "too many outline vertices");
	if(v.size() > m_capacity)
	{
		SafeRelease(m_vb);
		m_capacity = 0;
		ID3D11Device* dev = nullptr;
		ctx->GetDevice(&dev);
		uint32_t cap = 4096;
		while(cap < v.size())
			cap *= 2;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = cap * (UINT)sizeof(OutlineVertex);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const Status st = dev ? Hr(dev->CreateBuffer(&bd, nullptr, &m_vb), "CreateBuffer (vertices)")
		                      : Status::fail(Err::Device, "no device behind the context");
		SafeRelease(dev);
		C5R_TRY(st);
		m_capacity = cap;
	}
	D3D11_MAPPED_SUBRESOURCE ms = {};
	C5R_TRY(Hr(ctx->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "Map (vertices)"));
	memcpy(ms.pData, v.data(), v.size() * sizeof(OutlineVertex));
	ctx->Unmap(m_vb, 0);
	m_count = (uint32_t)v.size();
	return Status::Ok();
}

Status OutlineRenderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
                             uint32_t h, const float viewProj[16], float wrapWidth, const float rgba[4])
{
	if(!ready() || !ctx)
		return Status::fail(Err::Device, "the outline's pass is not initialised");
	if(!rtv || w == 0 || h == 0 || !viewProj || !rgba)
		return Status::fail(Err::BadLayout, "no target");
	for(int i = 0; i < 16; ++i)
		if(!Finite(viewProj[i]))
			return Status::fail(Err::BadLayout, "the camera's matrix is not finite");
	if(m_count == 0)
		return Status::Ok();

	// the context's state, put back at the end
	ID3D11RenderTargetView* oRtv = nullptr;
	ID3D11DepthStencilView* oDsv = nullptr;
	ctx->OMGetRenderTargets(1, &oRtv, &oDsv);
	ID3D11BlendState* oBlend = nullptr;
	float oFactor[4];
	UINT oMask = 0;
	ctx->OMGetBlendState(&oBlend, oFactor, &oMask);
	ID3D11DepthStencilState* oDepth = nullptr;
	UINT oRef = 0;
	ctx->OMGetDepthStencilState(&oDepth, &oRef);
	ID3D11RasterizerState* oRaster = nullptr;
	ctx->RSGetState(&oRaster);
	UINT nVp = 1;
	D3D11_VIEWPORT oVp = {};
	ctx->RSGetViewports(&nVp, &oVp);
	ID3D11InputLayout* oLayout = nullptr;
	ctx->IAGetInputLayout(&oLayout);
	D3D11_PRIMITIVE_TOPOLOGY oTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ctx->IAGetPrimitiveTopology(&oTopo);
	ID3D11Buffer* oVb = nullptr;
	UINT oStride = 0, oOff = 0;
	ctx->IAGetVertexBuffers(0, 1, &oVb, &oStride, &oOff);
	ID3D11VertexShader* oVs = nullptr;
	ctx->VSGetShader(&oVs, nullptr, nullptr);
	ID3D11PixelShader* oPs = nullptr;
	ctx->PSGetShader(&oPs, nullptr, nullptr);
	ID3D11Buffer* oVcb = nullptr;
	ctx->VSGetConstantBuffers(0, 1, &oVcb);
	ID3D11Buffer* oPcb = nullptr;
	ctx->PSGetConstantBuffers(0, 1, &oPcb);

	ctx->OMSetRenderTargets(1, &rtv, dsv);
	const float factor[4] = {0, 0, 0, 0};
	ctx->OMSetBlendState(m_blend, factor, 0xFFFFFFFFu);
	ctx->OMSetDepthStencilState(dsv ? m_depth : m_noDepth, 0);
	ctx->RSSetState(m_raster);
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)w;
	vp.Height = (float)h;
	vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);
	ctx->IASetInputLayout(m_layout);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(OutlineVertex), off = 0;
	ctx->IASetVertexBuffers(0, 1, &m_vb, &stride, &off);
	ctx->VSSetShader(m_vs, nullptr, 0);
	ctx->PSSetShader(m_ps, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &m_cb);
	ctx->PSSetConstantBuffers(0, 1, &m_cb);

	Status st = Status::Ok();
	const float offsets[3] = {0.0f, -wrapWidth, wrapWidth};
	const int copies = (wrapWidth > 0.0f && Finite(wrapWidth)) ? 3 : 1;
	for(int c = 0; c < copies && st.ok(); ++c)
	{
		D3D11_MAPPED_SUBRESOURCE ms = {};
		st = Hr(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "Map (constants)");
		if(!st.ok())
			break;
		Constants k = {};
		memcpy(k.vp, viewProj, sizeof(k.vp));
		k.offset[0] = offsets[c];
		memcpy(k.colour, rgba, sizeof(k.colour));
		memcpy(ms.pData, &k, sizeof(k));
		ctx->Unmap(m_cb, 0);
		ctx->Draw(m_count, 0);
	}

	ctx->OMSetRenderTargets(1, &oRtv, oDsv);
	ctx->OMSetBlendState(oBlend, oFactor, oMask);
	ctx->OMSetDepthStencilState(oDepth, oRef);
	ctx->RSSetState(oRaster);
	if(nVp)
		ctx->RSSetViewports(1, &oVp);
	ctx->IASetInputLayout(oLayout);
	ctx->IASetPrimitiveTopology(oTopo);
	ctx->IASetVertexBuffers(0, 1, &oVb, &oStride, &oOff);
	ctx->VSSetShader(oVs, nullptr, 0);
	ctx->PSSetShader(oPs, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &oVcb);
	ctx->PSSetConstantBuffers(0, 1, &oPcb);
	SafeRelease(oRtv);
	SafeRelease(oDsv);
	SafeRelease(oBlend);
	SafeRelease(oDepth);
	SafeRelease(oRaster);
	SafeRelease(oLayout);
	SafeRelease(oVb);
	SafeRelease(oVs);
	SafeRelease(oPs);
	SafeRelease(oVcb);
	SafeRelease(oPcb);
	return st;
}

// ---- the extra ---------------------------------------------------------------------------------------------------------

namespace {
const char* const kWaitText[VisibleOutline::kWaitCount] = {
	"", "the fog of war is not running (no plot states)", "the height field is not there yet",
	"the fog's plot table does not fit the height field"};

// A cheap identity of the height field: a new load gives a new one (sizes, placement, seed, a few samples) [I: a load
// with the same map, seed and heights at these samples would be taken for the same field; the fog's generation also
// moves at a load]
uint64_t FieldKey(const HeightGrid& g)
{
	uint64_t k = 1469598103934665603ull;
	auto mix = [&k](uint64_t v) {
		k ^= v;
		k *= 1099511628211ull;
	};
	mix(g.mapW);
	mix(g.mapH);
	mix(g.mapSeed);
	mix(g.wrapX ? 1 : 0);
	uint32_t b;
	memcpy(&b, &g.ox, 4);
	mix(b);
	memcpy(&b, &g.oy, 4);
	mix(b);
	mix(g.h.size());
	if(!g.h.empty())
		for(size_t i = 0; i < 8; ++i)
			mix(g.h[(g.h.size() - 1) * i / 7]);
	return k;
}
}  // namespace

VisibleOutline::VisibleOutline(OutlineLogFn log, void* logCtx) : m_log(log), m_logCtx(logCtx) {}

void VisibleOutline::say(const char* fmt, ...)
{
	char b[512];
	int n = snprintf(b, sizeof(b), "extras: VisibleAreaOutline: ");
	if(n < 0)
		n = 0;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b + n, sizeof(b) - (size_t)n, fmt, ap);
	va_end(ap);
	if(m_log)
		m_log(m_logCtx, b);
}

void VisibleOutline::fail(const std::string& why)
{
	if(m_failed)
		return;
	m_failed = true;
	say("off for this session: %s; the frame is drawn without it", why.c_str());
}

void VisibleOutline::configure(bool extraOn, const char* whyOff, bool fogConfigured)
{
	m_on = extraOn;
	m_failed = false;
	m_wait = kWaitNone;
	m_built = m_uploaded = false;
	if(!extraOn)
	{
		if(whyOff && whyOff[0])
			say("not used: %s", whyOff);
		return;
	}
	if(!fogConfigured)
	{
		fail("it needs the fog of war, which this server does not draw (render_fow=0)");
		return;
	}
	say("in effect (a line around the plots the active player sees now)");
}

Status VisibleOutline::initDevice(ID3D11Device* dev)
{
	m_haveDevice = false;
	m_built = m_uploaded = false;
	if(!m_on || m_failed)
		return Status::Ok();
	const Status st = m_r.init(dev);
	if(!st.ok())
	{
		fail("its draw could not be set up: " + st.str());
		return st;
	}
	m_haveDevice = true;
	return st;
}

void VisibleOutline::releaseDevice()
{
	m_r.release();
	m_haveDevice = false;
	m_uploaded = false;
}

void VisibleOutline::frame(ID3D11DeviceContext* ctx, const OutlineFrame& f)
{
	if(!on())
		return;
	frames++;
	if(!m_haveDevice || !ctx)
		return;
	Wait wait = kWaitNone;
	if(!f.states)
		wait = kWaitFog;
	else if(!f.grid)
		wait = kWaitGrid;
	else if(f.grid->mapW != f.w || f.grid->mapH != f.h + 4 || f.w == 0 || f.h == 0)
		wait = kWaitTable;
	if(wait != m_wait)
	{
		if(wait != kWaitNone)
			say("not drawn: %s", kWaitText[wait]);
		else
			say("drawn again: its inputs are back");
		m_wait = wait;
	}
	if(wait != kWaitNone)
	{
		waits[wait]++;
		m_built = false;
		return;
	}
	const uint64_t key = FieldKey(*f.grid);
	if(!m_built || f.fogGeneration != m_gen || f.grid != m_grid || key != m_fieldKey)
	{
		OutlineStats st;
		LARGE_INTEGER q0, q1, qf;
		QueryPerformanceCounter(&q0);
		const Status bs = BuildOutline(*f.grid, f.states, f.w, f.h, params, m_verts, &st);
		QueryPerformanceCounter(&q1);
		QueryPerformanceFrequency(&qf);
		lastBuildMs = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;
		if(!bs.ok())
		{
			buildErrors++;
			fail("its geometry could not be built: " + bs.str());
			return;
		}
		rebuilds++;
		lastEdges = st.edges;
		m_built = true;
		m_uploaded = false;
		m_gen = f.fogGeneration;
		m_grid = f.grid;
		m_fieldKey = key;
	}
	if(!m_uploaded)
	{
		const Status us = m_r.upload(ctx, m_verts);
		if(!us.ok())
		{
			drawErrors++;
			fail("its vertices could not be uploaded: " + us.str());
			return;
		}
		m_uploaded = true;
	}
	const Status ds = m_r.draw(ctx, f.rtv, f.dsv, f.width, f.height, f.viewProj, OutlineWrapWidth(*f.grid), rgba);
	if(!ds.ok())
	{
		drawErrors++;
		fail("its draw failed: " + ds.str());
		return;
	}
	if(drawn++ == 0)
	{
		float zmin = 0, zmax = 0;
		for(size_t i = 0; i < m_verts.size(); ++i)
		{
			zmin = (i == 0 || m_verts[i].z < zmin) ? m_verts[i].z : zmin;
			zmax = (i == 0 || m_verts[i].z > zmax) ? m_verts[i].z : zmax;
		}
		uint32_t n[3] = {0, 0, 0};   // visible, seen, unexplored
		for(size_t i = 0; i < (size_t)f.w * f.h; ++i)
		{
			const unsigned s = (unsigned)f.states[i];
			if(s < 3)
				n[s]++;
		}
		say("first draw: %u edges (%u vertices, heights %.2f..%.2f, built in %.3f ms) at fog generation %u, %ux%u plots (%u "
		    "visible, %u seen, %u unexplored)",
		    lastEdges, (unsigned)m_verts.size(), zmin, zmax, lastBuildMs, m_gen, f.w, f.h, n[0], n[1], n[2]);
	}
}

void VisibleOutline::totalsLine(char* out, size_t n) const
{
	if(!out || n == 0)
		return;
	snprintf(out, n,
	         "extras: VisibleAreaOutline: %s; frames %lld, drawn %lld, rebuilds %lld (last %u edges), waits fog %lld grid %lld "
	         "table %lld, errors build %lld draw %lld",
	         m_failed ? "off (failed)" : m_on ? "on" : "off", (long long)frames, (long long)drawn, (long long)rebuilds, lastEdges,
	         (long long)waits[kWaitFog], (long long)waits[kWaitGrid], (long long)waits[kWaitTable], (long long)buildErrors,
	         (long long)drawErrors);
}

}  // namespace srvextras
