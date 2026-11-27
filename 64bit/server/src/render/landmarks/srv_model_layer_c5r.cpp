// srv_model_layer_c5r.cpp - the model module's layer on c5r::landmarks (epic 0561, ticket 4875): the model library
// (4872), the wire index, readTap, resolve and ModelRenderer (4873) behind srv_models_scene.h's ModelLayer seam. Written
// 2026-11-18 for this repository (GPL-3.0-or-later); research/05-graphics-composition/city-renderer.md 11.
//
// Written against 4873's declarations (c5r/landmarks/tap.h and draw.h at 450af292e on task-4873-draw-layer-harness, thread
// 0561 #6145) before they are merged: until the team branch carries those headers this file compiles to nothing, the
// factory stays empty and render_cities=1 / render_landmarks=1 take the D118 route ("this server build has no model
// layer"). The content checks of a tap frame live once, in readTap (lead2-0561 #6146).
//
// What the layer does:
//   open (worker): the install (with the active mods' folders) and the database copy -> ArtCatalogue, ModelLibrary,
//     WireIndex::build (every file's wire keys and texture identities), BuildingPrograms::open (the EXE's programs by
//     digest, D83).
//   setDevice: ModelRenderer::init (programs, states, samplers); null releases it.
//   prepare(budget): files made drawable one by one (ModelRenderer::prepare when 4873's renderer has it, thread 0561
//     #6160; without it every indexed file counts as drawable once the renderer is made, its buffers made at first draw).
//   drawableKeys: per drawable file every part's wire key, its file class, the model source(s) that use the file, and
//     whether the part is skinned (BuildIdentitySet decides the bits).
//   accept: readTap (all content checks) -> resolve -> the items kept for drawing (only the records the compositor
//     DROPPED or HELD (ticket 0788: dropped from the next frame on, the frame this tap is drawn in), unless drawAll).
//   draw: the pass's target (scene: the target with the largest area; reflection: the largest other one), the server's
//     frame textures (t7 fog mask, t8 cloud base; white / black without the server's fog), and since ticket 4882 the PS
//     inputs the EXE supplies itself, read from the install (c5r/landmarks/inputs.h: t5 the fog ramp, t6 the Buildings
//     ColorKey volume, t9 the TerrainFOW volume, t2-t4 the cubes by the tap's identity; 4875 left them unbound, which
//     drew every model black live, s3 point 8), and the frame's own camera rows in every drawn record's VS cb1
//     (rows 0-3 view-projection, 8-11 OverlayProj, 12 FOWAnim; city-renderer.md 11.11).
//   Ticket 0709 (pan-coherence.md 5.14): the scene pass also draws the instances the EXE culled at the drawn tap's camera
//     that an older drawn tap held and that are in the EXE's box at the frame's camera (CulledKeep, srv_models_scene.h),
//     each as its own record (its world rows, its tapped constants), at the frame's camera like the rest. The sightings
//     are taken from each tap the scene pass draws (once per tap), so a replay of the same frames in order keeps the same.
//   Ticket 0789 (D269, the model store; architect-2026-11-25-retained-world.md 6.1, 6.3): with KeepOptions::store the
//     keep has no age bound, holds its own copies (StoredInst: the record, four rows, constants pooled by content), runs
//     in the reflection pass too (its own store, its own cameras), removes only on R3's evidence, draws absent and
//     cut-tap instances (CulledKeep's rules (f), (g)), and accept() keeps the passed records of a key the compositor drops
//     at P (R5, drawsAtP). Skinned parts (material 6) are not in the store (their origin is not in their rows): R5 and
//     the module's skip of a cut tap cover them.
#if defined(__has_include)
#if __has_include("c5r/landmarks/tap.h") && __has_include("c5r/landmarks/draw.h")
#define SRV_MODELS_HAVE_C5R 1
#endif
#endif

#ifdef SRV_MODELS_HAVE_C5R
#include "srv_models_scene.h"
#include "srv_model_tap.h"
#include "c5r/landmarks/cpumesh.h"
#include "c5r/landmarks/draw.h"
#include "c5r/landmarks/inputs.h"
#include "c5r/landmarks/library.h"
#include "c5r/landmarks/tap.h"
#include "c5r/cities/catalogue.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5models/art.h"
#include "extras/srv_extras.h"   // ticket 0808: RoadLayout's bridge placer (header only)

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_set>
#include <type_traits>

namespace srvmodels {

namespace {

using namespace c5r::landmarks;
using srvrender::Qpc;
using srvrender::QpcToMs;
using srvrender::RNote;   // ticket 0652
using srvrender::RLog;    // ticket 0808

// 4873's renderer may have prepare(file) / prepared(file) (asked on thread 0561 #6160); detected, not assumed
template <class T, class = void>
struct HasPrepare : std::false_type {};
template <class T>
struct HasPrepare<T, decltype((void)std::declval<T&>().prepare(0u), void())> : std::true_type {};

template <class R>
c5r::Status PrepareFile(R& r, uint32_t file, std::true_type) { return r.prepare(file); }
template <class R>
c5r::Status PrepareFile(R&, uint32_t, std::false_type) { return c5r::Status::Ok(); }

KeyFamily FamilyOf(ModelClass c)
{
	return c == ModelClass::CityBlock ? KeyFamily::CityBlock : c == ModelClass::CityPiece ? KeyFamily::CityPiece : KeyFamily::Landmark;
}
KeySource SourceOf(Source s)
{
	switch(s)
	{
	case Source::Wonder: return KeySource::Wonder;
	case Source::Wall: return KeySource::Wall;
	case Source::GreatWall: return KeySource::GreatWall;
	case Source::CitySet: return KeySource::CitySet;
	default: return KeySource::LandmarkRow;
	}
}
VPModelKey Wire(const WireKey& k)
{
	VPModelKey w;
	memset(&w, 0, sizeof(w));
	w.ib = k.ib;
	w.vb = k.vb;
	w.t0 = k.t0;
	w.t1 = k.t1;
	w.startIndex = k.startIndex;
	w.indexCount = k.indexCount;
	w.baseVertex = k.baseVertex;
	w.flags = k.flags;
	return w;
}

// Ticket 0789 (was 0709's KeepRef, which pinned the whole tap scene it came from): a remembered instance as its OWN copy:
// its record (instRow / instanceCount unused), its four rows, its VS / PS cb1 contents by reference to a block shared by
// content (the layer's pool), a CPU-built record shared by the instances of one tapped draw.
struct ConstBlock
{
	TapConst c;                 // firstRow unused: the rows are below
	std::vector<Float4> rows;
};
struct StoredInst
{
	bool cpu = false;
	DrawItem item;
	std::shared_ptr<const CpuDrawItem> cpuItem;
	Float4 rows[4];
	std::shared_ptr<const ConstBlock> vs, ps;
};
typedef CulledKeep<StoredInst> Keep;
// Ticket 0789: one pass's store and the accepted tap's part of it
struct PassStore
{
	Keep keep;
	int target = -1;                       // the pass's target in the accepted tap (-1: none, or the store not active)
	std::vector<Keep::Seen> seen;          // the tap's drawn instances, committed at the pass's first draw of the tap
	std::unordered_set<uint64_t> held;     // every instance the tap holds on the target (any flag)
	bool pending = false, haveVp = false;
	float tapVp[16] = {};                  // the tap's main camera on the target
	ModelScene kept;                       // this frame's kept records
	TapCameraRows keptRows;
};

// Ticket 0789: a class's mode bit as the compositor gives it (shell/composite/unit_filter.cpp, the drop decision): blocks
// and single models VPMODE_CITIES, instanced landmarks VPMODE_LANDMARKS, a skinned part the one bit its set entry has
uint32_t PairBit(Material m)
{
	switch(m)
	{
	case Material::Blocks:
	case Material::Single: return VPMODE_CITIES;
	case Material::Instanced: return VPMODE_LANDMARKS;
	default: return 0;
	}
}
uint32_t DrawBit(const TapDraw& d)
{
	const uint32_t b = PairBit(d.material);
	return b ? b : (d.bits == VPMODE_CITIES || d.bits == VPMODE_LANDMARKS ? d.bits : 0u);
}

// Ticket 0709: the scene target (the largest) or the reflection's (the largest other one) of a scene
int TargetOf(const ModelScene& s, int pass)
{
	int best = -1, second = -1;
	uint64_t ba = 0, sa = 0;
	for(size_t i = 0; i < s.targets.size(); ++i)
	{
		const uint64_t a = (uint64_t)s.targets[i].width * s.targets[i].height;
		if(best < 0 || a > ba)
		{
			second = best;
			sa = ba;
			best = (int)i;
			ba = a;
		}
		else if(second < 0 || a > sa)
		{
			second = (int)i;
			sa = a;
		}
	}
	return pass == 0 ? best : second;
}

// Ticket 0709: an instance's identity: its part (the file and part, or a CPU-built draw's wire key), its material and the
// bits of its origin (row 3 x, y, z), as server/evidence/0709/holes.py matched the taps of 5.13
uint64_t KeepId(bool cpu, uint32_t file, uint32_t part, const WireKey* key, Material m, const Float4& origin)
{
	uint8_t b[96] = {};
	size_t n = 0;
	const auto put = [&](const void* p, size_t k) {
		memcpy(b + n, p, k);
		n += k;
	};
	const uint8_t tag = cpu ? 1 : 0, mat = (uint8_t)m;
	put(&tag, 1);
	put(&mat, 1);
	if(cpu && key)
	{
		put(&key->ib, 8);
		put(&key->vb, 8);
		put(&key->t0, 8);
		put(&key->t1, 8);
		put(&key->startIndex, 4);
		put(&key->indexCount, 4);
		put(&key->baseVertex, 4);
		put(&key->flags, 4);
	}
	else
	{
		put(&file, 4);
		put(&part, 4);
	}
	put(&origin.x, 4);
	put(&origin.y, 4);
	put(&origin.z, 4);
	return VPModelHash64(b, n);
}

bool KeepMaterial(Material m) { return m == Material::Single || m == Material::Instanced || m == Material::Blocks; }

class C5rLayer : public ModelLayer
{
public:
	~C5rLayer() override
	{
		m_renderer.release();
		m_inputs.release();
	}

	c5r::Status open(const ModelsOptions& o, const std::wstring& dbPath) override
	{
		const int64_t q0 = Qpc();
		if(!c5m::SqliteTables::available())
			return c5r::Status::fail(c5r::Err::NotFound, "winsqlite3.dll is not available (the game database cannot be read)");
		c5m::Error e;
		if(!m_db.open(c5r::gamefs::toUtf8(dbPath), &e))
			return c5r::Status::fail(c5r::Err::Io, "the database copy '" + c5r::gamefs::toUtf8(dbPath) + "': " + e.what);
		c5r::gamefs::GameFiles::Options fo;
		fo.extraLooseRoots = o.looseRoots;
		C5R_TRY(m_files.open(o.install, fo));
		C5R_TRY(m_art.load(m_files, m_db));
		C5R_TRY(m_lib.load(m_files, m_db, m_art));
		m_index.reset(new WireIndex(m_lib, m_files));
		C5R_TRY(m_index->build());
		addCpuTextureNames(*m_index);   // ticket 4879: the bridge's maps (no library file names them)
		C5R_TRY(m_programs.open(o.exe));
		// ticket 4882: the PS inputs the EXE supplies itself (t2-t6, t9), from the same install
		C5R_TRY(m_inputs.open(m_files));
		// the sources that use each file (a skinned part's bit comes from it)
		m_fileSources.assign(m_lib.files().size(), std::set<KeySource>());
		for(const Model& m : m_lib.models())
			if(m.file >= 0 && (size_t)m.file < m_fileSources.size())
				m_fileSources[(size_t)m.file].insert(SourceOf(m.source));
		m_prepared.assign(m_lib.files().size(), false);
		m_openMs = QpcToMs(Qpc() - q0);
		return c5r::Status::Ok();
	}
	c5r::Status setDevice(ID3D11Device* dev) override
	{
		m_renderer.release();   // also the programs' device objects
		m_inputs.release();
		m_prepared.assign(m_lib.files().size(), false);
		m_next = 0;
		m_deviceOk = false;
		if(!dev)
			return c5r::Status::Ok();
		C5R_TRY(m_renderer.init(dev, m_programs, m_lib, m_files));
		m_renderer.setCpuMeshes(&m_cpu);   // ticket 4879
		C5R_TRY(m_inputs.create(dev));   // ticket 4882
		m_deviceOk = true;
		return c5r::Status::Ok();
	}
	bool prepare(double budgetMs) override
	{
		if(!m_deviceOk || m_next >= m_prepared.size())
			return false;
		const int64_t q0 = Qpc();
		bool changed = false;
		while(m_next < m_prepared.size())
		{
			const uint32_t f = (uint32_t)m_next++;
			if(!m_lib.files()[f].ok)
				continue;
			const c5r::Status st = PrepareFile(m_renderer, f, HasPrepare<ModelRenderer>());
			if(st.ok())
			{
				m_prepared[f] = true;
				changed = true;
			}
			else
				m_prepareFailed++;
			if(QpcToMs(Qpc() - q0) >= budgetMs)
				break;
		}
		return changed;
	}
	void drawableKeys(std::vector<KeyInfo>& out) const override
	{
		out.clear();
		for(uint32_t f = 0; f < m_prepared.size(); ++f)
		{
			if(!m_prepared[f])
				continue;
			const ModelFile& mf = m_lib.files()[f];
			for(uint32_t p = 0; p < mf.layout.parts.size(); ++p)
			{
				const WireKey* k = m_index->key(f, p);
				if(!k)
					continue;
				KeyInfo ki;
				ki.key = Wire(*k);
				ki.family = FamilyOf(mf.cls);
				ki.skinned = mf.layout.parts[p].skinned;
				// one entry per source using the file: a skinned part used by a city source and a landmark row becomes a
				// conflict in BuildIdentitySet and stays with the EXE
				const std::set<KeySource>& src = m_fileSources[f];
				if(src.empty())
				{
					ki.source = KeySource::LandmarkRow;
					out.push_back(ki);
				}
				for(KeySource s : src)
				{
					ki.source = s;
					out.push_back(ki);
					if(!ki.skinned)
						break;   // a rigid part's bit comes from its file's class: one entry
				}
			}
		}
		// ticket 4879: the CPU-built meshes whose buffers are held and whose textures are drawable
		for(const auto& kv : m_cpuKeys)
		{
			KeyInfo ki;
			ki.key = Wire(kv.first);
			ki.family = kv.second == Material::Blocks ? KeyFamily::CityBlock : KeyFamily::CityPiece;
			ki.source = KeySource::CpuCopied;
			ki.skinned = false;
			out.push_back(ki);
		}
	}
	// ---- ticket 4879: the CPU-built meshes (cpumesh.h, srv_model_geom.h) ----
	c5r::Status addGeometry(uint32_t kind, uint64_t hash, uint32_t stride, const uint8_t* p, size_t n) override
	{
		return m_cpu.add(kind == VPMGEOM_KIND_INDEX32 ? GeomKind::Index32 : GeomKind::Vertex, hash, stride, p, n);
	}
	bool holdsGeometry(uint32_t kind, uint64_t hash) const override
	{
		return m_cpu.has(kind == VPMGEOM_KIND_INDEX32 ? GeomKind::Index32 : GeomKind::Vertex, hash);
	}
	void releaseGeometry(uint32_t kind, uint64_t hash) override
	{
		const bool ib = kind == VPMGEOM_KIND_INDEX32;
		if(!m_cpu.remove(ib ? GeomKind::Index32 : GeomKind::Vertex, hash))
			return;
		for(auto it = m_cpuKeys.begin(); it != m_cpuKeys.end();)
		{
			if((ib ? it->first.ib : it->first.vb) == hash)
			{
				it = m_cpuKeys.erase(it);
				m_cpuChanged = true;
			}
			else
				++it;
		}
	}
	void heldGeometry(std::vector<std::pair<uint32_t, uint64_t>>& out) const override
	{
		out.clear();
		for(const auto& k : m_cpu.list())
			out.emplace_back(k.first == GeomKind::Index32 ? VPMGEOM_KIND_INDEX32 : VPMGEOM_KIND_VERTEX, k.second);
	}
	bool geometryBytes(uint32_t kind, uint64_t hash, uint32_t* stride, std::vector<uint8_t>* bytes) const override
	{
		// ticket 4888: the scene bundle's model_geom member (the bytes as the store holds them, checked by hash on entry)
		const CpuBuffer* b = m_cpu.get(kind == VPMGEOM_KIND_INDEX32 ? GeomKind::Index32 : GeomKind::Vertex, hash);
		if(!b)
			return false;
		if(stride)
			*stride = b->stride;
		if(bytes)
			*bytes = b->bytes;
		return true;
	}
	bool takeCpuKeysChanged() override
	{
		const bool c = m_cpuChanged;
		m_cpuChanged = false;
		return c;
	}
	std::string geometryLine() const override
	{
		char b[256];
		_snprintf_s(b, sizeof(b), _TRUNCATE, " cpu held=%u kb=%llu keys=%u r32=%u resolved=%u nogeom=%u stride=%u unnamed=%u notex=%u",
		            (uint32_t)m_cpu.size(), (unsigned long long)(m_cpu.bytes() / 1024), (uint32_t)m_cpuKeys.size(), m_cpuRep.r32,
		            m_cpuRep.resolved, m_cpuRep.noGeometry, m_cpuRep.badStride, m_cpuRep.textureUnnamed, m_cpuNoTex);
		std::string out = b;
		if(m_bridgeT.seen)   // ticket 0808: only once RoadLayout's placer met a bridge
		{
			_snprintf_s(b, sizeof(b), _TRUNCATE,
			            " bridges seen=%llu moved=%llu dropped=%llu kept=%llu dup=%llu fitfail=%llu nobuf=%llu max_shift=%.1f max_turn=%.1f",
			            (unsigned long long)m_bridgeT.seen, (unsigned long long)m_bridgeT.moved, (unsigned long long)m_bridgeT.dropped,
			            (unsigned long long)m_bridgeT.kept, (unsigned long long)m_bridgeT.duplicate, (unsigned long long)m_bridgeT.fitFailed,
			            (unsigned long long)m_bridgeT.noBuffer, m_bridgeT.maxShift, m_bridgeT.maxTurn);
			out += b;
		}
		return out;
	}
	c5r::Status accept(const uint8_t* image, size_t n) override
	{
		TapFrame f;
		C5R_TRY(readTap(image, n, f));
		ModelScene s;
		ResolveReport rep;
		C5R_TRY(resolve(*m_index, m_lib, f, s, &rep));
		// ticket 4879: the CPU-built draws whose buffers the store holds; a new key whose textures are drawable joins the
		// identity set (the compositor drops it only then: until the set has it, it is tapped and passed)
		CpuResolveReport crep;
		C5R_TRY(resolveCpu(m_cpu, *m_index, f, s, &crep));
		m_cpuRep = crep;
		{
			std::vector<CpuDrawItem> drawable;
			drawable.reserve(s.cpuItems.size());
			for(const CpuDrawItem& it : s.cpuItems)
			{
				if(!m_cpuKeys.count(it.key))
				{
					if(!m_deviceOk || !m_renderer.prepareTextures(it.t0, it.t1))
					{
						m_cpuNoTex++;
						continue;
					}
					m_cpuKeys[it.key] = it.material;
					m_cpuChanged = true;
				}
				drawable.push_back(it);
			}
			s.cpuItems.swap(drawable);
		}
		placeBridges(s);   // ticket 0808: under RoadLayout, the EXE's bridges onto our roads' crossings
		checkMissed(f, s);   // ticket 0789: the residual flash of the frame drawn before this tap
		// ticket 0789: the keys this tap DROPPED (the offline stand-in for the published sets, R5), and the classes it
		// dropped (with the header's latch: the offline stand-in for the server's mode bits)
		uint32_t droppedBits = 0;
		for(const TapDraw& d : f.draws)
			if(d.flags & VPMTAP_DRAW_DROPPED)
			{
				const uint32_t b = DrawBit(d);
				droppedBits |= b;
				if(!m_own.live)
				{
					VPModelSetKey sk;
					memset(&sk, 0, sizeof(sk));
					sk.key = Wire(d.key);
					sk.bits = (uint8_t)b;
					sk.cls = classOf(d.material);
					m_seenDropped.keys[VPModelHash64(&sk.key, sizeof(sk.key))] = sk;
				}
			}
		m_tap = KeepTap();
		m_tap.present = (int64_t)f.presentCount;
		m_tap.complete = f.truncated == 0;
		m_tap.tappedBits = f.wantBits | f.latchedBits | droppedBits;
		m_offlineBits = f.latchedBits | droppedBits;
		// ticket 0709 / 0789: per pass (0 scene, 1 reflection with the store), every instance the tap holds on the pass's
		// target (any flag) and its main camera
		for(int p = 0; p < 2; ++p)
		{
			PassStore& ps = m_ps[p];
			ps.held.clear();
			ps.haveVp = false;
			ps.seen.clear();
			ps.pending = false;
			ps.target = storeActive(p) ? TargetOf(s, p) : -1;
			if(ps.target < 0)
				continue;
			forEachKeepInstance(s, (uint32_t)ps.target, [&](bool, uint32_t, uint32_t, uint64_t id, const Float4&) { ps.held.insert(id); });
			ps.haveVp = mainCamera(f, s, (uint32_t)ps.target, ps.tapVp);
		}
		m_tapHandRecs = m_tapHandInst = 0;
		if(!m_drawAll)
		{
			// the records the compositor DROPPED; ticket 0788: a HELD record (drawn by the EXE in the tap's own frame, dropped
			// from the next one), the server draws this tap in the next frame (lategame-model-flash.md); ticket 0789 (R5): the
			// passed ones of a key the compositor drops at P (drawsAtP, which takes ServerDrawsRecord first)
			std::vector<DrawItem> keep;
			keep.reserve(s.items.size());
			for(const DrawItem& it : s.items)
				if(it.tapDraw < f.draws.size() && drawsAtP(f.draws[it.tapDraw], f, it.instanceCount))
					keep.push_back(it);
			s.items.swap(keep);
			std::vector<CpuDrawItem> keepCpu;
			for(const CpuDrawItem& it : s.cpuItems)
				if(it.tapDraw < f.draws.size() && drawsAtP(f.draws[it.tapDraw], f, it.instanceCount))
					keepCpu.push_back(it);
			s.cpuItems.swap(keepCpu);
		}
		m_scene = std::move(s);
		// ticket 0789: the instances this tap draws on the scene target (the residual-flash check of its frame)
		m_frameTapIds.clear();
		{
			const int t0 = TargetOf(m_scene, 0);
			if(t0 >= 0)
				forEachKeepInstance(m_scene, (uint32_t)t0, [&](bool, uint32_t, uint32_t, uint64_t id, const Float4&) { m_frameTapIds.insert(id); });
		}
		// ticket 4882: the tap's own camera rows, kept before draw() puts the frame's camera in
		keepTapCamera(m_scene, m_tapRows);
		// ticket 0709 / 0789: the instances this tap draws, as the sightings each pass commits once it draws this tap: own
		// copies, made now (before any camera is put in)
		m_keepPresent = (int64_t)f.presentCount;
		for(int p = 0; p < 2; ++p)
		{
			PassStore& ps = m_ps[p];
			if(ps.target < 0)
				continue;
			try
			{
				makeSightings(f, (uint32_t)ps.target, ps.seen);
				ps.pending = true;
			}
			catch(...)
			{
				ps.seen.clear();   // no memory: this tap gives no sightings (the store keeps what it has)
				ps.pending = false;
			}
		}
		if(++m_acceptsSincePoolSweep >= 256)
			sweepPool();
		m_haveScene = true;
		m_lastReport = rep;
		return c5r::Status::Ok();
	}
	c5r::Status draw(ID3D11DeviceContext* ctx, int pass, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	                 uint32_t h, const FrameTextures& ft, LayerDrawStats* st) override
	{
		if(st)
			*st = LayerDrawStats();
		if(!m_haveScene || !m_deviceOk)
			return c5r::Status::Ok();
		const int target = targetOf(pass);
		// ticket 0709 / 0789: the pass's store: the drawn tap's sightings and R3's evidence first (once per tap), then the
		// stored instances this frame draws besides the tap's own
		size_t kept = 0;
		KeepStats kst;
		PassStore& ps = m_ps[pass ? 1 : 0];
		const bool active = storeActive(pass) && target >= 0 && target == ps.target;
		KeepTap tap = m_tap;
		tap.ownedBits = m_own.live ? m_own.bits : m_offlineBits;
		if(active)
		{
			if(ps.pending)
			{
				ps.keep.commit(tap, &ps.held, ps.haveVp ? ps.tapVp : nullptr, ft.wrapPeriod, ps.seen);
				ps.pending = false;
				ps.seen.clear();
				if(pass == 0 && st)
				{
					st->handOverRecs = m_tapHandRecs;   // the R5 records of this tap, counted once (its first scene pass)
					st->handOverInst = m_tapHandInst;
				}
			}
			kept = buildKept(ps, (uint32_t)target, tap, ft, &kst, pass == 0 ? &m_frameKeptIds : nullptr);
		}
		// the residual-flash check (checkMissed), with or without the store: this scene pass's drawn instances and camera
		if(pass == 0 && target >= 0 && ft.haveViewProj)
		{
			if(!active)
				m_frameKeptIds.clear();
			memcpy(m_frameVp, ft.viewProj, sizeof(m_frameVp));
			m_frameTap = tap.present;
			m_haveFrame = true;
		}
		if(st && pass == 0)
		{
			st->missed = m_missed;
			st->missedKnown = m_missedKnown;
			st->missedFrames = m_missedFrames;
			st->missedChecked = m_missedChecked;
		}
		if(st && active)
		{
			st->keptInst = (uint32_t)kept;
			st->keepCandidates = kst.candidates;
			st->keepEntries = (uint32_t)ps.keep.size();
			st->keptReentry = kst.reentry;
			st->keptAbsent = kst.absent;
			st->keptCut = kst.cut;
			st->sameCamera = kst.sameCamera;
			st->tapCut = !tap.complete;
			st->storeRemoved = ps.keep.removed();
			st->storeEvicted = ps.keep.evicted();
			st->storeBytes = (uint64_t)ps.keep.size() * (sizeof(Keep::Entry) + 32) + (pass == 0 ? m_poolBytes : 0);
			st->storePool = (uint32_t)m_pool.size();
			noteStore(pass, kst, ps.keep.evicted(), !tap.complete);
		}
		if((m_scene.items.empty() && m_scene.cpuItems.empty() && !kept) || target < 0)
			return c5r::Status::Ok();
		ViewInput v;
		v.pass = pass ? DrawPass::Reflection : DrawPass::Scene;
		v.target = (uint32_t)target;
		v.rtv = rtv;
		v.dsv = dsv;
		v.viewport.Width = (float)w;
		v.viewport.Height = (float)h;
		v.viewport.MaxDepth = 1.0f;
		EnvInput env;
		for(int i = 0; i < 5; ++i)
			env.frame[i] = ft.t[i];
		// ticket 4882: t2-t4 by identity, t5 / t6 / t9 from the install; t7 / t8 the server's fog, else white / black
		m_inputs.fill(m_scene, (uint32_t)target, env);
		if(!env.frame[kFrameFogMask])
			env.frame[kFrameFogMask] = m_inputs.white();
		if(!env.frame[kFrameCloudBase])
			env.frame[kFrameCloudBase] = m_inputs.black();
		CameraShift cam;
		if(ft.haveViewProj)
		{
			FrameCamera fc;
			fc.viewProj = ft.viewProj;
			fc.overlayProj = ft.haveFog ? ft.overlayProj : nullptr;
			fc.fowAnim = ft.haveFog ? ft.fowAnim : nullptr;
			fc.eye = ft.haveEye ? ft.eye : nullptr;   // ticket 0598: PS cb1 row 16, the main view's eye in both passes
			fc.width = w;
			fc.height = h;
			// ticket 0652 (city-renderer.md 17): each tapped constant at the wrap copy of this camera it was tapped through
			fc.wrapPeriod = ft.wrapPeriod;
			fc.overlayProjWrap[0] = ft.haveFog ? ft.overlayProjWrap[0] : nullptr;
			fc.overlayProjWrap[1] = ft.haveFog ? ft.overlayProjWrap[1] : nullptr;
			applyFrameCamera(m_scene, (uint32_t)target, fc, m_tapRows, &cam);
			noteWrap(pass, cam, ft.wrapPeriod);
		}
		DrawReport rep;
		c5r::Status s = m_scene.items.empty() && m_scene.cpuItems.empty() ? c5r::Status::Ok()
		                                                                  : m_renderer.draw(ctx, m_scene, v, env, &rep);
		if(s.ok() && kept)
		{
			// ticket 0709: the kept instances at the frame's camera (each constant at its wrap copy, as the tap's), after the
			// tap's own; their cubes by identity as the tap's
			m_inputs.fill(ps.kept, (uint32_t)target, env);
			applyFrameCamera(ps.kept, (uint32_t)target, keptCamera(ft, w, h), ps.keptRows, nullptr);
			DrawReport rk;
			s = m_renderer.draw(ctx, ps.kept, v, env, &rk);
			rep.items += rk.items;
			rep.drawn += rk.drawn;
			rep.skipped += rk.skipped;
			rep.envMissing += rk.envMissing;
		}
		if(st)
		{
			st->camConsts = cam.consts;
			st->wrapConsts = cam.wrapConsts;
			st->unfitConsts = cam.unfitConsts;
			st->shiftN = cam.n;
			st->shiftPxSum = cam.sumPx;
			st->shiftPxMax = cam.maxPx;
			st->items = rep.items;
			st->drawn = rep.drawn;
			st->skipped = rep.skipped;
			st->envMissing = rep.envMissing;
			st->unknownDraws = pass == 0 ? m_scene.unknownDraws : 0;
			st->problems = (uint32_t)rep.problems.size();
			st->envUnknown = m_inputs.counts().unknown;
		}
		return s;
	}
	void clearScene() override
	{
		m_scene = ModelScene();
		m_tapRows.clear();
		m_haveScene = false;
		// ticket 0709 / 0789: a load forgets every sighting, both passes, and the keys seen dropped
		for(PassStore& ps : m_ps)
		{
			ps.keep.clear();
			ps.seen.clear();
			ps.held.clear();
			ps.pending = false;
			ps.haveVp = false;
			ps.target = -1;
			ps.kept = ModelScene();
			ps.keptRows.clear();
		}
		m_seenDropped.keys.clear();
		m_haveFrame = false;
		m_frameTapIds.clear();
		m_frameKeptIds.clear();
		m_pool.clear();
		m_poolBytes = 0;
		m_tap = KeepTap();
	}
	void setDrawAll(bool all) override { m_drawAll = all; }
	void setKeep(const KeepOptions& o) override   // ticket 0709; 0789: the reflection pass's store only with the store's rules
	{
		m_keepOpt = o;
		m_ps[0].keep.configure(o);
		KeepOptions r = o;
		r.on = o.on && o.store;
		m_ps[1].keep.configure(r);
	}
	void setOwnership(const ModelOwnership& o) override { m_own = o; }   // ticket 0789
	std::string describe() const override
	{
		char b[768];
		const LibraryCounts& c = m_lib.counts();
		size_t prepared = 0;
		for(bool p : m_prepared)
			prepared += p ? 1 : 0;
		const InstallInputs::Counts& ic = m_inputs.counts();
		_snprintf_s(b, sizeof(b), _TRUNCATE,
		            "library %u files (%u ok), %u keys, %u collisions; wire index: %u files failed, %u textures missing, "
		            "%.1f s; open %.0f ms; renderer %s, %llu files drawable (%s), %u prepare failures; GPU %llu bytes; "
		            "PS inputs (4882): %u cube identities (%u cubes of %u names), %u made, %u unknown, %u failed, Buildings "
		            "volume from file %d, TerrainFOW volume from file %d, %.0f ms",
		            c.files, c.filesOk, c.keys, c.keyCollisions, m_index ? m_index->failedFiles() : 0u,
		            m_index ? m_index->missingTextures() : 0u, m_index ? m_index->buildSeconds() : 0.0, m_openMs,
		            m_deviceOk ? "made" : "not made", (unsigned long long)prepared,
		            HasPrepare<ModelRenderer>::value ? "ModelRenderer::prepare" : "assumed: buffers at first draw",
		            m_prepareFailed, (unsigned long long)m_renderer.gpuBytes(), ic.identities, ic.cubes, ic.candidates, ic.made,
		            ic.unknown, ic.failed, ic.buildingsFromFile ? 1 : 0, ic.fowFromFile ? 1 : 0, ic.openMs);
		return b;
	}

private:
	// Ticket 0652: the first record drawn through a wrap copy is noted once. Ticket 0659 (city-renderer.md 17.9): constants
	// more than a quarter wrap period from every copy (a far camera jump between the tap and the frame) are drawn at the
	// nearest copy and noted plainly with the counts, at most every 10 s; only constants more than two periods away (no
	// camera of this map: a fault) draw nothing and are named LOUD, the first time and then at most every 10 s.
	void noteWrap(int pass, const CameraShift& cam, float wrapPeriod)
	{
		const int p = pass ? 1 : 0;
		m_wrapTotal[p] += cam.wrapConsts;
		if(cam.wrapConsts && !m_wrapNoted[p])
		{
			m_wrapNoted[p] = true;
			RNote("models: the %s pass draws %u tapped constant(s) through a wrap copy of the frame's camera (wrap period %.1f; "
			      "ticket 0652; noted once)", p ? "reflection" : "scene", cam.wrapConsts, wrapPeriod);
		}
		const int64_t now = Qpc();
		if(cam.farConsts)
		{
			// ticket 0659: a far camera jump (a minimap click, a jump by a notification or LookAt) is a player's action, no
			// fault: the jump's frame draws the tapped records at the nearest copy; a plain note, at most every 10 s
			m_far[p].consts += cam.farConsts;
			m_far[p].frames++;
			m_far[p].inst += cam.farInstances;
			m_far[p].onScreen += cam.farOnScreen;
			m_far[p].onScreenView0 += cam.farOnScreenView0;
			if(m_far[p].noteQpc == 0 || QpcToMs(now - m_far[p].noteQpc) >= 10000.0)
			{
				m_far[p].noteQpc = now;
				RNote("models: camera jump of more than a quarter wrap period (wrap period %.1f): the %s pass draws %u tapped "
				      "constant(s) at the nearest copy, %u of their %u instance origins in the frame (at view 0: %u) (totals: %llu "
				      "constants in %llu frames, %llu of %llu origins in the frame, at view 0 %llu; ticket 0659, at most every 10 s)",
				      wrapPeriod, p ? "reflection" : "scene", cam.farConsts, cam.farOnScreen, cam.farInstances,
				      cam.farOnScreenView0, (unsigned long long)m_far[p].consts, (unsigned long long)m_far[p].frames,
				      (unsigned long long)m_far[p].onScreen, (unsigned long long)m_far[p].inst,
				      (unsigned long long)m_far[p].onScreenView0);
			}
		}
		if(!cam.unfitConsts)
			return;
		// a fault: tapped rows more than two wrap periods from the frame's camera are no camera of this map
		m_unfitTotal[p] += cam.unfitConsts;
		m_unfitFrames[p]++;
		if(m_unfitLoudQpc[p] == 0 || QpcToMs(now - m_unfitLoudQpc[p]) >= 10000.0)
		{
			m_unfitLoudQpc[p] = now;
			RNote("LOUD: models: %u tapped constant(s) of the %s pass lie more than two wrap periods from the frame's camera "
			      "(wrap period %.1f): no camera of this map, their records are not drawn this frame (totals: %llu constants in "
			      "%llu frames; drawn through a wrap copy %llu)",
			      cam.unfitConsts, p ? "reflection" : "scene", wrapPeriod, (unsigned long long)m_unfitTotal[p],
			      (unsigned long long)m_unfitFrames[p], (unsigned long long)m_wrapTotal[p]);
		}
	}
	// pass 0: the target with the largest area (the scene); pass 1: the largest other one (the water reflection)
	int targetOf(int pass) const { return TargetOf(m_scene, pass); }

	// ---- ticket 0709 ----
	// Every instance of materials 3-5 of `target` in the scene's items and CPU-built items: fn(cpu, index, instance, id, origin)
	template <class Fn>
	static void forEachKeepInstance(const ModelScene& s, uint32_t target, Fn fn)
	{
		for(size_t i = 0; i < s.items.size(); ++i)
		{
			const DrawItem& it = s.items[i];
			if(it.target != target || !KeepMaterial(it.material))
				continue;
			const uint32_t n = it.material == Material::Single ? 1u : it.instanceCount;
			for(uint32_t k = 0; k < n; ++k)
			{
				const size_t r3 = (size_t)it.instRow + (size_t)k * 4 + 3;
				if(r3 >= s.rows.size())
					break;
				fn(false, (uint32_t)i, k, KeepId(false, it.file, it.part, nullptr, it.material, s.rows[r3]), s.rows[r3]);
			}
		}
		for(size_t i = 0; i < s.cpuItems.size(); ++i)
		{
			const CpuDrawItem& it = s.cpuItems[i];
			if(it.target != target || !KeepMaterial(it.material))
				continue;
			const uint32_t n = it.material == Material::Single ? 1u : it.instanceCount;
			for(uint32_t k = 0; k < n; ++k)
			{
				const size_t r3 = (size_t)it.instRow + (size_t)k * 4 + 3;
				if(r3 >= s.rows.size())
					break;
				fn(true, (uint32_t)i, k, KeepId(true, 0, 0, &it.key, it.material, s.rows[r3]), s.rows[r3]);
			}
		}
	}
	// A constant's rows 0-3 (the view-projection it was tapped with) as 16 floats
	static bool constCamera(const ModelScene& s, uint32_t c, float vp[16])
	{
		if(c == kNoConst || c >= s.consts.size() || s.consts[c].rows < 4 || (size_t)s.consts[c].firstRow + 4 > s.rows.size())
			return false;
		for(int k = 0; k < 4; ++k)
		{
			const Float4& r = s.rows[s.consts[c].firstRow + k];
			vp[k * 4 + 0] = r.x;
			vp[k * 4 + 1] = r.y;
			vp[k * 4 + 2] = r.z;
			vp[k * 4 + 3] = r.w;
		}
		return true;
	}
	// The tap's main camera on the scene target: the VS cb1 constant most of its draws use (any flag; the wrap views'
	// draws use their own), its rows 0-3 as tapped
	static bool mainCamera(const TapFrame& f, const ModelScene& s, uint32_t target, float vp[16])
	{
		std::map<uint32_t, uint32_t> uses;
		for(const TapDraw& d : f.draws)
			if(d.target == target && d.vsCb1 != kNoConst)
				uses[d.vsCb1]++;
		uint32_t best = kNoConst, n = 0;
		for(const auto& kv : uses)
			if(kv.second > n)
			{
				best = kv.first;
				n = kv.second;
			}
		(void)s;
		if(best == kNoConst || best >= f.consts.size() || f.consts[best].rows < 4 ||
		   (size_t)f.consts[best].firstRow + 4 > f.rows.size())
			return false;
		for(int k = 0; k < 4; ++k)
		{
			const Float4& r = f.rows[f.consts[best].firstRow + k];
			vp[k * 4 + 0] = r.x;
			vp[k * 4 + 1] = r.y;
			vp[k * 4 + 2] = r.z;
			vp[k * 4 + 3] = r.w;
		}
		return true;
	}
	FrameCamera keptCamera(const FrameTextures& ft, uint32_t w, uint32_t h) const
	{
		FrameCamera fc;
		fc.viewProj = ft.viewProj;
		fc.overlayProj = ft.haveFog ? ft.overlayProj : nullptr;
		fc.fowAnim = ft.haveFog ? ft.fowAnim : nullptr;
		fc.eye = ft.haveEye ? ft.eye : nullptr;
		fc.width = w;
		fc.height = h;
		fc.wrapPeriod = ft.wrapPeriod;
		fc.overlayProjWrap[0] = ft.haveFog ? ft.overlayProjWrap[0] : nullptr;
		fc.overlayProjWrap[1] = ft.haveFog ? ft.overlayProjWrap[1] : nullptr;
		return fc;
	}
	// ---- ticket 0808 ----
	// RoadLayout's bridges: with a placer in the slot (srv_extras.h: the roads producer's, only while RoadLayout is in force
	// and its production is), every CPU-built bridge draw (its vertex buffer once seen with the bridge's texture, so its other
	// passes follow) is turned and moved onto our road's crossing of its hex edge (new instance rows appended to the frame's
	// rows), dropped where our layout draws no road across that edge, or kept as tapped; one bridge per edge and target.
	// Without a placer nothing changes (RoadLayout 0: the frame as before, byte for byte).
	void placeBridges(ModelScene& s)
	{
		for(const CpuDrawItem& it : s.cpuItems)
			if(srvextras::IsBridgeTexture(it.t0) && m_bridgeVbs.size() < 4096)
				m_bridgeVbs.insert(it.key.vb);
		const std::shared_ptr<const srvextras::BridgePlacer> pl = srvextras::GetBridgePlacer();
		if(!pl)
		{
			if(m_bridgeGen)
				RNote("models: bridges: no placer (RoadLayout's production withdrawn): the EXE's bridges as tapped");
			m_bridgeGen = 0;
			return;
		}
		if(pl->generation() != m_bridgeGen)
		{
			m_bridgeGen = pl->generation();
			m_bridgeDetail = 0;
			m_bridgeLogged.clear();
			RNote("models: bridges: placer %llu: the EXE's road and rail bridges onto RoadLayout's river crossings (totals so far: "
			     "seen %llu moved %llu dropped %llu kept %llu duplicate %llu fit_failed %llu)",
			     (unsigned long long)m_bridgeGen, (unsigned long long)m_bridgeT.seen, (unsigned long long)m_bridgeT.moved,
			     (unsigned long long)m_bridgeT.dropped, (unsigned long long)m_bridgeT.kept, (unsigned long long)m_bridgeT.duplicate,
			     (unsigned long long)m_bridgeT.fitFailed);
		}
		bool any = false;
		for(const CpuDrawItem& it : s.cpuItems)
			if(m_bridgeVbs.count(it.key.vb))
			{
				any = true;
				break;
			}
		if(!any)
			return;
		std::set<std::pair<uint32_t, uint64_t>> placed;   // (target, edge)
		std::vector<CpuDrawItem> keep;
		keep.reserve(s.cpuItems.size());
		for(const CpuDrawItem& item : s.cpuItems)
		{
			if(!m_bridgeVbs.count(item.key.vb))
			{
				keep.push_back(item);
				continue;
			}
			m_bridgeT.seen++;
			const CpuBuffer* vb = m_cpu.get(GeomKind::Vertex, item.key.vb);
			if(!vb || (size_t)item.instRow + 4 > s.rows.size())
			{
				m_bridgeT.noBuffer++;
				keep.push_back(item);
				continue;
			}
			float rin[16];
			memcpy(rin, &s.rows[item.instRow], sizeof(rin));
			const srvextras::BridgePlaceResult r = pl->place(rin, vb->bytes.data(), vb->bytes.size(), vb->stride);
			const char* what = "kept";
			if(r.fitFailed)
			{
				m_bridgeT.fitFailed++;
				what = "kept (no bridge axis in its mesh)";
				keep.push_back(item);
			}
			else if(r.move == srvextras::BridgeMove::Drop)
			{
				m_bridgeT.dropped++;
				what = "dropped (our layout draws no road across this edge)";
			}
			else if(r.move == srvextras::BridgeMove::Move)
			{
				if(!placed.insert(std::make_pair(item.target, r.edgeKey)).second)
				{
					m_bridgeT.duplicate++;
					what = "dropped (a second bridge on this edge)";
				}
				else
				{
					m_bridgeT.moved++;
					m_bridgeT.maxShift = (std::max)(m_bridgeT.maxShift, r.shift);
					m_bridgeT.maxTurn = (std::max)(m_bridgeT.maxTurn, std::fabs(r.turnDeg));
					CpuDrawItem moved = item;
					moved.instRow = (uint32_t)s.rows.size();
					for(int k = 0; k < 4; ++k)
						s.rows.push_back(Float4{r.rows[4 * k], r.rows[4 * k + 1], r.rows[4 * k + 2], r.rows[4 * k + 3]});
					keep.push_back(moved);
					what = "moved";
				}
			}
			else
			{
				m_bridgeT.kept++;
				what = "kept (not at a hex edge)";
				keep.push_back(item);
			}
			if(m_bridgeDetail < 24 && m_bridgeLogged.insert(item.key.vb ^ ((uint64_t)item.target << 56) ^ (uint64_t)r.move).second)
			{
				m_bridgeDetail++;
				const uint64_t e = r.edgeKey ? r.edgeKey - 1 : 0;
				RLog("models: bridge vb %016llx target %u at (%.1f, %.1f, %.1f): %s; edge plot %llu dir %llu, %.1f from its middle, "
				     "moved %.1f, turned %.1f deg",
				     (unsigned long long)item.key.vb, item.target, rin[12], rin[13], rin[14], what,
				     (unsigned long long)(r.edgeKey ? e / 6 : 0), (unsigned long long)(r.edgeKey ? e % 6 : 0), r.edgeDist, r.shift,
				     r.turnDeg);
			}
		}
		s.cpuItems.swap(keep);
	}
	// ---- ticket 0789 ----
	// The residual flash: tap P arrives after frame P was drawn (from tap P - 1 and the store). Its DROPPED instances on the
	// scene target whose origin lies on screen at frame P's camera and that frame P did not draw were drawn by nobody in
	// frame P: counted (missed), split by whether the store held them (missedKnown). Under exact lockstep this is the
	// owner's flash measured in the server (0788's diagnostic, made a counter); not exact for an instance whose origin is
	// off screen while its body shows, or one entering where the frame's camera differs from tap P's.
	void checkMissed(const TapFrame& f, const ModelScene& s)
	{
		if(!m_haveFrame || (int64_t)f.presentCount != m_frameTap + 1)
			return;
		m_haveFrame = false;
		const int target = TargetOf(s, 0);
		if(target < 0)
			return;
		uint64_t miss = 0;
		forEachKeepInstance(s, (uint32_t)target, [&](bool cpu, uint32_t index, uint32_t, uint64_t id, const Float4& o) {
			const uint32_t td = cpu ? s.cpuItems[index].tapDraw : s.items[index].tapDraw;
			if(td >= f.draws.size() || !(f.draws[td].flags & VPMTAP_DRAW_DROPPED))
				return;
			if(m_frameTapIds.count(id) || m_frameKeptIds.count(id))
				return;
			const float* m = m_frameVp;
			const float cx = o.x * m[0] + o.y * m[4] + o.z * m[8] + m[12];
			const float cy = o.x * m[1] + o.y * m[5] + o.z * m[9] + m[13];
			const float cw = o.x * m[3] + o.y * m[7] + o.z * m[11] + m[15];
			if(!(cw > 1e-6f) || std::fabs(cx / cw) > 1.0f || std::fabs(cy / cw) > 1.0f)
				return;
			miss++;
			if(m_ps[0].keep.has(id))
				m_missedKnown++;
		});
		m_missedChecked++;
		m_missed += miss;
		if(miss)
			m_missedFrames++;
	}
	// The pass's store is active: the scene pass with the keep on; the reflection pass only with the store's rules
	bool storeActive(int pass) const { return m_keepOpt.on && (pass == 0 || m_keepOpt.store); }
	// R5 (architect-2026-11-25-retained-world.md 6.3): a record of tap C the server draws at P: every DROPPED one; with
	// the store's rules also a PASSED one (not unhashed) whose key the compositor drops at P: in the identity set of tap
	// C's setGeneration (the set P's drops use; offline: a key seen dropped since the load) for its class, under a mode
	// bit the compositor latches for P (live: the server's mode bits; offline: tap C's latch and dropped classes). A
	// record the EXE draws as well lands on the same pixels; one drawn by neither is the flash.
	bool drawsAtP(const TapDraw& d, const TapFrame& f, uint32_t instances)
	{
		if(ServerDrawsRecord(d.flags))   // DROPPED, or HELD (ticket 0788: passed this frame, dropped from the next on)
			return true;
		if(!m_keepOpt.on || !m_keepOpt.store || (d.flags & VPMTAP_DRAW_UNHASHED))
			return false;
		const OwnedKeys* set = nullptr;
		if(m_own.live)
		{
			if(!f.setGeneration || m_own.sets.empty())
				return false;   // no set at P: the compositor drops nothing
			for(size_t i = m_own.sets.size(); i-- > 0 && !set;)
				if(m_own.sets[i].first == f.setGeneration)
					set = m_own.sets[i].second.get();
			if(!set)
				set = m_own.sets.back().second.get();   // a generation older than the history: in doubt, the newest
		}
		else
			set = &m_seenDropped;
		if(!set)
			return false;
		const VPModelKey k = Wire(d.key);
		const VPModelSetKey* sk = set->find(k);
		if(!sk)
			return false;
		const uint8_t cls = classOf(d.material);
		const uint32_t setBits = sk->cls == VPMODEL_CLASS_NONE || sk->cls == cls ? sk->bits : 0u;
		uint32_t bit = PairBit(d.material);
		if(!bit)
			bit = setBits == VPMODE_CITIES || setBits == VPMODE_LANDMARKS ? setBits : 0u;
		if(!bit || !(setBits & bit))
			return false;
		const uint32_t bits = m_own.live ? m_own.bits : m_offlineBits;
		if(!(bits & bit))
			return false;
		m_tapHandRecs++;
		m_tapHandInst += instances;
		return true;
	}
	// A constant's content as a pooled block (shared by content across taps; the pool holds weak references)
	std::shared_ptr<const ConstBlock> pooledConst(const ModelScene& s, uint32_t c,
	                                              std::unordered_map<uint32_t, std::shared_ptr<const ConstBlock>>& local)
	{
		if(c == kNoConst || c >= s.consts.size())
			return nullptr;
		const auto l = local.find(c);
		if(l != local.end())
			return l->second;
		const TapConst& tc = s.consts[c];
		if((size_t)tc.firstRow + tc.rows > s.rows.size())
			return nullptr;
		const Float4* r = tc.rows ? &s.rows[tc.firstRow] : nullptr;
		uint64_t h = r ? VPModelHash64(r, (size_t)tc.rows * sizeof(Float4)) : 0;
		h ^= ((uint64_t)tc.stage << 56) ^ ((uint64_t)tc.rows << 32);
		std::shared_ptr<const ConstBlock> b;
		const auto it = m_pool.find(h);
		if(it != m_pool.end())
		{
			b = it->second.lock();
			if(b && (b->c.stage != tc.stage || b->rows.size() != tc.rows ||
			         (tc.rows && memcmp(b->rows.data(), r, (size_t)tc.rows * sizeof(Float4)))))
				b.reset();   // a hash collision: a block of its own, not pooled
		}
		if(!b)
		{
			auto nb = std::make_shared<ConstBlock>();
			nb->c = tc;
			nb->c.firstRow = 0;
			if(tc.rows)
				nb->rows.assign(r, r + tc.rows);
			b = nb;
			if(it == m_pool.end() || it->second.expired())
			{
				m_pool[h] = b;
				m_poolBytes += (uint64_t)tc.rows * sizeof(Float4) + sizeof(ConstBlock);
			}
		}
		local.emplace(c, b);
		return b;
	}
	void sweepPool()
	{
		m_acceptsSincePoolSweep = 0;
		uint64_t bytes = 0;
		for(auto it = m_pool.begin(); it != m_pool.end();)
		{
			const std::shared_ptr<const ConstBlock> b = it->second.lock();
			if(!b)
				it = m_pool.erase(it);
			else
			{
				bytes += (uint64_t)b->rows.size() * sizeof(Float4) + sizeof(ConstBlock);
				++it;
			}
		}
		m_poolBytes = bytes;
	}
	// The sightings of the drawn records of m_scene (before any camera is put in) on `target`: own copies
	void makeSightings(const TapFrame& f, uint32_t target, std::vector<Keep::Seen>& out)
	{
		out.clear();
		std::map<uint32_t, GroundBox> boxes;   // per VS cb1 constant: the camera it was tapped with
		std::unordered_map<uint32_t, std::shared_ptr<const ConstBlock>> local;
		std::unordered_map<uint32_t, std::shared_ptr<const CpuDrawItem>> cpuLocal;
		const ModelScene& s = m_scene;
		forEachKeepInstance(s, target, [&](bool cpu, uint32_t index, uint32_t inst, uint64_t id, const Float4& o) {
			const uint32_t vs = cpu ? s.cpuItems[index].vsCb1 : s.items[index].vsCb1;
			const uint32_t psc = cpu ? s.cpuItems[index].psCb1 : s.items[index].psCb1;
			const uint32_t instRow = cpu ? s.cpuItems[index].instRow : s.items[index].instRow;
			const uint32_t tapDraw = cpu ? s.cpuItems[index].tapDraw : s.items[index].tapDraw;
			const size_t r0 = (size_t)instRow + (size_t)inst * 4;
			if(r0 + 4 > s.rows.size())
				return;
			auto b = boxes.find(vs);
			if(b == boxes.end())
			{
				float vp[16];
				GroundBox g;
				if(constCamera(s, vs, vp))
					g = CullGroundBox(vp);
				b = boxes.emplace(vs, g).first;
			}
			Keep::Seen sn;
			sn.id = id;
			sn.x = o.x;
			sn.y = o.y;
			sn.box = b->second;
			sn.bit = tapDraw < f.draws.size() ? DrawBit(f.draws[tapDraw]) : ~0u;
			StoredInst& p = sn.payload;
			p.cpu = cpu;
			if(cpu)
			{
				auto c = cpuLocal.find(index);
				if(c == cpuLocal.end())
					c = cpuLocal.emplace(index, std::make_shared<const CpuDrawItem>(s.cpuItems[index])).first;
				p.cpuItem = c->second;
			}
			else
				p.item = s.items[index];
			for(int k = 0; k < 4; ++k)
				p.rows[k] = s.rows[r0 + (size_t)k];
			p.vs = pooledConst(s, vs, local);
			p.ps = pooledConst(s, psc, local);
			out.push_back(std::move(sn));
		});
	}
	// Ticket 0789: R3's still-camera bridging (LOUD) and the memory bound's evictions (LOUD), at most every 10 s per pass
	void noteStore(int pass, const KeepStats& k, uint64_t evicted, bool cut)
	{
		const int p = pass ? 1 : 0;
		const int64_t now = Qpc();
		const bool bridged = k.absent || k.cut;
		if(bridged)
			m_note[p].bridged++;
		if(bridged && k.sameCamera)
		{
			m_note[p].still++;
			if(m_note[p].stillQpc == 0 || QpcToMs(now - m_note[p].stillQpc) >= 10000.0)
			{
				m_note[p].stillQpc = now;
				RNote("LOUD: models: the %s pass's store drew %u instance(s) the drawn tap should have held at a still camera "
				      "(absent %u, cut tap %u%s): a fault in the tap is hidden from the eye, not from the log (R3; totals: %llu such "
				      "frames, %llu bridged frames; ticket 0789, at most every 10 s)",
				      p ? "reflection" : "scene", k.absent + k.cut, k.absent, k.cut, cut ? ", the tap is cut" : "",
				      (unsigned long long)m_note[p].still, (unsigned long long)m_note[p].bridged);
			}
		}
		if(evicted > m_note[p].evicted && (m_note[p].evictQpc == 0 || QpcToMs(now - m_note[p].evictQpc) >= 10000.0))
		{
			m_note[p].evictQpc = now;
			m_note[p].evicted = evicted;
			RNote("LOUD: models: the %s pass's store reached its bound (%llu entries): %llu oldest entries evicted so far "
			      "(render_models_store_max_entries; ticket 0789, at most every 10 s)",
			      p ? "reflection" : "scene", (unsigned long long)m_keepOpt.maxEntries, (unsigned long long)evicted);
		}
	}
	// The kept instances of this frame into ps.kept (one record per instance: its item, its four rows, its constants as
	// tapped), their tapped camera rows into ps.keptRows; the count
	size_t buildKept(PassStore& ps, uint32_t target, const KeepTap& tap, const FrameTextures& ft, KeepStats* kst,
	                 std::unordered_set<uint64_t>* keptIds)
	{
		ps.kept = ModelScene();
		ps.keptRows.clear();
		if(keptIds)
			keptIds->clear();
		if(!storeActive(&ps == &m_ps[1] ? 1 : 0) || !ft.haveViewProj || ps.keep.size() == 0 ||
		   (!ps.haveVp && (!m_keepOpt.store || tap.complete)))
			return 0;
		std::vector<const Keep::Entry*> sel;
		float noVp[16];
		const float* tapVp = ps.tapVp;
		if(!ps.haveVp)
		{
			memcpy(noVp, ft.viewProj, sizeof(noVp));   // a cut tap without a camera: (g) needs only the frame's box
			tapVp = noVp;
		}
		ps.keep.select(tap, ps.held, tapVp, ft.viewProj, ft.wrapPeriod, sel, kst);
		if(sel.empty())
			return 0;
		if(keptIds)
			for(const Keep::Entry* e : sel)
				keptIds->insert(e->id);
		ps.kept.frameSeq = m_scene.frameSeq;
		ps.kept.targets = m_scene.targets;
		std::unordered_map<const ConstBlock*, uint32_t> consts;
		const auto mapConst = [&](const std::shared_ptr<const ConstBlock>& b) -> uint32_t {
			if(!b)
				return kNoConst;
			const auto it = consts.find(b.get());
			if(it != consts.end())
				return it->second;
			TapConst tc = b->c;
			tc.firstRow = (uint32_t)ps.kept.rows.size();
			tc.rows = (uint32_t)b->rows.size();
			ps.kept.rows.insert(ps.kept.rows.end(), b->rows.begin(), b->rows.end());
			const uint32_t idx = (uint32_t)ps.kept.consts.size();
			ps.kept.consts.push_back(tc);
			consts.emplace(b.get(), idx);
			return idx;
		};
		size_t n = 0;
		for(const Keep::Entry* e : sel)
		{
			const StoredInst& r = e->payload;
			if(r.cpu && !r.cpuItem)
				continue;
			const uint32_t row = (uint32_t)ps.kept.rows.size();
			ps.kept.rows.insert(ps.kept.rows.end(), r.rows, r.rows + 4);
			const uint32_t vs = mapConst(r.vs), psc = mapConst(r.ps);
			if(!r.cpu)
			{
				DrawItem it = r.item;
				it.instRow = row;
				it.instanceCount = 1;
				it.target = target;
				it.vsCb1 = vs;
				it.psCb1 = psc;
				ps.kept.items.push_back(it);
			}
			else
			{
				CpuDrawItem it = *r.cpuItem;
				it.instRow = row;
				it.instanceCount = 1;
				it.target = target;
				it.vsCb1 = vs;
				it.psCb1 = psc;
				ps.kept.cpuItems.push_back(it);
			}
			n++;
		}
		keepTapCamera(ps.kept, ps.keptRows);
		return n;
	}
	c5m::SqliteTables m_db;
	c5r::gamefs::GameFiles m_files;
	c5r::cities::ArtCatalogue m_art;
	ModelLibrary m_lib;
	std::unique_ptr<WireIndex> m_index;
	BuildingPrograms m_programs;
	ModelRenderer m_renderer;
	InstallInputs m_inputs;                                  // ticket 4882
	TapCameraRows m_tapRows;                                 // ticket 4882: the tap's VS cb1 rows 0-3 per constant (0598: and 16)
	std::vector<std::set<KeySource>> m_fileSources;
	std::vector<bool> m_prepared;
	size_t m_next = 0;
	uint32_t m_prepareFailed = 0;
	bool m_deviceOk = false;
	ModelScene m_scene;
	bool m_haveScene = false;
	// ticket 0709 / 0789: per pass (0 scene, 1 reflection) the store, the accepted tap's instances and camera and the
	// frame's kept records (PassStore); the options; the accepted tap's header part (KeepTap), its Present, its R5 records;
	// the ownership (R5, the class check), the keys seen dropped and the classes the tap dropped (the offline stand-ins);
	// the constant pool
	PassStore m_ps[2];
	KeepOptions m_keepOpt;
	KeepTap m_tap;
	int64_t m_keepPresent = 0;
	uint32_t m_tapHandRecs = 0, m_tapHandInst = 0;
	ModelOwnership m_own;
	OwnedKeys m_seenDropped;
	uint32_t m_offlineBits = 0;
	std::unordered_map<uint64_t, std::weak_ptr<const ConstBlock>> m_pool;
	uint64_t m_poolBytes = 0;
	uint32_t m_acceptsSincePoolSweep = 0;
	struct StoreNotes
	{
		uint64_t bridged = 0, still = 0, evicted = 0;
		int64_t stillQpc = 0, evictQpc = 0;
	};
	StoreNotes m_note[2];
	// the residual-flash check (checkMissed): the last scene pass's drawn instances, its camera and tap
	std::unordered_set<uint64_t> m_frameTapIds, m_frameKeptIds;
	float m_frameVp[16] = {};
	int64_t m_frameTap = 0;
	bool m_haveFrame = false;
	uint64_t m_missed = 0, m_missedKnown = 0, m_missedFrames = 0, m_missedChecked = 0;
	bool m_drawAll = false;
	ResolveReport m_lastReport;
	double m_openMs = 0;
	// ticket 4879
	CpuMeshStore m_cpu;
	std::map<WireKey, Material> m_cpuKeys;
	bool m_cpuChanged = false;
	CpuResolveReport m_cpuRep;
	uint32_t m_cpuNoTex = 0;
	// ticket 0808: RoadLayout's bridges (the placer of srv_extras.h, published by the roads producer)
	std::unordered_set<uint64_t> m_bridgeVbs;   // vertex buffers drawn with the bridge's texture (any pass)
	uint64_t m_bridgeGen = 0;
	uint32_t m_bridgeDetail = 0;                // detail lines written for the placer in force
	std::unordered_set<uint64_t> m_bridgeLogged;  // (vertex buffer, target, outcome) already written for it
	struct BridgeTotals
	{
		uint64_t seen = 0, moved = 0, dropped = 0, kept = 0, duplicate = 0, fitFailed = 0, noBuffer = 0;
		float maxShift = 0, maxTurn = 0;
	};
	BridgeTotals m_bridgeT;
	bool m_wrapNoted[2] = {false, false};
	uint64_t m_wrapTotal[2] = {0, 0}, m_unfitTotal[2] = {0, 0}, m_unfitFrames[2] = {0, 0};
	int64_t m_unfitLoudQpc[2] = {0, 0};
	// ticket 0659: per pass, the far jumps' totals and the last note
	struct FarTotals
	{
		uint64_t consts = 0, frames = 0, inst = 0, onScreen = 0, onScreenView0 = 0;
		int64_t noteQpc = 0;
	};
	FarTotals m_far[2];
};

std::unique_ptr<ModelLayer> MakeLayer() { return std::unique_ptr<ModelLayer>(new C5rLayer()); }
struct Registrar
{
	Registrar() { RegisterModelLayerFactory(&MakeLayer); }
} s_registrar;

}  // namespace
}  // namespace srvmodels
#endif  // SRV_MODELS_HAVE_C5R
