// models_test.cpp - the server's side of the model tap (epic 0561, ticket 4875): the tap reader and the identity-set
// publisher (server/src/render/landmarks/srv_model_tap.cpp), the mode writer of the model bits (server/src/render/cities/
// srv_models_mode.cpp), the module (server/src/render/landmarks/srv_models_scene.cpp) with a stand-in layer, and the feed's
// model database flag (server/src/feed/srv_feed.cpp). No game data, no D3D device: a fake compositor serves frames through
// real named sections of the test's own names. Written 2026-11-18 for this repository (GPL-3.0-or-later).
//
//   models_test.exe                 lists the cases ("CASE <name>" lines), exit 0
//   models_test.exe <case> [DIR]    runs one case; "CASE <name> PASS" and exit 0, or "FAIL ..." lines, "CASE <name> FAIL"
//                                   and exit 1; exit 2 for an unknown case
#include "landmarks/srv_model_tap.h"
#include "landmarks/srv_models_scene.h"
#include "landmarks/srv_model_geom.h"
#include "cities/srv_models_mode.h"
#include "feed/srv_feed.h"

#include <cmath>
#include <functional>
#include <map>
#include <stdio.h>
#include <string>
#include <unordered_set>
#include <vector>

using namespace srvmodels;

static int g_fail = 0;
static std::wstring g_dir;

#define CHECK(...)                                                                  \
	do                                                                              \
	{                                                                               \
		if(!(__VA_ARGS__))                                                          \
		{                                                                           \
			printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__);                   \
			fflush(stdout);                                                         \
			g_fail++;                                                               \
		}                                                                           \
	} while(0)

// ---- the stand-in for srv_render_scene.cpp's factory (the test does not link the scene) ----
namespace srvmodels {
static ModelsSceneFactory g_sceneFactory = nullptr;
void RegisterModelsSceneFactory(ModelsSceneFactory f) { g_sceneFactory = f; }
std::unique_ptr<ModelsScene> MakeModelsScene() { return g_sceneFactory ? g_sceneFactory() : nullptr; }
}  // namespace srvmodels

static int64_t Ms(double ms) { return (int64_t)(ms * (double)srvrender::QpcFreq() / 1000.0); }

// ---- a fake compositor: the control block and the frame section under test names ----
struct FakeCompositor
{
	std::wstring ctlName, dataName;
	HANDLE hc = nullptr, hd = nullptr;
	VPShellModelTapControl* ctl = nullptr;
	uint8_t* data = nullptr;
	uint32_t pid = 0, slotBytes = 256u * 1024u;
	uint64_t seq = 0;
	explicit FakeCompositor(const wchar_t* tag, bool makeData = true)
	{
		pid = 40000u + (GetCurrentProcessId() % 10000u);
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\C5Test4875Ctl.%s.%u", tag, GetCurrentProcessId());
		ctlName = n;
		_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\C5Test4875Data.%s.%u", tag, GetCurrentProcessId());
		dataName = n;
		hc = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELTAP_CONTROL_BYTES, ctlName.c_str());
		ctl = (VPShellModelTapControl*)MapViewOfFile(hc, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_MODELTAP_CONTROL_BYTES);
		if(makeData)
		{
			hd = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELTAP_SLOTS * slotBytes,
			                        dataName.c_str());
			data = (uint8_t*)MapViewOfFile(hd, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		}
		ctl->magic = VPSHELL_MODELTAP_MAGIC;
		ctl->version = VPSHELL_MODELTAP_VERSION;
		ctl->state = VPMTAP_RUNNING;
		ctl->exePid = pid;
		ctl->slots = VPSHELL_MODELTAP_SLOTS;
		ctl->slotBytes = slotBytes;
	}
	~FakeCompositor()
	{
		if(data)
			UnmapViewOfFile(data);
		if(hd)
			CloseHandle(hd);
		if(ctl)
			UnmapViewOfFile(ctl);
		if(hc)
			CloseHandle(hc);
	}
	static std::vector<uint8_t> Image(uint64_t frameSeq, uint32_t extra = 64, uint8_t fill = 0x5A)
	{
		std::vector<uint8_t> img(sizeof(VPModelTapFrame) + extra, fill);
		VPModelTapFrame h;
		memset(&h, 0, sizeof(h));
		h.magic = VPSHELL_MODELTAP_MAGIC;
		h.version = VPSHELL_MODELTAP_VERSION;
		h.headerBytes = sizeof(VPModelTapFrame);
		h.drawBytes = sizeof(VPModelTapDraw);
		h.frameSeq = frameSeq;
		h.imageBytes = (uint32_t)img.size();
		h.fnv1a64 = VPModelHash64(img.data() + sizeof(h), img.size() - sizeof(h));
		memcpy(img.data(), &h, sizeof(h));
		return img;
	}
	// publish frame n: the slot first, then the control block under its seqlock
	void publish(const std::vector<uint8_t>& img, uint64_t n)
	{
		const uint32_t slot = (uint32_t)(n % VPSHELL_MODELTAP_SLOTS);
		memcpy(data + (size_t)slot * slotBytes, img.data(), img.size());
		VPModelTapFrame h;
		memcpy(&h, img.data(), sizeof(h));
		ctl->seq = ctl->seq + 1;
		ctl->frameSeq = n;
		ctl->slot = slot;
		ctl->frameBytes = (uint32_t)img.size();
		ctl->fnv1a64 = h.fnv1a64;
		ctl->seq = ctl->seq + 1;
		seq = n;
	}
	void next() { publish(Image(seq + 1), seq + 1); }
	void editBlock(const std::function<void(VPShellModelTapControl&)>& f)
	{
		ctl->seq = ctl->seq + 1;
		f(*ctl);
		ctl->seq = ctl->seq + 1;
	}
};

// a validator that checks the magic and can be told to refuse
struct Val
{
	c5r::Err refuse = c5r::Err::Ok;
	int calls = 0;
	static c5r::Status Run(void* ctx, const uint8_t* p, size_t n)
	{
		Val* v = (Val*)ctx;
		v->calls++;
		if(v->refuse != c5r::Err::Ok)
			return c5r::Status::fail(v->refuse, "refused by the test");
		if(n < sizeof(VPModelTapFrame) || ((const VPModelTapFrame*)p)->magic != VPSHELL_MODELTAP_MAGIC)
			return c5r::Status::fail(c5r::Err::BadMagic, "magic");
		return c5r::Status::Ok();
	}
};

static void CaseReaderAccepts()
{
	FakeCompositor fc(L"acc");
	ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
	Val v;
	r.setValidator(&Val::Run, &v);
	r.setWantBits(VPMODE_CITIES | VPMODE_LANDMARKS | 0x8u);   // bit 3 is not ours: masked
	CHECK(!r.poll());   // seq 0: never written
	CHECK(r.controlOpen() && fc.ctl->wantBits == (VPMODE_CITIES | VPMODE_LANDMARKS));
	CHECK(r.counters().never == 1);
	const std::vector<uint8_t> img = FakeCompositor::Image(1, 200, 0x11);
	fc.publish(img, 1);
	CHECK(r.poll());
	CHECK(r.haveImage() && r.frameSeq() == 1 && *r.image() == img);
	CHECK(fc.ctl->ackFrameSeq == 1 && fc.ctl->ackResult == 0 && fc.ctl->ackServerPid == GetCurrentProcessId());
	CHECK(!r.poll());   // the same frame again: nothing
	CHECK(r.counters().accepted == 1 && v.calls == 1);
	fc.next();
	fc.next();   // frame 2 never read
	CHECK(r.poll() && r.frameSeq() == 3 && r.counters().skipped == 1);
	// the file form
	const std::wstring path = g_dir + L"\\frame000001.mtap";
	CHECK(r.writeCurrentImage(path).ok());
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	CHECK(f != INVALID_HANDLE_VALUE);
	std::vector<uint8_t> back(4096);
	DWORD got = 0;
	ReadFile(f, back.data(), (DWORD)back.size(), &got, nullptr);
	CloseHandle(f);
	back.resize(got);
	CHECK(back == *r.image());
	ModelTapReader r2(fc.pid, L"Local\\C5Test4875None", L"Local\\C5Test4875None");
	r2.setValidator(&Val::Run, &v);
	CHECK(!r2.writeCurrentImage(path).ok());   // NotFound before an image
	CHECK(r2.loadImage(back.data(), back.size()).ok() && r2.frameSeq() == 3 && r2.counters().fileLoads == 1);
	v.refuse = c5r::Err::Truncated;
	const c5r::Status bad = r2.loadImage(back.data(), back.size());
	CHECK(!bad.ok() && bad.code == c5r::Err::Truncated && r2.counters().fileRefused == 1 && r2.frameSeq() == 3);
	DeleteFileW(path.c_str());
}

// one refusal: the block edit / frame, then the reason, the ackResult and that the last good image stays
static void Refusal(const char* what, uint32_t reason, int32_t ack, const std::function<void(FakeCompositor&)>& make,
                    c5r::Err valErr = c5r::Err::Ok)
{
	FakeCompositor fc(L"ref");
	ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
	Val v;
	r.setValidator(&Val::Run, &v);
	fc.next();
	CHECK(r.poll());
	const std::shared_ptr<const std::vector<uint8_t>> good = r.image();
	v.refuse = valErr;
	make(fc);
	const bool acc = r.poll();
	const bool ok = !acc && r.counters().refusedBy[reason] == 1 && r.counters().refused == 1 && r.lastResult() == ack &&
	                r.image() == good && r.frameSeq() == 1;
	if(!ok)
		printf("  %s: accepted %d, refusedBy %llu, lastResult %d (expected %d): %s\n", what, acc ? 1 : 0,
		       (unsigned long long)r.counters().refusedBy[reason], r.lastResult(), ack, r.lastWhy().c_str());
	CHECK(ok);
	// the ack names the refused frame with its code (but for the block's own refusals, which ack too)
	CHECK(fc.ctl->ackResult == ack && fc.ctl->ackFrameSeq == fc.ctl->frameSeq);
}

static void CaseReaderRefusals()
{
	Refusal("block magic", MREF_BLOCK_MAGIC, MTACK_VERSION, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.magic = 0x1234; });
	});
	Refusal("block version", MREF_BLOCK_VERSION, MTACK_VERSION, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.version = 2; });
	});
	Refusal("block pid", MREF_BLOCK, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.exePid += 1; });
	});
	Refusal("block slots", MREF_BLOCK, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.slots = 4; });
	});
	Refusal("block slot bytes small", MREF_BLOCK, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.slotBytes = 1024; });
	});
	Refusal("block slot bytes big", MREF_BLOCK, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.slotBytes = (VPSHELL_MODELTAP_SLOT_KB_MAX + 1) * 1024u; });
	});
	Refusal("slot index", MREF_SLOT, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.slot = (c.slot + 1) % 3; });
	});
	Refusal("frame bytes past the slot", MREF_SLOT, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([&](VPShellModelTapControl& c) { c.frameBytes = fc.slotBytes + 16; });
	});
	Refusal("frame bytes below the header", MREF_SLOT, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.frameBytes = 64; });
	});
	Refusal("seq", MREF_SEQ, MTACK_TORN, [](FakeCompositor& fc) {
		std::vector<uint8_t> img = FakeCompositor::Image(7);   // the slot says frame 7, the block 2
		fc.publish(img, 2);
	});
	Refusal("hash", MREF_HASH, MTACK_HASH, [](FakeCompositor& fc) {
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.fnv1a64 ^= 1; });
	});
	Refusal("content", MREF_CONTENT, (int32_t)c5r::Err::BadLayout, [](FakeCompositor& fc) { fc.next(); }, c5r::Err::BadLayout);
	Refusal("content truncated", MREF_CONTENT, (int32_t)c5r::Err::Truncated, [](FakeCompositor& fc) { fc.next(); },
	        c5r::Err::Truncated);
	// the frame section missing: open (-2)
	{
		FakeCompositor fc(L"nodata", false);
		ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
		Val v;
		r.setValidator(&Val::Run, &v);
		fc.editBlock([](VPShellModelTapControl& c) {
			c.frameSeq = 1;
			c.slot = 1;
			c.frameBytes = 256;
		});
		CHECK(!r.poll() && r.counters().refusedBy[MREF_OPEN] == 1 && r.lastResult() == MTACK_OPEN && fc.ctl->ackResult == MTACK_OPEN);
	}
	// no validator: content refused (Unsupported), never accepted
	{
		FakeCompositor fc(L"noval");
		ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
		fc.next();
		CHECK(!r.poll() && r.counters().refusedBy[MREF_CONTENT] == 1 && r.lastResult() == (int32_t)c5r::Err::Unsupported);
	}
	// state not RUNNING: nothing read, nothing acked
	{
		FakeCompositor fc(L"off");
		ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
		Val v;
		r.setValidator(&Val::Run, &v);
		fc.next();
		fc.editBlock([](VPShellModelTapControl& c) { c.state = VPMTAP_FAILED; });
		CHECK(!r.poll() && r.counters().notRunning == 1 && fc.ctl->ackFrameSeq == 0 && v.calls == 0);
	}
	// busy: the seqlock odd for the whole poll
	{
		FakeCompositor fc(L"busy");
		ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
		Val v;
		r.setValidator(&Val::Run, &v);
		fc.next();
		fc.ctl->seq = fc.ctl->seq + 1;   // odd
		CHECK(!r.poll() && r.counters().busy == 1 && v.calls == 0);
		fc.ctl->seq = fc.ctl->seq + 1;
		CHECK(r.poll());
	}
}

// torn: the producer passes our slot while we copy; simulated by the validator? No: the torn check runs before it. The
// block names frame n + 2 when the copy ends only if a producer ran in between: a thread publishes in a tight loop.
static void CaseReaderTorn()
{
	FakeCompositor fc(L"torn");
	ModelTapReader r(fc.pid, fc.ctlName, fc.dataName);
	Val v;
	r.setValidator(&Val::Run, &v);
	// a big frame so the copy takes a while, and a producer thread that publishes as fast as it can
	const std::vector<uint8_t> big = FakeCompositor::Image(1, fc.slotBytes - 256);
	fc.publish(big, 1);
	volatile LONG stop = 0;
	HANDLE th = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
		FakeCompositor* f = (FakeCompositor*)((void**)p)[0];
		volatile LONG* s = (volatile LONG*)((void**)p)[1];
		std::vector<uint8_t> img = FakeCompositor::Image(0, f->slotBytes - 256);
		while(!*s)
		{
			const uint64_t n = f->seq + 1;
			memcpy(img.data() + offsetof(VPModelTapFrame, frameSeq), &n, 8);
			f->publish(img, n);
		}
		return 0;
	}, (LPVOID) new void*[2]{&fc, (void*)&stop}, 0, nullptr);
	int polls = 0;
	while(polls < 20000 && r.counters().refusedBy[MREF_TORN] + r.counters().refusedBy[MREF_SEQ] + r.counters().refusedBy[MREF_HASH] == 0)
	{
		r.poll();
		polls++;
	}
	InterlockedExchange(&stop, 1);
	WaitForSingleObject(th, 5000);
	CloseHandle(th);
	const uint64_t torn = r.counters().refusedBy[MREF_TORN], seq = r.counters().refusedBy[MREF_SEQ];
	printf("  torn: %d polls, accepted %llu, torn %llu, seq %llu, hash %llu\n", polls, (unsigned long long)r.counters().accepted,
	       (unsigned long long)torn, (unsigned long long)seq, (unsigned long long)r.counters().refusedBy[MREF_HASH]);
	CHECK(torn + seq >= 1);   // a racing producer is caught (a torn copy is never accepted: every accepted image is whole)
	if(r.image())
	{
		VPModelTapFrame h;
		memcpy(&h, r.image()->data(), sizeof(h));
		CHECK(h.frameSeq == r.frameSeq());
	}
}

// ---- the identity set ----
static VPModelKey Key(uint64_t ib, uint64_t vb, uint32_t start = 0)
{
	VPModelKey k;
	memset(&k, 0, sizeof(k));
	k.ib = ib;
	k.vb = vb;
	k.t0 = ib ^ 0x77;
	k.startIndex = start;
	k.indexCount = 30;
	return k;
}
static KeyInfo Info(uint64_t ib, KeyFamily f, KeySource s, bool skinned)
{
	KeyInfo i;
	i.key = Key(ib, ib + 1000);
	i.family = f;
	i.source = s;
	i.skinned = skinned;
	return i;
}

static void CaseSetBuild()
{
	std::vector<KeyInfo> in;
	in.push_back(Info(1, KeyFamily::CityBlock, KeySource::CitySet, false));      // cities BLOCK
	in.push_back(Info(2, KeyFamily::CityPiece, KeySource::Wonder, false));       // cities SINGLE
	in.push_back(Info(3, KeyFamily::Landmark, KeySource::LandmarkRow, false));   // landmarks INST
	in.push_back(Info(4, KeyFamily::Landmark, KeySource::LandmarkRow, true));    // landmarks SKINNED
	in.push_back(Info(5, KeyFamily::CityPiece, KeySource::Wonder, true));        // cities SKINNED
	in.push_back(Info(6, KeyFamily::CityPiece, KeySource::Wall, false));         // CPU-built: out
	in.push_back(Info(7, KeyFamily::CityPiece, KeySource::GreatWall, false));    // CPU-built: out
	in.push_back(Info(3, KeyFamily::Landmark, KeySource::LandmarkRow, false));   // duplicate of 3
	in.push_back(Info(8, KeyFamily::Landmark, KeySource::LandmarkRow, false));
	in.push_back(Info(8, KeyFamily::CityPiece, KeySource::Wonder, false));       // conflict with 8: both out
	KeyInfo nokey = Info(9, KeyFamily::Landmark, KeySource::LandmarkRow, false);
	nokey.key.vb = 0;
	in.push_back(nokey);
	SetBuildReport rep;
	std::vector<VPModelSetKey> s = BuildIdentitySet(in, VPMODE_CITIES | VPMODE_LANDMARKS, &rep);
	CHECK(rep.in == 11 && rep.out == 5 && rep.cpuBuilt == 2 && rep.duplicates == 1 && rep.conflicts == 2 && rep.noKey == 1);
	CHECK(rep.cities == 3 && rep.landmarks == 2 && s.size() == 5);
	std::map<uint64_t, VPModelSetKey> by;
	for(const VPModelSetKey& k : s)
		by[k.key.ib] = k;
	CHECK(by.count(1) && by[1].bits == VPMODE_CITIES && by[1].cls == VPMODEL_CLASS_BLOCK);
	CHECK(by.count(2) && by[2].bits == VPMODE_CITIES && by[2].cls == VPMODEL_CLASS_SINGLE);
	CHECK(by.count(3) && by[3].bits == VPMODE_LANDMARKS && by[3].cls == VPMODEL_CLASS_INST);
	CHECK(by.count(4) && by[4].bits == VPMODE_LANDMARKS && by[4].cls == VPMODEL_CLASS_SKINNED);
	CHECK(by.count(5) && by[5].bits == VPMODE_CITIES && by[5].cls == VPMODEL_CLASS_SKINNED);
	CHECK(!by.count(6) && !by.count(7) && !by.count(8) && !by.count(9));
	for(size_t i = 1; i < s.size(); ++i)
		CHECK(memcmp(&s[i - 1].key, &s[i].key, sizeof(VPModelKey)) < 0);   // sorted, unique
	// only the landmarks bit wanted
	std::vector<VPModelSetKey> l = BuildIdentitySet(in, VPMODE_LANDMARKS, &rep);
	// key 8's cities entry is not wanted, so its landmarks entry stands alone (no conflict among the wanted bits)
	CHECK(l.size() == 3 && rep.classOff == 4 && rep.cities == 0 && rep.landmarks == 3 && rep.conflicts == 0);
	CHECK(BuildIdentitySet(in, 0, &rep).empty());
	// ticket 4879: CPU-built keys copied from the geometry section: walls BLOCK, the Great Wall / bridges SINGLE, both
	// cities; a landmark or skinned family is not a CPU-built mesh (left out)
	std::vector<KeyInfo> cpu;
	cpu.push_back(Info(20, KeyFamily::CityBlock, KeySource::CpuCopied, false));
	cpu.push_back(Info(21, KeyFamily::CityPiece, KeySource::CpuCopied, false));
	cpu.push_back(Info(22, KeyFamily::Landmark, KeySource::CpuCopied, false));
	cpu.push_back(Info(23, KeyFamily::CityPiece, KeySource::CpuCopied, true));
	std::vector<VPModelSetKey> c = BuildIdentitySet(cpu, VPMODE_CITIES | VPMODE_LANDMARKS, &rep);
	CHECK(c.size() == 2 && rep.cpuCopied == 2 && rep.noKey == 2 && rep.cities == 2);
	CHECK(c.size() == 2 && c[0].key.ib == 20 && c[0].cls == VPMODEL_CLASS_BLOCK && c[0].bits == VPMODE_CITIES);
	CHECK(c.size() == 2 && c[1].key.ib == 21 && c[1].cls == VPMODEL_CLASS_SINGLE && c[1].bits == VPMODE_CITIES);
	CHECK(BuildIdentitySet(cpu, VPMODE_LANDMARKS, &rep).empty());   // cities off: none
}

// ---- ticket 4879: the geometry section's reader ----
struct GeomLayer : ModelLayer
{
	std::map<std::pair<uint32_t, uint64_t>, std::vector<uint8_t>> held;
	int adds = 0;
	c5r::Status open(const ModelsOptions&, const std::wstring&) override { return c5r::Status::Ok(); }
	c5r::Status setDevice(ID3D11Device*) override { return c5r::Status::Ok(); }
	bool prepare(double) override { return false; }
	void drawableKeys(std::vector<KeyInfo>& out) const override { out.clear(); }
	c5r::Status accept(const uint8_t*, size_t) override { return c5r::Status::Ok(); }
	c5r::Status draw(ID3D11DeviceContext*, int, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, uint32_t,
	                 const FrameTextures&, LayerDrawStats*) override
	{
		return c5r::Status::Ok();
	}
	void clearScene() override {}
	void setDrawAll(bool) override {}
	std::string describe() const override { return "geom layer"; }
	// the store's rules (c5r CpuMeshStore, render/tests/landmarks/test_cpumesh.cpp checks the real one)
	c5r::Status addGeometry(uint32_t kind, uint64_t hash, uint32_t stride, const uint8_t* p, size_t n) override
	{
		adds++;
		if(n > VPSHELL_MODELGEOM_MAX_BUFFER)
			return c5r::Status::fail(c5r::Err::Unsupported, "oversize");
		if(VPModelGeomHash(kind, p, n) != hash)
			return c5r::Status::fail(c5r::Err::BadLayout, "content differs from its hash");
		held[{kind, hash}].assign(p, p + n);
		strides[{kind, hash}] = stride;
		return c5r::Status::Ok();
	}
	bool holdsGeometry(uint32_t kind, uint64_t hash) const override { return held.count({kind, hash}) != 0; }
	void releaseGeometry(uint32_t kind, uint64_t hash) override
	{
		held.erase({kind, hash});
		releases++;
	}
	// ticket 4888: the bytes as held (the scene bundle's model_geom member)
	bool geometryBytes(uint32_t kind, uint64_t hash, uint32_t* stride, std::vector<uint8_t>* bytes) const override
	{
		auto it = held.find({kind, hash});
		if(it == held.end())
			return false;
		if(stride)
			*stride = strides.count({kind, hash}) ? strides.at({kind, hash}) : 0;
		if(bytes)
			*bytes = it->second;
		return true;
	}
	std::map<std::pair<uint32_t, uint64_t>, uint32_t> strides;
	int releases = 0;
	void heldGeometry(std::vector<std::pair<uint32_t, uint64_t>>& out) const override
	{
		out.clear();
		for(const auto& kv : held)
			out.push_back(kv.first);
	}
};

// the compositor's side of the section, as unit_filter.cpp writes it (seq odd, data, entry, generation, seq even)
struct GeomSection
{
	HANDLE h = nullptr;
	uint8_t* v = nullptr;
	uint32_t pid = 0, dataBytes = 1u << 20;
	GeomSection()
	{
		pid = 0x7E000000u + GetCurrentProcessId();
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_MODELGEOM_FMT, pid);
		h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELGEOM_DATA_OFFSET + dataBytes, n);
		v = (uint8_t*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		VPModelGeomHeader* hd = hdr();
		hd->magic = VPSHELL_MODELGEOM_MAGIC;
		hd->version = VPSHELL_MODELGEOM_VERSION;
		hd->headerBytes = sizeof(VPModelGeomHeader);
		hd->entryBytes = sizeof(VPModelGeomEntry);
		hd->maxEntries = VPSHELL_MODELGEOM_MAX_ENTRIES;
		hd->dataOffset = VPSHELL_MODELGEOM_DATA_OFFSET;
		hd->dataBytes = dataBytes;
		hd->exePid = pid;
		hd->generation = 1;
	}
	~GeomSection()
	{
		UnmapViewOfFile(v);
		CloseHandle(h);
	}
	VPModelGeomHeader* hdr() { return (VPModelGeomHeader*)v; }
	VPModelGeomEntry* e(uint32_t i) { return (VPModelGeomEntry*)(v + sizeof(VPModelGeomHeader)) + i; }
	uint64_t put(uint32_t slot, uint32_t kind, uint32_t off, const std::vector<uint8_t>& b, uint32_t stride, uint64_t hashOverride = 0)
	{
		hdr()->seq++;
		memcpy(v + VPSHELL_MODELGEOM_DATA_OFFSET + off, b.data(), b.size());
		VPModelGeomEntry& x = *e(slot);
		x.hash = hashOverride ? hashOverride : VPModelGeomHash(kind, b.data(), b.size());
		x.stride = stride;
		x.offset = off;
		x.bytes = (uint32_t)b.size();
		hdr()->generation++;
		x.generation = hdr()->generation;
		x.kind = kind;
		hdr()->seq++;
		return x.hash;
	}
	void free(uint32_t slot)
	{
		hdr()->seq++;
		e(slot)->kind = VPMGEOM_KIND_FREE;
		hdr()->generation++;
		hdr()->seq++;
	}
};

static void CaseGeomReader()
{
	GeomLayer l;
	ModelGeomReader none(0x7D000000u + GetCurrentProcessId());
	CHECK(!none.poll(l) && !none.counters().open);   // no section yet: nothing, tried again later
	GeomSection s;
	ModelGeomReader r(s.pid);
	std::vector<uint8_t> ib(4 * 270), vb(20 * 160);
	for(size_t i = 0; i < ib.size(); ++i)
		ib[i] = (uint8_t)(i * 7);
	for(size_t i = 0; i < vb.size(); ++i)
		vb[i] = (uint8_t)(i * 13 + 1);
	const uint64_t hi = s.put(0, VPMGEOM_KIND_INDEX32, 0, ib, 4);
	const uint64_t hv = s.put(1, VPMGEOM_KIND_VERTEX, 4096, vb, 20);
	CHECK(r.poll(l) && l.held.size() == 2 && l.holdsGeometry(VPMGEOM_KIND_INDEX32, hi) && l.holdsGeometry(VPMGEOM_KIND_VERTEX, hv));
	CHECK(r.counters().added == 2 && r.counters().copiedBytes == ib.size() + vb.size() && r.counters().entries == 2);
	CHECK(l.held[{VPMGEOM_KIND_VERTEX, hv}] == vb);
	CHECK(!r.poll(l));   // the same generation: nothing read
	// a write after the copy (the bytes no longer match the hash): refused, counted once, not read again
	std::vector<uint8_t> bad = vb;
	const uint64_t hb = s.put(2, VPMGEOM_KIND_VERTEX, 8192, bad, 20, 0x1234567ull);
	const int adds = l.adds;
	r.poll(l);
	CHECK(!l.holdsGeometry(VPMGEOM_KIND_VERTEX, hb) && r.counters().refusedHash == 1);
	s.hdr()->generation++;
	r.poll(l);
	CHECK(l.adds == adds + 1 && r.counters().refusedHash == 1);   // the refused entry is not offered again
	CHECK(r.takeNote().find("hash") != std::string::npos);
	// an entry past the data: bounds, refused
	VPModelGeomEntry* x = s.e(3);
	s.hdr()->seq++;
	x->hash = 99;
	x->stride = 20;
	x->offset = s.dataBytes - 10;
	x->bytes = 40;
	x->generation = ++s.hdr()->generation;
	x->kind = VPMGEOM_KIND_VERTEX;
	s.hdr()->seq++;
	r.poll(l);
	CHECK(r.counters().refusedBounds == 1 && !l.holdsGeometry(VPMGEOM_KIND_VERTEX, 99));
	// the compositor mid-write (seq odd): nothing read, then read once it is done
	std::vector<uint8_t> vb2(32 * 10, 5);
	s.hdr()->seq++;
	memcpy(s.v + VPSHELL_MODELGEOM_DATA_OFFSET + 16384, vb2.data(), vb2.size());
	VPModelGeomEntry* y = s.e(4);
	y->hash = VPModelGeomHash(VPMGEOM_KIND_VERTEX, vb2.data(), vb2.size());
	y->stride = 32;
	y->offset = 16384;
	y->bytes = (uint32_t)vb2.size();
	y->generation = ++s.hdr()->generation;
	y->kind = VPMGEOM_KIND_VERTEX;
	CHECK(!r.poll(l) && !l.holdsGeometry(VPMGEOM_KIND_VERTEX, y->hash));
	s.hdr()->seq++;
	CHECK(r.poll(l) && l.holdsGeometry(VPMGEOM_KIND_VERTEX, y->hash));
	// released: the compositor freed the index buffer's entry (the EXE no longer draws it)
	s.free(0);
	CHECK(r.poll(l) && !l.holdsGeometry(VPMGEOM_KIND_INDEX32, hi) && l.holdsGeometry(VPMGEOM_KIND_VERTEX, hv) && r.counters().released == 1);
	// a header that does not hold: nothing read
	s.hdr()->seq++;
	s.hdr()->version = 99;
	s.hdr()->generation++;
	s.hdr()->seq++;
	const size_t before = l.held.size();
	CHECK(!r.poll(l) && l.held.size() == before && r.counters().refusedBounds == 2);
}

static void CaseSetPublish()
{
	FakeCompositor fc(L"set");
	wchar_t n[96];
	_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\C5Test4875Set.%u", GetCurrentProcessId());
	ModelSetPublisher pub(fc.pid, n);
	std::vector<KeyInfo> in = {Info(1, KeyFamily::CityBlock, KeySource::CitySet, false),
	                           Info(3, KeyFamily::Landmark, KeySource::LandmarkRow, false)};
	const std::vector<VPModelSetKey> keys = BuildIdentitySet(in, VPMODE_CITIES | VPMODE_LANDMARKS, nullptr);
	CHECK(!pub.acked(fc.ctl));   // nothing published
	CHECK(pub.publish(keys, fc.ctl).ok() && pub.generation() == 1 && fc.ctl->setGeneration == 1);
	std::vector<VPModelSetKey> back;
	CHECK(CheckModelSet(pub.view(), VPSHELL_MODELSET_MAX_BYTES, GetCurrentProcessId(), &back) == VPMSET_OK);
	CHECK(back.size() == 2 && memcmp(back.data(), keys.data(), 2 * sizeof(VPModelSetKey)) == 0);
	const VPModelSetHeader* h = (const VPModelSetHeader*)pub.view();
	CHECK((h->seq & 1) == 0 && h->seq >= 2 && h->generation == 1 && h->keyCount == 2 && h->keyOffset == 64);
	CHECK(!pub.acked(fc.ctl));   // the compositor has not answered
	// the compositor applies it
	fc.editBlock([](VPShellModelTapControl& c) {
		c.setGenerationApplied = 1;
		c.setResult = VPMSET_OK;
		c.setReads++;
	});
	CHECK(pub.acked(fc.ctl) && pub.counters().verdicts == 1);
	// generation 2: an old OK verdict does not ack it; a refusal before a new read is not counted
	CHECK(pub.publish(keys, fc.ctl).ok() && pub.generation() == 2 && fc.ctl->setGeneration == 2);
	CHECK(!pub.acked(fc.ctl));
	fc.editBlock([](VPShellModelTapControl& c) { c.setResult = VPMSET_HASH; });   // no new read (setReads unchanged)
	CHECK(!pub.acked(fc.ctl) && pub.counters().refusedBy[VPMSET_HASH] == 0);
	fc.editBlock([](VPShellModelTapControl& c) { c.setReads++; });
	CHECK(!pub.acked(fc.ctl) && pub.counters().refusedBy[VPMSET_HASH] == 1 && pub.counters().lastResult == VPMSET_HASH);
	CHECK(!pub.acked(fc.ctl) && pub.counters().refusedBy[VPMSET_HASH] == 1);   // once per generation
	// too many keys
	std::vector<VPModelSetKey> many(VPSHELL_MODELSET_MAX_KEYS + 1);
	const c5r::Status st = pub.publish(many, fc.ctl);
	CHECK(!st.ok() && st.code == c5r::Err::BadLayout && pub.generation() == 2);
	// an empty set is a valid generation
	CHECK(pub.publish(std::vector<VPModelSetKey>(), fc.ctl).ok() && pub.generation() == 3);
	CHECK(CheckModelSet(pub.view(), VPSHELL_MODELSET_MAX_BYTES, GetCurrentProcessId(), &back) == VPMSET_OK && back.empty());
	// a second publisher on the same name continues the generations
	CHECK(pub.publish(keys, nullptr).ok() && pub.generation() == 4);
	ModelSetPublisher pub2(fc.pid, n);
	CHECK(pub2.publish(keys, nullptr).ok() && pub2.generation() == 5);
}

static void CaseSetCheckNegative()
{
	wchar_t n[96];
	_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\C5Test4875SetNeg.%u", GetCurrentProcessId());
	ModelSetPublisher pub(1, n);
	std::vector<KeyInfo> in = {Info(1, KeyFamily::CityBlock, KeySource::CitySet, false),
	                           Info(4, KeyFamily::Landmark, KeySource::LandmarkRow, true)};
	CHECK(pub.publish(BuildIdentitySet(in, 6, nullptr), nullptr).ok());
	const uint32_t me = GetCurrentProcessId();
	std::vector<uint8_t> sec(pub.view(), pub.view() + 64 + 2 * 64);
	auto check = [&](const std::function<void(std::vector<uint8_t>&)>& f) {
		std::vector<uint8_t> c = sec;
		f(c);
		return CheckModelSet(c.data(), c.size(), me, nullptr);
	};
	CHECK(check([](std::vector<uint8_t>&) {}) == VPMSET_OK);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->magic ^= 1; }) == VPMSET_MAGIC);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->version = 2; }) == VPMSET_VERSION);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->keyBytes = 48; }) == VPMSET_VERSION);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->serverPid ^= 1; }) == VPMSET_PID);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->keyCount = 3; }) == VPMSET_BOUNDS);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->keyOffset = 128; }) == VPMSET_BOUNDS);
	CHECK(check([](std::vector<uint8_t>& c) { ((VPModelSetHeader*)c.data())->seq |= 1; }) == VPMSET_TORN);
	CHECK(check([](std::vector<uint8_t>& c) { c[64 + 3] ^= 1; }) == VPMSET_HASH);
	// a skinned key with both bits, or with no bit: refused (the hash recomputed so only the bits are wrong)
	auto bits = [&](int which, uint8_t b) {
		return check([&](std::vector<uint8_t>& c) {
			((VPModelSetKey*)(c.data() + 64))[which].bits = b;
			((VPModelSetHeader*)c.data())->fnv1a64 = VPModelHash64(c.data() + 64, 2 * 64);
		});
	};
	CHECK(bits(1, 6) == VPMSET_BOUNDS && bits(1, 0) == VPMSET_BOUNDS && bits(0, 8) == VPMSET_BOUNDS && bits(0, 2) == VPMSET_OK);
}

// ---- the mode writer ----
struct ModeRig
{
	VPShellMode block;
	ModelModeWriter w;
	int64_t t = 1000000;
	std::vector<std::string> lines;
	static void Log(void* ctx, const char* l) { ((ModeRig*)ctx)->lines.push_back(l); }
	ModeRig(bool cities, bool landmarks)
	{
		memset((void*)&block, 0, sizeof(block));
		w.setBounds(500, 2000, 30000);
		w.configure(cities, landmarks, 4242, srvrender::QpcFreq(), &Log, this);
		w.attach(&block);
	}
	// ticket 0641: the accepted-frame stamp as ModelsScene passes it: a fresh call stamps now, a stale one passes the last
	// stamp unchanged (the stamp never goes back), 0 before the first
	int64_t stamp = 0;
	void frame(bool c, bool l, bool freshFrame = true, double stepMs = 16)
	{
		t += Ms(stepMs);
		const bool r[2] = {c, l};
		if(freshFrame)
			stamp = t - Ms(1);
		w.onFrame(r, stamp, t);
	}
	void poll(bool c, bool l, bool freshFrame = true, double stepMs = 1)
	{
		t += Ms(stepMs);
		const bool r[2] = {c, l};
		if(freshFrame)
			stamp = t - Ms(1);
		w.onPoll(r, stamp, t);
	}
	// a loop of the product's shape: n iterations of a poll and a frame, stepMs apart
	void loop(int n, bool c, bool l, bool freshFrame, double stepMs)
	{
		for(int k = 0; k < n; ++k)
		{
			poll(c, l, freshFrame, stepMs / 2);
			frame(c, l, freshFrame, stepMs / 2);
		}
	}
};

static void CaseModeSetsAndKeepsUnits()
{
	ModeRig m(true, true);
	// the units writer's bit 0 and the path bit 3 set by others: never changed by this writer
	m.block.word = VPMODE_UNITS | VPMODE_PATHS;
	m.block.writerPid = 4242;
	m.block.changes = 7;
	m.block.readyLossTotal = 3;
	m.block.latchedWord = 0x5;   // the consumer part: never written by this writer
	VPMODE_DROPPED_CITIES(&m.block) = 11;
	VPMODE_PASSEDSET_LANDMARKS(&m.block) = 13;
	m.frame(false, false);
	CHECK(m.block.word == (VPMODE_UNITS | VPMODE_PATHS) && m.block.changes == 7);   // not ready: nothing written
	m.frame(true, false);
	CHECK(m.block.word == (VPMODE_UNITS | VPMODE_PATHS | VPMODE_CITIES) && m.block.changes == 8 && m.w.bits() == VPMODE_CITIES);
	CHECK(m.block.changedQpc == m.t);
	m.frame(true, true);
	CHECK(m.block.word == (VPMODE_UNITS | VPMODE_PATHS | VPMODE_CITIES | VPMODE_LANDMARKS) && m.block.changes == 9);
	m.frame(true, true);
	CHECK(m.block.changes == 9);   // no change, no store
	// the units writer clears bit 0 between our frames: we keep its value
	m.block.word &= ~VPMODE_UNITS;
	m.poll(true, false);   // landmarks lost between frames
	CHECK(m.block.word == (VPMODE_PATHS | VPMODE_CITIES) && m.w.phase(1) == ModelModeWriter::kLost);
	CHECK(m.block.readyLossTotal == 3);   // the units' counter is never ours
	CHECK(m.w.counters(1).losses == 1 && m.w.counters(1).lossesReady == 1);
	// bits 4-31 written 0 by us (garbage written by nobody else is cleared at our next change)
	m.block.word |= 0x100u;
	m.frame(true, true);
	CHECK(m.block.word == (VPMODE_PATHS | VPMODE_CITIES | VPMODE_LANDMARKS));
	CHECK(m.w.counters(1).recovered == 1);
	CHECK(m.block.latchedWord == 0x5 && VPMODE_DROPPED_CITIES(&m.block) == 11 && VPMODE_PASSEDSET_LANDMARKS(&m.block) == 13 &&
	      VPMODE_FILTERSTATE2(&m.block) == 0);
	// writerPid: written when not ours (a server without the units writer)
	ModeRig m2(true, false);
	m2.frame(true, false);
	CHECK(m2.block.writerPid == 4242 && m2.block.word == VPMODE_CITIES && m2.block.changes == 1);
	// an option off: its bit is never set
	ModeRig m3(false, true);
	m3.frame(true, true);
	CHECK(m3.block.word == VPMODE_LANDMARKS);
	ModeRig m4(false, false);
	m4.frame(true, true);
	m4.poll(true, true);
	CHECK(m4.block.word == 0 && m4.block.changes == 0 && m4.block.writerPid == 0);
}

static void CaseModeStaleAndHold()
{
	ModeRig m(true, false);
	m.frame(true, false);
	CHECK(m.w.bits() == VPMODE_CITIES);
	// no accepted frame while the loop runs (100 ms steps): cleared once 500 ms of steps passed (ticket 0641: step time)
	for(int k = 0; k < 4; ++k)
		m.poll(true, false, false, 100);
	CHECK(m.w.bits() == VPMODE_CITIES);
	m.poll(true, false, false, 100);
	CHECK(m.w.bits() == 0 && m.block.word == 0 && m.w.counters(0).lossesStale == 1 && m.w.phase(0) == ModelModeWriter::kLost);
	std::string why;
	CHECK(!m.w.failed(why));
	m.frame(true, false, true, 100);   // back within the bound
	CHECK(m.w.bits() == VPMODE_CITIES && m.w.counters(0).recovered == 1);
	m.poll(false, false);   // readiness lost
	CHECK(m.w.bits() == 0);
	for(int k = 0; k < 15; ++k)   // 1.5 s of 100 ms steps
		m.poll(false, false, true, 100);
	CHECK(!m.w.failed(why));
	for(int k = 0; k < 6; ++k)   // 2.1 s of steps: fatal (the rig's bound is 2000 ms)
		m.poll(false, false, true, 100);
	CHECK(m.w.failed(why) && why.find("cities") != std::string::npos && why.find("D118") != std::string::npos &&
	      why.find("of frames that ran") != std::string::npos);
	CHECK(m.w.counters(0).fatal == 1 && m.w.phase(0) == ModelModeWriter::kFailed && m.block.word == 0);
	// after the failure nothing more is written
	const uint32_t ch = m.block.changes;
	m.frame(true, false);
	CHECK(m.block.changes == ch && m.block.word == 0);
}

// ---- ticket 0641: the hold and the freshness in step time ----
static int CountLines(const std::vector<std::string>& lines, const char* s)
{
	int n = 0;
	for(const std::string& l : lines)
		n += l.find(s) != std::string::npos ? 1 : 0;
	return n;
}

// The product's defaults (30 s of step time, 500 ms no-frame, 30 s install), on a rig that is live with both bits.
static void LiveDefaults(ModeRig& m)
{
	m.w.setBounds(kModelsNoFrameMsDefault, kModelsHoldMsDefault, kModelsInstallMsDefault);
	m.loop(10, true, true, true, 33);
	const uint32_t want = (m.w.on(0) ? VPMODE_CITIES : 0u) | (m.w.on(1) ? VPMODE_LANDMARKS : 0u);
	CHECK(m.w.bits() == want && m.w.phase(m.w.on(0) ? 0 : 1) == ModelModeWriter::kLive);
}

static void CaseModeStallSteps()
{
	std::string why;
	CHECK(kModelsHoldMsDefault == 30000.0 && kModelsStepGapMs == 250.0);
	// one source of truth: the options the host copies into the writer (ModelsOptions(), srv_render_host.cpp) carry the
	// writer's defaults
	{
		const ModelsOptions o;
		CHECK(o.holdMs == kModelsHoldMsDefault && o.noFrameMs == kModelsNoFrameMsDefault && o.installMs == kModelsInstallMsDefault &&
		      o.openStallMs == kModelsOpenStallMsDefault && o.openMaxMs == kModelsOpenMaxMsDefault);
	}
	// (a) 0637's shape (c5h-new-a 2026-11-21 11:39, render.log): the render thread stopped 4848 ms in one terrain bake; the
	// compositor stopped tapping while the server was stale, so the first iterations after it saw no new frame; then
	// 2.4 s of slow iterations (bakes of 540, 677, 229 ms, a 779 ms log write) still without a new frame; then the frames
	// came back. The old rule (wall time, hold 2000 ms) ended the game at 2371 ms; now: one loss at most, recovered, no end.
	{
		ModeRig m(true, true);
		LiveDefaults(m);
		m.poll(true, true, false, 4848);   // the stall: one step, counted 250 ms
		m.frame(true, true, false, 1);
		CHECK(m.w.bits() == (VPMODE_CITIES | VPMODE_LANDMARKS));   // 251 ms of step time < 500: still fresh
		CHECK(m.w.stalls() == 1 && m.w.maxStepGapMs() > 4847 && m.w.maxStepGapMs() < 4849);
		const double gaps[] = {540, 677, 229, 779, 146};   // 2371 ms of wall time without a new frame
		for(double g : gaps)
		{
			m.poll(true, true, false, g / 2);
			m.frame(true, true, false, g / 2);
		}
		CHECK(!m.w.failed(why) && m.w.phase(0) == ModelModeWriter::kLost && m.w.bits() == 0);   // the EXE draws them
		CHECK(m.w.counters(0).lossesStale == 1 && m.w.counters(1).lossesStale == 1);
		m.loop(3, true, true, true, 33);   // the frames are back
		CHECK(!m.w.failed(why) && m.w.bits() == (VPMODE_CITIES | VPMODE_LANDMARKS) && m.w.counters(0).recovered == 1 &&
		      m.w.counters(1).recovered == 1);
		// the loss in step time is below the wall time across the stall
		CHECK(m.w.counters(0).lastLossMs < m.w.counters(0).lastLossWallMs && m.w.counters(0).lastLossWallMs > 2000);
		// one LOUD line when the loss starts and one when it recovers, per bit; the stall line is not LOUD
		CHECK(CountLines(m.lines, "LOUD: models: cities: no accepted tap frame") == 1 &&
		      CountLines(m.lines, "LOUD: models: cities: back after") == 1 && CountLines(m.lines, "LOUD: models mode:") == 0 &&
		      CountLines(m.lines, "the server's render loop did not run for 4848 ms") == 1);
		printf("  0637 shape: loss %.0f ms of step time, %.0f ms wall; stalls %llu\n", m.w.counters(0).lastLossMs,
		       m.w.counters(0).lastLossWallMs, (unsigned long long)m.w.stalls());
	}
	// (b) a 2.4 s stall and then a 10 s stall of the server's loop, the frames back after each: no loss at all (each costs
	// 250 ms of step time)
	{
		ModeRig m(true, true);
		LiveDefaults(m);
		m.poll(true, true, false, 2400);
		m.frame(true, true, true, 1);
		m.loop(5, true, true, true, 33);
		m.poll(true, true, false, 10000);
		m.frame(true, true, true, 1);
		m.loop(5, true, true, true, 33);
		CHECK(!m.w.failed(why) && m.w.bits() == (VPMODE_CITIES | VPMODE_LANDMARKS) && m.w.counters(0).losses == 0 &&
		      m.w.stalls() == 2);
	}
	// (c) the readiness lost (the identity set's verdict, a renderer) across a 10 s stall and back after it: recovered
	{
		ModeRig m(true, false);
		LiveDefaults(m);
		m.poll(false, false, true, 16);
		CHECK(m.w.phase(0) == ModelModeWriter::kLost && m.w.bits() == 0);
		m.poll(false, false, true, 10000);
		m.loop(20, false, false, true, 100);   // 2 s more of a running loop without readiness
		m.loop(1, true, false, true, 33);
		CHECK(!m.w.failed(why) && m.w.bits() == VPMODE_CITIES && m.w.counters(0).recovered == 1 &&
		      m.w.counters(0).lastLossMs < 3000 && m.w.counters(0).lastLossWallMs > 12000);
	}
	// (d) a loss that does not end while the loop runs at 30 frames a second: the fatal step at 30 s of step time, with
	// the reason; not before
	{
		ModeRig m(true, true);
		LiveDefaults(m);
		m.poll(false, true, true, 16);   // cities' readiness lost; landmarks stay
		int k = 0;
		for(; k < 2000 && !m.w.failed(why); ++k)
			m.loop(1, false, true, true, 33.3);
		const double lost = m.w.stepMs();
		CHECK(m.w.failed(why) && why.find("models: cities: the readiness or the tap frames were lost in play for") == 0 &&
		      why.find("of frames that ran") != std::string::npos &&
		      why.find("beyond render_models_hold_ms=30000") != std::string::npos && why.find("D118") != std::string::npos);
		CHECK(k >= 899 && k <= 902 && m.w.counters(0).fatal == 1 && m.w.phase(0) == ModelModeWriter::kFailed &&
		      m.block.word == VPMODE_LANDMARKS);   // the failed bit cleared (the server ends next)
		printf("  a lasting loss: fatal after %d iterations of 33.3 ms (step clock %.0f ms)\n", k, lost);
	}
	// (e) a lasting loss while EVERY step is a stall (one poll a second): it still ends, after 30000 / 250 = 120 steps
	{
		ModeRig m(true, false);
		LiveDefaults(m);
		m.poll(false, false, true, 16);
		int k = 0;
		for(; k < 1000 && !m.w.failed(why); ++k)
			m.poll(false, false, true, 1000);
		CHECK(m.w.failed(why) && (k == 120 || k == 121) && why.find("render_models_hold_ms=30000") != std::string::npos);
	}
	// (f) the no-frame rule in step time: the stamp stops while the loop runs at 33 ms: cleared after 500 ms of steps, not
	// before; across a stall the same stamp is still fresh
	{
		ModeRig m(true, false);
		LiveDefaults(m);
		m.loop(7, true, false, false, 66);   // 462 ms
		CHECK(m.w.bits() == VPMODE_CITIES);
		m.loop(1, true, false, false, 66);   // 528 ms
		CHECK(m.w.bits() == 0 && m.w.counters(0).lossesStale == 1);
	}
	// (g) the status line names the step clock
	{
		ModeRig m(true, false);
		LiveDefaults(m);
		m.poll(true, false, true, 3000);
		char line[2048] = {};
		m.w.appendLine(line, sizeof(line));
		CHECK(strstr(line, " | step ms=") != nullptr && strstr(line, " stalls=1 max_gap_ms=3000") != nullptr &&
		      strstr(line, " max_loss_wall_ms=") != nullptr);
	}
	// (h) LANDMARKS alone (c5h-new-mpk's case of 2026-11-21: landmarks lost 2233 ms, exit 5): the same rule on bit 2. The
	// render loop's gaps of 707 ms and more, no new frame meanwhile: one loss at most, recovered; a lasting loss of the
	// landmarks' readiness ends at 30 s of step time naming the landmarks
	{
		ModeRig m(false, true);
		LiveDefaults(m);
		CHECK(m.w.bits() == VPMODE_LANDMARKS);
		const double gaps[] = {707, 540, 900, 1200, 300, 707};
		for(double g : gaps)
		{
			m.poll(false, true, false, g / 2);
			m.frame(false, true, false, g / 2);
		}
		CHECK(!m.w.failed(why) && m.w.counters(1).losses <= 1);
		m.loop(3, false, true, true, 33);
		CHECK(!m.w.failed(why) && m.w.bits() == VPMODE_LANDMARKS && m.w.counters(1).recovered == m.w.counters(1).losses &&
		      m.w.counters(0).losses == 0);
		m.poll(false, false, true, 16);   // the landmarks' readiness lost for good
		int k = 0;
		for(; k < 2000 && !m.w.failed(why); ++k)
			m.loop(1, false, false, true, 33.3);
		CHECK(m.w.failed(why) && why.find("models: landmarks: the readiness or the tap frames were lost in play for") == 0 &&
		      why.find("render_models_hold_ms=30000") != std::string::npos && k >= 899 && k <= 902 &&
		      m.w.counters(1).fatal == 1 && m.block.word == 0);
	}
}

static void CaseModeLoadEndsLoss()
{
	ModeRig m(true, true);
	m.frame(true, true);
	m.poll(false, true);
	CHECK(m.w.phase(0) == ModelModeWriter::kLost && m.w.bits() == VPMODE_LANDMARKS);
	m.t += Ms(1000);
	m.w.onLoadBegin(m.t);
	CHECK(m.w.bits() == 0 && m.block.word == 0 && m.w.phase(0) == ModelModeWriter::kStarting &&
	      m.w.counters(0).lossesEndedByLoad == 1);
	for(int k = 0; k < 50; ++k)   // 5 s of steps, past the hold: no fatal (the loss ended)
		m.poll(false, false, true, 100);
	std::string why;
	CHECK(!m.w.failed(why));
	m.frame(true, true);
	CHECK(m.w.bits() == (VPMODE_CITIES | VPMODE_LANDMARKS));
}

static void CaseModeFilterFatal()
{
	// CANNOT_DROP while held and latched: fatal at once, the bit cleared first
	{
		ModeRig m(true, true);
		m.frame(true, true);
		m.block.latchedWord = VPMODE_CITIES | VPMODE_LANDMARKS;
		VPMODE_FILTERSTATE2(&m.block) = VPFILTER_WORD(VPFILTER_CAN_DROP, 0) | (VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPMFILTER_R_NO_SET) << 16);
		m.poll(true, true);
		std::string why;
		CHECK(m.w.failed(why) && why.find("landmarks") != std::string::npos && why.find("no identity set") != std::string::npos);
		CHECK(m.w.counters(1).filterFatal == 1 && m.w.counters(0).filterFatal == 0 && !(m.block.word & VPMODE_LANDMARKS));
	}
	// CANNOT_DROP but not latched, or the bit not held: never acts
	{
		ModeRig m(true, false);
		VPMODE_FILTERSTATE2(&m.block) = VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_MODE_OFF);
		m.frame(false, false);   // not held
		m.poll(false, false);
		std::string why;
		CHECK(!m.w.failed(why));
		m.frame(true, false);    // held, not latched
		m.poll(true, false);
		CHECK(!m.w.failed(why) && m.w.bits() == VPMODE_CITIES);
		m.block.latchedWord = VPMODE_LANDMARKS;   // another bit latched
		m.poll(true, false);
		CHECK(!m.w.failed(why));
		m.block.latchedWord = VPMODE_CITIES;
		m.frame(true, false);   // at a frame too
		CHECK(m.w.failed(why) && why.find("model_filter=off") != std::string::npos && m.w.counters(0).filterFatal == 1);
	}
	// not reported (0) and can drop (1) never act
	{
		ModeRig m(true, true);
		m.frame(true, true);
		m.block.latchedWord = VPMODE_CITIES | VPMODE_LANDMARKS;
		VPMODE_FILTERSTATE2(&m.block) = 0;
		m.poll(true, true);
		VPMODE_FILTERSTATE2(&m.block) = VPFILTER_WORD(VPFILTER_CAN_DROP, 0) | (VPFILTER_WORD(VPFILTER_CAN_DROP, 0) << 16);
		m.poll(true, true);
		std::string why;
		CHECK(!m.w.failed(why) && m.w.bits() == (VPMODE_CITIES | VPMODE_LANDMARKS));
	}
}

// Ticket 4885 (4878's live run e1, thread 0561 #6913): the model layer's open took 18-32 s on slow disks while the install
// bound counted from the load's first drawn frame, so a slow open took the D118 fatal step though nothing had failed. Now
// the install bound waits for the open's end and counts from the later of the two; the open has its own bounds (no
// progress, a ceiling) and its failure is fatal naming the open. Run from the case mode_install_bound (stand-in clock).
static bool Has(const std::vector<std::string>& lines, const char* s)
{
	for(const std::string& l : lines)
		if(l.find(s) != std::string::npos)
			return true;
	return false;
}

static void CaseModeOpenBounds()
{
	std::string why;
	// (a) a slow open (60 s, the worker making progress every second) while the EXE's frames are drawn: no fatal step;
	// once open and ready the bits are set
	{
		ModeRig m(true, true);
		m.frame(false, false);   // the load's first drawn frame
		m.w.onOpenBegin(m.t);
		CHECK(m.w.openCounters().state == ModelOpenCounters::kRunning && Has(m.lines, "the model layer's open begins"));
		for(int s = 0; s < 60; ++s)
		{
			m.frame(false, false, true, 500);
			m.poll(false, false, true, 500);
			m.w.onOpenProgress(m.t);
		}
		CHECK(!m.w.failed(why) && m.w.openCounters().state == ModelOpenCounters::kRunning && m.w.openCounters().progress == 60);
		m.w.onOpenEnd(true, nullptr, m.t);
		const ModelOpenCounters& oc = m.w.openCounters();
		CHECK(oc.state == ModelOpenCounters::kOpen && oc.openMs > 59999 && oc.openMs < 60001 && oc.maxGapMs > 999 &&
		      oc.maxGapMs < 1001 && oc.stallFatal == 0 && oc.maxFatal == 0 && oc.failFatal == 0);
		CHECK(Has(m.lines, "the model layer is open after 60000 ms"));
		m.poll(false, false, true, 20000);   // the identity set and the compositor's verdict take 20 s more: within the bound
		CHECK(!m.w.failed(why));
		m.frame(true, true);
		CHECK(!m.w.failed(why) && m.w.bits() == (VPMODE_CITIES | VPMODE_LANDMARKS) && m.block.word == (VPMODE_CITIES | VPMODE_LANDMARKS));
		CHECK(Has(m.lines, "ms after the model layer's open)") && m.w.counters(0).installFatal == 0);
		char line[2048] = {};
		m.w.appendLine(line, sizeof(line));
		CHECK(strstr(line, " | open state=open open_ms=60000 max_gap_ms=1000 progress=60 stall_fatal=0") != nullptr);
	}
	// (a) the install bound counts from the open's end (the later of the two), not from the load's first drawn frame
	{
		ModeRig m(true, false);
		m.frame(false, false);
		m.w.onOpenBegin(m.t);
		for(int s = 0; s < 40; ++s)
		{
			m.poll(false, false, true, 1000);
			m.w.onOpenProgress(m.t);
		}
		m.w.onOpenEnd(true, nullptr, m.t);
		m.frame(false, false, true, 1);
		m.poll(false, false, true, 29000);
		CHECK(!m.w.failed(why));
		m.poll(false, false, true, 1001);
		CHECK(m.w.failed(why) && why.find("render_models_install_ms=30000") != std::string::npos &&
		      why.find("later of the load's first drawn frame and the model layer's open") != std::string::npos &&
		      why.find("open after 40000 ms") != std::string::npos && m.w.counters(0).installFatal == 1);
	}
	// (b) an open that makes no progress: the D118 step past render_models_open_stall_ms, naming the open
	{
		ModeRig m(true, true);
		m.frame(false, false);
		m.w.onOpenBegin(m.t);
		m.frame(false, false, true, 30000);
		m.poll(false, false, true, 29000);
		CHECK(!m.w.failed(why));
		m.poll(false, false, true, 1001);
		CHECK(m.w.failed(why) && why.find("model layer's open") != std::string::npos &&
		      why.find("render_models_open_stall_ms=60000") != std::string::npos && why.find("D118") != std::string::npos);
		CHECK(m.w.openCounters().stallFatal == 1 && m.w.phase(0) == ModelModeWriter::kFailed && m.block.word == 0 &&
		      m.w.counters(0).installFatal == 0 && m.w.counters(1).installFatal == 0);
		const uint32_t ch = m.block.changes;
		m.frame(true, true);   // after the failure nothing more is written
		CHECK(m.block.changes == ch && m.block.word == 0);
	}
	// (b) an open that keeps "making progress" but never ends: the ceiling render_models_open_max_ms
	{
		ModeRig m(true, false);
		m.frame(false, false);
		m.w.onOpenBegin(m.t);
		int s = 0;
		for(; s < 700 && !m.w.failed(why); ++s)
		{
			m.poll(false, false, true, 1000);
			m.w.onOpenProgress(m.t);
		}
		CHECK(s == 601 && m.w.failed(why) && why.find("render_models_open_max_ms=600000") != std::string::npos &&
		      why.find("has not ended") != std::string::npos && m.w.openCounters().maxFatal == 1);
	}
	// (b) an open that fails: the D118 step with the open's reason
	{
		ModeRig m(true, false);
		m.frame(false, false);
		m.w.onOpenBegin(m.t);
		m.poll(false, false, true, 5000);
		m.w.onOpenEnd(false, "ArtDefine_Landmarks missing", m.t);
		CHECK(m.w.failed(why) && why.find("model layer's open") != std::string::npos && why.find("failed after 5000 ms") != std::string::npos &&
		      why.find("ArtDefine_Landmarks missing") != std::string::npos && m.w.openCounters().failFatal == 1 &&
		      m.w.openCounters().state == ModelOpenCounters::kFailed);
	}
	// both open bounds 0: none; the install bound still does not run while the open runs
	{
		ModeRig m(true, false);
		m.w.setOpenBounds(0, 0);
		m.frame(false, false);
		m.w.onOpenBegin(m.t);
		m.frame(false, false, true, 1800000);
		m.poll(false, false, true, 1800000);
		CHECK(!m.w.failed(why));
	}
	// (c) the install-fatal case after the open: an open of 100 ms, then 30 s without the bits = the D118 step
	{
		ModeRig m(true, false);
		m.w.onOpenBegin(m.t);
		m.t += Ms(100);
		m.w.onOpenEnd(true, nullptr, m.t);
		m.frame(false, false);
		m.poll(false, false, true, 29900);
		CHECK(!m.w.failed(why));
		m.poll(false, false, true, 200);
		CHECK(m.w.failed(why) && why.find("render_models_install_ms") != std::string::npos &&
		      why.find("open after 100 ms") != std::string::npos && m.w.counters(0).installFatal == 1);
	}
	// (c) a later load: the layer stays open (the open's state is the layer's), the bound counts from the new load's first
	// drawn frame
	{
		ModeRig m(true, false);
		m.w.onOpenBegin(m.t);
		m.w.onOpenEnd(true, nullptr, m.t + Ms(10));
		m.frame(true, false);
		CHECK(m.w.bits() == VPMODE_CITIES);
		m.t += Ms(60000);
		m.w.onLoadBegin(m.t);
		m.w.configure(true, false, 4242, srvrender::QpcFreq(), &ModeRig::Log, &m);   // a reconfigure keeps the open too
		m.w.attach(&m.block);
		CHECK(m.w.openCounters().state == ModelOpenCounters::kOpen);
		m.frame(false, false);
		m.poll(false, false, true, 29000);
		CHECK(!m.w.failed(why));
		m.poll(false, false, true, 1001);
		CHECK(m.w.failed(why) && why.find("render_models_install_ms") != std::string::npos);
	}
}

static void CaseModeInstallBound()
{
	ModeRig m(true, false);
	m.frame(false, false);
	m.poll(false, false, true, 20000);
	std::string why;
	CHECK(!m.w.failed(why));
	m.poll(false, false, true, 10001);
	CHECK(m.w.failed(why) && why.find("render_models_install_ms") != std::string::npos && m.w.counters(0).installFatal == 1);
	// ticket 4885: the open has not begun (no game database copy): the bound from the load's first drawn frame, saying so
	CHECK(why.find("open has not begun") != std::string::npos);
	// no drawn frame yet: the bound does not run (it counts from the load's first drawn frame)
	ModeRig m2(true, false);
	m2.poll(false, false, true, 60000);
	CHECK(!m2.w.failed(why));
	// installMs 0: no bound
	ModeRig m3(true, false);
	m3.w.setBounds(500, 2000, 0);
	m3.frame(false, false);
	m3.poll(false, false, true, 600000);
	CHECK(!m3.w.failed(why));
	CaseModeOpenBounds();   // ticket 4885
}

// ---- the module with a stand-in layer ----
struct FakeLayer : ModelLayer
{
	c5r::Status openSt, devSt, acceptSt, drawSt;
	std::vector<KeyInfo> keys;
	int opens = 0, accepts = 0, draws = 0, prepares = 0, clears = 0;
	bool drawAll = false;
	bool changed = false;
	c5r::Status open(const ModelsOptions&, const std::wstring&) override
	{
		opens++;
		return openSt;
	}
	c5r::Status setDevice(ID3D11Device*) override { return devSt; }
	bool prepare(double) override
	{
		prepares++;
		const bool c = changed;
		changed = false;
		return c;
	}
	void drawableKeys(std::vector<KeyInfo>& out) const override { out = keys; }
	c5r::Status accept(const uint8_t* p, size_t n) override
	{
		accepts++;
		if(!acceptSt.ok())
			return acceptSt;
		const c5r::Status st = Val::Run(&dummy, p, n);
		if(st.ok())
			memcpy(&lastPresent, p + offsetof(VPModelTapFrame, presentCount), sizeof(lastPresent));
		return st;
	}
	int64_t lastPresent = 0;   // the Present of the frame the layer holds (R1 pairing)
	c5r::Status draw(ID3D11DeviceContext*, int pass, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, uint32_t,
	                 const FrameTextures& ft, LayerDrawStats* st) override
	{
		draws++;
		lastFt = ft;
		if(st)
		{
			st->unknownDraws = 2;
			if(pass == 0)   // ticket 0709
			{
				st->keptInst = keptPerScenePass;
				st->keepEntries = 7;
			}
			// ticket 0789: the store's per-pass stats as the test sets them
			st->keptReentry = storeStats[pass ? 1 : 0].keptReentry;
			st->keptAbsent = storeStats[pass ? 1 : 0].keptAbsent;
			st->keptCut = storeStats[pass ? 1 : 0].keptCut;
			st->sameCamera = storeStats[pass ? 1 : 0].sameCamera;
			st->handOverRecs = storeStats[pass ? 1 : 0].handOverRecs;
			st->handOverInst = storeStats[pass ? 1 : 0].handOverInst;
			st->storeRemoved = storeStats[pass ? 1 : 0].storeRemoved;
			st->storeEvicted = storeStats[pass ? 1 : 0].storeEvicted;
			st->storeBytes = storeStats[pass ? 1 : 0].storeBytes;
			st->storePool = storeStats[pass ? 1 : 0].storePool;
			st->missed = storeStats[pass ? 1 : 0].missed;
			st->missedKnown = storeStats[pass ? 1 : 0].missedKnown;
			st->missedFrames = storeStats[pass ? 1 : 0].missedFrames;
			st->missedChecked = storeStats[pass ? 1 : 0].missedChecked;
			if(pass == 1)
			{
				st->keptInst = storeStats[1].keptInst;
				st->keepEntries = storeStats[1].keepEntries;
			}
			if(ft.haveViewProj)   // ticket 4882: the c5r layer's camera rows (applyFrameCamera) report so
			{
				st->camConsts = 1;
				st->shiftN = 2;
				st->shiftPxSum = 2.0 * shiftPx;
				st->shiftPxMax = shiftPx + pass;
			}
		}
		return drawSt;
	}
	FrameTextures lastFt;
	double shiftPx = 0;
	void clearScene() override { clears++; }
	void setDrawAll(bool all) override { drawAll = all; }
	// ticket 0709
	void setKeep(const KeepOptions& o) override
	{
		keep = o;
		keepCalls++;
	}
	KeepOptions keep;
	int keepCalls = 0;
	uint32_t keptPerScenePass = 0;   // reported as LayerDrawStats::keptInst by pass 0
	// ticket 0789
	LayerDrawStats storeStats[2];
	void setOwnership(const ModelOwnership& o) override
	{
		own = o;
		ownCalls++;
	}
	ModelOwnership own;
	int ownCalls = 0;
	std::string describe() const override { return "fake layer"; }
	mutable Val dummy;
	uint32_t geomHeld = 0;   // ticket 4883: CPU-built buffers the layer reports as held (no bytes)
	void heldGeometry(std::vector<std::pair<uint32_t, uint64_t>>& out) const override
	{
		out.clear();
		for(uint32_t i = 0; i < geomHeld; ++i)
			out.push_back({1u, 0x4883000000000000ull + i});
	}
};

// The product's section names are made from the EXE's pid; the test's fake compositor makes them under a pid nobody
// uses (this process's pid + a large offset is not an EXE's), so the scene opens them by setExePid.
struct ProductCompositor
{
	HANDLE hc = nullptr, hd = nullptr;
	VPShellModelTapControl* ctl = nullptr;
	uint8_t* data = nullptr;
	uint32_t pid = 0, slotBytes = 256u * 1024u;
	uint64_t seq = 0;
	uint32_t truncated = 0;   // ticket 0789: the next frames' header truncated (a cut tap)
	// ticket 4879: the real compositor's liveness rule (unit_filter.cpp's latch): with a block named here, wantBits and the
	// identity set are used only while its writerPid is this server's pid, so no frame is tapped and no set read before
	const VPShellMode* live = nullptr;
	bool isLive() const { return !live || live->writerPid == GetCurrentProcessId(); }
	ProductCompositor()
	{
		pid = 0x7F000000u + GetCurrentProcessId();
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_MODELTAP_CONTROL_FMT, pid);
		hc = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELTAP_CONTROL_BYTES, n);
		ctl = (VPShellModelTapControl*)MapViewOfFile(hc, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_MODELTAP_CONTROL_BYTES);
		_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_MODELTAP_DATA_FMT, pid);
		hd = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELTAP_SLOTS * slotBytes, n);
		data = (uint8_t*)MapViewOfFile(hd, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		ctl->magic = VPSHELL_MODELTAP_MAGIC;
		ctl->version = VPSHELL_MODELTAP_VERSION;
		ctl->state = VPMTAP_RUNNING;
		ctl->exePid = pid;
		ctl->slots = VPSHELL_MODELTAP_SLOTS;
		ctl->slotBytes = slotBytes;
		ctl->seq = 2;
	}
	~ProductCompositor()
	{
		UnmapViewOfFile(data);
		CloseHandle(hd);
		UnmapViewOfFile(ctl);
		CloseHandle(hc);
	}
	void next()
	{
		if(!isLive())
			return;
		const uint64_t n = seq + 1;
		std::vector<uint8_t> img = FakeCompositor::Image(n);
		const int64_t present = 1000 + (int64_t)n;   // the Present that ended EXE frame n (not in the hashed range)
		memcpy(img.data() + offsetof(VPModelTapFrame, presentCount), &present, sizeof(present));
		memcpy(img.data() + offsetof(VPModelTapFrame, truncated), &truncated, sizeof(truncated));   // ticket 0789 (header)
		const uint32_t slot = (uint32_t)(n % 3);
		memcpy(data + (size_t)slot * slotBytes, img.data(), img.size());
		VPModelTapFrame h;
		memcpy(&h, img.data(), sizeof(h));
		ctl->seq++;
		ctl->frameSeq = n;
		ctl->slot = slot;
		ctl->frameBytes = (uint32_t)img.size();
		ctl->fnv1a64 = h.fnv1a64;
		ctl->seq++;
		seq = n;
	}
	// the compositor's verdict on the set it finds (the real check, CheckModelSet)
	void readSet()
	{
		if(ctl->setGeneration == ctl->setGenerationApplied || !isLive())
			return;
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, VPSHELL_MODELSET_FMT, pid);
		HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, n);
		uint32_t res = VPMSET_OPEN;
		if(h)
		{
			const uint8_t* v = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
			res = v ? CheckModelSet(v, VPSHELL_MODELSET_MAX_BYTES, GetCurrentProcessId(), nullptr) : VPMSET_OPEN;
			if(v)
				UnmapViewOfFile(v);
			CloseHandle(h);
		}
		ctl->seq++;
		ctl->setReads++;
		ctl->setResult = res;
		if(res == VPMSET_OK)
			ctl->setGenerationApplied = ctl->setGeneration;
		ctl->seq++;
	}
};

static void SceneLoop(ModelsScene& s, ProductCompositor* pc, int loops, bool frames = true)
{
	for(int i = 0; i < loops; ++i)
	{
		if(pc && frames)
			pc->next();
		const int64_t t = srvrender::Qpc();
		s.poll(t);
		s.idle(1.0);
		if(pc)
			pc->readSet();
		FrameTextures ft;
		s.drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
		s.frameDone(srvrender::Qpc());
		Sleep(2);
	}
}

static std::unique_ptr<ModelsScene> NewScene(bool cities, bool landmarks, FakeLayer** out, VPShellMode* block,
                                             int source = kModelsSourceExe, bool drawAll = false)
{
	std::unique_ptr<ModelsScene> s = MakeModelsScene();
	ModelsOptions o;
	o.cities = cities;
	o.landmarks = landmarks;
	o.source = source;
	o.drawAll = drawAll;
	o.installMs = 3000;
	o.noFrameMs = 300;
	o.holdMs = 600;
	s->configure(o);
	memset((void*)block, 0, sizeof(*block));
	s->attachMode(block);
	FakeLayer* l = new FakeLayer();
	l->keys = {Info(1, KeyFamily::CityBlock, KeySource::CitySet, false), Info(3, KeyFamily::Landmark, KeySource::LandmarkRow, false)};
	s->setLayer(std::unique_ptr<ModelLayer>(l));
	*out = l;
	s->setDevice((ID3D11Device*)(uintptr_t)1);   // the fake layer never touches it
	return s;
}

static void CaseSceneFullPath()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, true, &l, &block, kModelsSourceExe, true);
	CHECK(l->drawAll);
	SceneLoop(*s, &pc, 3);
	CHECK(l->opens == 0 && block.word == 0);   // no database yet: the layer is not opened
	s->setExePid(pc.pid);
	s->setDatabase(L"C:\\nowhere\\db.db", 1);
	CHECK(pc.ctl->wantBits == 0);   // the reader is made, the control block opens at the next poll
	SceneLoop(*s, &pc, 300);
	CHECK(l->opens == 1);
	CHECK(pc.ctl->wantBits == (VPMODE_CITIES | VPMODE_LANDMARKS));
	CHECK(pc.ctl->setGeneration >= 1 && pc.ctl->setGenerationApplied == pc.ctl->setGeneration && pc.ctl->setResult == VPMSET_OK);
	const ModelsCounters c = s->counters();
	CHECK(c.layerOpen && c.deviceOk && c.setAcked && c.setKeys == 2 && c.setCities == 1 && c.setLandmarks == 1);
	CHECK(c.framesAccepted > 100 && c.drawnFrames > 100 && l->draws > 100 && c.unknownDraws > 0);
	CHECK(c.framesRefused == 0);   // frames before the layer opened are not read (openOnly), so none is refused
	CHECK(block.word == (VPMODE_CITIES | VPMODE_LANDMARKS) && s->modeBits() == block.word && block.writerPid == GetCurrentProcessId());
	CHECK(pc.ctl->ackFrameSeq == pc.seq || pc.ctl->ackFrameSeq + 1 == pc.seq);
	std::string why;
	CHECK(!s->failed(why));
	char line[2048] = {};
	s->line(line, sizeof(line));
	printf("  %s\n", line);
	CHECK(strstr(line, "cities on=1 bit=1 phase=live") != nullptr);
	// the compositor publishes nothing (a view without models: it publishes only while something is tapped, 10.5): every
	// published frame was read, so the bits stay
	SceneLoop(*s, &pc, 200, false);
	CHECK(block.word == (VPMODE_CITIES | VPMODE_LANDMARKS) && !s->failed(why));
	// the tap stops being usable (every published frame refused): the bits go at once, then the fatal step past the hold
	l->acceptSt = c5r::Status::fail(c5r::Err::BadLayout, "refused by the test");
	SceneLoop(*s, &pc, 30);
	CHECK(block.word == 0);
	SceneLoop(*s, &pc, 300);
	CHECK(s->failed(why) && why.find("hold") != std::string::npos);
	// the tap's state FAILED is stale too: covered by the freshness rule (state RUNNING); a refused frame keeps the last
	// good one
	// a refused frame keeps the last good one: covered by the reader's cases; the image hash names the current frame
	CHECK(s->imageHash() != 0);
}

// Ticket 4879 (step 0, the live smoke of 4875 on c5h-c, 2026-11-18): ServerUnits = 0, so no units writer claims the mode
// block; the compositor uses wantBits and the identity set only while writerPid is this server's. The model writer claims
// the block at attach, so the bits go live; before the fix writerPid stayed 0 and the install bound ended the server.
static void CaseSceneLivenessWithoutUnits()
{
	ProductCompositor pc;
	VPShellMode block;
	pc.live = &block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, true, &l, &block);
	CHECK(block.writerPid == GetCurrentProcessId() && block.word == 0 && block.changes == 0);   // claimed, nothing set yet
	s->setExePid(pc.pid);
	s->setDatabase(L"C:\\nowhere\\db.db", 1);
	SceneLoop(*s, &pc, 300);
	std::string why;
	CHECK(!s->failed(why));
	CHECK(pc.ctl->setGenerationApplied >= 1 && pc.ctl->setResult == VPMSET_OK && pc.seq > 100);
	CHECK(block.word == (VPMODE_CITIES | VPMODE_LANDMARKS) && block.writerPid == GetCurrentProcessId());
	// an option off on both: the block is never claimed (another server's or nobody's stays)
	ProductCompositor pc2;
	VPShellMode block2;
	FakeLayer* l2 = nullptr;
	std::unique_ptr<ModelsScene> s2 = NewScene(false, false, &l2, &block2);
	CHECK(block2.writerPid == 0);
	if(why.size())
		printf("  %s\n", why.c_str());
}

static void CaseSceneSetRefused()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	// the compositor refuses every set (a wrong server pid in its liveness check, say): the bit is never set, and the
	// install bound ends the server
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	for(int i = 0; i < 2000; ++i)
	{
		pc.next();
		s->poll(srvrender::Qpc());
		if(pc.ctl->setGeneration != pc.ctl->setGenerationApplied && pc.ctl->setResult != VPMSET_PID)
		{
			pc.ctl->seq++;
			pc.ctl->setReads++;
			pc.ctl->setResult = VPMSET_PID;
			pc.ctl->seq++;
		}
		FrameTextures ft;
		s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
		s->frameDone(srvrender::Qpc());
		std::string why;
		if(s->failed(why))
			break;
		Sleep(2);
	}
	std::string why;
	CHECK(block.word == 0 && s->counters().setRefused >= 1);
	CHECK(s->failed(why) && why.find("render_models_install_ms") != std::string::npos);
}

// Ticket 4885, the module in real time: a stand-in layer whose open is slow (the worker reads and parses in short bursts
// between waits, as a disk-bound open does) or hangs (blocked on an event: no CPU). Run from the case scene_failures.
struct SlowLayer : FakeLayer
{
	HANDLE gate = nullptr;   // the open waits on it first (a hung read); null = no wait
	double workMs = 0;       // then works this long: 2 ms on the CPU, 10 ms waiting, by turns
	c5r::Status open(const ModelsOptions& o, const std::wstring& db) override
	{
		if(gate)
			WaitForSingleObject(gate, 60000);
		const int64_t q0 = srvrender::Qpc();
		volatile uint64_t x = 0;
		while(srvrender::QpcToMs(srvrender::Qpc() - q0) < workMs)
		{
			const int64_t b = srvrender::Qpc();
			while(srvrender::QpcToMs(srvrender::Qpc() - b) < 2.0)
				x = x + 1;
			Sleep(10);
		}
		return FakeLayer::open(o, db);
	}
};

static std::unique_ptr<ModelsScene> NewSlowScene(SlowLayer* l, VPShellMode* block, double installMs, double stallMs)
{
	std::unique_ptr<ModelsScene> s = MakeModelsScene();
	ModelsOptions o;
	o.cities = true;
	o.landmarks = true;
	o.installMs = installMs;
	o.openStallMs = stallMs;
	o.openMaxMs = 0;
	o.noFrameMs = 300;
	o.holdMs = 600;
	s->configure(o);
	memset((void*)block, 0, sizeof(*block));
	s->attachMode(block);
	l->keys = {Info(1, KeyFamily::CityBlock, KeySource::CitySet, false), Info(3, KeyFamily::Landmark, KeySource::LandmarkRow, false)};
	s->setLayer(std::unique_ptr<ModelLayer>(l));
	s->setDevice((ID3D11Device*)(uintptr_t)1);
	return s;
}

// loops until the bits are both set, the module failed, or limitMs passed; the time spent
static double LoopUntil(ModelsScene& s, ProductCompositor& pc, VPShellMode& block, double limitMs)
{
	const int64_t q0 = srvrender::Qpc();
	std::string why;
	while(srvrender::QpcToMs(srvrender::Qpc() - q0) < limitMs && !s.failed(why) &&
	      block.word != (VPMODE_CITIES | VPMODE_LANDMARKS))
		SceneLoop(s, &pc, 1);
	return srvrender::QpcToMs(srvrender::Qpc() - q0);
}

static void CaseSceneOpenBounds()
{
	std::string why;
	// (a) an open of 1.5 s (making progress) while frames are drawn, install bound 300 ms: before 4885 the D118 step at
	// 300 ms; now no fatal, and both bits once ready
	{
		ProductCompositor pc;
		VPShellMode block;
		SlowLayer* l = new SlowLayer();
		l->workMs = 1500;
		std::unique_ptr<ModelsScene> s = NewSlowScene(l, &block, 300, 500);
		s->setExePid(pc.pid);
		s->setDatabase(L"C:\\nowhere\\db.db", 1);
		const double took = LoopUntil(*s, pc, block, 15000);
		CHECK(!s->failed(why) && block.word == (VPMODE_CITIES | VPMODE_LANDMARKS) && l->opens == 1 && took > 1500);
		char line[4096] = {};
		s->line(line, sizeof(line));
		CHECK(strstr(line, " | open state=open") != nullptr && strstr(line, "stall_fatal=0 max_fatal=0 fail_fatal=0") != nullptr);
		if(s->failed(why))
			printf("  %s\n", why.c_str());
	}
	// (b) an open that hangs (blocked, no CPU): the D118 step past render_models_open_stall_ms, naming the open, while
	// the install bound (300 ms) does not run
	{
		ProductCompositor pc;
		VPShellMode block;
		SlowLayer* l = new SlowLayer();
		HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		l->gate = gate;
		std::unique_ptr<ModelsScene> s = NewSlowScene(l, &block, 300, 400);
		s->setExePid(pc.pid);
		s->setDatabase(L"C:\\nowhere\\db.db", 1);
		const double took = LoopUntil(*s, pc, block, 10000);
		CHECK(s->failed(why) && why.find("model layer's open") != std::string::npos &&
		      why.find("render_models_open_stall_ms=400") != std::string::npos && why.find("install_ms") == std::string::npos);
		CHECK(took >= 400 && took < 5000 && block.word == 0 && !s->counters().layerOpen);
		SetEvent(gate);   // the worker ends; the module joins it when it goes
		s.reset();
		CloseHandle(gate);
	}
}

static void CaseSceneFailures()
{
	// the layer cannot open: fatal with its reason
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
		l->openSt = c5r::Status::fail(c5r::Err::NotFound, "ArtDefine_Landmarks missing");
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		SceneLoop(*s, &pc, 50);
		std::string why;
		CHECK(s->failed(why) && why.find("cannot open") != std::string::npos && why.find("ArtDefine_Landmarks") != std::string::npos);
		CHECK(block.word == 0);
	}
	// the renderer cannot start
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(false, true, &l, &block);
		l->devSt = c5r::Status::fail(c5r::Err::Device, "CreateVertexShader");
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		SceneLoop(*s, &pc, 50);
		std::string why;
		CHECK(s->failed(why) && why.find("renderer cannot start") != std::string::npos && block.word == 0);
	}
	// no layer in the build: fatal at the first poll
	{
		std::unique_ptr<ModelsScene> s = MakeModelsScene();
		ModelsOptions o;
		o.cities = true;
		s->configure(o);
		s->poll(srvrender::Qpc());
		std::string why;
		CHECK(s->failed(why) && why.find("no model layer") != std::string::npos);
	}
	// both options 0: nothing happens at all (no reader, no layer, no write)
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(false, false, &l, &block);
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		SceneLoop(*s, &pc, 30);
		std::string why;
		CHECK(!s->failed(why) && l->opens == 0 && pc.ctl->wantBits == 0 && pc.ctl->ackFrameSeq == 0 && block.word == 0 &&
		      block.changes == 0 && l->draws == 0);
	}
	// source own: an empty scene, the bits never set, no fatal step, nothing drawn
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(true, true, &l, &block, kModelsSourceOwn);
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		SceneLoop(*s, &pc, 30);
		std::string why;
		CHECK(!s->failed(why) && block.word == 0 && l->draws == 0 && pc.ctl->wantBits == 0);
	}
	// a draw that fails is counted, not fatal
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
		l->drawSt = c5r::Status::fail(c5r::Err::Device, "Map");
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		SceneLoop(*s, &pc, 100);
		std::string why;
		CHECK(!s->failed(why) && s->counters().drawErrors > 10 && s->counters().drawnFrames == 0);
	}
	// every frame refused by the content check: the bit is never set (no fresh frame), the install bound ends it
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
		l->acceptSt = c5r::Status::fail(c5r::Err::BadLayout, "a draw's rows past the row block");
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		for(int i = 0; i < 2000; ++i)
		{
			SceneLoop(*s, &pc, 1);
			std::string why;
			if(s->failed(why))
				break;
		}
		std::string why;
		CHECK(block.word == 0 && s->counters().framesRefused > 10 && pc.ctl->ackResult == (int32_t)c5r::Err::BadLayout);
		CHECK(s->failed(why) && why.find("install") != std::string::npos);
	}
	CaseSceneOpenBounds();   // ticket 4885
}

// 4874's rule (city-renderer.md 10.4): a bit is set only once the compositor's setGenerationApplied equals the generation
// published last; a live bit stays on the older applied generation while a newer one waits
static void CaseSceneSetRule()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, true, &l, &block);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	auto loop = [&](int n, bool compositorReads) {
		for(int i = 0; i < n; ++i)
		{
			pc.next();
			s->poll(srvrender::Qpc());
			s->idle(1.0);
			if(compositorReads)
				pc.readSet();
			FrameTextures ft;
			s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
			s->frameDone(srvrender::Qpc());
			Sleep(2);
		}
	};
	// the compositor does not read the set: generation 1 published, not applied: no bit
	loop(100, false);
	CHECK(pc.ctl->setGeneration == 1 && pc.ctl->setGenerationApplied == 0 && block.word == 0);
	// applied: both bits
	loop(20, true);
	CHECK(pc.ctl->setGenerationApplied == 1 && block.word == (VPMODE_CITIES | VPMODE_LANDMARKS));
	// the layer's drawable set grows (generation 2) and the compositor does not read it yet: the live bits stay; then a
	// load clears them, and they are not set again although generation 1 had keys of both (setGenerationApplied 1 != 2)
	l->keys.push_back(Info(9, KeyFamily::Landmark, KeySource::LandmarkRow, false));
	l->changed = true;
	loop(10, false);
	CHECK(pc.ctl->setGeneration == 2 && pc.ctl->setGenerationApplied == 1 && block.word == (VPMODE_CITIES | VPMODE_LANDMARKS));
	s->onLoadBegin(srvrender::Qpc());
	loop(40, false);   // well inside the install bound (3 s)
	CHECK(pc.ctl->setGeneration == 2 && pc.ctl->setGenerationApplied == 1 && block.word == 0);
	// applied: set again
	loop(20, true);
	CHECK(pc.ctl->setGenerationApplied == 2 && block.word == (VPMODE_CITIES | VPMODE_LANDMARKS));
	// live, a generation 3 waiting: the live bits stay
	l->keys.push_back(Info(10, KeyFamily::CityBlock, KeySource::CitySet, false));
	l->changed = true;
	loop(40, false);
	std::string why;
	CHECK(pc.ctl->setGeneration == 3 && pc.ctl->setGenerationApplied == 2 && block.word == (VPMODE_CITIES | VPMODE_LANDMARKS) &&
	      !s->failed(why));
}

// R1 (landmark-renderer.md 7.7): beginFrame(P) puts the tap of Present P into the layer; one older when P's is not there
// (after the bounded wait), one newer only when nothing older exists; 0 = no pairing
static void CaseScenePairing()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 20);   // frames 1..20 published, the layer open; the history holds the last four (Presents 1017..1020)
	CHECK(l->lastPresent == 1020);
	s->beginFrame(1018);      // an older frame of the history: put back into the layer
	ModelsCounters c = s->counters();
	CHECK(l->lastPresent == 1018 && c.pairExact == 1 && c.pairWaited == 0);
	s->beginFrame(1020);
	CHECK(l->lastPresent == 1020 && s->counters().pairExact == 2);
	// Present 1022 not published: with the default wait (0 since ticket 4882: under lockstep the tap of P comes only after
	// the server's frame P was taken) the newest older one (1020) is used at once, two Presents behind here
	int64_t q0 = srvrender::Qpc();
	s->beginFrame(1022);
	double ms = srvrender::QpcToMs(srvrender::Qpc() - q0);
	c = s->counters();
	printf("  older without a wait: %.2f ms\n", ms);
	CHECK(l->lastPresent == 1020 && c.pairOlder == 1 && c.maxLagPresents == 2 && c.maxPairWaitMs == 0 && c.pairWaited == 0);
	// with render_models_pair_wait_ms = 4 the reader is polled for 4 ms first (nothing comes), then the same choice
	{
		ModelsOptions o;
		o.cities = true;
		o.installMs = 3000;
		o.noFrameMs = 300;
		o.holdMs = 600;
		o.pairWaitMs = 4;
		s->configure(o);
	}
	q0 = srvrender::Qpc();
	s->beginFrame(1022);
	ms = srvrender::QpcToMs(srvrender::Qpc() - q0);
	c = s->counters();
	printf("  older after a 4 ms wait: %.2f ms\n", ms);
	CHECK(l->lastPresent == 1020 && c.pairOlder == 2 && ms >= 3.5 && c.maxPairWaitMs >= 3.5);
	// a Present older than every tap held: the oldest newer one, counted
	s->beginFrame(1000);
	c = s->counters();
	CHECK(c.pairNewer == 1 && l->lastPresent == 1017);
	// published while waiting: a thread publishes frame 21 (Present 1021) about one timer tick after the wait begins; the
	// wait raised to 200 ms (render_models_pair_wait_ms) so that the scheduler cannot make it miss
	{
		ModelsOptions o;
		o.cities = true;
		o.installMs = 3000;
		o.noFrameMs = 300;
		o.holdMs = 600;
		o.pairWaitMs = 200;
		s->configure(o);
	}
	struct Arg
	{
		ProductCompositor* pc;
	} arg = {&pc};
	HANDLE th = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
		Sleep(1);
		((Arg*)p)->pc->next();
		return 0;
	}, &arg, 0, nullptr);
	s->beginFrame(1021);
	WaitForSingleObject(th, 5000);
	CloseHandle(th);
	c = s->counters();
	printf("  published during the wait: max wait %.2f ms\n", c.maxPairWaitMs);
	CHECK(l->lastPresent == 1021 && c.pairExact == 3 && c.pairWaited == 1);
	// 0: no pairing, nothing changes
	const int accepts = l->accepts;
	s->beginFrame(0);
	CHECK(l->accepts == accepts);
	// a load clears the history: no tap to pair with (a frame the thread published late is read first)
	s->poll(srvrender::Qpc());
	s->onLoadBegin(srvrender::Qpc());
	s->beginFrame(1030);
	CHECK(s->counters().pairNone == 1);
	char line[2048] = {};
	s->line(line, sizeof(line));
	CHECK(strstr(line, " pair exact=") != nullptr);
}

// ticket 4883: the image the module reports (imageHash, writeCurrentImage: what the scene recorder writes as the frame's
// modeltap/ member after its draw) is the frame the layer holds, i.e. the tap beginFrame paired with the frame's Present,
// not the reader's newest. With the reader's newest (4875's form) the first check fails: Present 1020 written for a
// frame that drew 1018.
static bool WrittenTapPresent(ModelsScene& s, int64_t* present, uint64_t* hash)
{
	const std::wstring path = g_dir + L"\\recorded_image_4883.mtap";
	if(!s.writeCurrentImage(path).ok())
		return false;
	std::vector<uint8_t> b;
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER sz = {};
	DWORD got = 0;
	bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart >= (LONGLONG)sizeof(VPModelTapFrame);
	if(ok)
	{
		b.resize((size_t)sz.QuadPart);
		ok = ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) && got == b.size();
	}
	CloseHandle(f);
	DeleteFileW(path.c_str());
	if(!ok)
		return false;
	memcpy(present, b.data() + offsetof(VPModelTapFrame, presentCount), sizeof(*present));
	*hash = VPModelHash64(b.data(), b.size());
	return true;
}

static void CaseSceneRecordedImage()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 20);   // Presents 1017..1020 in the history; the layer holds the newest (1020)
	int64_t present = 0;
	uint64_t hash = 0;
	CHECK(WrittenTapPresent(*s, &present, &hash) && present == 1020 && hash == s->imageHash());
	s->beginFrame(1018);      // the frame of Present 1018 draws the tap of 1018 (put back into the layer)
	CHECK(l->lastPresent == 1018);
	CHECK(WrittenTapPresent(*s, &present, &hash) && present == 1018 && hash == s->imageHash());
	s->beginFrame(1022);      // not published: the newest older one, 1020
	CHECK(l->lastPresent == 1020 && WrittenTapPresent(*s, &present, &hash) && present == 1020);
	s->beginFrame(1019);
	CHECK(l->lastPresent == 1019 && WrittenTapPresent(*s, &present, &hash) && present == 1019 && hash == s->imageHash());
	s->beginFrame(0);         // no pairing: the layer keeps 1019, and so does the reported image
	CHECK(l->lastPresent == 1019 && WrittenTapPresent(*s, &present, &hash) && present == 1019);
	// the CPU-built meshes the layer holds (the scene recorder's gap note, sb::kModelGeomNote, when not 0 in a span)
	CHECK(s->heldGeometryCount() == 0);
	l->geomHeld = 4;
	CHECK(s->heldGeometryCount() == 4);
	s->onLoadBegin(srvrender::Qpc());
	CHECK(s->heldGeometryCount() == 4);   // a load clears the scene, not the store (the section's entries release them)
	l->geomHeld = 0;
	CHECK(s->heldGeometryCount() == 0);
}

// ticket 4888: the CPU-built meshes as the scene bundle's model_geom member. The module's snapshot (what the recorder
// writes) is the layer's held buffers with their strides and bytes, sorted by (kind, hash); heldSetHash moves only with the
// held set; loadGeometry (the player) makes the layer hold exactly the list given, through the layer's own checks.
static void CaseSceneGeometryMember()
{
	std::unique_ptr<ModelsScene> s = MakeModelsScene();
	ModelsOptions o;
	o.cities = true;
	s->configure(o);
	GeomLayer* l = new GeomLayer();
	s->setLayer(std::unique_ptr<ModelLayer>(l));
	std::vector<GeomBuffer> snap;
	s->geometrySnapshot(snap);
	CHECK(snap.empty() && s->heldSetHash() == 0);   // the layer is not open yet: nothing
	s->setDatabase(L"db", 1);
	GeomBuffer ib, vb, vb2;
	ib.kind = VPMGEOM_KIND_INDEX32;
	ib.stride = 4;
	ib.bytes.resize(4 * 270);
	for(size_t i = 0; i < ib.bytes.size(); ++i)
		ib.bytes[i] = (uint8_t)(i * 7);
	ib.hash = VPModelGeomHash(ib.kind, ib.bytes.data(), ib.bytes.size());
	vb.kind = VPMGEOM_KIND_VERTEX;
	vb.stride = 20;
	vb.bytes.resize(20 * 160);
	for(size_t i = 0; i < vb.bytes.size(); ++i)
		vb.bytes[i] = (uint8_t)(i * 13 + 1);
	vb.hash = VPModelGeomHash(vb.kind, vb.bytes.data(), vb.bytes.size());
	vb2 = vb;
	vb2.stride = 32;
	vb2.bytes.resize(32 * 50);
	vb2.hash = VPModelGeomHash(vb2.kind, vb2.bytes.data(), vb2.bytes.size());
	// the player's load: the layer then holds the list (opened on first use, as loadImage)
	CHECK(s->loadGeometry({vb, ib}).ok() && s->heldGeometryCount() == 2 && l->held.size() == 2);
	const uint64_t h2 = s->heldSetHash();
	CHECK(h2 != 0);
	s->geometrySnapshot(snap);
	CHECK(snap.size() == 2 && snap[0].kind == VPMGEOM_KIND_INDEX32 && snap[0].hash == ib.hash && snap[0].stride == 4 &&
	      snap[0].bytes == ib.bytes);
	CHECK(snap.size() == 2 && snap[1].kind == VPMGEOM_KIND_VERTEX && snap[1].hash == vb.hash && snap[1].stride == 20 &&
	      snap[1].bytes == vb.bytes);
	// the same list again: nothing added or released, the same set hash
	const int adds = l->adds;
	CHECK(s->loadGeometry({ib, vb}).ok() && l->adds == adds && l->releases == 0 && s->heldSetHash() == h2);
	// a changed list: the buffer not in it is released, the new one added, the set hash moves
	CHECK(s->loadGeometry({ib, vb2}).ok() && l->releases == 1 && s->heldGeometryCount() == 2 &&
	      !l->holdsGeometry(VPMGEOM_KIND_VERTEX, vb.hash) && l->holdsGeometry(VPMGEOM_KIND_VERTEX, vb2.hash));
	CHECK(s->heldSetHash() != h2 && s->heldSetHash() != 0);
	// a buffer the layer refuses (bytes changed after the hash): the load fails naming the buffer and the layer's reason
	GeomBuffer bad = vb;
	bad.bytes[5] ^= 0xFF;
	const c5r::Status st = s->loadGeometry({bad});
	CHECK(!st.ok() && st.msg.find("refuses the vertex buffer") != std::string::npos &&
	      st.msg.find("content differs from its hash") != std::string::npos);
	CHECK(!l->holdsGeometry(VPMGEOM_KIND_VERTEX, bad.hash) && s->heldGeometryCount() == 0);   // the others were released first
	// an empty list: none held, set hash 0
	CHECK(s->loadGeometry({ib}).ok() && s->heldGeometryCount() == 1);
	CHECK(s->loadGeometry({}).ok() && s->heldGeometryCount() == 0 && s->heldSetHash() == 0);
	// a layer that cannot open: refused, nothing held
	std::unique_ptr<ModelsScene> s2 = MakeModelsScene();
	s2->configure(o);
	s2->setLayer(std::unique_ptr<ModelLayer>(new GeomLayer()));
	CHECK(!s2->loadGeometry({ib}).ok() && s2->heldSetHash() == 0);   // no database: the layer never opens
}

static void CaseSceneFileImage()
{
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, true, &l, &block, kModelsSourceExe, true);
	s->setDatabase(L"db", 1);
	const std::vector<uint8_t> img = FakeCompositor::Image(9);
	CHECK(s->loadImage(img.data(), img.size()).ok() && l->opens == 1 && l->accepts == 1);
	CHECK(s->imageHash() == VPModelHash64(img.data(), img.size()));
	const std::wstring path = g_dir + L"\\frame000000.mtap";
	CHECK(s->writeCurrentImage(path).ok());
	DeleteFileW(path.c_str());
	l->acceptSt = c5r::Status::fail(c5r::Err::BadMagic, "x");
	CHECK(!s->loadImage(img.data(), img.size()).ok() && s->imageHash() == VPModelHash64(img.data(), img.size()));
	// replay draws without a compositor (the offline player: no bits, no tap)
	SceneLoop(*s, nullptr, 3);
	CHECK(l->draws == 3 && block.word == 0);
}

// MC #6166: render_units 0 + render_cities 1 = the copy wanted and the unit writers silent; all three 0 = no copy. The
// copy itself is srv_remote_main.cpp's (UnitDatabaseAtLoad, guarded by DatabaseCopyWanted): not linked here.
static void CaseFeedFlag()
{
	SrvFeed::SetEnabled(false);
	SrvFeed::SetUnitsEnabled(false);
	SrvFeed::SetModelDatabaseWanted(true);
	CHECK(!SrvFeed::ModelDatabaseWanted() && !SrvFeed::DatabaseCopyWanted());   // the feed off: nothing wanted
	SrvFeed::SetEnabled(true);
	CHECK(SrvFeed::ModelDatabaseWanted() && SrvFeed::DatabaseCopyWanted() && !SrvFeed::UnitsEnabled());
	SrvFeed::Event e = {};
	CHECK(SrvFeed::PushUnit(e, nullptr, SrvFeed::UnitOp::kNone) == 0);   // the units' writers stay off (silent)
	SrvFeed::SetModelDatabaseWanted(false);
	CHECK(!SrvFeed::ModelDatabaseWanted() && !SrvFeed::DatabaseCopyWanted() && !SrvFeed::UnitsEnabled());   // all off
	SrvFeed::SetUnitsEnabled(true);
	CHECK(SrvFeed::DatabaseCopyWanted() && !SrvFeed::ModelDatabaseWanted());   // the units' own case unchanged
	SrvFeed::SetUnitsEnabled(false);
	SrvFeed::SetEnabled(false);
}

// Ticket 4882 (s3 point 7): without the unit feed the model load mark pushes kLoadBegin itself (a[1] = the player), once
// per call; with the unit feed on nothing (ResetUnitsAt pushes it: never twice); with the feed off nothing.
static void CaseFeedModelLoadBegin()
{
	SrvFeed::Batch b;
	SrvFeed::SetEnabled(false);
	SrvFeed::SetUnitsEnabled(false);
	CHECK(SrvFeed::PushModelLoadBegin(0) == 0);
	SrvFeed::SetEnabled(true);
	SrvFeed::Drain(b);   // whatever earlier cases left
	const uint32_t seq = SrvFeed::PushModelLoadBegin(3);
	CHECK(seq != 0);
	CHECK(SrvFeed::Drain(b));   // the batch is dirty
	int loads = 0;
	for(const SrvFeed::Event& e : b.events)
		if(e.kind == SrvFeed::kLoadBegin)
		{
			loads++;
			CHECK(e.seq == seq && e.a[1] == 3 && e.plot == -1 && e.turn == -1);
		}
	CHECK(loads == 1);
	SrvFeed::SetUnitsEnabled(true);
	CHECK(SrvFeed::PushModelLoadBegin(0) == 0);   // the unit feed's own mark pushes it
	SrvFeed::Drain(b);
	loads = 0;
	for(const SrvFeed::Event& e : b.events)
		loads += e.kind == SrvFeed::kLoadBegin ? 1 : 0;
	CHECK(loads == 0);
	SrvFeed::ResetUnits();   // ... and does
	SrvFeed::Drain(b);
	loads = 0;
	for(const SrvFeed::Event& e : b.events)
		loads += e.kind == SrvFeed::kLoadBegin ? 1 : 0;
	CHECK(loads == 1);
	SrvFeed::SetUnitsEnabled(false);
	SrvFeed::SetEnabled(false);
	// the module's side of the event: a load clears the layer's scene (and the bits: mode_load_ends_loss)
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, nullptr, 2, false);
	const int clears = l->clears;
	s->onLoadBegin(srvrender::Qpc());
	CHECK(l->clears == clears + 1);
}

// Ticket 0598: a camera as srv_render_scene.cpp builds view 0's (view x projection, row-major, the row-vector product):
// a look-at from `eye` to `at` (z up) and a perspective; or, with ortho, an orthographic projection (the reflection's form).
static void TestViewProj(const double eye[3], const double at[3], bool ortho, float out[16])
{
	double z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
	const double zl = sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	for(double& v : z)
		v /= zl;
	const double up[3] = {0, 0, 1};
	double x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
	const double xl = sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	for(double& v : x)
		v /= xl;
	const double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
	double view[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, 0, 0, 0, 1};
	for(int k = 0; k < 3; ++k)
	{
		const double* axis = k == 0 ? x : k == 1 ? y : z;
		view[12 + k] = -(axis[0] * eye[0] + axis[1] * eye[1] + axis[2] * eye[2]);
	}
	double proj[16] = {};
	const double zn = 10.0, zf = 20000.0;
	if(ortho)
	{
		proj[0] = 2.0 / 4000.0;
		proj[5] = 2.0 / 4000.0;
		proj[10] = 1.0 / (zf - zn);
		proj[14] = -zn / (zf - zn);
		proj[15] = 1.0;
	}
	else
	{
		const double f = 1.0 / tan(0.5 * 0.785398);
		proj[0] = f / (16.0 / 9.0);
		proj[5] = f;
		proj[10] = zf / (zf - zn);
		proj[11] = 1.0;
		proj[14] = -zn * zf / (zf - zn);
	}
	for(int i = 0; i < 4; ++i)
		for(int j = 0; j < 4; ++j)
		{
			double a = 0;
			for(int k = 0; k < 4; ++k)
				a += view[i * 4 + k] * proj[k * 4 + j];
			out[i * 4 + j] = (float)a;
		}
}

// Ticket 0598 (city-renderer.md 15.2, 15.8): the eye written into PS cb1 row 16 is the frame's own main-view eye (the
// camera-to-world position, else solved from the frame's view-projection) and reaches the layer for both passes; the
// reflection pass (whose viewProj is the reflection camera's, orthographic) carries the main view's eye, as stock's row 16
// does in its reflection draws (0585's 267 reflection-target draws, 15.2 [C]). The tapped frame's eye (one pan step
// behind) is rejected by the same check.
static void CaseSceneEyeRow16()
{
	const double frameEye[3] = {991.95, -794.19, 473.70};   // P4's eye (15.2)
	const double tapEye[3] = {985.95, -794.19, 473.70};     // the tapped frame's: one pan step west
	const double at[3] = {991.95, -300.0, 0.0};
	float vp[16], vpTap[16], vpRefl[16];
	TestViewProj(frameEye, at, false, vp);
	TestViewProj(tapEye, at, false, vpTap);
	TestViewProj(frameEye, at, true, vpRefl);
	const auto isFrameEye = [&](const float e[3]) {
		for(int k = 0; k < 3; ++k)
			if(fabs((double)e[k] - frameEye[k]) > 0.05)   // float view-projection: about 1e-5 relative
				return false;
		return true;
	};
	float e[3] = {};
	CHECK(EyeOfViewProj(vp, e) && isFrameEye(e));
	float et[3] = {};
	CHECK(EyeOfViewProj(vpTap, et) && !isFrameEye(et));   // the tapped frame's eye fails the check
	float eo[3] = {7, 7, 7};
	CHECK(!EyeOfViewProj(vpRefl, eo) && eo[0] == 7 && eo[1] == 7 && eo[2] == 7);   // orthographic: no eye, untouched
	float bad[16] = {};
	CHECK(!EyeOfViewProj(bad, eo));
	memcpy(bad, vp, sizeof(bad));
	bad[5] = NAN;
	CHECK(!EyeOfViewProj(bad, eo));
	// FrameEye: the camera-to-world position exactly when given (the EXE's own eye value), else the solve
	float cw[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)frameEye[0], (float)frameEye[1], (float)frameEye[2], 1};
	float ef[3] = {};
	CHECK(FrameEye(cw, vp, ef) && ef[0] == (float)frameEye[0] && ef[1] == (float)frameEye[1] && ef[2] == (float)frameEye[2]);
	float cwTap[16];
	memcpy(cwTap, cw, sizeof(cw));
	cwTap[12] = (float)tapEye[0];
	CHECK(FrameEye(cwTap, vpTap, ef) && !isFrameEye(ef));   // the tapped frame's camera gives the tapped eye: rejected
	float zero[16] = {};
	CHECK(FrameEye(zero, vp, ef) && isFrameEye(ef));           // no position (all zero): the solve
	CHECK(FrameEye(nullptr, vp, ef) && isFrameEye(ef));
	float cwBad[16];
	memcpy(cwBad, cw, sizeof(cw));
	cwBad[3] = NAN;
	cwBad[12] = 1.0f;
	CHECK(FrameEye(cwBad, vp, ef) && isFrameEye(ef));          // a non-finite matrix: not used, the solve
	CHECK(!FrameEye(zero, vpRefl, eo));                        // neither: no eye
	// the module hands the frame's eye to the layer for both passes (the reflection copy keeps the main view's)
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 20);
	FrameTextures ft;
	ft.haveViewProj = true;
	memcpy(ft.viewProj, vp, sizeof(ft.viewProj));
	ft.haveEye = FrameEye(cw, ft.viewProj, ft.eye);   // as srv_render_scene.cpp's models camera block
	s->beginFrame(1021);   // one Present behind: the tap's own eye would be the older one
	FrameTextures refl = ft;   // as the reflection pass: the copy with the reflection camera's viewProj
	memcpy(refl.viewProj, vpRefl, sizeof(refl.viewProj));
	s->drawReflection(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, refl);
	CHECK(l->lastFt.haveEye && isFrameEye(l->lastFt.eye) && l->lastFt.viewProj[0] == vpRefl[0]);
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	CHECK(l->lastFt.haveEye && isFrameEye(l->lastFt.eye));
}

// Ticket 4882: the frame's camera reaches the layer for both passes and the layer's shift is counted by the pairing of
// the frame (exact or older) and per line window; FrameTextures without a camera count nothing.
static void CaseSceneCameraRows()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 20);   // Presents 1001..1020 published; no camera in SceneLoop's FrameTextures
	CHECK(s->counters().camFrames == 0);
	FrameTextures ft;
	ft.haveViewProj = true;
	ft.viewProj[0] = 2.0f;
	l->shiftPx = 0.0;
	s->beginFrame(1020);   // exact
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	CHECK(l->lastFt.haveViewProj && l->lastFt.viewProj[0] == 2.0f);
	l->shiftPx = 12.5;
	s->beginFrame(1021);   // not published: the newest older (1020), one Present behind
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	s->drawReflection(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);   // max 13.5 (pass 1)
	ModelsCounters c = s->counters();
	CHECK(c.camFrames == 2 && c.shiftExactN == 1 && c.shiftExactMaxPx == 0.0);
	CHECK(c.shiftOlderN == 1 && c.shiftOlderSumPx == 12.5 && c.shiftOlderMaxPx == 12.5);
	char line[2048] = {};
	s->line(line, sizeof(line));
	printf("  %s\n", strstr(line, " cam frames=") ? strstr(line, " cam frames=") : line);
	CHECK(strstr(line, " cam frames=2 exact_n=1 exact_max_px=0.000 older_n=1 older_mean_px=12.50 older_max_px=12.50 "
	                   "win_max_px=12.50 refl_win_max_px=13.50") != nullptr);
	s->line(line, sizeof(line));   // the window starts again
	CHECK(strstr(line, "win_max_px=0.00 refl_win_max_px=0.00") != nullptr);
	CHECK(c.pairWaited == 0 && c.maxPairWaitMs == 0);   // the default: no wait
	CaseSceneEyeRow16();   // ticket 0598
}

// ---- ticket 0709 (pan-coherence.md 5.14): the keep of the instances the EXE culled at the drawn tap's camera ----
// A game-like camera (row-vector product, left-handed, z up): the eye 400 units behind and 600 above the point looked at
// (cx, 0, 0), 45 degrees vertical, aspect 1.8, depth 10 .. 5000.
static void KeepCam(float cx, float vp[16])
{
	const double eye[3] = {cx, -400.0, 600.0}, at[3] = {cx, 0.0, 0.0}, up[3] = {0.0, 0.0, 1.0};
	double z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
	const auto norm = [](double* v) {
		const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		for(int i = 0; i < 3; ++i)
			v[i] /= l;
	};
	norm(z);
	double x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
	norm(x);
	const double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
	const auto dot = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
	const double v[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1};
	const double h = 1.0 / std::tan(3.14159265358979 / 8.0), w = h / 1.8, zn = 10.0, zf = 5000.0, q = zf / (zf - zn);
	const double pr[16] = {w, 0, 0, 0, 0, h, 0, 0, 0, 0, q, 1, 0, 0, -q * zn, 0};
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
		{
			double a = 0;
			for(int k = 0; k < 4; ++k)
				a += v[r * 4 + k] * pr[k * 4 + c];
			vp[r * 4 + c] = (float)a;
		}
}
typedef CulledKeep<int> TestKeep;
static TestKeep::Seen KeepSeen(uint64_t id, float x, float y, float camX, int payload)
{
	TestKeep::Seen s;
	float vp[16];
	KeepCam(camX, vp);
	s.id = id;
	s.x = x;
	s.y = y;
	s.box = CullGroundBox(vp);
	s.payload = payload;
	return s;
}
static std::vector<uint64_t> KeepSelect(const TestKeep& k, int64_t tapPresent, const std::unordered_set<uint64_t>& held,
                                        float tapCamX, float frameCamX, KeepStats* st = nullptr, float wrap = 0.0f)
{
	float c[16], f[16];
	KeepCam(tapCamX, c);
	KeepCam(frameCamX, f);
	std::vector<const TestKeep::Entry*> out;
	KeepStats local;
	k.select(tapPresent, held, c, f, wrap, out, st ? st : &local);
	std::vector<uint64_t> ids;
	for(const TestKeep::Entry* e : out)
		ids.push_back(e->id);
	return ids;
}

// The box of the camera: the ground under the screen's centre inside it, the screen's corners (unprojected to z = 0)
// inside it, the distances outside it in world units, the wrap copies, a singular matrix refused
static void CaseKeepGroundBox()
{
	float vp[16];
	KeepCam(1000.0f, vp);
	const GroundBox b = CullGroundBox(vp);
	printf("  box x %.1f .. %.1f, y %.1f .. %.1f\n", b.x0, b.x1, b.y0, b.y1);
	CHECK(b.ok && b.x0 < 1000.0f && b.x1 > 1000.0f && b.y0 < 0.0f && b.y1 > 0.0f);
	CHECK(std::fabs((b.x0 + b.x1) * 0.5f - 1000.0f) < 0.05f);   // symmetric about the camera's x
	CHECK(GroundBoxOutside(b, 1000.0f, 0.0f, 0.0f) < -100.0f);
	CHECK(std::fabs(GroundBoxOutside(b, b.x1 + 10.0f, 0.0f, 0.0f) - 10.0f) < 0.01f);
	CHECK(std::fabs(GroundBoxOutside(b, b.x0 - 25.0f, 0.0f, 0.0f) - 25.0f) < 0.01f);
	CHECK(std::fabs(GroundBoxOutside(b, 1000.0f, b.y1 + 5.0f, 0.0f) - 5.0f) < 0.01f);
	// a wrap copy of the box (x + W) holds the point W to the right of an inside one
	CHECK(GroundBoxOutside(b, 1000.0f + 4000.0f, 0.0f, 0.0f) > 1000.0f);
	CHECK(GroundBoxOutside(b, 1000.0f + 4000.0f, 0.0f, 4000.0f) < -100.0f);
	CHECK(std::fabs(GroundBoxOutside(b, b.x1 + 10.0f - 4000.0f, 0.0f, 4000.0f) - 10.0f) < 0.01f);
	// the ground points of the screen's four corners lie inside (or on) the box
	int inside = 0;
	for(int sx = -1; sx <= 1; sx += 2)
		for(int sy = -1; sy <= 1; sy += 2)
		{
			// search along the screen ray for z = 0 by projecting ground points: the point whose NDC is the corner
			float bestErr = 1e9f, bx = 0, by = 0;
			for(float gx = b.x0 - 50.0f; gx <= b.x1 + 50.0f; gx += 2.0f)
				for(float gy = b.y0 - 50.0f; gy <= b.y1 + 50.0f; gy += 2.0f)
				{
					const float cx = gx * vp[0] + gy * vp[4] + vp[12], cy = gx * vp[1] + gy * vp[5] + vp[13];
					const float cw = gx * vp[3] + gy * vp[7] + vp[15];
					if(cw <= 0.0f)
						continue;
					const float e = std::fabs(cx / cw - sx) + std::fabs(cy / cw - sy);
					if(e < bestErr)
					{
						bestErr = e;
						bx = gx;
						by = gy;
					}
				}
			if(bestErr < 0.05f && GroundBoxOutside(b, bx, by, 0.0f) <= 6.0f)
				inside++;
		}
	CHECK(inside == 4);
	float zero[16] = {};
	CHECK(!CullGroundBox(zero).ok);
	CHECK(GroundBoxOutside(CullGroundBox(zero), 0.0f, 0.0f, 0.0f) > 1e30f);
	float vp2[16];
	KeepCam(1000.0f, vp2);
	CHECK(SameViewProj(vp, vp2));
	vp2[12] += 0.01f * std::fabs(vp2[12]) + 0.01f;
	CHECK(!SameViewProj(vp, vp2));
}

// The rule: an instance that re-enters (held by an older drawn tap, outside the drawn tap's box, the camera moved away
// from it since, inside the frame's box + 64) is kept; one that vanished inside the drawn tap's box, or while the camera
// came nearer, or that a tap holds, or whose sighting is older than the age, or that leaves at the trailing edge, is not;
// a still camera keeps nothing
static void CaseKeepRule()
{
	float vp[16];
	KeepCam(0.0f, vp);
	const GroundBox a = CullGroundBox(vp);   // camera A at x 0
	const float iy = 0.0f;
	// instance 1: 50 units inside A's box at the left; instance 2 near the middle (held by every tap)
	const float ix = a.x0 + 50.0f;
	TestKeep k;
	KeepOptions o;
	k.configure(o);
	// drawn tap 100 at camera A holds 1 and 2; drawn tap 101 at x + 300 holds only 2 (1 culled, 250 units out)
	k.commit(100, {KeepSeen(1, ix, iy, 0.0f, 11), KeepSeen(2, 0.0f, 0.0f, 0.0f, 22)});
	k.commit(101, {KeepSeen(2, 0.0f, 0.0f, 300.0f, 22)});
	CHECK(k.size() == 2);
	const std::unordered_set<uint64_t> held2 = {2};
	// re-entry: frame P drawn from tap 102 (camera x + 200: 1 is 150 out of its box) at camera x + 100 (1 is 50 out of
	// its box, inside + 64): kept, with its payload
	KeepStats st;
	std::vector<uint64_t> ids = KeepSelect(k, 102, held2, 200.0f, 100.0f, &st);
	CHECK(ids.size() == 1 && ids[0] == 1 && st.kept == 1 && st.candidates == 1 && !st.sameCamera);
	{
		float c[16], f[16];
		KeepCam(200.0f, c);
		KeepCam(100.0f, f);
		std::vector<const TestKeep::Entry*> out;
		k.select(102, held2, c, f, 0.0f, out, nullptr);
		CHECK(out.size() == 1 && out[0]->payload == 11 && out[0]->last == 100);
	}
	// the frame's camera at x + 150: 1 is 100 out of the frame's box (beyond + 64): not kept (the EXE would not draw it)
	CHECK(KeepSelect(k, 102, held2, 200.0f, 150.0f).empty());
	// trailing edge: the camera goes on moving away (frame at x + 300): not kept
	CHECK(KeepSelect(k, 102, held2, 200.0f, 300.0f).empty());
	// removed inside the drawn tap's box: tap 102 at x + 20 (1 is 30 units inside) lacks it: not kept
	CHECK(KeepSelect(k, 102, held2, 20.0f, 0.0f).empty());
	// a tap that holds it (dropped or passed): not kept
	CHECK(KeepSelect(k, 102, {1, 2}, 200.0f, 100.0f).empty());
	// still camera: frame camera = the drawn tap's camera: nothing, even for an instance that would qualify otherwise
	KeepStats st2;
	CHECK(KeepSelect(k, 102, held2, 120.0f, 120.0f, &st2).empty() && st2.sameCamera);
	// age: the sighting at 100 is kept up to tap 100 + 120 and not after; a commit beyond the age forgets it
	CHECK(KeepSelect(k, 100 + kKeepMaxAgePresentsDefault, held2, 200.0f, 100.0f).size() == 1);
	CHECK(KeepSelect(k, 101 + kKeepMaxAgePresentsDefault, held2, 200.0f, 100.0f).empty());
	// a sighting in the drawn tap itself or newer is never a candidate (the scene draws it, or a later tap holds it)
	CHECK(KeepSelect(k, 100, {}, 200.0f, 100.0f).empty());
	{
		TestKeep k2;
		k2.configure(o);
		k2.commit(100, {KeepSeen(1, ix, iy, 0.0f, 11)});
		k2.commit(100 + kKeepMaxAgePresentsDefault, {});
		CHECK(k2.size() == 1);
		k2.commit(101 + kKeepMaxAgePresentsDefault, {});
		CHECK(k2.size() == 0);
	}
	// removed in the band while the camera came nearer: last seen at camera x + 100 (1 is 50 out of that box, inside
	// + 64: the EXE drew it), gone in the tap at x + 80 (30 out, nearer): the game removed it; not kept although it lies
	// outside the drawn tap's box and inside the frame's
	{
		TestKeep k3;
		k3.configure(o);
		k3.commit(100, {KeepSeen(1, ix, iy, 100.0f, 11)});
		CHECK(KeepSelect(k3, 101, {}, 80.0f, 60.0f).empty());
		// the same vanishing while the camera stood still at x + 100: not kept either
		CHECK(KeepSelect(k3, 101, {}, 100.0f, 60.0f).empty());
		// while the camera moved away (x + 160, 110 out) and came back (frame x + 60): kept
		CHECK(KeepSelect(k3, 101, {}, 160.0f, 60.0f).size() == 1);
	}
	// the same tap committed again changes nothing; an older Present (a load, another bundle) forgets everything
	{
		TestKeep k4;
		k4.configure(o);
		k4.commit(200, {KeepSeen(1, ix, iy, 0.0f, 11)});
		k4.commit(200, {});
		CHECK(k4.size() == 1);
		k4.commit(150, {KeepSeen(3, 0.0f, 0.0f, 0.0f, 33)});
		CHECK(k4.size() == 1 && KeepSelect(k4, 151, {}, 0.0f, 10.0f).empty());
	}
	// several kept: by id ascending
	{
		TestKeep k5;
		k5.configure(o);
		k5.commit(100, {KeepSeen(9, ix, iy + 10.0f, 0.0f, 9), KeepSeen(4, ix, iy - 10.0f, 0.0f, 4), KeepSeen(7, ix, iy, 0.0f, 7)});
		const std::vector<uint64_t> v = KeepSelect(k5, 101, {}, 200.0f, 100.0f);
		CHECK(v.size() == 3 && v[0] == 4 && v[1] == 7 && v[2] == 9);
	}
	// a smaller age option; the switch off keeps nothing and forgets
	{
		TestKeep k6;
		KeepOptions o6;
		o6.maxAgePresents = 8;
		k6.configure(o6);
		k6.commit(100, {KeepSeen(1, ix, iy, 0.0f, 11)});
		CHECK(KeepSelect(k6, 108, {}, 200.0f, 100.0f).size() == 1 && KeepSelect(k6, 109, {}, 200.0f, 100.0f).empty());
		o6.on = false;
		k6.configure(o6);
		CHECK(k6.size() == 0 && KeepSelect(k6, 101, {}, 200.0f, 100.0f).empty());
		k6.commit(110, {KeepSeen(1, ix, iy, 0.0f, 11)});
		CHECK(k6.size() == 0);
	}
	// the memory bound: the oldest sightings go first
	{
		TestKeep k7;
		KeepOptions o7;
		o7.maxEntries = 2;
		k7.configure(o7);
		k7.commit(100, {KeepSeen(1, ix, iy, 0.0f, 1)});
		k7.commit(101, {KeepSeen(2, ix, iy, 0.0f, 2)});
		k7.commit(102, {KeepSeen(3, ix, iy, 0.0f, 3)});
		CHECK(k7.size() == 2 && KeepSelect(k7, 103, {}, 200.0f, 100.0f).size() == 2);
	}
	// a wrapping map: the instance one wrap period to the right re-enters through the copy of the box
	{
		const float W = 5000.0f;
		TestKeep k8;
		k8.configure(o);
		k8.commit(100, {KeepSeen(1, ix + W, iy, 0.0f, 11)});
		CHECK(KeepSelect(k8, 101, {}, 200.0f, 100.0f, nullptr, W).size() == 1);
		CHECK(KeepSelect(k8, 101, {}, 200.0f, 100.0f, nullptr, 0.0f).empty());
	}
}

// The module: the options reach the layer (on by default, the age), C5_RENDER_MODELS_KEEP=0 turns the keep off, the
// scene pass's kept instances are counted and written into the models line
static void CaseSceneKeepSwitch()
{
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", nullptr);
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	// ticket 0789: the default is the store (no age bound); ModelsOptions::store false gives 0709's keep with its age
	CHECK(l->keepCalls >= 1 && l->keep.on && l->keep.store && l->keep.maxAgePresents == 0);
	CHECK(s->counters().keepOn);
	ModelsOptions o;
	o.cities = true;
	o.installMs = 3000;
	o.noFrameMs = 300;
	o.holdMs = 600;
	o.store = false;
	s->configure(o);
	CHECK(l->keep.on && !l->keep.store && l->keep.maxAgePresents == kKeepMaxAgePresentsDefault);
	o.keepMaxAgePresents = 30;
	s->configure(o);
	CHECK(l->keep.on && l->keep.maxAgePresents == 30);
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", "0");
	s->configure(o);
	CHECK(!l->keep.on && !s->counters().keepOn);
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", "1");
	s->configure(o);
	CHECK(l->keep.on);
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", nullptr);
	o.keepCulled = false;
	s->configure(o);
	CHECK(!l->keep.on);
	o.keepCulled = true;
	s->configure(o);
	CHECK(l->keep.on);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 20);
	ModelsCounters c = s->counters();
	CHECK(c.keepFrames == 0 && c.keepInst == 0 && c.keepEntries == 7);
	FrameTextures ft;
	l->keptPerScenePass = 3;
	s->beginFrame(1021);
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	s->drawReflection(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);   // pass 1: not counted
	l->keptPerScenePass = 5;
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	c = s->counters();
	CHECK(c.keepFrames == 2 && c.keepInst == 8 && c.keepMax == 5);
	char line[2048] = {};
	s->line(line, sizeof(line));
	printf("  %s\n", strstr(line, " keep_on=") ? strstr(line, " keep_on=") : line);
	CHECK(strstr(line, " keep_on=1 keep_frames=2 keep_inst=8 keep_max=5 keep_entries=7") != nullptr);
}

// ---- ticket 0789: the model store ----
static KeepTap StoreTap(int64_t present, bool complete = true, uint32_t tapped = ~0u, uint32_t owned = ~0u)
{
	KeepTap t;
	t.present = present;
	t.complete = complete;
	t.tappedBits = tapped;
	t.ownedBits = owned;
	return t;
}
static void StoreCommit(TestKeep& k, const KeepTap& t, const std::unordered_set<uint64_t>& held, float camX,
                        const std::vector<TestKeep::Seen>& seen)
{
	float vp[16];
	KeepCam(camX, vp);
	k.commit(t, &held, vp, 0.0f, seen);
}
static std::vector<uint64_t> StoreSelect(const TestKeep& k, const KeepTap& t, const std::unordered_set<uint64_t>& held,
                                         float tapCamX, float frameCamX, KeepStats* st = nullptr)
{
	float c[16], f[16];
	KeepCam(tapCamX, c);
	KeepCam(frameCamX, f);
	std::vector<const TestKeep::Entry*> out;
	KeepStats local;
	k.select(t, held, c, f, 0.0f, out, st ? st : &local);
	std::vector<uint64_t> ids;
	for(const TestKeep::Entry* e : out)
		ids.push_back(e->id);
	return ids;
}
static TestKeep::Seen StoreSeen(uint64_t id, float x, float y, float camX, int payload, uint32_t bit = ~0u)
{
	TestKeep::Seen s = KeepSeen(id, x, y, camX, payload);
	s.bit = bit;
	return s;
}

// The store's rules (srv_models_scene.h, CulledKeep with KeepOptions::store): the options; no age bound; R3 (removal
// after refuteTaps complete taps that lack an instance inside their box and tapped its class; a sighting resets it; a cut
// tap or an untapped class is no evidence); (f) an absent instance drawn while the count is below refuteTaps, at a still
// camera too; (g) a cut tap: every instance inside the frame's box + 64; the class check; the approach rule kept; the
// memory bound's evictions; an older Present clears
static void CaseStoreRule()
{
	const KeepOptions so = StoreKeepOptions(true, true, 120, kStoreRefuteTapsDefault, 0);
	CHECK(so.on && so.store && so.maxAgePresents == 0 && so.refuteTaps == 2 && so.maxEntries == kStoreMaxEntriesDefault);
	const KeepOptions off = StoreKeepOptions(true, false, 0, 2, 0);
	CHECK(off.on && !off.store && off.maxAgePresents == kKeepMaxAgePresentsDefault && off.maxEntries == 65536);
	CHECK(StoreKeepOptions(true, true, 0, 0, 0).refuteTaps == kStoreRefuteTapsDefault);
	CHECK(!StoreKeepOptions(false, true, 0, 2, 0).on);
	float vp[16];
	KeepCam(0.0f, vp);
	const GroundBox a = CullGroundBox(vp);
	const float ix = a.x0 + 50.0f, iy = 0.0f;   // 50 units inside camera 0's box at the left
	// (1) no age bound: seen at Present 100, then 600 taps at camera x + 300 (250 units out: no evidence), re-enters
	{
		TestKeep k;
		k.configure(so);
		StoreCommit(k, StoreTap(100), {1, 2}, 0.0f, {StoreSeen(1, ix, iy, 0.0f, 11), StoreSeen(2, 0.0f, 0.0f, 0.0f, 22)});
		for(int64_t p = 101; p <= 700; ++p)
			StoreCommit(k, StoreTap(p), {2}, 300.0f, {StoreSeen(2, 0.0f, 0.0f, 300.0f, 22)});
		CHECK(k.size() == 2 && k.removed() == 0);
		KeepStats st;
		const std::vector<uint64_t> ids = StoreSelect(k, StoreTap(700), {2}, 200.0f, 100.0f, &st);
		CHECK(ids.size() == 1 && ids[0] == 1 && st.reentry == 1 && st.absent == 0 && st.cut == 0);
		// 0709's keep with the same history: older than 120 Presents, forgotten
		TestKeep k0;
		k0.configure(off);
		k0.commit(100, {StoreSeen(1, ix, iy, 0.0f, 11)});
		for(int64_t p = 101; p <= 700; ++p)
			k0.commit(p, {});
		CHECK(k0.size() == 0);
	}
	// (2) R3: lacked inside the box by complete taps: drawn after the first (absent, at a still camera), removed at the second
	{
		TestKeep k;
		k.configure(so);
		StoreCommit(k, StoreTap(100), {1}, 0.0f, {StoreSeen(1, ix, iy, 0.0f, 11)});
		StoreCommit(k, StoreTap(101), {}, 0.0f, {});
		CHECK(k.size() == 1 && k.removed() == 0);
		KeepStats st;
		std::vector<uint64_t> ids = StoreSelect(k, StoreTap(101), {}, 0.0f, 0.0f, &st);
		CHECK(ids.size() == 1 && st.absent == 1 && st.sameCamera);
		// held by the tap (dropped or passed): not drawn
		CHECK(StoreSelect(k, StoreTap(101), {1}, 0.0f, 0.0f).empty());
		StoreCommit(k, StoreTap(102), {}, 0.0f, {});
		CHECK(k.size() == 0 && k.removed() == 1 && StoreSelect(k, StoreTap(102), {}, 0.0f, 0.0f).empty());
	}
	// (3) a sighting resets the count; a passed record (held, not seen) is no absence
	{
		TestKeep k;
		k.configure(so);
		StoreCommit(k, StoreTap(100), {1}, 0.0f, {StoreSeen(1, ix, iy, 0.0f, 11)});
		StoreCommit(k, StoreTap(101), {}, 0.0f, {});
		StoreCommit(k, StoreTap(102), {1}, 0.0f, {StoreSeen(1, ix, iy, 0.0f, 11)});
		StoreCommit(k, StoreTap(103), {}, 0.0f, {});
		CHECK(k.size() == 1 && k.removed() == 0);
		StoreCommit(k, StoreTap(104), {1}, 0.0f, {});   // held passed: present, the count stays
		StoreCommit(k, StoreTap(105), {1}, 0.0f, {});
		CHECK(k.size() == 1 && k.removed() == 0);
	}
	// (4) a cut tap and a tap that did not tap the class are no evidence; a cut tap draws every instance in the frame's box
	// + 64 that it does not hold; the class check (ownedBits)
	{
		TestKeep k;
		k.configure(so);
		StoreCommit(k, StoreTap(100), {1, 9}, 0.0f,
		            {StoreSeen(1, ix, iy, 0.0f, 11, VPMODE_CITIES), StoreSeen(9, a.x1 + 500.0f, iy, 0.0f, 99, VPMODE_CITIES)});
		for(int64_t p = 101; p <= 110; ++p)
			StoreCommit(k, StoreTap(p, false), {}, 0.0f, {});
		StoreCommit(k, StoreTap(111, true, VPMODE_LANDMARKS), {}, 0.0f, {});
		StoreCommit(k, StoreTap(112, true, VPMODE_LANDMARKS), {}, 0.0f, {});
		CHECK(k.size() == 2 && k.removed() == 0);
		KeepStats st;
		std::vector<uint64_t> ids = StoreSelect(k, StoreTap(112, false), {}, 0.0f, 0.0f, &st);
		CHECK(ids.size() == 1 && ids[0] == 1 && st.cut == 1);   // 9 lies beyond the frame's box + 64
		// a class the compositor does not drop at P (the EXE draws it): nothing
		CHECK(StoreSelect(k, StoreTap(112, false, ~0u, VPMODE_LANDMARKS), {}, 0.0f, 0.0f).empty());
		// complete, inside, never lacked by evidence (the class untapped): drawn (in doubt)
		KeepStats st2;
		ids = StoreSelect(k, StoreTap(112, true, VPMODE_LANDMARKS, VPMODE_CITIES), {}, 0.0f, 0.0f, &st2);
		CHECK(ids.size() == 1 && st2.absent == 1);
	}
	// (5) refuteTaps 3: drawn for two absent taps, removed at the third
	{
		TestKeep k;
		k.configure(StoreKeepOptions(true, true, 0, 3, 0));
		StoreCommit(k, StoreTap(100), {1}, 0.0f, {StoreSeen(1, ix, iy, 0.0f, 11)});
		StoreCommit(k, StoreTap(101), {}, 0.0f, {});
		StoreCommit(k, StoreTap(102), {}, 0.0f, {});
		CHECK(StoreSelect(k, StoreTap(102), {}, 0.0f, 0.0f).size() == 1);
		StoreCommit(k, StoreTap(103), {}, 0.0f, {});
		CHECK(k.size() == 0 && k.removed() == 1);
	}
	// (6) the approach rule stays: last seen 50 out of camera x + 100's box, lacked at camera x + 80 (30 out, nearer): not
	// drawn (neither a re-entry nor absent inside the box)
	{
		TestKeep k;
		k.configure(so);
		StoreCommit(k, StoreTap(100), {1}, 100.0f, {StoreSeen(1, ix, iy, 100.0f, 11)});
		CHECK(StoreSelect(k, StoreTap(101), {}, 80.0f, 60.0f).empty());
		CHECK(StoreSelect(k, StoreTap(101), {}, 160.0f, 60.0f).size() == 1);   // moved away and back: a re-entry
	}
	// (7) the memory bound evicts the oldest and counts them; an older Present clears everything
	{
		TestKeep k;
		k.configure(StoreKeepOptions(true, true, 0, 2, 2));
		StoreCommit(k, StoreTap(100), {1}, 300.0f, {StoreSeen(1, ix, iy, 300.0f, 1)});
		StoreCommit(k, StoreTap(101), {2}, 300.0f, {StoreSeen(2, ix, iy + 1.0f, 300.0f, 2)});
		StoreCommit(k, StoreTap(102), {3}, 300.0f, {StoreSeen(3, ix, iy + 2.0f, 300.0f, 3)});
		CHECK(k.size() == 2 && k.evicted() == 1);
		// a step back of at most kStoreStepBackPresents (the pairing went back) changes nothing; a larger one clears
		StoreCommit(k, StoreTap(102 - kStoreStepBackPresents), {}, 0.0f, {StoreSeen(8, 0.0f, 0.0f, 0.0f, 8)});
		CHECK(k.size() == 2 && k.removed() == 0);
		StoreCommit(k, StoreTap(50), {}, 0.0f, {StoreSeen(7, 0.0f, 0.0f, 0.0f, 7)});
		CHECK(k.size() == 1);
	}
	// (8) off: the store's rules do not apply (0709's keep: nothing at a still camera, no removal count)
	{
		TestKeep k;
		k.configure(off);
		StoreCommit(k, StoreTap(100), {1}, 0.0f, {StoreSeen(1, ix, iy, 0.0f, 11)});
		StoreCommit(k, StoreTap(101), {}, 0.0f, {});
		StoreCommit(k, StoreTap(102), {}, 0.0f, {});
		CHECK(k.size() == 1 && k.removed() == 0 && StoreSelect(k, StoreTap(102), {}, 0.0f, 0.0f).empty() &&
		      StoreSelect(k, StoreTap(102, false), {}, 0.0f, 0.0f).empty());
	}
	printf("  entry %u bytes (int payload)\n", (unsigned)sizeof(TestKeep::Entry));
}

// The module: [Renderer] ModelStore's option reaches the layer as the store's rules (on by default: no age bound), off
// gives 0709's keep; the store's line and counters (bridged and still-bridged frames, R5, both passes)
static void CaseSceneStoreSwitch()
{
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", nullptr);
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
	CHECK(l->keep.on && l->keep.store && l->keep.maxAgePresents == 0 && l->keep.refuteTaps == kStoreRefuteTapsDefault &&
	      l->keep.maxEntries == kStoreMaxEntriesDefault);
	CHECK(s->counters().storeOn);
	ModelsOptions o;
	o.cities = true;
	o.installMs = 3000;
	o.noFrameMs = 300;
	o.holdMs = 600;
	o.store = false;
	s->configure(o);
	CHECK(l->keep.on && !l->keep.store && l->keep.maxAgePresents == kKeepMaxAgePresentsDefault && !s->counters().storeOn);
	char line[2048] = {};
	s->storeLine(line, sizeof(line));
	CHECK(strstr(line, "models store: on=0") == line);
	o.store = true;
	o.storeRefuteTaps = 3;
	o.storeMaxEntries = 5000;
	s->configure(o);
	CHECK(l->keep.store && l->keep.refuteTaps == 3 && l->keep.maxEntries == 5000);
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", "0");
	s->configure(o);
	CHECK(!l->keep.on && !s->counters().storeOn);
	SetEnvironmentVariableA("C5_RENDER_MODELS_KEEP", nullptr);
	o.storeRefuteTaps = 2;
	s->configure(o);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 20);
	// the layer reports a still-camera absent bridge and R5 records in pass 0, a re-entry and kept instances in pass 1
	l->storeStats[0].keptAbsent = 2;
	l->storeStats[0].sameCamera = true;
	l->storeStats[0].handOverRecs = 3;
	l->storeStats[0].handOverInst = 40;
	l->storeStats[0].storeRemoved = 5;
	l->storeStats[0].storeBytes = 4096;
	l->storeStats[0].storePool = 9;
	l->storeStats[0].missedChecked = 100;
	l->storeStats[0].missed = 12;
	l->storeStats[0].missedFrames = 2;
	l->storeStats[1].keptReentry = 4;
	l->storeStats[1].keptInst = 4;
	l->storeStats[1].keepEntries = 11;
	l->storeStats[1].storeEvicted = 1;
	l->storeStats[1].storeBytes = 2048;
	FrameTextures ft;
	s->beginFrame(1021);
	s->drawReflection(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	l->storeStats[0].sameCamera = false;   // moving: bridged, not still-bridged
	l->storeStats[0].handOverRecs = 0;
	l->storeStats[0].storeRemoved = 4;     // a smaller total (a pass without the store) never lowers the count
	s->drawScene(nullptr, (ID3D11RenderTargetView*)(uintptr_t)1, nullptr, 64, 64, ft);
	const ModelsCounters c = s->counters();
	CHECK(c.storeOn && c.bridgedFrames == 2 && c.stillBridged == 1 && c.storeAbsent == 4 && c.storeReentry == 4 &&
	      c.reflKept == 4 && c.reflEntries == 11 && c.handOverFrames == 1 && c.handOverRecs == 3 && c.handOverInst == 40 &&
	      c.storeRemoved == 5 && c.storeEvicted == 1 && c.storeBytes == 6144 && c.storePool == 9);
	s->storeLine(line, sizeof(line));
	printf("  %s\n", line);
	CHECK(strstr(line, "models store: on=1 entries=7 refl_entries=11 kb=6 pool=9 reentry=4 absent=4 cut=0 refl_kept=4 "
	                   "bridged=2 still_bridged=1 removed=5 evicted=1 handover frames=1 recs=3 inst=40 taps cut=0") == line);
	CHECK(strstr(line, " missed checked=100 inst=12 known=0 frames=2") != nullptr);
	// the store's line is not part of the models line (that one is near the log's body length already)
	s->line(line, sizeof(line));
	CHECK(strstr(line, "models store:") == nullptr && strstr(line, " keep_on=1") != nullptr);
}

// The module hands the layer its ownership (R5): live with a mode block, the mode writer's bits, the published
// generations' keys (at most kOwnedSetHistory, newest last); again when the bits or the sets change
static void CaseSceneStoreOwnership()
{
	ProductCompositor pc;
	VPShellMode block;
	FakeLayer* l = nullptr;
	std::unique_ptr<ModelsScene> s = NewScene(true, true, &l, &block);
	s->setExePid(pc.pid);
	s->setDatabase(L"db", 1);
	SceneLoop(*s, &pc, 30);
	CHECK(l->ownCalls >= 1 && l->own.live && l->own.bits == s->modeBits());
	CHECK(!l->own.sets.empty() && l->own.sets.back().first == pc.ctl->setGeneration && l->own.sets.back().second &&
	      l->own.sets.back().second->keys.size() == (size_t)s->counters().setKeys);
	printf("  ownership: %d calls, bits 0x%X, %u set(s), generation %u, %u keys\n", l->ownCalls, l->own.bits,
	       (unsigned)l->own.sets.size(), l->own.sets.back().first, (unsigned)l->own.sets.back().second->keys.size());
	// every published key is found by its VPModelKey, a changed key is not
	for(const auto& kv : l->own.sets.back().second->keys)
	{
		VPModelKey k = kv.second.key;
		CHECK(l->own.sets.back().second->find(k) != nullptr);
		k.t0 ^= 1;
		CHECK(l->own.sets.back().second->find(k) == nullptr);
	}
	// more drawable keys: a new generation joins the history
	const uint32_t gen = pc.ctl->setGeneration;
	l->keys.push_back(Info(5, KeyFamily::Landmark, KeySource::LandmarkRow, false));
	l->changed = true;
	Sleep(600);   // the publisher's 500 ms bound
	SceneLoop(*s, &pc, 10);
	CHECK(pc.ctl->setGeneration > gen && l->own.sets.back().first == pc.ctl->setGeneration && l->own.sets.size() >= 2);
	// the offline player's form: no mode block -> not live
	FakeLayer* l2 = nullptr;
	std::unique_ptr<ModelsScene> s2 = MakeModelsScene();
	ModelsOptions o;
	o.cities = true;
	s2->configure(o);
	l2 = new FakeLayer();
	s2->setLayer(std::unique_ptr<ModelLayer>(l2));
	s2->beginFrame(0);
	CHECK(l2->ownCalls == 1 && !l2->own.live && l2->own.bits == 0 && l2->own.sets.empty());
}

// The pairing skips a cut tap (header truncated > 0) for the newest older complete one; the cut taps are counted; with the
// store off the cut tap is drawn as before
static void CaseSceneCutTap()
{
	for(int storeOn = 1; storeOn >= 0; --storeOn)
	{
		ProductCompositor pc;
		VPShellMode block;
		FakeLayer* l = nullptr;
		std::unique_ptr<ModelsScene> s = NewScene(true, false, &l, &block);
		if(!storeOn)
		{
			ModelsOptions o;
			o.cities = true;
			o.installMs = 3000;
			o.noFrameMs = 300;
			o.holdMs = 600;
			o.store = false;
			s->configure(o);
		}
		s->setExePid(pc.pid);
		s->setDatabase(L"db", 1);
		SceneLoop(*s, &pc, 19);   // Presents up to 1019, complete
		pc.truncated = 7;
		SceneLoop(*s, &pc, 1);    // Present 1020 cut
		pc.truncated = 0;
		s->beginFrame(1021);
		ModelsCounters c = s->counters();
		printf("  store %d: drawn tap %lld, taps cut %lld, skipped %lld\n", storeOn, (long long)l->lastPresent,
		       (long long)c.tapsCut, (long long)c.pairSkippedCut);
		CHECK(c.tapsCut == 1);
		if(storeOn)
			CHECK(l->lastPresent == 1019 && c.pairSkippedCut == 1 && c.maxLagPresents == 2);
		else
			CHECK(l->lastPresent == 1020 && c.pairSkippedCut == 0);
		// two cut taps in a row: the last complete one still (beyond the history's 4 when needed)
		pc.truncated = 1;
		SceneLoop(*s, &pc, 5);   // 1021..1025 cut
		pc.truncated = 0;
		s->beginFrame(1026);
		c = s->counters();
		if(storeOn)
			CHECK(l->lastPresent == 1019 && c.pairSkippedCut == 2 && c.tapsCut == 6);
		else
			CHECK(l->lastPresent == 1025);
		// a load forgets the last complete tap
		s->onLoadBegin(srvrender::Qpc());
		pc.truncated = 1;
		SceneLoop(*s, &pc, 1);
		s->beginFrame(1027);
		if(storeOn)
			CHECK(l->lastPresent == 1026);   // no complete tap: the cut one (the layer's store bridges)
	}
}

// Ticket 0788: the records the server draws: DROPPED and HELD ones (with any other flag beside), never a plain passed one
// (not in the set, bit off, unhashed, deferred alone)
static void CaseServerDrawsHeld()
{
	CHECK(ServerDrawsRecord(VPMTAP_DRAW_DROPPED));
	CHECK(ServerDrawsRecord(VPMTAP_DRAW_DROPPED | VPMTAP_DRAW_DEFERRED));
	CHECK(ServerDrawsRecord(VPMTAP_DRAW_HELD));
	CHECK(ServerDrawsRecord(VPMTAP_DRAW_HELD | VPMTAP_DRAW_DEFERRED));
	CHECK(!ServerDrawsRecord(0));
	CHECK(!ServerDrawsRecord(VPMTAP_DRAW_DEFERRED));
	CHECK(!ServerDrawsRecord(VPMTAP_DRAW_NOT_IN_SET));
	CHECK(!ServerDrawsRecord(VPMTAP_DRAW_BIT_OFF | VPMTAP_DRAW_DEFERRED));
	CHECK(!ServerDrawsRecord(VPMTAP_DRAW_UNHASHED));
	CHECK(VPMTAP_DRAW_HELD == 0x20u);   // the .mtap form and server/evidence/0709/mtap.py name it so
	for(unsigned f = 0; f < 256; ++f)   // every flag byte: drawn exactly when one of the two bits is set
		CHECK(ServerDrawsRecord((uint8_t)f) == ((f & 0x21u) != 0));
}

static void CaseSelftestFail() { CHECK(1 == 2); }

int main(int argc, char** argv)
{
	struct C
	{
		const char* name;
		void (*fn)();
	};
	const C cases[] = {{"reader_accepts", CaseReaderAccepts},
	                   {"reader_refusals", CaseReaderRefusals},
	                   {"reader_torn", CaseReaderTorn},
	                   {"set_build", CaseSetBuild},
	                   {"set_publish", CaseSetPublish},
	                   {"set_check_negative", CaseSetCheckNegative},
	                   {"mode_sets_and_keeps_units", CaseModeSetsAndKeepsUnits},
	                   {"mode_stale_and_hold", CaseModeStaleAndHold},
	                   {"mode_load_ends_loss", CaseModeLoadEndsLoss},
	                   {"mode_stall_steps", CaseModeStallSteps},   // ticket 0641
	                   {"mode_filter_fatal", CaseModeFilterFatal},
	                   {"mode_install_bound", CaseModeInstallBound},
	                   {"scene_full_path", CaseSceneFullPath},
	                   {"scene_liveness_without_units", CaseSceneLivenessWithoutUnits},
	                   {"geom_reader", CaseGeomReader},
	                   {"scene_set_refused", CaseSceneSetRefused},
	                   {"scene_failures", CaseSceneFailures},
	                   {"scene_file_image", CaseSceneFileImage},
	                   {"scene_pairing", CaseScenePairing},
	                   {"scene_recorded_image", CaseSceneRecordedImage},
	                   {"scene_geometry_member", CaseSceneGeometryMember},
	                   {"scene_set_rule", CaseSceneSetRule},
	                   {"feed_flag", CaseFeedFlag},
	                   {"feed_model_load_begin", CaseFeedModelLoadBegin},
	                   {"scene_camera_rows", CaseSceneCameraRows},
	                   {"keep_ground_box", CaseKeepGroundBox},       // ticket 0709
	                   {"keep_rule", CaseKeepRule},                  // ticket 0709
	                   {"scene_keep_switch", CaseSceneKeepSwitch},   // ticket 0709
	                   {"store_rule", CaseStoreRule},                // ticket 0789
	                   {"scene_store_switch", CaseSceneStoreSwitch},
	                   {"scene_store_ownership", CaseSceneStoreOwnership},
	                   {"scene_cut_tap", CaseSceneCutTap},
	                   {"server_draws_held", CaseServerDrawsHeld},   // ticket 0788
	                   {"selftest_fail", CaseSelftestFail}};
	if(argc < 2)
	{
		for(const C& c : cases)
			printf("CASE %s\n", c.name);
		return 0;
	}
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	g_dir = tmp;
	if(!g_dir.empty() && g_dir.back() == L'\\')
		g_dir.pop_back();
	if(argc >= 3)
	{
		wchar_t w[MAX_PATH];
		MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, w, MAX_PATH);
		g_dir = w;
	}
	for(const C& c : cases)
		if(strcmp(c.name, argv[1]) == 0)
		{
			c.fn();
			printf("CASE %s %s\n", c.name, g_fail ? "FAIL" : "PASS");
			return g_fail ? 1 : 0;
		}
	printf("no case matches '%s'\n", argv[1]);
	return 2;
}
