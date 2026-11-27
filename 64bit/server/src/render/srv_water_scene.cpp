// srv_water_scene.cpp - see srv_water_scene.h (epic 3600, issue 3607).
// Written 2026-11-14 for this repository (GPL-3.0-or-later).
#include "srv_water_scene.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace srvrender {

namespace cw = c5r::water;

const char* WaterWhyName(int w)
{
	static const char* n[WW_COUNT] = {"MODULE", "OFF", "PLACEHOLDER", "NO_MODULE", "NO_FIELD", "NO_TAP", "OTHER_LOAD", "NO_FIELDS"};
	return w >= 0 && w < WW_COUNT ? n[w] : "?";
}

namespace {

struct C5rWaterModule : WaterModule
{
	cw::Renderer r;
	cw::WaveRenderer wr;   // issue 3609: the coast waves (3606's renderer, the EXE's WaveShader)
	bool wavesOk = false;
	std::string wavesNo = "not initialised";
	const c5r::gamefs::GameFiles* shared = nullptr;   // issue 0198: the scene's index, for the next init only
	void shareFiles(const c5r::gamefs::GameFiles* f) override { shared = f; }
	c5r::Status init(ID3D11Device* dev, const std::wstring& exe, const std::wstring& gameDir) override
	{
		wavesOk = false;
		const c5r::gamefs::GameFiles* given = shared;
		shared = nullptr;
		C5R_TRY(r.init(dev, exe, gameDir));
		// The waves need the install's Wave_Coastline_ALL.dds; without them the ocean and rivers still draw.
		c5r::Status ws = gameDir.empty() ? c5r::Status::fail(c5r::Err::NotFound, "no install folder") : c5r::Status::Ok();
		if(ws.ok())
		{
			c5r::shaders::ShaderSection sec;
			c5r::gamefs::GameFiles fs;
			ws = sec.open(exe);
			if(ws.ok() && !given)
				ws = fs.open(gameDir);
			if(ws.ok())
				ws = wr.init(dev, sec, given ? *given : fs);
		}
		wavesOk = ws.ok();
		wavesNo = ws.ok() ? std::string() : ws.str();
		return c5r::Status::Ok();
	}
	bool wavesAvailable() const override { return wavesOk; }
	std::string wavesWhy() const override { return wavesNo; }
	c5r::Status setWaveChunks(const std::vector<cw::WaveChunk>& c) override
	{
		return wavesOk ? wr.setChunks(c) : c5r::Status::fail(c5r::Err::NotFound, "waves: " + wavesNo);
	}
	c5r::Status drawWaves(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const cw::WaveObject& wo,
	                      float planeZ, const std::vector<cw::WaveRecord>& waves, cw::WaveStats* st) override
	{
		if(st)
			*st = cw::WaveStats();
		if(!wavesOk)
			return c5r::Status::fail(c5r::Err::NotFound, "waves: " + wavesNo);
		const std::vector<std::vector<uint8_t>>& fr = r.frames();   // one WaterFrame per view of the last draw (0x000 = ViewProj)
		if(fr.empty())
			return c5r::Status::fail(c5r::Err::NotFound, "waves: no ocean frame drawn yet");
		for(const auto& f : fr)
		{
			float vp[16];
			if(f.size() < sizeof(vp))
				return c5r::Status::fail(c5r::Err::BadLayout, "waves: a WaterFrame shorter than its ViewProj");
			memcpy(vp, f.data(), sizeof(vp));
			cw::WaveStats one;
			C5R_TRY(wr.draw(ctx, rtv, dsv, vp, wo, planeZ, waves, &one));
			if(st)
			{
				st->chunksDrawn += one.chunksDrawn;
				st->indices += one.indices;
			}
		}
		return c5r::Status::Ok();
	}
	c5r::Status setHeightField(const c5r::heightfield::HeightGrid& g) override { return r.setHeightField(g); }
	c5r::Status updateHeightRegion(const c5r::heightfield::HeightGrid& g, const std::vector<c5r::terrain::SampleRect>& rects,
	                               const std::vector<c5r::bake::HeightPage>* built, c5r::terrain::RegionUpdateStats* st) override
	{
		return r.updateHeightRegion(g, rects, built, st);
	}
	c5r::Status setMesh(cw::System s, const cw::Mesh& m) override { return r.setMesh(s, m); }
	c5r::Status setTexture(cw::Slot slot, ID3D11ShaderResourceView* srv) override { return r.setTexture(slot, srv); }
	c5r::Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const cw::FrameInputs& in,
	                 const cw::Options& o, cw::FrameStats* st) override
	{
		return r.draw(ctx, rtv, dsv, in, o, st);
	}
	void release() override
	{
		wr.release();
		r.release();
		wavesOk = false;
		wavesNo = "released";
	}
};

float F32(const std::vector<uint8_t>& b, size_t off)
{
	float f = 0;
	if(off + 4 <= b.size())
		memcpy(&f, b.data() + off, 4);
	return f;
}

bool Finite(const float* p, int n)
{
	for(int i = 0; i < n; ++i)
		if(!std::isfinite(p[i]))
			return false;
	return true;
}

// The Water object's offsets the scene uses (water-system.md 7 [L 3601], 13).
const size_t kWaterObjBytes = 0xEC;
const size_t kFieldsFrom = 0x60;        // WaterFields::raw = Water+0x60 .. +0xE8
const size_t kSpeed = 0xD8, kClockA = 0xDC, kRateB = 0xE0, kClockB = 0xE4, kOceanPass = 0xE8;
const uint32_t kReflectionSize = 1024;   // "Reflection Texture Raw" when the ocean pass is not 0 [C 3602, L 3601]

// The tap's wave records (shell/composite/water_tap.cpp Gather, water-system.md 9.4): the Wave_System object (exeOffset 0),
// the chunk array (Wave+0x54, 36 bytes per chunk), the wave array (Wave+0x5C, 16 bytes per wave); the chunks' draw
// records (Wave+0x54 too, 0x80 bytes each) are skipped by their size.
const uint32_t kWaveChunkBytes = 36, kWaveRecBytes = 16;
const uint32_t kWaveChunksAt = 0x54, kWavesAt = 0x5C;
const double kWaveMaxDtMs = 250.0;   // a longer gap (a stall, a load) advances the waves by this much, not all at once

uint32_t U32At(const std::vector<uint8_t>& b, size_t off)
{
	uint32_t v = 0;
	if(off + 4 <= b.size())
		memcpy(&v, b.data() + off, 4);
	return v;
}

}  // namespace

c5r::Status ParseWaveRecords(const std::vector<WaterTapRecordCopy>& recs, cw::WaveObject& wo, std::vector<cw::WaveChunk>& chunks,
                             std::vector<cw::WaveRecord>& waves)
{
	chunks.clear();
	waves.clear();
	wo = cw::WaveObject();
	const WaterTapRecordCopy* obj = nullptr;
	const WaterTapRecordCopy* arr = nullptr;
	const WaterTapRecordCopy* wav = nullptr;
	std::vector<const VPShellWaterRecord*> draws;
	for(const auto& r : recs)
	{
		if(r.rec.system != VPWSYS_WAVE)
			continue;
		if(r.rec.role == VPWROLE_OBJECT && r.rec.exeOffset == 0 && !obj)
			obj = &r;
		else if(r.rec.role == VPWROLE_DRAW)
			draws.push_back(&r.rec);
	}
	if(!obj)
		return c5r::Status::fail(c5r::Err::NotFound, "waves: no Wave_System object in the image");
	C5R_TRY(cw::parseWaveObject(obj->data.data(), obj->data.size(), wo));
	if(!wo.chunkCount)
		return c5r::Status::Ok();
	for(const auto& r : recs)
	{
		if(r.rec.system != VPWSYS_WAVE || r.rec.role != VPWROLE_OBJECT)
			continue;
		if(r.rec.exeOffset == kWaveChunksAt && r.data.size() == (size_t)wo.chunkCount * kWaveChunkBytes && !arr)
			arr = &r;
		else if(r.rec.exeOffset == kWavesAt && !wav)
			wav = &r;
	}
	char b[200];
	if(!arr)
	{
		_snprintf_s(b, _TRUNCATE, "waves: no chunk array of %u x %u bytes", wo.chunkCount, kWaveChunkBytes);
		return c5r::Status::fail(c5r::Err::BadLayout, b);
	}
	if(!wav || wav->data.size() != (size_t)wo.waveCount * kWaveRecBytes)
	{
		_snprintf_s(b, _TRUNCATE, "waves: the wave array is %zu bytes, the object says %u waves", wav ? wav->data.size() : (size_t)0, wo.waveCount);
		return c5r::Status::fail(c5r::Err::BadLayout, b);
	}
	if(draws.size() != wo.chunkCount)
	{
		_snprintf_s(b, _TRUNCATE, "waves: %zu draws for %u chunks", draws.size(), wo.chunkCount);
		return c5r::Status::fail(c5r::Err::BadLayout, b);
	}
	waves.resize(wo.waveCount);
	for(uint32_t i = 0; i < wo.waveCount; ++i)
	{
		memcpy(&waves[i], wav->data.data() + (size_t)i * kWaveRecBytes, 12);
		waves[i].unused = 0.0f;   // the EXE never writes it (uninitialised, 12.5); the shader does not read it
	}
	for(uint32_t k = 0; k < wo.chunkCount; ++k)
	{
		const VPShellWaterRecord& d = *draws[k];
		if(d.vbRecord >= recs.size() || d.ibRecord >= recs.size())
		{
			_snprintf_s(b, _TRUNCATE, "waves: chunk %u's draw names no buffers", k);
			return c5r::Status::fail(c5r::Err::BadLayout, b);
		}
		const WaterTapRecordCopy& vb = recs[d.vbRecord];
		const WaterTapRecordCopy& ib = recs[d.ibRecord];
		if(vb.rec.role != VPWROLE_VB || vb.rec.stride != cw::kWaveVertexBytes || ib.rec.role != VPWROLE_IB || ib.rec.dxgiFormat != 57 ||
		   (size_t)d.indexCount * 2 > ib.data.size() || !d.indexCount)
		{
			_snprintf_s(b, _TRUNCATE, "waves: chunk %u: VB stride %u, IB format %u with %zu bytes for %u indices", k, vb.rec.stride,
			            ib.rec.dxgiFormat, ib.data.size(), d.indexCount);
			return c5r::Status::fail(c5r::Err::BadLayout, b);
		}
		const size_t c = (size_t)k * kWaveChunkBytes;
		cw::WaveChunk ch;
		ch.vb = vb.data;
		ch.ib.assign(ib.data.begin(), ib.data.begin() + (size_t)d.indexCount * 2);   // the draw's triangles (prims x 3)
		ch.firstWave = U32At(arr->data, c + 0x0C);
		ch.waveCount = U32At(arr->data, c + 0x10);
		for(int j = 0; j < 4; ++j)
			ch.bounds[j] = F32(arr->data, c + 0x14 + 4 * j);
		if((uint64_t)ch.firstWave + ch.waveCount > wo.waveCount)
		{
			_snprintf_s(b, _TRUNCATE, "waves: chunk %u: waves %u + %u past the %u waves", k, ch.firstWave, ch.waveCount, wo.waveCount);
			return c5r::Status::fail(c5r::Err::BadLayout, b);
		}
		chunks.push_back(std::move(ch));
	}
	return c5r::Status::Ok();
}

std::unique_ptr<WaterModule> MakeC5rWaterModule() { return std::unique_ptr<WaterModule>(new C5rWaterModule()); }

WaterScene::WaterScene(std::unique_ptr<WaterModule> m) : m_mod(std::move(m)) {}

WaterScene::~WaterScene() { releaseDevice(); }

void WaterScene::attach(VPShellHeader* hdr)
{
	m_reader.reset(hdr ? new WaterTapReader(hdr) : nullptr);
	m_haveImage = false;
	m_seenAccepted = 0;
}

void WaterScene::init(ID3D11Device* dev, const WaterSceneOptions& o)
{
	releaseDevice();
	m_opt = o;
	m_opt.files = nullptr;      // issue 0198: o.files is valid during this call only
	m_modTried = true;
	if(o.mode != WaterSceneOptions::kFull)
	{
		RNote("water scene: render_water=%s: %s", o.mode == WaterSceneOptions::kOff ? "0" : "placeholder",
		      o.mode == WaterSceneOptions::kOff ? "no water drawn" : "the flat placeholder plane only (the tap is still read and acknowledged)");
		return;
	}
	if(!m_mod)
		m_mod = MakeC5rWaterModule();
	const int64_t q0 = Qpc();
	m_mod->shareFiles(o.files);
	const c5r::Status st = m_mod->init(dev, o.exe, o.gameDir);
	if(!st.ok())
	{
		m_modWhy = st.str();
		// not LOUD: until the module has landed (issue 3604) and on an install without its art this is the expected fallback
		RNote("water scene: the water module is not available (%s): the placeholder plane is drawn", m_modWhy.c_str());
		return;
	}
	m_modOk = true;
	m_modWhy.clear();
	{   // t4: "Reflection Texture Raw", (0,0,0,0) at 1024 x 1024 RGBA8; issue 0382: a render target with a D24S8 depth that
		// the reflection hook draws the ice into (made as stock's: the pass clears colour (0,0,0,0) and depth 1, 0x5A0F80)
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = d.Height = kReflectionSize;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		std::vector<uint8_t> zero((size_t)kReflectionSize * kReflectionSize * 4, 0);
		D3D11_SUBRESOURCE_DATA sd = {zero.data(), kReflectionSize * 4, 0};
		c5r::Status ts = c5r::Status::fail(c5r::Err::Device, "no device");
		D3D11_TEXTURE2D_DESC dd = d;
		dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if(dev && SUCCEEDED(dev->CreateTexture2D(&d, &sd, &m_reflTex)) && SUCCEEDED(dev->CreateShaderResourceView(m_reflTex, nullptr, &m_reflSrv)))
		{
			ts = m_mod->setTexture(cw::Slot::Reflection, m_reflSrv);
			if(!SUCCEEDED(dev->CreateRenderTargetView(m_reflTex, nullptr, &m_reflRtv)) || !SUCCEEDED(dev->CreateTexture2D(&dd, nullptr, &m_reflDepth)) ||
			   !SUCCEEDED(dev->CreateDepthStencilView(m_reflDepth, nullptr, &m_reflDsv)))
			{
				reflectionErrors++;
				SafeRelease(m_reflRtv);
				SafeRelease(m_reflDsv);
				SafeRelease(m_reflDepth);
				RNote("LOUD: water scene: the reflection target cannot be drawn into (render target or depth not made): t4 stays (0,0,0,0), "
				      "no reflected ice");
			}
		}
		if(!ts.ok())
		{
			reflectionErrors++;
			RNote("water scene: the empty reflection target could not be bound (%s): the module's own t4 stays", ts.str().c_str());
		}
	}
	RNote("water scene: module ready in %.1f ms (EXE '%ls', install '%ls'), clock %s", QpcToMs(Qpc() - q0), o.exe.c_str(),
	      o.gameDir.c_str(), o.freeze ? "FROZEN (render_water_freeze=1, diagnostics)" : "the server's own at the EXE's rates");
	if(m_haveField && m_grid)
	{
		const c5r::Status hs = m_mod->setHeightField(*m_grid);
		if(!hs.ok())
		{
			m_haveField = false;
			RNote("water scene: setHeightField (re-applied) failed: %s", hs.str().c_str());
		}
	}
	if(m_haveImage)
		useImage();   // the meshes on the new device
}

void WaterScene::releaseDevice()
{
	if(m_mod)
		m_mod->release();
	SafeRelease(m_reflSrv);
	SafeRelease(m_reflRtv);
	SafeRelease(m_reflTex);
	SafeRelease(m_reflDsv);
	SafeRelease(m_reflDepth);
	m_modOk = false;
}

void WaterScene::poll()
{
	if(!m_reader)
		return;
	if(m_reader->poll())
		useImage();
}

void WaterScene::onField(const c5r::heightfield::HeightGrid& g, uint32_t loadSeq)
{
	m_grid = &g;
	m_fieldLoad = loadSeq;
	m_haveField = true;
	if(!m_modOk)
		return;
	const c5r::Status st = m_mod->setHeightField(g);
	if(!st.ok())
	{
		m_haveField = false;
		RNote("water scene: setHeightField (load seq %u) failed: %s: the placeholder plane is drawn", loadSeq, st.str().c_str());
	}
}

void WaterScene::onFieldRegion(const c5r::heightfield::HeightGrid& g, const std::vector<c5r::terrain::SampleRect>& rects,
                               const std::vector<c5r::bake::HeightPage>* built, c5r::terrain::RegionUpdateStats* stats)
{
	if(!m_modOk || !m_haveField || m_grid != &g)
	{
		onField(g, m_fieldLoad);   // not the field the module holds (or none): the whole field, as before 4909
		return;
	}
	const c5r::Status st = m_mod->updateHeightRegion(g, rects, built, stats);
	if(st.ok())
		return;
	if(st.code != c5r::Err::Unsupported)
		RNote("LOUD: water scene: the region update (%zu rects) failed: %s: the whole field is set", rects.size(), st.str().c_str());
	onField(g, m_fieldLoad);
}

// The accepted image -> the Water fields, the clock's start values and one mesh per system.
void WaterScene::useImage()
{
	if(!m_reader)
		return;
	const std::vector<WaterTapRecordCopy>& recs = m_reader->records();
	// ticket 0586: another image of the same load (a re-copy after the EXE's Waves_Reload, or the meshes on a new device)
	// keeps the water clock running: under D33 the tapped clock fields never advance, so starting again would make the
	// water's animation jump back to its load-time phase at every live change of the coast waves
	const bool sameLoad = m_haveImage && m_imageLoad == m_reader->loadSeq();
	const double keepStartMs = m_clockStartMs;
	const float keepDc0 = m_dc0, keepE4 = m_e4;
	m_haveImage = true;
	m_imageLoad = m_reader->loadSeq();
	m_waterZ = m_reader->image().waterZ;
	imagesUsed++;
	// the Water object (system OCEAN, OBJECT 0)
	m_fields = cw::WaterFields();
	for(const auto& r : recs)
	{
		if(r.rec.system != VPWSYS_OCEAN || r.rec.role != VPWROLE_OBJECT || r.rec.index != 0 || r.data.size() < kWaterObjBytes)
			continue;
		memcpy(m_fields.raw, r.data.data() + kFieldsFrom, sizeof(m_fields.raw));
		m_dc0 = F32(r.data, kClockA);
		m_e4 = F32(r.data, kClockB);
		m_dcRate = 4.0f * F32(r.data, kSpeed);   // 0.721/s at speed 0.18 [L 3601: +0xDC advances 4 x +0xD8 per second]
		m_e4Rate = F32(r.data, kRateB);          // 0.03/s [L 3601: +0xE4 advances +0xE0 per second]
		uint32_t pass = 0xFFFFFFFFu;
		if(r.data.size() >= kOceanPass + 4)
			memcpy(&pass, r.data.data() + kOceanPass, 4);
		m_oceanPass = pass == 0 || pass == 2 || pass == 3 ? (int)pass : -1;   // Water::Startup's choice by the quality setting
		const float c[4] = {m_dc0, m_e4, m_dcRate, m_e4Rate};
		m_fields.valid = Finite(c, 4) && m_dcRate >= 0 && m_dcRate < 100 && m_e4Rate >= 0 && m_e4Rate < 100;
		if(!m_fields.valid)
			RNote("water scene: load seq %u: the Water object's clock fields are not usable (%g %g %g %g)", m_imageLoad, m_dc0, m_e4,
			      m_dcRate, m_e4Rate);
	}
	m_clockStartMs = -1;   // starts at the first frame drawn with this image
	if(sameLoad && m_fields.valid)
	{   // ticket 0586: the same load's clock goes on (start values and start time of the first image)
		m_clockStartMs = keepStartMs;
		m_dc0 = keepDc0;
		m_e4 = keepE4;
	}
	if(!m_modOk)
		return;
	// the rivers as one mesh (a system's buffers would be joined into one VB and IB); the waves go to the wave renderer
	// below (issue 3609), so the module's Wave mesh is cleared
	m_systems = 0;
	{
		const c5r::Status cs = m_mod->setMesh(cw::System::Wave, cw::Mesh());
		if(!cs.ok())
			RNote("water scene: load seq %u: clearing the module's wave mesh failed: %s", m_imageLoad, cs.str().c_str());
	}
	const cw::System systems[1] = {cw::System::River};
	for(cw::System sys : systems)
	{
		cw::Mesh m;
		std::vector<uint32_t> vbFirst(recs.size(), 0), ibFirst(recs.size(), 0);
		bool bad = false;
		for(size_t i = 0; i < recs.size(); ++i)
		{
			const VPShellWaterRecord& r = recs[i].rec;
			if(r.system != (uint32_t)sys)
				continue;
			if(r.role == VPWROLE_VB)
			{
				if(m.stride && m.stride != r.stride)
					bad = true;
				m.stride = r.stride;
				vbFirst[i] = m.stride ? (uint32_t)(m.vb.size() / m.stride) : 0;
				m.vb.insert(m.vb.end(), recs[i].data.begin(), recs[i].data.end());
			}
			else if(r.role == VPWROLE_IB)
			{
				if(m.indexFormat && m.indexFormat != r.dxgiFormat)
					bad = true;
				m.indexFormat = r.dxgiFormat;
				const uint32_t e = r.dxgiFormat == 42 ? 4 : 2;
				ibFirst[i] = (uint32_t)(m.ib.size() / e);
				m.ib.insert(m.ib.end(), recs[i].data.begin(), recs[i].data.end());
			}
		}
		for(size_t i = 0; i < recs.size(); ++i)
		{
			const VPShellWaterRecord& r = recs[i].rec;
			if(r.system != (uint32_t)sys || r.role != VPWROLE_DRAW || r.vbRecord >= recs.size() || r.ibRecord >= recs.size())
				continue;
			cw::Mesh::Draw d;
			d.topology = r.topology;
			d.vertexCount = r.vertexCount;
			d.indexCount = r.indexCount;
			d.startIndex = ibFirst[r.ibRecord] + r.startIndex;
			d.baseVertex = (int32_t)vbFirst[r.vbRecord] + r.baseVertex;
			d.vbOffset = r.vbOffset;
			d.pass = r.pass;
			m.draws.push_back(d);
		}
		if(sys == cw::System::River && !m.draws.empty())
		{
			// the river own buffer ("river World", 0x80): the render writes a 4x4 identity into its first 64 bytes and
			// never the rest (water-system.md 8.1, 9.2 [C])
			m.ownConstants.assign(0x80, 0);
			const float one = 1.0f;
			for(int k = 0; k < 4; ++k)
				memcpy(m.ownConstants.data() + 4 * (5 * k), &one, 4);
		}
		if(bad)
		{
			meshErrors++;
			RNote("water scene: load seq %u system %u: chunks of different stride or index format: not drawn", m_imageLoad, (unsigned)sys);
			m = cw::Mesh();
		}
		const c5r::Status st = m_mod->setMesh(sys, m);   // an empty mesh clears the system
		if(!st.ok())
		{
			meshErrors++;
			RNote("water scene: load seq %u: setMesh(system %u, %zu vertex bytes, %zu draws) failed: %s", m_imageLoad, (unsigned)sys,
			      m.vb.size(), m.draws.size(), st.str().c_str());
		}
		else if(!m.draws.empty())
			m_systems |= 1u << (unsigned)sys;
	}
	// the coast waves (issue 3609)
	m_wavesOk = false;
	m_waveChunks = 0;
	m_waveLastMs = -1;
	m_lastWaveError.clear();
	{
		std::vector<cw::WaveChunk> chunks;
		c5r::Status ws = ParseWaveRecords(recs, m_waveObj, chunks, m_waves);
		if(ws.ok() && chunks.empty())
			ws = c5r::Status::fail(c5r::Err::NotFound, "waves: the Wave_System object has no chunks");
		if(ws.ok() && !m_mod->wavesAvailable())
			ws = c5r::Status::fail(c5r::Err::NotFound, "waves: " + m_mod->wavesWhy());
		if(ws.ok())
			ws = m_mod->setWaveChunks(chunks);
		if(ws.ok())
		{
			m_wavesOk = true;
			m_waveChunks = (uint32_t)chunks.size();
			m_systems |= 1u << (unsigned)cw::System::Wave;
		}
		else
		{
			if(ws.code != c5r::Err::NotFound)
				waveErrors++;
			m_waves.clear();
			RNote("water scene: load seq %u: no coast waves (%s)", m_imageLoad, ws.str().c_str());
		}
	}
	RNote("water scene: load seq %u: image used (meshes 0x%X, Water fields %s, clock %.4f / %.5f at %.3f / %.4f per s, water z %.3f, "
	      "waves %u chunks / %zu waves)",
	      m_imageLoad, m_systems, m_fields.valid ? "ok" : "MISSING", m_dc0, m_e4, m_dcRate, m_e4Rate, m_waterZ, m_waveChunks, m_waves.size());
}

void WaterScene::clockAt(double nowMs, float& dc, float& e4) const
{
	const double t = (m_opt.freeze || m_clockStartMs < 0) ? 0.0 : std::max(0.0, (nowMs - m_clockStartMs) / 1000.0);
	dc = (float)(m_dc0 + m_dcRate * t);
	e4 = (float)(m_e4 + m_e4Rate * t);
}

bool WaterScene::decide(const VPShellCamera& cam, double nowMs)
{
	m_lastNowMs = nowMs;
	const uint32_t load = cam.gridValid == 1 ? cam.gridSeq : 0;
	int w = WW_MODULE;
	if(m_opt.mode == WaterSceneOptions::kOff)
		w = WW_OFF;
	else if(m_opt.mode == WaterSceneOptions::kPlaceholder)
		w = WW_PLACEHOLDER;
	else if(!m_modOk)
		w = WW_NO_MODULE;
	else if(!m_haveField || (load && m_fieldLoad != load))
		w = WW_NO_FIELD;
	else if(!m_haveImage)
		w = WW_NO_TAP;
	else if(load && m_imageLoad != load)
		w = WW_OTHER_LOAD;
	else if(!m_fields.valid)
		w = WW_NO_FIELDS;
	m_why = w;
	if(w != m_loggedWhy || load != m_loggedLoad)
	{
		// once per load and reason: the placeholder's reason, or that the module draws
		if(w == WW_MODULE)
			RNote("water scene: load seq %u: the water module draws (meshes 0x%X: rivers %s, waves %s; the ocean from the field)", load,
			      m_systems, (m_systems & (1u << VPWSYS_RIVER)) ? "tapped" : "none", (m_systems & (1u << VPWSYS_WAVE)) ? "tapped" : "none");
		else if(w != WW_OFF)
		{
			const WaterTapReader* r = m_reader.get();
			RNote("water scene: load seq %u: placeholder plane (%s%s%s; tap: accepted %lld, refused %lld, last result %d%s%s)", load,
			      WaterWhyName(w), w == WW_NO_MODULE ? ": " : "", w == WW_NO_MODULE ? m_modWhy.c_str() : "", r ? (long long)r->accepted : 0LL,
			      r ? (long long)r->refused : 0LL, r ? r->lastResult() : 0, r && !r->lastWhy().empty() ? ": " : "",
			      r ? r->lastWhy().c_str() : "");
		}
		m_loggedWhy = w;
		m_loggedLoad = load;
	}
	if(w == WW_MODULE && m_clockStartMs < 0)
		m_clockStartMs = nowMs;
	if(placeholder())
	{
		placeholderFrames++;
		m_placeholderSec++;
	}
	return w == WW_MODULE;
}

// issue 3704 (epic 0167): the overlay of the next draw, set by the terrain scene (srv_render_scene.h WaterSceneOverlay)
namespace {
struct WaterOverlayInput
{
	const WaterScene* scene = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;   // not owned: the overlay reader's, valid until its next acquire()
	float proj[3][16] = {};
};
WaterOverlayInput g_waterOverlay;
}  // namespace

void WaterSceneOverlay(const WaterScene* w, ID3D11ShaderResourceView* srv, const float (*proj)[16])
{
	g_waterOverlay.scene = w;
	g_waterOverlay.srv = proj ? srv : nullptr;
	if(proj)
		memcpy(g_waterOverlay.proj, proj, sizeof(g_waterOverlay.proj));
}

// issue 4308 (epic 0169): the shadow of the next draw (t13, 0x220, ShadowMatrix0 per view) and the ice's place in it, set
// by the terrain scene (srv_render_scene.h WaterSceneShadow, WaterSceneAfterOcean)
namespace {
struct WaterShadowInput
{
	const WaterScene* scene = nullptr;
	bool set = false;
	ID3D11ShaderResourceView* srv = nullptr;   // not owned: the shadow reader's, valid until its next acquire()
	float strength = 0;
	float m[3][16] = {};
};
WaterShadowInput g_waterShadow;
struct WaterAfterOcean
{
	const WaterScene* scene = nullptr;
	void (*fn)(void*, ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*) = nullptr;
	void* user = nullptr;
};
WaterAfterOcean g_waterAfterOcean;
}  // namespace

void WaterSceneShadow(const WaterScene* w, ID3D11ShaderResourceView* srv, float strength, const float (*m)[16])
{
	g_waterShadow.scene = w;
	g_waterShadow.set = m != nullptr;
	g_waterShadow.srv = m ? srv : nullptr;
	g_waterShadow.strength = strength;
	if(m)
		memcpy(g_waterShadow.m, m, sizeof(g_waterShadow.m));
}

void WaterSceneAfterOcean(const WaterScene* w, void (*fn)(void*, ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*),
                          void* user)
{
	g_waterAfterOcean.scene = fn ? w : nullptr;
	g_waterAfterOcean.fn = fn;
	g_waterAfterOcean.user = user;
}

// issue 0382: what draws into the reflection target (srv_water_scene.h WaterSceneReflection) and render_ice_reflection
namespace {
struct WaterReflectionHook
{
	const WaterScene* scene = nullptr;
	WaterReflectionFn fn = nullptr;
	void* user = nullptr;
};
WaterReflectionHook g_waterReflection;
WaterReflectionHook g_waterReflectionModels;   // epic 0375: the units' reflection pass
bool g_reflectIce = true;
}  // namespace

void WaterSceneReflection(const WaterScene* w, WaterReflectionFn fn, void* user)
{
	g_waterReflection.scene = fn ? w : nullptr;
	g_waterReflection.fn = fn;
	g_waterReflection.user = user;
}

void WaterSceneReflectionModels(const WaterScene* w, WaterReflectionFn fn, void* user)
{
	g_waterReflectionModels.scene = fn ? w : nullptr;
	g_waterReflectionModels.fn = fn;
	g_waterReflectionModels.user = user;
}

void WaterSceneSetReflectIce(bool on) { g_reflectIce = on; }
bool WaterSceneReflectIce() { return g_reflectIce; }

// issue 3954 (epic 0168): the fog's mask of the next draw as ocean and river t10, set by the terrain scene (declared in
// srv_fow_scene.h, which this file does not include: the water tests build it without the feed's include path)
void WaterSceneFow(const WaterScene* w, ID3D11ShaderResourceView* mask, const float (*proj)[16]);
ID3D11ShaderResourceView* WaterSceneFowMask(const WaterScene* w);
namespace {
struct WaterFowInput
{
	const WaterScene* scene = nullptr;
	ID3D11ShaderResourceView* mask = nullptr;   // not owned: the fog scene's, valid until its next device release
	bool haveProj = false;
	float proj[3][16] = {};                     // the copy matrices that place the mask (as the overlay's)
};
WaterFowInput g_waterFow;
}  // namespace

void WaterSceneFow(const WaterScene* w, ID3D11ShaderResourceView* mask, const float (*proj)[16])
{
	g_waterFow.scene = w;
	g_waterFow.mask = mask;
	g_waterFow.haveProj = mask && proj;
	if(g_waterFow.haveProj)
		memcpy(g_waterFow.proj, proj, sizeof(g_waterFow.proj));
}

ID3D11ShaderResourceView* WaterSceneFowMask(const WaterScene* w) { return g_waterFow.scene == w ? g_waterFow.mask : nullptr; }

c5r::Status WaterScene::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                             const c5r::terrain::Camera& cam, cw::FrameStats* st)
{
	if(m_why != WW_MODULE || !m_modOk)
		return c5r::Status::fail(c5r::Err::NotFound, std::string("water not drawn: ") + WaterWhyName(m_why));
	if(!Finite(cam.view, 16) || !Finite(cam.proj, 16) || !Finite(cam.camWorld, 16) || !cam.width || !cam.height)
	{
		badCamera++;
		if(badCamera <= 3)
			RNote("water scene: camera not finite or of size 0 (%ux%u): water not drawn (%lld so far)", cam.width, cam.height,
			      (long long)badCamera);
		return c5r::Status::fail(c5r::Err::BadLayout, "camera not finite");
	}
	cw::FrameInputs in;
	in.cam = cam;
	in.fields = m_fields;
	float dc = 0, e4 = 0;
	clockAt(m_lastNowMs, dc, e4);
	memcpy(in.fields.raw + (kClockA - kFieldsFrom), &dc, 4);
	memcpy(in.fields.raw + (kClockB - kFieldsFrom), &e4, 4);
	in.clock.seconds = (m_opt.freeze || m_clockStartMs < 0) ? 0.0 : (m_lastNowMs - m_clockStartMs) / 1000.0;
	in.clock.fromExe = false;
	in.waterZ = -1.0f;   // Water+0xC8 as tapped (= the image's water z / (0.2 * 255))
	// The DistanceFog globals and the shadow blend: the module's defaults are stock's, and fog.w (0xD90FAC, the zoom
	// fraction) defaults to "from the camera's height" (issue 3609, water-system.md 10.7). Nothing to set here.
	// issue 3704: the overlay as terrain t1 (t9 here), each view with its EXE view's OverlayProj; none = the module's own
	// transparent input and identity (the picture before 3704)
	const bool ovl = g_waterOverlay.scene == this && g_waterOverlay.srv;
	m_mod->setTexture(cw::Slot::Overlay, ovl ? g_waterOverlay.srv : nullptr);
	if(ovl)
	{
		memcpy(in.overlayProj, g_waterOverlay.proj[1], sizeof(in.overlayProj));
		memcpy(in.overlayProjWrap[0], g_waterOverlay.proj[0], sizeof(in.overlayProjWrap[0]));
		memcpy(in.overlayProjWrap[1], g_waterOverlay.proj[2], sizeof(in.overlayProjWrap[1]));
		in.haveOverlayProjWrap = true;
	}
	// issue 3954: the fog's mask as t10 of the ocean and the rivers (the module shares t10 with the river draw); none =
	// the module's own constant input (the picture before 3954)
	m_mod->setTexture(cw::Slot::FowMask, WaterSceneFowMask(this));
	if(!ovl && g_waterFow.scene == this && g_waterFow.haveProj)
	{
		// no overlay bound (the tap lost or stale): the fog's kept pair places t10 (t9 stays the module's transparent input)
		memcpy(in.overlayProj, g_waterFow.proj[1], sizeof(in.overlayProj));
		memcpy(in.overlayProjWrap[0], g_waterFow.proj[0], sizeof(in.overlayProjWrap[0]));
		memcpy(in.overlayProjWrap[1], g_waterFow.proj[2], sizeof(in.overlayProjWrap[1]));
		in.haveOverlayProjWrap = true;
	}
	// issue 4308: the models' shadow as t13 with its strength (0x220) and each view's ShadowMatrix0; none = the module's own
	// zero t13, blend 1 and identity (the picture before 4308)
	const bool sh = g_waterShadow.scene == this && g_waterShadow.set;
	m_mod->setTexture(cw::Slot::Shadow, sh ? g_waterShadow.srv : nullptr);
	if(sh)
	{
		in.shadowBlend = g_waterShadow.strength;
		memcpy(in.shadowMatrix0, g_waterShadow.m[1], sizeof(in.shadowMatrix0));
		memcpy(in.shadowMatrix0Wrap[0], g_waterShadow.m[0], sizeof(in.shadowMatrix0Wrap[0]));
		memcpy(in.shadowMatrix0Wrap[1], g_waterShadow.m[2], sizeof(in.shadowMatrix0Wrap[1]));
		in.haveShadowMatrix0Wrap = true;
	}
	// issue 0382: the reflection pass (0x5A0F80 [C]): the target cleared to (0,0,0,0) and depth 1, the ice drawn into it by the
	// hook with the reflection camera of view 0, and WaterFrame 0x160 = that camera's view-projection (water-system.md 13.6)
	if(m_reflRtv && m_reflDsv)
		drawReflection(ctx, cam, in);
	cw::Options o;
	o.clearDepth = false;   // into the terrain's depth (water-system.md 13)
	o.oceanPass = m_oceanPass;
	o.waves = false;        // the module's own Wave mesh is not used: the wave renderer draws them below (issue 3609)
	cw::FrameStats local;
	cw::FrameStats* fs = st ? st : &local;
	const int64_t q0 = Qpc();
	const c5r::Status ds = m_mod->draw(ctx, rtv, dsv, in, o, fs);
	m_cpuSumSec += QpcToMs(Qpc() - q0);
	if(!ds.ok())
	{
		drawErrors++;
		const std::string e = ds.str();
		if(e != m_lastDrawError || drawErrors <= 3)
			RNote("water scene: draw failed: %s", e.c_str());
		m_lastDrawError = e;
		return ds;
	}
	m_lastDrawError.clear();
	// issue 4308: the polar ice after the ocean and the rivers, before the coast waves (polar-ice.md 2)
	if(g_waterAfterOcean.scene == this && g_waterAfterOcean.fn)
		g_waterAfterOcean.fn(g_waterAfterOcean.user, ctx, rtv, dsv);
	// the coast waves after the ocean and the rivers (water-system.md 12.1): one update per drawn frame with the server's
	// clock, then every chunk per view, additive, depth tested against the terrain's depth without writes
	if(m_wavesOk)
	{
		const int64_t q1 = Qpc();
		double dtMs = 0;
		if(!m_opt.freeze && m_waveLastMs >= 0)
			dtMs = std::min(std::max(0.0, m_lastNowMs - m_waveLastMs), kWaveMaxDtMs);
		m_waveLastMs = m_lastNowMs;
		cw::WaveStats ws;
		c5r::Status s2 = cw::updateWaves(m_waveObj, (float)(dtMs / 1000.0), m_waves);
		if(s2.ok())
			s2 = m_mod->drawWaves(ctx, rtv, dsv, m_waveObj, m_waterZ + 1.0f, m_waves, &ws);
		if(s2.ok())
		{
			fs->waveDraws = ws.chunksDrawn;
			waveDrawsTotal++;
			m_lastWaveError.clear();
		}
		else
		{
			waveErrors++;
			const std::string e = s2.str();
			if(e != m_lastWaveError)
				RNote("water scene: waves not drawn: %s", e.c_str());
			m_lastWaveError = e;
		}
		m_cpuSumSec += QpcToMs(Qpc() - q1);
	}
	m_lastStats = *fs;
	drawsTotal++;
	m_drawsSec++;
	return ds;
}

void WaterScene::drawReflection(ID3D11DeviceContext* ctx, const c5r::terrain::Camera& cam, cw::FrameInputs& in)
{
	const float clear[4] = {0, 0, 0, 0};
	ctx->ClearRenderTargetView(m_reflRtv, clear);
	ctx->ClearDepthStencilView(m_reflDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	// view 0's view-projection, summed as D3DXMatrixMultiply (the module's ViewProj, WaterFrame 0x000)
	float vp[16];
	for(int i = 0; i < 4; ++i)
		for(int j = 0; j < 4; ++j)
		{
			float s = cam.view[i * 4 + 0] * cam.proj[0 * 4 + j];
			s += cam.view[i * 4 + 1] * cam.proj[1 * 4 + j];
			s += cam.view[i * 4 + 2] * cam.proj[2 * 4 + j];
			s += cam.view[i * 4 + 3] * cam.proj[3 * 4 + j];
			vp[i * 4 + j] = s;
		}
	const c5r::Status rs = cw::reflectionCamera(vp, m_refl);
	if(!rs.ok())
	{
		// a camera without a ground footprint (looking at the horizon): stock's matrix is then not defined either; t4 stays
		// cleared and 0x160 the identity, so the water reads (0,0,0,0) as before 0382
		reflBadCamera++;
		const std::string e = rs.str();
		if(e != m_lastReflError)
			RNote("water scene: no reflection camera for this view (%s): the reflection stays empty (%lld so far)", e.c_str(),
			      (long long)reflBadCamera);
		m_lastReflError = e;
		return;
	}
	memcpy(in.shadowMatrix1, m_refl.viewProj, sizeof(in.shadowMatrix1));
	const bool models = g_waterReflectionModels.scene == this && g_waterReflectionModels.fn;
	const bool ice = g_reflectIce && g_waterReflection.scene == this && g_waterReflection.fn;
	if(!models && !ice)
		return;
	c5r::terrain::Camera rc;
	memcpy(rc.view, m_refl.view, sizeof(rc.view));
	memcpy(rc.proj, m_refl.proj, sizeof(rc.proj));
	rc.width = rc.height = kReflectionSize;
	// the scene camera's z (inverse(view) row 3, the view being a rotation and a translation)
	const float* v = cam.view;
	const float camZ = (float)-((double)v[12] * v[8] + (double)v[13] * v[9] + (double)v[14] * v[10]);
	// epic 0375 (merge with 0382): the models first (stock's pass draws the model systems, view bits 0x40/0x20/0x2, before
	// the ice behind 0x1: water-system.md 13.6), not gated by render_ice_reflection; the units count their own draws
	uint32_t md = 0;
	if(models)
	{
		const c5r::Status ms = g_waterReflectionModels.fn(g_waterReflectionModels.user, ctx, m_reflRtv, m_reflDsv, rc, camZ, &md);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		if(!ms.ok())
		{
			reflErrors++;
			const std::string e = ms.str();
			if(e != m_lastReflError)
				RNote("LOUD: water scene: the reflected models were not drawn: %s (frame drawn with an empty reflection; %lld so far)",
				      e.c_str(), (long long)reflErrors);
			m_lastReflError = e;
			ctx->ClearRenderTargetView(m_reflRtv, clear);   // nothing half-drawn is read
			return;
		}
	}
	uint32_t draws = 0;
	if(!ice)
	{
		// issue 4464: the models alone (render_ice_reflection=0) drew this frame's reflection: counted as a reflected frame
		// and the LOUD text reset, as after the ice (was: return with neither, so a repeated failure after a recovery was
		// not LOUD again and w_refl_frames stayed 0)
		m_lastReflError.clear();
		m_reflDraws = md;
		reflFrames++;
		m_reflSec++;
		return;
	}
	const c5r::Status hs = g_waterReflection.fn(g_waterReflection.user, ctx, m_reflRtv, m_reflDsv, rc, camZ, &draws);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);   // t4 is read next: never bound as a target and a resource at once
	if(!hs.ok())
	{
		reflErrors++;
		const std::string e = hs.str();
		if(e != m_lastReflError)
			RNote("LOUD: water scene: the reflected ice was not drawn: %s (frame drawn with an empty reflection; %lld so far)", e.c_str(),
			      (long long)reflErrors);
		m_lastReflError = e;
		ctx->ClearRenderTargetView(m_reflRtv, clear);   // nothing half-drawn is read
		return;
	}
	m_lastReflError.clear();
	m_reflDraws = md + draws;   // issue 4464: the models' draws (the units report 0: they count their own) and the ice's
	reflFrames++;
	m_reflSec++;
}

void WaterScene::secondLine(char* out, size_t n)
{
	const WaterTapReader* r = m_reader.get();
	_snprintf_s(out, n, _TRUNCATE,
	            "water 1s: w_mode=%d w_why=%s w_draws=%d w_placeholder=%d w_cpu_ms=%.3f w_load=%u w_meshes=0x%X w_tap_ok=%lld w_tap_refused=%lld w_tap_result=%d "
	            "w_ocean_draws=%u w_ocean_quads=%u w_pages_missing=%u w_river_draws=%u w_ocean_pass=%d w_river_pass=%d w_gpu_ms=%.3f "
	            "w_wave_chunks=%u w_wave_draws=%u w_wave_errors=%lld",
	            m_opt.mode, WaterWhyName(m_why), m_drawsSec, m_placeholderSec, m_drawsSec ? m_cpuSumSec / m_drawsSec : -1.0,
	            m_haveImage ? m_imageLoad : 0, m_systems, r ? (long long)r->accepted : 0LL, r ? (long long)r->refused : 0LL,
	            r ? r->lastResult() : 0, m_lastStats.oceanDraws, m_lastStats.oceanQuads, m_lastStats.pagesMissing, m_lastStats.riverDraws,
	            m_lastStats.oceanPassUsed, m_lastStats.riverPassUsed, m_lastStats.gpuMs, m_wavesOk ? m_waveChunks : 0u, m_lastStats.waveDraws,
	            (long long)waveErrors);
	// issue 0382: a line of its own: the joined per-second line is cut at the log's 1,536-byte body (srv_render_common.cpp
	// Emit) before the water part ends [L, 2026-11-16: it ended at "w_oce"]
	char rl[256];
	reflLine(rl, sizeof(rl));
	RLog("%s", rl);
	m_drawsSec = m_placeholderSec = 0;
	m_reflSec = 0;
	m_cpuSumSec = 0;
}

void WaterScene::reflLine(char* out, size_t n) const
{
	_snprintf_s(out, n, _TRUNCATE, "water refl 1s: w_refl_ice=%d w_refl_frames=%d w_refl_draws=%u w_refl_errors=%lld w_refl_badcam=%lld",
	            g_reflectIce ? 1 : 0, m_reflSec, m_reflSec ? m_reflDraws : 0u, (long long)reflErrors, (long long)reflBadCamera);
}

void WaterScene::totalsLine(char* out, size_t n)
{
	const WaterTapReader* r = m_reader.get();
	_snprintf_s(out, n, _TRUNCATE,
	            "water totals: draws %lld draw errors %lld placeholder frames %lld bad camera %lld images used %lld mesh errors %lld "
	            "tap accepted %lld refused %lld requests %lld wave frames %lld wave errors %lld reflected-ice frames %lld errors %lld no reflection camera %lld",
	            (long long)drawsTotal, (long long)drawErrors, (long long)placeholderFrames, (long long)badCamera, (long long)imagesUsed,
	            (long long)meshErrors,r ? (long long)r->accepted : 0LL, r ? (long long)r->refused : 0LL, r ? (long long)r->requests : 0LL, (long long)waveDrawsTotal,
	            (long long)waveErrors, (long long)reflFrames, (long long)reflErrors, (long long)reflBadCamera);
}

// ---- issue 3903 (epic 0301): the file source and the clock passed in (srv_water_scene.h) ------------------------------
bool WaterScene::loadImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam, uint32_t gridSeq,
                           uint32_t samplesX, uint32_t samplesY)
{
	if(!m_reader)
		m_reader.reset(new WaterTapReader(nullptr));   // no section: poll() does nothing, loadImage() is the only entry
	if(!m_reader->loadImage(block, image, imageBytes, haveCam, gridSeq, samplesX, samplesY))
		return false;
	useImage();
	return true;
}

WaterClockState WaterScene::clockState() const
{
	WaterClockState s;
	s.clockStartMs = m_clockStartMs;
	s.waveLastMs = m_waveLastMs;
	s.lastNowMs = m_lastNowMs;
	s.waves = m_waves;
	return s;
}

bool WaterScene::setClockState(const WaterClockState& s)
{
	if(!std::isfinite(s.clockStartMs) || !std::isfinite(s.waveLastMs) || !std::isfinite(s.lastNowMs))
	{
		RNote("water scene: setClockState refused: a time is not finite (%g, %g, %g)", s.clockStartMs, s.waveLastMs, s.lastNowMs);
		return false;
	}
	if(s.waves.size() != m_waves.size())
	{
		RNote("water scene: setClockState refused: %zu waves given, the image in use has %zu", s.waves.size(), m_waves.size());
		return false;
	}
	m_clockStartMs = s.clockStartMs;
	m_waveLastMs = s.waveLastMs;
	m_lastNowMs = s.lastNowMs;
	m_waves = s.waves;
	return true;
}

}  // namespace srvrender
