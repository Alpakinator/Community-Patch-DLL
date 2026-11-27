// srv_models_scene.cpp - the server's model module (epic 0561, ticket 4875). Contract: srv_models_scene.h; research/
// 05-graphics-composition/city-renderer.md 11. Written 2026-11-18 for this repository (GPL-3.0-or-later).
#include "srv_models_scene.h"
#include "srv_model_tap.h"
#include "srv_model_geom.h"
#include "../cities/srv_models_mode.h"
#include "feed/srv_feed.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <thread>

namespace srvmodels {

namespace {

using srvrender::Qpc;
using srvrender::QpcFreq;
using srvrender::QpcToMs;
using srvrender::RNote;

ModelLayerFactory g_layerFactory = nullptr;

void ModeLog(void*, const char* line) { RNote("%s", line); }

bool SameSet(const std::vector<VPModelSetKey>& a, const std::vector<VPModelSetKey>& b)
{
	return a.size() == b.size() && (a.empty() || memcmp(a.data(), b.data(), a.size() * sizeof(VPModelSetKey)) == 0);
}

// Ticket 0709: the keep's options from the module's, with the test switch C5_RENDER_MODELS_KEEP ("0" off, anything else
// on) over them; envSet: the variable was set.
// Ticket 0789: with ModelsOptions::store the store's rules (StoreKeepOptions), else 0709's keep.
KeepOptions KeepOptionsOf(const ModelsOptions& o, bool& envSet)
{
	KeepOptions k = StoreKeepOptions(o.keepCulled, o.store, o.keepMaxAgePresents, o.storeRefuteTaps, o.storeMaxEntries);
	char v[16] = {};
	const DWORD n = GetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", v, (DWORD)sizeof(v));
	envSet = n > 0 && n < sizeof(v);
	if(envSet)
		k.on = strcmp(v, "0") != 0;
	return k;
}

class ModelsSceneImpl : public ModelsScene
{
public:
	~ModelsSceneImpl() override { joinOpen(); }

	void configure(const ModelsOptions& o) override
	{
		m_opt = o;
		const bool exe = o.source == kModelsSourceExe;
		m_mode.setBounds(o.noFrameMs, o.holdMs, o.installMs);
		m_mode.setOpenBounds(o.openStallMs, o.openMaxMs);   // ticket 4885
		m_mode.configure(exe && o.cities, exe && o.landmarks, (uint32_t)GetCurrentProcessId(), QpcFreq(), &ModeLog, nullptr);
		if(m_block)
			m_mode.attach(m_block);
		m_wantBits = (exe && o.cities ? VPMODE_CITIES : 0u) | (exe && o.landmarks ? VPMODE_LANDMARKS : 0u);
		if(m_reader)
			m_reader->setWantBits(m_wantBits);
		if(o.any() && !exe && !m_ownSaid)
		{
			m_ownSaid = true;
			RNote("LOUD: models: render_models_source=own: an empty scene (the own layout is parked: tickets 4880 / 4881); the "
			      "model bits stay 0 and the EXE draws its cities and landmarks");
		}
		if(m_layer)
			m_layer->setDrawAll(o.drawAll);
		// ticket 0709
		bool envSet = false;
		m_keep = KeepOptionsOf(o, envSet);
		if(m_layer)
			m_layer->setKeep(m_keep);
		if(o.any() && exe && (!m_keepSaid || m_keepSaidOn != m_keep.on || m_keepSaidStore != m_keep.store))
		{
			m_keepSaid = true;
			m_keepSaidOn = m_keep.on;
			m_keepSaidStore = m_keep.store;
			if(m_keep.store)
				RNote("models: the model store (ticket 0789: every instance seen is kept until R3's evidence, %d complete taps, "
				      "removes it; both passes; the hand-over records R5; at most %llu entries per pass) is %s%s",
				      m_keep.refuteTaps, (unsigned long long)m_keep.maxEntries, m_keep.on ? "on" : "OFF",
				      envSet ? " by C5_RENDER_MODELS_KEEP" : "");
			else
				RNote("models: the keep of instances the EXE culled at the drawn tap's camera (ticket 0709) is %s%s (at most %lld "
				      "Presents old; the model store is off: [Renderer] ModelStore = 0 or render_models_store=0)",
				      m_keep.on ? "on" : "OFF", envSet ? " by C5_RENDER_MODELS_KEEP" : "", (long long)m_keep.maxAgePresents);
		}
		// MC #6166: the game database copy is made at each load whichever module asks (units or this one)
		SrvFeed::SetModelDatabaseWanted(exe && o.any());
	}
	void setLayer(std::unique_ptr<ModelLayer> layer) override
	{
		joinOpen();
		m_layer = std::move(layer);
		m_layerTried = true;
		m_ownGiven = 0;   // ticket 0789: the new layer gets the ownership at the next poll
		if(m_layer)
		{
			m_layer->setDrawAll(m_opt.drawAll);
			m_layer->setKeep(m_keep);   // ticket 0709
		}
	}
	void attachMode(VPShellMode* block) override
	{
		m_block = block;
		m_mode.attach(block);
	}
	void setExePid(uint32_t pid) override
	{
		if(!pid || pid == m_pid || !m_opt.any() || m_opt.source != kModelsSourceExe)
			return;
		m_pid = pid;
		m_reader.reset(new ModelTapReader(pid));
		m_reader->setValidator(&ModelsSceneImpl::Validate, this);
		m_reader->setWantBits(m_wantBits);
		m_pub.reset(new ModelSetPublisher(pid));
		// ticket 4879: the CPU-built meshes (walls, the Great Wall, bridges) are city art: their buffers only with cities on
		m_geom.reset((m_wantBits & VPMODE_CITIES) ? new ModelGeomReader(pid) : nullptr);
		m_published.clear();
		m_setAcked = false;
		m_ackedBits = 0;
		RNote("models: the EXE's pid %u: the tap reader and the identity set publisher are made (want bits 0x%X)", pid, m_wantBits);
	}
	void setDatabase(const std::wstring& path, uint32_t gen) override
	{
		if(path.empty() || gen == m_dbGen)
			return;
		m_dbGen = gen;
		m_dbPath = path;
	}
	void setDevice(ID3D11Device* dev) override
	{
		m_dev = dev;
		m_deviceOk = false;
		m_deviceTried = false;
		if(!dev && m_layer && m_openDone.load())
			m_layer->setDevice(nullptr);
	}

	bool poll(int64_t qpc) override
	{
		if(!m_opt.any() || m_opt.source != kModelsSourceExe || m_failed)
			return false;
		m_c.polls++;
		bool changed = false;
		{
			// the game database copy (srv_remote_main.cpp, made at each load while ModelDatabaseWanted, MC #6166)
			std::wstring db;
			uint32_t gen = 0;
			if(m_dbPath.empty() && SrvFeed::GetUnitDatabase(db, gen))
				setDatabase(db, gen);
		}
		startLayer();
		finishOpen();
		sampleOpen(qpc);   // ticket 4885
		if(m_layerOk && m_dev && !m_deviceTried)
		{
			m_deviceTried = true;
			const c5r::Status st = m_layer->setDevice(m_dev);
			m_deviceOk = st.ok();
			if(!st.ok())
				fail("models: the model renderer cannot start on the device: " + st.str());
			else
				RNote("models: the model renderer is made on the device (%s)", m_layer->describe().c_str());
		}
		giveOwnership();   // ticket 0789: before the reader's poll hands frames to the layer
		if(m_reader)
		{
			// until the layer is open the frames are not read (the control block is opened and the want bits written, so
			// the compositor taps already; its frames are not acked, which it only counts)
			if(!m_layerOk)
				m_reader->openOnly();
			else if(m_reader->poll())
			{
				m_c.framesAccepted++;
				remember();
				const std::shared_ptr<const std::vector<uint8_t>> img = m_reader->image();
				// draw again only when the frame's content changed (a still camera repeats the same bytes)
				if(img && (!m_lastImage || *m_lastImage != *img))
					changed = true;
				m_lastImage = img;
			}
			m_c.framesRefused = (int64_t)m_reader->counters().refused;
			// ticket 4879: the geometry section's buffers into the layer's store; the CPU-built keys the layer resolved since
			if(m_geom && m_layerOk && m_deviceOk)
			{
				m_geom->poll(*m_layer);
				const std::string note = m_geom->takeNote();
				if(!note.empty())
					RNote("LOUD: models: %s (the draw stays the EXE's)", note.c_str());
			}
			if(m_layerOk && m_layer->takeCpuKeysChanged())
				m_keysDirty = true;
			if(m_layerOk)
				freshness(qpc);
			publishIfChanged(qpc);
			if(m_pub && m_reader->control())
			{
				const bool was = m_setAcked;
				m_setAcked = m_pub->acked(m_reader->control());
				if(m_setAcked && !was)
				{
					m_ackedBits = m_publishedBits;
					RNote("models: the compositor applied identity set generation %u (%u keys)", m_pub->generation(),
					      (uint32_t)m_published.size());
				}
				m_c.setVerdicts = (int64_t)m_pub->counters().verdicts;
				uint64_t refused = 0;
				for(uint32_t r = 0; r < VPMSET_RESULT_COUNT; ++r)
					refused += m_pub->counters().refusedBy[r];
				m_c.setRefused = (int64_t)refused;
			}
		}
		// the mode writer only with a mode block (server.exe; the offline player has none: no bits, no install bound)
		if(m_block)
		{
			bool ready[ModelModeWriter::kBits];
			readiness(ready);
			m_mode.onPoll(ready, m_freshQpc, qpc);
			std::string why;
			if(m_mode.failed(why))
				fail(why);
		}
		return changed;
	}
	void idle(double budgetMs) override
	{
		if(m_failed || !m_layerOk || !m_deviceOk)
			return;
		if(m_layer->prepare(budgetMs))
			m_keysDirty = true;
	}
	void onLoadBegin(int64_t qpc) override
	{
		m_mode.onLoadBegin(qpc);
		m_hist.clear();
		m_lastComplete = Tap();   // ticket 0789
		m_haveTapHdr = false;
		m_layerSeq = 0;
		if(m_layerOk)
			m_layer->clearScene();
	}
	void beginFrame(int64_t present) override
	{
		m_lastLag = -1;
		giveOwnership();   // ticket 0789: the mode bits of this frame for the records put back below (R5)
		if(!present || !m_reader || !m_layerOk || m_failed)
			return;   // no pairing: the newest frame the layer holds (offline, tests)
		const Tap* t = findPresent(present);
		double waited = 0;
		if(!t && m_opt.pairWaitMs > 0)
		{
			// the compositor publishes the tap of Present P at P itself; the server's frame for P may start first: wait for
			// it a bounded time (R1, landmark-renderer.md 7.7)
			const int64_t q0 = Qpc();
			while(!t)
			{
				if(m_reader->poll())
				{
					m_c.framesAccepted++;
					remember();
					t = findPresent(present);
					if(t)
						break;
				}
				waited = QpcToMs(Qpc() - q0);
				if(waited >= m_opt.pairWaitMs || (!m_hist.empty() && m_hist.back().present > present))
					break;
				YieldProcessor();
				SwitchToThread();
			}
			waited = QpcToMs(Qpc() - q0);
			if(waited > m_c.maxPairWaitMs)
				m_c.maxPairWaitMs = waited;
		}
		if(t)
		{
			m_c.pairExact++;
			if(waited > 0)
				m_c.pairWaited++;
		}
		else
		{
			// the newest tap older than P (one frame behind), else the oldest newer one, else none
			for(size_t i = m_hist.size(); i-- > 0 && !t;)
				if(m_hist[i].present < present)
					t = &m_hist[i];
			if(t)
			{
				m_c.pairOlder++;
				if(present - t->present > m_c.maxLagPresents)
					m_c.maxLagPresents = present - t->present;
			}
			else if(!m_hist.empty())
			{
				t = &m_hist.front();
				m_c.pairNewer++;
			}
			else
			{
				m_c.pairNone++;
				return;
			}
		}
		// ticket 0789: a cut tap (its header's truncated > 0: the frame was published empty, every dropped draw lost) is not
		// drawn while an older complete one exists: the newest complete tap older than it (the history, else the last complete
		// tap kept), drawn at camera P like any older tap; the layer's store bridges when there is none
		if(t->truncated && m_keep.on && m_keep.store)
		{
			const Tap* c = nullptr;
			for(size_t i = m_hist.size(); i-- > 0 && !c;)
				if(!m_hist[i].truncated && m_hist[i].present < t->present)
					c = &m_hist[i];
			if(!c && m_lastComplete.img && m_lastComplete.present < t->present)
				c = &m_lastComplete;
			if(c)
			{
				t = c;
				m_c.pairSkippedCut++;
				if(present - t->present > m_c.maxLagPresents)
					m_c.maxLagPresents = present - t->present;
			}
		}
		m_lastLag = present - t->present;   // ticket 4882: 0 exact, > 0 older, < 0 newer
		if(t->seq != m_layerSeq)
		{
			const c5r::Status st = Validate(this, t->img->data(), t->img->size());
			if(!st.ok())
			{
				m_c.drawErrors++;
				if(m_c.drawErrors <= 20 || m_c.drawErrors % 100 == 0)
					RNote("LOUD: models: the tap of Present %lld cannot be put back into the layer: %s", (long long)t->present,
					      st.str().c_str());
			}
		}
	}
	void drawScene(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	               const FrameTextures& ft) override
	{
		drawPass(ctx, 0, rtv, dsv, w, h, ft);
	}
	void drawReflection(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	                    uint32_t h, const FrameTextures& ft) override
	{
		drawPass(ctx, 1, rtv, dsv, w, h, ft);
	}
	void frameDone(int64_t qpc) override
	{
		if(!m_opt.any() || m_opt.source != kModelsSourceExe || m_failed || !m_block)
			return;
		bool ready[ModelModeWriter::kBits];
		readiness(ready);
		m_mode.onFrame(ready, m_freshQpc, qpc);
		std::string why;
		if(m_mode.failed(why))
			fail(why);
	}
	bool failed(std::string& why) const override
	{
		if(m_failed)
			why = m_failWhy;
		return m_failed;
	}
	bool on() const override { return m_opt.any(); }
	uint32_t modeBits() const override { return m_mode.bits(); }
	void line(char* out, size_t n) const override
	{
		if(!n)
			return;
		const ModelTapCounters* rc = m_reader ? &m_reader->counters() : nullptr;
		_snprintf_s(out, n, _TRUNCATE,
		            "models: src=%s layer=%d device=%d set_gen=%u keys=%u (cities %u, landmarks %u) acked=%d bits_acked=0x%X "
		            "frames accepted=%llu refused=%llu skipped=%llu busy=%llu copy_ms=%.2f max_copy_ms=%.2f drawn=%lld refl=%lld "
		            "draw_errors=%lld unknown=%lld env_missing=%lld",
		            m_opt.source == kModelsSourceExe ? "exe" : "own", m_layerOk ? 1 : 0, m_deviceOk ? 1 : 0,
		            m_pub ? m_pub->generation() : 0u, (uint32_t)m_published.size(), m_c.setCities, m_c.setLandmarks,
		            m_setAcked ? 1 : 0, m_ackedBits, rc ? (unsigned long long)rc->accepted : 0ull,
		            rc ? (unsigned long long)rc->refused : 0ull, rc ? (unsigned long long)rc->skipped : 0ull,
		            rc ? (unsigned long long)rc->busy : 0ull, rc ? rc->lastCopyMs : 0.0, rc ? rc->maxCopyMs : 0.0,
		            (long long)m_c.drawnFrames, (long long)m_c.reflDrawn, (long long)m_c.drawErrors, (long long)m_c.unknownDraws,
		            (long long)m_c.envMissing);
		size_t used = strlen(out);
		if(used + 1 < n)
			_snprintf_s(out + used, n - used, _TRUNCATE,
			            " pair exact=%lld waited=%lld older=%lld newer=%lld none=%lld max_lag=%lld max_wait_ms=%.2f",
			            (long long)m_c.pairExact, (long long)m_c.pairWaited, (long long)m_c.pairOlder, (long long)m_c.pairNewer,
			            (long long)m_c.pairNone, (long long)m_c.maxLagPresents, m_c.maxPairWaitMs);
		used = strlen(out);
		if(used + 1 < n)   // ticket 4882: drawn at the frame's camera; the shift that avoided (px of the scene target)
			_snprintf_s(out + used, n - used, _TRUNCATE,
			            " cam frames=%lld exact_n=%lld exact_max_px=%.3f older_n=%lld older_mean_px=%.2f older_max_px=%.2f "
			            "win_max_px=%.2f refl_win_max_px=%.2f env_unknown=%u",
			            (long long)m_c.camFrames, (long long)m_c.shiftExactN, m_c.shiftExactMaxPx, (long long)m_c.shiftOlderN,
			            m_c.shiftOlderN ? m_c.shiftOlderSumPx / (double)m_c.shiftOlderN : 0.0, m_c.shiftOlderMaxPx,
			            m_winMaxPx[0], m_winMaxPx[1], m_c.envUnknown);
		m_winMaxPx[0] = m_winMaxPx[1] = 0;
		used = strlen(out);
		if(used + 1 < n)   // ticket 0709: the instances the EXE culled at the drawn tap's camera, drawn from an older tap
			_snprintf_s(out + used, n - used, _TRUNCATE, " keep_on=%d keep_frames=%lld keep_inst=%lld keep_max=%u keep_entries=%u",
			            m_keep.on ? 1 : 0, (long long)m_c.keepFrames, (long long)m_c.keepInst, m_c.keepMax, m_c.keepEntries);
		if(m_geom)   // ticket 4879
		{
			const GeomCounters& g = m_geom->counters();
			used = strlen(out);
			if(used + 1 < n)
				_snprintf_s(out + used, n - used, _TRUNCATE,
				            " geom open=%d gen=%u entries=%u added=%llu released=%llu torn=%llu refused=%llu/%llu/%llu/%llu kb=%llu%s",
				            g.open ? 1 : 0, g.generation, g.entries, (unsigned long long)g.added, (unsigned long long)g.released,
				            (unsigned long long)g.torn, (unsigned long long)g.refusedOversize, (unsigned long long)g.refusedLayout,
				            (unsigned long long)g.refusedHash, (unsigned long long)g.refusedBounds,
				            (unsigned long long)(g.copiedBytes / 1024), m_layer ? m_layer->geometryLine().c_str() : "");
		}
		m_mode.appendLine(out, n);
	}
	// Ticket 0789: the model store (R3's counters) and the taps' headers (Architect section 9), a line of its own (the
	// models line is near the log's 1,536-character body already)
	void storeLine(char* out, size_t n) const override
	{
		if(!n)
			return;
		_snprintf_s(out, n, _TRUNCATE,
		            "models store: on=%d entries=%u refl_entries=%u kb=%llu pool=%u reentry=%lld absent=%lld cut=%lld refl_kept=%lld "
		            "bridged=%lld "
		            "still_bridged=%lld removed=%llu evicted=%llu handover frames=%lld recs=%lld inst=%lld taps cut=%lld "
		            "skipped_cut=%lld gen_changes=%lld latch_changes=%lld passed_not_in_set=%lld passed_bit_off=%lld",
		            m_keep.on && m_keep.store ? 1 : 0, m_c.keepEntries, m_c.reflEntries, (unsigned long long)(m_c.storeBytes / 1024),
		            m_c.storePool,
		            (long long)m_c.storeReentry, (long long)m_c.storeAbsent, (long long)m_c.storeCut, (long long)m_c.reflKept,
		            (long long)m_c.bridgedFrames, (long long)m_c.stillBridged, (unsigned long long)m_c.storeRemoved,
		            (unsigned long long)m_c.storeEvicted, (long long)m_c.handOverFrames, (long long)m_c.handOverRecs,
		            (long long)m_c.handOverInst, (long long)m_c.tapsCut, (long long)m_c.pairSkippedCut, (long long)m_c.genChanges,
		            (long long)m_c.latchChanges, (long long)m_c.passedNotInSet, (long long)m_c.passedBitOff);
		const size_t used = strlen(out);
		if(used + 1 < n)   // the residual flash (the layer's checkMissed)
			_snprintf_s(out + used, n - used, _TRUNCATE, " missed checked=%llu inst=%llu known=%llu frames=%llu",
			            (unsigned long long)m_c.missedChecked, (unsigned long long)m_c.missed,
			            (unsigned long long)m_c.missedKnown, (unsigned long long)m_c.missedFrames);
	}
	ModelsCounters counters() const override
	{
		ModelsCounters c = m_c;
		c.layerOpen = m_layerOk;
		c.deviceOk = m_deviceOk;
		c.setAcked = m_setAcked;
		c.setKeys = (uint32_t)m_published.size();
		c.keepOn = m_keep.on;   // ticket 0709
		c.storeOn = m_keep.on && m_keep.store;   // ticket 0789
		if(m_reader)
			c.fileLoads = (int64_t)m_reader->counters().fileLoads;
		else
			c.fileLoads = m_fileLoads;
		return c;
	}
	uint64_t imageHash() const override
	{
		const std::shared_ptr<const std::vector<uint8_t>> img = currentImage();
		return img && !img->empty() ? VPModelHash64(img->data(), img->size()) : 0;
	}
	size_t heldGeometryCount() const override   // ticket 4883
	{
		if(!m_layer || !m_layerOk)
			return 0;
		std::vector<std::pair<uint32_t, uint64_t>> held;
		m_layer->heldGeometry(held);
		return held.size();
	}
	// ---- ticket 4888: the CPU-built meshes as the scene bundle's model_geom member ----
	void geometrySnapshot(std::vector<GeomBuffer>& out) const override
	{
		out.clear();
		if(!m_layer || !m_layerOk)
			return;
		std::vector<std::pair<uint32_t, uint64_t>> held;
		m_layer->heldGeometry(held);
		std::sort(held.begin(), held.end());
		out.reserve(held.size());
		for(const auto& k : held)
		{
			GeomBuffer g;
			g.kind = k.first;
			g.hash = k.second;
			if(m_layer->geometryBytes(k.first, k.second, &g.stride, &g.bytes))
				out.push_back(std::move(g));
		}
	}
	uint64_t heldSetHash() const override
	{
		if(!m_layer || !m_layerOk)
			return 0;
		std::vector<std::pair<uint32_t, uint64_t>> held;
		m_layer->heldGeometry(held);
		if(held.empty())
			return 0;
		std::sort(held.begin(), held.end());
		std::vector<uint8_t> b(held.size() * 12);
		for(size_t i = 0; i < held.size(); ++i)
		{
			memcpy(&b[i * 12], &held[i].first, 4);
			memcpy(&b[i * 12 + 4], &held[i].second, 8);
		}
		const uint64_t h = VPModelHash64(b.data(), b.size());
		return h ? h : 1;
	}
	c5r::Status loadGeometry(const std::vector<GeomBuffer>& in) override
	{
		startLayer();
		joinOpen();
		finishOpen();
		if(!m_layerOk)
			return c5r::Status::fail(c5r::Err::NotFound, "no model layer: " + m_layerWhy);
		std::vector<std::pair<uint32_t, uint64_t>> held;
		m_layer->heldGeometry(held);
		for(const auto& k : held)
		{
			bool keep = false;
			for(const GeomBuffer& g : in)
				keep = keep || (g.kind == k.first && g.hash == k.second);
			if(!keep)
				m_layer->releaseGeometry(k.first, k.second);
		}
		for(const GeomBuffer& g : in)
		{
			if(m_layer->holdsGeometry(g.kind, g.hash))
				continue;
			const c5r::Status st = m_layer->addGeometry(g.kind, g.hash, g.stride, g.bytes.data(), g.bytes.size());
			if(!st.ok())
			{
				char b[96];
				_snprintf_s(b, sizeof(b), _TRUNCATE, "the layer refuses the %s buffer %016llX: ",
				            g.kind == VPMGEOM_KIND_INDEX32 ? "index" : "vertex", (unsigned long long)g.hash);
				return c5r::Status::fail(st.code, b + st.str());
			}
		}
		m_geomLoads++;
		return c5r::Status::Ok();
	}
	c5r::Status writeCurrentImage(const std::wstring& path) const override
	{
		if(m_reader)
		{
			// ticket 4883: the frame the layer holds (currentImage), not the reader's newest
			const std::shared_ptr<const std::vector<uint8_t>> img = currentImage();
			if(!img)
				return c5r::Status::fail(c5r::Err::NotFound, "no model tap frame yet");
			return WriteModelImage(*img, path);
		}
		if(!m_fileImage)
			return c5r::Status::fail(c5r::Err::NotFound, "no model tap frame yet");
		return WriteModelImage(*m_fileImage, path);
	}
	c5r::Status loadImage(const uint8_t* image, size_t n) override
	{
		startLayer();
		joinOpen();
		finishOpen();
		if(!m_layerOk)
			return c5r::Status::fail(c5r::Err::NotFound, "no model layer: " + m_layerWhy);
		const c5r::Status st = Validate(this, image, n);
		if(st.ok())
		{
			m_fileLoads++;
			try
			{
				m_fileImage = std::make_shared<std::vector<uint8_t>>(image, image + n);
			}
			catch(...)
			{
				m_fileImage.reset();
			}
		}
		return st;
	}

private:
	static uint64_t HeaderU64(const uint8_t* image, size_t n, size_t off)
	{
		uint64_t v = 0;
		if(image && n >= sizeof(VPModelTapFrame))
			memcpy(&v, image + off, sizeof(v));
		return v;
	}
	static c5r::Status Validate(void* ctx, const uint8_t* image, size_t n)
	{
		ModelsSceneImpl* s = static_cast<ModelsSceneImpl*>(ctx);
		if(!s->m_layerOk)
			return c5r::Status::fail(c5r::Err::Unsupported, "the model layer is not open yet");
		const c5r::Status st = s->m_layer->accept(image, n);
		if(st.ok())
			s->m_layerSeq = HeaderU64(image, n, offsetof(VPModelTapFrame, frameSeq));   // the frame the layer holds now
		return st;
	}
	// a frame the reader accepted: into the pairing history (R1), the newest last, at most kHistory
	void remember()
	{
		const std::shared_ptr<const std::vector<uint8_t>> img = m_reader->image();
		if(!img)
			return;
		Tap t;
		t.img = img;
		t.seq = HeaderU64(img->data(), img->size(), offsetof(VPModelTapFrame, frameSeq));
		t.present = (int64_t)HeaderU64(img->data(), img->size(), offsetof(VPModelTapFrame, presentCount));
		t.truncated = HeaderU32(img->data(), img->size(), offsetof(VPModelTapFrame, truncated));
		tapHeaderCounters(*img);   // ticket 0789
		m_hist.push_back(t);
		while(m_hist.size() > kHistory)
			m_hist.erase(m_hist.begin());
		if(!t.truncated)
			m_lastComplete = t;
	}
	struct Tap
	{
		std::shared_ptr<const std::vector<uint8_t>> img;
		uint64_t seq = 0;
		int64_t present = 0;
		uint32_t truncated = 0;   // ticket 0789: the header's draws not tapped because of a bound (the frame published empty)
	};
	Tap m_lastComplete;   // ticket 0789: the newest accepted tap that was not cut
	static uint32_t HeaderU32(const uint8_t* image, size_t n, size_t off)
	{
		uint32_t v = 0;
		if(image && n >= sizeof(VPModelTapFrame))
			memcpy(&v, image + off, sizeof(v));
		return v;
	}
	// Ticket 0789 (Architect section 9's counters for 0788): an accepted tap's header and draw flags: cut taps, set
	// generation and latch changes between accepted taps, passed records NOT_IN_SET / BIT_OFF. The image passed the
	// reader's checks; the draw table is bounds-checked again here.
	void tapHeaderCounters(const std::vector<uint8_t>& img)
	{
		if(img.size() < sizeof(VPModelTapFrame))
			return;
		VPModelTapFrame h;
		memcpy(&h, img.data(), sizeof(h));
		if(h.truncated)
			m_c.tapsCut++;
		if(m_haveTapHdr && h.setGeneration != m_lastTapGen)
			m_c.genChanges++;
		if(m_haveTapHdr && h.latchedBits != m_lastTapLatch)
			m_c.latchChanges++;
		m_haveTapHdr = true;
		m_lastTapGen = h.setGeneration;
		m_lastTapLatch = h.latchedBits;
		if(h.drawBytes != sizeof(VPModelTapDraw) || h.drawOffset > img.size() ||
		   (uint64_t)h.drawCount * sizeof(VPModelTapDraw) > img.size() - h.drawOffset)
			return;
		for(uint32_t i = 0; i < h.drawCount; ++i)
		{
			uint8_t fl = 0;
			memcpy(&fl, img.data() + h.drawOffset + (size_t)i * sizeof(VPModelTapDraw) + offsetof(VPModelTapDraw, flags), 1);
			if(fl & VPMTAP_DRAW_DROPPED)
				continue;
			if(fl & VPMTAP_DRAW_NOT_IN_SET)
				m_c.passedNotInSet++;
			if(fl & VPMTAP_DRAW_BIT_OFF)
				m_c.passedBitOff++;
		}
	}
	// Ticket 0789: the ownership (R5) to the layer when it changed: live with a mode block, the writer's bits, the sets
	void giveOwnership()
	{
		if(!m_layer)
			return;
		m_own.live = m_block != nullptr;
		m_own.bits = m_block ? m_mode.bits() : 0u;
		if(m_ownGiven == m_ownVersion && m_ownBitsGiven == m_own.bits && m_ownLiveGiven == m_own.live)
			return;
		m_layer->setOwnership(m_own);
		m_ownGiven = m_ownVersion;
		m_ownBitsGiven = m_own.bits;
		m_ownLiveGiven = m_own.live;
	}
	static const size_t kHistory = 4;
	std::vector<Tap> m_hist;
	uint64_t m_layerSeq = 0;
	const Tap* findPresent(int64_t p) const
	{
		for(size_t i = m_hist.size(); i-- > 0;)
			if(m_hist[i].present == p)
				return &m_hist[i];
		return nullptr;
	}
	std::shared_ptr<const std::vector<uint8_t>> currentImage() const
	{
		if(m_reader)
		{
			// ticket 4883: the frame the layer holds, i.e. the one the last frame drew (beginFrame puts the paired tap of
			// the frame's Present back into the layer, one Present older than the reader's newest under the product's
			// lockstep). The scene recorder writes this image after the frame's draw; the reader's newest made a bundle hold
			// a tap the frame did not draw (the huge bundle: 12,100 px off in every frame).
			for(size_t i = m_hist.size(); i-- > 0;)
				if(m_hist[i].seq == m_layerSeq && m_hist[i].img)
					return m_hist[i].img;
			return m_reader->image();
		}
		return m_fileImage;
	}

	void fail(const std::string& why)
	{
		if(m_failed)
			return;
		m_failed = true;
		m_failWhy = why;
		RNote("LOUD: %s", why.c_str());
	}

	// The layer: made once from the factory, opened on a worker once the database is known.
	void startLayer()
	{
		if(!m_layerTried)
		{
			m_layerTried = true;
			m_layer = MakeModelLayer();
			if(!m_layer)
			{
				m_layerWhy = "this server build has no model layer (c5r::landmarks adapter)";
				fail("models: render_cities / render_landmarks is on but " + m_layerWhy);
				return;
			}
			m_layer->setDrawAll(m_opt.drawAll);
			m_layer->setKeep(m_keep);   // ticket 0709
		}
		if(!m_layer || m_openStarted || m_dbPath.empty())
			return;
		m_openStarted = true;
		const ModelsOptions o = m_opt;
		const std::wstring db = m_dbPath;
		ModelLayer* layer = m_layer.get();
		m_openQpc = Qpc();
		m_mode.onOpenBegin(m_openQpc);   // ticket 4885: the install bound waits for the open's end
		RNote("models: the model layer opens on a worker (database '%ls')", db.c_str());
		try
		{
			m_openThread = std::thread([this, layer, o, db]() {
				const c5r::Status st = layer->open(o, db);
				m_openStatus = st;
				m_openDone.store(true);
			});
		}
		catch(...)
		{
			m_openStatus = c5r::Status::fail(c5r::Err::OutOfMemory, "the worker thread cannot start");
			m_openDone.store(true);
		}
	}
	void finishOpen()
	{
		if(!m_openStarted || m_openFinished || !m_openDone.load())
			return;
		joinOpen();
		m_openFinished = true;
		if(!m_openStatus.ok())
		{
			m_layerWhy = m_openStatus.str();
			m_mode.onOpenEnd(false, m_layerWhy.c_str(), Qpc());
			fail("models: the model layer cannot open: " + m_layerWhy);
			return;
		}
		m_layerOk = true;
		m_keysDirty = true;
		m_mode.onOpenEnd(true, nullptr, Qpc());
		RNote("models: the model layer is open after %.0f ms (%s)", QpcToMs(Qpc() - m_openQpc), m_layer->describe().c_str());
	}
	// Ticket 4885: the open's progress for the mode writer's stall bound. Progress = the worker thread ran since the last
	// poll (its cycle count grew): reading and parsing the library's files on a slow disk keeps it growing; a worker
	// blocked for good (a hung read, a lock) does not. A cycle count that cannot be read is said once (LOUD) and counted as
	// progress, so only render_models_open_max_ms bounds that open.
	void sampleOpen(int64_t qpc)
	{
		if(!m_openStarted || m_openFinished || m_openDone.load() || !m_openThread.joinable())
			return;
		ULONG64 cycles = 0;
		if(!QueryThreadCycleTime((HANDLE)m_openThread.native_handle(), &cycles))
		{
			if(!m_cyclesSaid)
			{
				m_cyclesSaid = true;
				RNote("LOUD: models: the open's worker cycle count cannot be read (error %lu): its progress is not measured, only "
				      "render_models_open_max_ms bounds the open",
				      GetLastError());
			}
			m_mode.onOpenProgress(qpc);
			return;
		}
		if(cycles != m_openCycles)
		{
			m_openCycles = cycles;
			m_mode.onOpenProgress(qpc);
		}
	}
	void joinOpen()
	{
		if(m_openThread.joinable())
			m_openThread.join();
	}

	void publishIfChanged(int64_t qpc)
	{
		if(!m_layerOk || !m_deviceOk || !m_pub || !m_keysDirty || !m_reader->control())
			return;
		// at most every 500 ms: prepare() makes more files drawable frame by frame; each generation is a copy at the
		// compositor's next Present
		if(m_lastPublishQpc && QpcToMs(qpc - m_lastPublishQpc) < 500.0)
			return;
		m_keysDirty = false;
		std::vector<KeyInfo> keys;
		m_layer->drawableKeys(keys);
		m_c.drawableKeys = (uint32_t)keys.size();
		SetBuildReport rep;
		std::vector<VPModelSetKey> set = BuildIdentitySet(keys, m_wantBits, &rep);
		if(SameSet(set, m_published) && m_pub->generation())
			return;
		const c5r::Status st = m_pub->publish(set, m_reader->control());
		m_lastPublishQpc = qpc;
		if(!st.ok())
		{
			fail("models: the identity set cannot be published: " + st.str());
			return;
		}
		m_published.swap(set);
		// ticket 0789 (R5): the published generation's keys for the layer, the last kOwnedSetHistory generations
		try
		{
			auto ok = std::make_shared<OwnedKeys>();
			ok->keys.reserve(m_published.size());
			for(const VPModelSetKey& k : m_published)
				ok->keys[VPModelHash64(&k.key, sizeof(k.key))] = k;
			m_own.sets.emplace_back(m_pub->generation(), std::shared_ptr<const OwnedKeys>(ok));
			while(m_own.sets.size() > kOwnedSetHistory)
				m_own.sets.erase(m_own.sets.begin());
			m_ownVersion++;
		}
		catch(...)
		{
			RNote("LOUD: models: no memory for the identity set's copy of generation %u: the hand-over records (R5) use the "
			      "older sets", m_pub->generation());
		}
		m_publishedBits = (rep.cities ? VPMODE_CITIES : 0u) | (rep.landmarks ? VPMODE_LANDMARKS : 0u);
		m_c.setsPublished++;
		m_c.setCities = rep.cities;
		m_c.setLandmarks = rep.landmarks;
		m_setAcked = false;   // the new generation is not applied yet; the bits stay with the older one
		RNote("models: identity set: %u parts in, %u keys out (cities %u, landmarks %u; CPU-built copied %u; library wall parts %u, "
		      "option off %u, duplicates %u, conflicts %u, no key %u, over the limit %u)",
		      rep.in, rep.out, rep.cities, rep.landmarks, rep.cpuCopied, rep.cpuBuilt, rep.classOff, rep.duplicates, rep.conflicts,
		      rep.noKey, rep.overLimit);
	}

	// A bit is ready when its option is on with source exe, the layer and its renderer are up, and an identity set of
	// ours with keys of that bit has been applied by the compositor. To SET a bit (start-up, after a loss) the
	// compositor's setGenerationApplied must equal the generation published last (4874's rule, city-renderer.md 10.4: in
	// the frames between, its filterState2 says "no identity set" (9), which the D118 step reads as fatal); a bit that is
	// live stays live on an older applied generation while a newer one is on its way (the older set is a subset).
	void readiness(bool ready[ModelModeWriter::kBits]) const
	{
		for(int i = 0; i < ModelModeWriter::kBits; ++i)
		{
			const uint32_t b = ModelModeWriter::BitOf(i);
			const bool applied = (m_ackedBits & b) != 0 && (m_setAcked || m_mode.phase(i) == ModelModeWriter::kLive);
			ready[i] = m_mode.on(i) && m_layerOk && m_deviceOk && applied;
		}
	}
	// Fresh (the mode writer's frame clock): the compositor's tap is RUNNING and every frame it published has been read
	// and accepted. The compositor publishes only while something is tapped (city-renderer.md 10.5), so a view without
	// models publishes nothing and stays fresh; published frames that are refused (or not read) make it stale.
	void freshness(int64_t qpc)
	{
		if(!m_reader || !m_reader->haveBlock())
			return;
		const VPShellModelTapControl& b = m_reader->lastBlock();
		if(b.state == VPMTAP_RUNNING && b.frameSeq == m_reader->frameSeq())
			m_freshQpc = qpc;
	}
	int64_t m_freshQpc = 0;

	void drawPass(ID3D11DeviceContext* ctx, int pass, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	              uint32_t h, const FrameTextures& ft)
	{
		if(!m_opt.any() || m_opt.source != kModelsSourceExe || m_failed || !m_layerOk || !m_deviceOk || !rtv)
			return;
		if(!currentImage())
			return;
		LayerDrawStats st;
		const c5r::Status s = m_layer->draw(ctx, pass, rtv, dsv, w, h, ft, &st);
		if(!s.ok())
		{
			m_c.drawErrors++;
			if(m_c.drawErrors <= 20 || m_c.drawErrors % 100 == 0)
				RNote("LOUD: models: the %s draw failed (%lld so far): %s", pass ? "reflection" : "scene", (long long)m_c.drawErrors,
				      s.str().c_str());
			return;
		}
		if(pass == 0)
			m_c.drawnFrames++;
		else
			m_c.reflDrawn++;
		m_c.unknownDraws += st.unknownDraws;
		m_c.envMissing += st.envMissing;
		m_c.envUnknown = st.envUnknown;
		if(pass == 0)   // ticket 0709
		{
			m_c.keepEntries = st.keepEntries;
			if(st.keptInst)
			{
				m_c.keepFrames++;
				m_c.keepInst += st.keptInst;
				if(st.keptInst > m_c.keepMax)
					m_c.keepMax = st.keptInst;
			}
			// ticket 0789: R5's records of the tap, counted at its first scene pass
			if(st.handOverRecs)
			{
				m_c.handOverFrames++;
				m_c.handOverRecs += st.handOverRecs;
				m_c.handOverInst += st.handOverInst;
			}
			m_scenePassRemoved = (std::max)(m_scenePassRemoved, st.storeRemoved);   // the store's totals only grow
			m_scenePassEvicted = (std::max)(m_scenePassEvicted, st.storeEvicted);
			if(st.keepEntries || st.storePool)
				m_scenePassBytes = st.storeBytes;
			m_c.storePool = st.storePool;
			m_c.missedChecked = (std::max)(m_c.missedChecked, st.missedChecked);   // the layer's totals
			m_c.missed = (std::max)(m_c.missed, st.missed);
			m_c.missedKnown = (std::max)(m_c.missedKnown, st.missedKnown);
			m_c.missedFrames = (std::max)(m_c.missedFrames, st.missedFrames);
		}
		else
		{
			m_c.reflEntries = st.keepEntries;
			m_c.reflKept += st.keptInst;
			m_reflRemoved = (std::max)(m_reflRemoved, st.storeRemoved);
			m_reflEvicted = (std::max)(m_reflEvicted, st.storeEvicted);
			if(st.keepEntries)
				m_reflBytes = st.storeBytes;
		}
		// ticket 0789: both passes' store counters; a frame bridged what tap C should have held (absent, cut); at a still
		// camera (the layer says it LOUD)
		m_c.storeReentry += st.keptReentry;
		m_c.storeAbsent += st.keptAbsent;
		m_c.storeCut += st.keptCut;
		if(pass == 0 && (st.keptAbsent || st.keptCut))
		{
			m_c.bridgedFrames++;
			if(st.sameCamera)
				m_c.stillBridged++;
		}
		m_c.storeRemoved = m_scenePassRemoved + m_reflRemoved;
		m_c.storeEvicted = m_scenePassEvicted + m_reflEvicted;
		m_c.storeBytes = m_scenePassBytes + m_reflBytes;
		// ticket 4882: the scene pass at the frame's camera, its shift split by the frame's pairing (beginFrame); the largest
		// shift of each pass since the last line (idle: about 0 when the rows' convention holds; panning: the pan per frame)
		if(st.shiftN && st.shiftPxMax > m_winMaxPx[pass ? 1 : 0])
			m_winMaxPx[pass ? 1 : 0] = st.shiftPxMax;
		if(pass == 0 && st.camConsts)
		{
			m_c.camFrames++;
			if(st.shiftN && m_lastLag == 0)
			{
				m_c.shiftExactN++;
				if(st.shiftPxMax > m_c.shiftExactMaxPx)
					m_c.shiftExactMaxPx = st.shiftPxMax;
			}
			else if(st.shiftN && m_lastLag > 0)
			{
				m_c.shiftOlderN++;
				m_c.shiftOlderSumPx += st.shiftPxSum / st.shiftN;
				if(st.shiftPxMax > m_c.shiftOlderMaxPx)
					m_c.shiftOlderMaxPx = st.shiftPxMax;
			}
		}
	}
	int64_t m_lastLag = -1;   // ticket 4882: the Presents between the frame and the tap drawn (0 exact, -1 unpaired)
	uint64_t m_scenePassRemoved = 0, m_scenePassEvicted = 0, m_scenePassBytes = 0;   // ticket 0789: the layer's totals
	uint64_t m_reflRemoved = 0, m_reflEvicted = 0, m_reflBytes = 0;
	mutable double m_winMaxPx[2] = {0, 0};   // ticket 4882: the largest shift per pass since the last line()

	ModelsOptions m_opt;
	KeepOptions m_keep;   // ticket 0709
	bool m_keepSaid = false, m_keepSaidOn = false, m_keepSaidStore = false;
	// ticket 0789: the ownership handed to the layer (R5), the last one handed, and the previous accepted tap's header
	ModelOwnership m_own;
	uint64_t m_ownVersion = 1, m_ownGiven = 0;
	uint32_t m_ownBitsGiven = 0;
	bool m_ownLiveGiven = false;
	bool m_haveTapHdr = false;
	uint32_t m_lastTapGen = 0, m_lastTapLatch = 0;
	ModelModeWriter m_mode;
	VPShellMode* m_block = nullptr;
	uint32_t m_pid = 0, m_wantBits = 0;
	std::unique_ptr<ModelTapReader> m_reader;
	std::unique_ptr<ModelSetPublisher> m_pub;
	std::unique_ptr<ModelGeomReader> m_geom;   // ticket 4879
	std::unique_ptr<ModelLayer> m_layer;
	bool m_layerTried = false, m_layerOk = false;
	std::string m_layerWhy;
	bool m_openStarted = false, m_openFinished = false;
	std::atomic<bool> m_openDone{false};
	c5r::Status m_openStatus;
	std::thread m_openThread;
	int64_t m_openQpc = 0;
	ULONG64 m_openCycles = 0;      // ticket 4885: the open's worker's cycle count at the last sample
	bool m_cyclesSaid = false;
	std::wstring m_dbPath;
	uint32_t m_dbGen = 0;
	ID3D11Device* m_dev = nullptr;
	bool m_deviceOk = false, m_deviceTried = false;
	bool m_keysDirty = false;
	int64_t m_lastPublishQpc = 0;
	std::vector<VPModelSetKey> m_published;
	uint32_t m_publishedBits = 0, m_ackedBits = 0;
	bool m_setAcked = false;
	bool m_ownSaid = false;
	bool m_failed = false;
	std::string m_failWhy;
	std::shared_ptr<const std::vector<uint8_t>> m_fileImage;
	std::shared_ptr<const std::vector<uint8_t>> m_lastImage;
	int64_t m_fileLoads = 0;
	int64_t m_geomLoads = 0;   // ticket 4888: loadGeometry calls that held their buffers
	ModelsCounters m_c;
};

std::unique_ptr<ModelsScene> MakeImpl() { return std::unique_ptr<ModelsScene>(new ModelsSceneImpl()); }
struct Registrar
{
	Registrar() { RegisterModelsSceneFactory(&MakeImpl); }
} s_registrar;

}  // namespace

// ---- ticket 0709 (srv_models_scene.h) ----
namespace {
// the inverse of a 4 x 4 row-major matrix (Gauss-Jordan with partial pivots, double); false when singular
bool Invert4(const double m[16], double out[16])
{
	double a[4][8];
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 8; ++c)
			a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	double scale = 0.0;
	for(int i = 0; i < 16; ++i)
		scale = (std::max)(scale, std::fabs(m[i]));
	if(!(scale > 0.0))
		return false;
	for(int c = 0; c < 4; ++c)
	{
		int p = c;
		for(int r = c + 1; r < 4; ++r)
			if(std::fabs(a[r][c]) > std::fabs(a[p][c]))
				p = r;
		if(!(std::fabs(a[p][c]) > 1e-12 * scale))
			return false;
		if(p != c)
			for(int k = 0; k < 8; ++k)
				std::swap(a[p][k], a[c][k]);
		const double d = a[c][c];
		for(int k = 0; k < 8; ++k)
			a[c][k] /= d;
		for(int r = 0; r < 4; ++r)
			if(r != c && a[r][c] != 0.0)
			{
				const double f = a[r][c];
				for(int k = 0; k < 8; ++k)
					a[r][k] -= f * a[c][k];
			}
	}
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
			out[r * 4 + c] = a[r][c + 4];
	return true;
}
}  // namespace

GroundBox CullGroundBox(const float vp[16])
{
	GroundBox b;
	if(!vp)
		return b;
	double m[16], inv[16];
	for(int i = 0; i < 16; ++i)
	{
		if(!std::isfinite(vp[i]))
			return b;
		m[i] = vp[i];
	}
	if(!Invert4(m, inv))
		return b;
	// row-vector product: clip = (x, y, z, 1) vp, so (x, y, z, 1) ~ clip inv
	const auto unproject = [&](double sx, double sy, double sz, double p[3]) {
		const double c[4] = {sx, sy, sz, 1.0};
		double h[4];
		for(int j = 0; j < 4; ++j)
			h[j] = c[0] * inv[0 * 4 + j] + c[1] * inv[1 * 4 + j] + c[2] * inv[2 * 4 + j] + c[3] * inv[3 * 4 + j];
		if(!(std::fabs(h[3]) > 1e-12))
			return false;
		for(int j = 0; j < 3; ++j)
			p[j] = h[j] / h[3];
		return true;
	};
	double x0 = DBL_MAX, x1 = -DBL_MAX, y0 = DBL_MAX, y1 = -DBL_MAX;
	for(int sx = -1; sx <= 1; sx += 2)
		for(int sy = -1; sy <= 1; sy += 2)
		{
			double a[3], e[3];
			if(!unproject(sx, sy, 0.0, a) || !unproject(sx, sy, 1.0, e))
				return b;
			const double dz = e[2] - a[2];
			if(!(std::fabs(dz) > 1e-9))
				return b;   // an edge parallel to the ground: no cut
			for(int zi = 0; zi < 2; ++zi)
			{
				const double t = ((zi ? 127.5 : 0.0) - a[2]) / dz;
				const double x = a[0] + t * (e[0] - a[0]), y = a[1] + t * (e[1] - a[1]);
				if(!std::isfinite(x) || !std::isfinite(y))
					return b;
				x0 = (std::min)(x0, x);
				x1 = (std::max)(x1, x);
				y0 = (std::min)(y0, y);
				y1 = (std::max)(y1, y);
			}
		}
	if(!(std::fabs(x0) < 1e30 && std::fabs(x1) < 1e30 && std::fabs(y0) < 1e30 && std::fabs(y1) < 1e30))
		return b;
	b.ok = true;
	b.x0 = (float)x0;
	b.x1 = (float)x1;
	b.y0 = (float)y0;
	b.y1 = (float)y1;
	return b;
}

float GroundBoxOutside(const GroundBox& b, float x, float y, float wrapPeriod)
{
	if(!b.ok)
		return FLT_MAX;
	const auto one = [&](double px) {
		return (std::max)((std::max)((double)b.x0 - px, px - (double)b.x1), (std::max)((double)b.y0 - y, (double)y - b.y1));
	};
	double d = one(x);
	if(wrapPeriod > 0.0f && std::isfinite(wrapPeriod))
		for(int k = -1; k <= 1; k += 2)
			d = (std::min)(d, one((double)x - k * (double)wrapPeriod));   // inside the copy of the box moved by k W
	return (float)d;
}

bool SameViewProj(const float a[16], const float b[16])
{
	float big = 0.0f, diff = 0.0f;
	for(int i = 0; i < 16; ++i)
	{
		if(!std::isfinite(a[i]) || !std::isfinite(b[i]))
			return false;
		big = (std::max)(big, (std::max)(std::fabs(a[i]), std::fabs(b[i])));
		diff = (std::max)(diff, std::fabs(a[i] - b[i]));
	}
	return big > 0.0f && diff <= 1e-6f * big;
}

void RegisterModelLayerFactory(ModelLayerFactory f) { g_layerFactory = f; }
std::unique_ptr<ModelLayer> MakeModelLayer() { return g_layerFactory ? g_layerFactory() : nullptr; }

}  // namespace srvmodels
