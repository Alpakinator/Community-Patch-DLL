// srv_liveterrain_icewaves.cpp - the EXE's ice and coast waves after a live terrain change: the server's side (epic 0465,
// ticket 0586). Written 2026-11-19 for this repository (GPL-3.0-or-later). Design: srv_liveterrain_icewaves.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "srv_liveterrain_icewaves.h"
#include "srv_liveterrain.h"   // FieldRelevantChange
#include "../../../proxy/include/vp_livereload.h"   // the section's layout (shared with the x86 proxy)
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace srvrender {
namespace liveterrain {

static_assert(sizeof(vplr::Section) <= vplr::kSectionBytes, "the live reload section's layout");

namespace {
std::string Fmt(const char* fmt, ...)
{
	char b[600];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	return b;
}

const char* ResultName(int r)
{
	switch(r)
	{
	case vplr::kOk: return "ok";
	case vplr::kNotAsked: return "not asked";
	case vplr::kRefusedExe: return "REFUSED (EXE build)";
	case vplr::kRefusedPattern: return "REFUSED (code pattern)";
	case vplr::kGridMismatch: return "GRID MISMATCH";
	case vplr::kNotWritable: return "NOT WRITABLE";
	case vplr::kNoFunction: return "NO FUNCTION";
	case vplr::kLuaError: return "LUA ERROR";
	case vplr::kBadRequest: return "BAD REQUEST";
	}
	return "?";
}
}  // namespace

// ---- IceWaves ----
void IceWaves::onLoad(uint32_t loadSeq, uint32_t W, uint32_t H, const std::vector<SrvFeed::PlotRecord>& baseline)
{
	m_have = baseline.size() == (size_t)W * H && W > 0 && H > 0;
	m_load = loadSeq;
	m_W = W;
	m_H = H;
	m_drawn = baseline;
	m_exeIce.assign(baseline.size(), 0);
	m_exeWaves.assign(baseline.size(), 0);
	for(size_t i = 0; i < baseline.size(); ++i)
	{
		m_exeIce[i] = IcePlot(baseline[i]) ? 1 : 0;
		m_exeWaves[i] = WaveClass(baseline[i]);
	}
	m_inFlight = false;
	m_req = IceWavesRequest();
	m_iceFailed = m_wavesFailed = false;
	m_iceHeldNoted = m_wavesHeldNoted = false;
	m_refreshSeq = m_refreshDone = m_reqRefreshSeq = 0;   // the load's ice mesh was built from the load's field
}

void IceWaves::onDrawn(const std::vector<SrvFeed::PlotRecord>& sent)
{
	if(m_have && sent.size() == m_drawn.size())
		m_drawn = sent;
}

void IceWaves::onDrawn(const std::vector<SrvFeed::PlotRecord>& sent, const std::vector<SampleBox>& written)
{
	onDrawn(sent);
	if(!m_fieldRefresh || !m_have || sent.size() != m_drawn.size())   // MC #9538 item 4: off with the land/water step
		return;
	// the plots a rectangle covers, widened by the ring (x by one more: odd rows sit half a plot over); x wraps, y clips
	const int64_t W = m_W, H = m_H, ring = kIceFieldRing;
	for(const SampleBox& b : written)
	{
		if(!b.w || !b.h)
			continue;
		const int64_t px0 = (int64_t)(b.x0 / 64) - ring - 1, px1 = (int64_t)((b.x0 + b.w - 1) / 64) + ring + 1;
		const int64_t py0 = (int64_t)(b.y0 / 64) - 2 - ring, py1 = (int64_t)((b.y0 + b.h - 1) / 64) - 2 + ring;
		const int64_t xs = px1 - px0 + 1 >= W ? W : px1 - px0 + 1;
		for(int64_t y = py0 < 0 ? 0 : py0; y <= py1 && y < H; ++y)
			for(int64_t k = 0; k < xs; ++k)
			{
				const int64_t x = ((px0 + k) % W + W) % W;
				if(m_exeIce[(size_t)(y * W + x)])
				{
					++m_refreshSeq;
					++m_c.iceRefreshes;
					return;
				}
			}
	}
}

bool IceWaves::want(const std::vector<SrvFeed::PlotRecord>& cur, IceWavesRequest& req, std::string& note)
{
	note.clear();
	if(!m_have || m_inFlight || cur.size() != m_drawn.size())
		return false;
	const size_t n = cur.size();
	IceWavesRequest r;
	r.W = m_W;
	r.H = m_H;
	r.grid.assign(n, 0);
	r.waveClasses.assign(n, 0);
	uint32_t heldIce = 0, heldWavePlots = 0, firstHeld = 0xFFFFFFFFu;
	for(size_t i = 0; i < n; ++i)
	{
		const bool held = FieldRelevantChange(m_drawn[i], cur[i]);
		const uint8_t want = IcePlot(cur[i]) ? 1 : 0;
		if(held && want != m_exeIce[i])
		{
			++heldIce;
			r.grid[i] = m_exeIce[i];   // the gate: the EXE keeps the ice it has until the field shows the plot
		}
		else
			r.grid[i] = want;
		r.iceCount += r.grid[i];
		r.expectFlips += r.grid[i] != m_exeIce[i] ? 1u : 0u;
		r.waveClasses[i] = WaveClass(cur[i]);
		if(r.waveClasses[i] != m_exeWaves[i])
		{
			++r.wavePlots;
			if(held && m_wavesGate)
			{
				++heldWavePlots;
				if(firstHeld == 0xFFFFFFFFu)
					firstHeld = (uint32_t)i;
			}
		}
	}
	// 4914: a field window written beside the EXE's ice owes an Ice_Reload even with no bit to flip (the proxy then writes
	// nothing and calls it: expectFlips 0 is a count like any other)
	r.ice = (r.expectFlips > 0 || iceRefreshPending()) && !m_iceFailed;
	r.waves = r.wavePlots > 0 && heldWavePlots == 0 && !m_wavesFailed;
	if(heldIce && !m_iceHeldNoted)
	{
		m_iceHeldNoted = true;
		m_c.iceHeldPlots += heldIce;
		note = Fmt("live ice/waves: load seq %u: %u plot(s) whose ice changed wait with their field change for the reload (the EXE "
		           "keeps their ice)", m_load, heldIce);
	}
	if(heldWavePlots && !m_wavesHeldNoted)
	{
		m_wavesHeldNoted = true;
		++m_c.wavesHeld;
		note += (note.empty() ? "" : "; ") +
		        Fmt("live ice/waves: load seq %u: the coast waves wait for the reload: %u of the %u plot(s) whose wave class changed "
		            "wait in the field (first (%u, %u))", m_load, heldWavePlots, r.wavePlots, firstHeld % m_W, firstHeld / m_W);
	}
	if(!heldIce)
		m_iceHeldNoted = false;
	if(!heldWavePlots)
		m_wavesHeldNoted = false;
	if(!r.ice && !r.waves)
		return false;
	req = std::move(r);
	return true;
}

void IceWaves::posted(const IceWavesRequest& req)
{
	m_inFlight = true;
	m_req = req;
	m_reqRefreshSeq = m_refreshSeq;   // an ice request covers every field window written before it was posted
	++m_c.requests;
}

std::string IceWaves::answered(const IceWavesAnswer& a, bool& loud)
{
	loud = false;
	if(!m_inFlight)
		return std::string();
	m_inFlight = false;
	const bool iceOk = a.iceResult == vplr::kOk;
	if(m_req.ice)
	{
		// the grid as the proxy left it: every flip written (also when Ice_Reload then failed), or none (refused before)
		if(a.bitsSet + a.bitsCleared == m_req.expectFlips)
			for(size_t i = 0; i < m_exeIce.size() && i < m_req.grid.size(); ++i)
				m_exeIce[i] = m_req.grid[i];
		if(iceOk)
			++m_c.iceOk;
		else
		{
			m_iceFailed = true;
			loud = true;
		}
		// the windows written before the post are covered (on a failure the kind is not asked again in this load anyway)
		m_refreshDone = m_reqRefreshSeq;
	}
	if(m_req.waves)
	{
		if(a.wavesResult == vplr::kOk)
		{
			++m_c.wavesOk;
			m_exeWaves = m_req.waveClasses;   // Waves_Reload read the DLL's plots as the request saw them [I: no change between]
		}
		else
		{
			m_wavesFailed = true;
			loud = true;
		}
	}
	if(loud)
		++m_c.failures;
	return Fmt("%slive ice/waves: load seq %u: answer: ice %s (%u set, %u cleared, Ice_Reload %.1f ms), waves %s (Waves_Reload %.1f ms)%s%s%s",
	           loud ? "LOUD: " : "", m_load, m_req.ice ? ResultName(a.iceResult) : "not asked", a.bitsSet, a.bitsCleared,
	           a.iceMicros / 1000.0, m_req.waves ? ResultName(a.wavesResult) : "not asked", a.wavesMicros / 1000.0,
	           a.why.empty() ? "" : ": ", a.why.c_str(),
	           loud ? " (D118: the server keeps drawing the ice and waves it has; that kind is not asked again in this load)" : "");
}

// ---- LiveReloadLink ----
bool LiveReloadLink::open(uint32_t exePid, std::string& why)
{
	close();
	wchar_t name[64];
	_snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE, VPLR_SECTION_FMT, (unsigned)exePid);
	HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, vplr::kSectionBytes, name);
	if(!h)
	{
		why = Fmt("CreateFileMapping(Local\\VP64LiveReload.%u) failed, error %lu", exePid, GetLastError());
		return false;
	}
	const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
	vplr::Section* s = (vplr::Section*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, vplr::kSectionBytes);
	if(!s)
	{
		why = Fmt("MapViewOfFile of the live reload section failed, error %lu", GetLastError());
		CloseHandle(h);
		return false;
	}
	if(existed && s->magic == vplr::kMagic && (s->version != vplr::kVersion || s->bytes != sizeof(vplr::Section)))
	{
		why = Fmt("the section exists with version %u, %u bytes (this server: %u, %u)", s->version, s->bytes, vplr::kVersion,
		          (unsigned)sizeof(vplr::Section));
		UnmapViewOfFile(s);
		CloseHandle(h);
		return false;
	}
	if(!(existed && s->magic == vplr::kMagic))
	{
		memset(s, 0, sizeof(*s));
		s->version = vplr::kVersion;
		s->bytes = sizeof(vplr::Section);
		MemoryBarrier();
		s->magic = vplr::kMagic;
	}
	else
	{   // an earlier server of this EXE: go on from its sequence (an unanswered request of it is answered to nobody)
		const uint32_t hi = s->reqSeq > s->ackSeq ? s->reqSeq : s->ackSeq;
		s->ackSeq = hi;
		s->reqSeq = hi;
	}
	s->serverPid = GetCurrentProcessId();
	m_map = h;
	m_sec = s;
	m_pid = exePid;
	m_posted = 0;
	return true;
}

void LiveReloadLink::close()
{
	if(m_sec)
		UnmapViewOfFile(m_sec);
	if(m_map)
		CloseHandle((HANDLE)m_map);
	m_sec = nullptr;
	m_map = nullptr;
	m_posted = 0;
}

bool LiveReloadLink::post(const IceWavesRequest& r, uint32_t loadSeq, std::string& why)
{
	vplr::Section* s = (vplr::Section*)m_sec;
	if(!s)
	{
		why = "the section is not open";
		return false;
	}
	if(m_posted || s->reqSeq != s->ackSeq)
	{
		why = "a request is unanswered";
		return false;
	}
	if(r.W < 1 || r.W > vplr::kMaxW || r.H < 1 || r.H > vplr::kMaxH || r.grid.size() != (size_t)r.W * r.H)
	{
		why = Fmt("the map %u x %u does not fit the section (at most %u x %u)", r.W, r.H, vplr::kMaxW, vplr::kMaxH);
		return false;
	}
	s->reqFlags = (r.ice ? (uint32_t)vplr::kReqIce : 0u) | (r.waves ? (uint32_t)vplr::kReqWaves : 0u);
	s->loadSeq = loadSeq;
	s->mapW = r.W;
	s->mapH = r.H;
	s->iceCount = r.iceCount;
	s->expectFlips = r.ice ? r.expectFlips : 0;
	memset(s->ice, 0, sizeof(s->ice));
	for(size_t i = 0; i < r.grid.size(); ++i)
		if(r.grid[i])
			vplr::BitSet(s->ice, (uint32_t)i, true);
	const uint32_t seq = s->reqSeq + 1;
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&s->reqSeq, (LONG)seq);
	m_posted = seq;
	return true;
}

bool LiveReloadLink::poll(IceWavesAnswer& a)
{
	vplr::Section* s = (vplr::Section*)m_sec;
	if(!s || !m_posted || s->ackSeq != m_posted)
		return false;
	MemoryBarrier();
	a.iceResult = s->iceResult;
	a.wavesResult = s->wavesResult;
	a.bitsSet = s->bitsSet;
	a.bitsCleared = s->bitsCleared;
	a.iceMicros = s->iceMicros;
	a.wavesMicros = s->wavesMicros;
	char w[sizeof(s->why) + 1];
	memcpy(w, s->why, sizeof(s->why));
	w[sizeof(s->why)] = 0;
	a.why = w;
	m_posted = 0;
	return true;
}

}  // namespace liveterrain
}  // namespace srvrender
