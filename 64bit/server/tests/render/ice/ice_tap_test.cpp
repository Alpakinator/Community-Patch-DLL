// ice_tap_test.cpp - the test program of the ice tap's reader (epic 0169, issue 4307): server/src/render/srv_ice_tap.cpp on
// images made here (cases), and the same reader beside a producer (the stand-in's composite or the game: `read`), on a
// file (`file`) and an export of a file into the reference set's parts (`export`). Written 2026-11-15 for this
// repository (GPL-3.0-or-later). Run by server/tests/render/ice/test_ice_reader.py and shell/tests/test_ice_tap.py.
//
//   ice_tap_test.exe                         lists the cases (CASE <name>), exit 0
//   ice_tap_test.exe <case> [DIR]            runs one case; "CASE <name> PASS" and exit 0, or FAIL lines and exit 1; 2 unknown
//   ice_tap_test.exe read --pid P [--control NAME] [--seconds S] [--want-gen N] [--json F] [--save F]
//        polls the reader every 10 ms until a mesh of generation >= N (default 1) is applied or S seconds (default 20)
//        passed; exit 0 a mesh applied (state Active), 1 a refusal or Failed, 4 nothing applied, 3 the control section
//        never opened. --save writes the applied image (the file form).
//   ice_tap_test.exe file PATH [--json F]    loadFile; exit 0 accepted, 1 refused (the reason printed)
//   ice_tap_test.exe export PATH DIR         loadFile, then DIR/vb.bin, ib.bin, fields.bin, chunks.json; exit 0 / 1
#include "srv_ice_tap.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace srvrender;

namespace {

int g_fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fails++; } } while (0)
#define CHECKM(c, m) do { if (!(c)) { printf("FAIL line %d: %s (%s)\n", __LINE__, #c, std::string(m).c_str()); g_fails++; } } while (0)

void Put32(std::vector<uint8_t>& b, size_t off, uint32_t v) { memcpy(b.data() + off, &v, 4); }
void PutF(std::vector<uint8_t>& b, size_t off, float v) { memcpy(b.data() + off, &v, 4); }

// A good image of `chunks` chunks (2 by default): 40 vertices, 30 R16 indices (chunk 0: 6 triangles from 0, base 0;
// chunk 1: 4 triangles from 18, base 20), fields with the draw's floats set and the head (built 1, handles 7 / 9, count).
struct Spec
{
	uint32_t generation = 1, loadSeq = 3, pid = 0;
	std::function<void(VPShellIceImage&, std::vector<uint8_t>& fields, std::vector<VPShellIceChunk>& chunks,
	                   std::vector<uint8_t>& vb, std::vector<uint8_t>& ib)> mutate;
};

std::vector<uint8_t> MakeImage(const Spec& s)
{
	std::vector<uint8_t> fields(VPSHELL_ICE_FIELDS_BYTES), vb(320), ib(60);
	for(uint32_t i = 0; i < fields.size(); ++i)
		fields[i] = (uint8_t)((7 * i + 3) & 0xFF);
	const uint32_t offs[] = {0xFF0C, 0xFF14, 0xFF38, 0xFF3C, 0xFF40, 0xFF44, 0xFF48, 0xFF4C, 0xFFC0, 0xFFC4, 0xFFD8, 0xFFDC,
	                         0xFFE0, 0xFFE4, 0xFFE8, 0xFFEC, 0xFFF0, 0xFFF4, 0xFFF8, 0xFFFC, 0x10000, 0x10004, 0x10008,
	                         0x1000C, 0x10010, 0x10014, 0x10018, 0x1001C};
	for(uint32_t o : offs)
		PutF(fields, o - VPSHELL_ICE_FIELDS_BASE, 0.25f);
	PutF(fields, 0xFF1C - VPSHELL_ICE_FIELDS_BASE, -1793.0f);
	PutF(fields, 0xFF20 - VPSHELL_ICE_FIELDS_BASE, -1098.5f);
	PutF(fields, 0xFF24 - VPSHELL_ICE_FIELDS_BASE, 1823.9f);
	PutF(fields, 0xFF28 - VPSHELL_ICE_FIELDS_BASE, 1043.1f);
	fields[0xFEA5 - VPSHELL_ICE_FIELDS_BASE] = 1;
	Put32(fields, 0xFEE4 - VPSHELL_ICE_FIELDS_BASE, 7);
	Put32(fields, 0xFEE8 - VPSHELL_ICE_FIELDS_BASE, 9);
	Put32(fields, 0xFF30 - VPSHELL_ICE_FIELDS_BASE, 2);
	for(uint32_t j = 0; j < vb.size(); ++j)
		vb[j] = (uint8_t)((31 * j + 22) & 0xFF);
	for(uint32_t i = 0; i < 30; ++i)
	{
		uint16_t v = (uint16_t)(i < 18 ? (5 * i + 1) % 20 : (5 * i + 2) % 20);
		memcpy(ib.data() + 2 * i, &v, 2);
	}
	std::vector<VPShellIceChunk> chunks(2);
	const float b0[4] = {-1793.0f, 600.0f, 1823.9f, 1043.1f}, b1[4] = {-1793.0f, -1098.5f, 1823.9f, -700.0f};
	memcpy(chunks[0].box, b0, 16);
	chunks[0].triangles = 6; chunks[0].startIndex = 0; chunks[0].baseVertex = 0; chunks[0].recordHandle = 5;
	memcpy(chunks[1].box, b1, 16);
	chunks[1].triangles = 4; chunks[1].startIndex = 18; chunks[1].baseVertex = 20; chunks[1].recordHandle = 6;
	VPShellIceImage h = {};
	h.magic = VPSHELL_ICE_MAGIC; h.version = VPSHELL_ICE_VERSION; h.headerBytes = sizeof(h); h.chunkBytes = sizeof(VPShellIceChunk);
	h.generation = s.generation; h.loadSeq = s.loadSeq; h.exePid = s.pid ? s.pid : GetCurrentProcessId();
	h.fieldsBase = VPSHELL_ICE_FIELDS_BASE; h.fieldsBytes = VPSHELL_ICE_FIELDS_BYTES; h.vbStride = 8; h.ibFormat = 57;
	h.vbHandle = 7; h.ibHandle = 9;
	if(s.mutate)
		s.mutate(h, fields, chunks, vb, ib);
	h.chunkCount = h.chunkCount ? h.chunkCount : (uint32_t)chunks.size();
	if(!h.vbBytes) h.vbBytes = (uint32_t)vb.size();
	if(!h.ibBytes) h.ibBytes = (uint32_t)ib.size();
	if(!h.fieldsOffset) h.fieldsOffset = sizeof(h);
	if(!h.chunkOffset) h.chunkOffset = (h.fieldsOffset + VPSHELL_ICE_FIELDS_BYTES + 15) & ~15u;
	if(!h.vbOffset) h.vbOffset = (uint32_t)((h.chunkOffset + chunks.size() * sizeof(VPShellIceChunk) + 15) & ~15ull);
	if(!h.ibOffset) h.ibOffset = (h.vbOffset + (uint32_t)vb.size() + 15) & ~15u;
	const uint64_t natural = (uint64_t)h.ibOffset + ib.size();
	if(!h.imageBytes) h.imageBytes = natural;
	std::vector<uint8_t> img((size_t)std::max<uint64_t>(natural, h.imageBytes));
	memcpy(img.data(), &h, sizeof(h));
	auto put = [&](uint32_t off, const void* p, size_t n) { if(off + n <= img.size()) memcpy(img.data() + off, p, n); };
	put(h.fieldsOffset, fields.data(), fields.size());
	put(h.chunkOffset, chunks.data(), chunks.size() * sizeof(VPShellIceChunk));
	put(h.vbOffset, vb.data(), vb.size());
	put(h.ibOffset, ib.data(), ib.size());
	img.resize((size_t)h.imageBytes);
	return img;
}

// A producer made here: the control section and one image section per publish.
struct FakeProducer
{
	uint32_t pid = GetCurrentProcessId();
	std::wstring ctlName;
	HANDLE ctl = nullptr;
	VPShellIceTapControl* blk = nullptr;
	std::vector<HANDLE> images;

	explicit FakeProducer(const char* tag)
	{
		wchar_t n[96];
		_snwprintf_s(n, _countof(n), _TRUNCATE, L"Local\\VPShellIceTapTest.%u.%hs", pid, tag);
		ctlName = n;
		ctl = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_ICETAP_CONTROL_BYTES, n);
		blk = ctl ? (VPShellIceTapControl*)MapViewOfFile(ctl, FILE_MAP_WRITE, 0, 0, 0) : nullptr;
	}
	~FakeProducer()
	{
		for(HANDLE h : images) CloseHandle(h);
		if(blk) UnmapViewOfFile(blk);
		if(ctl) CloseHandle(ctl);
	}
	// state with an image (READY) or without; `name` empty: the expected name; fnvOverride != 1: that hash
	void publish(uint32_t state, const std::vector<uint8_t>* img, uint32_t generation, uint32_t loadSeq,
	             const wchar_t* name = nullptr, uint64_t fnvOverride = 1, uint32_t fail = 0, const char* failText = "",
	             bool tornSeq = false, uint64_t sectionBytes = 0)
	{
		wchar_t nm[64];
		_snwprintf_s(nm, _countof(nm), _TRUNCATE, VPSHELL_ICE_IMAGE_FMT, pid, generation);
		if(img)
		{
			const uint64_t sb = sectionBytes ? sectionBytes : img->size();
			HANDLE s = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sb, nm);
			void* v = s ? MapViewOfFile(s, FILE_MAP_WRITE, 0, 0, 0) : nullptr;
			if(v) { memcpy(v, img->data(), (size_t)std::min<uint64_t>(sb, img->size())); UnmapViewOfFile(v); }
			if(s) images.push_back(s);
		}
		InterlockedIncrement((volatile LONG*)&blk->seq);
		blk->magic = VPSHELL_ICE_MAGIC; blk->version = VPSHELL_ICE_VERSION; blk->headerBytes = sizeof(*blk);
		blk->state = state; blk->failReason = fail; blk->exePid = pid; blk->generation = img ? generation : 0;
		blk->loadSeq = loadSeq;
		VPShellIceImage h = {};
		if(img && img->size() >= sizeof(h)) memcpy(&h, img->data(), sizeof(h));
		blk->chunkCount = img ? h.chunkCount : 0;
		blk->imageBytes = img ? img->size() : 0;
		blk->fnv1a64 = img ? (fnvOverride != 1 ? fnvOverride : IceFnv1a64(img->data(), img->size())) : 0;
		wcsncpy_s(blk->imageName, img ? (name ? name : nm) : L"", _TRUNCATE);
		strncpy_s(blk->failText, failText, _TRUNCATE);
		if(!tornSeq) InterlockedIncrement((volatile LONG*)&blk->seq);
	}
};

bool SameMesh(const c5r::ice::Mesh& m, const std::vector<uint8_t>& img)
{
	VPShellIceImage h;
	memcpy(&h, img.data(), sizeof(h));
	if(m.vb.size() != h.vbBytes || memcmp(m.vb.data(), img.data() + h.vbOffset, h.vbBytes)) return false;
	if(m.ib.size() != h.ibBytes || memcmp(m.ib.data(), img.data() + h.ibOffset, h.ibBytes)) return false;
	if(!m.fields.valid || memcmp(m.fields.raw, img.data() + h.fieldsOffset, VPSHELL_ICE_FIELDS_BYTES)) return false;
	if(m.chunks.size() != h.chunkCount) return false;
	for(uint32_t c = 0; c < h.chunkCount; ++c)
	{
		VPShellIceChunk k;
		memcpy(&k, img.data() + h.chunkOffset + c * sizeof(k), sizeof(k));
		const c5r::ice::Chunk& o = m.chunks[c];
		if(memcmp(o.box, k.box, 16) || o.triangles != k.triangles || o.startIndex != k.startIndex || o.baseVertex != k.baseVertex)
			return false;
	}
	return true;
}

using Mut = std::function<void(VPShellIceImage&, std::vector<uint8_t>&, std::vector<VPShellIceChunk>&, std::vector<uint8_t>&,
                               std::vector<uint8_t>&)>;
const uint32_t F0 = VPSHELL_ICE_FIELDS_BASE;

struct Bad { const char* name; Mut mut; uint32_t reason; const char* text; };

std::vector<Bad> ImageNegatives()
{
	auto setF = [](uint32_t off, float v) { return Mut([=](VPShellIceImage&, std::vector<uint8_t>& f, std::vector<VPShellIceChunk>&,
	                                                       std::vector<uint8_t>&, std::vector<uint8_t>&) { PutF(f, off - F0, v); }); };
	return {
	    {"magic", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.magic = 0x12345678; }, IREF_MAGIC, "image magic 0x12345678"},
	    {"version", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.version = 2; }, IREF_VERSION, "image version 2"},
	    {"header", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.chunkBytes = 28; }, IREF_SIZES, "chunk 28 bytes"},
	    {"stride", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.vbStride = 12; }, IREF_SIZES, "stride 12"},
	    {"ibformat", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.ibFormat = 42; }, IREF_SIZES, "index format 42"},
	    {"nochunks", [](VPShellIceImage& h, auto&, auto& c, auto&, auto&) { c.clear(); h.chunkCount = 0; }, IREF_COUNT, "0 chunks"},
	    {"vbpartial", [](VPShellIceImage&, auto&, auto&, auto& vb, auto&) { vb.resize(324); }, IREF_COUNT, "VB 324 bytes"},
	    {"iboffset", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.ibOffset = 896; h.imageBytes = 972; }, IREF_OFFSET,
	     "the IB block at 896"},
	    {"misaligned", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.chunkOffset = 536; }, IREF_OFFSET, "the chunks block at 536"},
	    {"pastend", [](VPShellIceImage& h, auto&, auto&, auto&, auto&) { h.vbBytes = 4000; }, IREF_TRUNCATED, "the VB block"},
	    {"built", [](VPShellIceImage&, auto& f, auto&, auto&, auto&) { f[0xFEA5 - F0] = 0; }, IREF_FIELDS, "built flag 0"},
	    {"count", [](VPShellIceImage&, auto& f, auto&, auto&, auto&) { Put32(f, 0xFF30 - F0, 3); }, IREF_FIELDS, "count 3"},
	    {"handle", [](VPShellIceImage&, auto& f, auto&, auto&, auto&) { Put32(f, 0xFEE4 - F0, 8); }, IREF_FIELDS, "VB 0x8"},
	    {"nan", setF(0x1001C, NAN), IREF_FIELDS, "Ice+0x1001C is not finite"},
	    {"inf", setF(0xFF14, INFINITY), IREF_FIELDS, "Ice+0xFF14 is not finite"},
	    {"meshbox", setF(0xFF24, -5000.0f), IREF_FIELDS, "the mesh box"},
	    {"chunkbox", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[1].box[2] = -2000.0f; }, IREF_CHUNK, "chunk 1: box"},
	    {"chunknan", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[0].box[1] = NAN; }, IREF_CHUNK, "chunk 0: box"},
	    {"zerotris", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[0].triangles = 0; }, IREF_CHUNK, "chunk 0: 0 triangles"},
	    {"chunkpast", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[1].startIndex = 22; }, IREF_CHUNK,
	     "chunk 1: 4 triangles from index 22, the IB holds 30 indices"},
	    {"bigtris", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[1].triangles = 0x60000000u; }, IREF_CHUNK, "chunk 1: 1610612736 triangles"},
	    {"negbase", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[0].baseVertex = -1; }, IREF_CHUNK, "chunk 0: base vertex -1"},
	    {"basepast", [](VPShellIceImage&, auto&, auto& c, auto&, auto&) { c[1].baseVertex = 40; }, IREF_CHUNK, "chunk 1: base vertex 40"},
	    {"index", [](VPShellIceImage&, auto&, auto&, auto&, auto& ib) { uint16_t v = 20; memcpy(ib.data() + 50, &v, 2); }, IREF_INDEX,
	     "chunk 1: index 20 + base vertex 20 lies past the VB's 40 vertices"},
	};
}

// ---- the cases ---------------------------------------------------------------------------------------------------------
void CaseSectionAndFileEqual(const std::string& dir)
{
	FakeProducer p("good");
	CHECK(p.blk != nullptr);
	if(!p.blk) return;
	Spec s; s.pid = p.pid;
	const std::vector<uint8_t> img = MakeImage(s);
	IceTapReader r(p.pid, nullptr, p.ctlName);
	CHECK(!r.poll(0) && r.state() == IceTapReader::State::Waiting);   // never written: no seq yet
	p.publish(VPICE_READY, &img, 1, 3);
	CHECK(r.poll(0));
	CHECK(r.state() == IceTapReader::State::Active && r.generation() == 1 && r.loadSeq() == 3 && r.accepted == 1 && r.refused == 0);
	CHECK(!r.installFailure() && r.lastResult() == 0);
	CHECK(p.blk->ackGeneration == 1 && p.blk->ackResult == 0 && p.blk->ackServerPid == GetCurrentProcessId());
	CHECK(r.mesh() && SameMesh(*r.mesh(), img));
	CHECK(!r.poll(1) && r.poll(0));   // the same generation: nothing new for a caller that has it
	CHECK(r.accepted == 1);
	// the file form round trip
	const std::wstring path = std::wstring(dir.begin(), dir.end()) + L"\\ice_good.c5ice";
	CHECK(r.writeCurrentImage(path).ok());
	c5r::ice::Mesh m;
	std::string why;
	c5r::Status st = IceTapReader::loadFile(path, m, why);
	CHECKM(st.ok(), why);
	CHECK(SameMesh(m, img) && SameMesh(m, *r.currentImage()));
	CHECK(*r.currentImage() == img);
	// generation 2 of the same load, then 3 of a new load: applied in order
	Spec s2 = s; s2.generation = 2;
	std::vector<uint8_t> img2 = MakeImage(s2);
	p.publish(VPICE_READY, &img2, 2, 3);
	CHECK(r.poll(1) && r.generation() == 2 && r.accepted == 2);
	Spec s3 = s; s3.generation = 3; s3.loadSeq = 4;
	std::vector<uint8_t> img3 = MakeImage(s3);
	p.publish(VPICE_READY, &img3, 3, 4);
	CHECK(r.poll(2) && r.generation() == 3 && r.loadSeq() == 4);
	// the file form of a wrong length, a missing file
	{
		HANDLE f = CreateFileW((path + L".short").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		DWORD w = 0;
		WriteFile(f, img.data(), (DWORD)img.size() - 2, &w, nullptr);
		CloseHandle(f);
		c5r::Status s4 = IceTapReader::loadFile(path + L".short", m, why);
		CHECKM(s4.code == c5r::Err::Truncated && why.find("truncated: the image says") == 0, why);
		c5r::Status s5 = IceTapReader::loadFile(path + L".none", m, why);
		CHECKM(s5.code == c5r::Err::Io && why.find("cannot open") == 0, why);
	}
}

void CaseRefusals(const std::string&)
{
	FakeProducer p("refusals");
	if(!p.blk) { CHECK(p.blk); return; }
	Spec s; s.pid = p.pid;
	const std::vector<uint8_t> good = MakeImage(s);
	IceTapReader r(p.pid, nullptr, p.ctlName);
	uint32_t gen = 1;
	p.publish(VPICE_READY, &good, gen, 3);
	CHECK(r.poll(0) && r.accepted == 1);
	int64_t refused = 0;
	for(const Bad& b : ImageNegatives())
	{
		gen++;
		Spec sb = s; sb.generation = gen; sb.mutate = b.mut;
		std::vector<uint8_t> img = MakeImage(sb);
		// the image checks alone, and the file form's code
		c5r::ice::Mesh m;
		std::string why;
		const uint32_t reason = CheckIceImage(img.data(), img.size(), nullptr, m, why);
		CHECKM(reason == b.reason && why.find(b.text) != std::string::npos, std::string(b.name) + ": " + IceRefusalName(reason) + ": " + why);
		CHECK(m.vb.empty() && m.chunks.empty());   // nothing half-made
		c5r::Status st = IceTapReader::loadImage(img.data(), img.size(), m, why);
		CHECKM(!st.ok() && st.code == IceErr(b.reason), b.name);
		// through the section: refused with its ack, counted, the last good mesh stays
		p.publish(VPICE_READY, &img, gen, 3);
		CHECK(!r.poll(1));
		refused++;
		CHECKM(r.refused == refused && r.refusedBy(b.reason) >= 1, b.name);
		CHECKM(p.blk->ackGeneration == gen && p.blk->ackResult == IceAckResult(b.reason), b.name);
		CHECKM(r.lastWhy().find(b.text) != std::string::npos && r.lastWhy().find(IceRefusalName(b.reason)) != std::string::npos, r.lastWhy());
		CHECKM(r.generation() == 1 && r.mesh() && SameMesh(*r.mesh(), good) && r.state() == IceTapReader::State::Active, b.name);
		CHECK(!r.installFailure());   // a good mesh was accepted before
	}
	// block-level refusals
	struct BlockBad { const char* name; std::function<void()> pub; uint32_t reason; int32_t ack; const char* text; };
	std::vector<uint8_t> next;
	auto mk = [&](uint32_t g) { Spec x = s; x.generation = g; next = MakeImage(x); return &next; };
	std::vector<BlockBad> bb = {
	    {"hash", [&]() { gen++; p.publish(VPICE_READY, mk(gen), gen, 3, nullptr, 0x5555); }, IREF_HASH, -1, "image FNV-1a 64"},
	    {"nohash", [&]() { gen++; p.publish(VPICE_READY, mk(gen), gen, 3, nullptr, 0); }, IREF_HASH, -1, "no image hash"},
	    {"name", [&]() { gen++; p.publish(VPICE_READY, mk(gen), gen, 3, L"Local\\VPShellIce.1.1"); }, IREF_BLOCK, (int32_t)c5r::Err::BadLayout, "image 'Local\\VPShellIce.1.1'"},
	    {"open", [&]() { gen++; wchar_t nm[64]; _snwprintf_s(nm, _countof(nm), _TRUNCATE, VPSHELL_ICE_IMAGE_FMT, p.pid, gen);
	                     p.publish(VPICE_READY, nullptr, gen, 3); p.blk->generation = gen; p.blk->imageBytes = 1000;
	                     p.blk->fnv1a64 = 1; wcsncpy_s(p.blk->imageName, nm, _TRUNCATE); }, IREF_OPEN, -2, "cannot open the image section"},
	    {"disagree", [&]() { gen++; Spec x = s; x.generation = gen; x.loadSeq = 9; next = MakeImage(x); p.publish(VPICE_READY, &next, gen, 3); },
	     IREF_DISAGREE, (int32_t)c5r::Err::BadLayout, "image generation"},
	    {"stale", [&]() { p.publish(VPICE_READY, mk(1), 1, 3); p.blk->generation = 1; }, IREF_STALE, -3, "generation 1, the applied mesh is generation 1"},
	    {"blockmagic", [&]() { gen++; p.publish(VPICE_READY, mk(gen), gen, 3); InterlockedIncrement((volatile LONG*)&p.blk->seq);
	                           p.blk->version = 7; InterlockedIncrement((volatile LONG*)&p.blk->seq); }, IREF_BLOCK_MAGIC, -4, "version 7"},
	};
	for(const BlockBad& b : bb)
	{
		const int64_t before = r.refused;
		b.pub();
		CHECK(!r.poll(1));
		CHECKM(r.refused == before + 1 && r.refusedBy(b.reason) >= 1, b.name);
		CHECKM(p.blk->ackResult == b.ack, std::string(b.name) + " ack " + std::to_string(p.blk->ackResult));
		CHECKM(r.lastWhy().find(b.text) != std::string::npos, r.lastWhy());
		CHECKM(r.generation() == 1 && r.mesh() && SameMesh(*r.mesh(), good), b.name);
	}
	// a view shorter than the image the block announces
	{
		gen++;
		Spec x = s; x.generation = gen;
		std::vector<uint8_t> big = MakeImage(x);
		const int64_t before = r.refused;
		p.publish(VPICE_READY, &big, gen, 3, nullptr, 1, 0, "", false, 4096);
		p.blk->imageBytes = 200000;   // past the 64 KiB view
		CHECK(!r.poll(1) && r.refused == before + 1 && r.refusedBy(IREF_TRUNCATED) >= 1);
		CHECKM(r.lastWhy().find("the section's view") != std::string::npos, r.lastWhy());
	}
}

void CaseTorn(const std::string&)
{
	FakeProducer p("torn");
	if(!p.blk) { CHECK(p.blk); return; }
	Spec s; s.pid = p.pid;
	std::vector<uint8_t> img = MakeImage(s);
	IceTapReader r(p.pid, nullptr, p.ctlName);
	p.publish(VPICE_READY, &img, 1, 3, nullptr, 1, 0, "", true);   // seq left odd: a writer in progress
	CHECK(!r.poll(0) && r.torn == 1 && r.accepted == 0 && r.refused == 0 && p.blk->ackGeneration == 0);
	CHECK(r.state() == IceTapReader::State::Waiting && !r.mesh());
	InterlockedIncrement((volatile LONG*)&p.blk->seq);   // the writer finishes
	CHECK(r.poll(0) && r.accepted == 1 && p.blk->ackGeneration == 1);
}

void CaseFirstRefused(const std::string&)
{
	FakeProducer p("first");
	if(!p.blk) { CHECK(p.blk); return; }
	Spec s; s.pid = p.pid;
	s.mutate = [](VPShellIceImage&, std::vector<uint8_t>&, std::vector<VPShellIceChunk>& c, std::vector<uint8_t>&, std::vector<uint8_t>&) {
		c[0].triangles = 0; };
	std::vector<uint8_t> bad = MakeImage(s);
	IceTapReader r(p.pid, nullptr, p.ctlName);
	p.publish(VPICE_READY, &bad, 1, 3);
	CHECK(!r.poll(0) && r.refused == 1);
	CHECK(r.installFailure() && r.state() == IceTapReader::State::Failed && !r.mesh());
	CHECKM(r.lastWhy().find("chunk 0: 0 triangles") != std::string::npos, r.lastWhy());
	Spec g; g.pid = p.pid; g.generation = 2;
	std::vector<uint8_t> good = MakeImage(g);
	p.publish(VPICE_READY, &good, 2, 3);
	CHECK(r.poll(0) && !r.installFailure() && r.state() == IceTapReader::State::Active);
}

void CaseProducerStates(const std::string&)
{
	FakeProducer p("states");
	if(!p.blk) { CHECK(p.blk); return; }
	{
		IceTapReader none(p.pid, nullptr, L"Local\\VPShellIceTapTest.no.such.section");
		CHECK(!none.poll(0) && none.state() == IceTapReader::State::Absent && !none.installFailure());
	}
	IceTapReader r(p.pid, nullptr, p.ctlName);
	p.publish(VPICE_OFF, nullptr, 0, 0);
	CHECK(!r.poll(0) && r.state() == IceTapReader::State::Off && !r.installFailure());
	p.publish(VPICE_WRITING, nullptr, 0, 3);
	CHECK(!r.poll(0) && r.state() == IceTapReader::State::Waiting);
	Spec s; s.pid = p.pid;
	std::vector<uint8_t> img = MakeImage(s);
	p.publish(VPICE_READY, &img, 1, 3);
	CHECK(r.poll(0) && r.state() == IceTapReader::State::Active);
	// a new load is being copied: the last good mesh stays meanwhile
	p.publish(VPICE_WRITING, nullptr, 0, 4);
	CHECK(r.poll(0) && r.mesh() && r.state() == IceTapReader::State::Active);
	// a load without ice: no mesh, Waiting, noMesh
	p.publish(VPICE_NO_MESH, nullptr, 0, 4);
	CHECK(!r.poll(0) && !r.mesh() && r.noMesh() && r.state() == IceTapReader::State::Waiting && !r.installFailure());
	// CHANGED is transient (copied again): not an install failure
	p.publish(VPICE_FAILED, nullptr, 0, 4, nullptr, 1, VPICEF_CHANGED, "the Ice object changed during the copy");
	CHECK(!r.installFailure() && r.state() != IceTapReader::State::Failed);
	r.poll(0);
	CHECK(!r.installFailure() && r.state() == IceTapReader::State::Waiting);
	// a pattern mismatch: D125's route with the producer's text
	p.publish(VPICE_FAILED, nullptr, 0, 4, nullptr, 1, VPICEF_PATTERN, "pattern 3 'ice builder' at VA 0x495BDD does not match");
	r.poll(0);
	CHECK(r.installFailure() && r.state() == IceTapReader::State::Failed && !r.mesh());
	CHECKM(r.lastWhy() == "the ice tap failed: pattern 3 'ice builder' at VA 0x495BDD does not match", r.lastWhy());
	p.publish(VPICE_FAILED, nullptr, 0, 4, nullptr, 1, VPICEF_DESC, "VB handle 0x7: vertex format not 0x16");
	r.poll(0);
	CHECKM(r.installFailure() && r.lastWhy().find("vertex format not 0x16") != std::string::npos, r.lastWhy());
	CHECK(r.producerState() == VPICE_FAILED);
}

void CaseFuzz(const std::string&)
{
	// every single-byte change of a good image's header, fields head and chunk records, and every truncation: no crash,
	// either refused or accepted with a mesh that satisfies the module's own checks
	Spec s;
	const std::vector<uint8_t> good = MakeImage(s);
	uint32_t refusedN = 0, acceptedN = 0;
	for(size_t i = 0; i < 112 + 416 + 64; ++i)
		for(uint8_t x : {0x01, 0x80, 0xFF})
		{
			std::vector<uint8_t> img = good;
			img[i] ^= x;
			c5r::ice::Mesh m;
			std::string why;
			if(CheckIceImage(img.data(), img.size(), nullptr, m, why)) { refusedN++; continue; }
			acceptedN++;
			CHECK(!m.chunks.empty() && m.vb.size() % 8 == 0 && m.ib.size() % 2 == 0 && m.fields.valid);
		}
	for(size_t n = 0; n < good.size(); n += 7)
	{
		c5r::ice::Mesh m;
		std::string why;
		CHECK(CheckIceImage(good.data(), n, nullptr, m, why) != IREF_NONE);
	}
	CHECK(refusedN > 0 && acceptedN > 0);
	printf("fuzz: %u refused, %u accepted (bytes outside the checked fields)\n", refusedN, acceptedN);
}

void CaseSelftestFail(const std::string&)
{
	CHECK(1 == 2);
}

struct Case { const char* name; void (*fn)(const std::string&); };
const Case kCases[] = {
    {"section_and_file_equal", CaseSectionAndFileEqual}, {"refusals", CaseRefusals}, {"torn", CaseTorn},
    {"first_refused", CaseFirstRefused}, {"producer_states", CaseProducerStates}, {"fuzz", CaseFuzz},
    {"selftest_fail", CaseSelftestFail},
};

// ---- the tools ---------------------------------------------------------------------------------------------------------
std::string Hex64(uint64_t v) { char b[24]; sprintf_s(b, "%016llX", (unsigned long long)v); return b; }

void WriteText(const std::string& path, const std::string& text)
{
	FILE* f = nullptr;
	if(fopen_s(&f, path.c_str(), "wb") == 0 && f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
}

std::string MeshJson(const c5r::ice::Mesh& m)
{
	std::string j = "{\"vbBytes\": " + std::to_string(m.vb.size()) + ", \"ibBytes\": " + std::to_string(m.ib.size()) +
	                ", \"vbFnv\": \"" + Hex64(IceFnv1a64(m.vb.data(), m.vb.size())) + "\", \"ibFnv\": \"" +
	                Hex64(IceFnv1a64(m.ib.data(), m.ib.size())) + "\", \"fieldsFnv\": \"" +
	                Hex64(IceFnv1a64(m.fields.raw, sizeof(m.fields.raw))) + "\", \"chunks\": [";
	uint64_t tris = 0;
	for(size_t c = 0; c < m.chunks.size(); ++c)
	{
		const c5r::ice::Chunk& k = m.chunks[c];
		char b[256];
		sprintf_s(b, "%s{\"box\": [%.9g, %.9g, %.9g, %.9g], \"triangles\": %u, \"startIndex\": %u, \"baseVertex\": %d}", c ? ", " : "",
		          k.box[0], k.box[1], k.box[2], k.box[3], k.triangles, k.startIndex, k.baseVertex);
		j += b;
		tris += k.triangles;
	}
	return j + "], \"triangles\": " + std::to_string(tris) + "}";
}

int ToolRead(int argc, char** argv)
{
	uint32_t pid = 0, wantGen = 1;
	double seconds = 20;
	std::wstring control;
	std::string json, save;
	for(int i = 2; i < argc; ++i)
	{
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if(a == "--pid") pid = (uint32_t)strtoul(next().c_str(), nullptr, 10);
		else if(a == "--seconds") seconds = atof(next().c_str());
		else if(a == "--want-gen") wantGen = (uint32_t)strtoul(next().c_str(), nullptr, 10);
		else if(a == "--json") json = next();
		else if(a == "--save") save = next();
		else if(a == "--control") { std::string c = next(); control.assign(c.begin(), c.end()); }
		else { printf("unknown argument %s\n", a.c_str()); return 2; }
	}
	if(!pid) { printf("--pid required\n"); return 2; }
	std::unique_ptr<IceTapReader> own(control.empty() ? new IceTapReader(pid, nullptr) : new IceTapReader(pid, nullptr, control));
	IceTapReader* rd = own.get();
	const int64_t end = Qpc() + (int64_t)(seconds * QpcFreq());
	bool opened = false;
	while(Qpc() < end)
	{
		rd->poll(0);
		opened = opened || rd->state() != IceTapReader::State::Absent;
		if(rd->mesh() && rd->generation() >= wantGen) break;
		if(rd->installFailure() && rd->producerState() == VPICE_FAILED) break;
		Sleep(10);
	}
	const char* st[] = {"Absent", "Off", "Waiting", "Active", "Failed"};
	const auto m = rd->mesh();
	printf("ice reader: state %s, generation %u, load %u, accepted %lld refused %lld torn %llu, last result %d, producer state %u%s%s\n",
	       st[(int)rd->state()], rd->generation(), rd->loadSeq(), (long long)rd->accepted, (long long)rd->refused,
	       (unsigned long long)rd->torn, rd->lastResult(), rd->producerState(), rd->lastWhy().empty() ? "" : "; why: ",
	       rd->lastWhy().c_str());
	if(m) printf("ice reader: mesh %s\n", MeshJson(*m).c_str());
	if(!json.empty())
	{
		std::string j = "{\"state\": \"" + std::string(st[(int)rd->state()]) + "\", \"generation\": " + std::to_string(rd->generation()) +
		                ", \"loadSeq\": " + std::to_string(rd->loadSeq()) + ", \"accepted\": " + std::to_string(rd->accepted) +
		                ", \"refused\": " + std::to_string(rd->refused) + ", \"lastResult\": " + std::to_string(rd->lastResult()) +
		                ", \"installFailure\": " + (rd->installFailure() ? "true" : "false") + ", \"noMesh\": " +
		                (rd->noMesh() ? "true" : "false") + ", \"producerState\": " + std::to_string(rd->producerState()) +
		                ", \"applyMs\": " + std::to_string(rd->lastApplyMs) + ", \"mesh\": " + (m ? MeshJson(*m) : "null") + "}";
		WriteText(json, j);
	}
	if(!save.empty() && m)
	{
		std::wstring w(save.begin(), save.end());
		c5r::Status s = rd->writeCurrentImage(w);
		if(!s.ok()) { printf("ice reader: --save failed: %s\n", s.msg.c_str()); return 1; }
	}
	if(!opened) return 3;
	if(m && rd->state() == IceTapReader::State::Active) return 0;
	if(rd->refused || rd->installFailure()) return 1;
	return 4;
}

int ToolFile(int argc, char** argv, bool exportParts)
{
	if(argc < 3) { printf("PATH required\n"); return 2; }
	std::string path = argv[2];
	c5r::ice::Mesh m;
	std::string why;
	c5r::Status st = IceTapReader::loadFile(std::wstring(path.begin(), path.end()), m, why);
	if(!st.ok()) { printf("ice file: refused (%s): %s\n", c5r::errName(st.code), why.c_str()); return 1; }
	printf("ice file: accepted: %s\n", MeshJson(m).c_str());
	if(exportParts)
	{
		if(argc < 4) { printf("DIR required\n"); return 2; }
		const std::string d = argv[3];
		WriteText(d + "/vb.bin", std::string(m.vb.begin(), m.vb.end()));
		WriteText(d + "/ib.bin", std::string(m.ib.begin(), m.ib.end()));
		WriteText(d + "/fields.bin", std::string((const char*)m.fields.raw, sizeof(m.fields.raw)));
		WriteText(d + "/chunks.json", MeshJson(m));
	}
	else if(argc >= 5 && std::string(argv[3]) == "--json")
		WriteText(argv[4], MeshJson(m));
	return 0;
}

}  // namespace

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(argc < 2)
	{
		for(const Case& c : kCases) printf("CASE %s\n", c.name);
		return 0;
	}
	const std::string what = argv[1];
	if(what == "read") return ToolRead(argc, argv);
	if(what == "file") return ToolFile(argc, argv, false);
	if(what == "export") return ToolFile(argc, argv, true);
	for(const Case& c : kCases)
		if(what == c.name)
		{
			char tmp[MAX_PATH];
			GetTempPathA(MAX_PATH, tmp);
			c.fn(argc > 2 ? argv[2] : tmp);
			printf("CASE %s %s\n", c.name, g_fails ? "FAIL" : "PASS");
			return g_fails ? 1 : 0;
		}
	printf("no case matches '%s'\n", what.c_str());
	return 2;
}
