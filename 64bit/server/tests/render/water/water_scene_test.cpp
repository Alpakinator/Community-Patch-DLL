// water_scene_test.cpp - stand-in tests of server.exe's water scene (server/src/render/srv_water_scene.cpp, issue 3607).
// Written 2026-11-14 for this repository (GPL-3.0-or-later).
//
// Each case builds its own main section (a fake camera block) and, where it needs one, a fake water tap image in a named
// section published through the VPShellWaterTap block exactly as shell_composite.dll does (shell_shared.h), then runs
// the scene's poll / onField / decide / draw with a fake water module that records what the scene hands it (and, in
// one case, the real c5r::water module on a WARP device). Output: one line per case, `CASE <name> PASS|FAIL <detail>`;
// exit code = the number of failed cases (0 = all passed), 2 on usage.
//
//   water_scene_test.exe [case ...]      (no argument: every case)
#include "srv_water_scene.h"
#include "c5r/gamefs/gamefiles.h"
#include "../test_install.h"   // ticket 0589: the installed game (never the Steam install by default) and its build
#include <d3d11.h>
#include <cmath>
#include <stdio.h>
#include <string>
#include <vector>

using namespace srvrender;
namespace cw = c5r::water;

namespace {

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

// ---- the fake module ---------------------------------------------------------------------------------------------------
struct Fake : WaterModule
{
	bool failInit = false;
	bool waveRenderer = true, failWaveDraw = false;   // issue 3609
	int inits = 0, fields = 0, draws = 0, textures = 0, waveSets = 0, waveDraws = 0;
	std::vector<cw::WaveChunk> waveChunks;
	std::vector<cw::WaveRecord> lastWaves;
	float lastPlaneZ = 0;
	std::string order;   // "o" per module draw, "w" per wave draw
	std::vector<std::pair<cw::System, cw::Mesh>> meshes;
	cw::FrameInputs lastIn;
	cw::Options lastOpt;
	ID3D11ShaderResourceView* reflection = nullptr;
	// issue 0198: what shareFiles handed over, and what was pending when init ran
	int shares = 0;
	const c5r::gamefs::GameFiles* pending = nullptr;
	const c5r::gamefs::GameFiles* atInit = nullptr;
	void shareFiles(const c5r::gamefs::GameFiles* f) override
	{
		shares++;
		pending = f;
	}
	c5r::Status init(ID3D11Device*, const std::wstring&, const std::wstring&) override
	{
		atInit = pending;
		pending = nullptr;
		inits++;
		return failInit ? c5r::Status::fail(c5r::Err::NotFound, "fake: no module") : c5r::Status::Ok();
	}
	c5r::Status setHeightField(const c5r::heightfield::HeightGrid&) override
	{
		fields++;
		return c5r::Status::Ok();
	}
	// ticket 4909: the region update; regionMode 0 = the interface's default (Unsupported), 1 = Ok, 2 = BadLayout
	int regionMode = 0, regions = 0;
	size_t regionRects = 0;
	c5r::Status updateHeightRegion(const c5r::heightfield::HeightGrid& g, const std::vector<c5r::terrain::SampleRect>& rects,
	                               const std::vector<c5r::bake::HeightPage>* built, c5r::terrain::RegionUpdateStats* st) override
	{
		regions++;
		regionRects = rects.size();
		if(regionMode == 0)
			return WaterModule::updateHeightRegion(g, rects, built, st);
		return regionMode == 1 ? c5r::Status::Ok() : c5r::Status::fail(c5r::Err::BadLayout, "fake: refused");
	}
	c5r::Status setMesh(cw::System s, const cw::Mesh& m) override
	{
		meshes.push_back({s, m});
		return c5r::Status::Ok();
	}
	c5r::Status setTexture(cw::Slot s, ID3D11ShaderResourceView* srv) override
	{
		textures++;
		if(s == cw::Slot::Reflection)
			reflection = srv;
		return c5r::Status::Ok();
	}
	c5r::Status draw(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, const cw::FrameInputs& in,
	                 const cw::Options& o, cw::FrameStats*) override
	{
		draws++;
		order += "o";
		lastIn = in;
		lastOpt = o;
		return c5r::Status::Ok();
	}
	void release() override {}
	bool wavesAvailable() const override { return waveRenderer; }
	std::string wavesWhy() const override { return "fake: no wave renderer"; }
	c5r::Status setWaveChunks(const std::vector<cw::WaveChunk>& c) override
	{
		waveSets++;
		waveChunks = c;
		return c5r::Status::Ok();
	}
	c5r::Status drawWaves(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, const cw::WaveObject&, float planeZ,
	                      const std::vector<cw::WaveRecord>& w, cw::WaveStats* st) override
	{
		if(failWaveDraw)
			return c5r::Status::fail(c5r::Err::Device, "fake: wave draw failed");
		waveDraws++;
		order += "w";
		lastPlaneZ = planeZ;
		lastWaves = w;
		if(st)
			st->chunksDrawn = (uint32_t)waveChunks.size();
		return c5r::Status::Ok();
	}
	const cw::Mesh* mesh(cw::System s) const
	{
		for(auto it = meshes.rbegin(); it != meshes.rend(); ++it)
			if(it->first == s)
				return &it->second;
		return nullptr;
	}
};

// ---- sections ------------------------------------------------------------------------------------------------------------
struct Main
{
	HANDLE h = nullptr;
	VPShellHeader* hdr = nullptr;
	explicit Main(const std::wstring& name)
	{
		h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, name.c_str());
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
	}
	void camera(uint32_t gridSeq, uint32_t samplesX = 128, uint32_t samplesY = 128)
	{
		VPShellCamera* c = VPSHELL_CAMERA(hdr);
		c->gridSeq = gridSeq;
		c->gridValid = 1;
		c->samplesX = samplesX;
		c->samplesY = samplesY;
		c->seq += 2;
		if(!c->seq)
			c->seq = 2;
	}
};

VPShellCamera Cam(uint32_t gridSeq)
{
	VPShellCamera c = {};
	c.gridSeq = gridSeq;
	c.gridValid = 1;
	c.samplesX = c.samplesY = 128;
	return c;
}

void PutF(std::vector<uint8_t>& b, size_t off, float v) { memcpy(b.data() + off, &v, 4); }

struct Block
{
	std::vector<uint8_t> data;
	VPShellWaterRecord r = {};
};

// The wave ages of the images: two waves per chunk (issue 3609).
const float kAges[4] = {1.0f, 8.95f, -1.0f, 3.0f};
enum WaveFault { WF_NONE = 0, WF_NO_CHUNK_ARRAY, WF_WAVE_ARRAY_SIZE, WF_R32_INDICES };

// A Wave_System object as the tap copies it (0xB0 bytes), WaveSettings.xml's values (water-system.md 12.1), seed 0.
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

// An image like the producer's (shell/composite/water_tap.cpp Gather): river (object, record, VB 4 x 32 B, IB 6 x R32,
// draw pass 4), the Water object with the live D33 clock fields, the Wave_System object, and with `waveChunks` chunks
// the chunk array (Wave+0x54), the wave array (Wave+0x5C, 2 waves per chunk, ages kAges) and per chunk its draw record
// (0x80 bytes, Wave+0x54), VB 8 x 12 B, IB 12 x R16 and the draw. `fault` breaks one part (the negative cases).
std::vector<uint8_t> Image(uint32_t load, uint32_t pid, int waveChunks, bool river = true, int fault = WF_NONE)
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
	if(river)
	{
		add(VPWSYS_RIVER, VPWROLE_OBJECT, std::vector<uint8_t>(0x80, 2));
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
		if(fault != WF_NO_CHUNK_ARRAY)
		{
			std::vector<uint8_t> arr(36u * waveChunks, 0);
			for(int k = 0; k < waveChunks; ++k)
			{
				const uint32_t first = 2u * k, n = 2;
				const float bnd[4] = {10.0f * k, 0.0f, 10.0f * k + 9.0f, 9.0f};
				memcpy(arr.data() + 36 * k + 0x0C, &first, 4);
				memcpy(arr.data() + 36 * k + 0x10, &n, 4);
				memcpy(arr.data() + 36 * k + 0x14, bnd, 16);
			}
			bl[add(VPWSYS_WAVE, VPWROLE_OBJECT, arr)].r.exeOffset = 0x54;
		}
		std::vector<uint8_t> wv(16u * nw + (fault == WF_WAVE_ARRAY_SIZE ? 16u : 0u), 0);
		for(uint32_t i = 0; i < nw; ++i)
		{
			const float rec[4] = {0.0f, 0.0f, kAges[i % 4], std::nanf("")};   // the fourth float as the EXE leaves it
			memcpy(wv.data() + 16 * i, rec, 16);
		}
		uint32_t wa = add(VPWSYS_WAVE, VPWROLE_OBJECT, wv);
		bl[wa].r.exeOffset = 0x5C;
		bl[wa].r.flags = 2;
	}
	for(int k = 0; k < waveChunks; ++k)
	{
		bl[add(VPWSYS_WAVE, VPWROLE_OBJECT, std::vector<uint8_t>(0x80, 6))].r.exeOffset = 0x54;   // the chunk's draw record
		uint32_t v = add(VPWSYS_WAVE, VPWROLE_VB, vb(8, 12, (uint8_t)(9 + k)));
		bl[v].r.stride = 12;
		bl[v].r.count = 8;
		const bool r32 = fault == WF_R32_INDICES;
		uint32_t i = add(VPWSYS_WAVE, VPWROLE_IB, ib(12, 8, r32 ? 4 : 2));
		bl[i].r.dxgiFormat = r32 ? 42 : 57;
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
	h.mapW = h.mapH = 2;
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

struct Published
{
	HANDLE sec = nullptr;
	~Published()
	{
		if(sec)
			CloseHandle(sec);
	}
};

// Publishes `img` as copy `copy` of `load` (READY) through the block. badHash: the block's image hash is wrong.
void Publish(Main& m, Published& p, const std::vector<uint8_t>& img, uint32_t load, uint32_t copy, bool badHash = false)
{
	wchar_t name[64];
	_snwprintf_s(name, _TRUNCATE, L"Local\\VPShellWaterSceneTest.%u.%u.%u", GetCurrentProcessId(), load, copy);
	p.sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)img.size(), name);
	void* v = MapViewOfFile(p.sec, FILE_MAP_WRITE, 0, 0, 0);
	memcpy(v, img.data(), img.size());
	UnmapViewOfFile(v);
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
	wcsncpy_s(t->sectionName, name, _TRUNCATE);
	MemoryBarrier();
	t->seq++;
	if(t->seq & 1)
		t->seq++;
}

c5r::heightfield::HeightGrid Grid()
{
	c5r::heightfield::HeightGrid g;
	g.mapW = g.mapH = 2;
	g.samplesX = g.samplesY = 128;
	g.waterZ = 10.117f;
	g.h.assign(128 * 128, 1000);
	return g;
}

c5r::terrain::Camera TCam(bool nan = false)
{
	c5r::terrain::Camera c;
	for(int i = 0; i < 4; ++i)
		c.view[5 * i] = c.proj[5 * i] = c.camWorld[5 * i] = 1.0f;
	if(nan)
		c.view[12] = std::nanf("");
	c.width = 64;
	c.height = 64;
	return c;
}

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;

struct Case
{
	const char* name;
	bool (*fn)(std::string& why);
};

#define EXPECT(cond, ...)                                                                                                      \
	do                                                                                                                         \
	{                                                                                                                          \
		if(!(cond))                                                                                                            \
		{                                                                                                                      \
			char b_[512];                                                                                                      \
			_snprintf_s(b_, _TRUNCATE, __VA_ARGS__);                                                                          \
			why = b_;                                                                                                          \
			return false;                                                                                                      \
		}                                                                                                                      \
	} while(0)

std::wstring SecName(const char* c)
{
	wchar_t b[96];
	_snwprintf_s(b, _TRUNCATE, L"Local\\VPShellWaterSceneMain_%u_%S", GetCurrentProcessId(), c);
	return b;
}

// A scene with a fake module, attached to m, initialised (full mode unless told), with the field of `fieldLoad`.
struct Rig
{
	Main m;
	Fake* fake;
	WaterScene s;
	c5r::heightfield::HeightGrid grid = Grid();
	Rig(const char* name, int mode = WaterSceneOptions::kFull, bool freeze = false, bool failInit = false, uint32_t fieldLoad = 1)
	    : m(SecName(name)), fake(new Fake()), s(std::unique_ptr<WaterModule>(fake))
	{
		fake->failInit = failInit;
		m.camera(fieldLoad);
		s.attach(m.hdr);
		WaterSceneOptions o;
		o.mode = mode;
		o.freeze = freeze;
		s.init(g_dev, o);
		if(fieldLoad)
			s.onField(grid, fieldLoad);
	}
};

bool CaseNoTap(std::string& why)
{
	Rig r("notap");
	r.s.poll();
	EXPECT(!r.s.decide(Cam(1), 0), "decided to draw without a tap");
	EXPECT(r.s.why() == WW_NO_TAP && r.s.placeholder(), "why %s, placeholder %d", WaterWhyName(r.s.why()), (int)r.s.placeholder());
	EXPECT(r.fake->draws == 0, "module drew");
	return true;
}

bool CaseModuleDraws(std::string& why)
{
	Rig r("draws");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.reader() && r.s.reader()->accepted == 1, "image not accepted (result %d: %s)", r.s.reader()->lastResult(),
	       r.s.reader()->lastWhy().c_str());
	const VPShellWaterTap* t = VPSHELL_WATERTAP(r.m.hdr);
	EXPECT(t->ackLoadSeq == 1 && t->ackCopySeq == 1 && t->ackResult == 0, "ack %u/%u result %d", t->ackLoadSeq, t->ackCopySeq, t->ackResult);
	EXPECT(r.s.decide(Cam(1), 1000.0), "not drawing: %s", WaterWhyName(r.s.why()));
	EXPECT(!r.s.placeholder(), "placeholder on while the module draws");
	const cw::Mesh* river = r.fake->mesh(cw::System::River);
	EXPECT(river && river->vb.size() == 128 && river->ib.size() == 24 && river->stride == 32 && river->indexFormat == 42,
	       "river mesh %zu / %zu bytes", river ? river->vb.size() : 0, river ? river->ib.size() : 0);
	EXPECT(river->draws.size() == 1 && river->draws[0].indexCount == 6 && river->draws[0].pass == 4 && river->draws[0].topology == 4,
	       "river draw");
	EXPECT(river->ownConstants.size() == 0x80, "river own buffer %zu", river->ownConstants.size());
	float m00 = 0, m33 = 0, m01 = 1;
	memcpy(&m00, river->ownConstants.data(), 4);
	memcpy(&m33, river->ownConstants.data() + 60, 4);
	memcpy(&m01, river->ownConstants.data() + 4, 4);
	EXPECT(m00 == 1.0f && m33 == 1.0f && m01 == 0.0f, "river World not identity");
	const cw::Mesh* wave = r.fake->mesh(cw::System::Wave);
	EXPECT(wave && wave->draws.empty() && wave->vb.empty(), "waves set although the image has none");
	EXPECT(r.fake->reflection != nullptr, "t4 not bound (reflection errors %lld)", (long long)r.s.reflectionErrors);
	c5r::water::FrameStats st;
	const c5r::Status ds = r.s.draw(g_ctx, nullptr, nullptr, TCam(), &st);
	EXPECT(ds.ok() && r.fake->draws == 1, "draw: %s", ds.str().c_str());
	EXPECT(r.fake->lastIn.fields.valid && r.fake->lastOpt.oceanPass == 3 && !r.fake->lastOpt.clearDepth, "inputs: valid %d pass %d",
	       (int)r.fake->lastIn.fields.valid, r.fake->lastOpt.oceanPass);
	EXPECT(r.fake->lastIn.waterZ < 0 && !r.fake->lastIn.clock.fromExe, "waterZ / clock source");
	EXPECT(r.fake->fields == 1, "setHeightField %d times", r.fake->fields);
	EXPECT(CountLog("the water module draws") == 1, "draw decision logged %d times", CountLog("the water module draws"));
	return true;
}

bool ClockCase(bool freeze, std::string& why)
{
	Rig r(freeze ? "freeze" : "clock", WaterSceneOptions::kFull, freeze);
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 5000.0), "not drawing");
	r.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr);
	const float a0 = r.fake->lastIn.fields.get(0xDC), b0 = r.fake->lastIn.fields.get(0xE4);
	EXPECT(r.s.decide(Cam(1), 7000.0), "not drawing (2)");
	r.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr);
	const float a1 = r.fake->lastIn.fields.get(0xDC), b1 = r.fake->lastIn.fields.get(0xE4);
	EXPECT(fabsf(a0 - 0.145891f) < 1e-6f && fabsf(b0 - 0.00607878f) < 1e-7f, "start values %g %g", a0, b0);
	if(freeze)
		EXPECT(a1 == a0 && b1 == b0, "frozen clock moved: %g -> %g, %g -> %g", a0, a1, b0, b1);
	else
		EXPECT(fabsf((a1 - a0) - 1.44f) < 1e-4f && fabsf((b1 - b0) - 0.06f) < 1e-5f, "2 s advanced +0xDC by %g (want 1.44) and +0xE4 by %g (want 0.06)",
		       a1 - a0, b1 - b0);
	return true;
}
bool CaseClock(std::string& why) { return ClockCase(false, why); }
bool CaseFreeze(std::string& why) { return ClockCase(true, why); }

bool CaseRefused(std::string& why)
{
	Rig r("refused");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1, true);
	r.s.poll();
	EXPECT(r.s.reader()->refused == 1 && r.s.reader()->lastResult() == WTACK_HASH, "refusal %d", r.s.reader()->lastResult());
	EXPECT(VPSHELL_WATERTAP(r.m.hdr)->ackResult == WTACK_HASH, "ack result %d", VPSHELL_WATERTAP(r.m.hdr)->ackResult);
	EXPECT(!r.s.decide(Cam(1), 0) && r.s.why() == WW_NO_TAP && r.s.placeholder(), "why %s", WaterWhyName(r.s.why()));
	EXPECT(r.fake->meshes.empty(), "meshes set from a refused image");
	EXPECT(CountLog("placeholder plane (NO_TAP") == 1 && CountLog("last result -1") == 1, "placeholder reason not logged with the refusal");
	return true;
}

bool CaseStale(std::string& why)
{
	Rig r("stale", WaterSceneOptions::kFull, false, false, 2);   // camera and field of load 2, the image of load 1
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.reader()->refused == 1 && r.s.reader()->lastResult() == WTACK_STALE, "result %d", r.s.reader()->lastResult());
	EXPECT(!r.s.decide(Cam(2), 0) && r.s.why() == WW_NO_TAP, "why %s", WaterWhyName(r.s.why()));
	return true;
}

bool CaseOtherLoad(std::string& why)
{
	Rig r("otherload");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "load 1 not drawn");
	r.m.camera(2);
	r.s.onField(r.grid, 2);   // the next load's field arrives before its tap
	EXPECT(!r.s.decide(Cam(2), 10) && r.s.why() == WW_OTHER_LOAD && r.s.placeholder(), "why %s", WaterWhyName(r.s.why()));
	EXPECT(!r.s.decide(Cam(3), 20) && r.s.why() == WW_NO_FIELD, "a camera ahead of the field: why %s", WaterWhyName(r.s.why()));
	return true;
}

bool CaseNanCamera(std::string& why)
{
	Rig r("nancam");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	const c5r::Status ds = r.s.draw(g_ctx, nullptr, nullptr, TCam(true), nullptr);
	EXPECT(!ds.ok() && ds.code == c5r::Err::BadLayout && r.s.badCamera == 1 && r.fake->draws == 0, "NaN camera: %s, bad %lld, draws %d",
	       ds.str().c_str(), (long long)r.s.badCamera, r.fake->draws);
	c5r::terrain::Camera z = TCam();
	z.width = 0;
	EXPECT(!r.s.draw(g_ctx, nullptr, nullptr, z, nullptr).ok() && r.fake->draws == 0, "camera of size 0 drawn");
	return true;
}

bool CaseModuleMissing(std::string& why)
{
	Rig r("nomodule", WaterSceneOptions::kFull, false, true);
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.reader()->accepted == 1, "the tap is still read and acked");
	EXPECT(!r.s.decide(Cam(1), 0) && r.s.why() == WW_NO_MODULE && r.s.placeholder(), "why %s", WaterWhyName(r.s.why()));
	EXPECT(CountLog("LOUD") == 0, "a LOUD line for an absent module");
	EXPECT(CountLog("fake: no module") >= 1, "the module's reason not logged");
	return true;
}

bool CaseModes(std::string& why)
{
	{
		Rig r("off", WaterSceneOptions::kOff);
		Published p;
		Publish(r.m, p, Image(1, 4321, 0), 1, 1);
		r.s.poll();
		EXPECT(!r.s.decide(Cam(1), 0) && r.s.why() == WW_OFF && !r.s.placeholder(), "off: why %s placeholder %d", WaterWhyName(r.s.why()),
		       (int)r.s.placeholder());
		EXPECT(r.fake->inits == 0 && r.s.reader()->accepted == 1, "off: module initialised or tap not acked");
	}
	{
		Rig r("placeholder", WaterSceneOptions::kPlaceholder);
		Published p;
		Publish(r.m, p, Image(1, 4321, 0), 1, 1);
		r.s.poll();
		EXPECT(!r.s.decide(Cam(1), 0) && r.s.why() == WW_PLACEHOLDER && r.s.placeholder(), "placeholder mode: why %s", WaterWhyName(r.s.why()));
		EXPECT(r.s.reader()->accepted == 1 && VPSHELL_WATERTAP(r.m.hdr)->ackLoadSeq == 1, "placeholder mode: tap not acked");
	}
	return true;
}

// Issue 3609: the tap's wave records go to the wave renderer (not a module mesh), drawn after the ocean with the water z
// + 1, the waves updated once per drawn frame by the server's clock (the first frame by 0, a gap by at most 0.25 s).
bool CaseWavesDrawn(std::string& why)
{
	Rig r("waves");
	Published p;
	Publish(r.m, p, Image(1, 4321, 2, false), 1, 1);
	r.s.poll();
	EXPECT(r.s.wavesReady() && r.fake->waveSets == 1 && r.fake->waveChunks.size() == 2, "waves ready %d, sets %d, chunks %zu",
	       (int)r.s.wavesReady(), r.fake->waveSets, r.fake->waveChunks.size());
	const cw::WaveChunk& c1 = r.fake->waveChunks[1];
	EXPECT(c1.vb.size() == 8 * 12 && c1.ib.size() == 12 * 2 && c1.firstWave == 2 && c1.waveCount == 2 && c1.bounds[0] == 10.0f &&
	           c1.bounds[2] == 19.0f,
	       "chunk 1: vb %zu ib %zu first %u count %u bounds %g..%g", c1.vb.size(), c1.ib.size(), c1.firstWave, c1.waveCount, c1.bounds[0],
	       c1.bounds[2]);
	EXPECT(c1.vb[0] == (uint8_t)10, "chunk 1 has another chunk's vertices (first byte %u)", c1.vb[0]);
	const cw::Mesh* wm = r.fake->mesh(cw::System::Wave);
	EXPECT(wm && wm->draws.empty() && wm->vb.empty(), "the module's own wave mesh was set");
	const cw::Mesh* river = r.fake->mesh(cw::System::River);
	EXPECT(river && river->draws.empty(), "rivers set although the image has none");
	EXPECT(r.s.decide(Cam(1), 1000.0), "an image without rivers still draws (the ocean comes from the field)");
	cw::FrameStats st;
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, TCam(), &st).ok(), "draw 1");
	EXPECT(r.fake->order == "ow", "draw order %s (want the ocean, then the waves)", r.fake->order.c_str());
	EXPECT(!r.fake->lastOpt.waves, "the module was asked for its own wave mesh");
	EXPECT(fabsf(r.fake->lastPlaneZ - 11.117f) < 1e-5f, "wave plane z %g (want the water z + 1 = 11.117)", r.fake->lastPlaneZ);
	EXPECT(r.fake->lastWaves.size() == 4 && r.fake->lastWaves[0].age == 1.0f && r.fake->lastWaves[1].age == 8.95f,
	       "first frame: %zu waves, ages %g %g (want the tapped 1, 8.95)", r.fake->lastWaves.size(),
	       r.fake->lastWaves.size() ? r.fake->lastWaves[0].age : -99.0f, r.fake->lastWaves.size() > 1 ? r.fake->lastWaves[1].age : -99.0f);
	EXPECT(r.fake->lastWaves[0].unused == 0.0f, "the unused fourth float was passed on (%g)", r.fake->lastWaves[0].unused);
	EXPECT(st.waveDraws == 2, "stats: wave chunks drawn %u", st.waveDraws);
	EXPECT(r.s.decide(Cam(1), 1100.0), "frame 2");
	r.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr);
	const std::vector<cw::WaveRecord>& w = r.fake->lastWaves;
	EXPECT(fabsf(w[0].age - 1.1f) < 1e-5f && fabsf(w[2].age - (-0.9f)) < 1e-5f, "0.1 s later: ages %g %g (want 1.1, -0.9)", w[0].age, w[2].age);
	// wave 1 passed the cycle time (8.95 + 0.1 > 9): the frame's first restart with seed 0 = page 10, cell (0.25, 0.5),
	// delay -(2.1 + r x 2.9) = -2.102 s (water-system.md 12.1)
	EXPECT(w[1].age < -2.09f && w[1].age > -2.11f && w[1].u == 0.25f && w[1].v == 0.5f, "restart: age %g cell (%g, %g)", w[1].age, w[1].u,
	       w[1].v);
	EXPECT(r.s.decide(Cam(1), 6100.0), "frame 3");
	r.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr);
	EXPECT(fabsf(r.fake->lastWaves[0].age - 1.35f) < 1e-5f, "a 5 s gap advanced wave 0 to %g (want 1.35: at most 0.25 s)",
	       r.fake->lastWaves[0].age);
	EXPECT(r.s.waves().size() == 4 && r.s.waveDrawsTotal == 3 && r.s.waveErrors == 0, "totals: %lld wave frames, %lld errors",
	       (long long)r.s.waveDrawsTotal, (long long)r.s.waveErrors);
	{   // freeze: the tapped ages stay
		Rig f("wavesfrozen", WaterSceneOptions::kFull, true);
		Published pf;
		Publish(f.m, pf, Image(1, 4321, 1, false), 1, 1);
		f.s.poll();
		for(int i = 0; i < 3; ++i)
		{
			f.s.decide(Cam(1), 1000.0 + 500.0 * i);
			f.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr);
		}
		EXPECT(f.fake->waveDraws == 3 && f.fake->lastWaves.size() == 2 && f.fake->lastWaves[0].age == 1.0f &&
		           f.fake->lastWaves[1].age == 8.95f,
		       "frozen: %d draws, ages %g %g", f.fake->waveDraws, f.fake->lastWaves.size() ? f.fake->lastWaves[0].age : -99.0f,
		       f.fake->lastWaves.size() > 1 ? f.fake->lastWaves[1].age : -99.0f);
	}
	return true;
}

// Issue 3609, the negative side: a broken wave part, or a module without a wave renderer, or a failing wave draw,
// leaves the ocean drawn; the reason is logged (once per image, or once per distinct draw error), never LOUD.
bool CaseWavesBad(std::string& why)
{
	struct F
	{
		int fault;
		const char* log;
	};
	const F faults[3] = {{WF_NO_CHUNK_ARRAY, "no chunk array"}, {WF_WAVE_ARRAY_SIZE, "the wave array is 80 bytes"}, {WF_R32_INDICES, "IB format 42"}};
	for(const F& f : faults)
	{
		g_log.clear();
		Rig r("wavesbad");
		Published p;
		Publish(r.m, p, Image(1, 4321, 2, true, f.fault), 1, 1);
		r.s.poll();
		EXPECT(!r.s.wavesReady() && r.s.waveErrors == 1 && r.fake->waveSets == 0, "fault %d: ready %d, errors %lld", f.fault,
		       (int)r.s.wavesReady(), (long long)r.s.waveErrors);
		EXPECT(CountLog(f.log) == 1 && CountLog("no coast waves") == 1, "fault %d: reason '%s' not logged once", f.fault, f.log);
		EXPECT(r.s.decide(Cam(1), 0) && r.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr).ok() && r.fake->draws == 1 && r.fake->waveDraws == 0,
		       "fault %d: the ocean not drawn alone (draws %d, wave draws %d)", f.fault, r.fake->draws, r.fake->waveDraws);
		const cw::Mesh* river = r.fake->mesh(cw::System::River);
		EXPECT(river && river->draws.size() == 1, "fault %d: the rivers were lost with the waves", f.fault);
	}
	{
		g_log.clear();
		Rig r("wavesnorenderer");
		r.fake->waveRenderer = false;
		Published p;
		Publish(r.m, p, Image(1, 4321, 2), 1, 1);
		r.s.poll();
		EXPECT(!r.s.wavesReady() && r.s.waveErrors == 0 && CountLog("fake: no wave renderer") == 1, "no renderer: ready %d errors %lld",
		       (int)r.s.wavesReady(), (long long)r.s.waveErrors);
	}
	{
		g_log.clear();
		Rig r("wavesdrawfails");
		r.fake->failWaveDraw = true;
		Published p;
		Publish(r.m, p, Image(1, 4321, 2), 1, 1);
		r.s.poll();
		for(int i = 0; i < 3; ++i)
		{
			r.s.decide(Cam(1), 100.0 * i);
			EXPECT(r.s.draw(g_ctx, nullptr, nullptr, TCam(), nullptr).ok(), "a failing wave draw failed the water draw");
		}
		EXPECT(r.s.waveErrors == 3 && r.s.drawsTotal == 3 && CountLog("waves not drawn: ") == 1 && CountLog("LOUD") == 0,
		       "draw failure: errors %lld draws %lld logged %d", (long long)r.s.waveErrors, (long long)r.s.drawsTotal, CountLog("waves not drawn: "));
	}
	return true;
}

bool CaseLogOnce(std::string& why)
{
	Rig r("logonce");
	for(int i = 0; i < 100; ++i)
		r.s.decide(Cam(1), i * 16.0);
	EXPECT(CountLog("load seq 1: placeholder plane (NO_TAP") == 1, "logged %d times", CountLog("load seq 1: placeholder plane (NO_TAP"));
	EXPECT(r.s.placeholderFrames == 100, "placeholder frames %lld", (long long)r.s.placeholderFrames);
	return true;
}

bool CaseRealModuleBadExe(std::string& why)
{
	// the real c5r::water module, an EXE path that does not exist: NO_MODULE, logged, never LOUD
	Main m(SecName("realmod"));
	m.camera(1);
	WaterScene s;   // MakeC5rWaterModule()
	s.attach(m.hdr);
	WaterSceneOptions o;
	o.exe = L"C:\\no\\such\\CivilizationV_DX11.exe";
	o.gameDir = L"C:\\no\\such";
	s.init(g_dev, o);
	EXPECT(!s.decide(Cam(1), 0) && s.why() == WW_NO_MODULE && s.placeholder(), "why %s", WaterWhyName(s.why()));
	EXPECT(CountLog("LOUD") == 0, "LOUD line");
	return true;
}

// Issue 0198: the caller's index reaches the module once, right before its init, and the scene keeps no pointer to it
// (the next init without one hands null); mode off never initialises the module, so nothing is handed.
bool CaseSharedIndexHanded(std::string& why)
{
	Main m(SecName("shared"));
	m.camera(1);
	Fake* fake = new Fake();
	WaterScene s{std::unique_ptr<WaterModule>(fake)};
	s.attach(m.hdr);
	const c5r::gamefs::GameFiles* token = reinterpret_cast<const c5r::gamefs::GameFiles*>(&m);   // never read by the fake
	WaterSceneOptions o;
	o.files = token;
	s.init(g_dev, o);
	EXPECT(fake->shares == 1 && fake->atInit == token && fake->inits == 1, "first init: shares %d, inits %d, index %s", fake->shares,
	       fake->inits, fake->atInit == token ? "the caller's" : "another");
	o.files = nullptr;
	s.init(g_dev, o);
	EXPECT(fake->shares == 2 && fake->atInit == nullptr && fake->inits == 2, "second init without an index: shares %d, inits %d, index %s",
	       fake->shares, fake->inits, fake->atInit ? "kept from the first" : "null");
	o.mode = WaterSceneOptions::kOff;
	o.files = token;
	s.init(g_dev, o);
	EXPECT(fake->shares == 2 && fake->inits == 2, "mode off: shares %d, inits %d", fake->shares, fake->inits);
	return true;
}

// Issue 0198: the real module (MakeC5rWaterModule) with an index the caller opened: the waves read Wave_Coastline_ALL.dds
// from it (available); a second init without one opens its own again (available); with an index of an empty install
// (Assets/ and Resource/ only) the waves are not available and their reason names the file, while the ocean still
// initialises from gameDir (init Ok).
bool CaseRealModuleSharedIndex(std::string& why)
{
	const std::wstring game = c5test::GameDir().dir, exe = game + L"\\CivilizationV_DX11.exe";
	EXPECT(GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES, "%S missing (set C5_GAME_DIR or C5R_GAME_DIR; skipped counts as failed)",
	       exe.c_str());
	const std::string buildWhy = c5test::ExeBuildWhy(exe);
	EXPECT(buildWhy.empty(), "%s", buildWhy.c_str());
	std::unique_ptr<WaterModule> mod = MakeC5rWaterModule();
	{
		c5r::gamefs::GameFiles fs;
		const c5r::Status os = fs.open(game);
		EXPECT(os.ok(), "the install's index: %s", os.str().c_str());
		mod->shareFiles(&fs);
		const c5r::Status st = mod->init(g_dev, exe, game);
		EXPECT(st.ok(), "init with the shared index: %s", st.str().c_str());
		EXPECT(mod->wavesAvailable(), "no waves from the shared index: %s", mod->wavesWhy().c_str());
		mod->release();
	}
	{
		const c5r::Status st = mod->init(g_dev, exe, game);   // nothing shared: its own index, as before 0198
		EXPECT(st.ok(), "init without a shared index: %s", st.str().c_str());
		EXPECT(mod->wavesAvailable(), "no waves from the module's own index: %s", mod->wavesWhy().c_str());
		mod->release();
	}
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	const std::wstring root = std::wstring(tmp) + L"c5_water_empty_index_0198";
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW((root + L"\\Assets").c_str(), nullptr);
	CreateDirectoryW((root + L"\\Resource").c_str(), nullptr);
	std::string fail;
	{
		c5r::gamefs::GameFiles empty;
		const c5r::Status os = empty.open(root);
		if(!os.ok())
			fail = "the empty install's index did not open: " + os.str();
		else
		{
			mod->shareFiles(&empty);
			const c5r::Status st = mod->init(g_dev, exe, game);
			if(!st.ok())
				fail = "the ocean did not initialise beside an empty index: " + st.str();
			else if(mod->wavesAvailable())
				fail = "waves available from an index without Wave_Coastline_ALL.dds";
			else if(mod->wavesWhy().find(c5r::water::kWaveTextureName) == std::string::npos)
				fail = "the reason does not name the wave texture: " + mod->wavesWhy();
			mod->release();
		}
	}
	RemoveDirectoryW((root + L"\\Assets").c_str());
	RemoveDirectoryW((root + L"\\Resource").c_str());
	RemoveDirectoryW(root.c_str());
	EXPECT(fail.empty(), "%s", fail.c_str());
	return true;
}

// The real module with the installed EXE and game (read-only; ../test_install.h: C5_GAME_DIR, else C5R_GAME_DIR, else
// the setting render_game_dir, never the Steam install, build 4390913 only), on WARP: the scene's inputs are accepted
// and one frame draws into a 64 x 64 target with a depth buffer.
bool CaseRealModuleDraws(std::string& why)
{
	const std::wstring game = c5test::GameDir().dir, exe = game + L"\\CivilizationV_DX11.exe";
	EXPECT(GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES, "%S missing (set C5_GAME_DIR or C5R_GAME_DIR; skipped counts as failed)",
	       exe.c_str());
	const std::string buildWhy = c5test::ExeBuildWhy(exe);
	EXPECT(buildWhy.empty(), "%s", buildWhy.c_str());
	Main m(SecName("realdraw"));
	m.camera(1);
	WaterScene s;
	s.attach(m.hdr);
	WaterSceneOptions o;
	o.exe = exe;
	o.gameDir = game;
	s.init(g_dev, o);
	c5r::heightfield::HeightGrid g = Grid();
	s.onField(g, 1);
	Published p;
	Publish(m, p, Image(1, 4321, 1), 1, 1);
	s.poll();
	EXPECT(s.decide(Cam(1), 0), "not drawing: %s", WaterWhyName(s.why()));
	EXPECT(s.wavesReady(), "the real module took no waves (log above)");
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = 64;
	d.MipLevels = d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.BindFlags = D3D11_BIND_RENDER_TARGET;
	ID3D11Texture2D *ct = nullptr, *dt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	g_dev->CreateTexture2D(&d, nullptr, &ct);
	d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	g_dev->CreateTexture2D(&d, nullptr, &dt);
	g_dev->CreateRenderTargetView(ct, nullptr, &rtv);
	g_dev->CreateDepthStencilView(dt, nullptr, &dsv);
	// a camera looking down at the map centre from above (row vectors, D3DX convention)
	c5r::terrain::Camera cam = TCam();
	float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -300, 1};
	float proj[16] = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0.5f, 1};
	memcpy(cam.view, view, sizeof(view));
	memcpy(cam.proj, proj, sizeof(proj));
	c5r::water::FrameStats st;
	const c5r::Status ds = s.draw(g_ctx, rtv, dsv, cam, &st);
	if(rtv) rtv->Release();
	if(dsv) dsv->Release();
	if(ct) ct->Release();
	if(dt) dt->Release();
	EXPECT(ds.ok(), "draw: %s", ds.str().c_str());
	EXPECT(s.drawsTotal == 1 && CountLog("LOUD") == 0, "draws %lld, LOUD %d", (long long)s.drawsTotal, CountLog("LOUD"));
	// issue 3605's river part: the tapped river mesh is drawn after the ocean with the tapped pass (4)
	EXPECT(st.riverDraws == 1 && st.riverPassUsed == 4, "river draws %u pass %d", st.riverDraws, st.riverPassUsed);
	// issue 3609: the tapped wave chunk drawn with the EXE's WaveShader once per ocean view
	EXPECT(st.waveDraws >= 1 && s.waveErrors == 0 && s.waveDrawsTotal == 1, "wave chunks drawn %u, errors %lld", st.waveDraws,
	       (long long)s.waveErrors);
	return true;
}

// ---- issue 0382: the reflection target, its camera and the hook that draws the ice into it ---------------------------------
// The texel (x, y) of the texture behind rtv (RGBA8), through a 1 x 1 staging copy; 0xDEADBEEF when it cannot be read.
uint32_t Texel(ID3D11RenderTargetView* rtv, UINT x, UINT y)
{
	ID3D11Resource* res = nullptr;
	rtv->GetResource(&res);
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = 1;
	d.MipLevels = d.ArraySize = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_STAGING;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* st = nullptr;
	uint32_t v = 0xDEADBEEFu;
	if(res && SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &st)))
	{
		const D3D11_BOX box = {x, y, 0, x + 1, y + 1, 1};
		g_ctx->CopySubresourceRegion(st, 0, 0, 0, 0, res, 0, &box);
		D3D11_MAPPED_SUBRESOURCE m;
		if(SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			memcpy(&v, m.pData, 4);
			g_ctx->Unmap(st, 0);
		}
		st->Release();
	}
	if(res)
		res->Release();
	return v;
}

struct HookRec
{
	int calls = 0;
	bool fail = false;
	uint32_t texelAtCall = 0;   // texel (5, 7) when the hook was called: (0,0,0,0) after the scene's clear
	uint32_t width = 0, height = 0;
	bool targets = false;
	float view[16] = {}, proj[16] = {};
	float camZ = 0;
};

c5r::Status Hook(void* u, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const c5r::terrain::Camera& rc,
                 float camZ, uint32_t* draws)
{
	HookRec& h = *(HookRec*)u;
	h.calls++;
	h.targets = rtv && dsv;
	h.texelAtCall = rtv ? Texel(rtv, 5, 7) : 0xDEADBEEFu;
	h.width = rc.width;
	h.height = rc.height;
	memcpy(h.view, rc.view, sizeof(h.view));
	memcpy(h.proj, rc.proj, sizeof(h.proj));
	h.camZ = camZ;
	const float red[4] = {1, 0, 0, 1};
	if(rtv)
		ctx->ClearRenderTargetView(rtv, red);   // "draws" over the whole target
	*draws = 3;
	return h.fail ? c5r::Status::fail(c5r::Err::Device, "fake: reflection draw failed") : c5r::Status::Ok();
}

// A drawing camera: at (0, -100, 300) tilted 45 degrees towards +y (as the game's), perspective.
c5r::terrain::Camera TiltCam()
{
	c5r::terrain::Camera c = TCam();
	const float s = 0.70710678f;
	// view = rows of the camera axes (x = (1,0,0), y = (0,s,s), z = (0,-s,s)), translation -eye . axes
	const float ex = 0, ey = -100, ez = 300;
	const float v[16] = {1, 0, 0, 0, 0, s, -s, 0, 0, s, s, 0, -ex, -(ey * s + ez * s), -(-ey * s + ez * s), 1};
	const float p[16] = {1.5f, 0, 0, 0, 0, 2.0f, 0, 0, 0, 0, -1.0001f, -1, 0, 0, -1.0001f, 0};   // RH perspective, near 1
	memcpy(c.view, v, sizeof(v));
	memcpy(c.proj, p, sizeof(p));
	return c;
}

bool CaseReflectionHook(std::string& why)
{
	Rig r("reflhook");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	HookRec h;
	WaterSceneReflection(&r.s, Hook, &h);
	const c5r::terrain::Camera cam = TiltCam();
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw 1");
	EXPECT(h.calls == 1 && h.targets && h.width == 1024 && h.height == 1024, "hook calls %d targets %d size %ux%u", h.calls, (int)h.targets,
	       h.width, h.height);
	EXPECT(h.texelAtCall == 0, "target not cleared before the hook: 0x%08X", h.texelAtCall);
	const cw::ReflectionCamera& rc = r.s.lastReflection();
	EXPECT(!memcmp(h.view, rc.view, 64) && !memcmp(h.proj, rc.proj, 64), "the hook's camera is not the reflection camera");
	EXPECT(!memcmp(r.fake->lastIn.shadowMatrix1, rc.viewProj, 64), "WaterFrame 0x160 is not the reflection's view-projection");
	// the reflection camera is the rule's for this view (c5r::water::reflectionCamera of view x proj)
	float vp[16];
	for(int i = 0; i < 4; ++i)
		for(int j = 0; j < 4; ++j)
		{
			float s = cam.view[i * 4] * cam.proj[j];
			s += cam.view[i * 4 + 1] * cam.proj[4 + j];
			s += cam.view[i * 4 + 2] * cam.proj[8 + j];
			s += cam.view[i * 4 + 3] * cam.proj[12 + j];
			vp[i * 4 + j] = s;
		}
	cw::ReflectionCamera want;
	EXPECT(cw::reflectionCamera(vp, want).ok() && !memcmp(want.viewProj, rc.viewProj, 64), "not the rule's camera");
	EXPECT(fabsf(h.camZ - 300.0f) < 1e-3f, "scene camera z %g, want 300", h.camZ);
	// the next frame: cleared again before the hook (the hook left red), then drawn again
	EXPECT(r.s.decide(Cam(1), 10), "not drawing (2)");
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw 2");
	EXPECT(h.calls == 2 && h.texelAtCall == 0, "frame 2: calls %d texel 0x%08X", h.calls, h.texelAtCall);
	EXPECT(r.s.reflFrames == 2 && r.s.reflErrors == 0 && r.s.reflBadCamera == 0, "counters %lld %lld %lld", (long long)r.s.reflFrames,
	       (long long)r.s.reflErrors, (long long)r.s.reflBadCamera);
	char line[1024];
	r.s.reflLine(line, sizeof(line));
	EXPECT(strstr(line, "w_refl_ice=1 w_refl_frames=2 w_refl_draws=3 w_refl_errors=0 w_refl_badcam=0"), "line: %s", line);
	// no hook: not called, the target stays (0,0,0,0) (the picture before 0382), 0x160 still the reflection's
	WaterSceneReflection(&r.s, nullptr, nullptr);
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw 3");
	EXPECT(h.calls == 2 && !memcmp(r.fake->lastIn.shadowMatrix1, rc.viewProj, 64), "without a hook: calls %d", h.calls);
	// a hook set for another scene is not called
	WaterScene other;
	WaterSceneReflection(&other, Hook, &h);
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && h.calls == 2, "another scene's hook was called");
	WaterSceneReflection(&r.s, nullptr, nullptr);
	EXPECT(CountLog("LOUD") == 0, "LOUD lines");
	return true;
}

bool CaseReflectionHookFails(std::string& why)
{
	Rig r("reflfail");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	HookRec h;
	h.fail = true;
	WaterSceneReflection(&r.s, Hook, &h);
	const c5r::terrain::Camera cam = TiltCam();
	for(int i = 0; i < 2; ++i)
		EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "a failed reflection must not fail the water draw");
	EXPECT(h.calls == 2 && r.fake->draws == 2, "calls %d module draws %d", h.calls, r.fake->draws);
	EXPECT(r.s.reflErrors == 2 && r.s.reflFrames == 0, "errors %lld frames %lld", (long long)r.s.reflErrors, (long long)r.s.reflFrames);
	EXPECT(CountLog("LOUD: water scene: the reflected ice was not drawn: Device: fake: reflection draw failed") == 1, "LOUD once per new text: %d",
	       CountLog("LOUD: water scene: the reflected ice was not drawn"));
	// the half-drawn target is cleared after the failure: the next call finds (0,0,0,0)
	h.fail = false;
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && h.texelAtCall == 0 && r.s.reflFrames == 1, "after recovery: texel 0x%08X frames %lld",
	       h.texelAtCall, (long long)r.s.reflFrames);
	WaterSceneReflection(&r.s, nullptr, nullptr);
	return true;
}

bool CaseReflectionOff(std::string& why)
{
	Rig r("refloff");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	HookRec h;
	WaterSceneReflection(&r.s, Hook, &h);
	WaterSceneSetReflectIce(false);   // render_ice_reflection=0
	const bool ok = r.s.draw(g_ctx, nullptr, nullptr, TiltCam(), nullptr).ok();
	char line[1024];
	r.s.reflLine(line, sizeof(line));
	WaterSceneSetReflectIce(true);
	WaterSceneReflection(&r.s, nullptr, nullptr);
	EXPECT(ok && h.calls == 0 && r.s.reflFrames == 0, "render_ice_reflection=0: the hook was called (%d)", h.calls);
	EXPECT(strstr(line, "w_refl_ice=0 w_refl_frames=0"), "line: %s", line);
	EXPECT(WaterSceneReflectIce(), "the switch is not restored");
	return true;
}

bool CaseReflectionBadCamera(std::string& why)
{
	Rig r("reflbadcam");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	HookRec h;
	WaterSceneReflection(&r.s, Hook, &h);
	// clip x and y depend on z only: no ground footprint, no reflection camera
	c5r::terrain::Camera cam = TCam();
	const float v[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0.5f, 0.001f, 0, 0, 0, 0.5f, 1};
	memcpy(cam.view, v, sizeof(v));
	for(int i = 0; i < 2; ++i)
		EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw %d", i);
	WaterSceneReflection(&r.s, nullptr, nullptr);
	EXPECT(h.calls == 0 && r.s.reflBadCamera == 2 && r.fake->draws == 2, "calls %d badcam %lld draws %d", h.calls, (long long)r.s.reflBadCamera,
	       r.fake->draws);
	const float* m = r.fake->lastIn.shadowMatrix1;
	EXPECT(m[0] == 1 && m[5] == 1 && m[10] == 1 && m[15] == 1 && m[12] == 0, "0x160 not the identity without a camera");
	EXPECT(CountLog("no reflection camera for this view") == 1 && CountLog("LOUD") == 0, "logged %d", CountLog("no reflection camera for this view"));
	return true;
}

// ---- issue 4464 (epic 0375, the merge with 0382): the models' hook (WaterSceneReflectionModels), the units' reflection ------
// The order of the hooks' calls across one test (each call takes the next number).
int g_hookSeq = 0;
struct SeqRec
{
	HookRec h;
	int seq = 0;   // the number of its newest call
};
c5r::Status SeqHook(void* u, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const c5r::terrain::Camera& rc,
                    float camZ, uint32_t* draws)
{
	SeqRec& s = *(SeqRec*)u;
	s.seq = ++g_hookSeq;
	return Hook(&s.h, ctx, rtv, dsv, rc, camZ, draws);
}

// The texel (x, y) of the reflection target t4, through its shader view (after the draw: what the water reads).
uint32_t ReflTexel(const WaterScene& s, UINT x, UINT y)
{
	ID3D11ShaderResourceView* srv = s.reflectionSrv();
	if(!srv)
		return 0xDEADBEEFu;
	ID3D11Resource* res = nullptr;
	srv->GetResource(&res);
	ID3D11RenderTargetView* rtv = nullptr;
	uint32_t v = 0xDEADBEEFu;
	if(res && SUCCEEDED(g_dev->CreateRenderTargetView(res, nullptr, &rtv)))
	{
		v = Texel(rtv, x, y);
		rtv->Release();
	}
	if(res)
		res->Release();
	return v;
}

// Every hook and the ice switch back to the defaults when a case ends, failed or not (they are process-wide).
struct HooksReset
{
	~HooksReset()
	{
		WaterSceneReflectionModels(nullptr, nullptr, nullptr);
		WaterSceneReflection(nullptr, nullptr, nullptr);
		WaterSceneSetReflectIce(true);
	}
};

const uint32_t kRed = 0xFF0000FFu;   // (1, 0, 0, 1) as RGBA8 read little-endian: what Hook leaves in the target

bool CaseReflectionModels(std::string& why)
{
	HooksReset reset;
	Rig r("reflmodels");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	g_hookSeq = 0;
	SeqRec models, ice;
	WaterSceneReflectionModels(&r.s, SeqHook, &models);
	WaterSceneReflection(&r.s, SeqHook, &ice);
	const c5r::terrain::Camera cam = TiltCam();
	for(int frame = 1; frame <= 2; ++frame)
	{
		EXPECT(r.s.decide(Cam(1), 10.0 * frame), "not drawing (%d)", frame);
		EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw %d", frame);
		// once per drawn frame, the models before the ice
		EXPECT(models.h.calls == frame && ice.h.calls == frame, "frame %d: models %d ice %d calls", frame, models.h.calls, ice.h.calls);
		EXPECT(models.seq + 1 == ice.seq, "frame %d: models call %d, ice call %d (want the models first)", frame, models.seq, ice.seq);
		// after the scene's clear (the previous frame left red), and the ice finds the models' drawing in the same target
		EXPECT(models.h.texelAtCall == 0, "frame %d: target not cleared before the models: 0x%08X", frame, models.h.texelAtCall);
		EXPECT(ice.h.texelAtCall == kRed, "frame %d: the ice did not draw into the models' target: 0x%08X", frame, ice.h.texelAtCall);
	}
	// the same target size, camera and z as the ice; the camera is the reflection camera (0382, WaterFrame 0x160)
	const cw::ReflectionCamera& rc = r.s.lastReflection();
	EXPECT(models.h.targets && models.h.width == 1024 && models.h.height == 1024 && models.h.width == WaterScene::kReflectionTargetSize,
	       "models: targets %d size %ux%u", (int)models.h.targets, models.h.width, models.h.height);
	EXPECT(!memcmp(models.h.view, ice.h.view, 64) && !memcmp(models.h.proj, ice.h.proj, 64), "models' camera is not the ice's");
	EXPECT(!memcmp(models.h.view, rc.view, 64) && !memcmp(models.h.proj, rc.proj, 64), "models' camera is not the reflection camera");
	EXPECT(models.h.camZ == ice.h.camZ && fabsf(models.h.camZ - 300.0f) < 1e-3f, "camera z %g / %g", models.h.camZ, ice.h.camZ);
	EXPECT(ReflTexel(r.s, 5, 7) == kRed, "t4 after the frame: 0x%08X", ReflTexel(r.s, 5, 7));
	char line[1024];
	r.s.reflLine(line, sizeof(line));
	EXPECT(strstr(line, "w_refl_ice=1 w_refl_frames=2 w_refl_draws=6 w_refl_errors=0"), "line: %s", line);
	// unset: not called, the ice still is
	WaterSceneReflectionModels(&r.s, nullptr, nullptr);
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && models.h.calls == 2 && ice.h.calls == 3, "unset: models %d ice %d",
	       models.h.calls, ice.h.calls);
	EXPECT(ice.h.texelAtCall == 0, "without the models the ice finds the cleared target: 0x%08X", ice.h.texelAtCall);
	// set for another scene: not called
	WaterScene other;
	WaterSceneReflectionModels(&other, SeqHook, &models);
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && models.h.calls == 2 && ice.h.calls == 4, "another scene's: models %d ice %d",
	       models.h.calls, ice.h.calls);
	EXPECT(r.s.reflFrames == 4 && r.s.reflErrors == 0 && CountLog("LOUD") == 0, "frames %lld errors %lld LOUD %d", (long long)r.s.reflFrames,
	       (long long)r.s.reflErrors, CountLog("LOUD"));
	return true;
}

bool CaseReflectionModelsIceOff(std::string& why)
{
	HooksReset reset;
	Rig r("reflmodelsoff");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	g_hookSeq = 0;
	SeqRec models, ice;
	WaterSceneReflectionModels(&r.s, SeqHook, &models);
	WaterSceneReflection(&r.s, SeqHook, &ice);
	WaterSceneSetReflectIce(false);   // render_ice_reflection=0 gates the ice, not the models
	const c5r::terrain::Camera cam = TiltCam();
	for(int i = 0; i < 2; ++i)
		EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw %d", i);
	EXPECT(models.h.calls == 2 && ice.h.calls == 0, "render_ice_reflection=0: models %d ice %d", models.h.calls, ice.h.calls);
	EXPECT(models.h.texelAtCall == 0 && models.h.width == 1024, "models: texel 0x%08X size %u", models.h.texelAtCall, models.h.width);
	EXPECT(!memcmp(models.h.view, r.s.lastReflection().view, 64), "models' camera is not the reflection camera");
	EXPECT(!memcmp(r.fake->lastIn.shadowMatrix1, r.s.lastReflection().viewProj, 64), "WaterFrame 0x160 is not the reflection's");
	EXPECT(ReflTexel(r.s, 5, 7) == kRed, "t4 does not hold the models' drawing: 0x%08X", ReflTexel(r.s, 5, 7));
	// the models alone make a reflected frame (fixed in 4464: was 0)
	char line[1024];
	r.s.reflLine(line, sizeof(line));
	EXPECT(r.s.reflFrames == 2 && strstr(line, "w_refl_ice=0 w_refl_frames=2 w_refl_draws=3 w_refl_errors=0"), "frames %lld line: %s",
	       (long long)r.s.reflFrames, line);
	EXPECT(CountLog("LOUD") == 0, "LOUD lines");
	return true;
}

bool CaseReflectionModelsBadCamera(std::string& why)
{
	HooksReset reset;
	Rig r("reflmodelsbadcam");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	SeqRec models;
	WaterSceneReflectionModels(&r.s, SeqHook, &models);
	c5r::terrain::Camera cam = TCam();   // no ground footprint: no reflection camera
	const float v[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0.5f, 0.001f, 0, 0, 0, 0.5f, 1};
	memcpy(cam.view, v, sizeof(v));
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "draw");
	EXPECT(models.h.calls == 0 && r.s.reflBadCamera == 1 && r.s.reflFrames == 0, "without a camera: calls %d badcam %lld frames %lld",
	       models.h.calls, (long long)r.s.reflBadCamera, (long long)r.s.reflFrames);
	EXPECT(ReflTexel(r.s, 5, 7) == 0, "t4 not empty: 0x%08X", ReflTexel(r.s, 5, 7));
	return true;
}

bool CaseReflectionModelsFail(std::string& why)
{
	HooksReset reset;
	Rig r("reflmodelsfail");
	Published p;
	Publish(r.m, p, Image(1, 4321, 0), 1, 1);
	r.s.poll();
	EXPECT(r.s.decide(Cam(1), 0), "not drawing");
	SeqRec models, ice;
	models.h.fail = true;
	WaterSceneReflectionModels(&r.s, SeqHook, &models);
	WaterSceneReflection(&r.s, SeqHook, &ice);
	const c5r::terrain::Camera cam = TiltCam();
	for(int i = 0; i < 2; ++i)
		EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok(), "a failed models' reflection must not fail the water draw (%d)", i);
	// counted, LOUD once per new text, the ice not drawn, the target cleared (the hook left red), the water drawn
	EXPECT(models.h.calls == 2 && ice.h.calls == 0 && r.fake->draws == 2, "models %d ice %d module draws %d", models.h.calls, ice.h.calls,
	       r.fake->draws);
	EXPECT(r.s.reflErrors == 2 && r.s.reflFrames == 0, "errors %lld frames %lld", (long long)r.s.reflErrors, (long long)r.s.reflFrames);
	const char* kLoud = "LOUD: water scene: the reflected models were not drawn: Device: fake: reflection draw failed";
	EXPECT(CountLog(kLoud) == 1 && CountLog("LOUD") == 1, "LOUD once per new text: %d (all LOUD %d)", CountLog(kLoud), CountLog("LOUD"));
	EXPECT(ReflTexel(r.s, 5, 7) == 0, "t4 not cleared after the failure: 0x%08X", ReflTexel(r.s, 5, 7));
	char line[1024];
	r.s.reflLine(line, sizeof(line));
	EXPECT(strstr(line, "w_refl_frames=0") && strstr(line, "w_refl_errors=2"), "line: %s", line);
	// recovery: both drawn again; the same failure afterwards is LOUD again (a new episode)
	models.h.fail = false;
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && models.h.calls == 3 && ice.h.calls == 1 && r.s.reflFrames == 1,
	       "recovery: models %d ice %d frames %lld", models.h.calls, ice.h.calls, (long long)r.s.reflFrames);
	models.h.fail = true;
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && CountLog(kLoud) == 2 && r.s.reflErrors == 3, "second episode: LOUD %d errors %lld",
	       CountLog(kLoud), (long long)r.s.reflErrors);
	// with the ice off (render_ice_reflection=0) too: a recovery by the models alone resets the text (fixed in 4464)
	WaterSceneSetReflectIce(false);
	models.h.fail = false;
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && r.s.reflFrames == 2, "ice off recovery: frames %lld", (long long)r.s.reflFrames);
	models.h.fail = true;
	EXPECT(r.s.draw(g_ctx, nullptr, nullptr, cam, nullptr).ok() && CountLog(kLoud) == 3 && ice.h.calls == 1, "ice off: LOUD %d ice %d",
	       CountLog(kLoud), ice.h.calls);
	return true;
}

// Ticket 4909: a live edit's region goes to the module's region update; a module without one (Unsupported) takes the
// whole field silently, one that refuses takes it with a LOUD line; another grid than the scene's is the whole field.
bool CaseFieldRegion(std::string& why)
{
	std::vector<c5r::terrain::SampleRect> rects(2);
	rects[0].x1 = rects[0].y1 = 10;
	rects[1].x0 = rects[1].y0 = 20;
	rects[1].x1 = rects[1].y1 = 30;
	{
		Rig r("fieldregion1");
		r.fake->regionMode = 1;
		r.s.onFieldRegion(r.grid, rects, nullptr, nullptr);
		EXPECT(r.fake->regions == 1 && r.fake->regionRects == 2 && r.fake->fields == 1, "ok: regions %d rects %zu fields %d",
		       r.fake->regions, r.fake->regionRects, r.fake->fields);
	}
	{
		Rig r("fieldregion0");
		r.s.onFieldRegion(r.grid, rects, nullptr, nullptr);
		EXPECT(r.fake->regions == 1 && r.fake->fields == 2 && CountLog("region update") == 0, "unsupported: regions %d fields %d",
		       r.fake->regions, r.fake->fields);
	}
	{
		Rig r("fieldregion2");
		r.fake->regionMode = 2;
		r.s.onFieldRegion(r.grid, rects, nullptr, nullptr);
		EXPECT(r.fake->regions == 1 && r.fake->fields == 2 && CountLog("LOUD: water scene: the region update") == 1,
		       "refused: regions %d fields %d", r.fake->regions, r.fake->fields);
	}
	{
		Rig r("fieldregion3");
		r.fake->regionMode = 1;
		const c5r::heightfield::HeightGrid other = Grid();
		r.s.onFieldRegion(other, rects, nullptr, nullptr);
		EXPECT(r.fake->regions == 0 && r.fake->fields == 2, "other grid: regions %d fields %d", r.fake->regions, r.fake->fields);
	}
	return true;
}

// Ticket 0589: ../test_install.h's rules, with made-up files only (nothing is read from an install): the install's order
// (C5_GAME_DIR, C5R_GAME_DIR, C5_RENDER_GAME_DIR, render_game_dir of the settings file) and a placeholder naming the
// variables when none is set (never the Steam path); the EXE check refuses a missing file, a non-PE file, another
// stamp and build 4390913's stamp at an unknown size, accepts the stamp at a known size, and lets an unknown build
// through only with C5_ALLOW_UNKNOWN_EXE_BUILD=1. The variables are restored at the end.
bool WriteBytes(const std::wstring& path, const std::string& bytes, LONGLONG size = -1)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if(h == INVALID_HANDLE_VALUE)
		return false;
	DWORD put = 0;
	bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &put, nullptr) && put == bytes.size();
	if(ok && size >= 0)
	{
		LARGE_INTEGER at;
		at.QuadPart = size;
		ok = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && SetEndOfFile(h);
	}
	CloseHandle(h);
	return ok;
}

std::string FakePe(DWORD stamp)
{
	std::string b(512, '\0');
	b[0] = 'M';
	b[1] = 'Z';
	*(DWORD*)&b[0x3C] = 0x80;
	memcpy(&b[0x80], "PE\0\0", 4);
	*(WORD*)&b[0x84] = 0x14C;
	*(DWORD*)&b[0x88] = stamp;
	return b;
}

bool CaseInstallRules(std::string& why)
{
	const wchar_t* vars[] = {L"C5_GAME_DIR", L"C5R_GAME_DIR", L"C5_RENDER_GAME_DIR", L"C5_SETTINGS_FILE", c5test::kAllowEnv};
	struct Saved
	{
		const wchar_t* const* vars;
		std::wstring value[5];
		bool had[5] = {};
		~Saved()
		{
			for(int i = 0; i < 5; ++i)
				SetEnvironmentVariableW(vars[i], had[i] ? value[i].c_str() : nullptr);
		}
	} saved{vars};
	for(int i = 0; i < 5; ++i)
	{
		saved.value[i] = c5test::EnvW(vars[i]);
		saved.had[i] = !saved.value[i].empty();
		SetEnvironmentVariableW(vars[i], nullptr);
	}
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	const std::wstring root = std::wstring(tmp) + L"c5_0589_install_rules_" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(root.c_str(), nullptr);
	struct Cleanup
	{
		std::wstring root;
		~Cleanup()
		{
			for(const wchar_t* n : {L"settings.ini", L"tilde.ini", L"text.exe", L"other.exe", L"small.exe", L"known.exe"})
				DeleteFileW((root + L"\\" + n).c_str());
			RemoveDirectoryW(root.c_str());
		}
	} cleanup{root};

	// the order, and the placeholder
	SetEnvironmentVariableW(L"C5_SETTINGS_FILE", (root + L"\\absent.ini").c_str());
	c5test::Install in = c5test::GameDir();
	EXPECT(!in.set && in.dir.find(L"C5_GAME_DIR") != std::wstring::npos && in.dir.find(L"C5R_GAME_DIR") != std::wstring::npos &&
	           in.dir.find(L"steamapps") == std::wstring::npos && GetFileAttributesW((in.dir + L"\\CivilizationV_DX11.exe").c_str()) == INVALID_FILE_ATTRIBUTES,
	       "nothing set: %S (from %s)", in.dir.c_str(), in.from.c_str());
	EXPECT(WriteBytes(root + L"\\settings.ini", "# a comment\r\nrender_exe = C:\\nope\r\n  render_game_dir =  C:\\from-settings  \r\n"),
	       "settings file not written");
	EXPECT(WriteBytes(root + L"\\tilde.ini", "render_game_dir = ~\\civ\n"), "settings file not written");
	SetEnvironmentVariableW(L"C5_SETTINGS_FILE", (root + L"\\settings.ini").c_str());
	in = c5test::GameDir();
	EXPECT(in.set && in.dir == L"C:\\from-settings" && in.from.find("settings file") != std::string::npos, "settings file: %S (from %s)",
	       in.dir.c_str(), in.from.c_str());
	SetEnvironmentVariableW(L"C5_SETTINGS_FILE", (root + L"\\tilde.ini").c_str());
	in = c5test::GameDir();
	EXPECT(in.dir == c5test::EnvW(L"USERPROFILE") + L"\\civ", "~ in the settings file: %S", in.dir.c_str());
	SetEnvironmentVariableW(L"C5_RENDER_GAME_DIR", L"C:\\from-render-setting");
	EXPECT(c5test::GameDir().dir == L"C:\\from-render-setting", "C5_RENDER_GAME_DIR not taken before the settings file");
	SetEnvironmentVariableW(L"C5R_GAME_DIR", L"C:\\from-c5r");
	EXPECT(c5test::GameDir().dir == L"C:\\from-c5r" && c5test::GameDir().from == "C5R_GAME_DIR", "C5R_GAME_DIR not taken before C5_RENDER_GAME_DIR");
	SetEnvironmentVariableW(L"C5_GAME_DIR", L"C:\\from-c5");
	EXPECT(c5test::GameDir().dir == L"C:\\from-c5" && c5test::GameDir().from == "C5_GAME_DIR", "C5_GAME_DIR not taken first");

	// the EXE build
	std::string w = c5test::ExeBuildWhy(root + L"\\none.exe");
	EXPECT(w.find("does not exist") != std::string::npos && w.find("refusing") == 0, "missing: %s", w.c_str());
	EXPECT(WriteBytes(root + L"\\text.exe", "not a program"), "text.exe not written");
	w = c5test::ExeBuildWhy(root + L"\\text.exe");
	EXPECT(w.find("is not a PE file") != std::string::npos, "not PE: %s", w.c_str());
	EXPECT(WriteBytes(root + L"\\other.exe", FakePe(0x12345678), 16982584), "other.exe not written");
	w = c5test::ExeBuildWhy(root + L"\\other.exe");
	EXPECT(w.find("TimeDateStamp 0x12345678") != std::string::npos && w.find("a different build") != std::string::npos,
	       "another stamp: %s", w.c_str());
	EXPECT(WriteBytes(root + L"\\small.exe", FakePe(c5test::kKnownStamp)), "small.exe not written");
	w = c5test::ExeBuildWhy(root + L"\\small.exe");
	EXPECT(w.find("unknown size") != std::string::npos, "known stamp, 512 bytes: %s", w.c_str());
	for(LONGLONG size : {16982584ll, 16982840ll})
	{
		EXPECT(WriteBytes(root + L"\\known.exe", FakePe(c5test::kKnownStamp), size), "known.exe not written");
		w = c5test::ExeBuildWhy(root + L"\\known.exe");
		EXPECT(w.empty(), "build 4390913 at %lld bytes refused: %s", size, w.c_str());
	}
	SetEnvironmentVariableW(c5test::kAllowEnv, L"0");
	EXPECT(!c5test::ExeBuildWhy(root + L"\\other.exe").empty(), "C5_ALLOW_UNKNOWN_EXE_BUILD=0 let another build through");
	SetEnvironmentVariableW(c5test::kAllowEnv, L"1");
	EXPECT(c5test::ExeBuildWhy(root + L"\\other.exe").empty(), "C5_ALLOW_UNKNOWN_EXE_BUILD=1 did not let another build through");
	return true;
}

const Case kCases[] = {
	{"install_rules", CaseInstallRules},   // ticket 0589
	{"field_region", CaseFieldRegion},
	{"reflection_hook", CaseReflectionHook}, {"reflection_hook_fails", CaseReflectionHookFails}, {"reflection_off", CaseReflectionOff},
	{"reflection_bad_camera", CaseReflectionBadCamera},
	{"reflection_models", CaseReflectionModels}, {"reflection_models_ice_off", CaseReflectionModelsIceOff},
	{"reflection_models_bad_camera", CaseReflectionModelsBadCamera}, {"reflection_models_fail", CaseReflectionModelsFail},
	{"real_module_draws", CaseRealModuleDraws},
	{"no_tap", CaseNoTap},           {"module_draws", CaseModuleDraws}, {"clock", CaseClock},       {"freeze", CaseFreeze},
	{"refused", CaseRefused},        {"stale", CaseStale},              {"other_load", CaseOtherLoad}, {"nan_camera", CaseNanCamera},
	{"module_missing", CaseModuleMissing}, {"modes", CaseModes},       {"waves_drawn", CaseWavesDrawn}, {"waves_bad", CaseWavesBad}, {"log_once", CaseLogOnce},
	{"real_module_bad_exe", CaseRealModuleBadExe},
	{"shared_index_handed", CaseSharedIndexHanded}, {"real_module_shared_index", CaseRealModuleSharedIndex},
};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
	{
		printf("water_scene_test: no WARP device\n");
		return 99;
	}
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			if(!strcmp(argv[i], c.name))
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
		printf("water_scene_test: no case matches\n");
		return 2;
	}
	g_ctx->Release();
	g_dev->Release();
	return fails;
}
