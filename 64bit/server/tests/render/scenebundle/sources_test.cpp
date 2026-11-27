// sources_test.cpp - tests of the offline scene harness's sources, the clock passed in and the settle call (epic 0301,
// task 3903; contract: research/06-tooling-and-workflows/offline-scene-harness.md sections 4, 6 and 10).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). The section, image and field builders follow
// server/tests/render/water/water_scene_test.cpp and server/tests/render/overlay/test_scene_overlay.cpp (this repository).
//
// Everything is synthetic, made by the test (nothing from the game's files is stored, D83): a main section with a fake
// camera block, a height-field tap and a water tap image published as shell_composite.dll does (shell_shared.h), fake
// overlay textures, the feed driven through SrvFeed's writer calls. The GPU cases run on a WARP device with the EXE's
// programs (C5R_EXE, read-only) and the installed game's water art (read-only; C5_GAME_DIR, else C5R_GAME_DIR, else the
// setting render_game_dir, never the Steam install, ticket 0589); a missing input, or an EXE that is not build 4390913
// (../test_install.h), FAILS the case (a skip would count as a failure, COMMON 9).
// Output: one line per case, `CASE <name> PASS|FAIL <detail>`; exit code = the number of failed cases, 2 on usage (no
// case matches), 99 without a WARP device.
//
//   sources_test.exe [case ...]      (no argument: every case)
#include "srv_scene_sources.h"
#include "srv_scene_bundle.h"
#include "srv_render_scene.h"
#include "srv_residency_source.h"
#include "srv_water_scene.h"
#include "srv_water_tap.h"
#include "srv_options.h"
#include "feed/srv_feed.h"
#include "c5r/heightfield/heightfield.h"
#include "../test_install.h"   // ticket 0589
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvrender;
namespace cw = c5r::water;
namespace c5h = c5r::heightfield;

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::wstring g_exe, g_game;
std::vector<std::string> g_log;
void HostLog(const char* line) { g_log.push_back(line); }
int CountLog(const char* needle)
{
	int n = 0;
	for(const auto& l : g_log)
		if(l.find(needle) != std::string::npos)
			n++;
	return n;
}

#define EXPECT(cond, ...)                                                                                                      \
	do                                                                                                                         \
	{                                                                                                                          \
		if(!(cond))                                                                                                            \
		{                                                                                                                      \
			char b_[700];                                                                                                      \
			_snprintf_s(b_, _TRUNCATE, __VA_ARGS__);                                                                          \
			why = std::string(b_) + " [line " + std::to_string(__LINE__) + "]";                                              \
			return false;                                                                                                      \
		}                                                                                                                      \
	} while(0)

uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

int g_serial = 0;

// ---- the main section --------------------------------------------------------------------------------------------------
struct Main
{
	HANDLE h = nullptr;
	VPShellHeader* hdr = nullptr;
	std::wstring name;
	std::vector<HANDLE> secs;
	Main()
	{
		wchar_t nm[96];
		swprintf_s(nm, L"Local\\VPSourcesTest.%u.%d", GetCurrentProcessId(), ++g_serial);
		name = nm;
		h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, nm);
		hdr = h ? (VPShellHeader*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES) : nullptr;
		if(hdr)
		{
			memset(hdr, 0, VPSHELL_SECTION_BYTES);
			hdr->magic = VPSHELL_MAGIC;
			hdr->version = VPSHELL_VERSION;
		}
	}
	~Main()
	{
		if(hdr)
			UnmapViewOfFile(hdr);
		if(h)
			CloseHandle(h);
		for(HANDLE s : secs)
			CloseHandle(s);
	}
	// The camera block: a camera at (cx, 0, z) looking straight down, numViews 1, the grid constants of a field of
	// samples x samples (the overlay test's camera, test_scene_overlay.cpp).
	void camera(float cx, int64_t frame, uint32_t samples, uint32_t gridSeq = 1, float z = 120.0f)
	{
		VPShellCamera* c = VPSHELL_CAMERA(hdr);
		const uint32_t s = c->seq;
		c->seq = s | 1;
		MemoryBarrier();
		c->status = VPCAM_OK;
		c->presentCount = frame;
		c->frameStamp = frame;
		c->numViews = 1;
		const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, 0, -z, 1};
		const float w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, cx, 0, z, 1};
		memcpy(c->view[0], v, sizeof(v));
		memcpy(c->camWorld, w, sizeof(w));
		const float n = 1.0f, f = 1000.0f;
		const float ys = 1.0f / tanf(15.0f * 3.14159265f / 180.0f), xs = ys * 120.0f / 160.0f;
		const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
		memcpy(c->proj, p, sizeof(p));
		c->bbW = 160;
		c->bbH = 120;
		c->gridSeq = gridSeq;
		c->gridValid = 1;
		c->wrapX = 1;
		c->wrapPeriod = (float)samples - 1.0f;
		c->sx = 1.0f;
		c->sy = 0.866f;
		c->heightScale = 0.2f;
		c->ox = samples / 2.0f;
		c->oy = samples / 2.0f * 0.866f;
		c->samplesX = samples;
		c->samplesY = samples;
		MemoryBarrier();
		c->seq = (s | 1) + 1;
	}
	HANDLE section(const std::vector<uint8_t>& img, const wchar_t* tag, uint32_t a, uint32_t b, std::wstring& nm)
	{
		wchar_t n[96];
		swprintf_s(n, L"Local\\VPSourcesTest%s.%u.%d.%u.%u", tag, GetCurrentProcessId(), g_serial, a, b);
		nm = n;
		HANDLE s = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)img.size(), n);
		void* v = s ? MapViewOfFile(s, FILE_MAP_WRITE, 0, 0, 0) : nullptr;
		if(v)
		{
			memcpy(v, img.data(), img.size());
			UnmapViewOfFile(v);
		}
		secs.push_back(s);
		return s;
	}
};

// ---- the height field ---------------------------------------------------------------------------------------------------
// 4 x 4 plots (256 x 256 samples); `height` in u16 (20 << 8: land, class 2; 0: below the water at z 10)
c5h::HeightGrid Field(uint16_t height = (uint16_t)(20 << 8))
{
	c5h::HeightGrid g;
	g.mapW = 4;
	g.mapH = 4;
	g.samplesX = 256;
	g.samplesY = 256;
	g.wrapX = true;
	g.sx = 1.0f;
	g.sy = 0.866f;
	g.sz = 0.2f;
	g.ox = 128.0f;
	g.oy = 128.0f * 0.866f;
	g.waterZ = 10.0f;
	g.h.assign((size_t)g.samplesX * g.samplesY, height);
	return g;
}

std::vector<uint8_t> FieldBytes(uint16_t height = (uint16_t)(20 << 8))
{
	std::vector<uint8_t> img;
	c5h::writeTap(Field(height), img);
	return img;
}

void PublishField(Main& m, const std::vector<uint8_t>& img, uint32_t load = 1)
{
	std::wstring nm;
	m.section(img, L"Tap", load, 1, nm);
	VPShellTap* t = VPSHELL_TAP(m.hdr);
	t->seq = 1;
	t->state = VPTAP_READY;
	t->loadSeq = load;
	t->mapW = 4;
	t->mapH = 4;
	t->samplesX = 256;
	t->samplesY = 256;
	t->flags = 1;
	t->payloadBytes = img.size();
	t->copySeq = 1;
	wcscpy_s(t->sectionName, nm.c_str());
	MemoryBarrier();
	t->seq = 2;
}

// ---- the water image (water_scene_test.cpp's Image, with the map size as a parameter) ------------------------------------
struct Block
{
	std::vector<uint8_t> data;
	VPShellWaterRecord r = {};
};
void PutF(std::vector<uint8_t>& b, size_t off, float v) { memcpy(b.data() + off, &v, 4); }
const float kAges[4] = {1.0f, 8.95f, -1.0f, 3.0f};

std::vector<uint8_t> WaveObj(uint32_t chunks, uint32_t waves)
{
	std::vector<uint8_t> o(0xB0, 0);
	auto u = [&](size_t off, uint32_t v) { memcpy(o.data() + off, &v, 4); };
	auto f = [&](size_t off, float v) { memcpy(o.data() + off, &v, 4); };
	u(0x08, 8);
	u(0x0C, 2);
	u(0x10, 16);
	f(0x1C, 0.08f);
	f(0x20, 0.02f);
	f(0x24, 9.0f);
	f(0x28, 0.05f);
	f(0x2C, 4.0f);
	f(0x30, 6.5f);
	f(0x34, 8.7f);
	f(0x38, 2.1f);
	f(0x3C, 5.0f);
	f(0x40, 0.0478f);
	f(0x44, 0.0478f);
	f(0x48, 0.0478f);
	f(0x50, 128.0f);
	u(0x54, chunks ? 0x12345678u : 0);
	u(0x58, chunks);
	u(0x5C, waves ? 0x23456780u : 0);
	u(0x60, waves);
	return o;
}

// The wave vertices of the format the wave shader takes (water-system.md 12.1: POSITION float16 xy = the wave's centre,
// TEXCOORD = the corner and its uv, BLENDINDICES = the wave within the chunk): two waves of four corners near (8k, 8k).
std::vector<uint8_t> WaveVertices(int k)
{
	std::vector<uint8_t> v(8 * 12, 0);
	const uint16_t half[3] = {0x0000, 0x4800, 0x4C00};   // 0, 8, 16
	for(int w = 0; w < 2; ++w)
		for(int c = 0; c < 4; ++c)
		{
			uint8_t* p = v.data() + 12 * (4 * w + c);
			const uint16_t h = half[(k + w) % 3];
			memcpy(p, &h, 2);
			memcpy(p + 2, &h, 2);
			p[4] = p[6] = (uint8_t)((c & 1) ? 255 : 0);
			p[5] = p[7] = (uint8_t)((c & 2) ? 255 : 0);
			p[8] = (uint8_t)w;
		}
	return v;
}

std::vector<uint8_t> Image(uint32_t load, uint32_t pid, int waveChunks, uint32_t mapWH, bool visibleWaves = false)
{
	std::vector<Block> bl;
	auto add = [&](uint32_t sys, uint32_t role, std::vector<uint8_t> d) -> uint32_t {
		Block b;
		b.r.system = sys;
		b.r.role = role;
		uint32_t idx = 0;
		for(const auto& x : bl)
			if(x.r.system == sys && x.r.role == role)
				idx++;
		b.r.index = idx;
		b.r.vbRecord = b.r.ibRecord = 0xFFFFFFFFu;
		b.data = std::move(d);
		bl.push_back(b);
		return (uint32_t)bl.size() - 1;
	};
	auto vb = [](uint32_t n, uint32_t stride, uint8_t seed) {
		std::vector<uint8_t> v(n * stride);
		for(size_t i = 0; i < v.size(); ++i)
			v[i] = (uint8_t)(i * 7 + seed);
		return v;
	};
	auto ib = [](uint32_t n, uint32_t verts, uint32_t e) {
		std::vector<uint8_t> v(n * e);
		for(uint32_t i = 0; i < n; ++i)
		{
			uint32_t x = i % verts;
			memcpy(v.data() + i * e, &x, e);
		}
		return v;
	};
	auto draw = [&](uint32_t sys, uint32_t v, uint32_t i, uint32_t pass, uint32_t material) {
		uint32_t d = add(sys, VPWROLE_DRAW, {});
		Block& b = bl[d];
		b.r.topology = 4;
		b.r.vbRecord = v;
		b.r.ibRecord = i;
		b.r.vertexCount = bl[v].r.count;
		b.r.indexCount = bl[i].r.count;
		b.r.material = material;
		b.r.pass = pass;
	};
	add(VPWSYS_RIVER, VPWROLE_OBJECT, std::vector<uint8_t>(0x30, 1));
	add(VPWSYS_RIVER, VPWROLE_OBJECT, std::vector<uint8_t>(0x80, 2));
	{
		uint32_t v = add(VPWSYS_RIVER, VPWROLE_VB, vb(4, 32, 3));
		bl[v].r.stride = 32;
		bl[v].r.count = 4;
		uint32_t i = add(VPWSYS_RIVER, VPWROLE_IB, ib(6, 4, 4));
		bl[i].r.dxgiFormat = 42;
		bl[i].r.count = 6;
		draw(VPWSYS_RIVER, v, i, 4, 91);
	}
	std::vector<uint8_t> water(0xEC, 0);
	PutF(water, 0xD8, 0.18f);
	PutF(water, 0xDC, 0.145891f);
	PutF(water, 0xE0, 0.03f);
	PutF(water, 0xE4, 0.00607878f);
	uint32_t three = 3;
	memcpy(water.data() + 0xE8, &three, 4);
	add(VPWSYS_OCEAN, VPWROLE_OBJECT, water);
	const uint32_t nw = 2u * (uint32_t)waveChunks;
	add(VPWSYS_WAVE, VPWROLE_OBJECT, WaveObj((uint32_t)waveChunks, nw));
	if(waveChunks)
	{
		std::vector<uint8_t> arr(36u * waveChunks, 0);
		for(int k = 0; k < waveChunks; ++k)
		{
			const uint32_t first = 2u * k, n = 2;
			const float bnd[4] = {visibleWaves ? -100.0f : 10.0f * k, visibleWaves ? -100.0f : 0.0f, visibleWaves ? 100.0f : 10.0f * k + 9.0f,
			                      visibleWaves ? 100.0f : 9.0f};
			memcpy(arr.data() + 36 * k + 0x0C, &first, 4);
			memcpy(arr.data() + 36 * k + 0x10, &n, 4);
			memcpy(arr.data() + 36 * k + 0x14, bnd, 16);
		}
		bl[add(VPWSYS_WAVE, VPWROLE_OBJECT, arr)].r.exeOffset = 0x54;
		std::vector<uint8_t> wv(16u * nw, 0);
		for(uint32_t i = 0; i < nw; ++i)
		{
			const float rec[4] = {0.0f, 0.0f, kAges[i % 4], 0.0f};
			memcpy(wv.data() + 16 * i, rec, 16);
		}
		uint32_t wa = add(VPWSYS_WAVE, VPWROLE_OBJECT, wv);
		bl[wa].r.exeOffset = 0x5C;
		bl[wa].r.flags = 2;
	}
	for(int k = 0; k < waveChunks; ++k)
	{
		bl[add(VPWSYS_WAVE, VPWROLE_OBJECT, std::vector<uint8_t>(0x80, 6))].r.exeOffset = 0x54;
		uint32_t v = add(VPWSYS_WAVE, VPWROLE_VB, visibleWaves ? WaveVertices(k) : vb(8, 12, (uint8_t)(9 + k)));
		bl[v].r.stride = 12;
		bl[v].r.count = 8;
		uint32_t i = add(VPWSYS_WAVE, VPWROLE_IB, ib(12, 8, 2));
		bl[i].r.dxgiFormat = 57;
		bl[i].r.count = 12;
		draw(VPWSYS_WAVE, v, i, 0, 92);
	}
	uint64_t off = sizeof(VPShellWaterImage) + bl.size() * sizeof(VPShellWaterRecord);
	for(auto& b : bl)
	{
		if(b.data.empty())
			continue;
		off = (off + 15) & ~15ull;
		b.r.dataOffset = off;
		b.r.dataBytes = b.data.size();
		b.r.fnv1a64 = Fnv(b.data.data(), b.data.size());
		off += b.data.size();
	}
	std::vector<uint8_t> img((size_t)off, 0);
	VPShellWaterImage h = {};
	h.magic = VPSHELL_WATER_MAGIC;
	h.version = VPSHELL_WATER_IMAGE_VERSION;
	h.headerBytes = sizeof(h);
	h.recordBytes = sizeof(VPShellWaterRecord);
	h.recordCount = (uint32_t)bl.size();
	h.loadSeq = load;
	h.exePid = pid;
	h.mapW = h.mapH = mapWH;
	h.waterZ = 10.117f;
	h.imageBytes = off;
	memcpy(img.data(), &h, sizeof(h));
	for(size_t i = 0; i < bl.size(); ++i)
	{
		memcpy(img.data() + sizeof(h) + i * sizeof(VPShellWaterRecord), &bl[i].r, sizeof(VPShellWaterRecord));
		if(!bl[i].data.empty())
			memcpy(img.data() + bl[i].r.dataOffset, bl[i].data.data(), bl[i].data.size());
	}
	return img;
}

void PublishWater(Main& m, const std::vector<uint8_t>& img, uint32_t load, uint32_t copy, bool badHash = false)
{
	std::wstring nm;
	m.section(img, L"Water", load, copy, nm);
	VPShellWaterTap* t = VPSHELL_WATERTAP(m.hdr);
	t->seq++;
	MemoryBarrier();
	t->state = VPWTAP_READY;
	t->loadSeq = load;
	t->copySeq = copy;
	t->exePid = 4321;
	t->recordCount = ((const VPShellWaterImage*)img.data())->recordCount;
	t->imageBytes = img.size();
	t->sectionBytes = (img.size() + 0xFFFF) & ~0xFFFFull;
	t->fnv1a64 = Fnv(img.data(), img.size()) ^ (badHash ? 1ull : 0ull);
	wcsncpy_s(t->sectionName, nm.c_str(), _TRUNCATE);
	MemoryBarrier();
	t->seq++;
	if(t->seq & 1)
		t->seq++;
}

// What the water sink saw (the recorder's side).
struct WaterCapture
{
	int calls = 0;
	VPShellWaterTap block = {};
	std::vector<uint8_t> image;
	bool haveCam = false;
	uint32_t gridSeq = 0, samplesX = 0, samplesY = 0;
	static void Sink(void* ctx, const VPShellWaterTap& b, const uint8_t* img, size_t n, bool hc, uint32_t gs, uint32_t sx, uint32_t sy)
	{
		WaterCapture* c = (WaterCapture*)ctx;
		c->calls++;
		c->block = b;
		c->image.assign(img, img + n);
		c->haveCam = hc;
		c->gridSeq = gs;
		c->samplesX = sx;
		c->samplesY = sy;
	}
};

bool SameRecords(const WaterTapReader& a, const WaterTapReader& b, std::string& why)
{
	EXPECT(a.records().size() == b.records().size(), "records %zu / %zu", a.records().size(), b.records().size());
	for(size_t i = 0; i < a.records().size(); ++i)
	{
		EXPECT(!memcmp(&a.records()[i].rec, &b.records()[i].rec, sizeof(VPShellWaterRecord)), "record %zu differs", i);
		EXPECT(a.records()[i].data == b.records()[i].data, "record %zu's data differs", i);
	}
	EXPECT(!memcmp(&a.image(), &b.image(), sizeof(VPShellWaterImage)), "image header differs");
	EXPECT(a.loadSeq() == b.loadSeq(), "load seq %u / %u", a.loadSeq(), b.loadSeq());
	return true;
}

// ---- the fake water module (water_scene_test.cpp's, reduced to what the clock cases read) ------------------------------
struct FakeWater : WaterModule
{
	int draws = 0, waveDraws = 0;
	std::vector<cw::WaveChunk> waveChunks;
	std::vector<cw::WaveRecord> lastWaves;
	cw::FrameInputs lastIn;
	std::vector<std::pair<cw::System, cw::Mesh>> meshes;
	c5r::Status init(ID3D11Device*, const std::wstring&, const std::wstring&) override { return c5r::Status::Ok(); }
	c5r::Status setHeightField(const c5h::HeightGrid&) override { return c5r::Status::Ok(); }
	c5r::Status setMesh(cw::System s, const cw::Mesh& m) override
	{
		meshes.push_back({s, m});
		return c5r::Status::Ok();
	}
	c5r::Status setTexture(cw::Slot, ID3D11ShaderResourceView*) override { return c5r::Status::Ok(); }
	c5r::Status draw(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, const cw::FrameInputs& in, const cw::Options&,
	                 cw::FrameStats*) override
	{
		draws++;
		lastIn = in;
		return c5r::Status::Ok();
	}
	void release() override {}
	bool wavesAvailable() const override { return true; }
	c5r::Status setWaveChunks(const std::vector<cw::WaveChunk>& c) override
	{
		waveChunks = c;
		return c5r::Status::Ok();
	}
	c5r::Status drawWaves(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, const cw::WaveObject&, float,
	                      const std::vector<cw::WaveRecord>& w, cw::WaveStats*) override
	{
		waveDraws++;
		lastWaves = w;
		return c5r::Status::Ok();
	}
};

VPShellCamera WCam(uint32_t gridSeq = 1)
{
	VPShellCamera c = {};
	c.gridSeq = gridSeq;
	c.gridValid = 1;
	c.samplesX = c.samplesY = 128;
	return c;
}

c5r::terrain::Camera TCam()
{
	c5r::terrain::Camera c;
	for(int i = 0; i < 4; ++i)
		c.view[5 * i] = c.proj[5 * i] = c.camWorld[5 * i] = 1.0f;
	c.width = 64;
	c.height = 64;
	return c;
}

// One drawn frame's module inputs, as the fake saw them.
struct WaterSeen
{
	float dc = 0, e4 = 0;
	double seconds = 0;
	std::vector<cw::WaveRecord> waves;
	bool operator==(const WaterSeen& o) const
	{
		return !memcmp(&dc, &o.dc, 4) && !memcmp(&e4, &o.e4, 4) && !memcmp(&seconds, &o.seconds, 8) && waves.size() == o.waves.size() &&
		       (waves.empty() || !memcmp(waves.data(), o.waves.data(), waves.size() * sizeof(cw::WaveRecord)));
	}
};

// A water scene with the fake module, without a section (the player's form): the image through loadImage.
struct WaterRig
{
	FakeWater* fake = new FakeWater();
	WaterScene s{std::unique_ptr<WaterModule>(fake)};
	c5h::HeightGrid grid;
	WaterRig(bool freeze, const std::vector<uint8_t>& img, std::string& err)
	{
		grid.mapW = grid.mapH = 2;
		grid.samplesX = grid.samplesY = 128;
		grid.waterZ = 10.117f;
		grid.h.assign(128 * 128, 1000);
		s.attach(nullptr);
		WaterSceneOptions o;
		o.freeze = freeze;
		s.init(g_dev, o);
		s.onField(grid, 1);
		VPShellWaterTap b = {};
		b.state = VPWTAP_READY;
		b.loadSeq = 1;
		b.copySeq = 1;
		b.exePid = 4321;
		b.recordCount = ((const VPShellWaterImage*)img.data())->recordCount;
		b.imageBytes = img.size();
		b.fnv1a64 = Fnv(img.data(), img.size());
		if(!s.loadImage(b, img.data(), img.size(), true, 1, 128, 128))
			err = "loadImage refused: " + (s.reader() ? s.reader()->lastWhy() : std::string("no reader"));
	}
	bool frame(double nowMs, WaterSeen& seen)
	{
		if(!s.decide(WCam(), nowMs))
			return false;
		s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr);
		seen.dc = fake->lastIn.fields.get(0xDC);
		seen.e4 = fake->lastIn.fields.get(0xE4);
		seen.seconds = fake->lastIn.clock.seconds;
		seen.waves = fake->lastWaves;
		return true;
	}
};

// ==== the water tap: sink and file entry =================================================================================
bool CaseWaterLoadEqualsPoll(std::string& why)
{
	Main m;
	m.camera(0, 1, 128);
	WaterTapReader live(m.hdr);
	WaterCapture cap;
	live.setImageSink(&WaterCapture::Sink, &cap);
	PublishWater(m, Image(1, 4321, 2, 2), 1, 1);
	EXPECT(live.poll(), "live poll refused: %d %s", live.lastResult(), live.lastWhy().c_str());
	EXPECT(cap.calls == 1, "sink called %d times", cap.calls);
	EXPECT(cap.haveCam && cap.gridSeq == 1 && cap.samplesX == 128 && cap.samplesY == 128, "sink camera values %d %u %u %u",
	       (int)cap.haveCam, cap.gridSeq, cap.samplesX, cap.samplesY);
	EXPECT(cap.image.size() == cap.block.imageBytes, "sink image %zu bytes, block %llu", cap.image.size(),
	       (unsigned long long)cap.block.imageBytes);
	const VPShellWaterTap* t = VPSHELL_WATERTAP(m.hdr);
	EXPECT(t->ackLoadSeq == 1 && t->ackCopySeq == 1 && t->ackResult == 0, "ack %u/%u %d", t->ackLoadSeq, t->ackCopySeq, t->ackResult);
	WaterTapReader file(nullptr);
	EXPECT(file.loadImage(cap.block, cap.image.data(), cap.image.size(), cap.haveCam, cap.gridSeq, cap.samplesX, cap.samplesY),
	       "loadImage refused: %d %s", file.lastResult(), file.lastWhy().c_str());
	EXPECT(file.accepted == 1 && file.refused == 0 && file.requests == 0, "counters %lld %lld %lld", (long long)file.accepted,
	       (long long)file.refused, (long long)file.requests);
	if(!SameRecords(live, file, why))
		return false;
	EXPECT(live.records().size() > 10, "only %zu records (waves missing?)", live.records().size());
	// the same reader: a second live copy (copy 2) is read and sunk again; refused copies are never sunk
	PublishWater(m, Image(1, 4321, 2, 2), 1, 2, true);
	EXPECT(!live.poll() && live.lastResult() == WTACK_HASH, "bad hash: poll %d", live.lastResult());
	EXPECT(cap.calls == 1, "a refused copy reached the sink (%d calls)", cap.calls);
	EXPECT(t->ackCopySeq == 2 && t->ackResult == WTACK_HASH, "refused copy acked %u %d", t->ackCopySeq, t->ackResult);
	return true;
}

bool CaseWaterSinkNullIsToday(std::string& why)
{
	// without a sink the reader runs exactly as before: same acks, same records, same refusals (map size after the checks)
	Main m;
	m.camera(0, 1, 192);   // 3 x 3 plots: the image says 2 x 2
	WaterTapReader r(m.hdr);
	PublishWater(m, Image(1, 4321, 0, 2), 1, 1);
	EXPECT(!r.poll(), "a map-size mismatch was accepted");
	EXPECT(r.lastResult() == (int32_t)c5r::Err::BadLayout && r.lastWhy().find("map size") != std::string::npos, "result %d %s",
	       r.lastResult(), r.lastWhy().c_str());
	const VPShellWaterTap* t = VPSHELL_WATERTAP(m.hdr);
	EXPECT(t->ackLoadSeq == 1 && t->ackResult == (int32_t)c5r::Err::BadLayout, "ack %u %d", t->ackLoadSeq, t->ackResult);
	EXPECT(r.refused == 1 && r.accepted == 0 && r.records().empty(), "counters");
	// with a sink, the same refusal never reaches it (the map size is checked before the hand-off)
	Main m2;
	m2.camera(0, 1, 192);
	WaterTapReader r2(m2.hdr);
	WaterCapture cap;
	r2.setImageSink(&WaterCapture::Sink, &cap);
	PublishWater(m2, Image(1, 4321, 0, 2), 1, 1);
	EXPECT(!r2.poll() && r2.lastResult() == (int32_t)c5r::Err::BadLayout && cap.calls == 0, "with a sink: poll %d, sink %d",
	       r2.lastResult(), cap.calls);
	return true;
}

// The negatives of the file entry (contract 10): each is refused with poll()'s code, counted, and leaves the records.
bool CaseWaterLoadRefused(std::string& why)
{
	Main m;
	m.camera(0, 1, 128);
	WaterTapReader live(m.hdr);
	WaterCapture cap;
	live.setImageSink(&WaterCapture::Sink, &cap);
	PublishWater(m, Image(1, 4321, 1, 2), 1, 1);
	EXPECT(live.poll() && cap.calls == 1, "live poll");
	WaterTapReader f(nullptr);
	EXPECT(f.loadImage(cap.block, cap.image.data(), cap.image.size(), true, 1, 128, 128), "the good image refused: %s", f.lastWhy().c_str());
	const size_t good = f.records().size();
	struct Neg
	{
		const char* name;
		int32_t code;
		const char* text;
	};
	int n = 0;
	auto check = [&](const Neg& g, const VPShellWaterTap& b, const std::vector<uint8_t>& img, bool hc, uint32_t gs, uint32_t sx,
	                 uint32_t sy) -> std::string {
		const int64_t ref = f.refused;
		const bool ok = f.loadImage(b, img.data(), img.size(), hc, gs, sx, sy);
		n++;
		if(ok)
			return std::string(g.name) + ": accepted";
		if(f.lastResult() != g.code)
			return std::string(g.name) + ": result " + std::to_string(f.lastResult()) + " (" + f.lastWhy() + "), expected " +
			       std::to_string(g.code);
		if(f.lastWhy().find(g.text) == std::string::npos)
			return std::string(g.name) + ": why '" + f.lastWhy() + "' lacks '" + g.text + "'";
		if(f.refused != ref + 1 || f.records().size() != good)
			return std::string(g.name) + ": counters or records changed";
		return "";
	};
	std::string e;
	// a bad hash: one byte of a record's data changed
	std::vector<uint8_t> bad = cap.image;
	const VPShellWaterRecord* r0 = (const VPShellWaterRecord*)(bad.data() + sizeof(VPShellWaterImage));
	bad[(size_t)r0->dataOffset] ^= 0x5A;
	if(!(e = check({"record hash", WTACK_HASH, "FNV-1a"}, cap.block, bad, true, 1, 128, 128)).empty())
		EXPECT(false, "%s", e.c_str());
	// the image hash of the block
	VPShellWaterTap b2 = cap.block;
	b2.fnv1a64 ^= 2;
	if(!(e = check({"image hash", WTACK_HASH, "image FNV-1a"}, b2, cap.image, true, 1, 128, 128)).empty())
		EXPECT(false, "%s", e.c_str());
	// a stale grid seq
	if(!(e = check({"stale", WTACK_STALE, "stale"}, cap.block, cap.image, true, 2, 128, 128)).empty())
		EXPECT(false, "%s", e.c_str());
	// the map size
	if(!(e = check({"map size", (int32_t)c5r::Err::BadLayout, "map size"}, cap.block, cap.image, true, 1, 256, 256)).empty())
		EXPECT(false, "%s", e.c_str());
	// truncated: the image shorter than the block says
	std::vector<uint8_t> shortImg(cap.image.begin(), cap.image.end() - 16);
	if(!(e = check({"truncated", (int32_t)c5r::Err::Truncated, "image"}, cap.block, shortImg, true, 1, 128, 128)).empty())
		EXPECT(false, "%s", e.c_str());
	// the block's limits
	VPShellWaterTap b3 = cap.block;
	b3.recordCount = VPSHELL_WATER_MAX_RECORDS + 1;
	if(!(e = check({"limits", (int32_t)c5r::Err::BadLayout, "announces"}, b3, cap.image, true, 1, 128, 128)).empty())
		EXPECT(false, "%s", e.c_str());
	// a version the reader does not know
	std::vector<uint8_t> ver = cap.image;
	((VPShellWaterImage*)ver.data())->version = 99;
	if(!(e = check({"version", WTACK_VERSION, "version"}, cap.block, ver, true, 1, 128, 128)).empty())
		EXPECT(false, "%s", e.c_str());
	EXPECT(f.accepted == 1 && f.refused == n, "accepted %lld refused %lld of %d", (long long)f.accepted, (long long)f.refused, n);
	EXPECT(CountLog("recorded image of load seq 1 copy 1 refused") == n, "refusal lines %d", CountLog("recorded image of load seq 1 copy 1 refused"));
	return true;
}

// WaterScene::loadImage: the scene without a section uses the recorded image as poll() uses an accepted copy.
bool CaseWaterSceneLoadImage(std::string& why)
{
	Main m;
	m.camera(0, 1, 128);
	FakeWater* fl = new FakeWater();
	WaterScene live{std::unique_ptr<WaterModule>(fl)};
	live.attach(m.hdr);
	WaterSceneOptions o;
	live.init(g_dev, o);
	c5h::HeightGrid g;
	g.mapW = g.mapH = 2;
	g.samplesX = g.samplesY = 128;
	g.h.assign(128 * 128, 1000);
	live.onField(g, 1);
	WaterCapture cap;
	live.reader()->setImageSink(&WaterCapture::Sink, &cap);
	PublishWater(m, Image(1, 4321, 2, 2), 1, 1);
	live.poll();
	EXPECT(cap.calls == 1 && live.decide(WCam(), 1000.0), "live scene: sink %d, decide %s", cap.calls, WaterWhyName(live.why()));
	std::string err;
	WaterRig file(false, cap.image, err);
	EXPECT(err.empty(), "%s", err.c_str());
	EXPECT(file.s.decide(WCam(), 1000.0), "file scene does not draw: %s", WaterWhyName(file.s.why()));
	EXPECT(file.fake->meshes.size() == fl->meshes.size(), "meshes %zu / %zu", file.fake->meshes.size(), fl->meshes.size());
	EXPECT(file.fake->waveChunks.size() == fl->waveChunks.size() && fl->waveChunks.size() == 2, "wave chunks %zu / %zu",
	       file.fake->waveChunks.size(), fl->waveChunks.size());
	EXPECT(file.s.waves().size() == live.waves().size() &&
	           !memcmp(file.s.waves().data(), live.waves().data(), live.waves().size() * sizeof(cw::WaveRecord)),
	       "waves differ");
	// a refused recorded image: the scene keeps what it had (no image: the placeholder stays)
	WaterRig bad(false, std::vector<uint8_t>(cap.image.begin(), cap.image.end() - 8), err);
	EXPECT(!err.empty() && !bad.s.decide(WCam(), 1000.0) && bad.s.why() == WW_NO_TAP, "a truncated image was used (%s)",
	       WaterWhyName(bad.s.why()));
	return true;
}

// ==== the clock passed in (fake module: what the module is handed per frame) =============================================
// The clock held (the same nowMs every frame) gives render_water_freeze's frames: clock fields, clock seconds and waves.
bool CaseClockHeldEqualsFreeze(std::string& why)
{
	const std::vector<uint8_t> img = Image(1, 4321, 2, 2);
	std::string err;
	WaterRig frozen(true, img, err), held(false, img, err), moving(false, img, err);
	EXPECT(err.empty(), "%s", err.c_str());
	bool differs = false;
	for(int i = 0; i < 12; ++i)
	{
		WaterSeen a, b, c;
		EXPECT(frozen.frame(1000.0 + 33.0 * i, a) && held.frame(7777.0, b) && moving.frame(1000.0 + 33.0 * i, c), "a scene did not draw");
		EXPECT(a == b, "frame %d: frozen dc %.7g e4 %.7g s %.4g, held dc %.7g e4 %.7g s %.4g (waves %zu / %zu)", i, a.dc, a.e4, a.seconds,
		       b.dc, b.e4, b.seconds, a.waves.size(), b.waves.size());
		differs = differs || !(a == c);
	}
	EXPECT(differs, "the moving clock gave the frozen frames too: the case shows nothing");
	EXPECT(frozen.fake->waveDraws == 12 && held.fake->waveDraws == 12, "wave draws %d / %d", frozen.fake->waveDraws, held.fake->waveDraws);
	return true;
}

// setClockState at frame k, then frames k..N with the recorded nowMs = the live frames k..N (waves included: they
// integrate every frame's dt, clamped at 250 ms).
bool CaseClockStateReplay(std::string& why)
{
	const std::vector<uint8_t> img = Image(1, 4321, 2, 2);
	const double now[] = {1000, 1016.5, 1033, 1300, 1301, 1700.25, 1716, 1749, 2100, 2116, 2133, 2150, 2400, 2416};
	const int n = (int)(sizeof(now) / sizeof(now[0])), k = 5;
	std::string err;
	WaterRig live(false, img, err), file(false, img, err), unseeded(false, img, err);
	EXPECT(err.empty(), "%s", err.c_str());
	WaterClockState st;
	std::vector<WaterSeen> seen(n);
	for(int i = 0; i < n; ++i)
	{
		if(i == k)
			st = live.s.clockState();   // the recorder's water_clock.bin: before frame k's draw
		EXPECT(live.frame(now[i], seen[i]), "live frame %d not drawn", i);
	}
	EXPECT(st.clockStartMs == 1000.0 && st.waveLastMs == now[k - 1] && st.lastNowMs == now[k - 1] && st.waves.size() == 4,
	       "state at frame %d: start %g wave %g last %g, %zu waves", k, st.clockStartMs, st.waveLastMs, st.lastNowMs, st.waves.size());
	EXPECT(file.s.setClockState(st), "setClockState refused");
	bool unseededDiffers = false;
	for(int i = k; i < n; ++i)
	{
		WaterSeen b, c;
		EXPECT(file.frame(now[i], b), "file frame %d not drawn", i);
		EXPECT(b == seen[i], "frame %d: live dc %.7g s %.6g wave0 age %.6g, replay dc %.7g s %.6g wave0 age %.6g", i, seen[i].dc,
		       seen[i].seconds, seen[i].waves.empty() ? -9.0 : seen[i].waves[0].age, b.dc, b.seconds, b.waves.empty() ? -9.0 : b.waves[0].age);
		EXPECT(unseeded.frame(now[i], c), "unseeded frame %d", i);
		unseededDiffers = unseededDiffers || !(c == seen[i]);
	}
	EXPECT(unseededDiffers, "a scene without the state gave the same frames: the case shows nothing");
	const WaterClockState end = file.s.clockState(), endLive = live.s.clockState();
	EXPECT(end.waveLastMs == endLive.waveLastMs && end.clockStartMs == endLive.clockStartMs && end.waves.size() == endLive.waves.size() &&
	           !memcmp(end.waves.data(), endLive.waves.data(), end.waves.size() * sizeof(cw::WaveRecord)),
	       "end states differ");
	return true;
}

bool CaseClockStateRefused(std::string& why)
{
	std::string err;
	WaterRig r(false, Image(1, 4321, 2, 2), err);
	EXPECT(err.empty(), "%s", err.c_str());
	WaterSeen s;
	EXPECT(r.frame(500.0, s), "frame");
	const WaterClockState before = r.s.clockState();
	WaterClockState bad = before;
	bad.waves.pop_back();
	EXPECT(!r.s.setClockState(bad), "a wave array of another size was taken");
	bad = before;
	bad.lastNowMs = std::nan("");
	EXPECT(!r.s.setClockState(bad), "a NaN time was taken");
	const WaterClockState after = r.s.clockState();
	EXPECT(after.clockStartMs == before.clockStartMs && after.lastNowMs == before.lastNowMs && after.waves.size() == before.waves.size(),
	       "a refused state changed the scene");
	EXPECT(CountLog("setClockState refused") == 2, "refusal lines %d", CountLog("setClockState refused"));
	return true;
}

// ==== the GPU cases: the scene on WARP ====================================================================================
const uint32_t kW = 160, kH = 120;

struct Target
{
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* dt = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	Target()
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = kW;
		d.Height = kH;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET;
		g_dev->CreateTexture2D(&d, nullptr, &rt);
		if(rt)
			g_dev->CreateRenderTargetView(rt, nullptr, &rtv);
		d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		g_dev->CreateTexture2D(&d, nullptr, &dt);
		if(dt)
			g_dev->CreateDepthStencilView(dt, nullptr, &dsv);
	}
	~Target()
	{
		SafeRelease(dsv);
		SafeRelease(dt);
		SafeRelease(rtv);
		SafeRelease(rt);
	}
	std::vector<uint8_t> pixels()
	{
		std::vector<uint8_t> out;
		D3D11_TEXTURE2D_DESC d;
		rt->GetDesc(&d);
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* s = nullptr;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
			return out;
		g_ctx->CopyResource(s, rt);
		D3D11_MAPPED_SUBRESOURCE map;
		if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &map)))
		{
			out.resize((size_t)kW * kH * 4);
			for(uint32_t y = 0; y < kH; ++y)
				memcpy(&out[(size_t)y * kW * 4], (const uint8_t*)map.pData + (size_t)y * map.RowPitch, kW * 4);
			g_ctx->Unmap(s, 0);
		}
		s->Release();
		return out;
	}
};

struct SceneRig
{
	Main m;
	Scene scene;
	Target target;
	~SceneRig()
	{
		scene.releaseDevice();
		scene.detach();
	}
	// attach: the scene maps the section (the live form); else the player's form (no section)
	bool start(const SceneOptions& o, bool attach, std::string& why)
	{
		if(!m.hdr || !target.rtv || !target.dsv)
		{
			why = "section or target";
			return false;
		}
		if(attach && !scene.attach(m.name))
		{
			why = "attach";
			return false;
		}
		const c5r::Status st = scene.init(g_dev, g_ctx, o);
		if(!st.ok())
		{
			why = "init: " + st.str();
			return false;
		}
		return true;
	}
	SceneFrame frameOf(int64_t id) const
	{
		SceneFrame f;
		SnapCamera(m.hdr, f.cam);
		f.frameId = id;
		return f;
	}
	bool draw(const SceneFrame& f, std::vector<uint8_t>* px, std::string& why)
	{
		c5r::terrain::FrameStats fs;
		const c5r::Status st = scene.draw(g_ctx, target.rtv, target.dsv, kW, kH, f, fs);
		if(!st.ok())
		{
			why = "draw: " + st.str();
			return false;
		}
		if(px)
			*px = target.pixels();
		return true;
	}
};

SceneOptions BaseOptions()
{
	SceneOptions o;
	o.exe = g_exe;
	o.tiles = SceneOptions::kTilesNone;
	o.water = false;
	o.color = c5r::terrain::ColorSource::Constant;
	o.fallback = c5r::terrain::ColorSource::Constant;
	o.overlay = 0;
	return o;
}

bool NeedInputs(std::string& why, bool game)
{
	EXPECT(!g_exe.empty() && GetFileAttributesW(g_exe.c_str()) != INVALID_FILE_ATTRIBUTES, "C5R_EXE missing (%S)", g_exe.c_str());
	const std::string exeWhy = c5test::ExeBuildWhy(g_exe);   // ticket 0589
	EXPECT(exeWhy.empty(), "C5R_EXE: %s", exeWhy.c_str());
	if(game)
	{
		const std::wstring gexe = g_game + L"\\CivilizationV_DX11.exe";
		EXPECT(GetFileAttributesW(gexe.c_str()) != INVALID_FILE_ATTRIBUTES,
		       "the installed game is needed (C5_GAME_DIR or C5R_GAME_DIR, %S); a skip counts as failed", g_game.c_str());
		const std::string gameWhy = c5test::ExeBuildWhy(gexe);
		EXPECT(gameWhy.empty(), "the installed game: %s", gameWhy.c_str());
	}
	return true;
}

// Scene::loadField: the recorded C5HF gives the field the tap gives (the same picture), and refuses what tapPoll refuses.
bool CaseFieldLoadEqualsTap(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SceneRig a, b;
	a.m.camera(0, 10, 256);
	b.m.camera(0, 10, 256);
	PublishField(a.m, FieldBytes());
	std::string e;
	EXPECT(a.start(BaseOptions(), true, e) && b.start(BaseOptions(), false, e), "%s", e.c_str());
	a.scene.poll();
	EXPECT(a.scene.haveField() && a.scene.tapLoads == 1, "tap: no field");
	VPShellCamera cam;
	SnapCamera(b.m.hdr, cam);
	const std::vector<uint8_t> c5 = FieldBytes();
	const c5r::Status st = b.scene.loadField(c5.data(), c5.size(), 1, &cam);
	EXPECT(st.ok() && b.scene.haveField(), "loadField: %s", st.str().c_str());
	std::vector<uint8_t> pa, pb;
	EXPECT(a.draw(a.frameOf(10), &pa, e) && b.draw(b.frameOf(10), &pb, e), "%s", e.c_str());
	EXPECT(!pa.empty() && pa == pb, "pictures differ (%zu / %zu bytes)", pa.size(), pb.size());
	size_t drawn = 0;
	for(size_t i = 3; i < pa.size(); i += 4)
		drawn += pa[i] != 0;
	EXPECT(drawn > pa.size() / 8, "only %zu pixels drawn: the case shows nothing", drawn);
	return true;
}

bool CaseFieldLoadRefused(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SceneRig b;
	b.m.camera(0, 10, 256);
	std::string e;
	EXPECT(b.start(BaseOptions(), false, e), "%s", e.c_str());
	VPShellCamera cam;
	SnapCamera(b.m.hdr, cam);
	const std::vector<uint8_t> c5 = FieldBytes();
	c5r::Status st = b.scene.loadField(c5.data(), c5.size(), 2, &cam);   // the camera's grid is load 1
	EXPECT(st.code == c5r::Err::BadLayout && st.msg.find("stale") != std::string::npos && !b.scene.haveField(), "stale: %s", st.str().c_str());
	VPShellCamera big = cam;
	big.samplesX = big.samplesY = 512;
	st = b.scene.loadField(c5.data(), c5.size(), 1, &big);
	EXPECT(st.code == c5r::Err::BadLayout && st.msg.find("map size") != std::string::npos && !b.scene.haveField(), "map size: %s",
	       st.str().c_str());
	st = b.scene.loadField(c5.data(), 100, 1, &cam);
	EXPECT(!st.ok() && !b.scene.haveField(), "truncated header accepted");
	st = b.scene.loadField(c5.data(), c5.size() - 1000, 1, &cam);
	EXPECT(!st.ok() && !b.scene.haveField(), "truncated payload accepted: %s", st.str().c_str());
	std::vector<uint8_t> magic = c5;
	magic[0] ^= 0xFF;
	st = b.scene.loadField(magic.data(), magic.size(), 1, &cam);
	EXPECT(st.code == c5r::Err::BadMagic && !b.scene.haveField(), "bad magic: %s", st.str().c_str());
	st = b.scene.loadField(c5.data(), c5.size(), 1, &cam);
	EXPECT(st.ok() && b.scene.haveField(), "the good field after the refusals: %s", st.str().c_str());
	return true;
}

// The water in the scene with the real module: the scene's clock passed in (SceneFrame::nowMs) held = render_water_freeze's
// picture; setClockState at frame k then the recorded nowMs = the live pictures, pixel for pixel.
struct WaterSceneRig : SceneRig
{
	std::vector<uint8_t> img;
	bool begin(bool freeze, std::string& why)
	{
		m.camera(0.0f, 10, 256, 1, 160.0f);
		SceneOptions o = BaseOptions();
		o.water = true;
		o.waterMode = WaterSceneOptions::kFull;
		o.waterFreeze = freeze;
		o.install = g_game;
		o.exe = g_game + L"\\CivilizationV_DX11.exe";
		if(!start(o, false, why))
			return false;
		VPShellCamera cam;
		SnapCamera(m.hdr, cam);
		const std::vector<uint8_t> c5 = FieldBytes(0);   // below the water everywhere: ocean over the whole map
		const c5r::Status st = scene.loadField(c5.data(), c5.size(), 1, &cam);
		if(!st.ok())
		{
			why = "loadField: " + st.str();
			return false;
		}
		img = Image(1, 4321, 2, 4, true);
		VPShellWaterTap b = {};
		b.state = VPWTAP_READY;
		b.loadSeq = 1;
		b.copySeq = 1;
		b.exePid = 4321;
		b.recordCount = ((const VPShellWaterImage*)img.data())->recordCount;
		b.imageBytes = img.size();
		b.fnv1a64 = Fnv(img.data(), img.size());
		if(!scene.water().loadImage(b, img.data(), img.size(), true, 1, 256, 256))
		{
			why = "water loadImage: " + scene.water().reader()->lastWhy();
			return false;
		}
		return true;
	}
	bool shot(double nowMs, std::vector<uint8_t>& px, std::string& why)
	{
		SceneFrame f = frameOf(10);
		f.nowMs = nowMs;
		if(!draw(f, &px, why))
			return false;
		if(scene.water().why() != WW_MODULE)
		{
			why = std::string("the module does not draw: ") + WaterWhyName(scene.water().why());
			return false;
		}
		return true;
	}
};

bool CaseSceneClockHeldEqualsFreeze(std::string& why)
{
	if(!NeedInputs(why, true))
		return false;
	WaterSceneRig frozen, held, moving;
	std::string e;
	EXPECT(frozen.begin(true, e) && held.begin(false, e) && moving.begin(false, e), "%s", e.c_str());
	bool differs = false;
	for(int i = 0; i < 8; ++i)
	{
		std::vector<uint8_t> a, b, c;
		EXPECT(frozen.shot(1000.0 + 400.0 * i, a, e) && held.shot(5000.0, b, e) && moving.shot(1000.0 + 400.0 * i, c, e), "frame %d: %s", i,
		       e.c_str());
		size_t d = 0;
		for(size_t j = 0; j < a.size() && j < b.size(); ++j)
			d += a[j] != b[j];
		EXPECT(!a.empty() && a == b, "frame %d: frozen and held differ in %zu bytes", i, d);
		differs = differs || a != c;
	}
	char line[1500];
	moving.scene.water().secondLine(line, sizeof(line));
	// the same scene without water: the water must show in the picture
	SceneRig dry;
	dry.m.camera(0.0f, 10, 256, 1, 160.0f);
	EXPECT(dry.start(BaseOptions(), false, e), "%s", e.c_str());
	VPShellCamera dc;
	SnapCamera(dry.m.hdr, dc);
	const std::vector<uint8_t> c5 = FieldBytes(0);
	EXPECT(dry.scene.loadField(c5.data(), c5.size(), 1, &dc).ok(), "dry field");
	std::vector<uint8_t> pd, pf;
	EXPECT(dry.draw(dry.frameOf(10), &pd, e) && frozen.shot(9000.0, pf, e), "%s", e.c_str());
	size_t wet = 0;
	for(size_t j = 0; j < pd.size() && j < pf.size(); ++j)
		wet += pd[j] != pf[j];
	EXPECT(differs, "the moving clock drew the frozen picture: the case shows nothing (%zu bytes differ from the dry picture; %s)", wet, line);
	EXPECT(wet > pd.size() / 10, "the water shows in only %zu bytes of %zu", wet, pd.size());
	EXPECT(frozen.scene.water().waveDrawsTotal == 9 && held.scene.water().waveDrawsTotal == 8, "wave frames %lld / %lld",
	       (long long)frozen.scene.water().waveDrawsTotal, (long long)held.scene.water().waveDrawsTotal);
	return true;
}

bool CaseSceneClockStateReplay(std::string& why)
{
	if(!NeedInputs(why, true))
		return false;
	const double now[] = {1000, 1016.5, 1033, 1450, 1460, 1900.25, 1916, 1949, 2600, 2616, 2633};
	const int n = (int)(sizeof(now) / sizeof(now[0])), k = 4;
	WaterSceneRig live, file;
	std::string e;
	EXPECT(live.begin(false, e) && file.begin(false, e), "%s", e.c_str());
	std::vector<std::vector<uint8_t>> px(n);
	WaterClockState st;
	for(int i = 0; i < n; ++i)
	{
		if(i == k)
			st = live.scene.water().clockState();
		EXPECT(live.shot(now[i], px[i], e), "live %d: %s", i, e.c_str());
	}
	EXPECT(file.scene.water().setClockState(st), "setClockState refused");
	bool moved = false;
	for(int i = k; i < n; ++i)
	{
		std::vector<uint8_t> b;
		EXPECT(file.shot(now[i], b, e), "replay %d: %s", i, e.c_str());
		EXPECT(b == px[i], "frame %d: the replay differs from the live picture", i);
		moved = moved || px[i] != px[k];
	}
	EXPECT(moved, "the live frames did not change over the span: the case shows nothing");
	return true;
}

// ==== the overlay =========================================================================================================
OverlayTexels Texels(uint32_t seed, uint32_t size = 2048, uint32_t mips = 12, uint32_t fmt = 28)
{
	OverlayTexels t;
	t.width = t.height = size;
	t.mips = mips;
	t.dxgiFormat = fmt;
	t.bytes.resize((size_t)OverlayTightBytes(size, size, mips));
	uint32_t x = seed * 2654435761u + 1;
	for(size_t i = 0; i < t.bytes.size(); ++i)
	{
		x = x * 1664525u + 1013904223u;
		t.bytes[i] = (uint8_t)(x >> 24);
	}
	return t;
}

ID3D11ShaderResourceView* TextureOf(const OverlayTexels& t, D3D11_USAGE usage = D3D11_USAGE_DEFAULT)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = t.width;
	d.Height = t.height;
	d.MipLevels = t.mips;
	d.ArraySize = 1;
	d.Format = (DXGI_FORMAT)t.dxgiFormat;
	d.SampleDesc.Count = 1;
	d.Usage = usage;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd[16] = {};
	size_t off = 0;
	for(uint32_t m = 0; m < t.mips; ++m)
	{
		const uint32_t mw = std::max(1u, t.width >> m), mh = std::max(1u, t.height >> m);
		sd[m].pSysMem = t.bytes.data() + off;
		sd[m].SysMemPitch = mw * 4;
		off += (size_t)mw * mh * 4;
	}
	ID3D11Texture2D* tex = nullptr;
	ID3D11ShaderResourceView* v = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, sd, &tex)))
		return nullptr;
	D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
	vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	vd.Texture2D.MipLevels = t.mips;
	g_dev->CreateShaderResourceView(tex, &vd, &v);
	tex->Release();
	return v;
}

// An OverlayProj as the EXE builds it (overlay-system.md 4), over the 4 x 4 plot field
void ExeProj(float m[16])
{
	const float wx = 256.0f, wy = 256.0f * 0.866f, sx = 2.0f / wx, sy = 2.0f / wy;
	const float v[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -0.5f, 0, -1.0f, -1.0f, 0.5f, 1};
	memcpy(m, v, sizeof(v));
}

struct FakeOverlay : OverlaySource
{
	OverlayFrame pair;
	bool give = true;
	int acquires = 0;
	bool acquire(OverlayFrame& out) override
	{
		acquires++;
		out = OverlayFrame();
		if(!give)
			return false;
		out = pair;
		return true;
	}
	OverlayTapReader::State state() const override { return give ? OverlayTapReader::State::Active : OverlayTapReader::State::Waiting; }
	uint32_t failReason() const override { return 0; }
	std::string why() const override { return ""; }
};

FileOverlayFrame FrameOf(const OverlayFrame& p, const std::string& sha)
{
	FileOverlayFrame f;
	f.bound = true;
	f.generation = p.generation;
	f.numViews = p.numViews;
	f.presentCount = p.presentCount;
	memcpy(f.overlayProj, p.overlayProj, sizeof(f.overlayProj));
	f.sha256 = sha;
	return f;
}

// FileOverlaySource binds what the live source bound: the same lastOverlay() matrices, the same picture.
bool CaseOverlayFileBindsSame(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	const OverlayTexels t = Texels(7);
	SceneRig a, b;
	a.m.camera(0.0f, 10, 256);
	b.m.camera(0.0f, 10, 256);
	std::string e;
	SceneOptions o = BaseOptions();
	EXPECT(a.start(o, true, e) && b.start(o, false, e), "%s", e.c_str());
	VPShellCamera cam;
	SnapCamera(a.m.hdr, cam);
	const std::vector<uint8_t> c5 = FieldBytes();
	EXPECT(a.scene.loadField(c5.data(), c5.size(), 1, &cam).ok() && b.scene.loadField(c5.data(), c5.size(), 1, &cam).ok(), "fields");
	FakeOverlay* fake = new FakeOverlay();
	fake->pair.srv = TextureOf(t);
	EXPECT(fake->pair.srv, "fake texture");
	fake->pair.numViews = 1;
	ExeProj(fake->pair.overlayProj[0]);
	fake->pair.generation = 41;
	fake->pair.presentCount = 99;
	fake->pair.slot = 2;
	a.scene.setOverlaySource(std::unique_ptr<OverlaySource>(fake));
	std::map<std::string, OverlayTexels> tex;
	const std::string sha = std::string(64, 'a');
	tex[sha] = t;
	std::vector<FileOverlayFrame> frames;
	FileOverlayFrame unbound;
	frames.push_back(unbound);
	frames.push_back(FrameOf(fake->pair, sha));
	FileOverlayControl* ctl = nullptr;
	std::unique_ptr<OverlaySource> fs = MakeFileOverlaySource(g_dev, frames, tex, &ctl, &e);
	EXPECT(fs && ctl && ctl->frameCount() == 2, "file source refused: %s", e.c_str());
	b.scene.setOverlaySource(std::move(fs));
	// frame 0: not bound (the transparent default, state Waiting)
	ctl->seekFrame(0);
	b.scene.poll();
	std::vector<uint8_t> p0;
	EXPECT(b.draw(b.frameOf(10), &p0, e) && b.scene.lastOverlay().srv == nullptr, "frame 0 bound an overlay");
	// frame 1: the pair
	a.scene.poll();
	ctl->seekFrame(1);
	b.scene.poll();
	std::vector<uint8_t> pa, pb;
	EXPECT(a.draw(a.frameOf(11), &pa, e) && b.draw(b.frameOf(11), &pb, e), "%s", e.c_str());
	const c5r::terrain::Overlay& la = a.scene.lastOverlay();
	const c5r::terrain::Overlay& lb = b.scene.lastOverlay();
	EXPECT(la.srv && lb.srv && !memcmp(la.proj, lb.proj, sizeof(la.proj)), "lastOverlay differs (srv %p / %p)", (void*)la.srv, (void*)lb.srv);
	D3D11_SHADER_RESOURCE_VIEW_DESC da, db;
	la.srv->GetDesc(&da);
	lb.srv->GetDesc(&db);
	EXPECT(!memcmp(&da, &db, sizeof(da)), "the SRV descriptions differ");
	EXPECT(pa == pb, "the pictures differ");
	EXPECT(a.scene.ovlBoundFrames == 1 && b.scene.ovlBoundFrames == 1 && b.scene.ovlNoneFrames == 1 && la.srv == fake->pair.srv && lb.srv != la.srv,
	       "bound frames %lld / %lld, none %lld", (long long)a.scene.ovlBoundFrames, (long long)b.scene.ovlBoundFrames, (long long)b.scene.ovlNoneFrames);
	EXPECT(a.scene.ovlPairs == 1 && b.scene.ovlPairs == 1, "pairs %lld / %lld", (long long)a.scene.ovlPairs, (long long)b.scene.ovlPairs);
	// past the end: not bound
	ctl->seekFrame(5);
	b.scene.poll();
	EXPECT(b.draw(b.frameOf(12), nullptr, e) && b.scene.lastOverlay().srv == nullptr, "a frame past the end bound an overlay");
	SafeRelease(fake->pair.srv);   // the fake's pair is the test's (the scene's copy is gone with the next poll)
	return true;
}

bool CaseOverlayFileRefused(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	OverlayFrame p;
	p.numViews = 1;
	ExeProj(p.overlayProj[0]);
	p.generation = 3;
	const std::string sha(64, 'b');
	struct Neg
	{
		const char* name;
		OverlayTexels t;
		uint32_t numViews;
		bool missing;
		const char* text;
	};
	OverlayTexels shortBytes = Texels(1);
	shortBytes.bytes.resize(shortBytes.bytes.size() - 4);
	const Neg negs[] = {
	    {"size 1024", Texels(1, 1024, 11), 1, false, "1024x1024"},
	    {"11 mips", Texels(1, 2048, 11), 1, false, "11 mips"},
	    {"format B8G8R8A8", Texels(1, 2048, 12, 87), 1, false, "format 87"},
	    {"short bytes", shortBytes, 1, false, "bytes"},
	    {"missing texture", Texels(1), 1, true, "not in the bundle"},
	    {"numViews 0", Texels(1), 0, false, "numViews 0"},
	    {"numViews 5", Texels(1), 5, false, "numViews 5"},
	};
	for(const Neg& g : negs)
	{
		std::map<std::string, OverlayTexels> tex;
		if(!g.missing)
			tex[sha] = g.t;
		FileOverlayFrame f = FrameOf(p, sha);
		f.numViews = g.numViews;
		std::string e;
		FileOverlayControl* ctl = (FileOverlayControl*)1;
		std::unique_ptr<OverlaySource> s = MakeFileOverlaySource(g_dev, {f}, tex, &ctl, &e);
		EXPECT(!s && !ctl, "%s: accepted", g.name);
		EXPECT(e.find(g.text) != std::string::npos, "%s: why '%s' lacks '%s'", g.name, e.c_str(), g.text);
	}
	std::string e;
	EXPECT(!MakeFileOverlaySource(nullptr, std::vector<FileOverlayFrame>(), std::map<std::string, OverlayTexels>(), nullptr, &e) && e == "no device", "no device: %s", e.c_str());
	// a TYPELESS texture of the family is taken (the live tap's texture may be typeless)
	std::map<std::string, OverlayTexels> tex;
	tex[sha] = Texels(1, 2048, 12, 27);
	EXPECT(MakeFileOverlaySource(g_dev, {FrameOf(p, sha)}, tex, nullptr, &e) != nullptr, "TYPELESS refused: %s", e.c_str());
	return true;
}

// The recording wrapper: every call passes through; each new pair is handed out at once and its texels two calls later,
// byte for byte; a pending readback is mapped at the wrapper's end.
struct OvlRec : OverlayRecordSink
{
	std::vector<OverlayFrame> pairs;
	std::vector<std::pair<OverlayFrame, OverlayTexels>> texels;
	std::vector<std::string> errors;
	int64_t* calls = nullptr;
	std::vector<int64_t> texelsAtCall;
	void onOverlayPair(const OverlayFrame& p) override { pairs.push_back(p); }
	void onOverlayTexels(const OverlayFrame& p, const OverlayTexels& t) override
	{
		texels.push_back({p, t});
		texelsAtCall.push_back(calls ? *calls : -1);
	}
	void onOverlayReadbackError(const OverlayFrame&, const std::string& w) override { errors.push_back(w); }
};

bool CaseOverlayRecording(std::string& why)
{
	const OverlayTexels t1 = Texels(11), t2 = Texels(12);
	FakeOverlay* fake = new FakeOverlay();
	ID3D11ShaderResourceView* v1 = TextureOf(t1);
	ID3D11ShaderResourceView* v2 = TextureOf(t2);
	EXPECT(v1 && v2, "textures");
	fake->pair.srv = v1;
	fake->pair.numViews = 1;
	ExeProj(fake->pair.overlayProj[0]);
	fake->pair.generation = 1;
	OvlRec rec;
	int64_t calls = 0;
	rec.calls = &calls;
	{
		std::unique_ptr<OverlaySource> s = MakeRecordingOverlaySource(std::unique_ptr<OverlaySource>(fake), rec);
		EXPECT(s, "no wrapper");
		OverlayFrame out;
		for(int i = 0; i < 4; ++i)
		{
			calls++;
			EXPECT(s->acquire(out) && out.srv == v1 && out.generation == 1, "pass-through %d", i);
			EXPECT(s->state() == OverlayTapReader::State::Active, "state");
		}
		EXPECT(rec.pairs.size() == 1 && rec.texels.size() == 1 && rec.texelsAtCall[0] == 3, "pairs %zu texels %zu (at call %lld)",
		       rec.pairs.size(), rec.texels.size(), rec.texels.empty() ? -1LL : (long long)rec.texelsAtCall[0]);
		EXPECT(rec.texels[0].second.bytes == t1.bytes && rec.texels[0].second.mips == 12 && rec.texels[0].second.width == 2048 &&
		           rec.texels[0].second.dxgiFormat == 28,
		       "texels of generation 1 differ");
		EXPECT(rec.texels[0].first.generation == 1 && rec.texels[0].first.srv == nullptr, "pair of the texels");
		// a new generation on a new texture, then two more in a row (the ring), then the wrapper ends with one pending
		fake->pair.srv = v2;
		fake->pair.generation = 2;
		calls++;
		s->acquire(out);
		fake->pair.generation = 3;
		calls++;
		s->acquire(out);
		fake->give = false;   // a call without a pair passes through as false
		calls++;
		EXPECT(!s->acquire(out) && s->state() == OverlayTapReader::State::Waiting, "a missing pair");
		fake->give = true;
		fake->pair.generation = 4;
		calls++;
		s->acquire(out);
		EXPECT(rec.pairs.size() == 4, "pairs %zu", rec.pairs.size());
		calls = -2;   // what is mapped at the end
	}
	EXPECT(rec.errors.empty(), "readback errors: %s", rec.errors.empty() ? "" : rec.errors[0].c_str());
	EXPECT(rec.texels.size() == 4, "texels %zu of 4 pairs", rec.texels.size());
	for(size_t i = 1; i < rec.texels.size(); ++i)
		EXPECT(rec.texels[i].first.generation == i + 1 && rec.texels[i].second.bytes == t2.bytes, "texels %zu (generation %u)", i,
		       rec.texels[i].first.generation);
	EXPECT(rec.texelsAtCall.back() == -2, "the last readback was not mapped at the end");
	SafeRelease(v1);
	SafeRelease(v2);
	return true;
}

// ==== the feed ============================================================================================================
struct FeedRec : FeedRecordSink
{
	std::vector<FeedCall> calls;
	uint64_t iter = 0;
	void onDrain(const SrvFeed::Batch& b) override
	{
		FeedCall c;
		c.type = 1;
		c.loopIter = iter;
		c.batch = b;
		calls.push_back(c);
	}
	void onPlots(uint32_t caller, const std::vector<SrvFeed::PlotRecord>& t, uint32_t gen, uint32_t w, uint32_t h) override
	{
		FeedCall c;
		c.type = 2;
		c.loopIter = iter;
		c.caller = caller;
		c.gen = gen;
		c.width = w;
		c.height = h;
		c.table = t;
		calls.push_back(c);
	}
};

bool SameBatch(const SrvFeed::Batch& a, const SrvFeed::Batch& b)
{
	return a.snapshotGen == b.snapshotGen && a.width == b.width && a.height == b.height && a.dropped == b.dropped &&
	       a.changedPlots == b.changedPlots && a.events.size() == b.events.size() &&
	       (a.events.empty() || !memcmp(a.events.data(), b.events.data(), a.events.size() * sizeof(SrvFeed::Event)));
}

SrvFeed::PlotRecord Rec(int8_t terrain, int16_t improvement, uint8_t known)
{
	SrvFeed::PlotRecord r = {};
	r.terrain = terrain;
	r.plotType = 2;
	r.feature = -1;
	r.route = -1;
	r.resource = -1;
	r.improvement = improvement;
	r.owner = -1;
	r.improvementState = -1;
	r.routeState = -1;
	r.known = known;
	return r;
}

// The bake-relevant replay (contract 7): a scene with the residency (stand-in) records its feed calls through the
// recording wrapper; a second scene fed by MakeFileFeed from those calls gives the same filter decisions and counters.
bool CaseFeedReplayResidency(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	FeedRec rec;
	std::unique_ptr<FeedSource> recording = MakeRecordingFeed(LiveFeed(), rec);
	// the table before the field: 4 x 4 plots, getters and layout known
	SrvFeed::BeginSnapshot(4, 4);
	for(uint32_t i = 0; i < 16; ++i)
		SrvFeed::SetPlot(i, Rec(1, -1, 3));
	SrvFeed::EndSnapshot();
	SceneRig a, b;
	a.m.camera(0.0f, 10, 256);
	b.m.camera(0.0f, 10, 256);
	SceneOptions oa = BaseOptions();
	oa.tiles = SceneOptions::kTilesBake;
	oa.bake.pipeline = false;
	oa.bake.feed = recording.get();
	std::string e;
	EXPECT(a.start(oa, false, e), "%s", e.c_str());
	VPShellCamera cam;
	SnapCamera(a.m.hdr, cam);
	const std::vector<uint8_t> c5 = FieldBytes();
	rec.iter = 1;
	EXPECT(a.scene.loadField(c5.data(), c5.size(), 1, &cam).ok(), "field A");
	// three loop iterations of the host's order: drain, then the scene's changed plots
	auto loop = [&](Scene& s, FeedSource& f, uint64_t it) {
		rec.iter = it;
		SrvFeed::Batch bt;
		if(f.drain(bt) && !bt.changedPlots.empty())
			s.plotsChanged(bt.changedPlots.data(), bt.changedPlots.size());
	};
	loop(a.scene, *recording, 2);                 // the snapshot's own plots (filtered: the field's state)
	SrvFeed::SetPlot(5, Rec(1, 7, 3));            // an improvement: re-baked
	SrvFeed::SetPlot(6, Rec(1, -1, 3));           // no change of a bake field: filtered
	loop(a.scene, *recording, 3);
	SrvFeed::SetPlot(9, Rec(2, -1, 3));           // a terrain change: re-baked
	loop(a.scene, *recording, 4);
	loop(a.scene, *recording, 5);                 // nothing changed: no drain record
	ResidencySource* ra = static_cast<ResidencySource*>(a.scene.source());
	EXPECT(ra && ra->plotsPassed == 2, "live: passed %lld filtered %lld changed %lld", ra ? (long long)ra->plotsPassed : -1LL,
	       ra ? (long long)ra->plotsFiltered : -1LL, ra ? (long long)ra->plotsChanged : -1LL);
	size_t drains = 0, plots = 0;
	for(const FeedCall& c : rec.calls)
		(c.type == 1 ? drains : plots)++;
	EXPECT(drains == 3 && plots == 3, "recorded %zu drains and %zu tables (want 3 and 3)", drains, plots);
	EXPECT(rec.calls[0].type == 2 && rec.calls[0].caller == kPlotsBaseline && rec.calls[0].table.size() == 16, "first record: the baseline");
	// the replay
	FileFeedControl* ctl = nullptr;
	std::unique_ptr<FeedSource> file = MakeFileFeed(rec.calls, &ctl, &e);
	EXPECT(file && ctl && ctl->remaining() == 6, "file feed refused: %s", e.c_str());
	SceneOptions ob = oa;
	ob.bake.feed = file.get();
	EXPECT(b.start(ob, false, e), "%s", e.c_str());
	EXPECT(b.scene.loadField(c5.data(), c5.size(), 1, &cam).ok(), "field B");
	for(uint64_t it = 2; it <= 5; ++it)
	{
		ctl->setLoopIter(it);
		SrvFeed::Batch bt;
		if(file->drain(bt) && !bt.changedPlots.empty())
			b.scene.plotsChanged(bt.changedPlots.data(), bt.changedPlots.size());
	}
	ResidencySource* rb = static_cast<ResidencySource*>(b.scene.source());
	EXPECT(rb && !ctl->failed() && ctl->remaining() == 0, "replay: failed %d (%s), %zu left", (int)ctl->failed(), ctl->why().c_str(),
	       ctl->remaining());
	EXPECT(rb->plotsChanged == ra->plotsChanged && rb->plotsPassed == ra->plotsPassed && rb->plotsFiltered == ra->plotsFiltered,
	       "counters live %lld/%lld/%lld replay %lld/%lld/%lld", (long long)ra->plotsChanged, (long long)ra->plotsPassed,
	       (long long)ra->plotsFiltered, (long long)rb->plotsChanged, (long long)rb->plotsPassed, (long long)rb->plotsFiltered);
	EXPECT(rb->stats()->raises == ra->stats()->raises && rb->stats()->plotsChanged == ra->stats()->plotsChanged, "residency raises %lld / %lld",
	       (long long)rb->stats()->raises, (long long)ra->stats()->raises);
	a.scene.releaseDevice();
	b.scene.releaseDevice();
	SrvFeed::SetEnabled(false);
	return true;
}

// The file feed yields the recorded batches and tables in order; drain() waits for the loop iteration it was set to.
bool CaseFeedFileOrder(std::string& why)
{
	std::vector<FeedCall> c(5);
	c[0].type = 2, c[0].loopIter = 1, c[0].caller = kPlotsBaseline, c[0].gen = 1, c[0].width = 2, c[0].height = 1;
	c[0].table = {Rec(1, -1, 3), Rec(2, -1, 3)};
	c[1].type = 1, c[1].loopIter = 2, c[1].batch.snapshotGen = 1, c[1].batch.changedPlots = {0, 1};
	c[1].batch.events.resize(2);
	c[1].batch.events[0].seq = 1, c[1].batch.events[0].kind = SrvFeed::kSnapshot;
	c[1].batch.events[1].seq = 2, c[1].batch.events[1].kind = SrvFeed::kUiMirror, c[1].batch.events[1].a[2] = 77;
	c[2].type = 2, c[2].loopIter = 2, c[2].caller = kPlotsChanged, c[2].gen = 1, c[2].width = 2, c[2].height = 1;
	c[2].table = {Rec(1, 7, 3), Rec(2, -1, 3)};
	c[3].type = 1, c[3].loopIter = 4, c[3].batch.dropped = 9;
	c[3].batch.events.resize(1);
	c[3].batch.events[0].seq = 3, c[3].batch.events[0].kind = SrvFeed::kOverflow, c[3].batch.events[0].a[0] = 9;
	c[4].type = 1, c[4].loopIter = 4, c[4].batch.changedPlots = {1};
	FileFeedControl* ctl = nullptr;
	std::string e;
	std::unique_ptr<FeedSource> f = MakeFileFeed(c, &ctl, &e);
	EXPECT(f && ctl, "refused: %s", e.c_str());
	std::vector<SrvFeed::PlotRecord> t;
	uint32_t gen = 9, w = 9, h = 9;
	SrvFeed::Batch b;
	ctl->setLoopIter(1);
	EXPECT(!f->drain(b) && ctl->remaining() == 5, "a drain before the baseline");
	f->copyPlots(t, gen, w, h, kPlotsBaseline);
	EXPECT(gen == 1 && w == 2 && h == 1 && t.size() == 2 && !memcmp(t.data(), c[0].table.data(), 40), "baseline table");
	EXPECT(!f->drain(b), "drain of iteration 2 yielded at iteration 1");
	ctl->setLoopIter(2);
	EXPECT(f->drain(b) && SameBatch(b, c[1].batch), "drain 1");
	f->copyPlots(t, gen, w, h, kPlotsChanged);
	EXPECT(t.size() == 2 && t[0].improvement == 7, "changed table");
	EXPECT(!f->drain(b) && b.events.empty() && b.changedPlots.empty(), "no drain left at iteration 2 (batch cleared)");
	ctl->setLoopIter(4);
	EXPECT(f->drain(b) && SameBatch(b, c[3].batch) && f->drain(b) && SameBatch(b, c[4].batch), "drains of iteration 4");
	EXPECT(!f->drain(b) && ctl->remaining() == 0 && !ctl->failed(), "the end");
	return true;
}

// A call that does not match the next record fails the feed (out of order); every later call yields nothing.
bool CaseFeedFileOutOfOrder(std::string& why)
{
	std::vector<FeedCall> c(3);
	c[0].type = 2, c[0].caller = kPlotsBaseline, c[0].gen = 1, c[0].width = 1, c[0].height = 1, c[0].table = {Rec(1, -1, 3)};
	c[1].type = 1, c[1].loopIter = 1, c[1].batch.changedPlots = {0};
	c[2].type = 2, c[2].loopIter = 1, c[2].caller = kPlotsChanged, c[2].gen = 1, c[2].width = 1, c[2].height = 1, c[2].table = {Rec(1, 7, 3)};
	std::string e;
	{
		FileFeedControl* ctl = nullptr;
		std::unique_ptr<FeedSource> f = MakeFileFeed(c, &ctl, &e);
		std::vector<SrvFeed::PlotRecord> t;
		uint32_t gen = 0, w = 0, h = 0;
		f->copyPlots(t, gen, w, h, kPlotsChanged);   // the next record is the baseline (caller 1)
		EXPECT(ctl->failed() && t.empty() && gen == 0 && ctl->why().find("out of order") != std::string::npos &&
		           ctl->why().find("of caller 1") != std::string::npos,
		       "wrong caller: failed %d why '%s'", (int)ctl->failed(), ctl->why().c_str());
		SrvFeed::Batch b;
		EXPECT(!f->drain(b) && ctl->remaining() == 3, "a failed feed still yields");
	}
	{
		FileFeedControl* ctl = nullptr;
		std::unique_ptr<FeedSource> f = MakeFileFeed(c, &ctl, &e);
		std::vector<SrvFeed::PlotRecord> t;
		uint32_t gen = 0, w = 0, h = 0;
		f->copyPlots(t, gen, w, h, kPlotsBaseline);
		f->copyPlots(t, gen, w, h, kPlotsChanged);   // the next record is a DRAIN
		EXPECT(ctl->failed() && ctl->why().find("is DRAIN") != std::string::npos, "copyPlots before the drain: '%s'", ctl->why().c_str());
	}
	{
		FileFeedControl* ctl = nullptr;
		std::unique_ptr<FeedSource> f = MakeFileFeed(c, &ctl, &e);
		std::vector<SrvFeed::PlotRecord> t;
		uint32_t gen = 0, w = 0, h = 0;
		SrvFeed::Batch b;
		f->copyPlots(t, gen, w, h, kPlotsBaseline);
		f->drain(b);
		f->copyPlots(t, gen, w, h, kPlotsChanged);
		f->copyPlots(t, gen, w, h, kPlotsChanged);   // past the end
		EXPECT(ctl->failed() && ctl->why().find("after the last") != std::string::npos, "past the end: '%s'", ctl->why().c_str());
	}
	EXPECT(CountLog("LOUD: file feed: out of order") == 3, "LOUD lines %d", CountLog("LOUD: file feed: out of order"));
	return true;
}

bool CaseFeedFileRefused(std::string& why)
{
	FeedCall p;
	p.type = 2, p.caller = kPlotsBaseline, p.width = 1, p.height = 1, p.table = {Rec(1, -1, 3)};
	FeedCall d;
	d.type = 1;
	struct Neg
	{
		const char* name;
		std::vector<FeedCall> c;
		const char* text;
	};
	std::vector<Neg> negs;
	negs.push_back({"no PLOTS", {d, d}, "no PLOTS record"});
	negs.push_back({"empty", {}, "no PLOTS record"});
	{
		FeedCall a = p, b = d;
		a.loopIter = 5;
		b.loopIter = 4;
		negs.push_back({"loop back", {a, b}, "out of order"});
	}
	{
		FeedCall u = d;
		u.type = 3;   // issue 4457: records 3-6 are folded into their DRAIN by ParseFeed, never calls of their own
		negs.push_back({"unknown type", {p, u}, "unknown type 3"});
	}
	{
		FeedCall q = p;
		q.caller = 7;
		negs.push_back({"caller", {q}, "unknown caller 7"});
	}
	{
		FeedCall q = p;
		q.width = 2;
		negs.push_back({"table size", {q}, "1 records, 2x1"});
	}
	for(const Neg& g : negs)
	{
		std::string e;
		FileFeedControl* ctl = (FileFeedControl*)1;
		EXPECT(!MakeFileFeed(g.c, &ctl, &e) && !ctl, "%s: accepted", g.name);
		EXPECT(e.find(g.text) != std::string::npos, "%s: why '%s' lacks '%s'", g.name, e.c_str(), g.text);
	}
	return true;
}

// The recording wrapper passes the live calls through unchanged and hands the recorder exactly what they returned.
bool CaseFeedRecordingPassThrough(std::string& why)
{
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch junk;
	SrvFeed::Drain(junk);
	FeedRec rec;
	std::unique_ptr<FeedSource> f = MakeRecordingFeed(LiveFeed(), rec);
	SrvFeed::BeginSnapshot(2, 2);
	for(uint32_t i = 0; i < 4; ++i)
		SrvFeed::SetPlot(i, Rec(1, -1, 3));
	SrvFeed::EndSnapshot();
	SrvFeed::Event ev = {};
	ev.kind = SrvFeed::kUiMirror;
	ev.plot = -1;
	ev.a[0] = 5;
	SrvFeed::Push(ev);
	SrvFeed::Batch b;
	EXPECT(f->drain(b) && b.width == 2 && b.events.size() >= 2, "drain: %zu events", b.events.size());
	EXPECT(rec.calls.size() == 1 && SameBatch(rec.calls[0].batch, b), "the recorder's batch differs");
	EXPECT(!f->drain(b) && rec.calls.size() == 1, "an empty drain was recorded");
	std::vector<SrvFeed::PlotRecord> t;
	uint32_t gen = 0, w = 0, h = 0;
	f->copyPlots(t, gen, w, h, kPlotsChanged);
	std::vector<SrvFeed::PlotRecord> t2;
	uint32_t gen2 = 0, w2 = 0, h2 = 0;
	SrvFeed::CopyPlots(t2, gen2, w2, h2);
	EXPECT(t.size() == 4 && t2.size() == 4 && gen == gen2 && w == w2 && h == h2 && !memcmp(t.data(), t2.data(), 80),
	       "table differs from CopyPlots");
	EXPECT(rec.calls.size() == 2 && rec.calls[1].caller == kPlotsChanged && rec.calls[1].gen == gen && rec.calls[1].width == 2 &&
	           !memcmp(rec.calls[1].table.data(), t.data(), 80),
	       "the recorder's table differs");
	SrvFeed::SetEnabled(false);
	return true;
}

// ==== the settle call =====================================================================================================
struct SettleRig : SceneRig
{
	bool begin(double rate, std::string& why, double memoryLimitMb = 0, int failX = -1, bool field = true)
	{
		m.camera(0.0f, 10, 256);
		SceneOptions o = BaseOptions();
		o.tiles = SceneOptions::kTilesBake;
		o.bake.pipeline = false;
		o.bake.res.rate = rate;
		o.bake.res.burst = 1;
		o.bake.res.memoryLimitMb = memoryLimitMb;
		o.bake.faults.failX = failX;
		o.bake.faults.failY = failX >= 0 ? 1 : -1;
		if(!start(o, false, why))
			return false;
		if(!field)
			return true;
		VPShellCamera cam;
		SnapCamera(m.hdr, cam);
		const std::vector<uint8_t> c5 = FieldBytes();
		const c5r::Status st = scene.loadField(c5.data(), c5.size(), 1, &cam);
		if(!st.ok())
			why = "loadField: " + st.str();
		return st.ok();
	}
	ResidencySource* res() { return static_cast<ResidencySource*>(scene.source()); }
};

bool CaseSettleUntilNoBakePending(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SettleRig r;
	std::string e;
	EXPECT(r.begin(60.0, e), "%s", e.c_str());
	const c5r::bake::ResidencyStats* s0 = r.res()->stats();
	EXPECT(s0 && s0->land == 16 && s0->pending > 0 && !s0->aheadDone, "before: land %u pending %u", s0 ? s0->land : 0, s0 ? s0->pending : 0);
	SettleResult out;
	const c5r::Status st = r.scene.settle(g_ctx, r.frameOf(10), kW, kH, 20000.0, &out);
	EXPECT(st.ok() && out.settled, "settle: %s; %s", st.str().c_str(), out.what.c_str());
	EXPECT(out.pending == 0 && out.aheadDone && !out.stopped && out.visibleUnbaked == 0 && out.calls > 1,
	       "result: pending %u ahead %d stopped %d unbaked %u calls %d", out.pending, (int)out.aheadDone, (int)out.stopped, out.visibleUnbaked,
	       out.calls);
	// the residency's own counters, read now: nothing pending, every land tile baked; the next draw shows no gray tile
	const c5r::bake::ResidencyStats* s = r.res()->stats();
	EXPECT(s->pending == 0 && s->aheadDone && s->resident == 16, "after: pending %u resident %u ahead %d", s->pending, s->resident,
	       (int)s->aheadDone);
	const int64_t gray = s->grayTileFrames, stale = s->staleFrames, bakes = s->bakes;
	std::string e2;
	EXPECT(r.draw(r.frameOf(11), nullptr, e2), "%s", e2.c_str());
	s = r.res()->stats();
	EXPECT(s->grayTileFrames == gray && s->staleFrames == stale && s->bakes == bakes, "the draw after settle: gray %lld -> %lld, bakes %lld -> %lld",
	       (long long)gray, (long long)s->grayTileFrames, (long long)bakes, (long long)s->bakes);
	EXPECT(CountLog("residency: settle: settled after") == 1, "settle line");
	// at 60 bakes a second the 16 tiles take about a quarter second: the call waited for them
	EXPECT(out.ms > 100.0, "settled in %.1f ms: did it wait for the bakes?", out.ms);
	return true;
}

bool CaseSettleTimeout(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SettleRig r;
	std::string e;
	EXPECT(r.begin(2.0, e), "%s", e.c_str());   // 2 bakes a second: 16 land tiles take 8 s
	SettleResult out;
	const c5r::Status st = r.scene.settle(g_ctx, r.frameOf(10), kW, kH, 300.0, &out);
	EXPECT(st.ok() && !out.settled, "settle: %s settled %d", st.str().c_str(), (int)out.settled);
	EXPECT(out.ms >= 300.0 && out.ms < 3000.0 && out.pending > 0 && !out.aheadDone && !out.stopped, "result: ms %.1f pending %u ahead %d",
	       out.ms, out.pending, (int)out.aheadDone);
	EXPECT(out.what.find("NOT settled") != std::string::npos && out.what.find("tiles pending") != std::string::npos &&
	           out.what.find("not done") != std::string::npos,
	       "what: '%s'", out.what.c_str());
	EXPECT(r.res()->stats()->pending == out.pending, "pending %u / %u", r.res()->stats()->pending, out.pending);
	// a failed visible tile is never settled: it is not pending, but the view shows it without a bake
	SettleRig f;
	EXPECT(f.begin(1000.0, e, 0, 1), "%s", e.c_str());
	SettleResult o2;
	EXPECT(f.scene.settle(g_ctx, f.frameOf(10), kW, kH, 1500.0, &o2).ok() && !o2.settled && o2.visibleUnbaked > 0 && o2.pending == 0,
	       "failed tile: settled %d unbaked %u pending %u (%s)", (int)o2.settled, o2.visibleUnbaked, o2.pending, o2.what.c_str());
	return true;
}

bool CaseSettleStopped(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SettleRig r;
	std::string e;
	EXPECT(r.begin(1000.0, e, 0.001), "%s", e.c_str());   // every array allocation is out of memory: radius 5 .. 0, stopped
	SettleResult out;
	const c5r::Status st = r.scene.settle(g_ctx, r.frameOf(10), kW, kH, 20000.0, &out);
	EXPECT(st.ok() && !out.settled && out.stopped, "settle: %s settled %d stopped %d (%s)", st.str().c_str(), (int)out.settled,
	       (int)out.stopped, out.what.c_str());
	EXPECT(out.ms < 10000.0 && out.what.find("STOPPED") != std::string::npos, "a stopped residency waited %.0f ms: '%s'", out.ms,
	       out.what.c_str());
	return true;
}

bool CaseSettleNoSource(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	SceneRig r;
	r.m.camera(0.0f, 10, 256);
	std::string e;
	EXPECT(r.start(BaseOptions(), false, e), "%s", e.c_str());   // render_tiles=none
	SettleResult out;
	EXPECT(r.scene.settle(g_ctx, r.frameOf(10), kW, kH, 1000.0, &out).ok() && out.settled && out.calls == 0, "none: settled %d calls %d",
	       (int)out.settled, out.calls);
	// the residency without a field: an error, not a settle
	SettleRig n;
	EXPECT(n.begin(100.0, e, 0, -1, false), "%s", e.c_str());
	const c5r::Status st = n.scene.settle(g_ctx, n.frameOf(10), kW, kH, 1000.0, &out);
	EXPECT(st.code == c5r::Err::NotFound && !out.settled && out.what == "no field", "no field: %s (%s)", st.str().c_str(), out.what.c_str());
	EXPECT(n.scene.settle(g_ctx, n.frameOf(10), kW, kH, 1000.0, nullptr).code == c5r::Err::BadLayout, "a null result");
	SettleResult o3;
	SettleRig m;
	EXPECT(m.begin(100.0, e), "%s", e.c_str());
	EXPECT(m.scene.settle(g_ctx, m.frameOf(10), kW, kH, std::nan(""), &o3).code == c5r::Err::BadLayout, "maxMs NaN");
	return true;
}

// ==== the bundle forms: a bundle written with 3902's Writer and encoders, read back through the file sources ===========
std::wstring g_work;
namespace sb = srvrender::scenebundle;

bool WriteBundle(const std::wstring& dir, sb::Manifest& m, const OverlayTexels& t, const std::vector<uint8_t>& water,
                 std::string& sha, std::string& why)
{
	sb::Writer w;
	c5r::Status st = w.open(dir);
	EXPECT(st.ok(), "Writer::open: %s", st.str().c_str());
	const std::vector<uint8_t> c5 = FieldBytes();
	EXPECT(w.addMember("heightfield/load0001.c5hf", sb::MemberKind::Heightfield, c5.data(), c5.size()).ok(), "heightfield");
	VPShellWaterTap b = {};
	b.state = VPWTAP_READY;
	b.loadSeq = 1;
	b.copySeq = 1;
	b.exePid = 4321;
	b.recordCount = ((const VPShellWaterImage*)water.data())->recordCount;
	b.imageBytes = water.size();
	b.fnv1a64 = Fnv(water.data(), water.size());
	std::vector<uint8_t> wt;
	sb::EncodeWaterTap(b, 1, 1, 256, 256, water.data(), water.size(), &wt);
	EXPECT(w.addMember("water/load0001.wtap", sb::MemberKind::WaterTap, wt.data(), wt.size()).ok(), "water tap");
	// the feed: a baseline table, a drain, a changed table
	EXPECT(w.appendStart("feed.bin", sb::MemberKind::Feed).ok(), "feed head");
	sb::FeedEncoder enc;
	std::vector<uint8_t> rec;
	std::vector<SrvFeed::PlotRecord> table(16, Rec(1, -1, 3));
	enc.addPlots(1, kPlotsBaseline, 1, 4, 4, table, &rec);
	EXPECT(w.append("feed.bin", rec.data(), rec.size()).ok(), "feed 1");
	SrvFeed::Batch bt;
	bt.snapshotGen = 1;
	bt.width = bt.height = 4;
	bt.changedPlots = {5};
	enc.addDrain(2, 10, bt, &rec);
	EXPECT(w.append("feed.bin", rec.data(), rec.size()).ok(), "feed 2");
	table[5] = Rec(1, 7, 3);
	enc.addPlots(2, kPlotsChanged, 1, 4, 4, table, &rec);
	EXPECT(w.append("feed.bin", rec.data(), rec.size()).ok(), "feed 3");
	// the overlay texture and two frames (the first without, the second with the pair)
	std::vector<uint8_t> ovl;
	EXPECT(sb::EncodeOverlay(t.bytes.data(), t.bytes.size(), &ovl).ok(), "EncodeOverlay");
	sha = sb::Sha256Hex(ovl.data(), ovl.size());
	EXPECT(sha.size() == 64, "SHA-256");
	EXPECT(w.addMember("overlay/" + sha.substr(0, 16) + ".ovl", sb::MemberKind::OverlayTexture, ovl.data(), ovl.size()).ok(), "overlay");
	EXPECT(w.appendStart("frames.bin", sb::MemberKind::Frames).ok(), "frames head");
	for(int k = 0; k < 2; ++k)
	{
		sb::FrameRecord r;
		memset(&r, 0, sizeof(r));
		r.frameId = 10 + k;
		r.loopIter = 3 + k;
		r.nowMs = 1000.0 + k;
		r.width = kW;
		r.height = kH;
		if(k == 1)
		{
			r.overlayBound = 1;
			r.overlayGeneration = 41;
			r.overlayPresentCount = 99;
			r.overlayNumViews = 1;
			ExeProj(r.overlayProj[0]);
			for(int i = 0; i < 32; ++i)
				r.overlaySha256[i] = (uint8_t)strtoul(sha.substr(2 * i, 2).c_str(), nullptr, 16);
		}
		EXPECT(w.append("frames.bin", &r, sizeof(r)).ok(), "frame %d", k);
	}
	sb::WaterClockFile wc;
	wc.clockStartMs = 1000;
	wc.waveLastMs = 1016;
	wc.lastNowMs = 1016;
	wc.waves.resize(4);
	for(int i = 0; i < 4; ++i)
		wc.waves[i].age = 0.5f * i;
	std::vector<uint8_t> wcb;
	sb::EncodeWaterClock(wc, &wcb);
	EXPECT(w.addMember("water_clock.bin", sb::MemberKind::WaterClock, wcb.data(), wcb.size()).ok(), "water clock");
	st = w.finish(m);
	EXPECT(st.ok() && m.members.size() == 6, "finish: %s (%zu members)", st.str().c_str(), m.members.size());
	return true;
}

bool CaseBundleForms(std::string& why)
{
	if(!NeedInputs(why, false))
		return false;
	const std::wstring dir = g_work + L"\\bundle_forms";
	const OverlayTexels t = Texels(5);
	const std::vector<uint8_t> water = Image(1, 4321, 2, 4);
	sb::Manifest m;
	std::string sha, e;
	if(!WriteBundle(dir, m, t, water, sha, why))
		return false;
	// the feed
	FileFeedControl* fc = nullptr;
	std::unique_ptr<FeedSource> feed = MakeFileFeed(dir, m, &fc, &e);
	EXPECT(feed && fc && fc->remaining() == 3, "file feed: %s", e.c_str());
	std::vector<SrvFeed::PlotRecord> tb;
	uint32_t gen = 0, w = 0, h = 0;
	feed->copyPlots(tb, gen, w, h, kPlotsBaseline);
	SrvFeed::Batch b;
	EXPECT(tb.size() == 16 && gen == 1 && w == 4 && feed->drain(b) && b.changedPlots == std::vector<uint32_t>{5}, "feed records 1-2");
	feed->copyPlots(tb, gen, w, h, kPlotsChanged);
	EXPECT(!fc->failed() && tb.size() == 16 && tb[5].improvement == 7 && tb[4].improvement == -1, "feed record 3 (the diff applied)");
	// the scene: field, water image and clock, overlay frames from the bundle
	SceneRig r;
	r.m.camera(0.0f, 10, 256);
	SceneOptions o = BaseOptions();
	o.water = true;
	o.waterMode = WaterSceneOptions::kFull;
	o.install = g_game;
	EXPECT(r.start(o, false, e), "%s", e.c_str());
	VPShellCamera cam;
	SnapCamera(r.m.hdr, cam);
	EXPECT(LoadFieldMember(r.scene, dir, m, "heightfield/load0001.c5hf", 1, &cam, &e) && r.scene.haveField(), "field: %s", e.c_str());
	EXPECT(LoadWaterImageMember(r.scene.water(), dir, m, "water/load0001.wtap", &e), "water: %s", e.c_str());
	EXPECT(r.scene.water().reader()->records().size() > 10, "water records %zu", r.scene.water().reader()->records().size());
	EXPECT(LoadWaterClockMember(r.scene.water(), dir, m, &e), "water clock: %s", e.c_str());
	const WaterClockState cs = r.scene.water().clockState();
	EXPECT(cs.clockStartMs == 1000 && cs.waveLastMs == 1016 && cs.waves.size() == 4 && cs.waves[3].age == 1.5f, "clock state");
	FileOverlayControl* oc = nullptr;
	std::unique_ptr<OverlaySource> os = MakeFileOverlaySource(g_dev, dir, m, &oc, &e);
	EXPECT(os && oc && oc->frameCount() == 2, "overlay: %s", e.c_str());
	r.scene.setOverlaySource(std::move(os));
	oc->seekFrame(1);
	r.scene.poll();
	EXPECT(r.draw(r.frameOf(11), nullptr, e) && r.scene.lastOverlay().srv && r.scene.ovlBoundFrames == 1, "overlay frame 1 not bound: %s",
	       e.c_str());
	// the negatives: a changed member byte (SHA-256), a missing member, a texture the frame names but the bundle lacks
	{
		sb::Manifest bad = m;
		bad.members["feed.bin"].sha256[0] = bad.members["feed.bin"].sha256[0] == 'a' ? 'b' : 'a';
		EXPECT(!MakeFileFeed(dir, bad, nullptr, &e) && e.find("ShaMismatch") != std::string::npos && e.find("feed.bin") != std::string::npos,
		       "feed SHA: '%s'", e.c_str());
		bad = m;
		bad.members.erase("water_clock.bin");
		EXPECT(!LoadWaterClockMember(r.scene.water(), dir, bad, &e) && e.find("MissingMember") != std::string::npos, "clock missing: '%s'",
		       e.c_str());
		bad = m;
		bad.members.erase("overlay/" + sha.substr(0, 16) + ".ovl");
		EXPECT(!MakeFileOverlaySource(g_dev, dir, bad, nullptr, &e) && e.find("not in the bundle") != std::string::npos, "overlay missing: '%s'",
		       e.c_str());
		bad = m;
		bad.members["water/load0001.wtap"].bytes++;
		EXPECT(!LoadWaterImageMember(r.scene.water(), dir, bad, "water/load0001.wtap", &e) && e.find("water/load0001.wtap") != std::string::npos,
		       "water size: '%s'", e.c_str());
		EXPECT(!LoadFieldMember(r.scene, dir, m, "heightfield/load0001.c5hf", 2, &cam, &e) && e.find("stale") != std::string::npos,
		       "field stale: '%s'", e.c_str());
	}
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
};
const Case kCases[] = {
    {"water_load_equals_poll", CaseWaterLoadEqualsPoll},
    {"water_sink_null_is_today", CaseWaterSinkNullIsToday},
    {"water_load_refused", CaseWaterLoadRefused},
    {"water_scene_load_image", CaseWaterSceneLoadImage},
    {"clock_held_equals_freeze", CaseClockHeldEqualsFreeze},
    {"clock_state_replay", CaseClockStateReplay},
    {"clock_state_refused", CaseClockStateRefused},
    {"field_load_equals_tap", CaseFieldLoadEqualsTap},
    {"field_load_refused", CaseFieldLoadRefused},
    {"scene_clock_held_equals_freeze", CaseSceneClockHeldEqualsFreeze},
    {"scene_clock_state_replay", CaseSceneClockStateReplay},
    {"overlay_file_binds_same", CaseOverlayFileBindsSame},
    {"overlay_file_refused", CaseOverlayFileRefused},
    {"overlay_recording", CaseOverlayRecording},
    {"feed_replay_residency", CaseFeedReplayResidency},
    {"feed_file_order", CaseFeedFileOrder},
    {"feed_file_out_of_order", CaseFeedFileOutOfOrder},
    {"feed_file_refused", CaseFeedFileRefused},
    {"feed_recording_pass_through", CaseFeedRecordingPassThrough},
    {"settle_until_no_bake_pending", CaseSettleUntilNoBakePending},
    {"settle_timeout", CaseSettleTimeout},
    {"settle_stopped", CaseSettleStopped},
    {"settle_no_source", CaseSettleNoSource},
    {"bundle_forms", CaseBundleForms},
};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	if(const char* e = getenv("C5R_EXE"))
		g_exe.assign(e, e + strlen(e));
	const c5test::Install inst = c5test::GameDir();   // ticket 0589: never the Steam install by default
	g_game = inst.dir;
	printf("sources_test: installed game %S (from %s)\n", g_game.c_str(), inst.from.c_str());
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("sources_test: no WARP device\n");
		return 99;
	}
	// build_scenebundle_tests.py --run passes `--work DIR` (this program writes nothing there); every other argument is a
	// case name
	std::vector<const char*> names;
	for(int i = 1; i < argc; ++i)
	{
		if(!strcmp(argv[i], "--work") && i + 1 < argc)
		{
			++i;
			g_work.assign(argv[i], argv[i] + strlen(argv[i]));
			continue;
		}
		names.push_back(argv[i]);
	}
	if(g_work.empty())
	{
		// without --work: a folder of this run in %TEMP% (the bundle case writes only there; left behind)
		wchar_t tmp[MAX_PATH];
		GetTempPathW(MAX_PATH, tmp);
		g_work = std::wstring(tmp) + L"c5_sources_test_" + std::to_wstring(GetCurrentProcessId());
	}
	CreateDirectoryW(g_work.c_str(), nullptr);
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = names.empty();
		for(const char* n : names)
			if(!strcmp(n, c.name))
				want = true;
		if(!want)
			continue;
		g_log.clear();
		std::string why;
		const bool ok = c.fn(why);
		run++;
		fails += ok ? 0 : 1;
		printf("CASE %s %s%s%s\n", c.name, ok ? "PASS" : "FAIL", ok ? "" : " ", why.c_str());
		if(!ok)
			for(const auto& l : g_log)
				printf("    log: %s\n", l.c_str());
		fflush(stdout);
	}
	if(!run)
	{
		printf("sources_test: no case matches\n");
		return 2;
	}
	g_ctx->Release();
	g_dev->Release();
	// the folder's convention (build_scenebundle_tests.py): the last line, nothing skipped
	printf("RESULT: %d passed, %d failed, 0 skipped\n", run - fails, fails);
	return fails;
}
