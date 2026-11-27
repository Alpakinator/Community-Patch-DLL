// srv_scene_recorder.cpp - the scene recorder (epic 0301, task 3904). Contract and rules: srv_scene_recorder.h.
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
#include "srv_render_common.h"
#include "srv_scene_recorder.h"
#include "landmarks/srv_models_scene.h"   // ticket 4875: the model tap member
#include "extras/srv_extras.h"   // ticket 4934: SceneRoads (the road producer's span state)
#include <d3d11.h>
#include <stdarg.h>
#include <time.h>
#include <algorithm>
#include <cmath>

namespace srvrender {

namespace sb = scenebundle;

namespace {

std::string Fmt(const char* fmt, ...)
{
	char b[1024];
	va_list a;
	va_start(a, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
	va_end(a);
	return b;
}

bool ParseIntIn(const std::string& v, long long lo, long long hi, long long& out)
{
	if(v.empty())
		return false;
	char* end = nullptr;
	const long long x = strtoll(v.c_str(), &end, 10);
	if(!end || *end || x < lo || x > hi)
		return false;
	out = x;
	return true;
}

bool IsAbsolute(const std::string& p)
{
	if(p.size() >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
		return true;
	return p.size() >= 3 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/') && p[2] != '\\' && p[2] != '/';
}

// The local time as ISO 8601 with its offset ("2026-11-15T10:15:00-05:00") and as yyyymmdd-hhmmss.
std::string IsoNow()
{
	const __time64_t t = _time64(nullptr);
	struct tm lt = {}, ut = {};
	_localtime64_s(&lt, &t);
	_gmtime64_s(&ut, &t);
	struct tm l2 = lt, u2 = ut;
	l2.tm_isdst = 0;
	u2.tm_isdst = 0;
	const long long off = (long long)(_mkgmtime64(&l2) - _mkgmtime64(&u2)) / 60;   // minutes east of UTC
	const long long a = off < 0 ? -off : off;
	return Fmt("%04d-%02d-%02dT%02d:%02d:%02d%c%02lld:%02lld", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min,
	           lt.tm_sec, off < 0 ? '-' : '+', a / 60, a % 60);
}

// <save>_<yyyymmdd-hhmmss>_<machine>: true with the parts when the leaf has that form
bool SplitBundleId(const std::string& id, std::string& save, std::string& machine)
{
	const size_t m = id.rfind('_');
	if(m == std::string::npos || m == 0 || m + 1 >= id.size())
		return false;
	const size_t s = id.rfind('_', m - 1);
	if(s == std::string::npos || s == 0 || m - s - 1 != 15)
		return false;
	const std::string stamp = id.substr(s + 1, 15);
	for(size_t i = 0; i < 15; ++i)
		if(i == 8 ? stamp[i] != '-' : (stamp[i] < '0' || stamp[i] > '9'))
			return false;
	save = id.substr(0, s);
	machine = id.substr(m + 1);
	return true;
}

std::string ComputerName()
{
	wchar_t b[256] = L"";
	DWORD n = 255;
	if(!GetComputerNameW(b, &n))
		return "unknown";
	std::string s = sb::Narrow(b);
	for(char& c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

bool Colour4Bytes(DXGI_FORMAT f)
{
	return (f >= DXGI_FORMAT_R8G8B8A8_TYPELESS && f <= DXGI_FORMAT_R8G8B8A8_SINT) || f == DXGI_FORMAT_B8G8R8A8_UNORM ||
	       f == DXGI_FORMAT_B8G8R8X8_UNORM || (f >= DXGI_FORMAT_B8G8R8A8_TYPELESS && f <= DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
}

bool Depth32Float(DXGI_FORMAT f)
{
	return f == DXGI_FORMAT_R32_TYPELESS || f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_R32_FLOAT;
}

void HexToBytes(const std::string& hex, uint8_t out[32])
{
	auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
	for(int i = 0; i < 32; ++i)
		out[i] = (uint8_t)(hex.size() >= 64 ? v(hex[2 * i]) * 16 + v(hex[2 * i + 1]) : 0);
}

const size_t kFeedPendingMax = 16u << 20;   // pending feed bytes before they are written without their next frame id
const uint64_t kFinishAfterIters = 600;      // iterations after the span's end to wait for overlay texels
const size_t kMaxHistoryFrames = 100000;     // issue 0440: units_history.bin's records at most (74 MB)
const size_t kTexCacheMax = 4;               // texels kept for pairs no frame of the span bound (yet)

}  // namespace

// ==== the keys ===========================================================================================================
bool RecorderKeys::dumpsFrame(uint32_t k) const
{
	if(k >= (uint32_t)frames)
		return false;
	if(dumpEvery < 0)
		return true;
	if(dumpEvery == 0)
		return k == 0 || k + 1 == (uint32_t)frames;
	return k % (uint32_t)dumpEvery == 0;
}

uint32_t RecorderKeys::dumpCount() const
{
	uint32_t n = 0;
	for(uint32_t k = 0; k < (uint32_t)frames; ++k)
		n += dumpsFrame(k) ? 1 : 0;
	return n;
}

bool ParseRecorderKeys(const std::function<std::string(const char* key)>& get, RecorderKeys& out, std::string& why)
{
	RecorderKeys k;
	auto bad = [&](const char* key, const std::string& v, const std::string& want) {
		why = std::string(key) + "=" + v + ": " + want;
		return false;
	};
	const std::string dir = get("render_test_record"), after = get("render_test_record_after"),
	                  frames = get("render_test_record_frames"), dump = get("render_test_record_dump");
	if(dir.empty())
	{
		if(!after.empty())
			return bad("render_test_record_after", after, "needs render_test_record");
		if(!frames.empty())
			return bad("render_test_record_frames", frames, "needs render_test_record");
		if(!dump.empty())
			return bad("render_test_record_dump", dump, "needs render_test_record");
		out = k;
		return true;
	}
	if(!IsAbsolute(dir))
		return bad("render_test_record", dir, "an absolute folder (a drive or a UNC path)");
	if(dir.size() > 200)
		return bad("render_test_record", dir, "at most 200 characters");
	std::string d = dir;
	while(d.size() > 3 && (d.back() == '/' || d.back() == '\\'))
		d.pop_back();
	k.on = true;
	k.dir = sb::Widen(d);
	k.given["render_test_record"] = dir;
	long long n = 0;
	if(!after.empty())
	{
		if(!ParseIntIn(after, 1, 1000000, n))
			return bad("render_test_record_after", after, "an integer 1..1000000");
		k.after = (int)n;
		k.given["render_test_record_after"] = after;
	}
	if(!frames.empty())
	{
		if(!ParseIntIn(frames, 1, 100000, n))
			return bad("render_test_record_frames", frames, "an integer 1..100000");
		k.frames = (int)n;
		k.given["render_test_record_frames"] = frames;
	}
	if(!dump.empty())
	{
		if(dump == "first,last")
			k.dumpEvery = 0;
		else if(dump == "all")
			k.dumpEvery = -1;
		else if(dump.compare(0, 6, "every:") == 0 && ParseIntIn(dump.substr(6), 1, 100000, n))
			k.dumpEvery = (int)n;
		else
			return bad("render_test_record_dump", dump, "first,last, all or every:N (N 1..100000)");
		k.dump = dump;
		k.given["render_test_record_dump"] = dump;
	}
	if(k.dumpCount() < 2)
		return bad(k.given.count("render_test_record_dump") ? "render_test_record_dump" : "render_test_record_frames",
		           k.given.count("render_test_record_dump") ? dump : std::to_string(k.frames),
		           Fmt("%u dumped frame(s) of a span of %d: at least two are needed (contract 9)", k.dumpCount(), k.frames));
	out = k;
	return true;
}

// ==== the recorder =========================================================================================================
std::unique_ptr<SceneRecorder> SceneRecorder::Open(const RecorderKeys& k, const RecorderContext& c, std::string& why)
{
	if(!k.on || k.dir.empty())
	{
		why = "render_test_record is not set";
		return nullptr;
	}
	std::unique_ptr<SceneRecorder> r(new SceneRecorder());
	r->m_k = k;
	r->m_c = c;
	r->m_dir = k.dir;
	std::wstring leaf = k.dir;
	const size_t cut = leaf.find_last_of(L"/\\");
	if(cut != std::wstring::npos)
		leaf = leaf.substr(cut + 1);
	r->m_bundleId = sb::Narrow(leaf);
	std::string save, machine;
	if(SplitBundleId(r->m_bundleId, save, machine))
	{
		r->m_saveName = save;
		r->m_machine = machine;
	}
	else
		r->m_saveName = r->m_bundleId;
	if(!c.machine.empty())
		r->m_machine = c.machine;
	if(r->m_machine.empty())
		r->m_machine = ComputerName();
	r->m_created = IsoNow();
	// the bundle's parent folders are made when missing (the leaf is the writer's: absent or empty); a failure here
	// shows as the writer's own refusal below
	for(size_t i = 3; i < k.dir.size(); ++i)
		if(k.dir[i] == L'/' || k.dir[i] == L'\\')
			CreateDirectoryW(k.dir.substr(0, i).c_str(), nullptr);
	c5r::Status st = r->m_w.open(k.dir);
	if(st.ok())
		st = r->m_w.appendStart("feed.bin", sb::MemberKind::Feed);
	if(!st.ok())
	{
		why = "render_test_record=" + k.given.at("render_test_record") + ": " + st.str();
		return nullptr;
	}
	r->m_feedOpen = true;
	RNote("recorder: bundle '%ls' (id %s, save %s, machine %s): span of %d frames from the draw of published frame %d, dumps %s (%u)",
	      k.dir.c_str(), r->m_bundleId.c_str(), r->m_saveName.c_str(), r->m_machine.c_str(), k.frames, k.after, k.dump.c_str(),
	      k.dumpCount());
	return r;
}

SceneRecorder::~SceneRecorder()
{
	for(Staged& s : m_staged)
		releaseStaged(s);
}

void SceneRecorder::releaseStaged(Staged& s)
{
	SafeRelease(s.colour);
	SafeRelease(s.depth);
}

void SceneRecorder::error(const std::string& why)
{
	m_recordErrors++;
	const std::string head = why.substr(0, 48);
	if(m_errorsSeen.size() < 32 && m_errorsSeen.insert(head).second)
		RNote("LOUD: recorder: %s (record error %lld; the game goes on)", why.c_str(), (long long)m_recordErrors);
}

SceneRecorder::PairKey SceneRecorder::KeyOf(const OverlayFrame& p)
{
	PairKey k;
	k.generation = p.generation;
	k.slot = p.slot;
	k.presentCount = p.presentCount;
	return k;
}

// ---- the height field and the water -------------------------------------------------------------------------------------
void SceneRecorder::FieldSink(void* ctx, uint32_t loadSeq, const uint8_t* c5hf, size_t bytes)
{
	if(ctx)
		((SceneRecorder*)ctx)->onFieldCopy(loadSeq, c5hf, bytes);
}

void SceneRecorder::WaterSink(void* ctx, const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam,
                              uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY)
{
	if(ctx)
		((SceneRecorder*)ctx)->onWaterImage(block, image, imageBytes, haveCam, gridSeq, samplesX, samplesY);
}

void SceneRecorder::onFieldCopy(uint32_t loadSeq, const uint8_t* c5hf, size_t bytes)
{
	if(m_finished || !c5hf)
		return;
	m_fieldCopies++;
	std::string path = Fmt("heightfield/load%04u.c5hf", loadSeq);
	for(int n = 2; m_written.count(path); ++n)
		path = Fmt("heightfield/load%04u-%d.c5hf", loadSeq, n);   // a later copy of the same load (a re-request)
	const c5r::Status st = m_w.addMember(path, sb::MemberKind::Heightfield, c5hf, bytes);
	if(!st.ok())
	{
		error(path + ": " + st.str());
		return;
	}
	m_written.insert(path);
	if(bytes >= 16)
	{
		memcpy(&m_mapW, c5hf + 8, 4);
		memcpy(&m_mapH, c5hf + 12, 4);
	}
	m_loadSeq = loadSeq;
	RNote("recorder: %s written (%llu bytes, map %ux%u)", path.c_str(), (unsigned long long)bytes, m_mapW, m_mapH);
}

void SceneRecorder::onWaterImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam,
                                 uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY)
{
	if(m_finished || !image)
		return;
	m_waterCopies++;
	std::string path = Fmt("water/load%04u.wtap", block.loadSeq);
	if(m_written.count(path))
		path = Fmt("water/load%04u-c%u.wtap", block.loadSeq, block.copySeq);
	for(int n = 2; m_written.count(path); ++n)
		path = Fmt("water/load%04u-c%u-%d.wtap", block.loadSeq, block.copySeq, n);
	std::vector<uint8_t> file;
	sb::EncodeWaterTap(block, haveCam ? 1u : 0u, gridSeq, samplesX, samplesY, image, imageBytes, &file);
	const c5r::Status st = m_w.addMember(path, sb::MemberKind::WaterTap, file.data(), file.size());
	if(!st.ok())
	{
		error(path + ": " + st.str());
		return;
	}
	m_written.insert(path);
	// ticket 0622 (format version 11): the sink runs only for a copy the reader accepts, inside Scene::poll before the frame's
	// draw (WaterScene::poll then uses it): from the next afterDraw on, frames are drawn with this copy
	const std::string sha = sb::Sha256Hex(file.data(), file.size());
	if(sha.size() == 64)
	{
		memset(&m_waterNow, 0, sizeof(m_waterNow));
		m_waterNow.flags = sb::kWaterCopyInForce;
		m_waterNow.loadSeq = block.loadSeq;
		m_waterNow.copySeq = block.copySeq;
		HexToBytes(sha, m_waterNow.wtapSha256);
	}
	else
		error(path + ": its SHA-256 is not available (bcrypt.dll): water_frames.bin cannot name it");
	RNote("recorder: %s written (image %llu bytes, load %u copy %u)", path.c_str(), (unsigned long long)imageBytes, block.loadSeq,
	      block.copySeq);
}

// ---- the feed -----------------------------------------------------------------------------------------------------------
void SceneRecorder::onDrain(const SrvFeed::Batch& b)
{
	m_lastDrainMs = -1;   // issue 4456: a drain not recorded has no recorded clock
	if(m_finished || !m_feedOpen)
		return;
	m_feedDrains++;
	for(const SrvFeed::Event& e : b.events)
		if(e.kind == SrvFeed::kOverflow)
			m_feedOverflowEvents++;
	std::vector<uint8_t> rec;
	// issue 4457: the drain clock (QpcToMs, as the unit scene's frame clock) and the batch's unit side tables
	const double drainMs = QpcToMs(Qpc());
	const c5r::Status st = m_enc.addDrain(m_loopIter, -1, b, &rec, drainMs);
	if(!st.ok())
	{
		error("feed: " + st.str());
		return;
	}
	m_lastDrainMs = drainMs;   // issue 4456: the value feed.bin holds for this DRAIN
	m_feedPatch.push_back(m_feedPending.size() + 16);   // u32 type, u32 bytes, u64 loopIter, then i64 nextFrameId
	m_feedPending.insert(m_feedPending.end(), rec.begin(), rec.end());
	if(m_feedPending.size() > kFeedPendingMax)
	{
		if(!m_feedEarlyFlushes++)
			m_notes.push_back("recorder: feed DRAIN records written before the next draw have nextFrameId -1 (more than 16 MiB waited)");
		writeFeedPending(-1);
	}
}

void SceneRecorder::onPlots(uint32_t caller, const std::vector<SrvFeed::PlotRecord>& table, uint32_t gen, uint32_t w, uint32_t h)
{
	if(m_finished || !m_feedOpen)
		return;
	if((size_t)w * h != table.size())
	{
		error(Fmt("feed: a CopyPlots table of %zu records for %ux%u plots (caller %u) is not recorded", table.size(), w, h, caller));
		return;
	}
	m_feedTables++;
	std::vector<uint8_t> rec;
	m_enc.addPlots(m_loopIter, caller, gen, w, h, table, &rec);
	m_feedPending.insert(m_feedPending.end(), rec.begin(), rec.end());
}

void SceneRecorder::onUnits(const std::vector<SrvFeed::UnitRecord>& table, uint32_t unitTableGen)
{
	if(m_finished || !m_feedOpen)
		return;
	std::vector<uint8_t> rec;
	const c5r::Status st = m_enc.addUnits(m_loopIter, m_feedUnitCopies ? srvunits::rec::kUnitsLaterCopy : srvunits::rec::kUnitsFirstCopy,
	                                      unitTableGen, table, &rec);
	if(!st.ok())
	{
		error("feed: " + st.str());
		return;
	}
	m_feedUnitCopies++;
	m_feedPending.insert(m_feedPending.end(), rec.begin(), rec.end());
}

void SceneRecorder::onPlayers(const std::vector<SrvFeed::PlayerRecord>& rows)
{
	if(m_finished || !m_feedOpen)
		return;
	std::vector<uint8_t> rec;
	const c5r::Status st = m_enc.addPlayers(m_loopIter, rows, &rec);
	if(!st.ok())
	{
		error("feed: " + st.str());
		return;
	}
	m_feedPlayerCopies++;
	m_feedPending.insert(m_feedPending.end(), rec.begin(), rec.end());
}

void SceneRecorder::writeFeedPending(int64_t nextFrameId)
{
	if(m_feedPending.empty())
		return;
	for(size_t off : m_feedPatch)
		memcpy(&m_feedPending[off], &nextFrameId, 8);
	const c5r::Status st = m_w.append("feed.bin", m_feedPending.data(), m_feedPending.size());
	if(!st.ok())
		error("feed.bin: " + st.str());
	m_feedPending.clear();
	m_feedPatch.clear();
}

// ---- the overlay --------------------------------------------------------------------------------------------------------
void SceneRecorder::onOverlayPair(const OverlayFrame& pair)
{
	m_curPair = pair;
	m_havePair = pair.srv != nullptr;
	m_ovlPairs++;
}

void SceneRecorder::onOverlayTexels(const OverlayFrame& pair, const OverlayTexels& t)
{
	if(m_finished)
		return;
	m_ovlTexels++;
	const PairKey k = KeyOf(pair);
	if(m_needed.count(k))
	{
		m_needed.erase(k);
		writeOverlay(k, t);
		return;
	}
	if(m_spanDone || m_pairSha.count(k))
		return;
	if(!m_texCache.count(k))
		m_texOrder.push_back(k);
	m_texCache[k] = t;
	while(m_texOrder.size() > kTexCacheMax)
	{
		m_texCache.erase(m_texOrder.front());
		m_texOrder.erase(m_texOrder.begin());
	}
}

void SceneRecorder::onOverlayReadbackError(const OverlayFrame& pair, const std::string& why)
{
	const PairKey k = KeyOf(pair);
	m_pairFailed[k] = why;
	m_needed.erase(k);
	error(Fmt("overlay readback of generation %u: %s", pair.generation, why.c_str()));
}

void SceneRecorder::writeOverlay(const PairKey& k, const OverlayTexels& t)
{
	if(t.width != sb::kOverlaySize || t.height != sb::kOverlaySize || t.mips != sb::kOverlayMips || t.bytes.size() != sb::kOverlayTexelBytes)
	{
		m_pairFailed[k] = Fmt("texels %ux%u x %u mips, %zu bytes: not the tap's 2048 x 2048 x 12", t.width, t.height, t.mips, t.bytes.size());
		error("overlay of generation " + std::to_string(k.generation) + ": " + m_pairFailed[k]);
		return;
	}
	std::vector<uint8_t> file;
	c5r::Status st = sb::EncodeOverlay(t.bytes.data(), t.bytes.size(), &file);
	const std::string sha = st.ok() ? sb::Sha256Hex(file.data(), file.size()) : std::string();
	if(!st.ok() || sha.size() != 64)
	{
		m_pairFailed[k] = st.ok() ? "no SHA-256 (bcrypt)" : st.str();
		error("overlay of generation " + std::to_string(k.generation) + ": " + m_pairFailed[k]);
		return;
	}
	const std::string path = "overlay/" + sha.substr(0, 16) + ".ovl";
	if(m_written.count(path))
		m_ovlDeduped++;   // the same texture as an earlier pair: one file (contract 2.3)
	else
	{
		st = m_w.addMember(path, sb::MemberKind::OverlayTexture, file.data(), file.size());
		if(!st.ok())
		{
			m_pairFailed[k] = st.str();
			error(path + ": " + st.str());
			return;
		}
		m_written.insert(path);
		m_ovlFiles++;
	}
	m_pairSha[k] = sha;
}

bool SceneRecorder::overlaysResolved() const
{
	return m_needed.empty();
}

void SceneRecorder::setAdapter(uint64_t luid, const std::string& description, const std::string& driver)
{
	m_luid = Fmt("%016llX", (unsigned long long)luid);
	m_adapterDesc = description;
	m_adapterDriver = driver;
}

// ---- the frame hook -----------------------------------------------------------------------------------------------------
bool SceneRecorder::beforeDraw(int64_t frameId, int64_t published, const WaterScene& water, double* nowMs, const SceneFrame* frame)
{
	if(m_finished)
		return false;
	writeFeedPending(frameId);
	if(m_spanDone)
		return false;
	if(!m_spanStarted)
	{
		if(published + 1 < m_k.after)
		{
			if(m_units.scene)   // issue 0440: a frame before the span, drawn with the recorder's clock while units are recorded
			{
				*nowMs = QpcToMs(Qpc());
				noteUnitsFrame(frameId, *nowMs, frame);
			}
			return false;
		}
		m_spanStarted = true;
		if(m_units.scene)   // issue 0440: the controller's history ends here; the database in force at the span's first frame
		{
			m_unitsReadyAtSpan = m_units.scene->ready();
			if(!m_histStarted && !m_histOver && !m_histNoFrame && m_unitsReadyAtSpan && m_histHavePrev)
			{
				m_hist.push_back(m_histPrev);   // the controller started in the frame right before the span
				m_histStarted = true;
			}
			if(!m_histStarted && m_histWhy.empty())
				m_histWhy = m_histNoFrame ? "the host gave no frame blocks" :
				            m_unitsReadyAtSpan ? "the unit scene was ready before the recorder saw its first frame" :
				                                 "the unit scene was not ready at the span's first frame (its controller had not drawn)";
			m_unitsHaveDb = m_units.database && m_units.database(m_unitsDbPath, m_unitsDbGen);
			RNote("recorder: units at the span's start: ready %d, history %zu frames%s%s, database %ls (generation %u)", m_unitsReadyAtSpan ? 1 : 0,
			      m_hist.size(), m_histStarted ? "" : ": none, ", m_histStarted ? "" : m_histWhy.c_str(),
			      m_unitsHaveDb ? m_unitsDbPath.c_str() : L"(none)", m_unitsDbGen);
		}
		const WaterClockState cs = water.clockState();
		m_clock.clockStartMs = cs.clockStartMs;
		m_clock.waveLastMs = cs.waveLastMs;
		m_clock.lastNowMs = cs.lastNowMs;
		m_clock.waves = cs.waves;
		m_haveClock = true;
		RNote("recorder: span starts at frame %lld (published %lld, loop iteration %llu), water clock start %.3f, %zu waves",
		      (long long)frameId, (long long)published, (unsigned long long)m_loopIter, cs.clockStartMs, cs.waves.size());
		// issue 3911: FOWAnim's clock before this frame's draw (fog_clock.bin)
		if(m_fog && m_fog->state() != FowScene::kOff)
		{
			const FowClockState fc = m_fog->clockState();
			m_fogClock.clockS = fc.clockS;
			m_fogClock.clockLastMs = fc.clockLastMs;
			m_haveFogClock = true;
			RNote("recorder: fog clock at the span's start: T %.6f s, last fog frame at %.3f ms (fog %s)", fc.clockS, fc.clockLastMs,
			      FowScene::StateName(m_fog->state()));
		}
		writeDecalSet(frameId);   // issue 0364: the decal set in force, before this frame's draw
		if(m_fog)   // issue 0440 (MC #3003), ticket 0403 (format version 6): the FOWLevel-3 particles' state before this draw
		{
			m_fogPartFrames0 = m_fog->particleFrames;
			m_fogPartOnAtSpan = m_fog->particlesOn();
			std::string pwhy;
			m_havePartState = m_fogPartOnAtSpan && m_fog->particleState(m_partState, pwhy);
			if(m_fogPartOnAtSpan && !m_havePartState)
				m_partWhy = pwhy;
			// render_fow_particles: whether the particles were on at the span's first frame (the player's switch)
			setRenderConfig("render_fow_particles", m_fogPartOnAtSpan ? "1" : "0");
			if(m_havePartState)
				RNote("recorder: fog particles at the span's start: %s, seed %u, world %.0f x %.0f, state %zu bytes (%lld particle frames so far)",
				      m_partState.spawned ? "spawned" : "not spawned yet", m_partState.seed, m_partState.worldW, m_partState.worldH,
				      m_partState.sim.size(), (long long)m_fogPartFrames0);
			else
				RNote("recorder: fog particles at the span's start: %s", m_fogPartOnAtSpan ? ("on, no state: " + m_partWhy).c_str() : "off");
		}
		takeForestAndNear();   // ticket 0495 (format version 8): forest.bin and near.bin, before this frame's draw
		takeFarm();            // ticket 4852: the farm producer's input (render_config)
		takeRoads();           // ticket 4934: the road producer's input (render_config)
	}
	else if(frameId <= m_lastFrameId)
	{
		m_spanBroken = Fmt("frame %lld drawn after frame %lld (a redraw or a new composite): the span ends there", (long long)frameId,
		                   (long long)m_lastFrameId);
		m_spanDone = true;
		m_spanDoneIter = m_loopIter;
		RNote("recorder: %s", m_spanBroken.c_str());
		return false;
	}
	else
	{
		watchDecalGeneration(frameId);   // issue 0364
		// ticket 0495: forest.bin holds one cut mask; a change of the mask inside the span is a gap
		if(m_forestGet && m_haveForest && m_forestCutNote.empty())
			if(const SceneForest* f = m_forestGet())
				if(f->cutGeneration() != m_forestCutGen0)
				{
					m_forestCutNote = Fmt("%s: the cut mask changed inside the span at frame %lld (forest.bin holds the first frame's)",
					                      sb::kForestNote, (long long)frameId);
					RNote("recorder: %s", m_forestCutNote.c_str());
				}
		// ticket 4852: the farm line holds the first frame's input; a production put in force later is a gap
		if(m_farmGet && m_farmOnAtSpan && m_farmNote.empty())
			if(const FarmFieldsProducer* fp = m_farmGet())
				if(fp->version() != m_farmVersion0)
				{
					m_farmNote = Fmt("%s: the farm fields changed inside the span at frame %lld (render_farmfields_state holds the first "
					                 "frame's input)",
					                 sb::kFarmNote, (long long)frameId);
					RNote("recorder: %s", m_farmNote.c_str());
				}
		// ticket 4934: the same for the road layout
		if(m_roadsGet && m_roadsOnAtSpan && m_roadsNote.empty())
			if(const srvextras::SceneRoads* rp = m_roadsGet())
				if(rp->version() != m_roadsVersion0)
				{
					m_roadsNote = Fmt("%s: the road layout changed inside the span at frame %lld (render_roadlayout_state holds the first "
					                  "frame's input)",
					                  sb::kRoadNote, (long long)frameId);
					RNote("recorder: %s", m_roadsNote.c_str());
				}
	}
	// ticket 4875's takeModelTap moved to afterDraw by ticket 4883: the frame's tap is chosen inside Scene::draw
	noteLiveTerrainFrame(frameId);   // epic 0465, ticket 4907 (srv_liveterrain_bundle.cpp)
	*nowMs = QpcToMs(Qpc());
	return true;
}

// ---- ticket 4888 (format version 9, optional): modelgeom/frame<NNNNNN>.mgeo (srv_scene_bundle.h, the block at ModelGeomFile)
void SceneRecorder::takeModelGeometry(const srvmodels::ModelsScene& ms, uint32_t idx, int64_t frameId)
{
	if(m_modelGeomNoted)
		return;   // a member failed: the note stands for the rest of the span
	const uint64_t setHash = ms.heldSetHash();
	if(setHash == m_geomSetHash)
		return;   // the same held set as the last member (or none held and no member yet)
	std::string why;
	std::vector<srvmodels::GeomBuffer> held;
	ms.geometrySnapshot(held);
	const size_t heldCount = ms.heldGeometryCount();
	if(held.size() != heldCount)
		why = Fmt("the layer holds %zu buffer(s) but gave the bytes of %zu", heldCount, held.size());
	const std::string path = sb::ModelGeomPath(idx);
	if(why.empty() && path.empty())
		why = Fmt("frame index %u has no member name", idx);
	std::vector<uint8_t> b;
	uint64_t dataBytes = 0;
	if(why.empty())
	{
		std::vector<sb::ModelGeomBuffer> list(held.size());
		for(size_t i = 0; i < held.size(); ++i)
		{
			list[i].kind = held[i].kind;
			list[i].hash = held[i].hash;
			list[i].stride = held[i].stride;
			list[i].bytes.swap(held[i].bytes);
			dataBytes += list[i].bytes.size();
		}
		sb::EncodeModelGeom(std::move(list), &b);
		sb::CheckResult cr;
		if(!sb::ParseModelGeom(b, nullptr, &cr).ok())
			why = "the layer's buffers fail the member's checks: " + cr.line();
	}
	if(why.empty())
	{
		const c5r::Status st = m_w.addMember(path, sb::MemberKind::ModelGeom, b.data(), b.size());
		if(!st.ok())
			why = path + ": " + st.str();
	}
	if(!why.empty())
	{
		m_modelGeomNoted = true;
		error("models: the CPU-built meshes not recorded: " + why);
		m_notes.push_back(Fmt("%s: ticket 4879's geometry section (Local\\VPShellModelGeom.<pid>): the %zu buffer(s) the model layer "
		                      "held at frame %lld of the span were not recorded (%s), so the replay draws the models without them "
		                      "from frame index %u (not P1; only the offline library is affected, the product draws them live)",
		                      sb::kModelGeomNote, heldCount, (long long)frameId, why.c_str(), idx));
		RNote("recorder: %s", m_notes.back().c_str());
		return;
	}
	m_geomSetHash = setHash;
	m_geomMembers++;
	m_written.insert(path);
	RNote("recorder: CPU-built meshes: %zu buffer(s), %llu data bytes, in force from frame index %u (frame %lld): %s (%llu members)",
	      held.size(), (unsigned long long)dataBytes, idx, (long long)frameId, path.c_str(), (unsigned long long)m_geomMembers);
}

// ---- ticket 4875 (format version 9): modeltap/frame<NNNNNN>.mtap (srv_scene_bundle.h, the block at ModelTapFile) ------
void SceneRecorder::takeModelTap(int64_t frameId)
{
	if(!m_modelsGet)
		return;
	const srvmodels::ModelsScene* ms = m_modelsGet();
	const uint32_t idx = (uint32_t)m_frames.size();   // this frame's index in frames.bin (afterDraw appends it)
	// ticket 4888 (was 4883's gap note): the CPU-built meshes the layer held for this frame (ticket 4879's geometry section,
	// the walls, the Great Wall, bridges) as modelgeom/frame<NNNNNN>.mgeo, at the first frame where it holds any and again
	// whenever the held set changes; the note kModelGeomNote only when such a member cannot be written
	if(ms)
		takeModelGeometry(*ms, idx, frameId);
	const uint64_t hash = ms ? ms->imageHash() : 0;
	if(!hash)
	{
		if(idx == 0 && !m_modelNoFrameNoted)
		{
			m_modelNoFrameNoted = true;
			m_notes.push_back(Fmt("%s (frame %lld, %s): the span's first frames are drawn without the server's models", sb::kModelNoFrameNote,
			                      (long long)frameId, ms ? "the module has no frame yet" : "no model module"));
			RNote("recorder: %s", m_notes.back().c_str());
		}
		return;
	}
	if(hash == m_modelHash || m_modelFailed)
		return;
	const std::string path = sb::ModelTapPath(idx);
	wchar_t tmpDir[MAX_PATH] = L"";
	const DWORD n = GetTempPathW(MAX_PATH, tmpDir);
	const std::wstring tmp = (n && n < MAX_PATH ? std::wstring(tmpDir) : m_dir + L"\\..\\") +
	                         sb::Widen(Fmt("c5_scene_mtap_%lu_%lld.mtap", GetCurrentProcessId(), (long long)Qpc()));
	std::string why;
	std::vector<uint8_t> b;
	c5r::Status st = path.empty() ? c5r::Status::fail(c5r::Err::BadLayout, Fmt("frame index %u has no member name", idx))
	                              : ms->writeCurrentImage(tmp);
	if(!st.ok())
		why = "writeCurrentImage: " + st.str();
	else
	{
		HANDLE f = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		LARGE_INTEGER sz = {};
		if(f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (LONGLONG)VPSHELL_MODELTAP_SLOT_KB_MAX * 1024)
			why = Fmt("the written frame cannot be read back (error %lu, %lld bytes)", GetLastError(), (long long)sz.QuadPart);
		else
		{
			b.resize((size_t)sz.QuadPart);
			DWORD got = 0;
			if(!ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) || got != b.size())
				why = Fmt("the written frame reads back short (%lu of %zu bytes)", (unsigned long)got, b.size());
		}
		if(f != INVALID_HANDLE_VALUE)
			CloseHandle(f);
		DeleteFileW(tmp.c_str());
	}
	sb::ModelTapFile mf;
	sb::CheckResult cr;
	if(why.empty() && !sb::ParseModelTap(b, &mf, &cr).ok())
		why = "the module's frame fails the member's checks: " + cr.line();
	if(why.empty())
	{
		st = m_w.addMember(path, sb::MemberKind::ModelTap, b.data(), b.size());
		if(!st.ok())
			why = path + ": " + st.str();
	}
	if(!why.empty())
	{
		m_modelFailed = true;
		error("models: a model tap frame not recorded: " + why);
		m_w.markIncomplete("models: the model tap frame in force at frame index " + std::to_string(idx) + " was not recorded: " + why);
		return;
	}
	m_modelHash = hash;
	m_modelMembers++;
	m_written.insert(path);
	if(m_modelMembers <= 3 || m_modelMembers % 100 == 0)
		RNote("recorder: model tap frame %llu (%u draws, %zu bytes) in force from frame index %u (frame %lld): %s (%llu members)",
		      (unsigned long long)mf.frameSeq, mf.drawCount, b.size(), idx, (long long)frameId, path.c_str(),
		      (unsigned long long)m_modelMembers);
}

// ---- ticket 4852: the farm producer's input at the span's first frame (render_config; no member, format 8 unchanged) ------
void SceneRecorder::takeFarm()
{
	const FarmFieldsProducer* fp = m_farmGet ? m_farmGet() : nullptr;
	m_farmOnAtSpan = fp != nullptr;
	setRenderConfig("render_farmfields", fp ? "1" : "0");
	if(!fp)
		return;
	std::string text, why;
	if(fp->spanState(text, why))
	{
		setRenderConfig("render_farmfields_state", text);
		m_farmVersion0 = fp->version();
		RNote("recorder: farm fields at the span's start: %zu bytes of input, %zu decals in force", text.size(),
		      fp->producer().decals().decals.size());
	}
	else
	{
		m_notes.push_back(Fmt("%s: the farm producer was on and its input is not in the bundle (%s): the player draws no farm fields of "
		                      "its own",
		                      sb::kFarmNote, why.c_str()));
		RNote("recorder: %s", m_notes.back().c_str());
		m_farmNote = "-";   // no change note on top of this one
	}
}

// ---- ticket 4934: the road producer's input at the span's first frame (render_config; no member, format unchanged) ---------
void SceneRecorder::takeRoads()
{
	const srvextras::SceneRoads* rp = m_roadsGet ? m_roadsGet() : nullptr;
	m_roadsOnAtSpan = rp != nullptr;
	if(!rp)
		return;   // nothing written: a bundle without the road layout keeps the keys it had (rule A)
	setRenderConfig("render_roadlayout", "1");
	std::string text, why;
	if(rp->spanState(text, why))
	{
		setRenderConfig("render_roadlayout_state", text);
		m_roadsVersion0 = rp->version();
		RNote("recorder: road layout at the span's start: %zu bytes of input", text.size());
	}
	else
	{
		m_notes.push_back(Fmt("%s: the road producer was on and its input is not in the bundle (%s): the player draws the tap's roads",
		                      sb::kRoadNote, why.c_str()));
		RNote("recorder: %s", m_notes.back().c_str());
		m_roadsNote = "-";
	}
}

// ---- ticket 0495 (format version 8): the trees' and the near pairs' state at the span's first frame ---------------------
void SceneRecorder::takeForestAndNear()
{
	const SceneForest* f = m_forestGet ? m_forestGet() : nullptr;
	setRenderConfig("render_forest", f ? "1" : "0");
	if(f)
	{
		ForestSpanState s;
		std::string why;
		const int64_t q0 = Qpc();
		m_haveForest = f->spanState(s, why);
		if(m_haveForest)
		{
			m_forestCutGen0 = f->cutGeneration();
			m_forest.flags = (s.haveCut ? sb::kForestHaveCut : 0u) | (s.idsResolved ? sb::kForestIdsResolved : 0u);
			m_forest.sessionSeed = s.sessionSeed;
			m_forest.layoutSeed = s.layoutSeed;
			m_forest.loads = s.loads;
			m_forest.loadSeq = s.loadSeq;
			m_forest.forestId = s.forestId;
			m_forest.jungleId = s.jungleId;
			m_forest.streamState = s.streamState;
			m_forest.tableGen = s.tableGen;
			m_forest.tableW = s.tableW;
			m_forest.tableH = s.tableH;
			for(const ForestSpanRecord& r : s.records)
			{
				sb::ForestStateRecord o;
				o.x = r.x;
				o.y = r.y;
				o.kind = r.kind;
				o.layout = r.layout;
				m_forest.records.push_back(o);
			}
			m_forest.built = s.built;
			m_forest.marks = s.marks;
			m_forest.cutW = s.cutW;
			m_forest.cutH = s.cutH;
			m_forest.cutOx = s.cutOx;
			m_forest.cutOy = s.cutOy;
			m_forest.cutGeneration = s.cutGeneration;
			m_forest.cutCells = std::move(s.cutCells);
			RNote("recorder: forest at the span's start: %zu records, %zu built, %zu marks, cut %s, seeds %08x / %08x (%.1f ms)",
			      m_forest.records.size(), m_forest.built.size(), m_forest.marks.size(),
			      s.haveCut ? Fmt("%ux%u generation %llu", s.cutW, s.cutH, (unsigned long long)s.cutGeneration).c_str() : "empty",
			      s.sessionSeed, s.layoutSeed, QpcToMs(Qpc() - q0));
		}
		else
		{
			m_forestWhy = why;
			RNote("recorder: forest at the span's start: no state: %s", why.c_str());
		}
	}
	m_nearOnAtSpan = false;
	if(m_nearGet)
	{
		m_haveNear = m_nearGet(m_near.state, m_near.tilesX, m_near.tilesY, m_nearOnAtSpan, m_near.images);
		if(m_nearOnAtSpan && !m_haveNear)
			m_nearWhy = "the residency gave no near state (no field?)";
		RNote("recorder: near pairs at the span's start: %s", !m_nearOnAtSpan ? "off" :
		      m_haveNear ? Fmt("%ux%u tiles, state %zu bytes, %zu images of pairs behind their tile", m_near.tilesX, m_near.tilesY,
		                       m_near.state.size(), m_near.images.size()).c_str() : m_nearWhy.c_str());
	}
}

// ---- issue 0440 (format version 5): the unit scene's history and database ------------------------------------------------------
// One frame before the span: the frame before the first one drawn with ready() set is the controller's first frame (its
// beginFrame was the first after the pre-check passed, and ready() turns true once that frame drew), so the recorder keeps
// the last frame until then and every frame after it.
void SceneRecorder::noteUnitsFrame(int64_t frameId, double nowMs, const SceneFrame* frame)
{
	if(m_histOver || m_histNoFrame)
		return;
	if(!frame)
	{
		m_histNoFrame = true;
		m_hist.clear();
		m_histStarted = false;
		return;
	}
	sb::UnitsHistoryRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = frameId;
	r.loopIter = m_loopIter;
	r.nowMs = nowMs;
	r.flags = m_units.scene->ready() ? sb::kUnitsHistReady : 0;
	memcpy(&r.cam, &frame->cam, sizeof(r.cam));
	if(!m_histStarted)
	{
		if(r.flags & sb::kUnitsHistReady)
		{
			if(!m_histHavePrev)
			{
				m_histWhy = "the unit scene was ready before the recorder saw its first frame";
				m_histOver = true;
				return;
			}
			m_hist.push_back(m_histPrev);
			m_histStarted = true;
		}
		else
		{
			m_histPrev = r;
			m_histHavePrev = true;
			return;
		}
	}
	if(m_hist.size() >= kMaxHistoryFrames)
	{
		m_histWhy = Fmt("the history is longer than %zu frames (render_test_record_after is too far after the controller's start)", kMaxHistoryFrames);
		m_histOver = true;
		m_hist.clear();
		m_histStarted = false;
		RNote("recorder: LOUD: units: %s", m_histWhy.c_str());
		return;
	}
	m_hist.push_back(r);
}

void SceneRecorder::writeUnitsMembers()
{
	if(!m_units.scene || m_w.incomplete())
		return;
	// the database: the file as the unit scene opened it, read whole (sharing writes: SQLite may hold it open)
	std::string dbWhy;
	if(!m_unitsHaveDb)
		dbWhy = "the unit scene had no database at the span's first frame";
	else
	{
		uint32_t genNow = 0;
		std::wstring pathNow;
		if(m_units.database && m_units.database(pathNow, genNow) && (genNow != m_unitsDbGen || pathNow != m_unitsDbPath))
			dbWhy = Fmt("the database changed after the span's first frame (generation %u -> %u)", m_unitsDbGen, genNow);
		else
		{
			std::vector<uint8_t> b;
			HANDLE f = CreateFileW(m_unitsDbPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			                       FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			LARGE_INTEGER sz = {};
			if(f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &sz) || sz.QuadPart < 100 || sz.QuadPart > ((int64_t)1 << 32))
				dbWhy = Fmt("%ls cannot be read (error %lu, %lld bytes)", m_unitsDbPath.c_str(), GetLastError(), (long long)sz.QuadPart);
			else
			{
				b.resize((size_t)sz.QuadPart);
				size_t at = 0;
				while(at < b.size())
				{
					DWORD got = 0;
					if(!ReadFile(f, b.data() + at, (DWORD)(std::min)(b.size() - at, (size_t)1 << 24), &got, nullptr) || !got)
						break;
					at += got;
				}
				if(at != b.size())
					dbWhy = Fmt("%ls: read %zu of %zu bytes", m_unitsDbPath.c_str(), at, b.size());
			}
			if(f != INVALID_HANDLE_VALUE)
				CloseHandle(f);
			if(dbWhy.empty())
			{
				sb::CheckResult cr;
				if(!sb::ParseUnitsDb(b, &cr).ok())
					dbWhy = Fmt("%ls: ", m_unitsDbPath.c_str()) + cr.why;
				else
				{
					const c5r::Status st = m_w.addMember(sb::UnitsDbPath(m_unitsDbGen), sb::MemberKind::UnitsDb, b.data(), b.size());
					if(!st.ok())
						error("units_db: " + st.str());
					else
						RNote("recorder: units: database %ls (%zu bytes) -> %s", m_unitsDbPath.c_str(), b.size(), sb::UnitsDbPath(m_unitsDbGen).c_str());
				}
			}
		}
	}
	if(!dbWhy.empty())
	{
		m_notes.push_back(std::string(sb::kUnitsNoDbNote) + ": " + dbWhy);
		RNote("recorder: %s", m_notes.back().c_str());
	}
	if(m_histStarted && !m_hist.empty())
	{
		sb::UnitsHistoryFile hf;
		hf.dbGeneration = m_unitsDbGen;
		hf.flags = m_unitsReadyAtSpan ? sb::kUnitsHistReady : 0;
		hf.records = m_hist;
		std::vector<uint8_t> b;
		sb::EncodeUnitsHistory(hf, &b);
		sb::CheckResult cr;
		if(!sb::ParseUnitsHistory(b, nullptr, &cr).ok())   // the reader's own rules (a ready state that flipped back, a clock going back)
			m_histWhy = "the history breaks its file form's rules: " + cr.why;
		else
		{
			const c5r::Status st = m_w.addMember("units_history.bin", sb::MemberKind::UnitsHistory, b.data(), b.size());
			if(!st.ok())
				error("units_history: " + st.str());
			else
			{
				RNote("recorder: units: history of %zu frames (from frame %lld, loop iteration %llu) -> units_history.bin", m_hist.size(),
				      (long long)m_hist.front().frameId, (unsigned long long)m_hist.front().loopIter);
				return;
			}
		}
	}
	m_notes.push_back(std::string(sb::kUnitsNoHistoryNote) + ": " + (m_histWhy.empty() ? std::string("none recorded") : m_histWhy));
	RNote("recorder: %s", m_notes.back().c_str());
}

// ---- issue 0364 (format version 3): the decal member -------------------------------------------------------------------------
void SceneRecorder::writeDecalSet(int64_t frameId)
{
	if(!m_decalGet || m_decalWritten)
		return;
	m_decalWritten = true;
	const DecalTapReader* r = m_decalGet();
	m_decalGen = r ? r->generation() : 0;
	if(!m_decalGen)
	{
		m_notes.push_back(Fmt("%s (frame %lld, %s): the span is drawn without decals", sb::kDecalNoSetNote, (long long)frameId,
		                      r ? "the reader has no set yet" : "no reader yet"));
		RNote("recorder: %s", m_notes.back().c_str());
		return;
	}
	// the reader's own writer (the set's image bytes as it accepted them) into a file of ours, then into the bundle
	wchar_t tmpDir[MAX_PATH] = L"";
	const DWORD n = GetTempPathW(MAX_PATH, tmpDir);
	std::wstring tmp = (n && n < MAX_PATH ? std::wstring(tmpDir) : m_dir + L"\\..\\") +
	                   sb::Widen(Fmt("c5_scene_decal_%lu_%lld.dtap", GetCurrentProcessId(), (long long)Qpc()));
	std::string why;
	std::vector<uint8_t> b;
	c5r::Status st = r->writeCurrentImage(tmp);
	if(!st.ok())
		why = "writeCurrentImage: " + st.str();
	else
	{
		HANDLE f = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		LARGE_INTEGER sz = {};
		if(f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || (uint64_t)sz.QuadPart > VPSHELL_DECAL_MAX_BYTES)
			why = Fmt("the written image cannot be read back (error %lu, %lld bytes)", GetLastError(), (long long)sz.QuadPart);
		else
		{
			b.resize((size_t)sz.QuadPart);
			DWORD got = 0;
			if(!ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) || got != b.size())
				why = Fmt("the written image reads back short (%lu of %zu bytes)", (unsigned long)got, b.size());
		}
		if(f != INVALID_HANDLE_VALUE)
			CloseHandle(f);
		DeleteFileW(tmp.c_str());
	}
	sb::DecalTapFile df;
	sb::CheckResult cr;
	if(why.empty() && !sb::ParseDecalTap(b, &df, &cr).ok())
		why = "the reader's image fails the member's checks: " + cr.line();
	if(why.empty() && df.generation != m_decalGen)
		why = Fmt("the image is generation %u, the reader's %u", df.generation, m_decalGen);
	const std::string path = why.empty() ? sb::DecalTapPath(df.loadSeq) : std::string();
	if(why.empty() && path.empty())
		why = Fmt("load %u has no member name (decals/load<4 digits>.dtap)", df.loadSeq);
	if(why.empty())
	{
		st = m_w.addMember(path, sb::MemberKind::DecalTap, b.data(), b.size());
		if(!st.ok())
			why = path + ": " + st.str();
	}
	if(!why.empty())
	{
		error("decals: the set in force not recorded: " + why);
		m_w.markIncomplete("decals: the set in force at the span's first frame was not recorded: " + why);
		return;
	}
	m_decalPath = path;
	m_written.insert(path);
	RNote("recorder: decal set generation %u (load %u, %u decals, %zu bytes) in force at the span's first frame %lld: %s", m_decalGen, df.loadSeq,
	      df.decalCount, b.size(), (long long)frameId, path.c_str());
}

void SceneRecorder::watchDecalGeneration(int64_t frameId)
{
	if(!m_decalGet || !m_decalWritten || m_decalChangeNoted)
		return;
	const DecalTapReader* r = m_decalGet();
	const uint32_t gen = r ? r->generation() : 0;
	if(gen == m_decalGen)
		return;
	m_decalChangeNoted = true;
	m_notes.push_back(Fmt("decals: decal generation changed inside the span: generation %u at the first frame, %u before frame %lld (version 1 of "
	                      "the member holds the first set only: frames from %lld on drew another set)",
	                      m_decalGen, gen, (long long)frameId, (long long)frameId));
	RNote("recorder: %s", m_notes.back().c_str());
}

void SceneRecorder::afterDraw(ID3D11DeviceContext* ctx, const SceneFrame& f, double nowMs, uint32_t w, uint32_t h,
                              ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const c5r::terrain::Overlay& bound,
                              uint32_t flags)
{
	if(m_finished || m_spanDone || !m_spanStarted)
		return;
	sb::FrameRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = f.frameId;
	r.loopIter = m_loopIter;
	r.nowMs = nowMs;
	r.width = w;
	r.height = h;
	r.flags = flags & (sb::kFrameWaterModule | sb::kFrameWaves | sb::kFrameFog);   // kFrameFog: issue 3911
	PairKey key;
	if(bound.srv)
	{
		if(m_havePair && bound.srv == m_curPair.srv)
		{
			key = KeyOf(m_curPair);
			r.overlayBound = 1;
			r.overlayGeneration = m_curPair.generation;
			r.overlayPresentCount = m_curPair.presentCount;
			r.overlayNumViews = m_curPair.numViews;
			memcpy(r.overlayProj, m_curPair.overlayProj, sizeof(r.overlayProj));
			if(!m_pairSha.count(key) && !m_pairFailed.count(key))
			{
				auto c = m_texCache.find(key);
				if(c != m_texCache.end())
				{
					writeOverlay(key, c->second);
					m_texCache.erase(c);
					m_texOrder.erase(std::remove(m_texOrder.begin(), m_texOrder.end(), key), m_texOrder.end());
				}
				else
					m_needed.insert(key);
			}
		}
		else
		{
			m_spanBroken = Fmt("frame %lld binds an overlay the recorder was not handed", (long long)f.frameId);
			error(m_spanBroken);
			m_spanDone = true;
			m_spanDoneIter = m_loopIter;
			return;
		}
	}
	// ticket 4875 (format version 9): the model tap frame in force for this frame, when it changed. Ticket 4883: taken after
	// the draw (it was in beforeDraw), because Scene::draw's ModelsScene::beginFrame chooses the tap of the frame's Present
	// (usually one Present older than the reader's newest) and the module's image is that one only from then on; before
	// the frame is appended, so that the member's index is this frame's.
	takeModelTap(f.frameId);
	const size_t idx = m_frames.size();
	m_frames.push_back(r);
	{   // ticket 0622 (format version 11): the water copy this frame was drawn with
		sb::WaterFrameRecord wr = m_waterNow;
		wr.frameId = f.frameId;
		m_waterRecs.push_back(wr);
	}
	m_cams.push_back(f.cam);
	m_framePair.push_back(key);
	m_lastFrameId = f.frameId;
	if(m_k.dumpsFrame((uint32_t)idx) && ctx)
	{
		Staged s;
		s.frameId = f.frameId;
		s.frameIndex = idx;
		s.w = w;
		s.h = h;
		std::string why;
		ID3D11Device* dev = nullptr;
		ctx->GetDevice(&dev);
		auto stage = [&](ID3D11View* v, bool colour, ID3D11Texture2D** out) {
			if(!v)
			{
				why = colour ? "no colour target" : "no depth target";
				return false;
			}
			ID3D11Resource* res = nullptr;
			v->GetResource(&res);
			ID3D11Texture2D* tex = nullptr;
			if(!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) || !tex)
			{
				SafeRelease(res);
				why = colour ? "the colour target is not a 2D texture" : "the depth target is not a 2D texture";
				return false;
			}
			res->Release();
			D3D11_TEXTURE2D_DESC d;
			tex->GetDesc(&d);
			if(d.SampleDesc.Count != 1 || d.Width < w || d.Height < h || (colour ? !Colour4Bytes(d.Format) : !Depth32Float(d.Format)))
			{
				why = Fmt("%s target %ux%u format %u, %u samples: not readable as %ux%u %s", colour ? "colour" : "depth", d.Width, d.Height,
				          (unsigned)d.Format, d.SampleDesc.Count, w, h, colour ? "4-byte pixels" : "float32");
				tex->Release();
				return false;
			}
			d.Usage = D3D11_USAGE_STAGING;
			d.BindFlags = 0;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			d.MiscFlags = 0;
			const HRESULT hr = dev ? dev->CreateTexture2D(&d, nullptr, out) : E_FAIL;
			if(FAILED(hr) || !*out)
			{
				why = Fmt("staging texture for the %s target: 0x%08X", colour ? "colour" : "depth", (unsigned)hr);
				tex->Release();
				*out = nullptr;
				return false;
			}
			ctx->CopyResource(*out, tex);
			tex->Release();
			return true;
		};
		const bool ok = stage(rtv, true, &s.colour) && stage(dsv, false, &s.depth);
		SafeRelease(dev);
		if(ok)
			m_staged.push_back(s);
		else
		{
			releaseStaged(s);
			error(Fmt("frame %lld: no dump: %s", (long long)f.frameId, why.c_str()));
		}
	}
	if(m_frames.size() >= (size_t)m_k.frames)
	{
		m_spanDone = true;
		m_spanDoneIter = m_loopIter;
		RNote("recorder: span drawn: %zu frames, %lld to %lld", m_frames.size(), (long long)m_frames.front().frameId,
		      (long long)m_frames.back().frameId);
	}
}

// ---- epic 0169, issue 4308: the shadow and the ice of a recorded frame ------------------------------------------------------
void SceneRecorder::afterDrawSeamG(ID3D11DeviceContext* ctx, const ShadowFrame* shadow, const c5r::ice::Mesh* ice, uint32_t iceGeneration,
                                   uint32_t iceLoadSeq)
{
	if(m_finished || !m_spanStarted || m_shadowRecs.size() + 1 != m_frames.size())
		return;
	sb::ShadowRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = m_frames.back().frameId;
	if(shadow)
	{
		r.generation = shadow->generation;
		r.presentCount = shadow->presentCount;
		r.quality = shadow->quality;
		r.numViews = shadow->numViews;
		r.strength = shadow->srv ? shadow->strength : 0.0f;
		memcpy(r.matrices, shadow->matrices, sizeof(r.matrices));
		// ticket 4749 (format version 10): the slot's unit-mask-dropped flag and ShadowRaw's size, which the scene's
		// unitShadowStep read for this frame (the player hands them back, so that it merges the server's unit mask as live)
		if(shadow->unitMaskDropped)
			r.flags |= sb::kUnitMaskDropped;
		r.rawWidth = shadow->rawWidth;
		r.rawHeight = shadow->rawHeight;
		if(!shadow->srv)
			r.flags |= sb::kShadowQ0;
		else
		{
			r.flags |= sb::kShadowBound;
			r.dxgiFormat = shadow->format;
			r.width = shadow->width;
			r.height = shadow->height;
			StagedShadow s;
			s.index = m_shadowRecs.size();
			std::string why;
			ID3D11Resource* res = nullptr;
			shadow->srv->GetResource(&res);
			ID3D11Texture2D* tex = nullptr;
			if(!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) || !tex)
				why = "the shadow view's resource is not a 2D texture";
			SafeRelease(res);
			ID3D11Device* dev = nullptr;
			if(tex)
			{
				D3D11_TEXTURE2D_DESC d;
				tex->GetDesc(&d);
				if(d.Format != DXGI_FORMAT_R16_UNORM && d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
					why = Fmt("the shadow map's format %u is not 56 or 10", (unsigned)d.Format);
				else
				{
					s.w = d.Width;
					s.h = d.Height;
					s.format = (uint32_t)d.Format;
					r.width = d.Width;
					r.height = d.Height;
					r.dxgiFormat = (uint32_t)d.Format;
					d.MipLevels = 1;
					d.ArraySize = 1;
					d.Usage = D3D11_USAGE_STAGING;
					d.BindFlags = 0;
					d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					d.MiscFlags = 0;
					ctx->GetDevice(&dev);
					const HRESULT hr = dev ? dev->CreateTexture2D(&d, nullptr, &s.tex) : E_FAIL;
					if(FAILED(hr) || !s.tex)
						why = Fmt("staging texture for the shadow map: 0x%08X", (unsigned)hr);
					else
						ctx->CopySubresourceRegion(s.tex, 0, 0, 0, 0, tex, 0, nullptr);
				}
			}
			SafeRelease(dev);
			SafeRelease(tex);
			if(why.empty())
				m_shStaged.push_back(s);
			else
			{
				SafeRelease(s.tex);
				error(Fmt("frame %lld: shadow map not recorded: %s", (long long)r.frameId, why.c_str()));
			}
		}
	}
	if(ice && iceGeneration)
	{
		r.flags |= sb::kIceDrawn;
		r.iceGeneration = iceGeneration;
		r.iceLoadSeq = iceLoadSeq;
		if(!m_iceWritten.count(iceGeneration))
		{
			std::vector<uint8_t> b;
			sb::EncodeIceMesh(*ice, iceGeneration, iceLoadSeq, &b);
			const std::string p = sb::IceMeshPath(iceGeneration);
			const c5r::Status st = m_w.addMember(p, sb::MemberKind::IceMesh, b.data(), b.size());
			if(st.ok())
			{
				m_iceWritten.insert(iceGeneration);
				m_written.insert(p);
				m_iceFiles++;
				RNote("recorder: %s written (%zu chunks, %zu bytes)", p.c_str(), ice->chunks.size(), b.size());
			}
			else
				error(Fmt("%s: %s", p.c_str(), st.str().c_str()));
		}
	}
	m_shadowRecs.push_back(r);
}

void SceneRecorder::drawFailed(int64_t frameId, const std::string& why)
{
	if(m_finished || m_spanDone || !m_spanStarted)
		return;
	m_spanBroken = Fmt("the draw of frame %lld failed (%s): the span ends there", (long long)frameId, why.c_str());
	error(m_spanBroken);
	m_spanDone = true;
	m_spanDoneIter = m_loopIter;
}

void SceneRecorder::afterEnd(ID3D11DeviceContext* ctx)
{
	// issue 4308: the staged shadow maps, tight rows, deduplicated by content
	std::vector<StagedShadow> shs;
	shs.swap(m_shStaged);
	for(StagedShadow& s : shs)
	{
		const uint32_t bpp = s.format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 2;
		std::vector<uint8_t> px((size_t)s.w * s.h * bpp);
		D3D11_MAPPED_SUBRESOURCE m;
		const HRESULT hr = ctx && !m_finished ? ctx->Map(s.tex, 0, D3D11_MAP_READ, 0, &m) : E_FAIL;
		if(SUCCEEDED(hr))
		{
			for(uint32_t y = 0; y < s.h; ++y)
				memcpy(&px[(size_t)y * s.w * bpp], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)s.w * bpp);
			ctx->Unmap(s.tex, 0);
			std::vector<uint8_t> file;
			sb::EncodeShadowTexels(s.w, s.h, s.format, px.data(), px.size(), &file);
			const std::string hex = sb::Sha256Hex(file.data(), file.size());
			const std::string p = sb::ShadowTexPath(hex);
			c5r::Status st = c5r::Status::Ok();
			if(m_shTexWritten.count(hex))
				m_shDeduped++;
			else
			{
				st = m_w.addMember(p, sb::MemberKind::ShadowTexels, file.data(), file.size());
				if(st.ok())
				{
					m_shTexWritten.insert(hex);
					m_written.insert(p);
					m_shFiles++;
				}
			}
			if(st.ok() && s.index < m_shadowRecs.size())
				HexToBytes(hex, m_shadowRecs[s.index].texelSha256);
			else if(!st.ok())
				error(Fmt("%s: %s", p.c_str(), st.str().c_str()));
		}
		else if(!m_finished)
			error(Fmt("shadow map of record %zu: Map 0x%08X", s.index, (unsigned)hr));
		SafeRelease(s.tex);
	}
	if(m_staged.empty())
		return;
	std::vector<Staged> st;
	st.swap(m_staged);
	for(Staged& s : st)
	{
		std::string why;
		std::vector<uint8_t> px((size_t)s.w * s.h * 4), dep((size_t)s.w * s.h * 4);
		auto read = [&](ID3D11Texture2D* t, std::vector<uint8_t>& out) {
			D3D11_MAPPED_SUBRESOURCE m;
			const HRESULT hr = ctx ? ctx->Map(t, 0, D3D11_MAP_READ, 0, &m) : E_FAIL;
			if(FAILED(hr))
			{
				why = Fmt("Map 0x%08X", (unsigned)hr);
				return false;
			}
			for(uint32_t y = 0; y < s.h; ++y)
				memcpy(&out[(size_t)y * s.w * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)s.w * 4);
			ctx->Unmap(t, 0);
			return true;
		};
		bool ok = !m_finished && read(s.colour, px) && read(s.depth, dep);
		if(ok)
		{
			std::vector<uint8_t> bmp;
			sb::EncodeDumpColour(s.w, s.h, px.data(), &bmp);
			const std::string pc = Fmt("dumps/%lld.bmp", (long long)s.frameId), pd = Fmt("dumps/%lld.depth.f32", (long long)s.frameId);
			c5r::Status r1 = m_w.addMember(pc, sb::MemberKind::DumpColour, bmp.data(), bmp.size());
			c5r::Status r2 = r1.ok() ? m_w.addMember(pd, sb::MemberKind::DumpDepth, dep.data(), dep.size()) : r1;
			if(r1.ok() && r2.ok())
			{
				m_written.insert(pc);
				m_written.insert(pd);
				m_frames[s.frameIndex].flags |= sb::kFrameDumped;
				m_dumped.push_back(s.frameId);
				RNote("recorder: frame %lld dumped (%ux%u, colour and depth)", (long long)s.frameId, s.w, s.h);
			}
			else
				why = (r1.ok() ? r2 : r1).str();
			ok = r1.ok() && r2.ok();
		}
		if(!ok && !m_finished)
			error(Fmt("frame %lld: dump not written: %s", (long long)s.frameId, why.c_str()));
		releaseStaged(s);
	}
	std::sort(m_dumped.begin(), m_dumped.end());
}

// ---- finishing ------------------------------------------------------------------------------------------------------------
void SceneRecorder::step(const RecorderCounters& c)
{
	if(m_finished || !m_spanDone || !m_staged.empty() || !m_shStaged.empty())
		return;
	if(overlaysResolved())
		doFinish(c, "");
	else if(m_loopIter - m_spanDoneIter > kFinishAfterIters)
		doFinish(c, Fmt("the texels of %zu overlay pair(s) bound in the span did not arrive within %llu loop iterations", m_needed.size(),
		                (unsigned long long)kFinishAfterIters));
}

void SceneRecorder::finish(const RecorderCounters& c, const char* why)
{
	if(m_finished)
		return;
	for(Staged& s : m_staged)
	{
		error(Fmt("frame %lld: dump not written: the renderer stopped first", (long long)s.frameId));
		releaseStaged(s);
	}
	m_staged.clear();
	for(StagedShadow& s : m_shStaged)   // issue 4308
	{
		error(Fmt("shadow map of record %zu not written: the renderer stopped first", s.index));
		SafeRelease(s.tex);
	}
	m_shStaged.clear();
	std::string w;
	if(!overlaysResolved())
		w = Fmt("the texels of %zu overlay pair(s) bound in the span did not arrive before the end (%s)", m_needed.size(), why ? why : "-");
	doFinish(c, w);
}

void SceneRecorder::doFinish(const RecorderCounters& c, const std::string& whyIn)
{
	m_finished = true;
	std::string why = whyIn;
	writeFeedPending(-1);
	const bool writerFailed = m_w.incomplete();   // a write failed: the writer's own reason is the bundle's (first wins)
	if(!m_spanStarted && why.empty())
		why = Fmt("the span never started (no draw reached published frame %d)", m_k.after);
	if(why.empty() && m_dumped.size() < 2)
		why = Fmt("%zu dumped frame(s) of %u chosen: at least two are needed%s%s", m_dumped.size(), m_k.dumpCount(),
		          m_spanBroken.empty() ? "" : "; ", m_spanBroken.c_str());
	if(why.empty() && !m_feedDrains && !m_feedTables)
		why = "the feed recorded nothing (no drain returned true, no CopyPlots table): feed.bin would be empty";
	if(m_frames.size() < (size_t)m_k.frames)
		m_notes.push_back(Fmt("recorder: short span: %zu of %d frames%s%s", m_frames.size(), m_k.frames, m_spanBroken.empty() ? "" : ": ",
		                      m_spanBroken.c_str()));
	for(size_t i = 0; i < m_frames.size(); ++i)
		if(m_frames[i].overlayBound)
		{
			auto s = m_pairSha.find(m_framePair[i]);
			if(s != m_pairSha.end())
				HexToBytes(s->second, m_frames[i].overlaySha256);
			else if(why.empty())
			{
				auto f = m_pairFailed.find(m_framePair[i]);
				why = Fmt("frame %lld: its overlay (generation %u) has no texels: %s", (long long)m_frames[i].frameId, m_framePair[i].generation,
				          f != m_pairFailed.end() ? f->second.c_str() : "never arrived");
			}
		}
	c5r::Status st = c5r::Status::Ok();
	if(!m_w.incomplete())
	{
		if(m_spanStarted)
			writeUnitsMembers();   // issue 0440 (format version 5): units/gen<...>.db and units_history.bin, or their gap: notes
		// ticket 0403 (format version 6): the FOWLevel-3 particles' state at the span's first frame (particles.bin); issue 0440
		// (MC #3003): the server drew particles in the span and the bundle has no state for them: the gap note
		if(m_spanStarted && m_havePartState)
		{
			sb::FogParticlesFile pf;
			pf.flags = m_partState.spawned ? sb::kFogParticlesSpawned : 0;
			pf.seed = m_partState.seed;
			pf.worldW = m_partState.worldW;
			pf.worldH = m_partState.worldH;
			pf.state = m_partState.sim;
			std::vector<uint8_t> b;
			sb::EncodeFogParticles(pf, &b);
			st = m_w.addMember("particles.bin", sb::MemberKind::FogParticles, b.data(), b.size());
			RNote("recorder: particles.bin %zu bytes (%s, %lld particle frames in the span)", b.size(),
			      m_partState.spawned ? "spawned" : "not spawned", m_fog ? (long long)(m_fog->particleFrames - m_fogPartFrames0) : 0LL);
		}
		else if(m_spanStarted && m_fog && (m_fogPartOnAtSpan || m_fog->particleFrames > m_fogPartFrames0))
		{
			m_notes.push_back(Fmt("%s: the server drew the FOWLevel-3 particles (%lld particle frames from the span's start, on at its first frame %d) "
			                      "and their state is not in the bundle (%s): the player draws none",
			                      sb::kFogParticlesNote, (long long)(m_fog->particleFrames - m_fogPartFrames0), m_fogPartOnAtSpan ? 1 : 0,
			                      m_fogPartOnAtSpan ? m_partWhy.c_str() : "off at the span's first frame"));
			RNote("recorder: %s", m_notes.back().c_str());
		}
		// ticket 0495 (format version 8): the trees' state (forest.bin) and the near pairs' (near.bin) at the span's first
		// frame, or their gap notes
		if(m_spanStarted && m_forestGet && m_c.renderConfig["render_forest"] == "1")
		{
			if(m_haveForest && st.ok())
			{
				std::vector<uint8_t> b;
				sb::EncodeForestState(m_forest, &b);
				st = m_w.addMember("forest.bin", sb::MemberKind::ForestState, b.data(), b.size());
				RNote("recorder: forest.bin %zu bytes", b.size());
			}
			else if(!m_haveForest)
			{
				m_notes.push_back(Fmt("%s: the trees drew and their state is not in the bundle (%s): the player draws none", sb::kForestNote,
				                      m_forestWhy.c_str()));
				RNote("recorder: %s", m_notes.back().c_str());
			}
			if(!m_forestCutNote.empty())
				m_notes.push_back(m_forestCutNote);
		}
		if(m_spanStarted && m_farmOnAtSpan && !m_farmNote.empty() && m_farmNote != "-")   // ticket 4852
			m_notes.push_back(m_farmNote);
		if(m_spanStarted && m_roadsOnAtSpan && !m_roadsNote.empty() && m_roadsNote != "-")   // ticket 4934
			m_notes.push_back(m_roadsNote);
		if(m_spanStarted && m_nearOnAtSpan)
		{
			if(m_haveNear && st.ok())
			{
				std::vector<uint8_t> b;
				sb::EncodeNearPairs(m_near, &b);
				st = m_w.addMember("near.bin", sb::MemberKind::NearPairs, b.data(), b.size());
				RNote("recorder: near.bin %zu bytes", b.size());
			}
			else if(!m_haveNear)
			{
				m_notes.push_back(Fmt("%s: the near-camera pairs were on and their state is not in the bundle (%s)", sb::kNearNote, m_nearWhy.c_str()));
				RNote("recorder: %s", m_notes.back().c_str());
			}
		}
		if(st.ok())
			st = m_w.appendStart("frames.bin", sb::MemberKind::Frames);
		if(st.ok() && !m_frames.empty())
			st = m_w.append("frames.bin", m_frames.data(), m_frames.size() * sizeof(sb::FrameRecord));
		if(st.ok())
			st = m_w.appendStart("cameras.bin", sb::MemberKind::CameraBlocks);
		if(st.ok() && !m_cams.empty())
			st = m_w.append("cameras.bin", m_cams.data(), m_cams.size() * sizeof(VPShellCamera));
		if(st.ok())
		{
			std::vector<uint8_t> b;
			sb::EncodeWaterClock(m_clock, &b);
			st = m_w.addMember("water_clock.bin", sb::MemberKind::WaterClock, b.data(), b.size());
		}
		// issue 4308: shadow.bin, one record per frame (a frame without afterDrawSeamG: an empty record; a bound map
		// without its texels makes the bundle incomplete)
		if(st.ok() && !m_shadowRecs.empty())
		{
			for(size_t i = 0; i < m_shadowRecs.size() && why.empty(); ++i)
			{
				bool zero = true;
				for(uint8_t c2 : m_shadowRecs[i].texelSha256)
					zero = zero && c2 == 0;
				if((m_shadowRecs[i].flags & sb::kShadowBound) && zero)
					why = Fmt("frame %lld: its shadow map has no texels", (long long)m_shadowRecs[i].frameId);
			}
			while(m_shadowRecs.size() < m_frames.size())
			{
				sb::ShadowRecord e;
				memset(&e, 0, sizeof(e));
				e.frameId = m_frames[m_shadowRecs.size()].frameId;
				m_shadowRecs.push_back(e);
			}
			st = m_w.appendStart("shadow.bin", sb::MemberKind::Shadow);
			if(st.ok())
				st = m_w.append("shadow.bin", m_shadowRecs.data(), m_shadowRecs.size() * sizeof(sb::ShadowRecord));
		}
		// ticket 0622 (format version 11): water_frames.bin, one record per frame (the copy in force at its draw)
		if(st.ok())
			st = m_w.appendStart("water_frames.bin", sb::MemberKind::WaterFrames);
		if(st.ok() && !m_waterRecs.empty())
			st = m_w.append("water_frames.bin", m_waterRecs.data(), m_waterRecs.size() * sizeof(sb::WaterFrameRecord));
		if(st.ok() && m_haveFogClock)   // issue 3911
		{
			std::vector<uint8_t> b;
			sb::EncodeFogClock(m_fogClock, &b);
			st = m_w.addMember("fog_clock.bin", sb::MemberKind::FogClock, b.data(), b.size());
		}
		if(!st.ok())
			error("the span's members: " + st.str());
	}
	sb::Manifest m;
	m.bundleId = m_bundleId;
	m.created = m_created;
	m.recorderKeys = m_k.given;
	if(m_c.hashBinaries)
	{
		wchar_t self[MAX_PATH] = L"";
		GetModuleFileNameW(nullptr, self, MAX_PATH);
		std::string hex;
		uint64_t n = 0;
		if(sb::Sha256FileHex(self, &hex, &n).ok())
		{
			m.serverExeSha256 = hex;
			m.pins["server_sha256"] = hex;
		}
		if(!m_c.exe.empty() && sb::Sha256FileHex(m_c.exe, &hex, &n).ok())
			m.pins["exe_sha256"] = hex;
	}
	m.saveName = m_saveName;
	m.mapW = m_mapW;
	m.mapH = m_mapH;
	m.loadSeq = m_loadSeq;
	if(!m_c.install.empty())
	{
		const c5r::Status as = sb::ComputeAssetSet(m_c.install, m_machine, &m);
		if(!as.ok())
			m_notes.push_back("recorder: asset set not computed: " + as.str());
	}
	else
	{
		m.machine = m_machine;
		m.assetWhy = "no install given to the recorder";
	}
	m.adapterLuid = m_luid;
	m.adapterDescription = m_adapterDesc;
	m.adapterDriver = m_adapterDriver;
	takeLiveTerrain();   // epic 0465, ticket 4907: render_live_terrain and its journal (srv_liveterrain_bundle.cpp)
	m.renderConfig = m_c.renderConfig;
	m.firstFrameId = m_frames.empty() ? 0 : m_frames.front().frameId;
	m.frameCount = (uint32_t)m_frames.size();
	m.dumpedFrames = m_dumped;
	m.versionNotes = m_c.versionNotes;
	m.versionNotes.insert(m.versionNotes.end(), m_notes.begin(), m_notes.end());
	m.counters["water_refused"] = c.waterRefused;
	m.counters["overlay_refused"] = c.overlayRefused;
	m.counters["overlay_stale"] = c.overlayStale;
	m.counters["feed_overflow_events"] = m_feedOverflowEvents;
	m.counters["record_errors"] = m_recordErrors;
	if(!why.empty())
		m_w.markIncomplete(why);
	st = m_w.finish(m);
	m_finishWhy = st.ok() ? std::string() : (why.empty() || writerFailed ? st.str() : why);
	if(st.ok())
		RNote("recorder: bundle '%ls' finished: %zu frames (%lld..%lld), %zu dumped, %lld feed drains and %lld tables, %lld overlay file(s) "
		      "(%lld deduplicated), %lld record error(s)",
		      m_dir.c_str(), m_frames.size(), (long long)m.firstFrameId, m_frames.empty() ? 0LL : (long long)m_frames.back().frameId,
		      m_dumped.size(), (long long)m_feedDrains, (long long)m_feedTables, (long long)m_ovlFiles, (long long)m_ovlDeduped,
		      (long long)m_recordErrors);
	else
		RNote("LOUD: recorder: bundle '%ls' INCOMPLETE (scene.json says so; the checker refuses it): %s", m_dir.c_str(), m_finishWhy.c_str());
}

// ---- epic 0169, issue 4308: the player's side (srv_scene_recorder.h) -------------------------------------------------------
namespace {
std::string HexOf(const uint8_t s[32])
{
	static const char hx[] = "0123456789abcdef";
	std::string full(64, '0');
	for(int i = 0; i < 32; ++i)
	{
		full[2 * i] = hx[s[i] >> 4];
		full[2 * i + 1] = hx[s[i] & 15];
	}
	return full;
}

class FileShadowSource : public ShadowSource, public FileSeamGControl
{
public:
	~FileShadowSource() override
	{
		for(auto& kv : m_srv)
			SafeRelease(kv.second);
	}
	bool build(ID3D11Device* dev, const std::vector<sb::ShadowRecord>& recs, const std::map<std::string, std::vector<uint8_t>>& files,
	           std::string* why)
	{
		m_recs = recs;
		for(const sb::ShadowRecord& r : recs)
		{
			if(!(r.flags & sb::kShadowBound))
				continue;
			const std::string hex = HexOf(r.texelSha256);
			if(m_srv.count(hex))
				continue;
			auto f = files.find(hex);
			if(f == files.end())
			{
				*why = Fmt("frame %lld: shadow map %s is missing", (long long)r.frameId, sb::ShadowTexPath(hex).c_str());
				return false;
			}
			sb::ShadowTexFile tf;
			sb::CheckResult cr;
			if(!sb::ParseShadowTexels(f->second, &tf, &cr).ok())
			{
				*why = sb::ShadowTexPath(hex) + ": " + cr.line();
				return false;
			}
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = tf.width;
			d.Height = tf.height;
			d.MipLevels = d.ArraySize = 1;
			d.Format = (DXGI_FORMAT)tf.dxgiFormat;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_IMMUTABLE;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA s = {f->second.data() + tf.texelOffset, tf.width * tf.bytesPerTexel, 0};
			ID3D11Texture2D* t = nullptr;
			ID3D11ShaderResourceView* v = nullptr;
			HRESULT hr = dev ? dev->CreateTexture2D(&d, &s, &t) : E_FAIL;
			if(SUCCEEDED(hr))
				hr = dev->CreateShaderResourceView(t, nullptr, &v);
			SafeRelease(t);
			if(FAILED(hr) || !v)
			{
				*why = Fmt("%s: the device refused the texture (0x%08X)", sb::ShadowTexPath(hex).c_str(), (unsigned)hr);
				return false;
			}
			m_srv[hex] = v;
		}
		return true;
	}
	void seekFrame(uint32_t k) override { m_k = k; }
	bool acquire(ShadowFrame& out) override
	{
		out = ShadowFrame();
		if(m_k >= m_recs.size())
			return false;
		const sb::ShadowRecord& r = m_recs[m_k];
		if(!(r.flags & (sb::kShadowBound | sb::kShadowQ0)))
			return false;
		out.srv = (r.flags & sb::kShadowBound) ? m_srv[HexOf(r.texelSha256)] : nullptr;
		out.format = r.dxgiFormat;
		out.width = r.width;
		out.height = r.height;
		out.numViews = r.numViews;
		memcpy(out.matrices, r.matrices, sizeof(out.matrices));
		out.strength = r.strength;
		out.presentCount = r.presentCount;
		out.generation = r.generation;
		out.quality = r.quality;
		out.slot = 0;
		// ticket 4749: as the live slot said (format version 10; a version-9 record reads clear and 0 x 0, as before)
		out.unitMaskDropped = (r.flags & sb::kUnitMaskDropped) != 0;
		out.rawWidth = r.rawWidth;
		out.rawHeight = r.rawHeight;
		return true;
	}
	ShadowTapReader::State state() const override
	{
		return m_k < m_recs.size() && (m_recs[m_k].flags & (sb::kShadowBound | sb::kShadowQ0)) ? ShadowTapReader::State::Active
		                                                                                     : ShadowTapReader::State::Waiting;
	}
	uint32_t failReason() const override { return 0; }
	std::string why() const override { return std::string(); }

private:
	std::vector<sb::ShadowRecord> m_recs;
	std::map<std::string, ID3D11ShaderResourceView*> m_srv;
	uint32_t m_k = 0;
};

class FileIceSource : public IceSource, public FileSeamGControl
{
public:
	std::vector<sb::ShadowRecord> recs;
	std::map<uint32_t, std::shared_ptr<const c5r::ice::Mesh>> meshes;
	uint32_t k = 0, cur = 0, curLoad = 0;
	void seekFrame(uint32_t kk) override { k = kk; }
	bool poll(uint32_t have) override
	{
		if(k >= recs.size() || !(recs[k].flags & sb::kIceDrawn))
			return false;
		const uint32_t g = recs[k].iceGeneration;
		if(g == have || !meshes.count(g))
			return false;
		cur = g;
		curLoad = recs[k].iceLoadSeq;
		return true;
	}
	std::shared_ptr<const c5r::ice::Mesh> mesh() const override
	{
		auto i = meshes.find(cur);
		return i == meshes.end() ? nullptr : i->second;
	}
	uint32_t generation() const override { return cur; }
	uint32_t loadSeq() const override { return curLoad; }
	IceTapReader::State state() const override { return cur ? IceTapReader::State::Active : IceTapReader::State::Waiting; }
	bool installFailure() const override { return false; }
	std::string why() const override { return std::string(); }
};
}  // namespace

std::unique_ptr<ShadowSource> MakeFileShadowSource(ID3D11Device* dev, const std::vector<sb::ShadowRecord>& recs,
                                                   const std::map<std::string, std::vector<uint8_t>>& texelFiles, FileSeamGControl** control,
                                                   std::string* why)
{
	std::unique_ptr<FileShadowSource> s(new FileShadowSource());
	std::string w;
	if(!s->build(dev, recs, texelFiles, &w))
	{
		if(why)
			*why = w;
		return nullptr;
	}
	if(control)
		*control = s.get();
	return std::unique_ptr<ShadowSource>(s.release());
}

std::unique_ptr<IceSource> MakeFileIceSource(const std::vector<sb::ShadowRecord>& recs,
                                             const std::map<uint32_t, std::shared_ptr<const c5r::ice::Mesh>>& meshes, FileSeamGControl** control,
                                             std::string* why)
{
	for(const sb::ShadowRecord& r : recs)
		if((r.flags & sb::kIceDrawn) && !meshes.count(r.iceGeneration))
		{
			if(why)
				*why = Fmt("frame %lld drew ice mesh %s, which is missing", (long long)r.frameId, sb::IceMeshPath(r.iceGeneration).c_str());
			return nullptr;
		}
	std::unique_ptr<FileIceSource> s(new FileIceSource());
	s->recs = recs;
	s->meshes = meshes;
	if(control)
		*control = s.get();
	return std::unique_ptr<IceSource>(s.release());
}

bool LoadSeamGMembers(ID3D11Device* dev, const std::wstring& bundleDir, const sb::Manifest& m, std::unique_ptr<ShadowSource>* shadow,
                      FileSeamGControl** shadowCtl, std::unique_ptr<IceSource>* ice, FileSeamGControl** iceCtl,
                      std::vector<sb::ShadowRecord>* recs, std::string* note, std::string* why)
{
	shadow->reset();
	ice->reset();
	recs->clear();
	if(!m.members.count("shadow.bin"))
	{
		// format version 3 (issue 0364): every bundle is recorded after 4308; one without shadow.bin was recorded with neither
		auto on = [&](const char* k) {
			auto it = m.renderConfig.find(k);
			return it != m.renderConfig.end() && it->second == "1";
		};
		if(on("render_shadow") || on("render_ice"))
		{
			*why = std::string("shadow.bin: missing while render_config has ") + (on("render_shadow") ? "render_shadow=1" : "render_ice=1") +
			       " (format version 3 carries the shadow and ice members, contract 2.12)";
			return false;
		}
		*note = "no shadow.bin (render_shadow and render_ice off in the recording): no shadow and no ice are drawn";
		return true;
	}
	sb::CheckResult cr;
	std::vector<uint8_t> b;
	if(!sb::ReadMember(bundleDir, m, "shadow.bin", &b, &cr).ok() || !sb::ParseShadow(b, recs, &cr, m.formatVersion).ok())   // 4749: form by format
	{
		*why = "shadow.bin: " + cr.line();
		return false;
	}
	std::map<std::string, std::vector<uint8_t>> tex;
	std::map<uint32_t, std::shared_ptr<const c5r::ice::Mesh>> meshes;
	for(const auto& kv : m.members)
	{
		if(kv.second.kind != sb::MemberKind::ShadowTexels && kv.second.kind != sb::MemberKind::IceMesh)
			continue;
		std::vector<uint8_t> f;
		if(!sb::ReadMember(bundleDir, m, kv.first, &f, &cr).ok())
		{
			*why = kv.first + ": " + cr.line();
			return false;
		}
		if(kv.second.kind == sb::MemberKind::ShadowTexels)
			tex[kv.second.sha256] = std::move(f);
		else
		{
			std::shared_ptr<c5r::ice::Mesh> mesh(new c5r::ice::Mesh());
			uint32_t gen = 0, load = 0;
			if(!sb::ParseIceMesh(f, mesh.get(), &gen, &load, &cr).ok())
			{
				*why = kv.first + ": " + cr.line();
				return false;
			}
			meshes[gen] = mesh;
		}
	}
	std::string w;
	*shadow = MakeFileShadowSource(dev, *recs, tex, shadowCtl, &w);
	if(!*shadow)
	{
		*why = "shadow: " + w;
		return false;
	}
	*ice = MakeFileIceSource(*recs, meshes, iceCtl, &w);
	if(!*ice)
	{
		*why = "ice: " + w;
		return false;
	}
	size_t unitMask = 0;   // ticket 4749: frames whose slot said the EXE's unit mask was dropped (the player merges the server's)
	for(const sb::ShadowRecord& r : *recs)
		unitMask += (r.flags & sb::kUnitMaskDropped) ? 1 : 0;
	*note = Fmt("shadow.bin: %zu records, %zu shadow map file(s), %zu ice mesh(es), %zu with the unit mask dropped (form %u)",
	            recs->size(), tex.size(), meshes.size(), unitMask, sb::MemberKindVersionFor(sb::MemberKind::Shadow, m.formatVersion));
	return true;
}

std::string SceneRecorder::summary() const
{
	return Fmt("recorder: %s, span %s, frames %zu of %d, dumped %zu, fields %lld, water %lld, feed drains %lld tables %lld overflow events "
	           "%lld, overlay pairs %lld texels %lld files %lld deduplicated %lld, shadow records %zu files %lld deduplicated %lld, ice files %lld, "
	           "record errors %lld%s%s",
	           m_finished ? (m_finishWhy.empty() ? "finished" : "INCOMPLETE") : "recording",
	           m_spanDone ? "drawn" : m_spanStarted ? "running" : "waiting", m_frames.size(), m_k.frames, m_dumped.size(),
	           (long long)m_fieldCopies, (long long)m_waterCopies, (long long)m_feedDrains, (long long)m_feedTables,
	           (long long)m_feedOverflowEvents, (long long)m_ovlPairs, (long long)m_ovlTexels, (long long)m_ovlFiles, (long long)m_ovlDeduped,
	           m_shadowRecs.size(), (long long)m_shFiles, (long long)m_shDeduped, (long long)m_iceFiles, (long long)m_recordErrors,
	           m_finishWhy.empty() ? "" : ": ", m_finishWhy.c_str());
}

}  // namespace srvrender
