// srv_scene_sources.cpp - the live, recording and file sources of the offline scene harness (epic 0301, task 3903).
// Contract: srv_scene_sources.h; research/06-tooling-and-workflows/offline-scene-harness.md sections 4 and 6.
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
#include "srv_scene_sources.h"
#include "srv_scene_bundle.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <stdarg.h>
#include <string.h>

namespace srvrender {

namespace {

std::string Fmt(const char* fmt, ...)
{
	char b[512];
	va_list a;
	va_start(a, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
	va_end(a);
	return b;
}

void SetWhy(std::string* why, const std::string& s)
{
	if(why)
		*why = s;
}

// ---- the feed ---------------------------------------------------------------------------------------------------------
struct LiveFeedImpl : FeedSource
{
	bool drain(SrvFeed::Batch& out) override { return SrvFeed::Drain(out); }
	void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h, uint32_t) override
	{
		SrvFeed::CopyPlots(out, gen, w, h);
	}
};

struct RecordingFeed : FeedSource
{
	RecordingFeed(FeedSource& live, FeedRecordSink& rec) : m_live(live), m_rec(rec) {}
	bool drain(SrvFeed::Batch& out) override
	{
		const bool got = m_live.drain(out);
		if(got)
			m_rec.onDrain(out);
		return got;
	}
	void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h, uint32_t caller) override
	{
		m_live.copyPlots(out, gen, w, h, caller);
		m_rec.onPlots(caller, out, gen, w, h);
	}
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& gen) override   // issue 4457
	{
		m_live.copyUnits(out, gen);
		m_rec.onUnits(out, gen);
	}
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override
	{
		m_live.copyPlayers(out);
		m_rec.onPlayers(out);
	}
	FeedSource& m_live;
	FeedRecordSink& m_rec;
};

const char* CallName(uint32_t type) { return type == 1 ? "DRAIN" : type == 2 ? "PLOTS" : "?"; }

// ticket 0493: a replay out of order is most often a bundle recorded before a change to the render thread's feed reads
// that did not raise the bundle's format_version (the rule at kFormatVersion); the hint says where to look.
const char* const kOrderHint = " (the recorded reads differ from this program's: if the server's feed reads changed since the "
                               "recording, that change must raise the bundle format_version and the library be recorded again; "
                               "offline-scene-harness.md section 24)";

struct FileFeed : FeedSource, FileFeedControl
{
	std::vector<FeedCall> calls;
	size_t next = 0;
	uint64_t limit = UINT64_MAX;
	bool bad = false;
	std::string badWhy;

	bool drain(SrvFeed::Batch& out) override
	{
		out = SrvFeed::Batch();
		if(bad || next >= calls.size() || calls[next].type != 1 || calls[next].loopIter > limit)
			return false;
		drainMs = calls[next].drainMs;
		drainIter = calls[next].loopIter;
		unitQ = calls[next].unitCopies;
		playerQ = calls[next].playerCopies;
		unitAt = playerAt = 0;
		out = calls[next++].batch;
		return true;
	}
	// issue 4457: the unit scene's copies come from the DRAIN the last drain() yielded, in order
	bool noCopy(const char* call)
	{
		if(bad)
			return true;
		bad = true;
		badWhy = Fmt("out of order: %s called, no recorded copy left after the DRAIN of loop iteration %llu", call,
		             (unsigned long long)drainIter) + kOrderHint;
		RNote("LOUD: file feed: %s", badWhy.c_str());
		return true;
	}
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& gen) override
	{
		out.clear();
		gen = 0;
		if(bad || unitAt >= unitQ.size())
		{
			noCopy("copyUnits");
			return;
		}
		out = unitQ[unitAt].units;
		gen = unitQ[unitAt++].gen;
	}
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override
	{
		out.clear();
		if(bad || playerAt >= playerQ.size())
		{
			noCopy("copyPlayers");
			return;
		}
		out = playerQ[playerAt++];
	}
	size_t unitCopiesLeft() const override { return (unitQ.size() - unitAt) + (playerQ.size() - playerAt); }
	std::vector<FeedUnitCopy> unitQ;
	std::vector<std::vector<SrvFeed::PlayerRecord>> playerQ;
	size_t unitAt = 0, playerAt = 0;
	uint64_t drainIter = 0;
	double lastDrainMs() const override { return drainMs; }
	double drainMs = -1;
	void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h, uint32_t caller) override
	{
		out.clear();
		gen = w = h = 0;
		if(bad)
			return;
		if(next >= calls.size() || calls[next].type != 2 || calls[next].caller != caller)
		{
			bad = true;
			if(next >= calls.size())
				badWhy = Fmt("out of order: copyPlots (caller %u) after the last of %zu records", caller, calls.size());
			else
				badWhy = Fmt("out of order: copyPlots (caller %u) called, the next record (%zu of %zu, loop iteration %llu) is %s%s",
				             caller, next, calls.size(), (unsigned long long)calls[next].loopIter, CallName(calls[next].type),
				             calls[next].type == 2 ? Fmt(" of caller %u", calls[next].caller).c_str() : "");
			badWhy += kOrderHint;
			RNote("LOUD: file feed: %s", badWhy.c_str());
			return;
		}
		const FeedCall& c = calls[next++];
		out = c.table;
		gen = c.gen;
		w = c.width;
		h = c.height;
	}
	void setLoopIter(uint64_t it) override { limit = it; }
	bool failed() const override { return bad; }
	std::string why() const override { return badWhy; }
	size_t remaining() const override { return calls.size() - next; }
};

// ---- the overlay ------------------------------------------------------------------------------------------------------
bool Rgba8Family(uint32_t f) { return f >= 27 && f <= 32; }   // DXGI_FORMAT_R8G8B8A8_TYPELESS .. _SINT

struct RecordingOverlay : OverlaySource
{
	struct Entry
	{
		ID3D11Texture2D* staging = nullptr;
		bool busy = false;
		int64_t copiedAt = 0;   // acquire() call that queued the copy
		OverlayFrame pair;
		D3D11_TEXTURE2D_DESC desc = {};
	};
	RecordingOverlay(std::unique_ptr<OverlaySource> live, OverlayRecordSink& rec) : m_live(std::move(live)), m_rec(rec) {}
	~RecordingOverlay() override
	{
		for(Entry& e : m_ring)
			if(e.busy)
				mapOut(e);
		for(Entry& e : m_ring)
			SafeRelease(e.staging);
		SafeRelease(m_ctx);
	}
	bool acquire(OverlayFrame& out) override
	{
		m_calls++;
		// the copies queued two calls ago are done on the GPU by now (or nearly): map them before anything new
		for(Entry& e : m_ring)
			if(e.busy && m_calls - e.copiedAt >= 2)
				mapOut(e);
		const bool got = m_live->acquire(out);
		if(got && out.srv && (!m_have || out.generation != m_last.generation || out.srv != m_last.srv || out.slot != m_last.slot))
		{
			m_rec.onOverlayPair(out);
			queueCopy(out);
		}
		if(got && out.srv)
		{
			m_last = out;
			m_have = true;
		}
		else
			m_have = false;
		return got;
	}
	OverlayTapReader::State state() const override { return m_live->state(); }
	uint32_t failReason() const override { return m_live->failReason(); }
	std::string why() const override { return m_live->why(); }
	int64_t staleCount() const override { return m_live->staleCount(); }
	int64_t staleLongestPresents() const override { return m_live->staleLongestPresents(); }
	int64_t refusedCount() const override { return m_live->refusedCount(); }

private:
	void queueCopy(const OverlayFrame& f)
	{
		Entry& e = m_ring[m_nextEntry];
		m_nextEntry = (m_nextEntry + 1) % 3;
		if(e.busy)
			mapOut(e);   // a fourth pair within two calls: this one waits for the GPU
		ID3D11Resource* res = nullptr;
		f.srv->GetResource(&res);
		ID3D11Texture2D* tex = nullptr;
		if(!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) || !tex)
		{
			SafeRelease(res);
			m_rec.onOverlayReadbackError(f, "the pair's view is not over a 2D texture");
			return;
		}
		res->Release();
		D3D11_TEXTURE2D_DESC d;
		tex->GetDesc(&d);
		if(!m_ctx)
		{
			ID3D11Device* dev = nullptr;
			tex->GetDevice(&dev);
			if(dev)
			{
				dev->GetImmediateContext(&m_ctx);
				dev->Release();
			}
		}
		if(e.staging && (e.desc.Width != d.Width || e.desc.Height != d.Height || e.desc.MipLevels != d.MipLevels ||
		                 e.desc.Format != d.Format || e.desc.ArraySize != d.ArraySize))
			SafeRelease(e.staging);
		if(!e.staging)
		{
			D3D11_TEXTURE2D_DESC sd = d;
			sd.Usage = D3D11_USAGE_STAGING;
			sd.BindFlags = 0;
			sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			sd.MiscFlags = 0;
			ID3D11Device* dev = nullptr;
			tex->GetDevice(&dev);
			const HRESULT hr = dev ? dev->CreateTexture2D(&sd, nullptr, &e.staging) : E_FAIL;
			SafeRelease(dev);
			if(FAILED(hr) || !e.staging)
			{
				tex->Release();
				e.staging = nullptr;
				m_rec.onOverlayReadbackError(f, Fmt("staging texture %ux%u x %u mips format %u: 0x%08X", d.Width, d.Height, d.MipLevels,
				                                    (unsigned)d.Format, (unsigned)hr));
				return;
			}
			e.desc = d;
		}
		if(!m_ctx)
		{
			tex->Release();
			m_rec.onOverlayReadbackError(f, "no immediate context");
			return;
		}
		m_ctx->CopyResource(e.staging, tex);
		tex->Release();
		e.busy = true;
		e.copiedAt = m_calls;
		e.pair = f;
		e.pair.srv = nullptr;   // the view may be gone when the texels are handed out
	}
	void mapOut(Entry& e)
	{
		e.busy = false;
		const D3D11_TEXTURE2D_DESC& d = e.desc;
		OverlayTexels t;
		t.width = d.Width;
		t.height = d.Height;
		t.mips = d.MipLevels;
		t.dxgiFormat = (uint32_t)d.Format;
		if(!Rgba8Family(t.dxgiFormat) || d.ArraySize != 1)
		{
			m_rec.onOverlayReadbackError(e.pair, Fmt("format %u x %u slices: not 4 bytes a texel", t.dxgiFormat, d.ArraySize));
			return;
		}
		t.bytes.resize((size_t)OverlayTightBytes(t.width, t.height, t.mips));
		size_t off = 0;
		for(uint32_t m = 0; m < t.mips; ++m)
		{
			const uint32_t mw = std::max(1u, t.width >> m), mh = std::max(1u, t.height >> m);
			D3D11_MAPPED_SUBRESOURCE map;
			const HRESULT hr = m_ctx->Map(e.staging, m, D3D11_MAP_READ, 0, &map);
			if(FAILED(hr))
			{
				m_rec.onOverlayReadbackError(e.pair, Fmt("Map of mip %u: 0x%08X", m, (unsigned)hr));
				return;
			}
			for(uint32_t y = 0; y < mh; ++y)
				memcpy(&t.bytes[off + (size_t)y * mw * 4], (const uint8_t*)map.pData + (size_t)y * map.RowPitch, (size_t)mw * 4);
			m_ctx->Unmap(e.staging, m);
			off += (size_t)mw * mh * 4;
		}
		m_rec.onOverlayTexels(e.pair, t);
	}

	std::unique_ptr<OverlaySource> m_live;
	OverlayRecordSink& m_rec;
	ID3D11DeviceContext* m_ctx = nullptr;
	Entry m_ring[3];
	int m_nextEntry = 0;
	int64_t m_calls = 0;
	OverlayFrame m_last;
	bool m_have = false;
};

struct FileOverlay : OverlaySource, FileOverlayControl
{
	std::vector<FileOverlayFrame> frames;
	std::map<std::string, ID3D11ShaderResourceView*> srvs;
	uint32_t k = 0;
	OverlayTapReader::State st = OverlayTapReader::State::Waiting;
	~FileOverlay() override
	{
		for(auto& kv : srvs)
			SafeRelease(kv.second);
	}
	bool acquire(OverlayFrame& out) override
	{
		out = OverlayFrame();
		if(k >= frames.size() || !frames[k].bound)
		{
			st = OverlayTapReader::State::Waiting;
			return false;
		}
		const FileOverlayFrame& f = frames[k];
		out.srv = srvs[f.sha256];
		out.numViews = f.numViews;
		memcpy(out.overlayProj, f.overlayProj, sizeof(out.overlayProj));
		out.presentCount = f.presentCount;
		out.generation = f.generation;
		out.slot = 0;
		st = OverlayTapReader::State::Active;
		return true;
	}
	OverlayTapReader::State state() const override { return st; }
	uint32_t failReason() const override { return 0; }
	std::string why() const override { return ""; }
	void seekFrame(uint32_t i) override { k = i; }
	uint32_t frameCount() const override { return (uint32_t)frames.size(); }
};

}  // namespace

// ---- the feed -----------------------------------------------------------------------------------------------------------
FeedSource& LiveFeed()
{
	static LiveFeedImpl s_live;
	return s_live;
}

std::unique_ptr<FeedSource> MakeRecordingFeed(FeedSource& live, FeedRecordSink& rec)
{
	return std::unique_ptr<FeedSource>(new RecordingFeed(live, rec));
}

std::unique_ptr<FeedSource> MakeFileFeed(std::vector<FeedCall> calls, FileFeedControl** control, std::string* why)
{
	if(control)
		*control = nullptr;
	size_t plots = 0;
	for(size_t i = 0; i < calls.size(); ++i)
	{
		const FeedCall& c = calls[i];
		if(c.type != 1 && c.type != 2)
		{
			SetWhy(why, Fmt("record %zu: unknown type %u", i, c.type));
			return nullptr;
		}
		if(i && c.loopIter < calls[i - 1].loopIter)
		{
			SetWhy(why, Fmt("record %zu: out of order: loop iteration %llu after %llu", i, (unsigned long long)c.loopIter,
			                (unsigned long long)calls[i - 1].loopIter));
			return nullptr;
		}
		if(c.type == 2)
		{
			plots++;
			if(c.caller < kPlotsBaseline || c.caller > kPlotsForest)   // kPlotsFog: issue 3911; kPlotsForest: ticket 0495
			{
				SetWhy(why, Fmt("record %zu: PLOTS of unknown caller %u", i, c.caller));
				return nullptr;
			}
			if(c.table.size() != (size_t)c.width * c.height)
			{
				SetWhy(why, Fmt("record %zu: PLOTS table of %zu records, %ux%u announced", i, c.table.size(), c.width, c.height));
				return nullptr;
			}
		}
	}
	if(!plots)
	{
		SetWhy(why, Fmt("no PLOTS record among %zu: the plot tables the render thread read are missing (contract 7)", calls.size()));
		return nullptr;
	}
	FileFeed* f = new FileFeed();
	f->calls = std::move(calls);
	if(control)
		*control = f;
	return std::unique_ptr<FeedSource>(f);
}

// ---- the overlay --------------------------------------------------------------------------------------------------------
uint64_t OverlayTightBytes(uint32_t width, uint32_t height, uint32_t mips)
{
	uint64_t n = 0;
	for(uint32_t m = 0; m < mips; ++m)
		n += (uint64_t)std::max(1u, width >> m) * std::max(1u, height >> m) * 4;
	return n;
}

std::unique_ptr<OverlaySource> MakeRecordingOverlaySource(std::unique_ptr<OverlaySource> live, OverlayRecordSink& rec)
{
	if(!live)
		return nullptr;
	return std::unique_ptr<OverlaySource>(new RecordingOverlay(std::move(live), rec));
}

std::unique_ptr<OverlaySource> MakeFileOverlaySource(ID3D11Device* dev, const std::vector<FileOverlayFrame>& frames,
                                                     const std::map<std::string, OverlayTexels>& textures,
                                                     FileOverlayControl** control, std::string* why)
{
	if(control)
		*control = nullptr;
	if(!dev)
	{
		SetWhy(why, "no device");
		return nullptr;
	}
	std::unique_ptr<FileOverlay> s(new FileOverlay());
	s->frames = frames;
	for(size_t i = 0; i < frames.size(); ++i)
	{
		const FileOverlayFrame& f = frames[i];
		if(!f.bound)
			continue;
		if(f.numViews < 1 || f.numViews > VPSHELL_OVERLAY_MAX_VIEWS)
		{
			SetWhy(why, Fmt("frame %zu: numViews %u outside 1..%u", i, f.numViews, (unsigned)VPSHELL_OVERLAY_MAX_VIEWS));
			return nullptr;
		}
		if(s->srvs.count(f.sha256))
			continue;
		auto it = textures.find(f.sha256);
		if(it == textures.end())
		{
			SetWhy(why, Fmt("frame %zu: its texture %.64s is not in the bundle", i, f.sha256.c_str()));
			return nullptr;
		}
		const OverlayTexels& t = it->second;
		// the live reader's checks (srv_overlay_tap.cpp): 2048 x 2048, 12 mips, the R8G8B8A8 family
		if(t.width != 2048 || t.height != 2048 || t.mips != 12 || !Rgba8Family(t.dxgiFormat))
		{
			SetWhy(why, Fmt("texture %.16s: %ux%u x %u mips format %u, expected 2048x2048 x 12 mips of the R8G8B8A8 family",
			                f.sha256.c_str(), t.width, t.height, t.mips, t.dxgiFormat));
			return nullptr;
		}
		if((uint64_t)t.bytes.size() != OverlayTightBytes(t.width, t.height, t.mips))
		{
			SetWhy(why, Fmt("texture %.16s: %zu bytes, the tight 12 mips are %llu", f.sha256.c_str(), t.bytes.size(),
			                (unsigned long long)OverlayTightBytes(t.width, t.height, t.mips)));
			return nullptr;
		}
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = t.width;
		d.Height = t.height;
		d.MipLevels = t.mips;
		d.ArraySize = 1;
		d.Format = (DXGI_FORMAT)t.dxgiFormat;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_IMMUTABLE;
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
		HRESULT hr = dev->CreateTexture2D(&d, sd, &tex);
		ID3D11ShaderResourceView* v = nullptr;
		if(SUCCEEDED(hr))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
			vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // the live reader's SRV (srv_overlay_tap.cpp; overlay-system.md 3)
			vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MostDetailedMip = 0;
			vd.Texture2D.MipLevels = t.mips;
			hr = dev->CreateShaderResourceView(tex, &vd, &v);
			tex->Release();
		}
		if(FAILED(hr) || !v)
		{
			SetWhy(why, Fmt("texture %.16s: the device refused it (0x%08X)", f.sha256.c_str(), (unsigned)hr));
			return nullptr;
		}
		s->srvs[f.sha256] = v;
	}
	if(control)
		*control = s.get();
	return std::unique_ptr<OverlaySource>(s.release());
}

// ---- the bundle forms (3902's library) ---------------------------------------------------------------------------------
namespace {
namespace sb = scenebundle;

bool ReadM(const std::wstring& dir, const sb::Manifest& m, const std::string& path, std::vector<uint8_t>& b, std::string* why)
{
	sb::CheckResult res;
	if(sb::ReadMember(dir, m, path, &b, &res).ok())
		return true;
	SetWhy(why, res.line());
	return false;
}

std::string Hex32(const uint8_t* p)
{
	static const char hx[] = "0123456789abcdef";
	std::string s(64, '0');
	for(int i = 0; i < 32; ++i)
	{
		s[2 * i] = hx[p[i] >> 4];
		s[2 * i + 1] = hx[p[i] & 15];
	}
	return s;
}
}  // namespace

std::unique_ptr<FeedSource> MakeFileFeed(const std::wstring& bundleDir, const sb::Manifest& m, FileFeedControl** control, std::string* why)
{
	if(control)
		*control = nullptr;
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, "feed.bin", b, why))
		return nullptr;
	std::vector<sb::FeedRecord> recs;
	sb::CheckResult res;
	if(!sb::ParseFeed(b, &recs, &res).ok())
	{
		SetWhy(why, res.line());
		return nullptr;
	}
	std::vector<FeedCall> calls(recs.size());
	for(size_t i = 0; i < recs.size(); ++i)
	{
		FeedCall& c = calls[i];
		sb::FeedRecord& r = recs[i];
		c.type = r.type;
		c.loopIter = r.loopIter;
		c.batch = std::move(r.batch);
		c.drainMs = r.drainMs;
		for(sb::UnitCopy& u : r.unitCopies)
		{
			FeedUnitCopy fc;
			fc.gen = u.tableGen;
			fc.units = std::move(u.units);
			c.unitCopies.push_back(std::move(fc));
		}
		c.playerCopies = std::move(r.playerCopies);
		c.caller = r.caller;
		c.gen = r.snapshotGen;
		c.width = r.width;
		c.height = r.height;
		c.table = std::move(r.plots);
	}
	return MakeFileFeed(std::move(calls), control, why);
}

std::unique_ptr<OverlaySource> MakeFileOverlaySource(ID3D11Device* dev, const std::wstring& bundleDir, const sb::Manifest& m,
                                                     FileOverlayControl** control, std::string* why)
{
	if(control)
		*control = nullptr;
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, "frames.bin", b, why))
		return nullptr;
	std::vector<sb::FrameRecord> fr;
	sb::CheckResult res;
	if(!sb::ParseFrames(b, &fr, &res).ok())
	{
		SetWhy(why, res.line());
		return nullptr;
	}
	std::vector<FileOverlayFrame> frames(fr.size());
	std::map<std::string, OverlayTexels> textures;
	for(size_t k = 0; k < fr.size(); ++k)
	{
		const sb::FrameRecord& r = fr[k];
		FileOverlayFrame& f = frames[k];
		f.bound = r.overlayBound != 0;
		f.generation = r.overlayGeneration;
		f.numViews = r.overlayNumViews;
		f.presentCount = r.overlayPresentCount;
		memcpy(f.overlayProj, r.overlayProj, sizeof(f.overlayProj));
		if(!f.bound)
			continue;
		f.sha256 = Hex32(r.overlaySha256);
		if(textures.count(f.sha256))
			continue;
		const std::string path = "overlay/" + f.sha256.substr(0, 16) + ".ovl";
		auto it = m.members.find(path);
		if(it == m.members.end() || it->second.sha256 != f.sha256)
		{
			SetWhy(why, Fmt("frame %lld (record %zu) binds texture %.16s: %s", (long long)r.frameId, k, f.sha256.c_str(),
			                it == m.members.end() ? "not in the bundle" : "the member's SHA-256 differs"));
			return nullptr;
		}
		std::vector<uint8_t> ob;
		if(!ReadM(bundleDir, m, path, ob, why))
			return nullptr;
		sb::OverlayFile of;
		if(!sb::ParseOverlay(ob, &of, &res).ok())
		{
			SetWhy(why, res.line());
			return nullptr;
		}
		OverlayTexels t;
		t.width = of.width;
		t.height = of.height;
		t.mips = of.mips;
		t.dxgiFormat = of.dxgiFormat;
		const uint64_t n = OverlayTightBytes(t.width, t.height, t.mips);
		if((uint64_t)of.offset[0] + n > ob.size())
		{
			SetWhy(why, Fmt("%s: the texels end past the file", path.c_str()));
			return nullptr;
		}
		t.bytes.assign(ob.begin() + of.offset[0], ob.begin() + (size_t)(of.offset[0] + n));
		textures[f.sha256] = std::move(t);
	}
	return MakeFileOverlaySource(dev, frames, textures, control, why);
}

bool LoadWaterImageMember(WaterScene& w, const std::wstring& bundleDir, const sb::Manifest& m, const std::string& path, std::string* why)
{
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, path, b, why))
		return false;
	sb::WaterTapFile f;
	sb::CheckResult res;
	if(!sb::ParseWaterTap(b, &f, &res).ok())
	{
		SetWhy(why, res.line());
		return false;
	}
	if(!w.loadImage(f.block, b.data() + f.imageOffset, f.imageBytes, f.haveCam != 0, f.gridSeq, f.samplesX, f.samplesY))
	{
		const WaterTapReader* r = w.reader();
		SetWhy(why, Fmt("%s refused by the water tap's checks (result %d): %s", path.c_str(), r ? r->lastResult() : 0,
		                r ? r->lastWhy().c_str() : ""));
		return false;
	}
	return true;
}

bool LoadFieldMember(Scene& s, const std::wstring& bundleDir, const sb::Manifest& m, const std::string& path, uint32_t loadSeq,
                     const VPShellCamera* cam, std::string* why)
{
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, path, b, why))
		return false;
	const c5r::Status st = s.loadField(b.data(), b.size(), loadSeq, cam);
	if(!st.ok())
		SetWhy(why, path + " refused: " + st.str());
	return st.ok();
}

bool LoadWaterClockMember(WaterScene& w, const std::wstring& bundleDir, const sb::Manifest& m, std::string* why)
{
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, "water_clock.bin", b, why))
		return false;
	sb::WaterClockFile f;
	sb::CheckResult res;
	if(!sb::ParseWaterClock(b, &f, &res).ok())
	{
		SetWhy(why, res.line());
		return false;
	}
	WaterClockState st;
	st.clockStartMs = f.clockStartMs;
	st.waveLastMs = f.waveLastMs;
	st.lastNowMs = f.lastNowMs;
	st.waves = f.waves;
	if(!w.setClockState(st))
	{
		SetWhy(why, Fmt("water_clock.bin: refused by the water scene (%zu waves; see the log)", st.waves.size()));
		return false;
	}
	return true;
}

// ---- the fog of war (epic 0301, issue 3911) ----------------------------------------------------------------------------
namespace {

class FeedSourcePlotTable : public FowPlotTable
{
public:
	FeedSourcePlotTable(FeedSource& f, uint32_t caller) : m_feed(f), m_caller(caller) {}
	void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h) override
	{
		m_feed.copyPlots(out, gen, w, h, m_caller);
	}

private:
	FeedSource& m_feed;
	uint32_t m_caller;
};

class NoPlotTable : public FowPlotTable
{
public:
	void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h) override
	{
		out.clear();
		gen = w = h = 0;
	}
};

}  // namespace

std::unique_ptr<FowPlotTable> MakeFeedSourcePlotTable(FeedSource& feed, uint32_t caller)
{
	return std::unique_ptr<FowPlotTable>(new FeedSourcePlotTable(feed, caller));
}

void RestartFowWithOverlay(Scene& s, ID3D11Device* dev, const SceneOptions& o, FeedSource& feed)
{
	if(!o.fow)
		return;
	FowSceneOptions fo;   // Scene::init's (srv_render_scene.cpp), the overlay without the section
	fo.on = true;
	fo.overlay = o.overlay != 0;
	fo.level = o.fowLevel;
	fo.exe = o.exe;
	fo.gameDir = o.install;
	fo.freeze = o.waterFreeze;
	fo.installWaitMs = o.fowWaitMs;
	s.fow().setTable(std::unique_ptr<FowPlotTable>(new NoPlotTable()));
	s.fow().init(dev, fo);
	s.fow().setTable(MakeFeedSourcePlotTable(feed));
}

bool LoadFogClockMember(FowScene& f, const std::wstring& bundleDir, const sb::Manifest& m, std::string* why)
{
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, "fog_clock.bin", b, why))
		return false;
	sb::FogClockFile c;
	sb::CheckResult res;
	if(!sb::ParseFogClock(b, &c, &res).ok())
	{
		SetWhy(why, res.line());
		return false;
	}
	FowClockState st;
	st.clockS = c.clockS;
	st.clockLastMs = c.clockLastMs;
	if(!f.setClockState(st))
	{
		SetWhy(why, Fmt("fog_clock.bin: refused by the fog scene (T %g s, last frame at %g ms)", c.clockS, c.clockLastMs));
		return false;
	}
	return true;
}

// ---- format version 3, issue 0364: the decal member ------------------------------------------------------------------------
bool LoadDecalMember(Scene& s, const std::wstring& bundleDir, const sb::Manifest& m, std::string* note, std::string* why)
{
	auto rd = m.renderConfig.find("render_decals");
	std::string path;
	for(const auto& kv : m.members)
		if(kv.second.kind == sb::MemberKind::DecalTap)
		{
			if(!path.empty())
			{
				SetWhy(why, kv.first + ": a second decal member (" + path + " is the set in force)");
				return false;
			}
			path = kv.first;
		}
	if(rd == m.renderConfig.end() || rd->second != "1")
	{
		if(!path.empty())
		{
			SetWhy(why, path + ": a decal member while render_config's render_decals is not 1");
			return false;
		}
		if(note)
			*note = "render_decals off in the recording: no decals are drawn";
		return true;
	}
	if(path.empty())
	{
		for(const std::string& n : m.versionNotes)
			if(n.compare(0, strlen(sb::kDecalNoSetNote), sb::kDecalNoSetNote) == 0)
			{
				if(note)
					*note = "no decal member: " + n;
				return true;
			}
		SetWhy(why, std::string("decals/load<NNNN>.dtap: missing while render_config has render_decals=1 and version_notes have no \"") +
		                sb::kDecalNoSetNote + "\"");
		return false;
	}
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, path, b, why))
		return false;
	sb::DecalTapFile f;
	sb::CheckResult res;
	if(!sb::ParseDecalTap(b, &f, &res).ok())
	{
		res.member = path;
		SetWhy(why, res.line());
		return false;
	}
	if(path != sb::DecalTapPath(f.loadSeq))
	{
		SetWhy(why, Fmt("%s: the image is of load %u", path.c_str(), f.loadSeq));
		return false;
	}
	std::string w;
	if(!s.loadDecalImage(b.data(), b.size(), &w).ok())
	{
		SetWhy(why, path + ": " + w);
		return false;
	}
	if(note)
		*note = Fmt("%s: decal set generation %u (load %u, %u decals, %u names) applied before the span", path.c_str(), f.generation, f.loadSeq,
		            f.decalCount, f.nameCount);
	return true;
}

// ---- format version 9, ticket 4875: the model tap members --------------------------------------------------------------
bool LoadModelTapMembers(const std::wstring& bundleDir, const sb::Manifest& m, ModelTapMembers* out, std::string* note, std::string* why)
{
	(void)bundleDir;
	*out = ModelTapMembers();
	auto on = [&](const char* key) {
		auto it = m.renderConfig.find(key);
		return it != m.renderConfig.end() && it->second == "1";
	};
	const bool modelsOn = on("render_cities") || on("render_landmarks");
	for(const auto& kv : m.members)
		if(kv.second.kind == sb::MemberKind::ModelTap)
		{
			uint32_t idx = 0;
			if(!sb::ModelTapIndex(kv.first, &idx))
			{
				SetWhy(why, kv.first + ": not a model tap member path");
				return false;
			}
			out->members.push_back(std::make_pair(idx, kv.first));
		}
		else if(kv.second.kind == sb::MemberKind::ModelGeom)   // ticket 4888
		{
			uint32_t idx = 0;
			if(!sb::ModelGeomIndex(kv.first, &idx))
			{
				SetWhy(why, kv.first + ": not a model geometry member path");
				return false;
			}
			if(!on("render_cities"))
			{
				SetWhy(why, kv.first + ": a model_geom member while render_config's render_cities is not 1");
				return false;
			}
			out->geom.push_back(std::make_pair(idx, kv.first));
		}
	std::sort(out->members.begin(), out->members.end());
	std::sort(out->geom.begin(), out->geom.end());
	if(!modelsOn)
	{
		if(!out->members.empty())
		{
			SetWhy(why, out->members.front().second + ": a model tap member while render_config's render_cities and render_landmarks are not 1");
			return false;
		}
		if(note)
			*note = "render_cities and render_landmarks off in the recording: the server's models are not drawn";
		return true;
	}
	if(out->members.empty())
	{
		for(const std::string& n : m.versionNotes)
			if(n.compare(0, strlen(sb::kModelNoFrameNote), sb::kModelNoFrameNote) == 0)
			{
				if(note)
					*note = "no model tap member: " + n;
				return true;
			}
		SetWhy(why, std::string("modeltap/frame<NNNNNN>.mtap: missing while render_config has render_cities or render_landmarks = 1 and "
		                        "version_notes have no \"") + sb::kModelNoFrameNote + "\"");
		return false;
	}
	if(note)
	{
		*note = Fmt("%zu model tap members, the first in force from frame index %u", out->members.size(), out->members.front().first);
		if(!out->geom.empty())   // ticket 4888
			*note += Fmt("; %zu CPU-built geometry members (model_geom), the first in force from frame index %u", out->geom.size(),
			             out->geom.front().first);
	}
	return true;
}

bool ApplyModelTapFrame(Scene& s, const std::wstring& bundleDir, const sb::Manifest& m, ModelTapMembers& t, uint32_t frameIndex,
                        std::string* why)
{
	// ticket 4888: the CPU-built meshes' member in force first (the layer resolves the tap's R32 draws against what it holds)
	bool geomChanged = false;
	{
		size_t g = (size_t)-1;
		for(size_t i = 0; i < t.geom.size() && t.geom[i].first <= frameIndex; ++i)
			g = i;
		if(g != (size_t)-1 && g != t.geomApplied)
		{
			const std::string& gp = t.geom[g].second;
			if(!s.models())
			{
				SetWhy(why, gp + ": the scene has no model module (configureModels from render_config first)");
				return false;
			}
			std::vector<uint8_t> b;
			if(!ReadM(bundleDir, m, gp, b, why))
				return false;
			sb::ModelGeomFile f;
			sb::CheckResult res;
			if(!sb::ParseModelGeom(b, &f, &res).ok())
			{
				res.member = gp;
				SetWhy(why, res.line());
				return false;
			}
			std::vector<srvmodels::GeomBuffer> in(f.buffers.size());
			for(size_t i = 0; i < f.buffers.size(); ++i)
			{
				in[i].kind = f.buffers[i].kind;
				in[i].hash = f.buffers[i].hash;
				in[i].stride = f.buffers[i].stride;
				in[i].bytes.swap(f.buffers[i].bytes);
			}
			const c5r::Status st = s.models()->loadGeometry(in);
			if(!st.ok())
			{
				SetWhy(why, gp + ": the model module refuses the geometry: " + st.str());
				return false;
			}
			t.geomApplied = g;
			t.geomLoads++;
			t.geomBuffers = (uint32_t)in.size();
			geomChanged = true;
		}
	}
	size_t k = (size_t)-1;
	for(size_t i = 0; i < t.members.size() && t.members[i].first <= frameIndex; ++i)
		k = i;
	if(k == (size_t)-1 || (k == t.applied && !geomChanged))
		return true;
	const std::string& path = t.members[k].second;
	if(!s.models())
	{
		SetWhy(why, path + ": the scene has no model module (configureModels from render_config first)");
		return false;
	}
	std::vector<uint8_t> b;
	if(!ReadM(bundleDir, m, path, b, why))
		return false;
	sb::ModelTapFile f;
	sb::CheckResult res;
	if(!sb::ParseModelTap(b, &f, &res).ok())
	{
		res.member = path;
		SetWhy(why, res.line());
		return false;
	}
	const c5r::Status st = s.models()->loadImage(b.data(), b.size());
	if(!st.ok())
	{
		SetWhy(why, path + ": the model module refuses the frame: " + st.str());
		return false;
	}
	t.applied = k;
	t.loads++;
	return true;
}

}  // namespace srvrender
