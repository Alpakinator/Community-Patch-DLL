// test_unit_shadow.cpp - the tests of server/src/render/srv_unit_shadow.{h,cpp} (epic 0042, ticket 4785; research
// unit-combat-server.md 13). Written 2026-11-20 for this repository (GPL-3.0-or-later). Test program only.
//
// A WARP device, the EXE's ShadowShader resample programs from C5R_EXE (read-only), and masks written straight into the
// targets the module hands to its mask callback (UpdateSubresource: no unit module needed). Cases:
//   wanted            UnitShadowWanted's rule, every refusal named
//   init_refused      no programs / no device / a file that is not the EXE: init fails with the reason, nothing is made
//   resample_cpu      the GPU resample of a synthetic mask (blend off) against an exact CPU model of 9.2 R1 (8 bilinear
//                     alpha samples, wrap) at quality 3 and 2 sizes
//   merge_adds        quality 3: min(1, tap + R(mask)) by a saturating add; texels far from the mask keep the tap's bits
//   merge_q1          quality 1: the mask drawn onto the float copy (r = 1 under the mask), the rest the tap's bits
//   refusals          quality 0, a wrong format, no raw size, no callback: null, refused counted, no fatal
//   targets_again     a raw-size change makes the targets again; the same size does not
// Mode `pair MASK.bin W H PITCH BLUR.bin BW BH BPITCH`: the GPU resample of a captured ShadowRaw (R16G16B16A16_FLOAT rows of
// PITCH bytes) against the captured ShadowBlur (R16_UNORM) of the same frame (3907's exports; test_unit_shadow.py reads
// their json); prints the difference statistics, exit 0 when max <= 256 and >= 97 % of the shaded texels are within 64.
// Output: `CASE <name> PASS|FAIL <detail>`; exit code = failed cases, 2 usage, 99 no WARP device or C5R_EXE.
#include "srv_unit_shadow.h"
#include "srv_render_common.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace srvrender;

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::wstring g_exe;

void HostLog(const char*) {}

uint16_t Half(float f)
{
	// exact for the values used here (0, 1, multiples of 1/1024 in [0, 4]); no NaN / inf handling
	if(f == 0.0f)
		return 0;
	uint32_t x;
	memcpy(&x, &f, 4);
	const uint32_t sign = (x >> 16) & 0x8000u;
	const int e = (int)((x >> 23) & 0xFF) - 127 + 15;
	const uint32_t m = (x >> 13) & 0x3FFu;
	if(e <= 0)
		return (uint16_t)sign;
	return (uint16_t)(sign | ((uint32_t)e << 10) | m);
}

float FromHalf(uint16_t h)
{
	const uint32_t e = (h >> 10) & 0x1F, m = h & 0x3FF;
	float v = e == 0 ? (float)m / 1024.0f / 16384.0f : (1.0f + (float)m / 1024.0f) * powf(2.0f, (float)e - 15.0f);
	return (h & 0x8000) ? -v : v;
}

struct Tex
{
	ID3D11Texture2D* t = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	uint32_t w = 0, h = 0;
	DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
	bool make(uint32_t w_, uint32_t h_, DXGI_FORMAT f, const void* data, uint32_t pitch)
	{
		w = w_;
		h = h_;
		fmt = f;
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = w;
		d.Height = h;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = f;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		D3D11_SUBRESOURCE_DATA sd = {data, pitch, 0};
		return SUCCEEDED(g_dev->CreateTexture2D(&d, data ? &sd : nullptr, &t)) && SUCCEEDED(g_dev->CreateShaderResourceView(t, nullptr, &srv)) &&
		       SUCCEEDED(g_dev->CreateRenderTargetView(t, nullptr, &rtv));
	}
	~Tex()
	{
		SafeRelease(rtv);
		SafeRelease(srv);
		SafeRelease(t);
	}
};

// the texels of a 2D texture (any format) as bytes, rows tight
std::vector<uint8_t> ReadBack(ID3D11Resource* r, uint32_t& w, uint32_t& h, uint32_t bpp)
{
	ID3D11Texture2D* src = nullptr;
	r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src);
	std::vector<uint8_t> out;
	if(!src)
		return out;
	D3D11_TEXTURE2D_DESC d;
	src->GetDesc(&d);
	w = d.Width;
	h = d.Height;
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	d.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	if(SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &st)))
	{
		g_ctx->CopyResource(st, src);
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			out.resize((size_t)w * h * bpp);
			for(uint32_t y = 0; y < h; ++y)
				memcpy(&out[(size_t)y * w * bpp], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w * bpp);
			g_ctx->Unmap(st, 0);
		}
		st->Release();
	}
	src->Release();
	return out;
}

std::vector<uint16_t> ReadR16(ID3D11ShaderResourceView* v, uint32_t& w, uint32_t& h)
{
	ID3D11Resource* r = nullptr;
	v->GetResource(&r);
	const std::vector<uint8_t> b = ReadBack(r, w, h, 2);
	r->Release();
	std::vector<uint16_t> o(b.size() / 2);
	if(!o.empty())
		memcpy(o.data(), b.data(), b.size());
	return o;
}

// a mask: rgba (1, 1, 1, a) in the rectangle, 0 elsewhere (the target is cleared or a copy); written by UpdateSubresource
void WriteRect(ID3D11RenderTargetView* rtv, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float a)
{
	ID3D11Resource* r = nullptr;
	rtv->GetResource(&r);
	std::vector<uint16_t> px((size_t)(x1 - x0) * (y1 - y0) * 4);
	for(size_t i = 0; i < px.size(); i += 4)
	{
		px[i] = px[i + 1] = px[i + 2] = Half(1.0f);
		px[i + 3] = Half(a);
	}
	D3D11_BOX b = {x0, y0, 0, x1, y1, 1};
	g_ctx->UpdateSubresource(r, 0, &b, px.data(), (x1 - x0) * 8, 0);
	r->Release();
}

// the CPU model of 9.2 R1: mean of 8 bilinear samples of alpha at (+-1, +-1) and (+-0.5, +-0.5) source texels, wrap
double Bilinear(const std::vector<float>& a, uint32_t w, uint32_t h, double u, double v)
{
	const double x = u * w - 0.5, y = v * h - 0.5;
	const double x0 = floor(x), y0 = floor(y), fx = x - x0, fy = y - y0;
	auto at = [&](long long xi, long long yi) {
		xi = ((xi % (long long)w) + w) % w;
		yi = ((yi % (long long)h) + h) % h;
		return (double)a[(size_t)yi * w + (size_t)xi];
	};
	const long long ix = (long long)x0, iy = (long long)y0;
	return at(ix, iy) * (1 - fx) * (1 - fy) + at(ix + 1, iy) * fx * (1 - fy) + at(ix, iy + 1) * (1 - fx) * fy + at(ix + 1, iy + 1) * fx * fy;
}

uint16_t CpuResample(const std::vector<float>& a, uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, uint32_t i, uint32_t j)
{
	const double u = (i + 0.5) / bw, v = (j + 0.5) / bh, dx = 1.0 / w, dy = 1.0 / h;
	double acc = 0;
	const int s[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
	for(const auto& k : s)
	{
		acc += Bilinear(a, w, h, u + k[0] * dx, v + k[1] * dy);
		acc += Bilinear(a, w, h, u + 0.5 * k[0] * dx, v + 0.5 * k[1] * dy);
	}
	const double r = std::min(1.0, std::max(0.0, acc * 0.125));
	return (uint16_t)lround(r * 65535.0);
}

struct DiffStats
{
	int maxd = 0;
	double median = 0, within64 = 0;
	size_t n = 0;
};

DiffStats Diff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b)
{
	DiffStats s;
	std::vector<int> d;
	for(size_t i = 0; i < a.size() && i < b.size(); ++i)
		if(a[i] || b[i])
			d.push_back(abs((int)a[i] - (int)b[i]));
	s.n = d.size();
	if(d.empty())
		return s;
	size_t w64 = 0;
	for(int x : d)
	{
		s.maxd = std::max(s.maxd, x);
		w64 += x <= 64;
	}
	std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
	s.median = d[d.size() / 2];
	s.within64 = 100.0 * (double)w64 / (double)s.n;
	return s;
}

bool Init(UnitShadow& u, std::string& why)
{
	if(!u.init(g_dev, g_exe, why))
	{
		why = "init: " + why;
		return false;
	}
	return true;
}

#define CHECK(c)                                                                                                                         \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if(!(c))                                                                                                                           \
		{                                                                                                                                  \
			char b_[300];                                                                                                                  \
			snprintf(b_, sizeof(b_), "line %d: %s", __LINE__, #c);                                                                       \
			why = b_;                                                                                                                      \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while(0)

// ---- cases --------------------------------------------------------------------------------------------------------------
bool CaseWanted(std::string& why)
{
	const char* w = nullptr;
	CHECK(UnitShadowWanted(true, true, true, 3, true, 2554, 1411, &w) && !strcmp(w, ""));
	CHECK(UnitShadowWanted(true, true, true, 1, true, 2554, 1411, &w));
	CHECK(!UnitShadowWanted(false, true, true, 3, true, 2554, 1411, &w) && strstr(w, "render_units_shadow=0"));
	CHECK(!UnitShadowWanted(true, false, true, 3, true, 2554, 1411, &w) && strstr(w, "no unit scene"));
	CHECK(!UnitShadowWanted(true, true, false, 3, true, 2554, 1411, &w) && strstr(w, "slot flag clear"));
	CHECK(!UnitShadowWanted(true, true, true, 0, false, 2554, 1411, &w) && strstr(w, "quality 0"));
	CHECK(!UnitShadowWanted(true, true, true, 4, true, 2554, 1411, &w) && strstr(w, "out of range"));
	CHECK(!UnitShadowWanted(true, true, true, 3, true, 0, 1411, &w) && strstr(w, "no ShadowRaw size"));
	CHECK(!UnitShadowWanted(true, true, true, 3, true, 2554, 0, nullptr));
	return true;
}

bool CaseInitRefused(std::string& why)
{
	UnitShadow u;
	std::string w;
	const char vs[4] = {1, 2, 3, 4};
	CHECK(!u.initWith(g_dev, nullptr, 0, vs, 4, w) && w.find("VS c7615a1d is not in the EXE") != std::string::npos && !u.ready());
	CHECK(!u.initWith(g_dev, vs, 4, nullptr, 0, w) && w.find("PS be955cbb is not in the EXE") != std::string::npos);
	CHECK(!u.initWith(g_dev, nullptr, 0, nullptr, 0, w) && w.find("programs (VS c7615a1d, PS be955cbb) are not") != std::string::npos);
	CHECK(!u.initWith(nullptr, vs, 4, vs, 4, w) && w.find("no device") != std::string::npos);
	CHECK(!u.initWith(g_dev, vs, 4, vs, 4, w) && w.find("CreateVertexShader / CreatePixelShader") != std::string::npos && !u.ready());
	CHECK(!u.init(g_dev, L"C:\\no\\such\\CivilizationV_DX11.exe", w) && w.find(".shader section could not be read") != std::string::npos);
	// a refused module refuses every frame
	bool fatal = false;
	CHECK(u.apply(g_ctx, nullptr, 3, 64, 64, [](ID3D11RenderTargetView*, uint32_t, uint32_t) {}, w, &fatal) == nullptr && !fatal &&
	      w == "not initialised" && u.refused == 1);
	return true;
}

// a synthetic mask with blocks of alpha 1, 0.33, 2 (landmark overlap) and single texels
std::vector<float> SyntheticAlpha(uint32_t w, uint32_t h)
{
	std::vector<float> a((size_t)w * h, 0.0f);
	auto rect = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float v) {
		for(uint32_t y = y0; y < y1; ++y)
			for(uint32_t x = x0; x < x1; ++x)
				a[(size_t)y * w + x] = v;
	};
	rect(10, 10, 30, 22, 1.0f);
	rect(40, 5, 47, 40, 0.330078125f);
	rect(60, 30, 75, 45, 2.0f);
	rect(0, 50, 3, 60, 1.0f);       // at the left edge: the wrap sampler reads it from the right edge too
	a[(size_t)25 * w + 90] = 1.0f;  // one texel
	return a;
}

bool ResampleOne(UnitShadow& u, uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, std::string& why)
{
	const std::vector<float> a = SyntheticAlpha(w, h);
	std::vector<uint16_t> px((size_t)w * h * 4);
	for(size_t i = 0; i < a.size(); ++i)
	{
		px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = Half(a[i] > 0 ? 1.0f : 0.0f);
		px[i * 4 + 3] = Half(a[i]);
	}
	Tex raw, out;
	CHECK(raw.make(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, px.data(), w * 8));
	CHECK(out.make(bw, bh, DXGI_FORMAT_R16_UNORM, nullptr, 0));
	const float one[4] = {1, 1, 1, 1};
	g_ctx->ClearRenderTargetView(out.rtv, one);   // blend off overwrites it
	u.resample(g_ctx, raw.srv, w, h, out.rtv, bw, bh, false);
	uint32_t rw = 0, rh = 0;
	const std::vector<uint16_t> gpu = ReadR16(out.srv, rw, rh);
	CHECK(rw == bw && rh == bh && gpu.size() == (size_t)bw * bh);
	std::vector<uint16_t> cpu(gpu.size());
	for(uint32_t j = 0; j < bh; ++j)
		for(uint32_t i = 0; i < bw; ++i)
			cpu[(size_t)j * bw + i] = CpuResample(a, w, h, bw, bh, i, j);
	const DiffStats s = Diff(gpu, cpu);
	char b[200];
	snprintf(b, sizeof(b), "%ux%u -> %ux%u: %zu shaded texels, max %d, median %.0f, within 64 %.2f %%", w, h, bw, bh, s.n, s.maxd, s.median,
	         s.within64);
	printf("  resample_cpu %s\n", b);
	why = b;
	// WARP filters with 8 fractional bits of weight (as the spike's emulation, max 0.37 % against the captures)
	CHECK(s.n > 200 && s.maxd <= 256 && s.within64 >= 97.0);
	return true;
}

bool CaseResampleCpu(std::string& why)
{
	UnitShadow u;
	if(!Init(u, why))
		return false;
	// the q 3 and q 2 maps from a small "back buffer" (the ratio of 2554 x 1411 onto 2048 / 1024 is the same kind: minification)
	if(!ResampleOne(u, 128, 72, 96, 96, why))
		return false;
	if(!ResampleOne(u, 128, 72, 48, 48, why))
		return false;
	return true;
}

bool CaseMergeAdds(std::string& why)
{
	UnitShadow u;
	if(!Init(u, why))
		return false;
	const uint32_t W = 128, H = 72, B = 96;
	// the tap: 0.6 everywhere (a block's or a landmark's shadow under the whole map)
	std::vector<uint16_t> tapPx((size_t)B * B, 39321);
	Tex tap;
	CHECK(tap.make(B, B, DXGI_FORMAT_R16_UNORM, tapPx.data(), B * 2));
	std::string w;
	bool fatal = false;
	int calls = 0;
	uint32_t gotW = 0, gotH = 0;
	ID3D11ShaderResourceView* m = u.apply(g_ctx, tap.srv, 3, W, H,
	                                      [&](ID3D11RenderTargetView* r, uint32_t rw, uint32_t rh) {
		                                      calls++;
		                                      gotW = rw;
		                                      gotH = rh;
		                                      WriteRect(r, 40, 20, 80, 50, 1.0f);   // a unit's mask, alpha 1
	                                      },
	                                      w, &fatal);
	CHECK(m && !fatal && calls == 1 && gotW == W && gotH == H && u.applied == 1 && u.rawW() == W && u.rawH() == H);
	uint32_t rw = 0, rh = 0;
	const std::vector<uint16_t> o = ReadR16(m, rw, rh);
	CHECK(rw == B && rh == B);
	// inside the mask's footprint (well inside): 0.6 + 1 saturates to 1; far from it: the tap's bits exactly
	const uint32_t cx = (uint32_t)(60.0 / W * B), cy = (uint32_t)(35.0 / H * B);
	CHECK(o[(size_t)cy * B + cx] == 65535);
	CHECK(o[(size_t)2 * B + 2] == 39321 && o[(size_t)90 * B + 90] == 39321);
	// the edge: tap + R(mask), never below the tap's value
	size_t below = 0;
	for(uint16_t v : o)
		below += v < 39321;
	CHECK(below == 0);
	// the tapped texture itself is never written (the merge is on a copy)
	const std::vector<uint16_t> t2 = ReadR16(tap.srv, rw, rh);
	CHECK(t2 == tapPx);
	// a mask of alpha 0.33 adds R(0.33) = 0.33 inside: 0.6 + 0.33 = 0.93 (within 1/1024 of the float's rounding)
	std::vector<uint16_t> zero((size_t)B * B, 0);
	Tex tap0;
	CHECK(tap0.make(B, B, DXGI_FORMAT_R16_UNORM, zero.data(), B * 2));
	m = u.apply(g_ctx, tap0.srv, 3, W, H, [&](ID3D11RenderTargetView* r, uint32_t, uint32_t) { WriteRect(r, 40, 20, 80, 50, 0.330078125f); }, w,
	            &fatal);
	CHECK(m);
	const std::vector<uint16_t> o2 = ReadR16(m, rw, rh);
	const int v = o2[(size_t)cy * B + cx];
	CHECK(abs(v - (int)lround(0.330078125 * 65535)) <= 64);
	CHECK(o2[(size_t)2 * B + 2] == 0);
	return true;
}

bool CaseMergeQ1(std::string& why)
{
	UnitShadow u;
	if(!Init(u, why))
		return false;
	const uint32_t W = 64, H = 40;
	std::vector<uint16_t> px((size_t)W * H * 4);
	for(size_t i = 0; i < px.size(); i += 4)
	{
		px[i] = Half(0.25f);   // a landmark's r (the terrain reads saturate(t3.r) at q 1)
		px[i + 1] = px[i + 2] = Half(0.25f);
		px[i + 3] = Half(0.5f);
	}
	Tex tap;
	CHECK(tap.make(W, H, DXGI_FORMAT_R16G16B16A16_FLOAT, px.data(), W * 8));
	std::string w;
	bool fatal = false;
	ID3D11ShaderResourceView* m =
	    u.apply(g_ctx, tap.srv, 1, W, H, [&](ID3D11RenderTargetView* r, uint32_t, uint32_t) { WriteRect(r, 10, 10, 20, 20, 1.0f); }, w, &fatal);
	CHECK(m && !fatal && u.appliedQ1 == 1);
	ID3D11Resource* r = nullptr;
	m->GetResource(&r);
	uint32_t rw = 0, rh = 0;
	const std::vector<uint8_t> b = ReadBack(r, rw, rh, 8);
	r->Release();
	CHECK(rw == W && rh == H);
	auto texel = [&](uint32_t x, uint32_t y, int c) {
		uint16_t v;
		memcpy(&v, &b[((size_t)y * W + x) * 8 + c * 2], 2);
		return FromHalf(v);
	};
	CHECK(texel(15, 15, 0) == 1.0f);    // under the mask: r = 1
	CHECK(texel(30, 30, 0) == 0.25f);   // elsewhere: the tap's
	CHECK(texel(5, 5, 3) == 0.5f);
	// a q 1 slot whose size is not the raw size is refused (the slot is ShadowRaw itself)
	m = u.apply(g_ctx, tap.srv, 1, W + 1, H, [&](ID3D11RenderTargetView*, uint32_t, uint32_t) {}, w, &fatal);
	CHECK(!m && !fatal && w.find("does not fit quality 1") != std::string::npos);
	return true;
}

bool CaseRefusals(std::string& why)
{
	UnitShadow u;
	if(!Init(u, why))
		return false;
	std::vector<uint16_t> z(64 * 64, 0);
	Tex r16, f16;
	CHECK(r16.make(64, 64, DXGI_FORMAT_R16_UNORM, z.data(), 128));
	std::vector<uint16_t> z4(64 * 64 * 4, 0);
	CHECK(f16.make(64, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, z4.data(), 64 * 8));
	std::string w;
	bool fatal = false;
	int calls = 0;
	const UnitShadow::MaskFn fn = [&](ID3D11RenderTargetView*, uint32_t, uint32_t) { calls++; };
	CHECK(!u.apply(g_ctx, r16.srv, 0, 128, 72, fn, w, &fatal) && w.find("out of range") != std::string::npos);
	CHECK(!u.apply(g_ctx, r16.srv, 4, 128, 72, fn, w, &fatal));
	CHECK(!u.apply(g_ctx, r16.srv, 3, 0, 72, fn, w, &fatal));
	CHECK(!u.apply(g_ctx, nullptr, 3, 128, 72, fn, w, &fatal) && w == "no tapped texture");
	CHECK(!u.apply(g_ctx, r16.srv, 3, 128, 72, UnitShadow::MaskFn(), w, &fatal) && w == "no mask pass");
	CHECK(!u.apply(g_ctx, f16.srv, 3, 128, 72, fn, w, &fatal) && w.find("does not fit quality 3") != std::string::npos);   // q 3 needs R16
	CHECK(!u.apply(g_ctx, r16.srv, 1, 64, 64, fn, w, &fatal) && w.find("does not fit quality 1") != std::string::npos);    // q 1 needs float
	CHECK(calls == 0 && !fatal && u.refused == 7 && u.applied == 0 && u.targetsMade == 0);
	return true;
}

bool CaseTargetsAgain(std::string& why)
{
	UnitShadow u;
	if(!Init(u, why))
		return false;
	std::vector<uint16_t> z(64 * 64, 0);
	Tex r16;
	CHECK(r16.make(64, 64, DXGI_FORMAT_R16_UNORM, z.data(), 128));
	std::string w;
	bool fatal = false;
	const UnitShadow::MaskFn fn = [](ID3D11RenderTargetView*, uint32_t, uint32_t) {};
	CHECK(u.apply(g_ctx, r16.srv, 3, 100, 60, fn, w, &fatal) && u.targetsMade == 1);
	CHECK(u.apply(g_ctx, r16.srv, 3, 100, 60, fn, w, &fatal) && u.targetsMade == 1);
	CHECK(u.apply(g_ctx, r16.srv, 2, 120, 60, fn, w, &fatal) && u.targetsMade == 2 && u.rawW() == 120);
	std::vector<uint16_t> z4(120 * 60 * 4, 0);
	Tex f16;
	CHECK(f16.make(120, 60, DXGI_FORMAT_R16G16B16A16_FLOAT, z4.data(), 120 * 8));
	CHECK(u.apply(g_ctx, f16.srv, 1, 120, 60, fn, w, &fatal) && u.targetsMade == 3);   // quality 1: no raw target
	CHECK(u.apply(g_ctx, r16.srv, 3, 120, 60, fn, w, &fatal) && u.targetsMade == 4);   // back to 3
	CHECK(!fatal && u.applied == 5 && u.maxCpuMs >= u.lastCpuMs);
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
};
const Case kCases[] = {{"wanted", CaseWanted},       {"init_refused", CaseInitRefused}, {"resample_cpu", CaseResampleCpu}, {"merge_adds", CaseMergeAdds},
                       {"merge_q1", CaseMergeQ1},     {"refusals", CaseRefusals},       {"targets_again", CaseTargetsAgain}};

std::vector<uint8_t> ReadFile(const char* p)
{
	std::vector<uint8_t> b;
	FILE* f = nullptr;
	if(fopen_s(&f, p, "rb") || !f)
		return b;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(n > 0)
	{
		b.resize((size_t)n);
		if(fread(b.data(), 1, b.size(), f) != b.size())
			b.clear();
	}
	fclose(f);
	return b;
}

int Pair(int argc, char** argv)
{
	if(argc != 10)
	{
		printf("usage: test_unit_shadow pair MASK.bin W H PITCH BLUR.bin BW BH BPITCH\n");
		return 2;
	}
	const uint32_t w = (uint32_t)atoi(argv[3]), h = (uint32_t)atoi(argv[4]), pitch = (uint32_t)atoi(argv[5]);
	const uint32_t bw = (uint32_t)atoi(argv[7]), bh = (uint32_t)atoi(argv[8]), bpitch = (uint32_t)atoi(argv[9]);
	std::vector<uint8_t> mb = ReadFile(argv[2]), bb = ReadFile(argv[6]);
	if(mb.size() < (size_t)pitch * (h - 1) + (size_t)w * 8 || bb.size() < (size_t)bpitch * (bh - 1) + (size_t)bw * 2)
	{
		printf("CASE pair FAIL the files are shorter than their descriptions (%zu, %zu bytes)\n", mb.size(), bb.size());
		return 1;
	}
	mb.resize((size_t)pitch * h, 0);   // the last row is stored tight
	std::vector<uint16_t> blur((size_t)bw * bh);
	for(uint32_t y = 0; y < bh; ++y)
		memcpy(&blur[(size_t)y * bw], &bb[(size_t)y * bpitch], (size_t)bw * 2);
	UnitShadow u;
	std::string why;
	if(!Init(u, why))
	{
		printf("CASE pair FAIL %s\n", why.c_str());
		return 1;
	}
	Tex raw, out;
	if(!raw.make(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, mb.data(), pitch) || !out.make(bw, bh, DXGI_FORMAT_R16_UNORM, nullptr, 0))
	{
		printf("CASE pair FAIL textures\n");
		return 1;
	}
	u.resample(g_ctx, raw.srv, w, h, out.rtv, bw, bh, false);
	uint32_t rw = 0, rh = 0;
	const std::vector<uint16_t> gpu = ReadR16(out.srv, rw, rh);
	const DiffStats s = Diff(gpu, blur);
	size_t eq = 0, differ = 0;
	int maxAll = 0;
	for(size_t i = 0; i < gpu.size() && i < blur.size(); ++i)
	{
		const int d = abs((int)gpu[i] - (int)blur[i]);
		eq += d == 0 && (gpu[i] || blur[i]);
		differ += d != 0;
		maxAll = std::max(maxAll, d);
	}
	const bool ok = s.maxd <= 256 && s.within64 >= 97.0;
	printf("CASE pair %s mask %ux%u blur %ux%u: %zu texels either side shades: max %d, median %.0f, equal %zu, within 64 %.2f %%; whole map: max %d, "
	       "%zu of %zu differ\n",
	       ok ? "PASS" : "FAIL", w, h, bw, bh, s.n, s.maxd, s.median, eq, s.within64, maxAll, differ, gpu.size());
	return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	const char* e = getenv("C5R_EXE");
	if(!e || !*e)
	{
		printf("test_unit_shadow: C5R_EXE not set (the resample programs come from the EXE)\n");
		return 99;
	}
	g_exe.assign(e, e + strlen(e));
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("test_unit_shadow: no WARP device\n");
		return 99;
	}
	if(argc >= 2 && !strcmp(argv[1], "pair"))
		return Pair(argc, argv);
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			if(!strcmp(argv[i], c.name) || !strcmp(argv[i], "all"))
				want = true;
		if(!want)
			continue;
		std::string why;
		const bool ok = c.fn(why);
		run++;
		fails += ok ? 0 : 1;
		printf("CASE %s %s %s\n", c.name, ok ? "PASS" : "FAIL", why.c_str());
	}
	if(!run)
	{
		printf("test_unit_shadow: no case matches\n");
		return 2;
	}
	g_ctx->Release();
	g_dev->Release();
	return fails;
}
